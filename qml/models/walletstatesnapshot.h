// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETSTATESNAPSHOT_H
#define BITCOIN_QML_MODELS_WALLETSTATESNAPSHOT_H

#include <qml/models/receiverequestentry.h>

#include <interfaces/wallet.h>
#include <outputtype.h>
#include <policy/feerate.h>
#include <support/allocators/secure.h>
#include <uint256.h>

#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

namespace interfaces { class Node; }

enum class WalletKeyScheme { SingleKey, WatchOnly, MultiKey, ExternalSigner };
WalletKeyScheme ReadWalletKeyScheme(interfaces::Wallet& wallet);

// Owned values only. Readers publish this as a unit on the model's thread.
struct WalletStateSnapshot {
    QString name;
    CAmount balance{0};
    CAmount available_balance{0};
    WalletKeyScheme key_scheme{WalletKeyScheme::SingleKey};
    bool encrypted{false};
    bool locked{false};
    bool private_keys_disabled{false};
    bool external_signer{false};
    bool can_get_addresses{false};
    bool taproot_enabled{false};
    OutputType default_address_type{OutputType::BECH32};
    CFeeRate dust_relay_fee;
    QString receive_address_type;
    std::vector<interfaces::WalletAddress> addresses;
    std::map<QString, QString> labels;
    interfaces::Wallet::CoinsList coins;
    std::set<COutPoint> locked_coins;
    std::map<QString, CAmount> address_balances;
    std::set<QString> used_addresses;
    std::set<QString> change_addresses;
    std::vector<QmlRecentRequestEntry> receive_requests;
    std::map<QString, CAmount> received_amounts;
    std::set<QString> observed_addresses;
    bool unconfirmed_payments{false};
    QString persistence_error;
};

struct ReceiveCommand {
    enum class Kind { EnsureAddress, Save, Remove, Label, AddressType, RememberAddress };
    Kind kind{Kind::EnsureAddress};
    QmlRecentRequestEntry entry;
    QString address;
    QString address_type;
    QString label;
    bool next{false};
    bool reserve_receiving_address{false};
    std::optional<SecureString> passphrase;
};

struct ReceiveCommandResult {
    bool success{false};
    bool needs_unlock{false};
    QString error;
    QString receiving_address;
    std::optional<QmlRecentRequestEntry> saved_entry;
    std::optional<WalletStateSnapshot> snapshot;
    QString refresh_error;
};

struct CoinLockResult {
    bool success{true};
    WalletStateSnapshot snapshot;
};

// This object has no QObject/model references. All methods run on the wallet's
// serial backend executor, including persistence and notification reconciliation.
class WalletStateBackend {
public:
    explicit WalletStateBackend(std::shared_ptr<interfaces::Wallet> wallet);
    WalletStateSnapshot read(interfaces::Node* node);
    std::optional<WalletStateSnapshot> poll(interfaces::Node* node);
    ReceiveCommandResult execute(interfaces::Node* node, ReceiveCommand command);
    CoinLockResult setCoinsLocked(interfaces::Node* node, const std::vector<COutPoint>& outputs, bool locked);

private:
    QString settingsKey() const;
    bool setAddressType(interfaces::Node* node, const QString& type);
    std::shared_ptr<interfaces::Wallet> m_wallet;
    std::set<QString> m_observed_addresses;
    std::optional<uint256> m_last_processed_block;
    bool m_unconfirmed_payments{false};
};

#endif // BITCOIN_QML_MODELS_WALLETSTATESNAPSHOT_H
