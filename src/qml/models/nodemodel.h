// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NODEMODEL_H
#define BITCOIN_QML_MODELS_NODEMODEL_H

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <clientversion.h>

#include <memory>

#include <QObject>
#include <QString>

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
    Q_PROPERTY(QString fullClientVersion READ fullClientVersion CONSTANT)
    Q_PROPERTY(int numOutboundPeers READ numOutboundPeers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers READ maxNumOutboundPeers CONSTANT)
    Q_PROPERTY(int remainingSyncTime READ remainingSyncTime NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(double verificationProgress READ verificationProgress NOTIFY verificationProgressChanged)
    Q_PROPERTY(bool pause READ pause WRITE setPause NOTIFY pauseChanged)
    Q_PROPERTY(bool faulted READ errorState WRITE setErrorState NOTIFY errorStateChanged)
    Q_PROPERTY(double snapshotProgress READ snapshotProgress WRITE setSnapshotProgress NOTIFY snapshotProgressChanged)
    Q_PROPERTY(bool snapshotLoading READ snapshotLoading NOTIFY snapshotLoadingChanged)
    Q_PROPERTY(bool isSnapshotLoaded READ isSnapshotLoaded NOTIFY snapshotLoaded)
    Q_PROPERTY(bool headersSynced READ headersSynced WRITE setHeadersSynced NOTIFY headersSyncedChanged)
    Q_PROPERTY(bool isIBDCompleted READ isIBDCompleted WRITE setIsIBDCompleted NOTIFY isIBDCompletedChanged)

public:
    explicit NodeModel(interfaces::Node& node);

    int blockTipHeight() const { return m_block_tip_height; }
    void setBlockTipHeight(int new_height);
    QString fullClientVersion() const { return QString::fromStdString(FormatFullVersion()); }
    int numOutboundPeers() const { return m_num_outbound_peers; }
    void setNumOutboundPeers(int new_num);
    int maxNumOutboundPeers() const { return m_max_num_outbound_peers; }
    int remainingSyncTime() const { return m_remaining_sync_time; }
    void setRemainingSyncTime(double new_progress);
    double verificationProgress() const { return m_verification_progress; }
    void setVerificationProgress(double new_progress);
    bool pause() const { return m_pause; }
    void setPause(bool new_pause);
    bool errorState() const { return m_faulted; }
    void setErrorState(bool new_error);
    bool isSnapshotLoaded() const { return m_snapshot_loaded; }
    double snapshotProgress() const { return m_snapshot_progress; }
    void setSnapshotProgress(double new_progress);
    bool snapshotLoading() const { return m_snapshot_loading; }
    bool headersSynced() const { return m_headers_synced; }
    void setHeadersSynced(bool new_synced);
    bool isIBDCompleted() const { return m_is_ibd_completed; }
    void setIsIBDCompleted(bool new_completed);

    Q_INVOKABLE float getTotalBytesReceived() const { return (float)m_node.getTotalBytesRecv(); }
    Q_INVOKABLE float getTotalBytesSent() const { return (float)m_node.getTotalBytesSent(); }

    Q_INVOKABLE void startNodeInitializionThread();
    Q_INVOKABLE void requestShutdown();

    Q_INVOKABLE void snapshotLoadThread(QString path_file);

    void startShutdownPolling();
    void stopShutdownPolling();

    Q_INVOKABLE bool validateProxyAddress(QString addr_port);
    Q_INVOKABLE QString defaultProxyAddress();

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
    void errorStateChanged(bool new_error_state);

    void setTimeRatioList(int new_time);
    void setTimeRatioListInitial();
    void initializationFinished();
    void snapshotLoaded(bool result);
    void snapshotProgressChanged();
    void snapshotLoadingChanged();
    void showProgress(const QString& title, int progress);
    void headersSyncedChanged();
    void isIBDCompletedChanged();
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
    bool m_faulted{false};
    double m_snapshot_progress{0.0};
    int m_shutdown_polling_timer_id{0};
    int m_snapshot_timer_id{0};
    bool m_snapshot_loading{false};
    bool m_snapshot_loaded{false};
    bool m_headers_synced{false};
    bool m_is_ibd_completed{false};

    QVector<QPair<int, double>> m_block_process_time;

    interfaces::Node& m_node;
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_peers_changed;
    std::unique_ptr<interfaces::Handler> m_handler_snapshot_load_progress;
    void ConnectToBlockTipSignal();
    void ConnectToNumConnectionsChangedSignal();
    void ConnectToSnapshotLoadProgressSignal();
};

#endif // BITCOIN_QML_MODELS_NODEMODEL_H
