// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/peerlistmodel.h>
#include <qml/peerstatsutil.h>
#include <qml/peercommandport.h>
#include <qml/retainedsnapshot.h>
#include <interfaces/node.h>
#include <QTimer>
#include <QPointer>
#include <algorithm>
#include <stdexcept>
namespace {
QString PresentText(const std::string& value, qsizetype limit = 512)
{
    return QString::fromUtf8(value.data(), qsizetype(std::min(value.size(), size_t(limit) * 4))).left(limit);
}
PeerPresentation Present(const CNodeCombinedStats& stats, NodeClock::time_point now)
{
    PeerPresentation result;
    result.rawAddress = [&]() -> QString { return QString::fromStdString(stats.nodeStats.addr.ToStringAddr()); }();
    result.address = PresentText(stats.nodeStats.m_addr_name);
    result.addressLocal = PresentText(stats.nodeStats.addrLocal);
    result.type = [&]() -> QString { return PeerStatsUtil::ConnectionTypeToQString(stats.nodeStats.m_conn_type, /*prepend_direction=*/true); }();
    result.network = [&]() -> QString { return PeerStatsUtil::NetworkToQString(stats.nodeStats.m_network); }();
    result.transport = [&]() -> QString { return PeerStatsUtil::TransportToQString(stats.nodeStats.m_transport_type); }();
    result.sessionId = PresentText(stats.nodeStats.m_session_id, 128);
    result.version = [&]() -> QString { return QString::number(stats.nodeStats.nVersion); }();
    result.userAgent = PresentText(stats.nodeStats.cleanSubVer);
    result.services = [&]() -> QString { return PeerStatsUtil::FormatServicesStr(stats.nodeStateStats.their_services); }();
    result.transactionRelay = [&]() -> bool { return stats.nodeStateStats.m_relay_txs; }();
    result.addressRelay = [&]() -> bool { return stats.nodeStateStats.m_addr_relay_enabled; }();
    result.highBandwidth = [&]() -> bool { return stats.nodeStats.m_bip152_highbandwidth_to || stats.nodeStats.m_bip152_highbandwidth_from; }();
    result.addressesProcessed = [&]() -> QString { return QString::number(stats.nodeStateStats.m_addr_processed); }();
    result.addressesRateLimited = [&]() -> QString { return QString::number(stats.nodeStateStats.m_addr_rate_limited); }();
    result.startingHeight = [&]() -> QString { return {}; }();
    result.syncedHeaders = [&]() -> QString { return QString::number(stats.nodeStateStats.nSyncHeight); }();
    result.syncedBlocks = [&]() -> QString { return QString::number(stats.nodeStateStats.nCommonHeight); }();
    result.direction = [&]() -> QString { return QString::fromStdString(stats.nodeStats.fInbound ? "Inbound" : "Outbound"); }();
    result.connectionDuration = [&]() -> QString { return PeerStatsUtil::FormatDurationStr(now - stats.nodeStats.m_connected); }();
    result.lastSend = [&]() -> QString { return PeerStatsUtil::FormatDurationStr(now - stats.nodeStats.m_last_send); }();
    result.lastReceived = [&]() -> QString { return PeerStatsUtil::FormatDurationStr(now - stats.nodeStats.m_last_recv); }();
    result.bytesSent = [&]() -> QString { return PeerStatsUtil::FormatBytes(stats.nodeStats.nSendBytes); }();
    result.bytesReceived = [&]() -> QString { return PeerStatsUtil::FormatBytes(stats.nodeStats.nRecvBytes); }();
    result.pingTime = [&]() -> QString { return PeerStatsUtil::FormatPingTime(stats.nodeStats.m_last_ping_time); }();
    result.pingMin = [&]() -> QString { return PeerStatsUtil::FormatPingTime(stats.nodeStats.m_min_ping_time); }();
    result.pingWait = [&]() -> QString { return PeerStatsUtil::FormatPingTime(stats.nodeStateStats.m_ping_wait); }();
    result.timeOffset = [&]() -> QString { return PeerStatsUtil::FormatTimeOffset(Ticks<std::chrono::seconds>(stats.nodeStateStats.time_offset)); }();
    result.mappedAS = [&]() -> QString { return stats.nodeStats.m_mapped_as != 0 ? QString::number(stats.nodeStats.m_mapped_as) : QString{}; }();
    result.permission = [&]() -> QString {
        if (stats.nodeStats.m_permission_flags == NetPermissionFlags::None) {
            return {};
        }
        QStringList permissions;
        for (const auto& permission : NetPermissions::ToStrings(stats.nodeStats.m_permission_flags)) {
            permissions.append(QString::fromStdString(permission));
        }
        return permissions.join(" & ");
    }();
    return result;
}

struct PeerRows { std::vector<NativeListRow> rows; };
}
struct PeerListModel::DetailRegistry { std::map<qint64, std::set<PeerPresentationObserver*>> listeners; };
PeerListModel::PeerListModel(interfaces::Node& node, QObject* parent, bool backend_ready,
                           execution::SnapshotServices services, std::function<NodeClock::time_point()> clock)
    : NativeListModel(std::move(services), parent), m_backend_ready(backend_ready)
{
    if (!scheduler()) configureServices({});
    m_details = execution::RetainSnapshot<DetailRegistry>(scheduler(), [] { return std::make_unique<DetailRegistry>(); });
    setRead([node = &node, scheduler = scheduler(), clock = std::move(clock)] {
        interfaces::Node::NodesStats stats;
        if (!node->getNodesStats(stats)) throw std::runtime_error("Peer statistics unavailable");
        const auto now = clock();
        auto group = execution::RetainSnapshot<PeerRows>(scheduler, [&] {
            auto result = std::make_unique<PeerRows>(); result->rows.reserve(stats.size());
            for (auto& entry : stats) {
                auto record = std::make_shared<PeerRecord>();
                record->raw = {std::move(std::get<0>(entry)), std::move(std::get<2>(entry)), std::get<1>(entry)};
                record->presentation = Present(record->raw, now);
                const auto& raw = record->raw.nodeStats; const auto& view = record->presentation;
                record->connection = {
                    QStringLiteral("%1/%2/%3").arg(qint64(raw.nodeid)).arg(qint64(raw.m_connected.time_since_epoch().count()))
                        .arg(QString::fromStdString(raw.addr.ToStringAddrPort())),
                    raw.nodeid, raw.m_connected, raw.addr, view.sessionId, view.rawAddress, view.address, CSubNet{raw.addr}.IsValid()};
                const auto age = now - raw.m_connected;
                using namespace std::chrono_literals;
                const QString age_text = age >= 24h ? tr("%1 d").arg(age / 24h) : age >= 1h ? tr("%1 h").arg(age / 1h)
                    : age >= 1min ? tr("%1 m").arg(age / 1min) : tr("%1 s").arg(age / 1s);
                NativeListRow row; row.key = QString::number(raw.nodeid); row.payload = record;
                row.values = {{NetNodeId, QVariant::fromValue(qint64(raw.nodeid))}, {Age, age_text}, {Address, view.address},
                    {Direction, raw.fInbound ? tr("Inbound") : tr("Outbound")}, {ConnectionType, PeerStatsUtil::ConnectionTypeToQString(raw.m_conn_type, false)},
                    {Network, view.network}, {Ping, view.pingMin}, {Sent, view.bytesSent}, {Received, view.bytesReceived},
                    {Subversion, view.userAgent}, {Transport, view.transport},
                    {ConnectionId, record->connection.key}, {NodeIdText, QString::number(raw.nodeid)}};
                result->rows.push_back(std::move(row));
            }
            std::sort(result->rows.begin(), result->rows.end(), [](const auto& a, const auto& b) {
                return static_cast<const PeerRecord*>(a.payload.get())->raw.nodeStats.nodeid < static_cast<const PeerRecord*>(b.payload.get())->raw.nodeStats.nodeid;
            }); return result;
        });
        if (!group) throw std::runtime_error("Peer inventory capacity unavailable");
        std::vector<NativeRowHandle> rows; rows.reserve(group->rows.size());
        for (const auto& row : group->rows) rows.emplace_back(group, &row);
        return rows;
    });
    m_timer = new QTimer(this); m_timer->setInterval(250);
    connect(m_timer, &QTimer::timeout, this, &PeerListModel::refresh); refresh();
}
PeerListModel::~PeerListModel() { m_destroying = true; closeAdmission(); }
void PeerListModel::startAutoRefresh() { if (stopping()) return; m_auto_refresh_requested = true; if (m_backend_ready) m_timer->start(); }
void PeerListModel::stopAutoRefresh() { m_auto_refresh_requested = false; m_timer->stop(); }
void PeerListModel::backendInitialized() { if (stopping() || m_backend_ready) return; m_backend_ready = true; refresh(); if (m_auto_refresh_requested) m_timer->start(); }
void PeerListModel::beginShutdown() { closeAdmission(); }
void PeerListModel::closeAdmission() {
    if (stopping()) return;
    if (m_timer) m_timer->stop();
    m_details.reset();
    NativeListModel::closeAdmission();
    if (!m_destroying) Q_EMIT commandsChanged();
}
void PeerListModel::refresh() { if (m_backend_ready) refreshNative(); }
PeerCommandPort* PeerListModel::commands() const { return !stopping() && m_commands_object ? m_commands : nullptr; }
void PeerListModel::setCommands(PeerCommandPort* commands)
{
    if (stopping() || !commands || !commands->observerObject() || m_commands_object) return;
    disconnect(m_commands_change); disconnect(m_commands_destroyed);
    m_commands = commands; m_commands_object = commands->observerObject();
    m_commands_change = commands->observe(this, [this] { Q_EMIT commandsChanged(); });
    m_commands_destroyed = connect(m_commands_object, &QObject::destroyed, this, [this] { m_commands = nullptr; m_commands_object.clear(); Q_EMIT commandsChanged(); });
    Q_EMIT commandsChanged();
}
QVariant PeerListModel::data(const QModelIndex& index, int role) const {
    if (role != StatsRole) return NativeListModel::data(index, role);
    const auto row = rowHandle(index); if (!row) return {};
    const auto* record = static_cast<const PeerRecord*>(row->payload.get());
    return QVariant::fromValue(PeerStatsHandle{row, &record->raw});
}
Qt::ItemFlags PeerListModel::flags(const QModelIndex& index) const { return index.isValid() ? Qt::ItemIsEnabled | Qt::ItemIsSelectable : Qt::NoItemFlags; }
QHash<int, QByteArray> PeerListModel::roleNames() const {
    return {{StatsRole, "stats"}, {NetNodeId, "nodeId"}, {Age, "age"}, {Address, "address"}, {Direction, "direction"},
        {ConnectionType, "connectionType"}, {Network, "network"}, {Ping, "ping"}, {Sent, "sent"}, {Received, "received"},
        {Subversion, "subversion"}, {Transport, "transport"}, {ConnectionId, "connectionId"}, {NodeIdText, "nodeIdText"}};
}
void PeerListModel::watchDetails(qint64 id, PeerPresentationObserver* details) {
    if (!m_details || stopping() || !details || !details->observerObject()) return;
    m_details->listeners[id].insert(details);
    connect(details->observerObject(), &QObject::destroyed, this, [this, id, details] {
        if (!m_details) return;
        auto found = m_details->listeners.find(id); if (found == m_details->listeners.end()) return;
        found->second.erase(details); if (found->second.empty()) m_details->listeners.erase(found);
    });
}
void PeerListModel::rowUpdated(const QString& key, const NativeRowHandle&) { notifyDetails(key.toLongLong(),false); }
void PeerListModel::rowRemoved(const QString& key) { notifyDetails(key.toLongLong(),true); }
void PeerListModel::notifyDetails(qint64 id, bool removed, PeerPresentationObserver* after)
{
    const auto registry=m_details; if(!registry||stopping())return;
    const auto row=removed?NativeRowHandle{}:rowForKey(QString::number(id)); if(!removed&&!row)return;
    const auto* record=row?static_cast<const PeerRecord*>(row->payload.get()):nullptr;
    const QPointer<PeerListModel> guard{this}; auto* previous=after;
    for(int budget=0;budget<32;++budget) {
        auto found=registry->listeners.find(id);if(found==registry->listeners.end())return;
        const auto next=previous?found->second.upper_bound(previous):found->second.begin();if(next==found->second.end())return;
        previous=*next;
        if(removed) { found->second.erase(next);previous->markDisconnected(); }
        else previous->update(record->presentation,record->raw.fNodeStateStatsAvailable);
        if(!guard||stopping())return;
    }
    // A continuation retains only identity and a GUI observer, never Native payload.
    services().post([guard,id,removed,previous] { if(guard&&!guard->stopping())guard->notifyDetails(id,removed,previous); });
}
