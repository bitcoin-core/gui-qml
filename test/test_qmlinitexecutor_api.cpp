// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <net_processing.h>
#include <qml/initexecutor.h>
#include <qml/shutdowncoordinator.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>

#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QEventLoop>
#include <QSemaphore>
#include <QTimer>

#include <atomic>
#include <stdexcept>

Q_DECLARE_METATYPE(interfaces::BlockAndHeaderTipInfo)

namespace {
constexpr auto SIGNAL_TIMEOUT{5'000};

bool SameThreadAddress(const QThread* first, const QThread* second)
{
    return static_cast<const void*>(first) == static_cast<const void*>(second);
}

void DrainRetiredExecutors()
{
    QEventLoop loop;
    bool drained{false};
    BackendExecutor::shutdownAll(&loop, [&] { drained = true; loop.quit(); });
    while (!drained) loop.exec();
}

}

class ShutdownParticipant : public QObject
{
    Q_OBJECT
public:
    bool started{false};
    void finish() { Q_EMIT drained(); }
Q_SIGNALS:
    void drained();
};

class QmlInitExecutorApiTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void initializeEmitsResultAndRunsOffMainThread();
    void initializeEmitsUnsuccessfulResult();
    void initializeReportsShutdownRequest();
    void initializeEmitsRunawayExceptionOnFailure();
    void fatalInitializationRejectsPendingAndLaterShutdown();
    void queuedShutdownAfterInitialization_data();
    void queuedShutdownAfterInitialization();
    void shutdownEmitsResultAndRunsOffMainThread();
    void shutdownEmitsRunawayExceptionOnFailure();
    void interruptionBypassesBlockedInitialization();
    void shutdownWaitsForInterruptionAndEveryParticipant();
    void portMappingDrainsBeforeSingleInterruption();
};

void QmlInitExecutorApiTests::initTestCase()
{
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");
}

void QmlInitExecutorApiTests::initializeEmitsResultAndRunsOffMainThread()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return QThread::currentThread() != QCoreApplication::instance()->thread(); };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool ran_off_main_thread{false};

    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo* tip_info) {
        ran_off_main_thread = QThread::currentThread() != QCoreApplication::instance()->thread();
        tip_info->block_height = 101;
        tip_info->block_time = 1'700'000'001;
        tip_info->header_height = 105;
        tip_info->header_time = 1'700'000'099;
        tip_info->verification_progress = 0.75;
        return true;
    };

    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });

    QmlInitExecutor executor{node};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);

    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(initialize_spy.count(), 1);
    QCOMPARE(runaway_spy.count(), 0);
    QVERIFY(ran_off_main_thread.load());
    QCOMPARE(node.calls.appInitMain.load(), 1);

    const QList<QVariant> arguments = initialize_spy.takeFirst();
    QCOMPARE(arguments.at(0).toBool(), true);
    QCOMPARE(arguments.at(2).toBool(), true);
    QCOMPARE(arguments.at(3).toBool(), false);
    QCOMPARE(node.calls.isInitialBlockDownload.load(), 1);
    QCOMPARE(node.calls.shutdownRequested.load(), 1);

    const auto tip_info = arguments.at(1).value<interfaces::BlockAndHeaderTipInfo>();
    QCOMPARE(tip_info.block_height, 101);
    QCOMPARE(tip_info.block_time, 1'700'000'001LL);
    QCOMPARE(tip_info.header_height, 105);
    QCOMPARE(tip_info.header_time, 1'700'000'099LL);
    QCOMPARE(tip_info.verification_progress, 0.75);
}

void QmlInitExecutorApiTests::initializeEmitsUnsuccessfulResult()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return false; };
    node.ExpectNoCalls(node.calls.isInitialBlockDownload);
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(initialize_spy.count(), 1);
    QVERIFY(!initialize_spy.takeFirst().at(0).toBool());
    QCOMPARE(runaway_spy.count(), 0);
}

