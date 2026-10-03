// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/backendstage.h>
#include <qml/asyncjoin.h>

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
    void finalDrainIncludesDestroyedValueOwners();
    void finalDrainWaitsForDeferredThreadCleanup();
    void finalDrainIncludesDestroyedThreadOwner();
    void serialWorkPublishesOwnedResultsOnGui();
    void failureDoesNotStopLaterCommands();
    void receiverDestructionDiscardsCompletion();
    void shutdownDrainsWithoutWaitingAndDiscardsResults();
    void destructionReleasesBackendCapturesOnWorker();
    void ownerDestructionAfterResultOrException();
    void discardedOwningResultIsDestroyedOnWorker();
    void bootstrapStageProcessesGuiEventsAndReturnsOwnedResult();
};

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

void BackendExecutorTests::bootstrapStageProcessesGuiEventsAndReturnsOwnedResult()
{
    auto gate = std::make_shared<Gate>();
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] {
        gui_progress = true;
        gate->release.release();
    });
    const auto result = RunBackendStage([gate] {
        gate->release.acquire();
        return std::make_unique<int>(42);
    });
    QVERIFY(gui_progress);
    QCOMPARE(*result, 42);
    bool threw_expected{false};
    try {
        RunBackendStage([]() -> int { throw std::runtime_error{"failed stage"}; });
    } catch (const std::runtime_error&) {
        threw_expected = true;
    } catch (...) {
        QFAIL("Expected std::runtime_error from the failed backend stage");
    }
    QVERIFY(threw_expected);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BackendExecutorTests)
#else
QTEST_GUILESS_MAIN(BackendExecutorTests)
#endif
#include <test_backendexecutor.moc>
