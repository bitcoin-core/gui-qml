// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qml/models/nodemodel.h>
#include <qml/asyncjoin.h>
#include <qml/executor.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>

#include <common/args.h>
#include <common/system.h>
#include <chainparams.h>
#include <interfaces/node.h>
#include <net.h>
#include <net_processing.h>
#include <netbase.h>
#include <node/interface_ui.h>
#include <util/fs.h>
#include <util/time.h>
#include <validation.h>

#include <cassert>
#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <future>
#include <vector>
#include <mutex>
#include <optional>
#include <utility>
#include <atomic>

#include <QDateTime>
#include <QLocale>
#include <QCoreApplication>
#include <QMetaObject>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
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

QVariant InformationRow(const QString& label, const QString& value, bool mono = false)
{
    QVariantMap row;
    row.insert(QStringLiteral("label"), label);
    row.insert(QStringLiteral("value"), value);
    row.insert(QStringLiteral("mono"), mono);
    return QVariant::fromValue(row);
}
} // namespace

// Core callbacks share a bounded mailbox and coalesce changes into one pending
// GUI delivery. The mutex also protects the receiver until shutdown detaches it.
struct NodeModel::Notifications : std::enable_shared_from_this<Notifications> {
    struct Block { SynchronizationState state; interfaces::BlockTip tip; double progress; uint64_t generation; };
    struct Header { interfaces::BlockTip tip; bool presync; uint64_t generation; };
    std::mutex mutex;
    NodeModel* receiver{nullptr};
    std::shared_ptr<std::atomic_bool> diagnostic_loss;
    std::shared_ptr<startupdiagnostics::TextBudget> text_budget;
    PresentationServices::RetireValue retire_diagnostic;
    std::function<void(std::function<void()>)> post;
    bool delivery_pending{false};
    bool ready{false}, runtime_enabled{false};
    std::optional<bool> subscribed;
    QString subscription_error;
    int64_t header_spacing{600};
    std::deque<std::shared_ptr<RuntimeDialogRequest>> dialogs;
    std::shared_ptr<runtimeprompt::Service> prompts;
    bool runtime_overflow{false};
    bool request(QString message, unsigned int style, bool question) {
        const bool gui=QThread::currentThread()==QCoreApplication::instance()->thread();
        if(gui&&question)return false;
        bool runtime;
        {std::lock_guard lock{mutex};if(!receiver)return false;runtime=runtime_enabled;}
        auto details=startupdiagnostics::RetainText(text_budget,message,retire_diagnostic);
        QString summary=message.left(16'384);
        if(message.size()>16'384)summary+=QStringLiteral(" ")+QObject::tr("[additional message text omitted]");
        if(!details){if(diagnostic_loss)*diagnostic_loss=true;summary+=QStringLiteral(" ")+QObject::tr("[full message not available: too many messages]");}
        // Capture the producer phase. Startup diagnostics do not become runtime
        // dialogs just because their GUI delivery is delayed past readiness.
        if(!runtime&&!question&&((style&CClientUIInterface::ICON_WARNING)||!(style&CClientUIInterface::MODAL))){
            auto diagnostic=std::make_shared<RuntimeDialogRequest>();
            diagnostic->message=std::move(summary);diagnostic->full_message=std::move(details);diagnostic->style=style;
            std::lock_guard lock{mutex};if(!receiver)return false;
            if(dialogs.size()>=32){runtime_overflow=true;schedule();return false;}
            dialogs.push_back(std::move(diagnostic));schedule();return false;
        }
        const bool complete=bool(details);
        summary=summary.left(static_cast<qsizetype>(runtimeprompt::MAX_SUMMARY_CHARACTERS));
        const auto request=std::make_shared<const runtimeprompt::Request>(runtime?runtimeprompt::Origin::Runtime:runtimeprompt::Origin::Startup,
            style,question,std::move(summary),std::move(details),complete);
        const bool blocking=!gui&&(question||(style&CClientUIInterface::MODAL));
        std::shared_ptr<runtimeprompt::Continuation> continuation;
        if(!prompts||!prompts->enqueue(request,blocking,continuation)){
            std::lock_guard lock{mutex};if(receiver){runtime_overflow=true;schedule();}return false;
        }
        return continuation?continuation->wait().affirmative():false;
    }
    std::optional<Block> block;
    std::optional<Header> header;
    std::optional<int> peers;
    std::optional<int> last_peer_total;
    std::optional<bool> network_active;
    bool warnings{false};
    bool bans{false};
    uint64_t status_generation{0};
    uint64_t network_generation{0};
    uint64_t tip_generation{0};
    bool header_presync{false};

    // Called with mutex held, so detaching the receiver cannot race dispatch.
    void schedule()
    {
        if (!receiver || delivery_pending) return;
        delivery_pending = true;
        post([target = receiver, state = shared_from_this()] {
            {
                std::lock_guard lock{state->mutex};
                if (state->receiver != target) return;
                state->delivery_pending = false;
            }
            target->publishNotifications();
        });
    }
};

struct NodeModel::StatusSnapshot {
    int inbound{0};
    int outbound{0};
    QString warnings;
    std::optional<bool> network_active;
    bool mempool_available{false};
    uint64_t generation{0}, network_generation{0};
    uint64_t tip_generation{0};
    bool chain_known{false};
    QString startup_time, user_agent;
    QStringList warning_list;
    QString local_addresses, data_dir, blocks_dir, network;
    int header_height{0}, block_height{0};
    int64_t header_time{0}, block_time{0};
};

struct NodeModel::Subscriptions {
    std::vector<std::unique_ptr<interfaces::Handler>> handlers;

    void disconnect()
    {
        for (const auto& handler : handlers) {
            try { if (handler) handler->disconnect(); } catch (...) {}
        }
        handlers.clear();
    }
};
struct NodeModel::ShutdownSnapshot { bool requested{false}; };

struct NodeModel::ObservationQueues {
    explicit ObservationQueues(execution::Executor& executor)
        : core(executor.makeQueue(execution::Lane::Core)),
          priority(executor.makeQueue(execution::Lane::Priority)),
          outcomes(executor.makeQueue(execution::Lane::Utility)),
          retirement(executor.makeQueue(execution::Lane::Utility)) {}

    execution::SerialQueue core;
    execution::SerialQueue priority;
    execution::SerialQueue outcomes;
    execution::SerialQueue retirement;
};

struct NodeModel::LocalDrain {
    std::atomic_int pending{4}; // Queries, prompts, network commands, and GUI owners.
    std::promise<void> ready;

    void finish()
    {
        if (pending.fetch_sub(1) == 1) ready.set_value();
    }
};

NodeModel::NodeModel(interfaces::Node& node, bool backend_ready)
    : NodeModel(node, backend_ready, Services{}) {}

NodeModel::NodeModel(interfaces::Node& node, bool backend_ready, Services services)
    : m_notifications(std::make_shared<Notifications>()), m_services(std::move(services)),
      m_subscriptions(std::make_shared<Subscriptions>()),
      m_backend_queries_ready(backend_ready), m_node(node)
{
    m_notifications->receiver = this;
    if (!m_services.wall_seconds) m_services.wall_seconds = [] {
        return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    };
    if (!m_services.monotonic_milliseconds) {
        const auto start = std::chrono::steady_clock::now();
        m_services.monotonic_milliseconds = [start] { return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count(); };
    }
    const QPointer<NodeModel> observer{this};
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    const auto post = m_services.post;
    if (m_services.diagnostics) m_notifications->retire_diagnostic = m_services.diagnostics->valueRetirement();
    if (!m_services.observation_executor) {
        m_owned_executor = std::make_shared<execution::Executor>(execution::Executor::Config{});
        m_services.observation_executor = m_owned_executor.get();
    }
    auto& executor = *m_services.observation_executor;
    if (!m_notifications->retire_diagnostic || !m_services.network_retirement) {
        m_local_retirement = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        m_local_diagnostics = std::make_shared<PresentationServices>(PresentationServices::Ports{
            post, [queue = m_local_retirement](auto release) { return queue->post(std::move(release)); },
            {}, std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility))});
        if (!m_notifications->retire_diagnostic) {
            m_services.diagnostics = m_local_diagnostics.get();
            m_notifications->retire_diagnostic = m_local_diagnostics->valueRetirement();
        }
        if (!m_services.network_retirement) m_services.network_retirement = m_local_diagnostics->valueRetirement();
    }
    if (m_owned_executor || m_local_diagnostics) m_local_drain = std::make_shared<LocalDrain>();
    m_observations = std::make_shared<ObservationQueues>(executor);
    m_notifications->post = post;
    m_peer_actions = new PeerCommandModel(node, post, [this] {
        return networkBackendAvailable();
    }, this, postNative(execution::Lane::Core));
    connect(this, &NodeModel::networkBackendAvailabilityChanged, m_peer_actions, [this] {
        if (m_closed || m_workers_stopping || m_shutdown_requested) m_peer_actions->close();
        else m_peer_actions->readinessChanged();
    });
    m_notifications->diagnostic_loss = m_services.diagnostic_loss;
    m_notifications->text_budget = m_dialog_text_budget;
    m_startup_diagnostics = new StartupDiagnosticsModel(post, *m_services.diagnostics, this);
    connect(m_startup_diagnostics, &StartupDiagnosticsModel::changed, this, &NodeModel::startupDiagnosticsChanged);
    connect(m_startup_diagnostics, &StartupDiagnosticsModel::finished, this, &NodeModel::finishStartupDiagnostics);
    runtimeprompt::Service::Queues prompt_queues{
        std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility)),
        m_notifications->retire_diagnostic,
    };
    m_prompt_service = std::make_shared<runtimeprompt::Service>(std::move(prompt_queues),
        [observer, post] { post([observer] { if (observer) observer->publishRuntimePrompt(); }); },
        [observer, post, drain = m_local_drain] {
            if (drain) drain->finish();
            post([observer] {
                if (!observer) return;
                observer->m_prompt_drained = true;
                observer->finishDrain();
            });
        });
    m_notifications->prompts=m_prompt_service;
    NetworkModel::Ports network_ports;
    network_ports.post = post;
    if (m_local_drain) network_ports.native_drained = [drain = m_local_drain] { drain->finish(); };
    network_ports.queues = {
        std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Core)),
        std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility)),
        m_services.network_retirement,
    };
    network_ports.observe=[node=&m_node,state=m_notifications]{
        // Capture a conservative ingress frontier before the public read.
        // Never hold a GUI-shared mailbox mutex across Core work: a delayed
        // read must not block a later notice or its GUI publication. A newer
        // notification then supersedes this original read in Domain.
        uint64_t generation;
        { std::lock_guard lock{state->mutex}; generation=state->network_generation; }
        return networkcommand::Observation{node->getNetworkActive(),generation};
    };
    network_ports.set_active=[node=&m_node](bool active){node->setNetworkActive(active);};
    network_ports.generation=[state=m_notifications]{std::lock_guard lock{state->mutex};return state->network_generation;};
    m_network_model=new NetworkModel(std::move(network_ports),this);
    connect(m_network_model,&NetworkModel::changed,this,&NodeModel::publishNetworkAction);
    connect(m_network_model,&NetworkModel::shutdownFinished,this,&NodeModel::finishDrain);
    connect(this,&NodeModel::networkBackendAvailabilityChanged,this,&NodeModel::updateNetworkAvailability);
    initializeMempoolInfoPolling();
    initializeSubscriptions();
    refreshStatus();
    refreshMempoolInfo();
}

