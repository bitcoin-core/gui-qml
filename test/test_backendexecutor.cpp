// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
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
    void taskDrainWaitsForCompletionCleanup();
    void discardedTaskResultIsDestroyedOnWorker_data();
    void discardedTaskResultIsDestroyedOnWorker();
    void drainCallbacksAreQueuedAndLifetimeGuarded();
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
    bool all_drained{false};
    BackendExecutor::shutdownAll(this, [&] { all_drained = true; });
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
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
    BackendExecutor executor;
    QSignalSpy drained{&executor, &BackendExecutor::drained};
    bool job_finished{false};
    QVERIFY(executor.submit(this, [gate] {
        auto* deferred = new DeferredCleanup{gate};
        QObject::connect(QThread::currentThread(), &QThread::finished, deferred, &QObject::deleteLater);
    }, [&] { job_finished = true; }));
    QTRY_VERIFY_WITH_TIMEOUT(job_finished, TIMEOUT);
    executor.shutdown();
    bool all_drained{false};
    BackendExecutor::shutdownAll(this, [&] { all_drained = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    int heartbeats{0};
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
    timer.start(0);
    QTRY_VERIFY_WITH_TIMEOUT(heartbeats > 2, TIMEOUT);
    QCOMPARE(drained.count(), 0);
    QVERIFY(!executor.isDrained());
    QVERIFY(!all_drained);
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, TIMEOUT);
    QTRY_VERIFY_WITH_TIMEOUT(all_drained, TIMEOUT);
}

void BackendExecutorTests::finalDrainIncludesDestroyedValueOwners()
{
    auto gate = std::make_shared<Gate>();
    auto executor = std::make_unique<BackendExecutor>();
    auto release = qScopeGuard([gate] { gate->release.release(); });
    std::atomic_bool finished{false};
    QVERIFY(executor->submit(this, [gate, &finished] {
        gate->entered.release();
        gate->release.acquire();
        finished = true;
    }, [] {}));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    executor.reset();
    bool drained{false};
    BackendExecutor::shutdownAll(this, [&] { drained = true; });
    int heartbeats{0};
    QTimer timer;
    connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
    timer.start(0);
    QTRY_VERIFY_WITH_TIMEOUT(heartbeats > 2, TIMEOUT);
    QVERIFY(!drained);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QVERIFY(finished.load());
}

void BackendExecutorTests::serialWorkPublishesOwnedResultsOnGui()
{
    BackendExecutor executor;
    const auto gui_thread = QThread::currentThread();
    auto sequence = std::make_shared<std::vector<int>>();
    std::vector<int> results;
    std::atomic_bool off_gui{false};
    QVERIFY(executor.submit(this, [sequence, gui_thread, &off_gui] {
        off_gui = QThread::currentThread() != gui_thread;
        sequence->push_back(1);
        return std::make_unique<int>(sequence->back());
    }, [&](std::unique_ptr<int> result) {
        QCOMPARE(QThread::currentThread(), gui_thread);
        results.push_back(*result);
    }));
    QVERIFY(executor.submit(this, [sequence] {
        sequence->push_back(sequence->back() + 1);
        return sequence->back();
    }, [&](int result) { results.push_back(result); }));
    QTRY_COMPARE_WITH_TIMEOUT(results.size(), size_t{2}, TIMEOUT);
    QCOMPARE(results, (std::vector<int>{1, 2}));
    QVERIFY(off_gui.load());
    executor.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
}

void BackendExecutorTests::failureDoesNotStopLaterCommands()
{
    BackendExecutor executor;
    bool failed{false};
    bool succeeded{false};
    bool wrong_completion{false};
    const auto gui_thread = QThread::currentThread();
    QVERIFY(executor.submit(this, []() -> int { throw std::runtime_error{"backend error"}; },
        [&](int) { wrong_completion = true; }, [&](std::exception_ptr error) {
            QCOMPARE(QThread::currentThread(), gui_thread);
            try {
                std::rethrow_exception(error);
            } catch (const std::runtime_error& e) {
                QCOMPARE(QString::fromUtf8(e.what()), QString{"backend error"});
                failed = true;
            }
        }));
    QVERIFY(executor.submit(this, [] {}, [&] { succeeded = true; }));
    QTRY_VERIFY_WITH_TIMEOUT(failed && succeeded, TIMEOUT);
    QVERIFY(!wrong_completion);
    executor.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
}

void BackendExecutorTests::receiverDestructionDiscardsCompletion()
{
    BackendExecutor executor;
    auto receiver = std::make_unique<QObject>();
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    bool called{false};
    bool after{false};
    QVERIFY(executor.submit(receiver.get(), [gate] {
        gate->entered.release();
        gate->release.acquire();
        return 1;
    }, [&](int) { called = true; }));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    receiver.reset();
    QVERIFY(executor.submit(this, [] {}, [&] { after = true; }));
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(after, TIMEOUT);
    QVERIFY(!called);
    executor.shutdown();
    QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
}

