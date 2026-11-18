// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/nodelifecycleworker.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace nodelifecycle;
struct Fixture {
    ManualRoutedExecution execution;
    std::unique_ptr<Driver> driver;
    std::vector<Projection> publications;
    NativeInitialization native;
    int initializes{0}, interrupts{0}, shutdowns{0}, descriptions{0}, notices{0}, exit_status{7};
    bool throw_initialize{false}, throw_interrupt{false}, throw_shutdown{false}, throw_describe{false}, throw_publish{false};
    bool stopped_at_native_entry{false}, stored_before_publish{true};
    explicit Fixture(bool closed = false)
    {
        if (closed) execution.scheduler->close();
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
        driver = std::make_unique<Driver>(execution::SnapshotServices{execution.scheduler, {}}, std::move(effects),
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
    void controlAndDomain()
    {
        for (int turn = 0; turn < 64; ++turn) {
            const bool control = execution.step(execution::Lane::Control);
            const bool domain = execution.step(execution::Lane::Domain);
            if (!control && !domain) return;
        }
    }
    std::optional<execution::TaskId> startHeldInitialization()
    {
        if (!driver->initialize() || !execution.step(execution::Lane::Domain)) return {};
        return execution.dispatchNext(execution::Lane::Core);
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
        Fixture fixture; const auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->shutdown()); fixture.controlAndDomain();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.shutdowns, 0);
        QCOMPARE(fixture.driver->projection().phase, Phase::Draining);
        QVERIFY(fixture.execution.finishWork(*held)); fixture.settle();
        QCOMPARE(fixture.initializes, 1); QCOMPARE(fixture.shutdowns, 1);
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Stopped); QVERIFY(value.initialization); QVERIFY(value.initialization->success);
        QVERIFY(fixture.stopped_at_native_entry);
    }

    void interruptionRunsOnControlWhileInitializationIsHeld()
    {
        Fixture fixture; const auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->interrupt()); fixture.controlAndDomain();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.initializes, 0);
        QCOMPARE(fixture.driver->projection().phase, Phase::Draining);
        QCOMPARE(fixture.events(Event::Interrupted), 1);
        QVERIFY(fixture.execution.finishWork(*held)); fixture.settle();
    }

    void pendingOrdinaryWorkDoesNotBlockControlDecisionOrEffect()
    {
        Fixture fixture; const auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.execution.step(execution::Lane::Domain)); // Release original Domain-ingress record.
        int unrelated{0};
        for (int n = 0; n < 2; ++n) {
            QVERIFY(execution::Submit(*fixture.execution.scheduler, [&] { ++unrelated; }, [] {}, {},
                {execution::Lane::Core, fixture.execution.scheduler->allocateKey(), execution::WorkClass::Action}));
        }
        QVERIFY(fixture.driver->interrupt()); fixture.controlAndDomain();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(unrelated, 0); QCOMPARE(fixture.initializes, 0);
        QVERIFY(fixture.execution.finishWork(*held)); fixture.settle();
        QCOMPARE(unrelated, 2);
    }

    void ordinaryFenceRejectsNewStartButRetainsStopContinuation()
    {
        Fixture fixture;
        fixture.execution.scheduler->stopAcceptingWork();
        QVERIFY(!fixture.driver->initialize());
        QVERIFY(fixture.driver->shutdown()); fixture.settle();
        QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.shutdowns, 1);
        const auto status=fixture.driver->projection().core_exit_status;QVERIFY(status);QCOMPARE(*status,7);
    }

    void stopIngressFencesLateSuccessBeforeDomainControlReceipt()
    {
        Fixture fixture; const auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->interrupt());
        QVERIFY(fixture.execution.finishWork(*held));
        // Consume only Domain turns: interruption is admitted but its reserved
        // Native ingress has not executed. Initialization still cannot be Ready.
        while (fixture.execution.step(execution::Lane::Domain)) {}
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
        QVERIFY(value.diagnostic); QVERIFY(value.diagnostic_scope); QVERIFY(!value.core_exit_status);
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
        const auto held = fixture.startHeldInitialization(); QVERIFY(held);
        fixture.throw_interrupt = true;
        QVERIFY(fixture.driver->shutdown()); fixture.controlAndDomain();
        auto fatal = fixture.driver->projection();
        QCOMPARE(fatal.phase, Phase::Fatal); QVERIFY(fatal.diagnostic);
        const auto original = *fatal.diagnostic;
        QVERIFY(fixture.execution.finishWork(*held)); fixture.settle();
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
        Fixture fixture; fixture.execution.rejectLane(execution::Lane::Core);
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::StartupFailed); QVERIFY(value.initialization);
        QVERIFY(!value.initialization->attempted); QVERIFY(!value.initialization->success);
        QCOMPARE(fixture.initializes, 0);
    }

    void rejectedAcceptedInitializationStillCompletesPendingShutdown()
    {
        Fixture fixture; const auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->shutdown()); fixture.controlAndDomain();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.shutdowns, 0);
        fixture.execution.scheduler->stopAcceptingWork();
        QVERIFY(fixture.execution.scheduler->cancelQueued(*held));
        QVERIFY(fixture.execution.finishWork(*held)); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.shutdowns, 1);
        QVERIFY(value.initialization); QVERIFY(!value.initialization->attempted); QVERIFY(value.initialization->cancelled);
        QCOMPARE(value.phase, Phase::Stopped); QCOMPARE(value.core_exit_status, std::optional<int>{7});
    }

    void cancelledDomainIngressHasKnownUnattemptedOutcome()
    {
        Fixture fixture; QVERIFY(fixture.driver->initialize());
        const auto ingress = fixture.execution.dispatchNext(execution::Lane::Domain); QVERIFY(ingress);
        QVERIFY(fixture.execution.scheduler->cancelQueued(*ingress));
        QVERIFY(fixture.execution.finishWork(*ingress)); fixture.settle();
        const auto value = fixture.driver->projection();
        QVERIFY(value.initialization); QVERIFY(!value.initialization->attempted); QVERIFY(value.initialization->cancelled);
        QCOMPARE(fixture.initializes, 0); QCOMPARE(value.phase, Phase::Stopping);
    }

    void rejectedControlIngressIsExplicitAndCannotRestoreReadiness()
    {
        Fixture fixture; QVERIFY(fixture.driver->initialize()); fixture.settle();
        fixture.execution.rejectLane(execution::Lane::Control);
        QVERIFY(!fixture.driver->interrupt());
        QCOMPARE(fixture.driver->projection().phase, Phase::Stopping);
        QCOMPARE(fixture.interrupts, 0); QVERIFY(!fixture.driver->initialize());
    }

    void cancelledAcceptedControlIngressHasTerminalReceipt()
    {
        Fixture fixture; QVERIFY(fixture.driver->initialize()); fixture.settle();
        QVERIFY(fixture.driver->interrupt());
        const auto ingress = fixture.execution.dispatchNext(execution::Lane::Control); QVERIFY(ingress);
        QVERIFY(fixture.execution.scheduler->cancelQueued(*ingress));
        QVERIFY(fixture.execution.finishWork(*ingress)); fixture.settle();
        QCOMPARE(fixture.driver->projection().phase, Phase::Fatal);
        QCOMPARE(fixture.interrupts, 0); QCOMPARE(fixture.shutdowns, 0);
        QCOMPARE(fixture.events(Event::Fatal), 1);
    }

    void closeRetainsAcceptedNativeOutcomeAndReleasesCapabilities()
    {
        Fixture fixture; const auto held = fixture.startHeldInitialization(); QVERIFY(held);
        fixture.driver->close();
        QVERIFY(!fixture.driver->initialize()); QVERIFY(!fixture.driver->interrupt());
        QVERIFY(!fixture.driver->shutdown()); QVERIFY(!fixture.driver->retryPublish());
        QVERIFY(fixture.execution.finishWork(*held)); fixture.settle();
        const auto value = fixture.driver->projection();
        QVERIFY(value.initialization); QVERIFY(value.initialization->success);
        QCOMPARE(value.phase, Phase::Stopping); QCOMPARE(fixture.initializes, 1); QCOMPARE(fixture.shutdowns, 0);
        QVERIFY(fixture.execution.quiescent());
    }

    void acceptedDeferredShutdownOutlivesObserverClose()
    {
        Fixture fixture; const auto held = fixture.startHeldInitialization(); QVERIFY(held);
        QVERIFY(fixture.driver->shutdown()); fixture.controlAndDomain();
        QCOMPARE(fixture.interrupts, 1); QCOMPARE(fixture.shutdowns, 0);
        fixture.driver->close();
        QVERIFY(fixture.execution.finishWork(*held)); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(fixture.shutdowns, 1); QCOMPARE(value.phase, Phase::Stopped);
        QVERIFY(value.core_exit_status); QCOMPARE(*value.core_exit_status, 7);
        QVERIFY(fixture.execution.quiescent());
    }

    void fatalCloseReleasesMailboxBorrowersWhileFacadeRemainsAlive()
    {
        Fixture fixture; fixture.throw_initialize = true;
        QVERIFY(fixture.driver->initialize()); fixture.settle();
        fixture.publications.clear(); fixture.driver->close(); fixture.settle();
        const auto value = fixture.driver->projection();
        QCOMPARE(value.phase, Phase::Fatal); QVERIFY(!value.diagnostic); QVERIFY(!value.diagnostic_scope);
        QVERIFY(fixture.execution.quiescent());
    }

    void acceptedPortsRetireOnlyAfterNativeOutcome()
    {
        struct Captured { int* retired; ~Captured() { ++*retired; } };
        ManualRoutedExecution execution;
        int retired{0}, initialized{0};
        auto captured = std::make_shared<Captured>(); captured->retired = &retired;
        std::weak_ptr<Captured> lifetime = captured;
        Effects effects;
        effects.initialize = [captured, &initialized](const std::atomic_bool&) {
            ++initialized; NativeInitialization value; value.attempted = true; value.success = true; return value;
        };
        effects.interrupt = [captured] {};
        effects.shutdown = [captured] { return 0; };
        auto driver = std::make_unique<Driver>(execution::SnapshotServices{execution.scheduler, {}}, std::move(effects), Driver::Publish{});
        // std::function's moved-from small target may retain its captures on
        // libc++. Release the caller's port aliases before testing retirement.
        effects = {};
        captured.reset();
        QVERIFY(driver->initialize()); QVERIFY(execution.step(execution::Lane::Domain));
        const auto held = execution.dispatchNext(execution::Lane::Core); QVERIFY(held);
        driver.reset(); QVERIFY(!lifetime.expired()); QCOMPARE(retired, 0);
        QVERIFY(execution.finishWork(*held)); execution.drain();
        QCOMPARE(initialized, 1); QVERIFY(lifetime.expired()); QCOMPARE(retired, 1); QVERIFY(execution.quiescent());
    }

    void missingRequiredPortsAreRejectedBeforeEffects_data()
    {
        QTest::addColumn<int>("missing");
        QTest::newRow("scheduler") << 0; QTest::newRow("initialize") << 1;
        QTest::newRow("interrupt") << 2; QTest::newRow("shutdown") << 3;
    }
    void missingRequiredPortsAreRejectedBeforeEffects()
    {
        QFETCH(int, missing); ManualRoutedExecution execution; int calls{0};
        execution::SnapshotServices services{execution.scheduler, {}};
        Effects effects;
        effects.initialize = [&](const std::atomic_bool&) { ++calls; return NativeInitialization{}; };
        effects.interrupt = [&] { ++calls; }; effects.shutdown = [&] { ++calls; return 0; };
        if (missing == 0) services.scheduler.reset();
        if (missing == 1) effects.initialize = {};
        if (missing == 2) effects.interrupt = {};
        if (missing == 3) effects.shutdown = {};
        bool rejected{false};
        try { Driver driver{std::move(services), std::move(effects), {}}; }
        catch (const std::invalid_argument&) { rejected = true; }
        QVERIFY(rejected); QCOMPARE(calls, 0); QVERIFY(execution.quiescent());
    }

    void closedOwnerReservationDoesNotAdmitEffects()
    {
        Fixture fixture{true};
        QCOMPARE(fixture.driver->projection().phase, Phase::Fatal);
        QVERIFY(!fixture.driver->initialize()); QVERIFY(!fixture.driver->interrupt()); QVERIFY(!fixture.driver->shutdown());
        fixture.settle(); QCOMPARE(fixture.initializes, 0); QCOMPARE(fixture.interrupts, 0); QCOMPARE(fixture.shutdowns, 0);
        QVERIFY(fixture.execution.quiescent());
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NodeLifecycleWorkerTests)
#else
QTEST_MAIN(NodeLifecycleWorkerTests)
#endif
#include <test_nodelifecycleworker.moc>
