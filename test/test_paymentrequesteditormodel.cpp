// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <test/mocks/receiveeditortest.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QPointer>
#include <QThread>

namespace {
using Model = PaymentRequestEditorModel;
using execution::Lane;
struct Fixture {
    ReceiveEditorPorts ports;
    std::unique_ptr<Model> editor;
    explicit Fixture(int64_t id = 17) : editor(std::make_unique<Model>(id, 2, false, ports.editors())) { ports.drain(); }
    ~Fixture() { editor.reset(); ports.drain(); QVERIFY(ports.execution.quiescent()); }
};
}
class PaymentRequestEditorModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void producerRefreshDuringAnInitialReadIsCoalescedWithoutPolling()
    {
        ReceiveEditorPorts ports; auto services = ports.editors(); auto original = services.read;
        std::function<void(requestedit::Source)> held; int reads{0};
        services.read = [&](int64_t id, uint64_t view, auto done) {
            ++reads;
            if (reads == 1) { held = std::move(done); return execution::SubmitResult{{}, execution::Rejection::None}; }
            return original(id, view, std::move(done));
        };
        Model editor{17, 2, false, std::move(services)}; ports.drain(); QCOMPARE(reads, 1); QVERIFY(held);
        auto older = ports.snapshot(17, 2); older.current = false;
        editor.refresh(); ports.drain(); QCOMPARE(reads, 1);
        QVERIFY(execution::Submit(*ports.execution.scheduler, [done = std::move(held), older] { done(older); }, [] {}, {}, {Lane::Domain}));
        ports.drain(); QCOMPARE(reads, 2); QCOMPARE(editor.phase(), Model::Editing);
        QCOMPARE(editor.message()->text(), QStringLiteral("original message")); QCOMPARE(ports.writes, 0);
        editor.retire(); ports.drain(); QVERIFY(ports.execution.quiescent());
    }
    void saveCapturesEachOriginalFieldAndRetainsSuccessorInput()
    {
        Fixture f;
        f.editor->message()->edit("accepted message"); f.editor->save();
        f.editor->privateNote()->edit("successor note");
        QVERIFY(f.editor->busy()); QCOMPARE(f.ports.writes, 0);
        f.ports.domain(); QCOMPARE(f.ports.writes, 0);
        f.ports.drain(); QCOMPARE(f.ports.writes, 1);
        QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text, QStringLiteral("accepted message"));
        QCOMPARE(f.ports.records.at(17).values[requestedit::Note].text, QStringLiteral("original private note"));
        QCOMPARE(f.editor->privateNote()->text(), QStringLiteral("successor note"));
        QVERIFY(f.editor->saveResult().requestSaved); QVERIFY(f.editor->dirty());
    }
    // B2 (CI): a wallet notification makes the editor read a request while the
    // store reconciles, after the page saw it current and saved. The store
    // decides that save (Checking, saved again later), not a "changed" refusal.
    void saveDuringAReconcilingReadIsTheStoresDecision()
    {
        Fixture f;
        f.editor->message()->edit("typed before checking"); f.ports.drain(); QVERIFY(f.editor->canSave());
        f.ports.records.at(17).current = false;
        f.editor->refresh(); f.ports.execution.drain(); // Hold the new GUI projection.
        QVERIFY(f.editor->canSave());
        f.ports.next_write = walletrequests::Error::Checking;
        f.editor->save(); f.ports.drain();
        QCOMPARE(f.ports.writes, 1);
        QCOMPARE(f.editor->saveResult().error, walletrequests::Error::Checking);
        QVERIFY(!f.editor->canSave()); // Until the store is current again.
        f.ports.records.at(17).current = true; f.ports.epoch->advance();
        f.editor->refresh(); f.ports.drain(); QVERIFY(f.editor->canSave());
        f.editor->save(); f.ports.drain();
        QCOMPARE(f.ports.writes, 2); QVERIFY(f.editor->saveResult().requestSaved);
        QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text, QStringLiteral("typed before checking"));
    }
    void untouchedRefreshCannotBeOverwrittenByAnotherFieldIntent()
    {
        Fixture f;
        f.ports.records.at(17).values[requestedit::Label].text = "RPC label";
        ++f.ports.records.at(17).revision; f.ports.epoch->advance();
        f.editor->refresh(); f.ports.execution.drain(); // Hold the new GUI projection.
        f.editor->privateNote()->edit("only this field"); f.ports.drain();
        QCOMPARE(f.editor->publicLabel()->text(), QStringLiteral("RPC label"));
        f.editor->save(); f.ports.drain();
        QCOMPARE(f.ports.records.at(17).values[requestedit::Label].text, QStringLiteral("RPC label"));
        QCOMPARE(f.ports.records.at(17).values[requestedit::Note].text, QStringLiteral("only this field"));
    }
    void discardThenAnotherEditBeforeGuiAcknowledgementDoesNotResurrectFields()
    {
        Fixture f; f.editor->message()->edit("discard this"); f.ports.drain();
        f.editor->discard(); f.ports.execution.drain();
        f.editor->privateNote()->edit("keep this"); f.ports.drain(); f.editor->save(); f.ports.drain();
        QCOMPARE(f.ports.writes, 1);
        QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text, QStringLiteral("original message"));
        QCOMPARE(f.ports.records.at(17).values[requestedit::Note].text, QStringLiteral("keep this"));
    }
    void refusedEditAdmissionIsReplayedOnlyAsPartOfCapturedSave()
    {
        Fixture f; f.ports.execution.rejectLane(Lane::Domain);
        f.editor->message()->edit("accepted after refusal"); f.ports.drain();
        f.editor->save(); f.ports.drain();
        QCOMPARE(f.ports.writes, 1); QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text, QStringLiteral("accepted after refusal"));
    }
    void acknowledgementOfAnotherFieldDoesNotEraseEarlierRejectedInput()
    {
        Fixture f; f.ports.execution.rejectLane(Lane::Domain);
        f.editor->message()->edit("first field rejected by transport"); f.ports.drain();
        f.editor->privateNote()->edit("later acknowledged field"); f.ports.drain();
        QCOMPARE(f.editor->message()->text(), QStringLiteral("first field rejected by transport")); QVERIFY(f.editor->dirty());
        f.editor->save(); f.ports.drain(); QCOMPARE(f.ports.writes, 1);
        QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text, QStringLiteral("first field rejected by transport"));
        QCOMPARE(f.ports.records.at(17).values[requestedit::Note].text, QStringLiteral("later acknowledged field"));
    }
    void canonicalAmountSurvivesUnitChangesBeforeNativeWrite()
    {
        Fixture f; f.editor->amount()->setUnit(BitcoinAmount::Unit::BTC);
        f.editor->amount()->editInput("1.25");
        f.editor->save(); f.ports.domain();
        f.editor->amount()->setUnit(BitcoinAmount::Unit::SAT);
        f.ports.drain(); QCOMPARE(f.ports.writes, 1);
        QCOMPARE(f.ports.attempts.at(0).edit.values[requestedit::Amount].amount, CAmount{125'000'000});
        QCOMPARE(f.ports.records.at(17).values[requestedit::Amount].amount, CAmount{125'000'000});
        QCOMPARE(f.editor->amount()->satoshi(), CAmount{125'000'000});
    }
    void invalidAmountCannotPersistThePreviousValidAmount()
    {
        Fixture f;
        f.editor->amount()->editInput("1..2");
        f.editor->save(); f.ports.drain();
        QCOMPARE(f.ports.writes, 0); QVERIFY(!f.editor->valid()); QCOMPARE(f.editor->amount()->inputText(), QStringLiteral("1..2"));
        f.editor->discard(); f.ports.drain(); QVERIFY(f.editor->valid()); QCOMPARE(f.editor->amount()->satoshi(), CAmount{100});
    }
    void historicalUseRequiresExplicitPublicResolutionAndKeepsPrivateNote()
    {
        Fixture f;
        f.editor->message()->edit("public edit"); f.editor->privateNote()->edit("private edit"); f.ports.drain();
        f.ports.useSaved(17); f.editor->refresh(); f.ports.drain();
        QCOMPARE(f.editor->phase(), Model::Conflict); QVERIFY(!f.editor->publicFieldsEditable());
        f.editor->save(); f.ports.drain(); QCOMPARE(f.ports.writes, 0);
        f.editor->resolveConflict("message", TextEditSessionModel::UseCurrent); f.ports.drain();
        QCOMPARE(f.editor->privateNote()->text(), QStringLiteral("private edit"));
        f.editor->save(); f.ports.drain(); QCOMPARE(f.ports.writes, 1);
        QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text, QStringLiteral("original message"));
        QCOMPARE(f.ports.records.at(17).values[requestedit::Note].text, QStringLiteral("private edit"));
    }
    void partialWriteKeepsTheActualRequestAndSeparateLabelFailure_data()
    {
        QTest::addColumn<int>("error");
        QTest::newRow("label-write-failed") << int(walletrequests::Error::LabelPersistence);
        QTest::newRow("address-label-changed-after-request-write") << int(walletrequests::Error::Changed);
    }
    void partialWriteKeepsTheActualRequestAndSeparateLabelFailure()
    {
        QFETCH(int, error); const auto failure = static_cast<walletrequests::Error>(error);
        Fixture f; f.ports.next_write = failure; f.ports.save_label = false;
        f.ports.label_conflict_after_write = failure == walletrequests::Error::Changed;
        f.editor->privateNote()->edit("saved in request"); f.editor->save(); f.ports.drain();
        const auto actual = f.editor->saveResult();
        QVERIFY(actual.present); QVERIFY(actual.requestSaved); QVERIFY(!actual.labelSaved); QVERIFY(actual.outcomeKnown);
        QCOMPARE(actual.error, failure); QCOMPARE(actual.requestId, QStringLiteral("17"));
        QCOMPARE(f.ports.records.at(17).values[requestedit::Note].text, QStringLiteral("saved in request"));
        QVERIFY(f.editor->error().code != TextEditError::None); QCOMPARE(f.editor->error().message, actual.errorText);
        f.editor->refresh(); f.ports.drain(); QCOMPARE(f.editor->error().message, actual.errorText); QCOMPARE(f.ports.writes, 1);
    }
    void unknownOutcomeNeverReplaysTheOriginalWrite()
    {
        Fixture f; f.ports.next_write = walletrequests::Error::Uncertain;
        f.editor->message()->edit("original unknown write"); f.editor->save(); f.ports.drain();
        QCOMPARE(f.editor->phase(), Model::Uncertain); QVERIFY(!f.editor->saveResult().outcomeKnown);
        f.editor->refresh(); f.editor->save(); f.editor->reconcile(); f.ports.drain();
        QCOMPARE(f.ports.writes, 1); QCOMPARE(f.editor->phase(), Model::Uncertain);
    }
    void emptyAcknowledgementCannotClaimPersistence()
    {
        Fixture f; f.ports.save_request = f.ports.save_label = false;
        f.editor->message()->edit("not acknowledged"); f.editor->save(); f.ports.drain();
        QVERIFY(!f.editor->saveResult().requestSaved); QVERIFY(!f.editor->saveResult().outcomeKnown);
        QCOMPARE(f.editor->phase(), Model::Uncertain); QCOMPARE(f.ports.writes, 1);
    }
    void newSaveAfterRootClosureIsRejectedButAnAdmittedSaveFinishes()
    {
        Fixture f; f.editor->message()->edit("original accepted"); f.editor->save();
        f.ports.execution.scheduler->stopAcceptingWork(); f.ports.drain();
        QCOMPARE(f.ports.writes, 1); QVERIFY(f.editor->saveResult().requestSaved);
        f.editor->message()->edit("not admitted"); f.editor->save(); f.ports.drain(); QCOMPARE(f.ports.writes, 1);
    }
    void retirementBeforeFirstDomainTurnDoesNotEraseAcceptedOutcome()
    {
        Fixture f; f.editor->message()->edit("accepted before retirement"); f.editor->save(); f.editor->retire();
        f.ports.drain(); QCOMPARE(f.ports.writes, 1); QVERIFY(f.editor->saveResult().requestSaved);
        QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text, QStringLiteral("accepted before retirement"));
        QVERIFY(f.ports.execution.quiescent());
    }
    void oldCloseConsentCannotDiscardSuccessorInput()
    {
        Fixture f; f.editor->message()->edit("first"); f.editor->requestClose(); f.ports.drain();
        QPointer<TextEditClosePrompt> question{f.editor->closePrompt()}; QVERIFY(question);
        f.editor->privateNote()->edit("successor"); f.ports.drain();
        if (question) question->answer(TextEditClosePrompt::Discard);
        f.ports.drain(); QVERIFY(f.editor->available()); QVERIFY(f.editor->dirty());
        QCOMPARE(f.editor->privateNote()->text(), QStringLiteral("successor")); QCOMPARE(f.ports.writes, 0);
    }
    void closeOwnsItsOriginalOutcome_data()
    {
        QTest::addColumn<int>("choice");
        QTest::newRow("save") << int(TextEditClosePrompt::Save);
        QTest::newRow("discard") << int(TextEditClosePrompt::Discard);
        QTest::newRow("keep-editing") << int(TextEditClosePrompt::KeepEditing);
    }
    void closeOwnsItsOriginalOutcome()
    {
        QFETCH(int, choice); Fixture f; f.editor->message()->edit("original close intent"); f.editor->requestClose(); f.ports.drain();
        QVERIFY(f.editor->closePrompt()); f.editor->closePrompt()->answer(static_cast<TextEditClosePrompt::Choice>(choice)); f.ports.drain();
        QCOMPARE(f.ports.writes, choice == int(TextEditClosePrompt::Save) ? 1 : 0);
        QCOMPARE(f.editor->phase() == Model::Closed, choice != int(TextEditClosePrompt::KeepEditing));
        if (choice == int(TextEditClosePrompt::KeepEditing)) QVERIFY(f.editor->dirty());
    }
    void reentrantResultObserverCannotRewriteThePublishedReceipt()
    {
        Fixture f; std::vector<RequestSaveResult> received;
        connect(f.editor.get(), &Model::persisted, this, [&](const RequestSaveResult& actual) {
            received.push_back(actual);
            if (received.size() == 1) { f.editor->privateNote()->edit("successor note"); f.editor->save(); }
        });
        f.editor->message()->edit("original"); f.editor->save(); f.ports.drain();
        QCOMPARE(received.size(), size_t{2}); QCOMPARE(f.ports.writes, 2); QVERIFY(received[0].requestSaved);
        QVERIFY(received[0].operationId != received[1].operationId);
        QCOMPARE(f.ports.attempts[0].edit.values[requestedit::Note].text, QStringLiteral("original private note"));
        QCOMPARE(f.editor->saveResult().operationId, received[1].operationId); QCOMPARE(f.editor->privateNote()->text(), QStringLiteral("successor note"));
    }
    void disappearingProjectionDoesNotDiscardTheNativeWrite()
    {
        Fixture f; f.editor->message()->edit("survives view"); f.editor->save(); f.editor.reset(); f.ports.drain();
        QCOMPARE(f.ports.writes, 1); QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text, QStringLiteral("survives view"));
        QVERIFY(f.ports.execution.quiescent());
    }
    void sourceOversizeFailsClosedWithoutTruncatingSavedData()
    {
        Fixture f; f.ports.records.at(17).values[requestedit::Message].text = QString(16'385, QLatin1Char('x'));
        f.editor->refresh(); f.ports.drain();
        QVERIFY(!f.editor->valid()); f.editor->save(); f.ports.drain(); QCOMPARE(f.ports.writes, 0);
        QCOMPARE(f.ports.records.at(17).values[requestedit::Message].text.size(), 16'385);
    }
    void realThreadsSeparateDomainValidationNativeWriteAndGuiProjection()
    {
        BackendExecutor executor;
        const auto scheduler = executor.scheduler(); const auto gui = QThread::currentThread();
        std::atomic<QThread*> read_thread{nullptr}, write_thread{nullptr}; std::atomic_int writes{0};
        std::mutex mutex; ReceiveEditorPorts::Source storage;
        { ReceiveEditorPorts seed; storage = seed.records.at(17); }
        Model::Ports ports; ports.execution = {scheduler, presentation::QueuedPost()};
        ports.read = [&, scheduler](int64_t, uint64_t, auto done) {
            return execution::Submit(*scheduler, [&] { read_thread.store(QThread::currentThread()); std::lock_guard lock{mutex}; return storage; },
                std::move(done), {}, {Lane::Domain});
        };
        ports.admitWrite = [&, scheduler]() -> Model::Writer {
            const auto original = scheduler->openActionScope(); if (!original) return {};
            return [&, original](const requestedit::Intent& intent, auto done, const auto&) {
                return execution::Submit(*original, [&, intent] {
                    write_thread.store(QThread::currentThread()); ++writes; std::lock_guard lock{mutex};
                    storage.values = intent.edit.values; ++storage.revision;
                    requestedit::Write result; result.actual.id = 17; result.actual.request_saved = result.actual.label_saved = true;
                    result.actual.record = ReceiveEditorPorts::record(storage); return result;
                }, std::move(done), {}, {Lane::Core});
            };
        };
        auto editor = std::make_unique<Model>(17, 2, false, std::move(ports)); QTRY_COMPARE(editor->phase(), Model::Editing);
        editor->message()->edit("threaded write"); editor->save(); QTRY_VERIFY(editor->saveResult().requestSaved);
        // Lanes own separate queued effects; the worker pool may legally reuse
        // one physical thread once its previous effect has finished.
        QCOMPARE(writes.load(), 1); QVERIFY(read_thread.load()); QVERIFY(write_thread.load());
        QVERIFY(read_thread.load() != gui); QVERIFY(write_thread.load() != gui);
        QCOMPARE(editor->thread(), gui); editor.reset(); executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(PaymentRequestEditorModelTests)
#include <test_paymentrequesteditormodel.moc>
