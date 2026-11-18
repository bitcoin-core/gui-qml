// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/executor.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/retainedvalue.h>
#include <qml/snapshotquery.h>
#include <qml/walletopening.h>
#include <test/mocks/walletmodelservices.h>
#include <test/mocks/walletsession.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/mocks/manualwalletdrain.h>
#include <test/mocks/feepreviewchain.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest/QtTest>

#include <stdexcept>
#include <algorithm>
#include <limits>
#include <array>
#include <barrier>
#include <deque>
#include <thread>

namespace {
using execution::Lane;
using walletopening::Phase;
struct Wallet final : StubWallet {
    std::string getWalletName() override { ++reads; if (fail_read) throw std::runtime_error("synthetic metadata failure"); return name; }
    bool privateKeysDisabled() override { return no_keys; }
    bool hasExternalSigner() override { return external; }
    bool isCrypted() override { return true; }
    bool isLocked() override { return locked; }
    bool canGetAddresses() override { return addresses; }
    bool taprootEnabled() override { return taproot; }
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn fn) override { address_capability = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn fn) override { status = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn fn) override { unload = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn) override { transaction = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleAddressBookChanged(AddressBookChangedFn fn) override { address = std::move(fn); return {}; }
    std::string name{"original"};
    int reads{0};
    bool fail_read{false};
    bool no_keys{false}, external{false}, locked{true}, addresses{true}, taproot{true};
    CanGetAddressesChangedFn address_capability;
    StatusChangedFn status;
    UnloadFn unload;
    TransactionChangedFn transaction;
    AddressBookChangedFn address;
};
struct Fixture {
    ManualStartupExecution commands, retirement;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    int wakes{0};
    std::shared_ptr<walletopening::Session> session{std::make_shared<walletopening::Session>(
        std::make_shared<std::atomic_bool>(false), [this] { ++wakes; },
        walletopening::Session::Preferences{}, std::chrono::steady_clock::now, nullptr,
        walletopening::NativeEvents{}, WalletSessionQueues(commands.executor(), retirement.executor()))};
    ~Fixture()
    {
        if (session) session->stop(false, {});
        commands.drain();
        retirement.drain();
    }
    void start()
    {
        QVERIFY(session->start(wallet));
        commands.drain();
        QVERIFY(session->take()->metadata.has_value());
    }
    walletopening::Question question()
    {
        session->requestClose();
        commands.drain();
        const auto update = session->take();
        if (!update || !update->question) { QTest::qFail("Missing close question", __FILE__, __LINE__); return {}; }
        return *update->question;
    }
};
}

class WalletOpeningTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void stopAcknowledgesNativeReleaseAndReplaysForLateListeners()
    {
        Fixture fixture;
        fixture.start();
        const std::weak_ptr<Wallet> wallet = fixture.wallet;
        fixture.wallet.reset();
        int callbacks{0};
        const auto done = [&] {
            QVERIFY(wallet.expired());
            ++callbacks;
        };
        fixture.session->stop(false, done);
        fixture.session->stop(false, done);
        QCOMPARE(callbacks, 0);
        QVERIFY(!wallet.expired());
        fixture.commands.drain();
        QCOMPARE(callbacks, 0);

        fixture.retirement.drain();
        QCOMPARE(callbacks, 2);
        QVERIFY(wallet.expired());
        fixture.session->stop(false, done);
        QCOMPARE(callbacks, 3);
    }

    void closedCleanupQueueLeavesNativeOwnershipWithCaller()
    {
        ManualStartupExecution worker;
        auto opening = MakeWalletSession(worker.executor(), [] {});
        opening->cleanupQueue()->close();
        std::unique_ptr<interfaces::Wallet> wallet = std::make_unique<Wallet>();
        auto* original = wallet.get();
        auto handler = interfaces::MakeCleanupHandler([] {});
        auto* original_handler = handler.get();
        const auto start = opening->start(wallet, handler);
        QCOMPARE(start.submitted.rejection, execution::Rejection::Closed);
        QCOMPARE(wallet.get(), original);
        QCOMPARE(handler.get(), original_handler);
        QVERIFY(!opening->holdValues([] {}));
        QVERIFY(worker.quiescent());
    }

    void rejectedValueReservationReleasesItsAction()
    {
        Fixture fixture;
        fixture.start();
        auto action = fixture.session->startAction();
        QVERIFY(action);
        fixture.session->cleanupQueue()->close();
        bool ran{false};
        QVERIFY(!fixture.session->holdValues([action = std::move(action), &ran] { ran = true; }));
        bool drained{false};
        fixture.session->stop(false, [&] { drained = true; });
        fixture.commands.drain();
        fixture.retirement.drain();
        QVERIFY(drained);
        QVERIFY(!ran);
    }

    void closedOutcomeQueueRejectsCoreWorkBeforeExecution()
    {
        Fixture fixture;
        fixture.start();
        fixture.session->utilityQueue()->close();
        bool executed{false}, delivered{false};
        const auto read = fixture.session->read([&](interfaces::Wallet&) { executed = true; return 1; },
            [&](int) { delivered = true; });
        QCOMPARE(read.rejection, execution::Rejection::Closed);
        QVERIFY(fixture.commands.quiescent());
        fixture.commands.drain();
        QVERIFY(!executed);
        QVERIFY(!delivered);
    }

    void corePacketRetiresOnUtilityWhenDeliveryFinishesFirst()
    {
        struct Packet final : execution::Task {
            std::thread::id& delivered;
            std::thread::id& destroyed;
            Packet(std::thread::id& delivered, std::thread::id& destroyed) : delivered(delivered), destroyed(destroyed) {}
            void execute() override {}
            void notStarted(execution::NotStarted::Cause) override { QFAIL("Accepted packet was cancelled"); }
            void deliver() override { delivered = std::this_thread::get_id(); }
            ~Packet() override { destroyed = std::this_thread::get_id(); }
        };
        std::function<void()> core;
        execution::Executor executor{[&](Lane lane, execution::UtilityWorker, std::function<void()> turn) {
            if (lane == Lane::Core) core = std::move(turn);
            else std::thread(std::move(turn)).join();
            return true;
        }};
        auto opening = MakeWalletSession(executor, [] {});
        std::thread::id delivered, destroyed;
        const auto post = opening->observationPost(Lane::Core);
        QVERIFY(post);
        QVERIFY(post(std::make_shared<Packet>(delivered, destroyed)));
        QVERIFY(core);
        core();
        QVERIFY(delivered != std::thread::id{});
        QVERIFY(delivered != std::this_thread::get_id());
        QVERIFY(destroyed == delivered);
    }

    void executorShutdownRetiresWalletAndValuesAfterAcceptedCoreOutcome()
    {
        execution::Executor executor{execution::Executor::Config{}};
        QSemaphore ready, release_core;
        auto opening = MakeWalletSession(executor, [&] { ready.release(); });
        struct NativeWallet final : StubWallet {
            explicit NativeWallet(std::thread::id& destroyed) : destroyed(destroyed) {}
            ~NativeWallet() override { destroyed = std::this_thread::get_id(); }
            std::thread::id& destroyed;
        };
        struct Handler final : interfaces::Handler {
            Handler(std::thread::id& disconnected, std::thread::id& destroyed) : disconnected(disconnected), destroyed(destroyed) {}
            ~Handler() override { destroyed = std::this_thread::get_id(); }
            void disconnect() override { disconnected = std::this_thread::get_id(); }
            std::thread::id& disconnected;
            std::thread::id& destroyed;
        };
        std::thread::id destroyed, disconnected, handler_destroyed;
        std::unique_ptr<interfaces::Wallet> wallet = std::make_unique<NativeWallet>(destroyed);
        std::unique_ptr<interfaces::Handler> handler = std::make_unique<Handler>(disconnected, handler_destroyed);
        std::thread::id utility, retired_value, delivered, stopped;
        int result{0};
        const auto cleanup = qScopeGuard([&] {
            release_core.release();
            opening->stop(false, {});
            executor.stopAndJoin();
        });
        QVERIFY(opening->start(wallet, handler));
        QVERIFY(ready.tryAcquire(1, 5000));
        const auto update = opening->take();
        QVERIFY(update && update->metadata);
        auto held = opening->holdValues([&] { retired_value = std::this_thread::get_id(); });
        QVERIFY(held);
        QVERIFY(opening->perform(opening->startAction(), [&, held = std::move(held)](interfaces::Wallet&) {
            return release_core.tryAcquire(1, 10000) ? 42 : 0;
        }, [&](int value) { result = value; delivered = std::this_thread::get_id(); }));
        auto last = executor.makeQueue(Lane::Utility);
        last.whenDrained([&] {
            utility = std::this_thread::get_id();
            release_core.release();
        });
        opening->stop(false, [&] { stopped = std::this_thread::get_id(); });
        executor.stopAndJoin();
        QCOMPARE(result, 42);
        QVERIFY(utility != std::thread::id{});
        QVERIFY(utility != std::this_thread::get_id());
        QVERIFY(delivered == utility);
        QVERIFY(retired_value == utility);
        QVERIFY(stopped == utility);
        QVERIFY(destroyed == utility);
        QVERIFY(disconnected == utility);
        QVERIFY(handler_destroyed == utility);
    }

    void idleOpeningStopsWhileAnotherOpeningRunsOnCore()
    {
        struct TrackedWallet final : StubWallet {
            explicit TrackedWallet(std::shared_ptr<std::atomic<QThread*>> destroyed) : destroyed(std::move(destroyed)) {}
            ~TrackedWallet() override { destroyed->store(QThread::currentThread()); }
            const std::shared_ptr<std::atomic<QThread*>> destroyed;
        };
        execution::Executor executor{execution::Executor::Config{}};
        const auto cleanup = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        const auto make_opening = [&]() {
            const auto ready = std::make_shared<QSemaphore>();
            const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
            const auto outcomes = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
            auto opening = std::make_shared<walletopening::Session>(
                std::make_shared<std::atomic_bool>(false), [ready] { ready->release(); },
                walletopening::Session::Preferences{}, std::chrono::steady_clock::now, nullptr,
                walletopening::NativeEvents{}, walletopening::Session::Queues{cleanup, [core, outcomes](auto done) {
                    core->close();
                    core->whenDrained([outcomes, done = std::move(done)]() mutable {
                        outcomes->close();
                        outcomes->whenDrained(std::move(done));
                    });
                }, core, outcomes});
            return std::pair{std::move(opening), ready};
        };
        const auto [idle, idle_ready] = make_opening();
        const auto [busy, busy_ready] = make_opening();
        const auto release = std::make_shared<QSemaphore>();
        const auto stop_on_exit = qScopeGuard([&] {
            release->release();
            const auto stopped = std::make_shared<std::atomic_int>(0);
            idle->stop(false, [stopped] { ++*stopped; });
            busy->stop(false, [stopped] { ++*stopped; });
            QVERIFY(QTest::qWaitFor([&] { return stopped->load() == 2; }, 5000));
            executor.stopAndJoin();
        });
        const auto destroyed = std::make_shared<std::atomic<QThread*>>(nullptr);
        QVERIFY(idle->start(std::make_shared<TrackedWallet>(destroyed)));
        QVERIFY(busy->start(std::make_shared<Wallet>()));
        QTRY_VERIFY(idle_ready->available() > 0 && busy_ready->available() > 0);
        const auto idle_update = idle->take();
        const auto busy_update = busy->take();
        QVERIFY(idle_update && idle_update->metadata);
        QVERIFY(busy_update && busy_update->metadata);

        const auto entered = std::make_shared<QSemaphore>();
        const auto finished = std::make_shared<std::atomic_bool>(false);
        QVERIFY(busy->perform(busy->startAction(), [entered, release](interfaces::Wallet&) {
            entered->release();
            release->acquire();
            return true;
        }, [finished](bool result) { finished->store(result); }));
        QTRY_VERIFY(entered->available() > 0);

        const auto stopped = std::make_shared<std::atomic_bool>(false);
        idle->stop(false, [stopped] { stopped->store(true); });
        QTRY_VERIFY(stopped->load());
        QVERIFY(destroyed->load());
        QVERIFY(destroyed->load() != QThread::currentThread());
        QVERIFY(!finished->load());
        QVERIFY(busy->available());

        release->release();
        QTRY_VERIFY(finished->load());
    }

    void directCleanupWaitsForUtility_data()
    {
        QTest::addColumn<bool>("reject_core");
        QTest::newRow("accepted-start") << false;
        QTest::newRow("rejected-core-start") << true;
    }

    void directCleanupWaitsForUtility()
    {
        QFETCH(bool, reject_core);
        struct NativeState {
            QSemaphore entered, release;
            std::atomic<QThread*> destroyed_on{nullptr};
            std::atomic_bool stopped{false};
        };
        struct NativeWallet final : StubWallet {
            explicit NativeWallet(std::shared_ptr<NativeState> state) : state(std::move(state)) {}
            ~NativeWallet() override { state->destroyed_on = QThread::currentThread(); }
            std::shared_ptr<NativeState> state;
        };

        execution::Executor executor{execution::Executor::Config{}};
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            walletopening::Session::Queues{utility, [core](auto done) {
                core->close();
                core->whenDrained(std::move(done));
            }, core, utility});
        const auto state = std::make_shared<NativeState>();
        const auto release_on_exit = qScopeGuard([session, state] {
            state->release.release();
            session->stop(false, [state] { state->stopped = true; });
            QVERIFY(QTest::qWaitFor([&] { return state->stopped.load(); }, 5000));
        });
        QVERIFY(utility->post([state] {
            state->entered.release();
            state->release.acquire();
        }));
        QTRY_VERIFY(state->entered.available() > 0);

        QVERIFY(session->utilityQueue());
        if (reject_core) core->close();
        std::unique_ptr<interfaces::Wallet> wallet = std::make_unique<NativeWallet>(state);
        std::unique_ptr<interfaces::Handler> unload;
        auto started = session->start(wallet, unload);
        QCOMPARE(bool(started), !reject_core);
        QVERIFY(started.borrower);
        QVERIFY(!wallet);
        QVERIFY(!state->destroyed_on.load());

        session->stop(false, [state] { state->stopped = true; });
        started.borrower.reset();
        QVERIFY(!state->stopped.load());
        QVERIFY(!state->destroyed_on.load());
        state->release.release();
        QTRY_VERIFY(state->stopped.load());
        QVERIFY(state->destroyed_on.load());
        QVERIFY(state->destroyed_on.load() != QThread::currentThread());
    }

    void unclaimedWalletDrainsWithItsOriginalHandler()
    {
        struct Trace {
            QSemaphore release;
            std::atomic_bool utility_entered{false}, drained{false};
            std::atomic<QThread*> disconnected_on{nullptr}, destroyed_on{nullptr};
        };
        struct NativeWallet final : StubWallet {
            explicit NativeWallet(std::shared_ptr<Trace> trace) : trace(std::move(trace)) {}
            ~NativeWallet() override { trace->destroyed_on = QThread::currentThread(); }
            const std::shared_ptr<Trace> trace;
        };
        struct Handler final : interfaces::Handler {
            explicit Handler(std::shared_ptr<Trace> trace) : trace(std::move(trace)) {}
            void disconnect() override { trace->disconnected_on = QThread::currentThread(); }
            const std::shared_ptr<Trace> trace;
        };

        execution::Executor executor{execution::Executor::Config{}};
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(true), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            walletopening::Session::Queues{utility, [core, utility](auto done) {
                core->close();
                core->whenDrained([utility, done = std::move(done)] {
                    utility->close();
                    utility->whenDrained(done);
                });
            }, core, utility});

        const auto trace = std::make_shared<Trace>();
        const auto release = qScopeGuard([trace] { trace->release.release(); });
        QVERIFY(utility->post([trace] {
            trace->utility_entered = true;
            trace->release.acquire();
        }));
        QTRY_VERIFY(trace->utility_entered.load());

        std::unique_ptr<interfaces::Wallet> original = std::make_unique<NativeWallet>(trace);
        walletopening::Source source{std::move(original)};
        std::unique_ptr<interfaces::Handler> handler = std::make_unique<Handler>(trace);
        const auto started = session->start(source, handler);
        QCOMPARE(started.submitted.rejection, execution::Rejection::Closed);
        QVERIFY(!started.borrower);
        QVERIFY(std::get<std::unique_ptr<interfaces::Wallet>>(source));
        QVERIFY(handler);

        session->retireUnclaimed(source, handler);
        QVERIFY(!std::get<std::unique_ptr<interfaces::Wallet>>(source));
        QVERIFY(!handler);
        session->stop(false, [trace] { trace->drained = true; });
        QVERIFY(!trace->drained.load());
        QVERIFY(!trace->destroyed_on.load());
        trace->release.release();
        QTRY_VERIFY(trace->drained.load());
        QVERIFY(trace->destroyed_on.load());
        QVERIFY(trace->destroyed_on.load() != QThread::currentThread());
        QVERIFY(trace->destroyed_on.load() == trace->disconnected_on.load());
    }

    void nativeCleanupDoesNotBlockUtilityIo()
    {
        struct Trace {
            QSemaphore remove_entered, remove_release;
            std::atomic<QThread*> remove_thread{nullptr}, io_thread{nullptr};
            std::atomic_bool io_ran{false}, drained{false};
        };
        struct BlockingRemoveWallet final : StubWallet {
            explicit BlockingRemoveWallet(std::shared_ptr<Trace> trace) : m_trace(std::move(trace)) {}
            std::string getWalletName() override { return "blocked-remove"; }
            void remove() override
            {
                m_trace->remove_thread = QThread::currentThread();
                m_trace->remove_entered.release();
                m_trace->remove_release.acquire();
            }
            std::shared_ptr<Trace> m_trace;
        };

        const auto trace = std::make_shared<Trace>();
        const auto unblock = qScopeGuard([trace] { trace->remove_release.release(); });
        execution::Executor executor{execution::Executor::Config{}};
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility, execution::UtilityWorker::IO));
        const auto native_cleanup = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility, execution::UtilityWorker::Cleanup));
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            walletopening::Session::Queues{utility, [core](auto done) {
                core->close();
                core->whenDrained(std::move(done));
            }, core, utility, native_cleanup});

        std::unique_ptr<interfaces::Wallet> original = std::make_unique<BlockingRemoveWallet>(trace);
        walletopening::Source source{std::move(original)};
        std::unique_ptr<interfaces::Handler> handler;
        auto started = session->start(source, handler);
        QCOMPARE(started.submitted.rejection, execution::Rejection::None);
        QVERIFY(started.borrower);
        started.borrower.reset();

        session->stop(true, [trace] { trace->drained = true; });
        QVERIFY(trace->remove_entered.tryAcquire(1, 5000));
        QVERIFY(!trace->drained.load());

        QVERIFY(utility->post([trace] {
            trace->io_thread = QThread::currentThread();
            trace->io_ran = true;
        }));
        QTRY_VERIFY(trace->io_ran.load());
        QVERIFY(trace->io_thread.load() != trace->remove_thread.load());
        QVERIFY(!trace->drained.load());

        trace->remove_release.release();
        QTRY_VERIFY(trace->drained.load());
    }

    void directActionSettlesBeforeCleanup_data()
    {
        QTest::addColumn<bool>("reject_core");
        QTest::newRow("accepted") << false;
        QTest::newRow("rejected") << true;
    }

    void directActionSettlesBeforeCleanup()
    {
        QFETCH(bool, reject_core);
        struct TrackedValue {
            explicit TrackedValue(bool& retired) : retired(retired) {}
            ~TrackedValue() { retired = true; }
            bool& retired;
        };
        struct NativeWallet final : StubWallet {
            explicit NativeWallet(bool& destroyed) : m_destroyed(destroyed) {}
            ~NativeWallet() override { m_destroyed = true; }
            std::string getWalletName() override { return "original"; }
            bool& m_destroyed;
        };

        std::deque<std::function<void()>> effects, outcomes;
        bool reject_next_core{false};
        execution::Executor executor{[&](Lane lane, execution::UtilityWorker, std::function<void()> task) {
            if (lane == Lane::Core && std::exchange(reject_next_core, false)) return false;
            (lane == Lane::Core ? effects : outcomes).push_back(std::move(task));
            return true;
        }};
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        const auto step = [](auto& queue) {
            if (queue.empty()) return false;
            auto task = std::move(queue.front());
            queue.pop_front();
            task();
            return true;
        };
        std::function<void()> work_drained;
        bool destroyed{false}, stopped{false};
        bool observation_ran{false}, observation_cancelled{false}, domain_observation_ran{false};
        bool follow_up_planned{false};
        bool values_retired{false};
        bool retained_value_retired{false};
        int reads{0};
        std::optional<int> domain_value;
        bool domain_failure{false};
        std::optional<std::string> result, follow_up;
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            walletopening::Session::Queues{utility, [&](auto done) {
                work_drained = std::move(done);
            }, core, utility});
        const auto retire = qScopeGuard([&] {
            session->stop(false, {});
            while (step(effects) || step(outcomes)) {}
            if (auto complete = std::exchange(work_drained, {})) complete();
            while (step(outcomes)) {}
        });
        std::unique_ptr<interfaces::Wallet> native = std::make_unique<NativeWallet>(destroyed);
        std::unique_ptr<interfaces::Handler> unload;
        QVERIFY(session->start(native, unload));
        QVERIFY(step(effects));
        QVERIFY(step(outcomes));
        const auto startup = session->take();
        QVERIFY(startup && startup->metadata);

        if (!reject_core) {
            int reads{0}, notices{0};
            const auto query = std::make_shared<execution::SnapshotQuery<int>>([&] { return ++reads; }, [&] { ++notices; },
                session->observationPost());
            query->request();
            query->request();
            QVERIFY(step(effects));
            QCOMPARE(reads, 1);
            QCOMPARE(notices, 0);
            QVERIFY(step(outcomes));
            QCOMPARE(notices, 1);
            const auto first = query->take();
            QVERIFY(first && first->snapshot);
            QCOMPARE(*first->snapshot, 1);
            QVERIFY(step(effects));
            QCOMPARE(reads, 2);
            query->close();
            QVERIFY(step(outcomes));
            QCOMPARE(notices, 1);
            QVERIFY(!query->take());
        }

        session->requestClose();
        QVERIFY(step(outcomes));
        const auto close_request = session->take();
        QVERIFY(close_request && close_request->question);

        auto action = session->startAction();
        QVERIFY(action);
        session->answerClose(*close_request->question, true);
        QVERIFY(step(outcomes));
        const auto changed = session->take();
        QVERIFY(changed && changed->question);
        QVERIFY(changed->question->number != close_request->question->number);
        QVERIFY(session->available());

        auto retained = !reject_core ? walletopening::RetainValues<TrackedValue>(session, [&] {
            return std::make_unique<TrackedValue>(retained_value_retired);
        }) : nullptr;
        QCOMPARE(bool(retained), !reject_core);

        reject_next_core = reject_core;
        const auto posted = session->perform(action, [&](interfaces::Wallet& wallet) {
            ++reads;
            return wallet.getWalletName();
        }, [&, action](std::string name) {
            result = std::move(name);
            QVERIFY(session->planAction(action, [&, action] {
                follow_up_planned = true;
                QVERIFY(session->perform(action, [&](interfaces::Wallet& wallet) {
                    ++reads;
                    return wallet.getWalletName();
                }, [&](std::string name) { follow_up = std::move(name); }));
            }));
        });
        QCOMPARE(posted.rejection, reject_core ? execution::Rejection::Transport : execution::Rejection::None);
        if (!reject_core) {
            QVERIFY(session->retireValues([&] { values_retired = true; }));
            QVERIFY(session->postDomain([&] { domain_observation_ran = true; }));
            QVERIFY(session->postDomain([] { return 42; }, [&](int value) { domain_value = value; }, {}));
            QVERIFY(session->postDomain([] { throw std::runtime_error{"value work failed"}; }, [] {},
                [&](std::exception_ptr error) { domain_failure = bool(error); }));
            QVERIFY(session->read([&](interfaces::Wallet&) {
                observation_ran = true;
                return true;
            }, [](bool) {}, [&](std::exception_ptr error) {
                observation_cancelled = execution::NotStartedCause(error) == execution::NotStarted::Cause::Cancelled;
            }));
        }
        session->stop(false, [&] { stopped = true; });
        QVERIFY(!session->startAction());
        QCOMPARE(session->postDomain([] {}, [] {}, {}).rejection, execution::Rejection::Closed);
        QVERIFY(!work_drained);
        action.reset();
        QVERIFY(!destroyed);
        QVERIFY(!stopped);

        if (!reject_core) {
            QVERIFY(step(effects));
            QCOMPARE(reads, 1);
            QVERIFY(!work_drained); // The pending outcome still owns the admitted action.
            QVERIFY(!result && !stopped && !destroyed);
            QVERIFY(step(effects)); // Cancel the queued observation before its native read.
            while (step(outcomes)) {}
            QCOMPARE(result, std::optional<std::string>{"original"});
            QVERIFY(!observation_ran && observation_cancelled);
            QVERIFY(!domain_observation_ran);
            QCOMPARE(domain_value, std::optional<int>{42});
            QVERIFY(domain_failure);
            QVERIFY(values_retired);
            QVERIFY(!destroyed);
            QVERIFY(follow_up_planned);
            QVERIFY(step(effects));
            QCOMPARE(reads, 2);
            QVERIFY(!work_drained);
            QVERIFY(!follow_up && !stopped && !destroyed);
            QVERIFY(step(outcomes));
            QCOMPARE(follow_up, result);
            QVERIFY(!work_drained);
            QVERIFY(!retained_value_retired);
            retained.reset();
            QVERIFY(!retained_value_retired);
            QVERIFY(step(outcomes));
            QVERIFY(retained_value_retired);
        } else {
            QCOMPARE(reads, 0);
            QVERIFY(!result);
        }
        QVERIFY(work_drained);
        std::exchange(work_drained, {})();
        QVERIFY(!stopped && !destroyed);
        while (!stopped) QVERIFY(step(outcomes));
        QVERIFY(destroyed);
    }

    void retainedValueSharesOwnershipWithItsOwnReferences()
    {
        struct Value : std::enable_shared_from_this<Value> {
            bool& destroyed;
            explicit Value(bool& destroyed) : destroyed(destroyed) {}
            ~Value() { destroyed = true; }
        };
        bool destroyed{false}, stopped{false};
        ManualStartupExecution worker;
        auto opening = MakeWalletSession(worker.executor(), [] {});
        auto value = walletopening::RetainValues<Value>(opening, [&] { return std::make_unique<Value>(destroyed); });
        QVERIFY(value);
        auto retained = value->weak_from_this().lock();
        QCOMPARE(retained.get(), value.get());
        value.reset();
        opening->stop(false, [&] { stopped = true; });
        worker.drain();
        QVERIFY(!stopped);
        QVERIFY(!destroyed);
        retained.reset();
        QVERIFY(!destroyed);
        worker.drain();
        QVERIFY(destroyed);
        QVERIFY(stopped);
    }

    void unavailableRetainedValuePortDoesNotConstruct()
    {
        int constructed{0};
        const auto factory = [&] { ++constructed; return std::make_unique<int>(42); };
        QVERIFY(!walletopening::RetainValues<int>(std::shared_ptr<walletopening::Session>{}, factory));
        QCOMPARE(constructed, 0);
    }
    void failedRefreshCannotAcknowledgeUnreconciledTransactions_data()
    {
        QTest::addColumn<bool>("later_transaction");
        QTest::newRow("failed-notice-needs-rescan") << false;
        QTest::newRow("new-notice-during-failed-refresh") << true;
    }
    void failedRefreshCannotAcknowledgeUnreconciledTransactions()
    {
        QFETCH(bool, later_transaction);
        Fixture fixture; fixture.start();
        fixture.wallet->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        if (later_transaction) fixture.wallet->transaction(Txid{}, CT_UPDATED);
        fixture.wallet->fail_read = true; fixture.commands.drain();
        fixture.session->acknowledgeTransactions();
        QVERIFY(fixture.session->notificationsPending());
        const auto failed = fixture.session->take(); QVERIFY(failed && failed->failed);
        fixture.session->retire();
        fixture.session->retryNotifications(); fixture.commands.drain();
        QVERIFY(fixture.commands.quiescent());
    }
    void callbackRacingShutdownCannotRefreshTheClosingOpening()
    {
        ManualStartupExecution commands, retirement;
        const auto gate = std::make_shared<std::atomic_bool>(false);
        const auto native = std::make_shared<Wallet>();
        const auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, gate, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native));
        commands.drain();
        session->take();
        native->status();
        QVERIFY(commands.hasWork(Lane::Core));
        gate->store(true);
        commands.drain();
        QVERIFY(!session->available());
        QCOMPARE(native->reads, 1);
        QVERIFY(!session->take());
        QVERIFY(commands.quiescent());
    }
    void transactionAcknowledgementWaitsForBothRefreshAndConsumer()
    {
        Fixture fixture; fixture.start();
        fixture.wallet->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        fixture.session->acknowledgeTransactions(); QVERIFY(fixture.session->notificationsPending());
        fixture.commands.drain();
        fixture.session->acknowledgeTransactions(); QVERIFY(fixture.session->notificationsPending());
        const auto update = fixture.session->take();
        QVERIFY(update && update->transactions.size() == 1); QVERIFY(!update->rescan);
        fixture.session->acknowledgeTransactions(); QVERIFY(!fixture.session->notificationsPending());
        fixture.commands.reject_utility = true;
        fixture.session->onTransactions([](auto, auto, bool) {});
        fixture.commands.drain();
        fixture.session->acknowledgeTransactions(); QVERIFY(fixture.session->notificationsPending());
        const auto rescan = fixture.session->take();
        QVERIFY(rescan && rescan->rescan && rescan->transactions.empty());
        fixture.session->acknowledgeTransactions(); QVERIFY(!fixture.session->notificationsPending());
    }
    void publicPreferencePortRepairsMalformedSettingsWithoutCopyingTheirPayload()
    {
        struct Settings final : FeePreviewChain {
            common::SettingsValue getRwSetting(const std::string&) override { return values; }
            bool updateRwSetting(const std::string&, const interfaces::SettingsUpdate& update) override
            {
                const auto action = update(values);
                return action && *action == interfaces::SettingsAction::WRITE;
            }
            common::SettingsValue values{std::string(65'537, 'x')};
        } settings;
        const auto port = walletopening::ChainPreferences(settings);
        QCOMPARE(port.read("original"), std::string{});
        QVERIFY(port.write("original", "legacy"));
        QVERIFY(settings.values.isObject()); QCOMPARE(settings.values.size(), size_t{1});
        QCOMPARE(port.read("original"), std::string{"legacy"});
    }
    void retirementBeforeStartRejectsEveryNativeAction()
    {
        Fixture fixture;
        fixture.session->retire();
        QVERIFY(!fixture.session->available()); QVERIFY(!fixture.session->borrow());
        QVERIFY(!fixture.session->startAction());
        QCOMPARE(fixture.session->read([](auto&) { return 1; }, [](int) {}).rejection, execution::Rejection::Closed);
        QVERIFY(fixture.commands.quiescent()); QVERIFY(fixture.retirement.quiescent());
    }
    void unavailableNativeHandleNeverExecutesRead()
    {
        Fixture fixture;
        int effects{0};
        QCOMPARE(fixture.session->read([&](auto&) { return ++effects; }, [](int) {}).rejection,
                 execution::Rejection::Closed);
        QCOMPARE(effects, 0);
        fixture.commands.reject_core = true;
        const auto started = fixture.session->start(fixture.wallet);
        QVERIFY(!started);
        QCOMPARE(fixture.wallet->reads, 0);
        QVERIFY(!fixture.wallet->status);
        QVERIFY(!fixture.session->available());
    }
    void rejectedCloseControlNeverGrantsConsent()
    {
        Fixture fixture; fixture.start();
        fixture.commands.reject_utility = true; fixture.session->requestClose(); fixture.commands.drain();
        const auto failed_request = fixture.session->take();
        QVERIFY(failed_request && failed_request->failed); QVERIFY(!failed_request->question);
        QVERIFY(fixture.session->available());
        const auto question = fixture.question();
        fixture.commands.reject_utility = true; fixture.session->answerClose(question, true); fixture.commands.drain();
        QVERIFY(fixture.session->take()->failed); QVERIFY(fixture.session->available());
        fixture.session->retire();
        fixture.session->answerClose(question, true); fixture.session->requestClose(); fixture.commands.drain();
        const auto closed = fixture.session->take();
        QVERIFY(closed); QCOMPARE(closed->phase, Phase::Closing); QVERIFY(!closed->question);
    }
    void shutdownGateCancelsQueuedObservationAndConsumesAnAbsentErrorObserver()
    {
        ManualStartupExecution commands, retirement;
        auto gate = std::make_shared<std::atomic_bool>(false);
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            [] { return std::chrono::steady_clock::now(); }, gate, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        int effects{0}, outcomes{0};
        QVERIFY(session->read([&](auto&) { return ++effects; }, [&](int) { ++outcomes; }));
        gate->store(true); commands.drain();
        QCOMPARE(effects, 0); QCOMPARE(outcomes, 0); QVERIFY(commands.quiescent());
        QVERIFY(!session->startAction());
    }
    void unsupportedOrUnconfirmedPreferenceNeverClaimsPersistence_data()
    {
        QTest::addColumn<bool>("addresses"); QTest::addColumn<bool>("write_ok");
        QTest::addColumn<bool>("taproot");
        QTest::newRow("no-address-capability") << false << true << true;
        QTest::newRow("write-failed") << true << false << true;
        QTest::newRow("write-succeeded-readback-differs") << true << true << true;
        QTest::newRow("unsupported-taproot") << true << true << false;
    }
    void unsupportedOrUnconfirmedPreferenceNeverClaimsPersistence()
    {
        QFETCH(bool, addresses); QFETCH(bool, write_ok); QFETCH(bool, taproot);
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<Wallet>(); native->addresses = addresses; native->taproot = taproot;
        int writes{0};
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [](const auto&) { return std::string{"bech32"}; },
                [&](const auto&, const auto&) { ++writes; return write_ok; }},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference(taproot ? "legacy" : "bech32m")); commands.drain();
        const auto result = session->take();
        QVERIFY(result && result->metadata && result->preference_saved);
        QVERIFY(!*result->preference_saved);
        QCOMPARE(result->metadata->receive_preference, std::string{"bech32"});
        QCOMPARE(result->metadata->can_get_addresses, addresses);
        // R30 separates temporary aggregate keypool/unlock availability from
        // per-type support. Unconfirmed readback never claims success; a known
        // unsupported Taproot request still must not call the write port.
        QCOMPARE(result->preference_revision, uint64_t{1}); QCOMPARE(writes, taproot ? 1 : 0);
    }
    void temporaryAddressUnavailabilityStillRequiresExactPreferenceReadback()
    {
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<Wallet>(); native->addresses = false;
        std::string saved{"bech32"}; int writes{0};
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [&](const auto&) { return saved; },
                [&](const auto&, const auto& type) { ++writes; saved = type; return true; }},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference("legacy")); commands.drain();
        const auto result = session->take(); QVERIFY(result && result->metadata && result->preference_saved);
        QVERIFY(*result->preference_saved); QCOMPARE(writes, 1); QCOMPARE(saved, std::string{"legacy"});
        QVERIFY(!result->metadata->can_get_addresses);
        QCOMPARE(result->metadata->receive_preference, std::string{"legacy"});
    }
    void failedStatusRefreshDoesNotInventTransactionWork()
    {
        Fixture fixture; fixture.start();
        fixture.wallet->fail_read = true; fixture.wallet->status(); fixture.commands.drain();
        const auto failed = fixture.session->take();
        QVERIFY(failed && failed->failed); QVERIFY(!failed->rescan);
        QVERIFY(!fixture.session->notificationsPending());
    }
    void concurrentNativeCallbacksReceiveUniqueOrderedStamps()
    {
        walletopening::NativeEventClock clock;
        constexpr size_t workers{4}, per_worker{4096};
        std::array<std::vector<uint64_t>, workers> results;
        std::barrier start{static_cast<std::ptrdiff_t>(workers)};
        std::vector<std::thread> threads;
        threads.reserve(workers);
        for (size_t worker{0}; worker < workers; ++worker) threads.emplace_back([&, worker] {
            results[worker].reserve(per_worker);
            start.arrive_and_wait();
            for (size_t i{0}; i < per_worker; ++i) results[worker].push_back(clock.next());
        });
        for (auto& thread : threads) thread.join();
        std::vector<uint64_t> ordered;
        ordered.reserve(workers * per_worker);
        for (const auto& result : results) {
            QVERIFY(std::is_sorted(result.begin(), result.end()));
            ordered.insert(ordered.end(), result.begin(), result.end());
        }
        std::sort(ordered.begin(), ordered.end());
        QCOMPARE(clock.value.load(), uint64_t{workers * per_worker});
        for (size_t i{0}; i < ordered.size(); ++i) QCOMPARE(ordered[i], uint64_t{i + 1});
    }
    void closeQuestionIdentityAndPhaseGuardsPreventConsentReuse()
    {
        auto opening = std::make_shared<const walletopening::Identity>();
        auto other = std::make_shared<const walletopening::Identity>();
        walletopening::State state{opening};
        using walletopening::Answer;
        QCOMPARE(state.answer({opening, 1, 0}, 0, true), Answer::Ignored);
        QVERIFY(state.requestClose(0)); const auto first = *state.question();
        QCOMPARE(state.answer({other, first.number, 0}, 0, true), Answer::Ignored);
        QCOMPARE(state.answer({opening, first.number + 1, 0}, 0, true), Answer::Ignored);
        QCOMPARE(state.answer(first, 1, true), Answer::Changed);
        const auto revised = *state.question();
        QVERIFY(revised.number != first.number); QCOMPARE(revised.consequences, uint64_t{1});
        QCOMPARE(state.answer(first, 1, false), Answer::Ignored);
        QCOMPARE(state.answer(revised, 1, false), Answer::Dismissed);
        QVERIFY(!state.question()); QCOMPARE(state.phase(), Phase::Open);
        QVERIFY(state.requestClose(1)); const auto accepted = *state.question();
        QCOMPARE(state.answer(accepted, 1, true), Answer::Close);
        QCOMPARE(state.phase(), Phase::Closing); QVERIFY(!state.question());
        QVERIFY(!state.requestClose(2));
        QCOMPARE(state.answer(accepted, 1, true), Answer::Ignored);
        QCOMPARE(state.answer(accepted, 1, false), Answer::Ignored);
    }
    void failingNativeCleanupStillClosesAndDrains_data()
    {
        QTest::addColumn<bool>("disconnect_fails"); QTest::addColumn<bool>("remove_fails");
        QTest::newRow("success") << false << false;
        QTest::newRow("disconnect-failed") << true << false;
        QTest::newRow("remove-failed") << false << true;
        QTest::newRow("both-failed") << true << true;
    }
    void failingNativeCleanupStillClosesAndDrains()
    {
        QFETCH(bool, disconnect_fails); QFETCH(bool, remove_fails);
        struct Native final : StubWallet {
            struct Handler final : interfaces::Handler {
                Native& owner;
                explicit Handler(Native& owner) : owner(owner) {}
                void disconnect() override {
                    ++owner.disconnects;
                    if (owner.disconnect_fails) throw std::runtime_error{"Disconnect failed"};
                }
            };
            std::string getWalletName() override { return "original"; }
            std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn) override { return std::make_unique<Handler>(*this); }
            void remove() override { ++removes; if (remove_fails) throw std::runtime_error{"Remove failed"}; }
            int disconnects{0}, removes{0}; bool disconnect_fails{false}, remove_fails{false};
        };
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<Native>();
        native->disconnect_fails = disconnect_fails; native->remove_fails = remove_fails;
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        auto started = session->start(native); QVERIFY(started); started.borrower.reset();
        commands.drain(); session->take();
        session->retire(true); session->stop(false, {});
        QCOMPARE(native->disconnects, 0); QCOMPARE(native->removes, 0);
        commands.drain(); retirement.drain();
        const auto result = session->take();
        QVERIFY(result); QCOMPARE(result->phase, Phase::Closed);
        QCOMPARE(result->failed, disconnect_fails || remove_fails);
        QCOMPARE(native->disconnects, 1); QCOMPARE(native->removes, 1);
        QVERIFY(retirement.quiescent()); QVERIFY(commands.quiescent());
        QVERIFY(!session->borrow());
    }
    void unloadClockSaturationStillFencesTheOriginalOpening()
    {
        auto clock = std::make_shared<walletopening::NativeEventClock>();
        auto unloaded = std::make_shared<std::atomic<uint64_t>>(0);
        walletopening::NativeEvents missing_clock{{}, unloaded};
        missing_clock.observeUnload();
        QCOMPARE(unloaded->load(), uint64_t{0});
        walletopening::NativeEvents missing_target{clock, {}};
        missing_target.observeUnload();
        QCOMPARE(clock->value.load(), uint64_t{0});
        QCOMPARE(clock->next(), uint64_t{1});
        clock->value = std::numeric_limits<uint64_t>::max() - 1;
        walletopening::NativeEvents events{clock, unloaded};
        events.observeUnload();
        QCOMPARE(unloaded->load(), std::numeric_limits<uint64_t>::max());
        QCOMPARE(clock->next(), uint64_t{0});
        events.observeUnload();
        QCOMPARE(unloaded->load(), std::numeric_limits<uint64_t>::max());
        unloaded->store(0);
        events.observeUnload();
        QCOMPARE(unloaded->load(), std::numeric_limits<uint64_t>::max());
    }
    void localAdmissionCannotReopenTheSharedClosingFence()
    {
        auto gate = std::make_shared<std::atomic_bool>(false);
        walletopening::ClosedFlag first{gate}, second{gate}, local;
        QVERIFY(!first); QVERIFY(!second); QVERIFY(!local);
        first = true; local = true;
        QVERIFY(first); QVERIFY(local); QVERIFY(!second);
        gate->store(true); first = false; local = false;
        QVERIFY(first); QVERIFY(second); QVERIFY(!local);
        QCOMPARE(first.shared(), second.shared());
    }
    void closedRetirementAcquiresNoWalletOrHandlers()
    {
        ManualStartupExecution commands;
        const auto native = std::make_shared<Wallet>();
        const auto session = MakeWalletSession(commands.executor(), [] {});
        bool stopped{false};
        session->stop(false, [&] { stopped = true; });
        commands.drain();
        QVERIFY(stopped);
        const auto result = session->start(native);
        QCOMPARE(result.submitted.rejection, execution::Rejection::Closed);
        QVERIFY(!result.borrower);
        QVERIFY(!session->available());
        QVERIFY(session->closedFlag()->load());
        QCOMPARE(native.use_count(), 1L);
        QCOMPARE(native->reads, 0);
        QVERIFY(!native->status);
        QVERIFY(!native->unload);
        const auto update = session->take();
        QVERIFY(update && update->phase == Phase::Closed);
        QVERIFY(!session->borrow());
        QVERIFY(!session->startAction());
        QCOMPARE(session->start(native).submitted.rejection, execution::Rejection::Closed);
        QVERIFY(commands.quiescent());
    }
    void addressNotificationsAndLateCopiedCallbacksKeepTheirOpening()
    {
        Fixture fixture; fixture.start();
        const auto address = fixture.wallet->address;
        const auto status = fixture.wallet->status;
        const auto capability = fixture.wallet->address_capability;
        const auto transaction = fixture.wallet->transaction;
        const auto unload = fixture.wallet->unload;
        QVERIFY(address && status && capability && transaction && unload);
        address({}, "label", false, wallet::AddressPurpose{}, CT_NEW);
        QVERIFY(!fixture.session->take());
        fixture.commands.drain();
        const auto update = fixture.session->take();
        QVERIFY(update && update->addresses && update->metadata);
        QCOMPARE(update->metadata->name, std::string{"original"});
        std::weak_ptr<walletopening::Session> old = fixture.session;
        fixture.session->stop(false, {}); fixture.commands.drain(); fixture.retirement.drain();
        fixture.session.reset(); QVERIFY(old.expired());
        const auto wakes = fixture.wakes;
        address({}, "late", false, wallet::AddressPurpose{}, CT_UPDATED);
        status(); capability(); transaction(Txid{}, CT_DELETED); unload();
        QCOMPARE(fixture.wakes, wakes);
        QVERIFY(fixture.commands.quiescent()); QVERIFY(fixture.retirement.quiescent());
    }
    void invalidPersistentMetadataCannotEscapeTheBoundedSnapshot_data()
    {
        QTest::addColumn<QString>("preference");
        QTest::addColumn<QString>("expected");
        for (const auto* value : {"legacy", "p2sh-segwit", "bech32", "bech32m"})
            QTest::newRow(value) << QString::fromLatin1(value) << QString::fromLatin1(value);
        QTest::newRow("empty") << QString{} << QString{};
        QTest::newRow("invalid") << QStringLiteral("unrecognized-type") << QString{};
        QTest::newRow("oversized-preference") << QString(32'769, QChar{'x'}) << QString{};
    }
    void invalidPersistentMetadataCannotEscapeTheBoundedSnapshot()
    {
        QFETCH(QString, preference); QFETCH(QString, expected);
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [preference](const std::string&) { return preference.toStdString(); }, {}},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain();
        const auto update = session->take();
        QVERIFY(update && update->metadata && !update->failed);
        QCOMPARE(update->metadata->receive_preference, expected.toStdString());
    }
    void oversizedWalletMetadataFailsBeforeItCanBePublished()
    {
        Fixture fixture;
        fixture.wallet->name.assign(32'769, 'x');
        auto started = fixture.session->start(fixture.wallet);
        QVERIFY(started); started.borrower.reset();
        fixture.commands.drain();
        const auto failure = fixture.session->take();
        QVERIFY(failure && failure->failed && !failure->metadata);
        QVERIFY(!fixture.session->available());
        fixture.session->stop(false, {});
        fixture.commands.drain(); fixture.retirement.drain();
        const auto closed = fixture.session->take();
        QVERIFY(closed && closed->phase == Phase::Closed && !closed->metadata);
    }
    void preferenceExceptionReportsTheOriginalRevision_data()
    {
        QTest::addColumn<bool>("fail_write");
        QTest::newRow("write-throws-before-effect") << true;
        QTest::newRow("readback-throws-after-effect") << false;
    }
    void preferenceExceptionReportsTheOriginalRevision()
    {
        QFETCH(bool, fail_write);
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        bool fail_read{false}; int attempts{0}; std::string persisted;
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [&](const std::string&) { if (fail_read) throw std::runtime_error{"Readback failed"}; return persisted; },
                [&](const std::string&, const std::string& value) {
                    ++attempts;
                    if (fail_write) throw std::runtime_error{"Write failed"};
                    persisted = value; fail_read = true; return true;
                }},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference("legacy"));
        const auto revision = session->preferenceRevision();
        QCOMPARE(attempts, 0); QVERIFY(!session->take());
        commands.drain(); const auto result = session->take();
        QVERIFY(result && result->preference_saved && !*result->preference_saved);
        QCOMPARE(result->preference_revision, revision); QCOMPARE(result->phase, Phase::Open);
        QCOMPARE(attempts, 1);
        QCOMPARE(persisted, fail_write ? std::string{} : std::string{"legacy"});
        commands.drain(); QCOMPARE(attempts, 1);
    }
    void lateTransactionConsumerReconcilesAfterDomainRejection()
    {
        Fixture fixture; fixture.start();
        fixture.wallet->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        fixture.commands.drain();
        QVERIFY(fixture.session->notificationsPending());
        const auto revision = fixture.session->transactionRevision();
        int deliveries{0}; bool rescan{false}; uint64_t delivered_revision{0};
        fixture.commands.reject_utility = true;
        fixture.session->onTransactions([&](uint64_t value, auto, bool full) {
            ++deliveries; delivered_revision = value; rescan = full;
        });
        QCOMPARE(deliveries, 0);
        fixture.session->acknowledgeTransactions();
        QVERIFY(fixture.session->notificationsPending());
        fixture.commands.drain();
        QCOMPARE(deliveries, 1); QVERIFY(rescan); QCOMPARE(delivered_revision, revision);
        fixture.session->take(); fixture.session->acknowledgeTransactions();
        QVERIFY(!fixture.session->notificationsPending());
        fixture.session->onTransactions({});
    }
    void transactionOverflowRetainsARescanAndNewestDuplicate()
    {
        Fixture fixture; fixture.start();
        const auto id = [](unsigned int value) {
            std::vector<unsigned char> bytes(32);
            bytes[0] = static_cast<unsigned char>(value % 256);
            bytes[1] = static_cast<unsigned char>(value / 256);
            return Txid::FromUint256(uint256{bytes});
        };
        for (unsigned int n{0}; n < 257; ++n) fixture.wallet->transaction(id(n), CT_NEW);
        fixture.wallet->transaction(id(1), CT_DELETED);
        fixture.wallet->transaction(id(257), CT_NEW);
        QCOMPARE(fixture.wallet->reads, 1);
        fixture.session->acknowledgeTransactions(); QVERIFY(fixture.session->notificationsPending());
        fixture.commands.drain();
        const auto result = fixture.session->take();
        QVERIFY(result && result->rescan); QVERIFY(result->transactions.size() <= 256);
        QCOMPARE(result->transaction_revision, uint64_t{259});
        QCOMPARE(result->transactions.at(id(1)), CT_DELETED);
        fixture.session->acknowledgeTransactions(); QVERIFY(!fixture.session->notificationsPending());
    }
    void closedOpeningDiscardsQueuedObservationButConsumesAdmittedDomainAction()
    {
        Fixture fixture; fixture.start();
        int observations{0}, actions{0};
        QVERIFY(fixture.session->postDomain([&] { ++observations; }));
        QVERIFY(fixture.session->postDomain([&] { ++actions; }, execution::WorkClass::Action));
        fixture.session->close();
        fixture.commands.drain();
        QCOMPARE(observations, 0); QCOMPARE(actions, 1);
        QCOMPARE(fixture.session->postDomain([] {}).rejection, execution::Rejection::Closed);
        QCOMPARE(fixture.session->postDomain([] {}, execution::WorkClass::Action).rejection, execution::Rejection::Closed);
    }
    void originalActionFailureAndForeignPermitsHaveDistinctOutcomes()
    {
        Fixture first, second; first.start(); second.start();
        auto action = first.session->startAction(); QVERIFY(action);
        int effects{0}, outcomes{0}, errors{0};
        QCOMPARE(second.session->planAction(action, [&] { ++effects; }).rejection, execution::Rejection::Invalid);
        QCOMPARE(second.session->perform(action, [&](auto&) { ++effects; return 1; }, [&](int) { ++outcomes; }).rejection,
                 execution::Rejection::Invalid);
        QCOMPARE(first.session->planAction({}, [] {}).rejection, execution::Rejection::Invalid);
        QCOMPARE(first.session->perform({}, [](auto&) { return 1; }, [](int) {}).rejection, execution::Rejection::Invalid);
        QVERIFY(first.session->perform(action, [&](auto&) -> int { ++effects; throw std::runtime_error{"Original write failed"}; },
            [&](int) { ++outcomes; }, [&](std::exception_ptr error) {
                QVERIFY(error); ++errors;
                try { std::rethrow_exception(error); }
                catch (const std::runtime_error& failure) { QCOMPARE(QString::fromUtf8(failure.what()), QStringLiteral("Original write failed")); }
            }));
        first.session->stop(false, {});
        first.commands.drain(); QCOMPARE(effects, 1); QCOMPARE(outcomes, 0); QCOMPARE(errors, 1);
        QVERIFY(first.session->perform(action, [](auto&) -> int { throw std::runtime_error{"Unobserved original failure"}; }, [](int) {}));
        first.commands.drain(); action.reset(); first.commands.drain(); first.retirement.drain();
        QVERIFY(first.retirement.quiescent());
    }
    void cancelledTransportLetsResourceCleanupProgress()
    {
        Fixture fixture;
        fixture.start();
        int reads{0}, failures{0}, retired{0};
        bool stopped{false};
        const auto finish = qScopeGuard([&] {
            fixture.commands.drain();
            fixture.retirement.drain();
            fixture.commands.drain();
            fixture.retirement.drain();
        });
        auto held = fixture.session->holdValues([&] { ++retired; });
        QVERIFY(held);
        QVERIFY(fixture.session->read([&, held = std::move(held)](interfaces::Wallet&) {
            ++reads;
            return true;
        }, [](bool) { QFAIL("Cancelled read succeeded"); }, [&](std::exception_ptr error) {
            QCOMPARE(execution::NotStartedCause(error), std::make_optional(execution::NotStarted::Cause::Cancelled));
            ++failures;
        }));
        auto original = fixture.commands.take(Lane::Core);
        QVERIFY(original);
        fixture.session->stop(false, [&] { stopped = true; });
        original();
        QCOMPARE(reads, 0);
        QCOMPARE(failures, 0);
        fixture.commands.drain();
        QCOMPARE(failures, 1);
        QCOMPARE(retired, 0);
        fixture.retirement.drain();
        fixture.commands.drain();
        fixture.retirement.drain();
        QCOMPARE(retired, 1);
        QVERIFY(stopped);
        original();
        QCOMPARE(reads, 0);
        QCOMPARE(failures, 1);
        QCOMPARE(retired, 1);
        QVERIFY(fixture.commands.quiescent());
        QVERIFY(fixture.retirement.quiescent());
    }
    void admittedActionCompletesDependentStagesAfterFullClose()
    {
        Fixture fixture;
        fixture.start();
        const auto session = fixture.session;
        auto action = session->startAction();
        QVERIFY(action);
        auto output = walletopening::RetainValues<int>(session, [] { return std::make_unique<int>(0); });
        QVERIFY(output);
        int effects{0};
        bool stopped{false};
        std::shared_ptr<int> observed;
        const auto drain = qScopeGuard([&] {
            action.reset();
            output.reset();
            fixture.commands.drain();
            observed.reset();
            fixture.retirement.drain();
            fixture.commands.drain();
            fixture.retirement.drain();
        });
        QVERIFY(session->planAction(action, [&, action, output = std::move(output)] {
            QVERIFY(session->perform(action, [&, output](interfaces::Wallet&) {
                ++effects;
                *output = 42;
                return output;
            }, [&](auto result) { observed = std::move(result); }));
        }));
        action.reset();
        session->stop(false, [&] { stopped = true; });
        QVERIFY(!session->startAction());
        QVERIFY(fixture.commands.step(Lane::Utility));
        QCOMPARE(effects, 0);
        QVERIFY(fixture.commands.step(Lane::Core));
        QCOMPARE(effects, 1);
        QVERIFY(!observed);
        fixture.commands.drain();
        QVERIFY(observed);
        QCOMPARE(*observed, 42);
        fixture.retirement.drain();
        QVERIFY(!stopped);
        observed.reset();
        fixture.retirement.drain();
        fixture.commands.drain();
        fixture.retirement.drain();
        QVERIFY(stopped);
    }
    void originalActionScopeCompletesDependentEffectsAfterFullQueueClosure()
    {
        Fixture fixture; fixture.start();
        std::vector<QString> trace;
        auto action = fixture.session->startAction(); QVERIFY(action);
        auto session = fixture.session;
        QVERIFY(session->planAction(action, [session, action, &trace] {
            trace.emplace_back("plan-before-write");
            QVERIFY(session->perform(action, [&trace](interfaces::Wallet&) { trace.emplace_back("commit"); return true; },
                [session, action, &trace](bool committed) {
                    QVERIFY(committed); trace.emplace_back("actual-commit-outcome");
                    QVERIFY(session->planAction(action, [session, action, &trace] {
                        trace.emplace_back("plan-original-notes");
                        QVERIFY(session->perform(action, [&trace](interfaces::Wallet&) { trace.emplace_back("notes-write"); return true; },
                            [&trace](bool saved) { QVERIFY(saved); trace.emplace_back("actual-notes-outcome"); }));
                    }));
                }));
        }));
        fixture.session->close();
        fixture.session->stop(false, {});
        QVERIFY(!fixture.session->startAction());
        action.reset();
        QVERIFY(!fixture.retirement.step(Lane::Utility)); // Original accepted action still pins its native wallet.
        QCOMPARE(trace.size(), size_t{0});
        fixture.commands.drain();
        QCOMPARE(trace, std::vector<QString>({"plan-before-write", "commit", "actual-commit-outcome", "plan-original-notes", "notes-write", "actual-notes-outcome"}));
        fixture.retirement.drain();
        QCOMPARE(fixture.session->take()->phase, Phase::Closed);
        QVERIFY(fixture.commands.quiescent()); QVERIFY(fixture.retirement.quiescent());
    }
    void facadeSeparatesSecurityAdmissionEffectOutcomeAndPublication()
    {
        struct Native final : StubWallet {
            std::string getWalletName() override { return "original"; }
            bool isCrypted() override { return encrypted; }
            bool isLocked() override { return encrypted; }
            bool encryptWallet(const SecureString& password) override { ++effects; encrypted = password == SecureString{"secret"}; return encrypted; }
            int effects{0}; bool encrypted{false};
        };
        ManualStartupExecution commands;
        auto native = std::make_shared<Native>();
        WalletQmlModel model{WalletWithQueues(native, commands.executor()), nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        QVERIFY(model.capabilitiesKnown());
        QVERIFY(model.encryptWallet("secret"));
        QVERIFY(!model.encryptWallet("duplicate"));
        QCOMPARE(native->effects, 0);
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecurityPending);
        while (!native->effects && commands.step(Lane::Core)) {}
        QCOMPARE(native->effects, 1);
        QVERIFY(!model.isEncrypted());
        while (commands.step(Lane::Utility)) {}
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecurityPending);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecuritySucceeded);
        // Only Session metadata can update observed security; consume its
        // fresh Native read after the operation's historical outcome.
        commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(model.isEncrypted()); QVERIFY(model.isLocked());
        commands.drain();
    }
    void actualSecurityActionInvalidatesOlderCloseConsent()
    {
        struct Native final : StubWallet {
            std::string getWalletName() override { return "original"; }
            bool encryptWallet(const SecureString&) override { ++effects; return true; }
            int effects{0};
        };
        ManualStartupExecution commands;
        auto native = std::make_shared<Native>();
        WalletQmlModel model{WalletWithQueues(native, commands.executor()), nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        model.requestClose(); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        const QPointer<WalletCloseQuestion> original_question{model.closeQuestion()};
        QVERIFY(original_question);
        QVERIFY(model.available());
        QVERIFY(model.encryptWallet("secret"));
        original_question->accept();
        commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(native->effects, 1);
        QVERIFY(model.available());
        QVERIFY(model.closeQuestion());
        QVERIFY(model.closeQuestion() != original_question);
        if (original_question) original_question->accept();
        commands.drain();
        QVERIFY(model.available());
    }
    void retirementClosingDuringStartupDoesNotAcquireNativeOwnership()
    {
        ManualStartupExecution commands;
        const auto native = std::make_shared<Wallet>();
        const auto session = MakeWalletSession(commands.executor(), [] {});
        bool stopped{false};
        session->stop(false, [&] { stopped = true; });
        QVERIFY(!stopped);
        const auto started = session->start(native);
        QCOMPARE(started.submitted.rejection, execution::Rejection::Closed);
        QVERIFY(!started.borrower);
        QCOMPARE(native.use_count(), 1L);
        QCOMPARE(native->reads, 0);
        QVERIFY(!native->status);
        QVERIFY(!native->unload);
        commands.drain();
        QVERIFY(stopped);
        QVERIFY(commands.quiescent());
        QCOMPARE(session->take()->phase, Phase::Closed);
    }
    void startupFailureCannotRetireTheReturnedClientBeforeHandoff()
    {
        struct BrokenWallet final : StubWallet {
            std::string getWalletName() override { throw std::runtime_error("synthetic startup capture failure"); }
        };
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<BrokenWallet>();
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        auto started = session->start(native);
        QVERIFY(started);
        QVERIFY(started.borrower);
        commands.drain();
        QVERIFY(!session->available());
        session->stop(false, {}); commands.drain();
        QVERIFY(!retirement.step(Lane::Utility));
        QCOMPARE(started.borrower.get(), native.get());
        started.borrower.reset();
        retirement.drain();
        QVERIFY(retirement.quiescent());
    }
    void duplicateOpeningStartDoesNotRetireItsLiveResource()
    {
        Fixture f; f.start();
        QCOMPARE(f.session->start(f.wallet).submitted.rejection, execution::Rejection::Invalid);
        QVERIFY(f.session->available());
        QVERIFY(f.session->borrow());
    }
    void registrationAndCaptureWaitForCoreAndDomain()
    {
        Fixture f;
        QVERIFY(f.session->start(f.wallet));
        QCOMPARE(f.wallet->reads, 0);
        QVERIFY(!f.wallet->status);
        QVERIFY(!f.session->take());
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->reads, 1);
        QVERIFY(f.wallet->status);
        QCOMPARE(f.wakes, 0);
        QVERIFY(f.commands.step(Lane::Utility));
        QCOMPARE(f.wakes, 1);
        const auto update = f.session->take();
        QVERIFY(update && update->metadata);
        QCOMPARE(update->metadata->name, std::string{"original"});
        QVERIFY(update->metadata->encrypted);
        QVERIFY(update->metadata->locked);
    }
    void receiveCapabilitiesRefreshFromTheirActualProducer()
    {
        Fixture f; f.start();
        QVERIFY(f.wallet->address_capability);
        f.wallet->taproot = false;
        f.wallet->address_capability();
        QVERIFY(!f.session->take());
        f.commands.drain();
        const auto update = f.session->take();
        QVERIFY(update && update->metadata);
        QVERIFY(update->metadata->can_get_addresses);
        QVERIFY(!update->metadata->taproot);
    }
    void queuedPreferenceChangesKeepLatestOutcomePending()
    {
        ManualStartupExecution commands;
        auto native = std::make_shared<Wallet>();
        std::string saved;
        int writes{0};
        walletopening::Session::Preferences preferences{
            [&](const std::string&) { return saved; },
            [&](const std::string&, const std::string& type) { saved = type; ++writes; return true; }};
        WalletQmlModel model{WalletWithQueues(native, commands.executor(), std::move(preferences)), nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        QVERIFY(model.setDefaultReceiveAddressType("legacy"));
        QVERIFY(model.setDefaultReceiveAddressType("bech32"));
        QCOMPARE(writes, 0);
        const auto write = [&](int expected) {
            while (writes < expected && (commands.step(Lane::Core) || commands.step(Lane::Utility))) {}
            return writes == expected;
        };
        const auto consume = [&] {
            bool delivered{false};
            if (!model.opening()->utilityQueue()->post([&] { delivered = true; })) return false;
            while (!delivered && commands.step(Lane::Utility)) {}
            return delivered;
        };
        QVERIFY(write(1));
        QVERIFY(consume());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(writes, 1);
        QVERIFY(model.property("receivePreferencePending").toBool());
        QVERIFY(write(2));
        // Metadata publication can enqueue other Domain work. Native success
        // alone cannot settle pending: consume the exact second write outcome.
        QVERIFY(model.property("receivePreferencePending").toBool());
        QVERIFY(consume());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(writes, 2);
        QVERIFY(!model.property("receivePreferencePending").toBool());
        QCOMPARE(saved, std::string{"bech32"});
        QCOMPARE(model.defaultReceiveAddressType(), QString{"bech32"});
    }
    void publicSettingsPortPreservesOtherWalletAndReportsWriteFailure_data()
    {
        QTest::addColumn<bool>("write_ok");
        QTest::newRow("persisted") << true;
        QTest::newRow("write-failed-memory-kept") << false;
    }
    void publicSettingsPortPreservesOtherWalletAndReportsWriteFailure()
    {
        QFETCH(bool, write_ok);
        struct Settings final : FeePreviewChain {
            common::SettingsValue getRwSetting(const std::string&) override { return values; }
            bool updateRwSetting(const std::string&, const interfaces::SettingsUpdate& update) override
            {
                ++writes;
                const auto action = update(values);
                return action && *action == interfaces::SettingsAction::WRITE && write_ok;
            }
            common::SettingsValue values{UniValue::VOBJ};
            int writes{0}; bool write_ok{true};
        } settings;
        settings.write_ok = write_ok;
        settings.values.pushKV("other-wallet", "bech32");
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::ChainPreferences(settings),
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference("legacy"));
        QCOMPARE(settings.writes, 0);
        commands.drain();
        const auto result = session->take();
        QVERIFY(result && result->preference_saved && result->metadata);
        QCOMPARE(*result->preference_saved, write_ok);
        QCOMPARE(result->metadata->receive_preference, std::string{"legacy"});
        QCOMPARE(settings.values["other-wallet"].get_str(), std::string{"bech32"});
        commands.drain(); QCOMPARE(settings.writes, 1);
    }
    void receivePreferenceSeparatesEffectOutcomeAndCachesReadback()
    {
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        std::string saved;
        int writes{0};
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [&](const std::string&) { return saved; },
                [&](const std::string& name, const std::string& type) { if (name != "original") QTest::qFail("Preference retargeted", __FILE__, __LINE__); saved = type; ++writes; return true; }},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QCOMPARE(session->setReceivePreference("invalid").rejection, execution::Rejection::Invalid);
        QVERIFY(session->setReceivePreference("p2sh-segwit"));
        QCOMPARE(writes, 0);
        QVERIFY(commands.step(Lane::Core));
        QCOMPARE(writes, 1); QCOMPARE(saved, std::string{"p2sh-segwit"});
        QVERIFY(!session->take());
        QVERIFY(commands.step(Lane::Utility));
        auto update = session->take();
        QVERIFY(update && update->preference_saved && *update->preference_saved && update->metadata);
        QCOMPARE(update->metadata->receive_preference, saved);
        native->taproot = false;
        QVERIFY(session->setReceivePreference("bech32m")); commands.drain();
        update = session->take();
        QVERIFY(update && update->preference_saved && !*update->preference_saved);
        QCOMPARE(writes, 1); QCOMPARE(saved, std::string{"p2sh-segwit"});
    }
    void closeQuestionDoesNotFenceNewActionsAndOldConsentCannotClose()
    {
        Fixture f; f.start();
        const auto before = f.question();
        QVERIFY(f.session->available());
        QVERIFY(f.session->admitAction());
        f.session->answerClose(before, true);
        f.commands.drain();
        QVERIFY(f.session->available());
        const auto changed = f.session->take();
        QVERIFY(changed && changed->question);
        QVERIFY(changed->question->number != before.number);
        f.session->answerClose(before, true);
        f.commands.drain();
        QVERIFY(f.session->available());
        f.session->answerClose(*changed->question, false);
        f.commands.drain();
        const auto dismissed = f.session->take();
        QVERIFY(dismissed && !dismissed->question);
        QVERIFY(f.session->available());
    }
    void currentConsentFencesAdmissionAndWaitsForBorrower()
    {
        Fixture f; f.start();
        auto held = f.session->borrow(); QVERIFY(held);
        const auto question = f.question();
        f.session->answerClose(question, true);
        f.commands.drain();
        QVERIFY(!f.session->available());
        f.session->stop(true, {});
        f.commands.drain();
        QVERIFY(!f.session->admitAction());
        QVERIFY(!f.session->borrow());
        QVERIFY(!f.retirement.step(Lane::Utility));
        const auto before_outcome = f.session->take();
        QVERIFY(before_outcome && before_outcome->phase == Phase::Closing);
        held.reset();
        QVERIFY(f.retirement.step(Lane::Utility));
        const auto closed = f.session->take();
        QVERIFY(closed && closed->phase == Phase::Closed && !closed->question);
        QVERIFY(f.retirement.quiescent());
    }
    void rejectedLatePreferenceCannotReopenClosingPublication()
    {
        Fixture f; f.start();
        auto held = f.session->borrow();
        f.session->answerClose(f.question(), true); f.commands.drain();
        QVERIFY(!f.session->available());
        QCOMPARE(f.session->setReceivePreference("legacy").rejection, execution::Rejection::Closed);
        const auto update = f.session->take();
        QVERIFY(update && update->phase == Phase::Closing && !update->question);
        f.session->stop(true, {});
        f.commands.drain();
        held.reset(); f.retirement.drain();
        QCOMPARE(f.session->take()->phase, Phase::Closed);
    }
    void oldQuestionCannotCloseReopenedWalletWithSameName()
    {
        Fixture old; old.start(); const auto question = old.question();
        old.session->stop(false, {}); old.commands.drain(); old.retirement.drain();
        Fixture reopened; reopened.start();
        QVERIFY(old.session->identity() != reopened.session->identity());
        reopened.question();
        reopened.session->answerClose(question, true); reopened.commands.drain();
        QVERIFY(reopened.session->available());
        QVERIFY(reopened.session->take()->question.has_value());
    }
    void externalUnloadSupersedesQuestionBeforeGuiDelivery()
    {
        Fixture f; f.start(); const auto question = f.question();
        QVERIFY(f.wallet->unload);
        f.wallet->unload();
        QVERIFY(!f.session->available());
        f.session->answerClose(question, true);
        f.commands.drain();
        f.session->stop(false, {});
        f.commands.drain(); f.retirement.drain();
        const auto update = f.session->take();
        QVERIFY(update && update->unloaded && update->phase == Phase::Closed && !update->question);
    }
    void admittedReadRetainsOutcomeUntilDomainConsumption()
    {
        Fixture f; f.start();
        int outcomes{0};
        QVERIFY(f.session->read([](interfaces::Wallet&) { return 42; }, [&](int value) { QCOMPARE(value, 42); ++outcomes; }));
        QVERIFY(f.commands.step(Lane::Core));
        f.session->stop(false, {});
        QVERIFY(!f.retirement.step(Lane::Utility));
        QCOMPARE(outcomes, 0);
        QVERIFY(f.commands.step(Lane::Utility));
        QCOMPARE(outcomes, 1);
        f.retirement.drain();
        QVERIFY(f.retirement.quiescent());
    }
    void rejectedNoticeRetainsReconciliationAndUsesControlledBackoff()
    {
        ManualStartupExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        std::chrono::steady_clock::time_point now{};
        auto session = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{}, [&] { return now; }, nullptr, walletopening::NativeEvents{},
            WalletSessionQueues(commands.executor(), retirement.executor()));
        const auto cleanup = qScopeGuard([&] { session->stop(false, {}); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        commands.reject_core = true;
        native->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        QCOMPARE(native->reads, 1);
        QVERIFY(session->notificationsPending());
        QVERIFY(session->take()->failed);
        QVERIFY(!commands.hasWork(Lane::Core));
        while (commands.step(Lane::Utility)) {} // Retire the rejected read's reserved outcome.
        QVERIFY(commands.quiescent());
        for (int n{0}; n < 100; ++n) { session->retryNotifications(); native->status(); }
        QVERIFY(commands.quiescent());
        now += std::chrono::milliseconds{999}; session->retryNotifications();
        QVERIFY(commands.quiescent());
        now += std::chrono::milliseconds{1}; session->retryNotifications();
        native->fail_read = true; commands.drain();
        QVERIFY(session->take()->failed);
        const auto before = native->reads;
        now += std::chrono::milliseconds{1999}; session->retryNotifications();
        QVERIFY(commands.quiescent()); QCOMPARE(native->reads, before);
        native->fail_read = false;
        now += std::chrono::milliseconds{1}; session->retryNotifications(); commands.drain();
        const auto recovered = session->take();
        QVERIFY(recovered && recovered->rescan && recovered->metadata);
        QVERIFY(session->notificationsPending());
        session->acknowledgeTransactions();
        QVERIFY(!session->notificationsPending());
    }
    void pendingNotificationsCoalesceWithoutGuiOrCapturePolling()
    {
        Fixture f; f.start();
        for (int n{0}; n < 100; ++n) f.wallet->status();
        QCOMPARE(f.wallet->reads, 1);
        f.commands.drain();
        const auto reads = f.wallet->reads;
        QVERIFY(reads <= 3);
        const auto update = f.session->take();
        QVERIFY(update && update->status && update->metadata);
        f.commands.drain();
        QCOMPARE(f.wallet->reads, reads);
    }

};

#include <test_walletopening.moc>
BITCOINQML_REGISTER_QT_TEST(WalletOpeningTests)
