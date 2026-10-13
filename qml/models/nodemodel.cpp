// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodemodel.h>
#include <qml/asyncjoin.h>

#include <common/args.h>
#include <common/system.h>
#include <chainparams.h>
#include <interfaces/node.h>
#include <net.h>
#include <net_processing.h>
#include <netbase.h>
#include <node/interface_ui.h>
#include <util/fs.h>
#include <util/string.h>
#include <util/threadnames.h>
#include <util/time.h>
#include <validation.h>

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <vector>
#include <mutex>
#include <optional>
#include <utility>

#include <QDateTime>
#include <QEventLoop>
#include <QMetaObject>
#include <QScopedValueRollback>
#include <QThread>
#include <QTimer>
#include <QTimerEvent>
#include <QVariantMap>

using namespace std::chrono_literals;

static constexpr int MEMPOOL_INFO_POLLING_INTERVAL_MS{3000};
static constexpr int HEADER_HEIGHT_DELTA_SYNC{24};

namespace {
QStringList SplitWarnings(const QString& warnings)
{
    QString normalized{warnings};
    normalized.replace(QStringLiteral("<hr/>"), QStringLiteral("<hr />"), Qt::CaseInsensitive);
    normalized.replace(QStringLiteral("<hr>"), QStringLiteral("<hr />"), Qt::CaseInsensitive);

    QStringList result;
    for (const QString& warning : normalized.split(QStringLiteral("<hr />"), Qt::SkipEmptyParts)) {
        const QString trimmed{warning.trimmed()};
        if (!trimmed.isEmpty()) {
            result.push_back(trimmed);
        }
    }
    return result;
}

QString RuntimeDialogTitle(unsigned int style)
{
    if (style & CClientUIInterface::ICON_ERROR) {
        return QObject::tr("Error");
    }
    if (style & CClientUIInterface::ICON_WARNING) {
        return QObject::tr("Warning");
    }
    return QObject::tr("Information");
}

QString RuntimeDialogIcon(unsigned int style)
{
    if (style & CClientUIInterface::ICON_ERROR) {
        return QStringLiteral("image://images/error");
    }
    if (style & CClientUIInterface::ICON_WARNING) {
        return QStringLiteral("image://images/alert-filled");
    }
    return QStringLiteral("image://images/info-filled");
}

unsigned int RuntimeDialogButtons(unsigned int style)
{
    const unsigned int buttons{style & CClientUIInterface::BTN_MASK};
    return buttons ? buttons : CClientUIInterface::BTN_OK;
}

bool HasMultipleRuntimeDialogButtons(unsigned int buttons)
{
    return (buttons & (buttons - 1)) != 0;
}

QVariant InformationRow(const QString& label, const QString& value)
{
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), value);
    return QVariant::fromValue(row);
}
} // namespace

// Core callbacks share a bounded mailbox and coalesce changes into one pending
// GUI delivery. The mutex also protects the receiver until shutdown detaches it.
struct NodeModel::Notifications {
    struct Block { SynchronizationState state; interfaces::BlockTip tip; double progress; };
    struct Header { interfaces::BlockTip tip; bool presync; };
    std::mutex mutex;
    NodeModel* receiver{nullptr};
    bool delivery_pending{false};
    std::optional<Block> block;
    std::optional<Header> header;
    std::optional<int> peers;
    std::optional<int> last_peer_total;
    std::optional<bool> network_active;
    bool warnings{false};
    bool bans{false};
    uint64_t status_generation{0};
    uint64_t network_generation{0};

    // Called with mutex held, so detaching the receiver cannot race dispatch.
    void schedule()
    {
        if (!receiver || delivery_pending) return;
        delivery_pending = true;
        QMetaObject::invokeMethod(receiver, [target = receiver] {
            {
                std::lock_guard lock{target->m_notifications->mutex};
                target->m_notifications->delivery_pending = false;
            }
            target->publishNotifications();
        }, Qt::QueuedConnection);
    }
};

struct NodeModel::StatusSnapshot {
    int inbound;
    int outbound;
    QString warnings;
    std::optional<bool> network_active;
    bool mempool_available;
};

NodeModel::NodeModel(interfaces::Node& node, bool backend_ready)
    : m_notifications{std::make_shared<Notifications>()}, m_backend_queries_ready{backend_ready}, m_node{node}
{
    m_notifications->receiver = this;
    m_sync_progress_clock.start();
    initializeMempoolInfoPolling();
    ConnectToBlockTipSignal();
    ConnectToHeaderTipSignal();
    ConnectToNumConnectionsChangedSignal();
    ConnectToNetworkActiveChangedSignal();
    ConnectToAlertChangedSignal();
    ConnectToRuntimeDialogSignals();
    ConnectToBannedListChangedSignal();
    connect(&m_snapshots, &BackendExecutor::drained, this, &NodeModel::finishDrain);
    connect(m_commands.get(), &BackendExecutor::drained, this, &NodeModel::finishDrain);
    refreshStatus();
    refreshMempoolInfo();
}

NodeModel::~NodeModel()
{
    detachNotifications();
    m_snapshots.shutdown();
    m_commands->shutdown();
    unsubscribeFromCoreSignals();
    setMempoolInfoPollingActive(false);
    if (m_mempool_info_thread) {
        m_mempool_info_thread->quit();
        m_mempool_info_thread->wait();
    }
}

