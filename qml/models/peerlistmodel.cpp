// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistmodel.h>

#include <interfaces/node.h>
#include <qml/backendexecutor.h>
#include <qml/peerstatsutil.h>

#include <QList>
#include <QTimer>

#include <cassert>
#include <chrono>
#include <utility>

namespace {
constexpr auto MODEL_UPDATE_DELAY{std::chrono::milliseconds{250}};
}

PeerListModel::PeerListModel(interfaces::Node& node, QObject* parent, bool backend_ready)
    : QAbstractListModel(parent), m_node(node), m_executor(std::make_shared<BackendExecutor>()), m_backend_ready(backend_ready)
{
    connect(m_executor.get(), &BackendExecutor::drained, this, &PeerListModel::drained);
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PeerListModel::refresh);
    m_timer->setInterval(MODEL_UPDATE_DELAY);

    refresh();
}

PeerListModel::~PeerListModel()
{
    beginShutdown();
}

void PeerListModel::startAutoRefresh()
{
    if (m_stopping || m_auto_refresh_requested) return;
    m_auto_refresh_requested = true;
    ++m_generation;
    if (m_backend_ready) {
        refresh();
        m_timer->start();
    }
}

void PeerListModel::stopAutoRefresh()
{
    m_auto_refresh_requested = false;
    m_timer->stop();
    ++m_generation;
    m_refresh_requested = false;
}

void PeerListModel::backendInitialized()
{
    if (m_stopping || m_backend_ready) return;
    m_backend_ready = true;
    refresh();
    if (m_auto_refresh_requested) m_timer->start();
}

void PeerListModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    stopAutoRefresh();
    m_refresh_running = false;
    Q_EMIT refreshStateChanged();
    m_executor->shutdown();
}

bool PeerListModel::isDrained() const
{
    return m_executor->isDrained();
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
    if (m_stopping || !m_backend_ready) return;
    if (m_refresh_running) {
        m_refresh_requested = true;
        return;
    }
    m_refresh_running = true;
    m_refresh_requested = false;
    m_refresh_error.clear();
    Q_EMIT refreshStateChanged();
    const auto generation = m_generation;
    const bool accepted = m_executor->submit(this, [node = &m_node] {
        interfaces::Node::NodesStats stats;
        QList<CNodeCombinedStats> peers;
        const bool success = node->getNodesStats(stats);
        if (success) {
            peers.reserve(stats.size());
            for (auto& entry : stats) {
                peers.append({std::move(std::get<0>(entry)), std::move(std::get<2>(entry)), std::get<1>(entry)});
            }
        }
        return std::make_pair(success, std::move(peers));
    }, [this, generation](auto result) {
        finishRefresh(generation, std::move(result.second), result.first ? QString{} : tr("Unable to refresh peers."));
    }, [this, generation](std::exception_ptr) {
        finishRefresh(generation, {}, tr("Unable to refresh peers."));
    });
    if (!accepted) {
        m_refresh_running = false;
        m_refresh_requested = false;
        m_refresh_error = tr("The node is shutting down.");
        Q_EMIT refreshStateChanged();
    }
}

void PeerListModel::finishRefresh(quint64 generation, QList<CNodeCombinedStats> peers, const QString& error)
{
    m_refresh_running = false;
    if (m_stopping) return;
    if (generation == m_generation) {
        m_refresh_error = error;
        if (error.isEmpty()) {
            m_ready = true;
            applySnapshot(std::move(peers));
        }
    }
    if (m_refresh_requested) refresh();
    else Q_EMIT refreshStateChanged();
}

void PeerListModel::applySnapshot(QList<CNodeCombinedStats> new_peers_data)
{
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
