// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistmodel.h>
#include <util/check.h>

namespace {
QString DirectionKey(const CNodeStats& stats)
{
    return stats.fInbound ? QStringLiteral("inbound") : QStringLiteral("outbound");
}

QString ConnectionTypeKey(ConnectionType type)
{
    switch (type) {
    case ConnectionType::INBOUND: return QStringLiteral("inbound");
    case ConnectionType::OUTBOUND_FULL_RELAY: return QStringLiteral("full-relay");
    case ConnectionType::MANUAL: return QStringLiteral("manual");
    case ConnectionType::FEELER: return QStringLiteral("feeler");
    case ConnectionType::BLOCK_RELAY: return QStringLiteral("block-relay");
    case ConnectionType::ADDR_FETCH: return QStringLiteral("address-fetch");
    case ConnectionType::PRIVATE_BROADCAST: return QStringLiteral("private-broadcast");
    }
    return {};
}

QString NetworkKey(Network network)
{
    switch (network) {
    case NET_UNROUTABLE: return QStringLiteral("unroutable");
    case NET_IPV4: return QStringLiteral("ipv4");
    case NET_IPV6: return QStringLiteral("ipv6");
    case NET_ONION: return QStringLiteral("onion");
    case NET_I2P: return QStringLiteral("i2p");
    case NET_CJDNS: return QStringLiteral("cjdns");
    case NET_INTERNAL: return QStringLiteral("internal");
    case NET_MAX: break;
    }
    return {};
}

QString TransportKey(TransportProtocolType transport)
{
    switch (transport) {
    case TransportProtocolType::DETECTING: return QStringLiteral("detecting");
    case TransportProtocolType::V1: return QStringLiteral("v1");
    case TransportProtocolType::V2: return QStringLiteral("v2");
    }
    return {};
}
} // namespace

PeerListSortProxy::PeerListSortProxy(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    m_sort_role = PeerListModel::NetNodeId;
    setSortRole(m_sort_role);
    setDynamicSortFilter(true);

    const auto notify_count = [this] { Q_EMIT countChanged(); };
    connect(this, &QAbstractItemModel::rowsInserted, this, notify_count);
    connect(this, &QAbstractItemModel::rowsRemoved, this, notify_count);
    connect(this, &QAbstractItemModel::modelReset, this, notify_count);
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
        return QVariant::fromValue(peerDetailsAt(index.row()));
    }

    return QSortFilterProxyModel::data(index, role);
}

PeerDetailsModel* PeerListSortProxy::peerDetailsAt(int row) const
{
    if (row < 0 || row >= rowCount() || !sourceModel()) return nullptr;

    const QModelIndex proxy_index = index(row, 0);
    const QModelIndex source_index = mapToSource(proxy_index);
    const qint64 node_id = sourceModel()->data(source_index, PeerListModel::NetNodeId).toLongLong();
    if (auto existing = m_detail_models.value(node_id)) return existing;

    const auto stats = sourceModel()->data(source_index, PeerListModel::StatsRole)
        .value<const CNodeCombinedStats*>();
    auto* peer_model = qobject_cast<PeerListModel*>(sourceModel());
    if (!stats || !peer_model) return nullptr;

    auto* details = new PeerDetailsModel(stats, peer_model);
    m_detail_models.insert(node_id, details);
    connect(details, &PeerDetailsModel::disconnected, this, [this, node_id, details] {
        if (m_detail_models.value(node_id) == details) m_detail_models.remove(node_id);
        details->deleteLater();
    });
    return details;
}