NodeModel::~NodeModel()
{
    m_destroying = true;
    beginShutdown();
}

void NodeModel::close()
{
    const QPointer<NodeModel> observer{this};
    if(std::exchange(m_closed,true))return;
    // A native question can hold the Core worker needed by an accepted
    // settings write. Release the waiting producer before pre-interrupt drain.
    if (m_prompt_service) m_prompt_service->close();
    if(m_status_query)m_status_query->close();
    if(m_mempool_query)m_mempool_query->close();
    Q_EMIT networkBackendAvailabilityChanged();if(!observer)return;
    setNodeReady(false);if(!observer)return;
    if (m_startup_diagnostics) m_startup_diagnostics->close();
    if(!observer)return;
    if (m_runtime_diagnostics && m_runtime_diagnostics != m_startup_diagnostics) m_runtime_diagnostics->close();
    if(!observer)return;
    m_observations->core.close();
}

void NodeModel::beginShutdown()
{
    const QPointer<NodeModel> observer{this};
    if (m_workers_stopping) {
        // An original admitted lifecycle operation may publish its fatal
        // diagnostic after worker shutdown. A repeated terminal close must
        // also retire that newly retained presentation while the facade lives.
        if (m_startup_diagnostics) m_startup_diagnostics->close();
        if (!observer) return;
        if (m_runtime_diagnostics && m_runtime_diagnostics != m_startup_diagnostics) m_runtime_diagnostics->close();
        return;
    }
    m_workers_stopping = true;
    if (m_local_drain) joinLocalExecution();
    const auto gui_closed = qScopeGuard([drain = m_local_drain] { if (drain) drain->finish(); });
    // Detach the notification receiver and arrange cleanup before a signal can
    // destroy the facade. A queued registration may still own its handlers.
    detachNotifications();
    if (m_status_query) m_status_query->close();
    if (m_mempool_query) m_mempool_query->close();
    if (m_shutdown_query) m_shutdown_query->close();
    closeObservations();
    if(m_network_model){
        if(m_destroying)m_network_model->retire();
        else m_network_model->close();
    }
    if(!observer)return;
    if (m_startup_diagnostics) m_startup_diagnostics->close();
    if(!observer)return;
    if (m_runtime_diagnostics && m_runtime_diagnostics != m_startup_diagnostics) m_runtime_diagnostics->close();
    if(!observer)return;
    m_initialization_result.reset();
    m_ready_initialization.reset();
    clearRuntimeDialogs(false);
    setMempoolInfoPollingActive(false);
    if (!observer) return;
    if (!m_destroying) Q_EMIT networkBackendAvailabilityChanged();
    if (!observer) return;
    if (m_destroying) { m_node_ready = false; std::lock_guard lock{m_notifications->mutex}; m_notifications->ready = false; }
    else setNodeReady(false);
    if (!observer) return;
    stopShutdownPolling();
    if (observer) finishDrain();
}

std::function<bool(std::function<void()>)> NodeModel::valueRetirement() const
{
    return [queues = m_observations](std::function<void()> release) {
        return queues->retirement.post([release = std::move(release)] {
            try { release(); } catch (...) {}
        });
    };
}

