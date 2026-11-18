// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/executor.h>
#include <qml/ownedwork.h>
#include <qml/retainedsnapshot.h>
#include <qml/asyncjoin.h>

#include <QCoreApplication>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>
#include <memory>
#include <set>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
constexpr int TIMEOUT{5'000};
struct Gate {
    QSemaphore entered;
    QSemaphore release;
};
}

class BackendExecutorTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void lastIdleOriginalScopeWakesActualNativeShutdown_data()
    {
        QTest::addColumn<bool>("routed");
        QTest::newRow("legacy-single-worker") << false;
        QTest::newRow("routed-five-workers") << true;
    }
    void lastIdleOriginalScopeWakesActualNativeShutdown()
    {
        QFETCH(bool, routed);
        auto executor = routed ? std::make_unique<BackendExecutor>(execution::Options{}) : std::make_unique<BackendExecutor>();
        auto scope = executor->scheduler()->openActionScope(); QVERIFY(scope);
        struct Probe { std::atomic<int> acknowledged{0}; std::atomic_bool on_gui{false}; };
        const auto probe = std::make_shared<Probe>();
        executor->m_shutdown_started_for_testing = [probe] { if (QThread::currentThread() == qApp->thread()) probe->on_gui = true; ++probe->acknowledged; };
        const auto cleanup = qScopeGuard([&] { scope.reset(); executor->shutdown(); QVERIFY(QTest::qWaitFor([&] { return executor->isDrained(); }, TIMEOUT)); });
        QSignalSpy drained(executor.get(), &BackendExecutor::drained);
        executor->shutdown();
        QTRY_COMPARE_WITH_TIMEOUT(probe->acknowledged.load(), routed ? 5 : 1, TIMEOUT);
        QVERIFY(!probe->on_gui); QVERIFY(!executor->isDrained()); QCOMPARE(drained.size(), 0);
        bool heartbeat{false}; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY_WITH_TIMEOUT(heartbeat, TIMEOUT);
        scope.reset(); // No task, result, resource borrower or cleanup turn remains to wake a worker.
        QTRY_VERIFY_WITH_TIMEOUT(executor->isDrained(), TIMEOUT); QCOMPARE(drained.size(), 1);
    }

    void compatibilityWorkCannotBlockMigratedCoreReads()
    {
        using namespace execution;
        BackendExecutor executor{Options{}};
        const auto scheduler = executor.scheduler();
        const auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        struct Probe {
            std::atomic<QThread*> compatibility{nullptr}, core{nullptr};
            QSemaphore outcome;
            std::atomic_int value{0};
        };
        const auto probe = std::make_shared<Probe>();
        const auto gui = QThread::currentThread();
        QVERIFY(executor.submit(this, [gate, probe] {
            probe->compatibility = QThread::currentThread();
            gate->entered.release();
            gate->release.acquire();
        }, [] {}));
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        QVERIFY(Submit(*scheduler, [probe] {
            probe->core = QThread::currentThread();
            return 42;
        }, [probe](int value) {
            probe->value = value;
            probe->outcome.release();
        }));
        // No GUI event processing and no legacy completion are needed for the
        // migrated read and its owned Domain outcome to make progress.
        QVERIFY(probe->outcome.tryAcquire(1, TIMEOUT));
        QCOMPARE(probe->value.load(), 42);
        QVERIFY(probe->compatibility.load() != gui);
        QVERIFY(probe->core.load() != gui);
        QVERIFY(probe->compatibility.load() != probe->core.load());
        QSignalSpy drained{&executor, &BackendExecutor::drained};
        executor.shutdown();
        QVERIFY(drained.empty());
        gate->release.release();
        QTRY_COMPARE(drained.size(), 1);
        QCOMPARE(scheduler->pending(), size_t{0});
        QVERIFY(executor.isDrained());
    }
    void activeGuiCallbackRetainsOwningPacketThroughReentrantShutdown()
    {
        struct Probe {
            std::atomic_bool destroyed{false}, off_gui{false}, callback_done{false}, shutdown_seen{false};
            QSemaphore inactive_released;
        };
        auto probe = std::make_shared<Probe>();
        const auto gui = QThread::currentThread();
        struct Value {
            std::shared_ptr<Probe> probe;
            QThread* gui;
            bool marker;
            Value(std::shared_ptr<Probe> p, QThread* thread, bool is_marker) : probe(std::move(p)), gui(thread), marker(is_marker) {}
            ~Value()
            {
                if (marker) probe->inactive_released.release();
                else { probe->off_gui = QThread::currentThread() != gui; probe->destroyed = true; }
            }
        };
        auto executor = std::make_unique<BackendExecutor>();
        QVERIFY(executor->submit(this, [probe, gui] { return std::make_shared<Value>(probe, gui, false); },
            [&, probe, gui](const std::shared_ptr<Value>& value) {
                if (!value) return;
                executor->submit(this, [probe, gui] { return std::make_shared<Value>(probe, gui, true); }, [](const std::shared_ptr<Value>&) {});
                executor.reset();
                // The inactive marker is released only by beginShutdown. Hold
                // GUI completion until that actual worker boundary is reached.
                probe->shutdown_seen = probe->inactive_released.tryAcquire(1, TIMEOUT);
                probe->callback_done = true;
            }));
        QTRY_VERIFY(probe->callback_done.load());
        QVERIFY(probe->shutdown_seen.load());
        QTRY_VERIFY(probe->destroyed.load());
        QVERIFY(probe->off_gui.load());
    }
    void routedControlAndCleanupProgressWhileCoreIsHeld()
    {
        using namespace execution;
        BackendExecutor executor{Options{}};
        auto scheduler = executor.scheduler();
        auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        const auto gui = QThread::currentThread();
        struct Probe {
            std::atomic<QThread*> core{nullptr}, domain{nullptr}, control{nullptr}, cleanup{nullptr};
            std::atomic<int> written{0}, consumed{0};
            QSemaphore control_done, cleanup_done;
            bool heartbeat{false};
        };
        const auto probe = std::make_shared<Probe>();
        QVERIFY(Submit(*scheduler, [gate, probe] {
            probe->core = QThread::currentThread();
            gate->entered.release(); gate->release.acquire();
            probe->written = 1;
            return 42;
        }, [probe](int value) { probe->domain = QThread::currentThread(); probe->consumed = value; }));
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        scheduler->stopAcceptingWork();
        QCOMPARE(Submit(*scheduler, [] {}, [] {}).rejection, Rejection::Closed);
        QVERIFY(Submit(*scheduler, [probe] { probe->control = QThread::currentThread(); }, [probe] {
            probe->domain = QThread::currentThread(); probe->control_done.release();
        }, {}, {Lane::Control}));
        QVERIFY(Submit(*scheduler, [probe] { probe->cleanup = QThread::currentThread(); }, [probe] {
            probe->domain = QThread::currentThread(); probe->cleanup_done.release();
        }, {}, {Lane::Cleanup}));
        // GUI processing is withheld: all three background transports progress.
        QVERIFY(probe->control_done.tryAcquire(1, TIMEOUT));
        QVERIFY(probe->cleanup_done.tryAcquire(1, TIMEOUT));
        QCOMPARE(probe->written.load(), 0);
        for (auto* thread : {probe->core.load(), probe->domain.load(), probe->control.load(), probe->cleanup.load()}) {
            QVERIFY(thread); QVERIFY(thread != gui);
        }
        QCOMPARE(std::set<QThread*>({probe->core.load(), probe->domain.load(), probe->control.load(), probe->cleanup.load()}).size(), size_t{4});
        QSignalSpy drained{&executor, &BackendExecutor::drained};
        executor.shutdown();
        QTimer::singleShot(0, &executor, [probe] { probe->heartbeat = true; });
        QTRY_VERIFY(probe->heartbeat);
        QVERIFY(drained.empty());
        QCOMPARE(probe->consumed.load(), 0);
        gate->release.release();
        QTRY_COMPARE(drained.size(), 1);
        QCOMPARE(probe->written.load(), 1);
        QCOMPARE(probe->consumed.load(), 42);
        QCOMPARE(scheduler->pending(), size_t{0});
        QVERIFY(executor.isDrained());
    }
    void nativeCleanupAffinityRetiresOnCoreWhileControlRemainsIndependent()
    {
        using namespace execution;
        BackendExecutor executor{Options{},nullptr,BackendExecutor::CleanupAffinity::Core};
        const auto scheduler=executor.scheduler();
        const auto gate=std::make_shared<Gate>();
        const auto release=qScopeGuard([gate]{gate->release.release();});
        struct Probe {
            std::atomic<QThread*> core{nullptr},cleanup{nullptr},control{nullptr};
            std::atomic_int retired{0};
            std::atomic_bool heartbeat{false};
            QSemaphore read,interrupted;
        };
        const auto probe=std::make_shared<Probe>();
        QVERIFY(Submit(*scheduler,[probe]{probe->core=QThread::currentThread();},[probe]{probe->read.release();}));
        QVERIFY(probe->read.tryAcquire(1,TIMEOUT));
        struct Native {
            std::shared_ptr<Probe> probe;std::shared_ptr<Gate> gate;
            Native(std::shared_ptr<Probe> p,std::shared_ptr<Gate> g):probe(std::move(p)),gate(std::move(g)){}
            ~Native(){probe->cleanup=QThread::currentThread();gate->entered.release();gate->release.acquire();++probe->retired;}
        };
        auto value=RetainSnapshot<Native>(scheduler,[&]{return std::make_unique<Native>(probe,gate);});
        QVERIFY(value);value.reset();
        QVERIFY(gate->entered.tryAcquire(1,TIMEOUT));
        QCOMPARE(probe->cleanup.load(),probe->core.load());QVERIFY(probe->core.load()!=qApp->thread());
        QVERIFY(Submit(*scheduler,[probe]{probe->control=QThread::currentThread();probe->interrupted.release();},[] {},{}, {Lane::Control}));
        QVERIFY(probe->interrupted.tryAcquire(1,TIMEOUT));
        QVERIFY(probe->control.load()!=probe->core.load());QVERIFY(probe->control.load()!=qApp->thread());
        QTimer::singleShot(0,this,[probe]{probe->heartbeat=true;});
        executor.shutdown();QTRY_VERIFY(probe->heartbeat);QVERIFY(!executor.isDrained());QCOMPARE(probe->retired.load(),0);
        gate->release.release();QTRY_VERIFY(executor.isDrained());QCOMPARE(probe->retired.load(),1);
    }
    void routedOwningOutcomeSurvivesDestroyedGuiOwner()
    {
        using namespace execution;
        auto executor = std::make_unique<BackendExecutor>(Options{});
        auto scheduler = executor->scheduler();
        auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        const auto gui = QThread::currentThread();
        struct Probe {
            QSemaphore wrote;
            std::atomic<int> effects{0}, outcomes{0};
            bool drained{false}, heartbeat{false};
        };
        const auto probe = std::make_shared<Probe>();
        auto destroyed = std::make_shared<std::atomic_bool>(false);
        auto destroyed_off_gui = std::make_shared<std::atomic_bool>(false);
        struct Value {
            std::shared_ptr<std::atomic_bool> destroyed, off_gui;
            QThread* gui;
            Value(std::shared_ptr<std::atomic_bool> d, std::shared_ptr<std::atomic_bool> off, QThread* thread)
                : destroyed(std::move(d)), off_gui(std::move(off)), gui(thread) {}
            ~Value() { *off_gui = QThread::currentThread() != gui; *destroyed = true; }
        };
        QVERIFY(Submit(*scheduler, [gate] {
            gate->entered.release(); gate->release.acquire();
        }, [] {}, {}, {Lane::Domain}));
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        QVERIFY(Submit(*scheduler, [probe, destroyed, destroyed_off_gui, gui] {
            auto result = std::make_unique<Value>(destroyed, destroyed_off_gui, gui);
            ++probe->effects; probe->wrote.release(); return result;
        }, [probe, gui](std::unique_ptr<Value> value) {
            if (value && probe->effects == 1 && QThread::currentThread() != gui) ++probe->outcomes;
        }));
        QVERIFY(probe->wrote.tryAcquire(1, TIMEOUT));
        executor.reset();
        QCOMPARE(probe->effects.load(), 1);
        QCOMPARE(probe->outcomes.load(), 0);
        QVERIFY(!destroyed->load());
        WaitForThreadJoins(this, [probe] { probe->drained = true; });
        QTimer::singleShot(0, this, [probe] { probe->heartbeat = true; });
        QTRY_VERIFY(probe->heartbeat);
        QVERIFY(!probe->drained);
        gate->release.release();
        QTRY_VERIFY(probe->drained);
        QCOMPARE(probe->outcomes.load(), 1);
        QVERIFY(destroyed->load()); QVERIFY(destroyed_off_gui->load());
        QCOMPARE(scheduler->pending(), size_t{0});
    }
    void domainCompletionDoesNotNeedGuiTurn() {
        BackendExecutor executor;
        auto scheduler = executor.scheduler();
        QSemaphore completed;
        std::atomic<bool> worker_ok{false}, completion_ok{false};
        execution::SubmitResult admitted;
        std::thread submitter{[&] {
            admitted = execution::Submit(*scheduler, [&] {
                worker_ok = QThread::currentThread() != qApp->thread();
                return 17;
            }, [&](int value) {
                completion_ok = value == 17 && QThread::currentThread() != qApp->thread();
                completed.release();
            });
        }};
        submitter.join();
        QVERIFY(admitted);
        // Intentionally withhold GUI processing: a GUI completion hop fails.
        QVERIFY(completed.tryAcquire(1, TIMEOUT));
        QVERIFY(worker_ok);
        QVERIFY(completion_ok);
        QTRY_COMPARE(scheduler->pending(), size_t{0});
    }
    void acceptedDomainOutcomeSurvivesOwnerDestruction() {
        auto executor = std::make_unique<BackendExecutor>();
        auto scheduler = executor->scheduler();
        auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        std::atomic<int> consumed{0};
        QVERIFY(execution::Submit(*scheduler, [gate] {
            gate->entered.release();
            gate->release.acquire();
            return 3;
        }, [&](int value) { consumed = value; }));
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        executor.reset();
        QCOMPARE(execution::Submit(*scheduler, [] {}, [] {}).rejection, execution::Rejection::Closed);
        gate->release.release();
        QTRY_COMPARE(consumed.load(), 3);
        QTRY_COMPARE(scheduler->pending(), size_t{0});
    }
    void threadJoinCompletesOnGui();
    void finalDrainIncludesDestroyedValueOwners();
    void finalDrainWaitsForDeferredThreadCleanup();
    void finalDrainIncludesDestroyedThreadOwner();
    void serialWorkPublishesOwnedResultsOnGui();
    void failureDoesNotStopLaterCommands();
    void receiverDestructionDiscardsCompletion();
    void shutdownDrainsWithoutWaitingAndDiscardsResults();
    void destructionReleasesBackendCapturesOnWorker();
    void ownerDestructionAfterResultOrException();
    void completionCanDestroyExecutor();
    void discardedOwningResultIsDestroyedOnWorker();
    void taskOutcomesAreDeliveredOnGui();
    void queuedTaskCancellationSkipsWork_data();
    void queuedTaskCancellationSkipsWork();
    void runningTaskCancellation_data();
    void runningTaskCancellation();
    void cancellationAfterWorkFinishesKeepsOutcome();
    void rejectedTaskCompletesAsynchronously_data();
    void rejectedTaskCompletesAsynchronously();
    void taskShutdownPreservesOutcomesAndCleanup();
    void taskCompletionSurvivesExecutorDestruction();
    void taskCompletionCanDestroyOwners();
    void taskDrainWaitsForCompletionCleanup_data();
    void taskDrainWaitsForCompletionCleanup();
    void discardedTaskResultIsDestroyedOnWorker_data();
    void discardedTaskResultIsDestroyedOnWorker();
    void drainCallbacksAreQueuedAndLifetimeGuarded_data();
    void drainCallbacksAreQueuedAndLifetimeGuarded();
    void groupedDrainWaitsForIndependentCoreAndOwnedTask();
};