void QmlInitExecutorApiTests::fatalInitializationRejectsPendingAndLaterShutdown()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    QSemaphore release_init, release_interrupt;
    std::atomic_bool interrupted{false};
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) -> bool {
        release_init.acquire();
        throw std::runtime_error{"init failed during shutdown"};
    };
    node.start_shutdown_fn = [&] { release_interrupt.acquire(); interrupted = true; };
    node.ExpectNoCalls(node.calls.appShutdown);
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    const auto release_workers = qScopeGuard([&] { release_init.release(); release_interrupt.release(); });
    QmlShutdownCoordinator coordinator{executor};
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
    QSignalSpy interrupt_spy(&executor, &QmlInitExecutor::interruptResult);
    QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
    executor.initialize();
    coordinator.requestShutdown();
    release_init.release();
    QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
    release_interrupt.release();
    QTRY_VERIFY_WITH_TIMEOUT(interrupted.load(), SIGNAL_TIMEOUT);
    QTest::qWait(100);
    executor.shutdown();
    DrainRetiredExecutors();
    QCOMPARE(interrupt_spy.count(), 0);
    QCOMPARE(shutdown_spy.count(), 0);
    QCOMPARE(node.calls.appShutdown.load(), 0);
}

void QmlInitExecutorApiTests::queuedShutdownAfterInitialization_data()
{
    QTest::addColumn<QString>("outcome");
    for (const auto* outcome : {"success", "failure", "exception", "unknown", "readiness-exception"}) {
        QTest::newRow(outcome) << QString::fromLatin1(outcome);
    }
}

void QmlInitExecutorApiTests::queuedShutdownAfterInitialization()
{
    QFETCH(QString, outcome);
    const bool fatal = outcome != "success" && outcome != "failure";
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    QSemaphore entered, release;
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) -> bool {
        entered.release();
        release.acquire();
        if (outcome == "exception") throw std::runtime_error{"init failed after interruption"};
        if (outcome == "unknown") throw 42;
        return outcome != "failure";
    };
    node.is_initial_block_download_fn = [&] {
        if (outcome == "readiness-exception") throw std::runtime_error{"readiness check failed"};
        return false;
    };
    node.shutdown_requested_fn = [] { return false; };
    node.start_shutdown_fn = [] {};
    node.app_shutdown_fn = [] {};
    node.ExpectExactly(node.calls.appShutdown, fatal ? 0 : 1);
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    const auto unblock = qScopeGuard([&] { release.release(); });
    QmlShutdownCoordinator coordinator{executor};
    QSignalSpy initialized{&executor, &QmlInitExecutor::initializeResult};
    QSignalSpy interrupted{&executor, &QmlInitExecutor::interruptResult};
    QSignalSpy runaway{&executor, &QmlInitExecutor::runawayException};
    QSignalSpy shutdown{&executor, &QmlInitExecutor::shutdownResult};
    QSignalSpy finished{&coordinator, &QmlShutdownCoordinator::finished};
    executor.initialize();
    executor.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, SIGNAL_TIMEOUT);
    coordinator.requestShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(interrupted.count(), 1, SIGNAL_TIMEOUT);
    // Shutdown is now queued behind initialization, before the GUI sees its result.
    release.release();
    if (fatal) QTRY_COMPARE_WITH_TIMEOUT(runaway.count(), 1, SIGNAL_TIMEOUT);
    else QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
    executor.initialize();
    executor.shutdown();
    DrainRetiredExecutors();
    QCOMPARE(node.calls.appInitMain.load(), 1);
    QCOMPARE(initialized.count(), fatal ? 0 : 1);
    QCOMPARE(shutdown.count(), fatal ? 0 : 1);
    QCOMPARE(finished.count(), fatal ? 0 : 1);
    QCOMPARE(runaway.count(), fatal ? 1 : 0);
    QCOMPARE(node.calls.appShutdown.load(), fatal ? 0 : 1);
}

void QmlInitExecutorApiTests::initializeReportsShutdownRequest()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool shutdown_requested{false};
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        shutdown_requested = true;
        return true;
    };
    node.shutdown_requested_fn = [&] { return shutdown_requested.load(); };
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(node.calls.shutdownRequested.load(), 1);
    const auto result = initialize_spy.takeFirst();
    QVERIFY(result.at(0).toBool());
    QVERIFY(result.at(3).toBool());
}

void QmlInitExecutorApiTests::initializeEmitsRunawayExceptionOnFailure()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();

    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) -> bool {
        throw std::runtime_error{"init failed"};
    };

    node.ExpectNoCalls(node.calls.shutdownRequested);
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);

    executor.initialize();

    QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(runaway_spy.count(), 1);
    QCOMPARE(initialize_spy.count(), 0);
    QCOMPARE(runaway_spy.takeFirst().at(0).toString(), QString{"init failed"});
    QCOMPARE(node.calls.appInitMain.load(), 1);
}

