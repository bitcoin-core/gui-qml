// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H
#define BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H

#include <interfaces/wallet.h>
#include <qml/backendexecutor.h>
#include <support/allocators/secure.h>

#include <functional>
#include <optional>

#include <QObject>
#include <QString>

class SignVerifyMessageModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString signingError READ signingError NOTIFY signingErrorChanged)
    Q_PROPERTY(bool signingNeedsUnlock READ signingNeedsUnlock NOTIFY signingNeedsUnlockChanged)
    Q_PROPERTY(QString signature READ signature NOTIFY signatureChanged)
    Q_PROPERTY(bool signingPending READ signingPending NOTIFY signingPendingChanged)
    Q_PROPERTY(bool verificationPending READ verificationPending NOTIFY verificationPendingChanged)
    Q_PROPERTY(bool verificationValid READ verificationValid NOTIFY verificationFinished)

public:
    using SecurityStateChangedFn = std::function<void()>;

    explicit SignVerifyMessageModel(QObject* parent = nullptr);
    SignVerifyMessageModel(std::shared_ptr<interfaces::Wallet> wallet, std::shared_ptr<BackendExecutor> executor, QObject* parent = nullptr);

    QString signingError() const { return m_signing_error; }
    bool signingNeedsUnlock() const { return m_signing_needs_unlock; }
    QString signature() const { return m_signature; }
    bool signingPending() const { return m_signing_pending; }
    bool verificationPending() const { return m_verification_pending; }
    bool verificationValid() const { return m_verification_valid; }

    void setWallet(std::shared_ptr<interfaces::Wallet> wallet);
    void setSecurityStateChangedFn(SecurityStateChangedFn fn);

    Q_INVOKABLE bool isLegacyP2PKHAddress(const QString& address) const;
    Q_INVOKABLE bool signMessage(const QString& address, const QString& message);
    Q_INVOKABLE bool signMessageWithPassphrase(const QString& address, const QString& message, const QString& passphrase);
    Q_INVOKABLE bool verifyMessage(const QString& address, const QString& message, const QString& signature);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void clearSigningStatus();

Q_SIGNALS:
    void signingErrorChanged();
    void signingNeedsUnlockChanged();
    void signatureChanged();
    void signingPendingChanged();
    void verificationPendingChanged();
    void signingFinished(bool success);
    void verificationFinished();

private:
    bool signMessageInternal(const QString& address, const QString& message, std::optional<SecureString> passphrase);
    void setSigningStatus(const QString& error, bool needs_unlock = false);
    void setSignature(const QString& signature);
    void notifySecurityStateChanged();

    std::shared_ptr<interfaces::Wallet> m_wallet;
    std::shared_ptr<BackendExecutor> m_executor;
    quint64 m_signing_generation{0};
    quint64 m_verification_generation{0};
    bool m_signing_pending{false};
    bool m_verification_pending{false};
    bool m_verification_valid{false};
    SecurityStateChangedFn m_security_state_changed;
    QString m_signing_error;
    bool m_signing_needs_unlock{false};
    QString m_signature;
};

#endif // BITCOIN_QML_MODELS_SIGNVERIFYMESSAGEMODEL_H