void NodeModel::finishDrain()
{
    if (!m_workers_stopping || m_drained || m_join_pending || !m_observations_drained ||
        !m_network_model->drained() || !m_prompt_drained) return;
    m_drained = true;
    Q_EMIT workDrained();
}

std::function<execution::SubmitResult(std::shared_ptr<execution::Task>)> NodeModel::postNative(execution::Lane lane) const
{
    if (lane != execution::Lane::Core && lane != execution::Lane::Priority) return {};
    auto* worker = lane == execution::Lane::Priority ? &m_observations->priority : &m_observations->core;
    return [worker, queues = m_observations](std::shared_ptr<execution::Task> packet) {
        if (!packet) return execution::SubmitResult{{}, execution::Rejection::Invalid};
        const bool accepted = worker->post([queues, packet = std::move(packet)]() mutable {
            packet->execute();
            const auto deliver = [packet = std::move(packet)] { packet->deliver(); };
            if (!queues->outcomes.post(deliver)) deliver();
        });
        return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
    };
}

void NodeModel::joinLocalExecution()
{
    m_join_pending = true;
    auto* const app = QCoreApplication::instance();
    JoinThreadAsync([app, executor = std::move(m_owned_executor), values = m_local_diagnostics, cleanup = m_local_retirement,
                     native_ready = m_local_drain->ready.get_future().share()]() mutable {
        native_ready.wait();
        if (values) {
            const auto drained = std::make_shared<std::promise<void>>();
            auto ready = drained->get_future();
            QMetaObject::invokeMethod(app, [app, values = std::move(values), cleanup = std::move(cleanup), drained] {
                values->close();
                values->whenRetired(app, [values, cleanup, drained] {
                    cleanup->close();
                    cleanup->whenDrained([drained] { drained->set_value(); });
                });
            }, Qt::QueuedConnection);
            ready.wait();
        }
        if (executor) executor->stopAndJoin();
    }, this, [this] {
        m_join_pending = false;
        finishDrain();
    });
}

void NodeModel::closeObservations()
{
    const auto queues = m_observations;
    const auto done = [observer = QPointer<NodeModel>{this}, post = presentation::ReliablePost(m_services.post), drain = m_local_drain] {
        if (drain) drain->finish();
        post([observer] {
            if (!observer) return;
            observer->m_observations_drained = true;
            observer->finishDrain();
        });
    };
    const auto workers = {&queues->core, &queues->priority, &queues->retirement};
    const auto close_outcomes = [queues, subscriptions = std::move(m_subscriptions), remaining = std::make_shared<std::atomic_size_t>(workers.size()), done] {
        // Accepted Core work can still hand off its result to Utility. Core has
        // also stopped using the handler collection by the time Utility drains.
        if (remaining->fetch_sub(1) != 1) return;
        queues->outcomes.close();
        queues->outcomes.whenDrained([subscriptions, done] {
            subscriptions->disconnect();
            done();
        });
    };
    for (auto* worker : workers) {
        worker->close();
        worker->whenDrained(close_outcomes);
    }
}

void NodeModel::detachNotifications()
{
    {std::lock_guard lock{m_notifications->mutex};m_notifications->receiver=nullptr;m_notifications->dialogs.clear();}
    if(m_prompt_service)m_prompt_service->close();
}

void NodeModel::publishNotifications()
{
    const QPointer<NodeModel> observer{this};
    const auto current = [&] { return observer && !observer->m_workers_stopping && !observer->m_shutdown_requested && !observer->m_closed; };
    if (!current()) return;
    std::optional<Notifications::Block> block;
    std::optional<Notifications::Header> header;
    std::optional<int> peers;
    std::optional<bool> network_active;
    uint64_t network_generation{0};
    bool warnings;
    bool bans;
    std::optional<bool> subscribed;
    QString subscription_error;
    std::deque<std::shared_ptr<RuntimeDialogRequest>> dialogs;
    bool runtime_overflow;
    {
        std::lock_guard lock{m_notifications->mutex};
        block = std::exchange(m_notifications->block, {});
        header = std::exchange(m_notifications->header, {});
        peers = std::exchange(m_notifications->peers, {});
        network_active = std::exchange(m_notifications->network_active, {});
        network_generation=m_notifications->network_generation;
        warnings = std::exchange(m_notifications->warnings, false);
        bans = std::exchange(m_notifications->bans, false);
    }
    // A public getter sampled after these Native callbacks may already have
    // supplied newer facts. Do not replay an older retained GUI notification.
    if (header) {
        m_seen_header_tip = true;
        if (header->generation <= m_chain_read_generation) header.reset();
    }
    if (block) {
        m_seen_block_tip = true;
    }
    bool block_changed{false};
    {
        // Decide sync completion only after both heights belong to this snapshot.
        const bool previous = std::exchange(m_applying_sync_snapshot, true);
        const auto applying = qScopeGuard([observer, previous] { if (observer) observer->m_applying_sync_snapshot = previous; });
        if (header) {
            m_seen_header_tip = true;
            setHeaderSyncState(header->tip.block_height, header->tip.block_time, header->presync);
            if (!current()) return;
        }
        if (block) {
            m_seen_block_tip = true;
            if (block->generation > m_chain_read_generation) {
                block_changed = true;
                m_block_tip_time = block->tip.block_time;
                setBlockTipHeight(block->tip.block_height);
                if (!current()) return;
            }
            // Getter readback covers height/time, not these independent
            // observations. A delayed callback still owns its sync/progress.
            setVerificationProgress(block->progress);
            if (!current()) return;
            setBlockSyncActive(block->state != SynchronizationState::POST_INIT);
            if (!current()) return;
            // Equal timestamps/heights can still identify a different active
            // chain. Preserve the notification for history consumers, using
            // the newer cached time when getter readback covered this payload.
            Q_EMIT blockTipTimeChanged(m_block_tip_time);
            if (!current()) return;
        }
    }
    {
        std::lock_guard lock{m_notifications->mutex};
        subscribed = std::exchange(m_notifications->subscribed, {});
        subscription_error = m_notifications->subscription_error;
        m_header_spacing = m_notifications->header_spacing;
        dialogs.swap(m_notifications->dialogs);
        runtime_overflow = std::exchange(m_notifications->runtime_overflow, false);
    }
    if (subscribed) {
        m_subscriptions_ready = *subscribed;
        Q_EMIT notificationsReadyChanged();
        if (!current()) return;
        Q_EMIT networkBackendAvailabilityChanged();
        if (!current()) return;
        if (!*subscribed) {
            setStartupError(subscription_error); if (!current()) return;
            setErrorState(true); if (!current()) return;
            requestShutdown(); return;
        }
        if (m_initialization_requested && !m_initialization_dispatched) {
            m_initialization_dispatched = true; Q_EMIT requestedInitialize();
            if (!current()) return;
        }
    }
    if (runtime_overflow) { m_status_error = tr("Some node messages exceeded the dialog queue limit."); Q_EMIT observationErrorChanged(); }
    if (!current()) return;
    for(const auto& request:dialogs){
        if(request->style&CClientUIInterface::ICON_WARNING)
            recordStartupWarningMessage(request->full_message?*request->full_message:request->message);
        else if(request->style&CClientUIInterface::ICON_ERROR)
            recordStartupErrorMessage(request->full_message?*request->full_message:request->message);
        if(!current())return;
    }
    updateSyncCompletion();
    if (!current()) return;
    if (block_changed || header || peers || warnings || network_active) refreshStatus();
    if (network_active && m_network_model) {
        m_network_model->observe(*network_active, network_generation);
        // This bounded legacy property projects the actual captured Core fact.
        // Domain still owns intent reconciliation. A delayed Domain frame may
        // not overwrite a newer native notification during readiness handoff.
        if(network_generation>=m_pause_generation){
            m_pause_generation = network_generation;
            m_network_known = true;
            if (m_pause == *network_active) {
                m_pause = !*network_active;
                Q_EMIT pauseChanged(m_pause);
                if (!current()) return;
            }
        }
    }
    if (bans) Q_EMIT bannedListChanged();
    if (!current()) return;
    if (block || header || peers || warnings || network_active) Q_EMIT nodeInformationChanged();
}

