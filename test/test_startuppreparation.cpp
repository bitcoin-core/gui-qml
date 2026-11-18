// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/executor.h>
#include <qml/startuppreparation.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>
#include <QCoreApplication>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <deque>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

namespace {
using namespace startup::preparation;
using Lane = execution::Lane;
struct Trace {
    Lane lane{Lane::Utility};
    QStringList calls;
    int captures_destroyed{0};
    bool lanes_correct{true}, cleanup_correct{true};
    void call(const QString& name, Lane expected) { calls << name; lanes_correct &= lane == expected; }
};
struct NativeCapture {
    std::shared_ptr<Trace> trace;
    Lane cleanup_lane;
    ~NativeCapture() { ++trace->captures_destroyed; trace->cleanup_correct &= trace->lane == cleanup_lane; }
};
struct Fixture {
    ManualStartupExecution shared;
    ManualStartupExecution endpoint_worker;
    const std::shared_ptr<Trace> trace{std::make_shared<Trace>()};
    std::unique_ptr<Coordinator> coordinator;
    std::shared_ptr<const Result> received;
    std::deque<CompleteTransport> transport_callbacks;
    CompleteTransport duplicate_transport;
    Done duplicate_retire;
    bool endpoint_retired{false}, retire_before_registration{false};
    bool reject_start{false}, reject_retire{false}, throw_start{false}, throw_retire{false};
    bool cancel_before_read{false}, throw_read{false}, throw_prepare{false}, cancel_from_prepare{false}, throw_completion{false}, post_summary{false};
    Disposition summary{Disposition::Failed};
    bool summary_native_started{false};
    int summary_deliveries{0};
    int reads{0}, starts{0}, preparations{0}, retire_requests{0}, retire_effects{0}, completions{0};
    Profile profile{QStringLiteral("/original/profile"), QStringLiteral("regtest")};
    Profile observed_start, observed_prepare;
    startup::PreparationOutcome native_outcome{startup::PreparationOutcome::Ready};
    std::deque<std::function<void()>> gui;