void BackendExecutorTests::threadJoinCompletesOnGui()
{
    const auto gui_thread = QThread::currentThread();
    auto* thread = new QThread{this};
    const QPointer<QThread> thread_guard{thread};
    std::atomic_bool deleted_on_gui{false};
    connect(thread, &QObject::destroyed, [&] {
        deleted_on_gui = QThread::currentThread() == gui_thread;
    });
    thread->start();
    thread->quit();
    bool joined{false};
    JoinThreadAsync(thread, this, [&] {
        QCOMPARE(QThread::currentThread(), gui_thread);
        QVERIFY(!thread_guard);
        joined = true;
    });
    bool drained{false};
    WaitForThreadJoins(this, [&] {
        QCOMPARE(QThread::currentThread(), gui_thread);
        QVERIFY(joined);
        drained = true;
    });
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QVERIFY(deleted_on_gui.load());
}

void BackendExecutorTests::finalDrainIncludesDestroyedThreadOwner()
{
    auto gate = std::make_shared<Gate>();
    const auto release = qScopeGuard([gate] { gate->release.release(); });
    auto owner = std::make_unique<QObject>();
    auto* thread = new QThread{owner.get()};
    auto* worker = new QObject;
    worker->moveToThread(thread);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(worker, &QObject::destroyed, [gate] {
        gate->entered.release();
        gate->release.acquire();
    });
    thread->start();
    thread->quit();
    bool owner_notified{false};
    JoinThreadAsync(thread, owner.get(), [&] { owner_notified = true; });
    owner.reset();
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    QObject receiver;
    bool all_drained{false};
    WaitForThreadJoins(&receiver, [&] { all_drained = true; });
    bool gui_progress{false};
    QTimer::singleShot(0, &receiver, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, TIMEOUT);
    QVERIFY(!all_drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(all_drained, TIMEOUT);
    QVERIFY(!owner_notified);
}

void BackendExecutorTests::finalDrainWaitsForDeferredThreadCleanup()
{
    struct DeferredCleanup : QObject {
        std::shared_ptr<Gate> gate;
        explicit DeferredCleanup(std::shared_ptr<Gate> gate_in) : gate(std::move(gate_in)) {}
        ~DeferredCleanup() override
        {
            gate->entered.release();
            gate->release.acquire();
        }
    };
    auto gate = std::make_shared<Gate>();
    const auto release = qScopeGuard([gate] { gate->release.release(); });
    QSemaphore cancel_gui_deadlock_watchdog;
    std::thread gui_deadlock_watchdog{[gate, &cancel_gui_deadlock_watchdog] {
        if (!cancel_gui_deadlock_watchdog.tryAcquire(1, TIMEOUT)) gate->release.release();
    }};
    const auto stop_gui_deadlock_watchdog = qScopeGuard([&] {
        cancel_gui_deadlock_watchdog.release();
        gui_deadlock_watchdog.join();
    });
    QObject receiver;
    auto* thread = new QThread;
    const QPointer<QThread> thread_guard{thread};
    auto* deferred = new DeferredCleanup{gate};
    deferred->moveToThread(thread);
    const auto job_finished = std::make_shared<std::atomic_bool>(false);
    connect(thread, &QThread::started, deferred, [job_finished] { *job_finished = true; });
    connect(thread, &QThread::finished, deferred, &QObject::deleteLater);
    bool joining{false}, joined{false};
    const auto join = [&] {
        if (std::exchange(joining, true)) return;
        thread->quit();
        JoinThreadAsync(thread, &receiver, [&] { joined = true; });
    };
    const auto cleanup = qScopeGuard([&] {
        gate->release.release();
        join();
    });
    thread->start();
    QTRY_VERIFY_WITH_TIMEOUT(job_finished->load(), TIMEOUT);
    join();
    bool all_drained{false};
    WaitForThreadJoins(&receiver, [&] { all_drained = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    int heartbeats{0};
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
    timer.start(0);
    QTRY_VERIFY_WITH_TIMEOUT(heartbeats > 2, TIMEOUT);
    QVERIFY(thread_guard);
    QVERIFY(!joined);
    QVERIFY(!all_drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(joined, TIMEOUT);
    QTRY_VERIFY_WITH_TIMEOUT(all_drained, TIMEOUT);
    QVERIFY(!thread_guard);
}

void BackendExecutorTests::finalDrainIncludesDestroyedValueOwners()
{
    auto gate = std::make_shared<Gate>();
    const auto finished = std::make_shared<std::atomic_bool>(false);
    auto executor = std::make_shared<execution::Executor>(execution::Executor::Config{});
    auto queue = std::make_unique<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility));
    const auto release = qScopeGuard([gate] { gate->release.release(); });
    QVERIFY(queue->post([gate, finished] {
        gate->entered.release();
        gate->release.acquire();
        *finished = true;
    }));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    queue->close();
    queue.reset();
    QObject receiver;
    bool joined{false};
    JoinThreadAsync([worker = std::move(executor)] { worker->stopAndJoin(); }, &receiver, [&] { joined = true; });
    bool drained{false};
    WaitForThreadJoins(&receiver, [&] { drained = true; });
    int heartbeats{0};
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
    timer.start(0);
    QTRY_VERIFY_WITH_TIMEOUT(heartbeats > 2, TIMEOUT);
    QVERIFY(!joined);
    QVERIFY(!drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QVERIFY(joined);
    QVERIFY(finished->load());
}

void BackendExecutorTests::serialWorkPublishesOwnedResultsOnGui()
{
    execution::Executor executor{execution::Executor::Config{}};
    execution::OwnedWork owner{{}, &executor};
    const auto gui_thread = QThread::currentThread();
    auto sequence = std::make_shared<std::vector<int>>();
    std::vector<int> results;
    std::atomic_bool off_gui{false};
    const auto unexpected = [](std::exception_ptr) { QFAIL("An accepted operation failed"); };
    QVERIFY(owner.submit([sequence, gui_thread, &off_gui] {
        off_gui = QThread::currentThread() != gui_thread;
        sequence->push_back(1);
        return std::make_unique<int>(sequence->back());
    }, [&](const std::unique_ptr<int>& result) {
        QCOMPARE(QThread::currentThread(), gui_thread);
        results.push_back(*result);
    }, unexpected));
    QVERIFY(owner.submit([sequence] {
        sequence->push_back(sequence->back() + 1);
        return sequence->back();
    }, [&](int result) {
        QCOMPARE(QThread::currentThread(), gui_thread);
        results.push_back(result);
    }, unexpected));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), size_t{2}, TIMEOUT);
    QCOMPARE(results, (std::vector<int>{1, 2}));
    QVERIFY(off_gui.load());
}

void BackendExecutorTests::failureDoesNotStopLaterCommands()
{
    execution::Executor executor{execution::Executor::Config{}};
    execution::OwnedWork owner{{}, &executor};
    bool failed{false};
    bool succeeded{false};
    bool wrong_completion{false};
    const auto gui_thread = QThread::currentThread();
    QVERIFY(owner.submit([]() -> int { throw std::runtime_error{"backend error"}; },
        [&](int) { wrong_completion = true; }, [&](std::exception_ptr error) {
            QCOMPARE(QThread::currentThread(), gui_thread);
            try {
                std::rethrow_exception(error);
            } catch (const std::runtime_error& e) {
                QCOMPARE(QString::fromUtf8(e.what()), QString{"backend error"});
                failed = true;
            }
        }));
    QVERIFY(owner.submit([] { return true; }, [&](bool actual) { succeeded = actual; },
        [](std::exception_ptr) { QFAIL("The later operation failed"); }));
    QTRY_VERIFY_WITH_TIMEOUT(failed && succeeded, TIMEOUT);
    QVERIFY(!wrong_completion);
}

void BackendExecutorTests::receiverDestructionDiscardsCompletion()
{
    execution::Executor executor{execution::Executor::Config{}};
    execution::OwnedWork successor{{}, &executor};
    auto receiver = std::make_unique<execution::OwnedWork>(execution::SnapshotServices{}, &executor);
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    bool called{false};
    bool after{false};
    const auto unexpected = [](std::exception_ptr) { QFAIL("An accepted operation failed"); };
    QVERIFY(receiver->submit([gate] {
        gate->entered.release();
        gate->release.acquire();
        return 1;
    }, [&](int) { called = true; }, unexpected));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    const auto original_drained = std::make_shared<std::atomic_bool>(false);
    receiver->close([original_drained] { *original_drained = true; });
    receiver.reset();
    QVERIFY(successor.submit([] { return true; }, [&](bool actual) { after = actual; }, unexpected));
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(original_drained->load() && after, TIMEOUT);
    QVERIFY(!called);
}

void BackendExecutorTests::shutdownDrainsWithoutWaitingAndDiscardsResults()
{
    execution::Executor executor{execution::Executor::Config{}};
    execution::OwnedWork owner{{}, &executor};
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    auto commands = std::make_shared<std::atomic<int>>(0);
    bool applied{false};
    const auto unexpected = [](std::exception_ptr) { QFAIL("An accepted operation failed"); };
    QVERIFY(owner.submit([gate, commands] {
        gate->entered.release();
        gate->release.acquire();
        return ++*commands;
    }, [&](int) { applied = true; }, unexpected));
    QVERIFY(owner.submit([commands] { return ++*commands; }, [&](int) { applied = true; }, unexpected));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    const auto drained = std::make_shared<std::atomic_int>(0);
    owner.close([drained] { ++*drained; });
    owner.close();
    QVERIFY(!owner.submit([] { return true; }, [&](bool) { applied = true; }, [](std::exception_ptr) {}));
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, TIMEOUT);
    QCOMPARE(drained->load(), 0);
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(drained->load(), 1, TIMEOUT);
    QCOMPARE(commands->load(), 2);
    QVERIFY(!applied);
}