QString NodeModel::observationError() const
{
    QStringList errors{m_status_error, m_mempool_error, m_shutdown_error};
    errors.removeAll(QString{});
    return errors.join(QLatin1Char('\n'));
}

void NodeModel::refreshStatus()
{
    if (!m_backend_queries_ready || m_closed || m_shutdown_requested || m_workers_stopping) return;
    if (!m_status_query) {
        const QPointer<NodeModel> observer{this};
        m_status_query = std::make_shared<execution::SnapshotQuery<StatusSnapshot>>([node = &m_node, state = m_notifications] {
                StatusSnapshot result;
                bool ready, presync;
                {
                    std::lock_guard lock{state->mutex}; ready = state->ready; presync = state->header_presync;
                    result.generation = state->status_generation; result.network_generation = state->network_generation;
                    result.tip_generation = state->tip_generation;
                }
                result.inbound = node->getNodeCount(ConnectionDirection::In);
                result.outbound = node->getNodeCount(ConnectionDirection::Out);
                const auto warning_text = QString::fromStdString(node->getWarnings().translated);
                result.warnings = warning_text.left(32768);
                if (result.warnings.size() < warning_text.size()) result.warnings += QStringLiteral(" ") + QObject::tr("[additional warning text omitted]");
                result.warning_list = SplitWarnings(result.warnings);
                result.network_active = ready ? std::optional<bool>{node->getNetworkActive()} : std::nullopt;
                result.mempool_available = !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY);
                result.data_dir = QString::fromStdString(fs::PathToString(gArgs.GetDataDirNet()));
                result.blocks_dir = QString::fromStdString(fs::PathToString(gArgs.GetBlocksDirPath()));
                result.network = QString::fromStdString(Params().GetChainTypeString());
                result.startup_time = QLocale{}.toString(QDateTime::currentDateTime().addSecs(-TicksSeconds(GetUptime())), QLocale::ShortFormat);
                result.user_agent = QString::fromStdString(strSubVersion);
                if (ready) {
                    result.chain_known = node->getHeaderTip(result.header_height, result.header_time) && !presync;
                    result.block_height = node->getNumBlocks(); result.block_time = node->getLastBlockTime();
                    for (const auto& [addr, info] : node->getNetLocalAddresses()) {
                        if (!result.local_addresses.isEmpty()) result.local_addresses += QStringLiteral(", ");
                        result.local_addresses += QString::fromStdString(addr.ToStringAddr());
                        if (!addr.IsI2P()) result.local_addresses += QStringLiteral(":") + QString::number(info.nPort);
                        if (result.local_addresses.size() > 16384) { result.local_addresses = result.local_addresses.left(16384) + QStringLiteral(" ") + QObject::tr("[additional addresses omitted]"); break; }
                    }
                }
                return result;
            }, [observer, post = m_services.post] { post([observer] { if (observer) observer->publishStatus(); }); },
            postNative(execution::Lane::Core));
    }
    m_status_query->request();
}

void NodeModel::publishStatus()
{
    const QPointer<NodeModel> observer{this};
    const auto current = [&] { return observer && !observer->m_workers_stopping && !observer->m_shutdown_requested && !observer->m_closed; };
    const auto result = m_status_query->take();
    if (!current() || !result) return;
    const auto error = result->error ? tr("Node information could not be refreshed.") : QString{};
    if (m_status_error != error) { m_status_error = error; Q_EMIT observationErrorChanged(); }
    if (!current()) return;
    if (!result->snapshot) return; // A failed read preserves the previous facts.
    const auto& snapshot = *result->snapshot;
    bool network_current, chain_current;
    std::optional<int> total;
    {
        std::lock_guard lock{m_notifications->mutex};
        network_current = snapshot.network_generation == m_notifications->network_generation;
        chain_current = snapshot.tip_generation == m_notifications->tip_generation;
        total = m_notifications->last_peer_total;
    }
    m_information_local_addresses = snapshot.local_addresses;
    m_information_data_dir = snapshot.data_dir; m_information_blocks_dir = snapshot.blocks_dir;
    m_information_network = snapshot.network; m_information_startup_time = snapshot.startup_time;
    m_information_user_agent = snapshot.user_agent;
    if (snapshot.chain_known && chain_current) {
        // InvalidateBlock can lower Core's best header without a header-tip
        // callback. Reuse this already admitted public read instead of keeping
        // a historical maximum in either information or synchronization state.
        m_chain_read_generation = snapshot.tip_generation;
        const bool time_changed = m_block_tip_time != snapshot.block_time || m_block_tip_height != snapshot.block_height;
        m_block_tip_time = snapshot.block_time;
        {
            const bool previous = std::exchange(m_applying_sync_snapshot, true);
            const auto applying = qScopeGuard([observer, previous] { if (observer) observer->m_applying_sync_snapshot = previous; });
            setHeaderSyncState(snapshot.header_height, snapshot.header_time, /*presync=*/false);
            if (!current()) return;
            // A public header projection signal can reenter GUI delivery and
            // consume a newer Native block notification before this setter.
            bool block_current;
            { std::lock_guard lock{m_notifications->mutex}; block_current = snapshot.tip_generation == m_notifications->tip_generation; }
            if (block_current) {
                setBlockTipHeight(snapshot.block_height);
                if (!current()) return;
            }
        }
        updateSyncCompletion();
        if (!current()) return;
        if (time_changed) { Q_EMIT blockTipTimeChanged(m_block_tip_time); if (!current()) return; }
    }
    setNumInboundPeers(snapshot.inbound); if (!current()) return;
    setNumOutboundPeers(snapshot.outbound); if (!current()) return;
    setNumPeers(total.value_or(snapshot.inbound + snapshot.outbound));
    if (!current()) return;
    if (m_warnings != snapshot.warnings || m_warning_list != snapshot.warning_list) {
        m_warnings = snapshot.warnings; m_warning_list = snapshot.warning_list; Q_EMIT warningsChanged();
        if (!current()) return;
    }
    if (snapshot.network_active && network_current && m_network_model)
        m_network_model->observe(*snapshot.network_active,snapshot.network_generation);
    if (m_mempool_information_available != snapshot.mempool_available) {
        m_mempool_information_available = snapshot.mempool_available; Q_EMIT mempoolInfoChanged();
        if (!current()) return;
    }
    Q_EMIT nodeInformationChanged();
}

void NodeModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        const QPointer<NodeModel> observer{this};
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
        if (observer) observer->updateSyncCompletion();
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
    if (!m_backend_queries_ready || m_closed || m_shutdown_requested || m_workers_stopping) return;
    requestMempoolInfoRefresh();
}

