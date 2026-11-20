// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NODEMODEL_H
#define BITCOIN_QML_MODELS_NODEMODEL_H

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <clientversion.h>
#include <qml/models/syncprogresstracker.h>
#include <qml/backendexecutor.h>
#include <qml/models/networkmodel.h>
#include <qml/models/runtimepromptmodel.h>
#include <qml/nodelifecycle.h>
#include <qml/snapshotquery.h>
#include <qml/models/peercommandmodel.h>

#include <deque>
#include <memory>
#include <condition_variable>
#include <mutex>
#include <chrono>

#include <qml/models/startupdiagnosticsmodel.h>
#include <common/args.h>

#include <QObject>
#include <QStringList>
#include <QString>
#include <QVariantList>

QT_BEGIN_NAMESPACE
class QTimer;
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
    Q_PROPERTY(bool mempoolInformationAvailable READ mempoolInformationAvailable NOTIFY mempoolInfoChanged)
    Q_PROPERTY(qint64 remainingSyncTime READ remainingSyncTime NOTIFY remainingSyncTimeChanged)
    /** Estimated chain-verification progress used only for progress presentation. */
    Q_PROPERTY(double verificationProgress READ verificationProgress NOTIFY verificationProgressChanged)
    /** True after synchronization completes, until the node falls substantially behind. */
    Q_PROPERTY(bool initialSyncComplete READ initialSyncComplete NOTIFY initialSyncCompleteChanged)
    /** True while Core reports initial block download through initialization or tip notifications. */
    Q_PROPERTY(bool blockSyncActive READ blockSyncActive NOTIFY blockSyncActiveChanged)
    Q_PROPERTY(bool headerSyncActive READ headerSyncActive NOTIFY headerSyncChanged)
    Q_PROPERTY(bool headerPresync READ headerPresync NOTIFY headerSyncChanged)
    Q_PROPERTY(double headerSyncProgress READ headerSyncProgress NOTIFY headerSyncChanged)
    Q_PROPERTY(bool notificationsReady READ notificationsReady NOTIFY notificationsReadyChanged)
    Q_PROPERTY(QString observationError READ observationError NOTIFY observationErrorChanged)
    Q_PROPERTY(NetworkModel* networkModel READ networkModel CONSTANT)
    Q_PROPERTY(nodelifecycle::Phase lifecyclePhase READ lifecyclePhase NOTIFY lifecyclePhaseChanged)
    Q_PROPERTY(bool networkBackendAvailable READ networkBackendAvailable NOTIFY networkBackendAvailabilityChanged)
    Q_PROPERTY(bool networkActionReady READ networkActionReady NOTIFY networkActionReadyChanged)
    Q_PROPERTY(bool networkActionPending READ networkActionPending NOTIFY networkActionPendingChanged)
    Q_PROPERTY(PeerCommandModel* peerCommands READ peerCommands CONSTANT)
    Q_PROPERTY(QString networkActionError READ networkActionError NOTIFY networkActionErrorChanged)
    Q_PROPERTY(bool pause READ pause NOTIFY pauseChanged)
    Q_PROPERTY(bool faulted READ errorState NOTIFY errorStateChanged)
    Q_PROPERTY(QString startupError READ startupError NOTIFY startupErrorChanged)
    Q_PROPERTY(QAbstractItemModel* startupDiagnostics READ startupDiagnostics CONSTANT)
    Q_PROPERTY(QAbstractItemModel* runtimeDialogDiagnostics READ runtimeDialogDiagnostics NOTIFY runtimeDialogChanged)
    Q_PROPERTY(bool startupDiagnosticsPending READ startupDiagnosticsPending NOTIFY startupDiagnosticsChanged)
    Q_PROPERTY(bool fatalException READ fatalException NOTIFY fatalExceptionChanged)
    // True from the first moment a quit is asked for, before any page reacts
    // to it, so a field losing focus because of the quit can tell (#1114).
    Q_PROPERTY(bool shutdownRequested READ shutdownRequested NOTIFY shutdownRequestedChanged)
    // Whether this node runs with pruning. A change needs a restart, so the
    // value read at start holds for the session (#1127).
    Q_PROPERTY(bool pruneMode READ pruneMode CONSTANT)
    Q_PROPERTY(QString warnings READ warnings NOTIFY warningsChanged)
    Q_PROPERTY(QVariantList informationRows READ nodeInformationRows NOTIFY nodeInformationChanged)
    Q_PROPERTY(QStringList warningList READ warningList NOTIFY warningsChanged)
    Q_PROPERTY(bool hasWarnings READ hasWarnings NOTIFY warningsChanged)
    Q_PROPERTY(QVariantList runtimePrompts READ runtimePrompts NOTIFY runtimeDialogChanged)
    Q_PROPERTY(RuntimePromptModel* runtimePrompt READ runtimePrompt NOTIFY runtimeDialogChanged)
    Q_PROPERTY(bool runtimeDialogVisible READ runtimeDialogVisible NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogTitle READ runtimeDialogTitle NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogMessage READ runtimeDialogMessage NOTIFY runtimeDialogChanged)
    Q_PROPERTY(QString runtimeDialogIcon READ runtimeDialogIcon NOTIFY runtimeDialogChanged)
    Q_PROPERTY(unsigned int runtimeDialogButtons READ runtimeDialogButtons NOTIFY runtimeDialogChanged)
    Q_PROPERTY(bool runtimeDialogQuestion READ runtimeDialogQuestion NOTIFY runtimeDialogChanged)

