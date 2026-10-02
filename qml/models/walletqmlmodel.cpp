
// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletqmlmodel.h>

#include <common/messages.h>
#include <qml/bitcoinamount.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/receiverequestentry.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signverifymessagemodel.h>
#include <qml/models/walletunlock.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/models/transactionflow.h>
#include <qml/util.h>

#include <chainparams.h>
#include <common/types.h>
#include <consensus/amount.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <node/psbt.h>
#include <node/transaction.h>
#include <node/types.h>
#include <addresstype.h>
#include <outputtype.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <policy/truc_policy.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <qml/bitcoinunits.h>
#include <script/solver.h>
#include <support/allocators/secure.h>
#include <univalue.h>
#include <util/result.h>
#include <util/rbf.h>
#include <util/threadnames.h>
#include <util/translation.h>
#include <wallet/coincontrol.h>
#include <wallet/fees.h>
#include <wallet/scriptpubkeyman.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>

#include <QDateTime>
#include <QMetaObject>
#include <QRegularExpression>
#include <QScopedValueRollback>
#include <QSettings>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <array>
#include <cstddef>
#include <functional>
#include <limits>
#include <optional>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace {
constexpr unsigned int DEFAULT_STANDARD_FEE_TARGET{6};
constexpr int FEE_ESTIMATE_DEBOUNCE_MS{250};
constexpr unsigned int FEE_RATE_BASIS_VBYTES{1000};
constexpr std::array<unsigned int, 3> STANDARD_FEE_TARGETS{2, DEFAULT_STANDARD_FEE_TARGET, 10};
constexpr std::array<unsigned int, 7> CUSTOM_FEE_TARGETS{2, 3, 4, 6, 10, 25, 50};
const QRegularExpression CUSTOM_FEE_RATE_PATTERN{QStringLiteral(R"(^[0-9]+(?:\.[0-9]{0,3})?$)")};

struct ReceiveRequestPaymentScan {
    std::map<Txid, std::map<QString, CAmount>> payments;
    std::map<QString, CAmount> totals;
    std::map<QString, std::set<Txid>> txids_by_address;
    std::set<Txid> unconfirmed_txids;
    std::set<QString> observed_addresses;
};

struct UnconfirmedReceiveRequestPaymentCheck {
    std::set<Txid> inactive;
    std::set<Txid> confirmed;
};

std::map<QString, CAmount> ScanReceiveRequestTransaction(const interfaces::WalletTx& tx,
    interfaces::Wallet& wallet, const QSet<QString>& request_addresses, std::set<QString>& observed, bool& unconfirmed)
{
    std::map<QString, CAmount> received;
    unconfirmed = false;
    if (!tx.tx) return received;
    for (size_t i = 0; i < tx.tx->vout.size(); ++i) {
        const auto& output = tx.tx->vout[i];
        if (output.nValue <= 0 || i >= tx.txout_is_mine.size() || !tx.txout_is_mine[i]) continue;
        CTxDestination destination;
        if (!ExtractDestination(output.scriptPubKey, destination)) continue;
        const QString address = QString::fromStdString(EncodeDestination(destination));
        if (!request_addresses.contains(address)) continue;
        observed.insert(address);
        received[address] += output.nValue;
    }
    if (received.empty()) return received;

    interfaces::WalletTxStatus status{};
    interfaces::WalletOrderForm order_form;
    bool in_mempool{false};
    int num_blocks{0};
    wallet.getWalletTxDetails(tx.tx->GetHash(), status, order_form, in_mempool, num_blocks);
    const auto replacement = tx.value_map.find("replaced_by_txid");
    const bool replaced = replacement != tx.value_map.end() && !replacement->second.empty();
    // The receiver's wallet has no replacement marker on the original tx.
    // An unconfirmed transaction that left the mempool must not add to the
    // received total, but its address remains locked against reuse.
    if (status.depth_in_main_chain < 0 || status.is_abandoned ||
        (status.depth_in_main_chain == 0 && (!in_mempool || replaced))) received.clear();
    else unconfirmed = status.depth_in_main_chain == 0;
    return received;
}

int FallbackFeeMultiplier(const unsigned int target)
{
    for (size_t i = 0; i < STANDARD_FEE_TARGETS.size(); ++i) {
        if (STANDARD_FEE_TARGETS[i] == target) {
            return static_cast<int>(STANDARD_FEE_TARGETS.size() - i);
        }
    }

    return 1;
}

QString FormatFeeEstimate(CAmount amount)
{
    BitcoinAmount bitcoin_amount;
    bitcoin_amount.setSatoshi(amount);
    return bitcoin_amount.displayWithUnit();
}

bool AmountPlusFeeExceedsBalance(const CAmount amount, const CAmount fee, const CAmount balance)
{
    if (amount > balance) {
        return true;
    }
    return fee > balance - amount;
}

void ClearTransactionInputScripts(CMutableTransaction& tx)
{
    for (CTxIn& input : tx.vin) {
        input.scriptSig.clear();
        input.scriptWitness.SetNull();
    }
}

void ApplySelectedInputsPolicy(wallet::CCoinControl& coin_control)
{
    // Manually selected coins should be the only wallet inputs used.
    coin_control.m_allow_other_inputs = !coin_control.HasSelected();
}

std::optional<CAmount> ParseCustomFeeRatePerKvB(const QString& custom_fee_rate)
{
    const QString trimmed = custom_fee_rate.trimmed();
    if (trimmed.isEmpty() || !CUSTOM_FEE_RATE_PATTERN.match(trimmed).hasMatch()) {
        return std::nullopt;
    }

    const QStringList parts = trimmed.split('.');
    bool whole_ok{false};
    const CAmount whole_part{parts.at(0).toLongLong(&whole_ok)};
    if (!whole_ok) {
        return std::nullopt;
    }

    QString fractional_part = parts.size() == 2 ? parts.at(1) : QString{};
    while (fractional_part.size() < 3) {
        fractional_part += QLatin1Char{'0'};
    }

    bool fractional_ok{false};
    const CAmount fractional_value{
        fractional_part.isEmpty() ? 0 : fractional_part.toLongLong(&fractional_ok)};
    if (!fractional_part.isEmpty() && !fractional_ok) {
        return std::nullopt;
    }

    const CAmount fee_rate_per_kvb = (whole_part * FEE_RATE_BASIS_VBYTES) + fractional_value;
    if (fee_rate_per_kvb < DEFAULT_MIN_RELAY_TX_FEE) {
        return std::nullopt;
    }

    return fee_rate_per_kvb;
}

std::optional<CTxDestination> PreviewChangeDestination(const OutputType change_type)
{
    const uint160 dummy_key_hash{};
    switch (change_type) {
    case OutputType::BECH32M:
        return WitnessV1Taproot{XOnlyPubKey::NUMS_H};
    case OutputType::BECH32:
        return WitnessV0KeyHash{dummy_key_hash};
    case OutputType::P2SH_SEGWIT:
        return ScriptHash{GetScriptForDestination(WitnessV0KeyHash{dummy_key_hash})};
    case OutputType::LEGACY:
        return PKHash{dummy_key_hash};
    case OutputType::UNKNOWN:
        return std::nullopt;
    }
    return std::nullopt;
}

void ApplyPreviewChangeDestination(wallet::CCoinControl& coin_control, const OutputType change_type)
{
    if (const auto destination = PreviewChangeDestination(change_type)) {
        coin_control.destChange = *destination;
    }
}

void ApplyRegtestStaticFeeOverride(wallet::CCoinControl& coin_control)
{
    if (Params().GetChainType() != ChainType::REGTEST) {
        return;
    }

    // Regtest commonly runs without fee estimation, so use a fixed static fee
    // rate instead of target-based estimation.
    coin_control.m_confirm_target.reset();
    coin_control.m_feerate = CFeeRate{wallet::DEFAULT_TRANSACTION_MINFEE};
}

// Use the same available input set for automatic sendall and sweep detection.
// Clear manual selections because AvailableCoins otherwise omits those outputs.
std::vector<wallet::COutput> AvailableSendAllCoins(const wallet::CWallet& wallet, wallet::CCoinControl control)
    EXCLUSIVE_LOCKS_REQUIRED(wallet.cs_wallet)
{
    control.UnSelectAll();
    wallet::CoinFilterParams filter;
    filter.min_amount = 0;
    filter.skip_locked = true;
    return wallet::AvailableCoins(wallet, &control, std::nullopt, filter).All();
}

bool TransactionSweepsAvailableFunds(interfaces::Wallet& wallet_interface, const CTransaction& tx, const wallet::CCoinControl& control)
{
    auto* wallet = wallet_interface.wallet();
    if (!wallet) return false;
    LOCK(wallet->cs_wallet);
    // Selecting every coin is not a sweep if funds return to the wallet.
    for (const auto& output : tx.vout) {
        if (wallet->IsMine(output)) return false;
    }
    const auto available = AvailableSendAllCoins(*wallet, control);
    return !available.empty() && std::all_of(available.begin(), available.end(), [&tx](const auto& coin) {
        return std::any_of(tx.vin.begin(), tx.vin.end(), [&coin](const auto& input) {
            return input.prevout == coin.outpoint;
        });
    });
}

// Build the no-change transaction using the same rules as Core's sendall RPC.
// Calling the RPC itself would also sign (including invoking external signers),
// which must not happen while the user is editing the form or previewing fees.
util::Result<wallet::CreatedTransactionResult> CreateSendAllTransaction(
    interfaces::Wallet& wallet_interface,
    const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& control, int remainder_index, bool sign)
{
    auto* wallet = wallet_interface.wallet();
    if (!wallet || remainder_index < 0 || static_cast<size_t>(remainder_index) >= recipients.size()) {
        return util::Error{Untranslated("Unable to prepare a send-all transaction.")};
    }
    LOCK(wallet->cs_wallet);
    FeeCalculation fee_calc;
    const CFeeRate rate = wallet::GetMinimumFeeRate(*wallet, control, &fee_calc);
    if (control.m_feerate && rate > *control.m_feerate) {
        return util::Error{Untranslated("Fee rate is below the minimum fee rate.")};
    }
    if (fee_calc.reason == FeeReason::FALLBACK && !wallet->m_allow_fallback_fee) {
        return util::Error{Untranslated("Fee estimation failed. Fallbackfee is disabled.")};
    }
    // Explicit selections restrict sendall; otherwise use all available coins
    // without changing the form's coin-control selection.
    auto inputs = control.ListSelected();
    if (inputs.empty()) {
        for (const auto& coin : AvailableSendAllCoins(*wallet, control)) {
            inputs.push_back(coin.outpoint);
        }
    }
    if (inputs.empty()) return util::Error{Untranslated("No available inputs to use maximum.")};
    CMutableTransaction tx;
    tx.version = control.m_version;
    CAmount total{0};
    for (const auto& input : inputs) {
        const auto* source = wallet->GetWalletTx(input.hash);
        if (!source || input.n >= source->tx->vout.size() || wallet->IsSpent(input)
            || !wallet->IsMine(source->tx->vout[input.n])) {
            return util::Error{Untranslated("A selected input is no longer available.")};
        }
        if (wallet->GetTxDepthInMainChain(*source) == 0 && source->tx->version == TRUC_VERSION) {
            return util::Error{Untranslated("Cannot spend an unconfirmed version 3 input in this transaction.")};
        }
        total += source->tx->vout[input.n].nValue;
        const uint32_t sequence = wallet->m_signal_rbf ? MAX_BIP125_RBF_SEQUENCE : CTxIn::MAX_SEQUENCE_NONFINAL;
        tx.vin.emplace_back(input, CScript{}, sequence);
    }
    CAmount fixed_amount{0};
    std::set<CTxDestination> destinations;
    for (size_t i = 0; i < recipients.size(); ++i) {
        const auto& recipient = recipients[i];
        if (!destinations.insert(recipient.dest).second) {
            return util::Error{Untranslated("Each recipient must have a different address when using maximum.")};
        }
        const CAmount amount = static_cast<int>(i) == remainder_index ? 0 : recipient.nAmount;
        fixed_amount += amount;
        tx.vout.emplace_back(amount, GetScriptForDestination(recipient.dest));
    }
    const auto size = wallet::CalculateMaximumSignedTxSize(CTransaction(tx), wallet);
    if (size.vsize < 0) return util::Error{Untranslated("Unable to determine the transaction size.")};
    if (size.weight > MAX_STANDARD_TX_WEIGHT) return util::Error{Untranslated("Transaction too large.")};
    const CAmount fee = rate.GetFee(size.vsize)
        + wallet->chain().calculateCombinedBumpFee(inputs, rate).value_or(0);
    if (fee > wallet->m_default_max_tx_fee) return util::Error{Untranslated("Transaction fee exceeds the maximum configured fee.")};
    if (total <= fee || fixed_amount > total - fee) {
        return util::Error{Untranslated("Selected inputs do not cover the amount plus fee")};
    }
    tx.vout[remainder_index].nValue = total - fee - fixed_amount;
    for (const auto& output : tx.vout) {
        if (IsDust(output, wallet->chain().relayDustFee())) {
            return util::Error{Untranslated("The amount after fees is below the dust threshold.")};
        }
    }
    FastRandomContext rng;
    wallet::DiscourageFeeSniping(tx, rng, wallet->chain(), wallet->GetLastBlockHash(), wallet->GetLastBlockHeight());
    if (sign && !wallet->SignTransaction(tx)) return util::Error{Untranslated("Signing transaction failed.")};
    return wallet::CreatedTransactionResult{MakeTransactionRef(std::move(tx)), fee, std::nullopt, fee_calc};
}

// A failed fixed-amount preview can still report the fee needed to spend
// selected inputs when even a transaction without change is unaffordable.
std::optional<SendFeePreview> EstimateInsufficientSelectedInputsFee(
    interfaces::Wallet& wallet_interface, const std::vector<wallet::CRecipient>& recipients,
    const wallet::CCoinControl& control)
{
    auto* wallet = wallet_interface.wallet();
    if (!wallet || !control.HasSelected() || control.m_allow_other_inputs) return std::nullopt;

    LOCK(wallet->cs_wallet);
    FeeCalculation fee_calc;
    const CFeeRate rate = wallet::GetMinimumFeeRate(*wallet, control, &fee_calc);
    if (rate.GetFeePerK() <= 0 || (control.m_feerate && rate > *control.m_feerate)
        || (fee_calc.reason == FeeReason::FALLBACK && !wallet->m_allow_fallback_fee)) {
        return std::nullopt;
    }

    CMutableTransaction tx;
    tx.version = control.m_version;
    CAmount input_amount{0};
    const auto inputs = control.ListSelected();
    for (const auto& input : inputs) {
        const auto* source = wallet->GetWalletTx(input.hash);
        if (!source || input.n >= source->tx->vout.size() || wallet->IsSpent(input)
            || !wallet->IsMine(source->tx->vout[input.n])) {
            return std::nullopt;
        }
        input_amount += source->tx->vout[input.n].nValue;
        tx.vin.emplace_back(input);
    }

    CAmount send_amount{0};
    for (const auto& recipient : recipients) {
        send_amount += recipient.nAmount;
        tx.vout.emplace_back(recipient.nAmount, GetScriptForDestination(recipient.dest));
    }
    const auto size = wallet::CalculateMaximumSignedTxSize(CTransaction(tx), wallet, &control);
    if (size.vsize < 0) return std::nullopt;
    const CAmount fee = rate.GetFee(size.vsize)
        + wallet->chain().calculateCombinedBumpFee(inputs, rate).value_or(0);
    if (!AmountPlusFeeExceedsBalance(send_amount, fee, input_amount)) return std::nullopt;
    return SendFeePreview{fee, static_cast<int>(inputs.size()), rate.GetFeePerK(), std::nullopt, false};
}