void NodeModel::setMempoolInfoPollingActive(bool active)
{
    if ((m_workers_stopping && active) || m_mempool_info_polling_active == active) return;
    const QPointer<NodeModel> observer{this};
    m_mempool_info_polling_active = active;
    if (!m_destroying) Q_EMIT mempoolInfoPollingActiveChanged(active);
    if (!observer) return;
    if (active && m_backend_queries_ready) { schedulePoll(Poll::Mempool); refreshMempoolInfo(); }
    else if (m_services.cancel) m_services.cancel(Poll::Mempool);
}

void NodeModel::initializeMempoolInfoPolling()
{
    m_mempool_info_timer = new QTimer(this);
    m_shutdown_timer = new QTimer(this);
    m_mempool_info_timer->setSingleShot(true); m_shutdown_timer->setSingleShot(true);
    connect(m_mempool_info_timer, &QTimer::timeout, this, [this] {
        auto callback = std::exchange(m_mempool_deadline, {}); if (callback) callback();
    });
    connect(m_shutdown_timer, &QTimer::timeout, this, [this] {
        auto callback = std::exchange(m_shutdown_deadline, {}); if (callback) callback();
    });
    if (!m_services.schedule) m_services.schedule = [this](Poll poll, std::chrono::milliseconds delay, auto callback) {
        auto* timer = poll == Poll::Mempool ? m_mempool_info_timer : m_shutdown_timer;
        auto& deadline = poll == Poll::Mempool ? m_mempool_deadline : m_shutdown_deadline;
        deadline = std::move(callback); timer->start(int(delay.count()));
    };
    if (!m_services.cancel) m_services.cancel = [this](Poll poll) {
        auto* timer = poll == Poll::Mempool ? m_mempool_info_timer : m_shutdown_timer;
        auto& deadline = poll == Poll::Mempool ? m_mempool_deadline : m_shutdown_deadline;
        timer->stop(); deadline = {};
    };
}

void NodeModel::schedulePoll(Poll poll)
{
    if (m_workers_stopping || m_shutdown_requested || m_closed) return;
    if (poll == Poll::Mempool && (!m_mempool_info_polling_active || !m_backend_queries_ready)) return;
    if (poll == Poll::Shutdown && (!m_shutdown_polling || m_fatal_exception)) return;
    const QPointer<NodeModel> observer{this};
    m_services.schedule(poll, poll == Poll::Mempool ? std::chrono::milliseconds{MEMPOOL_INFO_POLLING_INTERVAL_MS} : 200ms,
        [observer, poll] {
            if (!observer || observer->m_workers_stopping || observer->m_shutdown_requested || observer->m_closed) return;
            if (poll == Poll::Mempool) {
                if (!observer->m_mempool_info_polling_active) return;
                observer->refreshMempoolInfo(); observer->refreshStatus();
            } else {
                if (!observer->m_shutdown_polling || observer->m_fatal_exception) return;
                observer->m_shutdown_query->request();
            }
            observer->schedulePoll(poll);
        });
}

void NodeModel::requestMempoolInfoRefresh()
{
    if (!m_mempool_query) {
        const QPointer<NodeModel> observer{this};
        m_mempool_query = std::make_shared<execution::SnapshotQuery<MempoolInfo>>([node = &m_node] { return MempoolInfo{int(node->getMempoolSize()), node->getMempoolDynamicUsage() / 1'000'000.0, node->getMempoolMaxUsage() / 1'000'000.0}; },
            [observer, post = m_services.post] { post([observer] { if (observer) observer->publishMempoolInfo(); }); },
            postNative(execution::Lane::Core));
    }
    m_mempool_query->request();
}

void NodeModel::publishMempoolInfo()
{
    const auto result = m_mempool_query->take();
    if (!result || m_workers_stopping || m_shutdown_requested) return;
    const auto error = result->error ? tr("Mempool information could not be refreshed.") : QString{};
    if (m_mempool_error != error) { m_mempool_error = error; Q_EMIT observationErrorChanged(); }
    if (result->snapshot) applyMempoolInfo(*result->snapshot);
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
        m_sync_progress_tracker.addSample(m_services.monotonic_milliseconds(), new_progress)};
    if (!estimate || *estimate == m_remaining_sync_time) return;

    m_remaining_sync_time = *estimate;
    Q_EMIT remainingSyncTimeChanged();
}
void NodeModel::setVerificationProgress(double new_progress)
{
    if (new_progress != m_verification_progress) {
        const QPointer<NodeModel> observer{this};
        setRemainingSyncTime(new_progress);
        if (!observer || m_workers_stopping || m_shutdown_requested || m_closed) return;

        m_verification_progress = new_progress;
        Q_EMIT verificationProgressChanged();
    }
}

void NodeModel::setNodeReady(bool ready)
{
    if (m_node_ready == ready) return;

    m_node_ready = ready;
    { std::lock_guard lock{m_notifications->mutex}; m_notifications->ready = ready; }
    updateSyncCompletion();
}

void NodeModel::setBlockSyncActive(bool active)
{
    if (m_block_sync_active == active) {
        return;
    }

    const QPointer<NodeModel> observer{this};
    m_block_sync_active = active;
    Q_EMIT blockSyncActiveChanged();
    if (observer) observer->updateSyncCompletion();
}

void NodeModel::setHeaderSyncState(int height, int64_t block_time, bool presync)
{
    const QPointer<NodeModel> observer{this};
    m_header_tip_height = height;
    m_header_tip_time = block_time;

    const int64_t estimate_headers_left{(m_services.wall_seconds() - block_time) / m_header_spacing};
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
    if (observer) observer->updateSyncCompletion();
}