public:
    // Pass false while appInitMain can still install backend services.
    // Node must outlive workDrained(); shutdown never waits for its queries on GUI.
    enum class Poll { Mempool, Shutdown };
    struct Services {
        execution::SnapshotServices execution;
        // Actual diagnostic intake loss affects the process receipt; ordinary
        // explicit command rejection does not.
        std::shared_ptr<std::atomic_bool> diagnostic_loss;
        // One-shot deadline transports; callbacks only enqueue observations.
        std::function<void(Poll, std::chrono::milliseconds, std::function<void()>)> schedule;
        std::function<void(Poll)> cancel;
        std::function<int64_t()> wall_seconds;
        std::function<int64_t()> monotonic_milliseconds;
    };
    explicit NodeModel(interfaces::Node& node, bool backend_ready = true);
    NodeModel(interfaces::Node& node, bool backend_ready, Services services);
    ~NodeModel() override;
    void beginShutdown();
    void closeAdmission();
    // Internal lifecycle receipt; the scheduler capability is never a QML command.
    void handleLifecycleException(const QString& message, std::shared_ptr<execution::Scheduler> original_diagnostic_scope);
    std::shared_ptr<execution::Scheduler> queryScheduler() const { return m_query_scheduler; }
    // Value-only retirement shares the existing worker and its shutdown join.
    // Admission stays open through closeAdmission(), until beginShutdown().
    // A false admission leaves ownership with the caller's retirement mailbox.
    bool retireValues(std::function<void()> release);
    bool isDrained() const { return m_drained; }
    bool notificationsReady() const { return m_subscriptions_ready; }
    QString observationError() const;
    NetworkModel* networkModel() const { return m_network_model; }
    nodelifecycle::Phase lifecyclePhase() const { return m_lifecycle_phase; }
    void setLifecyclePhase(nodelifecycle::Phase phase);
    bool networkBackendAvailable() const { return m_backend_queries_ready && m_subscriptions_ready && !m_admission_closed && !m_shutdown_requested && !m_workers_stopping; }
    // Command readiness additionally requires an actual current observation.
    bool networkActionReady() const { return networkBackendAvailable() && m_network_model && m_network_model->ready(); }
    PeerCommandModel* peerCommands() const { return m_peer_actions; }
    bool networkActionPending() const { return m_network_action_pending; }
    QString networkActionError() const { return m_network_action_error; }

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
    qint64 remainingSyncTime() const { return m_remaining_sync_time; }
    void setRemainingSyncTime(double new_progress);
    double verificationProgress() const { return m_verification_progress; }
    void setVerificationProgress(double new_progress);
    bool initialSyncComplete() const { return m_initial_sync_complete; }
    bool blockSyncActive() const { return m_block_sync_active; }
    bool headerSyncActive() const { return m_header_sync_active; }
    bool headerPresync() const { return m_header_presync; }
    double headerSyncProgress() const { return m_header_sync_progress; }
    bool pause() const { return m_pause; }
    void setPause(bool new_pause);
    bool errorState() const { return m_faulted; }
    void setErrorState(bool new_error);
    QString startupError() const { return m_startup_error; }
    bool fatalException() const { return m_fatal_exception; }
    bool shutdownRequested() const { return m_shutdown_requested; }
    bool pruneMode() const { return m_prune_mode; }
    void setStartupError(const QString& error);
    void addStartupWarnings(const QStringList& warnings);
    void addStartupWarnings(std::shared_ptr<const startupdiagnostics::Collection> warnings);
    QAbstractItemModel* startupDiagnostics() const { return m_startup_diagnostics; }
    QAbstractItemModel* runtimeDialogDiagnostics() const { return m_runtime_diagnostics; }
    bool startupDiagnosticsPending() const { return m_startup_diagnostics && m_startup_diagnostics->busy(); }
    QString warnings() const { return m_warnings; }
    QStringList warningList() const { return m_warning_list; }
    bool hasWarnings() const { return !m_warning_list.empty(); }
    RuntimePromptModel* runtimePrompt()const{return m_runtime_prompt;}
    QVariantList runtimePrompts()const;
    bool runtimeDialogVisible() const { return m_runtime_dialog_visible; }
    QString runtimeDialogTitle() const { return m_runtime_dialog_title; }
    QString runtimeDialogMessage() const { return m_runtime_dialog_message; }
    QString runtimeDialogIcon() const { return m_runtime_dialog_icon; }
    unsigned int runtimeDialogButtons() const { return m_runtime_dialog_buttons; }
    bool runtimeDialogQuestion() const { return m_runtime_dialog_question; }

    Q_INVOKABLE void refreshMempoolInfo();

    Q_INVOKABLE void startNodeInitializionThread();
    Q_INVOKABLE void requestShutdown();

    void startShutdownPolling();
    void stopShutdownPolling();

    Q_INVOKABLE QVariantList nodeInformationRows();
    Q_INVOKABLE void answerRuntimeDialog(unsigned int button);
