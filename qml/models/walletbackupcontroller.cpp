// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletbackupcontroller.h>

WalletBackupController::WalletBackupController(QObject* parent) : WalletPageController(parent)
{
    connect(this, &WalletPageController::availableChanged, this, &WalletBackupController::changed);
}

WalletBackupController::~WalletBackupController()
{
    if (m_operation) disconnect(m_operation, nullptr, this, nullptr);
    stop();
    // The operation is a child: destroying it retires a save that has not
    // been admitted; an admitted write finishes in the backend.
}

void WalletBackupController::setBackups(WalletBackupInterface* backups)
{
    if (m_backups == backups || !bindWallet(backups)) return;
    m_backups = backups;
    connect(backups, &WalletBackupInterface::busyChanged, this, &WalletBackupController::changed);
    Q_EMIT backupsChanged();
    Q_EMIT changed();
}

bool WalletBackupController::canStart() const
{
    return available() && m_backups && !m_backups->busy() && !busy();
}

QString WalletBackupController::error() const
{
    if (!m_refusal.isEmpty()) return m_refusal;
    return m_operation ? m_operation->error() : QString{};
}

bool WalletBackupController::begin(const QString& path)
{
    m_refusal.clear();
    if (!available() || !m_backups) {
        m_refusal = tr("This wallet is closing.");
        Q_EMIT changed();
        return false;
    }
    if (busy()) {
        m_refusal = tr("A backup operation is already pending.");
        Q_EMIT changed();
        return false;
    }
    QString why;
    const QPointer<WalletBackupController> guard{this};
    auto* operation = m_backups->startBackup(path, why);
    if (!guard) return false;
    if (!operation) {
        m_refusal = why.isEmpty() ? tr("The backup could not be started.") : why;
        Q_EMIT changed();
        return false;
    }
    replace(operation);
    return operation->busy() || operation->saved();
}

void WalletBackupController::dismiss()
{
    if (busy()) return;
    const bool had_state = m_operation || !m_refusal.isEmpty();
    m_refusal.clear();
    replace(nullptr);
    if (had_state) Q_EMIT changed();
}

void WalletBackupController::stop()
{
    // The backend decides: before its write is admitted the save ends and
    // nothing is written; after, it finishes and the outcome stays here.
    if (m_operation && m_operation->busy()) m_operation->retireBeforeWrite();
}

void WalletBackupController::replace(FileSaveOperation* operation)
{
    if (m_operation == operation) return;
    if (m_operation) {
        disconnect(m_operation, nullptr, this, nullptr);
        m_operation->deleteLater();
    }
    m_operation = operation;
    if (operation) {
        operation->setParent(this);
        connect(operation, &FileSaveOperation::changed, this, &WalletBackupController::changed);
    }
    Q_EMIT changed();
}
