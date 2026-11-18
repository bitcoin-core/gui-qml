// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <interfaces/handler.h>
#include <net_processing.h>
#include <node/interface_ui.h>
#include <qml/asyncjoin.h>
#include <qml/execution.h>
#include <qml/executor.h>
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

// Every original Native initialization observes passive startup diagnostics to
// distinguish a Core failure from cancellation. This explicit fixture port is
// separate from runtime question/answer handling and never supplies an answer.
class PassiveStartupNode : public StrictMockNode {
public:
    PassiveStartupNode()
    {
        handle_message_box_fn = [](interfaces::Node::MessageBoxFn) {
            return interfaces::MakeCleanupHandler([] {});
        };
    }
};

// These ordinary-shutdown fixtures explicitly supply the public final status.
// Fault/status receipt rows below provide their own independent nonzero ports.
class NormalExitNode : public PassiveStartupNode {
public:
    std::atomic_int exit_reads{0};
    std::atomic_bool shutdown_returned{false},status_after_shutdown{true},status_off_gui{true};
    std::atomic<QThread*> shutdown_thread{nullptr},status_thread{nullptr};
    void appShutdown() override {
        StrictMockNode::appShutdown();shutdown_thread=QThread::currentThread();shutdown_returned=true;
    }
    int getExitStatus() override {
        ++exit_reads;status_thread=QThread::currentThread();
        if(!shutdown_returned.load())status_after_shutdown=false;
        if(QThread::currentThread()==QCoreApplication::instance()->thread())status_off_gui=false;
        return 0;
    }
};

bool SameThreadAddress(const QThread* first, const QThread* second)
{
    return static_cast<const void*>(first) == static_cast<const void*>(second);
}