void NodeModel::beginShutdown()
{
    if (m_workers_stopping) return;
    detachNotifications();
    setMempoolInfoPollingActive(false);
    m_workers_stopping = true;
    Q_EMIT networkActionReadyChanged();
    m_snapshots.shutdown();
    m_commands->shutdown();
    setNodeReady(false);
    stopShutdownPolling();
    if (!m_mempool_info_thread) {
        finishDrain();
        return;
    }
    auto handlers = std::make_shared<std::vector<std::unique_ptr<interfaces::Handler>>>();
    handlers->push_back(std::move(m_handler_notify_block_tip));
    handlers->push_back(std::move(m_handler_notify_header_tip));
    handlers->push_back(std::move(m_handler_notify_num_peers_changed));
    handlers->push_back(std::move(m_handler_notify_network_active_changed));
    handlers->push_back(std::move(m_handler_notify_alert_changed));
    handlers->push_back(std::move(m_handler_message_box));
    handlers->push_back(std::move(m_handler_question));
    handlers->push_back(std::move(m_handler_notify_banned_list_changed));
    JoinThreadAsync(m_mempool_info_thread, this, [this] {
        m_mempool_info_thread = nullptr;
        m_mempool_info_worker = nullptr;
        m_mempool_info_timer = nullptr;
        finishDrain();
    });
    QMetaObject::invokeMethod(m_mempool_info_worker,
        [handlers = std::move(handlers), timer = m_mempool_info_timer, thread = m_mempool_info_thread] {
            timer->stop();
            for (const auto& handler : *handlers) {
                if (handler) handler->disconnect();
            }
            handlers->clear();
            QMetaObject::invokeMethod(thread, &QThread::quit, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
}

void NodeModel::finishDrain()
{
    if (!m_workers_stopping || m_drained || m_mempool_info_thread ||
        !m_snapshots.isDrained() || !m_commands->isDrained()) return;
    m_drained = true;
    Q_EMIT workDrained();
}

void NodeModel::detachNotifications()
{
    std::lock_guard lock{m_notifications->mutex};
    m_notifications->receiver = nullptr;
}

void NodeModel::publishNotifications()
{
    if (m_workers_stopping || m_shutdown_requested) return;
    std::optional<Notifications::Block> block;
    std::optional<Notifications::Header> header;
    std::optional<int> peers;
    std::optional<bool> network_active;
    bool warnings;
    bool bans;
    {
        std::lock_guard lock{m_notifications->mutex};
        block = std::exchange(m_notifications->block, {});
        header = std::exchange(m_notifications->header, {});
        peers = std::exchange(m_notifications->peers, {});
        network_active = std::exchange(m_notifications->network_active, {});
        warnings = std::exchange(m_notifications->warnings, false);
        bans = std::exchange(m_notifications->bans, false);
    }
    {
        // Decide sync completion only after both heights belong to this snapshot.
        const QScopedValueRollback applying{m_applying_sync_snapshot, true};
        if (header) {
            m_seen_header_tip = true;
            setHeaderSyncState(header->tip.block_height, header->tip.block_time, header->presync);
        }
        if (block) {
            m_seen_block_tip = true;
            m_block_tip_time = block->tip.block_time;
            setBlockTipHeight(block->tip.block_height);
            setVerificationProgress(block->progress);
            setBlockSyncActive(block->state != SynchronizationState::POST_INIT);
            Q_EMIT blockTipTimeChanged(m_block_tip_time);
        }
    }
    updateSyncCompletion();
    if (peers || warnings) refreshStatus();
    if (network_active) {
        m_network_known = true;
        if (m_pause != !*network_active) {
            m_pause = !*network_active;
            Q_EMIT pauseChanged(m_pause);
        }
    }
    if (bans) Q_EMIT bannedListChanged();
    if (block || header || peers || warnings || network_active) Q_EMIT nodeInformationChanged();
}

void NodeModel::refreshStatus()
{
    if (!m_backend_queries_ready || m_shutdown_requested || m_workers_stopping) return;
    if (m_status_pending) {
        m_status_again = true;
        return;
    }
    m_status_pending = true;
    uint64_t generation;
    {
        std::lock_guard lock{m_notifications->mutex};
        generation = m_notifications->status_generation;
    }
    m_snapshots.submit(this, [node = &m_node, ready = m_node_ready] {
        return StatusSnapshot{static_cast<int>(node->getNodeCount(ConnectionDirection::In)),
                              static_cast<int>(node->getNodeCount(ConnectionDirection::Out)),
                              QString::fromStdString(node->getWarnings().translated),
                              ready ? std::optional<bool>{node->getNetworkActive()} : std::nullopt,
                              !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY)};
    }, [this, generation](StatusSnapshot snapshot) {
        m_status_pending = false;
        if (m_shutdown_requested) return;
        bool current;
        std::optional<int> total;
        {
            std::lock_guard lock{m_notifications->mutex};
            current = generation == m_notifications->status_generation;
            total = m_notifications->last_peer_total;
        }
        if (current) {
            m_status_ready = true;
            setNumInboundPeers(snapshot.inbound);
            setNumOutboundPeers(snapshot.outbound);
            setNumPeers(total.value_or(snapshot.inbound + snapshot.outbound));
            setWarnings(snapshot.warnings);
            if (snapshot.network_active) {
                m_network_known = true;
                if (m_pause != !*snapshot.network_active) {
                    m_pause = !*snapshot.network_active;
                    Q_EMIT pauseChanged(m_pause);
                }
            }
            if (m_mempool_information_available != snapshot.mempool_available) {
                m_mempool_information_available = snapshot.mempool_available;
                Q_EMIT mempoolInfoChanged();
            }
            Q_EMIT nodeInformationChanged();
        }
        if (std::exchange(m_status_again, false) || !current) refreshStatus();
    }, [this](std::exception_ptr) {
        m_status_pending = false;
        if (std::exchange(m_status_again, false)) refreshStatus();
    });
}

void NodeModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
        updateSyncCompletion();
    }
}

