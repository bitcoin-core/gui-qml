// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/transactionattempt.h>
#include <qml/executor.h>
#include <qml/walletprepare.h>
#include <test/mocks/manualstartupexecution.h>

#include <QSemaphore>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>

namespace {
walletopening::Opening NewOpening()
{
    return std::make_shared<const walletopening::Identity>();
}
std::shared_ptr<const senddraft::Snapshot> NewReadyDraft()
{
    auto draft = std::make_shared<senddraft::Snapshot>();
    draft->state = senddraft::State::Ready;
    draft->all_valid = true;
    return draft;
}
} // namespace

class TransactionAttemptTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void exactPreparationInstallsImmutableReview()
    {
        transactionattempt::Preparation attempt;
        const auto opening = NewOpening();
        const auto ticket = attempt.begin(opening, 11);
        QVERIFY(ticket.id != 0);
        QVERIFY(attempt.phase() == transactionattempt::Phase::Preparing);

        auto review = std::make_shared<const walletreview::Snapshot>();
        const auto installed = attempt.install(ticket, {opening, 11, true}, review, 12);
        QVERIFY(installed == transactionattempt::Transition::Accepted);
        QVERIFY(attempt.phase() == transactionattempt::Phase::Reviewing);
        QVERIFY(attempt.candidate() == review);
        QCOMPARE(attempt.candidateReview(), transactionattempt::ReviewId{12});
        QVERIFY(attempt.candidateCurrent(12));
        QVERIFY(!attempt.candidateCurrent(11));
    }

    void editedIntentStalesHeldSuccessAndRequiresNewTicket()
    {
        transactionattempt::Preparation attempt;
        const auto opening = NewOpening();
        const auto first = attempt.begin(opening, 3);
        attempt.intentChanged();
        auto stale_review = std::make_shared<const walletreview::Snapshot>();

        QVERIFY(attempt.install(first, {opening, 3, true}, stale_review, 4) == transactionattempt::Transition::Stale);
        QVERIFY(attempt.phase() == transactionattempt::Phase::NeedsRevalidation);
        QVERIFY(!attempt.candidate());

        const auto next = attempt.begin(opening, 3);
        QVERIFY(next.id > first.id);
        QVERIFY(next.intent_revision > first.intent_revision);
        auto current_review = std::make_shared<const walletreview::Snapshot>();
        QVERIFY(attempt.install(next, {opening, 3, true}, current_review, 5) == transactionattempt::Transition::Accepted);
        QVERIFY(attempt.candidateCurrent(5));
    }

    void staleFailureCannotReplaceNewerPreparation()
    {
        transactionattempt::Preparation attempt;
        const auto opening = NewOpening();
        const auto older = attempt.begin(opening, 4);
        attempt.intentChanged();
        const auto newer = attempt.begin(opening, 4);
        QVERIFY(attempt.fail(older, {opening, 4, true}, transactionattempt::Phase::Failed) == transactionattempt::Transition::Ignored);
        QVERIFY(attempt.phase() == transactionattempt::Phase::Preparing);
        QVERIFY(attempt.fail(newer, {opening, 4, true}, transactionattempt::Phase::NeedsUnlock) == transactionattempt::Transition::Accepted);
        QVERIFY(attempt.phase() == transactionattempt::Phase::NeedsUnlock);
    }

    void changedSessionOrPriorReviewRejectsPublication()
    {
        transactionattempt::Preparation attempt;
        const auto opening = NewOpening();
        const auto other = NewOpening();
        const auto ticket = attempt.begin(opening, 21);
        QVERIFY(attempt.assess(ticket, {other, 21, true}) == transactionattempt::Transition::Stale);
        QVERIFY(attempt.phase() == transactionattempt::Phase::NeedsRevalidation);

        const auto successor = attempt.begin(opening, 21);
        QVERIFY(attempt.assess(successor, {opening, 22, true}) == transactionattempt::Transition::Stale);
        QVERIFY(attempt.phase() == transactionattempt::Phase::NeedsRevalidation);
    }

    void displayOnlyChangesDoNotInvalidateCandidate()
    {
        transactionattempt::Preparation attempt;
        const auto opening = NewOpening();
        const auto ticket = attempt.begin(opening, 8);
        auto review = std::make_shared<const walletreview::Snapshot>();
        QVERIFY(attempt.install(ticket, {opening, 8, true}, review, 9) == transactionattempt::Transition::Accepted);

        // Display-unit updates are presentation-only and deliberately do not
        // send an intent-change event or change the reviewed candidate identity.
        QVERIFY(attempt.assess(ticket, {opening, 8, true}) == transactionattempt::Transition::Ignored);
        QVERIFY(attempt.candidateCurrent(9));
        QVERIFY(attempt.phase() == transactionattempt::Phase::Reviewing);
    }

    void equivalentReprepareKeepsExistingCandidateCurrent()
    {
        transactionattempt::Preparation attempt;
        const auto opening = NewOpening();
        const auto first = attempt.begin(opening, 6);
        auto review = std::make_shared<const walletreview::Snapshot>();
        QVERIFY(attempt.install(first, {opening, 6, true}, review, 7) == transactionattempt::Transition::Accepted);

        const auto retry = attempt.begin(opening, 7);
        QVERIFY(retry.id > first.id);
        QVERIFY(attempt.phase() == transactionattempt::Phase::Preparing);
        QVERIFY(attempt.candidateCurrent(7));

        attempt.intentChanged();
        QVERIFY(!attempt.candidateCurrent(7));
    }

    void closeRetiresCandidateAndRejectsLateCompletion()
    {
        transactionattempt::Preparation attempt;
        const auto opening = NewOpening();
        const auto ticket = attempt.begin(opening, 2);
        auto review = std::make_shared<const walletreview::Snapshot>();
        QVERIFY(attempt.install(ticket, {opening, 2, true}, review, 3) == transactionattempt::Transition::Accepted);
        attempt.close();
        QVERIFY(attempt.phase() == transactionattempt::Phase::Closed);
        QVERIFY(!attempt.candidate());
        QVERIFY(attempt.install(ticket, {opening, 2, true}, review, 4) == transactionattempt::Transition::Ignored);
        QVERIFY(attempt.phase() == transactionattempt::Phase::Closed);
    }

    void ownerRunsAdmissionAndOutcomeOnlyOnControlledDomainTurns()
    {
        ManualStartupExecution transport;
        auto utility = std::make_shared<execution::SerialQueue>(transport.executor().makeQueue(execution::Lane::Utility));
        transactionattempt::Store owner{utility};
        const auto opening = NewOpening();
        transactionattempt::StartInput input;
        input.id = 1;
        input.intent_revision = 0;
        input.facts = {opening, 4, true};
        input.draft = NewReadyDraft();
        input.draft_current = true;

        bool began{false};
        transactionattempt::Result begin_result;
        const auto admitted = owner.begin(input, [&](auto result) {
            began = true;
            begin_result = std::move(result);
        });
        QVERIFY(admitted);
        QVERIFY(!began);
        QVERIFY(!transport.step(execution::Lane::Core));
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(began);
        QCOMPARE(begin_result.snapshot.id, std::uint64_t{1});
        QVERIFY(begin_result.snapshot.phase == transactionattempt::Phase::Preparing);

        auto candidate = std::make_shared<const walletreview::Snapshot>();
        bool resolved{false};
        transactionattempt::Result resolution;
        const auto ticket = begin_result.snapshot.ticket;
        QVERIFY(owner.resolve(ticket, input.facts, walletprepare::Error::None, candidate, 5,
            [&](auto result) { resolved = true; resolution = std::move(result); }));
        QVERIFY(!resolved);
        QVERIFY(!resolved);
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(resolved);
        QVERIFY(resolution.transition == transactionattempt::Transition::Accepted);
        QVERIFY(resolution.snapshot.phase == transactionattempt::Phase::Reviewing);
        QVERIFY(resolution.snapshot.candidate == candidate);
        QVERIFY(resolution.snapshot.candidate_current);
        QCOMPARE(resolution.snapshot.current_review, transactionattempt::ReviewId{5});
        QVERIFY(owner.close());
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(transport.quiescent());
    }

    void ownerDrainsAcceptedPreparationOnUtilityWithSchedulerClosed()
    {
        using execution::Lane;
        using transactionattempt::Phase;
        std::vector<Phase> phases;
        bool candidate_retired{false};
        transactionattempt::Ticket ticket;
        ManualStartupExecution worker;
        const auto utility = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(Lane::Utility));
        transactionattempt::Store owner{utility};

        transactionattempt::StartInput input;
        input.id = 1;
        input.facts = {NewOpening(), 3, true};
        input.draft = NewReadyDraft();
        input.draft_current = true;
        const auto record = [&](transactionattempt::Result result) {
            QCOMPARE(result.transition, transactionattempt::Transition::Accepted);
            phases.push_back(result.snapshot.phase);
        };
        QVERIFY(owner.begin(input, [&](auto result) {
            ticket = result.snapshot.ticket;
            record(std::move(result));
        }));
        QVERIFY(phases.empty());
        QVERIFY(!worker.step(Lane::Core));
        QVERIFY(worker.step(Lane::Utility));
        QCOMPARE(phases, std::vector<Phase>{Phase::Preparing});
        QCOMPARE(ticket.session, input.facts.session);

        auto candidate = std::shared_ptr<const walletreview::Snapshot>{new walletreview::Snapshot{},
            [&](const walletreview::Snapshot* value) {
                candidate_retired = true;
                delete value;
            }};
        const auto original = candidate.get();
        QVERIFY(owner.resolve(ticket, input.facts, walletprepare::Error::None, std::move(candidate), 4,
            [&](auto result) {
                QCOMPARE(result.snapshot.candidate.get(), original);
                QCOMPARE(result.snapshot.current_review, transactionattempt::ReviewId{4});
                QVERIFY(result.snapshot.candidate_current);
                record(std::move(result));
            }));
        QVERIFY(owner.intentChanged(1, [&](auto result) {
            QVERIFY(!result.snapshot.candidate_current);
            record(std::move(result));
        }));
        QVERIFY(owner.close(record));
        utility->close();
        QVERIFY(!owner.begin(input, record));
        QVERIFY(!candidate_retired);
        QVERIFY(!worker.step(Lane::Core));
        worker.drain();
        const std::vector<Phase> expected{Phase::Preparing, Phase::Reviewing, Phase::NeedsRevalidation, Phase::Closed};
        QCOMPARE(phases, expected);
        QVERIFY(candidate_retired);
        QVERIFY(worker.quiescent());
    }

    void ownerRejectsHeldResultAfterIntentRevisionAdvances()
    {
        ManualStartupExecution transport;
        auto utility = std::make_shared<execution::SerialQueue>(transport.executor().makeQueue(execution::Lane::Utility));
        transactionattempt::Store owner{utility};
        const auto opening = NewOpening();
        transactionattempt::StartInput input;
        input.id = 1;
        input.facts = {opening, 7, true};
        input.draft = NewReadyDraft();
        input.draft_current = true;
        transactionattempt::Result begin_result;
        QVERIFY(owner.begin(input, [&](auto result) { begin_result = std::move(result); }));
        QVERIFY(transport.step(execution::Lane::Utility));

        bool invalidated{false};
        QVERIFY(owner.intentChanged(1, [&](auto result) {
            invalidated = result.snapshot.intent_revision == 1;
        }));
        QVERIFY(!invalidated);
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(invalidated);

        auto stale_candidate = std::make_shared<const walletreview::Snapshot>();
        transactionattempt::Result held_result;
        bool resolved{false};
        QVERIFY(owner.resolve(begin_result.snapshot.ticket, input.facts, walletprepare::Error::None,
            stale_candidate, 8, [&](auto result) { resolved = true; held_result = std::move(result); }));
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(resolved);
        QVERIFY(held_result.transition == transactionattempt::Transition::Stale);
        QVERIFY(held_result.snapshot.phase == transactionattempt::Phase::NeedsRevalidation);
        QVERIFY(!held_result.snapshot.candidate);
        QVERIFY(!held_result.snapshot.candidate_current);
        QVERIFY(transport.quiescent());
    }

    void rejectedRequestCannotReplaceTheOriginalAttemptMetadata_data()
    {
        QTest::addColumn<bool>("ordinary");
        QTest::newRow("replacement-replay") << false;
        QTest::newRow("ordinary-request-replay") << true;
    }
    void rejectedRequestCannotReplaceTheOriginalAttemptMetadata()
    {
        QFETCH(bool, ordinary);
        ManualStartupExecution transport;
        auto utility = std::make_shared<execution::SerialQueue>(transport.executor().makeQueue(execution::Lane::Utility));
        transactionattempt::Store owner{utility};
        const auto opening = NewOpening();
        const QString original{64, QLatin1Char{'a'}};
        transactionattempt::ReplacementStart input{7, 3, {opening, 0, true}, original};
        transactionattempt::Result started, rejected;
        QVERIFY(owner.beginReplacement(input, [&](auto value) { started = std::move(value); }));
        transport.drain();
        if (ordinary) {
            transactionattempt::StartInput replay;
            replay.id = 7; replay.intent_revision = 3; replay.facts = input.facts;
            QVERIFY(owner.begin(replay, [&](auto value) { rejected = std::move(value); }));
        } else {
            input.original_txid = QString(64, QLatin1Char{'b'});
            QVERIFY(owner.beginReplacement(input, [&](auto value) { rejected = std::move(value); }));
        }
        transport.drain();
        QVERIFY(rejected.transition == transactionattempt::Transition::Ignored);
        QCOMPARE(rejected.snapshot.ticket, started.snapshot.ticket);
        QCOMPARE(rejected.snapshot.phase, started.snapshot.phase);
        QCOMPARE(rejected.snapshot.kind, transactionattempt::Kind::Replacement);
        QCOMPARE(rejected.snapshot.original_txid, original);
        QVERIFY(transport.quiescent());
    }
    void replacementOwnerRetainsOriginalAndApprovedCandidateAcrossTransitions()
    {
        ManualStartupExecution transport;
        auto utility = std::make_shared<execution::SerialQueue>(transport.executor().makeQueue(execution::Lane::Utility));
        transactionattempt::Store owner{utility};
        const auto opening = NewOpening();
        const QString original_txid{QString(64, QLatin1Char{'a'})};
        transactionattempt::ReplacementStart input{1, 1, {opening, 0, true}, original_txid};
        transactionattempt::Result begun_result;
        bool did_begin{false};
        QVERIFY(owner.beginReplacement(input, [&](auto result) { did_begin = true; begun_result = std::move(result); }));
        QVERIFY(!did_begin);
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(did_begin);
        QVERIFY(begun_result.snapshot.kind == transactionattempt::Kind::Replacement);
        QCOMPARE(begun_result.snapshot.original_txid, original_txid);
        QVERIFY(begun_result.snapshot.phase == transactionattempt::Phase::Preparing);

        auto candidate = std::make_shared<const walletreview::Snapshot>();
        transactionattempt::Result installed;
        bool installed_value{false};
        QVERIFY(owner.install(begun_result.snapshot.ticket, input.facts, candidate, 9,
            [&](auto result) { installed_value = true; installed = std::move(result); }));
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(installed_value);
        QVERIFY(installed.snapshot.phase == transactionattempt::Phase::Reviewing);
        QVERIFY(installed.snapshot.candidate == candidate);
        QCOMPARE(installed.snapshot.candidate_review, transactionattempt::ReviewId{9});

        transactionattempt::Result submitting;
        bool submitting_value{false};
        const transactionattempt::CurrentFacts reviewed{opening, 9, true};
        QVERIFY(owner.advance(installed.snapshot.ticket, reviewed, transactionattempt::Phase::Reviewing,
            transactionattempt::Phase::Submitting,
            [&](auto result) { submitting_value = true; submitting = std::move(result); }));
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(submitting_value);
        QVERIFY(submitting.snapshot.phase == transactionattempt::Phase::Submitting);
        QCOMPARE(submitting.snapshot.original_txid, original_txid);
        QVERIFY(submitting.snapshot.candidate == candidate);

        transactionattempt::Result stale;
        bool stale_value{false};
        QVERIFY(owner.advance(submitting.snapshot.ticket, {opening, 9, false}, transactionattempt::Phase::Submitting,
            transactionattempt::Phase::NeedsRevalidation,
            [&](auto result) { stale_value = true; stale = std::move(result); }));
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(stale_value);
        QVERIFY(stale.snapshot.phase == transactionattempt::Phase::NeedsRevalidation);
        QCOMPARE(stale.snapshot.original_txid, original_txid);
        QVERIFY(!stale.snapshot.candidate);
        QVERIFY(!stale.snapshot.candidate_current);
        QVERIFY(owner.close());
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(transport.quiescent());
    }

    void ownerTransitionsAndOutcomeRunOffGuiOnProductionUtilityQueue()
    {
        const auto gui_thread = QThread::currentThread();
        std::atomic<QThread*> owner_thread{nullptr};
        QSemaphore completed, drained;
        execution::Executor executor{execution::Executor::Config{}};
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        transactionattempt::Store owner{utility};
        transactionattempt::StartInput input;
        input.id = 1;
        input.facts = {NewOpening(), 2, true};
        input.draft = NewReadyDraft();
        input.draft_current = true;
        QVERIFY(owner.begin(std::move(input), [&](auto result) {
            owner_thread = QThread::currentThread();
            if (result.snapshot.phase == transactionattempt::Phase::Preparing) completed.release();
        }));
        QVERIFY(completed.tryAcquire(1, 5'000));
        QVERIFY(owner_thread.load());
        QVERIFY(owner_thread.load() != gui_thread);
        utility->close();
        utility->whenDrained([&] { drained.release(); });
        QVERIFY(drained.tryAcquire(1, 5'000));
        executor.stopAndJoin();
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(TransactionAttemptTests)
#else
QTEST_MAIN(TransactionAttemptTests)
#endif
#include <test_transactionattempt.moc>