void DrainRetiredExecutors()
{
    QEventLoop loop;
    bool drained{false};
    WaitForThreadJoins(&loop, [&] { drained = true; loop.quit(); });
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
    execution::Executor m_executor{execution::Executor::Config{}};

    std::shared_ptr<execution::SerialQueue> makePriorityQueue()
    {
        return std::make_shared<execution::SerialQueue>(m_executor.makeQueue(execution::Lane::Priority));
    }

private Q_SLOTS:
    void stopAdmissionFencesQueuedReadyPresentation_data(){
        QTest::addColumn<bool>("at_ready_signal");QTest::newRow("Native-receipt-before-GUI")<<false;QTest::newRow("reentrant-ready-signal")<<true;
    }
    void stopAdmissionFencesQueuedReadyPresentation(){
        QFETCH(bool,at_ready_signal);PassiveStartupNode node;QSemaphore native_receipt;
        node.app_init_main_fn=[](interfaces::BlockAndHeaderTipInfo*){return true;};
        node.is_initial_block_download_fn=[]{return false;};node.shutdown_requested_fn=[]{return false;};node.start_shutdown_fn=[]{};
        const auto drain=qScopeGuard([]{DrainRetiredExecutors();});
        QmlInitExecutor executor{node, makePriorityQueue(),{},{},[&](auto){native_receipt.release();}};
        bool requested{false};int late_ready{0};
        const auto disconnect=qScopeGuard([&]{QObject::disconnect(&executor,nullptr,this,nullptr);});
        QSignalSpy submitted{&executor,&QmlInitExecutor::acceptingRequestsChanged};
        QVERIFY(executor.acceptingRequests());
        connect(&executor,&QmlInitExecutor::phaseChanged,this,[&]{
            if(executor.phase()!=nodelifecycle::Phase::Ready)return;
            if(requested)++late_ready;
            if(at_ready_signal&&!requested){requested=true;executor.interrupt();QVERIFY(!executor.acceptingRequests());}
        });
        QSignalSpy initialized{&executor,&QmlInitExecutor::initializeResult};executor.initialize();
        if(!at_ready_signal){QVERIFY(native_receipt.tryAcquire(1,SIGNAL_TIMEOUT));requested=true;executor.interrupt();QVERIFY(!executor.acceptingRequests());}
        QTRY_COMPARE(initialized.count(),1);QVERIFY(requested);QCOMPARE(late_ready,0);
        QVERIFY(!initialized.front().front().toBool());QVERIFY(initialized.front().at(3).toBool());
        QVERIFY(executor.initializationOutcome());QVERIFY(executor.initializationAttempted());QVERIFY(executor.initializationSucceeded());
        QCOMPARE(node.calls.appInitMain.load(),1);QTRY_COMPARE(node.calls.startShutdown.load(),1);
        QVERIFY(!executor.acceptingRequests());QCOMPARE(submitted.count(),1);
    }

    void settingsDrainCanRunWhileInitializationWaitsForInterrupt()
    {
        PassiveStartupNode node;
        QSemaphore init_entered, interrupt, write_entered, write_release;
        std::atomic_bool persisted{false}, interrupt_saw_write{false};
        node.is_initial_block_download_fn = [] { return false; };
        node.shutdown_requested_fn = [] { return false; };
        node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
            init_entered.release();
            interrupt.acquire();
            return false;
        };
        node.start_shutdown_fn = [&] {
            interrupt_saw_write = persisted.load();
            interrupt.release();
        };
        node.app_shutdown_fn = [] {};
        const auto drain = qScopeGuard([] { DrainRetiredExecutors(); });
        QmlInitExecutor executor{node, makePriorityQueue()};
        auto core = m_executor.makeQueue(execution::Lane::Core);
        const auto unblock = qScopeGuard([&] { write_release.release(); interrupt.release(); });
        QmlShutdownCoordinator coordinator{executor};
        coordinator.addBeforeInterruptTask([&](auto done) {
            core.close();
            core.whenDrained(std::move(done));
        });
        QSignalSpy finished{&coordinator, &QmlShutdownCoordinator::finished};
        executor.initialize();
        QVERIFY(init_entered.tryAcquire(1, SIGNAL_TIMEOUT));
        QVERIFY(core.post([&] {
            write_entered.release();
            write_release.acquire();
            persisted = true;
        }));
        QVERIFY(write_entered.tryAcquire(1, SIGNAL_TIMEOUT));
        coordinator.requestShutdown();
        QCOMPARE(node.calls.startShutdown.load(), 0);
        QCOMPARE(finished.count(), 0);
        write_release.release();
        QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
        QVERIFY(interrupt_saw_write.load());
        QCOMPARE(node.calls.appShutdown.load(), 1);
    }
    void admissionNotificationMayRetireTheObserver_data(){
        QTest::addColumn<bool>("shutdown");QTest::newRow("interrupt")<<false;QTest::newRow("shutdown")<<true;
    }
    void admissionNotificationMayRetireTheObserver(){
        QFETCH(bool,shutdown);PassiveStartupNode node;
        const auto drain=qScopeGuard([]{DrainRetiredExecutors();});
        auto executor=std::make_unique<QmlInitExecutor>(node, makePriorityQueue());int notices{0};bool closed{false};
        connect(executor.get(),&QmlInitExecutor::acceptingRequestsChanged,this,[&]{
            ++notices;closed=!executor->acceptingRequests();executor.reset();
        });
        if(shutdown)executor->shutdown();else executor->interrupt();
        QVERIFY(!executor);QVERIFY(closed);QCOMPARE(notices,1);
        DrainRetiredExecutors();
        QCOMPARE(node.calls.appInitMain.load(),0);QCOMPARE(node.calls.startShutdown.load(),0);QCOMPARE(node.calls.appShutdown.load(),0);
    }
    void terminalShutdownRejectsLateControlIngress_data(){
        QTest::addColumn<bool>("reentrant");QTest::newRow("Stopped-signal")<<true;QTest::newRow("after-Stopped")<<false;
    }
    void terminalShutdownRejectsLateControlIngress(){
        QFETCH(bool,reentrant);NormalExitNode node;
        node.app_init_main_fn=[](interfaces::BlockAndHeaderTipInfo*){return true;};
        node.is_initial_block_download_fn=[]{return false;};node.shutdown_requested_fn=[]{return false;};
        node.start_shutdown_fn=[]{};node.app_shutdown_fn=[]{};
        const auto drain=qScopeGuard([]{DrainRetiredExecutors();});QmlInitExecutor executor{node, makePriorityQueue()};
        QSignalSpy initialized{&executor,&QmlInitExecutor::initializeResult},finished{&executor,&QmlInitExecutor::shutdownResult},fatal{&executor,&QmlInitExecutor::runawayException};
        int terminal_observations{0};
        const auto disconnect=qScopeGuard([&]{QObject::disconnect(&executor,nullptr,this,nullptr);});
        const auto late=[&]{executor.initialize();executor.interrupt();executor.shutdown();};
        connect(&executor,&QmlInitExecutor::phaseChanged,this,[&]{
            if(executor.phase()==nodelifecycle::Phase::Stopped){++terminal_observations;if(reentrant)late();}
        });
        executor.initialize();QTRY_COMPARE(initialized.count(),1);executor.shutdown();QTRY_COMPARE(finished.count(),1);
        if(!reentrant)late();
        QCOMPARE(fatal.count(),0);QCOMPARE(terminal_observations,1);QCOMPARE(executor.phase(),nodelifecycle::Phase::Stopped);
        QVERIFY(!executor.acceptingRequests());QCOMPARE(node.calls.appInitMain.load(),1);
        QCOMPARE(node.calls.startShutdown.load(),1);QCOMPARE(node.calls.appShutdown.load(),1);QCOMPARE(node.exit_reads.load(),1);
    }
    void nativeCoreExitStatusPrecedesGuiDelivery_data() {
        QTest::addColumn<int>("status");QTest::addColumn<bool>("drop_observer");
        QTest::newRow("success-visible")<<0<<false;QTest::newRow("failure-visible")<<1<<false;
        QTest::newRow("success-observer-dropped")<<0<<true;QTest::newRow("failure-observer-dropped")<<1<<true;
    }
    void nativeCoreExitStatusPrecedesGuiDelivery() {
        QFETCH(int,status);QFETCH(bool,drop_observer);
        struct StatusNode final : PassiveStartupNode {
            std::function<int()> exit_status;
            int getExitStatus() override { return exit_status(); }
        } node;
        std::atomic_bool shutdown_done{false},read_after_shutdown{false},native_only{true};
        std::atomic_int reads{0},recorded{-1};QSemaphore captured;
        const auto gui=QThread::currentThread();
        node.app_init_main_fn=[](interfaces::BlockAndHeaderTipInfo*){return true;};
        node.is_initial_block_download_fn=[]{return false;};node.shutdown_requested_fn=[]{return false;};
        node.start_shutdown_fn=[]{};node.app_shutdown_fn=[&]{shutdown_done=true;};
        node.exit_status=[&]{++reads;read_after_shutdown=shutdown_done.load();if(QThread::currentThread()==gui)native_only=false;return status;};
        node.ExpectExactly(node.calls.appShutdown,1);[[maybe_unused]] const auto verify=node.VerifyOnExit();
        const auto drain_retired=qScopeGuard([]{DrainRetiredExecutors();});
        auto executor=std::make_unique<QmlInitExecutor>(node, makePriorityQueue(),QmlInitExecutor::SubscriptionFactory{},std::shared_ptr<std::atomic_bool>{},QmlInitExecutor::StartupOutcomePort{},[&](int value){
            if(QThread::currentThread()==gui)native_only=false;
            recorded=value;captured.release();
        });
        QSignalSpy initialized{executor.get(),&QmlInitExecutor::initializeResult},shutdown{executor.get(),&QmlInitExecutor::shutdownResult};
        executor->initialize();QTRY_COMPARE(initialized.count(),1);executor->shutdown();
        QVERIFY(!executor->acceptingRequests());
        // Hold queued GUI delivery while the actual Native receipt arrives.
        QVERIFY(captured.tryAcquire(1,SIGNAL_TIMEOUT));QCOMPARE(shutdown.count(),0);
        QCOMPARE(recorded.load(),status);QVERIFY(read_after_shutdown.load());QVERIFY(native_only.load());
        if(drop_observer)executor.reset();else {QTRY_COMPARE(shutdown.count(),1);QCOMPARE(executor->phase(),nodelifecycle::Phase::Stopped);QVERIFY(!executor->acceptingRequests());}
        executor.reset();DrainRetiredExecutors();QCOMPARE(reads.load(),1);QCOMPARE(recorded.load(),status);
    }
    void nativeCoreExitReceiptFailureIsExplicit() {
        struct StatusNode final : PassiveStartupNode { int getExitStatus() override { return 1; } } node;
        node.app_init_main_fn=[](interfaces::BlockAndHeaderTipInfo*){return true;};
        node.is_initial_block_download_fn=[]{return false;};node.shutdown_requested_fn=[]{return false;};node.start_shutdown_fn=[]{};node.app_shutdown_fn=[]{};
        const auto loss=std::make_shared<std::atomic_bool>(false);
        const auto drain_retired=qScopeGuard([]{DrainRetiredExecutors();});
        QmlInitExecutor executor{node, makePriorityQueue(),{},loss,{},[](int){throw std::runtime_error("receipt observer failed");}};
        QSignalSpy initialized{&executor,&QmlInitExecutor::initializeResult},shutdown{&executor,&QmlInitExecutor::shutdownResult};
        executor.initialize();QTRY_COMPARE(initialized.count(),1);executor.shutdown();QTRY_COMPARE(shutdown.count(),1);
        QVERIFY(loss->load());QCOMPARE(node.calls.appShutdown.load(),1);
    }
    void nativeStartupOutcomePrecedesGuiDelivery_data(){QTest::addColumn<int>("scenario");QTest::newRow("ready")<<0;QTest::newRow("failed")<<1;QTest::newRow("user-cancelled")<<2;QTest::newRow("error-and-cancel")<<3;QTest::newRow("uncertain")<<4;}
    void nativeStartupOutcomePrecedesGuiDelivery(){
        QFETCH(int,scenario);PassiveStartupNode node;interfaces::Node::MessageBoxFn message;std::atomic<int> recorded{-1};std::atomic<bool> gui_record{false};
        node.handle_message_box_fn=[&](auto callback){message=std::move(callback);return interfaces::MakeCleanupHandler([&]{message={};});};
        node.app_init_main_fn=[&](interfaces::BlockAndHeaderTipInfo*){if(scenario==3)message(Untranslated("Original startup failure"),CClientUIInterface::ICON_ERROR);if(scenario==4)throw std::runtime_error("Original native startup exception");return scenario==0;};
        node.is_initial_block_download_fn=[] {return false;};node.shutdown_requested_fn=[scenario]{return scenario==2||scenario==3;};
        const auto drain_retired=qScopeGuard([]{DrainRetiredExecutors();});
        QmlInitExecutor executor{node, makePriorityQueue(),{}, {},[&](QmlInitExecutor::StartupOutcome value){gui_record=QThread::currentThread()==QCoreApplication::instance()->thread();recorded=int(value);}};
        QSignalSpy initialized{&executor,&QmlInitExecutor::initializeResult},fatal{&executor,&QmlInitExecutor::runawayException};
        executor.initialize();QTRY_VERIFY(recorded.load()!=-1);
        const auto expected=scenario==0?QmlInitExecutor::StartupOutcome::Ready:scenario==2?QmlInitExecutor::StartupOutcome::Cancelled:scenario==4?QmlInitExecutor::StartupOutcome::Uncertain:QmlInitExecutor::StartupOutcome::Failed;
        QCOMPARE(recorded.load(),int(expected));QVERIFY(!gui_record.load());
        if(scenario==4){QTRY_COMPARE(fatal.count(),1);QCOMPARE(initialized.count(),0);}else{QTRY_COMPARE(initialized.count(),1);QCOMPARE(initialized.front().front().toBool(),scenario==0);}
        QCOMPARE(executor.acceptingRequests(),scenario==0);
    }
    void runawayExtractionAndExceptionRetirementStayNative_data() {
        QTest::addColumn<int>("phase");
        QTest::newRow("initialize") << 0; QTest::newRow("interrupt") << 1; QTest::newRow("shutdown") << 2;
    }
    void runawayExtractionAndExceptionRetirementStayNative() {
        QFETCH(int, phase);
        struct Trace { std::atomic_int calls{0}, destroyed{0}; std::atomic_bool gui_what{false}, gui_destroy{false}; };
        struct Tracked final : std::exception {
            std::shared_ptr<Trace> trace;
            explicit Tracked(std::shared_ptr<Trace> value) : trace(std::move(value)) {}
            const char* what() const noexcept override {
                ++trace->calls;
                if (QThread::currentThread() == QCoreApplication::instance()->thread()) trace->gui_what = true;
                return "Native lifecycle diagnostic";
            }
            ~Tracked() override {
                ++trace->destroyed;
                if (QThread::currentThread() == QCoreApplication::instance()->thread()) trace->gui_destroy = true;
            }
        };
        auto trace = std::make_shared<Trace>();
        PassiveStartupNode node;
        node.is_initial_block_download_fn = [] { return false; }; node.shutdown_requested_fn = [] { return false; };
        node.app_init_main_fn = [trace, phase](interfaces::BlockAndHeaderTipInfo*) {
            if (phase == 0) throw Tracked{trace};
            return true;
        };
        node.start_shutdown_fn = [trace, phase] { if (phase == 1) throw Tracked{trace}; };
        node.app_shutdown_fn = [trace, phase] { if (phase == 2) throw Tracked{trace}; };
        const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
        QmlInitExecutor executor{node, makePriorityQueue()}; QSignalSpy fatal{&executor, &QmlInitExecutor::runawayException};
        QSignalSpy initialized{&executor, &QmlInitExecutor::initializeResult};
        if (phase == 1) executor.interrupt();
        else {
            executor.initialize();
            if (phase == 2) {
                QVERIFY(!initialized.empty() || initialized.wait(SIGNAL_TIMEOUT)); executor.shutdown();
            }
        }
        QVERIFY(!fatal.empty() || fatal.wait(SIGNAL_TIMEOUT)); QCOMPARE(fatal.size(), 1);
        QCOMPARE(fatal[0][0].toString(), QString{"Native lifecycle diagnostic"});
        QVERIFY(trace->calls.load() > 0); QVERIFY(trace->destroyed.load() > 0);
        QVERIFY(!trace->gui_what); QVERIFY(!trace->gui_destroy);
    }
    void initTestCase();
    void subscriptionCleanupPrecedesShutdownFailure();
    void subscriptionCleanupPrecedesShutdown();
    void subscriptionRetirementDoesNotBlockGuiOrInterruption();
    void registrationFailureCleansUpOnWorker();
    void cancelledInitializationDoesNotRegister();
    void unsuccessfulInitializationDoesNotRegister();
    void initializeRegistersBeforeResultOnWorker();
    void destructorCleansUpOnWorker();
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
    void asynchronousRetirementPrecedesInterrupt();
    void portMappingDrainsBeforeSingleInterruption();
};