void BackendExecutorTests::destructionReleasesBackendCapturesOnWorker()
{
    execution::Executor executor{execution::Executor::Config{}};
    auto owner = std::make_unique<execution::OwnedWork>(execution::SnapshotServices{}, &executor);
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    const auto gui_thread = QThread::currentThread();
    auto released = std::make_shared<std::atomic_bool>(false);
    auto backend = std::shared_ptr<int>(new int{42}, [released, gui_thread](int* value) {
        *released = QThread::currentThread() != gui_thread;
        delete value;
    });
    bool applied{false};
    QVERIFY(owner->submit([gate, backend = std::move(backend)] {
        gate->entered.release();
        gate->release.acquire();
        return *backend;
    }, [&](int) { applied = true; }, [](std::exception_ptr) { QFAIL("An accepted operation failed"); }));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    const auto drained = std::make_shared<std::atomic_bool>(false);
    owner->close([drained] { *drained = true; });
    owner.reset();
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, TIMEOUT);
    QVERIFY(!drained->load());
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained->load(), TIMEOUT);
    QVERIFY(released->load());
    QVERIFY(!applied);
}

void BackendExecutorTests::discardedOwningResultIsDestroyedOnWorker()
{
    for (const bool shutdown : {false, true}) {
        execution::Executor executor{execution::Executor::Config{}};
        auto owner = std::make_unique<execution::OwnedWork>(execution::SnapshotServices{}, &executor);
        auto gate = std::make_shared<Gate>();
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        auto destroyed = std::make_shared<std::atomic_bool>(false);
        const auto gui_thread = QThread::currentThread();
        bool applied{false};
        QVERIFY(owner->submit([gate, destroyed, gui_thread] {
            gate->entered.release();
            gate->release.acquire();
            return std::shared_ptr<int>(new int{1}, [destroyed, gui_thread](int* value) {
                *destroyed = QThread::currentThread() != gui_thread;
                delete value;
            });
        }, [&](const std::shared_ptr<int>&) { applied = true; }, [](std::exception_ptr) { QFAIL("An accepted operation failed"); }));
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
        if (shutdown) owner->close();
        else owner.reset();
        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(destroyed->load(), TIMEOUT);
        QVERIFY(!applied);
    }
}