void NodeModel::setNumOutboundPeers(int new_num)
{
    if (new_num != m_num_outbound_peers) {
        m_num_outbound_peers = new_num;
        Q_EMIT numOutboundPeersChanged();
    }
}

void NodeModel::setNumPeers(int new_num)
{
    if (new_num != m_num_peers) {
        m_num_peers = new_num;
        Q_EMIT numPeersChanged();
    }
}

void NodeModel::setNumInboundPeers(int new_num)
{
    if (new_num != m_num_inbound_peers) {
        m_num_inbound_peers = new_num;
        Q_EMIT numInboundPeersChanged();
    }
}

void NodeModel::refreshMempoolInfo()
{
    if (!m_backend_queries_ready || m_workers_stopping) return;
    requestMempoolInfoRefresh();
}

void NodeModel::setMempoolInfoPollingActive(bool active)
{
    if (m_workers_stopping) return;
    if (m_mempool_info_polling_active == active) {
        return;
    }
    m_mempool_info_polling_active = active;
    Q_EMIT mempoolInfoPollingActiveChanged(active);

    QTimer* timer = m_mempool_info_timer;
    if (!timer || !m_backend_queries_ready) {
        return;
    }

    QMetaObject::invokeMethod(timer, [timer, active] {
        if (active) {
            timer->start();
        } else {
            timer->stop();
        }
    }, Qt::QueuedConnection);

    if (active) {
        refreshMempoolInfo();
    }
}

void NodeModel::initializeMempoolInfoPolling()
{
    m_mempool_info_worker = new QObject;
    m_mempool_info_thread = new QThread(this);
    m_mempool_info_thread->setObjectName(QStringLiteral("mempool-info"));
    m_mempool_info_timer = new QTimer;
    m_mempool_info_timer->setInterval(MEMPOOL_INFO_POLLING_INTERVAL_MS);

    m_mempool_info_worker->moveToThread(m_mempool_info_thread);
    m_mempool_info_timer->moveToThread(m_mempool_info_thread);

    connect(m_mempool_info_timer, &QTimer::timeout, m_mempool_info_worker, [this] {
        fetchMempoolInfo();
    });
    connect(m_mempool_info_thread, &QThread::finished, m_mempool_info_timer, &QObject::deleteLater);
    connect(m_mempool_info_thread, &QThread::finished, m_mempool_info_worker, &QObject::deleteLater);

    m_mempool_info_thread->start();
    QTimer::singleShot(0, m_mempool_info_worker, [] {
        util::ThreadRename("qml-mempool");
    });
}

void NodeModel::requestMempoolInfoRefresh()
{
    if (!m_mempool_info_worker) {
        return;
    }

    QTimer::singleShot(0, m_mempool_info_worker, [this] {
        fetchMempoolInfo();
    });
}

void NodeModel::fetchMempoolInfo()
{
    const MempoolInfo info{
        static_cast<int>(m_node.getMempoolSize()),
        m_node.getMempoolDynamicUsage() / 1'000'000.0,
        m_node.getMempoolMaxUsage() / 1'000'000.0,
    };

    QMetaObject::invokeMethod(this, [this, info] {
        applyMempoolInfo(info);
    }, Qt::QueuedConnection);
}

void NodeModel::applyMempoolInfo(const MempoolInfo& info)
{
    if (m_workers_stopping || m_shutdown_requested) return;
    if (info.transaction_count != m_mempool_transaction_count ||
        info.usage_mb != m_mempool_usage_mb ||
        info.max_usage_mb != m_mempool_max_usage_mb) {
        m_mempool_transaction_count = info.transaction_count;
        m_mempool_usage_mb = info.usage_mb;
        m_mempool_max_usage_mb = info.max_usage_mb;
        Q_EMIT mempoolInfoChanged();
    }
}

void NodeModel::setRemainingSyncTime(double new_progress)
{
    if (new_progress >= 1.0) {
        m_sync_progress_tracker.reset();
        if (m_remaining_sync_time != 0) {
            m_remaining_sync_time = 0;
            Q_EMIT remainingSyncTimeChanged();
        }
        return;
    }

    const std::optional<int64_t> estimate{
        m_sync_progress_tracker.addSample(m_sync_progress_clock.elapsed(), new_progress)};
    if (!estimate || *estimate == m_remaining_sync_time) return;

    m_remaining_sync_time = *estimate;
    Q_EMIT remainingSyncTimeChanged();
}
void NodeModel::setVerificationProgress(double new_progress)
{
    if (new_progress != m_verification_progress) {
        setRemainingSyncTime(new_progress);

        m_verification_progress = new_progress;
        Q_EMIT verificationProgressChanged();
    }
}

