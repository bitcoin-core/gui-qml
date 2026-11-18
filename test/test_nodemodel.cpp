// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualnodeobservations.h>
#include <test/qt_test_registry.h>

#include <qml/models/nodemodel.h>
#include <qml/executor.h>
#include <qml/guipresentation.h>
#include <qml/ownedwork.h>
#include <qml/retainedvalue.h>
#include <qml/initexecutor.h>
#include <qml/quithandler.h>
#include <qml/shutdowncoordinator.h>

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <chainparams.h>
#include <node/interface_ui.h>
#include <util/translation.h>
#include <util/time.h>
#include <validation.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>

#include <QVariantMap>
#include <QMetaProperty>
#include <QDateTime>
#include <QLocale>
#include <QEventLoop>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTimer>

namespace {
constexpr int ASYNC_TIMEOUT_MS{5'000};

template <typename T>
concept HasLegacyPeerCommands = requires(T& model) { model.requestDisconnectPeer(qint64{1}); }
    || requires(T& model) { model.requestBanPeer(QString{}, int64_t{3600}); }
    || requires(T& model) { model.disconnectPeer(qint64{1}); }
    || requires(T& model) { model.banPeer(QString{}, int64_t{3600}); };
template <typename T>
concept HasLegacyTrafficGetters = requires(T& model) { model.getTotalBytesReceived(); }
    || requires(T& model) { model.getTotalBytesSent(); };
static_assert(!HasLegacyPeerCommands<NodeModel>);
static_assert(!HasLegacyTrafficGetters<NodeModel>);

void DrainNodeModel(NodeModel& model)
{
    if (model.isDrained()) return;
    QEventLoop loop;
    QObject::connect(&model, &NodeModel::workDrained, &loop, &QEventLoop::quit);
    model.beginShutdown();
    while (!model.isDrained()) loop.exec();
}

QString InformationValue(NodeModel& model, const QString& label)
{
    for (const auto& row : model.nodeInformationRows()) {
        const auto values = row.toMap();
        if (values.value(QStringLiteral("label")).toString() == label) return values.value(QStringLiteral("value")).toString();
    }
    return {};
}

std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

struct MempoolState {
    std::atomic<size_t> count{0};
    std::atomic<size_t> usage_bytes{0};
    std::atomic<size_t> max_bytes{0};
    std::atomic<int> max_usage_calls{0};
};

struct PeerCountState {
    std::atomic<size_t> total{0};
    std::atomic<size_t> inbound{0};
    std::atomic<size_t> outbound{0};
};

void ConfigureNodeModelDefaults(MockNode& node)
{
    node.handle_notify_block_tip_fn = [](interfaces::Node::NotifyBlockTipFn) { return MakeNoopHandler(); };
    node.handle_notify_header_tip_fn = [](interfaces::Node::NotifyHeaderTipFn) { return MakeNoopHandler(); };
    node.handle_notify_num_connections_changed_fn = [](interfaces::Node::NotifyNumConnectionsChangedFn) { return MakeNoopHandler(); };
    node.handle_notify_network_active_changed_fn = [](interfaces::Node::NotifyNetworkActiveChangedFn) { return MakeNoopHandler(); };
    node.handle_notify_alert_changed_fn = [](interfaces::Node::NotifyAlertChangedFn) { return MakeNoopHandler(); };
    node.handle_message_box_fn = [](interfaces::Node::MessageBoxFn) { return MakeNoopHandler(); };
    node.handle_question_fn = [](interfaces::Node::QuestionFn) { return MakeNoopHandler(); };
    node.handle_banned_list_changed_fn = [](interfaces::Node::BannedListChangedFn) { return MakeNoopHandler(); };
    node.get_warnings_fn = [] { return Untranslated(""); };
}

void ConfigurePeerCountGetters(MockNode& node, PeerCountState& peers)
{
    node.get_node_count_fn = [&peers](ConnectionDirection direction) {
        switch (direction) {
        case ConnectionDirection::Both:
            return peers.total.load();
        case ConnectionDirection::In:
            return peers.inbound.load();
        case ConnectionDirection::Out:
            return peers.outbound.load();
        case ConnectionDirection::None:
            return size_t{0};
        }
        return size_t{0};
    };
}

void ConfigureMempoolGetters(MockNode& node, MempoolState& mempool)
{
    node.get_mempool_size_fn = [&mempool] {
        return mempool.count.load();
    };
    node.get_mempool_dynamic_usage_fn = [&mempool] {
        return mempool.usage_bytes.load();
    };
    node.get_mempool_max_usage_fn = [&mempool] {
        ++mempool.max_usage_calls;
        return mempool.max_bytes.load();
    };
}

void WaitForInitialMempoolRefresh(MempoolState& mempool)
{
    QTRY_VERIFY_WITH_TIMEOUT(mempool.max_usage_calls.load() >= 1, ASYNC_TIMEOUT_MS);
    QCoreApplication::processEvents();
}

// These are public Node observations/callbacks, driven by the existing manual
// transport. No private model state or Core chainstate is inspected.
struct ManualTipState {
    int block{200}, header{200};
    int64_t now{1'800'000'000}, block_time{now}, header_time{now};
    bool header_available{true};
    interfaces::Node::NotifyBlockTipFn block_tip;
    interfaces::Node::NotifyHeaderTipFn header_tip;
    interfaces::Node::NotifyNumConnectionsChangedFn peers;
};

void ConfigureManualTips(MockNode& node, ManualTipState& tips)
{
    ConfigureNodeModelDefaults(node);
    node.get_num_blocks_fn = [&] { return tips.block; };
    node.get_last_block_time_fn = [&] { return tips.block_time; };
    node.get_header_tip_fn = [&](int& height, int64_t& time) {
        if (!tips.header_available) return false;
        height = tips.header; time = tips.header_time; return true;
    };
    node.handle_notify_block_tip_fn = [&](auto callback) { tips.block_tip = std::move(callback); return MakeNoopHandler(); };
    node.handle_notify_header_tip_fn = [&](auto callback) { tips.header_tip = std::move(callback); return MakeNoopHandler(); };
    node.handle_notify_num_connections_changed_fn = [&](auto callback) { tips.peers = std::move(callback); return MakeNoopHandler(); };
}

NodeModel::Services ManualTipServices(ManualNodeObservations& env, ManualTipState& tips)
{
    auto services = env.services();
    services.wall_seconds = [&] { return tips.now; };
    services.monotonic_milliseconds = [] { return int64_t{0}; };
    return services;
}

} // namespace

class NodeModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void featureOwnersReplaceTheLegacyNodeSchema()
    {
        const auto& meta = NodeModel::staticMetaObject;
        QCOMPARE(meta.indexOfProperty("peerActionPending"), -1);
        QCOMPARE(meta.indexOfMethod("peerActionChanged()"), -1);
        QCOMPARE(meta.indexOfMethod("requestDisconnectPeer(qint64)"), -1);
        QCOMPARE(meta.indexOfMethod("getTotalBytesReceived()"), -1);
        const auto peer = meta.property(meta.indexOfProperty("peerCommands"));
        QVERIFY(peer.isValid()); QVERIFY(peer.isConstant());
        QCOMPARE(peer.metaType().id(), qMetaTypeId<PeerCommandModel*>());
        for (const char* name : {"pause", "faulted"}) {
            const auto property = meta.property(meta.indexOfProperty(name));
            QVERIFY(property.isValid()); QVERIFY(property.hasNotifySignal()); QVERIFY(!property.isWritable());
        }

        const auto pending = PeerCommandModel::staticMetaObject.property(PeerCommandModel::staticMetaObject.indexOfProperty("pending"));
        QVERIFY(pending.hasNotifySignal()); QCOMPARE(pending.notifySignal().name(), QByteArray{"changed"});
    }
    void manualStartupWarningAdmissionRejectionCanDeleteTheObserver()
    {
        MockNode node;
        ConfigureNodeModelDefaults(node);
        MempoolState mempool;
        ConfigureMempoolGetters(node,mempool);
        ManualNodeObservations env;
        auto services = env.services();
        auto model=std::make_unique<NodeModel>(node,false,std::move(services));
        bool rejection{false},armed{false};
        int chain_ready{0};
        const auto cleanup=qScopeGuard([&]{
            if(model){QObject::disconnect(model.get(),nullptr,this,nullptr);model->beginShutdown();model.reset();}
            for(int turn=0;turn<8;++turn){env.drain();env.publishAll();}
        });
        env.drain();env.publishAll();
        QVERIFY(model->notificationsReady());
        connect(model.get(),&NodeModel::chainStateReady,this,[&]{++chain_ready;});
        // Availability ingress is already admitted by the earlier connection;
        // reject the next fresh prompt intake, before any handoff is owned.
        connect(model.get(),&NodeModel::networkBackendAvailabilityChanged,this,[&]{if(!armed){armed=true;env.rejectNext(execution::Lane::Utility);}});
        connect(model.get(),&NodeModel::observationErrorChanged,this,[&]{rejection=true;model.reset();});
        model->addStartupWarnings({QStringLiteral("Original startup warning")});
        model->initializeResult(true,{},false,false);
        for(int turn=0;turn<8&&model;++turn){env.drain();env.publishAll();}
        QVERIFY(armed);
        QVERIFY(rejection);
        QVERIFY(!model);
        QCOMPARE(chain_ready,0);
        for(int turn=0;turn<8;++turn){env.drain();env.publishAll();}
        QVERIFY(DrainManualNodeTurns(env));
    }

    void manualPauseObserverKeepsItsReentrantIntentPending()
    {
        struct NetworkNode final : MockNode {
            std::function<void(bool)> set_active;
            void setNetworkActive(bool value)override{set_active(value);}
        } node;
        ConfigureNodeModelDefaults(node);
        MempoolState mempool;
        ConfigureMempoolGetters(node,mempool);
        bool active{false},reentered{false};
        int writes{0};
        interfaces::Node::NotifyNetworkActiveChangedFn notice;
        node.handle_notify_network_active_changed_fn=[&](auto callback){notice=std::move(callback);return MakeNoopHandler();};
        node.get_network_active_fn=[&]{return active;};
        node.set_active=[&](bool value){++writes;if(active!=value){active=value;if(notice)notice(value);}};
        ManualNodeObservations env;
        auto services = env.services();
        auto model=std::make_unique<NodeModel>(node,true,std::move(services));
        const auto cleanup=qScopeGuard([&]{
            QObject::disconnect(model.get(),nullptr,this,nullptr);model->beginShutdown();model.reset();
            for(int turn=0;turn<8;++turn){env.drain();env.publishAll();}
        });
        QSignalSpy pending{model.get(),&NodeModel::networkActionPendingChanged};
        connect(model.get(),&NodeModel::pauseChanged,this,[&](bool paused){
            if(paused&&!reentered){reentered=true;model->setPause(false);}
        });
        for(int turn=0;turn<8&&!reentered;++turn){env.drain();env.publishAll();}
        QVERIFY(reentered);
        QCOMPARE(writes,0);
        QVERIFY(model->networkModel()->pending());
        QVERIFY(model->networkActionPending());
        QCOMPARE(pending.size(),1);
        for(int turn=0;turn<8;++turn){env.drain();env.publishAll();}
        QCOMPARE(writes,1);
        QVERIFY(active);
        QVERIFY(!model->pause());
        QVERIFY(!model->networkModel()->pending());
        QVERIFY(!model->networkActionPending());
        QCOMPARE(pending.size(),2);
    }

    void manualQueuedNetworkIntentYieldsToLaterBackendObservation() {
        struct NetworkNode final : MockNode {
            std::function<void(bool)> set_active;
            void setNetworkActive(bool value) override { set_active(value); }
        } node;
        MempoolState mempool;ConfigureNodeModelDefaults(node);ConfigureMempoolGetters(node,mempool);
        ManualNodeObservations env;
        std::map<NodeModel::Poll,std::function<void()>> deadlines;
        auto services = env.services();
        services.schedule=[&](auto poll,auto,auto turn){deadlines[poll]=std::move(turn);};
        services.cancel=[&](auto poll){deadlines.erase(poll);};
        services.wall_seconds=[]{return int64_t{1700000000};};services.monotonic_milliseconds=[]{return int64_t{0};};
        bool active=true;std::vector<bool> writes;
        interfaces::Node::NotifyNetworkActiveChangedFn network_changed;
        node.handle_notify_network_active_changed_fn=[&](auto callback){network_changed=std::move(callback);return MakeNoopHandler();};
        node.get_network_active_fn=[&]{return active;};
        node.set_active=[&](bool value){writes.push_back(value);if(active!=value){active=value;if(network_changed)network_changed(value);}};
        auto model=std::make_unique<NodeModel>(node,true,std::move(services));
        const auto settle = [&] { return DrainManualNodeTurns(env); };
        const auto cleanup=qScopeGuard([&]{if(model)model->beginShutdown();model.reset();settle();});
        QVERIFY(settle());QVERIFY(model->networkActionReady());QVERIFY(network_changed);QVERIFY(!model->pause());
        model->setPause(true);
        // Hold the queued Core effect and its original Utility outcome separately.
        for (int turn = 0; turn < 8 && !env.hasWork(execution::Lane::Core); ++turn) {
            QVERIFY(env.step(execution::Lane::Utility));
        }
        QVERIFY(env.step(execution::Lane::Core));
        QCOMPARE(writes,std::vector<bool>{false});QVERIFY(model->networkActionPending());
        // Resume is a new GUI intent while the first Domain/GUI outcome is held.
        model->setPause(false);
        // Actual public backend transitions model RPC's later Resume, then Pause.
        // Core emits no notification for an unchanged value, so both are needed.
        node.setNetworkActive(true);node.setNetworkActive(false);
        QVERIFY(settle());QCOMPARE(writes,std::vector<bool>({false,true,false}));
        QVERIFY(!active);QVERIFY(model->pause());QVERIFY(!model->networkActionPending());
        // Only a fresh explicit command may override the newer observation.
        model->setPause(false);QVERIFY(settle());QVERIFY(active);QVERIFY(!model->pause());
        QCOMPARE(writes,std::vector<bool>({false,true,false,true}));
        model->beginShutdown();QVERIFY(settle());QVERIFY(model->isDrained());
        model.reset();QVERIFY(settle());QVERIFY(env.turnsIdle());QVERIFY(env.publicationsIdle());
    }
    void longNativeStartupWarningHasFullDetailsAndDoesNotHoldShutdown_data() {
        QTest::addColumn<bool>("close_first");
        QTest::newRow("publish-full-details") << false; QTest::newRow("close-before-publication") << true;
    }
    void longNativeStartupWarningHasFullDetailsAndDoesNotHoldShutdown() {
        QFETCH(bool, close_first);
        MockNode node; ConfigureNodeModelDefaults(node); MempoolState mempool; ConfigureMempoolGetters(node, mempool);
        interfaces::Node::MessageBoxFn message_box;
        node.handle_message_box_fn = [&](auto callback) { message_box = std::move(callback); return MakeNoopHandler(); };
        ManualNodeObservations env;
        auto loss = std::make_shared<std::atomic_bool>(false);
        auto services = env.services();
        services.diagnostic_loss = loss;
        NodeModel model{node, false, std::move(services)};
        const auto cleanup = qScopeGuard([&] { model.beginShutdown(); env.drain(); env.publishAll(); });
        env.drain(); env.publishAll(); QVERIFY(model.notificationsReady()); QVERIFY(message_box);
        const std::string text = std::string(300'000, 'w') + " complete original warning tail";
        std::thread native([&] { message_box(Untranslated(text), CClientUIInterface::ICON_WARNING); });
        native.join(); env.publishAll();
        QSignalSpy initialized{&model, &NodeModel::nodeInitialized};
        model.initializeResult(false, {}); QVERIFY(model.startupDiagnosticsPending());
        env.drain(); QCOMPARE(initialized.size(), 0); QVERIFY(model.startupError().isEmpty());
        if (close_first) model.beginShutdown();
        env.publishAll();
        if (!close_first) {
            QCOMPARE(initialized.size(), 1); QVERIFY(model.startupError().size() < 16500);
            auto* details = model.startupDiagnostics(); QVERIFY(details->rowCount() > 250);
            QString full;
            for (int row = 0; row < details->rowCount(); ++row) {
                const auto chunk = details->data(details->index(row, 0), Qt::UserRole + 1).toString();
                QVERIFY(chunk.size() <= 1024); full += chunk;
            }
            QVERIFY(full.contains(QString::fromStdString(text))); QVERIFY(full.endsWith("Node initialization failed."));
        } else { QCOMPARE(initialized.size(), 0); QVERIFY(model.startupError().isEmpty()); }
        model.beginShutdown();
        const auto settle = [&] { env.drain(); env.publishAll(); return model.isDrained(); };
        QTRY_VERIFY_WITH_TIMEOUT(settle(), ASYNC_TIMEOUT_MS); QVERIFY(!loss->load());
    }

    void fatalLifecycleDiagnosticUsesOriginalScopeAfterQuietClose_data() {
        QTest::addColumn<bool>("workers_stopped");
        QTest::newRow("interrupt-after-submitted-close") << false;
        QTest::newRow("shutdown-after-worker-drain") << true;
    }
    void fatalLifecycleDiagnosticUsesOriginalScopeAfterQuietClose() {
        QFETCH(bool, workers_stopped);
        ManualNodeObservations env;
        MockNode node; ConfigureNodeModelDefaults(node);
        MempoolState mempool; ConfigureMempoolGetters(node, mempool);
        auto loss = std::make_shared<std::atomic_bool>(false);
        {
            auto services = env.services();
            services.diagnostic_loss = loss;
            NodeModel model{node, false, std::move(services)};
            env.drain(); env.publishAll();
            model.close();
            if (workers_stopped) { model.beginShutdown(); env.drain(); env.publishAll(); }
            QVERIFY(!model.postNative(execution::Lane::Core)(execution::MakeTask([] {}, [] {}, {})));
            const auto core_submissions = env.coreSubmissions();
            const QString diagnostic = QString(300'000, QLatin1Char('x')) + QStringLiteral(" original lifecycle fatal tail");
            model.handleRunawayException(diagnostic);
            QVERIFY(model.fatalException()); QVERIFY(model.startupDiagnosticsPending());
            QVERIFY(model.startupError().isEmpty());
            QVERIFY(env.hasWork(execution::Lane::Utility));
            while (env.step(execution::Lane::Utility)) {}
            QCOMPARE(env.coreSubmissions(), core_submissions);
            QVERIFY(model.startupError().isEmpty()); // Native completion alone does not publish.
            env.publishAll();
            QVERIFY(!model.startupDiagnosticsPending());
            QVERIFY(model.startupError().contains(QStringLiteral("can no longer continue safely")));
            QVERIFY(model.startupError().size() < 16500);
            QString complete;
            auto* details = model.startupDiagnostics();
            for (int row = 0; row < details->rowCount(); ++row) {
                const auto chunk = details->data(details->index(row, 0), Qt::UserRole + 1).toString();
                QVERIFY(chunk.size() <= 1024); complete += chunk;
            }
            QVERIFY(complete.endsWith(diagnostic)); QVERIFY(!loss->load());
            QVERIFY(!model.postNative(execution::Lane::Core)(execution::MakeTask([] {}, [] {}, {})));
            model.close(); model.beginShutdown();
            QVERIFY(DrainManualNodeTurns(env));
            QVERIFY(model.isDrained()); // Quiet publication must not require facade destruction.
        }
        QVERIFY(DrainManualNodeTurns(env));
    }

    void initTestCase() { SelectParams(ChainType::REGTEST); }
    void localizedInformationDatesUseTheCapturedSystemConvention_data();
    void localizedInformationDatesUseTheCapturedSystemConvention();
    void acceptedNetworkActionOutlivesFacade();
    void originalAdmissionIsCountedBeforeQuietClose_data();
    void originalAdmissionIsCountedBeforeQuietClose();
    void manualOriginalActionScopeOwnsLateStagesAndNativeCleanup();
    void sharedRetirementUsesControlledCleanupAndActualDrain_data() {
        QTest::addColumn<bool>("fence_first");
        QTest::addColumn<bool>("shared_executor");
        QTest::newRow("ordinary-submitted-open") << false << false;
        QTest::newRow("ordinary-submitted-closed") << true << false;
        QTest::newRow("shared-submitted-open") << false << true;
        QTest::newRow("shared-submitted-closed") << true << true;
    }
    void sharedRetirementUsesControlledCleanupAndActualDrain();
    void manualInitializationWaitsForNativeSubscriptionAndPublication();
    void manualPollingCachesFactsAndRejectsStaleDeadlines();
    void sharedObservationsUseCoreAndPriorityAndDrainBeforeNodeShutdown();
    void guiQuestionFailsClosedWithoutNestedEventLoop();
    void shutdownPollBypassesBlockedObservation();
    void latestNotificationsSurviveStaleInitializationAndReorg();
    void caughtUpInitializationClearsEarlierInitialDownloadNotification();
    void networkCommandsCoalesceDesiredStateWithoutBlockingGui();
    void newerNetworkNotificationSupersedesBlockedCommandResult();
    void statusNotificationBurstUsesOneFollowupSnapshot();
    void overlappingStatusNotificationsPublishEachCompletedSnapshot();
    void networkNotificationSupersedesBlockedStatusSnapshot();
    void refreshMempoolInfoUpdatesProperties();
    void activatingMempoolPollingEmitsSignalsAndRefreshesImmediately();
    void peerCountsInitializeAndRefreshFromDirectionalNodeCounts();
    void disconnectPeerReturnsNodeResult();
    void banPeerRejectsInvalidInputs();
    void banPeerReturnsFalseWithoutDisconnectWhenBackendFails();
    void banPeerDisconnectsAddressAfterSuccessfulBan();
    void networkActionReadyFollowsInitializationAndShutdown_data();
    void networkActionReadyFollowsInitializationAndShutdown();
    void peerAdmissionDoesNotWaitForNetworkObservation_data();
    void peerAdmissionDoesNotWaitForNetworkObservation();
    void requestShutdownEmitsOnlyOnce();
    void initializationFailureRequestsShutdownWhenCoreWasInterrupted();
    void initializationFailureWithoutCoreInterruptOnlySetsErrorState();
    void initializationSuccessDuringCoreShutdownSkipsReadyState();
    void shutdownUnsubscribesCoreSignalsBeforeDrained();
    void nodeNotificationHandlersUpdateModelThroughQueuedSignals();
    void bannedListNotificationFromGuiThreadIsNotDeliveredReentrantly();
    void notificationsDuringAndAfterShutdownAreIgnored();
    void blockTipUpdatesQueuedAcrossThreadsRetainPayloadValues();
    void initialSyncCompletionIgnoresVerificationEstimateForGenesis();
    void initialSyncCompletionWaitsForStartupCatchUpAndLatches();
    void syncCompletionResetsForLargeHeaderGap_data();
    void syncCompletionResetsForLargeHeaderGap();
    void coalescedTipsDoNotLatchCompletionFromIntermediateHeights();
    void blockSyncActiveFollowsInitializationAndBlockTipState();
    void alertNotificationsRefreshWarningList();
    void headerTipNotificationsExposeHeaderSyncProgress();
    void startupWarningsAreShownOnceAndDoNotBecomeCurrentWarnings();
    void manualStartupCallbacksKeepTheirPresentationPhase_data();
    void manualStartupCallbacksKeepTheirPresentationPhase();
    void manualStartupSummaryReportsAFullRuntimeQueue();
    void manualStartupSummaryPreservesDiagnosticsWhenDialogTextBudgetIsFull();
    void manualStartupSummaryRejectsReentrantShutdown();
    void manualInitializationStopsAfterPublicReentrancy_data();
    void manualInitializationStopsAfterPublicReentrancy();
    void runtimeMessageHandlerOpensAfterInitialization();
    void runtimeQuestionHandlerBlocksForAnswerAndReturnsResult();
    void closingCancelsAWaitingRuntimeQuestionBeforeWorkerDrain();
    void runtimeQuestionUnsupportedAnswerCannotConsumeThePendingQuestion();
    void runtimeStartupQuestionFailureLetsInitializeResultRequestShutdown();
    void runtimeStartupErrorDialogLetsInitializeResultRequestShutdown();
    void runtimeDialogDefaultsToOkWhenNoButtonsAreSpecified();
    void runtimeDialogExposesFullCoreButtonMask();
    void runtimeBlockingDialogsAreQueued();
    void runtimeNonBlockingDialogsAreQueued();
    void initializeFailureShowsStartupWarningsWithoutMakingThemCurrentWarnings();
    void initializeFailureUsesNodeErrorMessages();
    void runawayExceptionSetsFatalStartupError();
    void fatalExceptionExitsWithoutDraining_data();
    void fatalExceptionExitsWithoutDraining();
    void nodeInformationRowsAvoidChainmanBeforeInitialization();
    void nodeInformationRowsExposeDiagnostics();
    void informationRowsReopenFromCacheWhileStatusReadIsHeld();
    void manualRewindUsesCurrentHeaderReadback_data();
    void manualRewindUsesCurrentHeaderReadback();
    void manualBlockRowsRejectOlderStatusRead_data();
    void manualBlockRowsRejectOlderStatusRead();
    void manualReadbackDoesNotReplayPendingTip();
    void manualReadbackPreservesPresync();
    void manualHeaderProjectionCannotOverwriteReentrantBlockTip();
    void initEmitsRequestedInitialize();
    void initGuardBlocksSecondEmission();
    void shutdownPollingOnlyRequestsLifecycleControl();
};

void NodeModelTests::acceptedNetworkActionOutlivesFacade()
{
    struct NetworkNode final : MockNode {
        int effects{0}; bool active{true};
        void setNetworkActive(bool value) override { ++effects; active = value; }
    } node;
    ConfigureNodeModelDefaults(node);
    node.get_network_active_fn = [&] { return node.active; };
    ManualNodeObservations env;
    auto services = env.services();
    auto model = std::make_unique<NodeModel>(node, false, std::move(services));
    env.drain(); env.publishAll(); QVERIFY(model->notificationsReady());
    model->initializeResult(true, {}, false, false);
    for(int turn=0;turn<8&&!model->networkActionReady();++turn){env.drain();env.publishAll();}
    QVERIFY(model->networkActionReady());
    model->setPause(true); QVERIFY(model->networkActionPending()); QCOMPARE(node.effects, 0);
    model.reset(); // Accepted effect and its outcome survive GUI observer loss.
    QCOMPARE(node.effects, 0);
    env.drain(); env.publishAll();
    QCOMPARE(node.effects, 1); QVERIFY(!node.active); QVERIFY(DrainManualNodeTurns(env));
}

void NodeModelTests::originalAdmissionIsCountedBeforeQuietClose_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("scope-origin") << 0;
    QTest::newRow("ordinary-packet") << 1;
    QTest::newRow("owned-cleanup-reservation") << 2;
}
void NodeModelTests::originalAdmissionIsCountedBeforeQuietClose()
{
    QFETCH(int, kind);
    using execution::Lane;
    const Lane lane = kind == 0 ? Lane::Core : kind == 1 ? Lane::Priority : Lane::Utility;
    QSemaphore entered, release;
    std::atomic_bool pause{false};
    ManualNodeObservations env{[&](Lane dispatched) {
        if (dispatched != lane || !pause.exchange(false)) return;
        entered.release();
        release.acquire();
    }};
    MockNode node;
    ConfigureNodeModelDefaults(node);
    NodeModel model{node, false, env.services()};
    QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(model.notificationsReady());

    const auto original = model.postNative(lane);
    const auto retire = model.valueRetirement();
    int effects{0}, outcomes{0};
    bool admitted{false};
    std::thread producer;
    const auto cleanup = qScopeGuard([&] {
        release.release();
        if (producer.joinable()) producer.join();
        model.beginShutdown();
        QVERIFY(DrainManualNodeTurns(env));
        QVERIFY(model.isDrained());
    });

    pause = true;
    producer = std::thread([&] {
        if (kind == 2) admitted = retire([&] { ++effects; });
        else admitted = bool(original(execution::MakeTask([&] { ++effects; }, [&] { ++outcomes; }, {})));
    });
    QVERIFY(entered.tryAcquire(1, ASYNC_TIMEOUT_MS));
    model.close();
    QVERIFY(!model.postNative(Lane::Core)(execution::MakeTask([] {}, [] {}, {})));
    model.beginShutdown();
    QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(effects, 0);
    QCOMPARE(outcomes, 0);
    // The worker has not received the callback yet, but close already
    // accounts for the original Core, Priority or cleanup submission.
    QVERIFY(!model.isDrained());

    release.release();
    producer.join();
    QVERIFY(admitted);
    QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(effects, 1);
    QCOMPARE(outcomes, kind == 2 ? 0 : 1);
    QVERIFY(model.isDrained());
}

void NodeModelTests::sharedRetirementUsesControlledCleanupAndActualDrain()
{
    QFETCH(bool, fence_first);
    QFETCH(bool, shared_executor);
    using execution::Lane;
    MockNode node;
    ConfigureNodeModelDefaults(node);
    ManualNodeObservations env;
    NodeModel model{node, false, shared_executor ? env.services() : NodeModel::Services{}};
    QSemaphore started, release;
    std::atomic_int retired{0};
    std::atomic_bool off_gui{false};
    const auto* gui = QThread::currentThread();
    const auto cleanup = qScopeGuard([&] {
        release.release();
        if (shared_executor) {
            model.beginShutdown();
            QVERIFY(DrainManualNodeTurns(env));
            QVERIFY(model.isDrained());
        } else {
            DrainNodeModel(model);
        }
    });
    if (shared_executor) QVERIFY(DrainManualNodeTurns(env));
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);

    if (fence_first) {
        model.close();
        QVERIFY(!model.postNative(Lane::Core)(execution::MakeTask([] {}, [] {}, {})));
    }
    const auto retire = model.valueRetirement();
    QVERIFY(retire([&] {
        off_gui = QThread::currentThread() != gui;
        started.release();
        if (!shared_executor) release.acquire();
        ++retired;
    }));
    if (shared_executor) {
        QVERIFY(env.hasWork(Lane::Utility));
        QVERIFY(!env.hasWork(Lane::Core));
        QVERIFY(!env.hasWork(Lane::Priority));
    } else {
        QVERIFY(started.tryAcquire(1, ASYNC_TIMEOUT_MS));
    }
    QCOMPARE(retired.load(), 0);

    model.beginShutdown();
    QVERIFY(!model.isDrained());
    QCOMPARE(retired.load(), 0);
    QVERIFY(!retire([&] { retired += 100; }));
    if (shared_executor) {
        // The GUI and Core can process their own turns while cleanup is held.
        while (env.step(Lane::Core) || env.step(Lane::Priority)) {}
        env.publishAll();
        QCOMPARE(retired.load(), 0);
        QVERIFY(!model.isDrained());
        QVERIFY(DrainManualNodeTurns(env));
        QVERIFY(started.tryAcquire());
    } else {
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(model.isDrained(), ASYNC_TIMEOUT_MS);
        QVERIFY(off_gui.load());
    }
    QCOMPARE(retired.load(), 1);
    QVERIFY(model.isDrained());
}

void NodeModelTests::manualOriginalActionScopeOwnsLateStagesAndNativeCleanup()
{
    using execution::Lane;
    MockNode node;
    ConfigureNodeModelDefaults(node);
    ManualNodeObservations env;
    NodeModel model{node, false, env.services()};
    execution::OwnedWork work{env.executor(), env.post()};
    const auto values = std::make_shared<execution::ValueCleanupQueue>(
        std::make_shared<execution::SerialQueue>(env.executor().makeQueue(Lane::Utility)));
    struct Value {
        int& retired;
        explicit Value(int& count) : retired(count) {}
        ~Value() { ++retired; }
    };
    int effects{0}, outcomes{0}, retired{0}, published{0};
    bool work_drained{false}, values_drained{false};
    std::function<void(int)> finish;
    std::function<void(std::exception_ptr)> fail;
    std::shared_ptr<Value> value;
    const auto cleanup = qScopeGuard([&] {
        model.beginShutdown();
        work.close();
        if (fail) fail(std::make_exception_ptr(std::runtime_error{"Test ended before the action completed"}));
        value.reset();
        QVERIFY(DrainManualNodeTurns(env));
    });
    QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(model.notificationsReady());

    // The original feature owns its continuation before shutdown starts.
    QVERIFY(work.submitAsync<int>([&](auto done, auto failed) {
        finish = std::move(done);
        fail = std::move(failed);
        return true;
    }, [&](int) { ++published; }, [](std::exception_ptr) {}));
    const auto submit = work.queuePost();
    model.close();
    QVERIFY(!model.postNative(Lane::Core)(execution::MakeTask([] {}, [] {}, {})));
    work.close([&] { work_drained = true; });
    QVERIFY(!work.submit([] { return 0; }, [](int) {}, [](std::exception_ptr) {}));
    QVERIFY(submit(Lane::Core, execution::MakeTask([&] { ++effects; return 7; }, [&](int result) {
        QCOMPARE(result, 7);
        ++outcomes;
        value = execution::RetainValues<Value>([&](auto release) { return values->hold(std::move(release)); },
            [&] { return std::make_unique<Value>(retired); });
    }, fail)));
    QCOMPARE(effects, 0);
    model.beginShutdown();
    QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(effects, 1);
    QCOMPARE(outcomes, 1);
    QVERIFY(value);
    values->close([&] { values_drained = true; });
    QVERIFY(model.isDrained());
    QVERIFY(!work_drained);
    QVERIFY(!values_drained);

    QVERIFY(submit(Lane::Domain, execution::MakeTask([&] { effects += 10; }, [&] { ++outcomes; }, fail)));
    QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(effects, 11);
    QCOMPARE(outcomes, 2);
    QCOMPARE(retired, 0);
    QVERIFY(!work_drained);
    QVERIFY(!values_drained);

    value.reset();
    QCOMPARE(retired, 0);
    QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(retired, 1);
    QVERIFY(values_drained);
    QVERIFY(!work_drained);

    finish(7);
    QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(work_drained);
    QCOMPARE(published, 0);
}

void NodeModelTests::localizedInformationDatesUseTheCapturedSystemConvention_data()
{
    QTest::addColumn<QString>("name"); QTest::newRow("de-DE-24-hour") << QStringLiteral("de_DE"); QTest::newRow("en-US-12-hour") << QStringLiteral("en_US");
}
void NodeModelTests::localizedInformationDatesUseTheCapturedSystemConvention()
{
    QFETCH(QString, name); const QLocale previous; const QLocale locale{name}; QLocale::setDefault(locale);
    const auto restore_locale = qScopeGuard([&] { QLocale::setDefault(previous); });
    MockNode node; ConfigureNodeModelDefaults(node); MempoolState mempool; ConfigureMempoolGetters(node, mempool);
    const auto timestamp = QDateTime(QDate{2026, 11, 15}, QTime{18, 30}).toSecsSinceEpoch();
    node.get_header_tip_fn = [timestamp](int& height, int64_t& time) { height = 1; time = timestamp; return true; };
    node.get_num_blocks_fn = [] { return 1; }; node.get_last_block_time_fn = [timestamp] { return timestamp; };
    ManualNodeObservations env; auto services = env.services();
    NodeModel model{node, false, std::move(services)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    env.drain(); env.publishAll(); model.initializeResult(true, {}, false, false);
    // Diagnostics publication schedules the subsequent native status read.
    for (int turn{0}; turn < 16 && (!env.turnsIdle() || !env.publicationsIdle()); ++turn) { env.drain(); env.publishAll(); }
    QVERIFY(env.turnsIdle()); QVERIFY(env.publicationsIdle()); QVERIFY(node.calls.getHeaderTip.load() > 0);
    const auto expected = locale.toString(QDateTime::fromSecsSinceEpoch(timestamp), QLocale::ShortFormat);
    QCOMPARE(InformationValue(model, "Header time"), expected); QCOMPARE(InformationValue(model, "Last block time"), expected);
    const auto startup = InformationValue(model, "Startup time"); QVERIFY(!startup.isEmpty()); QVERIFY(locale.toDateTime(startup, QLocale::ShortFormat).isValid());
}

void NodeModelTests::manualInitializationWaitsForNativeSubscriptionAndPublication()
{
    MockNode node; ConfigureNodeModelDefaults(node);
    int disconnected{0};
    node.handle_question_fn = [&](auto) {
        return interfaces::MakeCleanupHandler([&] { ++disconnected; });
    };
    ManualNodeObservations env;
    auto services = env.services();
    NodeModel model{node, false, std::move(services)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    QSignalSpy initialize(&model, &NodeModel::requestedInitialize);
    model.startNodeInitializionThread(); model.startNodeInitializionThread();
    QCOMPARE(initialize.size(), 0); QVERIFY(!model.notificationsReady());
    QCOMPARE(node.calls.handleQuestion.load(), 0);
    QVERIFY(env.step(execution::Lane::Core)); // Core installs all public notification handlers.
    QCOMPARE(node.calls.handleQuestion.load(), 1); QCOMPARE(initialize.size(), 0);
    QCOMPARE(disconnected, 0);
    QVERIFY(env.step(execution::Lane::Utility)); // Utility records subscription outcome.
    QCOMPARE(initialize.size(), 0);
    env.publishAll();
    QVERIFY(model.notificationsReady()); QCOMPARE(initialize.size(), 1);
    QCOMPARE(disconnected, 0); // The installed handlers stay live while requests are open.
    model.startNodeInitializionThread(); QCOMPARE(initialize.size(), 1);
    model.beginShutdown(); QCOMPARE(disconnected, 0);
    env.drain(); QCOMPARE(disconnected, 1); env.publishAll();
    QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(model.isDrained());
}

void NodeModelTests::manualPollingCachesFactsAndRejectsStaleDeadlines()
{
    MockNode node; ConfigureNodeModelDefaults(node);
    MempoolState mempool; ConfigureMempoolGetters(node, mempool); mempool.count = 7;
    node.ExpectNoCalls(node.calls.getTotalBytesRecv);
    node.ExpectNoCalls(node.calls.getTotalBytesSent);
    ManualNodeObservations env;
    std::map<NodeModel::Poll, std::function<void()>> deadlines;
    std::map<NodeModel::Poll, std::chrono::milliseconds> delays;
    auto services = env.services();
    services.schedule = [&](auto poll, auto delay, auto callback) { delays[poll] = delay; deadlines[poll] = std::move(callback); };
    services.cancel = [&](auto poll) { deadlines.erase(poll); };
    services.wall_seconds = [] { return int64_t{1'800'000'000}; };
    int64_t elapsed{0}; services.monotonic_milliseconds = [&] { return elapsed; };
    NodeModel model{node, false, std::move(services)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    env.drain(); env.publishAll(); QVERIFY(model.notificationsReady());
    model.initializeResult(true, {}, false, false);
    env.drain(); env.publishAll();
    QCOMPARE(node.calls.getMempoolSize.load(), 0);
    model.setMempoolInfoPollingActive(true);
    QCOMPARE(delays.at(NodeModel::Poll::Mempool).count(), int64_t{3000});
    env.drain();
    QCOMPARE(model.mempoolTransactionCount(), 0);
    env.publishAll();
    QCOMPARE(model.mempoolTransactionCount(), 7);
    QCOMPARE(node.calls.getTotalBytesRecv.load(), 0); QCOMPARE(node.calls.getTotalBytesSent.load(), 0);
    const int calls = node.calls.getMempoolSize.load();
    mempool.count = 19;
    auto deadline = deadlines.at(NodeModel::Poll::Mempool); deadline();
    QCOMPARE(node.calls.getMempoolSize.load(), calls);
    env.drain(); QCOMPARE(model.mempoolTransactionCount(), 7);
    env.publishAll(); QCOMPARE(model.mempoolTransactionCount(), 19);
    auto stale = deadlines.at(NodeModel::Poll::Mempool);
    model.setMempoolInfoPollingActive(false); stale(); env.drain(); env.publishAll();
    QCOMPARE(model.mempoolTransactionCount(), 19); QVERIFY(!deadlines.contains(NodeModel::Poll::Mempool));
    node.shutdown_requested_fn = [] { return true; };
    QSignalSpy shutdown(&model, &NodeModel::requestedShutdown);
    model.startShutdownPolling();
    QCOMPARE(delays.at(NodeModel::Poll::Shutdown).count(), int64_t{200});
    auto poll = deadlines.at(NodeModel::Poll::Shutdown); poll();
    QCOMPARE(shutdown.size(), 0); env.drain(); QCOMPARE(shutdown.size(), 0);
    env.publishAll(); QCOMPARE(shutdown.size(), 1);
}

void NodeModelTests::sharedObservationsUseCoreAndPriorityAndDrainBeforeNodeShutdown()
{
    using execution::Lane;
    MockNode node;
    ConfigureNodeModelDefaults(node);
    std::optional<Lane> active_lane, registration_lane, cleanup_lane;
    int disconnected{0};
    node.handle_question_fn = [&](auto) {
        registration_lane = active_lane;
        return interfaces::MakeCleanupHandler([&] {
            cleanup_lane = active_lane;
            ++disconnected;
        });
    };
    MempoolState mempool;
    ConfigureMempoolGetters(node, mempool);
    mempool.count = 7;
    node.shutdown_requested_fn = [] { return false; };
    ManualExecutionEnvironment env;
    std::map<Lane, std::deque<std::function<void()>>> turns;
    execution::Executor executor{[&](Lane lane, execution::UtilityWorker, auto turn) {
        turns[lane].push_back(std::move(turn));
        return true;
    }};
    const auto step = [&](Lane lane) {
        auto& pending = turns[lane];
        if (pending.empty()) return false;
        auto turn = std::move(pending.front());
        pending.pop_front();
        active_lane = lane;
        const auto reset_lane = qScopeGuard([&] { active_lane.reset(); });
        turn();
        return true;
    };
    const auto post = [&](auto turn) { env.post(std::move(turn)); };
    const auto retirement = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
    PresentationServices diagnostics{{post, [retirement](auto work) {
        return retirement->post(std::move(work));
    }, {}, std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility))}};
    std::function<void()> shutdown_poll;
    NodeModel::Services services;
    services.post = post;
    services.diagnostics = &diagnostics;
    services.network_retirement = diagnostics.valueRetirement();
    services.schedule = [&](auto poll, auto, auto turn) {
        if (poll == NodeModel::Poll::Shutdown) shutdown_poll = std::move(turn);
    };
    services.cancel = [&](auto poll) {
        if (poll == NodeModel::Poll::Shutdown) shutdown_poll = {};
    };
    services.observation_executor = &executor;
    NodeModel model{node, true, std::move(services)};
    const auto cleanup = qScopeGuard([&] {
        model.beginShutdown();
        while (step(Lane::Core) || step(Lane::Priority) || step(Lane::Utility)) {}
        env.publishAll();
    });
    const auto post_core = model.postNative(Lane::Core);
    QVERIFY(post_core);
    env.publishAll();
    QVERIFY(!model.notificationsReady());
    QCOMPARE(node.calls.handleQuestion.load(), 0);
    QCOMPARE(node.calls.getMempoolSize.load(), 0);
    QCOMPARE(node.calls.getNodeCount.load(), 0);

    model.startShutdownPolling();
    QVERIFY(shutdown_poll);
    auto run_poll = shutdown_poll;
    run_poll();
    QVERIFY(step(Lane::Priority));
    QCOMPARE(node.calls.shutdownRequested.load(), 1);
    QCOMPARE(node.calls.getMempoolSize.load(), 0);
    QCOMPARE(node.calls.handleQuestion.load(), 0);
    QVERIFY(step(Lane::Core));
    QCOMPARE(node.calls.handleQuestion.load(), 1);
    QVERIFY(registration_lane == Lane::Core);
    QVERIFY(!model.notificationsReady());
    while (step(Lane::Core)) {}
    QCOMPARE(node.calls.getMempoolSize.load(), 1);
    QCOMPARE(node.calls.getNodeCount.load(), 2);
    QCOMPARE(model.mempoolTransactionCount(), 0);
    QVERIFY(step(Lane::Utility));
    while (step(Lane::Utility)) {}
    QCOMPARE(model.mempoolTransactionCount(), 0);
    QVERIFY(!model.notificationsReady());
    env.publishAll();
    QVERIFY(model.notificationsReady());
    QCOMPARE(model.mempoolTransactionCount(), 7);

    mempool.count = 19;
    model.refreshMempoolInfo();
    QVERIFY(step(Lane::Core));
    model.refreshMempoolInfo(); // Coalesce a follow-up while Utility holds the result.
    QSignalSpy drained{&model, &NodeModel::workDrained};
    model.close();
    QVERIFY(!post_core(execution::MakeTask([] {}, [] {}, {})));
    model.beginShutdown();
    env.publishAll();
    QVERIFY(!model.isDrained());
    QCOMPARE(disconnected, 0);
    QVERIFY(step(Lane::Utility));
    while (step(Lane::Utility)) {}
    QVERIFY(!step(Lane::Core));
    QCOMPARE(disconnected, 1);
    QVERIFY(cleanup_lane == Lane::Utility);
    QCOMPARE(drained.size(), 0);
    env.publishAll();
    QCOMPARE(drained.size(), 1);
    QCOMPARE(node.calls.getMempoolSize.load(), 2);
    QCOMPARE(model.mempoolTransactionCount(), 7);
}

void NodeModelTests::guiQuestionFailsClosedWithoutNestedEventLoop()
{
    MockNode node; ConfigureNodeModelDefaults(node);
    interfaces::Node::QuestionFn question;
    node.handle_question_fn = [&](auto callback) { question = std::move(callback); return MakeNoopHandler(); };
    NodeModel model{node, false}; const auto cleanup = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    QVERIFY(question);
    QVERIFY(!question(Untranslated("GUI cannot block"), {}, CClientUIInterface::BTN_OK));
    QVERIFY(!model.runtimeDialogVisible());
}

void NodeModelTests::shutdownPollBypassesBlockedObservation()
{
    MockNode node; ConfigureNodeModelDefaults(node);
    QSemaphore entered, release;
    std::atomic_bool blocked{true};
    node.get_mempool_size_fn = [&] {
        if (blocked.load()) { entered.release(); release.acquire(); }
        return size_t{0};
    };
    node.shutdown_requested_fn = [] { return true; };
    NodeModel model{node};
    const auto cleanup = qScopeGuard([&] {
        // Release the original blocked read and make any already admitted
        // follow-up nonblocking before draining the original Native owner.
        blocked = false; release.release(); DrainNodeModel(model);
    });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, ASYNC_TIMEOUT_MS);
    QSignalSpy shutdown(&model, &NodeModel::requestedShutdown);
    model.startShutdownPolling();
    bool heartbeat{false}; QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY_WITH_TIMEOUT(heartbeat, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(shutdown.size(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(node.calls.getMempoolMaxUsage.load(), 0); // Core observation is still blocked.
}

void NodeModelTests::refreshMempoolInfoUpdatesProperties()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    mempool.count = 12;
    mempool.usage_bytes = 12'500'000;
    mempool.max_bytes = 300'000'000;

    QSignalSpy mempool_info_spy{&model, &NodeModel::mempoolInfoChanged};
    model.refreshMempoolInfo();

    QTRY_COMPARE_WITH_TIMEOUT(mempool_info_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.mempoolTransactionCount(), 12);
    QCOMPARE(model.mempoolUsageMB(), 12.5);
    QCOMPARE(model.mempoolMaxUsageMB(), 300.0);
}

void NodeModelTests::activatingMempoolPollingEmitsSignalsAndRefreshesImmediately()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    mempool.count = 3;
    mempool.usage_bytes = 4'000'000;
    mempool.max_bytes = 500'000'000;

    QSignalSpy polling_active_spy{&model, &NodeModel::mempoolInfoPollingActiveChanged};
    QSignalSpy mempool_info_spy{&model, &NodeModel::mempoolInfoChanged};
    model.setMempoolInfoPollingActive(true);

    QCOMPARE(model.mempoolInfoPollingActive(), true);
    QCOMPARE(polling_active_spy.count(), 1);
    QCOMPARE(polling_active_spy.takeFirst().at(0).toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(mempool_info_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.mempoolTransactionCount(), 3);
    QCOMPARE(model.mempoolUsageMB(), 4.0);
    QCOMPARE(model.mempoolMaxUsageMB(), 500.0);

    model.setMempoolInfoPollingActive(false);
    QCOMPARE(model.mempoolInfoPollingActive(), false);
    QCOMPARE(polling_active_spy.count(), 1);
    QCOMPARE(polling_active_spy.takeFirst().at(0).toBool(), false);
}

void NodeModelTests::peerCountsInitializeAndRefreshFromDirectionalNodeCounts()
{
    MockNode node;
    MempoolState mempool;
    PeerCountState peers;
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed_fn;

    peers.total = 3;
    peers.inbound = 1;
    peers.outbound = 2;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    ConfigurePeerCountGetters(node, peers);
    node.handle_notify_num_connections_changed_fn = [&](interfaces::Node::NotifyNumConnectionsChangedFn fn) {
        connections_changed_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(connections_changed_fn);
    QTRY_COMPARE_WITH_TIMEOUT(model.numPeers(), 3, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.numInboundPeers(), 1);
    QCOMPARE(model.numOutboundPeers(), 2);

    QSignalSpy peers_spy{&model, &NodeModel::numPeersChanged};
    QSignalSpy inbound_spy{&model, &NodeModel::numInboundPeersChanged};
    QSignalSpy outbound_spy{&model, &NodeModel::numOutboundPeersChanged};

    peers.total = 7;
    peers.inbound = 5;
    peers.outbound = 2;
    connections_changed_fn(7);

    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(inbound_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(outbound_spy.count(), 0);
    QCOMPARE(model.numPeers(), 7);
    QCOMPARE(model.numInboundPeers(), 5);
    QCOMPARE(model.numOutboundPeers(), 2);

    peers.total = 8;
    peers.inbound = 5;
    peers.outbound = 3;
    connections_changed_fn(8);

    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 2, ASYNC_TIMEOUT_MS);
    QCOMPARE(inbound_spy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(outbound_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.numPeers(), 8);
    QCOMPARE(model.numInboundPeers(), 5);
    QCOMPARE(model.numOutboundPeers(), 3);
}

void NodeModelTests::disconnectPeerReturnsNodeResult()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::vector<NodeId> disconnected_ids;
    node.disconnect_by_id_fn = [&](NodeId id) {
        disconnected_ids.push_back(id);
        return id == 7;
    };
    node.ExpectExactly(node.calls.disconnectById, 2);
    QSignalSpy outcomes{model.peerCommands(), &PeerCommandModel::finished};
    const auto first = model.peerCommands()->requestDisconnectPeer(7); QVERIFY(first);
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(outcomes.at(0).at(0).toULongLong(), first);
    QCOMPARE(outcomes.at(0).at(1).toInt(), static_cast<int>(PeerCommandOperation::Outcome::Succeeded));
    const auto second = model.peerCommands()->requestDisconnectPeer(8); QVERIFY(second); QVERIFY(second != first);
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 2, ASYNC_TIMEOUT_MS);
    QCOMPARE(outcomes.at(1).at(0).toULongLong(), second);
    QCOMPARE(outcomes.at(1).at(1).toInt(), static_cast<int>(PeerCommandOperation::Outcome::Rejected));
    QCOMPARE(disconnected_ids, std::vector<NodeId>({7, 8}));
}

void NodeModelTests::banPeerRejectsInvalidInputs()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.ExpectNoCalls(node.calls.ban);
    node.ExpectNoCalls(node.calls.disconnectByAddress);

    QSignalSpy outcomes{model.peerCommands(), &PeerCommandModel::finished};
    const auto ticket = model.peerCommands()->requestBanPeer(QStringLiteral("not an address"), 3600); QVERIFY(ticket);
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(outcomes.at(0).at(0).toULongLong(), ticket);
    QCOMPARE(outcomes.at(0).at(1).toInt(), static_cast<int>(PeerCommandOperation::Outcome::Rejected));
    QVERIFY(!model.peerCommands()->requestBanPeer(QStringLiteral("127.0.0.1"), 0));
    QVERIFY(!model.peerCommands()->requestBanPeer(QStringLiteral("127.0.0.1"), -1));
    QCOMPARE(outcomes.size(), 1);
}

void NodeModelTests::banPeerReturnsFalseWithoutDisconnectWhenBackendFails()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    bool received_expected_ban{false};
    node.ban_fn = [&](const CNetAddr& value, int64_t duration) {
        received_expected_ban = value.ToStringAddr() == "127.0.0.1" && duration == 3600;
        return false;
    };
    node.ExpectExactly(node.calls.ban, 1);
    node.ExpectNoCalls(node.calls.disconnectByAddress);

    QSignalSpy outcomes{model.peerCommands(), &PeerCommandModel::finished};
    const auto ticket = model.peerCommands()->requestBanPeer(QStringLiteral("127.0.0.1"), 3600); QVERIFY(ticket);
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(outcomes.at(0).at(0).toULongLong(), ticket);
    QCOMPARE(outcomes.at(0).at(1).toInt(), static_cast<int>(PeerCommandOperation::Outcome::Rejected));
    QVERIFY(received_expected_ban);
}

void NodeModelTests::banPeerDisconnectsAddressAfterSuccessfulBan()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    bool received_expected_ban{false};
    bool received_expected_disconnect{false};
    node.ban_fn = [&](const CNetAddr& value, int64_t duration) {
        received_expected_ban = value.ToStringAddr() == "127.0.0.1" && duration == 3600;
        return true;
    };
    node.disconnect_by_address_fn = [&](const CNetAddr& value) {
        received_expected_disconnect = value.ToStringAddr() == "127.0.0.1";
        return true;
    };
    node.ExpectExactly(node.calls.ban, 1);
    node.ExpectExactly(node.calls.disconnectByAddress, 1);

    QSignalSpy outcomes{model.peerCommands(), &PeerCommandModel::finished};
    const auto ticket = model.peerCommands()->requestBanPeer(QStringLiteral("127.0.0.1"), 3600); QVERIFY(ticket);
    QTRY_COMPARE_WITH_TIMEOUT(outcomes.size(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(outcomes.at(0).at(0).toULongLong(), ticket);
    QCOMPARE(outcomes.at(0).at(1).toInt(), static_cast<int>(PeerCommandOperation::Outcome::Succeeded));
    QVERIFY(received_expected_ban);
    QVERIFY(received_expected_disconnect);
}

void NodeModelTests::networkActionReadyFollowsInitializationAndShutdown_data()
{
    QTest::addColumn<bool>("initialize_success");
    QTest::addColumn<bool>("direct_drain");
    QTest::newRow("successful-init-shutdown-request") << true << false;
    QTest::newRow("successful-init-direct-drain") << true << true;
    QTest::newRow("failed-init-shutdown-request") << false << false;
    QTest::newRow("failed-init-direct-drain") << false << true;
}

void NodeModelTests::networkActionReadyFollowsInitializationAndShutdown()
{
    QFETCH(bool, initialize_success);
    QFETCH(bool, direct_drain);
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    NodeModel model{node, false};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    QSignalSpy ready{&model, &NodeModel::networkActionReadyChanged};
    QVERIFY(!model.networkActionReady());

    model.initializeResult(initialize_success, {}, false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(model.networkActionReady(), initialize_success, ASYNC_TIMEOUT_MS);
    QCOMPARE(ready.count(), initialize_success ? 1 : 0);

    if (direct_drain) model.beginShutdown();
    else model.requestShutdown();
    QVERIFY(!model.networkActionReady());
    QCOMPARE(ready.count(), initialize_success ? 2 : 0);

    // Repeated shutdown and a late successful initialization cannot enable
    // network controls after a shutdown request or worker retirement.
    const int notifications = ready.count();
    if (direct_drain) model.beginShutdown();
    else model.requestShutdown();
    model.initializeResult(true, {}, false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QVERIFY(!model.networkActionReady());
    QCOMPARE(ready.count(), notifications);
}

void NodeModelTests::peerAdmissionDoesNotWaitForNetworkObservation_data()
{
    QTest::addColumn<bool>("shutdown");
    QTest::newRow("ordinary-submitted-closed") << false;
    QTest::newRow("workers-retiring") << true;
}

void NodeModelTests::peerAdmissionDoesNotWaitForNetworkObservation()
{
    QFETCH(bool, shutdown);
    MockNode node;
    ConfigureNodeModelDefaults(node);
    MempoolState mempool;
    ConfigureMempoolGetters(node, mempool);
    int effects{0};
    node.disconnect_by_id_fn = [&](NodeId id) { ++effects; return id == 7; };
    ManualNodeObservations env;
    NodeModel model{node, false, env.services()};
    QObject receiver;
    const auto cleanup = qScopeGuard([&] {
        QObject::disconnect(&model, nullptr, &receiver, nullptr);
        model.beginShutdown();
        QVERIFY(DrainManualNodeTurns(env));
        QVERIFY(model.isDrained());
    });
    QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(model.notificationsReady());
    QVERIFY(!model.networkBackendAvailable());
    QVERIFY(!model.peerCommands()->canStart());
    quint64 original{0};
    bool captured{false};
    QSignalSpy terminal{model.peerCommands(), &PeerCommandModel::finished};
    QSignalSpy drained{&model, &NodeModel::workDrained};
    connect(&model, &NodeModel::networkBackendAvailabilityChanged, &receiver, [&] {
        if (captured || !model.networkBackendAvailable()) return;
        captured = true;
        // NetworkModel has only queued its initial observation. Peer commands
        // require the live public backend, not Pause/Resume's known-state read.
        QCOMPARE(node.calls.getNetworkActive.load(), 0);
        QVERIFY(!model.networkModel()->known());
        QVERIFY(!model.networkActionReady());
        QVERIFY(model.peerCommands()->canStart());
        original = model.peerCommands()->requestDisconnectPeer(7);
        QVERIFY(original);
        QVERIFY(model.peerCommands()->pending());
        QCOMPARE(effects, 0);
        if (shutdown) model.beginShutdown();
        else model.close();
        QVERIFY(!model.networkBackendAvailable());
        QVERIFY(!model.peerCommands()->canStart());
        QCOMPARE(model.peerCommands()->requestDisconnectPeer(8), quint64{0});
        QCOMPARE(effects, 0);
    });
    model.initializeResult(true, {}, false, false);
    QVERIFY(env.step(execution::Lane::Utility)); // Startup diagnostics are ready for GUI delivery.
    env.publishAll();
    QVERIFY(captured);
    QVERIFY(original);
    QCOMPARE(effects, 0);
    QVERIFY(model.peerCommands()->pending());
    while (env.step(execution::Lane::Utility)) {}
    QCOMPARE(effects, 0);
    QCOMPARE(drained.count(), 0);
    for (int turn = 0; turn < 4 && effects == 0; ++turn) {
        QVERIFY(env.step(execution::Lane::Core));
    }
    QCOMPARE(effects, 1);
    QVERIFY(env.step(execution::Lane::Utility));
    while (env.step(execution::Lane::Utility)) {} // Shutdown can also queue cleanup here.
    QCOMPARE(node.calls.disconnectById.load(), 1);
    QVERIFY(model.peerCommands()->pending()); // Its GUI receipt is still held.
    QCOMPARE(terminal.count(), 0);
    env.publishAll();
    QVERIFY(!model.peerCommands()->pending());
    const auto result = model.peerCommands()->lastResult();
    QVERIFY(result.present);
    QVERIFY(result.outcomeKnown);
    QVERIFY(result.mutationAttempted);
    QVERIFY(result.disconnectConfirmed);
    QCOMPARE(result.nodeIdText, QStringLiteral("7"));
    QCOMPARE(result.outcome, PeerCommandOperation::Outcome::Succeeded);
    QVERIFY(!model.peerCommands()->canStart());
    QCOMPARE(model.peerCommands()->requestDisconnectPeer(8), quint64{0});
    QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(drained.count(), shutdown ? 1 : 0);
    QCOMPARE(effects, 1);
    QCOMPARE(terminal.count(), 0); // Closed views stay quiet; actual result remains.
}

void NodeModelTests::requestShutdownEmitsOnlyOnce()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    model.requestShutdown();
    model.requestShutdown();

    QCOMPARE(shutdown_spy.count(), 1);
}

void NodeModelTests::initializationFailureRequestsShutdownWhenCoreWasInterrupted()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.shutdown_requested_fn = [] { return true; };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy error_state_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};
    model.initializeResult(false, {}, false, true);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QCOMPARE(error_state_spy.count(), 1);
    QVERIFY(model.errorState());
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(initialized_spy.count(), 1);
}

void NodeModelTests::initializationFailureWithoutCoreInterruptOnlySetsErrorState()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.shutdown_requested_fn = [] { return false; };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy error_state_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};
    model.initializeResult(false, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QCOMPARE(error_state_spy.count(), 1);
    QVERIFY(model.errorState());
    QCOMPARE(shutdown_spy.count(), 0);
    QCOMPARE(initialized_spy.count(), 1);
}

void NodeModelTests::initializationSuccessDuringCoreShutdownSkipsReadyState()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.shutdown_requested_fn = [] { return true; };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};
    QSignalSpy ready_state_spy{&model, &NodeModel::chainStateReady};
    model.initializeResult(true, {}, false, true);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(initialized_spy.count(), 0);
    QCOMPARE(ready_state_spy.count(), 0);
}

