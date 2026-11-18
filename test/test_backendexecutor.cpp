// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/executor.h>
#include <qml/ownedwork.h>
#include <qml/snapshotquery.h>
#include <qml/retainedvalue.h>
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
#include <string_view>
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
        using namespace execution;
        const auto executor = std::make_shared<Executor>(Executor::Config{});
        const auto values = std::make_shared<ValueCleanupQueue>(
            std::make_shared<SerialQueue>(executor->makeQueue(Lane::Utility)));
        const auto gui = QThread::currentThread();
        const auto retired_off_gui = std::make_shared<std::atomic_bool>(false);
        auto held = values->hold([retired_off_gui, gui] { *retired_off_gui = QThread::currentThread() != gui; });
        QVERIFY(held);
        if (routed) {
            auto core = executor->makeQueue(Lane::Core);
            auto ran = std::make_shared<QSemaphore>();
            QVERIFY(core.post([ran] { ran->release(); }));
            QVERIFY(ran->tryAcquire(1, TIMEOUT));
        }
        QObject receiver;
        bool joined{false}, heartbeat{false};
        auto join = JoinThreadAfterDrain([executor] { executor->stopAndJoin(); }, &receiver, [&] { joined = true; });
        values->close(std::move(join));
        QTimer::singleShot(0, &receiver, [&] { heartbeat = true; });
        QTRY_VERIFY_WITH_TIMEOUT(heartbeat, TIMEOUT);
        QVERIFY(!joined);
        QVERIFY(!retired_off_gui->load());
        held.reset();
        QTRY_VERIFY_WITH_TIMEOUT(joined, TIMEOUT);
        QVERIFY(retired_off_gui->load());
    }

    void compatibilityWorkCannotBlockMigratedCoreReads()
    {
        using namespace execution;
        const auto executor = std::make_shared<Executor>(Executor::Config{});
        auto rpc = executor->makeQueue(Lane::Rpc);
        auto core = executor->makeQueue(Lane::Core);
        const auto outcome = std::make_shared<SerialQueue>(executor->makeQueue(Lane::Utility));
        const auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        struct Probe {
            std::atomic<QThread*> rpc{nullptr}, core{nullptr}, utility{nullptr};
            QSemaphore ready;
            std::atomic_int value{0};
        };
        const auto probe = std::make_shared<Probe>();
        const auto gui = QThread::currentThread();
        QVERIFY(rpc.post([gate, probe] {
            probe->rpc = QThread::currentThread();
            gate->entered.release();
            gate->release.acquire();
        }));
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        QVERIFY(core.post([outcome, probe] {
            probe->core = QThread::currentThread();
            outcome->post([probe] {
                probe->utility = QThread::currentThread();
                probe->value = 42;
                probe->ready.release();
            });
        }));
        QVERIFY(probe->ready.tryAcquire(1, TIMEOUT));
        QCOMPARE(probe->value.load(), 42);
        QVERIFY(probe->rpc.load() != gui);
        QVERIFY(probe->core.load() != gui);
        QVERIFY(probe->utility.load() != gui);
        QVERIFY(probe->rpc.load() != probe->core.load());
        QVERIFY(probe->rpc.load() != probe->utility.load());
        gate->release.release();
        executor->stopAndJoin();
    }
    void activeGuiCallbackRetainsOwningPacketThroughReentrantShutdown()
    {
        using namespace execution;
        Executor executor{Executor::Config{}};
        auto utility = executor.makeQueue(Lane::Utility);
        const auto values = std::make_shared<ValueCleanupQueue>(
            std::make_shared<SerialQueue>(executor.makeQueue(Lane::Utility)));
        const auto gui = QThread::currentThread();
        struct Value {
            QThread* gui;
            std::atomic_bool& destroyed_off_gui;
            ~Value() { destroyed_off_gui = QThread::currentThread() != gui; }
        };
        std::atomic_bool destroyed_off_gui{false};
        auto owner = std::make_unique<OwnedWork>(executor);
        const auto stop = qScopeGuard([&] {
            owner.reset();
            executor.stopAndJoin();
        });
        bool callback_done{false};
        const auto drained = std::make_shared<std::atomic_bool>(false);
        QVERIFY(owner->submit([values, gui, &destroyed_off_gui] {
            return RetainValues<Value>([values](auto cleanup) { return values->hold(std::move(cleanup)); },
                [&] { return std::unique_ptr<Value>{new Value{gui, destroyed_off_gui}}; });
        }, [&](const std::shared_ptr<Value>& result) {
            QVERIFY(result);
            owner.reset();
            values->close([drained] { *drained = true; });
            auto marker = std::make_shared<QSemaphore>();
            QVERIFY(utility.post([marker] { marker->release(); }));
            QVERIFY(marker->tryAcquire(1, TIMEOUT));
            QVERIFY(!destroyed_off_gui);
            QVERIFY(!drained->load());
            callback_done = true;
        }, [](std::exception_ptr) { QFAIL("An accepted operation failed"); }));
        QTRY_VERIFY(callback_done);
        QTRY_VERIFY(drained->load());
        QVERIFY(destroyed_off_gui);
    }
    void routedControlAndCleanupProgressWhileCoreIsHeld()
    {
        using namespace execution;
        const auto executor = std::make_shared<Executor>(Executor::Config{});
        auto core = executor->makeQueue(Lane::Core);
        auto priority = executor->makeQueue(Lane::Priority);
        const auto utility = std::make_shared<SerialQueue>(executor->makeQueue(Lane::Utility));
        const auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        struct Probe {
            std::atomic<QThread*> core{nullptr}, priority{nullptr}, utility{nullptr};
            std::atomic_int written{0}, consumed{0};
            QSemaphore interrupted, retired;
        };
        const auto probe = std::make_shared<Probe>();
        QVERIFY(core.post([gate, probe, utility] {
            probe->core = QThread::currentThread();
            gate->entered.release();
            gate->release.acquire();
            ++probe->written;
            utility->post([probe] { probe->consumed = 42; });
        }));
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        core.close();
        QVERIFY(!core.post([] {}));
        QVERIFY(priority.post([probe] {
            probe->priority = QThread::currentThread();
            probe->interrupted.release();
        }));
        QVERIFY(utility->post([probe] {
            probe->utility = QThread::currentThread();
            probe->retired.release();
        }));
        QVERIFY(probe->interrupted.tryAcquire(1, TIMEOUT));
        QVERIFY(probe->retired.tryAcquire(1, TIMEOUT));
        QCOMPARE(probe->written.load(), 0);
        const auto gui = QThread::currentThread();
        for (auto* worker : {probe->core.load(), probe->priority.load(), probe->utility.load()}) {
            QVERIFY(worker && worker != gui);
        }
        QCOMPARE(std::set<QThread*>({probe->core.load(), probe->priority.load(), probe->utility.load()}).size(), size_t{3});
        bool drained{false}, heartbeat{false};
        QObject receiver;
        auto join = JoinThreadAfterDrain([executor] { executor->stopAndJoin(); }, &receiver, [&] { drained = true; });
        core.whenDrained([utility, join = std::move(join)] {
            utility->close();
            utility->whenDrained(join);
        });
        QTimer::singleShot(0, &receiver, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QVERIFY(!drained);
        QCOMPARE(probe->consumed.load(), 0);
        gate->release.release();
        QTRY_VERIFY(drained);
        QCOMPARE(probe->written.load(), 1);
        QCOMPARE(probe->consumed.load(), 42);
    }
    void nativeCleanupAffinityRetiresOnCoreWhileControlRemainsIndependent()
    {
        using namespace execution;
        const auto executor = std::make_shared<Executor>(Executor::Config{});
        const auto core = std::make_shared<SerialQueue>(executor->makeQueue(Lane::Core));
        auto priority = executor->makeQueue(Lane::Priority);
        const auto values = std::make_shared<ValueCleanupQueue>(core);
        const auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        struct Probe {
            std::atomic<QThread*> core{nullptr}, cleanup{nullptr}, priority{nullptr};
            std::atomic_int retired{0};
            QSemaphore read, interrupted;
        };
        const auto probe = std::make_shared<Probe>();
        QVERIFY(core->post([probe] {
            probe->core = QThread::currentThread();
            probe->read.release();
        }));
        QVERIFY(probe->read.tryAcquire(1, TIMEOUT));
        auto held = values->hold([probe, gate] {
            probe->cleanup = QThread::currentThread();
            gate->entered.release();
            gate->release.acquire();
            ++probe->retired;
        });
        QVERIFY(held);
        held.reset();
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        QVERIFY(probe->cleanup.load() == probe->core.load());
        QVERIFY(probe->core.load() != qApp->thread());
        QVERIFY(priority.post([probe] {
            probe->priority = QThread::currentThread();
            probe->interrupted.release();
        }));
        QVERIFY(probe->interrupted.tryAcquire(1, TIMEOUT));
        QVERIFY(probe->priority.load() != probe->core.load());
        QVERIFY(probe->priority.load() != qApp->thread());
        QObject receiver;
        bool drained{false}, heartbeat{false};
        values->close(JoinThreadAfterDrain([executor] { executor->stopAndJoin(); }, &receiver, [&] { drained = true; }));
        QTimer::singleShot(0, &receiver, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QVERIFY(!drained);
        QCOMPARE(probe->retired.load(), 0);
        gate->release.release();
        QTRY_VERIFY(drained);
        QCOMPARE(probe->retired.load(), 1);
    }
    void routedOwningOutcomeSurvivesDestroyedGuiOwner()
    {
        using namespace execution;
        auto executor = std::make_shared<Executor>(Executor::Config{});
        auto core = executor->makeQueue(Lane::Core);
        const auto utility = std::make_shared<SerialQueue>(executor->makeQueue(Lane::Utility));
        auto observer = std::make_unique<QObject>();
        const QPointer<QObject> view{observer.get()};
        const auto post = presentation::QueuedPost();
        const auto publications = std::make_shared<int>(0);
        auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        const auto gui = QThread::currentThread();
        struct Probe {
            QSemaphore wrote;
            std::atomic_int effects{0}, outcomes{0};
            std::atomic_bool destroyed_off_gui{false};
        };
        const auto probe = std::make_shared<Probe>();
        QVERIFY(utility->post([gate] {
            gate->entered.release();
            gate->release.acquire();
        }));
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        QVERIFY(core.post([utility, probe, gui, view, post, publications] {
            auto result = std::shared_ptr<int>{new int{42}, [probe, gui](int* value) {
                probe->destroyed_off_gui = QThread::currentThread() != gui;
                delete value;
            }};
            ++probe->effects;
            utility->post([probe, gui, view, post, publications, result = std::move(result)] {
                if (*result == 42 && probe->effects == 1 && QThread::currentThread() != gui) ++probe->outcomes;
                post([view, publications] { if (view) ++*publications; });
            });
            probe->wrote.release();
        }));
        QVERIFY(probe->wrote.tryAcquire(1, TIMEOUT));
        observer.reset();
        QCOMPARE(probe->effects.load(), 1);
        QCOMPARE(probe->outcomes.load(), 0);
        QVERIFY(!probe->destroyed_off_gui);
        core.close();
        utility->close();
        QObject receiver;
        bool drained{false}, heartbeat{false};
        JoinThreadAsync([executor = std::move(executor)] { executor->stopAndJoin(); }, &receiver, [&] { drained = true; });
        QTimer::singleShot(0, &receiver, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QVERIFY(!drained);
        gate->release.release();
        QTRY_VERIFY(drained);
        QCOMPARE(probe->outcomes.load(), 1);
        QVERIFY(probe->destroyed_off_gui);
        QCOMPARE(*publications, 0);
    }
    void domainCompletionDoesNotNeedGuiTurn() {
        using namespace execution;
        Executor executor{Executor::Config{}};
        OwnedWork owner{executor};
        auto post = owner.queuePost();
        struct Probe { QSemaphore completed; std::atomic_bool worker_ok{false}, completion_ok{false}; };
        const auto probe = std::make_shared<Probe>();
        bool admitted{false};
        std::thread submitter{[&] {
            admitted = post(Lane::Domain, MakeTask([probe] {
                probe->worker_ok = QThread::currentThread() != qApp->thread();
                return 17;
            }, [probe](int value) {
                probe->completion_ok = value == 17 && QThread::currentThread() != qApp->thread();
                probe->completed.release();
            }, {}));
        }};
        submitter.join();
        QVERIFY(admitted);
        QVERIFY(probe->completed.tryAcquire(1, TIMEOUT));
        QVERIFY(probe->worker_ok);
        QVERIFY(probe->completion_ok);
        owner.close();
        executor.stopAndJoin();
    }
    void acceptedDomainOutcomeSurvivesOwnerDestruction() {
        using namespace execution;
        auto executor = std::make_shared<Executor>(Executor::Config{});
        auto owner = std::make_unique<OwnedWork>(*executor);
        auto post = owner->queuePost();
        auto gate = std::make_shared<Gate>();
        const auto release = qScopeGuard([gate] { gate->release.release(); });
        const auto consumed = std::make_shared<std::atomic_int>(0);
        QVERIFY(post(Lane::Domain, MakeTask([gate] {
            gate->entered.release();
            gate->release.acquire();
            return 3;
        }, [consumed](int value) { *consumed = value; }, {})));
        QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
        QObject receiver;
        bool joined{false};
        owner->whenQueuesDrained(JoinThreadAfterDrain([worker = std::move(executor)] { worker->stopAndJoin(); }, &receiver, [&] { joined = true; }));
        owner->close();
        QVERIFY(!owner->submit([] { return 7; }, [](int) { QFAIL("Work was accepted after close"); }, [](std::exception_ptr) {}));
        owner.reset();
        QCOMPARE(consumed->load(), 0);
        gate->release.release();
        QTRY_VERIFY(joined);
        QCOMPARE(consumed->load(), 3);
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
    execution::OwnedWork owner{executor};
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
    execution::OwnedWork owner{executor};
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
    execution::OwnedWork successor{executor};
    auto receiver = std::make_unique<execution::OwnedWork>(executor);
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
    execution::OwnedWork owner{executor};
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
    auto owner = std::make_unique<execution::OwnedWork>(executor);
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
        auto owner = std::make_unique<execution::OwnedWork>(executor);
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
        auto owner = std::make_unique<execution::OwnedWork>(*executor);
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
        auto owner = std::make_unique<execution::OwnedWork>(*executor);
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
    using namespace execution;
    Executor executor{Executor::Config{}};
    OwnedWork owner{executor};
    const auto send = owner.queuePost();
    const auto gui_thread = QThread::currentThread();
    const auto worker_thread = std::make_shared<std::atomic<QThread*>>(nullptr);
    const auto post = presentation::QueuedPost();
    auto completed = std::make_shared<std::vector<int>>();
    const auto guard = std::make_unique<QObject>();
    const QPointer<QObject> view{guard.get()};
    const auto expect = [post, completed, view, gui_thread](int value) {
        post([view, completed, value, gui_thread] {
            if (!view) return;
            QCOMPARE(QThread::currentThread(), gui_thread);
            completed->push_back(value);
        });
    };
    QVERIFY(send(Lane::Core, MakeTask([worker_thread] {
        *worker_thread = QThread::currentThread();
        return 42;
    }, [expect](int value) { expect(value == 42 ? 1 : -1); }, {})));
    QVERIFY(send(Lane::Core, MakeTask([] {}, [expect] { expect(2); }, {})));
    QVERIFY(send(Lane::Core, MakeTask([value = std::make_unique<int>(7)]() mutable {
        return std::move(value);
    }, [expect](std::unique_ptr<int> value) { expect(value && *value == 7 ? 3 : -3); }, {})));
    QVERIFY(send(Lane::Core, MakeTask([]() -> int { throw std::runtime_error{"backend error"}; },
        [expect](int) { expect(-4); }, [expect](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const std::runtime_error& failure) { expect(std::string_view{failure.what()} == "backend error" ? 4 : -4); }
        })));
    QVERIFY(completed->empty());
    QTRY_COMPARE_WITH_TIMEOUT(completed->size(), size_t{4}, TIMEOUT);
    QCOMPARE(*completed, (std::vector<int>{1, 2, 3, 4}));
    QVERIFY(worker_thread->load() && worker_thread->load() != gui_thread);
    owner.close();
    executor.stopAndJoin();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(completed->size(), size_t{4});
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
    using namespace execution;
    auto executor = std::make_shared<Executor>(Executor::Config{});
    const auto core = std::make_shared<SerialQueue>(executor->makeQueue(Lane::Core));
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    QVERIFY(core->post([gate] { gate->entered.release(); gate->release.acquire(); }));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    const auto ran = std::make_shared<std::atomic_bool>(false);
    const auto released_on_worker = std::make_shared<std::atomic_bool>(false);
    const auto notifications = std::make_shared<std::atomic_int>(0);
    const auto gui_thread = QThread::currentThread();
    auto backend = std::shared_ptr<int>{new int{1}, [released_on_worker, gui_thread](int* value) {
        *released_on_worker = QThread::currentThread() != gui_thread;
        delete value;
    }};
    const std::weak_ptr<int> backend_guard{backend};
    auto query = std::make_shared<SnapshotQuery<int>>([ran, backend = std::move(backend)] {
        *ran = true;
        return *backend;
    }, [notifications] { ++*notifications; }, [core](auto task) {
        const bool accepted = core->post([task] { task->execute(); task->deliver(); });
        return SubmitResult{{}, accepted ? Rejection::None : Rejection::Transport};
    });
    query->request();
    query->close();
    query->close();
    query.reset();
    if (shutdown || destroy_executor) core->close();
    QObject receiver;
    bool joined{false};
    auto done = JoinThreadAfterDrain([worker = executor] { worker->stopAndJoin(); }, &receiver, [&] { joined = true; });
    if (destroy_executor) executor.reset();
    QVERIFY(!ran->load());
    QCOMPARE(notifications->load(), 0);
    QVERIFY(!backend_guard.expired());
    QVERIFY(!joined);
    core->close();
    core->whenDrained(std::move(done));
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(joined, TIMEOUT);
    QVERIFY(!ran->load());
    QCOMPARE(notifications->load(), 0);
    QVERIFY(backend_guard.expired());
    QVERIFY(released_on_worker->load());
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
    using namespace execution;
    Executor executor{Executor::Config{}};
    OwnedWork owner{executor};
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    auto effects = std::make_shared<std::atomic_int>(0);
    int completions{0};
    QVERIFY(owner.submit([gate, effects, fail] {
        gate->entered.release();
        gate->release.acquire();
        ++*effects;
        if (fail) throw std::runtime_error{"backend error"};
        return 42;
    }, [&](int value) {
        ++completions;
        QVERIFY(!fail);
        QCOMPARE(effects->load(), 1);
        QCOMPARE(value, 42);
    }, [&](std::exception_ptr error) {
        ++completions;
        QVERIFY(fail);
        QCOMPARE(effects->load(), 1);
        try { std::rethrow_exception(error); }
        catch (const std::runtime_error& failure) { QCOMPARE(QString::fromUtf8(failure.what()), QStringLiteral("backend error")); }
    }));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    if (discard_result) { owner.close(); owner.close(); }
    bool gui_progress{false};
    QObject receiver;
    QTimer::singleShot(0, &receiver, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, TIMEOUT);
    QCOMPARE(completions, 0);
    QCOMPARE(effects->load(), 0);
    gate->release.release();
    if (!discard_result) QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    const auto drained = std::make_shared<std::atomic_bool>(false);
    owner.close([drained] { *drained = true; });
    QTRY_VERIFY_WITH_TIMEOUT(drained->load(), TIMEOUT);
    QCOMPARE(effects->load(), 1);
    QCOMPARE(completions, discard_result ? 0 : 1);
}

void BackendExecutorTests::cancellationAfterWorkFinishesKeepsOutcome()
{
    using namespace execution;
    Executor executor{Executor::Config{}};
    auto core = executor.makeQueue(Lane::Core);
    auto observer = std::make_unique<QObject>();
    const QPointer<QObject> view{observer.get()};
    const auto post = presentation::QueuedPost();
    const auto completions = std::make_shared<int>(0);
    const auto command = std::make_shared<OwnedCommand<int>>([view, post, completions] {
        post([view, completions] { if (view) ++*completions; });
    });
    QVERIFY(command->startWith([&core](auto done, auto failed) {
        auto task = MakeTask([] { return 42; }, std::move(done), std::move(failed));
        const bool accepted = core.post([task] { task->execute(); task->deliver(); });
        return SubmitResult{{}, accepted ? Rejection::None : Rejection::Transport};
    }));
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    QVERIFY(core.post([gate] { gate->entered.release(); gate->release.acquire(); }));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    QCOMPARE(*completions, 0);
    observer.reset();
    QVERIFY(command->result());
    QCOMPARE(command->result()->value, std::optional<int>{42});
    gate->release.release();
    executor.stopAndJoin();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QCOMPARE(*completions, 0);
    QCOMPARE(command->result()->value, std::optional<int>{42});
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
    using namespace execution;
    Executor executor{[](Lane, UtilityWorker, auto) { return false; }};
    OwnedWork owner{executor};
    if (already_drained) executor.stopAndJoin();
    const auto ran = std::make_shared<std::atomic_bool>(false);
    int completions{0};
    const auto gui_thread = QThread::currentThread();
    QVERIFY(!owner.submit([ran] { *ran = true; return 42; }, [](int) { QFAIL("Rejected work ran"); }, [&](std::exception_ptr error) {
        ++completions;
        QCOMPARE(QThread::currentThread(), gui_thread);
        QVERIFY(NotStartedCause(error) == NotStarted::Cause::Transport);
    }));
    QCOMPARE(completions, 0);
    QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    QVERIFY(!ran->load());
}

void BackendExecutorTests::taskShutdownPreservesOutcomesAndCleanup()
{
    using namespace execution;
    auto executor = std::make_shared<Executor>(Executor::Config{});
    auto core = executor->makeQueue(Lane::Core);
    const auto utility = std::make_shared<SerialQueue>(executor->makeQueue(Lane::Utility));
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    const auto effects = std::make_shared<std::atomic_int>(0);
    const auto completed = std::make_shared<std::vector<int>>();
    QObject receiver;
    const QPointer<QObject> view{&receiver};
    const auto post = presentation::QueuedPost();
    const auto enqueue = [&](std::function<int()> work) {
        return core.post([utility, work = std::move(work), post, view, completed] {
            const int result = work();
            utility->post([post, view, completed, result] {
                post([view, completed, result] { if (view) completed->push_back(result); });
            });
        });
    };
    QVERIFY(enqueue([gate, effects] {
        gate->entered.release();
        gate->release.acquire();
        ++*effects;
        return 1;
    }));
    QVERIFY(enqueue([effects] { ++*effects; return 2; }));
    OwnedWork owner{*executor};
    bool view_called{false};
    QVERIFY(owner.submit([effects] { ++*effects; return 3; }, [&](int) { view_called = true; },
        [](std::exception_ptr) { QFAIL("An accepted operation failed"); }));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    owner.close();
    core.close();
    bool drained{false};
    auto join = JoinThreadAfterDrain([worker = std::move(executor)] { worker->stopAndJoin(); }, &receiver, [&] { drained = true; });
    core.whenDrained([utility, join = std::move(join)] {
        utility->close();
        utility->whenDrained(join);
    });
    QVERIFY(!drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QCOMPARE(*completed, (std::vector<int>{1, 2}));
    QCOMPARE(effects->load(), 3);
    QVERIFY(!view_called);
}

void BackendExecutorTests::taskCompletionSurvivesExecutorDestruction()
{
    using namespace execution;
    auto executor = std::make_shared<Executor>(Executor::Config{});
    auto owner = std::make_unique<OwnedWork>(*executor);
    const auto enqueue = owner->queuePost();
    const auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    const auto post = presentation::QueuedPost();
    QObject receiver;
    const QPointer<QObject> view{&receiver};
    auto completions = std::make_shared<int>(0);
    QVERIFY(enqueue(Lane::Core, MakeTask([gate] {
        gate->entered.release();
        gate->release.acquire();
        return 42;
    }, [view, post, completions](int value) {
        post([view, completions, value] {
            if (!view) return;
            QCOMPARE(value, 42);
            ++*completions;
        });
    }, {})));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    bool executor_drained{false};
    owner->whenQueuesDrained(JoinThreadAfterDrain([worker = std::move(executor)] { worker->stopAndJoin(); }, &receiver, [&] {
        executor_drained = true;
        QCOMPARE(*completions, 1);
    }));
    owner.reset();
    bool drained{false};
    WaitForThreadJoins(&receiver, [&] { drained = true; QCOMPARE(*completions, 1); });
    QVERIFY(!drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QVERIFY(executor_drained);
    QCOMPARE(*completions, 1);
}

void BackendExecutorTests::taskCompletionCanDestroyOwners()
{
    using namespace execution;
    for (const bool fail : {false, true}) {
        auto executor = std::make_shared<Executor>(Executor::Config{});
        auto owner = std::make_unique<OwnedWork>(*executor);
        auto receiver = std::make_unique<QObject>();
        int completions{0};
        const auto finish = [&] {
            ++completions;
            receiver.reset();
            owner->whenQueuesDrained(JoinThreadAfterDrain([worker = std::move(executor)] { worker->stopAndJoin(); }, this, [] {}));
            owner.reset();
        };
        QVERIFY(owner->submit([fail] {
            if (fail) throw std::runtime_error{"backend error"};
            return true;
        }, [&](bool value) {
            QVERIFY(value);
            QVERIFY(!fail);
            finish();
        }, [&](std::exception_ptr error) {
            QVERIFY(fail);
            QVERIFY(error);
            finish();
        }));
        QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
        QVERIFY(!owner);
        QVERIFY(!executor);
        QVERIFY(!receiver);
        QObject waiter;
        bool drained{false};
        WaitForThreadJoins(&waiter, [&] { drained = true; });
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
    using namespace execution;
    auto executor = std::make_shared<Executor>(Executor::Config{});
    auto retire = std::make_shared<SerialQueue>(executor->makeQueue(Lane::Utility));
    auto probe = bounded ? executor->makeQueue(Lane::Core) : executor->makeQueue(Lane::Utility);
    const auto values = std::make_shared<ValueCleanupQueue>(retire);
    auto owner = std::make_unique<OwnedWork>(*executor);
    const auto gui = QThread::currentThread();
    struct State {
        std::atomic_bool result_off_gui{false}, completion_on_gui{false}, completion_released{false};
    };
    const auto observed = std::make_shared<State>();
    struct Result {
        std::shared_ptr<State> observed;
        QThread* gui;
        ~Result() { observed->result_off_gui = QThread::currentThread() != gui; }
    };
    const auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    auto cleanup_capture = std::shared_ptr<int>{new int, [observed, gui](int* value) {
        observed->completion_on_gui = QThread::currentThread() == gui;
        observed->completion_released = true;
        delete value;
    }};
    bool callback_active{false}, drained{false};
    QObject receiver;
    int completions{0};
    QVERIFY(owner->submit([gate, values, observed, gui] {
        gate->entered.release();
        gate->release.acquire();
        return RetainValues<Result>([values](auto cleanup) { return values->hold(std::move(cleanup)); },
            [observed, gui] { return std::unique_ptr<Result>{new Result{observed, gui}}; });
    }, [&, kept = std::move(cleanup_capture)](const std::shared_ptr<Result>& result) {
        QVERIFY(result);
        QVERIFY(kept);
        ++completions;
        callback_active = true;
        owner.reset();
        values->close(JoinThreadAfterDrain([worker = executor] { worker->stopAndJoin(); }, &receiver, [&] {
            QVERIFY(!callback_active);
            QVERIFY(observed->completion_released);
            QVERIFY(observed->completion_on_gui);
            QVERIFY(observed->result_off_gui);
            drained = true;
        }));
        const auto ran = std::make_shared<std::atomic_bool>(false);
        QVERIFY(probe.post([ran] { *ran = true; }));
        QTRY_VERIFY_WITH_TIMEOUT(ran->load(), TIMEOUT);
        QVERIFY(!observed->completion_released);
        QVERIFY(!observed->result_off_gui);
        QVERIFY(!drained);
        callback_active = false;
    }, [](std::exception_ptr) { QFAIL("An accepted operation failed"); }));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
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
    using namespace execution;
    auto executor = std::make_shared<Executor>(Executor::Config{});
    auto owner = std::make_unique<OwnedWork>(*executor);
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    const auto destroyed_on_worker = std::make_shared<std::atomic_bool>(false);
    const auto gui_thread = QThread::currentThread();
    int completions{0};
    QVERIFY(owner->submit([gate, destroyed_on_worker, gui_thread] {
        gate->entered.release();
        gate->release.acquire();
        return std::shared_ptr<int>{new int{42}, [destroyed_on_worker, gui_thread](int* value) {
            *destroyed_on_worker = QThread::currentThread() != gui_thread;
            delete value;
        }};
    }, [&](const std::shared_ptr<int>&) { ++completions; },
       [](std::exception_ptr) { QFAIL("An accepted operation failed"); }));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    bool drained{false};
    QObject receiver;
    owner->whenQueuesDrained(JoinThreadAfterDrain([worker = executor] { worker->stopAndJoin(); }, &receiver, [&] { drained = true; }));
    if (cancel) owner->close();
    else owner.reset();
    if (destroy_executor) executor.reset();
    QCOMPARE(completions, 0);
    QVERIFY(!drained);
    QVERIFY(!destroyed_on_worker->load());
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QVERIFY(destroyed_on_worker->load());
    QCOMPARE(completions, 0);
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
    using namespace execution;
    auto executor = std::make_shared<Executor>(Executor::Config{});
    auto queue = executor->makeQueue(bounded ? Lane::Core : Lane::Utility);
    const auto completed = std::make_shared<std::atomic_bool>(false);
    QVERIFY(queue.post([completed] { *completed = true; }));
    QObject receiver;
    int completions{0};
    bool destroyed_receiver_called{false};
    auto destroyed_receiver = std::make_unique<QObject>();
    JoinThreadAsync([worker = std::move(executor)] { worker->stopAndJoin(); }, &receiver, [&] {
        QVERIFY(completed->load());
        QCOMPARE(completions, 0);
    });
    WaitForThreadJoins(destroyed_receiver.get(), [&] { destroyed_receiver_called = true; });
    destroyed_receiver.reset();
    const auto gui_thread = QThread::currentThread();
    WaitForThreadJoins(&receiver, [&] {
        ++completions;
        QVERIFY(completed->load());
        QCOMPARE(QThread::currentThread(), gui_thread);
    });
    QCOMPARE(completions, 0);
    QTRY_COMPARE_WITH_TIMEOUT(completions, 1, TIMEOUT);
    WaitForThreadJoins(&receiver, [&] { ++completions; });
    QCOMPARE(completions, 1);
    destroyed_receiver = std::make_unique<QObject>();
    WaitForThreadJoins(destroyed_receiver.get(), [&] { destroyed_receiver_called = true; });
    destroyed_receiver.reset();
    QTRY_COMPARE_WITH_TIMEOUT(completions, 2, TIMEOUT);
    QVERIFY(!destroyed_receiver_called);
}


void BackendExecutorTests::groupedDrainWaitsForIndependentCoreAndOwnedTask()
{
    using namespace execution;
    auto executor = std::make_shared<Executor>(Executor::Config{});
    auto core = executor->makeQueue(Lane::Core);
    auto utility = executor->makeQueue(Lane::Utility);
    const auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    const auto consumed = std::make_shared<std::atomic_int>(0);
    QVERIFY(core.post([gate, consumed] {
        gate->entered.release();
        gate->release.acquire();
        *consumed = 17;
    }));
    QVERIFY(gate->entered.tryAcquire(1, TIMEOUT));
    const auto post = presentation::QueuedPost();
    QObject receiver;
    const auto completions = std::make_shared<int>(0);
    QVERIFY(utility.post([post, view = QPointer<QObject>{&receiver}, completions] {
        post([view, completions] { if (view) ++*completions; });
    }));
    bool drained{false};
    JoinThreadAsync([worker = std::move(executor)] { worker->stopAndJoin(); }, &receiver, [&] {
        QCOMPARE(consumed->load(), 17);
        QCOMPARE(*completions, 1);
        drained = true;
    });
    QTRY_COMPARE_WITH_TIMEOUT(*completions, 1, TIMEOUT);
    QCOMPARE(consumed->load(), 0);
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