void NodeModel::setNodeReady(bool ready)
{
    if (m_node_ready == ready) return;

    m_node_ready = ready;
    updateSyncCompletion();
}

void NodeModel::setBlockSyncActive(bool active)
{
    if (m_block_sync_active == active) {
        return;
    }

    m_block_sync_active = active;
    Q_EMIT blockSyncActiveChanged();
    updateSyncCompletion();
}

void NodeModel::setHeaderSyncState(int height, int64_t block_time, bool presync)
{
    m_header_tip_height = height;
    m_header_tip_time = block_time;

    const int64_t estimate_headers_left{(GetTime() - block_time) / Params().GetConsensus().nPowTargetSpacing};
    const bool active{height > 0 && estimate_headers_left > HEADER_HEIGHT_DELTA_SYNC};
    const double progress{active ? 1.0 * height / (height + estimate_headers_left) : 0.0};

    if (m_header_sync_active != active ||
        m_header_presync != presync ||
        m_header_sync_progress != progress) {
        m_header_sync_active = active;
        m_header_presync = presync;
        m_header_sync_progress = progress;
        Q_EMIT headerSyncChanged();
    }
    updateSyncCompletion();
}

void NodeModel::updateSyncCompletion()
{
    if (m_applying_sync_snapshot) return;
    if (m_initial_sync_complete) {
        // Ignore ordinary header-first block announcements, but resume the
        // sync presentation after falling substantially behind (e.g. sleep).
        if (int64_t{m_header_tip_height} - m_block_tip_height > HEADER_HEIGHT_DELTA_SYNC) {
            m_initial_sync_complete = false;
            Q_EMIT initialSyncCompleteChanged();
        }
        return;
    }

    if (!m_node_ready ||
        m_block_sync_active ||
        m_header_sync_active ||
        m_header_presync ||
        m_block_tip_height < m_header_tip_height) {
        return;
    }

    // After startup or renewed catch-up, stay in the sync presentation until
    // the active chain reaches the best known header, even inside the gap
    // threshold above. This avoids flickering between the two presentations.
    m_initial_sync_complete = true;
    Q_EMIT initialSyncCompleteChanged();
}

void NodeModel::setPause(bool new_pause)
{
    if (!m_backend_queries_ready || m_shutdown_requested || m_workers_stopping) return;
    m_network_desired_pause = new_pause;
    if (m_network_action_pending || m_pause == new_pause) return;
    m_network_action_pending = true;
    m_network_action_error.clear();
    Q_EMIT networkActionErrorChanged();
    Q_EMIT networkActionPendingChanged();
    submitNetworkAction();
}

void NodeModel::submitNetworkAction()
{
    struct Result { bool paused; uint64_t generation; };
    const bool requested{m_network_desired_pause};
    m_commands->submit(this, [node = &m_node, state = m_notifications, requested] {
        node->setNetworkActive(!requested);
        uint64_t generation;
        {
            std::lock_guard lock{state->mutex};
            generation = state->network_generation;
        }
        return Result{!node->getNetworkActive(), generation};
    }, [this, requested](Result result) {
        if (m_shutdown_requested) return;
        bool current;
        {
            std::lock_guard lock{m_notifications->mutex};
            current = result.generation == m_notifications->network_generation;
        }
        if (current) {
            m_network_known = true;
            if (m_pause != result.paused) {
                m_pause = result.paused;
                Q_EMIT pauseChanged(m_pause);
            }
        } else {
            // An RPC command can supersede this result while it is queued.
            // Consume its value notification and request a fresh snapshot.
            publishNotifications();
            refreshStatus();
        }
        if (m_network_desired_pause != requested) {
            submitNetworkAction();
            return;
        }
        m_network_action_pending = false;
        if (current && result.paused != requested) m_network_action_error = tr("Unable to change network activity.");
        Q_EMIT networkActionPendingChanged();
        Q_EMIT networkActionErrorChanged();
        Q_EMIT nodeInformationChanged();
    }, [this](std::exception_ptr) {
        m_network_action_pending = false;
        m_network_action_error = tr("Unable to change network activity.");
        Q_EMIT networkActionPendingChanged();
        Q_EMIT networkActionErrorChanged();
    });
}

void NodeModel::setErrorState(bool faulted)
{
    if (m_faulted != faulted) {
        m_faulted = faulted;
        Q_EMIT errorStateChanged(faulted);
    }
}

void NodeModel::setStartupError(const QString& error)
{
    if (m_startup_error != error) {
        m_startup_error = error;
        Q_EMIT startupErrorChanged();
    }
}

void NodeModel::addStartupWarnings(const QStringList& warnings)
{
    for (const QString& warning : warnings) {
        recordStartupWarningMessage(warning);
    }
}

void NodeModel::setWarnings(const QString& warnings)
{
    const QStringList warning_list{SplitWarnings(warnings)};
    if (m_warnings == warnings && m_warning_list == warning_list) {
        return;
    }
    m_warnings = warnings;
    m_warning_list = warning_list;
    Q_EMIT warningsChanged();
}