int PeerListSortProxy::indexOfNodeId(qint64 node_id) const
{
    for (int row = 0; row < rowCount(); ++row) {
        if (QSortFilterProxyModel::data(index(row, 0), PeerListModel::NetNodeId).toLongLong() == node_id) {
            return row;
        }
    }
    return -1;
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
    sort(0, m_sort_ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    Q_EMIT sortByChanged(roleName);
}

void PeerListSortProxy::setSortAscending(bool ascending)
{
    if (m_sort_ascending == ascending) return;
    m_sort_ascending = ascending;
    sort(0, m_sort_ascending ? Qt::AscendingOrder : Qt::DescendingOrder);
    Q_EMIT sortAscendingChanged(ascending);
}

void PeerListSortProxy::setSearchText(const QString& search_text)
{
    if (m_search_text == search_text) return;
    m_search_text = search_text;
    invalidateFilter();
    Q_EMIT searchTextChanged(search_text);
}

void PeerListSortProxy::setDirectionFilters(const QStringList& filters)
{
    if (m_direction_filters == filters) return;
    m_direction_filters = filters;
    invalidateFilter();
    Q_EMIT directionFiltersChanged(filters);
}

void PeerListSortProxy::setConnectionTypeFilters(const QStringList& filters)
{
    if (m_connection_type_filters == filters) return;
    m_connection_type_filters = filters;
    invalidateFilter();
    Q_EMIT connectionTypeFiltersChanged(filters);
}

void PeerListSortProxy::setNetworkFilters(const QStringList& filters)
{
    if (m_network_filters == filters) return;
    m_network_filters = filters;
    invalidateFilter();
    Q_EMIT networkFiltersChanged(filters);
}

void PeerListSortProxy::setTransportFilters(const QStringList& filters)
{
    if (m_transport_filters == filters) return;
    m_transport_filters = filters;
    invalidateFilter();
    Q_EMIT transportFiltersChanged(filters);
}

bool PeerListSortProxy::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    if (!sourceModel()) return false;
    const QModelIndex source_index = sourceModel()->index(source_row, 0, source_parent);
    const auto* stats = sourceModel()->data(source_index, PeerListModel::StatsRole)
        .value<const CNodeCombinedStats*>();
    if (!stats) return false;

    if (!m_direction_filters.isEmpty() && !m_direction_filters.contains(DirectionKey(stats->nodeStats))) return false;
    if (!m_connection_type_filters.isEmpty()
        && !m_connection_type_filters.contains(ConnectionTypeKey(stats->nodeStats.m_conn_type))) return false;
    if (!m_network_filters.isEmpty() && !m_network_filters.contains(NetworkKey(stats->nodeStats.m_network))) return false;
    if (!m_transport_filters.isEmpty()
        && !m_transport_filters.contains(TransportKey(stats->nodeStats.m_transport_type))) return false;

    const QString needle = m_search_text.trimmed();
    if (needle.isEmpty()) return true;

    const QList<int> searchable_roles{
        PeerListModel::NetNodeId,
        PeerListModel::Address,
        PeerListModel::Direction,
        PeerListModel::ConnectionType,
        PeerListModel::Network,
        PeerListModel::Transport,
        PeerListModel::Subversion,
        PeerListModel::Sent,
        PeerListModel::Received,
    };
    for (const int role : searchable_roles) {
        if (sourceModel()->data(source_index, role).toString().contains(needle, Qt::CaseInsensitive)) return true;
    }
    return false;
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
        return sourceModel()->data(left_index, m_sort_role).toString().localeAwareCompare(
            sourceModel()->data(right_index, m_sort_role).toString()) < 0;
    case PeerListModel::Direction:
    case PeerListModel::ConnectionType:
    case PeerListModel::Network:
        return sourceModel()->data(left_index, m_sort_role).toString().localeAwareCompare(
            sourceModel()->data(right_index, m_sort_role).toString()) < 0;
    case PeerListModel::Ping:
        return left_stats.m_min_ping_time < right_stats.m_min_ping_time;
    case PeerListModel::Sent:
        return left_stats.nSendBytes < right_stats.nSendBytes;
    case PeerListModel::Received:
        return left_stats.nRecvBytes < right_stats.nRecvBytes;
    case PeerListModel::Subversion:
    case PeerListModel::Transport:
        return sourceModel()->data(left_index, m_sort_role).toString().localeAwareCompare(
            sourceModel()->data(right_index, m_sort_role).toString()) < 0;
    }
    return false;
}