void QmlInitExecutorApiTests::initTestCase()
{
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");
}

void QmlInitExecutorApiTests::initializeEmitsResultAndRunsOffMainThread()
{
    PassiveStartupNode node;
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

    QmlInitExecutor executor{node, makePriorityQueue()};
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
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return false; };
    node.ExpectNoCalls(node.calls.isInitialBlockDownload);
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue()};
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
    PassiveStartupNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    QSemaphore entered_init, release_init, entered_interrupt, release_interrupt;
    std::atomic_bool interrupted{false};
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) -> bool {
        entered_init.release();
        release_init.acquire();
        throw std::runtime_error{"init failed during shutdown"};
    };
    node.start_shutdown_fn = [&] {
        entered_interrupt.release();
        release_interrupt.acquire();
        interrupted = true;
    };
    node.ExpectNoCalls(node.calls.appShutdown);
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue()};
    const auto release_workers = qScopeGuard([&] { release_init.release(); release_interrupt.release(); });
    QmlShutdownCoordinator coordinator{executor};
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
    QSignalSpy interrupt_spy(&executor, &QmlInitExecutor::interruptResult);
    QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
    executor.initialize();
    QVERIFY(entered_init.tryAcquire(1,SIGNAL_TIMEOUT));
    coordinator.requestShutdown();
    QVERIFY(entered_interrupt.tryAcquire(1, SIGNAL_TIMEOUT));
    release_init.release();
    QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
    release_interrupt.release();
    QTRY_VERIFY_WITH_TIMEOUT(interrupted.load(), SIGNAL_TIMEOUT);
    DrainRetiredExecutors();
    // The original text is still available after the workers have drained.
    QCOMPARE(runaway_spy.count(), 1);
    QVERIFY(runaway_spy.front().front().toString().contains(QStringLiteral("init failed during shutdown")));
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
    NormalExitNode node;
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
    QmlInitExecutor executor{node, makePriorityQueue()};
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
    QCOMPARE(node.exit_reads.load(),fatal?0:1);
    QVERIFY(node.status_after_shutdown.load());QVERIFY(node.status_off_gui.load());
    QVERIFY(SameThreadAddress(node.status_thread.load(), node.shutdown_thread.load()));
}