void BackendExecutorTests::shutdownDrainsWithoutWaitingAndDiscardsResults()
{
    BackendExecutor executor;
    QSignalSpy drained{&executor, &BackendExecutor::drained};
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    auto commands = std::make_shared<std::atomic<int>>(0);
    bool applied{false};
    QVERIFY(executor.submit(this, [gate, commands] {
        gate->entered.release();
        gate->release.acquire();
        ++*commands;
    }, [&] { applied = true; }));
    QVERIFY(executor.submit(this, [commands] { ++*commands; }, [&] { applied = true; }));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    executor.shutdown();
    executor.shutdown();
    QVERIFY(!executor.submit(this, [] {}, [] {}));
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, TIMEOUT);
    QCOMPARE(drained.count(), 0);
    gate->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(drained.count(), 1, TIMEOUT);
    QCOMPARE(commands->load(), 2);
    QVERIFY(!applied);
    QVERIFY(executor.isDrained());
}

void BackendExecutorTests::destructionReleasesBackendCapturesOnWorker()
{
    auto executor = std::make_unique<BackendExecutor>();
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    const auto gui_thread = QThread::currentThread();
    auto released = std::make_shared<std::atomic_bool>(false);
    auto backend = std::shared_ptr<int>(new int{42}, [released, gui_thread](int* value) {
        *released = QThread::currentThread() != gui_thread;
        delete value;
    });
    bool applied{false};
    QVERIFY(executor->submit(this, [gate, backend = std::move(backend)] {
        gate->entered.release();
        gate->release.acquire();
        return *backend;
    }, [&](int) { applied = true; }));
    QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
    executor.reset();
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY_WITH_TIMEOUT(gui_progress, TIMEOUT);
    gate->release.release();
    QTRY_VERIFY_WITH_TIMEOUT(released->load(), TIMEOUT);
    QVERIFY(!applied);
}

void BackendExecutorTests::discardedOwningResultIsDestroyedOnWorker()
{
    for (const bool shutdown : {false, true}) {
        BackendExecutor executor;
        auto receiver = std::make_unique<QObject>();
        auto gate = std::make_shared<Gate>();
        const auto unblock = qScopeGuard([gate] { gate->release.release(); });
        auto destroyed = std::make_shared<std::atomic_bool>(false);
        const auto gui_thread = QThread::currentThread();
        bool applied{false};
        QVERIFY(executor.submit(receiver.get(), [gate, destroyed, gui_thread] {
            gate->entered.release();
            gate->release.acquire();
            return std::shared_ptr<int>(new int{1}, [destroyed, gui_thread](int* value) {
                *destroyed = QThread::currentThread() != gui_thread;
                delete value;
            });
        }, [&](std::shared_ptr<int>) { applied = true; }));
        QTRY_VERIFY_WITH_TIMEOUT(gate->entered.available(), TIMEOUT);
        if (shutdown) executor.shutdown();
        else receiver.reset();
        gate->release.release();
        QTRY_VERIFY_WITH_TIMEOUT(destroyed->load(), TIMEOUT);
        QVERIFY(!applied);
        executor.shutdown();
        QTRY_VERIFY_WITH_TIMEOUT(executor.isDrained(), TIMEOUT);
    }
}

void BackendExecutorTests::completionCanDestroyExecutor()
{
    for (bool fail : {false, true}) {
        auto executor = std::make_unique<BackendExecutor>();
        bool completed{false};
        const auto destroy_owner = [&] {
            executor.reset();
            completed = true;
        };
        QVERIFY(executor->submit(this, [fail] {
            if (fail) throw std::runtime_error{"backend error"};
        }, destroy_owner, [&](std::exception_ptr) { destroy_owner(); }));
        QTRY_VERIFY_WITH_TIMEOUT(completed, TIMEOUT);
        QVERIFY(!executor);
        bool drained{false};
        BackendExecutor::shutdownAll(this, [&] { drained = true; });
        QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    }
}

void BackendExecutorTests::ownerDestructionAfterResultOrException()
{
    for (int attempt = 0; attempt < 32; ++attempt) {
        auto executor = std::make_unique<BackendExecutor>();
        bool completed{false};
        const bool fail = attempt % 2;
        QVERIFY(executor->submit(this, [fail] {
            if (fail) throw std::runtime_error{"backend error"};
        }, [&] { completed = true; }, [&](std::exception_ptr) { completed = true; }));
        QTRY_VERIFY_WITH_TIMEOUT(completed, TIMEOUT);
        executor.reset();
    }
    bool drained{false};
    BackendExecutor::shutdownAll(this, [&] { drained = true; });
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
    }
    bool drained{false};
    BackendExecutor::shutdownAll(&receiver, [&] {
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
    BackendExecutor::shutdownAll(&receiver, [&] {
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
        BackendExecutor::shutdownAll(this, [&] { drained = true; });
        QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
        QCOMPARE(completions, 1);
    }
}

void BackendExecutorTests::taskDrainWaitsForCompletionCleanup()
{
    BackendExecutor executor;
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
    bool drained{false};
    BackendExecutor::shutdownAll(this, [&] {
        drained = true;
        QCOMPARE(completions, cancel ? 1 : 0);
        QVERIFY(destroyed_on_worker->load());
    });
    QTRY_VERIFY_WITH_TIMEOUT(drained, TIMEOUT);
    QCOMPARE(completions, cancel ? 1 : 0);
}

void BackendExecutorTests::drainCallbacksAreQueuedAndLifetimeGuarded()
{
    BackendExecutor executor;
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

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BackendExecutorTests)
#else
QTEST_GUILESS_MAIN(BackendExecutorTests)
#endif
#include <test_backendexecutor.moc>
