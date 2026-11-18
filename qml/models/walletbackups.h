// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETBACKUPS_H
#define BITCOIN_QML_MODELS_WALLETBACKUPS_H

#include <qml/models/backuppolicyoperation.h>
#include <qml/models/walletbackupinterface.h>

#include <QPointer>

#include <functional>

/** A wallet's backups (WalletBackupInterface). It keeps a pending record for
 * the one backup that may run: the record is held by the operation's backend
 * services, so it clears only when the backend finishes or retires the save,
 * even after the page and its operation object are gone. */
class WalletBackups final : public WalletBackupInterface
{
    Q_OBJECT
public:
    //! Creates and starts an operation for `path` whose backend calls
    //! `outcome` on its terminal phase. Returns null with `why` set.
    using Start = std::function<BackupFileOperation*(const QString& path, QString& why,
        std::function<void(backup::Snapshot)> outcome)>;
    WalletBackups(std::function<bool()> available, Start start, QObject* parent);

    bool available() const override { return !m_closed && m_available && m_available(); }
    bool busy() const override { return m_pending != 0; }
    FileSaveOperation* startBackup(const QString& path, QString& why) override;
    //! The wallet unloads: retire a pending backup before its write if the
    //! backend still allows it; an admitted write finishes.
    void close();
    //! Availability may have changed.
    void refresh() { Q_EMIT availableChanged(); }

private:
    void finish(quint64 record);

    const std::function<bool()> m_available;
    const Start m_start;
    QPointer<BackupFileOperation> m_operation;
    quint64 m_pending{0};
    quint64 m_next{0};
    bool m_closed{false};
};

#endif // BITCOIN_QML_MODELS_WALLETBACKUPS_H
