// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NODEMODEL_H
#define BITCOIN_QML_MODELS_NODEMODEL_H

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <clientversion.h>

#include <deque>
#include <memory>

#include <QObject>
#include <QStringList>
#include <QString>
#include <QVariantList>

const char DEFAULT_PROXY_HOST[] = "127.0.0.1";
constexpr uint16_t DEFAULT_PROXY_PORT = 9050;

QT_BEGIN_NAMESPACE
class QThread;
class QTimer;
class QTimerEvent;
class QEventLoop;
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
    Q_PROPERTY(int numPeers READ numPeers NOTIFY numPeersChanged)
    Q_PROPERTY(int numInboundPeers READ numInboundPeers NOTIFY numInboundPeersChanged)
    Q_PROPERTY(int numOutboundPeers READ numOutboundPeers NOTIFY numOutboundPeersChanged)
    Q_PROPERTY(int maxNumOutboundPeers READ maxNumOutboundPeers CONSTANT)
    Q_PROPERTY(int mempoolTransactionCount READ mempoolTransactionCount NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolUsageMB READ mempoolUsageMB NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolMaxUsageMB READ mempoolMaxUsageMB NOTIFY mempoolInfoChanged)
    Q_PROPERTY(bool mempoolInfoPollingActive READ mempoolInfoPollingActive WRITE setMempoolInfoPollingActive NOTIFY mempoolInfoPollingActiveChanged)
    Q_PROPERTY(bool mempoolInformationAvailable READ mempoolInformationAvailable CONSTANT)
    Q_PROPERTY(int remainingSyncTime READ remainingSyncTime NOTIFY remainingSyncTimeChanged)
    Q_PROPERTY(double verificationProgress READ verificationProgress NOTIFY verificationProgressChanged)
    Q_PROPERTY(bool blockSyncActive READ blockSyncActive NOTIFY blockSyncActiveChanged)
    Q_PROPERTY(bool headerSyncActive READ headerSyncActive NOTIFY headerSyncChanged)
    Q_PROPERTY(bool headerPresync READ headerPresync NOTIFY headerSyncChanged)
    Q_PROPERTY(double headerSyncProgress READ headerSyncProgress NOTIFY headerSyncChanged)
    Q_PROPERTY(bool pause READ pause WRITE setPause NOTIFY pauseChanged)
    Q_PROPERTY(bool faulted READ errorState WRITE setErrorState NOTIFY errorStateChanged)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupErrorChanged)
    Q_PROPERTY(QString warnings READ warnings NOTIFY warningsChanged)
    Q_PROPERTY(QStringList warningList READ warningList NOTIFY warningsChanged)
    Q_PROPERTY(bool hasWarnings READ hasWarnings NOTIFY warningsChanged)
    Q_PROPERTY(bool runtimeDialogVisible READ runtimeDialogVisible NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogTitle READ runtimeDialogTitle NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogMessage READ runtimeDialogMessage NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogIcon READ runtimeDialogIcon NOTIFY runtimeDialogChanged)
    Q_PROPERTY(unsigned int runtimeDialogButtons READ runtimeDialogButtons NOTIFY runtimeDialogChanged)
    Q_PROPERTY(bool runtimeDialogQuestion READ runtimeDialogQuestion NOTIFY runtimeDialogChanged)