void NodeModel::showStartupWarnings()
{
    if (m_startup_warning_messages.isEmpty()) {
        return;
    }

    const QString warnings{m_startup_warning_messages.join(QStringLiteral("\n\n"))};
    m_startup_warning_messages.clear();
    // MSG_WARNING is modal; startup notices should be shown once without blocking initialization.
    showRuntimeDialogOnGuiThread(warnings, CClientUIInterface::ICON_WARNING, /*question=*/false);
}

void NodeModel::recordStartupErrorMessage(const QString& message)
{
    const QString error{message.trimmed()};
    if (error.isEmpty() || m_startup_error_messages.contains(error)) {
        return;
    }
    m_startup_error_messages.push_back(error);
}

void NodeModel::recordStartupWarningMessage(const QString& message)
{
    const QString warning{message.trimmed()};
    if (warning.isEmpty() || m_startup_warning_messages.contains(warning)) {
        return;
    }
    m_startup_warning_messages.push_back(warning);
}

void NodeModel::startNodeInitializionThread()
{
    if (m_initialization_requested) {
        return;
    }
    m_initialization_requested = true;
    Q_EMIT requestedInitialize();
}

void NodeModel::requestShutdown()
{
    if (m_fatal_exception) {
        std::_Exit(EXIT_FAILURE);
    }
    if (m_shutdown_requested) {
        return;
    }
    m_shutdown_requested = true;
    Q_EMIT networkActionReadyChanged();
    while (m_runtime_dialog_active) answerRuntimeDialog(CClientUIInterface::BTN_CANCEL);
    stopShutdownPolling();
    Q_EMIT requestedShutdown();
}

void NodeModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info, bool initial_block_download, bool shutdown_requested)
{
    if (m_workers_stopping || m_fatal_exception) return;
    if (success && (m_shutdown_requested || shutdown_requested)) {
        requestShutdown();
        return;
    }

    if (!success) {
        if (m_startup_failure_dialog_shown) {
            requestShutdown();
            Q_EMIT nodeInitialized();
            return;
        }
        setErrorState(true);
        refreshStatus();
        QString startup_error{m_startup_error_messages.isEmpty() ? tr("Node initialization failed.") : m_startup_error_messages.join(QStringLiteral("\n\n"))};
        if (!m_startup_warning_messages.isEmpty()) {
            startup_error = tr("Startup warnings:") + QStringLiteral("\n") + m_startup_warning_messages.join(QStringLiteral("\n\n")) + QStringLiteral("\n\n") + startup_error;
        }
        m_startup_warning_messages.clear();
        setStartupError(startup_error);
        if (shutdown_requested) {
            requestShutdown();
        }
    } else {
        m_backend_queries_ready = true;
        Q_EMIT networkActionReadyChanged();
        if (m_mempool_info_polling_active) {
            QMetaObject::invokeMethod(m_mempool_info_timer, [timer = m_mempool_info_timer] { timer->start(); }, Qt::QueuedConnection);
        }
        m_startup_error_messages.clear();
        m_runtime_dialogs_enabled = true;
        showStartupWarnings();
        {
            const QScopedValueRollback applying{m_applying_sync_snapshot, true};
            publishNotifications();
            if (!m_seen_block_tip) {
                m_block_tip_time = tip_info.block_time;
                setBlockTipHeight(tip_info.block_height);
                setVerificationProgress(tip_info.verification_progress);
                setBlockSyncActive(initial_block_download);
            }
            // LoadChainTip emits an INIT_DOWNLOAD notification even on a caught-up
            // restart. Core's IBD flag only latches false: preserve a newer POST_INIT
            // notification, but let this final startup result clear the init flag.
            if (!initial_block_download) setBlockSyncActive(false);
            if (!m_seen_header_tip) setHeaderSyncState(tip_info.header_height, tip_info.header_time, /*presync=*/false);
            setNodeReady(true);
        }
        updateSyncCompletion();
        refreshStatus();
        refreshNodeInformation();
        refreshMempoolInfo();
        Q_EMIT chainStateReady();
    }
    Q_EMIT nodeInitialized();
}

void NodeModel::handleRunawayException(const QString& message)
{
    stopShutdownPolling();
    setMempoolInfoPollingActive(false);
    m_backend_queries_ready = false;
    m_shutdown_requested = true;
    if (!m_fatal_exception) {
        m_fatal_exception = true;
        Q_EMIT fatalExceptionChanged();
    }
    setErrorState(true);
    setStartupError(tr("A fatal error occurred. %1 can no longer continue safely and will quit.").arg(CLIENT_NAME)
                    + (message.isEmpty() ? QString{} : QStringLiteral("\n\n") + message));
}

void NodeModel::startShutdownPolling()
{
    if (m_fatal_exception || m_shutdown_polling_timer_id != 0) {
        return;
    }
    m_shutdown_polling_timer_id = startTimer(200ms);
}

void NodeModel::stopShutdownPolling()
{
    if (m_shutdown_polling_timer_id == 0) {
        return;
    }
    killTimer(m_shutdown_polling_timer_id);
    m_shutdown_polling_timer_id = 0;
}