#ifdef ENABLE_TEST_AUTOMATION
    Q_INVOKABLE void showRuntimeDialogForTest(const QString& message, unsigned int style, bool question);
#endif

public Q_SLOTS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info, bool initial_block_download = false, bool shutdown_requested = false);
    void handleRunawayException(const QString& message);

Q_SIGNALS:
    void startupDiagnosticsChanged();
    void workDrained();
    void observationErrorChanged();
    void notificationsReadyChanged();
    void nodeInformationChanged();
    void networkActionReadyChanged();
    void networkBackendAvailabilityChanged();
    void lifecyclePhaseChanged();
    void networkActionPendingChanged();
    void networkActionErrorChanged();
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
    void initialSyncCompleteChanged();
    void blockSyncActiveChanged();
    void headerSyncChanged();
    void pauseChanged(bool new_pause);
    void errorStateChanged(bool new_error_state);
    void startupErrorChanged();
    void fatalExceptionChanged();
    void shutdownRequestedChanged();
    void warningsChanged();
    void runtimeDialogChanged();

    /** Active-chain block timestamp changed; consumers may update timelines. */
    void blockTipTimeChanged(qint64 block_time);
    /** Chain initialization succeeded and history-dependent models may load. */
    void chainStateReady();
    void nodeInitialized();
    void bannedListChanged();

