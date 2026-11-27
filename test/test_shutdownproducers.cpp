// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <node/interface_ui.h>
#include <qml/asyncjoin.h>
#include <qml/executor.h>
#include <qml/guipresentation.h>
#include <qml/models/banlistmodel.h>
#include <qml/models/blockclockmodel.h>
#include <qml/models/blockclockhistory.h>
#include <qml/models/debuglogmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/nodemodel.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/rpcconsolemodel.h>
#include <test/mocks/drainexecutors.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/manualnodeobservations.h>
#include <test/mocks/manualstartupexecution.h>
#include <util/translation.h>

#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QTemporaryDir>

#include <atomic>
#include <thread>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

class ShutdownProducersTests : public QObject
{
    Q_OBJECT

    void trafficQueueJoinsIncludeDeferredCleanup(bool destroy_owner)
    {
        const auto entered = std::make_shared<QSemaphore>();
        const auto release = std::make_shared<QSemaphore>();
        std::atomic_bool cleaned{false}, joining{false}, joined{false};
        MockNode node;
        execution::Executor executor{execution::Executor::Config{}};
        auto tower = std::make_unique<NetworkTrafficTower>(node, executor);
        std::thread joiner;
        const auto unblock = qScopeGuard([release, &joiner] {
            release->release();
            if (joiner.joinable()) joiner.join();
        });
        {
            auto cleanup = std::shared_ptr<int>(new int{0}, [entered, release, &cleaned](int* value) {
                entered->release();
                release->acquire();
                delete value;
                cleaned = true;
            });
            node.get_total_bytes_recv_fn = [cleanup = std::move(cleanup)]() mutable {
                // Release a value owned only by this accepted sample.
                auto last = std::move(cleanup);
                return int64_t{100};
            };
        }
        QSignalSpy drained{tower.get(), &NetworkTrafficTower::drained};
        QVERIFY(drained.isValid());
        tower->startSampling();
        tower->beginShutdown();
        QTRY_VERIFY(entered->available() > 0);
        QVERIFY(!cleaned);
        QCOMPARE(drained.size(), 0);
        if (destroy_owner) {
            tower.reset();
            joiner = std::thread([&] {
                joining = true;
                executor.stopAndJoin();
                joined = true;
            });
            QTRY_VERIFY(joining);
            QVERIFY(!joined);
        }
        bool gui_progress{false};
        QTimer::singleShot(0, this, [&] { gui_progress = true; });
        QTRY_VERIFY(gui_progress);
        QCOMPARE(drained.size(), 0);
        release->release();
        if (destroy_owner) {
            QTRY_VERIFY(joined);
        } else {
            QTRY_COMPARE(drained.size(), 1);
        }
        QVERIFY(cleaned);
    }

    struct WorkerCleanup {
        std::atomic_bool armed{false}, finished{false};
        QSemaphore entered, release;

        static void arm(const std::shared_ptr<WorkerCleanup>& cleanup)
        {
            struct WorkerExit {
                std::shared_ptr<WorkerCleanup> cleanup;
                ~WorkerExit()
                {
                    cleanup->entered.release();
                    cleanup->release.acquire();
                    cleanup->finished = true;
                }
            };
            thread_local const WorkerExit exit{cleanup}; // NOLINT(bitcoin-nontrivial-threadlocal): Tests thread exit.
            cleanup->armed = true;
        }
    };

    template <typename Model>
    void checkWorkerCleanup(std::unique_ptr<Model>& model, void (Model::*stop)(), void (Model::*drain_signal)(),
                            const std::shared_ptr<WorkerCleanup>& cleanup, bool destroy_owner)
    {
        auto unblock = qScopeGuard([cleanup] { cleanup->release.release(); });
        QSignalSpy drained(model.get(), drain_signal);
        QVERIFY(drained.isValid());
        QTRY_VERIFY(cleanup->armed.load());
        (model.get()->*stop)();
        QTRY_VERIFY(cleanup->entered.available() > 0);
        QCOMPARE(drained.size(), 0);
        QVERIFY(!cleanup->finished);

        bool joined{false}, gui_progress{false};
        QObject receiver;
        if (destroy_owner) {
            model.reset();
            WaitForThreadJoins(&receiver, [&] { joined = true; });
            QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
            QVERIFY(!joined);
        }
        QTimer::singleShot(0, &receiver, [&] { gui_progress = true; });
        QTRY_VERIFY(gui_progress);
        QCOMPARE(drained.size(), 0);

        cleanup->release.release();
        unblock.dismiss();
        if (destroy_owner) {
            QTRY_VERIFY(joined);
        } else {
            QTRY_COMPARE(drained.size(), 1);
        }
        QVERIFY(cleanup->finished);
    }