void BackendExecutorTests::completionCanDestroyExecutor()
{
    for (bool fail : {false, true}) {
        auto executor = std::make_shared<execution::Executor>(execution::Executor::Config{});
        auto owner = std::make_unique<execution::OwnedWork>(execution::SnapshotServices{}, executor.get());
        bool completed{false};
        const auto destroy_owner = [&] {
            owner->whenQueuesDrained(JoinThreadAfterDrain([worker = executor] { worker->stopAndJoin(); }, this, [] {}));
            owner.reset();
            executor.reset();
            completed = true;
        };
        QVERIFY(owner->submit([fail] {
            if (fail) throw std::runtime_error{"backend error"};
            return true;
        }, [&](bool) { destroy_owner(); }, [&](std::exception_ptr) { destroy_owner(); }));
        QTRY_VERIFY_WITH_TIMEOUT(completed, TIMEOUT);
        QVERIFY(!owner);
        QVERIFY(!executor);
        bool drained{false};
        WaitForThreadJoins(this, [&] { drained = true; });
        QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    }
}

void BackendExecutorTests::ownerDestructionAfterResultOrException()
{
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto executor = std::make_shared<execution::Executor>(execution::Executor::Config{});
        auto owner = std::make_unique<execution::OwnedWork>(execution::SnapshotServices{}, executor.get());
        bool completed{false};
        const bool fail = attempt % 2;
        QVERIFY(owner->submit([fail] {
            if (fail) throw std::runtime_error{"backend error"};
            return true;
        }, [&](bool) { completed = true; }, [&](std::exception_ptr) { completed = true; }));
        QTRY_VERIFY_WITH_TIMEOUT(completed, TIMEOUT);
        owner->whenQueuesDrained(JoinThreadAfterDrain([worker = std::move(executor)] { worker->stopAndJoin(); }, this, [] {}));
        owner.reset();
    }
    bool drained{false};
    WaitForThreadJoins(this, [&] { drained = true; });
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
}