void NodeModel::updateSyncCompletion()
{
    if (m_applying_sync_snapshot || m_workers_stopping || m_shutdown_requested || m_closed) return;
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
    if(networkActionReady())m_network_model->requestActive(!new_pause);
}
void NodeModel::updateNetworkAvailability()
{
    const bool available=networkBackendAvailable();
    if(!m_network_model||available==m_network_available)return;
    m_network_available=available;
    m_network_model->setAvailable(available);
}
void NodeModel::publishNetworkAction()
{
    if(!m_network_model||m_destroying)return;
    const QPointer<NodeModel> observer{this};
    const bool current_observation=m_network_model->known()&&m_network_model->observedGeneration()>=m_pause_generation;
    if(current_observation){m_network_known=true;m_pause_generation=m_network_model->observedGeneration();}
    if(current_observation && m_pause==m_network_model->observedActive()){
        m_pause=!m_network_model->observedActive();Q_EMIT pauseChanged(m_pause);if(!observer)return;
    }
    const bool pending=m_network_model->pending();
    if(m_network_action_pending!=pending){m_network_action_pending=pending;Q_EMIT networkActionPendingChanged();if(!observer)return;}
    const auto error=m_network_model->error();
    if(m_network_action_error!=error){m_network_action_error=error;Q_EMIT networkActionErrorChanged();if(!observer)return;}
    const bool ready=networkActionReady();
    if(m_network_ready_snapshot!=ready){m_network_ready_snapshot=ready;Q_EMIT networkActionReadyChanged();if(!observer)return;}
    Q_EMIT nodeInformationChanged();
}
void NodeModel::setLifecyclePhase(nodelifecycle::Phase phase)
{
    if(m_lifecycle_phase==phase)return;
    m_lifecycle_phase=phase;Q_EMIT lifecyclePhaseChanged();
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

void NodeModel::diagnosticLost(const QString& error)
{
    if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
    m_status_error = error; Q_EMIT observationErrorChanged();
}

void NodeModel::addStartupWarnings(const QStringList& warnings)
{
    if (!m_startup_diagnostics->append(warnings, true)) diagnosticLost(tr("Startup warnings could not be shown."));
}

void NodeModel::addStartupWarnings(std::shared_ptr<const startupdiagnostics::Collection> warnings)
{
    if (!m_startup_diagnostics->adopt(std::move(warnings))) diagnosticLost(tr("Startup warnings could not be shown."));
}

bool NodeModel::showStartupWarnings()
{
    if (m_startup_warnings_shown || !m_startup_diagnostics->hasWarnings()) return false;
    m_startup_warnings_shown = true;
    return showRuntimeDialogOnGuiThread(m_startup_diagnostics->summary(), CClientUIInterface::ICON_WARNING,
        /*question=*/false, /*startup_summary=*/true);
}

void NodeModel::recordStartupErrorMessage(const QString& message)
{
    if (!m_startup_diagnostics->append(message, false)) diagnosticLost(tr("A startup error could not be shown."));
}

void NodeModel::recordStartupWarningMessage(const QString& message)
{
    if (!m_startup_diagnostics->append(message, true)) diagnosticLost(tr("A startup warning could not be shown."));
}

void NodeModel::startNodeInitializionThread()
{
    if (m_initialization_requested) {
        return;
    }
    m_initialization_requested = true;
    if (m_subscriptions_ready) { m_initialization_dispatched = true; Q_EMIT requestedInitialize(); }
}

void NodeModel::requestShutdown()
{
    if (m_fatal_exception) {
        std::_Exit(EXIT_FAILURE);
    }
    if (m_shutdown_requested) {
        return;
    }
    const QPointer<NodeModel> observer{this};
    m_shutdown_requested = true;
    Q_EMIT shutdownRequestedChanged();
    if (!observer) return;
    if(m_status_query)m_status_query->close();
    if(m_mempool_query)m_mempool_query->close();
    m_ready_initialization.reset();
    setNodeReady(false);
    if (!observer) return;
    detachNotifications();
    Q_EMIT networkBackendAvailabilityChanged();
    if (!observer) return;
    clearRuntimeDialogs(true);
    if (!observer) return;
    stopShutdownPolling();
    if (!observer) return;
    Q_EMIT requestedShutdown();
}

void NodeModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info, bool initial_block_download, bool shutdown_requested)
{
    if (m_workers_stopping || m_fatal_exception || m_closed || m_initialization_result) return;
    const QPointer<NodeModel> observer{this};
    publishNotifications();
    if (!observer || m_workers_stopping || m_fatal_exception || m_closed) return;
    if (m_startup_diagnostics_sealed || (success && (m_shutdown_requested || shutdown_requested)) ||
        (!success && m_startup_failure_dialog_shown)) {
        finishInitialization(success, tip_info, initial_block_download, shutdown_requested); return;
    }
    m_startup_diagnostics_sealed = true;
    m_initialization_result = InitializationResult{success, tip_info, initial_block_download, shutdown_requested};
    if (success) {
        m_runtime_dialogs_enabled = true;
        std::lock_guard lock{m_notifications->mutex};m_notifications->runtime_enabled=true;
    }
    const bool admitted = m_startup_diagnostics->project(success, tr("Node initialization failed."), tr("Startup warnings:"));
    if (observer && !admitted) finishStartupDiagnostics();
}

void NodeModel::finishStartupDiagnostics()
{
    if (m_workers_stopping && !m_fatal_exception) return;
    const QPointer<NodeModel> observer{this};
    if (m_initialization_result) {
        const auto result = *std::exchange(m_initialization_result, {});
        if (!m_startup_diagnostics->error().isEmpty()) {
            if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
            const auto error = m_startup_diagnostics->error();
            setErrorState(true); if (!observer) return;
            setStartupError(error); if (observer) Q_EMIT nodeInitialized(); return;
        }
        finishInitialization(result.success, result.tip, result.initial_block_download, result.shutdown_requested);
    } else if (m_fatal_exception) {
        setStartupError(m_startup_diagnostics->error().isEmpty() ? m_startup_diagnostics->summary() : m_startup_diagnostics->error());
    }
}

void NodeModel::finishInitialization(bool success, interfaces::BlockAndHeaderTipInfo tip_info, bool initial_block_download, bool shutdown_requested)
{
    const QPointer<NodeModel> observer{this};
    const auto current = [&] { return observer && !observer->m_workers_stopping && !observer->m_fatal_exception && !observer->m_closed && !observer->m_shutdown_requested; };
    if (m_workers_stopping || m_fatal_exception || m_closed) return;
    // Consume startup notifications before changing their presentation phase.
    publishNotifications();
    if (!observer || m_workers_stopping || m_fatal_exception || m_closed) return;
    if (success && (m_shutdown_requested || shutdown_requested)) {
        requestShutdown();
        return;
    }

    if (!success) {
        if (m_startup_failure_dialog_shown) {
            requestShutdown();
            if (observer && !m_workers_stopping && !m_closed) Q_EMIT nodeInitialized();
            return;
        }
        setErrorState(true);
        if (!current()) return;
        refreshStatus();
        if (!current()) return;
        setStartupError(m_startup_diagnostics->summary());
        if (!current()) return;
        if (shutdown_requested) {
            requestShutdown();
            if (!observer || m_workers_stopping || m_closed) return;
        }
    } else {
        m_backend_queries_ready = true;
        Q_EMIT networkBackendAvailabilityChanged();
        if (!current()) return;
        if (m_mempool_info_polling_active) {
            schedulePoll(Poll::Mempool);
            if (!current()) return;
        }
        m_runtime_dialogs_enabled = true;
        {std::lock_guard lock{m_notifications->mutex};m_notifications->runtime_enabled=true;}
        m_ready_initialization = InitializationResult{success, tip_info, initial_block_download, shutdown_requested};
        const bool warning_pending = showStartupWarnings();
        if (!current()) return;
        m_startup_warning_pending = warning_pending;
        finishReadyInitialization();
        return;
    }
    Q_EMIT nodeInitialized();
}

