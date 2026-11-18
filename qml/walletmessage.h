// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETMESSAGE_H
#define BITCOIN_QML_WALLETMESSAGE_H

#include <interfaces/wallet.h>
#include <qml/secretinput.h>
#include <support/allocators/secure.h>
#include <QString>

#include <memory>
#include <optional>

namespace walletmessage {
enum class Error { None, InvalidAddress, NeedsUnlock, IncorrectPassphrase, Signing, RelockFailed, Uncertain };
struct SignRequest {
    QString address, message;
    std::optional<SecureString> passphrase;
    secretinput::Captured captured_passphrase;
};
struct SignResult {
    Error error{Error::None};
    QString signature, message;
    bool relock_failed{false};
};
struct VerifyResult { bool valid{false}; bool failed{false}; };
bool IsLegacy(const QString& address);
SignResult Sign(interfaces::Wallet& wallet, SignRequest request);
VerifyResult Verify(const QString& address, const QString& message, const QString& signature);
} // namespace walletmessage

#endif // BITCOIN_QML_WALLETMESSAGE_H