public:
    explicit NodeModel(interfaces::Node& node);
    ~NodeModel() override;

    int blockTipHeight() const { return m_block_tip_height; }
    void setBlockTipHeight(int new_height);
    QString fullClientVersion() const { return QString::fromStdString(FormatFullVersion()); }
    int numPeers() const { return m_num_peers; }
    void setNumPeers(int new_num);
    int numInboundPeers() const { return m_num_inbound_peers; }
    void setNumInboundPeers(int new_num);
    int numOutboundPeers() const { return m_num_outbound_peers; }
    void setNumOutboundPeers(int new_num);
    int maxNumOutboundPeers() const { return m_max_num_outbound_peers; }
    int mempoolTransactionCount() const { return m_mempool_transaction_count; }
    double mempoolUsageMB() const { return m_mempool_usage_mb; }
    double mempoolMaxUsageMB() const { return m_mempool_max_usage_mb; }
    bool mempoolInfoPollingActive() const { return m_mempool_info_polling_active; }
    void setMempoolInfoPollingActive(bool active);
    bool mempoolInformationAvailable() const { return m_mempool_information_available; }
    int remainingSyncTime() const { return m_remaining_sync_time; }
    void setRemainingSyncTime(double new_progress);
    double verificationProgress() const { return m_verification_progress; }
    void setVerificationProgress(double new_progress);
    bool blockSyncActive() const { return m_block_sync_active; }
    bool headerSyncActive() const { return m_header_sync_active; }
    bool headerPresync() const { return m_header_presync; }
    double headerSyncProgress() const { return m_header_sync_progress; }
    bool pause() const { return m_pause; }
    void setPause(bool new_pause);
    bool errorState() const { return m_faulted; }
    void setErrorState(bool new_error);
    QString startupError() const { return m_startup_error; }
    void setStartupError(const QString& error);
    void addStartupWarnings(const QStringList& warnings);
    QString warnings() const { return m_warnings; }
    QStringList warningList() const { return m_warning_list; }
    bool hasWarnings() const { return !m_warning_list.empty(); }
    bool runtimeDialogVisible() const { return m_runtime_dialog_visible; }
    QString runtimeDialogTitle() const { return m_runtime_dialog_title; }
    QString runtimeDialogMessage() const { return m_runtime_dialog_message; }
    QString runtimeDialogIcon() const { return m_runtime_dialog_icon; }
    unsigned int runtimeDialogButtons() const { return m_runtime_dialog_buttons; }
    bool runtimeDialogQuestion() const { return m_runtime_dialog_question; }

    Q_INVOKABLE float getTotalBytesReceived() const { return (float)m_node.getTotalBytesRecv(); }
    Q_INVOKABLE float getTotalBytesSent() const { return (float)m_node.getTotalBytesSent(); }
    Q_INVOKABLE void refreshMempoolInfo();

    Q_INVOKABLE void startNodeInitializionThread();
    Q_INVOKABLE void requestShutdown();

    void startShutdownPolling();
    void stopShutdownPolling();

    Q_INVOKABLE bool validateProxyAddress(QString addr_port);
    Q_INVOKABLE QString defaultProxyAddress();
    Q_INVOKABLE bool disconnectPeer(int nodeId);
    Q_INVOKABLE bool banPeer(const QString& rawAddress, int64_t banDuration);
    Q_INVOKABLE QVariantList nodeInformationRows();
    Q_INVOKABLE void answerRuntimeDialog(unsigned int button);
#ifdef ENABLE_TEST_AUTOMATION
    Q_INVOKABLE void showRuntimeDialogForTest(const QString& message, unsigned int style, bool question);
#endif

public Q_SLOTS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);
    void handleRunawayException(const QString& message);

Q_SIGNALS:
    void blockTipHeightChanged();
    void mempoolInfoChanged();
    void mempoolInfoPollingActiveChanged(bool active);
    void numPeersChanged();
    void numInboundPeersChanged();
    void numOutboundPeersChanged();
    void remainingSyncTimeChanged();
    void requestedInitialize();
    void requestedShutdown();
    void verificationProgressChanged();
    void blockSyncActiveChanged();
    void headerSyncChanged();
    void pauseChanged(bool new_pause);
    void errorStateChanged(bool new_error_state);
    void startupErrorChanged();
    void warningsChanged();
    void runtimeDialogChanged();

    void setTimeRatioList(int new_time);
    void setTimeRatioListInitial();
    void nodeInitialized();
    void bannedListChanged();

protected:
    void timerEvent(QTimerEvent* event) override;

