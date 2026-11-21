// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/quithandler.h>
#include <qml/startupdialog.h>
#include <qml/startupeffects.h>
#include <qml/guipresentation.h>
#include <test/mocks/manualexecutionenvironment.h>

#include <QApplication>
#include <QEventLoop>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QSemaphore>
#include <QSignalSpy>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>
#include <vector>

namespace {
void SendApplicationQuitEvent()
{
    QEvent quit{QEvent::Quit};
    QCoreApplication::sendEvent(QCoreApplication::instance(), &quit);
}
}

class BootstrapBeforeExecTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { qApp->setQuitOnLastWindowClosed(false); }
    void dismissedStartupDialogAllowsFinalDrain_data();
    void dismissedStartupDialogAllowsFinalDrain();
    void repeatedNativeQuitAllowsOnboardingExitAndFinalDrain();
    void startupPhaseUsesActualDomainOutcome();
    void startupPhaseReportsAdmissionAndNativeFailures();
    void startupPhaseKeepsGuiResponsiveOnRealWorker();
    void nativeInterfacesRetireAfterActorsAndKeepGuiResponsiveUntilFinalJoin();
};

void BootstrapBeforeExecTests::dismissedStartupDialogAllowsFinalDrain_data()
{
    QTest::addColumn<QString>("action");
    QTest::newRow("native-quit") << QStringLiteral("quit");
    QTest::newRow("qml-close-button") << QStringLiteral("qml");
    QTest::newRow("window-close") << QStringLiteral("close");
}

void BootstrapBeforeExecTests::dismissedStartupDialogAllowsFinalDrain()
{
    QFETCH(QString, action);
    QmlQuitHandler quit_handler;
    QQmlApplicationEngine engine;
    QQuickWindow window;
    window.show();
    QTimer::singleShot(0, this, [&] {
        if (action == "quit") SendApplicationQuitEvent();
        else if (action == "qml") Q_EMIT engine.quit();
        else window.close();
    });
    ExecStartupDialog(engine, window, quit_handler);
    std::atomic_bool work_finished{false};
    BackendExecutor executor;
    executor.submit(this, [&] { work_finished = true; }, [] {});
    QEventLoop drain;
    bool drained{false};
    BackendExecutor::shutdownAll(&drain, [&] { drained = true; drain.quit(); });
    while (!drained) drain.exec();
    QVERIFY(work_finished.load());
    QVERIFY(executor.isDrained());
}

void BootstrapBeforeExecTests::repeatedNativeQuitAllowsOnboardingExitAndFinalDrain()
{
    QmlQuitHandler quit_handler;
    QSignalSpy requested{&quit_handler, &QmlQuitHandler::quitRequested};
    QEventLoop onboarding;
    connect(&quit_handler, &QmlQuitHandler::quitRequested, &onboarding, &QEventLoop::quit);
    QTimer::singleShot(0, this, SendApplicationQuitEvent);
    onboarding.exec();

    QSemaphore release;
    std::atomic_bool work_finished{false};
    BackendExecutor executor;
    executor.submit(this, [&] { release.acquire(); work_finished = true; }, [] {});
    QTimer::singleShot(0, this, [&] {
        SendApplicationQuitEvent();
        QTimer::singleShot(0, this, [&] { release.release(); });
    });
    QEventLoop drain;
    bool drained{false};
    BackendExecutor::shutdownAll(&drain, [&] { drained = true; drain.quit(); });
    while (!drained) drain.exec();
    QVERIFY(executor.isDrained());
    QVERIFY(work_finished.load());
    QCOMPARE(requested.size(), 2);
}

void BootstrapBeforeExecTests::startupPhaseUsesActualDomainOutcome()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> publications;
    bool native_finished{false}, saw_unconsumed{false};
    QTimer driver;
    connect(&driver, &QTimer::timeout, this, [&] {
        if (env.step(execution::Lane::Core)) { saw_unconsumed = native_finished && publications.empty(); return; }
        if (env.step(execution::Lane::Domain)) return;
        if (!publications.empty()) { auto work = std::move(publications.front()); publications.pop_front(); work(); }
    });
    driver.start(0);
    const auto value = startup::Run({env.scheduler, [&](auto work) { publications.push_back(std::move(work)); }}, [&] {
        native_finished = true; return 42;
    });
    driver.stop();
    QCOMPARE(value, 42); QVERIFY(saw_unconsumed); QVERIFY(env.quiescent());
    // A result observed before its queued wakeup still leaves a safe weak turn.
    while (!publications.empty()) { auto work = std::move(publications.front()); publications.pop_front(); work(); }
}