void BackendExecutorTests::taskOutcomesAreDeliveredOnGui()
{
    BackendExecutor executor;
    QObject receiver;
    const auto gui_thread = QThread::currentThread();
    auto worker_thread = std::make_shared<std::atomic<QThread*>>(nullptr);
    std::vector<int> completed;
    const auto first = executor.submitTask(&receiver, [worker_thread] {
        *worker_thread = QThread::currentThread();
        return 42;
    }, [&](BackendExecutor::Outcome<int> outcome) {
        completed.push_back(1);
        QCOMPARE(QThread::currentThread(), gui_thread);
        QVERIFY(std::holds_alternative<int>(outcome));
        QCOMPARE(std::get<int>(outcome), 42);
    });
    const auto second = executor.submitTask(&receiver, [] {}, [&](BackendExecutor::Outcome<void> outcome) {
        completed.push_back(2);
        QCOMPARE(QThread::currentThread(), gui_thread);
        QVERIFY(std::holds_alternative<std::monostate>(outcome));
    });
    const auto third = executor.submitTask(&receiver, [value = std::make_unique<int>(7)]() mutable {
        return std::move(value);
    }, [&](BackendExecutor::Outcome<std::unique_ptr<int>> outcome) {
        completed.push_back(3);
        QCOMPARE(QThread::currentThread(), gui_thread);
        QVERIFY(std::holds_alternative<std::unique_ptr<int>>(outcome));
        QCOMPARE(*std::get<std::unique_ptr<int>>(outcome), 7);
    });
    const auto fourth = executor.submitTask(&receiver, []() -> int { throw std::runtime_error{"backend error"}; },
        [&](BackendExecutor::Outcome<int> outcome) {
            completed.push_back(4);
            QCOMPARE(QThread::currentThread(), gui_thread);
            QVERIFY(std::holds_alternative<std::exception_ptr>(outcome));
            try {
                std::rethrow_exception(std::get<std::exception_ptr>(outcome));
            } catch (const std::runtime_error& error) {
                QCOMPARE(QString::fromUtf8(error.what()), QString{"backend error"});
            }
        });
    QVERIFY(first != 0 && first < second && second < third && third < fourth);
    QVERIFY(completed.empty());
    QTRY_COMPARE_WITH_TIMEOUT(completed.size(), size_t{4}, TIMEOUT);
    QCOMPARE(completed, (std::vector<int>{1, 2, 3, 4}));
    QVERIFY(worker_thread->load() != gui_thread);
    QVERIFY(!executor.cancel(first));
    executor.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
    QCOMPARE(completed.size(), size_t{4});
}

