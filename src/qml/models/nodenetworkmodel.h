// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NODENETWORKMODEL_H
#define BITCOIN_QML_MODELS_NODENETWORKMODEL_H
#include <interfaces/handler.h>
#include <net.h>
#include <QObject>
#include <memory>
namespace interfaces { class Node; }

class NodeNetworkModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int numPeers READ numPeers NOTIFY numPeersChanged)
    Q_PROPERTY(int numInboundPeers READ numInboundPeers NOTIFY numInboundPeersChanged)
    Q_PROPERTY(int numOutboundPeers READ numOutboundPeers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers READ maxNumOutboundPeers CONSTANT)
    Q_PROPERTY(bool pause READ pause WRITE setPause NOTIFY pauseChanged)
public:
    explicit NodeNetworkModel(interfaces::Node& node);
    ~NodeNetworkModel() override;
    int numPeers() const { return m_num_peers; }
    void setNumPeers(int new_num);
    int numInboundPeers() const { return m_num_inbound_peers; }
    void setNumInboundPeers(int new_num);
    int numOutboundPeers() const { return m_num_outbound_peers; }
    void setNumOutboundPeers(int new_num);
    int maxNumOutboundPeers() const { return m_max_num_outbound_peers; }
    bool pause() const { return m_pause; }
    void setPause(bool new_pause);
public Q_SLOTS:
    void refreshPeerCounts();
    void stop();
Q_SIGNALS:
    void numPeersChanged();
    void numInboundPeersChanged();
    void numOutboundPeersChanged();
    void pauseChanged(bool pause);
private:
    interfaces::Node& m_node;
    int m_num_peers{0};
    int m_num_inbound_peers{0};
    int m_num_outbound_peers{0};
    static constexpr int m_max_num_outbound_peers{MAX_OUTBOUND_FULL_RELAY_CONNECTIONS + MAX_BLOCK_RELAY_ONLY_CONNECTIONS};
    bool m_pause{false};
    bool m_stopped{false};
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_peers_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_network_active_changed;
    void ConnectToNumConnectionsChangedSignal();
    void ConnectToNetworkActiveChangedSignal();
};
#endif // BITCOIN_QML_MODELS_NODENETWORKMODEL_H
