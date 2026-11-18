// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/texteditsessionmodel.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QPointer>
#include <atomic>
#include <deque>
#include <stdexcept>

namespace {
using Model = TextEditSessionModel;
using execution::Lane;
struct ReleaseNotice {
    explicit ReleaseNotice(std::function<void()> release) : release(std::move(release)) {}
    ~ReleaseNotice() { release(); }
    const std::function<void()> release;
};
struct Fixture {
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui;
    QString persisted{"original"};
    int reads{0}, validations{0}, writes{0}, queries{0};
    bool fail{false}, unknown{false}, throw_after_admission{false};
    Model::Write actual;
    std::unique_ptr<Model> editor;
    explicit Fixture(std::function<void()> release = {})
    {
        Model::Ports ports;
        ports.execution = {execution.scheduler, [this](auto turn) { gui.push_back(std::move(turn)); }};
        ports.validate = [this, owner = release ? std::make_shared<ReleaseNotice>(std::move(release)) : nullptr](const QString& value) {
            (void)owner; ++validations; return value.size() <= 20;
        };
        ports.read = [this](auto done) { return execution::Submit(*execution.scheduler, [this] {
            ++reads; return Model::Read{persisted, {}};
        }, std::move(done), {}, {Lane::Core}); };
        ports.write = [this](const auto& attempt, auto done, const auto& action) {
            const auto admission = execution::Submit(*action, [this, attempt] {
                ++writes;
                Model::Write value; value.value = attempt.values[0];
                if (attempt.base[0] != persisted) {
                    value.outcome = persistentedit::Outcome::Rejected; value.current = Model::Read{persisted, {}};
                    value.error = {TextEditError::Changed, "external change"};
                } else if (fail) {
                    value.error = {TextEditError::Persistence, "not saved"};
                } else {
                    persisted = value.value; value.persisted = true; value.outcome = persistentedit::Outcome::Saved;
                    value.current = Model::Read{persisted, {}};
                }
                actual = value;
                if (unknown) { value.outcome = persistentedit::Outcome::Unknown; value.persisted = false; value.current.reset(); value.error = {TextEditError::Unknown, "unconfirmed"}; }
                return value;
            }, std::move(done), {}, {Lane::Core});
            if (throw_after_admission) throw std::runtime_error{"Writer transport threw after admission"};
            return admission;
        };
        ports.query = [this](const auto&, auto done) { return execution::Submit(*execution.scheduler, [this] {
            ++queries; return actual;
        }, std::move(done), {}, {Lane::Domain}); };
        editor = std::make_unique<Model>("original-address", std::move(ports));
    }
    void publish()
    {
        while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
    }
    void drain() { execution.drain(); publish(); }
    void domain() { while (execution.step(Lane::Domain)) {} publish(); }
    ~Fixture()
    {
        editor.reset(); drain();
        QVERIFY(execution.quiescent());
    }
};
}
class TextEditSessionModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void saveCapturesLatestInputBeforeItsDomainProjection()
    {
        Fixture f; f.drain(); QCOMPARE(f.editor->text()->text(), QString{"original"});
        f.editor->text()->edit("accepted"); f.editor->save();
        QCOMPARE(f.writes, 0); QVERIFY(f.editor->busy());
        f.domain(); QCOMPARE(f.writes, 0); QCOMPARE(f.editor->phase(), Model::Saving);
        f.editor->text()->edit("successor"); f.domain();
        QVERIFY(f.execution.step(Lane::Core)); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"accepted"}); QCOMPARE(f.editor->text()->text(), QString{"successor"});
        QVERIFY(f.editor->dirty()); QVERIFY(f.editor->canSave());
        QCOMPARE(f.editor->saveResult().value, QString{"accepted"}); QVERIFY(f.editor->saveResult().persisted);
    }
    void initialReadKeepsCapturedSaveAndSuccessorDistinct()
    {
        Fixture f; f.domain(); QCOMPARE(f.reads, 0);
        f.editor->text()->edit("first"); f.editor->save(); f.editor->text()->edit("later"); f.domain();
        f.drain(); QCOMPARE(f.persisted, QString{"first"}); QCOMPARE(f.editor->text()->text(), QString{"later"});
        QCOMPARE(f.writes, 1); QVERIFY(f.editor->dirty());
    }
    void refreshingCannotCancelOrRetargetAnOriginalSave_data()
    {
        QTest::addColumn<bool>("external_change");
        QTest::newRow("unchanged-storage") << false; QTest::newRow("changed-storage") << true;
    }
    void refreshingCannotCancelOrRetargetAnOriginalSave()
    {
        QFETCH(bool, external_change); Fixture f; f.drain();
        f.editor->text()->edit("accepted"); f.domain();
        f.editor->refresh(); f.domain(); // Hold the actual Core read.
        QCOMPARE(f.reads, 1); f.editor->save(); f.domain(); QVERIFY(f.editor->busy());
        f.editor->text()->edit("successor"); f.domain();
        if (external_change) f.persisted = "external";
        f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.reads, 3);
        QCOMPARE(f.editor->text()->text(), QStringLiteral("successor")); QVERIFY(f.editor->dirty());
        QCOMPARE(f.persisted, external_change ? QStringLiteral("external") : QStringLiteral("accepted"));
        QCOMPARE(f.editor->saveResult().persisted, !external_change);
        QCOMPARE(f.editor->conflict(), external_change);
        QCOMPARE(f.editor->saveResult().outcome, external_change ? TextEditSaveResult::Rejected : TextEditSaveResult::Saved);
    }
    void failedSaveKeepsTheExactDraftAndItsScopedError()
    {
        Fixture f; f.drain(); f.fail = true;
        f.editor->text()->edit("attempt"); f.editor->save(); f.drain();
        QCOMPARE(f.persisted, QString{"original"}); QCOMPARE(f.editor->text()->text(), QString{"attempt"});
        QVERIFY(f.editor->dirty()); QCOMPARE(f.editor->error().code, TextEditError::Persistence);
        QVERIFY(f.editor->saveResult().present); QVERIFY(!f.editor->saveResult().persisted);
        f.editor->text()->edit("corrected"); f.domain(); QCOMPARE(f.editor->error().code, TextEditError::None);
        f.fail = false; f.editor->save(); f.drain(); QCOMPARE(f.persisted, QString{"corrected"}); QCOMPARE(f.writes, 2);
    }
    void changedBackendAtWriteBoundaryRequiresExplicitResolution()
    {
        Fixture f; f.drain(); f.editor->text()->edit("my note"); f.editor->save(); f.domain();
        f.persisted = "rpc note"; f.drain();
        QCOMPARE(f.persisted, QString{"rpc note"}); QCOMPARE(f.editor->text()->text(), QString{"my note"});
        QVERIFY(f.editor->conflict()); QCOMPARE(f.editor->committedText(), QString{"rpc note"});
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 1);
        f.editor->resolveConflict("text", Model::KeepEdit); f.domain();
        f.editor->save(); f.drain(); QCOMPARE(f.persisted, QString{"my note"}); QCOMPARE(f.writes, 2);
    }
    void observationDoesNotWriteUntouchedOrOverrideDirtyIntent()
    {
        Fixture f; f.drain(); f.persisted = "fresh"; f.editor->refresh(); f.drain();
        QCOMPARE(f.editor->text()->text(), QString{"fresh"}); QVERIFY(!f.editor->dirty());
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 0);
        f.editor->text()->edit("draft"); f.domain(); f.persisted = "rpc"; f.editor->refresh(); f.drain();
        QCOMPARE(f.editor->text()->text(), QString{"draft"}); QVERIFY(f.editor->conflict());
        f.editor->resolveConflict("text", Model::UseCurrent); f.domain();
        QCOMPARE(f.editor->text()->text(), QString{"rpc"}); QVERIFY(!f.editor->dirty()); QCOMPARE(f.writes, 0);
    }
    void unknownQueriesTheOriginalReceiptWithoutRepeatingWrite()
    {
        Fixture f; f.drain(); f.unknown = true; f.editor->text()->edit("saved"); f.editor->save(); f.drain();
        QCOMPARE(f.editor->phase(), Model::Uncertain); QVERIFY(!f.editor->saveResult().outcomeKnown);
        f.editor->refresh(); f.drain(); QCOMPARE(f.editor->phase(), Model::Uncertain);
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 1);
        f.editor->reconcile(); f.drain(); QCOMPARE(f.queries, 1); QCOMPARE(f.writes, 1);
        QVERIFY(f.editor->saveResult().persisted); QVERIFY(f.editor->saveResult().outcomeKnown); QVERIFY(!f.editor->dirty());
    }
    void receiptQueryNeverReusesHistoricalReadback_data()
    {
        QTest::addColumn<bool>("retained_readback");
        QTest::newRow("with-original-readback") << true;
        QTest::newRow("without-original-readback") << false;
    }
    void receiptQueryNeverReusesHistoricalReadback()
    {
        QFETCH(bool, retained_readback);
        Fixture f; f.drain(); f.unknown = true;
        f.editor->text()->edit("saved"); f.editor->save(); f.drain();
        QCOMPARE(f.editor->phase(), Model::Uncertain);
        const auto operation = f.editor->saveResult().operationId;
        QCOMPARE(f.writes, 1);
        if (!retained_readback) f.actual.current.reset();
        // Independent current storage changed after the original saved effect.
        // Its historical receipt must keep "saved" while a new read sees this.
        f.persisted = "external";
        f.editor->reconcile(); f.drain();
        QCOMPARE(f.queries, 1); QCOMPARE(f.writes, 1);
        QCOMPARE(f.editor->saveResult().operationId, operation);
        QVERIFY(f.editor->saveResult().outcomeKnown);
        QVERIFY(f.editor->saveResult().persisted);
        QCOMPARE(f.editor->saveResult().value, QString{"saved"});
        QCOMPARE(f.editor->text()->text(), QString{"external"});
        QCOMPARE(f.editor->phase(), Model::Editing); QVERIFY(!f.editor->dirty());
    }
    void closePromptHasExactConsentAndThreeOutcomes_data()
    {
        QTest::addColumn<int>("choice");
        QTest::newRow("save") << int(Model::Save); QTest::newRow("discard") << int(Model::Discard); QTest::newRow("keep") << int(Model::KeepEditing);
    }
    void closePromptHasExactConsentAndThreeOutcomes()
    {
        QFETCH(int, choice); Fixture f; f.drain(); f.editor->text()->edit("draft"); f.editor->requestClose(); f.domain();
        QVERIFY(f.editor->closePrompt()); const auto old_token = f.editor->closeToken();
        f.editor->text()->edit("successor"); f.domain();
        f.editor->answerClose(old_token, Model::Discard); f.domain(); QVERIFY(f.editor->dirty());
        QVERIFY(f.editor->closeToken() != old_token); QVERIFY(f.editor->closePrompt());
        f.editor->closePrompt()->answer(static_cast<TextEditClosePrompt::Choice>(choice)); f.drain();
        QCOMPARE(f.writes, choice == Model::Save ? 1 : 0);
        QCOMPARE(f.persisted, choice == Model::Save ? QString{"successor"} : QString{"original"});
        QCOMPARE(f.editor->phase(), choice == Model::KeepEditing ? Model::Editing : Model::Closed);
        if (choice == Model::KeepEditing) QCOMPARE(f.editor->text()->text(), QString{"successor"});
    }
    void retireWhileSavingDrainsAndStillPublishesActualReceipt()
    {
        Fixture f; f.drain(); f.editor->text()->edit("saved"); f.editor->save(); f.domain();
        f.editor->retire(); f.editor->retire(); f.domain(); QCOMPARE(f.editor->phase(), Model::Retiring);
        f.editor->text()->edit("late"); f.editor->save(); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"saved"}); QCOMPARE(f.editor->phase(), Model::Closed);
        QVERIFY(f.editor->saveResult().persisted); QCOMPARE(f.editor->saveResult().value, QString{"saved"});
    }
    void retirementKeepsTheSuccessorDraftAndOriginalWriteReceiptSeparate()
    {
        Fixture f; f.drain(); f.editor->text()->edit("accepted"); f.editor->save();
        QVERIFY(f.editor->busy()); f.editor->text()->edit("successor"); f.domain();
        QSignalSpy refused{f.editor.get(), &Model::commandRejected};
        f.editor->save(); QCOMPARE(refused.size(), 1);
        f.editor->retire(); f.domain(); QVERIFY(f.editor->busy());
        f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"accepted"});
        QCOMPARE(f.editor->saveResult().value, QString{"accepted"});
        QCOMPARE(f.editor->text()->text(), QString{"successor"}); QCOMPARE(f.editor->phase(), Model::Closed);
        QVERIFY(!f.editor->busy());
    }
    void destroyedViewCannotCancelOrRetargetAnAdmittedSave()
    {
        Fixture f; f.drain(); f.editor->text()->edit("saved"); f.editor->save(); f.domain();
        f.editor.reset(); f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"saved"});
        QVERIFY(f.execution.quiescent());
    }
    void invalidAndTransportRefusalNeverReachTheWritePort()
    {
        Fixture f; f.drain(); f.editor->text()->edit(QString(21, 'a')); f.editor->save(); f.drain();
        QCOMPARE(f.writes, 0); QCOMPARE(f.editor->error().code, TextEditError::Invalid); QVERIFY(f.editor->dirty());
        QVERIFY(f.editor->saveResult().present); QCOMPARE(f.editor->saveResult().outcome, TextEditSaveResult::Rejected);
        const auto original = f.editor->text()->text(); QSignalSpy refused{f.editor.get(), &Model::commandRejected};
        f.editor->text()->edit(QString(32769, 'z')); QCOMPARE(f.editor->text()->text(), original); QCOMPARE(refused.size(), 1);
        f.editor->discard(); f.domain(); QCOMPARE(f.editor->text()->text(), QString{"original"});
    }
    void retiredProjectionDoesNotHoldAnySchedulerResource()
    {
        Fixture f; f.drain(); f.editor->text()->edit("unsaved"); f.domain();
        f.editor->retire(); f.drain();
        QVERIFY(f.execution.quiescent()); QCOMPARE(f.editor->phase(), Model::Closed);
        QCOMPARE(f.editor->text()->text(), QString{"unsaved"}); QVERIFY(!f.editor->busy());
        QCOMPARE(f.writes, 0);
    }
    void retirementReleasesResourcesEvenWhenItsDomainPostIsRejected()
    {
        Fixture f; f.drain(); f.execution.rejectLane(Lane::Domain);
        f.editor->retire(); f.drain();
        QVERIFY(f.execution.quiescent()); QCOMPARE(f.editor->phase(), Model::Closed); QVERIFY(!f.editor->busy());
        QCOMPARE(f.writes, 0);
    }
    void presentationCoalescesWithoutDroppingTheLatestOwnedInput()
    {
        Fixture f; f.drain();
        for (int i{0}; i < 64; ++i) {
            f.editor->text()->edit(QString::number(i));
            while (f.execution.step(Lane::Domain)) {}
            QVERIFY(f.gui.size() <= 1);
        }
        f.publish(); QCOMPARE(f.editor->text()->text(), QString{"63"}); QVERIFY(f.editor->dirty());
        f.editor->save(); QVERIFY(f.editor->busy()); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"63"});
    }
    void aRejectedEditCannotMakeSaveUseAnOlderProjection()
    {
        Fixture f; f.drain(); f.execution.rejectLane(Lane::Domain);
        f.editor->text()->edit("latest literal"); f.drain();
        QCOMPARE(f.writes, 0); QCOMPARE(f.editor->text()->text(), QString{"latest literal"});
        f.editor->save(); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"latest literal"});
        QCOMPARE(f.editor->saveResult().value, QString{"latest literal"});
    }
    void overBudgetInputMustBeCorrectedBeforeAnOlderDraftCanSave()
    {
        Fixture f; f.drain(); f.editor->text()->edit("draft"); f.domain();
        f.editor->text()->edit(QString(32769, 'x')); QVERIFY(!f.editor->valid());
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 0); QCOMPARE(f.persisted, QString{"original"});
        f.editor->text()->edit("draft"); f.domain(); QVERIFY(f.editor->valid());
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"draft"});
    }
    void writerTransportExceptionCannotEraseOrReplayItsAcceptedEffect()
    {
        Fixture f; f.drain(); f.throw_after_admission = true;
        f.editor->text()->edit("accepted"); f.editor->save(); f.domain();
        QCOMPARE(f.writes, 0); QCOMPARE(f.editor->phase(), Model::Uncertain);
        QVERIFY(!f.editor->saveResult().outcomeKnown);
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"accepted"});
        QVERIFY(f.editor->saveResult().outcomeKnown); QVERIFY(f.editor->saveResult().persisted);
    }
    void rootOrdinaryClosureRejectsANewSaveFromAnOldEditor()
    {
        Fixture f; f.drain(); f.editor->text()->edit("late"); f.domain();
        f.execution.scheduler->closeOrdinaryAdmission();
        QSignalSpy refused{f.editor.get(), &Model::commandRejected};
        f.editor->save(); f.drain();
        QCOMPARE(f.writes, 0); QCOMPARE(f.persisted, QString{"original"});
        QCOMPARE(refused.size(), 1); QCOMPARE(f.editor->error().code, TextEditError::Refused);
        QCOMPARE(f.editor->phase(), Model::Editing); QCOMPARE(f.editor->text()->text(), QString{"late"});
    }
    void originalAdmittedSaveSurvivesRootOrdinaryClosure()
    {
        Fixture f; f.drain(); f.editor->text()->edit("accepted"); f.editor->save();
        QCOMPARE(f.writes, 0); f.execution.scheduler->closeOrdinaryAdmission(); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"accepted"});
        QVERIFY(f.editor->saveResult().persisted); QCOMPARE(f.editor->saveResult().value, QString{"accepted"});
        QVERIFY(!f.editor->dirty());
    }
    void reentrantObserverDestructionCannotBecomeTheLastGuiActorOwner()
    {
        {
            Fixture original; original.drain();
            QObject::connect(original.editor.get(), &Model::commandRejected, qApp, [&] { original.editor->discard(); });
            QSignalSpy receipt{original.editor.get(), &Model::commandRejected};
            original.execution.rejectLane(Lane::Domain); original.editor->refresh();
            QCOMPARE(receipt.size(), 1);
            QCOMPARE(receipt.front().at(1).value<TextEditError>().code, TextEditError::Refused);
            QCOMPARE(original.editor->error().code, TextEditError::None);
            original.drain(); QCOMPARE(original.writes, 0);
        }
        bool worker_turn{false}, released_on_worker{false}; int released{0}, rejected{0};
        Fixture f{[&] { ++released; released_on_worker = worker_turn; }}; f.drain();
        QObject::connect(f.editor.get(), &Model::commandRejected, qApp, [&] {
            ++rejected; f.editor.reset();
            for (;;) {
                worker_turn = true; const bool stepped = f.execution.stepNext(); worker_turn = false;
                if (!stepped) break;
            }
            // Hold every queued GUI publication inside this original signal.
            QCOMPARE(released, 1); QVERIFY(released_on_worker); QVERIFY(f.execution.quiescent());
        });
        f.execution.rejectLane(Lane::Domain); f.editor->refresh();
        QCOMPARE(rejected, 1); QCOMPARE(released, 1); QVERIFY(released_on_worker);
        QVERIFY(!f.gui.empty()); f.publish(); QVERIFY(f.execution.quiescent());
    }
    void realWorkersSeparateValidationPersistenceAndProjection()
    {
        BackendExecutor executor{execution::Options{}};
        const auto scheduler = executor.scheduler(); const auto gui = QThread::currentThread();
        std::atomic<QThread*> read_thread{nullptr}, write_thread{nullptr}, validation_thread{nullptr};
        std::atomic<int> writes{0}; QString persisted{"base"};
        Model::Ports ports; ports.execution.scheduler = scheduler;
        ports.validate = [&](const QString&) { validation_thread = QThread::currentThread(); return true; };
        ports.read = [&](auto done) { return execution::Submit(*scheduler, [&] { read_thread = QThread::currentThread(); return Model::Read{persisted, {}}; }, done, {}, {Lane::Core}); };
        ports.write = [&](const auto& attempt, auto done, const auto& action) { return execution::Submit(*action, [&, value = attempt.values[0]] {
            write_thread = QThread::currentThread(); ++writes; persisted = value;
            return Model::Write{persistentedit::Outcome::Saved, true, value, Model::Read{value, {}}, {}};
        }, done, {}, {Lane::Core}); };
        auto editor = std::make_unique<Model>("original", std::move(ports));
        QTRY_COMPARE(editor->phase(), Model::Editing);
        editor->text()->edit("saved"); editor->save(); QTRY_VERIFY(editor->saveResult().persisted);
        QCOMPARE(writes.load(), 1); QVERIFY(read_thread.load() != gui); QVERIFY(write_thread.load() != gui);
        QVERIFY(validation_thread.load() != gui); QVERIFY(validation_thread.load() != write_thread.load());
        QCOMPARE(editor->thread(), gui); editor.reset(); executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(TextEditSessionModelTests)
#include <test_texteditsessionmodel.moc>
