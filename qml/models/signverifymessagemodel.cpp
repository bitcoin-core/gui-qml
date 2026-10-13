// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/signverifymessagemodel.h>

#include <common/signmessage.h>
#include <key_io.h>
#include <qml/models/walletunlock.h>
#include <qml/util.h>
#include <support/allocators/secure.h>

namespace {
std::optional<PKHash> LegacyP2PKHFromAddress(const QString& address)
{
    const CTxDestination destination{DecodeDestination(address.trimmed().toStdString())};
    if (const auto* pkhash{std::get_if<PKHash>(&destination)}) {
        return *pkhash;
    }
    return std::nullopt;
}
} // namespace

SignVerifyMessageModel::SignVerifyMessageModel(QObject* parent)
    : SignVerifyMessageModel({}, std::make_shared<BackendExecutor>(), parent)
{
}

SignVerifyMessageModel::SignVerifyMessageModel(std::shared_ptr<interfaces::Wallet> wallet, std::shared_ptr<BackendExecutor> executor, QObject* parent)
    : QObject(parent), m_wallet(std::move(wallet)), m_executor(std::move(executor))
{
}

void SignVerifyMessageModel::setWallet(std::shared_ptr<interfaces::Wallet> wallet)
{
    if (m_wallet == wallet) return;
    m_wallet = std::move(wallet);
    clear();
}

void SignVerifyMessageModel::setSecurityStateChangedFn(SecurityStateChangedFn fn)
{
    m_security_state_changed = std::move(fn);
}

bool SignVerifyMessageModel::isLegacyP2PKHAddress(const QString& address) const
{
    return LegacyP2PKHFromAddress(address).has_value();
}

bool SignVerifyMessageModel::signMessage(const QString& address, const QString& message)
{
    return signMessageInternal(address, message, std::nullopt);
}

bool SignVerifyMessageModel::signMessageWithPassphrase(const QString& address, const QString& message, const QString& passphrase)
{
    return signMessageInternal(address, message, QmlUtil::SecureStringFromQString(passphrase));
}

bool SignVerifyMessageModel::signMessageInternal(const QString& address, const QString& message, std::optional<SecureString> passphrase)
{
    if (m_signing_pending) return false;
    clearSigningStatus();
    setSignature({});
    if (!m_wallet) { setSigningStatus(tr("No wallet is selected.")); return false; }
    const auto pkhash = LegacyP2PKHFromAddress(address);
    if (!pkhash) { setSigningStatus(tr("Enter a legacy P2PKH bitcoin address.")); return false; }
    struct Result { QString signature; QString error; bool needs_unlock{false}; };
    m_signing_pending = true;
    const auto generation = ++m_signing_generation;
    Q_EMIT signingPendingChanged();
    const auto wallet = m_wallet;
    const bool accepted = m_executor->submit(this, [wallet, pkhash = *pkhash, message = message.toStdString(), passphrase = std::move(passphrase)]() mutable {
        Result result;
        bool relock{false};
        if (wallet->isCrypted() && wallet->isLocked()) {
            if (!passphrase) {
                result.needs_unlock = true;
                result.error = tr("Enter your wallet password to sign this message.");
                return result;
            }
            const auto unlocked = TryUnlockWithPassphrase(*wallet, *passphrase);
            passphrase.reset();
            if (unlocked == WalletUnlockResult::IncorrectPassphrase) { result.error = tr("The wallet password you entered was incorrect."); return result; }
            relock = unlocked == WalletUnlockResult::UnlockedNowRelockRequired;
        }
        if (passphrase) { QmlUtil::ClearSecureString(*passphrase); passphrase.reset(); }
        WalletRelockGuard guard{*wallet, [] {}, relock};
        std::string signature;
        const auto status = wallet->signMessage(message, pkhash, signature);
        if (status == SigningResult::OK) result.signature = QString::fromStdString(signature);
        else result.error = QString::fromStdString(SigningResultString(status));
        return result;
    }, [this, generation](Result result) {
        m_signing_pending = false;
        Q_EMIT signingPendingChanged();
        notifySecurityStateChanged();
        if (generation != m_signing_generation) return;
        setSigningStatus(result.error, result.needs_unlock);
        setSignature(result.signature);
        Q_EMIT signingFinished(result.error.isEmpty());
    }, [this, generation](std::exception_ptr) {
        m_signing_pending = false;
        Q_EMIT signingPendingChanged();
        notifySecurityStateChanged();
        if (generation != m_signing_generation) return;
        setSigningStatus(tr("The message could not be signed. Please try again."));
        Q_EMIT signingFinished(false);
    });
    if (!accepted) { m_signing_pending = false; Q_EMIT signingPendingChanged(); }
    return accepted;
}

bool SignVerifyMessageModel::verifyMessage(const QString& address, const QString& message, const QString& signature)
{
    if (m_verification_pending) return false;
    const auto generation = ++m_verification_generation;
    m_verification_pending = true;
    m_verification_valid = false;
    Q_EMIT verificationPendingChanged();
    const bool accepted = m_executor->submit(this, [address = address.trimmed().toStdString(), message = message.toStdString(), signature = signature.trimmed().toStdString()] {
        return MessageVerify(address, signature, message) == MessageVerificationResult::OK;
    }, [this, generation](bool valid) {
        m_verification_pending = false;
        Q_EMIT verificationPendingChanged();
        if (generation != m_verification_generation) return;
        m_verification_valid = valid;
        Q_EMIT verificationFinished();
    }, [this, generation](std::exception_ptr) {
        m_verification_pending = false;
        Q_EMIT verificationPendingChanged();
        if (generation != m_verification_generation) return;
        m_verification_valid = false;
        Q_EMIT verificationFinished();
    });
    if (!accepted) { m_verification_pending = false; Q_EMIT verificationPendingChanged(); }
    return accepted;
}

void SignVerifyMessageModel::clear()
{
    ++m_signing_generation;
    ++m_verification_generation;
    m_verification_valid = false;
    clearSigningStatus();
    setSignature(QString());
}

void SignVerifyMessageModel::clearSigningStatus()
{
    setSigningStatus(QString());
}

void SignVerifyMessageModel::setSigningStatus(const QString& error, bool needs_unlock)
{
    if (m_signing_error != error) {
        m_signing_error = error;
        Q_EMIT signingErrorChanged();
    }
    if (m_signing_needs_unlock != needs_unlock) {
        m_signing_needs_unlock = needs_unlock;
        Q_EMIT signingNeedsUnlockChanged();
    }
}

void SignVerifyMessageModel::setSignature(const QString& signature)
{
    if (m_signature != signature) {
        m_signature = signature;
        Q_EMIT signatureChanged();
    }
}

void SignVerifyMessageModel::notifySecurityStateChanged()
{
    if (m_security_state_changed) {
        m_security_state_changed();
    }
}
