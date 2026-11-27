// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETBACKUPCONTROLLER_H
#define BITCOIN_QML_MODELS_WALLETBACKUPCONTROLLER_H

#include <qml/models/walletbackupinterface.h>
#include <qml/models/walletpagecontroller.h>

#include <QPointer>
#include <QString>

/** The wallet settings page's backup: the save it started, and why one could
 * not start. Declared by WalletSectionPage. When the page is hidden or its
 * wallet can no longer be used, a backup whose write the backend has not
 * admitted ends without writing; an admitted write finishes and its outcome
 * stays here until dismissed. */
class WalletBackupController : public WalletPageController
{
    Q_OBJECT
    //! The wallet this page backs up, given once (WalletPageController).
    Q_PROPERTY(WalletBackupInterface* backups READ backups WRITE setBackups NOTIFY backupsChanged)
    //! The save this page started, or null.
    Q_PROPERTY(FileSaveOperation* operation READ operation NOTIFY changed)
    //! A backup can start: the page is available and none of this wallet's
    //! backups is pending, from this page or another.
    Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool saved READ saved NOTIFY changed)
    //! Why a backup could not start, else why this page's save failed.
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit WalletBackupController(QObject* parent = nullptr);
    ~WalletBackupController() override;

    WalletBackupInterface* backups() const { return m_backups; }
    void setBackups(WalletBackupInterface* backups);
    FileSaveOperation* operation() const { return m_operation; }
    bool canStart() const;
    bool busy() const { return m_operation && m_operation->busy(); }
    bool saved() const { return m_operation && m_operation->saved(); }
    QString error() const;

    //! Backs the wallet up to `path`; one at a time per wallet.
    Q_INVOKABLE bool begin(const QString& path);
    //! Forgets a finished result and why a backup could not start.
    Q_INVOKABLE void dismiss();

Q_SIGNALS:
    void backupsChanged();
    void changed();

protected:
    void stop() override;

private:
    void replace(FileSaveOperation* operation);

    QPointer<WalletBackupInterface> m_backups;
    QPointer<FileSaveOperation> m_operation;
    QString m_refusal;
};

#endif // BITCOIN_QML_MODELS_WALLETBACKUPCONTROLLER_H