void QmlInitExecutorApiTests::initializeReportsShutdownRequest()
{
    PassiveStartupNode node;
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
    QmlInitExecutor executor{node, makePriorityQueue()};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(node.calls.shutdownRequested.load(), 1);
    const auto result = initialize_spy.takeFirst();
    QVERIFY(!result.at(0).toBool());
    QVERIFY(executor.initializationOutcome());
    QVERIFY(executor.initializationAttempted());QVERIFY(executor.initializationSucceeded());QVERIFY(executor.initializationCancelled());
    QVERIFY(result.at(3).toBool());
}

void QmlInitExecutorApiTests::initializeEmitsRunawayExceptionOnFailure()
{
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();

    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) -> bool {
        throw std::runtime_error{"init failed"};
    };

    node.ExpectNoCalls(node.calls.shutdownRequested);
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue()};
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
    NormalExitNode node;
    node.start_shutdown_fn = [] {};
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
    QmlInitExecutor executor{node, makePriorityQueue()};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
    executor.initialize();
    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    executor.shutdown();

    QVERIFY(!shutdown_spy.isEmpty() || shutdown_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(runaway_spy.count(), 0);
    QCOMPARE(executor.phase(),nodelifecycle::Phase::Stopped);QVERIFY(!executor.acceptingRequests());
    QVERIFY(shutdown_thread.load() != QCoreApplication::instance()->thread());
    QVERIFY(SameThreadAddress(shutdown_thread.load(), initialization_thread.load()));
    QCOMPARE(node.calls.appShutdown.load(), 1);
    QCOMPARE(node.exit_reads.load(),1);
    QVERIFY(node.status_after_shutdown.load());QVERIFY(node.status_off_gui.load());
    QVERIFY(SameThreadAddress(node.status_thread.load(), node.shutdown_thread.load()));
}

