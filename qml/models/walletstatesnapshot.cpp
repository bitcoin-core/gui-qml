// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletstatesnapshot.h>

#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/walletunlock.h>
#include <qml/util.h>
#include <qml/core_settings.h>

#include <interfaces/node.h>
#include <key_io.h>
#include <node/context.h>
#include <policy/policy.h>
#include <script/solver.h>
#include <univalue.h>
#include <util/string.h>
#include <wallet/coincontrol.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/types.h>
#include <wallet/wallet.h>

#include <QCoreApplication>
#include <QSettings>

#include <algorithm>
#include <limits>

namespace {
bool WalletUsesMultiKeyDescriptor(const wallet::CWallet& wallet)
{
    for (const auto* spk_man : wallet.GetActiveScriptPubKeyMans()) {
        const auto* descriptor_spk_man = dynamic_cast<const wallet::DescriptorScriptPubKeyMan*>(spk_man);
        if (!descriptor_spk_man) {
            continue;
        }

        std::string descriptor;
        if (descriptor_spk_man->GetDescriptorString(descriptor, /*priv=*/false) &&
            descriptor.find("multi(") != std::string::npos) {
            return true;
        }
    }

    return false;
}

QString Tr(const char* message) { return QCoreApplication::translate("WalletQmlModel", message); }

QString Address(const CTxDestination& destination) { return QString::fromStdString(EncodeDestination(destination)); }

std::optional<OutputType> AddressType(const CTxDestination& destination)
{
    if (std::holds_alternative<PKHash>(destination)) return OutputType::LEGACY;
    if (std::holds_alternative<ScriptHash>(destination)) return OutputType::P2SH_SEGWIT;
    if (std::holds_alternative<WitnessV0KeyHash>(destination) || std::holds_alternative<WitnessV0ScriptHash>(destination)) return OutputType::BECH32;
    if (std::holds_alternative<WitnessV1Taproot>(destination)) return OutputType::BECH32M;
    return std::nullopt;
}

bool SupportsType(const WalletStateSnapshot& state, OutputType type)
{
    return state.can_get_addresses && type != OutputType::UNKNOWN && (type != OutputType::BECH32M || state.taproot_enabled);
}
} // namespace