void BootstrapBeforeExecTests::startupPhaseReportsAdmissionAndNativeFailures()
{
    ManualRoutedExecution env;
    execution::SnapshotServices services{env.scheduler, presentation::QueuedPost()};
    env.rejectLane(execution::Lane::Core);
    bool called{false}, rejected{false};
    try { startup::Run(services, [&] { called = true; return false; }); }
    catch (const std::runtime_error&) { rejected = true; }
    QVERIFY(rejected); QVERIFY(!called); QVERIFY(env.quiescent());
    QTimer driver;
    connect(&driver, &QTimer::timeout, this, [&] { env.drain(); });
    driver.start(0);
    QString failure;
    try { startup::Run(services, []() -> bool { throw std::runtime_error{"native startup failed"}; }); }
    catch (const std::runtime_error& error) { failure = QString::fromUtf8(error.what()); }
    driver.stop();
    QCOMPARE(failure, QStringLiteral("native startup failed")); QVERIFY(env.quiescent());
}

void BootstrapBeforeExecTests::startupPhaseKeepsGuiResponsiveOnRealWorker()
{
    BackendExecutor executor{execution::Options{}};
    QSemaphore release;
    std::atomic_bool entered{false}, off_gui{false};
    auto* gui_thread = QThread::currentThread();
    int heartbeats{0};
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, this, [&] { if (entered) { ++heartbeats; release.release(); } });
    heartbeat.start(1);
    const auto value = startup::Run({executor.scheduler(), presentation::QueuedPost()}, [&] {
        off_gui = QThread::currentThread() != gui_thread;
        entered = true; release.acquire(); return 7;
    });
    heartbeat.stop();
    QCOMPARE(value, 7); QVERIFY(off_gui.load()); QVERIFY(heartbeats > 0);
    bool drained{false};
    executor.whenDrained(this, [&] { drained = true; }); executor.shutdown();
    QTRY_VERIFY(drained);
}

void BootstrapBeforeExecTests::nativeInterfacesRetireAfterActorsAndKeepGuiResponsiveUntilFinalJoin()
{
    struct Token {
        QString name; QStringList* order; std::vector<QThread*>* threads;
        QSemaphore* release; std::atomic_bool* entered;
        Token(QString name, QStringList* order, std::vector<QThread*>* threads, QSemaphore* release, std::atomic_bool* entered)
            : name(std::move(name)), order(order), threads(threads), release(release), entered(entered) {}
        ~Token() {
            order->append(name); threads->push_back(QThread::currentThread());
            if (release) { *entered = true; release->acquire(); }
        }
    };
    struct Interfaces { std::unique_ptr<Token> init, node, chain; };
    auto holder = std::make_shared<Interfaces>();
    QStringList order; std::vector<QThread*> threads;
    QSemaphore actor_release, cleanup_release;
    std::atomic_bool actor_entered{false}, actor_finished{false}, cleanup_entered{false};
    BackendExecutor factory{execution::Options{}}, actor{execution::Options{}};
    startup::Run({factory.scheduler(), presentation::QueuedPost()}, [&] {
        holder->init = std::make_unique<Token>("init", &order, &threads, nullptr, nullptr);
        holder->node = std::make_unique<Token>("node", &order, &threads, nullptr, nullptr);
        holder->chain = std::make_unique<Token>("chain", &order, &threads, &cleanup_release, &cleanup_entered);
        return true;
    });
    actor.submit(this, [&] { actor_entered = true; actor_release.acquire(); actor_finished = true; }, [] {});
    bool released_actor{false}, released_cleanup{false}, ordered{true}; int cleanup_heartbeats{0};
    QTimer heartbeat;
    connect(&heartbeat, &QTimer::timeout, this, [&] {
        if (actor_entered && !released_actor) { released_actor = true; actor_release.release(); }
        if (cleanup_entered && !released_cleanup && ++cleanup_heartbeats >= 2) {
            ordered = actor_finished && actor.isDrained() && factory.isDrained();
            released_cleanup = true; cleanup_release.release();
        }
    });
    heartbeat.start(0);
    startup::DrainThenRetire([holder] { holder->chain.reset(); holder->node.reset(); holder->init.reset(); });
    heartbeat.stop();
    QVERIFY(ordered); QVERIFY(released_actor); QVERIFY(released_cleanup); QVERIFY(cleanup_heartbeats >= 2);
    QCOMPARE(order, QStringList({"chain", "node", "init"}));
    QCOMPARE(threads.size(), size_t{3}); for (auto* thread : threads) QVERIFY(thread != QThread::currentThread());
    QVERIFY(!holder->chain && !holder->node && !holder->init);
}

QTEST_MAIN(BootstrapBeforeExecTests)
#include <test_bootstrap.moc>