void QmlInitExecutorApiTests::shutdownEmitsRunawayExceptionOnFailure()
{
    PassiveStartupNode node;
    node.start_shutdown_fn = [] {};
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.app_shutdown_fn = [] { throw std::runtime_error{"shutdown failed"}; };

    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue()};
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
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    QSemaphore release;
    std::atomic_bool entered{false};
    std::atomic_bool off_gui{false};
    std::atomic<QThread*> interrupt_thread{nullptr}, priority_thread{nullptr};
    const auto gui_thread = QThread::currentThread();
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        entered = true;
        release.acquire();
        return false;
    };
    node.start_shutdown_fn = [&] {
        auto* thread = QThread::currentThread();
        off_gui = thread != gui_thread;
        interrupt_thread = thread;
        release.release();
    };
    node.app_shutdown_fn = [] {};
    execution::Executor workers{execution::Executor::Config{}};
    const auto priority = std::make_shared<execution::SerialQueue>(workers.makeQueue(execution::Lane::Priority));
    QVERIFY(priority->post([&] { priority_thread = QThread::currentThread(); }));
    QTRY_VERIFY_WITH_TIMEOUT(priority_thread.load(), SIGNAL_TIMEOUT);
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, priority};
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
    QVERIFY(SameThreadAddress(interrupt_thread.load(), priority_thread.load()));
    QCOMPARE(node.calls.startShutdown.load(), 1);
    executor.shutdown();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
}

void QmlInitExecutorApiTests::asynchronousRetirementPrecedesInterrupt()
{
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    node.start_shutdown_fn = [] {};
    node.app_shutdown_fn = [] {};
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue()};
    QmlShutdownCoordinator coordinator{executor};
    bool fenced{false}, began{false};
    std::function<void()> acknowledge;
    coordinator.addStopHook([&] { executor.close(); });
    coordinator.addStopHook([&] { fenced = true; });
    coordinator.addBeforeInterruptTask([&](std::function<void()> done) {
        QVERIFY(fenced);
        began = true;
        acknowledge = std::move(done);
    });
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    coordinator.requestShutdown();
    coordinator.requestShutdown();
    QVERIFY(began);
    QVERIFY(!executor.acceptingRequests());
    QCoreApplication::processEvents();
    QCOMPARE(node.calls.startShutdown.load(), 0);
    QCOMPARE(node.calls.appShutdown.load(), 0);
    acknowledge();
    acknowledge();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
    QCOMPARE(node.calls.startShutdown.load(), 1);
    QCOMPARE(node.calls.appShutdown.load(), 1);
}

