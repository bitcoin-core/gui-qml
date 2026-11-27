// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>
#include <qml/retainedsnapshot.h>
#include <qml/retainedvalue.h>
#include <qml/snapshotquery.h>
#include <qml/sourceepoch.h>
#include <qml/models/activitysnapshot.h>
#include <qml/models/feereadstate.h>
#include <qml/walletlifecyclestate.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualnodeobservations.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QSemaphore>

#include <atomic>
#include <thread>

#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

class ExecutionTests : public QObject
{
    Q_OBJECT
    static execution::SnapshotQuery<int>::Post postTask(execution::SerialQueue& queue, execution::SerialQueue& outcomes)
    {
        return [&queue, &outcomes](std::shared_ptr<execution::Task> task) {
            const bool accepted = queue.post([task, &outcomes] {
                task->execute();
                QVERIFY(outcomes.post([task] { task->deliver(); }));
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
    }
    static std::shared_ptr<execution::ValueCleanupQueue> cleanupQueue(execution::Executor& executor)
    {
        return std::make_shared<execution::ValueCleanupQueue>(
            std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility)));
    }
    static void checkCleanupInterleaving(bool close_executor)
    {
        using namespace execution;
        ManualStartupExecution worker;
        auto queue = std::make_shared<SerialQueue>(worker.executor().makeQueue(Lane::Utility));
        auto values = std::make_shared<ValueCleanupQueue>(queue);
        std::vector<std::shared_ptr<void>> held;
        int retired{0};
        for (int index = 0; index < 257; ++index) {
            const auto owner = close_executor ? std::make_shared<ValueCleanupQueue>(queue) : values;
            held.push_back(owner->hold([&] { ++retired; }));
            QVERIFY(held.back());
        }
        QVERIFY(worker.quiescent());
        bool drained{false};
        if (close_executor) queue->whenDrained([&] { drained = true; });
        else values->close([&] { drained = true; });
        held.clear();
        QCOMPARE(retired, 0);
        int retired_before_other{-1};
        QVERIFY(worker.outcomes.post([&] { retired_before_other = retired; }));
        if (close_executor) worker.executor().stopAndJoin();
        QVERIFY(!drained);
        worker.drain();
        QCOMPARE(retired_before_other, 1);
        QCOMPARE(retired, 257);
        QVERIFY(drained);
        QVERIFY(worker.quiescent());
    }

private Q_SLOTS:
    void cleanupReservedAfterWorkWaitsForItsOutcome_data()
    {
        QTest::addColumn<bool>("stop_executor");
        QTest::newRow("owner-close") << false;
        QTest::newRow("executor-stop") << true;
    }
    void cleanupReservedAfterWorkWaitsForItsOutcome()
    {
        QFETCH(bool, stop_executor);
        using namespace execution;
        ManualStartupExecution env;
        auto delivery = std::make_shared<ValueCleanupQueue>(
            std::make_shared<SerialQueue>(env.executor().makeQueue(Lane::Utility)));
        auto cleanup = std::make_shared<ValueCleanupQueue>(
            std::make_shared<SerialQueue>(env.executor().makeQueue(Lane::Utility)));
        auto held = std::make_shared<std::shared_ptr<void>>();
        std::vector<int> order;
        const auto finish = qScopeGuard([&] { held.reset(); env.drain(); });
        QVERIFY(delivery->postWithOutcome(env.core, MakeTask([&] { order.push_back(1); },
            [&, held] { order.push_back(2); }, {})));
        *held = cleanup->hold([&] { order.push_back(3); });
        QVERIFY(*held);
        held.reset();
        cleanup->close([&] { order.push_back(4); });
        if (stop_executor) env.executor().stopAndJoin();
        QVERIFY(!env.hasWork(Lane::Utility));
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(order, std::vector<int>({1}));
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(order, std::vector<int>({1, 2}));
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(order, std::vector<int>({1, 2, 3}));
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(order, std::vector<int>({1, 2, 3, 4}));
        QVERIFY(env.quiescent());
    }
    void throwingInitialTransportRollsBackOnlyItsAdmission_data()
    {
        QTest::addColumn<bool>("priority");
        QTest::addColumn<bool>("retain");
        QTest::newRow("core") << false << false;
        QTest::newRow("core-retained") << false << true;
        QTest::newRow("priority") << true << false;
        QTest::newRow("priority-retained") << true << true;
    }
    void throwingInitialTransportRollsBackOnlyItsAdmission()
    {
        QFETCH(bool, priority);
        QFETCH(bool, retain);
        using namespace execution;
        bool reject{true};
        std::deque<std::function<void()>> turns;
        Executor executor{[&](Lane, UtilityWorker, auto turn) {
            if (std::exchange(reject, false)) {
                if (retain) turns.push_back(std::move(turn));
                throw std::runtime_error{"transport refused"};
            }
            turns.push_back(std::move(turn));
            return true;
        }};
        auto work = executor.makeQueue(priority ? Lane::Priority : Lane::Core);
        auto cleanup = std::make_shared<ValueCleanupQueue>(std::make_shared<SerialQueue>(executor.makeQueue(Lane::Utility)));
        auto outcomes = executor.makeQueue(Lane::Utility);
        const auto step = [&] { auto turn = std::move(turns.front()); turns.pop_front(); turn(); };
        int effects{0}, delivered{0}, retired{0}, failed{0};
        const auto drain = qScopeGuard([&] { while (!turns.empty()) step(); });
        auto held = cleanup->hold([&] { ++retired; });
        QVERIFY(held);
        const auto submit = [&] {
            return postTask(work, outcomes)(MakeTask([&] { ++effects; }, [&] { ++delivered; },
                [&](std::exception_ptr) { ++failed; }));
        };
        QVERIFY(!submit());
        QCOMPARE(turns.size(), retain ? size_t{1} : size_t{0});
        QVERIFY(submit());
        if (retain) step();
        QCOMPARE(effects, 0);
        QCOMPARE(delivered, 0);
        while (!turns.empty()) step();
        QCOMPARE(effects, 1);
        QCOMPARE(delivered, 1);
        QCOMPARE(failed, 0);
        QCOMPARE(retired, 0);
        held.reset();
        step();
        QCOMPARE(retired, 1);
        QVERIFY(turns.empty());
    }
    void throwingLaterCoreTransportKeepsEveryAcceptedOutcome_data()
    {
        QTest::addColumn<bool>("retain");
        QTest::newRow("before-handoff") << false;
        QTest::newRow("retained-callback") << true;
    }
    void throwingLaterCoreTransportKeepsEveryAcceptedOutcome()
    {
        QFETCH(bool, retain);
        using namespace execution;
        struct Turn { Lane lane; std::function<void()> run; };
        std::deque<Turn> turns;
        int core_posts{0}, destroyed{0};
        Lane active{Lane::Priority}, destroyed_on{Lane::Priority};
        std::vector<int> effects, outcomes;
        Executor executor{[&](Lane lane, UtilityWorker, auto turn) {
            if (lane == Lane::Core && ++core_posts == 2) {
                if (retain) turns.push_back({lane, std::move(turn)});
                throw std::runtime_error{"later Core transport refused"};
            }
            turns.push_back({lane, std::move(turn)});
            return true;
        }};
        auto core = executor.makeQueue(Lane::Core);
        auto delivery = cleanupQueue(executor);
        const auto drain = [&] {
            while (!turns.empty()) {
                auto turn = std::move(turns.front());
                turns.pop_front();
                active = turn.lane;
                turn.run();
            }
        };
        const auto finish = qScopeGuard([&] { drain(); });
        auto capture = std::shared_ptr<void>{nullptr, [&](void*) {
            ++destroyed;
            destroyed_on = active;
        }};
        for (int value : {1, 2, 3}) {
            QVERIFY(delivery->postWithOutcome(core, MakeTask([&, value] {
                effects.push_back(value);
                return value;
            }, [&, value, owner = value == 2 ? capture : nullptr](int result) {
                QCOMPARE(active, Lane::Utility);
                QCOMPARE(result, value);
                outcomes.push_back(result);
            }, {})));
        }
        capture.reset();
        executor.stopAndJoin();
        QVERIFY(effects.empty());
        drain();
        QCOMPARE(effects, std::vector<int>({1, 2, 3}));
        QCOMPARE(outcomes, effects);
        QCOMPARE(destroyed, 1);
        QCOMPARE(destroyed_on, Lane::Utility);
        QVERIFY(turns.empty());
    }
    void finalAdmissionPreservesExistingDrain_data()
    {
        QTest::addColumn<bool>("accepted");
        QTest::newRow("last-accepted") << true;
        QTest::newRow("already-closed") << false;
    }
    void finalAdmissionPreservesExistingDrain()
    {
        QFETCH(bool, accepted);
        using namespace execution;
        ManualStartupExecution env;
        auto delivery = cleanupQueue(env.executor());
        auto cleanup = cleanupQueue(env.executor());
        int effects{0}, outcomes{0}, retired{0};
        std::shared_ptr<void> held;
        const auto finish = qScopeGuard([&] { held.reset(); env.drain(); });
        const auto submit = [&] {
            return delivery->postWithOutcome(env.core, MakeTask([&] { ++effects; }, [&] { ++outcomes; }, {}));
        };
        if (accepted) {
            held = cleanup->hold([&] { ++retired; });
            QVERIFY(held);
            QVERIFY(submit());
        }
        env.executor().stopAndJoin();
        QVERIFY(!submit());
        QVERIFY(!cleanup->hold([&] { ++retired; }));
        QCOMPARE(effects, 0);
        QCOMPARE(retired, 0);
        env.drain();
        QCOMPARE(effects, int(accepted));
        QCOMPARE(outcomes, int(accepted));
        QCOMPARE(retired, 0);
        held.reset();
        env.drain();
        QCOMPARE(retired, int(accepted));
        QVERIFY(env.quiescent());
    }
    void newQueuesCannotReuseAClosedOwnersAdmission()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto original = env.executor().makeQueue(Lane::Core);
        int first{0}, second{0};
        const auto finish = qScopeGuard([&] { env.drain(); });
        QVERIFY(original.post([&] { ++first; }));
        original.close();
        auto successor = env.executor().makeQueue(Lane::Core);
        QVERIFY(!original.post([&] { ++second; }));
        QVERIFY(successor.post([&] { ++second; }));
        env.executor().stopAndJoin();
        auto late = env.executor().makeQueue(Lane::Core);
        QVERIFY(!late.post([&] { ++second; }));
        QVERIFY(!successor.post([&] { ++first; }));
        QCOMPARE(first, 0);
        QCOMPARE(second, 0);
        env.drain();
        QCOMPARE(first, 1);
        QCOMPARE(second, 1);
        QVERIFY(env.quiescent());
    }
    void ownersRetainBothOriginalDrainCallbacks()
    {
        ManualStartupExecution env;
        auto first = cleanupQueue(env.executor());
        auto second = cleanupQueue(env.executor());
        std::vector<int> drained;
        auto one = first->hold([] {});
        auto two = second->hold([] {});
        QVERIFY(one && two);
        const auto finish = qScopeGuard([&] { one.reset(); two.reset(); env.drain(); });
        first->close([&] { drained.push_back(1); });
        second->close([&] { drained.push_back(2); });
        env.executor().stopAndJoin();
        QVERIFY(env.quiescent());
        one.reset();
        env.drain();
        QCOMPARE(drained, std::vector<int>({1}));
        two.reset();
        env.drain();
        QCOMPARE(drained, std::vector<int>({1, 2}));
        QVERIFY(env.quiescent());
    }
    void closingOneObservationLeavesItsSiblingAlone()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto submit = postTask(env.core, env.outcomes);
        int original_reads{0}, original_notices{0}, sibling_reads{0}, sibling_notices{0};
        auto original = std::make_shared<SnapshotQuery<int>>([&] { return ++original_reads; },
            [&] { ++original_notices; }, submit);
        auto sibling = std::make_shared<SnapshotQuery<int>>([&] { return ++sibling_reads; },
            [&] { ++sibling_notices; }, submit);
        const auto finish = qScopeGuard([&] { env.drain(); });
        original->request();
        sibling->request();
        original->close();
        original->close();
        original->request();
        env.drain();
        QCOMPARE(original_reads, 0);
        QCOMPARE(original_notices, 0);
        QVERIFY(!original->take());
        QCOMPARE(sibling_reads, 1);
        QCOMPARE(sibling_notices, 1);
        auto result = sibling->take();
        QVERIFY(result && result->snapshot);
        QCOMPARE(*result->snapshot, 1);
        QVERIFY(env.quiescent());
    }
    void releasingABorrowKeepsTheOriginalOwnerAlive()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto values = cleanupQueue(env.executor());
        int retired{0};
        auto original = values->hold([&] { ++retired; });
        QVERIFY(original);
        auto borrowed = original;
        borrowed.reset();
        QVERIFY(!env.step(Lane::Utility));
        env.executor().stopAndJoin();
        QVERIFY(!env.step(Lane::Utility));
        QCOMPARE(retired, 0);
        original.reset();
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(retired, 1);
        QVERIFY(env.quiescent());
    }
    void retiredOwnerWaitsForTheLastBorrowAndItsCleanup()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto values = cleanupQueue(env.executor());
        int retired{0}, drained{0};
        auto held = values->hold([&] { ++retired; });
        QVERIFY(held);
        const auto finish = qScopeGuard([&] { held.reset(); env.drain(); });
        values->close([&] { ++drained; });
        values.reset();
        env.executor().stopAndJoin();
        QVERIFY(!env.step(Lane::Utility));
        held.reset();
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(retired, 1);
        QCOMPARE(drained, 0);
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(drained, 1);
        QVERIFY(env.quiescent());
    }
    void cancelledTransportCannotReenterQueuedCleanup_data()
    {
        QTest::addColumn<bool>("control");
        QTest::newRow("Core-transport") << false;
        QTest::newRow("Control-transport") << true;
    }
    void cancelledTransportCannotReenterQueuedCleanup()
    {
        QFETCH(bool, control);
        using namespace execution;
        ManualNodeObservations env;
        const auto lane = control ? Lane::Priority : Lane::Core;
        auto work = env.executor().makeQueue(lane);
        auto values = cleanupQueue(env.executor());
        auto delivery = cleanupQueue(env.executor());
        int effects{0}, completed{0}, cancelled{0}, retired{0}, drained{0};
        ChangeCounter source;
        const auto finish = qScopeGuard([&] { env.drain(); });
        auto held = values->hold([&] { ++retired; });
        QVERIFY(held);
        QVERIFY(delivery->postWithOutcome(work, MakeTask([&, revision = source.value.load()] {
            if (!source.current(revision)) throw NotStarted{NotStarted::Cause::Cancelled};
            ++effects;
        }, [&] { ++completed; }, [&, held = std::move(held)](auto error) {
            QCOMPARE(NotStartedCause(error), std::make_optional(NotStarted::Cause::Cancelled));
            ++cancelled;
        })));
        values->close([&] { ++drained; });
        source.advance();
        auto stale = env.take(lane);
        QVERIFY(stale);
        stale();
        QCOMPARE(cancelled, 0);
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(cancelled, 1);
        stale();
        QCOMPARE(effects, 0);
        QCOMPARE(completed, 0);
        QCOMPARE(retired, 0);
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(retired, 1);
        QCOMPARE(drained, 0);
        env.drain();
        QCOMPARE(drained, 1);
        QCOMPARE(cancelled, 1);
        QVERIFY(env.turnsIdle());
    }
    void unavailableRetainedValuePortsNeverConstructValues()
    {
        int constructed{0};
        const auto factory = [&] { ++constructed; return std::make_unique<int>(42); };
        QVERIFY(!execution::RetainSnapshot<int>({}, factory));
        QVERIFY(!execution::RetainValues<int>(execution::ValueCleanup{}, factory));
        QCOMPARE(constructed, 0);

        ManualStartupExecution env;
        auto queue = std::make_shared<execution::SerialQueue>(env.executor().makeQueue(execution::Lane::Utility));
        auto values = std::make_shared<execution::ValueCleanupQueue>(queue);
        bool drained{false};
        int retired{0};
        values->close([&] { drained = true; });
        QVERIFY(!values->hold([&] { ++retired; }));
        QVERIFY(!execution::RetainValues<int>([&](auto cleanup) { return values->hold(std::move(cleanup)); }, factory));
        env.drain();
        QCOMPARE(constructed, 0);
        QCOMPARE(retired, 0);
        QVERIFY(drained);
        QVERIFY(env.quiescent());
    }
    void rejectedProvisionalHoldRetiresItsReservation()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto queue = std::make_shared<SerialQueue>(env.executor().makeQueue(Lane::Utility));
        auto values = std::make_shared<ValueCleanupQueue>(queue);
        int constructed{0}, retired{0};
        const ValueCleanup reject = [&](auto cleanup) -> std::shared_ptr<void> {
            auto provisional = values->hold([&, cleanup = std::move(cleanup)] {
                cleanup();
                ++retired;
            });
            if (!provisional) throw std::logic_error{"Expected a provisional hold"};
            return {};
        };
        QVERIFY(!RetainValues<int>(reject, [&] { ++constructed; return std::make_unique<int>(42); }));
        QCOMPARE(constructed, 0);
        QCOMPARE(retired, 0);
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(retired, 1);
        QVERIFY(env.quiescent());
    }
    void staleTransportCannotResolveANewerTask()
    {
        using namespace execution;
        ManualStartupExecution env;
        const auto submit = postTask(env.core, env.outcomes);
        int original_effects{0}, original_outcomes{0}, next_effects{0}, next_outcomes{0};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        QVERIFY(submit(MakeTask([&] { ++original_effects; }, [&] { ++original_outcomes; }, {})));
        auto original = env.take(Lane::Core);
        QVERIFY(original);
        original();
        env.drain();
        QCOMPARE(original_effects, 1);
        QCOMPARE(original_outcomes, 1);
        QVERIFY(submit(MakeTask([&] { ++next_effects; }, [&] { ++next_outcomes; }, {})));
        original();
        QCOMPARE(original_effects, 1);
        QCOMPARE(original_outcomes, 1);
        QCOMPARE(next_effects, 0);
        QVERIFY(env.hasWork(Lane::Core));
        env.drain();
        env.core.close();
        original();
        QCOMPARE(original_effects, 1);
        QCOMPARE(original_outcomes, 1);
        QCOMPARE(next_effects, 1);
        QCOMPARE(next_outcomes, 1);
        QVERIFY(env.quiescent());
    }
    void discardedObservationFailureDoesNotEraseTheNextSuccess()
    {
        using namespace execution;
        ManualStartupExecution env;
        int reads{0}, notices{0}; bool fail{true};
        auto query = std::make_shared<SnapshotQuery<int>>( [&] {
            ++reads; if (fail) throw std::runtime_error{"read unavailable"}; return 42;
        }, [&] { ++notices; }, postTask(env.core, env.outcomes));
        query->discardReadFailure(); QVERIFY(!query->take());
        query->request(); env.drain(); QCOMPARE(reads, 1); QCOMPARE(notices, 1);
        query->discardReadFailure(); QVERIFY(!query->take());
        fail = false; query->request(); env.drain(); QCOMPARE(reads, 2); QCOMPARE(notices, 2);
        query->discardReadFailure();
        const auto result = query->take(); QVERIFY(result); QVERIFY(!result->error); QVERIFY(result->snapshot);
        QCOMPARE(*result->snapshot, 42);
        query->close(); query.reset(); env.drain(); QVERIFY(env.quiescent());
    }
    void completedCommandKeepsResultAfterItsSchedulerRetires()
    {
        using namespace execution;
        int notices{0};
        auto command = std::make_shared<OwnedCommand<int>>([&] { ++notices; });
        std::weak_ptr<SerialQueue> queue;
        {
            ManualStartupExecution env;
            auto core = std::make_shared<SerialQueue>(env.executor().makeQueue(Lane::Core));
            queue = core;
            QVERIFY(command->startWith([&](auto done, auto failed) {
                return postTask(*core, env.outcomes)(MakeTask([] { return 42; }, std::move(done), std::move(failed)));
            }));
            env.drain();
            QVERIFY(env.quiescent());
        }
        QVERIFY(queue.expired());
        const auto result = command->result();
        QVERIFY(result && result->value);
        QCOMPARE(*result->value, 42);
        QCOMPARE(notices, 1);
    }
    void duplicateOwnedOutcomesKeepTheFirstFactAndPublicationStatus_data()
    {
        QTest::addColumn<bool>("throw_notice");
        QTest::newRow("published") << false; QTest::newRow("publication-failed") << true;
    }
    void duplicateOwnedOutcomesKeepTheFirstFactAndPublicationStatus()
    {
        QFETCH(bool, throw_notice);
        using namespace execution;
        int notices{0};
        auto command = std::make_shared<OwnedCommand<int>>([&] {
            ++notices; if (throw_notice) throw std::runtime_error{"publication unavailable"};
        });
        std::function<void(int)> completed; std::function<void(std::exception_ptr)> failed;
        const auto admitted = command->startWith([&](auto done, auto error) {
            completed = std::move(done); failed = std::move(error); return SubmitResult{TaskId{42}, Rejection::None};
        });
        QVERIFY(admitted); QVERIFY(!command->result());
        completed(7); failed(std::make_exception_ptr(std::runtime_error{"late error"})); completed(9);
        const auto result = command->result(); QVERIFY(result); QVERIFY(result->value); QVERIFY(!result->error);
        QCOMPARE(*result->value, 7); QCOMPARE(result->publication_failed, throw_notice); QCOMPARE(notices, 1);
        completed = {}; failed = {}; command.reset();
    }
    void feeReceiptKeepsConcurrentNativeFactsIndependent()
    {
        ManualNodeObservations env;
        auto core = env.executor().makeQueue(execution::Lane::Core);
        auto utility = env.executor().makeQueue(execution::Lane::Utility);
        int reads{0}, fee_observed{0}, facts_observed{0}, outcome_value{0};
        QSemaphore start, posted;
        std::thread native{[&] {
            start.acquire();
            env.post()([&] { ++facts_observed; });
            posted.release();
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            env.drain();
            env.publishAll();
        });
        QVERIFY(postTask(core, utility)(execution::MakeTask([&] { ++reads; return 42; }, [&](int value) {
            outcome_value = value;
            start.release();
            posted.acquire();
            env.post()([&] { ++fee_observed; });
        }, {})));
        QVERIFY(env.step(execution::Lane::Core));
        QCOMPARE(reads, 1);
        QCOMPARE(fee_observed, 0);
        QCOMPARE(facts_observed, 0);
        QVERIFY(env.step(execution::Lane::Utility));
        native.join();
        QCOMPARE(outcome_value, 42);
        auto fee = env.takePublicationBack();
        QVERIFY(fee);
        QVERIFY(!env.publicationsIdle());
        env.publishAll();
        QCOMPARE(facts_observed, 1);
        QCOMPARE(fee_observed, 0);
        fee();
        QCOMPARE(facts_observed, 1);
        QCOMPARE(fee_observed, 1);
        QVERIFY(env.turnsIdle());
        QVERIFY(env.publicationsIdle());
    }
    void manualDuplicatePublicationCopiesMutableCapture()
    {
        ManualExecutionEnvironment env;
        std::vector<int> observed;
        const auto cleanup = qScopeGuard([&] { env.stop(); env.publishAll(); });
        env.post([state = 0, &observed]() mutable { observed.push_back(++state); });
        QVERIFY(env.duplicateFirstPublication());
        QCOMPARE(env.publicationCount(), size_t{2});
        QVERIFY(observed.empty());
        env.publishAll();
        QCOMPARE(observed, std::vector<int>({1, 1}));
        QVERIFY(env.quiescent());
    }
    void routedNativeCleanupIngressKeepsOriginalManualOutcome()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto values = cleanupQueue(env.executor());
        int retired{0}, outcomes{0};
        auto held = values->hold([&] { ++retired; });
        QVERIFY(held);
        values->close([&] { ++outcomes; });
        QVERIFY(env.quiescent());
        QSemaphore start;
        std::atomic_bool finished{false};
        std::thread native{[&, held = std::move(held)]() mutable {
            start.acquire();
            held.reset();
            finished = true;
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            env.drain();
        });
        start.release();
        QVERIFY(QTest::qWaitFor([&] {
            (void)env.queuedCount();
            (void)env.quiescent();
            return finished.load();
        }, 3000));
        native.join();
        QCOMPARE(env.queuedCount(), size_t{1});
        auto retirement = env.take(Lane::Utility);
        QVERIFY(retirement);
        QCOMPARE(retired, 0);
        QCOMPARE(outcomes, 0);
        retirement();
        QCOMPARE(retired, 1);
        QCOMPARE(outcomes, 0);
        auto outcome = env.take(Lane::Utility);
        QVERIFY(outcome);
        outcome();
        QCOMPARE(outcomes, 1);
        QVERIFY(env.quiescent());
    }
    void manualTransportSmallCallableDestructionMayReenter()
    {
        ManualExecutionEnvironment env;
        struct Probe {
            ManualExecutionEnvironment* environment;
            bool armed{false}, queued_phase{false}, busy_during_queued_destruction{false};
            int copies{0}, destroyed{0}, callbacks{0}, observed{0};
            void reenter() {
                (void)environment->publicationsIdle();
                if (queued_phase && !environment->transportIdle()) busy_during_queued_destruction = true;
                environment->post([this] { ++observed; });
            }
        } probe{&env};
        const auto cleanup = qScopeGuard([&] {
            probe.armed = false;
            env.stop(); env.publishAll();
        });
        struct SmallCallable {
            Probe* probe;
            explicit SmallCallable(Probe* value) : probe(value) {}
            SmallCallable(const SmallCallable& other) noexcept : probe(other.probe) {
                if (probe->armed) { ++probe->copies; probe->reenter(); }
            }
            void operator()() const { ++probe->callbacks; }
            ~SmallCallable() { if (probe->armed) { ++probe->destroyed; probe->reenter(); } }
        };
        env.restart(SmallCallable{&probe});
        probe.armed = true; // Client copy/destructor hooks arm after registration.
        env.restart([] {}); // Replacement retires the boxed callable outside lock.
        QVERIFY(probe.destroyed > 0);
        env.stop();
        probe.armed = false;
        env.restart(SmallCallable{&probe});
        probe.armed = true;
        const auto before_stop = probe.destroyed;
        env.stop();
        QVERIFY(probe.destroyed > before_stop);
        env.publishAll();
        QCOMPARE(probe.callbacks, 0);
        QVERIFY(env.quiescent());
        probe.armed = false;
        env.post(SmallCallable{&probe});
        probe.armed = true;
        probe.queued_phase = true;
        QVERIFY(env.publishNext()); // Extract/invoke/destroy queued small callable.
        QCOMPARE(probe.callbacks, 1);
        QVERIFY(probe.busy_during_queued_destruction);
        probe.queued_phase = false;
        env.publishAll();
        QVERIFY(probe.observed > 0);
        QVERIFY(env.quiescent());
        env.restart({});
        QVERIFY(env.transportIdle());
        env.advance(std::chrono::milliseconds{250});
        QCOMPARE(probe.callbacks, 1);
        QVERIFY(env.quiescent());
    }
    void manualTransportNativeIngressKeepsDeliveryControlled()
    {
        ManualNodeObservations env;
        auto core = env.executor().makeQueue(execution::Lane::Core);
        auto utility = env.executor().makeQueue(execution::Lane::Utility);
        const auto submit = postTask(core, utility);
        std::atomic<int> effects{0}, outcomes{0}, publications{0};
        std::atomic_bool finished{false}, rejected{false};
        QSemaphore start;
        std::thread native{[&] {
            start.acquire();
            for (int index = 0; index < 8; ++index) {
                const auto admitted = submit(execution::MakeTask([&] { ++effects; }, [&] { ++outcomes; }, {}));
                if (!admitted) rejected = true;
                env.post()([&] { ++publications; });
            }
            finished = true;
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            env.drain(); env.publishAll();
        });
        start.release();
        QVERIFY(QTest::qWaitFor([&] {
            (void)env.turnsIdle();
            (void)env.publicationsIdle();
            return finished.load();
        }, 3000));
        native.join();
        QVERIFY(!rejected);
        QCOMPARE(effects.load(), 0);
        QCOMPARE(outcomes.load(), 0);
        QCOMPARE(publications.load(), 0);
        QVERIFY(!env.publicationsIdle());
        QVERIFY(!env.turnsIdle());
        env.drain();
        QCOMPARE(effects.load(), 8);
        QCOMPARE(outcomes.load(), 8);
        QCOMPARE(publications.load(), 0);
        QVERIFY(env.turnsIdle());
        env.publishAll();
        QCOMPARE(publications.load(), 8);
        QVERIFY(env.turnsIdle());
        QVERIFY(env.publicationsIdle());
    }
    void manualTransportRetiredTimerCaptureMayReenter()
    {
        ManualExecutionEnvironment env;
        int destroyed{0}, published{0};
        const auto cleanup = qScopeGuard([&] { env.stop(); env.publishAll(); });
        auto capture = std::shared_ptr<int>{new int{0}, [&](int* value) {
            delete value;
            ++destroyed;
            (void)env.trace();
            env.post([&] { ++published; });
        }};
        env.restart([capture] {});
        capture.reset();
        env.stop(); // Last capture destructor must run outside transport mutex.
        QCOMPARE(destroyed, 1);
        QCOMPARE(published, 0);
        QCOMPARE(env.publicationCount(), size_t{1});
        env.publishAll();
        QCOMPARE(published, 1);
        QVERIFY(env.quiescent());
        int expired{0};
        env.restart([&] { ++expired; env.post([&] { ++published; }); });
        env.advance(std::chrono::milliseconds{249});
        QCOMPARE(expired, 0);
        env.advance(std::chrono::milliseconds{1});
        QCOMPARE(expired, 1);
        QCOMPARE(published, 1);
        env.publishAll();
        QCOMPARE(published, 2);
        QVERIFY(env.quiescent());
    }
    void coalescedRequestDuringRejectedAdmissionDoesNotRetryInline()
    {
        using namespace execution;
        bool rejecting{true};
        int reads{0}, notices{0}, attempts{0};
        std::shared_ptr<Task> pending;
        std::shared_ptr<SnapshotQuery<int>> query;
        query = std::make_shared<SnapshotQuery<int>>(
            [&] { ++reads; return 42; }, [&] { ++notices; },
            [&](std::shared_ptr<Task> task) -> SubmitResult {
                ++attempts;
                if (rejecting) {
                    query->request();
                    return {{}, Rejection::Transport};
                }
                pending = std::move(task);
                return {{}, Rejection::None};
            });
        query->request();
        QCOMPARE(attempts, 1); QCOMPARE(reads, 0); QCOMPARE(notices, 1);
        const auto rejected = query->take(); QVERIFY(rejected); QVERIFY(rejected->error); QVERIFY(!rejected->snapshot);
        QVERIFY(!pending);
        rejecting = false;
        query->request(); QCOMPARE(attempts, 2); QCOMPARE(reads, 0);
        QVERIFY(pending);
        pending->execute();
        pending->deliver();
        QCOMPARE(reads, 1); QCOMPARE(notices, 2);
        const auto retried = query->take(); QVERIFY(retried); QVERIFY(!retried->error); QVERIFY(retried->snapshot);
        QCOMPARE(*retried->snapshot, 42);
        query->close();
    }

    void controlSnapshotRunsWhileOrdinaryObservationIsHeld()
    {
        using namespace execution;
        ManualNodeObservations env;
        auto core = env.executor().makeQueue(Lane::Core);
        auto priority = env.executor().makeQueue(Lane::Priority);
        auto outcomes = env.executor().makeQueue(Lane::Utility);
        int ordinary_reads{0}, control_reads{0};
        std::deque<std::function<void()>> gui;
        auto ordinary = std::make_shared<SnapshotQuery<int>>(
            [&] { ++ordinary_reads; return 7; }, [] {}, postTask(core, outcomes));
        std::optional<SnapshotQuery<int>::Result> published;
        std::shared_ptr<SnapshotQuery<int>> control;
        control = std::make_shared<SnapshotQuery<int>>(
            [&] { ++control_reads; return 42; },
            [&] { gui.emplace_back([&] { published = control->take(); }); }, postTask(priority, outcomes));
        ordinary->request(); control->request();
        QCOMPARE(ordinary_reads, 0); QCOMPARE(control_reads, 0); QVERIFY(!published);
        QVERIFY(env.step(Lane::Priority)); QCOMPARE(control_reads, 1); QCOMPARE(ordinary_reads, 0);
        QVERIFY(!published); QVERIFY(gui.empty());
        QVERIFY(env.step(Lane::Utility)); QVERIFY(!published); QCOMPARE(gui.size(), size_t{1});
        auto publication = std::move(gui.front()); gui.pop_front(); publication();
        QVERIFY(published); QVERIFY(published->snapshot); QVERIFY(!published->error); QCOMPARE(*published->snapshot, 42);
        QCOMPARE(ordinary_reads, 0);
        ordinary->close(); control->close(); env.drain();
        QCOMPARE(ordinary_reads, 0); QCOMPARE(control_reads, 1); QVERIFY(env.turnsIdle());
    }

    void originalBorrowKeepsCleanupAliveAfterItsOwnerCloses()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto values = cleanupQueue(env.executor());
        int retired{0};
        auto held = values->hold([&] { ++retired; });
        QVERIFY(held);
        auto borrowed = held;
        values->close({});
        env.executor().stopAndJoin();
        held.reset();
        values.reset();
        QVERIFY(!env.step(Lane::Utility));
        borrowed.reset();
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(retired, 1);
        env.drain();
        QVERIFY(env.quiescent());
    }
    void closingOneOwnerCannotRetireItsSiblingsResource()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto first = cleanupQueue(env.executor());
        auto second = cleanupQueue(env.executor());
        int first_retired{0}, second_retired{0};
        auto a = first->hold([&] { ++first_retired; });
        auto b = second->hold([&] { ++second_retired; });
        QVERIFY(a && b);
        first->close({});
        QVERIFY(!first->hold([] {}));
        auto later = second->hold([&] { ++second_retired; });
        QVERIFY(later);
        a.reset();
        env.drain();
        QCOMPARE(first_retired, 1);
        QCOMPARE(second_retired, 0);
        second->close({});
        b.reset();
        later.reset();
        env.drain();
        QCOMPARE(first_retired, 1);
        QCOMPARE(second_retired, 2);
        QVERIFY(env.quiescent());
    }

    void releasingScopeRetiresUnrequestedCleanupInBoundedBatches()
    {
        checkCleanupInterleaving(false);
    }

    void retiredOwnerRejectsLaterWorkForItsResource()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto values = cleanupQueue(env.executor());
        int retired{0}, touched_after_retirement{0};
        auto held = values->hold([&] { ++retired; });
        QVERIFY(held);
        values->close({});
        held.reset();
        QVERIFY(env.hasWork(Lane::Utility));
        QVERIFY(!values->hold([&] { ++touched_after_retirement; }));
        env.drain();
        QCOMPARE(retired, 1);
        QCOMPARE(touched_after_retirement, 0);
        QVERIFY(env.quiescent());
    }

    void closingLargeIdleInventoryDispatchesBoundedTurns()
    {
        checkCleanupInterleaving(true);
    }

    void missingDispatcherAdmitsNoWork()
    {
        using namespace execution;
        Executor executor{Executor::Dispatch{}};
        auto core = executor.makeQueue(Lane::Core);
        int effects{0};
        QVERIFY(!core.post([&] { ++effects; }));
        core.close();
        QCOMPARE(effects, 0);
    }
    void cancellingWaitingCoreAndPriorityWorkOwnsEveryOutcome()
    {
        using namespace execution;
        ManualNodeObservations env;
        auto core = env.executor().makeQueue(Lane::Core);
        auto other = env.executor().makeQueue(Lane::Core);
        auto priority = env.executor().makeQueue(Lane::Priority);
        auto outcomes = env.executor().makeQueue(Lane::Utility);
        ChangeCounter original, revoked;
        int effects{0}, completed{0}, cancelled{0};
        const auto finish = qScopeGuard([&] { env.drain(); });
        const auto submit = [&](SerialQueue& queue, const ChangeCounter& source) {
            return postTask(queue, outcomes)(MakeTask([&, revision = source.value.load()] {
                if (!source.current(revision)) throw NotStarted{NotStarted::Cause::Cancelled};
                ++effects;
            }, [&] { ++completed; }, [&](auto error) {
                QCOMPARE(NotStartedCause(error), std::make_optional(NotStarted::Cause::Cancelled));
                ++cancelled;
            }));
        };
        QVERIFY(submit(core, original));
        QVERIFY(submit(core, revoked));
        QVERIFY(submit(other, revoked));
        QVERIFY(submit(priority, revoked));
        revoked.advance();
        env.drain();
        QCOMPARE(effects, 1);
        QCOMPARE(completed, 1);
        QCOMPARE(cancelled, 3);
        QVERIFY(submit(other, revoked));
        env.drain();
        QCOMPARE(effects, 2);
        QCOMPARE(completed, 2);
        QCOMPARE(cancelled, 3);
        QVERIFY(env.turnsIdle());
    }
    void cancelledPacketRetiresItsCaptureAndKeepsItsFailure()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto values = cleanupQueue(env.executor());
        int effects{0}, failures{0}, retired{0};
        auto held = values->hold([&] { ++retired; });
        QVERIFY(held);
        const auto finish = qScopeGuard([&] { env.drain(); });
        auto task = MakeTask([&, held = std::move(held)] { ++effects; }, [] {}, [&](auto error) {
            try { std::rethrow_exception(error); }
            catch (const NotStarted& stopped) {
                QCOMPARE(stopped.cause, NotStarted::Cause::Cancelled);
                QCOMPARE(QString::fromLatin1(stopped.what()), QStringLiteral("Task did not start"));
                ++failures;
            }
        });
        task->notStarted(NotStarted::Cause::Cancelled);
        QVERIFY(env.outcomes.post([task = std::move(task)] { task->deliver(); }));
        env.drain();
        QCOMPARE(effects, 0);
        QCOMPARE(failures, 1);
        QCOMPARE(retired, 1);
        QVERIFY(env.quiescent());
    }
    void cancellingOneWaitingReadPreservesItsSuccessor()
    {
        using namespace execution;
        ManualStartupExecution env;
        const auto post = postTask(env.core, env.outcomes);
        std::vector<int> effects, outcomes;
        const auto finish = qScopeGuard([&] { env.drain(); });
        ChangeCounter source;
        const auto submit = [&](int value, bool check_revision) {
            return post(MakeTask([&, value, check_revision, revision = source.value.load()] {
                if (check_revision && !source.current(revision)) throw NotStarted{NotStarted::Cause::Cancelled};
                effects.push_back(value);
                return value;
            }, [&](int result) { outcomes.push_back(result); }, [&, value](auto error) {
                QCOMPARE(NotStartedCause(error), std::make_optional(NotStarted::Cause::Cancelled));
                outcomes.push_back(-value);
            }));
        };
        QVERIFY(submit(1, false));
        QVERIFY(submit(2, true));
        source.advance();
        QVERIFY(submit(3, true));
        env.drain();
        QCOMPARE(effects, std::vector<int>({1, 3}));
        QCOMPARE(outcomes, std::vector<int>({1, -2, 3}));
        QVERIFY(env.quiescent());
    }
    void reservedCleanupWaitsForTheOutcomeAndTheBorrow()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto values = cleanupQueue(env.executor());
        auto delivery = cleanupQueue(env.executor());
        int effects{0}, outcomes{0}, retired{0}, drained{0};
        auto original = values->hold([&] { ++retired; });
        QVERIFY(original);
        auto borrowed = original;
        const auto finish = qScopeGuard([&] { borrowed.reset(); original.reset(); env.drain(); });
        QVERIFY(delivery->postWithOutcome(env.core, MakeTask([&] { ++effects; },
            [&, original = std::move(original)] { ++outcomes; }, {})));
        values->close([&] { ++drained; });
        env.core.close();
        QVERIFY(!values->hold([] {}));
        QVERIFY(!env.core.post([] {}));
        QVERIFY(!env.hasWork(Lane::Utility));
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(effects, 1);
        QCOMPARE(outcomes, 0);
        borrowed.reset();
        QCOMPARE(retired, 0);
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(outcomes, 1);
        QCOMPARE(retired, 0);
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(retired, 1);
        QCOMPARE(drained, 0);
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(drained, 1);
        QVERIFY(env.quiescent());
    }
    void parentClosureActivatesCleanupWithoutAnotherAdmission()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto values = cleanupQueue(env.executor());
        int retired{0};
        auto borrowed = values->hold([&] { ++retired; });
        QVERIFY(borrowed);
        env.executor().stopAndJoin();
        QVERIFY(!values->hold([] {}));
        QVERIFY(!env.step(Lane::Utility));
        borrowed.reset();
        env.drain();
        QCOMPARE(retired, 1);
        auto successor = cleanupQueue(env.executor());
        QVERIFY(!successor->hold([&] { ++retired; }));
        QVERIFY(env.quiescent());
    }
    void subscriptionDecisionsDoNotResurrectAfterRetirement()
    {
        using namespace walletlife;
        State state;
        QCOMPARE(state.phase(), Phase::Registering);
        QVERIFY(!state.apply(Event::Notification));
        QVERIFY(!state.apply(Event::CleanupFinished));
        QCOMPARE(state.apply(Event::Registered), std::make_optional(Publication::Ready));
        QCOMPARE(state.phase(), Phase::Active);
        QVERIFY(!state.apply(Event::Registered));
        QCOMPARE(state.apply(Event::Notification), std::make_optional(Publication::Changed));
        QCOMPARE(state.apply(Event::RegistrationFailed), std::make_optional(Publication::Failed));
        QCOMPARE(state.phase(), Phase::Failed);
        QVERIFY(!state.apply(Event::Notification));
        QVERIFY(!state.apply(Event::RegistrationFailed));
        state.apply(Event::Close);
        QCOMPARE(state.phase(), Phase::Closing);
        QVERIFY(!state.apply(Event::Registered));
        QVERIFY(!state.apply(Event::RegistrationFailed));
        QCOMPARE(state.apply(Event::CleanupFinished), std::make_optional(Publication::Retired));
        QCOMPARE(state.phase(), Phase::Closed);
        for (auto event : {Event::Registered, Event::RegistrationFailed, Event::Notification, Event::Close, Event::CleanupFinished, static_cast<Event>(255)}) {
            QVERIFY(!state.apply(event));
            QCOMPARE(state.phase(), Phase::Closed);
        }
        State closed_during_registration;
        closed_during_registration.apply(Event::Close);
        QVERIFY(!closed_during_registration.apply(Event::Registered));
        QVERIFY(!closed_during_registration.apply(Event::Close));
        QCOMPARE(closed_during_registration.apply(Event::CleanupFinished), std::make_optional(Publication::Retired));
        State failed_registration;
        QCOMPARE(failed_registration.apply(Event::RegistrationFailed), std::make_optional(Publication::Failed));
        QCOMPARE(failed_registration.phase(), Phase::Failed);
        State closed_active;
        closed_active.apply(Event::Registered); closed_active.apply(Event::Close);
        QCOMPARE(closed_active.phase(), Phase::Closing);
    }
    void observationsAndActionsBothProgress()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto observations = env.executor().makeQueue(Lane::Core);
        std::vector<int> effects;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        const auto submit = [&](SerialQueue& queue, int value) {
            return queue.post([&, value] { effects.push_back(value); });
        };
        QVERIFY(submit(env.core, 1));
        QVERIFY(submit(observations, 9));
        QVERIFY(submit(env.core, 2));
        QVERIFY(submit(env.core, 3));
        QVERIFY(submit(env.core, 4));
        env.drain();
        QCOMPARE(effects, std::vector<int>({1, 9, 2, 3, 4}));
        env.reject_core = true;
        QVERIFY(!submit(observations, 11));
        QVERIFY(submit(observations, 12));
        env.drain();
        QCOMPARE(effects, std::vector<int>({1, 9, 2, 3, 4, 12}));
        QVERIFY(env.quiescent());
    }
    void coreQueuesAreFairAndOutcomeConsumptionIsSeparate()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto second = env.executor().makeQueue(Lane::Core);
        auto third = env.executor().makeQueue(Lane::Core);
        std::vector<int> effects, outcomes;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        const auto submit = [&](SerialQueue& queue, int value) {
            return postTask(queue, env.outcomes)(MakeTask([&, value] {
                effects.push_back(value);
                return value * 10;
            }, [&](int result) { outcomes.push_back(result); }, {}));
        };
        QVERIFY(submit(env.core, 1));
        QVERIFY(submit(env.core, 2));
        QVERIFY(submit(second, 3));
        QVERIFY(submit(third, 4));
        QVERIFY(effects.empty());
        while (env.step(Lane::Core)) {}
        QCOMPARE(effects, std::vector<int>({1, 3, 4, 2}));
        QVERIFY(outcomes.empty());
        auto first = env.take(Lane::Utility);
        QVERIFY(first);
        QVERIFY(outcomes.empty());
        first();
        QCOMPARE(outcomes, std::vector<int>({10}));
        env.drain();
        QCOMPARE(outcomes, std::vector<int>({10, 30, 40, 20}));
        QVERIFY(env.quiescent());
    }
    void rpcCanFinishWhileCoreIsHeldAndShutdownWorkRemainsAvailable()
    {
        using namespace execution;
        ManualNodeObservations env;
        auto core = env.executor().makeQueue(Lane::Core);
        auto rpc = env.executor().makeQueue(Lane::Rpc);
        auto control = env.executor().makeQueue(Lane::Priority);
        auto utility = env.executor().makeQueue(Lane::Utility);
        std::vector<int> effects, outcomes;
        int interrupted{0}, retired{0};
        const auto drain = qScopeGuard([&] { while (env.step(Lane::Rpc)) {} env.drain(); });
        const auto submit = [&](SerialQueue& queue, int value) {
            return postTask(queue, utility)(MakeTask([&, value] {
                effects.push_back(value);
                return value;
            }, [&](int result) { outcomes.push_back(result); }, {}));
        };
        QVERIFY(submit(core, 1));
        QVERIFY(submit(core, 2));
        QVERIFY(submit(rpc, 3));
        auto held = env.take(Lane::Core);
        QVERIFY(held);
        QVERIFY(!env.hasWork(Lane::Core));
        core.close();
        rpc.close();
        QVERIFY(!submit(core, 4));
        QVERIFY(control.post([&] { ++interrupted; }));
        QVERIFY(utility.post([&] { ++retired; }));
        QVERIFY(env.step(Lane::Priority));
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(interrupted, 1);
        QCOMPARE(retired, 1);
        QVERIFY(effects.empty());
        QVERIFY(env.step(Lane::Rpc));
        QVERIFY(!env.hasWork(Lane::Core));
        held();
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(effects, std::vector<int>({3, 1, 2}));
        QVERIFY(outcomes.empty());
        env.drain();
        QCOMPARE(outcomes, effects);
        QVERIFY(env.turnsIdle());
    }
    void cancelledObservationAndRejectedReadHaveDistinctOutcomes()
    {
        using namespace execution;
        ManualStartupExecution env;
        auto second = env.executor().makeQueue(Lane::Core);
        int reads{0}, cancelled_notices{0}, failed_notices{0};
        auto cancelled = std::make_shared<SnapshotQuery<int>>([&] { return ++reads; },
            [&] { ++cancelled_notices; }, postTask(env.core, env.outcomes));
        auto refused = std::make_shared<SnapshotQuery<int>>([&] { return ++reads; },
            [&] { ++failed_notices; }, postTask(second, env.outcomes));
        const auto finish = qScopeGuard([&] { env.drain(); });
        cancelled->request();
        cancelled->close();
        env.reject_core = true;
        refused->request();
        QCOMPARE(failed_notices, 1);
        auto result = refused->take();
        QVERIFY(result && !result->snapshot);
        QCOMPARE(NotStartedCause(result->error), std::make_optional(NotStarted::Cause::Transport));
        env.drain();
        QCOMPARE(reads, 0);
        QCOMPARE(cancelled_notices, 0);
        QVERIFY(!cancelled->take());
        QVERIFY(!refused->take());
        refused->request();
        env.drain();
        QCOMPARE(reads, 1);
        QCOMPARE(failed_notices, 2);
        result = refused->take();
        QVERIFY(result && result->snapshot && !result->error);
        QVERIFY(env.quiescent());
    }
    void transportRejectionAndInvalidWorkAdmitNothing()
    {
        using namespace execution;
        ManualStartupExecution env;
        int effects{0}, outcomes{0};
        const auto finish = qScopeGuard([&] { env.drain(); });
        const auto submit = [&] {
            return postTask(env.core, env.outcomes)(MakeTask([&] { ++effects; }, [&] { ++outcomes; }, {}));
        };
        env.reject_core = true;
        QCOMPARE(submit().rejection, Rejection::Transport);
        QVERIFY(env.quiescent());
        QVERIFY(submit());
        QVERIFY(submit());
        env.drain();
        QCOMPARE(effects, 2);
        QCOMPARE(outcomes, 2);
        QVERIFY(!env.core.post({}));
        QVERIFY(env.quiescent());
        Executor executor{Executor::Config{}};
        auto invalid = executor.makeQueue(static_cast<Lane>(255));
        QVERIFY(!invalid.post([&] { ++effects; }));
        executor.stopAndJoin();
        QCOMPARE(effects, 2);
    }
    void executionOutcomeAndObservationAreSeparate()
    {
        using namespace execution;
        ManualStartupExecution env;
        int effect{0}, domain{0}, observed{0};
        std::function<void()> publication;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        auto task = MakeTask([&] { effect = 7; return 9; }, [&](int result) {
            domain = result;
            publication = [&] { observed = domain; };
        }, {});
        const std::weak_ptr<Task> pending = task;
        QVERIFY(postTask(env.core, env.outcomes)(std::move(task)));
        QCOMPARE(effect, 0);
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(effect, 7);
        QCOMPARE(domain, 0);
        QVERIFY(!pending.expired());
        env.core.close();
        QVERIFY(!env.core.post([] {}));
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(domain, 9);
        QCOMPARE(observed, 0);
        QVERIFY(pending.expired());
        publication();
        QCOMPARE(observed, 9);
        QVERIFY(env.quiescent());
    }
    void rejectionExceptionAndFifo()
    {
        using namespace execution;
        ManualStartupExecution env;
        const auto submit = postTask(env.core, env.outcomes);
        int effects{0}, results{0}, failures{0};
        std::vector<int> order;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        env.reject_core = true;
        const auto rejected = submit(MakeTask([&] { ++effects; }, [&] { ++results; }, {}));
        QCOMPARE(rejected.rejection, Rejection::Transport);
        QVERIFY(env.quiescent());
        QVERIFY(submit(MakeTask([&]() -> int { ++effects; order.push_back(1); throw std::runtime_error("synthetic"); },
            [&](int) { ++results; }, [&](std::exception_ptr error) {
                QVERIFY(error);
                ++failures;
                order.push_back(3);
            })));
        QVERIFY(submit(MakeTask([&] { ++effects; order.push_back(2); }, [&] { ++results; order.push_back(4); }, {})));
        env.drain();
        QCOMPARE(effects, 2);
        QCOMPARE(results, 1);
        QCOMPARE(failures, 1);
        QCOMPARE(order, std::vector<int>({1, 2, 3, 4}));
        QVERIFY(env.quiescent());
        QVERIFY(submit(MakeTask([] { throw 1; }, [] {}, {})));
        env.drain();
        QVERIFY(env.quiescent());
    }
    void completionCanSubmitAnotherOwnedCommand()
    {
        using namespace execution;
        ManualStartupExecution env;
        const auto submit = postTask(env.core, env.outcomes);
        int effects{0}, outcomes{0};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        QVERIFY(submit(MakeTask([&] { ++effects; }, [&] {
            ++outcomes;
            QVERIFY(submit(MakeTask([&] { ++effects; }, [&] { ++outcomes; }, {})));
        }, {})));
        env.drain();
        QCOMPARE(effects, 2);
        QCOMPARE(outcomes, 2);
        QVERIFY(env.quiescent());
    }
    void ownedResultsAreReleasedAfterAccounting()
    {
        using namespace execution;
        ManualStartupExecution env;
        int released{0};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        struct Value { int& count; ~Value() { ++count; } };
        QVERIFY(postTask(env.core, env.outcomes)(MakeTask([&] { return std::make_unique<Value>(released); },
            [](std::unique_ptr<Value>) {}, {})));
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(released, 0);
        QVERIFY(env.step(Lane::Utility));
        QCOMPARE(released, 1);
        QVERIFY(env.quiescent());
    }
    void feeDecisionGuardsAndTerminalClose()
    {
        FeeReadState state;
        using K = FeeReadState::Kind;
        using P = FeeReadState::Phase;
        QCOMPARE(state.phase(), P::Idle);
        QVERIFY(!state.apply({K::Complete, {1}}));
        QVERIFY(state.apply({K::Invalidate, {1}}));
        QVERIFY(!state.apply({K::Invalidate, {1}}));
        QVERIFY(!state.apply({K::Dispatch, {1}}));
        QVERIFY(state.apply({K::Dispatch, {2}}));
        QCOMPARE(state.phase(), P::Reading);
        QVERIFY(!state.apply({K::Complete, {1}}));
        QVERIFY(state.apply({K::Invalidate, {3}}));
        QCOMPARE(state.phase(), P::Idle);
        QVERIFY(!state.apply({K::Complete, {2}}));
        QVERIFY(state.apply({K::Dispatch, {4}}));
        QVERIFY(state.apply({K::Complete, {4}}));
        QVERIFY(!state.apply({K::Complete, {4}}));
        QVERIFY(!state.apply({static_cast<K>(255), {5}}));
        QVERIFY(state.apply({K::Close, {}}));
        QCOMPARE(state.phase(), P::Closed);
        for (auto kind : {K::Invalidate, K::Dispatch, K::Complete, K::Close}) QVERIFY(!state.apply({kind, {6}}));
    }
};

BITCOINQML_REGISTER_QT_TEST(ExecutionTests)
#include <test_execution.moc>