std::optional<SendFeePreview> TryPreviewFee(interfaces::Wallet& wallet,
                                     const std::vector<wallet::CRecipient>& recipients,
                                     const wallet::CCoinControl& coin_control, int remainder_index = -1)
{
    const auto result = remainder_index >= 0
        ? CreateSendAllTransaction(wallet, recipients, coin_control, remainder_index, /*sign=*/false)
        : wallet.createTransaction(recipients, coin_control, /*sign=*/false, /*change_pos=*/std::nullopt);
    if (!result) {
        return remainder_index < 0
            ? EstimateInsufficientSelectedInputsFee(wallet, recipients, coin_control)
            : std::nullopt;
    }

    const CAmount rate = coin_control.m_feerate
        ? coin_control.m_feerate->GetFeePerK()
        : wallet.getMinimumFee(FEE_RATE_BASIS_VBYTES, coin_control, nullptr, nullptr);
    return SendFeePreview{result->fee, result->tx ? static_cast<int>(result->tx->vin.size()) : 0, rate,
        remainder_index >= 0 ? std::make_optional(result->tx->vout[remainder_index].nValue) : std::nullopt,
        result->tx && !result->change_pos && TransactionSweepsAvailableFunds(wallet, *result->tx, coin_control)};
}

std::optional<SendFeePreview> EstimatePreviewFee(interfaces::Wallet& wallet,
                                          const std::vector<wallet::CRecipient>& recipients,
                                          const wallet::CCoinControl& base_coin_control,
                                          const OutputType preview_change_type,
                                          const unsigned int target, int remainder_index = -1)
{
    wallet::CCoinControl coin_control{base_coin_control};
    ApplySelectedInputsPolicy(coin_control);
    coin_control.m_feerate.reset();
    coin_control.m_confirm_target = target;
    ApplyPreviewChangeDestination(coin_control, preview_change_type);
    ApplyRegtestStaticFeeOverride(coin_control);

    if (const auto fee = TryPreviewFee(wallet, recipients, coin_control, remainder_index)) {
        return fee;
    }

    // A maximum amount must use the same fee policy as final construction.
    // Do not fill an amount using a made-up rate if sendall cannot be estimated.
    if (remainder_index >= 0 || Params().GetChainType() == ChainType::REGTEST) {
        return std::nullopt;
    }

    const CAmount required_fee_per_k = wallet.getRequiredFee(FEE_RATE_BASIS_VBYTES);
    if (required_fee_per_k <= 0) {
        return std::nullopt;
    }

    wallet::CCoinControl fallback_coin_control{coin_control};
    fallback_coin_control.m_confirm_target.reset();
    // Keep fallback previews distinct across presets even when the backend can
    // only provide a minimum required feerate.
    fallback_coin_control.m_feerate = CFeeRate{required_fee_per_k * FallbackFeeMultiplier(target)};

    if (const auto fee = TryPreviewFee(wallet, recipients, fallback_coin_control, remainder_index)) {
        return fee;
    }

    return std::nullopt;
}

std::optional<SendFeePreview> EstimateCustomPreviewFee(interfaces::Wallet& wallet,
                                                const std::vector<wallet::CRecipient>& recipients,
                                                const wallet::CCoinControl& base_coin_control,
                                                const OutputType preview_change_type,
                                                const CAmount fee_rate_per_kvb, int remainder_index = -1)
{
    wallet::CCoinControl coin_control{base_coin_control};
    ApplySelectedInputsPolicy(coin_control);
    coin_control.m_confirm_target.reset();
    coin_control.m_feerate = CFeeRate{fee_rate_per_kvb};
    // An explicit custom rate may be below the wallet's automatic minimum.
    coin_control.fOverrideFeeRate = true;
    ApplyPreviewChangeDestination(coin_control, preview_change_type);

    if (const auto fee = TryPreviewFee(wallet, recipients, coin_control, remainder_index)) {
        return fee;
    }

    return std::nullopt;
}

std::optional<std::vector<wallet::CRecipient>> BuildRecipients(const SendRecipientsListModel& recipients, const SendRecipient* maximum_recipient = nullptr)
{
    std::vector<wallet::CRecipient> vec_send;
    vec_send.reserve(recipients.recipients().size());

    for (auto* recipient : recipients.recipients()) {
        if (recipient == nullptr || (recipient != maximum_recipient && !recipient->isValid())) {
            return std::nullopt;
        }

        const CTxDestination destination = DecodeDestination(recipient->address()->address().toStdString());
        if (!IsValidDestination(destination)) {
            return std::nullopt;
        }

        vec_send.push_back({destination, recipient->cAmount(), false});
    }

    if (vec_send.empty()) {
        return std::nullopt;
    }

    return vec_send;
}

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

QString LocalizedString(const bilingual_str& value)
{
    return QString::fromStdString(value.translated.empty() ? value.original : value.translated);
}

QString OutputTypeId(OutputType type)
{
    return QString::fromStdString(FormatOutputType(type));
}

QString OutputTypeIdFromDestination(const CTxDestination& destination)
{
    if (std::get_if<PKHash>(&destination)) return OutputTypeId(OutputType::LEGACY);
    if (std::get_if<ScriptHash>(&destination)) return OutputTypeId(OutputType::P2SH_SEGWIT);
    if (std::get_if<WitnessV0KeyHash>(&destination) || std::get_if<WitnessV0ScriptHash>(&destination)) return OutputTypeId(OutputType::BECH32);
    if (std::get_if<WitnessV1Taproot>(&destination)) return OutputTypeId(OutputType::BECH32M);
    return {};
}

QString OutputTypeLabel(OutputType type)
{
    switch (type) {
    case OutputType::BECH32M:
        return QObject::tr("Bech32m (Taproot)");
    case OutputType::BECH32:
        return QObject::tr("Bech32 (SegWit)");
    case OutputType::P2SH_SEGWIT:
        return QObject::tr("Base58 (P2SH-SegWit)");
    case OutputType::LEGACY:
        return QObject::tr("Base58 (Legacy)");
    case OutputType::UNKNOWN:
        return {};
    }
    return {};
}

QString OutputTypeDescription(OutputType type)
{
    switch (type) {
    case OutputType::BECH32M:
        return QObject::tr("Lower fees · Better privacy");
    case OutputType::BECH32:
        return QObject::tr("Lower fees · Widely supported");
    case OutputType::P2SH_SEGWIT:
        return QObject::tr("Higher fees · Backward compatible");
    case OutputType::LEGACY:
        return QObject::tr("Higher fees · Not recommended");
    case OutputType::UNKNOWN:
        return {};
    }
    return {};
}

} // namespace

WalletQmlModel::WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, interfaces::Node* node, QObject *parent)
    : QObject(parent)
    , m_wallet(std::move(wallet))
    , m_node(node)
{
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    m_receive_payment_poll_timer.setInterval(1000);
    connect(&m_receive_payment_poll_timer, &QTimer::timeout, this, &WalletQmlModel::pollUnconfirmedReceiveRequestPayments);
    reloadReceiveRequests();
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(m_wallet.get(), this);
    m_bump_transaction_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    connect(m_send_recipients, &SendRecipientsListModel::totalAmountChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    connect(m_send_recipients, &SendRecipientsListModel::validationChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    m_sign_verify_message_model = new SignVerifyMessageModel(m_wallet.get(), this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_receiving_address = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_imported_psbt_model = new PsbtQmlModel(m_wallet.get(), m_node, this);
    initializeFeeEstimator();
    refreshSecurityState();
    subscribeToWalletSignals();
}

WalletQmlModel::WalletQmlModel(interfaces::Node* node, QObject* parent)
    : QObject(parent)
    , m_node(node)
{
    m_address_list_model = new AddressListModel(this);
    m_bump_transaction_model = new BumpTransactionModel(nullptr, this);
    m_bump_transaction_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_coins_list_model = new CoinsListModel(this);
    m_send_recipients = new SendRecipientsListModel(this);
    connect(m_send_recipients, &SendRecipientsListModel::totalAmountChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    connect(m_send_recipients, &SendRecipientsListModel::validationChanged,
            this, &WalletQmlModel::sendAmountExhaustsBalanceChanged);
    m_sign_verify_message_model = new SignVerifyMessageModel(nullptr, this);
    m_sign_verify_message_model->setSecurityStateChangedFn([this]() { refreshSecurityState(); });
    m_current_payment_request = new PaymentRequest(this);
    m_receiving_address = new PaymentRequest(this);
    m_detail_payment_request = new PaymentRequest(this);
    m_receive_requests = new ReceiveRequestHistoryModel(this);
    m_imported_psbt_model = new PsbtQmlModel(nullptr, m_node, this);
    initializeFeeEstimator();
}

WalletQmlModel::WalletQmlModel(QObject* parent)
    : WalletQmlModel(nullptr, parent)
{
}

WalletQmlModel::~WalletQmlModel()
{
    unsubscribeFromWalletSignals();
    m_receive_payment_poll_timer.stop();
    if (m_receive_payment_poll_thread) {
        m_receive_payment_poll_thread->wait();
        delete m_receive_payment_poll_thread;
    }
    if (m_receive_reconciliation_thread) {
        m_receive_reconciliation_thread->wait();
        delete m_receive_reconciliation_thread;
    }
    if (m_fee_estimation_timer) {
        m_fee_estimation_timer->stop();
    }
    if (m_fee_estimation_thread) {
        m_fee_estimation_thread->quit();
        m_fee_estimation_thread->wait();
    }
    delete m_fee_estimation_worker;
    delete m_transaction_activity_model;
    delete m_address_list_model;
    delete m_coins_list_model;
    delete m_send_recipients;
    delete m_sign_verify_message_model;
    delete m_current_payment_request;
    delete m_detail_payment_request;
    delete m_receive_requests;
    delete m_imported_psbt_model;
    if (m_current_transaction) {
        delete m_current_transaction;
    }
}

TransactionActivityModel* WalletQmlModel::transactionActivityModel()
{
    // Instantiate when the activity screen first asks for it.
    if (!m_transaction_activity_model) m_transaction_activity_model = new TransactionActivityModel(this);
    return m_transaction_activity_model;
}

void WalletQmlModel::setNode(interfaces::Node* node)
{
    m_node = node;
    if (m_imported_psbt_model) {
        m_imported_psbt_model->setNode(node);
    }
}

void WalletQmlModel::initializeFeeEstimator()
{
    m_fee_estimation_worker = new QObject;
    m_fee_estimation_thread = new QThread(this);
    m_fee_estimation_worker->moveToThread(m_fee_estimation_thread);
    m_fee_estimation_thread->start();
    QTimer::singleShot(0, m_fee_estimation_worker, []() {
        util::ThreadRename("qml-fee-est");
    });

    m_fee_estimation_timer = new QTimer(this);
    m_fee_estimation_timer->setSingleShot(true);
    m_fee_estimation_timer->setInterval(FEE_ESTIMATE_DEBOUNCE_MS);
    connect(m_fee_estimation_timer, &QTimer::timeout, this, &WalletQmlModel::requestFeeEstimatesNow);
}

QString WalletQmlModel::balance() const
{
    if (!m_wallet) {
        return "0";
    }
    return QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), m_wallet->getBalance());
}

qint64 WalletQmlModel::balanceSatoshi() const
{
    if (!m_wallet) {
        return 0;
    }
    return m_wallet->getBalance();
}

QString WalletQmlModel::estimatedFee() const
{
    if (m_custom_fee_enabled) {
        return customFeeRateValid() && m_custom_fee_estimate.has_value()
            ? FormatFeeEstimate(m_custom_fee_estimate->fee)
            : QString{};
    }
    return estimatedFeeForTarget(feeTargetBlocks());
}

std::optional<SendFeePreview> WalletQmlModel::selectedFeePreview() const
{
    if (m_custom_fee_enabled) return customFeeRateValid() ? m_custom_fee_estimate : std::nullopt;
    const auto it = m_fee_estimates.constFind(feeTargetBlocks());
    return it == m_fee_estimates.constEnd() ? std::nullopt : std::make_optional(it.value());
}

std::optional<CAmount> WalletQmlModel::selectedFeeEstimate() const
{
    const auto preview = selectedFeePreview();
    return preview ? std::make_optional(preview->fee) : std::nullopt;
}

qint64 WalletQmlModel::estimatedFeeSatoshi() const
{
    return selectedFeeEstimate().value_or(-1);
}

QString WalletQmlModel::estimatedFeeRate() const
{
    const auto preview = selectedFeePreview();
    return preview && preview->rate_per_kvb > 0
        ? QString::number(preview->rate_per_kvb / 1000.0, 'f', 3).remove(QRegularExpression("0+$")).remove(QRegularExpression("\\.$"))
        : QString{};
}

int WalletQmlModel::estimatedInputCount() const
{
    const auto preview = selectedFeePreview();
    return preview ? preview->inputs : 0;
}

qint64 WalletQmlModel::sendTotalSatoshi() const
{
    if (!m_send_recipients) return 0;
    const auto fee = selectedFeeEstimate();
    return m_send_recipients->totalAmountSatoshi()
        + (fee ? *fee : 0);
}

qint64 WalletQmlModel::availableSendBalanceSatoshi() const
{
    return m_wallet ? m_wallet->getAvailableBalance(wallet::CCoinControl{}) : 0;
}

bool WalletQmlModel::sendDraftSweepsWallet() const
{
    const auto preview = selectedFeePreview();
    return preview && preview->sweeps_wallet;
}

void WalletQmlModel::useMaximum()
{
    if (!m_wallet || !m_send_recipients || !m_send_recipients->currentRecipient()) return;
    setMaximumRecipient(m_send_recipients->currentRecipient());
    scheduleFeeEstimates();
}

void WalletQmlModel::setMaximumRecipient(SendRecipient* recipient)
{
    if (m_maximum_recipient == recipient) return;
    QObject::disconnect(m_maximum_amount_connection);
    QObject::disconnect(m_maximum_recipient_destroyed_connection);
    m_maximum_recipient = recipient;
    if (recipient) {
        m_maximum_amount_connection = connect(recipient->amount(), &BitcoinAmount::amountChanged, this, [this] {
            // Editing the prefilled amount switches back to a fixed-amount payment.
            if (!m_updating_maximum) setMaximumRecipient(nullptr);
        });
        m_maximum_recipient_destroyed_connection = connect(recipient, &QObject::destroyed, this, [this] {
            m_maximum_recipient.clear();
            Q_EMIT maximumRecipientChanged();
        });
    }
    Q_EMIT maximumRecipientChanged();
}

void WalletQmlModel::updateMaximumAmount()
{
    const auto preview = selectedFeePreview();
    if (!m_maximum_recipient || !preview || !preview->maximum_amount) return;
    QScopedValueRollback<bool> updating{m_updating_maximum, true};
    m_maximum_recipient->amount()->setSatoshi(*preview->maximum_amount);
}

void WalletQmlModel::setCustomFeeTarget(unsigned int target)
{
    if (!m_wallet || target < 1 || target > 1008) return;
    wallet::CCoinControl control{m_coin_control};
    control.m_feerate.reset();
    control.m_confirm_target = target;
    ApplyRegtestStaticFeeOverride(control);
    const CAmount rate = control.m_feerate ? control.m_feerate->GetFeePerK()
        : m_wallet->getMinimumFee(FEE_RATE_BASIS_VBYTES, control, nullptr, nullptr);
    setCustomFeeEnabled(true);
    // Leave an unavailable estimate blank instead of suggesting a zero fee.
    setCustomFeeRate(rate > 0 ? QString::number(rate / 1000.0, 'f', 3) : QString{});
    // Explicit slider selection takes precedence over an inferred target when
    // several targets currently have the same estimated fee rate.
    setFeeTargetBlocks(target);
}

CFeeRate WalletQmlModel::dustRelayFee() const
{
    return m_node ? m_node->getDustRelayFee() : CFeeRate{DUST_RELAY_TX_FEE};
}

bool WalletQmlModel::sendAmountExhaustsBalance() const
{
    if (!m_wallet || !m_send_recipients || !m_send_recipients->allValid()) {
        return false;
    }

    wallet::CCoinControl coin_control{m_coin_control};
    ApplySelectedInputsPolicy(coin_control);

    const CAmount balance{m_wallet->getAvailableBalance(coin_control)};
    const CAmount total_amount{m_send_recipients->totalAmountSatoshi()};
    if (total_amount > balance) {
        return true;
    }
    if (const auto fee = selectedFeeEstimate()) {
        return AmountPlusFeeExceedsBalance(total_amount, *fee, balance);
    }

    // Without an estimate, a fixed-amount send cannot safely spend the full
    // balance because prepareTransaction() will still need to add a fee.
    return total_amount >= balance;
}

bool WalletQmlModel::customFeeRateValid() const
{
    return ParseCustomFeeRatePerKvB(m_custom_fee_rate).has_value();
}

QString WalletQmlModel::estimatedFeeForTarget(const unsigned int target_blocks) const
{
    const auto estimate = m_fee_estimates.constFind(target_blocks);
    if (estimate != m_fee_estimates.constEnd()) {
        return FormatFeeEstimate(estimate.value().fee);
    }

    return {};
}

int WalletQmlModel::feeTargetIndex(const unsigned int target_blocks) const
{
    for (size_t i = 0; i < STANDARD_FEE_TARGETS.size(); ++i) {
        if (STANDARD_FEE_TARGETS[i] == target_blocks) {
            return static_cast<int>(i);
        }
    }

    return 1;
}

QString WalletQmlModel::name() const
{
    if (!m_wallet) {
        return QString();
    }
    return QString::fromStdString(m_wallet->getWalletName());
}

QString WalletQmlModel::displayName() const
{
    if (!m_display_name.isEmpty()) {
        return m_display_name;
    }
    return name();
}

void WalletQmlModel::setDisplayName(const QString& display_name)
{
    if (m_display_name != display_name) {
        m_display_name = display_name;
        Q_EMIT displayNameChanged();
    }
}

QString WalletQmlModel::keyScheme() const
{
    if (!m_wallet) {
        return {};
    }
    return keySchemeDisplayText(keySchemeForWallet(*m_wallet));
}

WalletQmlModel::KeyScheme WalletQmlModel::keySchemeKind() const
{
    if (!m_wallet) {
        return KeyScheme::SingleKey;
    }
    return keySchemeForWallet(*m_wallet);
}

WalletQmlModel::KeyScheme WalletQmlModel::keySchemeForWallet(interfaces::Wallet& wallet)
{
    const wallet::CWallet* raw_wallet = wallet.wallet();
    if (raw_wallet) {
        LOCK(raw_wallet->cs_wallet);
        if (raw_wallet->IsWalletFlagSet(wallet::WALLET_FLAG_EXTERNAL_SIGNER)) {
            return KeyScheme::ExternalSigner;
        }
        if (raw_wallet->IsWalletFlagSet(wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS)) {
            return KeyScheme::WatchOnly;
        }
        if (WalletUsesMultiKeyDescriptor(*raw_wallet)) {
            return KeyScheme::MultiKey;
        }
        return KeyScheme::SingleKey;
    }
    if (wallet.privateKeysDisabled()) {
        return KeyScheme::WatchOnly;
    }
    return KeyScheme::SingleKey;
}

QString WalletQmlModel::keySchemeDisplayText(KeyScheme scheme)
{
    switch (scheme) {
    case KeyScheme::WatchOnly:
        return tr("Watch-only");
    case KeyScheme::MultiKey:
        return tr("Multi-key");
    case KeyScheme::ExternalSigner:
        return tr("External signer");
    case KeyScheme::SingleKey:
    default:
        return tr("Single-key");
    }
}

QString WalletQmlModel::privateKeysStatus() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->privateKeysDisabled() ? tr("Disabled") : tr("Enabled");
}

QString WalletQmlModel::externalSignerStatus() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->hasExternalSigner() ? tr("Enabled") : tr("None");
}

