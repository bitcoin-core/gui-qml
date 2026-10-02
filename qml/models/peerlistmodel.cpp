// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistmodel.h>

#include <interfaces/node.h>
#include <qml/peerstatsutil.h>

#include <QList>
#include <QTimer>

#include <array>
#include <cassert>
#include <chrono>
#include <utility>

namespace {
constexpr auto MODEL_UPDATE_DELAY{std::chrono::milliseconds{250}};
}

PeerListModel::PeerListModel(interfaces::Node& node, QObject* parent)
    : QAbstractListModel(parent), m_node(node)
{
    m_worker = new QObject;
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName("qml-peers");
    m_thread.start();
    m_timer = new QTimer(this);
    connect(m_timer, &QTimer::timeout, this, &PeerListModel::refresh);
    m_timer->setInterval(MODEL_UPDATE_DELAY);

    refresh();
}

PeerListModel::~PeerListModel()
{
    m_thread.quit();
    m_thread.wait();
}

void PeerListModel::startAutoRefresh()
{
    m_auto_refresh = true;
    updateRefreshTimer();
}

void PeerListModel::stopAutoRefresh()
{
    m_auto_refresh = false;
    updateRefreshTimer();
}

void PeerListModel::setWidgetActive(bool active)
{
    if (m_widget_active == active) return;
    m_widget_active = active;
    Q_EMIT widgetActiveChanged();
    updateRefreshTimer();
    if (active) refresh();
}

void PeerListModel::updateRefreshTimer()
{
    if (m_shutting_down || (!m_auto_refresh && !m_widget_active)) {
        m_timer->stop();
        return;
    }
    // The detailed table keeps its existing cadence; the dashboard only needs
    // one snapshot per second. Neither consumer can stop the other's refreshes.
    m_timer->start(m_auto_refresh ? MODEL_UPDATE_DELAY : std::chrono::milliseconds{1000});
}

void PeerListModel::setSummary(QVariantMap summary)
{
    if (m_summary == summary) return;
    m_summary = std::move(summary);
    Q_EMIT summaryChanged();
}

void PeerListModel::stopForShutdown()
{
    m_shutting_down = true;
    m_timer->stop();
    setSummary({{"ready", false}});
    QMetaObject::invokeMethod(m_worker, [this] {
        QMetaObject::invokeMethod(this, [this] { Q_EMIT shutdownFinished(); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
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
    if (m_shutting_down || m_pending) return;
    m_pending = true;
    QMetaObject::invokeMethod(m_worker, [this] {
        interfaces::Node::NodesStats nodes_stats;
        if (!m_node.getNodesStats(nodes_stats)) {
            QMetaObject::invokeMethod(this, [this] {
                m_pending = false;
                if (!m_shutting_down) setSummary({{"ready", false}});
            }, Qt::QueuedConnection);
            return;
        }

        decltype(m_peers_data) new_peers_data;
        new_peers_data.reserve(nodes_stats.size());
        constexpr std::array ids{"ipv4", "ipv6", "tor", "i2p", "cjdns", "other"};
        std::array<int, ids.size()> counts{};
        int inbound{0};
        for (const auto& node_stats : nodes_stats) {
            const CNodeCombinedStats stats{std::get<0>(node_stats), std::get<2>(node_stats), std::get<1>(node_stats)};
            new_peers_data.append(stats);
            inbound += stats.nodeStats.fInbound;
            size_t bucket{5};
            switch (stats.nodeStats.m_network) {
            case NET_IPV4: bucket = 0; break;
            case NET_IPV6: bucket = 1; break;
            case NET_ONION: bucket = 2; break;
            case NET_I2P: bucket = 3; break;
            case NET_CJDNS: bucket = 4; break;
            default: break;
            }
            ++counts[bucket];
        }
        QVariantList groups;
        for (size_t i{0}; i < counts.size(); ++i) {
            if (counts[i]) groups.append(QVariantMap{{"id", ids[i]}, {"count", counts[i]}});
        }
        const int total = new_peers_data.size();
        QVariantMap summary{{"ready", true}, {"total", total}, {"inbound", inbound},
                            {"outbound", total - inbound}, {"groups", groups}};
        QMetaObject::invokeMethod(this, [this, peers = std::move(new_peers_data), summary = std::move(summary)]() mutable {
            m_pending = false;
            if (!m_shutting_down) applySnapshot(std::move(peers), std::move(summary));
        }, Qt::QueuedConnection);

    }, Qt::QueuedConnection);
}

void PeerListModel::applySnapshot(QList<CNodeCombinedStats> new_peers_data, QVariantMap summary)
{
    setSummary(std::move(summary));
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