void NodeModel::finishReadyInitialization()
{
    const QPointer<NodeModel> observer{this};
    const auto current = [&] { return observer && !observer->m_workers_stopping && !observer->m_fatal_exception && !observer->m_closed && !observer->m_shutdown_requested; };
    if (!current() || m_startup_warning_pending || !m_ready_initialization) return;
    const auto result = *std::exchange(m_ready_initialization, {});
    {
        const bool previous = std::exchange(m_applying_sync_snapshot, true);
        const auto applying = qScopeGuard([observer, previous] { if (observer) observer->m_applying_sync_snapshot = previous; });
        publishNotifications();
        if (!current()) return;
        if (!m_seen_block_tip) {
            m_block_tip_time = result.tip.block_time;
            setBlockTipHeight(result.tip.block_height);
            if (!current()) return;
            setVerificationProgress(result.tip.verification_progress);
            if (!current()) return;
            setBlockSyncActive(result.initial_block_download);
            if (!current()) return;
        }
        // LoadChainTip emits an INIT_DOWNLOAD notification even on a caught-up
        // restart. Core's IBD flag only latches false: preserve a newer POST_INIT
        // notification, but let this final startup result clear the init flag.
        if (!result.initial_block_download) setBlockSyncActive(false);
        if (!current()) return;
        if (!m_seen_header_tip) setHeaderSyncState(result.tip.header_height, result.tip.header_time, /*presync=*/false);
        if (!current()) return;
        setNodeReady(true);
        if (!current()) return;
    }
    updateSyncCompletion();
    if (!current()) return;
    refreshStatus();
    if (!current()) return;
    refreshMempoolInfo();
    if (!current()) return;
    Q_EMIT chainStateReady();
    if (!current()) return;
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
    m_initialization_result.reset();
    m_ready_initialization.reset();
    const bool retained = m_startup_diagnostics->resetInput() && m_startup_diagnostics->append(message, false);
    if (!retained && m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
    const auto prefix = tr("A fatal error occurred. %1 can no longer continue safely and will quit.").arg(CLIENT_NAME);
    const auto fallback = retained ? QString{} : tr("The full error is too large to show. Check debug.log for details.");
    if (!m_startup_diagnostics->project(false, fallback, {}, prefix, /*normalize=*/false)) {
        if (m_services.diagnostic_loss) *m_services.diagnostic_loss = true;
        setStartupError(prefix + QStringLiteral("\n\n") + m_startup_diagnostics->error());
    }
}

void NodeModel::startShutdownPolling()
{
    if (m_fatal_exception || m_shutdown_requested || m_workers_stopping || m_shutdown_polling) return;
    if (!m_shutdown_query) {
        const QPointer<NodeModel> observer{this};
        m_shutdown_query = std::make_shared<execution::SnapshotQuery<ShutdownSnapshot>>([node = &m_node] { return ShutdownSnapshot{node->shutdownRequested()}; },
            [observer, post = m_services.post] { post([observer] { if (observer) observer->publishShutdownStatus(); }); },
            postNative(execution::Lane::Priority));
    }
    m_shutdown_polling = true; schedulePoll(Poll::Shutdown);
}

void NodeModel::stopShutdownPolling()
{
    m_shutdown_polling = false;
    if (m_services.cancel) m_services.cancel(Poll::Shutdown);
}

void NodeModel::publishShutdownStatus()
{
    const auto result = m_shutdown_query->take();
    if (!result || m_workers_stopping || m_shutdown_requested) return;
    const auto error = result->error ? tr("The node shutdown state could not be checked.") : QString{};
    if (m_shutdown_error != error) { m_shutdown_error = error; Q_EMIT observationErrorChanged(); }
    if (result->snapshot && result->snapshot->requested && !m_fatal_exception) requestShutdown();
}

void NodeModel::initializeSubscriptions()
{
    const auto subscriptions = m_subscriptions;
    const auto state = m_notifications;
    const auto done = [state](bool subscribed, const QString& error = {}) {
        std::lock_guard lock{state->mutex}; state->subscribed = subscribed; state->subscription_error = error; state->schedule();
    };
    const auto register_handlers = [node = &m_node, state, subscriptions] {
        { std::lock_guard lock{state->mutex}; state->header_spacing = std::max(int64_t{1}, int64_t{Params().GetConsensus().nPowTargetSpacing}); }
        auto& handlers = subscriptions->handlers;
        handlers.push_back(node->handleNotifyBlockTip([state](SynchronizationState sync, interfaces::BlockTip tip, double progress) {
            std::lock_guard lock{state->mutex}; state->block = Notifications::Block{sync, std::move(tip), progress, ++state->tip_generation}; state->schedule();
        }));
        handlers.push_back(node->handleNotifyHeaderTip([state](SynchronizationState, interfaces::BlockTip tip, bool presync) {
            std::lock_guard lock{state->mutex}; state->header_presync = presync;
            state->header = Notifications::Header{std::move(tip), presync, ++state->tip_generation}; state->schedule();
        }));
        handlers.push_back(node->handleNotifyNumConnectionsChanged([state](int total) {
            std::lock_guard lock{state->mutex}; state->peers = total; state->last_peer_total = total; ++state->status_generation; state->schedule();
        }));
        handlers.push_back(node->handleNotifyNetworkActiveChanged([state](bool active) {
            std::lock_guard lock{state->mutex}; state->network_active = active; ++state->network_generation; ++state->status_generation; state->schedule();
        }));
        handlers.push_back(node->handleNotifyAlertChanged([state] {
            std::lock_guard lock{state->mutex}; state->warnings = true; ++state->status_generation; state->schedule();
        }));
        const auto convert = [state](const std::string& text) {
            const bool gui = QThread::currentThread() == QCoreApplication::instance()->thread();
            if (!gui) return QString::fromStdString(text);
            QString result = QString::fromUtf8(text.data(), std::min<size_t>(text.size(), 16384));
            if (text.size() > 16384) {
                if (state->diagnostic_loss) *state->diagnostic_loss = true;
                result += QStringLiteral(" ") + QObject::tr("[complete diagnostic rejected: Core work ran on GUI]");
            }
            return result;
        };
        handlers.push_back(node->handleMessageBox([state, convert](const bilingual_str& message, unsigned int style) {
            state->request(convert(message.translated), style, false);
        }));
        handlers.push_back(node->handleQuestion([state, convert](const bilingual_str& message, const std::string&, unsigned int style) {
            if (QThread::currentThread() == QCoreApplication::instance()->thread()) return false;
            return state->request(convert(message.translated), style, true);
        }));
        handlers.push_back(node->handleBannedListChanged([state] {
            std::lock_guard lock{state->mutex}; state->bans = true; state->schedule();
        }));
    };
    const auto registered = [done] { done(true); };
    const auto failed = [done](std::exception_ptr) { done(false, QObject::tr("Node notification handlers could not be installed.")); };
    const auto post = postNative(execution::Lane::Core);
    if (!post(execution::MakeTask(register_handlers, registered, failed))) {
        done(false, tr("Node notifications could not be set up."));
    }
}

QVariantList NodeModel::nodeInformationRows()
{
    if (!m_backend_queries_ready || m_shutdown_requested || m_workers_stopping) return {};
    const int header_height = m_header_tip_height;
    const int64_t header_time = m_header_tip_time;
    const QString local_addresses = m_information_local_addresses.isEmpty() ? tr("None") : m_information_local_addresses;
    const int block_height = m_block_tip_height;
    const int64_t last_block_time = m_block_tip_time;
    const QString warning_text{m_warning_list.empty() ? tr("None") : m_warning_list.join(QStringLiteral("\n"))};

    QVariantList rows;
    rows.push_back(InformationRow(tr("Client version"), fullClientVersion(), /*mono=*/true));
    rows.push_back(InformationRow(tr("User agent"), m_information_user_agent, /*mono=*/true));
    rows.push_back(InformationRow(tr("Datadir"), m_information_data_dir, /*mono=*/true));
    rows.push_back(InformationRow(tr("Blocks dir"), m_information_blocks_dir, /*mono=*/true));
    rows.push_back(InformationRow(tr("Startup time"), m_information_startup_time));
    rows.push_back(InformationRow(tr("Network"), m_information_network));
    rows.push_back(InformationRow(tr("Block height"), QString::number(block_height)));
    rows.push_back(InformationRow(tr("Header height"), QString::number(header_height)));
    rows.push_back(InformationRow(tr("Header time"), header_time > 0 ? QLocale{}.toString(QDateTime::fromSecsSinceEpoch(header_time), QLocale::ShortFormat) : tr("Unknown")));
    rows.push_back(InformationRow(tr("Last block time"), last_block_time > 0 ? QLocale{}.toString(QDateTime::fromSecsSinceEpoch(last_block_time), QLocale::ShortFormat) : tr("Unknown")));
    rows.push_back(InformationRow(tr("Verification progress"), QStringLiteral("%1%").arg(QString::number(m_verification_progress * 100.0, 'f', 2))));
    rows.push_back(InformationRow(tr("Peers"), tr("%1 total (%2 inbound, %3 outbound)").arg(m_num_peers).arg(m_num_inbound_peers).arg(m_num_outbound_peers)));
    rows.push_back(InformationRow(tr("Network active"), m_network_known ? (!m_pause ? tr("Yes") : tr("No")) : tr("Unknown")));
    rows.push_back(InformationRow(tr("Local addresses"), local_addresses));
    rows.push_back(InformationRow(tr("Warnings"), warning_text));
    return rows;
}

bool NodeModel::showRuntimeDialogOnGuiThread(const QString& message,unsigned int style,bool question,bool startup_summary)
{
    if(m_shutdown_requested||m_workers_stopping)return false;
    if(!m_runtime_dialogs_enabled&&!question&&!startup_summary){
        if(style&CClientUIInterface::ICON_WARNING){recordStartupWarningMessage(message);return false;}
        if(!(style&CClientUIInterface::MODAL)){if(style&CClientUIInterface::ICON_ERROR)recordStartupErrorMessage(message);return false;}
    }
    auto details=startupdiagnostics::RetainText(m_dialog_text_budget,message,m_notifications->retire_diagnostic);
    QString summary=message.left(16'384);
    if(!details){if(m_services.diagnostic_loss)*m_services.diagnostic_loss=true;summary+=QStringLiteral(" ")+tr("[full message not available: too many messages]");}
    const bool complete=bool(details);
    summary=summary.left(static_cast<qsizetype>(runtimeprompt::MAX_SUMMARY_CHARACTERS));
    const auto request=std::make_shared<const runtimeprompt::Request>(startup_summary?runtimeprompt::Origin::StartupSummary:
        m_runtime_dialogs_enabled?runtimeprompt::Origin::Runtime:runtimeprompt::Origin::Startup,style,question,std::move(summary),std::move(details),complete);
    std::shared_ptr<runtimeprompt::Continuation> unused;
    if(startup_summary)m_startup_warning_answer=request->answer;
    if(!m_prompt_service->enqueue(request,false,unused)){
        if(startup_summary)m_startup_warning_answer.reset();
        m_status_error=tr("A node dialog could not be shown because the dialog queue is full.");Q_EMIT observationErrorChanged();
        return false;
    }
    return true;
}
QVariantList NodeModel::runtimePrompts()const
{
    return m_runtime_prompt?QVariantList{QVariant::fromValue(m_runtime_prompt)}:QVariantList{};
}
void NodeModel::publishRuntimePrompt()
{
    if(m_closed||m_shutdown_requested||m_workers_stopping)return;
    const auto projection=m_prompt_service->projection();
    const QPointer<NodeModel> observer{this};
    if(projection.rejected>m_prompt_rejections){
        m_prompt_rejections=projection.rejected;
        m_status_error=tr("A node message could not be shown because too many are waiting.");
        Q_EMIT observationErrorChanged();if(!observer||m_shutdown_requested||m_workers_stopping)return;
    }
    const auto active=projection.state==runtimeprompt::State::Presenting||projection.state==runtimeprompt::State::AwaitingAnswer?projection.active:nullptr;
    if(m_runtime_prompt&&active&&m_runtime_prompt->id()==active->descriptor.id){
        completeStartupWarning(active->request->answer==m_startup_warning_answer);
        return;
    }
    if(!m_runtime_prompt&&!active){completeStartupWarning(false);return;}
    clearRuntimeDialogs(false);if(!observer||m_shutdown_requested||m_workers_stopping)return;
    if(active){
        const auto& request=*active->request;
        if(request.origin==runtimeprompt::Origin::StartupSummary)m_runtime_diagnostics=m_startup_diagnostics;
        else{
            m_runtime_diagnostics=new StartupDiagnosticsModel(m_services.post, *m_services.diagnostics, this);
            m_runtime_diagnostics->append(request.details?*request.details:request.summary,false);
            if(!m_runtime_diagnostics->project(false,{},{},{},false)&&m_services.diagnostic_loss)*m_services.diagnostic_loss=true;
        }
        m_runtime_prompt=new RuntimePromptModel(active,m_prompt_service,m_runtime_diagnostics,this);
        m_runtime_dialog_title=m_runtime_prompt->title();m_runtime_dialog_message=m_runtime_prompt->message();
        m_runtime_dialog_icon=m_runtime_prompt->icon();m_runtime_dialog_buttons=m_runtime_prompt->buttons();m_runtime_dialog_question=m_runtime_prompt->question();
        if(request.origin==runtimeprompt::Origin::Startup&&(request.question||(request.style&CClientUIInterface::ICON_ERROR)))m_startup_failure_dialog_shown=true;
        m_runtime_dialog_visible=true;
    }
    const bool startup_summary=active&&active->request->answer==m_startup_warning_answer;
    Q_EMIT runtimeDialogChanged();
    if(!observer||m_shutdown_requested||m_workers_stopping||m_closed)return;
    completeStartupWarning(startup_summary);
}
void NodeModel::completeStartupWarning(bool presented)
{
    if(!m_startup_warning_pending||(!presented&&(!m_startup_warning_answer||!m_startup_warning_answer->result())))return;
    const QPointer<NodeModel> observer{this};
    m_startup_warning_pending=false;
    m_startup_warning_answer.reset();
    if(!presented){
        m_status_error=tr("The startup warning could not be presented. Its diagnostics remain available.");
        Q_EMIT observationErrorChanged();
        if(!observer||m_shutdown_requested||m_workers_stopping||m_closed)return;
    }
    finishReadyInitialization();
}
void NodeModel::clearRuntimeDialogs(bool notify)
{
    const QPointer<NodeModel> observer{this};
    auto* prompt=std::exchange(m_runtime_prompt,nullptr);
    const QPointer<StartupDiagnosticsModel> details{std::exchange(m_runtime_diagnostics,nullptr)};
    const bool retire=details&&details!=m_startup_diagnostics;
    const bool visible=std::exchange(m_runtime_dialog_visible,false);
    if(prompt)prompt->deleteLater();
    if(retire){details->close();if(details)details->deleteLater();}
    if(observer&&notify&&visible)Q_EMIT runtimeDialogChanged();
}
void NodeModel::answerRuntimeDialog(unsigned int button)
{
    // Compatibility/native automation boundary. Real QML carries the immutable
    // per-prompt QObject and cannot retarget a delayed click to its successor.
    if(m_runtime_prompt){m_runtime_prompt->presented();m_runtime_prompt->answer(button);}
}

#ifdef ENABLE_TEST_AUTOMATION
void NodeModel::showRuntimeDialogForTest(const QString& message,unsigned int style,bool question)
{
    auto details=startupdiagnostics::RetainText(m_dialog_text_budget,message,m_notifications->retire_diagnostic);
    const auto request=std::make_shared<const runtimeprompt::Request>(runtimeprompt::Origin::Runtime,style,question,message.left(16384),std::move(details));
    std::shared_ptr<runtimeprompt::Continuation> unused;m_prompt_service->enqueue(request,false,unused);
}
#endif