bool WalletQmlModel::canManagePassphrase() const
{
    return m_wallet && !m_wallet->privateKeysDisabled();
}

bool WalletQmlModel::encryptWallet(const QString& passphrase)
{
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    if (passphrase.isEmpty()) {
        setSettingsError(tr("Enter a new wallet password."));
        return false;
    }

    SecureString secure_passphrase{QmlUtil::SecureStringFromQString(passphrase)};
    const bool encrypted{m_wallet->encryptWallet(secure_passphrase)};
    QmlUtil::ClearSecureString(secure_passphrase);
    if (!encrypted) {
        setSettingsError(tr("The wallet password could not be set."));
        return false;
    }

    refreshSecurityState();
    return true;
}

bool WalletQmlModel::changeWalletPassphrase(const QString& old_passphrase, const QString& new_passphrase)
{
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    if (old_passphrase.isEmpty()) {
        setSettingsError(tr("Enter the current wallet password."));
        return false;
    }
    if (new_passphrase.isEmpty()) {
        setSettingsError(tr("Enter a new wallet password."));
        return false;
    }

    SecureString secure_old_passphrase{QmlUtil::SecureStringFromQString(old_passphrase)};
    SecureString secure_new_passphrase{QmlUtil::SecureStringFromQString(new_passphrase)};
    const bool changed{m_wallet->changeWalletPassphrase(secure_old_passphrase, secure_new_passphrase)};
    QmlUtil::ClearSecureString(secure_old_passphrase);
    QmlUtil::ClearSecureString(secure_new_passphrase);
    if (!changed) {
        setSettingsError(tr("The current wallet password was incorrect."));
        return false;
    }

    refreshSecurityState();
    return true;
}

bool WalletQmlModel::backupWallet(const QString& path)
{
    clearSettingsError();
    if (!m_wallet) {
        setSettingsError(tr("No wallet is selected."));
        return false;
    }
    if (path.trimmed().isEmpty()) {
        setSettingsError(tr("Choose a location for the wallet backup."));
        return false;
    }

    if (!m_wallet->backupWallet(path.toStdString())) {
        setSettingsError(tr("The wallet could not be backed up."));
        return false;
    }

    return true;
}

void WalletQmlModel::clearSettingsError()
{
    setSettingsError(QString());
}

QVariantList WalletQmlModel::availableReceiveAddressTypes() const
{
    if (!m_wallet || !m_wallet->canGetAddresses()) {
        return {};
    }

    QVariantList types;
    const std::array ordered_types{
        OutputType::BECH32M,
        OutputType::BECH32,
        OutputType::P2SH_SEGWIT,
        OutputType::LEGACY,
    };

    for (const OutputType type : ordered_types) {
        if (type == OutputType::BECH32M && (!m_wallet || !m_wallet->taprootEnabled())) {
            continue;
        }
        QVariantMap item;
        item.insert(QStringLiteral("id"), OutputTypeId(type));
        item.insert(QStringLiteral("label"), OutputTypeLabel(type));
        item.insert(QStringLiteral("description"), OutputTypeDescription(type));
        types.append(item);
    }
    return types;
}

QString WalletQmlModel::defaultReceiveAddressType() const
{
    const QVariantList available_types = availableReceiveAddressTypes();
    QString saved_type;
    if (m_node) {
        const auto preferences = m_node->getPersistentSetting("qml_receive_address_types");
        const auto& value = preferences[name().toStdString()];
        if (value.isStr()) saved_type = QString::fromStdString(value.get_str());
    }
    for (const QVariant& item : available_types) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == saved_type) {
            return saved_type;
        }
    }

    // The list starts with Taproot, falling back to SegWit when unavailable.
    return available_types.empty()
        ? QString{}
        : available_types.front().toMap().value(QStringLiteral("id")).toString();
}

void WalletQmlModel::setDefaultReceiveAddressType(const QString& address_type)
{
    for (const QVariant& item : availableReceiveAddressTypes()) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == address_type) {
            if (!m_node) return;
            auto preferences = m_node->getPersistentSetting("qml_receive_address_types");
            if (!preferences.isObject()) preferences = common::SettingsValue{UniValue::VOBJ};
            preferences.pushKV(name().toStdString(), address_type.toStdString());
            m_node->updateRwSetting("qml_receive_address_types", preferences);
            return;
        }
    }
}

QString WalletQmlModel::receiveAddressTypeLabel(const QString& address_type) const
{
    for (const QVariant& item : availableReceiveAddressTypes()) {
        const QVariantMap type{item.toMap()};
        if (type.value(QStringLiteral("id")).toString() == address_type) {
            return type.value(QStringLiteral("label")).toString();
        }
    }
    return {};
}

void WalletQmlModel::removeWallet()
{
    if (!m_wallet) {
        return;
    }
    m_wallet->remove();
}

bool WalletQmlModel::setCurrentPaymentRequestAddress(QString address)
{
    if (!m_wallet || !m_current_payment_request || address.isEmpty()) {
        return false;
    }

    const CTxDestination destination{DecodeDestination(address.toStdString())};
    if (!IsValidDestination(destination)) {
        return false;
    }

    // A request already saved for this address is loaded for editing, not
    // duplicated: reopening the action on such an address means changing
    // the saved request, and silently minting a second request for the
    // same address multiplies rows that all describe one ask.
    if (m_receive_requests) {
        const QVariantList existing = m_receive_requests->matchingEntriesForAddress(address);
        if (!existing.isEmpty()) {
            const QString request_id = existing.first().toMap().value(QStringLiteral("requestId")).toString();
            if (!loadPaymentRequest(request_id)) {
                return false;
            }
            m_current_payment_request->setIsEditing(true);
            return true;
        }
    }

    m_current_payment_request->clear();
    m_current_payment_request->setDestination(destination);
    m_current_payment_request->setNoteSelf(getAddressLabel(address));
    m_current_payment_request->setIsEditing(false);
    m_current_payment_request->setIsEditing(true);
    // The explicit Receive action in Addresses must keep the chosen address.
    m_receiving_address->clear();
    m_receiving_address->setDestination(destination);
    QSettings settings;
    settings.setValue(persistedReceiveAddressTypeKey() + QStringLiteral("/address"), address);
    return true;
}

bool WalletQmlModel::ensureReceivingAddress(bool next, const QString& address_type)
{
    if (!m_wallet || !m_receiving_address) return false;
    auto* receiving = m_receiving_address;
    const QString type = address_type.isEmpty() ? defaultReceiveAddressType() : address_type;
    const auto available = availableReceiveAddressTypes();
    if (std::none_of(available.begin(), available.end(), [&](const QVariant& item) {
        return item.toMap().value(QStringLiteral("id")).toString() == type;
    })) return false;

    QSettings settings;
    const QString key = persistedReceiveAddressTypeKey() + QStringLiteral("/address");
    if (receiving->address().isEmpty() && !next) {
        const auto destination = DecodeDestination(settings.value(key).toString().toStdString());
        // Restore only an address belonging to this wallet's receive address book.
        wallet::AddressPurpose purpose{};
        if (IsValidDestination(destination) && m_wallet->getAddress(destination, nullptr, &purpose)
            && purpose == wallet::AddressPurpose::RECEIVE) receiving->setDestination(destination);
    }
    if (!receiving->address().isEmpty()) {
        for (const auto& tx : m_wallet->getWalletTxs()) recordReceiveRequestPayment(tx);
        const bool same_type = address_type.isEmpty() || OutputTypeIdFromDestination(receiving->destination()) == type;
        if (!next && !receiving->paymentReceived() && same_type) return true;
    }
    const auto parsed = ParseOutputType(type.toStdString());
    if (!parsed) return false;
    const auto destination = m_wallet->getNewDestination(*parsed, "");
    if (!destination || !IsValidDestination(*destination)) {
        receiving->setNeedsUnlock(m_wallet->isCrypted() && m_wallet->isLocked());
        return false;
    }
    receiving->clear();
    receiving->setDestination(*destination);
    settings.setValue(key, receiving->address());
    settings.sync();
    setDefaultReceiveAddressType(type);
    return true;
}

bool WalletQmlModel::ensureReceivingAddressWithPassphrase(const QString& passphrase, bool next, const QString& address_type)
{
    if (!m_wallet || !m_receiving_address) return false;
    auto secure_passphrase = QmlUtil::SecureStringFromQString(passphrase);
    const auto result = TryUnlockWithPassphrase(*m_wallet, secure_passphrase);
    if (result == WalletUnlockResult::IncorrectPassphrase) {
        m_receiving_address->setUnlockError(tr("The wallet password you entered was incorrect."));
        return false;
    }
    const bool relock = result == WalletUnlockResult::UnlockedNowRelockRequired;
    if (relock) refreshSecurityState();
    WalletRelockGuard guard{*m_wallet, [this] { refreshSecurityState(); }, relock};
    return ensureReceivingAddress(next, address_type);
}