void QmlInitExecutorApiTests::shutdownWaitsForInterruptionAndEveryParticipant()
{
    PassiveStartupNode node;
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
    QmlInitExecutor executor{node, makePriorityQueue()};
    const auto unblock = qScopeGuard([&] { release_hook.release(); });
    auto deferred_worker = m_executor.makeQueue(execution::Lane::Utility);
    const auto deferred_drained = std::make_shared<std::atomic_bool>(false);
    const auto worker_entered = std::make_shared<QSemaphore>();
    const auto release_worker = std::make_shared<QSemaphore>();
    const auto unblock_worker = qScopeGuard([release_worker] { release_worker->release(); });
    QVERIFY(deferred_worker.post([worker_entered, release_worker] {
        worker_entered->release();
        release_worker->acquire();
    }));
    QVERIFY(worker_entered->tryAcquire(1, SIGNAL_TIMEOUT));
    deferred_worker.close();
    deferred_worker.whenDrained([deferred_drained] { deferred_drained->store(true); });
    QmlShutdownCoordinator coordinator{executor};
    ShutdownParticipant first, second;
    coordinator.addParticipant(&first, &ShutdownParticipant::drained, [&] { first.started = true; });
    coordinator.addParticipant(&second, &ShutdownParticipant::drained, [&] { second.started = true; });
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    QSignalSpy coordinator_finished{&coordinator, &QmlShutdownCoordinator::finished};
    coordinator.requestShutdown();
    coordinator.requestShutdown();
    QTRY_VERIFY_WITH_TIMEOUT(hook_entered.load(), SIGNAL_TIMEOUT);
    QVERIFY(!executor.acceptingRequests());QVERIFY(executor.phase()!=nodelifecycle::Phase::Stopped);
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
    // The application can now destroy its views. Its final worker join
    // follows that destruction, so this worker can still be active here.
    QTRY_COMPARE_WITH_TIMEOUT(coordinator_finished.count(), 1, SIGNAL_TIMEOUT);
    QVERIFY(!deferred_drained->load());
    QCOMPARE(node.calls.startShutdown.load(), 1);
    QCOMPARE(node.calls.appShutdown.load(), 1);
    QCOMPARE(executor.phase(),nodelifecycle::Phase::Stopped);QVERIFY(!executor.acceptingRequests());
    release_worker->release();
    QTRY_VERIFY_WITH_TIMEOUT(deferred_drained->load(), SIGNAL_TIMEOUT);
}

void QmlInitExecutorApiTests::portMappingDrainsBeforeSingleInterruption()
{
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    node.start_shutdown_fn = [] {};
    node.app_shutdown_fn = [] {};
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue()};
    QmlShutdownCoordinator coordinator{executor};
    ShutdownParticipant settings;
    int fences{0};
    coordinator.addStopHook([&] { ++fences; });
    coordinator.addStopHook([&] { ++fences; });
    bool ui_notified{false};
    connect(&coordinator, &QmlShutdownCoordinator::shutdownStarted, this, [&] {
        QCOMPARE(fences, 2);
        QVERIFY(!settings.started);
        QCOMPARE(node.calls.startShutdown.load(), 0);
        ui_notified = true;
        coordinator.requestShutdown(); // Reentrant focus handlers cannot restart shutdown.
    });
    coordinator.addBeforeInterruptParticipant(&settings, &ShutdownParticipant::drained, [&] { settings.started = true; });
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    coordinator.requestShutdown();
    QVERIFY(ui_notified);
    QCOMPARE(fences, 2);
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

void QmlInitExecutorApiTests::destructorCleansUpOnWorker()
{
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.shutdown_requested_fn = [] { return false; };
    node.ExpectNoCalls(node.calls.appShutdown);
    std::atomic<QThread*> registration_thread{nullptr};
    std::atomic<QThread*> cleanup_thread{nullptr};
    std::atomic_int cleanup_count{0};
    {
        const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
        QmlInitExecutor executor{node, makePriorityQueue(), [&] {
            registration_thread = QThread::currentThread();
            return interfaces::MakeCleanupHandler([&] {
                cleanup_thread = QThread::currentThread();
                ++cleanup_count;
            });
        }};
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        executor.initialize();
        QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(cleanup_count.load(), 0);
    }
    QTRY_COMPARE_WITH_TIMEOUT(cleanup_count.load(), 1, SIGNAL_TIMEOUT);
    QVERIFY(cleanup_thread.load() != QCoreApplication::instance()->thread());
    QVERIFY(SameThreadAddress(cleanup_thread.load(), registration_thread.load()));
}