void NodeModelTests::shutdownUnsubscribesCoreSignalsBeforeDrained()
{
    MockNode node;
    MempoolState mempool;
    std::atomic<int> disconnected_handlers{0};
    auto counted_handler = [&] {
        return interfaces::MakeCleanupHandler([&] { ++disconnected_handlers; });
    };

    ConfigureMempoolGetters(node, mempool);
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn) {
        return counted_handler();
    };
    node.handle_notify_header_tip_fn = [&](interfaces::Node::NotifyHeaderTipFn) {
        return counted_handler();
    };
    node.handle_notify_num_connections_changed_fn = [&](interfaces::Node::NotifyNumConnectionsChangedFn) {
        return counted_handler();
    };
    node.handle_notify_network_active_changed_fn = [&](interfaces::Node::NotifyNetworkActiveChangedFn) {
        return counted_handler();
    };
    node.handle_notify_alert_changed_fn = [&](interfaces::Node::NotifyAlertChangedFn) {
        return counted_handler();
    };
    node.handle_message_box_fn = [&](interfaces::Node::MessageBoxFn) {
        return counted_handler();
    };
    node.handle_question_fn = [&](interfaces::Node::QuestionFn) {
        return counted_handler();
    };
    node.handle_banned_list_changed_fn = [&](interfaces::Node::BannedListChangedFn) {
        return counted_handler();
    };

    auto model{std::make_unique<NodeModel>(node)};
    WaitForInitialMempoolRefresh(mempool);

    const int refresh_calls = mempool.max_usage_calls.load();
    model->setMempoolInfoPollingActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(mempool.max_usage_calls.load() > refresh_calls, ASYNC_TIMEOUT_MS);

    QSignalSpy drained{model.get(), &NodeModel::workDrained};
    DrainNodeModel(*model);
    QCOMPARE(drained.count(), 1);
    QCOMPARE(disconnected_handlers.load(), 8);
    model.reset();

}