bool WalletQmlModel::commitReceivingPaymentRequest()
{
    if (!m_wallet || !m_receiving_address || !m_current_payment_request) return false;
    if (!m_current_payment_request->id().isEmpty()) return false;
    // Reconcile before committing: never silently attach a draft to a different
    // address if a payment landed while the user was filling out the form.
    for (const auto& tx : m_wallet->getWalletTxs()) recordReceiveRequestPayment(tx);
    if (m_receiving_address->address().isEmpty() || m_receiving_address->paymentReceived()) return false;
    if (m_current_payment_request->amount()->satoshi() == 0 && m_current_payment_request->label().trimmed().isEmpty()
        && m_current_payment_request->message().trimmed().isEmpty() && m_current_payment_request->noteSelf().trimmed().isEmpty()) return false;
    m_current_payment_request->setDestination(m_receiving_address->destination());
    if (!savePaymentRequest(m_current_payment_request)) return false;
    // Reserve this address for the saved request. The next Receive view must
    // allocate a fresh address, including after restarting the wallet.
    QSettings settings;
    settings.remove(persistedReceiveAddressTypeKey() + QStringLiteral("/address"));
    settings.sync();
    m_receiving_address->clear();
    return true;
}

bool WalletQmlModel::ensurePaymentRequestDestination()
{
    if (!m_wallet || !m_current_payment_request) {
        return false;
    }
    if (!m_current_payment_request->address().isEmpty()) {
        return true;
    }
    const QString address_type_id = m_current_payment_request->addressType().isEmpty()
        ? defaultReceiveAddressType()
        : m_current_payment_request->addressType();
    OutputType output_type = m_wallet->getDefaultAddressType();
    if (!address_type_id.isEmpty()) {
        const auto parsed_type{ParseOutputType(address_type_id.toStdString())};
        if (!parsed_type) {
            return false;
        }
        output_type = *parsed_type;
    }
    const auto destination{m_wallet->getNewDestination(output_type, m_current_payment_request->noteSelf().toStdString())};
    if (!destination || !IsValidDestination(destination.value())) {
        return false;
    }
    m_current_payment_request->setAddressType(OutputTypeId(output_type));
    m_current_payment_request->setDestination(destination.value());
    return true;
}

bool WalletQmlModel::savePaymentRequest(PaymentRequest* request)
{
    if (!m_wallet || !request) {
        return false;
    }

    const bool is_update = !request->id().isEmpty();
    // Until the background history scan completes, an old request might have
    // received a payment that is not yet reflected in its persisted lock.
    if (is_update && (receiveRequestReconciliationPending() || m_receive_request_notifications_pending.load() > 0)) return false;
    if (is_update && request->amount()->satoshi() == 0 && request->label().trimmed().isEmpty()
        && request->message().trimmed().isEmpty() && request->noteSelf().trimmed().isEmpty()) return false;
    const QString request_id_text = is_update
        ? request->id()
        : QString::number(nextPaymentRequestId());

    bool parse_ok{false};
    const int64_t request_id{request_id_text.toLongLong(&parse_ok)};
    if (!parse_ok || request_id <= 0) {
        return false;
    }

    // Capture the stored private note before this save replaces the entry, so the
    // address book sync below can tell a note edit from a save that only
    // touched other fields (amount, message).
    std::optional<QString> previous_note;
    if (is_update && m_receive_requests) {
        if (const auto previous_entry = m_receive_requests->entryById(request_id_text)) {
            previous_note = QString::fromStdString(previous_entry->recipient.noteSelf);
        }
    }

    QmlRecentRequestEntry request_entry;
    request_entry.id = request_id;
    request_entry.date = is_update ? request->created() : QDateTime::currentDateTime();
    if (!is_update) {
        request->setCreated(request_entry.date);
    }
    if (!MoneyRange(request->amount()->satoshi())) return false;
    if (is_update) {
        // Reconcile before saving too: a payment may have arrived while an
        // inline editor was open, before its queued notification was delivered.
        const CAmount edited_amount = request->amount()->satoshi();
        const std::string edited_label = request->label().toStdString();
        const std::string edited_message = request->message().toStdString();
        const auto existing = m_receive_requests->entryById(request_id_text);
        if (!existing || request->address().toStdString() != existing->recipient.address) return false;
        request_entry.payment_received = existing->payment_received;
        if (existing->payment_received &&
            (edited_amount != existing->recipient.amount ||
             edited_label != existing->recipient.label ||
             edited_message != existing->recipient.message)) {
            return false;
        }
        request_entry.date = existing->date;
    }

    request_entry.recipient.address = request->address().toStdString();
    request_entry.recipient.label = request->label().toStdString();
    request_entry.recipient.amount = request->amount()->satoshi();
    request_entry.recipient.message = request->message().toStdString();
    request_entry.recipient.noteSelf = request->noteSelf().toStdString();

    const bool saved = m_wallet->setAddressReceiveRequest(
        request->destination(),
        request_id_text.toStdString(),
        ReceiveRequestHistoryModel::SerializeEntry(request_entry));
    if (!saved) {
        return false;
    }

    if (!is_update) {
        request->setId(static_cast<unsigned int>(request_id));
    }

    if (m_receive_requests) {
        m_receive_requests->prependOrReplace(request_entry);
        m_receive_request_addresses.insert(request->address());
        if (!is_update && m_receive_reconciliation_thread) m_receive_reconciliation_requested = true;
    }

    // Address book labels are private. Never copy public payment metadata
    // into them, or fan a request's note out to other requests on the address.
    const QString note = request->noteSelf();
    const bool note_edited = !previous_note.has_value() || note != *previous_note;
    const QString address_label = getAddressLabel(request->address());
    if (note_edited && note != address_label &&
        (!note.isEmpty() || (previous_note && address_label == *previous_note))) {
        writeAddressBookLabel(request->address(), note);
    }

    request->setIsEditing(false);

    if (m_detail_payment_request && m_detail_payment_request->id() == request->id()) {
        loadPaymentRequestDetail(request->id());
    }

    return true;
}

bool WalletQmlModel::commitPaymentRequest()
{
    if (!m_wallet || !m_current_payment_request) {
        return false;
    }

    if (!MoneyRange(m_current_payment_request->amount()->satoshi())) return false;
    if (!ensurePaymentRequestDestination()) {
        if (m_wallet->isCrypted() && m_wallet->isLocked()) {
            m_current_payment_request->setNeedsUnlock(true);
        }
        return false;
    }
    return savePaymentRequest(m_current_payment_request);
}

bool WalletQmlModel::commitPaymentRequestWithPassphrase(const QString& passphrase)
{
    if (!m_wallet || !m_current_payment_request) {
        return false;
    }

    SecureString secure_passphrase{QmlUtil::SecureStringFromQString(passphrase)};
    const auto result{TryUnlockWithPassphrase(*m_wallet, secure_passphrase)};
    switch (result) {
    case WalletUnlockResult::IncorrectPassphrase:
        m_current_payment_request->setUnlockError(tr("The wallet password you entered was incorrect."));
        return false;
    case WalletUnlockResult::AlreadyUnlocked:
    case WalletUnlockResult::UnlockedNowRelockRequired:
        break;
    }

    const bool need_relock = result == WalletUnlockResult::UnlockedNowRelockRequired;
    if (need_relock) {
        refreshSecurityState();
    }
    WalletRelockGuard relock_guard{*m_wallet, [this] { refreshSecurityState(); }, need_relock};

    if (!MoneyRange(m_current_payment_request->amount()->satoshi())) return false;
    if (!ensurePaymentRequestDestination()) {
        return false;
    }
    if (!savePaymentRequest(m_current_payment_request)) {
        return false;
    }

    m_current_payment_request->setNeedsUnlock(false);
    m_current_payment_request->setUnlockError(QString());
    return true;
}

void WalletQmlModel::reloadReceiveRequests()
{
    if (!m_receive_requests) return;
    if (!m_wallet) {
        m_receive_requests->setEntries({});
        m_receive_request_payments.clear();
        m_receive_request_totals.clear();
        m_receive_request_txids_by_address.clear();
        m_receive_request_unconfirmed_txids.clear();
        ++m_receive_payment_revision;
        m_receive_payment_poll_timer.stop();
        m_receive_request_addresses.clear();
        return;
    }
    m_receive_requests->setEntries(
        ReceiveRequestHistoryModel::DeserializeEntries(m_wallet->getAddressReceiveRequests()));
    m_receive_request_addresses = m_receive_requests->requestAddresses();
    m_receive_request_payments.clear();
    m_receive_request_totals.clear();
    m_receive_request_txids_by_address.clear();
    m_receive_request_unconfirmed_txids.clear();
    ++m_receive_payment_revision;
    m_receive_payment_poll_timer.stop();
    refreshReceiveRequestPayments();
}

bool WalletQmlModel::removeReceiveRequest(const QString& request_id)
{
    if (!m_wallet || !m_receive_requests) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return false;
    const CTxDestination destination = DecodeDestination(entry->recipient.address);
    if (!IsValidDestination(destination)) return false;
    if (!m_wallet->setAddressReceiveRequest(destination, request_id.toStdString(), std::string{})) {
        return false;
    }
    m_receive_requests->removeByRequestId(request_id);
    m_receive_request_addresses = m_receive_requests->requestAddresses();
    refreshReceiveRequestPayments();
    return true;
}

bool WalletQmlModel::loadPaymentRequest(const QString& request_id)
{
    if (!m_current_payment_request || !m_receive_requests) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return false;
    if (entry->id < 0 || entry->id > std::numeric_limits<unsigned int>::max()) return false;

    const CTxDestination destination = DecodeDestination(entry->recipient.address);
    if (!IsValidDestination(destination)) return false;

    m_current_payment_request->clear();
    m_current_payment_request->setDestination(destination);
    m_current_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_current_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_current_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_current_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_current_payment_request->setId(static_cast<unsigned int>(entry->id));
    m_current_payment_request->setCreated(entry->date);
    m_current_payment_request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(m_current_payment_request->address()));
    m_current_payment_request->setPaymentReceived(entry->payment_received);
    m_current_payment_request->setIsEditing(false);
    return true;
}

bool WalletQmlModel::loadPaymentRequestDetail(const QString& request_id)
{
    if (!m_detail_payment_request || !m_receive_requests) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return false;
    if (entry->id < 0 || entry->id > std::numeric_limits<unsigned int>::max()) return false;

    const CTxDestination destination = DecodeDestination(entry->recipient.address);
    if (!IsValidDestination(destination)) return false;

    m_detail_payment_request->clear();
    m_detail_payment_request->setDestination(destination);
    m_detail_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_detail_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_detail_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_detail_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_detail_payment_request->setId(static_cast<unsigned int>(entry->id));
    m_detail_payment_request->setCreated(entry->date);
    m_detail_payment_request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(m_detail_payment_request->address()));
    m_detail_payment_request->setPaymentReceived(entry->payment_received);
    m_detail_payment_request->setIsEditing(false);
    return true;
}

bool WalletQmlModel::updatePaymentRequest(const QString& request_id, qint64 amount,
                                        const QString& label, const QString& message, const QString& note)
{
    if (!m_wallet || !m_receive_requests || !MoneyRange(amount)) return false;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry || entry->id <= 0 || entry->id > std::numeric_limits<unsigned int>::max()) return false;
    PaymentRequest edited;
    edited.setId(static_cast<unsigned int>(entry->id));
    edited.setDestination(DecodeDestination(entry->recipient.address));
    edited.setCreated(entry->date);
    edited.amount()->setSatoshi(amount);
    edited.setLabel(label);
    edited.setMessage(message);
    edited.setNoteSelf(note);
    if (!savePaymentRequest(&edited)) return false;
    // The Receive draft is independent of the modal's saved request.
    if (m_current_payment_request->id() == request_id) loadPaymentRequest(request_id);
    return true;
}

void WalletQmlModel::refreshReceiveRequestPayments()
{
    if (!m_wallet || !m_receive_requests) return;
    if (m_receive_reconciliation_thread) {
        m_receive_reconciliation_requested = true;
        return;
    }
    if (m_receive_requests->count() == 0) {
        m_receive_request_payments.clear();
        m_receive_request_totals.clear();
        m_receive_request_txids_by_address.clear();
        m_receive_request_unconfirmed_txids.clear();
        ++m_receive_payment_revision;
        m_receive_payment_poll_timer.stop();
        updateReceivedPaymentRequestAmounts();
        return;
    }
    const auto wallet = m_wallet;
    const auto addresses = m_receive_request_addresses;
    m_receive_reconciliation_thread = QThread::create([this, wallet, addresses] {
        ReceiveRequestPaymentScan scan;
        for (const auto& tx : wallet->getWalletTxs()) {
            std::set<QString> observed;
            bool unconfirmed{false};
            auto received = ScanReceiveRequestTransaction(tx, *wallet, addresses, observed, unconfirmed);
            scan.observed_addresses.insert(observed.begin(), observed.end());
            if (!received.empty()) {
                const Txid txid = tx.tx->GetHash();
                if (unconfirmed) scan.unconfirmed_txids.insert(txid);
                for (const auto& [address, amount] : received) {
                    scan.totals[address] += amount;
                    scan.txids_by_address[address].insert(txid);
                }
                scan.payments.emplace(txid, std::move(received));
            }
        }
        QMetaObject::invokeMethod(this, [this, scan = std::move(scan)]() mutable {
            m_receive_reconciliation_thread->wait();
            delete m_receive_reconciliation_thread;
            m_receive_reconciliation_thread = nullptr;
            if (m_receive_reconciliation_requested) {
                m_receive_reconciliation_requested = false;
                m_receive_reconciliation_updates.clear();
                refreshReceiveRequestPayments();
                if (!receiveRequestReconciliationPending()) Q_EMIT receiveRequestReconciliationPendingChanged();
                return;
            }
            m_receive_reconciliation_applying = true;
            m_receive_request_payments = std::move(scan.payments);
            m_receive_request_totals = std::move(scan.totals);
            m_receive_request_txids_by_address = std::move(scan.txids_by_address);
            m_receive_request_unconfirmed_txids = std::move(scan.unconfirmed_txids);
            ++m_receive_payment_revision;
            for (const QString& address : scan.observed_addresses) markReceiveRequestPayment(address);
            for (const auto& [txid, deleted] : m_receive_reconciliation_updates) {
                removeReceiveRequestPayment(txid);
                if (!deleted) {
                    interfaces::WalletTxStatus status{};
                    interfaces::WalletOrderForm order_form;
                    bool in_mempool{false};
                    int num_blocks{0};
                    recordReceiveRequestPayment(m_wallet->getWalletTxDetails(txid, status, order_form,
                                                                             in_mempool, num_blocks));
                    recheckReceiveRequestPayments(txid);
                }
            }
            m_receive_reconciliation_updates.clear();
            updateReceivedPaymentRequestAmounts();
            updateReceivePaymentPollTimer();
            m_receive_reconciliation_applying = false;
            Q_EMIT receiveRequestReconciliationPendingChanged();
        }, Qt::QueuedConnection);
    });
    m_receive_reconciliation_thread->start();
    Q_EMIT receiveRequestReconciliationPendingChanged();
}

