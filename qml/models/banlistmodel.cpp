// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/banlistmodel.h>

#include <interfaces/node.h>
#include <net_types.h>
#include <qml/backendexecutor.h>

#include <QDateTime>
#include <QLocale>

#include <utility>

BanListModel::BanListModel(interfaces::Node& node, QObject* parent, bool backend_ready,
                         std::shared_ptr<BackendExecutor> executor)
    : QAbstractListModel(parent), m_node(node),
      m_executor(executor ? std::move(executor) : std::make_shared<BackendExecutor>()), m_backend_ready(backend_ready)
{
    connect(m_executor.get(), &BackendExecutor::drained, this, &BanListModel::drained);
}

BanListModel::~BanListModel()
{
    beginShutdown();
}

void BanListModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    m_refresh_requested = false;
    m_refresh_running = false;
    m_action_pending = false;
    m_external_action_pending = false;
    Q_EMIT refreshStateChanged();
    Q_EMIT actionStateChanged();
    m_executor->shutdown();
}

bool BanListModel::isDrained() const
{
    return m_executor->isDrained();
}

void BanListModel::setExternalActionPending(bool pending)
{
    if (m_stopping || m_external_action_pending == pending) return;
    m_external_action_pending = pending;
    Q_EMIT actionStateChanged();
    // The command completion requests the final snapshot. Keeping the request
    // deferred here coalesces backend notifications with that completion.
}

void BanListModel::finishExternalAction(bool refresh_required)
{
    if (m_stopping) return;
    setExternalActionPending(false);
    if (refresh_required || m_refresh_requested) refresh();
}

void BanListModel::backendInitialized()
{
    if (m_stopping || m_backend_ready) return;
    m_backend_ready = true;
    refresh();
}

int BanListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_ban_list.size();
}

QVariant BanListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_ban_list.size()) return {};
    const BanListEntry& entry = m_ban_list.at(index.row());
    switch (static_cast<BanRoles>(role)) {
    case BanRoles::AddressRole:
        return QString::fromStdString(entry.subnet.ToString());
    case BanRoles::BanUntilRole: {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(entry.ban_entry.nBanUntil);
        return QLocale::system().toString(dt, QStringLiteral("MMMM d, yyyy h:mm AP"));
    }
    }
    return {};
}

QHash<int, QByteArray> BanListModel::roleNames() const
{
    return {
        {static_cast<int>(BanRoles::AddressRole), "address"},
        {static_cast<int>(BanRoles::BanUntilRole), "banUntil"},
    };
}

bool BanListModel::unbanAt(int row)
{
    if (m_stopping || !m_backend_ready || actionPending() || row < 0 || row >= m_ban_list.size()) return false;
    // Copy the target before dispatch: a subsequent refresh may remove/reorder
    // its row while the command waits behind another ban-list operation.
    const CSubNet subnet = m_ban_list.at(row).subnet;
    m_action_pending = true;
    m_action_error.clear();
    Q_EMIT actionStateChanged();
    const bool accepted = m_executor->submit(this, [node = &m_node, subnet] { return node->unban(subnet); },
        [this](bool success) {
            finishUnban(success, success ? QString{} : tr("Could not unban peer. The ban list may have changed."));
        }, [this](std::exception_ptr) {
            finishUnban(false, tr("Could not unban peer. The ban list could not be updated."));
        });
    if (!accepted) {
        m_action_pending = false;
        m_action_error = tr("The node is shutting down.");
        Q_EMIT actionStateChanged();
    }
    return accepted;
}

void BanListModel::finishUnban(bool success, const QString& error)
{
    if (m_stopping) return;
    m_action_pending = false;
    m_action_error = error;
    Q_EMIT actionStateChanged();
    // Notifications raised by this mutation only mark a refresh as needed;
    // read the final map once after the command completes, including failures.
    m_refresh_requested = false;
    // Observers acknowledge the notification mailbox before the next read is
    // queued, so a later external mutation cannot be cleared after that read.
    Q_EMIT unbanFinished(success, error);
    refresh();
}

void BanListModel::refresh()
{
    if (m_stopping || !m_backend_ready) return;
    if (m_refresh_running || actionPending()) {
        m_refresh_requested = true;
        return;
    }
    m_refresh_running = true;
    m_refresh_requested = false;
    m_refresh_error.clear();
    Q_EMIT refreshStateChanged();
    const bool accepted = m_executor->submit(this, [node = &m_node] {
        banmap_t banned;
        QList<BanListEntry> entries;
        const bool success = node->getBanned(banned);
        if (success) {
            entries.reserve(banned.size());
            for (const auto& [subnet, entry] : banned) entries.append({subnet, entry});
        }
        return std::make_pair(success, std::move(entries));
    }, [this](auto result) {
        finishRefresh(std::move(result.second), result.first ? QString{} : tr("Unable to refresh banned peers."));
    }, [this](std::exception_ptr) {
        finishRefresh({}, tr("Unable to refresh banned peers."));
    });
    if (!accepted) {
        m_refresh_running = false;
        m_refresh_requested = false;
        m_refresh_error = tr("The node is shutting down.");
        Q_EMIT refreshStateChanged();
    }
}

void BanListModel::finishRefresh(QList<BanListEntry> entries, const QString& error)
{
    m_refresh_running = false;
    if (m_stopping) return;
    m_refresh_error = error;
    if (error.isEmpty()) {
        m_ready = true;
        const bool count_changed = entries.size() != m_ban_list.size();
        beginResetModel();
        m_ban_list = std::move(entries);
        endResetModel();
        if (count_changed) Q_EMIT countChanged();
    }
    if (m_refresh_requested && !actionPending()) refresh();
    else Q_EMIT refreshStateChanged();
}