void NodeModelTests::nodeNotificationHandlersUpdateModelThroughQueuedSignals()
{
    MockNode node;
    MempoolState mempool;
    PeerCountState peers;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed_fn;
    interfaces::Node::BannedListChangedFn banned_list_changed_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    ConfigurePeerCountGetters(node, peers);
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn fn) {
        block_tip_fn = std::move(fn);
        return MakeNoopHandler();
    };
    node.handle_notify_num_connections_changed_fn = [&](interfaces::Node::NotifyNumConnectionsChangedFn fn) {
        connections_changed_fn = std::move(fn);
        return MakeNoopHandler();
    };
    node.handle_banned_list_changed_fn = [&](interfaces::Node::BannedListChangedFn fn) {
        banned_list_changed_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);
    QVERIFY(connections_changed_fn);
    QVERIFY(banned_list_changed_fn);

    QSignalSpy block_tip_height_spy{&model, &NodeModel::blockTipHeightChanged};
    QSignalSpy verification_progress_spy{&model, &NodeModel::verificationProgressChanged};
    QSignalSpy time_ratio_spy{&model, &NodeModel::blockTipTimeChanged};
    QSignalSpy peers_spy{&model, &NodeModel::numPeersChanged};
    QSignalSpy inbound_peers_spy{&model, &NodeModel::numInboundPeersChanged};
    QSignalSpy outbound_peers_spy{&model, &NodeModel::numOutboundPeersChanged};
    QSignalSpy banned_list_spy{&model, &NodeModel::bannedListChanged};

    block_tip_fn(SynchronizationState::POST_INIT, interfaces::BlockTip{144, 1'700'000'000, uint256{}}, 0.42);
    peers.total = 9;
    peers.inbound = 2;
    peers.outbound = 7;
    connections_changed_fn(9);
    banned_list_changed_fn();

    QTRY_COMPARE_WITH_TIMEOUT(block_tip_height_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(verification_progress_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(time_ratio_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(inbound_peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(outbound_peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(banned_list_spy.count(), 1, ASYNC_TIMEOUT_MS);

    QCOMPARE(model.blockTipHeight(), 144);
    QCOMPARE(model.verificationProgress(), 0.42);
    QCOMPARE(time_ratio_spy.takeFirst().at(0).toInt(), 1'700'000'000);
    QCOMPARE(model.numPeers(), 9);
    QCOMPARE(model.numInboundPeers(), 2);
    QCOMPARE(model.numOutboundPeers(), 7);
}

void NodeModelTests::bannedListNotificationFromGuiThreadIsNotDeliveredReentrantly()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::BannedListChangedFn banned_list_changed_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_banned_list_changed_fn = [&](interfaces::Node::BannedListChangedFn fn) {
        banned_list_changed_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(banned_list_changed_fn);

    QSignalSpy banned_list_spy{&model, &NodeModel::bannedListChanged};

    // A ban list change caused by a GUI action reaches the notification handler
    // on the GUI thread. Delivering it directly would reset the ban list model
    // while the QML handler that triggered it is still running.
    banned_list_changed_fn();
    QCOMPARE(banned_list_spy.count(), 0);

    // Process queued calls only: delivery must not depend on a timer firing.
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(banned_list_spy.count(), 1);

    // A burst needs only one queued delivery, and a later change must wake the
    // GUI again after the previous batch has been consumed.
    for (int i = 0; i < 100; ++i) banned_list_changed_fn();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(banned_list_spy.count(), 2);
    banned_list_changed_fn();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(banned_list_spy.count(), 3);
}

void NodeModelTests::notificationsDuringAndAfterShutdownAreIgnored()
{
    for (const bool drain : {false, true}) {
        MockNode node;
        MempoolState mempool;
        interfaces::Node::BannedListChangedFn banned_list_changed;
        ConfigureNodeModelDefaults(node);
        ConfigureMempoolGetters(node, mempool);
        node.handle_banned_list_changed_fn = [&](interfaces::Node::BannedListChangedFn fn) {
            banned_list_changed = std::move(fn);
            return MakeNoopHandler();
        };

        auto model{std::make_unique<NodeModel>(node, false)};
        QTRY_VERIFY_WITH_TIMEOUT(model->notificationsReady(), ASYNC_TIMEOUT_MS);
        QSignalSpy changed{model.get(), &NodeModel::bannedListChanged};
        QVERIFY(banned_list_changed);
        banned_list_changed(); // Leave a delivery queued when shutdown starts.

        std::atomic_bool stop{false};
        std::binary_semaphore started{0};
        std::thread worker([&] {
            banned_list_changed();
            started.release();
            while (!stop.load()) banned_list_changed();
        });
        const auto join = qScopeGuard([&] {
            stop = true;
            worker.join();
        });
        started.acquire();
        if (drain) DrainNodeModel(*model);
        model.reset();
        QCOMPARE(changed.count(), 0);
        // A backend callback can retain the shared notification state even after
        // the GUI receiver has been destroyed.
        banned_list_changed();
    }
}

void NodeModelTests::blockTipUpdatesQueuedAcrossThreadsRetainPayloadValues()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn fn) {
        block_tip_fn = std::move(fn);
        return MakeNoopHandler();
    };
    node.handle_notify_num_connections_changed_fn = [](interfaces::Node::NotifyNumConnectionsChangedFn) {
        return MakeNoopHandler();
    };
    node.handle_banned_list_changed_fn = [](interfaces::Node::BannedListChangedFn) {
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);

    std::vector<double> seen_progress;
    std::vector<int> seen_heights;
    std::vector<qint64> seen_times;

    QObject::connect(&model, &NodeModel::verificationProgressChanged, &model, [&] {
        seen_progress.push_back(model.verificationProgress());
    });
    QObject::connect(&model, &NodeModel::blockTipHeightChanged, &model, [&] {
        seen_heights.push_back(model.blockTipHeight());
    });
    QObject::connect(&model, &NodeModel::blockTipTimeChanged, &model, [&](qint64 block_time) {
        seen_times.push_back(block_time);
    });

    std::thread worker([&] {
        block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{123, 1'700'000'001, uint256{}}, 0.51);
        block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{456, 1'700'000'099, uint256{}}, 0.75);
    });
    worker.join();

    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(seen_progress.size(), size_t{1});
    QTRY_COMPARE_WITH_TIMEOUT(seen_heights.size(), size_t{1}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(seen_times.size(), size_t{1}, ASYNC_TIMEOUT_MS);
    QCOMPARE(seen_progress.at(0), 0.75);
    QCOMPARE(seen_heights.at(0), 456);
    QCOMPARE(seen_times.at(0), 1'700'000'099);
    QCOMPARE(model.blockTipHeight(), 456);
    QVERIFY(qFuzzyCompare(model.verificationProgress(), 0.75));
}

void NodeModelTests::blockSyncActiveFollowsInitializationAndBlockTipState()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;
    bool initial_block_download{true};

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.is_initial_block_download_fn = [&] { return initial_block_download; };
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn fn) {
        block_tip_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);
    QVERIFY(!model.blockSyncActive());
    QVERIFY(!model.initialSyncComplete());

    QSignalSpy block_sync_spy{&model, &NodeModel::blockSyncActiveChanged};
    QSignalSpy sync_complete_spy{&model, &NodeModel::initialSyncCompleteChanged};
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 0,
        .block_time = 1'700'000'000,
        .header_height = 100,
        .header_time = GetTime(),
        .verification_progress = 0.25,
    }, initial_block_download, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QCOMPARE(block_sync_spy.count(), 1);
    QVERIFY(model.blockSyncActive());
    QVERIFY(!model.initialSyncComplete());
    QCOMPARE(sync_complete_spy.count(), 0);

    block_tip_fn(SynchronizationState::POST_INIT, interfaces::BlockTip{101, 1'700'000'001, uint256{}}, 0.25);
    QTRY_COMPARE_WITH_TIMEOUT(block_sync_spy.count(), 2, ASYNC_TIMEOUT_MS);
    QVERIFY(!model.blockSyncActive());
    QVERIFY(model.initialSyncComplete());
    QCOMPARE(sync_complete_spy.count(), 1);

    block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{102, 1'700'000'002, uint256{}}, 0.26);
    QTRY_COMPARE_WITH_TIMEOUT(block_sync_spy.count(), 3, ASYNC_TIMEOUT_MS);
    QVERIFY(model.blockSyncActive());
    QVERIFY(model.initialSyncComplete());
    QCOMPARE(sync_complete_spy.count(), 1);
}

void NodeModelTests::initialSyncCompletionIgnoresVerificationEstimateForGenesis()
{
    MockNode node;
    MempoolState mempool;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.is_initial_block_download_fn = [] { return false; };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QSignalSpy sync_complete_spy{&model, &NodeModel::initialSyncCompleteChanged};

    // A valid genesis-only chain can be complete (for example, regtest), and
    // the verification estimate is not a completion predicate.
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 0,
        .block_time = 1'700'000'000,
        .header_height = 0,
        .header_time = GetTime(),
        .verification_progress = 0.25,
    });
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QVERIFY(!model.blockSyncActive());
    QVERIFY(model.initialSyncComplete());
    QCOMPARE(sync_complete_spy.count(), 1);
}

void NodeModelTests::initialSyncCompletionWaitsForStartupCatchUpAndLatches()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;
    interfaces::Node::NotifyHeaderTipFn header_tip_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.is_initial_block_download_fn = [] { return false; };
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn fn) {
        block_tip_fn = std::move(fn);
        return MakeNoopHandler();
    };
    node.handle_notify_header_tip_fn = [&](interfaces::Node::NotifyHeaderTipFn fn) {
        header_tip_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);
    QVERIFY(header_tip_fn);
    QSignalSpy sync_complete_spy{&model, &NodeModel::initialSyncCompleteChanged};

    const int64_t stale_header_time{
        GetTime() - 25 * Params().GetConsensus().nPowTargetSpacing};
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 313'768,
        .block_time = stale_header_time,
        .header_height = 313'768,
        .header_time = stale_header_time,
        .verification_progress = 0.90,
    });
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    // Core can latch out of IBD before connecting to a peer when the saved tip
    // is recent by Core's standard. The stale header still keeps the startup
    // transition pending.
    QVERIFY(!model.blockSyncActive());
    QVERIFY(model.headerSyncActive());
    QVERIFY(!model.initialSyncComplete());
    QCOMPARE(sync_complete_spy.count(), 0);

    header_tip_fn(SynchronizationState::POST_INIT,
                  interfaces::BlockTip{313'900, GetTime(), uint256{}},
                  /*presync=*/false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.headerSyncActive(), ASYNC_TIMEOUT_MS);
    QVERIFY(!model.initialSyncComplete());

    block_tip_fn(SynchronizationState::POST_INIT,
                 interfaces::BlockTip{313'899, GetTime(), uint256{}},
                 0.9999);
    QTRY_COMPARE_WITH_TIMEOUT(model.blockTipHeight(), 313'899, ASYNC_TIMEOUT_MS);
    QVERIFY(!model.initialSyncComplete());
    QCOMPARE(sync_complete_spy.count(), 0);

    block_tip_fn(SynchronizationState::POST_INIT,
                 interfaces::BlockTip{313'900, GetTime(), uint256{}},
                 1.0);
    QTRY_VERIFY_WITH_TIMEOUT(model.initialSyncComplete(), ASYNC_TIMEOUT_MS);
    QCOMPARE(sync_complete_spy.count(), 1);

    // A header is normally announced before its block. Once startup sync has
    // completed, that ordinary one-block gap must not regress the clock.
    header_tip_fn(SynchronizationState::POST_INIT,
                  interfaces::BlockTip{313'901, GetTime(), uint256{}},
                  /*presync=*/false);
    block_tip_fn(SynchronizationState::POST_INIT,
                 interfaces::BlockTip{313'901, GetTime(), uint256{}},
                 1.0);
    QTRY_COMPARE_WITH_TIMEOUT(model.blockTipHeight(), 313'901, ASYNC_TIMEOUT_MS);
    QVERIFY(model.initialSyncComplete());
    QCOMPARE(sync_complete_spy.count(), 1);
}

void NodeModelTests::syncCompletionResetsForLargeHeaderGap_data()
{
    QTest::addColumn<bool>("rewind_blocks");
    QTest::newRow("headers-after-sleep") << false;
    QTest::newRow("active-chain-rewind") << true;
}

void NodeModelTests::syncCompletionResetsForLargeHeaderGap()
{
    QFETCH(bool, rewind_blocks);
    MockNode node;
    MempoolState mempool;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;
    interfaces::Node::NotifyHeaderTipFn header_tip_fn;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.is_initial_block_download_fn = [] { return false; };
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn fn) {
        block_tip_fn = std::move(fn);
        return MakeNoopHandler();
    };
    node.handle_notify_header_tip_fn = [&](interfaces::Node::NotifyHeaderTipFn fn) {
        header_tip_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);
    QVERIFY(header_tip_fn);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 10'000,
        .block_time = GetTime(),
        .header_height = 10'000,
        .header_time = GetTime(),
        .verification_progress = 1.0,
    });
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QVERIFY(model.initialSyncComplete());
    QSignalSpy sync_complete_spy{&model, &NodeModel::initialSyncCompleteChanged};

    for (const int gap : {1, 24, 25, 200}) {
        if (rewind_blocks) {
            block_tip_fn(SynchronizationState::POST_INIT,
                         interfaces::BlockTip{10'000 - gap, GetTime(), uint256{}}, 0.9999);
        } else {
            header_tip_fn(SynchronizationState::POST_INIT,
                          interfaces::BlockTip{10'000 + gap, GetTime(), uint256{}}, /*presync=*/false);
        }
        if (rewind_blocks) QTRY_COMPARE_WITH_TIMEOUT(model.blockTipHeight(), 10'000 - gap, ASYNC_TIMEOUT_MS);
        else QTRY_COMPARE_WITH_TIMEOUT(InformationValue(model, QStringLiteral("Header height")), QString::number(10'000 + gap), ASYNC_TIMEOUT_MS);
        QVERIFY(!model.blockSyncActive());
        QCOMPARE(model.initialSyncComplete(), gap <= 24);
        QCOMPARE(sync_complete_spy.count(), gap <= 24 ? 0 : 1);
    }

    // After resuming sync, stay there even inside the hysteresis threshold.
    // Completion requires reaching the best known header again.
    const int header_height{rewind_blocks ? 10'000 : 10'200};
    for (const int gap : {24, 1, 0}) {
        block_tip_fn(SynchronizationState::POST_INIT,
                     interfaces::BlockTip{header_height - gap, GetTime(), uint256{}}, 0.9999);
        QTRY_COMPARE_WITH_TIMEOUT(model.blockTipHeight(), header_height - gap, ASYNC_TIMEOUT_MS);
        QCOMPARE(model.initialSyncComplete(), gap == 0);
        QCOMPARE(sync_complete_spy.count(), gap == 0 ? 2 : 1);
    }
}

void NodeModelTests::coalescedTipsDoNotLatchCompletionFromIntermediateHeights()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    interfaces::Node::NotifyBlockTipFn block_tip;
    interfaces::Node::NotifyHeaderTipFn header_tip;
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn callback) {
        block_tip = std::move(callback);
        return MakeNoopHandler();
    };
    node.handle_notify_header_tip_fn = [&](interfaces::Node::NotifyHeaderTipFn callback) {
        header_tip = std::move(callback);
        return MakeNoopHandler();
    };
    NodeModel model{node, false};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    QVERIFY(block_tip);
    QVERIFY(header_tip);
    const auto now = GetTime();
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 100, .block_time = now,
        .header_height = 100, .header_time = now,
        .verification_progress = 1.0,
    }, false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QVERIFY(model.initialSyncComplete());

    header_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{200, now, uint256{1}}, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.initialSyncComplete(), ASYNC_TIMEOUT_MS);
    QSignalSpy completion{&model, &NodeModel::initialSyncCompleteChanged};

    // Both callbacks precede the next GUI publication. The shorter replacement
    // chain remains one block behind its best header. Applying header 91 while
    // block 100 is still cached must not latch completion for the final 90/91.
    block_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{90, now, uint256{2}}, 0.9);
    header_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{91, now, uint256{3}}, false);
    QTRY_COMPARE_WITH_TIMEOUT(model.blockTipHeight(), 90, ASYNC_TIMEOUT_MS);
    QCOMPARE(InformationValue(model, QStringLiteral("Header height")), QStringLiteral("91"));
    QVERIFY(!model.initialSyncComplete());
    QCOMPARE(completion.count(), 0);

    block_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{91, now, uint256{3}}, 1.0);
    QTRY_VERIFY_WITH_TIMEOUT(model.initialSyncComplete(), ASYNC_TIMEOUT_MS);
    QCOMPARE(completion.count(), 1);
}

void NodeModelTests::alertNotificationsRefreshWarningList()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::NotifyAlertChangedFn alert_changed_fn;
    std::mutex warnings_mutex;
    bilingual_str warnings{Untranslated("")};

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.get_warnings_fn = [&] { std::lock_guard lock{warnings_mutex}; return warnings; };
    node.handle_notify_alert_changed_fn = [&](interfaces::Node::NotifyAlertChangedFn fn) {
        alert_changed_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(alert_changed_fn);
    QVERIFY(!model.hasWarnings());

    QSignalSpy warnings_spy{&model, &NodeModel::warningsChanged};
    { std::lock_guard lock{warnings_mutex}; warnings = bilingual_str{"first<hr />second", "translated first<hr />translated second"}; }
    alert_changed_fn();

    QTRY_COMPARE_WITH_TIMEOUT(warnings_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.warningList(), QStringList({QStringLiteral("translated first"), QStringLiteral("translated second")}));
    QCOMPARE(model.warnings(), QStringLiteral("translated first<hr />translated second"));
    QVERIFY(model.hasWarnings());
}

void NodeModelTests::headerTipNotificationsExposeHeaderSyncProgress()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::NotifyHeaderTipFn header_tip_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_notify_header_tip_fn = [&](interfaces::Node::NotifyHeaderTipFn fn) {
        header_tip_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(header_tip_fn);

    QSignalSpy header_spy{&model, &NodeModel::headerSyncChanged};
    QSignalSpy block_sync_spy{&model, &NodeModel::blockSyncActiveChanged};
    const int height{100};
    const int64_t block_time{GetTime() - 100 * Params().GetConsensus().nPowTargetSpacing};
    header_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{height, block_time, uint256{}}, /*presync=*/true);

    QTRY_COMPARE_WITH_TIMEOUT(header_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(block_sync_spy.count(), 0);
    QVERIFY(model.headerSyncActive());
    QVERIFY(model.headerPresync());
    QVERIFY(model.headerSyncProgress() > 0.45);
    QVERIFY(model.headerSyncProgress() < 0.55);
    QVERIFY(!model.blockSyncActive());
}

void NodeModelTests::startupWarningsAreShownOnceAndDoNotBecomeCurrentWarnings()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;
    interfaces::Node::NotifyAlertChangedFn alert_changed_fn;
    std::mutex warnings_mutex;
    bilingual_str warnings{"network warning", "Translated network warning"};

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.get_warnings_fn = [&] { std::lock_guard lock{warnings_mutex}; return warnings; };
    node.handle_notify_alert_changed_fn = [&](interfaces::Node::NotifyAlertChangedFn fn) {
        alert_changed_fn = std::move(fn);
        return MakeNoopHandler();
    };
    node.handle_message_box_fn = [&](interfaces::Node::MessageBoxFn fn) {
        message_box_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    model.addStartupWarnings({QStringLiteral("Translated early startup warning")});
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(alert_changed_fn);
    QVERIFY(message_box_fn);

    QSignalSpy runtime_dialog_spy{&model, &NodeModel::runtimeDialogChanged};
    message_box_fn(
        bilingual_str{"Startup warning", "Translated startup warning"},
        CClientUIInterface::MSG_WARNING);
    QCOMPARE(runtime_dialog_spy.count(), 0);

    model.initializeResult(true, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QTRY_COMPARE_WITH_TIMEOUT(runtime_dialog_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Warning"));
    QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated early startup warning\n\nTranslated startup warning"));
    QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK));

    QTRY_COMPARE_WITH_TIMEOUT(model.warningList(), QStringList({QStringLiteral("Translated network warning")}), ASYNC_TIMEOUT_MS);
    QVERIFY(model.hasWarnings());
    QVERIFY(model.startupError().isEmpty());

    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    QTRY_COMPARE_WITH_TIMEOUT(runtime_dialog_spy.count(), 2, ASYNC_TIMEOUT_MS);
    QVERIFY(!model.runtimeDialogVisible());

    QSignalSpy warnings_spy{&model, &NodeModel::warningsChanged};
    { std::lock_guard lock{warnings_mutex}; warnings = Untranslated(""); }
    alert_changed_fn();

    QTRY_COMPARE_WITH_TIMEOUT(warnings_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.warningList(), QStringList());
    QVERIFY(!model.hasWarnings());
}

void NodeModelTests::manualStartupCallbacksKeepTheirPresentationPhase_data()
{
    QTest::addColumn<unsigned int>("style");
    QTest::addColumn<bool>("question");
    QTest::addColumn<bool>("dialog_expected");
    QTest::addColumn<bool>("shutdown_expected");
    QTest::addColumn<bool>("warning_expected");
    QTest::addColumn<bool>("error_expected");
    QTest::newRow("nonmodal-warning") << unsigned(CClientUIInterface::ICON_WARNING) << false << false << false << true << false;
    QTest::newRow("modal-warning") << unsigned(CClientUIInterface::ICON_WARNING | CClientUIInterface::MODAL) << false << false << false << true << false;
    QTest::newRow("nonmodal-error") << unsigned(CClientUIInterface::ICON_ERROR) << false << false << false << false << true;
    QTest::newRow("nonmodal-information") << unsigned(CClientUIInterface::ICON_INFORMATION) << false << false << false << false << false;
    QTest::newRow("modal-information") << unsigned(CClientUIInterface::ICON_INFORMATION | CClientUIInterface::MODAL | CClientUIInterface::BTN_OK) << false << true << false << false << false;
    QTest::newRow("modal-error") << unsigned(CClientUIInterface::ICON_ERROR | CClientUIInterface::MODAL | CClientUIInterface::BTN_OK) << false << true << true << false << false;
    QTest::newRow("startup-question") << unsigned(CClientUIInterface::ICON_INFORMATION | CClientUIInterface::BTN_OK | CClientUIInterface::BTN_ABORT) << true << true << true << false << false;
}

void NodeModelTests::manualStartupCallbacksKeepTheirPresentationPhase()
{
    QFETCH(unsigned int, style);
    QFETCH(bool, question);
    QFETCH(bool, dialog_expected);
    QFETCH(bool, shutdown_expected);
    QFETCH(bool, warning_expected);
    QFETCH(bool, error_expected);
    MockNode node;
    ConfigureNodeModelDefaults(node);
    MempoolState mempool;
    ConfigureMempoolGetters(node, mempool);
    interfaces::Node::MessageBoxFn message_box;
    interfaces::Node::QuestionFn ask;
    node.handle_message_box_fn = [&](auto callback) { message_box = std::move(callback); return MakeNoopHandler(); };
    node.handle_question_fn = [&](auto callback) { ask = std::move(callback); return MakeNoopHandler(); };
    ManualNodeObservations env;
    auto loss = std::make_shared<std::atomic_bool>(false);
    auto services = env.services();
    services.diagnostic_loss = loss;
    NodeModel model{node, false, std::move(services)};
    std::thread native;
    std::atomic_bool returned{false};
    bool answer{true};
    const auto cleanup = qScopeGuard([&] {
        QObject::disconnect(&model, nullptr, this, nullptr);
        model.beginShutdown();
        if (native.joinable()) native.join();
        env.drain(); env.publishAll();
    });
    env.drain(); env.publishAll();
    QVERIFY(model.notificationsReady());
    QVERIFY(message_box);
    QVERIFY(ask);
    QSignalSpy initialized{&model, &NodeModel::nodeInitialized};
    QSignalSpy shutdown{&model, &NodeModel::requestedShutdown};
    native = std::thread([&] {
        const bilingual_str text{"Original startup message", "Translated startup message"};
        if (question) answer = ask(text, "Noninteractive fallback", style);
        else message_box(text, style);
        returned = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(!env.turnsIdle() || !env.publicationsIdle() || returned.load(), ASYNC_TIMEOUT_MS);
    // Native ingress is captured, but its Utility intake and GUI frame are held.
    QVERIFY(!model.runtimeDialogVisible());
    if (dialog_expected) QVERIFY(!returned.load());
    env.drain(); env.publishAll();
    QCOMPARE(model.runtimeDialogVisible(), dialog_expected);
    if (dialog_expected) {
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated startup message"));
        QCOMPARE(model.runtimeDialogQuestion(), question);
        model.answerRuntimeDialog(question ? CClientUIInterface::BTN_ABORT : CClientUIInterface::BTN_OK);
        env.drain();
    }
    native.join();
    env.drain(); env.publishAll();
    QVERIFY(returned.load());
    if (question) QVERIFY(!answer);
    QCOMPARE(initialized.size(), 0);
    QCOMPARE(shutdown.size(), 0);
    model.initializeResult(false, {});
    if (!shutdown_expected) {
        QVERIFY(model.startupDiagnosticsPending());
        QCOMPARE(initialized.size(), 0);
    }
    env.drain(); env.publishAll();
    QVERIFY(!model.startupDiagnosticsPending());
    QCOMPARE(initialized.size(), 1);
    QCOMPARE(shutdown.size(), shutdown_expected ? 1 : 0);
    QCOMPARE(model.errorState(), !shutdown_expected);
    QVERIFY(!model.runtimeDialogVisible());
    QVERIFY(!loss->load());
    if (shutdown_expected) QVERIFY(model.startupError().isEmpty());
    else {
        const auto expected = error_expected ? QStringLiteral("Translated startup message")
            : warning_expected ? QStringLiteral("Startup warnings:\nTranslated startup message\n\nNode initialization failed.")
            : QStringLiteral("Node initialization failed.");
        QCOMPARE(model.startupError(), expected);
    }
}

void NodeModelTests::manualStartupSummaryReportsAFullRuntimeQueue()
{
    MockNode node;
    ConfigureNodeModelDefaults(node);
    MempoolState mempool;
    ConfigureMempoolGetters(node, mempool);
    interfaces::Node::MessageBoxFn message_box;
    node.handle_message_box_fn = [&](auto callback) { message_box = std::move(callback); return MakeNoopHandler(); };
    ManualNodeObservations env;
    auto loss = std::make_shared<std::atomic_bool>(false);
    auto services = env.services();
    services.diagnostic_loss = loss;
    NodeModel model{node, false, std::move(services)};
    const auto cleanup = qScopeGuard([&] {
        QObject::disconnect(&model, nullptr, this, nullptr);
        model.beginShutdown(); env.drain(); env.publishAll();
    });
    env.drain(); env.publishAll();
    QVERIFY(model.notificationsReady());
    QVERIFY(message_box);
    QStringList errors;
    connect(&model, &NodeModel::observationErrorChanged, this, [&] { errors.push_back(model.observationError()); });
    model.addStartupWarnings({QStringLiteral("Original startup warning")});
    QSignalSpy initialized{&model, &NodeModel::nodeInitialized};
    model.initializeResult(true, {}, false, false);
    QVERIFY(model.startupDiagnosticsPending());
    env.drain();
    // Physical diagnostic work settles, but its original GUI receipt stays
    // held while Runtime ingress fills the owned prompt queue.
    std::deque<std::function<void()>> held_projection;
    while(auto turn=env.takePublicationBack())held_projection.push_front(std::move(turn));
    const auto release_projection=[&]{
        while(!held_projection.empty()){
            auto turn=std::move(held_projection.front());held_projection.pop_front();turn();
        }
    };
    const auto restore_projection=qScopeGuard([&]{release_projection();env.drain();env.publishAll();});
    QVERIFY(!held_projection.empty());
    std::thread native([&] {
        for (int index = 0; index < 32; ++index) {
            message_box(Untranslated(QStringLiteral("Runtime message %1").arg(index).toStdString()), CClientUIInterface::ICON_INFORMATION);
        }
    });
    native.join();
    env.drain();env.publishAll();
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Runtime message 0"));
    QCOMPARE(initialized.size(), 0);
    release_projection();
    env.drain(); env.publishAll();
    QVERIFY(!model.startupDiagnosticsPending());
    QCOMPARE(initialized.size(), 1);
    QVERIFY(errors.contains(QStringLiteral("A node dialog could not be shown because the dialog queue is full.")));
    QVERIFY(!errors.contains(QStringLiteral("Some node messages exceeded the dialog queue limit.")));
    QVERIFY(model.startupDiagnostics()->rowCount() > 0);
    for (int index = 0; index < 32; ++index) {
        QVERIFY(model.runtimeDialogVisible());
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Runtime message %1").arg(index));
        model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        env.drain();env.publishAll();
    }
    QVERIFY(!model.runtimeDialogVisible());
    QVERIFY(!loss->load());
}

void NodeModelTests::manualStartupSummaryPreservesDiagnosticsWhenDialogTextBudgetIsFull()
{
    MockNode node;
    ConfigureNodeModelDefaults(node);
    MempoolState mempool;
    ConfigureMempoolGetters(node, mempool);
    interfaces::Node::MessageBoxFn message_box;
    node.handle_message_box_fn = [&](auto callback) { message_box = std::move(callback); return MakeNoopHandler(); };
    ManualNodeObservations env;
    auto loss = std::make_shared<std::atomic_bool>(false);
    auto services = env.services();
    services.diagnostic_loss = loss;
    NodeModel model{node, false, std::move(services)};
    const auto cleanup = qScopeGuard([&] {
        QObject::disconnect(&model, nullptr, this, nullptr);
        model.beginShutdown(); env.drain(); env.publishAll();
    });
    env.drain(); env.publishAll();
    QVERIFY(model.notificationsReady());
    QVERIFY(message_box);
    model.addStartupWarnings({QStringLiteral("Original startup warning remains in its details")});
    model.initializeResult(true, {}, false, false);
    QVERIFY(model.startupDiagnosticsPending());
    env.drain();
    // Physical diagnostic work settles, but its original GUI receipt stays
    // held while Runtime ingress fills the owned prompt queue.
    std::deque<std::function<void()>> held_projection;
    while(auto turn=env.takePublicationBack())held_projection.push_front(std::move(turn));
    const auto release_projection=[&]{
        while(!held_projection.empty()){
            auto turn=std::move(held_projection.front());held_projection.pop_front();turn();
        }
    };
    const auto restore_projection=qScopeGuard([&]{release_projection();env.drain();env.publishAll();});
    QVERIFY(!held_projection.empty());
    std::thread native([&] {
        message_box(Untranslated(std::string(startupdiagnostics::TextBudget::MAX_CHARS, 'r')), CClientUIInterface::ICON_INFORMATION);
    });
    native.join();
    env.drain();env.publishAll();
    QVERIFY(model.runtimeDialogVisible());
    QVERIFY(model.runtimeDialogMessage().startsWith(QString(16384, QLatin1Char('r'))));
    QVERIFY(model.runtimeDialogMessage().contains(QStringLiteral("additional message text omitted")));
    QVERIFY(!loss->load()); // The full text was accepted on Native and remains retained by this request.
    release_projection();
    env.drain(); env.publishAll();
    QVERIFY(!model.startupDiagnosticsPending());
    QVERIFY(loss->load());
    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    env.drain();env.publishAll();
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Warning"));
    QVERIFY(model.runtimeDialogMessage().contains(QStringLiteral("Original startup warning remains in its details")));
    QVERIFY(model.runtimeDialogMessage().contains(QStringLiteral("full message not available: too many messages")));
    auto* details = model.startupDiagnostics();
    QString full;
    for (int row = 0; row < details->rowCount(); ++row) full += details->data(details->index(row, 0), Qt::UserRole + 1).toString();
    QVERIFY(full.contains(QStringLiteral("Original startup warning remains in its details")));
    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    env.drain();env.publishAll();
    QVERIFY(!model.runtimeDialogVisible());
}

void NodeModelTests::manualStartupSummaryRejectsReentrantShutdown()
{
    MockNode node;
    ConfigureNodeModelDefaults(node);
    MempoolState mempool;
    ConfigureMempoolGetters(node, mempool);
    ManualNodeObservations env;
    auto services = env.services();
    NodeModel model{node, false, std::move(services)};
    const auto cleanup = qScopeGuard([&] {
        QObject::disconnect(&model, nullptr, this, nullptr);
        model.beginShutdown(); env.drain(); env.publishAll();
    });
    env.drain(); env.publishAll();
    QVERIFY(model.notificationsReady());
    model.addStartupWarnings({QStringLiteral("Original startup warning")});
    bool interrupted{false};
    QSignalSpy shutdown{&model, &NodeModel::requestedShutdown};
    QSignalSpy dialogs{&model, &NodeModel::runtimeDialogChanged};
    connect(&model, &NodeModel::networkBackendAvailabilityChanged, this, [&] {
        if (interrupted || !model.networkBackendAvailable()) return;
        interrupted = true;
        model.requestShutdown();
    });
    model.initializeResult(true, {}, false, false);
    QCOMPARE(shutdown.size(), 0);
    env.drain(); env.publishAll();
    QVERIFY(interrupted);
    QCOMPARE(shutdown.size(), 1);
    QCOMPARE(dialogs.size(), 0);
    QVERIFY(!model.runtimeDialogVisible());
    QVERIFY(!model.networkActionReady());
    QVERIFY(model.startupDiagnostics()->rowCount() > 0);
}

void NodeModelTests::manualInitializationStopsAfterPublicReentrancy_data()
{
    QTest::addColumn<int>("boundary");
    QTest::addColumn<int>("invalidation");
    const std::array<const char*, 29> boundaries{
        "readiness", "startup-dialog", "initial-height", "initial-remaining-time", "initial-verification", "initial-block-sync", "initial-header-sync",
        "initial-sync-complete", "chain-ready", "subscriptions-ready", "initialize-dispatch", "notification-overflow", "notification-tip-time",
        "notification-pause", "notification-bans", "notification-information", "startup-error", "startup-fault", "startup-shutdown", "notification-dialog",
        "subscription-network-ready", "notification-header", "notification-block-sync", "notification-verification", "notification-height",
        "notification-remaining-time", "startup-warning-intake-failure", "startup-error-intake-failure", "shutdown-polling"};
    const std::array<const char*, 5> invalidations{"request-shutdown", "stop-workers", "close-submitted", "delete-observer", "continue"};
    for (size_t boundary = 0; boundary < boundaries.size(); ++boundary) {
        for (size_t invalidation = 0; invalidation < invalidations.size(); ++invalidation) {
            const QByteArray name = QByteArray{boundaries[boundary]} + '-' + invalidations[invalidation];
            QTest::newRow(name.constData()) << int(boundary) << int(invalidation);
        }
    }
}

void NodeModelTests::manualInitializationStopsAfterPublicReentrancy()
{
    QFETCH(int, boundary);
    QFETCH(int, invalidation);
    MockNode node;
    ConfigureNodeModelDefaults(node);
    MempoolState mempool;
    ConfigureMempoolGetters(node, mempool);
    interfaces::Node::MessageBoxFn message_box;
    interfaces::Node::NotifyBlockTipFn block_tip;
    interfaces::Node::NotifyHeaderTipFn header_tip;
    interfaces::Node::NotifyNetworkActiveChangedFn network_active;
    interfaces::Node::BannedListChangedFn banned;
    node.handle_message_box_fn = [&](auto callback) { message_box = std::move(callback); return MakeNoopHandler(); };
    node.handle_notify_block_tip_fn = [&](auto callback) { block_tip = std::move(callback); return MakeNoopHandler(); };
    node.handle_notify_header_tip_fn = [&](auto callback) { header_tip = std::move(callback); return MakeNoopHandler(); };
    node.handle_notify_network_active_changed_fn = [&](auto callback) { network_active = std::move(callback); return MakeNoopHandler(); };
    node.handle_banned_list_changed_fn = [&](auto callback) { banned = std::move(callback); return MakeNoopHandler(); };
    ManualNodeObservations env;
    int64_t milliseconds{0};
    int scheduled{0};
    std::vector<std::function<void()>> deadlines;
    auto services = env.services();
    services.wall_seconds = [] { return int64_t{100'000}; };
    services.monotonic_milliseconds = [&] { return milliseconds; };
    services.schedule = [&](NodeModel::Poll, std::chrono::milliseconds, auto callback) { ++scheduled; deadlines.push_back(std::move(callback)); };
    services.cancel = [](NodeModel::Poll) {};
    auto model = std::make_unique<NodeModel>(node, false, std::move(services));
    const QPointer<NodeModel> original{model.get()};
    const auto cleanup = qScopeGuard([&] {
        // Test callbacks below borrow stack oracles. Detach them before the
        // early-declared cleanup guard can publish shutdown notifications.
        if (model) QObject::disconnect(model.get(), nullptr, this, nullptr);
        if (model) model->beginShutdown();
        model.reset(); env.drain(); env.publishAll();
    });
    const bool subscription_boundary = boundary == 9 || boundary == 10 || boundary == 20;
    if (!subscription_boundary) {
        env.drain(); env.publishAll();
        QVERIFY(model->notificationsReady());
        QVERIFY(message_box && block_tip && header_tip && network_active && banned);
    }
    if (boundary == 3 || boundary == 25) {
        model->setVerificationProgress(0.1);
        milliseconds = 5000;
        model->setVerificationProgress(0.2);
        QVERIFY(model->remainingSyncTime() > 0);
    }
    model->setMempoolInfoPollingActive(true);
    bool reached{false};
    int scheduled_at_boundary{-1}, chain_ready{0}, late_chain_ready{0}, initialized{0};
    bool synchronized_at_boundary{false};
    connect(model.get(), &NodeModel::chainStateReady, this, [&] {
        ++chain_ready;
        if (reached && invalidation != 4) ++late_chain_ready;
    });
    connect(model.get(), &NodeModel::nodeInitialized, this, [&] { ++initialized; });
    const auto invalidate = [&] {
        if (reached) return;
        reached = true;
        scheduled_at_boundary = scheduled;
        synchronized_at_boundary = model->initialSyncComplete();
        if (invalidation == 0) model->requestShutdown();
        else if (invalidation == 1) model->beginShutdown();
        else if (invalidation == 2) model->close();
        else if (invalidation == 3) model.reset();
    };
    switch (boundary) {
    case 0: case 20: connect(model.get(), &NodeModel::networkBackendAvailabilityChanged, this, invalidate); break;
    case 1: case 19: connect(model.get(), &NodeModel::runtimeDialogChanged, this, invalidate); break;
    case 2: case 24: connect(model.get(), &NodeModel::blockTipHeightChanged, this, invalidate); break;
    case 3: case 25: connect(model.get(), &NodeModel::remainingSyncTimeChanged, this, invalidate); break;
    case 4: case 23: connect(model.get(), &NodeModel::verificationProgressChanged, this, invalidate); break;
    case 5: case 22: connect(model.get(), &NodeModel::blockSyncActiveChanged, this, invalidate); break;
    case 6: case 21: connect(model.get(), &NodeModel::headerSyncChanged, this, invalidate); break;
    case 7: connect(model.get(), &NodeModel::initialSyncCompleteChanged, this, invalidate); break;
    case 8: connect(model.get(), &NodeModel::chainStateReady, this, invalidate); break;
    case 9: connect(model.get(), &NodeModel::notificationsReadyChanged, this, invalidate); break;
    case 10: connect(model.get(), &NodeModel::requestedInitialize, this, invalidate); break;
    case 11: case 26: case 27: connect(model.get(), &NodeModel::observationErrorChanged, this, invalidate); break;
    case 12: connect(model.get(), &NodeModel::blockTipTimeChanged, this, invalidate); break;
    case 13: connect(model.get(), &NodeModel::pauseChanged, this, invalidate); break;
    case 14: connect(model.get(), &NodeModel::bannedListChanged, this, invalidate); break;
    case 15: connect(model.get(), &NodeModel::nodeInformationChanged, this, invalidate); break;
    case 16: connect(model.get(), &NodeModel::startupErrorChanged, this, invalidate); break;
    case 17: connect(model.get(), &NodeModel::errorStateChanged, this, invalidate); break;
    case 18: connect(model.get(), &NodeModel::requestedShutdown, this, invalidate); break;
    case 28: connect(model.get(), &NodeModel::mempoolInfoPollingActiveChanged, this, invalidate); break;
    }
    if (subscription_boundary) {
        if (boundary == 10) model->startNodeInitializionThread();
        env.drain(); env.publishAll();
        QVERIFY(reached);
    }
    if (boundary == 28) {
        model->beginShutdown();
        QVERIFY(reached);
    }
    const bool failure = boundary == 16 || boundary == 17 || boundary == 18 || boundary == 26 || boundary == 27;
    const interfaces::BlockAndHeaderTipInfo tip{10, 100'000, boundary == 6 ? 100 : 10, boundary == 6 ? 0 : 100'000, 1.0};
    if (model && boundary != 28 && (!reached || invalidation == 4)) {
        if (boundary == 26 || boundary == 27) {
            model->addStartupWarnings({QString(startupdiagnostics::Collection::MAX_CHARS, QLatin1Char('s'))});
            message_box(Untranslated("New startup message"), boundary == 26 ? CClientUIInterface::ICON_WARNING : CClientUIInterface::ICON_ERROR);
            env.publishAll();
        } else model->addStartupWarnings({QStringLiteral("Original startup warning")});
        if (model && (!reached || invalidation == 4)) {
            if (boundary == 18) message_box(Untranslated("Original modal startup error"), CClientUIInterface::ICON_ERROR | CClientUIInterface::MODAL);
            model->initializeResult(!failure, tip, boundary == 5, false);
        }
        if (model && (!reached || invalidation == 4)) {
            if (boundary == 11) for (int index = 0; index < 33; ++index) message_box(Untranslated("Queued runtime message"), CClientUIInterface::ICON_INFORMATION);
            else if (boundary == 13) network_active(false);
            else if (boundary == 14) banned();
            else if (boundary == 19) message_box(Untranslated("Original runtime dialog"), CClientUIInterface::ICON_INFORMATION);
            else if (boundary == 21) header_tip(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{100, 0, uint256{}}, false);
            else if (boundary == 12 || boundary == 15 || boundary == 22 || boundary == 23 || boundary == 24 || boundary == 25) {
                block_tip(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{10, 100'000, uint256{}}, 1.0);
            }
            // Deliver callbacks while the original Native diagnostic projection
            // remains held. Readiness must respect the resulting invalidation.
            env.publishAll();
        }
    }
    env.drain(); env.publishAll();
    env.drain(); env.publishAll(); // Consume work admitted before the public invalidation.
    if(boundary==19&&invalidation==4){
        // This callback was captured after the initialization receipt. It is
        // an original Runtime prompt ahead of the startup summary, so its
        // continuation must be acknowledged before that summary can publish.
        QVERIFY(model->runtimeDialogVisible());
        QCOMPARE(model->runtimeDialogMessage(),QStringLiteral("Original runtime dialog"));
        model->answerRuntimeDialog(CClientUIInterface::BTN_OK);
        env.drain();env.publishAll();
    }
    QVERIFY(reached);
    QCOMPARE(late_chain_ready, 0);
    if (invalidation == 4) {
        QVERIFY(model);
        QCOMPARE(chain_ready, failure || boundary == 28 ? 0 : 1);
        QCOMPARE(initialized, boundary == 28 ? 0 : 1);
        QCOMPARE(model->networkActionReady(), !failure && boundary != 28);
    } else {
        QCOMPARE(chain_ready, boundary == 8 ? 1 : 0);
        QCOMPARE(initialized, boundary == 18 && invalidation == 0 ? 1 : 0);
        QCOMPARE(scheduled, scheduled_at_boundary);
        if (model) {
            QVERIFY(!model->networkActionReady());
            QCOMPARE(model->initialSyncComplete(), synchronized_at_boundary);
        } else QVERIFY(!original);
        const auto mempool_reads = node.calls.getMempoolSize.load();
        const auto status_reads = node.calls.getHeaderTip.load();
        // A regressing callback may append to the original deadline queue.
        // Iterate an owned snapshot so that the assertion remains valid.
        const std::vector<std::function<void()>> captured_deadlines{deadlines.begin(), deadlines.end()};
        for (const auto& callback : captured_deadlines) callback();
        env.drain(); env.publishAll();
        QCOMPARE(node.calls.getMempoolSize.load(), mempool_reads);
        QCOMPARE(node.calls.getHeaderTip.load(), status_reads);
        QCOMPARE(scheduled, scheduled_at_boundary);
    }
}

void NodeModelTests::runtimeMessageHandlerOpensAfterInitialization()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_message_box_fn = [&](interfaces::Node::MessageBoxFn fn) {
        message_box_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);
    model.initializeResult(true, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    std::atomic<bool> finished{false};
    int prompt_count{0};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        ++prompt_count;
        QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Error"));
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated runtime error"));
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK));
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        });
    });

    std::thread worker([&] {
        message_box_fn(
            bilingual_str{"Runtime error", "Translated runtime error"},
            CClientUIInterface::MSG_ERROR);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    // The backend answer and the queued GUI dismissal are separate events.
    QTRY_VERIFY_WITH_TIMEOUT(!model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
    QCOMPARE(prompt_count, 1);
}

void NodeModelTests::runtimeQuestionUnsupportedAnswerCannotConsumeThePendingQuestion()
{
    MockNode node;MempoolState mempool;ConfigureNodeModelDefaults(node);ConfigureMempoolGetters(node,mempool);
    interfaces::Node::QuestionFn question;
    node.handle_question_fn=[&](auto callback){question=std::move(callback);return MakeNoopHandler();};
    ManualNodeObservations env;
    NodeModel model{node,false,env.services()};
    const auto pump=[&]{for(int turn=0;turn<8;++turn){env.drain();env.publishAll();}};
    pump();QVERIFY(model.notificationsReady());model.initializeResult(true,{},false,false);pump();QVERIFY(question);
    std::atomic_bool returned{false},answer{false};
    std::thread native{[&]{answer=question(Untranslated("Original OK-only question"),"",CClientUIInterface::MSG_ERROR);returned=true;}};
    const auto cleanup=qScopeGuard([&]{model.beginShutdown();pump();native.join();pump();});
    QTRY_VERIFY_WITH_TIMEOUT(([&]{pump();return model.runtimeDialogVisible();})(),ASYNC_TIMEOUT_MS);
    QCOMPARE(model.runtimeDialogButtons(),static_cast<unsigned int>(CClientUIInterface::BTN_OK));
    model.answerRuntimeDialog(CClientUIInterface::BTN_YES);pump();
    QVERIFY(model.runtimeDialogVisible());QVERIFY(!returned.load()); // Independent unchanged public-action oracle.
    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);pump();
    QTRY_VERIFY_WITH_TIMEOUT(([&]{pump();return returned.load();})(),ASYNC_TIMEOUT_MS);QVERIFY(answer.load());
    QTRY_VERIFY_WITH_TIMEOUT(([&]{pump();return !model.runtimeDialogVisible();})(),ASYNC_TIMEOUT_MS);
}

void NodeModelTests::runtimeQuestionHandlerBlocksForAnswerAndReturnsResult()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::QuestionFn question_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_question_fn = [&](interfaces::Node::QuestionFn fn) {
        question_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(question_fn);

    std::atomic<bool> result{false};
    std::atomic<bool> finished{false};
    int prompt_count{0};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        ++prompt_count;
        QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Error"));
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated rebuild?"));
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK | CClientUIInterface::BTN_ABORT));
        QVERIFY(model.runtimeDialogQuestion());
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        });
    });

    std::thread worker([&] {
        result = question_fn(
            bilingual_str{"Rebuild?", "Translated rebuild?"},
            "Non interactive",
            CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QTRY_VERIFY_WITH_TIMEOUT(!model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
    QCOMPARE(prompt_count, 1);
    QVERIFY(result.load());
}

void NodeModelTests::closingCancelsAWaitingRuntimeQuestionBeforeWorkerDrain()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    interfaces::Node::QuestionFn question;
    node.handle_question_fn = [&](interfaces::Node::QuestionFn fn) {
        question = std::move(fn);
        return MakeNoopHandler();
    };
    NodeModel model{node};
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    QVERIFY(question);

    std::atomic_bool returned{false};
    std::atomic_bool answer{true};
    std::thread native{[&] {
        answer = question(Untranslated("Waiting runtime question"), "", CClientUIInterface::MSG_ERROR);
        returned = true;
    }};
    const auto release = qScopeGuard([&] {
        DrainNodeModel(model);
        native.join();
    });
    QTRY_VERIFY_WITH_TIMEOUT(model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
    QVERIFY(!returned.load());

    model.close();
    QTRY_VERIFY_WITH_TIMEOUT(returned.load(), ASYNC_TIMEOUT_MS);
    QVERIFY(!answer.load());
}

void NodeModelTests::runtimeStartupQuestionFailureLetsInitializeResultRequestShutdown()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::QuestionFn question_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_question_fn = [&](interfaces::Node::QuestionFn fn) {
        question_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(question_fn);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy faulted_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy startup_error_spy{&model, &NodeModel::startupErrorChanged};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};

    std::atomic<bool> result{true};
    std::atomic<bool> finished{false};
    int prompt_count{0};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        ++prompt_count;
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated rebuild?"));
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK | CClientUIInterface::BTN_ABORT));
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_ABORT);
        });
    });

    std::thread worker([&] {
        result = question_fn(
            bilingual_str{"Rebuild?", "Translated rebuild?"},
            "Non interactive",
            CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QTRY_VERIFY_WITH_TIMEOUT(!model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
    QCOMPARE(prompt_count, 1);
    QVERIFY(!result.load());
    QCOMPARE(shutdown_spy.count(), 0);

    model.initializeResult(false, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(initialized_spy.count(), 1);
    QCOMPARE(faulted_spy.count(), 0);
    QCOMPARE(startup_error_spy.count(), 0);
    QVERIFY(!model.errorState());
    QVERIFY(model.startupError().isEmpty());
}

void NodeModelTests::runtimeStartupErrorDialogLetsInitializeResultRequestShutdown()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_message_box_fn = [&](interfaces::Node::MessageBoxFn fn) {
        message_box_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy faulted_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy startup_error_spy{&model, &NodeModel::startupErrorChanged};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};

    std::atomic<bool> finished{false};
    int prompt_count{0};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        ++prompt_count;
        QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Error"));
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated failed to initialize"));
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK));
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        });
    });

    std::thread worker([&] {
        message_box_fn(
            bilingual_str{"Failed to initialize", "Translated failed to initialize"},
            CClientUIInterface::MSG_ERROR);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QTRY_VERIFY_WITH_TIMEOUT(!model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
    QCOMPARE(prompt_count, 1);
    QCOMPARE(shutdown_spy.count(), 0);

    model.initializeResult(false, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(initialized_spy.count(), 1);
    QCOMPARE(faulted_spy.count(), 0);
    QCOMPARE(startup_error_spy.count(), 0);
    QVERIFY(!model.errorState());
    QVERIFY(model.startupError().isEmpty());
}

void NodeModelTests::runtimeDialogDefaultsToOkWhenNoButtonsAreSpecified()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_message_box_fn = [&](interfaces::Node::MessageBoxFn fn) {
        message_box_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);
    model.initializeResult(true, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    std::atomic<bool> finished{false};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK));
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        });
    });

    std::thread worker([&] {
        message_box_fn(
            bilingual_str{"Information", "Translated information"},
            CClientUIInterface::ICON_INFORMATION | CClientUIInterface::MODAL);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QTRY_VERIFY_WITH_TIMEOUT(!model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
}

void NodeModelTests::runtimeDialogExposesFullCoreButtonMask()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_message_box_fn = [&](interfaces::Node::MessageBoxFn fn) {
        message_box_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);
    model.initializeResult(true, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    const unsigned int full_button_mask{CClientUIInterface::BTN_MASK};
    std::atomic<bool> finished{false};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        QCOMPARE(model.runtimeDialogButtons(), full_button_mask);
        QVERIFY(model.runtimeDialogButtons() & CClientUIInterface::BTN_IGNORE);
        QVERIFY(model.runtimeDialogButtons() & CClientUIInterface::BTN_HELP);
        QVERIFY(model.runtimeDialogButtons() & CClientUIInterface::BTN_RESET);
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_RESET);
        });
    });

    std::thread worker([&] {
        message_box_fn(
            bilingual_str{"Full button mask", "Translated full button mask"},
            CClientUIInterface::ICON_WARNING | CClientUIInterface::MODAL | full_button_mask);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QTRY_VERIFY_WITH_TIMEOUT(!model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
}

void NodeModelTests::runtimeBlockingDialogsAreQueued()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::QuestionFn question_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_question_fn = [&](interfaces::Node::QuestionFn fn) {
        question_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(question_fn);
    model.initializeResult(true, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QStringList prompts;
    std::atomic<bool> first_result{false}, second_result{true};
    std::atomic<bool> first_done{false}, second_done{false};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (model.runtimeDialogVisible()) prompts.push_back(model.runtimeDialogMessage());
    });
    std::thread first([&] {
        first_result = question_fn(bilingual_str{"First?", "Translated first?"}, "Non interactive", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
        first_done = true;
    });
    QTRY_COMPARE_WITH_TIMEOUT(model.runtimeDialogMessage(), QStringLiteral("Translated first?"), ASYNC_TIMEOUT_MS);
    std::thread second([&] {
        second_result = question_fn(bilingual_str{"Second?", "Translated second?"}, "Non interactive", CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
        second_done = true;
    });
    bool heartbeat{false}; QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY_WITH_TIMEOUT(heartbeat, ASYNC_TIMEOUT_MS);
    QVERIFY(!first_done); QVERIFY(!second_done);
    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    QTRY_COMPARE_WITH_TIMEOUT(model.runtimeDialogMessage(), QStringLiteral("Translated second?"), ASYNC_TIMEOUT_MS);
    model.answerRuntimeDialog(CClientUIInterface::BTN_ABORT);
    QTRY_VERIFY_WITH_TIMEOUT(first_done.load() && second_done.load(), ASYNC_TIMEOUT_MS);
    first.join(); second.join();
    QTRY_VERIFY_WITH_TIMEOUT(!model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
    QCOMPARE(prompts, QStringList({QStringLiteral("Translated first?"), QStringLiteral("Translated second?")}));
    QVERIFY(first_result); QVERIFY(!second_result);

}

void NodeModelTests::runtimeNonBlockingDialogsAreQueued()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.handle_message_box_fn = [&](interfaces::Node::MessageBoxFn fn) {
        message_box_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);
    model.initializeResult(true, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QSignalSpy runtime_dialog_spy{&model, &NodeModel::runtimeDialogChanged};
    QStringList presented;
    connect(&model,&NodeModel::runtimeDialogChanged,this,[&]{
        if(model.runtimeDialogVisible())presented.push_back(model.runtimeDialogMessage());
    });
    const auto disconnect=qScopeGuard([&]{QObject::disconnect(&model,nullptr,this,nullptr);});
    message_box_fn(
        bilingual_str{"First", "Translated first"},
        CClientUIInterface::ICON_INFORMATION);
    QTRY_COMPARE_WITH_TIMEOUT(runtime_dialog_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated first"));

    message_box_fn(
        bilingual_str{"Second", "Translated second"},
        CClientUIInterface::ICON_WARNING);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(runtime_dialog_spy.count(), 1);
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated first"));

    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    QTRY_COMPARE_WITH_TIMEOUT(model.runtimeDialogMessage(), QStringLiteral("Translated second"), ASYNC_TIMEOUT_MS);
    QVERIFY(model.runtimeDialogVisible());

    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    QTRY_VERIFY_WITH_TIMEOUT(!model.runtimeDialogVisible(), ASYNC_TIMEOUT_MS);
    QCOMPARE(presented,QStringList({QStringLiteral("Translated first"),QStringLiteral("Translated second")}));
}

void NodeModelTests::initializeFailureShowsStartupWarningsWithoutMakingThemCurrentWarnings()
{
    MockNode node;
    MempoolState mempool;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.get_warnings_fn = [] { return bilingual_str{"pre-release warning", "Translated pre-release warning"}; };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    model.addStartupWarnings({QStringLiteral("Translated startup warning")});
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy faulted_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy startup_error_spy{&model, &NodeModel::startupErrorChanged};
    model.initializeResult(false, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QCOMPARE(faulted_spy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(startup_error_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QVERIFY(model.errorState());
    QTRY_COMPARE_WITH_TIMEOUT(model.warningList(), QStringList({QStringLiteral("Translated pre-release warning")}), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.startupError(), QStringLiteral("Startup warnings:\nTranslated startup warning\n\nNode initialization failed."));
}

void NodeModelTests::initializeFailureUsesNodeErrorMessages()
{
    MockNode node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.get_warnings_fn = [] { return bilingual_str{"pre-release warning", "Translated pre-release warning"}; };
    node.handle_message_box_fn = [&](interfaces::Node::MessageBoxFn fn) {
        message_box_fn = std::move(fn);
        return MakeNoopHandler();
    };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);

    QSignalSpy runtime_dialog_spy{&model, &NodeModel::runtimeDialogChanged};

    std::atomic<bool> finished{false};
    std::thread worker([&] {
        message_box_fn(
            bilingual_str{"Unable to bind original", "Translated unable to bind"},
            CClientUIInterface::ICON_ERROR);
        message_box_fn(
            bilingual_str{"Failed to listen original", "Translated failed to listen"},
            CClientUIInterface::ICON_ERROR);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();
    QCoreApplication::processEvents(QEventLoop::AllEvents, ASYNC_TIMEOUT_MS);
    QCOMPARE(runtime_dialog_spy.count(), 0);

    QSignalSpy faulted_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy startup_error_spy{&model, &NodeModel::startupErrorChanged};
    model.initializeResult(false, {});
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QCOMPARE(faulted_spy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(startup_error_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QVERIFY(model.errorState());
    QTRY_COMPARE_WITH_TIMEOUT(model.warningList(), QStringList({QStringLiteral("Translated pre-release warning")}), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.startupError(), QStringLiteral("Translated unable to bind\n\nTranslated failed to listen"));
}

void NodeModelTests::runawayExceptionSetsFatalStartupError()
{
    MockNode node;
    MempoolState mempool;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    model.handleRunawayException(QStringLiteral("std::runtime_error: boom"));
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QVERIFY(model.errorState());
    QVERIFY(model.fatalException());
    QVERIFY(model.startupError().contains(QStringLiteral("can no longer continue safely")));
    QVERIFY(model.startupError().endsWith(QStringLiteral("std::runtime_error: boom")));
}

void NodeModelTests::fatalExceptionExitsWithoutDraining_data()
{
    QTest::addColumn<QString>("phase");
    QTest::addColumn<bool>("native_quit");
    for (const auto* phase : {"initialize", "interrupt", "shutdown", "unknown", "empty", "pending-poll"}) {
        for (const bool native_quit : {false, true}) {
            const QByteArray tag = QByteArray{phase} + (native_quit ? "-quit" : "-dialog");
            QTest::newRow(tag.constData()) << QString::fromLatin1(phase) << native_quit;
        }
    }
}

void NodeModelTests::fatalExceptionExitsWithoutDraining()
{
    QFETCH(QString, phase);
    QFETCH(bool, native_quit);
    if (!qEnvironmentVariableIsSet("BITCOIN_QML_FATAL_EXCEPTION_CHILD")) {
        QSKIP("Executed by bitcoinqml_fatal_exception_tests in child processes.");
    }
    QCOMPARE(std::atexit([] { std::_Exit(93); }), 0);

    MockNode node;
    ConfigureNodeModelDefaults(node);
    std::atomic_bool core_shutdown{false};
    const auto fail = [&]() -> void {
        core_shutdown = true;
        if (phase == "unknown") throw 42;
        throw std::runtime_error(phase == "empty" ? "" : "fatal test exception");
    };
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) -> bool { fail(); return false; };
    node.start_shutdown_fn = [&] { if (phase == "interrupt") fail(); };
    node.app_shutdown_fn = [&] { if (phase == "shutdown") fail(); };
    QSemaphore poll_entered, release_poll;
    node.shutdown_requested_fn = [&] {
        if (phase == "pending-poll") {
            poll_entered.release();
            release_poll.acquire();
        }
        return core_shutdown.load();
    };

    const bool backend_ready{false};
    NodeModel model{node, backend_ready};
    execution::Executor worker{execution::Executor::Config{}};
    QmlInitExecutor executor{node, std::make_shared<execution::SerialQueue>(worker.makeQueue(execution::Lane::Priority))};
    QmlShutdownCoordinator coordinator{executor};
    QmlQuitHandler quit_handler;
    connect(&quit_handler, &QmlQuitHandler::quitRequested, &model, &NodeModel::requestShutdown);
    connect(&model, &NodeModel::requestedShutdown, &coordinator, &QmlShutdownCoordinator::requestShutdown);
    connect(&executor, &QmlInitExecutor::runawayException, &model, &NodeModel::handleRunawayException);
    connect(&coordinator, &QmlShutdownCoordinator::finished, this, [] { std::_Exit(86); });

    QSemaphore entered, release;
    auto stalled = worker.makeQueue(execution::Lane::Utility);
    if (!stalled.post([&] { entered.release(); release.acquire(); })) std::_Exit(87);
    if (!entered.tryAcquire(1, 5'000)) std::_Exit(87);
    const auto request_quit = [] {
        QEvent quit{QEvent::Quit};
        QCoreApplication::sendEvent(QCoreApplication::instance(), &quit);
    };
    connect(&model, &NodeModel::startupErrorChanged, this, [&] {
        const QString detail = phase == "unknown" ? QStringLiteral("Unknown exception")
            : phase == "empty" ? QString{} : QStringLiteral("fatal test exception");
        if (!model.fatalException() || !model.errorState()
            || !model.startupError().contains(QStringLiteral("can no longer continue safely"))
            || !model.startupError().contains(detail)) std::_Exit(88);
        // An in-flight poll must not acknowledge the fatal notification when
        // its reply arrives after shutdown polling has been stopped.
        if (phase == "pending-poll") release_poll.release();
        constexpr int acknowledgement_after_shutdown_poll_ms{300};
        QTimer::singleShot(acknowledgement_after_shutdown_poll_ms, this, [&] {
            if (phase == "interrupt" && node.calls.appShutdown.load() != 0) std::_Exit(89);
            std::puts("fatal notification acknowledged");
            std::fflush(stdout);
            if (native_quit) request_quit();
            else model.requestShutdown();
            std::_Exit(90);
        });
    });
    model.startShutdownPolling();
    if (phase == "pending-poll") {
        QTRY_VERIFY_WITH_TIMEOUT(poll_entered.available() > 0, ASYNC_TIMEOUT_MS);
    }
    if (phase == "interrupt" || phase == "shutdown") {
        if (native_quit) request_quit();
        else model.requestShutdown();
    } else {
        executor.initialize();
    }
    QTimer::singleShot(5'000, this, [] { std::_Exit(91); });
    QEventLoop loop;
    loop.exec();
    std::_Exit(92);
}

void NodeModelTests::nodeInformationRowsAvoidChainmanBeforeInitialization()
{
    MockNode node;
    MempoolState mempool;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.ExpectNoCalls(node.calls.getHeaderTip);
    node.ExpectNoCalls(node.calls.getNumBlocks);
    node.ExpectNoCalls(node.calls.getLastBlockTime);
    node.ExpectNoCalls(node.calls.getNetLocalAddresses);
    node.ExpectExactly(node.calls.getNetworkActive, 1);

    ManualNodeObservations env;
    NodeModel model{node, true, env.services()};
    const auto drain = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    // Hold the actual initial Core reads. Before an observation, network state
    // remains Unknown and no GUI information getter may touch any backend.
    QCOMPARE(node.calls.getNetworkActive.load(), 0);

    const QVariantList rows = model.nodeInformationRows();
    QVERIFY(!rows.empty());

    bool saw_unknown_network_active{false};
    for (const QVariant& row : rows) {
        const QVariantMap map{row.toMap()};
        saw_unknown_network_active |=
            map.value(QStringLiteral("label")).toString() == QStringLiteral("Network active") &&
            map.value(QStringLiteral("value")).toString() == QStringLiteral("Unknown");
    }
    QVERIFY(saw_unknown_network_active);
    QCOMPARE(node.calls.getNetworkActive.load(), 0);
    QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(model.notificationsReady());
    QCOMPARE(InformationValue(model, QStringLiteral("Network active")), QStringLiteral("No"));
    // R36 independently reads safe network availability; Chainman-dependent
    // getters above must still remain unused until initialization succeeds.
    QCOMPARE(node.calls.getNetworkActive.load(), 1);
}

void NodeModelTests::nodeInformationRowsExposeDiagnostics()
{
    MockNode node;
    MempoolState mempool;
    PeerCountState peers;

    peers.total = 3;
    peers.inbound = 1;
    peers.outbound = 2;

    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    ConfigurePeerCountGetters(node, peers);
    node.get_num_blocks_fn = [] { return 321; };
    node.get_header_tip_fn = [](int& height, int64_t& block_time) {
        height = 333;
        block_time = 1'700'000'333;
        return true;
    };
    node.get_last_block_time_fn = [] { return int64_t{1'700'000'321}; };
    node.get_network_active_fn = [] { return true; };
    node.get_net_local_addresses_fn = [] { return std::map<CNetAddr, LocalServiceInfo>{}; };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 320,
        .block_time = 1'700'000'321,
        .header_height = 333,
        .header_time = 1'700'000'333,
        .verification_progress = 0.5,
    });
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);

    QTRY_COMPARE_WITH_TIMEOUT(model.numPeers(), 3, ASYNC_TIMEOUT_MS);
    const QVariantList rows = model.nodeInformationRows();
    QVERIFY(!rows.empty());

    bool saw_network_active{false};
    bool saw_peer_counts{false};
    for (const QVariant& row : rows) {
        const QVariantMap map{row.toMap()};
        const QString value{map.value(QStringLiteral("value")).toString()};
        saw_network_active |= value == QStringLiteral("Yes");
        saw_peer_counts |= value == QStringLiteral("3 total (1 inbound, 2 outbound)");
    }
    QVERIFY(saw_network_active);
    QVERIFY(saw_peer_counts);
}

// #963: reopening Node information while its status read is held shows the
// cached rows at once and adds no backend work; a failed read keeps them.
void NodeModelTests::informationRowsReopenFromCacheWhileStatusReadIsHeld()
{
    MockNode node; ManualTipState tips; ConfigureManualTips(node, tips);
    std::atomic_bool fail{false};
    node.get_warnings_fn = [&] { if (fail) throw std::runtime_error("Held status read failed"); return bilingual_str{}; };
    ManualNodeObservations env;
    NodeModel model{node, false, ManualTipServices(env, tips)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.notificationsReady()); QVERIFY(tips.block_tip);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = tips.block, .block_time = tips.block_time,
        .header_height = tips.header, .header_time = tips.header_time, .verification_progress = 1.0,
    }, false, false);
    QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.observationError().isEmpty());
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("200"));
    const auto backend = [&] {
        return node.calls.getHeaderTip.load() + node.calls.getNumBlocks.load() + node.calls.getLastBlockTime.load() + node.calls.getNetLocalAddresses.load()
            + node.calls.getNetworkActive.load() + node.calls.getWarnings.load() + node.calls.getNodeCount.load();
    };
    // Core moves on; the block callback queues a status read that stays held.
    ++tips.block; ++tips.block_time; tips.header = 210; tips.header_time += 10;
    tips.block_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{tips.block, tips.block_time, uint256{}}, 1.0);
    env.publishAll(); QVERIFY(!env.turnsIdle());
    const auto calls = backend(); const auto posted = env.coreSubmissions();
    const auto warning_reads = node.calls.getWarnings.load();
    const auto shown = model.nodeInformationRows(); QVERIFY(!shown.isEmpty());
    for (int open = 0; open < 64; ++open) QCOMPARE(model.nodeInformationRows(), shown);
    QCOMPARE(backend(), calls); QCOMPARE(env.coreSubmissions(), posted);
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("200"));
    // The held read fails: report it and keep the previous facts.
    fail = true; QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(node.calls.getWarnings.load(), warning_reads + 1);
    QVERIFY(!model.observationError().isEmpty()); QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("200"));
    // The next completed read replaces both the error and the stale rows.
    fail = false; ++tips.block; ++tips.block_time;
    tips.block_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{tips.block, tips.block_time, uint256{}}, 1.0);
    QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(model.observationError().isEmpty()); QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("210"));
}

void NodeModelTests::manualRewindUsesCurrentHeaderReadback_data()
{
    QTest::addColumn<int>("depth");
    QTest::addColumn<bool>("invalidate_header");
    QTest::addColumn<bool>("header_available");
    QTest::newRow("shallow-invalidation") << 20 << true << true;
    QTest::newRow("deep-invalidation") << 30 << true << true;
    QTest::newRow("valid-header-rewind-control") << 30 << false << true;
    QTest::newRow("unavailable-header-control") << 30 << true << false;
}

void NodeModelTests::manualRewindUsesCurrentHeaderReadback()
{
    QFETCH(int, depth); QFETCH(bool, invalidate_header); QFETCH(bool, header_available);
    MockNode node; ManualTipState tips; ConfigureManualTips(node, tips);
    ManualNodeObservations env;
    NodeModel model{node, false, ManualTipServices(env, tips)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.notificationsReady()); QVERIFY(tips.block_tip);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = tips.block, .block_time = tips.block_time,
        .header_height = tips.header, .header_time = tips.header_time, .verification_progress = 1.0,
    }, false, false);
    QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.initialSyncComplete());
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("200"));
    const auto getter_calls = node.calls.getHeaderTip.load();
    tips.block -= depth; tips.block_time -= depth;
    if (invalidate_header) { tips.header = tips.block; tips.header_time = tips.block_time; }
    tips.header_available = header_available;
    tips.block_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{tips.block, tips.block_time, uint256{}}, 0.9999);
    env.publishAll(); // Publish the actual block callback; the read is held.
    QCOMPARE(model.blockTipHeight(), tips.block);
    QCOMPARE(node.calls.getHeaderTip.load(), getter_calls); // No Core getter on GUI.
    QCOMPARE(model.initialSyncComplete(), depth <= 24);
    QVERIFY(DrainManualNodeTurns(env));
    const int observed_header = header_available ? tips.header : 200;
    const auto observed_time = header_available ? tips.header_time : tips.now;
    QCOMPARE(InformationValue(model, "Header height"), QString::number(observed_header));
    QCOMPARE(InformationValue(model, "Block height"), QString::number(tips.block));
    QCOMPARE(InformationValue(model, "Header time"), QLocale{}.toString(QDateTime::fromSecsSinceEpoch(observed_time), QLocale::ShortFormat));
    QCOMPARE(InformationValue(model, "Last block time"), QLocale{}.toString(QDateTime::fromSecsSinceEpoch(tips.block_time), QLocale::ShortFormat));
    QCOMPARE(model.initialSyncComplete(), observed_header == tips.block);
}

