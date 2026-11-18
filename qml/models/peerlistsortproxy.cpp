// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistmodel.h>
#include <qml/retainedsnapshot.h>
#include <algorithm>
#include <mutex>
#include <stdexcept>
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


struct PeerListSortProxy::DetailCache { QHash<QString, QPointer<PeerDetailsModel>> models; };
struct PeerListSortProxy::Input {
    std::mutex mutex;
    NativeVersionHandle source;
    int role{PeerListModel::NetNodeId}; bool ascending{true};
    QString search; QStringList directions, connections, networks, transports;
};
PeerListSortProxy::PeerListSortProxy(QObject* parent) : NativeListModel({}, parent) {
    m_sort_role = PeerListModel::NetNodeId;
    connect(this, &QAbstractItemModel::rowsInserted, this, &PeerListSortProxy::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &PeerListSortProxy::countChanged);
}
PeerListSortProxy::~PeerListSortProxy() { m_destroying = true; close(); }
void PeerListSortProxy::close() {
    if (stopping()) return;
    m_details.reset(); m_input.reset(); NativeListModel::close();
    if (!m_destroying) Q_EMIT admissionClosed();
}
void PeerListSortProxy::setSourceModel(QAbstractItemModel* source) {
    if (sourceModel() == source || stopping()) return;
    if (sourceModel()) {
        disconnect(sourceModel(), nullptr, this, nullptr);
        m_details = execution::RetainSnapshot<DetailCache>(scheduler(), [] { return std::make_unique<DetailCache>(); });
        QAbstractProxyModel::setSourceModel(source);
        if (auto* peer = qobject_cast<PeerListModel*>(source)) connect(peer, &NativeListModel::snapshotReady, this, &PeerListSortProxy::refreshProjection);
        refreshProjection();
        return;
    }
    auto* peer = qobject_cast<PeerListModel*>(source);
    if (!peer) { QAbstractProxyModel::setSourceModel(source); return; }
    configureServices(peer->services());
    m_input = execution::RetainSnapshot<Input>(scheduler(), [] { return std::make_unique<Input>(); });
    m_details = execution::RetainSnapshot<DetailCache>(scheduler(), [] { return std::make_unique<DetailCache>(); });
    if (!m_input || !m_details) throw std::runtime_error("Peer projection capacity unavailable");
    setRead([input = m_input] {
        NativeVersionHandle version; int role; bool ascending; QString search; QStringList directions, connections, networks, transports;
        { std::lock_guard lock{input->mutex}; version=input->source; role=input->role; ascending=input->ascending; search=input->search.trimmed();
          directions=input->directions; connections=input->connections; networks=input->networks; transports=input->transports; }
        QString displayed_id;
        if (search.startsWith(QLatin1Char('#'))) {
            const auto digits = search.mid(1);
            const bool numeric = !digits.isEmpty() && std::all_of(digits.begin(), digits.end(), [](QChar ch) { return ch >= QLatin1Char('0') && ch <= QLatin1Char('9'); });
            bool valid{false}; const auto id = numeric ? digits.toLongLong(&valid) : -1;
            if (valid && id >= 0) displayed_id = QString::number(id);
        }
        auto rows = NativeListModel::Rows(version);
        std::erase_if(rows, [&](const auto& row) {
            const auto& stats = static_cast<const PeerRecord*>(row->payload.get())->raw.nodeStats;
            if (!directions.isEmpty() && !directions.contains(DirectionKey(stats))) return true;
            if (!connections.isEmpty() && !connections.contains(ConnectionTypeKey(stats.m_conn_type))) return true;
            if (!networks.isEmpty() && !networks.contains(NetworkKey(stats.m_network))) return true;
            if (!transports.isEmpty() && !transports.contains(TransportKey(stats.m_transport_type))) return true;
            if (search.isEmpty()) return false;
            if (!displayed_id.isEmpty()) return row->values.value(PeerListModel::NodeIdText).toString() != displayed_id;
            for (int role : {PeerListModel::NetNodeId, PeerListModel::Address, PeerListModel::Direction, PeerListModel::ConnectionType,
                            PeerListModel::Network, PeerListModel::Transport, PeerListModel::Subversion, PeerListModel::Sent, PeerListModel::Received}) {
                if (row->values.value(role).toString().contains(search, Qt::CaseInsensitive)) return false;
            } return true;
        });
        std::sort(rows.begin(), rows.end(), [&](const auto& left, const auto& right) {
            const auto& a = static_cast<const PeerRecord*>(left->payload.get())->raw.nodeStats;
            const auto& b = static_cast<const PeerRecord*>(right->payload.get())->raw.nodeStats;
            auto compare = [](const auto& a, const auto& b) { return a < b ? -1 : a > b ? 1 : 0; };
            int result{0};
            switch (role) {
            case PeerListModel::NetNodeId: result=compare(a.nodeid,b.nodeid); break;
            case PeerListModel::Age: result=compare(b.m_connected,a.m_connected); break;
            case PeerListModel::Ping: result=compare(a.m_min_ping_time,b.m_min_ping_time); break;
            case PeerListModel::Sent: result=compare(a.nSendBytes,b.nSendBytes); break;
            case PeerListModel::Received: result=compare(a.nRecvBytes,b.nRecvBytes); break;
            default: result=QString::localeAwareCompare(left->values.value(role).toString(),right->values.value(role).toString()); break;
            }
            if (!result) result=compare(a.nodeid,b.nodeid);
            return ascending ? result < 0 : result > 0;
        }); return rows;
    }, execution::Lane::Domain);
    QAbstractProxyModel::setSourceModel(source);
    connect(peer, &NativeListModel::snapshotReady, this, &PeerListSortProxy::refreshProjection);
    refreshProjection();
}
void PeerListSortProxy::refreshProjection() {
    if (!m_input || stopping()) return;
    auto* source = qobject_cast<PeerListModel*>(sourceModel());
    invalidateProjection();
    { std::lock_guard lock{m_input->mutex}; m_input->source=source ? source->completedSnapshot() : NativeVersionHandle{}; m_input->role=m_sort_role; m_input->ascending=m_sort_ascending;
      m_input->search=m_search_text; m_input->directions=m_direction_filters; m_input->connections=m_connection_type_filters;
      m_input->networks=m_network_filters; m_input->transports=m_transport_filters; }
    refreshNative();
}
QModelIndex PeerListSortProxy::mapToSource(const QModelIndex& value) const {
    const auto row=rowHandle(value); if (!row || !sourceModel()) return {};
    auto* source=qobject_cast<PeerListModel*>(sourceModel()); return source ? source->index(source->indexForKey(row->key)) : QModelIndex{};
}
QModelIndex PeerListSortProxy::mapFromSource(const QModelIndex& value) const {
    auto* source=qobject_cast<PeerListModel*>(sourceModel()); if (!source || value.model()!=source) return {};
    const auto row=source->rowHandle(value); return row ? index(indexForKey(row->key)) : QModelIndex{};
}
QHash<int,QByteArray> PeerListSortProxy::roleNames() const { return sourceModel() ? sourceModel()->roleNames() : QHash<int,QByteArray>{}; }
int PeerListSortProxy::RoleNameToRole(const QString& name) const { const auto keys=roleNames().keys(name.toUtf8()); return keys.empty()?PeerListModel::NetNodeId:keys.front(); }
QVariant PeerListSortProxy::data(const QModelIndex& value,int role) const { return role==PeerListModel::StatsRole ? QVariant::fromValue(peerDetailsAt(value.row())) : NativeListModel::data(value,role); }
PeerDetailsModel* PeerListSortProxy::peerDetailsAt(int position) const {
    const auto row=rowHandle(index(position)); auto* source=qobject_cast<PeerListModel*>(sourceModel()); if (!row || !source || !m_details) return nullptr;
    const auto* original=static_cast<const PeerRecord*>(row->payload.get());
    const auto source_row=source->rowForKey(row->key); if (!source_row) return nullptr;
    const auto* record=static_cast<const PeerRecord*>(source_row->payload.get());
    if (!SamePeerConnection(original->connection,record->connection)) return nullptr;
    const auto id=record->connection.key;
    if (auto existing=m_details->models.value(id); existing && existing->connected()) return existing;
    auto* details=new PeerDetailsModel(PeerStatsHandle{source_row,&record->raw},source); m_details->models.insert(id,details);
    connect(details,&PeerDetailsModel::disconnected,this,[this,id,details] { if(m_details && m_details->models.value(id)==details)m_details->models.remove(id); details->deleteLater(); });
    connect(details,&QObject::destroyed,this,[this,id,details] { if(m_details && m_details->models.value(id)==details)m_details->models.remove(id); });
    return details;
}
int PeerListSortProxy::indexOfNodeId(qint64 id) const { return indexForKey(QString::number(id)); }
QString PeerListSortProxy::sortBy() const { return m_sort_by; }
void PeerListSortProxy::setSortBy(const QString& value) { if (m_sort_by==value || stopping()) return; m_sort_by=value; m_sort_role=RoleNameToRole(value); refreshProjection(); Q_EMIT sortByChanged(value); }
void PeerListSortProxy::setSortAscending(bool value) { if (m_sort_ascending==value || stopping()) return; m_sort_ascending=value; refreshProjection(); Q_EMIT sortAscendingChanged(value); }
void PeerListSortProxy::setSearchText(const QString& value) { if (m_search_text==value || stopping()) return; m_search_text=value; refreshProjection(); Q_EMIT searchTextChanged(value); }
void PeerListSortProxy::setDirectionFilters(const QStringList& value) { if (m_direction_filters==value || stopping()) return; m_direction_filters=value; refreshProjection(); Q_EMIT directionFiltersChanged(value); }
void PeerListSortProxy::setConnectionTypeFilters(const QStringList& value) { if (m_connection_type_filters==value || stopping()) return; m_connection_type_filters=value; refreshProjection(); Q_EMIT connectionTypeFiltersChanged(value); }
void PeerListSortProxy::setNetworkFilters(const QStringList& value) { if (m_network_filters==value || stopping()) return; m_network_filters=value; refreshProjection(); Q_EMIT networkFiltersChanged(value); }
void PeerListSortProxy::setTransportFilters(const QStringList& value) { if (m_transport_filters==value || stopping()) return; m_transport_filters=value; refreshProjection(); Q_EMIT transportFiltersChanged(value); }
