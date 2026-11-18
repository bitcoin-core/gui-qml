// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletmessage.h>
#include <common/signmessage.h>
#include <key_io.h>
#include <QObject>
#include <utility>

namespace walletmessage {
bool IsLegacy(const QString& address)
{
    if (address.size() > 128) return false;
    return std::holds_alternative<PKHash>(DecodeDestination(address.trimmed().toStdString()));
}

SignResult Sign(interfaces::Wallet& wallet, SignRequest request)
{
    SignResult result;
    bool relock{false};
    const auto finish = [&] {
        if (std::exchange(relock, false)) {
            try { result.relock_failed = !wallet.lock(); } catch (...) { result.relock_failed = true; }
            if (result.relock_failed) result.error = Error::RelockFailed;
        }
        request.passphrase.reset();
        return result;
    };
    try {
        if (request.captured_passphrase) request.passphrase = secretinput::Materialize(request.captured_passphrase);
        request.captured_passphrase.reset();
        if (!IsLegacy(request.address)) { result.error = Error::InvalidAddress; return finish(); }
        const auto destination = DecodeDestination(request.address.trimmed().toStdString());
        if (wallet.privateKeysDisabled()) {
            result.error = Error::Signing;
            result.message = QObject::tr("This wallet has no private keys for signing messages.");
            return finish();
        }
        if (wallet.isCrypted() && wallet.isLocked()) {
            if (!request.passphrase) { result.error = Error::NeedsUnlock; return finish(); }
            // A throwing unlock may already have changed the wallet state.
            relock = true;
            if (!wallet.unlock(*request.passphrase)) { relock = false; result.error = Error::IncorrectPassphrase; return finish(); }
        }
        std::string signature;
        const auto signed_message = wallet.signMessage(request.message.toStdString(), std::get<PKHash>(destination), signature);
        if (signed_message != SigningResult::OK) {
            result.error = Error::Signing;
            result.message = QString::fromStdString(SigningResultString(signed_message)).left(4096);
        } else result.signature = QString::fromStdString(signature);
    } catch (const secretinput::Failure&) {
        result.error = Error::Signing; result.message = QObject::tr("The wallet password could not be prepared. No message was signed.");
    } catch (...) { result.error = Error::Uncertain; }
    return finish();
}

VerifyResult Verify(const QString& address, const QString& message, const QString& signature)
{
    try {
        if (!IsLegacy(address) || signature.trimmed().isEmpty()) return {};
        return {MessageVerify(address.trimmed().toStdString(), signature.trimmed().toStdString(), message.toStdString()) == MessageVerificationResult::OK, false};
    } catch (...) { return {false, true}; }
}
} // namespace walletmessage