void NodeModelTests::manualBlockRowsRejectOlderStatusRead_data()
{
    QTest::addColumn<bool>("domain_first");
    QTest::newRow("old-result-before-notification") << true;
    QTest::newRow("notification-before-old-result") << false;
}

void NodeModelTests::manualBlockRowsRejectOlderStatusRead()
{
    QFETCH(bool, domain_first);
    MockNode node; ManualTipState tips; ConfigureManualTips(node, tips);
    ManualNodeObservations env;
    NodeModel model{node, false, ManualTipServices(env, tips)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    QVERIFY(DrainManualNodeTurns(env)); QVERIFY(tips.block_tip); QVERIFY(tips.peers);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = tips.block, .block_time = tips.block_time,
        .header_height = tips.header, .header_time = tips.header_time, .verification_progress = 1.0,
    }, false, false);
    QVERIFY(DrainManualNodeTurns(env));
    tips.peers(1); env.publishAll();
    QVERIFY(env.step(execution::Lane::Core)); // Native status read owns the old 200/200 facts.
    if (domain_first) QVERIFY(env.step(execution::Lane::Utility));
    tips.block = tips.header = 170; tips.block_time = tips.header_time = tips.now - 30;
    tips.block_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{170, tips.block_time, uint256{}}, 0.9999);
    env.publishAll();
    if (!domain_first) { QVERIFY(env.step(execution::Lane::Utility)); env.publishAll(); }
    QCOMPARE(model.blockTipHeight(), 170);
    // The later read has not run: an information consumer must already see
    // the downgoing public notification, rather than the maximum of two ages.
    QVERIFY(!env.turnsIdle());
    const auto getter_calls = node.calls.getNumBlocks.load();
    QCOMPARE(InformationValue(model, "Block height"), QStringLiteral("170"));
    QCOMPARE(InformationValue(model, "Last block time"), QLocale{}.toString(QDateTime::fromSecsSinceEpoch(tips.block_time), QLocale::ShortFormat));
    QCOMPARE(node.calls.getNumBlocks.load(), getter_calls);
    QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(InformationValue(model, "Block height"), QStringLiteral("170"));
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("170"));
    QVERIFY(model.initialSyncComplete());
}

