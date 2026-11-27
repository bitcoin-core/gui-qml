// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETPREPARE_H
#define BITCOIN_QML_WALLETPREPARE_H

#include <qml/walletopening.h>
#include <qml/walletreview.h>
#include <qml/senddraft.h>
#include <qml/secretinput.h>
#include <qml/models/coinselection.h>
#include <support/allocators/secure.h>
#include <wallet/coincontrol.h>
#include <set>

namespace walletprepare {
enum class Error { None, Invalid, InsufficientBalance, InsufficientSelectedBalance, NeedsUnlock, IncorrectPassphrase, Core, SweepUnknown, RelockFailed, Failed };
struct Request {
    QList<walletreview::Recipient> recipients;
    std::shared_ptr<const senddraft::Snapshot> draft;
    quint64 maximum_token{0};
    wallet::CCoinControl control;
    std::shared_ptr<const std::set<COutPoint>> selected;
    coinselection::InputMode input_mode{coinselection::InputMode::Automatic};
    std::optional<CAmount> available_balance;
    int remainder{-1};
    bool automatic_fee{false};
    int target_blocks{0}, display_unit{0};
    QString fee_rate;
    std::optional<SecureString> passphrase;
    secretinput::Captured captured_passphrase;
};
struct Result {
    std::shared_ptr<const void> lifetime{};
    Error error{Error::None};
    QString message{};
    std::shared_ptr<const walletreview::Snapshot> review{};
    bool relock_failed{false};
};
Result Execute(interfaces::Wallet& wallet, Request request);
Result ExecuteInto(interfaces::Wallet& wallet, Request request, std::shared_ptr<Result> owner);
} // namespace walletprepare

#endif // BITCOIN_QML_WALLETPREPARE_H