void QmlInitExecutorApiTests::initializeRegistersBeforeResultOnWorker()
{
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool initialized{false};
    std::atomic_bool registered_after_initialization{false};
    std::atomic_bool registration_complete_at_result_emission{false};
    std::atomic<QThread*> initialization_thread{nullptr};
    std::atomic<QThread*> registration_thread{nullptr};
    std::atomic_int registration_count{0};
    std::atomic_int cleanup_count{0};
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        initialization_thread = QThread::currentThread();
        initialized = true;
        return true;
    };
    node.shutdown_requested_fn = [] { return false; };

    {
        const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
        QmlInitExecutor executor{node, makePriorityQueue(), [&] {
            registered_after_initialization = initialized.load();
            registration_thread = QThread::currentThread();
            ++registration_count;
            return interfaces::MakeCleanupHandler([&] { ++cleanup_count; });
        }};
        QObject::connect(&executor, &QmlInitExecutor::initializeResult, &executor, [&] {
            registration_complete_at_result_emission = registration_count.load() == 1 && cleanup_count.load() == 0;
        }, Qt::DirectConnection);
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        executor.initialize();

        QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
        QVERIFY(initialize_spy.takeFirst().at(0).toBool());
        QVERIFY(registered_after_initialization.load());
        QVERIFY(registration_complete_at_result_emission.load());
        QCOMPARE(registration_count.load(), 1);
        QCOMPARE(node.calls.shutdownRequested.load(), 2);
        QVERIFY(registration_thread.load() == initialization_thread.load());
        QVERIFY(registration_thread.load() != QCoreApplication::instance()->thread());
        QCOMPARE(cleanup_count.load(), 0);
    }
    QTRY_COMPARE_WITH_TIMEOUT(cleanup_count.load(), 1, SIGNAL_TIMEOUT);
}

void QmlInitExecutorApiTests::unsuccessfulInitializationDoesNotRegister()
{
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return false; };
    node.ExpectNoCalls(node.calls.isInitialBlockDownload);
    std::atomic_int registration_count{0};
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue(), [&] {
        ++registration_count;
        return std::unique_ptr<interfaces::Handler>{};
    }};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(initialize_spy.count(), 1);
    QVERIFY(!initialize_spy.takeFirst().at(0).toBool());
    QCOMPARE(runaway_spy.count(), 0);
    QCOMPARE(registration_count.load(), 0);
}

void QmlInitExecutorApiTests::cancelledInitializationDoesNotRegister()
{
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool shutdown_requested{false};
    node.app_init_main_fn = [&](interfaces::BlockAndHeaderTipInfo*) {
        shutdown_requested = true;
        return true;
    };
    node.shutdown_requested_fn = [&] { return shutdown_requested.load(); };
    std::atomic_int registration_count{0};
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue(), [&] {
        ++registration_count;
        return std::unique_ptr<interfaces::Handler>{};
    }};
    QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
    executor.initialize();

    QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
    QCOMPARE(node.calls.shutdownRequested.load(), 2);
    QCOMPARE(registration_count.load(), 0);
}

void QmlInitExecutorApiTests::registrationFailureCleansUpOnWorker()
{
    PassiveStartupNode node;
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.shutdown_requested_fn = [] { return false; };
    std::atomic<QThread*> registration_thread{nullptr};
    std::atomic<QThread*> cleanup_thread{nullptr};
    std::atomic_int cleanup_count{0};
    std::atomic_bool cleanup_preceded_exception{false};

    {
        const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
        QmlInitExecutor executor{node, makePriorityQueue(), [&]() -> std::unique_ptr<interfaces::Handler> {
            registration_thread = QThread::currentThread();
            auto registration = interfaces::MakeCleanupHandler([&] {
                cleanup_thread = QThread::currentThread();
                ++cleanup_count;
            });
            throw std::runtime_error{"registration failed"};
        }};
        QObject::connect(&executor, &QmlInitExecutor::runawayException, &executor, [&] {
            cleanup_preceded_exception = cleanup_count.load() == 1;
        }, Qt::DirectConnection);
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
        executor.initialize();

        QVERIFY(!runaway_spy.isEmpty() || runaway_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(runaway_spy.count(), 1);
        QCOMPARE(initialize_spy.count(), 0);
        QCOMPARE(runaway_spy.takeFirst().at(0).toString(), QString{"registration failed"});
        QVERIFY(cleanup_preceded_exception.load());
        QVERIFY(cleanup_thread.load() != QCoreApplication::instance()->thread());
        // QCOMPARE can format the pointers, and this worker has exited.
        QVERIFY(cleanup_thread.load() == registration_thread.load());
    }
    QTRY_COMPARE_WITH_TIMEOUT(cleanup_count.load(), 1, SIGNAL_TIMEOUT);
}

void QmlInitExecutorApiTests::subscriptionRetirementDoesNotBlockGuiOrInterruption()
{
    NormalExitNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    node.start_shutdown_fn = [] {};
    QSemaphore entered;
    QSemaphore release;
    std::atomic_bool retired{false};
    std::atomic_bool retired_off_gui{false};
    std::atomic_bool shutdown_after_retirement{false};
    const auto gui_thread = QThread::currentThread();
    node.app_shutdown_fn = [&] { shutdown_after_retirement = retired.load(); };
    const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
    QmlInitExecutor executor{node, makePriorityQueue(), [&] {
        return interfaces::MakeCleanupHandler([&] {
            retired_off_gui = QThread::currentThread() != gui_thread;
            entered.release();
            release.acquire();
            retired = true;
        });
    }};
    const auto unblock = qScopeGuard([&] { release.release(); });
    QSignalSpy initialized{&executor, &QmlInitExecutor::initializeResult};
    QSignalSpy interrupted{&executor, &QmlInitExecutor::interruptResult};
    QSignalSpy finished{&executor, &QmlInitExecutor::shutdownResult};
    executor.initialize();
    QTRY_COMPARE_WITH_TIMEOUT(initialized.count(), 1, SIGNAL_TIMEOUT);
    executor.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, SIGNAL_TIMEOUT);
    executor.interrupt();
    QTRY_COMPARE_WITH_TIMEOUT(interrupted.count(), 1, SIGNAL_TIMEOUT);
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, SIGNAL_TIMEOUT);
    QVERIFY(retired_off_gui.load());
    QCOMPARE(node.calls.appShutdown.load(), 0);
    QCOMPARE(finished.count(), 0);
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(finished.count(), 1, SIGNAL_TIMEOUT);
    QVERIFY(shutdown_after_retirement.load());
    QCOMPARE(node.exit_reads.load(),1);
    QVERIFY(node.status_after_shutdown.load());QVERIFY(node.status_off_gui.load());
    QVERIFY(SameThreadAddress(node.status_thread.load(), node.shutdown_thread.load()));
}

