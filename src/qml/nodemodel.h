// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_NODEMODEL_H
#define BITCOIN_QML_NODEMODEL_H

#include <interfaces/handler.h>
#include <interfaces/node.h>

#include <memory>

#include <QObject>

QT_BEGIN_NAMESPACE
class QTimerEvent;
QT_END_NAMESPACE

namespace interfaces {
class Node;
}

/** Model for Bitcoin network client. */
class NodeModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int blockTipHeight READ blockTipHeight NOTIFY blockTipHeightChanged)
    Q_PROPERTY(int numOutboundPeers READ numOutboundPeers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers READ maxNumOutboundPeers CONSTANT)
    Q_PROPERTY(int remainingSyncTime READ remainingSyncTime NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(double verificationProgress READ verificationProgress NOTIFY verificationProgressChanged)
    Q_PROPERTY(bool pause READ pause WRITE setPause NOTIFY pauseChanged)

public:
    explicit NodeModel(interfaces::Node& node);

    int blockTipHeight() const { return m_block_tip_height; }
    void setBlockTipHeight(int new_height);
    int numOutboundPeers() const { return m_num_outbound_peers; }
    void setNumOutboundPeers(int new_num);
    int maxNumOutboundPeers() const { return m_max_num_outbound_peers; }
    int remainingSyncTime() const { return m_remaining_sync_time; }
    void setRemainingSyncTime(double new_progress);
    double verificationProgress() const { return m_verification_progress; }
    void setVerificationProgress(double new_progress);
    bool pause() const { return m_pause; }
    void setPause(bool new_pause);

    Q_INVOKABLE float getTotalBytesReceived() const { return (float)m_node.getTotalBytesRecv(); }
    Q_INVOKABLE float getTotalBytesSent() const { return (float)m_node.getTotalBytesSent(); }

    Q_INVOKABLE void startNodeInitializionThread();

    void startShutdownPolling();
    void stopShutdownPolling();

public Q_SLOTS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);

Q_SIGNALS:
    void blockTipHeightChanged();
    void numOutboundPeersChanged();
    void remainingSyncTimeChanged();
    void requestedInitialize();
    void requestedShutdown();
    void verificationProgressChanged();
    void pauseChanged(bool new_pause);

    void setTimeRatioList(int new_time);
    void setTimeRatioListInitial();

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    // Properties that are exposed to QML.
    int m_block_tip_height{0};
    int m_num_outbound_peers{0};
    static constexpr int m_max_num_outbound_peers{MAX_OUTBOUND_FULL_RELAY_CONNECTIONS + MAX_BLOCK_RELAY_ONLY_CONNECTIONS};
    int m_remaining_sync_time{0};
    double m_verification_progress{0.0};
    bool m_pause{false};

    int m_shutdown_polling_timer_id{0};

    QVector<QPair<int, double>> m_block_process_time;

    interfaces::Node& m_node;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_peers_changed;

    void ConnectToBlockTipSignal();
    void ConnectToNumConnectionsChangedSignal();
};

#endif // BITCOIN_QML_NODEMODEL_H
