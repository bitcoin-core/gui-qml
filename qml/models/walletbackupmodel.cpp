// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletbackupmodel.h>

WalletBackupModel::WalletBackupModel(std::function<bool()> available, Start start, QObject* parent)
    : WalletBackupInterface(parent), m_available(std::move(available)), m_start(std::move(start))
{
}

FileSaveOperation* WalletBackupModel::startBackup(const QString& path, QString& why)
{
    if (!available()) { why = tr("This wallet is closing."); return nullptr; }
    if (busy()) { why = tr("A backup operation is already pending."); return nullptr; }
    if (path.size() > 32'768 || path.trimmed().isEmpty()) { why = tr("Choose a location for the wallet backup."); return nullptr; }
    const quint64 record = ++m_next;
    m_pending = record;
    const QPointer<WalletBackupModel> self{this};
    // Called on the GUI thread (the factory marshals the backend's outcome),
    // also after the page and its operation object are gone.
    auto* operation = m_start(path, why, [self, record](backup::Snapshot) { if (self) self->finish(record); });
    if (!self) return nullptr;
    if (!operation) {
        finish(record);
        return nullptr;
    }
    // A start that failed before the backend admitted it has no outcome.
    if (!operation->busy()) finish(record);
    m_operation = operation;
    Q_EMIT busyChanged();
    return operation;
}

void WalletBackupModel::close()
{
    if (std::exchange(m_closed, true)) return;
    if (m_operation && m_operation->busy()) m_operation->retireBeforeWrite();
    Q_EMIT availableChanged();
}

void WalletBackupModel::finish(quint64 record)
{
    if (m_pending != record) return;
    m_pending = 0;
    Q_EMIT busyChanged();
}