    void debugLogQueueJoinsIncludeDeferredCleanup(bool destroy_owner)
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("debug.log");
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QVERIFY(file.write("2026-06-19T10:00:00Z readiness\n") > 0);
        }
        const auto cleanup = std::make_shared<WorkerCleanup>();
        auto* const gui_thread = QThread::currentThread();
        auto log = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
            [cleanup, gui_thread, post = presentation::QueuedPost()](auto publication) {
                if (QThread::currentThread() != gui_thread) WorkerCleanup::arm(cleanup);
                post(std::move(publication));
            });
        log->setActive(true);
        checkWorkerCleanup(log, &DebugLogModel::stop, &DebugLogModel::drained, cleanup, destroy_owner);
    }

    void rpcQueueJoinsIncludeDeferredCleanup(bool destroy_owner)
    {
        MockNode node;
        const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
        const auto cleanup = std::make_shared<WorkerCleanup>();
        node.execute_rpc_fn = [cleanup](const auto&, const auto&, const auto&) {
            WorkerCleanup::arm(cleanup);
            return UniValue{7};
        };
        auto console = std::make_unique<RpcConsoleModel>(node);
        QVERIFY(console->submitNodeCommand(QStringLiteral("getblockcount")));
        checkWorkerCleanup(console, &RpcConsoleModel::beginShutdown, &RpcConsoleModel::drained, cleanup, destroy_owner);
        QCOMPARE(node.calls.executeRpc.load(), 1);
    }