    Fixture()
    {
        auto capture = std::make_shared<NativeCapture>(trace, Lane::Utility);
        Ports ports;
        ports.read_profile = [this, capture] {
            (void)capture; // Native capture ownership is an independent retirement oracle.
            if (cancel_before_read) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            trace->call(QStringLiteral("read"), Lane::Core); ++reads;
            if (throw_read) throw std::runtime_error{"original read error"};
            return profile;
        };
        ports.start_transport = [this](Profile original, CompleteTransport done) {
            trace->call(QStringLiteral("start"), Lane::Utility); ++starts; observed_start = std::move(original);
            if (throw_start) throw std::runtime_error{"original start error"};
            if (reject_start) return execution::SubmitResult{{}, execution::Rejection::Capacity};
            const bool accepted = endpoint_worker.core.post([this, done = std::move(done)] {
                trace->call(QStringLiteral("transport-native"), Lane::Core);
                duplicate_transport = done;
                transport_callbacks.push_back(done);
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        ports.prepare_native = [this](Profile original) {
            trace->call(QStringLiteral("prepare"), Lane::Core); ++preparations; observed_prepare = std::move(original);
            if (throw_prepare) throw std::runtime_error{"original preparation error"};
            if (cancel_from_prepare) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            return native_outcome;
        };
        ports.retire_transport = [this](Done done) {
            trace->call(QStringLiteral("retire"), Lane::Utility); ++retire_requests;
            if (retire_before_registration) {
                endpoint_retired = true;
                return execution::SubmitResult{{}, execution::Rejection::Closed};
            }
            if (throw_retire) throw std::runtime_error{"original stop error"};
            if (reject_retire) return execution::SubmitResult{{}, execution::Rejection::Capacity};
            const bool accepted = endpoint_worker.core.post([this, done = std::move(done)] {
                trace->call(QStringLiteral("physical-retire"), Lane::Core); ++retire_effects; endpoint_retired = true;
                duplicate_retire = done;
                done();
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        ports.transport_retired = [this] { trace->call(QStringLiteral("retired?"), Lane::Utility); return endpoint_retired; };
        coordinator = std::make_unique<Coordinator>(shared.executor(), std::move(ports));
    }
    ~Fixture() { dispose(); }
    bool step(Lane lane)
    {
        const auto previous = std::exchange(trace->lane, lane);
        const bool ran = shared.step(lane) || endpoint_worker.step(lane);
        trace->lane = previous;
        return ran;
    }

    void drain()
    {
        for (int turn = 0; turn < 512; ++turn) {
            if (!step(Lane::Utility) && !step(Lane::Core)) break;
        }
    }

    bool quiescent() const { return shared.quiescent() && endpoint_worker.quiescent(); }

    execution::SubmitResult start(Context context = {})
    {
        return coordinator->start(std::move(context), [this](std::shared_ptr<const Result> result) {
            trace->call(QStringLiteral("completed"), Lane::Utility); ++completions;
            if (throw_completion) throw std::runtime_error{"observer publication failed"};
            if (post_summary) {
                const auto disposition = result->disposition;
                const bool attempted = result->native_started;
                gui.emplace_back([this, disposition, attempted] { summary = disposition; summary_native_started = attempted; ++summary_deliveries; });
            }
            received = std::move(result);
        });
    }
    void deliver(transport::StartStatus status, QString error = {}, desktopintent::Disposition disposition = desktopintent::Disposition::Accepted)
    {
        QVERIFY(!transport_callbacks.empty());
        auto callback = std::move(transport_callbacks.front()); transport_callbacks.pop_front();
        transport::StartResult receipt{status, std::move(error), {{71, 37, disposition}}, {QStringLiteral("original-producer:71")}};
        QVERIFY(endpoint_worker.outcomes.post([callback = std::move(callback), receipt = std::move(receipt)]() mutable {
            callback(std::move(receipt));
        }));
    }
    void dispose()
    {
        if (coordinator) coordinator->close();
        drain();
        while (!transport_callbacks.empty()) { deliver(transport::StartStatus::Unknown); drain(); }
        if (coordinator && coordinator->phase() == Phase::RetirementBlocked) {
            reject_retire = throw_retire = false;
            (void)coordinator->retryRetirement(); drain();
        }
        coordinator.reset(); received.reset(); duplicate_transport = {}; duplicate_retire = {};
        endpoint_worker.core.close();
        endpoint_worker.outcomes.close();
        drain();
    }
};
Context Saved()
{
    QmlOnboardingSettings::ProfileResult receipt;
    receipt.completed = receipt.replacement_written = receipt.directory_saved = receipt.cleanup_completed = true;
    return {receipt};
}
} // namespace

class StartupPreparationTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void primaryConsumesNativeReadBeforeStartingTransportAndPreparation()
    {
        Fixture fixture;
        QVERIFY(fixture.start(Saved()));
        QCOMPARE(fixture.reads, 0); QCOMPARE(fixture.starts, 0);
        QVERIFY(fixture.step(Lane::Utility));
        QCOMPARE(fixture.reads, 0);
        QVERIFY(fixture.step(Lane::Core));
        QCOMPARE(fixture.reads, 1); QCOMPARE(fixture.starts, 0);
        fixture.drain();
        QCOMPARE(fixture.starts, 1); QCOMPARE(fixture.preparations, 0);
        QVERIFY(fixture.observed_start == fixture.profile);
        fixture.deliver(transport::StartStatus::Primary);
        QVERIFY(fixture.step(Lane::Utility));
        QCOMPARE(fixture.preparations, 0);
        fixture.drain();
        QVERIFY(fixture.received);
        QCOMPARE(fixture.received->disposition, Disposition::Ready);
        QVERIFY(fixture.received->native); QCOMPARE(*fixture.received->native, startup::PreparationOutcome::Ready);
        QVERIFY(fixture.received->native_started); QVERIFY(!fixture.received->endpoint_retired);
        QVERIFY(!fixture.received->cancelled);
        QVERIFY(fixture.received->saved_profile->completed);
        QCOMPARE(fixture.received->transport->receipts.front().inspection, uint64_t{37});
        QVERIFY(fixture.observed_prepare == fixture.profile);
        QCOMPARE(fixture.reads, 1); QCOMPARE(fixture.starts, 1); QCOMPARE(fixture.preparations, 1);
        QCOMPARE(fixture.retire_requests, 0); QCOMPARE(fixture.completions, 1);
        QVERIFY(fixture.trace->lanes_correct);
    }
    void nonPrimaryPreservesOriginalReceiptAndWaitsForPhysicalRetirement_data()
    {
        QTest::addColumn<int>("status"); QTest::addColumn<int>("disposition"); QTest::addColumn<int>("request_disposition");
        QTest::newRow("forwarded") << int(transport::StartStatus::Forwarded) << int(Disposition::Forwarded) << int(desktopintent::Disposition::Accepted);
        QTest::newRow("forwarded-known-dropped-on-stop") << int(transport::StartStatus::Forwarded) << int(Disposition::Forwarded) << int(desktopintent::Disposition::DroppedOnStop);
        QTest::newRow("rejected") << int(transport::StartStatus::Rejected) << int(Disposition::Failed) << int(desktopintent::Disposition::Accepted);
        QTest::newRow("unknown") << int(transport::StartStatus::Unknown) << int(Disposition::Uncertain) << int(desktopintent::Disposition::Accepted);
    }
    void nonPrimaryPreservesOriginalReceiptAndWaitsForPhysicalRetirement()
    {
        QFETCH(int, status); QFETCH(int, disposition); QFETCH(int, request_disposition);
        Fixture fixture; QVERIFY(fixture.start(Saved())); fixture.drain();
        fixture.deliver(static_cast<transport::StartStatus>(status), QStringLiteral("original transport diagnostic"), static_cast<desktopintent::Disposition>(request_disposition));
        while (fixture.step(Lane::Utility)) {}
        QCOMPARE(fixture.retire_requests, 1); QCOMPARE(fixture.retire_effects, 0);
        QCOMPARE(fixture.completions, 0); QVERIFY(!fixture.coordinator->result());
        fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, static_cast<Disposition>(disposition));
        QCOMPARE(fixture.received->error, QStringLiteral("original transport diagnostic"));
        QCOMPARE(fixture.received->transport->status, static_cast<transport::StartStatus>(status));
        QCOMPARE(fixture.received->transport->receipts.front().delivery, uint64_t{71});
        QCOMPARE(fixture.received->transport->receipts.front().disposition, static_cast<desktopintent::Disposition>(request_disposition));
        QCOMPARE(fixture.received->transport->producer_keys, std::vector<QString>{QStringLiteral("original-producer:71")});
        QVERIFY(fixture.received->saved_profile->replacement_written);
        QVERIFY(fixture.received->endpoint_retired); QVERIFY(!fixture.received->native_started);
        QCOMPARE(fixture.preparations, 0); QCOMPARE(fixture.completions, 1);
    }
    void transportRetiringBeforeCallbackRegistrationCompletesStartup()
    {
        Fixture fixture;
        fixture.retire_before_registration = true;
        QVERIFY(fixture.start(Saved()));
        fixture.drain();
        fixture.deliver(transport::StartStatus::Unknown, QStringLiteral("original transport diagnostic"));
        fixture.drain();
        QCOMPARE(fixture.coordinator->phase(), Phase::Completed);
        QVERIFY(fixture.received);
        QCOMPARE(fixture.received->disposition, Disposition::Uncertain);
        QCOMPARE(fixture.received->error, QStringLiteral("original transport diagnostic"));
        QVERIFY(fixture.received->endpoint_retired);
        QCOMPARE(fixture.retire_requests, 1);
        QCOMPARE(fixture.completions, 1);
        QCOMPARE(fixture.preparations, 0);
    }
    void savedPartialOrUnknownNeverStartsAnotherHandoff_data()
    {
        QTest::addColumn<bool>("unknown"); QTest::addColumn<bool>("closing");
        QTest::newRow("known-partial") << false << false; QTest::newRow("unknown-original") << true << false;
        QTest::newRow("known-partial-before-close") << false << true; QTest::newRow("unknown-original-before-close") << true << true;
    }
    void savedPartialOrUnknownNeverStartsAnotherHandoff()
    {
        QFETCH(bool, unknown); QFETCH(bool, closing);
        Fixture fixture; auto context = Saved(); context.saved_profile->completed = false; context.saved_profile->uncertain = unknown;
        QVERIFY(fixture.start(context));
        if (closing) fixture.coordinator->close();
        fixture.drain();
        QCOMPARE(fixture.reads, 0); QCOMPARE(fixture.starts, 0); QCOMPARE(fixture.preparations, 0);
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, unknown ? Disposition::Uncertain : Disposition::Failed);
        QVERIFY(fixture.received->saved_profile->replacement_written);
        QCOMPARE(fixture.received->saved_profile->uncertain, unknown);
        QCOMPARE(fixture.received->closing, closing); QVERIFY(!fixture.received->cancelled);
    }
    void failuresKeepKnownProfileAndTruthfulAttemptFacts_data()
    {
        QTest::addColumn<int>("failure");
        QTest::newRow("read-exception") << 0;
        QTest::newRow("transport-rejected") << 1;
        QTest::newRow("transport-exception") << 2;
        QTest::newRow("preparation-known-failure") << 3;
        QTest::newRow("preparation-exception") << 4;
        QTest::newRow("read-submitted-rejected") << 5;
        QTest::newRow("preparation-submitted-rejected") << 6;
    }
    void failuresKeepKnownProfileAndTruthfulAttemptFacts()
    {
        QFETCH(int, failure); Fixture fixture;
        fixture.throw_read = failure == 0; fixture.reject_start = failure == 1; fixture.throw_start = failure == 2;
        fixture.native_outcome = failure == 3 ? startup::PreparationOutcome::BaseInitializationFailed : startup::PreparationOutcome::Ready;
        fixture.throw_prepare = failure == 4;
        if (failure == 5) fixture.shared.reject_core = true;
        QVERIFY(fixture.start(Saved())); fixture.drain();
        if (failure == 3 || failure == 4 || failure == 6) {
            if (failure == 6) fixture.shared.reject_core = true;
            fixture.deliver(transport::StartStatus::Primary); fixture.drain();
        }
        QVERIFY(fixture.received);
        QCOMPARE(fixture.received->disposition, failure == 2 || failure == 4 ? Disposition::Uncertain : Disposition::Failed);
        QCOMPARE(fixture.received->native_started, failure == 3 || failure == 4);
        QVERIFY(!fixture.received->cancelled);
        QVERIFY(fixture.received->saved_profile->completed); QVERIFY(fixture.received->endpoint_retired);
        QCOMPARE(fixture.preparations, failure == 3 || failure == 4 ? 1 : 0);
        QVERIFY(!fixture.received->error.isEmpty()); QCOMPARE(fixture.completions, 1);
    }
    void closeBeforeOrAfterNativeReadNeverStartsTransport_data()
    {
        QTest::addColumn<int>("read_stage");
        QTest::newRow("before-first-phase") << 0; QTest::newRow("physical-read-before-domain-consumption") << 2;
        QTest::newRow("queued-native-read") << 1;
    }
    void closeBeforeOrAfterNativeReadNeverStartsTransport()
    {
        QFETCH(int, read_stage); Fixture fixture; QVERIFY(fixture.start(Saved()));
        if (read_stage > 0) QVERIFY(fixture.step(Lane::Utility));
        if (read_stage == 2) QVERIFY(fixture.step(Lane::Core));
        fixture.coordinator->close(); fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, Disposition::Failed);
        QVERIFY(fixture.received->closing); QVERIFY(fixture.received->endpoint_retired);
        QVERIFY(fixture.received->cancelled);
        QVERIFY(fixture.received->saved_profile->completed); QVERIFY(!fixture.received->native_started);
        QCOMPARE(fixture.reads, read_stage == 2 ? 1 : 0); QCOMPARE(fixture.starts, 0); QCOMPARE(fixture.preparations, 0);
        if (read_stage == 2) { QVERIFY(fixture.received->profile); QVERIFY(*fixture.received->profile == fixture.profile); }
        QCOMPARE(fixture.completions, 1);
    }
    void closeFencesQueuedNativePreparationButRetainsLatePrimaryReceipt()
    {
        Fixture fixture; QVERIFY(fixture.start()); fixture.drain();
        fixture.deliver(transport::StartStatus::Primary);
        QVERIFY(fixture.step(Lane::Utility));
        QVERIFY(fixture.step(Lane::Utility));
        fixture.coordinator->close();
        fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, Disposition::Failed);
        QVERIFY(fixture.received->closing); QVERIFY(fixture.received->endpoint_retired);
        QVERIFY(fixture.received->cancelled);
        QCOMPARE(fixture.received->transport->status, transport::StartStatus::Primary);
        QVERIFY(!fixture.received->native_started); QCOMPARE(fixture.preparations, 0);
    }
    void closeWhileStartIsPendingRetiresAndAccountsTheOriginalLateReply()
    {
        Fixture fixture; QVERIFY(fixture.start(Saved())); fixture.drain();
        fixture.coordinator->close(); fixture.drain();
        QVERIFY(fixture.endpoint_retired); QCOMPARE(fixture.completions, 0);
        fixture.deliver(transport::StartStatus::Primary); fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, Disposition::Failed);
        QCOMPARE(fixture.received->transport->receipts.size(), size_t{1});
        QVERIFY(fixture.received->saved_profile->completed); QCOMPARE(fixture.preparations, 0);
        QVERIFY(fixture.received->cancelled);
    }
    void nativeReadyAfterCloseRemainsReadyAndDoesNotRetireAheadOfOutcome()
    {
        Fixture fixture; QVERIFY(fixture.start()); fixture.drain();
        fixture.deliver(transport::StartStatus::Primary);
        QVERIFY(fixture.step(Lane::Utility));
        QVERIFY(fixture.step(Lane::Utility));
        QVERIFY(fixture.step(Lane::Core));
        QCOMPARE(fixture.preparations, 1); QCOMPARE(fixture.completions, 0);
        fixture.coordinator->close();
        QCOMPARE(fixture.retire_requests, 0);
        fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, Disposition::Ready);
        QVERIFY(fixture.received->native_started); QVERIFY(fixture.received->closing); QVERIFY(fixture.received->endpoint_retired);
        QVERIFY(!fixture.received->cancelled);
        QCOMPARE(fixture.completions, 1);
    }
    void closeAfterNativeFailureBeforeDomainConsumptionIsNotCancellation_data()
    {
        QTest::addColumn<int>("failure");
        QTest::newRow("known-native-failure") << 0;
        QTest::newRow("entered-native-exception") << 1;
        QTest::newRow("entered-port-throws-cancelled") << 2;
    }
    void closeAfterNativeFailureBeforeDomainConsumptionIsNotCancellation()
    {
        QFETCH(int, failure); Fixture fixture;
        fixture.native_outcome = startup::PreparationOutcome::BaseInitializationFailed;
        fixture.throw_prepare = failure == 1; fixture.cancel_from_prepare = failure == 2;
        QVERIFY(fixture.start(Saved())); fixture.drain();
        fixture.deliver(transport::StartStatus::Primary);
        QVERIFY(fixture.step(Lane::Utility));
        QVERIFY(fixture.step(Lane::Utility)); QVERIFY(fixture.step(Lane::Core));
        QCOMPARE(fixture.preparations, 1); QCOMPARE(fixture.completions, 0);
        fixture.coordinator->close(); fixture.drain();
        QVERIFY(fixture.received);
        QCOMPARE(fixture.received->disposition, failure == 0 ? Disposition::Failed : Disposition::Uncertain);
        QVERIFY(fixture.received->closing); QVERIFY(!fixture.received->cancelled);
        QVERIFY(fixture.received->native_started); QVERIFY(fixture.received->endpoint_retired);
        QVERIFY(fixture.received->saved_profile->completed); QVERIFY(!fixture.received->error.isEmpty());
        if (failure == 0) QCOMPARE(fixture.received->native, std::optional{startup::PreparationOutcome::BaseInitializationFailed});
        else QVERIFY(!fixture.received->native);
        QCOMPARE(fixture.received->transport->receipts.front().delivery, uint64_t{71});
        QCOMPARE(fixture.completions, 1); QCOMPARE(fixture.preparations, 1);
        fixture.dispose(); QVERIFY(fixture.quiescent());
    }
    void authoritativeNotStartedCancellationRequiresClosing_data()
    {
        QTest::addColumn<int>("phase"); QTest::addColumn<bool>("closing");
        QTest::newRow("domain-cancelled-while-closing") << 0 << true;
        QTest::newRow("read-cancelled-while-closing") << 1 << true;
        QTest::newRow("preparation-cancelled-while-closing") << 2 << true;
        QTest::newRow("domain-cancelled-without-close") << 0 << false;
        QTest::newRow("read-cancelled-without-close") << 1 << false;
        QTest::newRow("preparation-cancelled-without-close") << 2 << false;
    }
    void authoritativeNotStartedCancellationRequiresClosing()
    {
        QFETCH(int, phase); QFETCH(bool, closing); Fixture fixture;
        QVERIFY(fixture.start(Saved()));
        if (phase == 1) {
            QVERIFY(fixture.step(Lane::Utility));
            fixture.cancel_before_read = !closing;
        }
        if (phase == 2) {
            fixture.drain();
            fixture.deliver(transport::StartStatus::Primary);
            QVERIFY(fixture.step(Lane::Utility));
            if (closing) QVERIFY(fixture.step(Lane::Utility));
        }
        if (closing) {
            // Close fences the next accepted turn at its original stage.
            QVERIFY(fixture.shared.hasWork(phase == 0 ? Lane::Utility : Lane::Core));
            fixture.coordinator->close();
        } else if (phase != 1) {
            // A refused Core dispatch reports a failure with no native effect.
            // SerialQueue does not withdraw an accepted origin without close.
            fixture.shared.reject_core = true;
        }
        fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, Disposition::Failed);
        QCOMPARE(fixture.received->closing, closing); QCOMPARE(fixture.received->cancelled, closing);
        QVERIFY(!fixture.received->native_started); QVERIFY(!fixture.received->native);
        QVERIFY(fixture.received->endpoint_retired); QVERIFY(fixture.received->saved_profile->completed);
        QCOMPARE(fixture.reads, phase == 2 ? 1 : 0); QCOMPARE(fixture.starts, phase == 2 ? 1 : 0);
        QCOMPARE(fixture.preparations, 0); QCOMPARE(fixture.completions, 1);
        fixture.dispose(); QVERIFY(fixture.quiescent());
    }
    void publishedReadyHasImmutableOriginalReceiptDuringLaterStop()
    {
        Fixture fixture; QVERIFY(fixture.start()); fixture.drain(); fixture.deliver(transport::StartStatus::Primary); fixture.drain();
        const auto original = fixture.received; QVERIFY(original);
        fixture.endpoint_worker.outcomes.close();
        fixture.coordinator->close(); fixture.drain();
        const auto stopped = fixture.coordinator->result(); QVERIFY(stopped);
        QCOMPARE(stopped->disposition, Disposition::Ready); QVERIFY(stopped->closing); QVERIFY(stopped->endpoint_retired);
        QVERIFY(!original->cancelled); QVERIFY(!stopped->cancelled);
        QVERIFY(!original->closing); QVERIFY(!original->endpoint_retired);
        QCOMPARE(stopped->transport->receipts.front().inspection, original->transport->receipts.front().inspection);
        QCOMPARE(fixture.completions, 1); QCOMPARE(fixture.preparations, 1);
    }
    void readyStopControlRejectionCanRetryWithoutChangingTheOriginalOutcome()
    {
        Fixture fixture; QVERIFY(fixture.start()); fixture.drain(); fixture.deliver(transport::StartStatus::Primary); fixture.drain();
        fixture.endpoint_worker.outcomes.close();
        fixture.shared.reject_utility = true;
        fixture.coordinator->close();
        QCOMPARE(fixture.coordinator->phase(), Phase::RetirementBlocked);
        QCOMPARE(fixture.retire_requests, 0); QCOMPARE(fixture.coordinator->result()->disposition, Disposition::Ready);
        QVERIFY(!fixture.coordinator->result()->endpoint_retired);
        QVERIFY(fixture.coordinator->retryRetirement()); fixture.drain();
        QCOMPARE(fixture.coordinator->result()->disposition, Disposition::Ready);
        QVERIFY(fixture.coordinator->result()->closing); QVERIFY(fixture.coordinator->result()->endpoint_retired);
        QCOMPARE(fixture.completions, 1); QCOMPARE(fixture.preparations, 1); QCOMPARE(fixture.starts, 1);
    }
    void rejectedRetirementNeedsExplicitOriginalRecovery_data()
    {
        QTest::addColumn<bool>("throwing"); QTest::newRow("rejected") << false; QTest::newRow("throwing") << true;
    }
    void rejectedRetirementNeedsExplicitOriginalRecovery()
    {
        QFETCH(bool, throwing); Fixture fixture;
        fixture.reject_retire = !throwing; fixture.throw_retire = throwing;
        QVERIFY(fixture.start(Saved())); fixture.drain(); fixture.deliver(transport::StartStatus::Unknown); fixture.drain();
        QCOMPARE(fixture.coordinator->phase(), Phase::RetirementBlocked);
        QVERIFY(!fixture.coordinator->result()); QCOMPARE(fixture.completions, 0); QCOMPARE(fixture.retire_effects, 0);
        fixture.reject_retire = fixture.throw_retire = false;
        fixture.endpoint_worker.outcomes.close();
        QVERIFY(fixture.coordinator->retryRetirement()); fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, Disposition::Uncertain);
        QVERIFY(fixture.received->endpoint_retired); QVERIFY(fixture.received->saved_profile->completed);
        QCOMPARE(fixture.starts, 1); QCOMPARE(fixture.preparations, 0); QCOMPARE(fixture.retire_effects, 1);
    }
    void duplicateTransportAndRetirementCallbacksOwnNoSuccessorOrCapability()
    {
        Fixture fixture; QVERIFY(fixture.start()); fixture.drain(); fixture.deliver(transport::StartStatus::Forwarded); fixture.drain();
        QVERIFY(fixture.duplicate_transport && fixture.duplicate_retire);
        QVERIFY(fixture.endpoint_worker.outcomes.post([&] {
            fixture.duplicate_transport({transport::StartStatus::Primary, QStringLiteral("late contradictory reply"), {}});
            fixture.duplicate_retire();
        }));
        fixture.drain();
        QCOMPARE(fixture.received->disposition, Disposition::Forwarded); QCOMPARE(fixture.completions, 1); QCOMPARE(fixture.preparations, 0);
        fixture.dispose(); QVERIFY(fixture.quiescent());
        QCOMPARE(fixture.trace->captures_destroyed, 1); QVERIFY(fixture.trace->cleanup_correct);
    }
    void facadeLossPreservesAcceptedChainAndNativePortRetirement()
    {
        Fixture fixture; QVERIFY(fixture.start(Saved())); fixture.drain();
        fixture.coordinator.reset();
        QCOMPARE(fixture.trace->captures_destroyed, 0);
        fixture.deliver(transport::StartStatus::Primary); fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, Disposition::Ready);
        QCOMPARE(fixture.preparations, 1); QCOMPARE(fixture.retire_requests, 0);
        QCOMPARE(fixture.trace->captures_destroyed, 1); QVERIFY(fixture.trace->cleanup_correct);
        fixture.dispose(); QVERIFY(fixture.quiescent());
    }
    void sharedDrainWaitsForLateReplyAndResultRelease()
    {
        int drained{0};
        Fixture fixture;
        QVERIFY(fixture.start(Saved()));
        fixture.drain();
        fixture.coordinator->close();
        fixture.coordinator->whenDrained([&] { ++drained; });
        fixture.coordinator.reset();
        fixture.drain();
        QVERIFY(fixture.endpoint_retired);
        QCOMPARE(fixture.completions, 0);
        QCOMPARE(drained, 0);

        fixture.deliver(transport::StartStatus::Primary);
        fixture.drain();
        QVERIFY(fixture.received);
        QVERIFY(fixture.received->saved_profile->completed);
        QVERIFY(fixture.received->cancelled);
        QCOMPARE(fixture.preparations, 0);
        QCOMPARE(fixture.completions, 1);
        QCOMPARE(drained, 0);
        QCOMPARE(fixture.trace->captures_destroyed, 1);
        QVERIFY(fixture.trace->cleanup_correct);

        fixture.received.reset();
        QCOMPARE(drained, 0);
        fixture.drain();
        QCOMPARE(drained, 1);
        QVERIFY(fixture.shared.quiescent());
        QVERIFY(fixture.duplicate_transport);
        fixture.duplicate_transport({transport::StartStatus::Primary, {}, {}});
        fixture.drain();
        QCOMPARE(fixture.completions, 1);
        QCOMPARE(drained, 1);
    }
    void throwingObserverCannotEraseOrReplayNativeOutcome()
    {
        Fixture fixture; fixture.throw_completion = true;
        QVERIFY(fixture.start()); fixture.drain(); fixture.deliver(transport::StartStatus::Primary); fixture.drain();
        QVERIFY(fixture.coordinator->result()); QCOMPARE(fixture.coordinator->result()->disposition, Disposition::Ready);
        QCOMPARE(fixture.completions, 1); QCOMPARE(fixture.preparations, 1);
        QVERIFY(!fixture.start()); fixture.drain(); QCOMPARE(fixture.preparations, 1);
    }
    void deferredGuiScalarProjectionHoldsNoNativeCapability()
    {
        Fixture fixture; fixture.post_summary = true;
        QVERIFY(fixture.start(Saved())); fixture.drain(); fixture.deliver(transport::StartStatus::Primary); fixture.drain();
        QCOMPARE(fixture.gui.size(), size_t{1}); QCOMPARE(fixture.summary_deliveries, 0);
        fixture.dispose();
        QVERIFY(fixture.quiescent()); QCOMPARE(fixture.trace->captures_destroyed, 1);
        auto delivery = std::move(fixture.gui.front()); fixture.gui.pop_front(); delivery();
        QCOMPARE(fixture.summary, Disposition::Ready); QVERIFY(fixture.summary_native_started);
        QCOMPARE(fixture.summary_deliveries, 1); QCOMPARE(fixture.preparations, 1);
    }
    void everyRequiredPortIsCheckedBeforeEffects_data()
    {
        QTest::addColumn<int>("missing");
        QTest::newRow("scheduler") << 0; QTest::newRow("read") << 1; QTest::newRow("transport") << 2;
        QTest::newRow("prepare") << 3; QTest::newRow("retire") << 4; QTest::newRow("retired-query") << 5;
    }
    void everyRequiredPortIsCheckedBeforeEffects()
    {
        QFETCH(int, missing); ManualStartupExecution execution;
        int effects{0};
        Ports ports{[&] { ++effects; return Profile{}; },
            [&](Profile, CompleteTransport) { ++effects; return execution::SubmitResult{}; },
            [&](Profile) { ++effects; return startup::PreparationOutcome::Ready; },
            [&](Done) { ++effects; return execution::SubmitResult{}; }, [&] { ++effects; return true; }};
        switch (missing) {
        case 0: execution.executor().stopAndJoin(); break; case 1: ports.read_profile = {}; break;
        case 2: ports.start_transport = {}; break; case 3: ports.prepare_native = {}; break;
        case 4: ports.retire_transport = {}; break; case 5: ports.transport_retired = {}; break;
        }
        bool rejected{false};
        try {
            Coordinator coordinator{execution.executor(), std::move(ports)};
            rejected = !coordinator.start({}, [&](auto) { ++effects; });
        } catch (const std::invalid_argument&) { rejected = true; }
        execution.drain();
        QVERIFY(rejected); QCOMPARE(effects, 0); QVERIFY(execution.quiescent());
    }
    void ordinaryClosureRejectsANewOriginBeforeAnyRead()
    {
        Fixture fixture; fixture.shared.reject_utility = true;
        QVERIFY(!fixture.start(Saved())); fixture.drain();
        QCOMPARE(fixture.reads, 0); QCOMPARE(fixture.starts, 0); QCOMPARE(fixture.preparations, 0); QCOMPARE(fixture.completions, 0);
    }
    void concurrentStartAndCloseKeepTheOriginalSavedReceiptAndDrain()
    {
        // The first two schedules establish both public dispositions; the
        // remaining sixteen release different callers together. A read may
        // enter, but cannot finish before the original close has returned.
        for (int schedule = 0; schedule < 18; ++schedule) {
            execution::Executor shared{execution::Executor::Config{}};
            const auto transport = std::make_shared<execution::SerialQueue>(shared.makeQueue(Lane::Core));
            struct ConcurrentTrace {
                QSemaphore release_read;
                std::atomic_int reads{0}, starts{0}, preparations{0}, completions{0}, retire_effects{0}, captures_destroyed{0};
                std::atomic_bool endpoint_retired{false}, off_gui{true}, saved_facts{false}, terminal_facts{false}, cleanup_off_gui{true};
            };
            struct Captured {
                std::shared_ptr<ConcurrentTrace> trace;
                ~Captured() {
                    trace->cleanup_off_gui = QThread::currentThread() != QCoreApplication::instance()->thread();
                    ++trace->captures_destroyed;
                }
            };
            const auto trace = std::make_shared<ConcurrentTrace>();
            auto capture = std::make_shared<Captured>(); capture->trace = trace;
            const auto off_gui = [trace] { if (QThread::currentThread() == QCoreApplication::instance()->thread()) trace->off_gui = false; };
            Ports ports;
            ports.read_profile = [trace, capture, off_gui] {
                (void)capture;
                off_gui(); ++trace->reads; trace->release_read.acquire();
                return Profile{QStringLiteral("/original/profile"), QStringLiteral("regtest")};
            };
            ports.start_transport = [trace, off_gui](Profile, CompleteTransport) {
                off_gui(); ++trace->starts;
                return execution::SubmitResult{{}, execution::Rejection::Invalid};
            };
            ports.prepare_native = [trace, off_gui](Profile) {
                off_gui(); ++trace->preparations; return startup::PreparationOutcome::Ready;
            };
            ports.retire_transport = [trace, off_gui, transport](Done done) {
                off_gui();
                const bool accepted = transport->post([trace, off_gui, done = std::move(done)] {
                    off_gui(); ++trace->retire_effects; trace->endpoint_retired = true;
                    done();
                });
                return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
            };
            ports.transport_retired = [trace, off_gui] { off_gui(); return trace->endpoint_retired.load(); };
            auto coordinator = std::make_unique<Coordinator>(shared, std::move(ports));
            const auto drained = std::make_shared<std::atomic_bool>(false);
            coordinator->whenDrained([drained] { *drained = true; });
            capture.reset();
            QSemaphore ready, go, start_returned, close_returned;
            execution::SubmitResult submitted;
            std::thread starter{[&] {
                ready.release(); go.acquire();
                if (schedule == 1) close_returned.acquire();
                auto context = Saved(); context.saved_profile->error = QStringLiteral("original saved receipt");
                submitted = coordinator->start(std::move(context), [trace, off_gui](std::shared_ptr<const Result> result) {
                    off_gui();
                    trace->saved_facts = result->saved_profile && result->saved_profile->completed
                        && result->saved_profile->replacement_written && result->saved_profile->directory_saved
                        && result->saved_profile->cleanup_completed && !result->saved_profile->uncertain
                        && result->saved_profile->error == QStringLiteral("original saved receipt");
                    trace->terminal_facts = result->disposition == Disposition::Failed && result->closing
                        && result->cancelled && result->endpoint_retired && !result->native_started && !result->transport && !result->native;
                    ++trace->completions;
                });
                start_returned.release();
            }};
            std::thread closer{[&] {
                ready.release(); go.acquire();
                if (schedule == 0) start_returned.acquire();
                coordinator->close(); close_returned.release(); trace->release_read.release();
            }};
            const auto finish = qScopeGuard([&] {
                go.release(2); start_returned.release(); close_returned.release(); trace->release_read.release();
                if (starter.joinable()) starter.join();
                if (closer.joinable()) closer.join();
                coordinator.reset();
                transport->close();
                shared.stopAndJoin();
            });
            const bool callers_ready = ready.tryAcquire(2, 5000);
            go.release(2); starter.join(); closer.join();
            QVERIFY(callers_ready);
            if (schedule == 0) QVERIFY(submitted);
            if (schedule == 1) QVERIFY(!submitted);
            QTRY_VERIFY(trace->endpoint_retired.load());
            if (submitted) {
                QTRY_COMPARE(trace->completions.load(), 1);
                QVERIFY(trace->saved_facts.load()); QVERIFY(trace->terminal_facts.load());
            } else {
                QCOMPARE(submitted.rejection, execution::Rejection::Closed);
                QCOMPARE(trace->completions.load(), 0);
            }
            coordinator.reset();
            transport->close();
            const auto transport_drained = std::make_shared<std::atomic_bool>(false);
            transport->whenDrained([transport_drained] { *transport_drained = true; });
            QTRY_VERIFY(transport_drained->load());
            QTRY_VERIFY(drained->load());
            QCOMPARE(trace->completions.load(), submitted ? 1 : 0);
            QCOMPARE(trace->starts.load(), 0); QCOMPARE(trace->preparations.load(), 0);
            QCOMPARE(trace->retire_effects.load(), 1); QVERIFY(trace->reads.load() <= 1);
            QCOMPARE(trace->captures_destroyed.load(), 1);
            QVERIFY(trace->off_gui.load()); QVERIFY(trace->cleanup_off_gui.load());
        }
    }
    void admittedReadNotStartedKeepsItsOwnedFailure()
    {
        Fixture fixture;
        QVERIFY(fixture.start());
        fixture.shared.reject_core = true;
        fixture.drain();
        QVERIFY(fixture.received); QCOMPARE(fixture.received->disposition, Disposition::Failed);
        QCOMPARE(fixture.reads, 0); QCOMPARE(fixture.starts, 0); QCOMPARE(fixture.preparations, 0);
        QVERIFY(!fixture.received->native_started); QCOMPARE(fixture.completions, 1);
        QVERIFY(!fixture.received->cancelled);
    }
};
#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(StartupPreparationTests)
#else
QTEST_MAIN(StartupPreparationTests)
#endif
#include <test_startuppreparation.moc>
