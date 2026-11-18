// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETPASSWORDCONTROLLER_H
#define BITCOIN_QML_MODELS_WALLETPASSWORDCONTROLLER_H

#include <qml/models/securityoperation.h>
#include <qml/models/walletpagecontroller.h>
#include <qml/models/walletsecurityinterface.h>

#include <QObject>
#include <QPointer>
#include <QString>

/** The wallet password page's state: the operation it started, why one could
 * not start, and how it ended. Declared by WalletPasswordSettings. */
class WalletPasswordController : public WalletPageController
{
    Q_OBJECT
    //! The wallet this page acts on, given once (WalletPageController).
    Q_PROPERTY(WalletSecurityInterface* security READ security WRITE setSecurity NOTIFY securityChanged)
    Q_PROPERTY(SecurityOperation* operation READ operation NOTIFY changed)
    //! The page's operation runs.
    Q_PROPERTY(bool saving READ saving NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    //! The page changed the password.
    Q_PROPERTY(bool passwordUpdated READ passwordUpdated NOTIFY changed)
public:
    explicit WalletPasswordController(QObject* parent = nullptr);
    ~WalletPasswordController() override;

    WalletSecurityInterface* security() const { return m_security; }
    void setSecurity(WalletSecurityInterface* security);
    SecurityOperation* operation() const { return m_operation; }
    bool saving() const { return m_operation && m_operation->busy(); }
    QString errorText() const;
    bool passwordUpdated() const;

    //! Sets the wallet's first password.
    Q_INVOKABLE bool encrypt(const QString& replacement, const QString& confirmation);
    //! Changes the wallet's password.
    Q_INVOKABLE bool changePassphrase(const QString& current, const QString& replacement, const QString& confirmation);
    //! Forgets a finished result and why an operation could not start.
    Q_INVOKABLE void clearResult();

Q_SIGNALS:
    void securityChanged();
    void changed();
    //! The page's operation succeeded.
    void succeeded(SecurityOperation::Kind kind);

private:
    bool start(SecurityOperation::Kind kind, const QString& current, const QString& replacement, const QString& confirmation);
    void follow();
    //! Cancels an operation that still waits for its passwords and forgets
    //! the page's state; an operation already submitted finishes.
    void stop() override;
    void forget();

    QPointer<WalletSecurityInterface> m_security;
    QPointer<SecurityOperation> m_operation;
    QString m_admission_error;
    bool m_reported{false};
};

#endif // BITCOIN_QML_MODELS_WALLETPASSWORDCONTROLLER_H
