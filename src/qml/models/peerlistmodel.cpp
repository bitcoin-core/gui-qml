// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistmodel.h>

#include <interfaces/node.h>
#include <netbase.h>
#include <qml/peerstatsutil.h>

#include <QList>
#include <QTimer>

#include <cassert>
#include <chrono>
#include <utility>

namespace {
constexpr auto MODEL_UPDATE_DELAY{std::chrono::milliseconds{250}};
}

PeerListModel::PeerListModel(interfaces::Node& node, QObject* parent)
    : QAbstractListModel(parent), m_node(node)
{
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PeerListModel::refresh);
    m_timer->setInterval(MODEL_UPDATE_DELAY);

    refresh();
}

PeerListModel::~PeerListModel() = default;

bool PeerListModel::disconnectPeer(qint64 node_id)
{
    if (m_stopped) return false;
    const bool disconnected{m_node.disconnectById(node_id)};
    if (disconnected) refresh();
    return disconnected;
}

bool PeerListModel::banPeer(const QString& raw_address, int64_t ban_duration)
{
    if (m_stopped || ban_duration <= 0) return false;
    const auto address{LookupHost(raw_address.toStdString(), /*fAllowLookup=*/false)};
    if (!address || !m_node.ban(*address, ban_duration)) return false;
    m_node.disconnectByAddress(*address);
    refresh();
    return true;
}

void PeerListModel::startAutoRefresh()
{
    if (!m_stopped) m_timer->start();
}

void PeerListModel::stop()
{
    m_stopped = true;
    stopAutoRefresh();
}

void PeerListModel::stopAutoRefresh()
{
    m_timer->stop();
}

int PeerListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_peers_data.size();
}

QVariant PeerListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_peers_data.size()) return {};
    const CNodeCombinedStats& rec = m_peers_data.at(index.row());

    switch (role) {
    case NetNodeId:
        return static_cast<qint64>(rec.nodeStats.nodeid);
    case Age:
        return PeerStatsUtil::FormatPeerAge(rec.nodeStats.m_connected);
    case Address:
        return QString::fromStdString(rec.nodeStats.m_addr_name);
    case Direction:
        return QString(rec.nodeStats.fInbound ? tr("Inbound") : tr("Outbound"));
    case ConnectionType:
        return PeerStatsUtil::ConnectionTypeToQString(rec.nodeStats.m_conn_type, /*prepend_direction=*/false);
    case Network:
        return PeerStatsUtil::NetworkToQString(rec.nodeStats.m_network);
    case Ping:
        return PeerStatsUtil::FormatPingTime(rec.nodeStats.m_min_ping_time);
    case Sent:
        return PeerStatsUtil::FormatBytes(rec.nodeStats.nSendBytes);
    case Received:
        return PeerStatsUtil::FormatBytes(rec.nodeStats.nRecvBytes);
    case Subversion:
        return QString::fromStdString(rec.nodeStats.cleanSubVer);
    case StatsRole:
        return QVariant::fromValue(&rec);
    }

    return {};
}

QHash<int, QByteArray> PeerListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NetNodeId] = "nodeId";
    roles[Age] = "age";
    roles[Address] = "address";
    roles[Direction] = "direction";
    roles[ConnectionType] = "connectionType";
    roles[Network] = "network";
    roles[Ping] = "ping";
    roles[Sent] = "sent";
    roles[Received] = "received";
    roles[Subversion] = "subversion";
    roles[StatsRole] = "stats";
    return roles;
}

Qt::ItemFlags PeerListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid()) return Qt::NoItemFlags;
    return Qt::ItemIsSelectable | Qt::ItemIsEnabled;
}

void PeerListModel::refresh()
{
    if (m_stopped) return;
    interfaces::Node::NodesStats nodes_stats;
    if (!m_node.getNodesStats(nodes_stats)) return;

    decltype(m_peers_data) new_peers_data;
    new_peers_data.reserve(nodes_stats.size());
    for (const auto& node_stats : nodes_stats) {
        const CNodeCombinedStats stats{std::get<0>(node_stats), std::get<2>(node_stats), std::get<1>(node_stats)};
        new_peers_data.append(stats);
    }

    const bool count_changed = m_peers_data.size() != new_peers_data.size();
    bool order_changed{false};
    if (!count_changed) {
        for (int i = 0; i < m_peers_data.size(); ++i) {
            if (m_peers_data.at(i).nodeStats.nodeid != new_peers_data.at(i).nodeStats.nodeid) {
                order_changed = true;
                break;
            }
        }
    }

    if (count_changed || order_changed) {
        beginResetModel();
        m_peers_data = std::move(new_peers_data);
        endResetModel();
        return;
    }

    m_peers_data = std::move(new_peers_data);

    if (rowCount() > 0) {
        const auto top_left = index(0, 0);
        const auto bottom_right = index(rowCount() - 1, 0);
        Q_EMIT dataChanged(top_left, bottom_right);
    }
}
