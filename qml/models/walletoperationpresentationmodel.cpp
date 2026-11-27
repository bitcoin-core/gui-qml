// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/walletoperationpresentationmodel.h>
#include <QScopeGuard>
#include <algorithm>

WalletOperationPresentationModel::WalletOperationPresentationModel(QObject* parent) : QAbstractListModel(parent) {}
WalletOperationPresentationModel::~WalletOperationPresentationModel()
{
    auto entries = std::move(m_entries);
    for (const auto& entry : entries) { disconnect(entry.changed); disconnect(entry.destroyed); }
    for (const auto& entry : entries) if (entry.object && entry.operation->presentationHasPrompt() && !entry.operation->presentationBusy()) (void)entry.operation->dismissPresentation();
}
int WalletOperationPresentationModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : int(m_entries.size()); }
QVariant WalletOperationPresentationModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || size_t(index.row()) >= m_entries.size()) return {};
    if (role == OperationRole) return QVariant::fromValue(m_entries[size_t(index.row())].object.data());
    if (role == ActiveRole) return index.row() == 0;
    return {};
}
QHash<int, QByteArray> WalletOperationPresentationModel::roleNames() const { return {{OperationRole, "operation"}, {ActiveRole, "active"}}; }
void WalletOperationPresentationModel::setShowTerminalResults(bool value)
{
    if (m_terminals == value) return;
    m_terminals = value;
    const QPointer<WalletOperationPresentationModel> guard{this}; Q_EMIT showTerminalResultsChanged(); if (guard) refresh();
}
bool WalletOperationPresentationModel::refuse(const QString& id, const QString& reason)
{
    m_error = {QStringLiteral("PresentationUnavailable"), reason, {}, {}};
    const auto original_error = m_error;
    const QPointer<WalletOperationPresentationModel> guard{this}; Q_EMIT errorChanged(); if (guard) Q_EMIT commandRejected(id, original_error); return false;
}
bool WalletOperationPresentationModel::offer(QObject* object)
{
    auto* operation = object ? qobject_cast<WalletOperationPresentation*>(object) : nullptr;
    if (!operation) return refuse({}, tr("This object is not a wallet operation."));
    const auto id = operation->presentationId();
    if (std::any_of(m_entries.begin(), m_entries.end(), [object](const auto& entry) { return entry.object == object; })) return true;
    if (m_mutating || m_entries.size() == 32) return refuse(id, tr("Another wallet dialog is open. Close it and try again."));
    if (!m_terminals && operation->presentationTerminal()) return true;
    const QPointer<WalletOperationPresentationModel> guard{this}; const QPointer<QObject> original{object}; m_mutating = true;
    const auto reset = qScopeGuard([guard] { if (guard) { guard->m_mutating = false; guard->refresh(); } });
    const int row = int(m_entries.size()); beginInsertRows({}, row, row); if (!guard) return false;
    m_entries.push_back({original, operation, id, {}, {}}); endInsertRows(); if (!guard) return false;
    if (original) {
        // The common typed capability has no domain callbacks. Observe only
        // the public projection's ordinary change signal and QObject lifetime.
        m_entries.back().changed = operation->observePresentation(this, [this] { refresh(); });
        m_entries.back().destroyed = connect(object, &QObject::destroyed, this, [this] { refresh(); });
    }
    if (!m_error.empty()) { m_error = {}; Q_EMIT errorChanged(); }
    return true;
}
bool WalletOperationPresentationModel::erase(size_t row)
{
    if (m_mutating || row >= m_entries.size()) { m_refresh_pending = true; return false; }
    const QPointer<WalletOperationPresentationModel> guard{this}; m_mutating = true;
    const auto reset = qScopeGuard([guard] { if (guard) { guard->m_mutating = false; if (guard->m_refresh_pending) guard->refresh(); } });
    beginRemoveRows({}, int(row), int(row)); if (!guard) return false;
    disconnect(m_entries[row].changed); disconnect(m_entries[row].destroyed); m_entries.erase(m_entries.begin() + row);
    endRemoveRows(); if (!guard) return false;
    if (row == 0 && !m_entries.empty()) Q_EMIT dataChanged(index(0), index(0), {ActiveRole});
    return true;
}
bool WalletOperationPresentationModel::dismiss(QObject* operation)
{
    if (m_entries.empty() || !m_entries.front().object || m_entries.front().object != operation) {
        const auto* presentation = operation ? qobject_cast<WalletOperationPresentation*>(operation) : nullptr;
        return refuse(presentation ? presentation->presentationId() : QString{}, tr("This answer belongs to a different wallet operation."));
    }
    const QString original = m_entries.front().id;
    if (m_entries.front().operation->presentationBusy()) return refuse(original, tr("Wait for the current wallet action to finish."));
    const QPointer<WalletOperationPresentationModel> guard{this}; const auto receipt = m_entries.front().operation->dismissPresentation();
    if (!guard) return receipt.accepted;
    if (!receipt.accepted) return refuse(original, receipt.error.message);
    return !m_entries.empty() && m_entries.front().id == original ? erase(0) : true;
}
void WalletOperationPresentationModel::reportClosed(QObject* operation, QObject* prompt)
{
    // Native Popup exit may arrive after the next row became current. Such a
    // teardown observation cannot dismiss or authorize the successor.
    if (m_entries.empty() || !m_entries.front().object || m_entries.front().object != operation || !prompt) return;
    if (m_entries.front().operation->presentationPrompt() == prompt && m_entries.front().operation->presentationHasPrompt() && !m_entries.front().operation->presentationBusy()) (void)dismiss(operation);
}
void WalletOperationPresentationModel::refresh()
{
    if (m_mutating) { m_refresh_pending = true; return; }
    m_refresh_pending = false;
    for (size_t row = 0; row < m_entries.size();) {
        if (!m_entries[row].object || (!m_terminals && m_entries[row].operation->presentationTerminal())) { if (!erase(row)) return; }
        else ++row;
    }
}