void BackendExecutorTests::queuedTaskCancellationSkipsWork_data()
{
    QTest::addColumn<bool>("shutdown");
    QTest::addColumn<bool>("destroy_executor");
    QTest::newRow("running") << false << false;
    QTest::newRow("stopping") << true << false;
    QTest::newRow("executor-destroyed") << false << true;
}

void BackendExecutorTests::queuedTaskCancellationSkipsWork()
{
    QFETCH(bool, shutdown);
    QFETCH(bool, destroy_executor);
    auto executor = std::make_unique<BackendExecutor>();
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    QVERIFY(executor->submit(&receiver, [gate] {
        gate->entered.release();
        gate->release.acquire();
    }, [] {}));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    auto ran = std::make_shared<std::atomic_bool>(false);
    auto released_on_worker = std::make_shared<std::atomic_bool>(false);
    const auto gui_thread = QThread::currentThread();
    auto backend = std::shared_ptr<int>{new int{1}, [released_on_worker, gui_thread](int* value) {
        *released_on_worker = QThread::currentThread() != gui_thread;
        delete value;
    }};
    const std::weak_ptr<int> backend_guard{backend};
    int completions{0};
    const auto id = executor->submitTask(&receiver, [ran, backend = std::move(backend)] {
        *ran = true;
        return *backend;
    }, [&](BackendExecutor::Outcome<int> outcome) {
        ++completions;
        QCOMPARE(QThread::currentThread(), gui_thread);
        QVERIFY(std::holds_alternative<BackendExecutor::Cancelled>(outcome));
        QCOMPARE(std::get<BackendExecutor::Cancelled>(outcome).reason, BackendExecutor::Cancelled::Reason::Requested);
    });
    QVERIFY(executor->cancel(id));
    QVERIFY(!executor->cancel(id));
    QVERIFY(!executor->cancel(0));
    QCOMPARE(completions, 0);
    if (shutdown) executor->shutdown();
    if (destroy_executor) executor.reset();
    QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    QVERIFY(!ran->load());
    QVERIFY(!backend_guard.expired());
    if (executor) {
        QVERIFY(!executor->isDrained());
        QVERIFY(!executor->cancel(id));
        executor->shutdown();
    }
    bool drained{false};
    WaitForThreadJoins(&receiver, [&] {
        drained = true;
        QCOMPARE(completions, 1);
        QVERIFY(backend_guard.expired());
        QVERIFY(released_on_worker->load());
    });
    QVERIFY(!drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QVERIFY(!ran->load());
    QCOMPARE(completions, 1);
}

void BackendExecutorTests::runningTaskCancellation_data()
{
    QTest::addColumn<bool>("discard_result");
    QTest::addColumn<bool>("fail");
    QTest::newRow("keep-value") << false << false;
    QTest::newRow("keep-error") << false << true;
    QTest::newRow("discard-value") << true << false;
    QTest::newRow("discard-error") << true << true;
}

void BackendExecutorTests::runningTaskCancellation()
{
    QFETCH(bool, discard_result);
    QFETCH(bool, fail);
    BackendExecutor executor;
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    auto effects = std::make_shared<std::atomic<int>>(0);
    int completions{0};
    const auto id = executor.submitTask(&receiver, [gate, effects, fail] {
        gate->entered.release();
        gate->release.acquire();
        ++*effects;
        if (fail) throw std::runtime_error{"backend error"};
        return 42;
    }, [&](BackendExecutor::Outcome<int> outcome) {
        ++completions;
        QCOMPARE(effects->load(), 1);
        if (discard_result) {
            QVERIFY(std::holds_alternative<BackendExecutor::Cancelled>(outcome));
            QCOMPARE(std::get<BackendExecutor::Cancelled>(outcome).reason, BackendExecutor::Cancelled::Reason::Requested);
        } else if (fail) {
            QVERIFY(std::holds_alternative<std::exception_ptr>(outcome));
        } else {
            QVERIFY(std::holds_alternative<int>(outcome));
            QCOMPARE(std::get<int>(outcome), 42);
        }
    }, discard_result ? BackendExecutor::CancelPolicy::DiscardRunningResult : BackendExecutor::CancelPolicy::QueuedOnly);
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    QCOMPARE(executor.cancel(id), discard_result);
    QVERIFY(!executor.cancel(id));
    bool gui_progress{false};
    QTimer::singleShot(0, &receiver, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, TIMEOUT);
    QCOMPARE(completions, 0);
    QCOMPARE(effects->load(), 0);
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    QVERIFY(!executor.cancel(id));
    executor.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
    QCOMPARE(completions, 1);
}

void BackendExecutorTests::cancellationAfterWorkFinishesKeepsOutcome()
{
    BackendExecutor executor;
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    int completions{0};
    const auto id = executor.submitTask(&receiver, [] { return 42; }, [&](BackendExecutor::Outcome<int> outcome) {
        ++completions;
        QVERIFY(std::holds_alternative<int>(outcome));
        QCOMPARE(std::get<int>(outcome), 42);
    }, BackendExecutor::CancelPolicy::DiscardRunningResult);
    QVERIFY(executor.submit(&receiver, [gate] {
        gate->entered.release();
        gate->release.acquire();
    }, [] {}));
    // The next job proves the result is settled without dispatching its GUI completion.
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    QCOMPARE(completions, 0);
    QVERIFY(!executor.cancel(id));
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    executor.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
}

void BackendExecutorTests::rejectedTaskCompletesAsynchronously_data()
{
    QTest::addColumn<bool>("already_drained");
    QTest::newRow("stopping") << false;
    QTest::newRow("drained") << true;
}

void BackendExecutorTests::rejectedTaskCompletesAsynchronously()
{
    QFETCH(bool, already_drained);
    BackendExecutor executor;
    QObject receiver;
    executor.shutdown();
    if (already_drained) QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
    auto ran = std::make_shared<std::atomic_bool>(false);
    int completions{0};
    const auto gui_thread = QThread::currentThread();
    const auto id = executor.submitTask(&receiver, [ran] { *ran = true; }, [&](BackendExecutor::Outcome<void> outcome) {
        ++completions;
        QCOMPARE(QThread::currentThread(), gui_thread);
        QVERIFY(std::holds_alternative<BackendExecutor::Cancelled>(outcome));
        QCOMPARE(std::get<BackendExecutor::Cancelled>(outcome).reason, BackendExecutor::Cancelled::Reason::Rejected);
    });
    QVERIFY(id != 0);
    QVERIFY(!executor.cancel(id));
    QCOMPARE(completions, 0);
    QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    QVERIFY(!ran->load());
    QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
    QCOMPARE(completions, 1);
}

void BackendExecutorTests::taskShutdownPreservesOutcomesAndCleanup()
{
    BackendExecutor executor;
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    auto effects = std::make_shared<std::atomic<int>>(0);
    std::vector<int> completed;
    executor.submitTask(&receiver, [gate, effects] {
        gate->entered.release();
        gate->release.acquire();
        return ++*effects;
    }, [&](BackendExecutor::Outcome<int> outcome) {
        QVERIFY(std::holds_alternative<int>(outcome));
        completed.push_back(std::get<int>(outcome));
    });
    executor.submitTask(&receiver, [effects] { return ++*effects; }, [&](BackendExecutor::Outcome<int> outcome) {
        QVERIFY(std::holds_alternative<int>(outcome));
        completed.push_back(std::get<int>(outcome));
    });
    bool legacy_completion{false};
    QVERIFY(executor.submit(&receiver, [effects] { ++*effects; }, [&] { legacy_completion = true; }));
    bool drained{false};
    connect(&executor, &BackendExecutor::drained, &receiver, [&] {
        drained = true;
        QCOMPARE(completed, (std::vector<int>{1, 2}));
        QCOMPARE(effects->load(), 3);
    });
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    executor.shutdown();
    executor.shutdown();
    QVERIFY(!drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QVERIFY(!legacy_completion);
    QCOMPARE(completed, (std::vector<int>{1, 2}));
}

void BackendExecutorTests::taskCompletionSurvivesExecutorDestruction()
{
    auto executor = std::make_unique<BackendExecutor>();
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    int completions{0};
    executor->submitTask(&receiver, [gate] {
        gate->entered.release();
        gate->release.acquire();
        return 42;
    }, [&](BackendExecutor::Outcome<int> outcome) {
        ++completions;
        QVERIFY(std::holds_alternative<int>(outcome));
        QCOMPARE(std::get<int>(outcome), 42);
    });
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    bool executor_drained{false};
    executor->whenDrained(&receiver, [&] {
        executor_drained = true;
        QCOMPARE(completions, 1);
    });
    executor.reset();
    bool drained{false};
    WaitForThreadJoins(&receiver, [&] {
        drained = true;
        QCOMPARE(completions, 1);
    });
    QVERIFY(!drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QTRY_VERIFY_WITH_TIMEOUT(executor_drained, TIMEOUT);
    QCOMPARE(completions, 1);
}

void BackendExecutorTests::taskCompletionCanDestroyOwners()
{
    for (const bool fail : {false, true}) {
        auto executor = std::make_unique<BackendExecutor>();
        auto receiver = std::make_unique<QObject>();
        int completions{0};
        executor->submitTask(receiver.get(), [fail] {
            if (fail) throw std::runtime_error{"backend error"};
        }, [&](BackendExecutor::Outcome<void> outcome) {
            ++completions;
            receiver.reset();
            executor.reset();
            QCOMPARE(std::holds_alternative<std::exception_ptr>(outcome), fail);
        });
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
        QVERIFY(!executor);
        QVERIFY(!receiver);
        bool drained{false};
        WaitForThreadJoins(this, [&] { drained = true; });
        QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
        QCOMPARE(completions, 1);
    }
}

void BackendExecutorTests::taskDrainWaitsForCompletionCleanup_data()
{
    QTest::addColumn<bool>("bounded");
    QTest::newRow("compatibility") << false;
    QTest::newRow("bounded-group") << true;
}

void BackendExecutorTests::taskDrainWaitsForCompletionCleanup()
{
    QFETCH(bool, bounded);
    auto owner = bounded ? std::make_unique<BackendExecutor>(execution::Options{}) : std::make_unique<BackendExecutor>();
    auto& executor = *owner;
    QObject receiver;
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    const auto gui_thread = QThread::currentThread();
    auto worker_result = std::make_shared<std::weak_ptr<QObject>>();
    auto result_released_on_worker = std::make_shared<std::atomic_bool>(false);
    bool callback_active{false};
    std::atomic_bool completion_released{false};
    std::atomic_bool released_on_gui{false};
    auto completion_capture = std::shared_ptr<int>{new int{1}, [&](int* value) {
        completion_released = true;
        released_on_gui = QThread::currentThread() == gui_thread;
        delete value;
    }};
    bool drained{false};
    executor.whenDrained(&receiver, [&] {
        drained = true;
        QVERIFY(!callback_active);
        QVERIFY(completion_released.load());
        QVERIFY(released_on_gui.load());
        QVERIFY(result_released_on_worker->load());
    });
    int completions{0};
    const auto id = executor.submitTask(&receiver, [gate, gui_thread, worker_result, result_released_on_worker] {
        gate->entered.release();
        gate->release.acquire();
        auto result = std::shared_ptr<QObject>{new QObject, [gui_thread, result_released_on_worker](QObject* value) {
            *result_released_on_worker = QThread::currentThread() != gui_thread;
            delete value;
        }};
        *worker_result = result;
        return result;
    }, [&, completion_capture = std::move(completion_capture)](BackendExecutor::Outcome<std::shared_ptr<QObject>> outcome) {
        ++completions;
        QVERIFY(std::holds_alternative<BackendExecutor::Cancelled>(outcome));
        QVERIFY(completion_capture);
        callback_active = true;
        executor.shutdown();
        auto probe = worker_result->lock();
        QVERIFY(probe);
        auto acknowledged = std::make_shared<bool>(false);
        // The second worker hop follows any cleanup queued by the shutdown
        // barrier while this completion is still inside a nested event loop.
        QMetaObject::invokeMethod(probe.get(), [probe, acknowledged] {
            QMetaObject::invokeMethod(QCoreApplication::instance(), [probe, acknowledged] {
                QMetaObject::invokeMethod(probe.get(), [probe, acknowledged]() mutable {
                    probe.reset();
                    QMetaObject::invokeMethod(QCoreApplication::instance(), [acknowledged] {
                        *acknowledged = true;
                    }, Qt::QueuedConnection);
                }, Qt::QueuedConnection);
            }, Qt::QueuedConnection);
        }, Qt::QueuedConnection);
        QTRY_VERIFY_WITH_TIMEOUT(*acknowledged, TIMEOUT);
        QVERIFY(!drained);
        QVERIFY(!executor.isDrained());
        QVERIFY(!completion_released.load());
        callback_active = false;
    }, BackendExecutor::CancelPolicy::DiscardRunningResult);
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    QVERIFY(executor.cancel(id));
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QCOMPARE(completions, 1);
}

void BackendExecutorTests::discardedTaskResultIsDestroyedOnWorker_data()
{
    QTest::addColumn<bool>("cancel");
    QTest::addColumn<bool>("destroy_executor");
    QTest::newRow("receiver-destroyed") << false << false;
    QTest::newRow("running-cancelled") << true << false;
    QTest::newRow("running-cancelled-executor-destroyed") << true << true;
}

void BackendExecutorTests::discardedTaskResultIsDestroyedOnWorker()
{
    QFETCH(bool, cancel);
    QFETCH(bool, destroy_executor);
    auto executor = std::make_unique<BackendExecutor>();
    auto receiver = std::make_unique<QObject>();
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    auto destroyed_on_worker = std::make_shared<std::atomic_bool>(false);
    const auto gui_thread = QThread::currentThread();
    int completions{0};
    const auto id = executor->submitTask(receiver.get(), [gate, destroyed_on_worker, gui_thread] {
        gate->entered.release();
        gate->release.acquire();
        return std::shared_ptr<int>{new int{42}, [destroyed_on_worker, gui_thread](int* value) {
            *destroyed_on_worker = QThread::currentThread() != gui_thread;
            delete value;
        }};
    }, [&](BackendExecutor::Outcome<std::shared_ptr<int>> outcome) {
        ++completions;
        QVERIFY(std::holds_alternative<BackendExecutor::Cancelled>(outcome));
        QCOMPARE(std::get<BackendExecutor::Cancelled>(outcome).reason, BackendExecutor::Cancelled::Reason::Requested);
    }, BackendExecutor::CancelPolicy::DiscardRunningResult);
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    if (cancel) QVERIFY(executor->cancel(id));
    else receiver.reset();
    if (destroy_executor) executor.reset();
    QCOMPARE(completions, 0);
    QVERIFY(!destroyed_on_worker->load());
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(destroyed_on_worker->load(), TIMEOUT);
    if (executor) executor->shutdown();
    bool drained{false};
    WaitForThreadJoins(this, [&] {
        drained = true;
        QCOMPARE(completions, cancel ? 1 : 0);
        QVERIFY(destroyed_on_worker->load());
    });
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QCOMPARE(completions, cancel ? 1 : 0);
}

void BackendExecutorTests::drainCallbacksAreQueuedAndLifetimeGuarded_data()
{
    QTest::addColumn<bool>("bounded");
    QTest::newRow("compatibility") << false;
    QTest::newRow("bounded-group") << true;
}

void BackendExecutorTests::drainCallbacksAreQueuedAndLifetimeGuarded()
{
    QFETCH(bool, bounded);
    auto owner = bounded ? std::make_unique<BackendExecutor>(execution::Options{}) : std::make_unique<BackendExecutor>();
    auto& executor = *owner;
    QObject receiver;
    int completions{0};
    bool destroyed_receiver_called{false};
    auto destroyed_receiver = std::make_unique<QObject>();
    executor.whenDrained(destroyed_receiver.get(), [&] { destroyed_receiver_called = true; });
    destroyed_receiver.reset();
    const auto gui_thread = QThread::currentThread();
    executor.whenDrained(&receiver, [&] {
        ++completions;
        QVERIFY(executor.isDrained());
        QCOMPARE(QThread::currentThread(), gui_thread);
    });
    connect(&executor, &BackendExecutor::drained, &receiver, [&] { QCOMPARE(completions, 0); });
    executor.shutdown();
    QCOMPARE(completions, 0);
    QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    executor.whenDrained(&receiver, [&] { ++completions; });
    QCOMPARE(completions, 1);
    destroyed_receiver = std::make_unique<QObject>();
    executor.whenDrained(destroyed_receiver.get(), [&] { destroyed_receiver_called = true; });
    destroyed_receiver.reset();
    QTRY_COMPARE_WITH_TIMEOUT(completions, 2, TIMEOUT);
    QVERIFY(!destroyed_receiver_called);
}


void BackendExecutorTests::groupedDrainWaitsForIndependentCoreAndOwnedTask()
{
    auto executor = std::make_unique<BackendExecutor>(execution::Options{});
    const auto scheduler = executor->scheduler();
    const auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    std::atomic_int consumed{0};
    QVERIFY(execution::Submit(*scheduler, [gate] {
        gate->entered.release();
        gate->release.acquire();
        return 17;
    }, [&](int value) { consumed = value; }));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    int completions{0};
    executor->submitTask(this, [] { return 42; }, [&](BackendExecutor::Outcome<int> outcome) {
        QVERIFY(std::holds_alternative<int>(outcome));
        QCOMPARE(std::get<int>(outcome), 42);
        ++completions;
    });
    bool drained{false};
    executor->whenDrained(this, [&] {
        QCOMPARE(consumed.load(), 17);
        QCOMPARE(completions, 1);
        QCOMPARE(scheduler->pending(), size_t{0});
        drained = true;
    });
    executor.reset();
    QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    QCOMPARE(consumed.load(), 0);
    QVERIFY(!drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BackendExecutorTests)
#else
QTEST_GUILESS_MAIN(BackendExecutorTests)
#endif
#include <test_backendexecutor.moc>