void WalletQmlModel::recordReceiveRequestPayment(const interfaces::WalletTx& tx)
{
    if (!m_wallet || !m_receive_requests || !tx.tx) return;
    const Txid txid = tx.tx->GetHash();
    removeReceiveRequestPayment(txid);
    for (size_t i = 0; i < tx.tx->vout.size(); ++i) {
        const auto& output = tx.tx->vout[i];
        if (output.nValue <= 0 || i >= tx.txout_is_mine.size() || !tx.txout_is_mine[i]) continue;
        CTxDestination destination;
        if (!ExtractDestination(output.scriptPubKey, destination)) continue;
        const QString address = QString::fromStdString(EncodeDestination(destination));
        if (m_receiving_address && address == m_receiving_address->address()) {
            m_receiving_address->setPaymentReceived(true);
        }
        markReceiveRequestPayment(address);
    }
    std::set<QString> observed;
    bool unconfirmed{false};
    auto received = ScanReceiveRequestTransaction(tx, *m_wallet, m_receive_request_addresses, observed, unconfirmed);
    if (!received.empty()) {
        if (unconfirmed) m_receive_request_unconfirmed_txids.insert(txid);
        ++m_receive_payment_revision;
        for (const auto& [address, amount] : received) {
            m_receive_request_totals[address] += amount;
            m_receive_request_txids_by_address[address].insert(txid);
        }
        m_receive_request_payments.emplace(txid, std::move(received));
        updateReceivePaymentPollTimer();
    }
}

void WalletQmlModel::markReceiveRequestPayment(const QString& address)
{
    if (!m_receive_requests || !m_wallet) return;
    const auto requests = m_receive_requests->entriesForAddress(address);
    for (auto entry : requests) {
        if (entry.payment_received) continue;
        entry.payment_received = true;
        const QString id = QString::number(entry.id);
        const auto destination = DecodeDestination(entry.recipient.address);
        // Keep the in-memory lock even if persistence fails.
        m_wallet->setAddressReceiveRequest(destination, id.toStdString(),
            ReceiveRequestHistoryModel::SerializeEntry(entry));
        m_receive_requests->prependOrReplace(entry);
        for (auto* held : {m_current_payment_request, m_detail_payment_request}) {
            if (!held || held->id() != id) continue;
            held->setPaymentReceived(true);
            held->amount()->setSatoshi(entry.recipient.amount);
            held->setLabel(QString::fromStdString(entry.recipient.label));
            held->setMessage(QString::fromStdString(entry.recipient.message));
        }
    }
}

void WalletQmlModel::removeReceiveRequestPayment(const Txid& txid)
{
    m_receive_request_unconfirmed_txids.erase(txid);
    ++m_receive_payment_revision;
    updateReceivePaymentPollTimer();
    const auto it = m_receive_request_payments.find(txid);
    if (it == m_receive_request_payments.end()) return;
    for (const auto& [address, amount] : it->second) {
        auto txids = m_receive_request_txids_by_address.find(address);
        if (txids != m_receive_request_txids_by_address.end()) {
            txids->second.erase(txid);
            if (txids->second.empty()) m_receive_request_txids_by_address.erase(txids);
        }
        auto total = m_receive_request_totals.find(address);
        if (total != m_receive_request_totals.end()) {
            total->second -= amount;
            if (total->second == 0) m_receive_request_totals.erase(total);
        }
    }
    m_receive_request_payments.erase(it);
}

void WalletQmlModel::updateReceivePaymentPollTimer()
{
    if (m_receive_request_unconfirmed_txids.empty()) m_receive_payment_poll_timer.stop();
    else if (!m_receive_payment_poll_timer.isActive()) m_receive_payment_poll_timer.start();
}

void WalletQmlModel::pollUnconfirmedReceiveRequestPayments()
{
    if (!m_wallet || m_receive_payment_poll_thread || receiveRequestReconciliationPending() ||
        m_receive_request_notifications_pending.load() > 0) return;
    const auto wallet = m_wallet;
    const auto txids = m_receive_request_unconfirmed_txids;
    const auto revision = m_receive_payment_revision;
    m_receive_payment_poll_thread = QThread::create([this, wallet, txids, revision] {
        UnconfirmedReceiveRequestPaymentCheck check;
        for (const Txid& txid : txids) {
            interfaces::WalletTxStatus status{};
            interfaces::WalletOrderForm order_form;
            bool in_mempool{false};
            int num_blocks{0};
            const auto tx = wallet->getWalletTxDetails(txid, status, order_form, in_mempool, num_blocks);
            const auto replacement = tx.value_map.find("replaced_by_txid");
            const bool replaced = replacement != tx.value_map.end() && !replacement->second.empty();
            if (!tx.tx || status.depth_in_main_chain < 0 || status.is_abandoned ||
                (status.depth_in_main_chain == 0 && (!in_mempool || replaced))) check.inactive.insert(txid);
            else if (status.depth_in_main_chain > 0) check.confirmed.insert(txid);
        }
        QMetaObject::invokeMethod(this, [this, revision, check = std::move(check)] {
            m_receive_payment_poll_thread->wait();
            delete m_receive_payment_poll_thread;
            m_receive_payment_poll_thread = nullptr;
            if (revision != m_receive_payment_revision || receiveRequestReconciliationPending() ||
                m_receive_request_notifications_pending.load() > 0) return;
            for (const Txid& txid : check.inactive) removeReceiveRequestPayment(txid);
            for (const Txid& txid : check.confirmed) m_receive_request_unconfirmed_txids.erase(txid);
            updateReceivePaymentPollTimer();
            if (!check.inactive.empty()) updateReceivedPaymentRequestAmounts();
        }, Qt::QueuedConnection);
    });
    m_receive_payment_poll_thread->start();
}

void WalletQmlModel::recheckReceiveRequestPayments(const Txid& changed_txid)
{
    const auto changed = m_receive_request_payments.find(changed_txid);
    if (changed == m_receive_request_payments.end()) return;
    std::set<Txid> related;
    for (const auto& [address, amount] : changed->second) {
        const auto it = m_receive_request_txids_by_address.find(address);
        if (it != m_receive_request_txids_by_address.end()) related.insert(it->second.begin(), it->second.end());
    }
    related.erase(changed_txid);
    for (const Txid& txid : related) {
        interfaces::WalletTxStatus status{};
        interfaces::WalletOrderForm order_form;
        bool in_mempool{false};
        int num_blocks{0};
        const auto tx = m_wallet->getWalletTxDetails(txid, status, order_form, in_mempool, num_blocks);
        const auto replacement = tx.value_map.find("replaced_by_txid");
        const bool replaced = replacement != tx.value_map.end() && !replacement->second.empty();
        if (status.depth_in_main_chain < 0 || status.is_abandoned ||
            (status.depth_in_main_chain == 0 && (!in_mempool || replaced))) removeReceiveRequestPayment(txid);
    }
}

CAmount WalletQmlModel::receivedPaymentRequestAmount(const QString& address) const
{
    const auto it = m_receive_request_totals.find(address);
    return it == m_receive_request_totals.end() ? 0 : it->second;
}

void WalletQmlModel::updateReceivedPaymentRequestAmounts()
{
    for (auto* request : {m_current_payment_request, m_detail_payment_request}) {
        if (request && !request->id().isEmpty()) {
            request->setReceivedAmountSatoshi(receivedPaymentRequestAmount(request->address()));
        }
    }
}

void WalletQmlModel::usePaymentRequestAsTemplate(const QString& request_id)
{
    if (!m_current_payment_request || !m_receive_requests) return;
    const auto entry = m_receive_requests->entryById(request_id);
    if (!entry) return;
    const CTxDestination destination = DecodeDestination(entry->recipient.address);

    // A repeated request starts with a fresh receiving address, allocated by
    // the Receive page (including its normal wallet-unlock flow).
    m_receiving_address->clear();
    QSettings settings;
    settings.remove(persistedReceiveAddressTypeKey() + QStringLiteral("/address"));
    m_current_payment_request->clear();
    m_current_payment_request->setLabel(QString::fromStdString(entry->recipient.label));
    m_current_payment_request->setMessage(QString::fromStdString(entry->recipient.message));
    m_current_payment_request->setNoteSelf(QString::fromStdString(entry->recipient.noteSelf));
    m_current_payment_request->amount()->setSatoshi(entry->recipient.amount);
    m_current_payment_request->setAddressType(OutputTypeIdFromDestination(destination));

    // Toggle isEditing to re-trigger QML input sync with populated values
    m_current_payment_request->setIsEditing(false);
    m_current_payment_request->setIsEditing(true);
}

unsigned int WalletQmlModel::nextPaymentRequestId() const
{
    if (!m_receive_requests) return 1;
    const int64_t max_id = m_receive_requests->maxId();
    if (max_id <= 0 || max_id >= std::numeric_limits<unsigned int>::max() - 1) return 1;
    return static_cast<unsigned int>(max_id + 1);
}

std::set<interfaces::WalletTx> WalletQmlModel::getWalletTxs() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getWalletTxs();
}

interfaces::WalletTx WalletQmlModel::getWalletTx(const uint256& hash) const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getWalletTx(Txid::FromUint256(hash));
}

bool WalletQmlModel::tryGetTxStatus(const uint256& txid,
                                    interfaces::WalletTxStatus& tx_status,
                                    int& num_blocks,
                                    int64_t& block_time) const
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->tryGetTxStatus(Txid::FromUint256(txid), tx_status, num_blocks, block_time);
}

QString WalletQmlModel::getAddressLabel(const QString& address) const
{
    if (!m_wallet || address.isEmpty()) {
        return {};
    }

    const CTxDestination destination = DecodeDestination(address.toStdString());
    if (!IsValidDestination(destination)) {
        return {};
    }

    std::string label;
    if (m_wallet->getAddress(destination, &label, nullptr)) {
        if (!label.empty()) {
            return QString::fromStdString(label);
        }
    }

    for (const interfaces::WalletAddress& wallet_address : getAddresses()) {
        if (wallet_address.dest == destination) {
            return QString::fromStdString(wallet_address.name);
        }
    }

    return {};
}

bool WalletQmlModel::setAddressLabel(const QString& address, const QString& label)
{
    if (!writeAddressBookLabel(address, label)) {
        return false;
    }

    // Address labels and request notes are private; public request fields
    // must remain unchanged when an address is renamed.
    syncPaymentRequestNoteToAddress(address, label);
    return true;
}

bool WalletQmlModel::writeAddressBookLabel(const QString& address, const QString& label)
{
    if (!m_wallet || address.isEmpty()) {
        return false;
    }

    const CTxDestination destination{DecodeDestination(address.toStdString())};
    if (!IsValidDestination(destination)) {
        return false;
    }

    wallet::AddressPurpose purpose{wallet::AddressPurpose::RECEIVE};
    if (!m_wallet->getAddress(destination, nullptr, &purpose)) {
        return false;
    }

    return m_wallet->setAddressBook(destination, label.toStdString(), purpose);
}

void WalletQmlModel::syncPaymentRequestNoteToAddress(const QString& address, const QString& label)
{
    if (!m_wallet || !m_receive_requests) {
        return;
    }

    const CTxDestination destination{DecodeDestination(address.toStdString())};
    if (!IsValidDestination(destination)) {
        return;
    }

    refreshReceiveRequestPayments();
    for (QmlRecentRequestEntry entry : m_receive_requests->entriesForAddress(address)) {
        // Already in step: nothing to re-store.
        if (QString::fromStdString(entry.recipient.noteSelf) == label) {
            continue;
        }
        entry.recipient.noteSelf = label.toStdString();
        const QString request_id{QString::number(entry.id)};
        // Only mirror the new label into the in-memory models once it is
        // persisted; a failed write must not leave them showing a label the
        // wallet does not hold.
        if (!m_wallet->setAddressReceiveRequest(destination, request_id.toStdString(),
                                                ReceiveRequestHistoryModel::SerializeEntry(entry))) {
            continue;
        }
        m_receive_requests->prependOrReplace(entry);
        // A detail page or editor holding this request would otherwise keep
        // showing its stale copy until reloaded. Skip the editor mid-edit so
        // an unsaved draft is not clobbered.
        if (m_detail_payment_request && m_detail_payment_request->id() == request_id) {
            m_detail_payment_request->setNoteSelf(label);
        }
        if (m_current_payment_request && m_current_payment_request->id() == request_id &&
            !m_current_payment_request->isEditing()) {
            m_current_payment_request->setNoteSelf(label);
        }
    }
}

std::vector<interfaces::WalletAddress> WalletQmlModel::getAddresses() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->getAddresses();
}

std::map<QString, CAmount> WalletQmlModel::addressBalances() const
{
    std::map<QString, CAmount> balances;
    if (!m_wallet) {
        return balances;
    }

    for (const auto& coins_entry : m_wallet->listCoins()) {
        for (const auto& [outpoint, tx_out] : coins_entry.second) {
            CTxDestination destination;
            if (!ExtractDestination(tx_out.txout.scriptPubKey, destination))
                continue;

            const QString address{QString::fromStdString(EncodeDestination(destination))};
            if (address.isEmpty())
                continue;

            balances[address] += tx_out.txout.nValue;
        }
    }

    return balances;
}

std::set<QString> WalletQmlModel::usedAddresses() const
{
    std::set<QString> addresses;

    if (!m_wallet)
        return addresses;

    std::set<QString> receive_addresses;
    for (const interfaces::WalletAddress& wallet_address : getAddresses()) {
        if (wallet_address.purpose != wallet::AddressPurpose::RECEIVE || !wallet_address.is_mine) {
            continue;
        }

        const QString address{QString::fromStdString(EncodeDestination(wallet_address.dest))};
        if (!address.isEmpty()) {
            receive_addresses.insert(address);
        }
    }

    for (const interfaces::WalletTx& wallet_tx : m_wallet->getWalletTxs()) {
        for (size_t i{0}; i < wallet_tx.txout_address.size(); ++i) {
            if (i >= wallet_tx.txout_address_is_mine.size() || !wallet_tx.txout_address_is_mine[i])
                continue;

            if (i < wallet_tx.txout_is_change.size() && wallet_tx.txout_is_change[i])
                continue;

            const QString address{QString::fromStdString(EncodeDestination(wallet_tx.txout_address[i]))};
            if (!address.isEmpty() && receive_addresses.count(address) > 0)
                addresses.insert(address);
        }
    }

    return addresses;
}

