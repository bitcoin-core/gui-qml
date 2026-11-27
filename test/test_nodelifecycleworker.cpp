// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/nodelifecycleworker.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>

#include <algorithm>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace nodelifecycle;
using execution::Lane;

struct Turns {
    std::deque<std::shared_ptr<execution::Task>> native, priority;
    std::optional<Lane> reject;
    bool closed{false};

    auto& queue(Lane lane) { return lane == Lane::Priority ? priority : native; }
    bool post(Lane lane, std::shared_ptr<execution::Task> task)
    {
        if (closed || reject == lane) {
            reject.reset();
            return false;
        }
        queue(lane).push_back(std::move(task));
        return true;
    }
    Driver::Post port(Lane lane) { return [this, lane](auto task) { return post(lane, std::move(task)); }; }
    std::shared_ptr<execution::Task> take(Lane lane)
    {
        auto& turns = queue(lane);
        if (turns.empty()) return {};
        auto turn = std::move(turns.front());
        turns.pop_front();
        return turn;
    }
    void finish(std::shared_ptr<execution::Task> turn, bool cancelled = false)
    {
        if (cancelled) turn->notStarted(execution::NotStarted::Cause::Cancelled);
        else turn->execute();
        turn->deliver();
    }
    bool step(Lane lane)
    {
        auto turn = take(lane);
        if (!turn) return false;
        finish(std::move(turn));
        return true;
    }
    void drain() { while (step(Lane::Core) || step(Lane::Priority)) {} }
    bool idle() const { return native.empty() && priority.empty(); }
};

struct Fixture {
    Turns execution;
    std::unique_ptr<Driver> driver;
    std::vector<Projection> publications;
    NativeInitialization native;
    int initializes{0}, interrupts{0}, shutdowns{0}, descriptions{0}, notices{0}, exit_status{7};
    bool throw_initialize{false}, throw_interrupt{false}, throw_shutdown{false}, throw_describe{false}, throw_publish{false};
    bool stopped_at_native_entry{false}, stored_before_publish{true};
    explicit Fixture(bool closed = false)
    {
        execution.closed = closed;
        native.attempted = true; native.success = true;
        Effects effects;
        effects.initialize = [this](const std::atomic_bool& stopping) {
            ++initializes; stopped_at_native_entry = stopping.load();
            if (throw_initialize) throw std::runtime_error{"original initialization exception"};
            return native;
        };
        effects.interrupt = [this] { ++interrupts; if (throw_interrupt) throw std::runtime_error{"original interruption exception"}; };
        effects.shutdown = [this] { ++shutdowns; if (throw_shutdown) throw std::runtime_error{"original shutdown exception"}; return exit_status; };
        effects.describe = [this](std::exception_ptr error) {
            ++descriptions;
            if (throw_describe) throw std::runtime_error{"description unavailable"};
            try { if (error) std::rethrow_exception(error); }
            catch (const std::exception& value) { return QString::fromUtf8(value.what()); }
            catch (...) { return QStringLiteral("original unknown exception"); }
            return QStringLiteral("no exception");
        };
        driver = std::make_unique<Driver>(execution.port(Lane::Core), execution.port(Lane::Priority), std::move(effects),
            [this](Projection value) {
                ++notices;
                const auto retained = driver->projection();
                stored_before_publish = stored_before_publish && retained.event == value.event && retained.phase == value.phase;
                if (throw_publish) throw std::runtime_error{"publication rejected"};
                publications.push_back(std::move(value));
            });
    }
    ~Fixture() { publications.clear(); driver.reset(); execution.drain(); }
    void settle() { execution.drain(); }
    void runPriority()
    {
        while (execution.step(Lane::Priority)) {}
    }
    std::shared_ptr<execution::Task> startHeldInitialization()
    {
        if (!driver->initialize() || !execution.step(Lane::Core)) return {};
        return execution.take(Lane::Core);
    }
    int events(Event event) const
    {
        return static_cast<int>(std::count_if(publications.begin(), publications.end(), [event](const auto& value) { return value.event == event; }));
    }
};
} // namespace

class NodeLifecycleWorkerTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initializationReceiptIsStoredBeforeNotification()
    {
        Fixture fixture;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Ready); QCOMPARE(value.event, Event::Initialized);
        QVERIFY(value.initialization); QVERIFY(value.initialization->attempted); QVERIFY(value.initialization->success);
        QCOMPARE(fixture.initializes, 1); QCOMPARE(fixture.events(Event::Initialized), 1);
        QVERIFY(fixture.stored_before_publish);
    }

    void returnedAttemptedFactIsNotRewritten_data()
    {
        QTest::addColumn<bool>("attempted"); QTest::addColumn<bool>("success"); QTest::addColumn<bool>("cancelled");
        QTest::newRow("ready") << true << true << false;
        QTest::newRow("known-failure") << true << false << false;
        QTest::newRow("not-attempted-failure") << false << false << false;
        QTest::newRow("not-attempted-cancel") << false << false << true;
        QTest::newRow("successful-effect-cancelled") << true << true << true;
    }
    void returnedAttemptedFactIsNotRewritten()
    {
        QFETCH(bool, attempted); QFETCH(bool, success); QFETCH(bool, cancelled);
        Fixture fixture;
        fixture.native.attempted = attempted; fixture.native.success = success; fixture.native.cancelled = cancelled;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        const auto value = fixture.driver->projection(); QVERIFY(value.initialization);
        QCOMPARE(value.initialization->attempted, attempted); QCOMPARE(value.initialization->success, success);
        QCOMPARE(value.initialization->cancelled, cancelled);
        QCOMPARE(value.phase, cancelled ? Phase::Stopping : success ? Phase::Ready : Phase::StartupFailed);
        QCOMPARE(value.initialization->tip.block_height, 0); QCOMPARE(value.initialization->tip.header_height, 0);
        QCOMPARE(value.initialization->tip.block_time, int64_t{0}); QCOMPARE(value.initialization->tip.header_time, int64_t{0});
        QCOMPARE(value.initialization->tip.verification_progress, 0.0);
    }

    void directShutdownAutomaticallyInterruptsAndRecordsExit()
    {
        Fixture fixture;
        QVERIFY(fixture.driver->shutdown()); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.shutdowns, 1);
        QCOMPARE(value.phase, Phase::Stopped); QCOMPARE(value.event, Event::Shutdown);
        QVERIFY(value.core_exit_status); QCOMPARE(*value.core_exit_status, 7);
        QCOMPARE(fixture.events(Event::Interrupted), 1); QCOMPARE(fixture.events(Event::Shutdown), 1);
    }

    void contradictoryNativeReceiptCannotBecomeReadyOrStrandStart()
    {
        Fixture fixture; fixture.native.attempted = false; fixture.native.success = true;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Fatal); QVERIFY(value.initialization);
        QVERIFY(!value.initialization->attempted); QVERIFY(value.initialization->success);
        QVERIFY(fixture.driver->shutdown()); fixture.settle(); QCOMPARE(fixture.shutdowns, 0);
    }

    void directShutdownWaitsForInitializationOutcome()
    {
        Fixture fixture; auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->shutdown()); fixture.runPriority();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.shutdowns, 0);
        QCOMPARE(fixture.driver->projection().phase, Phase::Draining);
        fixture.execution.finish(std::move(held)); fixture.settle();
        QCOMPARE(fixture.initializes, 1); QCOMPARE(fixture.shutdowns, 1);
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Stopped); QVERIFY(value.initialization); QVERIFY(value.initialization->success);
        QVERIFY(fixture.stopped_at_native_entry);
    }

    void interruptionRunsOnControlWhileInitializationIsHeld()
    {
        Fixture fixture; auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->interrupt()); fixture.runPriority();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.initializes, 0);
        QCOMPARE(fixture.driver->projection().phase, Phase::Draining);
        QCOMPARE(fixture.events(Event::Interrupted), 1);
        fixture.execution.finish(std::move(held)); fixture.settle();
    }

    void pendingOrdinaryWorkDoesNotBlockControlDecisionOrEffect()
    {
        Fixture fixture; auto held = fixture.startHeldInitialization(); QVERIFY(held);
            int unrelated{0};
        for (int n = 0; n < 2; ++n) {
            QVERIFY(fixture.execution.post(Lane::Core, execution::MakeTask([&] { ++unrelated; }, [] {}, {})));
        }
        QVERIFY(fixture.driver->interrupt()); fixture.runPriority();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(unrelated, 0); QCOMPARE(fixture.initializes, 0);
        fixture.execution.finish(std::move(held)); fixture.settle();
        QCOMPARE(unrelated, 2);
    }

    void ordinaryFenceRejectsNewStartButRetainsStopContinuation()
    {
        Fixture fixture;
        QVERIFY(fixture.driver->shutdown());
        QVERIFY(!fixture.driver->initialize());
        QCOMPARE(fixture.interrupts, 0);
        fixture.settle();
        QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.shutdowns, 1);
        const auto status=fixture.driver->projection().core_exit_status;QVERIFY(status);QCOMPARE(*status,7);
    }

    void stopIngressFencesLateSuccessBeforeDomainControlReceipt()
    {
        Fixture fixture; auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->interrupt());
        fixture.execution.finish(std::move(held));
        // The native result arrived before the queued Priority interruption.
        // It still cannot restore readiness after the stop request.
        const auto value = fixture.driver->projection();
        QVERIFY(value.initialization); QVERIFY(value.initialization->success); QVERIFY(value.initialization->attempted);
        QVERIFY(!value.initialization->cancelled);
        QCOMPARE(value.phase, Phase::Stopping); QCOMPARE(fixture.interrupts, 0);
        fixture.settle(); QCOMPARE(fixture.interrupts, 1);
    }

    void throwingPublisherDoesNotStrandEffectAndRetryDoesNotReplay()
    {
        Fixture fixture; fixture.throw_publish = true;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Ready); QVERIFY(value.initialization); QVERIFY(value.initialization->success);
        QVERIFY(value.publication_failed); QCOMPARE(fixture.initializes, 1); QVERIFY(fixture.stored_before_publish);
        const int before = fixture.notices; fixture.throw_publish = false;
        QVERIFY(fixture.driver->retryPublish()); fixture.settle();
        value = fixture.driver->projection();
        QVERIFY(!value.publication_failed); QCOMPARE(fixture.notices, before + 1);
        QCOMPARE(fixture.initializes, 1); QCOMPARE(fixture.interrupts, 0); QCOMPARE(fixture.shutdowns, 0);
        QCOMPARE(fixture.events(Event::Initialized), 1);
    }

    void terminalPublicationRetryDoesNotRepeatTeardown()
    {
        Fixture fixture;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        fixture.throw_publish = true;
        QVERIFY(fixture.driver->shutdown()); fixture.settle();
        auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Stopped); QVERIFY(value.core_exit_status); QCOMPARE(*value.core_exit_status, 7);
        QVERIFY(value.publication_failed); QCOMPARE(fixture.shutdowns, 1);
        fixture.throw_publish = false;
        QVERIFY(fixture.driver->retryPublish()); fixture.settle();
        value = fixture.driver->projection();
        QVERIFY(!value.publication_failed); QCOMPARE(fixture.shutdowns, 1); QCOMPARE(fixture.interrupts, 1);
        QCOMPARE(fixture.events(Event::Shutdown), 1);
    }
    void nativeFatalNeverStartsUnsafeTeardown_data()
    {
        QTest::addColumn<int>("operation");
        QTest::newRow("initialize") << 0; QTest::newRow("interrupt") << 1; QTest::newRow("shutdown") << 2;
    }
    void nativeFatalNeverStartsUnsafeTeardown()
    {
        QFETCH(int, operation); Fixture fixture;
        fixture.throw_initialize = operation == 0; fixture.throw_interrupt = operation == 1; fixture.throw_shutdown = operation == 2;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        if (operation != 0) { QVERIFY(fixture.driver->shutdown()); fixture.settle(); }
        auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Fatal); QCOMPARE(value.event, Event::Fatal);
        QVERIFY(value.diagnostic); QVERIFY(value.diagnostic->startsWith(QStringLiteral("original ")));
        QVERIFY(!value.core_exit_status);
        QCOMPARE(fixture.descriptions, 1);
        QCOMPARE(fixture.shutdowns, operation == 2 ? 1 : 0);
        const int controls = fixture.interrupts, shutdowns = fixture.shutdowns;
        QVERIFY(fixture.driver->shutdown()); QVERIFY(fixture.driver->interrupt()); fixture.settle();
        QCOMPARE(fixture.interrupts, controls); QCOMPARE(fixture.shutdowns, shutdowns);
        QCOMPARE(fixture.events(Event::Shutdown), 0);
    }

    void fatalReceiptSurvivesLateInitializationAndThrowingPublisher()
    {
        Fixture fixture; fixture.throw_publish = true;
        auto held = fixture.startHeldInitialization(); QVERIFY(held);
        fixture.throw_interrupt = true;
        QVERIFY(fixture.driver->shutdown()); fixture.runPriority();
        auto fatal = fixture.driver->projection();
        QCOMPARE(fatal.phase, Phase::Fatal); QVERIFY(fatal.diagnostic);
        const auto original = *fatal.diagnostic;
        fixture.execution.finish(std::move(held)); fixture.settle();
        auto value = fixture.driver->projection();
        QCOMPARE(value.event, Event::Fatal); QCOMPARE(*value.diagnostic, original);
        QVERIFY(value.initialization); QVERIFY(value.initialization->success); QVERIFY(value.publication_failed);
        QCOMPARE(fixture.shutdowns, 0);
        fixture.throw_publish = false;
        QVERIFY(fixture.driver->retryPublish()); fixture.settle();
        QCOMPARE(fixture.events(Event::Fatal), 1); QCOMPARE(fixture.shutdowns, 0);
        QVERIFY(!fixture.driver->projection().publication_failed);
    }

    void diagnosticDescriptionFailureHasBoundedFallback()
    {
        Fixture fixture; fixture.throw_initialize = true; fixture.throw_describe = true;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Fatal); QVERIFY(value.diagnostic);
        QCOMPARE(*value.diagnostic, QStringLiteral("The node lifecycle diagnostic could not be prepared."));
        QCOMPARE(fixture.shutdowns, 0);
    }

    void knownNativeRejectionRetainsNotAttemptedReceipt()
    {
        Fixture fixture;
        QVERIFY(fixture.driver->initialize());
        fixture.execution.reject = Lane::Core;
        fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::StartupFailed); QVERIFY(value.initialization);
        QVERIFY(!value.initialization->attempted); QVERIFY(!value.initialization->success);
        QCOMPARE(fixture.initializes, 0);
    }

    void rejectedAcceptedInitializationStillCompletesPendingShutdown()
    {
        Fixture fixture; auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->shutdown()); fixture.runPriority();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.shutdowns, 0);
        fixture.execution.finish(std::move(held), /*cancelled=*/true); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.shutdowns, 1);
        QVERIFY(value.initialization); QVERIFY(!value.initialization->attempted); QVERIFY(value.initialization->cancelled);
        QCOMPARE(value.phase, Phase::Stopped); QCOMPARE(value.core_exit_status, std::optional<int>{7});
    }

    void cancelledDomainIngressHasKnownUnattemptedOutcome()
    {
        Fixture fixture; QVERIFY(fixture.driver->initialize());
        auto ingress = fixture.execution.take(Lane::Core); QVERIFY(ingress);
        fixture.execution.finish(std::move(ingress), /*cancelled=*/true); fixture.settle();
        const auto value = fixture.driver->projection();
        QVERIFY(value.initialization); QVERIFY(!value.initialization->attempted); QVERIFY(value.initialization->cancelled);
        QCOMPARE(fixture.initializes, 0); QCOMPARE(value.phase, Phase::Stopping);
    }

    void rejectedControlIngressIsExplicitAndCannotRestoreReadiness()
    {
        Fixture fixture; QVERIFY(fixture.driver->initialize()); fixture.settle();
        fixture.execution.reject = Lane::Priority;
        QVERIFY(!fixture.driver->interrupt());
        QCOMPARE(fixture.driver->projection().phase, Phase::Stopping);
        QCOMPARE(fixture.interrupts, 0); QVERIFY(!fixture.driver->initialize());
    }

    void cancelledAcceptedControlIngressHasTerminalReceipt()
    {
        Fixture fixture; QVERIFY(fixture.driver->initialize()); fixture.settle();
        QVERIFY(fixture.driver->interrupt());
        auto ingress = fixture.execution.take(Lane::Priority); QVERIFY(ingress);
        fixture.execution.finish(std::move(ingress), /*cancelled=*/true); fixture.settle();
        QCOMPARE(fixture.driver->projection().phase, Phase::Fatal);
        QCOMPARE(fixture.interrupts, 0); QCOMPARE(fixture.shutdowns, 0);
        QCOMPARE(fixture.events(Event::Fatal), 1);
    }

    void closeRetainsAcceptedNativeOutcomeAndReleasesCapabilities()
    {
        Fixture fixture; auto held = fixture.startHeldInitialization(); QVERIFY(held);
        fixture.driver->close();
        QVERIFY(!fixture.driver->initialize()); QVERIFY(!fixture.driver->interrupt());
        QVERIFY(!fixture.driver->shutdown()); QVERIFY(!fixture.driver->retryPublish());
        fixture.execution.finish(std::move(held)); fixture.settle();
        const auto value = fixture.driver->projection();
        QVERIFY(value.initialization); QVERIFY(value.initialization->success);
        QCOMPARE(value.phase, Phase::Stopping); QCOMPARE(fixture.initializes, 1); QCOMPARE(fixture.shutdowns, 0);
        QVERIFY(fixture.execution.idle());
    }

    void acceptedDeferredShutdownOutlivesObserverClose()
    {
        Fixture fixture; auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->shutdown()); fixture.runPriority();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.shutdowns, 0);
        fixture.driver->close();
        fixture.execution.finish(std::move(held)); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(fixture.shutdowns, 1); QCOMPARE(value.phase, Phase::Stopped);
        QVERIFY(value.core_exit_status); QCOMPARE(*value.core_exit_status, 7);
        QVERIFY(fixture.execution.idle());
    }

    void fatalCloseReleasesMailboxBorrowersWhileFacadeRemainsAlive()
    {
        Fixture fixture; fixture.throw_initialize = true;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        const auto original = fixture.driver->projection().diagnostic;
        QVERIFY(original);
        fixture.publications.clear(); fixture.driver->close(); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Fatal); QVERIFY(!value.diagnostic);
        QCOMPARE(*original, QStringLiteral("original initialization exception"));
        QVERIFY(fixture.execution.idle());
    }

    void acceptedPortsRetireOnlyAfterNativeOutcome()
    {
        struct Captured { int* retired; ~Captured() { ++*retired; } };
        Turns execution;
        int retired{0}, initialized{0};
        auto captured = std::make_shared<Captured>(); captured->retired = &retired;
        std::weak_ptr<Captured> lifetime = captured;
        Effects effects;
        effects.initialize = [captured, &initialized](const std::atomic_bool&) {
            ++initialized; NativeInitialization value; value.attempted = true; value.success = true; return value;
        };
        effects.interrupt = [captured] {};
        effects.shutdown = [captured] { return 0; };
        auto driver = std::make_unique<Driver>(execution.port(Lane::Core), execution.port(Lane::Priority), std::move(effects), Driver::Publish{});
        // std::function's moved-from small target may retain its captures on
        // libc++. Release the caller's port aliases before testing retirement.
        effects = {};
        captured.reset();
        QVERIFY(driver->initialize()); QVERIFY(execution.step(Lane::Core));
        auto held = execution.take(Lane::Core); QVERIFY(held);
        driver.reset(); QVERIFY(!lifetime.expired()); QCOMPARE(retired, 0);
        execution.finish(std::move(held)); execution.drain();
        QCOMPARE(initialized, 1); QVERIFY(lifetime.expired()); QCOMPARE(retired, 1); QVERIFY(execution.idle());
    }

    void missingRequiredPortsAreRejectedBeforeEffects_data()
    {
        QTest::addColumn<int>("missing");
        QTest::newRow("scheduler") << 0; QTest::newRow("initialize") << 1;
        QTest::newRow("interrupt") << 2; QTest::newRow("shutdown") << 3;
    }
    void missingRequiredPortsAreRejectedBeforeEffects()
    {
        QFETCH(int, missing); Turns execution; int calls{0};
        auto native = execution.port(Lane::Core);
        auto priority = execution.port(Lane::Priority);
        Effects effects;
        effects.initialize = [&](const std::atomic_bool&) { ++calls; return NativeInitialization{}; };
        effects.interrupt = [&] { ++calls; }; effects.shutdown = [&] { ++calls; return 0; };
        if (missing == 0) native = {};
        if (missing == 1) effects.initialize = {};
        if (missing == 2) effects.interrupt = {};
        if (missing == 3) effects.shutdown = {};
        bool rejected{false};
        try { Driver driver{std::move(native), std::move(priority), std::move(effects), {}}; }
        catch (const std::invalid_argument&) { rejected = true; }
        QVERIFY(rejected); QCOMPARE(calls, 0); QVERIFY(execution.idle());
    }

    void closedOwnerReservationDoesNotAdmitEffects()
    {
        Fixture fixture{true};
        QVERIFY(!fixture.driver->initialize()); QVERIFY(!fixture.driver->interrupt()); QVERIFY(!fixture.driver->shutdown());
        const auto receipt = fixture.driver->projection();
        QCOMPARE(receipt.phase, Phase::StartupFailed);
        QVERIFY(receipt.initialization);
        QVERIFY(!receipt.initialization->attempted);
        fixture.settle(); QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.interrupts, 0); QCOMPARE(fixture.shutdowns, 0);
        QVERIFY(fixture.execution.idle());
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NodeLifecycleWorkerTests)
#else
QTEST_MAIN(NodeLifecycleWorkerTests)
#endif
#include <test_nodelifecycleworker.moc>