void NodeModelTests::manualReadbackDoesNotReplayPendingTip()
{
    MockNode node; ManualTipState tips; ConfigureManualTips(node, tips);
    ManualNodeObservations env;
    NodeModel model{node, false, ManualTipServices(env, tips)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    QVERIFY(DrainManualNodeTurns(env)); QVERIFY(tips.block_tip); QVERIFY(tips.peers);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = tips.block, .block_time = tips.block_time,
        .header_height = tips.header, .header_time = tips.header_time, .verification_progress = 1.0,
    }, true, false);
    QVERIFY(DrainManualNodeTurns(env));
    QSignalSpy times{&model, &NodeModel::blockTipTimeChanged};
    tips.peers(1); env.publishAll(); // Admit a read, but hold Native execution.
    tips.block_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{210, tips.now + 1, uint256{}}, 0.75);
    auto old_notification = env.takePublicationBack(); QVERIFY(old_notification);
    tips.block = tips.header = 220; tips.block_time = tips.header_time = tips.now + 2;
    env.drain(); env.publishAll(); // The public getters sample after callback210.
    QCOMPARE(model.blockTipHeight(), 220);
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("220"));
    const auto getter_calls = node.calls.getHeaderTip.load();
    old_notification(); // Deliver the original retained GUI callback unchanged.
    QCOMPARE(model.blockTipHeight(), 220);
    QCOMPARE(InformationValue(model, "Block height"), QStringLiteral("220"));
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("220"));
    QCOMPARE(node.calls.getHeaderTip.load(), getter_calls);
    QVERIFY(!times.isEmpty()); QCOMPARE(times.last().at(0).toLongLong(), tips.block_time);
    QVERIFY(!model.blockSyncActive());
    QCOMPARE(model.verificationProgress(), 0.75);
    QVERIFY(model.initialSyncComplete());
}

