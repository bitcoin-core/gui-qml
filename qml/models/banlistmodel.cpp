// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/banlistmodel.h>

#include <interfaces/node.h>
#include <net_types.h>

#include <QDateTime>
#include <QLocale>

BanListModel::BanListModel(interfaces::Node& node, QObject* parent)
    : QAbstractListModel(parent), m_node(node)
{
    connect(&m_backend, &BackendWorker::drained, this, &BanListModel::backendDrained);
}

int BanListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_ban_list.size();
}

QVariant BanListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_ban_list.size()) return {};
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
    RequireModelThread(this);
    if (m_draining || !m_node_ready || row < 0 || row >= m_ban_list.size()) return false;
    m_backend.submit([node = &m_node, subnet = m_ban_list.at(row).subnet] { return node->unban(subnet); },
                     [this](bool success) { Q_EMIT unbanFinished(success); refresh(); });
    return true;
}

void BanListModel::refresh()
{
    RequireModelThread(this);
    if (m_draining || !m_node_ready) return;
    if (m_pending) {
        m_again = true;
        return;
    }
    m_pending = true;
    m_backend.submit([node = &m_node] {
        banmap_t bans;
        node->getBanned(bans);
        return bans;
    }, [this](const banmap_t& bans) {
        RequireModelThread(this);
        m_pending = false;
        beginResetModel();
        m_ban_list.clear();
        m_ban_list.reserve(static_cast<int>(bans.size()));
        for (const auto& [subnet, entry] : bans) m_ban_list.append({subnet, entry});
        endResetModel();
        Q_EMIT countChanged();
        if (std::exchange(m_again, false)) refresh();
    });
}

void BanListModel::onNodeReady()
{
    RequireModelThread(this);
    if (m_draining || m_node_ready) return;
    m_node_ready = true;
    refresh();
}

void BanListModel::drainBackend()
{
    RequireModelThread(this);
    m_draining = true;
    m_backend.drain();
}