std::set<QString> WalletQmlModel::changeAddresses() const
{
    std::set<QString> addresses;
    if (!m_wallet) {
        return addresses;
    }

    std::set<COutPoint> change_outpoints;
    for (const interfaces::WalletTx& wallet_tx : m_wallet->getWalletTxs()) {
        if (!wallet_tx.tx)
            continue;

        const Txid txid{wallet_tx.tx->GetHash()};
        for (size_t i{0}; i < wallet_tx.txout_is_change.size(); ++i) {
            if (!wallet_tx.txout_is_change[i])
                continue;

            change_outpoints.insert(COutPoint{txid, static_cast<uint32_t>(i)});
        }
    }

    for (const auto& coins_entry : m_wallet->listCoins()) {
        for (const auto& [outpoint, tx_out] : coins_entry.second) {
            if (tx_out.txout.nValue <= 0)
                continue;

            if (change_outpoints.count(outpoint) > 0) {
                CTxDestination destination;
                if (!ExtractDestination(tx_out.txout.scriptPubKey, destination))
                    continue;

                const QString address{QString::fromStdString(EncodeDestination(destination))};
                if (address.isEmpty())
                    continue;

                addresses.insert(address);
            }
        }
    }

    return addresses;
}

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleTransactionChanged(TransactionChangedFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    return m_wallet->handleTransactionChanged([fn = std::move(fn)](const Txid& txid, ChangeType status) {
        fn(txid.ToUint256(), status);
    });
}

void WalletQmlModel::scheduleFeeEstimates()
{
    if (m_updating_maximum || m_fee_estimation_timer == nullptr) {
        return;
    }
    m_fee_estimation_timer->stop();

    // Until a destination is entered, show the gross remainder. A valid
    // address enables the sendall preview, which replaces it with the net amount.
    if (m_wallet && m_maximum_recipient && m_maximum_recipient->address()->address().isEmpty()) {
        wallet::CCoinControl control{m_coin_control};
        ApplySelectedInputsPolicy(control);
        CAmount remainder = m_wallet->getAvailableBalance(control);
        for (const auto* recipient : m_send_recipients->recipients()) {
            if (recipient != m_maximum_recipient) remainder -= recipient->cAmount();
        }
        QScopedValueRollback<bool> updating{m_updating_maximum, true};
        m_maximum_recipient->amount()->setSatoshi(std::max(CAmount{0}, remainder));
    }

    if (!m_wallet || !m_send_recipients || !BuildRecipients(*m_send_recipients, m_maximum_recipient).has_value()) {
        clearFeeEstimates();
        return;
    }

    ++m_fee_estimate_request_id;

    bool estimates_changed{!m_fee_estimates.isEmpty()};
    bool custom_estimate_changed{m_custom_fee_estimate.has_value()};
    if (estimates_changed) {
        m_fee_estimates.clear();
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate.reset();
    }
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }

    bool pending_changed{!m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = true;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }

    m_fee_estimation_timer->start();
}

void WalletQmlModel::requestFeeEstimatesNow()
{
    if (!m_wallet || !m_send_recipients) {
        clearFeeEstimates();
        return;
    }

    const auto recipients = BuildRecipients(*m_send_recipients, m_maximum_recipient);
    if (!recipients.has_value()) {
        clearFeeEstimates();
        return;
    }

    const int remainder_index = m_send_recipients->recipients().indexOf(m_maximum_recipient);
    const quint64 request_id = ++m_fee_estimate_request_id;
    const wallet::CCoinControl base_coin_control{m_coin_control};
    const OutputType preview_change_type{base_coin_control.m_change_type.value_or(m_wallet->getDefaultAddressType())};
    const bool custom_fee_enabled{m_custom_fee_enabled};
    const std::optional<CAmount> custom_fee_rate_per_kvb{
        ParseCustomFeeRatePerKvB(m_custom_fee_rate)};
    interfaces::Wallet* const wallet = m_wallet.get();

    if (!m_fee_estimate_pending) {
        m_fee_estimate_pending = true;
        Q_EMIT feeEstimatePendingChanged();
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }

    QTimer::singleShot(0, m_fee_estimation_worker, [this, request_id, remainder_index, recipients = *recipients, base_coin_control, preview_change_type, custom_fee_enabled, custom_fee_rate_per_kvb, wallet]() {
        QHash<unsigned int, SendFeePreview> estimates;
        std::optional<SendFeePreview> custom_estimate;

        for (const unsigned int target : STANDARD_FEE_TARGETS) {
            if (const auto estimate = EstimatePreviewFee(*wallet,
                                                         recipients,
                                                         base_coin_control,
                                                         preview_change_type,
                                                         target, remainder_index)) {
                estimates.insert(target, *estimate);
            }
        }

        if (custom_fee_enabled && custom_fee_rate_per_kvb.has_value()) {
            if (const auto estimate = EstimateCustomPreviewFee(*wallet,
                                                               recipients,
                                                               base_coin_control,
                                                               preview_change_type,
                                                               *custom_fee_rate_per_kvb, remainder_index)) {
                custom_estimate = *estimate;
            }
        }

        QMetaObject::invokeMethod(this, [this, estimates, custom_estimate, request_id]() {
            applyFeeEstimates(estimates, custom_estimate, request_id);
        }, Qt::QueuedConnection);
    });
}

void WalletQmlModel::applyFeeEstimates(const QHash<unsigned int, SendFeePreview>& estimates,
                                       const std::optional<SendFeePreview>& custom_estimate,
                                       const quint64 request_id)
{
    if (request_id != m_fee_estimate_request_id) {
        return;
    }

    bool estimates_changed{m_fee_estimates != estimates};
    bool custom_estimate_changed{m_custom_fee_estimate != custom_estimate};
    if (estimates_changed) {
        m_fee_estimates = estimates;
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate = custom_estimate;
    }
    updateMaximumAmount();
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

void WalletQmlModel::clearFeeEstimates()
{
    ++m_fee_estimate_request_id;

    bool estimates_changed{!m_fee_estimates.isEmpty()};
    bool custom_estimate_changed{m_custom_fee_estimate.has_value()};
    if (estimates_changed) {
        m_fee_estimates.clear();
    }
    if (custom_estimate_changed) {
        m_custom_fee_estimate.reset();
    }
    if (estimates_changed || custom_estimate_changed) {
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }

    bool pending_changed{m_fee_estimate_pending};
    if (pending_changed) {
        m_fee_estimate_pending = false;
        Q_EMIT feeEstimatePendingChanged();
    }

    if (estimates_changed || custom_estimate_changed || pending_changed) {
        ++m_fee_estimate_revision;
        Q_EMIT feeEstimateRevisionChanged();
    }
}

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleStatusChanged(StatusChangedFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    return m_wallet->handleStatusChanged(fn);
}

std::unique_ptr<interfaces::Handler> WalletQmlModel::handleUnload(UnloadFn fn)
{
    if (!m_wallet) {
        return nullptr;
    }
    return m_wallet->handleUnload(fn);
}

bool WalletQmlModel::prepareTransaction()
{
    return prepareTransactionInternal(std::nullopt);
}

bool WalletQmlModel::prepareTransactionWithPassphrase(const QString& passphrase)
{
    return prepareTransactionInternal(std::optional<SecureString>{QmlUtil::SecureStringFromQString(passphrase)});
}

bool WalletQmlModel::prepareTransactionInternal(std::optional<SecureString> passphrase)
{
    clearTransactionStatus();
    if (!m_wallet || !m_send_recipients || m_send_recipients->recipients().empty()) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        setTransactionStatus(tr("Enter at least one valid recipient to continue."));
        return false;
    }

    if (!m_send_recipients->allValid()) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        setTransactionStatus(m_send_recipients->validationError());
        return false;
    }

    const auto vec_send = BuildRecipients(*m_send_recipients, m_maximum_recipient);
    if (!vec_send.has_value()) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        setTransactionStatus(tr("Enter at least one valid recipient to continue."));
        return false;
    }

    if (!m_wallet->privateKeysDisabled() && m_wallet->isCrypted() && m_wallet->isLocked() && !passphrase.has_value()) {
        refreshSecurityState();
        setTransactionStatus(tr("Enter your wallet password to prepare this transaction."), true);
        return false;
    }

    bool relock{false};
    if (!unlockForAction(passphrase, relock)) {
        return false;
    }
    WalletRelockGuard relock_guard{*m_wallet, [this] { refreshSecurityState(); }, relock};

    CAmount total = 0;
    for (const auto& recipient : *vec_send) {
        total += recipient.nAmount;
    }

    wallet::CCoinControl coin_control{m_coin_control};
    ApplySelectedInputsPolicy(coin_control);
    if (m_custom_fee_enabled) {
        const auto custom_fee_rate_per_kvb = ParseCustomFeeRatePerKvB(m_custom_fee_rate);
        if (!custom_fee_rate_per_kvb.has_value()) {
            return false;
        }
        coin_control.m_confirm_target.reset();
        coin_control.m_feerate = CFeeRate{*custom_fee_rate_per_kvb};
        coin_control.fOverrideFeeRate = true;
    } else {
        coin_control.m_feerate.reset();
        if (!coin_control.m_confirm_target.has_value()) {
            coin_control.m_confirm_target = DEFAULT_STANDARD_FEE_TARGET;
        }
        ApplyRegtestStaticFeeOverride(coin_control);
    }

    CAmount balance = m_wallet->getAvailableBalance(coin_control);
    if (!m_maximum_recipient && balance < total) {
        relock_guard.relock();
        setTransactionStatus(coin_control.HasSelected()
            ? tr("Selected inputs do not cover the amount plus fee")
            : tr("The wallet does not have enough balance for this transaction."));
        return false;
    }

    const bool sign = !m_wallet->privateKeysDisabled();
    const auto result = m_maximum_recipient
        ? CreateSendAllTransaction(*m_wallet, *vec_send, coin_control, m_send_recipients->recipients().indexOf(m_maximum_recipient), sign)
        : m_wallet->createTransaction(*vec_send, coin_control, sign, /*change_pos=*/std::nullopt);
    if (result) {
        const CTransactionRef& newTx = result->tx;
        if (m_maximum_recipient) {
            // Fee estimates can change between editing and opening review.
            // Show the recipient's actual output amount in the review as well.
            QScopedValueRollback<bool> updating{m_updating_maximum, true};
            const int index = m_send_recipients->recipients().indexOf(m_maximum_recipient);
            m_maximum_recipient->amount()->setSatoshi(newTx->vout[index].nValue);
        }
        if (m_current_transaction) {
            delete m_current_transaction;
        }
        m_current_transaction = new WalletQmlModelTransaction(m_send_recipients, this);
        m_current_psbt.reset();
        m_current_transaction_source = CurrentTransactionSource::SendDraft;
        m_current_transaction_sweeps_wallet = !result->change_pos && TransactionSweepsAvailableFunds(*m_wallet, *newTx, coin_control);
        m_current_transaction_can_send = true;
        m_current_transaction_can_broadcast = false;
        m_current_transaction_review_message.clear();
        m_current_transaction->setWtx(newTx);
        if (!m_current_transaction->captureReviewedRecipients(*m_send_recipients)) {
            delete m_current_transaction;
            m_current_transaction = nullptr;
            m_current_transaction_can_send = false;
            setTransactionStatus(tr("Unable to match the prepared transaction's recipients."));
            return false;
        }
        m_current_transaction->setTransactionFee(result->fee);
        m_current_transaction->setReviewFeeDetails(feeTargetBlocks(), estimatedFeeRate());
        if (m_maximum_recipient) {
            m_current_transaction->reassignAmounts(
                result->change_pos ? static_cast<int>(*result->change_pos) : -1);
        }
        m_current_transaction->setDisplayUnit(m_display_unit);
        relock_guard.relock();
        Q_EMIT currentTransactionChanged();
        return true;
    }

    relock_guard.relock();
    setTransactionStatus(LocalizedString(util::ErrorString(result)));
    return false;
}

void WalletQmlModel::approveExternalSignerTransaction()
{
    if (!m_wallet || !m_current_transaction || !m_wallet->hasExternalSigner()) {
        Q_EMIT externalSignerApprovalFailed(tr("External signer not available."), true);
        return;
    }

    CTransactionRef& current_tx = m_current_transaction->getWtx();
    if (!current_tx) {
        Q_EMIT externalSignerApprovalFailed(tr("Couldn't prepare transaction for external signing."), false);
        return;
    }

    try {
        CMutableTransaction empty_tx;
        PartiallySignedTransaction psbtx{empty_tx};
        if (m_current_psbt) {
            psbtx = *m_current_psbt;
        } else {
            CMutableTransaction unsigned_tx{*current_tx};
            ClearTransactionInputScripts(unsigned_tx);
            psbtx = PartiallySignedTransaction{unsigned_tx};
        }

        bool complete{false};
        const auto draft_err = m_wallet->fillPSBT({.sign = false, .bip32_derivs = true},
            /*n_signed=*/nullptr, psbtx, complete);
        if (draft_err) {
            Q_EMIT externalSignerApprovalFailed(
                PsbtQmlModel::PsbtErrorText(*draft_err),
                *draft_err == common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND);
            return;
        }

        if (!complete) {
            const auto sign_err = m_wallet->fillPSBT({.sign = true, .bip32_derivs = true},
                /*n_signed=*/nullptr, psbtx, complete);
            if (sign_err) {
                const bool signer_not_found = *sign_err == common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND;
                QString message;
                switch (*sign_err) {
                case common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND:
                    message = tr("External signer not found. Connect one device and try again.");
                    break;
                case common::PSBTError::EXTERNAL_SIGNER_FAILED:
                    message = tr("External signer failed to sign. Try again.");
                    break;
                default:
                    message = PsbtQmlModel::PsbtErrorText(*sign_err);
                    break;
                }
                Q_EMIT externalSignerApprovalFailed(message, signer_not_found);
                return;
            }
        }

        CMutableTransaction signed_tx;
        if (!FinalizeAndExtractPSBT(psbtx, signed_tx)) {
            m_current_psbt = std::make_unique<PartiallySignedTransaction>(std::move(psbtx));
            m_current_transaction_can_send = false;
            m_current_transaction_can_broadcast = false;
            m_current_transaction_review_message = tr("Signed on external signer. More signatures are required.");
            Q_EMIT currentTransactionChanged();
            Q_EMIT externalSignerApprovalPartiallySucceeded();
            return;
        }

        m_current_psbt = std::make_unique<PartiallySignedTransaction>(std::move(psbtx));
        m_current_transaction->setWtx(MakeTransactionRef(std::move(signed_tx)));
        m_current_transaction_can_send = true;
        m_current_transaction_can_broadcast = false;
        m_current_transaction_review_message.clear();
        Q_EMIT currentTransactionChanged();
        Q_EMIT externalSignerApprovalSucceeded();
    } catch (const std::runtime_error& err) {
        Q_EMIT externalSignerApprovalFailed(QString::fromStdString(err.what()), false);
    }
}