private Q_SLOTS:
    void producerJoinsIncludeDeferredCleanup_data()
    {
        QTest::addColumn<int>("producer");
        QTest::addColumn<bool>("destroy_owner_before_join_completion");
        for (int producer = 0; producer < 4; ++producer) {
            for (bool destroy_owner_before_join_completion : {false, true}) {
                const QByteArray name = QByteArray::number(producer) + (destroy_owner_before_join_completion ? "-deleted" : "-drained");
                QTest::newRow(name.constData()) << producer << destroy_owner_before_join_completion;
            }
        }
    }

    void producerJoinsIncludeDeferredCleanup()
    {
        QFETCH(int, producer);
        QFETCH(bool, destroy_owner_before_join_completion);
        SelectParams(ChainType::REGTEST);
        if (producer == 1) {
            trafficQueueJoinsIncludeDeferredCleanup(destroy_owner_before_join_completion);
            return;
        }
        if (producer == 2) {
            rpcQueueJoinsIncludeDeferredCleanup(destroy_owner_before_join_completion);
            return;
        }
        if (producer == 3) {
            debugLogQueueJoinsIncludeDeferredCleanup(destroy_owner_before_join_completion);
            return;
        }
        MockNode node;
        const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
        const auto cleanup = std::make_shared<WorkerCleanup>();
        node.handle_question_fn = [cleanup](interfaces::Node::QuestionFn) {
            WorkerCleanup::arm(cleanup);
            return interfaces::MakeCleanupHandler([] {});
        };
        auto model = std::make_unique<NodeModel>(node);
        checkWorkerCleanup(model, &NodeModel::beginShutdown, &NodeModel::workDrained, cleanup,
            destroy_owner_before_join_completion);
        QCOMPARE(node.calls.handleQuestion.load(), 1);
    }

    void backendQueriesWaitForSuccessfulInitialization()
    {
        SelectParams(ChainType::REGTEST);
        MockNode node;
        ManualNodeObservations environment;
        int64_t milliseconds{0};
        std::vector<std::function<void()>> deadlines;
        auto services = environment.services();
        const auto retirement = services.network_retirement;
        const auto post = services.post;
        const auto queues = [&] {
            using execution::Lane;
            auto& executor = environment.executor();
            return NativeListModel::Queues{
                std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core)),
                std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility)),
                retirement, {},
            };
        };
        services.wall_seconds = [] { return int64_t{100'000}; };
        services.monotonic_milliseconds = [&] { return milliseconds; };
        services.schedule = [&](NodeModel::Poll, std::chrono::milliseconds, auto callback) {
            deadlines.push_back(std::move(callback));
        };
        services.cancel = [](NodeModel::Poll) {};
        const bool backend_ready{false};
        NodeModel model(node, backend_ready, std::move(services));
        QVERIFY(model.nodeInformationRows().isEmpty());
        PeerListModel peers(node, nullptr, backend_ready, post,
                            [] { return NodeClock::time_point{}; }, queues());
        BanListModel bans(node, nullptr, backend_ready, post, queues());
        const auto settle = [&] { QVERIFY(DrainManualNodeTurns(environment)); };
        const auto cleanup = qScopeGuard([&] {
            model.beginShutdown(); peers.beginShutdown(); bans.beginShutdown(); settle();
        });
        connect(&model, &NodeModel::chainStateReady, &peers, &PeerListModel::backendInitialized);
        connect(&model, &NodeModel::chainStateReady, &bans, &BanListModel::backendInitialized);
        model.setMempoolInfoPollingActive(true);
        model.refreshMempoolInfo();
        peers.startAutoRefresh();
        peers.refresh();
        bans.refresh();
        model.setPause(true);
        QVERIFY(!model.peerCommands()->requestDisconnectPeer(1));
        model.initializeResult(false, {});
        QVERIFY(model.nodeInformationRows().isEmpty());
        settle();
        QCOMPARE(node.calls.getMempoolSize.load(), 0);
        QCOMPARE(node.calls.getNodeCount.load(), 0);
        QCOMPARE(node.calls.getNodesStats.load(), 0);
        QCOMPARE(node.calls.getBanned.load(), 0);
        QVERIFY(!peers.findChild<QTimer*>()->isActive());

        model.initializeResult(true, {});
        // Readiness can reuse an already projected diagnostic result. Either
        // path must admit reads without executing them inline on this caller.
        QCOMPARE(node.calls.getMempoolSize.load(), 0);
        QCOMPARE(node.calls.getNodeCount.load(), 0);
        QCOMPARE(node.calls.getNodesStats.load(), 0);
        QCOMPARE(node.calls.getBanned.load(), 0);
        settle();
        QVERIFY(node.calls.getMempoolSize.load() > 0);
        QVERIFY(node.calls.getNodeCount.load() > 0);
        QVERIFY(node.calls.getNodesStats.load() >= 1);
        QCOMPARE(node.calls.getBanned.load(), 1);
        QVERIFY(peers.findChild<QTimer*>()->isActive());
        QSignalSpy drained(&model, &NodeModel::workDrained);
        model.beginShutdown();
        peers.beginShutdown();
        bans.beginShutdown();
        settle(); QCOMPARE(drained.size(), 1);
        const int mempool_queries = node.calls.getMempoolSize.load();
        const int peer_queries = node.calls.getNodesStats.load();
        const int ban_queries = node.calls.getBanned.load();
        model.initializeResult(true, {});
        model.refreshMempoolInfo();
        peers.backendInitialized();
        bans.backendInitialized();
        milliseconds += 60'000;
        const std::vector<std::function<void()>> old_deadlines{deadlines.begin(), deadlines.end()};
        for (const auto& deadline : old_deadlines) deadline();
        settle();
        QCOMPARE(node.calls.getMempoolSize.load(), mempool_queries);
        QCOMPARE(node.calls.getNodesStats.load(), peer_queries);
        QCOMPARE(node.calls.getBanned.load(), ban_queries);
        QVERIFY(DrainManualNodeTurns(environment));
    }

    void stoppedViewsDoNotRestartBackendQueries()
    {
        SelectParams(ChainType::REGTEST);
        MockNode node;
        PeerListModel peers(node, nullptr);
        BanListModel bans(node);
        ManualStartupExecution worker;
        const auto utility = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(execution::Lane::Utility));
        PresentationServices services{{{}, [&](auto turn) { return worker.outcomes.post(std::move(turn)); }, {}, utility}};
        auto history = std::make_shared<BlockClockHistory>();
        BlockClockModel clock(history, services);
        history->initialize({});
        clock.refreshHistory();
        peers.beginShutdown();
        bans.beginShutdown();
        clock.stop();
        worker.drain();
        const int peer_queries = node.calls.getNodesStats.load();
        const int ban_queries = node.calls.getBanned.load();
        peers.startAutoRefresh();
        peers.refresh();
        bans.refresh();
        QVERIFY(!bans.unbanAt(0));
        clock.refreshHistory();
        clock.updateCurrentTime(QDateTime::currentDateTime().addDays(1));
        QVERIFY(!clock.timerActive());
        const auto* timer = peers.findChild<QTimer*>();
        QVERIFY(timer);
        QVERIFY(!timer->isActive());
        QCOMPARE(node.calls.getNodesStats.load(), peer_queries);
        QCOMPARE(node.calls.getBanned.load(), ban_queries);
        QVERIFY(history->retentionStart().has_value());
        QVERIFY(worker.quiescent());
        clock.close();
        worker.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        worker.drain();
        QCOMPARE(services.pendingRetirements(), size_t{0});
    }

    void nodeHandlersRetireOffGuiBeforeDrain()
    {
        SelectParams(ChainType::REGTEST);
        struct Handler final : interfaces::Handler {
            QSemaphore& entered;
            QSemaphore& release;
            std::atomic<bool>& gui_call;
            QThread* gui;
            Handler(QSemaphore& entered, QSemaphore& release, std::atomic<bool>& gui_call, QThread* gui)
                : entered(entered), release(release), gui_call(gui_call), gui(gui) {}
            void disconnect() override {
                gui_call = QThread::currentThread() == gui;
                entered.release();
                release.acquire();
            }
        };
        QSemaphore entered;
        QSemaphore release;
        std::atomic<bool> gui_call{false};
        MockNode node;
        const auto gui_thread = QCoreApplication::instance()->thread();
        node.handle_notify_alert_changed_fn = [&](interfaces::Node::NotifyAlertChangedFn) {
            return std::make_unique<Handler>(entered, release, gui_call, gui_thread);
        };
        NodeModel model(node);
        QTRY_VERIFY(model.notificationsReady());
        const auto unblock = qScopeGuard([&] { release.release(); });
        QSignalSpy drained(&model, &NodeModel::workDrained);
        model.beginShutdown();
        QTRY_VERIFY(entered.available() > 0);
        bool heartbeat{false};
        QTimer::singleShot(0, &model, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QCOMPARE(drained.size(), 0);
        QVERIFY(!gui_call.load());
        QVERIFY(model.nodeInformationRows().isEmpty());
        QVERIFY(!model.peerCommands()->requestDisconnectPeer(1));
        release.release();
        QTRY_COMPARE(drained.size(), 1);
    }


    void shutdownDismissesBlockingBackendQuestion()
    {
        SelectParams(ChainType::REGTEST);
        MockNode node;
        interfaces::Node::QuestionFn question;
        node.handle_question_fn = [&](interfaces::Node::QuestionFn callback) {
            question = std::move(callback);
            return interfaces::MakeCleanupHandler([] {});
        };
        NodeModel model(node);
        QTRY_VERIFY(model.notificationsReady());
        QVERIFY(question);
        QSignalSpy requested(&model, &NodeModel::requestedShutdown);
        std::atomic<bool> finished{false}, result{true};
        std::thread worker([&] {
            result = question(Untranslated("Continue?"), "Continue?", CClientUIInterface::BTN_OK | CClientUIInterface::BTN_CANCEL);
            finished = true;
        });
        const auto unblock = qScopeGuard([&] { model.requestShutdown(); if (worker.joinable()) worker.join(); });
        QTRY_VERIFY(model.runtimeDialogVisible());
        QVERIFY(!finished.load());
        model.requestShutdown();
        QTRY_VERIFY(finished.load()); worker.join();
        QVERIFY(!result.load()); QCOMPARE(requested.size(), 1);
        QVERIFY(!model.runtimeDialogVisible());
        QVERIFY(!question(Untranslated("Late question"), "Late question", CClientUIInterface::BTN_OK));
        QVERIFY(!model.runtimeDialogVisible());
        model.beginShutdown();
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ShutdownProducersTests)
#else
QTEST_MAIN(ShutdownProducersTests)
#endif
#include <test_shutdownproducers.moc>