void QmlInitExecutorApiTests::shutdownEmitsResultAndRunsOffMainThread()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic<QThread*> initialization_thread{nullptr};
    std::atomic<QThread*> shutdown_thread{nullptr};

    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        initialization_thread = QThread::currentThread();
        return true;
    };
    node.app_shutdown_fn = [&] { shutdown_thread = QThread::currentThread(); };

    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
    executor.initialize();
    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    executor.shutdown();

    QVERIFY(!shutdown_spy.isEmpty() || shutdown_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(runaway_spy.count(), 0);
    QVERIFY(shutdown_thread.load() != QCoreApplication::instance()->thread());
    QVERIFY(SameThreadAddress(shutdown_thread.load(), initialization_thread.load()));
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

void QmlInitExecutorApiTests::shutdownEmitsRunawayExceptionOnFailure()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.app_shutdown_fn = [] { throw std::runtime_error{"shutdown failed"}; };

    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
    executor.initialize();
    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    executor.shutdown();

    QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(runaway_spy.count(), 1);
    QCOMPARE(shutdown_spy.count(), 0);
    QCOMPARE(runaway_spy.takeFirst().at(0).toString(), QString{"shutdown failed"});
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

void QmlInitExecutorApiTests::interruptionBypassesBlockedInitialization()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    QSemaphore release;
    std::atomic_bool entered{false};
    std::atomic_bool off_gui{false};
    const auto gui_thread = QThread::currentThread();
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        entered = true;
        release.acquire();
        return false;
    };
    node.start_shutdown_fn = [&] {
        off_gui = QThread::currentThread() != gui_thread;
        release.release();
    };
    node.app_shutdown_fn = [] {};
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    const auto unblock = qScopeGuard([&] { release.release(); });
    QSignalSpy initialized{&executor, &QmlInitExecutor::initializeResult};
    QSignalSpy interrupted{&executor, &QmlInitExecutor::interruptResult};
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    executor.initialize();
    QTRY_VERIFY_WITH_TIMEOUT(entered.load(), SIGNAL_TIMEOUT);
    executor.interrupt();
    executor.interrupt();
    QTRY_COMPARE_WITH_TIMEOUT(interrupted.count(), 1, SIGNAL_TIMEOUT);
    QTRY_COMPARE_WITH_TIMEOUT(initialized.count(), 1, SIGNAL_TIMEOUT);
    QVERIFY(off_gui.load());
    QCOMPARE(node.calls.startShutdown.load(), 1);
    executor.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
}

void QmlInitExecutorApiTests::shutdownWaitsForInterruptionAndEveryParticipant()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    QSemaphore release_hook;
    std::atomic_bool hook_entered{false};
    node.start_shutdown_fn = [&] {
        hook_entered = true;
        release_hook.acquire();
    };
    node.app_shutdown_fn = [] {};
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    const auto unblock = qScopeGuard([&] { release_hook.release(); });
    QmlShutdownCoordinator coordinator{executor};
    ShutdownParticipant first, second;
    coordinator.addParticipant(&first, &ShutdownParticipant::drained, [&] { first.started = true; });
    coordinator.addParticipant(&second, &ShutdownParticipant::drained, [&] { second.started = true; });
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    coordinator.requestShutdown();
    coordinator.requestShutdown();
    QTRY_VERIFY_WITH_TIMEOUT(hook_entered.load(), SIGNAL_TIMEOUT);
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, SIGNAL_TIMEOUT);
    QVERIFY(!first.started);
    QCOMPARE(node.calls.appShutdown.load(), 0);
    release_hook.release();
    QTRY_VERIFY_WITH_TIMEOUT(first.started && second.started, SIGNAL_TIMEOUT);
    first.finish();
    first.finish();
    QCOMPARE(node.calls.appShutdown.load(), 0);
    second.finish();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
    QCOMPARE(node.calls.startShutdown.load(), 1);
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

void QmlInitExecutorApiTests::portMappingDrainsBeforeSingleInterruption()
{
    StrictMockNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    node.start_shutdown_fn = [] {};
    node.app_shutdown_fn = [] {};
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node};
    QmlShutdownCoordinator coordinator{executor};
    ShutdownParticipant settings;
    coordinator.addBeforeInterruptParticipant(&settings, &ShutdownParticipant::drained, [&] { settings.started = true; });
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    coordinator.requestShutdown();
    QVERIFY(settings.started);
    QCOMPARE(node.calls.startShutdown.load(), 0);
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, SIGNAL_TIMEOUT);
    settings.finish();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
    QCOMPARE(node.calls.startShutdown.load(), 1);
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QmlInitExecutorApiTests)
#else
QTEST_MAIN(QmlInitExecutorApiTests)
#endif
#include <test_qmlinitexecutor_api.moc>