bool WalletQmlModel::sendTransaction()
{
    return sendTransactionInternal();
}

bool WalletQmlModel::sendTransactionWithPassphrase(const QString& passphrase)
{
    return sendTransactionInternal(std::optional<SecureString>{QmlUtil::SecureStringFromQString(passphrase)});
}

bool WalletQmlModel::broadcastCurrentTransaction()
{
    clearTransactionStatus();
    if (!m_node || !m_current_transaction || !m_current_psbt || !m_current_transaction_can_broadcast) {
        setTransactionStatus(tr("This transaction is not ready to broadcast."));
        return false;
    }

    PartiallySignedTransaction psbt{*m_current_psbt};
    const node::PSBTAnalysis analysis{node::AnalyzePSBT(psbt)};
    if (!analysis.fee || *analysis.fee < 0) {
        setTransactionStatus(tr("The transaction fee is missing or invalid."));
        return false;
    }

    CMutableTransaction mutable_tx;
    if (!FinalizeAndExtractPSBT(psbt, mutable_tx)) {
        setTransactionStatus(tr("This transaction is not fully signed."));
        return false;
    }

    const CTransactionRef tx{MakeTransactionRef(std::move(mutable_tx))};
    std::string error_string;
    const node::TransactionError error{
        m_node->broadcastTransaction(tx, node::DEFAULT_MAX_RAW_TX_FEE_RATE.GetFeePerK(), error_string)};
    if (error != node::TransactionError::OK) {
        QString message{QString::fromStdString(common::TransactionErrorString(error).translated)};
        if (!error_string.empty()) {
            message += QStringLiteral(": ") + QString::fromStdString(error_string);
        }
        setTransactionStatus(tr("Transaction broadcast failed: %1").arg(message));
        return false;
    }

    m_current_transaction->setWtx(tx);
    m_current_psbt.reset();
    m_current_transaction_source = CurrentTransactionSource::None;
    m_current_transaction_can_send = false;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message.clear();
    clearTransactionStatus();
    clearSelectedCoins();
    Q_EMIT currentTransactionChanged();
    return true;
}

bool WalletQmlModel::sendTransactionInternal(std::optional<SecureString> passphrase)
{
    clearTransactionStatus();
    if (!m_wallet || !m_current_transaction) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        setTransactionStatus(tr("Review a transaction before sending it."));
        return false;
    }

    if (m_current_psbt) {
        if (!m_current_transaction_can_send) {
            if (passphrase.has_value()) {
                QmlUtil::ClearSecureString(*passphrase);
                passphrase.reset();
            }
            setTransactionStatus(m_current_transaction_review_message.isEmpty()
                ? tr("This transaction cannot be sent from this wallet.")
                : m_current_transaction_review_message);
            return false;
        }

        PartiallySignedTransaction psbt{*m_current_psbt};
        CMutableTransaction mutable_tx;
        PartiallySignedTransaction finalized_psbt{psbt};
        bool complete{FinalizeAndExtractPSBT(finalized_psbt, mutable_tx)};

        if (!complete) {
            if (m_wallet->privateKeysDisabled() && !m_wallet->hasExternalSigner()) {
                if (passphrase.has_value()) {
                    QmlUtil::ClearSecureString(*passphrase);
                    passphrase.reset();
                }
                setTransactionStatus(tr("This wallet cannot sign transactions."));
                return false;
            }
            if (!m_wallet->privateKeysDisabled() && m_wallet->isCrypted() && m_wallet->isLocked() && !passphrase.has_value()) {
                setTransactionStatus(tr("Enter your wallet password to send this transaction."), true);
                return false;
            }

            bool relock{false};
            if (!unlockForAction(passphrase, relock)) {
                return false;
            }
            WalletRelockGuard relock_guard{*m_wallet, [this] { refreshSecurityState(); }, relock};

            size_t signed_inputs{0};
            const std::optional<common::PSBTError> fill_error{
                m_wallet->fillPSBT({.sign = true, .bip32_derivs = true}, &signed_inputs, psbt, complete)};
            if (fill_error) {
                setTransactionStatus(PsbtQmlModel::PsbtErrorText(*fill_error));
                return false;
            }
            if (!complete || !FinalizeAndExtractPSBT(psbt, mutable_tx)) {
                setTransactionStatus(tr("Only PSBTs this wallet can fully sign are supported right now."));
                return false;
            }
            relock_guard.relock();
        } else if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }

        const CTransactionRef signed_tx{MakeTransactionRef(std::move(mutable_tx))};
        interfaces::WalletValueMap value_map;
        interfaces::WalletOrderForm order_form;
        m_wallet->commitTransaction(signed_tx, value_map, order_form);
        saveSentRecipientLabels();
        m_current_transaction->setWtx(signed_tx);
        m_current_psbt.reset();
        m_current_transaction_source = CurrentTransactionSource::None;
        m_current_transaction_can_send = true;
        m_current_transaction_can_broadcast = false;
        m_current_transaction_review_message.clear();
        clearTransactionStatus();
        clearSelectedCoins();
        return true;
    }

    if (passphrase.has_value()) {
        QmlUtil::ClearSecureString(*passphrase);
        passphrase.reset();
    }

    CTransactionRef signed_tx = m_current_transaction->getWtx();
    if (!signed_tx) {
        setTransactionStatus(tr("Review a transaction before sending it."));
        return false;
    }

    if (m_wallet->privateKeysDisabled() && !m_wallet->hasExternalSigner()) {
        setTransactionStatus(tr("This wallet cannot sign transactions."));
        return false;
    }

    interfaces::WalletValueMap value_map;
    interfaces::WalletOrderForm order_form;
    m_wallet->commitTransaction(signed_tx, value_map, order_form);
    saveSentRecipientLabels();
    m_current_transaction_source = CurrentTransactionSource::None;

    clearTransactionStatus();
    clearSelectedCoins();
    return true;
}

void WalletQmlModel::saveSentRecipientLabels()
{
    if (m_current_transaction_source != CurrentTransactionSource::SendDraft) return;

    // Activity resolves outgoing notes through the address book, as the Qt
    // wallet does. Persist only user-provided notes after the transaction is
    // committed; do not relabel payment requests or write generated headings.
    for (auto it = m_current_transaction->recipientLabels().cbegin();
         it != m_current_transaction->recipientLabels().cend(); ++it) {
        const CTxDestination destination{DecodeDestination(it.key().toStdString())};
        if (!IsValidDestination(destination)) continue;
        std::string old_label;
        const bool exists = m_wallet->getAddress(destination, &old_label, nullptr);
        const std::string label{it.value().toStdString()};
        if (exists && old_label == label) continue;
        // Preserve an existing address's purpose, including owned recipients.
        m_wallet->setAddressBook(destination, label,
            exists ? std::nullopt : std::optional{wallet::AddressPurpose::SEND});
    }
}

WalletQmlModel::PsbtImportResult WalletQmlModel::importPsbtFromFile(const QString& path)
{
    clearTransactionStatus();
    if (!m_imported_psbt_model) {
        return PsbtImportResult::PsbtUnsupported;
    }

    CMutableTransaction empty_tx;
    PartiallySignedTransaction psbt{empty_tx};
    const QString load_err{PsbtQmlModel::LoadPsbtFromFile(path, psbt)};
    if (!load_err.isEmpty()) {
        m_imported_psbt_model->setError(load_err);
        return PsbtImportResult::PsbtUnsupported;
    }

    const auto unsigned_tx{psbt.GetUnsignedTx()};
    if (m_wallet && unsigned_tx) {
        const Txid psbt_txid{unsigned_tx->GetHash()};
        interfaces::WalletTxStatus tx_status;
        int num_blocks{0};
        int64_t block_time{0};
        if (m_wallet->tryGetTxStatus(psbt_txid, tx_status, num_blocks, block_time)) {
            m_imported_psbt_model->setMatchedTxid(QString::fromStdString(psbt_txid.GetHex()));
            return PsbtImportResult::TransactionAlreadyKnown;
        }
    }

    PsbtImportResult result{PsbtImportResult::PsbtUnsupported};
    QString reason;
    if (tryImportPsbtToReview(psbt, result, reason)) {
        m_imported_psbt_model->clear();
        return result;
    }

    m_imported_psbt_model->setError(reason.isEmpty() ? tr("This PSBT is not supported yet.") : reason);
    return PsbtImportResult::PsbtUnsupported;
}

QString WalletQmlModel::saveCurrentTransactionAsPsbt(const QString& path)
{
    if (!m_wallet || !m_current_transaction) {
        return tr("No transaction is prepared.");
    }
    CTransactionRef& current_tx = m_current_transaction->getWtx();
    if (!current_tx) {
        return tr("No transaction is prepared.");
    }

    CMutableTransaction empty_tx;
    PartiallySignedTransaction psbtx{empty_tx};
    try {
        if (m_current_psbt) {
            psbtx = *m_current_psbt;
            if (m_current_transaction_source == CurrentTransactionSource::ImportedPsbt) {
                return PsbtQmlModel::SavePsbtToFile(psbtx, path);
            }
            if (!m_current_transaction_can_send) {
                return PsbtQmlModel::SavePsbtToFile(psbtx, path);
            }
        } else {
            CMutableTransaction mtx{*current_tx};
            ClearTransactionInputScripts(mtx);
            psbtx = PartiallySignedTransaction{mtx};
        }

        bool complete{false};
        const auto err{m_wallet->fillPSBT({.sign = false, .bip32_derivs = true},
                                          /*n_signed=*/nullptr, psbtx, complete)};
        if (err) {
            return PsbtQmlModel::PsbtErrorText(*err);
        }
    } catch (const std::runtime_error& err) {
        return QString::fromStdString(err.what());
    }

    return PsbtQmlModel::SavePsbtToFile(psbtx, path);
}

bool WalletQmlModel::tryImportPsbtToReview(const PartiallySignedTransaction& psbt, PsbtImportResult& result, QString& reason)
{
    if (!m_wallet) {
        reason = tr("No wallet is loaded.");
        return false;
    }
    const auto unsigned_tx{psbt.GetUnsignedTx()};
    if (!unsigned_tx) {
        reason = tr("The PSBT does not contain an unsigned transaction.");
        return false;
    }
    if (unsigned_tx->vin.empty() || unsigned_tx->vout.empty()) {
        reason = tr("The PSBT has no inputs or outputs.");
        return false;
    }
    if (psbt.inputs.size() != unsigned_tx->vin.size() || psbt.outputs.size() != unsigned_tx->vout.size()) {
        reason = tr("The PSBT is malformed.");
        return false;
    }

    const bool spends_only_wallet_inputs{std::all_of(unsigned_tx->vin.begin(), unsigned_tx->vin.end(), [this](const CTxIn& input) {
        return m_wallet->txinIsMine(input);
    })};

    PartiallySignedTransaction analysis_psbt{psbt};
    bool complete{FinalizePSBT(analysis_psbt)};
    size_t could_sign{0};
    const std::optional<common::PSBTError> fill_error{
        m_wallet->fillPSBT({.sign = false, .bip32_derivs = false}, &could_sign, analysis_psbt, complete)};
    if (fill_error) {
        reason = PsbtQmlModel::PsbtErrorText(*fill_error);
        return false;
    }
    complete = FinalizePSBT(analysis_psbt);
    const auto analysis_tx{analysis_psbt.GetUnsignedTx()};
    if (!analysis_tx) {
        reason = tr("The PSBT does not contain an unsigned transaction.");
        return false;
    }
    std::optional<std::pair<int, int>> multisig_sig_info;
    if (!complete) {
        for (size_t i{0}; i < analysis_psbt.inputs.size(); ++i) {
            if (auto info{PsbtQmlModel::MultisigPsbtInputSigInfo(analysis_psbt, i)}) {
                multisig_sig_info = info;
                break;
            }
        }
    }

    const node::PSBTAnalysis analysis{node::AnalyzePSBT(analysis_psbt)};
    const bool fee_is_known{analysis.fee && *analysis.fee >= 0};
    const size_t unsigned_inputs{CountPSBTUnsignedInputs(analysis_psbt)};
    const bool wallet_has_signer{!m_wallet->privateKeysDisabled() || m_wallet->hasExternalSigner()};
    const bool can_send{fee_is_known && !multisig_sig_info && spends_only_wallet_inputs && (complete || (wallet_has_signer && unsigned_inputs > 0 && could_sign >= unsigned_inputs))};
    const bool can_broadcast{fee_is_known && complete};
    const QString review_message{
        !fee_is_known
            ? tr("The transaction fee is missing or invalid. Add valid input information before broadcasting.")
            : multisig_sig_info
                ? tr("This transaction requires %1 of %2 signatures.").arg(multisig_sig_info->first).arg(multisig_sig_info->second)
                : can_send || can_broadcast
                    ? QString{}
                    : tr("This wallet does not have the keys to sign this transaction.")};

    struct DraftRecipient {
        QString address;
        QString label;
        CAmount amount;
    };
    std::vector<DraftRecipient> draft_recipients;
    CAmount recipient_total{0};
    for (const CTxOut& output : analysis_tx->vout) {
        CTxDestination destination;
        if (!ExtractDestination(output.scriptPubKey, destination)) {
            if (output.nValue == 0 && output.scriptPubKey.IsUnspendable()) {
                continue;
            }
            reason = tr("Only PSBTs with standard address outputs are supported right now.");
            return false;
        }
        if (can_send && m_wallet->txoutIsMine(output)) {
            continue;
        }
        const QString address{QString::fromStdString(EncodeDestination(destination))};
        draft_recipients.push_back({address, getAddressLabel(address), output.nValue});
        recipient_total += output.nValue;
    }

    if (draft_recipients.empty()) {
        reason = tr("The PSBT does not have any recipient outputs to review.");
        return false;
    }
    if (draft_recipients.size() > 25) {
        reason = tr("The PSBT has more recipients than this send flow supports.");
        return false;
    }
    if (recipient_total <= 0) {
        reason = tr("The PSBT does not send a positive amount.");
        return false;
    }

    m_send_recipients->clear();
    for (size_t i{0}; i < draft_recipients.size(); ++i) {
        if (i > 0) {
            m_send_recipients->add();
        }
        SendRecipient* recipient{m_send_recipients->currentRecipient()};
        recipient->setAddress(draft_recipients[i].address);
        recipient->setLabel(draft_recipients[i].label);
        recipient->amount()->setSatoshi(draft_recipients[i].amount);
        recipient->setMessage(QString());
    }
    m_send_recipients->setCurrentIndex(0);

    if (m_current_transaction) {
        delete m_current_transaction;
    }
    m_current_transaction = new WalletQmlModelTransaction(m_send_recipients, this);
    m_current_transaction->setWtx(MakeTransactionRef(*analysis_tx));
    if (!m_current_transaction->captureReviewedRecipients(*m_send_recipients)) {
        delete m_current_transaction;
        m_current_transaction = nullptr;
        reason = tr("Unable to match the imported transaction's recipients.");
        return false;
    }
    if (analysis.fee) {
        m_current_transaction->setTransactionFee(*analysis.fee);
    }
    m_current_transaction->setDisplayUnit(m_display_unit);
    m_current_psbt = std::make_unique<PartiallySignedTransaction>(psbt);
    m_current_transaction_source = CurrentTransactionSource::ImportedPsbt;
    m_current_transaction_can_send = can_send;
    m_current_transaction_can_broadcast = can_broadcast;
    m_current_transaction_review_message = review_message;
    Q_EMIT currentTransactionChanged();

    result = can_send ? PsbtImportResult::WalletCanSign : PsbtImportResult::WalletCannotSign;
    return true;
}

