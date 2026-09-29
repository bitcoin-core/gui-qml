// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistmodel.h>

#include <interfaces/node.h>
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
    connect(&m_backend, &BackendWorker::drained, this, &PeerListModel::backendDrained);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PeerListModel::refresh);
    m_timer->setInterval(MODEL_UPDATE_DELAY);

}

PeerListModel::~PeerListModel() = default;

void PeerListModel::startAutoRefresh()
{
    m_timer->start();
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
    case Transport:
        return PeerStatsUtil::TransportToQString(rec.nodeStats.m_transport_type);
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
    roles[Transport] = "transport";
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
    RequireModelThread(this);
    if (m_draining || !m_node_ready) return;
    if (m_pending) {
        m_again = true;
        return;
    }
    m_pending = true;
    m_backend.submit([node = &m_node] {
        interfaces::Node::NodesStats stats;
        const bool success = node->getNodesStats(stats);
        return std::make_pair(success, std::move(stats));
    }, [this](auto result) {
        RequireModelThread(this);
        m_pending = false;
        if (std::exchange(m_again, false)) refresh();
        if (!result.first) return;
        decltype(m_peers_data) new_peers_data;
        new_peers_data.reserve(result.second.size());
        for (const auto& node_stats : result.second) {
            new_peers_data.append({std::get<0>(node_stats), std::get<2>(node_stats), std::get<1>(node_stats)});
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
    });
}

void PeerListModel::onNodeReady()
{
    RequireModelThread(this);
    if (m_draining || m_node_ready) return;
    m_node_ready = true;
    refresh();
}

void PeerListModel::drainBackend()
{
    RequireModelThread(this);
    m_draining = true;
    m_timer->stop();
    m_backend.drain();
}
