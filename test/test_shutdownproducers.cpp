// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <node/interface_ui.h>
#include <qml/backendexecutor.h>
#include <qml/models/banlistmodel.h>
#include <qml/models/blockclockmodel.h>
#include <qml/models/blockclockhistory.h>
#include <qml/models/debuglogmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/nodemodel.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/rpcconsolemodel.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <util/translation.h>

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
        MockNode node;
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        std::unique_ptr<QObject> model;
        std::function<void()> stop;
        switch (producer) {
        case 0: {
            auto* instance = new NodeModel(node);
            model.reset(instance);
            stop = [instance] { instance->beginShutdown(); };
            break;
        }
        case 1: {
            auto* instance = new NetworkTrafficTower(node);
            model.reset(instance);
            stop = [instance] { instance->beginShutdown(); };
            break;
        }
        case 2: {
            auto* instance = new RpcConsoleModel(node);
            model.reset(instance);
            stop = [instance] { instance->beginShutdown(); };
            break;
        }
        case 3: {
            auto* instance = new DebugLogModel(fs::PathFromString(directory.filePath("debug.log").toStdString()));
            model.reset(instance);
            stop = [instance] { instance->stop(); };
            break;
        }
        }
        QVERIFY(model);
        auto entered = std::make_shared<QSemaphore>();
        auto release = std::make_shared<QSemaphore>();
        const auto unblock = qScopeGuard([release] { release->release(); });
        {
            // Producers use owned executors rather than a view-specific
            // QThread. Delay the actual last-reference cleanup
            // on that worker; drain must include it even after owner deletion.
            auto* executor = model->findChild<BackendExecutor*>();
            QVERIFY(executor);
            auto cleanup = std::shared_ptr<int>(new int{0}, [entered, release](int* value) {
                entered->release();
                release->acquire();
                delete value;
            });
            QVERIFY(executor->submit(model.get(), [cleanup] {}, [] {}));
        }
        QSignalSpy drained(model.get(), producer == 0 ? SIGNAL(workDrained()) : SIGNAL(drained()));
        QVERIFY(drained.isValid());
        stop();
        QTRY_VERIFY(entered->available() > 0);
        bool gui_progress{false};
        QTimer::singleShot(0, this, [&] { gui_progress = true; });
        QTRY_VERIFY(gui_progress);
        QCOMPARE(drained.size(), 0);
        release->release();
        if (destroy_owner_before_join_completion) {
            model.reset();
            bool all_drained{false};
            BackendExecutor::shutdownAll(this, [&] { all_drained = true; });
            QTRY_VERIFY(all_drained);
        } else {
            QTRY_COMPARE(drained.size(), 1);
        }
    }

    void backendQueriesWaitForSuccessfulInitialization()
    {
        SelectParams(ChainType::REGTEST);
        MockNode node;
        ManualExecutionEnvironment environment;
        int64_t milliseconds{0};
        std::vector<std::function<void()>> deadlines;
        const execution::SnapshotServices execution_services{environment.scheduler,
            [&](auto publication) { environment.post(std::move(publication)); }};
        NodeModel::Services services;
        services.execution = execution_services;
        services.wall_seconds = [] { return int64_t{100'000}; };
        services.monotonic_milliseconds = [&] { return milliseconds; };
        services.schedule = [&](NodeModel::Poll, std::chrono::milliseconds, auto callback) {
            deadlines.push_back(std::move(callback));
        };
        services.cancel = [](NodeModel::Poll) {};
        const bool backend_ready{false};
        NodeModel model(node, backend_ready, std::move(services));
        QVERIFY(model.nodeInformationRows().isEmpty());
        PeerListModel peers(node, nullptr, backend_ready, execution_services,
                            [] { return NodeClock::time_point{}; });
        BanListModel bans(node, nullptr, backend_ready, execution_services);
        const auto settle = [&] {
            for (int turn{0}; turn < 128; ++turn) {
                environment.drain(); environment.publishAll();
                if (environment.transportIdle() && environment.publicationsIdle()) return;
            }
            QTest::qFail("The original backend query owners did not settle", __FILE__, __LINE__);
        };
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
        QVERIFY(environment.quiescent());
    }

    void stoppedViewsDoNotRestartBackendQueries()
    {
        SelectParams(ChainType::REGTEST);
        MockNode node;
        PeerListModel peers(node, nullptr);
        BanListModel bans(node);
        auto history = std::make_shared<BlockClockHistory>();
        BlockClockModel clock(history);
        history->initialize({});
        clock.refreshHistory();
        peers.beginShutdown();
        bans.beginShutdown();
        clock.stop();
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