void QmlInitExecutorApiTests::subscriptionCleanupPrecedesShutdown()
{
    NormalExitNode node;
    node.start_shutdown_fn = [] {};
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_bool ran_off_main_thread{false};
    std::atomic<QThread*> registration_thread{nullptr};
    std::atomic<QThread*> cleanup_thread{nullptr};
    std::atomic_int cleanup_count{0};
    std::atomic_bool cleanup_preceded_shutdown{false};

    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.shutdown_requested_fn = [] { return false; };
    node.app_shutdown_fn = [&] {
        cleanup_preceded_shutdown = cleanup_count.load() == 1 && cleanup_thread.load() == QThread::currentThread();
        ran_off_main_thread = QThread::currentThread() != QCoreApplication::instance()->thread();
    };

    {
        const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
        QmlInitExecutor executor{node, makePriorityQueue(), [&] {
            registration_thread = QThread::currentThread();
            return interfaces::MakeCleanupHandler([&] {
                cleanup_thread = QThread::currentThread();
                ++cleanup_count;
            });
        }};
        QSignalSpy initialize_spy(&executor, &QmlInitExecutor::initializeResult);
        QSignalSpy shutdown_spy(&executor, &QmlInitExecutor::shutdownResult);
        QSignalSpy runaway_spy(&executor, &QmlInitExecutor::runawayException);
        executor.initialize();
        QVERIFY(!initialize_spy.isEmpty() || initialize_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(cleanup_count.load(), 0);
        executor.shutdown();

        QVERIFY(!shutdown_spy.isEmpty() || shutdown_spy.wait(SIGNAL_TIMEOUT));
        QCOMPARE(shutdown_spy.count(), 1);
        QCOMPARE(runaway_spy.count(), 0);
        QVERIFY(ran_off_main_thread.load());
        QVERIFY(cleanup_preceded_shutdown.load());
        QVERIFY(SameThreadAddress(cleanup_thread.load(), registration_thread.load()));
        QCOMPARE(node.calls.appShutdown.load(), 1);
    }
    QTRY_COMPARE_WITH_TIMEOUT(cleanup_count.load(), 1, SIGNAL_TIMEOUT);
    QCOMPARE(node.exit_reads.load(),1);
    QVERIFY(node.status_after_shutdown.load());QVERIFY(node.status_off_gui.load());
    QVERIFY(SameThreadAddress(node.status_thread.load(), node.shutdown_thread.load()));
}

void QmlInitExecutorApiTests::subscriptionCleanupPrecedesShutdownFailure()
{
    PassiveStartupNode node;
    node.start_shutdown_fn = [] {};
    node.is_initial_block_download_fn = [] { return false; };
    node.shutdown_requested_fn = [] { return false; };
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    std::atomic_int cleanup_count{0};
    std::atomic_bool cleanup_ran_off_main_thread{false};
    std::atomic_bool cleanup_preceded_shutdown{false};

    node.app_init_main_fn = [](interfaces::BlockAndHeaderTipInfo*) { return true; };
    node.shutdown_requested_fn = [] { return false; };
    node.app_shutdown_fn = [&] {
        cleanup_preceded_shutdown = cleanup_count.load() == 1;
        throw std::runtime_error{"shutdown failed"};
    };

    {
        const auto drain_retired = qScopeGuard([] { DrainRetiredExecutors(); });
        QmlInitExecutor executor{node, makePriorityQueue(), [&] {
            return interfaces::MakeCleanupHandler([&] {
                cleanup_ran_off_main_thread = QThread::currentThread() != QCoreApplication::instance()->thread();
                ++cleanup_count;
            });
        }};
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
        QVERIFY(cleanup_preceded_shutdown.load());
        QVERIFY(cleanup_ran_off_main_thread.load());
    }
    QTRY_COMPARE_WITH_TIMEOUT(cleanup_count.load(), 1, SIGNAL_TIMEOUT);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QmlInitExecutorApiTests)
#else
QTEST_MAIN(QmlInitExecutorApiTests)
#endif
#include <test_qmlinitexecutor_api.moc>