void NodeModelTests::manualReadbackPreservesPresync()
{
    MockNode node; ManualTipState tips; ConfigureManualTips(node, tips);
    ManualNodeObservations env;
    NodeModel model{node, false, ManualTipServices(env, tips)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    QVERIFY(DrainManualNodeTurns(env)); QVERIFY(tips.header_tip); QVERIFY(tips.peers);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = tips.block, .block_time = tips.block_time,
        .header_height = tips.header, .header_time = tips.header_time, .verification_progress = 1.0,
    }, false, false);
    QVERIFY(DrainManualNodeTurns(env));
    tips.header_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{300, tips.now, uint256{}}, true);
    tips.peers(7); QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(model.headerPresync()); QVERIFY(!model.initialSyncComplete());
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("300"));
    QCOMPARE(model.numPeers(), 7); // Unrelated status still advances.
    tips.header_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{200, tips.now, uint256{}}, false);
    QVERIFY(DrainManualNodeTurns(env));
    QVERIFY(!model.headerPresync()); QVERIFY(model.initialSyncComplete());
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("200"));
}

void NodeModelTests::manualHeaderProjectionCannotOverwriteReentrantBlockTip()
{
    MockNode node; ManualTipState tips; ConfigureManualTips(node, tips);
    ManualNodeObservations env;
    NodeModel model{node, false, ManualTipServices(env, tips)};
    const auto cleanup = qScopeGuard([&] { model.beginShutdown(); QVERIFY(DrainManualNodeTurns(env)); QVERIFY(model.isDrained()); });
    QVERIFY(DrainManualNodeTurns(env)); QVERIFY(tips.block_tip); QVERIFY(tips.peers);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = tips.block, .block_time = tips.block_time,
        .header_height = tips.header, .header_time = tips.header_time, .verification_progress = 1.0,
    }, false, false);
    QVERIFY(DrainManualNodeTurns(env));
    bool reentered{false};
    connect(&model, &NodeModel::headerSyncChanged, &model, [&] {
        if (std::exchange(reentered, true)) return;
        tips.block = tips.header = 150; tips.block_time = tips.header_time = tips.now;
        tips.block_tip(SynchronizationState::POST_INIT, interfaces::BlockTip{150, tips.now, uint256{}}, 0.9);
        env.publishAll(); // Deliver the genuine public callback at this signal.
    });
    tips.block = tips.header = 300; tips.header_time = tips.now - 100 * 600;
    tips.peers(1); env.publishAll(); env.drain(); env.publishAll();
    QVERIFY(reentered); QCOMPARE(model.blockTipHeight(), 150);
    QCOMPARE(InformationValue(model, "Block height"), QStringLiteral("150"));
    QVERIFY(!env.turnsIdle()); // The newer query is admitted, not executed yet.
    QVERIFY(DrainManualNodeTurns(env));
    QCOMPARE(InformationValue(model, "Header height"), QStringLiteral("150"));
    QCOMPARE(model.blockTipHeight(), 150); QVERIFY(model.initialSyncComplete());
}

