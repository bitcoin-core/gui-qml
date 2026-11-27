// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodenetworkmodel.h>
#include <interfaces/node.h>
#include <cassert>

NodeNetworkModel::NodeNetworkModel(interfaces::Node& node) : m_node{node}
{
    m_pause = !m_node.getNetworkActive();
    refreshPeerCounts();
    ConnectToNumConnectionsChangedSignal();
    ConnectToNetworkActiveChangedSignal();
}

NodeNetworkModel::~NodeNetworkModel()
{
    stop();
}

void NodeNetworkModel::stop()
{
    m_stopped = true;
    m_handler_notify_num_peers_changed.reset();
    m_handler_notify_network_active_changed.reset();
}

void NodeNetworkModel::setNumOutboundPeers(int new_num)
{
    if (new_num != m_num_outbound_peers) {
        m_num_outbound_peers = new_num;
        Q_EMIT numOutboundPeersChanged();
    }
}

void NodeNetworkModel::setNumPeers(int new_num)
{
    if (new_num != m_num_peers) {
        m_num_peers = new_num;
        Q_EMIT numPeersChanged();
    }
}

void NodeNetworkModel::setNumInboundPeers(int new_num)
{
    if (new_num != m_num_inbound_peers) {
        m_num_inbound_peers = new_num;
        Q_EMIT numInboundPeersChanged();
    }
}

void NodeNetworkModel::refreshPeerCounts()
{
    if (m_stopped) return;
    const bool pause{!m_node.getNetworkActive()};
    if (m_pause != pause) {
        m_pause = pause;
        Q_EMIT pauseChanged(pause);
    }
    setNumPeers(static_cast<int>(m_node.getNodeCount(ConnectionDirection::Both)));
    setNumInboundPeers(static_cast<int>(m_node.getNodeCount(ConnectionDirection::In)));
    setNumOutboundPeers(static_cast<int>(m_node.getNodeCount(ConnectionDirection::Out)));
}

void NodeNetworkModel::setPause(bool new_pause)
{
    if (m_stopped) return;
    if (m_node.getNetworkActive() == new_pause) m_node.setNetworkActive(!new_pause);
    if (m_pause != new_pause) {
        m_pause = new_pause;
        Q_EMIT pauseChanged(new_pause);
    }
}

void NodeNetworkModel::ConnectToNumConnectionsChangedSignal()
{
    assert(!m_handler_notify_num_peers_changed);

    m_handler_notify_num_peers_changed = m_node.handleNotifyNumConnectionsChanged(
        [this]([[maybe_unused]] int new_num_connections) {
            QMetaObject::invokeMethod(this, [this] {
                refreshPeerCounts();
            }, Qt::QueuedConnection);
        });
}

void NodeNetworkModel::ConnectToNetworkActiveChangedSignal()
{
    assert(!m_handler_notify_network_active_changed);

    m_handler_notify_network_active_changed = m_node.handleNotifyNetworkActiveChanged(
        [this](bool) {
            QMetaObject::invokeMethod(this, [this] {
                // Query current state, rather than replaying a stale notification
                // over a newer GUI action waiting in the same event queue.
                refreshPeerCounts();
            }, Qt::QueuedConnection);
        });
}