QVariantMap WalletQmlModel::currentTransactionFlow() const
{
    if (!m_wallet || !m_current_transaction || !m_current_transaction->getWtx()) return {};

    const auto& tx = m_current_transaction->getWtx();
    interfaces::WalletTx preview{};
    preview.tx = tx;
    preview.debit = 0;
    std::vector<std::optional<CTxOut>> prevouts;
    prevouts.reserve(tx->vin.size());
    for (size_t i = 0; i < tx->vin.size(); ++i) {
        const auto& input = tx->vin[i];
        preview.txin_is_mine.push_back(m_wallet->txinIsMine(input));
        preview.debit += m_wallet->getDebit(input);
        const auto parent = m_wallet->getTx(input.prevout.hash);
        if (parent && input.prevout.n < parent->vout.size()) {
            prevouts.emplace_back(parent->vout[input.prevout.n]);
        } else if (m_current_psbt && i < m_current_psbt->inputs.size()
                   && !m_current_psbt->inputs[i].witness_utxo.IsNull()) {
            prevouts.emplace_back(m_current_psbt->inputs[i].witness_utxo);
        } else if (m_current_psbt && i < m_current_psbt->inputs.size()
                   && m_current_psbt->inputs[i].non_witness_utxo
                   && input.prevout.n < m_current_psbt->inputs[i].non_witness_utxo->vout.size()) {
            prevouts.emplace_back(m_current_psbt->inputs[i].non_witness_utxo->vout[input.prevout.n]);
        } else {
            prevouts.emplace_back(std::nullopt);
        }
    }
    for (const auto& output : tx->vout) {
        const bool owned = m_wallet->txoutIsMine(output);
        preview.txout_is_mine.push_back(owned);
        CTxDestination destination;
        const QString address = ExtractDestination(output.scriptPubKey, destination)
            ? QString::fromStdString(EncodeDestination(destination)) : QString{};
        const bool reviewed_recipient = m_current_transaction->isReviewedRecipient(address);
        preview.txout_is_change.push_back(owned && !reviewed_recipient);
    }
    QVariantMap flow = BuildTransactionFlow(preview, prevouts);
    if (flow.isEmpty()) return flow;
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_display_unit);
    const auto format_amount = [unit](CAmount amount) -> QString {
        return QmlBitcoinUnits::formatForDisplay(unit, amount) + QLatin1Char(' ')
            + QmlBitcoinUnits::label(unit, amount);
    };
    for (const QString& side : {QStringLiteral("inputs"), QStringLiteral("outputs")}) {
        QVariantList entries = flow.value(side).toList();
        for (QVariant& value : entries) {
            QVariantMap entry = value.toMap();
            if (entry.value(QStringLiteral("amountKnown")).toBool()) {
                entry.insert(QStringLiteral("amount"), format_amount(entry.value(QStringLiteral("amountSat")).toLongLong()));
            }
            if (side == QStringLiteral("inputs") && entry.value(QStringLiteral("ownership")).toString() == QStringLiteral("wallet")) {
                const QString label = getAddressLabel(entry.value(QStringLiteral("address")).toString());
                if (!label.isEmpty()) entry.insert(QStringLiteral("label"), label);
            }
            value = entry;
        }
        flow.insert(side, entries);
    }
    if (flow.value(QStringLiteral("feeKnown")).toBool()) {
        flow.insert(QStringLiteral("feeAmount"), format_amount(flow.value(QStringLiteral("feeSat")).toLongLong()));
    }
    return flow;
}

void WalletQmlModel::discardCurrentTransaction()
{
    const bool had_transaction_state{
        m_current_transaction ||
        m_current_psbt ||
        m_current_transaction_can_send ||
        m_current_transaction_can_broadcast ||
        !m_current_transaction_review_message.isEmpty()};

    delete m_current_transaction;
    m_current_transaction = nullptr;
    m_current_psbt.reset();
    m_current_transaction_source = CurrentTransactionSource::None;
    m_current_transaction_can_send = false;
    m_current_transaction_can_broadcast = false;
    m_current_transaction_review_message.clear();
    clearTransactionStatus();
    m_send_recipients->clear();

    if (had_transaction_state) {
        Q_EMIT currentTransactionChanged();
    }
}

bool WalletQmlModel::canBumpTransaction(const uint256& txid) const
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->transactionCanBeBumped(Txid::FromUint256(txid));
}

interfaces::Wallet::CoinsList WalletQmlModel::listCoins() const
{
    if (!m_wallet) {
        return {};
    }
    return m_wallet->listCoins();
}

bool WalletQmlModel::lockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->lockCoin(output, true);
}

bool WalletQmlModel::unlockCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->unlockCoin(output);
}

bool WalletQmlModel::isLockedCoin(const COutPoint& output)
{
    if (!m_wallet) {
        return false;
    }
    return m_wallet->isLockedCoin(output);
}

void WalletQmlModel::listLockedCoins(std::vector<COutPoint>& outputs)
{
    if (!m_wallet) {
        return;
    }
    m_wallet->listLockedCoins(outputs);
}

void WalletQmlModel::selectCoin(const COutPoint& output)
{
    const bool was_selected{m_coin_control.IsSelected(output)};
    m_coin_control.Select(output);
    if (!was_selected) {
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
    scheduleFeeEstimates();
}

void WalletQmlModel::unselectCoin(const COutPoint& output)
{
    const bool was_selected{m_coin_control.IsSelected(output)};
    m_coin_control.UnSelect(output);
    if (was_selected) {
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
    scheduleFeeEstimates();
}

bool WalletQmlModel::isSelectedCoin(const COutPoint& output)
{
    return m_coin_control.IsSelected(output);
}

std::vector<COutPoint> WalletQmlModel::listSelectedCoins() const
{
    return m_coin_control.ListSelected();
}

void WalletQmlModel::clearSelectedCoins()
{
    setMaximumRecipient(nullptr);
    if (m_coin_control.HasSelected()) setSelectedCoins({});
}

void WalletQmlModel::setSelectedCoins(const std::vector<COutPoint>& outputs)
{
    // Restoring a cancelled coin-picker edit preserves the maximum recipient.
    m_coin_control.UnSelectAll();
    for (const auto& output : outputs) m_coin_control.Select(output);
    if (m_coins_list_model) m_coins_list_model->refreshSelection();
    Q_EMIT sendAmountExhaustsBalanceChanged();
    scheduleFeeEstimates();
}

unsigned int WalletQmlModel::feeTargetBlocks() const
{
    return m_coin_control.m_confirm_target.value_or(DEFAULT_STANDARD_FEE_TARGET);
}

void WalletQmlModel::setFeeTargetBlocks(unsigned int target_blocks)
{
    if (m_coin_control.m_confirm_target != target_blocks) {
        m_coin_control.m_confirm_target = target_blocks;
        updateMaximumAmount();
        Q_EMIT feeTargetBlocksChanged();
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
    }
}

void WalletQmlModel::setCustomFeeEnabled(const bool enabled)
{
    if (m_custom_fee_enabled != enabled) {
        m_custom_fee_enabled = enabled;
        Q_EMIT customFeeEnabledChanged();
        Q_EMIT estimatedFeeChanged();
        Q_EMIT sendAmountExhaustsBalanceChanged();
        scheduleFeeEstimates();
    }
}

void WalletQmlModel::setCustomFeeRate(const QString& fee_rate)
{
    const QString trimmed_fee_rate = fee_rate.trimmed();
    const bool was_valid = customFeeRateValid();

    if (m_custom_fee_rate == trimmed_fee_rate) {
        return;
    }

    m_custom_fee_rate = trimmed_fee_rate;
    m_custom_fee_estimate.reset();

    if (m_wallet) {
        if (const auto requested_rate = ParseCustomFeeRatePerKvB(m_custom_fee_rate)) {
            std::array<CAmount, CUSTOM_FEE_TARGETS.size()> target_rates{};
            for (size_t i = 0; i < CUSTOM_FEE_TARGETS.size(); ++i) {
                wallet::CCoinControl control{m_coin_control};
                control.m_feerate.reset();
                control.m_confirm_target = CUSTOM_FEE_TARGETS[i];
                target_rates[i] = m_wallet->getMinimumFee(FEE_RATE_BASIS_VBYTES, control, nullptr, nullptr);
            }

            // When the estimator has no target-specific data, moving the thumb
            // would imply a confirmation estimate we do not actually have.
            if (std::any_of(target_rates.begin() + 1, target_rates.end(), [&](CAmount rate) {
                    return rate != target_rates.front();
                })) {
                unsigned int inferred_target = CUSTOM_FEE_TARGETS.back();
                for (size_t i = 0; i < CUSTOM_FEE_TARGETS.size(); ++i) {
                    if (target_rates[i] > 0 && *requested_rate >= target_rates[i]) {
                        inferred_target = CUSTOM_FEE_TARGETS[i];
                        break;
                    }
                }
                setFeeTargetBlocks(inferred_target);
            }
        }
    }

    Q_EMIT customFeeRateChanged();
    if (was_valid != customFeeRateValid()) {
        Q_EMIT customFeeRateValidChanged();
    }
    Q_EMIT estimatedFeeChanged();
    Q_EMIT sendAmountExhaustsBalanceChanged();
    scheduleFeeEstimates();
}

void WalletQmlModel::setDisplayUnit(int unit)
{
    if (unit != m_display_unit) {
        m_display_unit = unit;
        if (m_address_list_model) {
            m_address_list_model->setDisplayUnit(unit);
        }
        if (m_current_transaction) {
            m_current_transaction->setDisplayUnit(unit);
            Q_EMIT currentTransactionChanged();
        }
        Q_EMIT balanceChanged();
        Q_EMIT displayUnitChanged(unit);
    }
}

void WalletQmlModel::subscribeToWalletSignals()
{
    if (!m_wallet) {
        return;
    }
    m_handler_status_changed = handleStatusChanged([this]() {
        QMetaObject::invokeMethod(this, [this]() {
            refreshSecurityState();
            Q_EMIT balanceChanged();
            Q_EMIT sendAmountExhaustsBalanceChanged();
        }, Qt::QueuedConnection);
    });
    m_handler_address_list_changed = m_wallet->handleAddressBookChanged([this](const CTxDestination&, const std::string&, bool, wallet::AddressPurpose, ChangeType) {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT addressListChanged();
        }, Qt::QueuedConnection);
    });
    m_handler_transaction_changed = handleTransactionChanged([this](const uint256& txid, ChangeType change) {
        m_receive_request_notifications_pending.fetch_add(1);
        QMetaObject::invokeMethod(this, [this, txid, change] {
            const Txid id = Txid::FromUint256(txid);
            if (m_receive_reconciliation_thread) m_receive_reconciliation_updates[id] = change == CT_DELETED;
            removeReceiveRequestPayment(id);
            if (change != CT_DELETED) {
                recordReceiveRequestPayment(getWalletTx(txid));
                recheckReceiveRequestPayments(id);
            }
            updateReceivedPaymentRequestAmounts();
            Q_EMIT transactionChanged(QString::fromStdString(txid.ToString()), change);
            Q_EMIT balanceChanged();
            Q_EMIT sendAmountExhaustsBalanceChanged();
            m_receive_request_notifications_pending.fetch_sub(1);
        }, Qt::QueuedConnection);
    });
    m_handler_unload = handleUnload([this]() {
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT walletUnloaded();
        }, Qt::QueuedConnection);
    });
}

void WalletQmlModel::unsubscribeFromWalletSignals()
{
    if (m_handler_status_changed) {
        m_handler_status_changed->disconnect();
    }
    if (m_handler_address_list_changed) {
        m_handler_address_list_changed->disconnect();
    }
    if (m_handler_transaction_changed) {
        m_handler_transaction_changed->disconnect();
    }
    if (m_handler_unload) {
        m_handler_unload->disconnect();
    }
}

void WalletQmlModel::refreshSecurityState()
{
    const bool encrypted = m_wallet ? m_wallet->isCrypted() : false;
    const bool locked = m_wallet ? m_wallet->isLocked() : false;
    if (m_is_encrypted != encrypted || m_is_locked != locked) {
        m_is_encrypted = encrypted;
        m_is_locked = locked;
        Q_EMIT securityStateChanged();
    }
}

bool WalletQmlModel::unlockForAction(std::optional<SecureString>& passphrase, bool& relock)
{
    relock = false;
    if (!m_wallet) {
        if (passphrase.has_value()) {
            QmlUtil::ClearSecureString(*passphrase);
            passphrase.reset();
        }
        return true;
    }
    if (!passphrase.has_value()) {
        // Either the wallet is unlocked already (action proceeds), or it isn't
        // and the caller asked for an unlock-less attempt — let the action fail
        // downstream rather than blocking here.
        return true;
    }

    const auto result{TryUnlockWithPassphrase(*m_wallet, *passphrase)};
    passphrase.reset();
    switch (result) {
    case WalletUnlockResult::IncorrectPassphrase:
        setTransactionStatus(tr("The wallet password you entered was incorrect."));
        return false;
    case WalletUnlockResult::AlreadyUnlocked:
        return true;
    case WalletUnlockResult::UnlockedNowRelockRequired:
        relock = true;
        refreshSecurityState();
        return true;
    }
    return false;
}

void WalletQmlModel::clearTransactionStatus()
{
    setTransactionStatus(QString());
}

void WalletQmlModel::setTransactionStatus(const QString& error, bool needs_unlock)
{
    if (m_transaction_error != error) {
        m_transaction_error = error;
        Q_EMIT transactionErrorChanged();
    }
    if (m_transaction_needs_unlock != needs_unlock) {
        m_transaction_needs_unlock = needs_unlock;
        Q_EMIT transactionNeedsUnlockChanged();
    }
}

void WalletQmlModel::setSettingsError(const QString& error)
{
    if (m_settings_error != error) {
        m_settings_error = error;
        Q_EMIT settingsErrorChanged();
    }
}

QString WalletQmlModel::persistedReceiveAddressTypeKey() const
{
    return QStringLiteral("receiveAddressTypes/%1").arg(name());
}
