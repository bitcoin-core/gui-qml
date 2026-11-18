// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>
#include <qml/executor.h>
#include <qml/retainedvalue.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <deque>
#include <future>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

using namespace std::chrono_literals;

class ExecutorTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void closeDrainsInOrderAndNotifiesEarlyAndLateListeners()
    {
        using namespace execution;
        std::deque<std::function<void()>> pending;
        Executor executor{[&](Lane, UtilityWorker, std::function<void()> task) {
            pending.push_back(std::move(task));
            return true;
        }};
        auto queue = executor.makeQueue(Lane::Core);
        auto other = executor.makeQueue(Lane::Core);
        std::vector<int> order;
        const auto runNext = [&] {
            auto task = std::move(pending.front());
            pending.pop_front();
            task();
        };
        QVERIFY(queue.post([&] { order.push_back(1); }));
        QVERIFY(queue.post([&] { order.push_back(2); }));
        queue.whenDrained([&] { order.push_back(3); });
        queue.whenDrained([&] { order.push_back(4); });
        queue.close();
        queue.close();
        QVERIFY(!queue.post([&] { order.push_back(-1); }));
        QCOMPARE(pending.size(), 1U);
        runNext();
        QCOMPARE(order, (std::vector{1}));
        runNext();
        QCOMPARE(order, (std::vector{1, 2}));
        runNext();
        QCOMPARE(order, (std::vector{1, 2, 3, 4}));
        queue.whenDrained([&] { order.push_back(5); });
        runNext();
        QVERIFY(other.post([&] { order.push_back(6); }));
        runNext();
        QCOMPARE(order, (std::vector{1, 2, 3, 4, 5, 6}));
    }

    void utilityQueuesTakeTurns()
    {
        using namespace execution;
        std::deque<std::function<void()>> pending;
        Executor executor{[&](Lane, UtilityWorker, std::function<void()> task) {
            pending.push_back(std::move(task));
            return true;
        }};
        auto first = executor.makeQueue(Lane::Utility);
        auto second = executor.makeQueue(Lane::Utility);
        std::vector<int> order;
        QVERIFY(first.post([&] { order.push_back(1); }));
        QVERIFY(first.post([&] { order.push_back(3); }));
        QVERIFY(second.post([&] { order.push_back(2); }));
        while (!pending.empty()) {
            auto task = std::move(pending.front());
            pending.pop_front();
            task();
        }
        QCOMPARE(order, (std::vector{1, 2, 3}));
    }

    void customDispatchOwnsPendingWorkAfterStop()
    {
        using namespace execution;
        std::function<void()> pending;
        bool ran{};
        {
            Executor executor{[&](Lane, UtilityWorker, std::function<void()> task) {
                pending = std::move(task);
                return true;
            }};
            auto queue = executor.makeQueue(Lane::Core);
            QVERIFY(queue.post([&] { ran = true; }));
        }
        QVERIFY(!ran);
        pending();
        QVERIFY(ran);
    }

    void captureRetirementCanCloseAndPrecedesDrain()
    {
        using namespace execution;
        Executor executor{[](Lane, UtilityWorker, std::function<void()> task) {
            task();
            return true;
        }};
        auto queue = executor.makeQueue(Lane::Core);
        std::vector<int> order;
        auto capture = std::shared_ptr<void>{nullptr, [&](void*) {
            order.push_back(2);
            queue.close();
        }};
        queue.whenDrained([&] { order.push_back(3); });
        QVERIFY(queue.post([&, capture = std::move(capture)] {
            order.push_back(1);
        }));
        QCOMPARE(order, (std::vector{1, 2, 3}));
    }

    void rejectedDispatchReleasesCaptureOutsideTheQueue_data()
    {
        QTest::addColumn<bool>("throws");
        QTest::newRow("rejected") << false;
        QTest::newRow("transport-throws") << true;
    }

    void rejectedDispatchReleasesCaptureOutsideTheQueue()
    {
        QFETCH(bool, throws);
        using namespace execution;
        bool reject{true};
        Executor executor{[&](Lane, UtilityWorker, std::function<void()> task) {
            if (std::exchange(reject, false)) {
                if (throws) throw std::runtime_error("Transport unavailable");
                return false;
            }
            task();
            return true;
        }};
        auto queue = executor.makeQueue(Lane::Core);
        bool ran{false}, admitted{false};
        auto capture = std::shared_ptr<void>{nullptr, [&](void*) {
            admitted = queue.post([&] { ran = true; });
        }};
        QVERIFY(!queue.post([capture = std::move(capture)] { QFAIL("Rejected work ran"); }));
        QVERIFY(admitted);
        QVERIFY(ran);
    }

    void priorityRpcAndCleanupRemainIndependent()
    {
        using namespace execution;
        Executor executor{Executor::Config{}};
        auto core = executor.makeQueue(Lane::Core);
        auto idle = executor.makeQueue(Lane::Core);
        auto priority = executor.makeQueue(Lane::Priority);
        auto rpc = executor.makeQueue(Lane::Rpc);
        auto signer = executor.makeQueue(Lane::Utility, UtilityWorker::Signer);
        auto cleanup = executor.makeQueue(Lane::Utility);

        std::promise<void> release, core_started, signer_started, priority_done, rpc_done, cleanup_done, drained;
        const auto barrier = release.get_future().share();
        auto core_wait = core_started.get_future();
        auto signer_wait = signer_started.get_future();
        auto priority_wait = priority_done.get_future();
        auto rpc_wait = rpc_done.get_future();
        auto cleanup_wait = cleanup_done.get_future();
        auto drain_wait = drained.get_future();

        const bool core_posted = core.post([&] { core_started.set_value(); barrier.wait(); });
        const bool signer_posted = signer.post([&] { signer_started.set_value(); barrier.wait(); });
        const bool core_running = core_wait.wait_for(3s) == std::future_status::ready;
        const bool signer_running = signer_wait.wait_for(3s) == std::future_status::ready;
        const bool priority_posted = priority.post([&] { priority_done.set_value(); });
        const bool rpc_posted = rpc.post([&] { rpc_done.set_value(); });
        const bool cleanup_posted = cleanup.post([&] { cleanup_done.set_value(); });
        idle.whenDrained([&] { drained.set_value(); });
        idle.close();
        const bool priority_finished = priority_wait.wait_for(3s) == std::future_status::ready;
        const bool rpc_finished = rpc_wait.wait_for(3s) == std::future_status::ready;
        const bool cleanup_finished = cleanup_wait.wait_for(3s) == std::future_status::ready;
        const bool idle_finished = drain_wait.wait_for(3s) == std::future_status::ready;
        release.set_value();
        executor.stopAndJoin();

        QVERIFY(core_posted && signer_posted && priority_posted && rpc_posted && cleanup_posted);
        QVERIFY(core_running && signer_running);
        QVERIFY(priority_finished);
        QVERIFY(rpc_finished);
        QVERIFY(cleanup_finished);
        QVERIFY(idle_finished);
    }

    void additionalQueuesShareTheSameCoreWorker()
    {
        using namespace execution;
        Executor executor{Executor::Config{}};
        std::vector<SerialQueue> queues;
        std::vector<std::thread::id> thread_ids;
        for (int i = 0; i < 12; ++i) {
            queues.push_back(executor.makeQueue(Lane::Core));
            QVERIFY(queues.back().post([&] { thread_ids.push_back(std::this_thread::get_id()); }));
        }
        executor.stopAndJoin();
        QCOMPARE(thread_ids.size(), queues.size());
        for (const auto id : thread_ids) QVERIFY(id == thread_ids.front());
    }

    void concurrentCloseDrainsExactlyTheAcceptedPosts()
    {
        using namespace execution;
        Executor executor{Executor::Config{}};
        auto queue = executor.makeQueue(Lane::Core);
        std::atomic<unsigned> completed{0};
        std::promise<void> started;
        auto started_wait = started.get_future();
        unsigned accepted{0};
        std::thread producer{[&] {
            while (queue.post([&] { ++completed; })) {
                if (++accepted == 1) started.set_value();
                if (accepted == 1000) break;
            }
        }};
        const bool admitted = started_wait.wait_for(3s) == std::future_status::ready;
        queue.close();
        producer.join();
        executor.stopAndJoin();

        QVERIFY(admitted);
        QVERIFY(!queue.post([] {}));
        QCOMPARE(completed.load(), accepted);
    }

    void refusedCoreTaskReleasesClosingOutcomeQueue()
    {
        using namespace execution;
        int deliveries{0}, drains{0};
        std::deque<std::function<void()>> utility;
        std::shared_ptr<SerialQueue> outcomes;
        Executor executor{[&](Lane lane, UtilityWorker, std::function<void()> turn) {
            if (lane == Lane::Core) {
                outcomes->close();
                outcomes->whenDrained([&] { ++drains; });
                return false;
            }
            utility.push_back(std::move(turn));
            return true;
        }};
        outcomes = std::make_shared<SerialQueue>(executor.makeQueue(Lane::Utility));
        auto cleanup = std::make_shared<ValueCleanupQueue>(outcomes);
        auto core = executor.makeQueue(Lane::Core);
        const auto result = cleanup->postWithOutcome(core, MakeTask([] {}, [&] { ++deliveries; }, {}));
        QCOMPARE(result.rejection, Rejection::Transport);
        QCOMPARE(drains, 0);
        QCOMPARE(utility.size(), size_t{1});
        auto notify = std::move(utility.front());
        utility.pop_front();
        notify();
        QCOMPARE(drains, 1);
        QCOMPARE(deliveries, 0);
        QVERIFY(utility.empty());
    }

    void shutdownRetiresAcceptedValuesOnTheirUtilityWorker()
    {
        using namespace execution;
        Executor executor{Executor::Config{}};
        auto queue = std::make_shared<SerialQueue>(executor.makeQueue(Lane::Utility));
        auto cleanup = std::make_shared<ValueCleanupQueue>(queue);
        std::thread::id utility, retired;
        QVERIFY(queue->post([&] { utility = std::this_thread::get_id(); }));
        auto hold = cleanup->hold([&] { retired = std::this_thread::get_id(); });
        QVERIFY(hold);
        queue->close();
        QVERIFY(!cleanup->hold([] {}));
        auto core = executor.makeQueue(Lane::Core);
        QVERIFY(core.post([hold = std::move(hold)] {}));
        executor.stopAndJoin();
        QVERIFY(utility != std::thread::id{});
        QVERIFY(retired == utility);
    }

    void stopJoinsThreadLocalDestruction()
    {
        using namespace execution;
        struct Retirement {
            std::promise<void>& entered;
            std::future<void> release;
            ~Retirement() { entered.set_value(); release.wait(); }
        };

        std::promise<void> entered, release;
        auto entered_wait = entered.get_future();
        Executor executor{Executor::Config{}};
        auto queue = executor.makeQueue(Lane::Core);
        QVERIFY(queue.post([&] {
            thread_local Retirement retirement{entered, release.get_future()}; // NOLINT(bitcoin-nontrivial-threadlocal): Tests thread exit.
        }));
        auto shutdown = std::async(std::launch::async, [&] { executor.stopAndJoin(); });
        const bool destructor_entered = entered_wait.wait_for(3s) == std::future_status::ready;
        const bool premature = shutdown.wait_for(0ms) == std::future_status::ready;
        release.set_value();
        shutdown.get();
        QVERIFY(destructor_entered);
        QVERIFY(!premature);
        QVERIFY(!queue.post([] {}));
    }

    void cleanupUtilityWorkerIsSeparateFromIoAndJoinedOnStop()
    {
        using namespace execution;
        std::promise<std::thread::id> cleanup_entered, io_ran;
        std::promise<void> release_cleanup;
        auto cleanup_wait = cleanup_entered.get_future();
        auto io_wait = io_ran.get_future();
        auto release_future = release_cleanup.get_future().share();

        Executor executor{Executor::Config{}};
        auto cleanup = executor.makeQueue(Lane::Utility, UtilityWorker::Cleanup);
        auto io = executor.makeQueue(Lane::Utility, UtilityWorker::IO);
        QVERIFY(cleanup.post([&] {
            cleanup_entered.set_value(std::this_thread::get_id());
            release_future.wait();
        }));
        QCOMPARE(cleanup_wait.wait_for(3s), std::future_status::ready);
        const auto cleanup_thread = cleanup_wait.get();

        QVERIFY(io.post([&] { io_ran.set_value(std::this_thread::get_id()); }));
        QCOMPARE(io_wait.wait_for(3s), std::future_status::ready);
        QVERIFY(io_wait.get() != cleanup_thread);

        auto shutdown = std::async(std::launch::async, [&] { executor.stopAndJoin(); });
        QCOMPARE(shutdown.wait_for(50ms), std::future_status::timeout);
        release_cleanup.set_value();
        shutdown.get();
        QVERIFY(!cleanup.post([] {}));
    }
};

BITCOINQML_REGISTER_QT_TEST(ExecutorTests)
#include <test_executor.moc>