void NodeModelTests::initEmitsRequestedInitialize()
{
    MockNode node;
    ConfigureNodeModelDefaults(node);
    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);

    QSignalSpy spy{&model, &NodeModel::requestedInitialize};
    model.startNodeInitializionThread();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, ASYNC_TIMEOUT_MS);
}

void NodeModelTests::initGuardBlocksSecondEmission()
{
    MockNode node;
    ConfigureNodeModelDefaults(node);
    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);

    QSignalSpy spy{&model, &NodeModel::requestedInitialize};
    model.startNodeInitializionThread();
    model.startNodeInitializionThread();
    QTRY_COMPARE_WITH_TIMEOUT(spy.count(), 1, ASYNC_TIMEOUT_MS);
}

void NodeModelTests::shutdownPollingOnlyRequestsLifecycleControl()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    node.shutdown_requested_fn = [] { return true; };

    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.ExpectExactly(node.calls.startShutdown, 0);

    model.startShutdownPolling();

    QTRY_COMPARE_WITH_TIMEOUT(shutdown_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(node.calls.startShutdown.load(), 0);

    model.requestShutdown();
    QCOMPARE(shutdown_spy.count(), 1);
}


void NodeModelTests::latestNotificationsSurviveStaleInitializationAndReorg()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    interfaces::Node::NotifyBlockTipFn block;
    interfaces::Node::NotifyHeaderTipFn header;
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn callback) {
        block = std::move(callback);
        return MakeNoopHandler();
    };
    node.handle_notify_header_tip_fn = [&](interfaces::Node::NotifyHeaderTipFn callback) {
        header = std::move(callback);
        return MakeNoopHandler();
    };
    NodeModel model{node, false};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    QVERIFY(block);
    QVERIFY(header);
    const auto now = GetTime();
    block(SynchronizationState::POST_INIT, interfaces::BlockTip{200, now, uint256{1}}, 0.9);
    header(SynchronizationState::POST_INIT, interfaces::BlockTip{200, now, uint256{1}}, false);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 100, .block_time = now - 100,
        .header_height = 100, .header_time = now - 100,
        .verification_progress = 0.1,
    }, true, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.blockTipHeight(), 200);
    QCOMPARE(model.verificationProgress(), 0.9);
    QVERIFY(!model.blockSyncActive());
    QCOMPARE(InformationValue(model, QStringLiteral("Header height")), QStringLiteral("200"));
    QSignalSpy times{&model, &NodeModel::blockTipTimeChanged};
    for (int id = 2; id < 10'002; ++id) {
        block(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{200, now + id, uint256{static_cast<uint8_t>(id)}}, 0.8);
    }
    block(SynchronizationState::POST_INIT, interfaces::BlockTip{199, now + 10, uint256{10}}, 0.7);
    QTRY_COMPARE_WITH_TIMEOUT(model.blockTipHeight(), 199, ASYNC_TIMEOUT_MS);
    QCOMPARE(times.count(), 1);
    QCOMPARE(times.at(0).at(0).toLongLong(), now + 10);
    QCOMPARE(model.verificationProgress(), 0.7);
    QVERIFY(!model.blockSyncActive());
    model.initializeResult(true, {}, true, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.blockTipHeight(), 199);
    QCOMPARE(model.verificationProgress(), 0.7);
    QVERIFY(!model.blockSyncActive());
    QCOMPARE(node.calls.isInitialBlockDownload.load(), 0);
    QCOMPARE(node.calls.shutdownRequested.load(), 0);
}