private:
    struct MempoolInfo {
        int transaction_count{0};
        double usage_mb{0.0};
        double max_usage_mb{0.0};
    };

    struct RuntimeDialogRequest {
        QString message;
        unsigned int style{0};
        bool question{false};
        bool answer{false};
        bool answered{false};
        unsigned int button{0};
        QEventLoop* loop{nullptr};
    };

    // Properties that are exposed to QML.
    int m_block_tip_height{0};
    int m_num_peers{0};
    int m_num_inbound_peers{0};
    int m_num_outbound_peers{0};
    static constexpr int m_max_num_outbound_peers{MAX_OUTBOUND_FULL_RELAY_CONNECTIONS + MAX_BLOCK_RELAY_ONLY_CONNECTIONS};
    int m_mempool_transaction_count{0};
    double m_mempool_usage_mb{0.0};
    double m_mempool_max_usage_mb{0.0};
    bool m_mempool_info_polling_active{false};
    bool m_mempool_information_available{true};
    int m_remaining_sync_time{0};
    double m_verification_progress{0.0};
    bool m_block_sync_active{false};
    bool m_pause{false};
    bool m_faulted{false};
    QString m_startup_error;
    QStringList m_startup_error_messages;
    QStringList m_startup_warning_messages;
    QString m_warnings;
    QStringList m_warning_list;
    bool m_header_sync_active{false};
    bool m_header_presync{false};
    double m_header_sync_progress{0.0};
    int m_header_tip_height{0};
    int64_t m_header_tip_time{0};
    bool m_node_ready{false};
    bool m_initialization_requested{false};
    bool m_shutdown_requested{false};
    bool m_runtime_dialogs_enabled{false};
    bool m_startup_failure_dialog_shown{false};
    bool m_runtime_dialog_visible{false};
    bool m_runtime_dialog_question{false};
    QString m_runtime_dialog_title;
    QString m_runtime_dialog_message;
    QString m_runtime_dialog_icon;
    unsigned int m_runtime_dialog_buttons{0};
    std::shared_ptr<RuntimeDialogRequest> m_runtime_dialog_active;
    std::deque<std::shared_ptr<RuntimeDialogRequest>> m_runtime_dialog_queue;

    int m_shutdown_polling_timer_id{0};

    QVector<QPair<int, double>> m_block_process_time;

    interfaces::Node& m_node;
    QObject* m_mempool_info_worker{nullptr};
    QThread* m_mempool_info_thread{nullptr};
    QTimer* m_mempool_info_timer{nullptr};
    std::unique_ptr<interfaces::Handler> m_handler_notify_block_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_header_tip;
    std::unique_ptr<interfaces::Handler> m_handler_notify_num_peers_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_network_active_changed;
    std::unique_ptr<interfaces::Handler> m_handler_notify_alert_changed;
    std::unique_ptr<interfaces::Handler> m_handler_message_box;
    std::unique_ptr<interfaces::Handler> m_handler_question;
    std::unique_ptr<interfaces::Handler> m_handler_notify_banned_list_changed;

    void ConnectToBlockTipSignal();
    void ConnectToHeaderTipSignal();
    void ConnectToNumConnectionsChangedSignal();
    void ConnectToNetworkActiveChangedSignal();
    void ConnectToAlertChangedSignal();
    void ConnectToRuntimeDialogSignals();
    void ConnectToBannedListChangedSignal();
    void unsubscribeFromCoreSignals();
    void initializeMempoolInfoPolling();
    void refreshPeerCounts();
    void refreshWarnings();
    void recordStartupErrorMessage(const QString& message);
    void recordStartupWarningMessage(const QString& message);
    void showStartupWarnings();
    void setWarnings(const QString& warnings);
    void setBlockSyncActive(bool active);
    void setHeaderSyncState(int height, int64_t block_time, bool presync);
    bool showRuntimeDialog(const QString& message, unsigned int style, bool question);
    bool showRuntimeDialogOnGuiThread(const QString& message, unsigned int style, bool question);
    void showRuntimeDialogRequest(const std::shared_ptr<RuntimeDialogRequest>& request);
    void requestMempoolInfoRefresh();
    void fetchMempoolInfo();
    void applyMempoolInfo(const MempoolInfo& info);
};

#endif // BITCOIN_QML_MODELS_NODEMODEL_H
