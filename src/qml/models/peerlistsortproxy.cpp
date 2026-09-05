// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistmodel.h>
#include <util/check.h>

PeerListSortProxy::PeerListSortProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    m_sort_role = PeerListModel::NetNodeId;
    setSortRole(m_sort_role);
    setDynamicSortFilter(true);
    connect(this, &QAbstractProxyModel::sourceModelChanged, this, [this] { m_details.clear(); });
}

QHash<int, QByteArray> PeerListSortProxy::roleNames() const
{
    if (sourceModel()) {
        return sourceModel()->roleNames();
    }
    return {};
}

int PeerListSortProxy::RoleNameToRole(const QString & name) const
{
    auto role_names = roleNames();
    auto keys = role_names.keys(name.toUtf8());
    if (!keys.empty()) {
        return keys.first();
    } else {
        return PeerListModel::NetNodeId;
    }
}

QVariant PeerListSortProxy::data(const QModelIndex& index, int role) const
{
    if (role == PeerListModel::StatsRole) {
        const auto* stats{QSortFilterProxyModel::data(index, role).value<const CNodeCombinedStats*>()};
        auto* model{qobject_cast<PeerListModel*>(sourceModel())};
        if (!stats || !model) return {};
        auto& details{m_details[stats->nodeStats.nodeid]};
        if (!details) details = new PeerDetailsModel(stats, model);
        return QVariant::fromValue(details.data());
    }

    return QSortFilterProxyModel::data(index, role);
}

QString PeerListSortProxy::sortBy() const
{
    return m_sort_by;
}

void PeerListSortProxy::setSortBy(const QString & roleName)
{
    if (m_sort_by == roleName) return;

    m_sort_by = roleName;
    m_sort_role = RoleNameToRole(roleName);
    setSortRole(m_sort_role);
    sort(0);
    Q_EMIT sortByChanged(roleName);
}

bool PeerListSortProxy::lessThan(const QModelIndex& left_index, const QModelIndex& right_index) const
{
    const CNodeStats left_stats = Assert(sourceModel()->data(left_index, PeerListModel::StatsRole).value<const CNodeCombinedStats*>())->nodeStats;
    const CNodeStats right_stats = Assert(sourceModel()->data(right_index, PeerListModel::StatsRole).value<const CNodeCombinedStats*>())->nodeStats;

    switch (m_sort_role) {
    case PeerListModel::NetNodeId:
        return left_stats.nodeid < right_stats.nodeid;
    case PeerListModel::Age:
        return left_stats.m_connected > right_stats.m_connected;
    case PeerListModel::Address:
        return left_stats.m_addr_name.compare(right_stats.m_addr_name) < 0;
    case PeerListModel::Direction:
        return left_stats.fInbound > right_stats.fInbound;
    case PeerListModel::ConnectionType:
        return left_stats.m_conn_type < right_stats.m_conn_type;
    case PeerListModel::Network:
        return left_stats.m_network < right_stats.m_network;
    case PeerListModel::Ping:
        return left_stats.m_min_ping_time < right_stats.m_min_ping_time;
    case PeerListModel::Sent:
        return left_stats.nSendBytes < right_stats.nSendBytes;
    case PeerListModel::Received:
        return left_stats.nRecvBytes < right_stats.nRecvBytes;
    case PeerListModel::Subversion:
        return left_stats.cleanSubVer.compare(right_stats.cleanSubVer) < 0;
    }
    return false;
}