void NodeModel::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != m_shutdown_polling_timer_id) {
        return;
    }
    if (m_shutdown_poll_pending || m_workers_stopping) return;
    m_shutdown_poll_pending = true;
    QMetaObject::invokeMethod(m_mempool_info_worker, [this] {
        const bool requested{m_node.shutdownRequested()};
        QMetaObject::invokeMethod(this, [this, requested] {
            m_shutdown_poll_pending = false;
            // A poll already in flight cannot acknowledge a later fatal error.
            if (requested && !m_fatal_exception) requestShutdown();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void NodeModel::ConnectToBlockTipSignal()
{
    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(
        [state = m_notifications](SynchronizationState sync, interfaces::BlockTip tip, double progress) {
            std::lock_guard lock{state->mutex};
            state->block = Notifications::Block{sync, std::move(tip), progress};
            state->schedule();
        });
}

void NodeModel::ConnectToHeaderTipSignal()
{
    m_handler_notify_header_tip = m_node.handleNotifyHeaderTip(
        [state = m_notifications](SynchronizationState, interfaces::BlockTip tip, bool presync) {
            std::lock_guard lock{state->mutex};
            state->header = Notifications::Header{std::move(tip), presync};
            state->schedule();
        });
}

void NodeModel::ConnectToNumConnectionsChangedSignal()
{
    m_handler_notify_num_peers_changed = m_node.handleNotifyNumConnectionsChanged(
        [state = m_notifications](int total) {
            std::lock_guard lock{state->mutex};
            state->peers = total;
            state->last_peer_total = total;
            ++state->status_generation;
            state->schedule();
        });
}

void NodeModel::ConnectToNetworkActiveChangedSignal()
{
    m_handler_notify_network_active_changed = m_node.handleNotifyNetworkActiveChanged(
        [state = m_notifications](bool active) {
            std::lock_guard lock{state->mutex};
            state->network_active = active;
            ++state->network_generation;
            ++state->status_generation;
            state->schedule();
        });
}

void NodeModel::ConnectToAlertChangedSignal()
{
    m_handler_notify_alert_changed = m_node.handleNotifyAlertChanged([state = m_notifications] {
        std::lock_guard lock{state->mutex};
        state->warnings = true;
        ++state->status_generation;
        state->schedule();
    });
}

void NodeModel::ConnectToRuntimeDialogSignals()
{
    assert(!m_handler_message_box);
    assert(!m_handler_question);

    m_handler_message_box = m_node.handleMessageBox(
        [this](const bilingual_str& message, unsigned int style) {
            showRuntimeMessageBox(
                QString::fromStdString(message.translated),
                style);
        });
    m_handler_question = m_node.handleQuestion(
        [this](const bilingual_str& message, [[maybe_unused]] const std::string& non_interactive_message, unsigned int style) {
            return showRuntimeQuestion(
                QString::fromStdString(message.translated),
                style);
        });
}

bool NodeModel::validateProxyAddress(QString address_port)
{
    uint16_t port{0};
    std::string addr_port{address_port.toStdString()};
    std::string hostname;
    // First, attempt to split the input address into hostname and port components.
    // We call SplitHostPort to validate that a port is provided in addr_port.
    // If either splitting fails or port is zero (not specified), return false.
    if (!SplitHostPort(addr_port, port, hostname) || !port) return false;

    // Create a service endpoint (CService) from the address and port.
    // If port is missing in addr_port, DEFAULT_PROXY_PORT is used as the fallback.
    CService serv(LookupNumeric(addr_port, DEFAULT_PROXY_PORT));

    // Construct the Proxy with the service endpoint and return if it's valid
    Proxy addrProxy = Proxy(serv, true);
    return addrProxy.IsValid();
}

QString NodeModel::defaultProxyAddress()
{
    return QString::fromStdString(std::string(DEFAULT_PROXY_HOST) + ":" + util::ToString(DEFAULT_PROXY_PORT));
}

bool NodeModel::disconnectPeer(int nodeId)
{
    if (!m_backend_queries_ready || m_shutdown_requested || m_workers_stopping || m_peer_action_pending) return false;
    m_peer_action_pending = true;
    m_peer_action_error.clear();
    Q_EMIT peerActionPendingChanged();
    Q_EMIT peerActionErrorChanged();
    return m_commands->submit(this, [node = &m_node, nodeId] { return node->disconnectById(nodeId); },
        [this](bool success) { finishPeerAction(QStringLiteral("disconnect"), success); },
        [this](std::exception_ptr) { finishPeerAction(QStringLiteral("disconnect"), false); });
}

bool NodeModel::banPeer(const QString& rawAddress, int64_t banDuration)
{
    if (!m_backend_queries_ready || m_shutdown_requested || m_workers_stopping || m_peer_action_pending || banDuration <= 0) return false;
    const auto address{LookupHost(rawAddress.toStdString(), /*fAllowLookup=*/false)};
    if (!address) return false;
    m_peer_action_pending = true;
    m_peer_action_error.clear();
    Q_EMIT peerActionPendingChanged();
    Q_EMIT peerActionErrorChanged();
    return m_commands->submit(this, [node = &m_node, address = *address, banDuration] {
        if (!node->ban(address, banDuration)) return false;
        // A successful ban remains successful if the peer left in the meantime.
        node->disconnectByAddress(address);
        return true;
    }, [this](bool success) { finishPeerAction(QStringLiteral("ban"), success); },
       [this](std::exception_ptr) { finishPeerAction(QStringLiteral("ban"), false); });
}

void NodeModel::finishPeerAction(const QString& action, bool success, const QString& error)
{
    if (m_shutdown_requested) return;
    if (action == QStringLiteral("ban")) acknowledgeBanNotification();
    m_peer_action_pending = false;
    m_peer_action_error = success ? QString{} : (error.isEmpty() ?
        (action == QStringLiteral("ban") ? tr("Unable to ban peer.") : tr("Unable to disconnect peer.")) : error);
    Q_EMIT peerActionPendingChanged();
    Q_EMIT peerActionErrorChanged();
    Q_EMIT peerActionFinished(action, success, m_peer_action_error);
}

void NodeModel::acknowledgeBanNotification()
{
    std::lock_guard lock{m_notifications->mutex};
    m_notifications->bans = false;
}

void NodeModel::refreshNodeInformation()
{
    if (!m_node_ready || m_shutdown_requested || m_workers_stopping) return;
    if (m_information_pending) {
        m_information_again = true;
        return;
    }
    m_information_pending = true;
    m_snapshots.submit(this, [node = &m_node] {
        InformationSnapshot snapshot;
        snapshot.user_agent = QString::fromStdString(strSubVersion);
        snapshot.data_dir = QString::fromStdString(fs::PathToString(gArgs.GetDataDirNet()));
        snapshot.blocks_dir = QString::fromStdString(fs::PathToString(gArgs.GetBlocksDirPath()));
        snapshot.startup_time = QDateTime::currentDateTime().addSecs(-TicksSeconds(GetUptime())).toString();
        snapshot.network = QString::fromStdString(Params().GetChainTypeString());
        QStringList addresses;
        for (const auto& [address, info] : node->getNetLocalAddresses()) {
            QString text{QString::fromStdString(address.ToStringAddr())};
            if (!address.IsI2P()) text += QStringLiteral(":") + QString::number(info.nPort);
            addresses.push_back(text);
        }
        snapshot.local_addresses = addresses.join(QStringLiteral(", "));
        return snapshot;
    }, [this](InformationSnapshot snapshot) {
        m_information_pending = false;
        if (m_shutdown_requested) return;
        m_information = std::move(snapshot);
        m_information_ready = true;
        Q_EMIT nodeInformationChanged();
        if (std::exchange(m_information_again, false)) refreshNodeInformation();
    }, [this](std::exception_ptr) {
        m_information_pending = false;
        if (std::exchange(m_information_again, false)) refreshNodeInformation();
    });
}

QVariantList NodeModel::nodeInformationRows() const
{
    const QString unknown{tr("Unknown")};
    const QString local_addresses{!m_information_ready ? unknown :
        m_information.local_addresses.isEmpty() ? tr("None") : m_information.local_addresses};
    const QString warning_text{!m_status_ready ? unknown :
        m_warning_list.empty() ? tr("None") : m_warning_list.join(QStringLiteral("\n"))};
    QVariantList rows;
    rows.push_back(InformationRow(tr("Client version"), fullClientVersion()));
    rows.push_back(InformationRow(tr("User agent"), m_information_ready ? m_information.user_agent : unknown));
    rows.push_back(InformationRow(tr("Datadir"), m_information_ready ? m_information.data_dir : unknown));
    rows.push_back(InformationRow(tr("Blocks dir"), m_information_ready ? m_information.blocks_dir : unknown));
    rows.push_back(InformationRow(tr("Startup time"), m_information_ready ? m_information.startup_time : unknown));
    rows.push_back(InformationRow(tr("Network"), m_information_ready ? m_information.network : unknown));
    rows.push_back(InformationRow(tr("Block height"), m_node_ready || m_seen_block_tip ? QString::number(m_block_tip_height) : unknown));
    rows.push_back(InformationRow(tr("Header height"), m_node_ready || m_seen_header_tip ? QString::number(m_header_tip_height) : unknown));
    rows.push_back(InformationRow(tr("Header time"), m_header_tip_time > 0 ? QDateTime::fromSecsSinceEpoch(m_header_tip_time).toString() : unknown));
    rows.push_back(InformationRow(tr("Last block time"), m_block_tip_time > 0 ? QDateTime::fromSecsSinceEpoch(m_block_tip_time).toString() : unknown));
    rows.push_back(InformationRow(tr("Verification progress"), QStringLiteral("%1%").arg(QString::number(m_verification_progress * 100.0, 'f', 2))));
    rows.push_back(InformationRow(tr("Peers"), m_status_ready ? tr("%1 total (%2 inbound, %3 outbound)").arg(m_num_peers).arg(m_num_inbound_peers).arg(m_num_outbound_peers) : unknown));
    rows.push_back(InformationRow(tr("Network active"), m_network_known ? (m_pause ? tr("No") : tr("Yes")) : unknown));
    rows.push_back(InformationRow(tr("Local addresses"), local_addresses));
    rows.push_back(InformationRow(tr("Warnings"), warning_text));
    return rows;
}

void NodeModel::showRuntimeMessageBox(const QString& message, unsigned int style)
{
    if (QThread::currentThread() == thread()) {
        showRuntimeDialogOnGuiThread(message, style, /*question=*/false);
        return;
    }

    if (!(style & CClientUIInterface::MODAL)) {
        QMetaObject::invokeMethod(this, [this, message, style] {
            showRuntimeDialogOnGuiThread(message, style, /*question=*/false);
        }, Qt::QueuedConnection);
        return;
    }

    QMetaObject::invokeMethod(this, [this, message, style] {
        showRuntimeDialogOnGuiThread(message, style, /*question=*/false);
    }, Qt::BlockingQueuedConnection);
}

bool NodeModel::showRuntimeQuestion(const QString& message, unsigned int style)
{
    if (QThread::currentThread() == thread()) {
        return showRuntimeDialogOnGuiThread(message, style, /*question=*/true);
    }

    bool result{false};
    QMetaObject::invokeMethod(this, [this, &result, message, style] {
        result = showRuntimeDialogOnGuiThread(message, style, /*question=*/true);
    }, Qt::BlockingQueuedConnection);
    return result;
}

bool NodeModel::showRuntimeDialogOnGuiThread(const QString& message, unsigned int style, bool question)
{
    if (m_shutdown_requested || m_workers_stopping) return false;
    if (!m_runtime_dialogs_enabled && !question) {
        if (style & CClientUIInterface::ICON_WARNING) {
            recordStartupWarningMessage(message);
            return false;
        }
        if (!(style & CClientUIInterface::MODAL)) {
            if (style & CClientUIInterface::ICON_ERROR) {
                recordStartupErrorMessage(message);
            }
            return false;
        }
    }

    const bool blocking{(style & CClientUIInterface::MODAL) || question};
    auto request{std::make_shared<RuntimeDialogRequest>()};
    request->message = message;
    request->style = style;
    request->question = question;
    if (!m_runtime_dialogs_enabled && (question || (style & CClientUIInterface::ICON_ERROR))) {
        m_startup_failure_dialog_shown = true;
    }

    QEventLoop loop;
    if (blocking) {
        request->loop = &loop;
    }

    if (m_runtime_dialog_active) {
        m_runtime_dialog_queue.push_back(request);
    } else {
        showRuntimeDialogRequest(request);
    }

    if (!blocking) {
        return false;
    }

    if (!request->answered) {
        loop.exec();
    }
    request->loop = nullptr;

    return request->answer;
}

void NodeModel::showRuntimeDialogRequest(const std::shared_ptr<RuntimeDialogRequest>& request)
{
    m_runtime_dialog_active = request;
    m_runtime_dialog_title = RuntimeDialogTitle(request->style);
    m_runtime_dialog_message = request->message;
    m_runtime_dialog_icon = RuntimeDialogIcon(request->style);
    m_runtime_dialog_buttons = RuntimeDialogButtons(request->style);
    m_runtime_dialog_question = request->question || HasMultipleRuntimeDialogButtons(m_runtime_dialog_buttons);
    m_runtime_dialog_visible = true;
    Q_EMIT runtimeDialogChanged();
}

void NodeModel::answerRuntimeDialog(unsigned int button)
{
    if (!m_runtime_dialog_active) {
        return;
    }

    auto answered_dialog{std::move(m_runtime_dialog_active)};
    answered_dialog->button = button;
    answered_dialog->answer = button == CClientUIInterface::BTN_OK;
    answered_dialog->answered = true;
    if (answered_dialog->loop) {
        answered_dialog->loop->quit();
    }

    if (!m_runtime_dialog_queue.empty()) {
        auto next_dialog{m_runtime_dialog_queue.front()};
        m_runtime_dialog_queue.pop_front();
        showRuntimeDialogRequest(next_dialog);
        return;
    }

    m_runtime_dialog_visible = false;
    Q_EMIT runtimeDialogChanged();
}

#ifdef ENABLE_TEST_AUTOMATION
void NodeModel::showRuntimeDialogForTest(const QString& message, unsigned int style, bool question)
{
    auto request{std::make_shared<RuntimeDialogRequest>()};
    request->message = message;
    request->style = style;
    request->question = question;

    if (m_runtime_dialog_active) {
        m_runtime_dialog_queue.push_back(request);
    } else {
        showRuntimeDialogRequest(request);
    }
}
#endif

void NodeModel::ConnectToBannedListChangedSignal()
{
    assert(!m_handler_notify_banned_list_changed);
    m_handler_notify_banned_list_changed = m_node.handleBannedListChanged([state = m_notifications] {
        std::lock_guard lock{state->mutex};
        state->bans = true;
        state->schedule();
    });
}

void NodeModel::unsubscribeFromCoreSignals()
{
    if (m_handler_notify_block_tip) {
        m_handler_notify_block_tip->disconnect();
    }
    if (m_handler_notify_header_tip) {
        m_handler_notify_header_tip->disconnect();
    }
    if (m_handler_notify_num_peers_changed) {
        m_handler_notify_num_peers_changed->disconnect();
    }
    if (m_handler_notify_network_active_changed) {
        m_handler_notify_network_active_changed->disconnect();
    }
    if (m_handler_notify_alert_changed) {
        m_handler_notify_alert_changed->disconnect();
    }
    if (m_handler_message_box) {
        m_handler_message_box->disconnect();
    }
    if (m_handler_question) {
        m_handler_question->disconnect();
    }
    if (m_handler_notify_banned_list_changed) {
        m_handler_notify_banned_list_changed->disconnect();
    }
}