void NodeModelTests::caughtUpInitializationClearsEarlierInitialDownloadNotification()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    interfaces::Node::NotifyBlockTipFn block;
    node.handle_notify_block_tip_fn = [&](interfaces::Node::NotifyBlockTipFn callback) {
        block = std::move(callback);
        return MakeNoopHandler();
    };
    NodeModel model{node, false};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    QVERIFY(block);
    const auto now = GetTime();
    // LoadChainTip reports INIT_DOWNLOAD even when the existing chain is caught up.
    block(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{200, now, uint256{1}}, 1.0);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 200, .block_time = now,
        .header_height = 200, .header_time = now,
        .verification_progress = 1.0,
    }, false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QCOMPARE(model.blockTipHeight(), 200);
    QVERIFY(!model.blockSyncActive());
    QVERIFY(model.initialSyncComplete());
    QCOMPARE(node.calls.isInitialBlockDownload.load(), 0);
    QCOMPARE(node.calls.shutdownRequested.load(), 0);
}

void NodeModelTests::networkCommandsCoalesceDesiredStateWithoutBlockingGui()
{
    struct NetworkNode final : MockNode {
        std::function<void(bool)> set_active;
        void setNetworkActive(bool active) override { set_active(active); }
    } node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    QSemaphore entered;
    QSemaphore release;
    std::atomic_bool active{true};
    std::atomic_bool off_gui{true};
    std::atomic_int commands{0};
    std::vector<bool> requested_states;
    const auto gui_thread = QThread::currentThread();
    node.get_network_active_fn = [&] { return active.load(); };
    node.set_active = [&](bool requested) {
        if (QThread::currentThread() == gui_thread) off_gui = false;
        requested_states.push_back(requested);
        if (++commands == 1) {
            entered.release();
            release.acquire();
        }
        active = requested;
    };
    NodeModel model{node, false};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    const auto unblock = qScopeGuard([&] { release.release(); });
    QSignalSpy status_ready{&model, &NodeModel::nodeInformationChanged};
    model.initializeResult(true, {}, false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!status_ready.isEmpty(), ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(InformationValue(model, QStringLiteral("Network active")), QStringLiteral("Yes"), ASYNC_TIMEOUT_MS);
    QSignalSpy pending{&model, &NodeModel::networkActionPendingChanged};
    model.setPause(true);
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, ASYNC_TIMEOUT_MS);
    QVERIFY(model.networkActionPending());
    for (int i = 0; i < 100; ++i) model.setPause(i % 2 == 0);
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY_WITH_TIMEOUT(heartbeat, ASYNC_TIMEOUT_MS);
    QCOMPARE(commands.load(), 1);
    QCOMPARE(pending.count(), 1);
    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(!model.networkActionPending(), ASYNC_TIMEOUT_MS);
    QCOMPARE(commands.load(), 2);
    QCOMPARE(requested_states, std::vector<bool>({false, true}));
    QCOMPARE(pending.count(), 2);
    QVERIFY(!model.pause());
    QVERIFY(model.networkActionError().isEmpty());
    QVERIFY(off_gui.load());
    model.beginShutdown();
    model.setPause(true);
    QCOMPARE(commands.load(), 2);
}


void NodeModelTests::newerNetworkNotificationSupersedesBlockedCommandResult()
{
    struct NetworkNode final : MockNode {
        std::function<void(bool)> set_active;
        void setNetworkActive(bool active) override { set_active(active); }
    } node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    QSemaphore entered;
    QSemaphore release;
    std::atomic_bool active{true};
    std::atomic_bool gate_next_read{false};
    interfaces::Node::NotifyNetworkActiveChangedFn network_changed;
    node.handle_notify_network_active_changed_fn = [&](interfaces::Node::NotifyNetworkActiveChangedFn callback) {
        network_changed = std::move(callback);
        return MakeNoopHandler();
    };
    node.set_active = [&](bool requested) {
        active = requested;
        gate_next_read = true;
    };
    node.get_network_active_fn = [&] {
        const bool captured = active.load();
        if (gate_next_read.exchange(false)) {
            entered.release();
            release.acquire();
        }
        return captured;
    };
    NodeModel model{node, false};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    const auto unblock = qScopeGuard([&] { release.release(); });
    QSignalSpy status_ready{&model, &NodeModel::nodeInformationChanged};
    model.initializeResult(true, {}, false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!status_ready.isEmpty(), ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(InformationValue(model, QStringLiteral("Local addresses")), QStringLiteral("None"), ASYNC_TIMEOUT_MS);
    // Network state is a separate observation and may publish after the addresses.
    QTRY_COMPARE_WITH_TIMEOUT(InformationValue(model, QStringLiteral("Network active")), QStringLiteral("Yes"), ASYNC_TIMEOUT_MS);
    QVERIFY(network_changed);
    model.setPause(true);
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, ASYNC_TIMEOUT_MS);
    QVERIFY(model.networkActionPending());
    // An RPC command resumes networking after this command captured inactive.
    QSignalSpy information{&model, &NodeModel::nodeInformationChanged};
    active = true;
    network_changed(true);
    QTRY_VERIFY_WITH_TIMEOUT(!information.isEmpty(), ASYNC_TIMEOUT_MS);
    QVERIFY(!model.pause());
    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(!model.networkActionPending(), ASYNC_TIMEOUT_MS);
    QVERIFY(!model.pause());
    QCOMPARE(InformationValue(model, QStringLiteral("Network active")), QStringLiteral("Yes"));
    QVERIFY(model.networkActionError().isEmpty());
}


void NodeModelTests::statusNotificationBurstUsesOneFollowupSnapshot()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    QSemaphore entered;
    QSemaphore release;
    std::atomic_int version{0};
    std::atomic_bool off_gui{true};
    const auto gui_thread = QThread::currentThread();
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed;
    interfaces::Node::NotifyAlertChangedFn warnings_changed;
    node.handle_notify_num_connections_changed_fn = [&](interfaces::Node::NotifyNumConnectionsChangedFn callback) {
        connections_changed = std::move(callback);
        return MakeNoopHandler();
    };
    node.handle_notify_alert_changed_fn = [&](interfaces::Node::NotifyAlertChangedFn callback) {
        warnings_changed = std::move(callback);
        return MakeNoopHandler();
    };
    node.get_node_count_fn = [&](ConnectionDirection direction) {
        if (QThread::currentThread() == gui_thread) off_gui = false;
        const size_t count = version.load() == 0 ? 1 : (direction == ConnectionDirection::In ? 9 : 4);
        if (node.calls.getNodeCount.load() == 1) {
            entered.release();
            release.acquire();
        }
        return count;
    };
    node.get_warnings_fn = [&] {
        if (QThread::currentThread() == gui_thread) off_gui = false;
        return Untranslated(version.load() == 0 ? "Old warning" : "Latest warning");
    };
    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    const auto unblock = qScopeGuard([&] { release.release(); });
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, ASYNC_TIMEOUT_MS);
    QVERIFY(connections_changed);
    QVERIFY(warnings_changed);
    QCOMPARE(model.numPeers(), 0);
    QVERIFY(model.warnings().isEmpty());
    version = 1;
    for (int notification = 1; notification <= 10'000; ++notification) {
        connections_changed(notification);
        warnings_changed();
    }
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY_WITH_TIMEOUT(heartbeat, ASYNC_TIMEOUT_MS);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(node.calls.getNodeCount.load(), 1);
    QCOMPARE(node.calls.getWarnings.load(), 0);
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(model.numPeers(), 10'000, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(model.numInboundPeers(), 9, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.numOutboundPeers(), 4);
    QCOMPARE(model.warnings(), QStringLiteral("Latest warning"));
    QCOMPARE(node.calls.getNodeCount.load(), 4);
    QCOMPARE(node.calls.getWarnings.load(), 2);
    QVERIFY(off_gui.load());
}

void NodeModelTests::overlappingStatusNotificationsPublishEachCompletedSnapshot()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    QSemaphore entered;
    QSemaphore release;
    std::atomic_int snapshots{0};
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed;
    interfaces::Node::NotifyAlertChangedFn warnings_changed;
    node.handle_notify_num_connections_changed_fn = [&](interfaces::Node::NotifyNumConnectionsChangedFn callback) {
        connections_changed = std::move(callback);
        return MakeNoopHandler();
    };
    node.handle_notify_alert_changed_fn = [&](interfaces::Node::NotifyAlertChangedFn callback) {
        warnings_changed = std::move(callback);
        return MakeNoopHandler();
    };
    node.get_node_count_fn = [&](ConnectionDirection direction) {
        if (direction == ConnectionDirection::In) {
            ++snapshots;
            entered.release();
            release.acquire();
            return static_cast<size_t>(snapshots.load());
        }
        return static_cast<size_t>(2 * snapshots.load());
    };
    node.get_warnings_fn = [&] {
        return Untranslated(QStringLiteral("Status %1").arg(snapshots.load()).toStdString());
    };
    NodeModel model{node};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    const auto unblock = qScopeGuard([&] { release.release(5); });
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, ASYNC_TIMEOUT_MS);
    entered.acquire();
    for (int completed = 1; completed <= 3; ++completed) {
        // Every query overlaps newer notifications; there is never a quiet
        // interval in which the old implementation would publish a result.
        connections_changed(100 + completed);
        warnings_changed();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, ASYNC_TIMEOUT_MS);
        entered.acquire();
        QTRY_COMPARE_WITH_TIMEOUT(model.numInboundPeers(), completed, ASYNC_TIMEOUT_MS);
        QCOMPARE(model.numOutboundPeers(), 2 * completed);
        QCOMPARE(model.numPeers(), 100 + completed);
        QCOMPARE(model.warnings(), QStringLiteral("Status %1").arg(completed));
        QCOMPARE(snapshots.load(), completed + 1);
    }
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(model.numInboundPeers(), 4, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.numOutboundPeers(), 8);
    QCOMPARE(model.numPeers(), 103);
    QCOMPARE(model.warnings(), QStringLiteral("Status 4"));
}

void NodeModelTests::networkNotificationSupersedesBlockedStatusSnapshot()
{
    MockNode node;
    MempoolState mempool;
    ConfigureNodeModelDefaults(node);
    ConfigureMempoolGetters(node, mempool);
    QSemaphore entered;
    QSemaphore release;
    std::atomic_bool active{true};
    std::atomic_int queries{0};
    interfaces::Node::NotifyNetworkActiveChangedFn network_changed;
    node.handle_notify_network_active_changed_fn = [&](interfaces::Node::NotifyNetworkActiveChangedFn callback) {
        network_changed = std::move(callback);
        return MakeNoopHandler();
    };
    node.get_network_active_fn = [&] {
        const bool result = active.load();
        if (++queries == 1) {
            entered.release();
            release.acquire();
        }
        return result;
    };
    NodeModel model{node, false};
    const auto drain = qScopeGuard([&] { DrainNodeModel(model); });
    QTRY_VERIFY_WITH_TIMEOUT(model.notificationsReady(), ASYNC_TIMEOUT_MS);
    const auto unblock = qScopeGuard([&] { release.release(); });
    model.initializeResult(true, {}, false, false);
    QTRY_VERIFY_WITH_TIMEOUT(!model.startupDiagnosticsPending(), ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, ASYNC_TIMEOUT_MS);
    active = false;
    network_changed(false);
    QTRY_VERIFY_WITH_TIMEOUT(model.pause(), ASYNC_TIMEOUT_MS);
    QSignalSpy pause_changes{&model, &NodeModel::pauseChanged};
    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(queries.load() >= 2, ASYNC_TIMEOUT_MS);
    QCoreApplication::processEvents();
    QVERIFY(model.pause());
    QCOMPARE(pause_changes.count(), 0);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NodeModelTests)
#else
QTEST_MAIN(NodeModelTests)
#endif
#include <test_nodemodel.moc>