private:
    struct Notifications;
    struct StatusSnapshot;
    std::shared_ptr<Notifications> m_notifications;
    struct WorkLedger;
    class TrackedScheduler;
    struct Subscriptions;
    struct ShutdownSnapshot;
    Services m_services;
    std::unique_ptr<BackendExecutor> m_snapshots;
    std::shared_ptr<execution::Scheduler> m_query_scheduler;
    // Private value-only capability; ordinary queries never receive this port.
    std::shared_ptr<execution::Scheduler> m_retirement_scheduler;
    std::shared_ptr<WorkLedger> m_work_ledger;
    std::shared_ptr<Subscriptions> m_subscriptions;
    std::optional<execution::ResourceLease> m_subscription_lease;
    bool m_subscriptions_ready{false};
    bool m_initialization_dispatched{false};
    std::shared_ptr<execution::SnapshotQuery<StatusSnapshot>> m_status_query;
    std::shared_ptr<execution::SnapshotQuery<ShutdownSnapshot>> m_shutdown_query;
    QString m_status_error, m_mempool_error, m_shutdown_error;
    bool m_admission_closed{false};
    PeerCommandModel* m_peer_actions{nullptr};
    QString m_information_local_addresses;
    QString m_information_data_dir, m_information_blocks_dir, m_information_network;
    uint64_t m_chain_read_generation{0};
    NetworkModel* m_network_model{nullptr};
    bool m_network_available{false};
    nodelifecycle::Phase m_lifecycle_phase{nodelifecycle::Phase::Constructed};
    void updateNetworkAvailability();
    void publishNetworkAction();
    bool m_network_known{false}, m_network_ready_snapshot{false};
    uint64_t m_pause_generation{0};
    bool m_network_action_pending{false};
    QString m_network_action_error;
    bool m_drained{false};
    bool m_destroying{false};
    bool m_seen_block_tip{false};
    bool m_seen_header_tip{false};
    qint64 m_block_tip_time{0};
    void detachNotifications();
    void publishNotifications();
    void refreshStatus();
    void publishStatus();
    void publishMempoolInfo();
    void publishShutdownStatus();
    void initializeSubscriptions();
    void schedulePoll(Poll poll);
    void finishDrain();

    struct MempoolInfo {
        int transaction_count{0};
        double usage_mb{0.0};
        double max_usage_mb{0.0};
    };

    struct RuntimeDialogRequest {
        QString message;
        std::shared_ptr<const QString> full_message;
        unsigned int style{0};
    };
    std::shared_ptr<runtimeprompt::Service> m_prompt_service;
    RuntimePromptModel* m_runtime_prompt{nullptr};
    uint64_t m_prompt_rejections{0};
    void publishRuntimePrompt();

    // Properties that are exposed to QML.
    int m_block_tip_height{0};
    int m_num_peers{0};
    int m_num_inbound_peers{0};
    int m_num_outbound_peers{0};
    static constexpr int m_max_num_outbound_peers{MAX_OUTBOUND_FULL_RELAY_CONNECTIONS + MAX_BLOCK_RELAY_ONLY_CONNECTIONS};
    std::shared_ptr<execution::SnapshotQuery<MempoolInfo>> m_mempool_query;
    QString m_information_startup_time, m_information_user_agent;
    int m_mempool_transaction_count{0};
    double m_mempool_usage_mb{0.0};
    double m_mempool_max_usage_mb{0.0};
    bool m_mempool_info_polling_active{false};
    bool m_workers_stopping{false};
    bool m_mempool_information_available{true};
    qint64 m_remaining_sync_time{0};
    double m_verification_progress{0.0};
    bool m_block_sync_active{false};
    bool m_pause{false};
    bool m_faulted{false};
    QString m_startup_error;
    StartupDiagnosticsModel* m_startup_diagnostics{nullptr};
    StartupDiagnosticsModel* m_runtime_diagnostics{nullptr};
    std::shared_ptr<startupdiagnostics::TextBudget> m_dialog_text_budget{std::make_shared<startupdiagnostics::TextBudget>()};
    struct InitializationResult { bool success; interfaces::BlockAndHeaderTipInfo tip; bool initial_block_download; bool shutdown_requested; };
    std::optional<InitializationResult> m_initialization_result;
    std::optional<InitializationResult> m_ready_initialization;
    bool m_startup_warning_pending{false};
    std::shared_ptr<runtimeprompt::AnswerCell> m_startup_warning_answer;
    bool m_startup_diagnostics_sealed{false}, m_startup_warnings_shown{false};
    QString m_warnings;
    QStringList m_warning_list;
    bool m_header_sync_active{false};
    bool m_header_presync{false};
    double m_header_sync_progress{0.0};
    int m_header_tip_height{0};
    int64_t m_header_tip_time{0};
    bool m_node_ready{false};
    bool m_backend_queries_ready;
    bool m_initial_sync_complete{false};
    bool m_applying_sync_snapshot{false};
    bool m_initialization_requested{false};
    bool m_shutdown_requested{false};
    const bool m_prune_mode{gArgs.GetIntArg("-prune", 0) != 0};
    bool m_fatal_exception{false};
    bool m_runtime_dialogs_enabled{false};
    bool m_startup_failure_dialog_shown{false};
    bool m_runtime_dialog_visible{false};
    bool m_runtime_dialog_question{false};
    QString m_runtime_dialog_title;
    QString m_runtime_dialog_message;
    QString m_runtime_dialog_icon;
    unsigned int m_runtime_dialog_buttons{0};

    bool m_shutdown_polling{false};
    QTimer* m_shutdown_timer{nullptr};
    std::function<void()> m_mempool_deadline, m_shutdown_deadline;
    int64_t m_header_spacing{600};

    SyncProgressTracker m_sync_progress_tracker;

    interfaces::Node& m_node;
    QTimer* m_mempool_info_timer{nullptr};

    void initializeMempoolInfoPolling();
    void finishInitialization(bool success, interfaces::BlockAndHeaderTipInfo tip_info, bool initial_block_download, bool shutdown_requested);
    void finishStartupDiagnostics();
    void recordStartupErrorMessage(const QString& message);
    void recordStartupWarningMessage(const QString& message);
    void diagnosticLost(const QString& error);
    bool showStartupWarnings();
    void finishReadyInitialization();
    void completeStartupWarning(bool presented);
    void setNodeReady(bool ready);
    void setBlockSyncActive(bool active);
    void setHeaderSyncState(int height, int64_t block_time, bool presync);
    void updateSyncCompletion();
    bool showRuntimeDialogOnGuiThread(const QString& message, unsigned int style, bool question, bool startup_summary = false);
    void clearRuntimeDialogs(bool notify);
    void requestMempoolInfoRefresh();
    void applyMempoolInfo(const MempoolInfo& info);
};

#endif // BITCOIN_QML_MODELS_NODEMODEL_H