WalletKeyScheme ReadWalletKeyScheme(interfaces::Wallet& wallet)
{
    const wallet::CWallet* raw_wallet = wallet.wallet();
    if (raw_wallet) {
        LOCK(raw_wallet->cs_wallet);
        if (raw_wallet->IsWalletFlagSet(wallet::WALLET_FLAG_EXTERNAL_SIGNER)) {
            return WalletKeyScheme::ExternalSigner;
        }
        if (raw_wallet->IsWalletFlagSet(wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            return WalletKeyScheme::WatchOnly;
        }
        if (WalletUsesMultiKeyDescriptor(*raw_wallet)) {
            return WalletKeyScheme::MultiKey;
        }
        return WalletKeyScheme::SingleKey;
    }
    if (wallet.privateKeysDisabled()) {
        return WalletKeyScheme::WatchOnly;
    }
    return WalletKeyScheme::SingleKey;
}

WalletStateBackend::WalletStateBackend(std::shared_ptr<interfaces::Wallet> wallet) : m_wallet(std::move(wallet)) {}

CoinLockResult WalletStateBackend::setCoinsLocked(interfaces::Node* node, const std::vector<COutPoint>& outputs, bool locked)
{
    CoinLockResult result;
    for (const auto& output : outputs) {
        // RPC changes do not notify the GUI. Apply the requested state even if
        // the last published snapshot already matches it.
        const bool success = locked ? m_wallet->lockCoin(output, true) : m_wallet->unlockCoin(output);
        result.success = success && result.success;
    }
    // A failed database write can still change the in-memory lock state.
    result.snapshot = read(node);
    return result;
}

QString WalletStateBackend::settingsKey() const
{
    return QStringLiteral("receiveAddressTypes/%1/address").arg(QString::fromStdString(m_wallet->getWalletName()));
}

WalletStateSnapshot WalletStateBackend::read(interfaces::Node* node)
{
    // Capture the wallet's processed tip before reading. If it advances during
    // the scan, the next poll will reconcile again. Failed reads remain dirty.
    m_last_processed_block.reset();
    interfaces::WalletBalances balances;
    uint256 processed_block;
    const bool have_processed_block = m_wallet->tryGetBalances(balances, processed_block);
    WalletStateSnapshot state;
    state.name = QString::fromStdString(m_wallet->getWalletName());
    state.balance = m_wallet->getBalance();
    state.available_balance = m_wallet->getAvailableBalance(wallet::CCoinControl{});
    state.key_scheme = ReadWalletKeyScheme(*m_wallet);
    state.encrypted = m_wallet->isCrypted();
    state.locked = m_wallet->isLocked();
    state.private_keys_disabled = m_wallet->privateKeysDisabled();
    state.external_signer = m_wallet->hasExternalSigner();
    state.can_get_addresses = m_wallet->canGetAddresses();
    state.taproot_enabled = m_wallet->taprootEnabled();
    state.default_address_type = m_wallet->getDefaultAddressType();
    state.dust_relay_fee = node ? node->getDustRelayFee() : CFeeRate{DUST_RELAY_TX_FEE};
    state.receive_address_type = state.taproot_enabled ? QStringLiteral("bech32m") : QStringLiteral("bech32");
    if (node) {
        const auto preferences = node->getPersistentSetting("qml_receive_address_types");
        const auto& saved = preferences[state.name.toStdString()];
        if (saved.isStr()) {
            const auto type = ParseOutputType(saved.get_str());
            if (type && SupportsType(state, *type)) state.receive_address_type = QString::fromStdString(saved.get_str());
        }
    }
    state.addresses = m_wallet->getAddresses();
    std::set<QString> receive_addresses;
    for (const auto& address : state.addresses) {
        const auto text = Address(address.dest);
        state.labels[text] = QString::fromStdString(address.name);
        if (address.is_mine && address.purpose == wallet::AddressPurpose::RECEIVE) receive_addresses.insert(text);
    }
    state.coins = m_wallet->listCoins();
    std::vector<COutPoint> locked;
    m_wallet->listLockedCoins(locked);
    state.locked_coins.insert(locked.begin(), locked.end());
    state.receive_requests = ReceiveRequestHistoryModel::DeserializeEntries(m_wallet->getAddressReceiveRequests());
    std::set<QString> request_addresses;
    for (const auto& request : state.receive_requests) {
        const auto address = QString::fromStdString(request.recipient.address);
        request_addresses.insert(address);
        if (request.payment_received) m_observed_addresses.insert(address);
    }
    std::set<COutPoint> change_outputs;
    for (const auto& tx : m_wallet->getWalletTxs()) {
        if (!tx.tx) continue;
        std::map<QString, CAmount> received;
        for (size_t i = 0; i < tx.tx->vout.size(); ++i) {
            if (i < tx.txout_is_change.size() && tx.txout_is_change[i]) change_outputs.emplace(tx.tx->GetHash(), i);
            if (i >= tx.txout_is_mine.size() || !tx.txout_is_mine[i]) continue;
            CTxDestination destination;
            const auto& output = tx.tx->vout[i];
            if (!ExtractDestination(output.scriptPubKey, destination)) continue;
            const auto address = Address(destination);
            if (receive_addresses.contains(address) && (i >= tx.txout_is_change.size() || !tx.txout_is_change[i])) state.used_addresses.insert(address);
            if (output.nValue <= 0) continue;
            m_observed_addresses.insert(address);
            if (request_addresses.contains(address)) received[address] += output.nValue;
        }
        if (received.empty()) continue;
        interfaces::WalletTxStatus status{};
        interfaces::WalletOrderForm order_form;
        bool in_mempool{false};
        int blocks{0};
        m_wallet->getWalletTxDetails(tx.tx->GetHash(), status, order_form, in_mempool, blocks);
        const auto replacement = tx.value_map.find("replaced_by_txid");
        const bool replaced = replacement != tx.value_map.end() && !replacement->second.empty();
        if (status.depth_in_main_chain < 0 || status.is_abandoned || (status.depth_in_main_chain == 0 && (!in_mempool || replaced))) continue;
        state.unconfirmed_payments |= status.depth_in_main_chain == 0;
        for (const auto& [address, amount] : received) state.received_amounts[address] += amount;
    }
    for (const auto& [ancestor, coins] : state.coins) {
        for (const auto& [outpoint, output] : coins) {
            CTxDestination destination;
            if (!ExtractDestination(output.txout.scriptPubKey, destination)) continue;
            const auto address = Address(destination);
            state.address_balances[address] += output.txout.nValue;
            if (output.txout.nValue > 0 && change_outputs.contains(outpoint)) state.change_addresses.insert(address);
        }
    }
    state.observed_addresses = m_observed_addresses;
    for (auto& request : state.receive_requests) {
        if (request.payment_received || !m_observed_addresses.contains(QString::fromStdString(request.recipient.address))) continue;
        request.payment_received = true;
        if (!m_wallet->setAddressReceiveRequest(DecodeDestination(request.recipient.address), util::ToString(request.id), ReceiveRequestHistoryModel::SerializeEntry(request))) {
            state.persistence_error = Tr("The received-payment status could not be saved. The address will not be reused.");
        }
    }
    if (have_processed_block) m_last_processed_block = processed_block;
    m_unconfirmed_payments = state.unconfirmed_payments;
    return state;
}

std::optional<WalletStateSnapshot> WalletStateBackend::poll(interfaces::Node* node)
{
    // A node-tip notification can precede wallet processing, and maturity does
    // not emit a transaction notification. Retry busy wallets on the next poll.
    interfaces::WalletBalances balances;
    uint256 processed_block;
    if (!m_unconfirmed_payments) {
        if (!m_wallet->tryGetBalances(balances, processed_block)) return std::nullopt;
        if (m_last_processed_block == processed_block) return std::nullopt;
    }
    return read(node);
}

bool WalletStateBackend::setAddressType(interfaces::Node* node, const QString& type)
{
    if (!node) return true;
    const auto updated_preferences = [&] {
        auto preferences = node->getPersistentSetting("qml_receive_address_types");
        if (!preferences.isObject()) preferences = common::SettingsValue{UniValue::VOBJ};
        preferences.pushKV(m_wallet->getWalletName(), type.toStdString());
        return preferences;
    };
    if (auto* context = node->context(); context && context->args) {
        return QmlCoreSettings::PersistSettings(*context->args, {QStringLiteral("qml_receive_address_types")}, [&] {
            // Read and merge inside the shared writer lock: other wallet
            // queues may update their own entry in the same settings object.
            QmlCoreSettings::SetRwSetting(*context->args, QStringLiteral("qml_receive_address_types"), updated_preferences());
            return true;
        });
    }
    node->updateRwSetting("qml_receive_address_types", updated_preferences());
    return true;
}

ReceiveCommandResult WalletStateBackend::execute(interfaces::Node* node, ReceiveCommand command)
{
    ReceiveCommandResult result;
    auto finish = [&] {
        try {
            result.snapshot = read(node);
        } catch (...) {
            // Persistence may already have succeeded. Keep its result and ID
            // so retrying the refresh cannot create another request.
            result.refresh_error = Tr("Wallet information could not be refreshed. It will be retried.");
        }
        return std::move(result);
    };
    auto state = read(node);
    bool relock{false};
    if (command.passphrase) {
        const auto unlocked = TryUnlockWithPassphrase(*m_wallet, *command.passphrase);
        command.passphrase.reset();
        if (unlocked == WalletUnlockResult::IncorrectPassphrase) {
            result.error = Tr("The wallet password you entered was incorrect.");
            return finish();
        }
        relock = unlocked == WalletUnlockResult::UnlockedNowRelockRequired;
    }
    WalletRelockGuard guard{*m_wallet, [] {}, relock};
    // Keep validation and persistence together, but release cs_wallet before
    // relocking (which acquires the wallet's relock mutex first).
    std::unique_ptr<UniqueLock<RecursiveMutex>> wallet_lock;
    if (command.kind == ReceiveCommand::Kind::Save || command.kind == ReceiveCommand::Kind::EnsureAddress) {
        if (auto* wallet = m_wallet->wallet()) {
            wallet_lock = std::make_unique<UniqueLock<RecursiveMutex>>(wallet->cs_wallet, "wallet->cs_wallet", __FILE__, __LINE__);
            state = read(node);
        }
    }
    auto complete = [&] {
        wallet_lock.reset();
        guard.relock();
        return finish();
    };
    QSettings settings;
    const auto address_key = settingsKey();
    const auto type_text = command.address_type.isEmpty() ? state.receive_address_type : command.address_type;
    const auto type = ParseOutputType(type_text.toStdString());
    auto new_address = [&](const std::string& label) -> QString {
        if (!type || !SupportsType(state, *type)) {
            result.error = Tr("The selected address type is unavailable.");
            return {};
        }
        const auto destination = m_wallet->getNewDestination(*type, label);
        if (!destination || !IsValidDestination(*destination)) {
            result.needs_unlock = m_wallet->isCrypted() && m_wallet->isLocked();
            result.error = result.needs_unlock ? Tr("Enter your wallet password to create an address.") : Tr("A receiving address could not be generated. Please try again.");
            return {};
        }
        return Address(*destination);
    };
    const auto saved_address = [&](const QString& address) {
        return std::any_of(state.receive_requests.begin(), state.receive_requests.end(), [&](const auto& entry) {
            return entry.recipient.address == address.toStdString();
        });
    };
    if (command.kind == ReceiveCommand::Kind::EnsureAddress) {
        auto address = command.address.isEmpty() ? settings.value(address_key).toString() : command.address;
        const auto destination = DecodeDestination(address.toStdString());
        wallet::AddressPurpose purpose{};
        const auto old_type = AddressType(destination);
        if (command.next || !IsValidDestination(destination) || !m_wallet->getAddress(destination, nullptr, &purpose) || purpose != wallet::AddressPurpose::RECEIVE || m_observed_addresses.contains(address) || saved_address(address) || (!command.address_type.isEmpty() && (!old_type || !type || *old_type != *type))) {
            address = new_address({});
        }
        if (address.isEmpty()) return complete();
        settings.setValue(address_key, address);
        settings.sync();
        if (settings.status() != QSettings::NoError || !setAddressType(node, type_text)) result.error = Tr("The receiving address preference could not be saved.");
        else { result.receiving_address = address; result.success = true; }
    } else if (command.kind == ReceiveCommand::Kind::Save) {
        auto entry = command.entry;
        if (!MoneyRange(entry.recipient.amount)) { result.error = Tr("The request amount is invalid."); return complete(); }
        const auto existing = std::find_if(state.receive_requests.begin(), state.receive_requests.end(), [&](const auto& item) { return item.id == entry.id; });
        if (entry.id != 0) {
            if (existing == state.receive_requests.end() || existing->recipient.address != entry.recipient.address) { result.error = Tr("The payment request is no longer available."); return complete(); }
            entry.payment_received = existing->payment_received;
            entry.date = existing->date;
            if (entry.payment_received && (entry.recipient.amount != existing->recipient.amount || entry.recipient.label != existing->recipient.label || entry.recipient.message != existing->recipient.message)) { result.error = Tr("A paid payment request cannot be changed."); return complete(); }
        } else {
            if (command.reserve_receiving_address && (entry.recipient.address.empty() || m_observed_addresses.contains(QString::fromStdString(entry.recipient.address)))) { result.error = Tr("This address has received a payment. Create a new receiving address."); return complete(); }
            if (command.reserve_receiving_address && saved_address(QString::fromStdString(entry.recipient.address))) { result.error = Tr("This address already belongs to a saved request. Create a new receiving address."); return complete(); }
            int64_t maximum{0};
            for (const auto& item : state.receive_requests) maximum = std::max(maximum, item.id);
            if (maximum >= std::numeric_limits<unsigned int>::max()) { result.error = Tr("No more payment requests can be saved."); return complete(); }
            entry.id = maximum + 1;
            entry.date = QDateTime::fromSecsSinceEpoch(QDateTime::currentSecsSinceEpoch());
            if (entry.recipient.address.empty()) entry.recipient.address = new_address(entry.recipient.noteSelf).toStdString();
            if (entry.recipient.address.empty()) return complete();
        }
        if (!m_wallet->setAddressReceiveRequest(DecodeDestination(entry.recipient.address), util::ToString(entry.id), ReceiveRequestHistoryModel::SerializeEntry(entry))) { result.error = Tr("The payment request could not be saved."); return complete(); }
        result.saved_entry = entry;
        result.success = true;
        const auto address = QString::fromStdString(entry.recipient.address);
        std::string old_label_text;
        m_wallet->getAddress(DecodeDestination(entry.recipient.address), &old_label_text, nullptr);
        const auto old_label = QString::fromStdString(old_label_text);
        const auto note = QString::fromStdString(entry.recipient.noteSelf);
        const bool note_changed = existing == state.receive_requests.end() || existing->recipient.noteSelf != entry.recipient.noteSelf;
        if (note_changed && old_label != note && (!note.isEmpty() || (existing != state.receive_requests.end() && old_label.toStdString() == existing->recipient.noteSelf))) {
            if (!m_wallet->setAddressBook(DecodeDestination(entry.recipient.address), entry.recipient.noteSelf, wallet::AddressPurpose::RECEIVE)) result.error = Tr("The request was saved, but its address label could not be saved.");
        }
        if (command.reserve_receiving_address) { settings.remove(address_key); settings.sync(); }
    } else if (command.kind == ReceiveCommand::Kind::Remove) {
        const auto existing = std::find_if(state.receive_requests.begin(), state.receive_requests.end(), [&](const auto& item) { return item.id == command.entry.id; });
        result.success = existing != state.receive_requests.end() && m_wallet->setAddressReceiveRequest(DecodeDestination(existing->recipient.address), util::ToString(existing->id), {});
        if (!result.success) result.error = Tr("The payment request could not be removed.");
    } else if (command.kind == ReceiveCommand::Kind::Label) {
        const auto destination = DecodeDestination(command.address.toStdString());
        wallet::AddressPurpose purpose{};
        result.success = IsValidDestination(destination) && m_wallet->getAddress(destination, nullptr, &purpose) && m_wallet->setAddressBook(destination, command.label.toStdString(), purpose);
        if (result.success) {
            for (auto entry : state.receive_requests) {
                if (entry.recipient.address != command.address.toStdString() || entry.recipient.noteSelf == command.label.toStdString()) continue;
                entry.recipient.noteSelf = command.label.toStdString();
                if (!m_wallet->setAddressReceiveRequest(destination, util::ToString(entry.id), ReceiveRequestHistoryModel::SerializeEntry(entry))) result.error = Tr("The address label was saved, but a payment request note could not be saved.");
            }
        } else result.error = Tr("The address label could not be saved.");
    } else if (command.kind == ReceiveCommand::Kind::AddressType) {
        result.success = type && SupportsType(state, *type) && setAddressType(node, type_text);
        if (!result.success) result.error = Tr("The receiving address preference could not be saved.");
    } else if (command.kind == ReceiveCommand::Kind::RememberAddress) {
        if (command.address.isEmpty()) settings.remove(address_key);
        else settings.setValue(address_key, command.address);
        settings.sync();
        result.success = settings.status() == QSettings::NoError;
        if (!result.success) result.error = Tr("The receiving address preference could not be saved.");
    }
    return complete();
}
