// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/texteditsessionmodel.h>
#include <qml/executor.h>
#include <qml/guipresentation.h>
#include <qml/retainedvalue.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
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
    ManualStartupExecution execution;
    const std::shared_ptr<execution::ValueCleanupQueue> values{std::make_shared<execution::ValueCleanupQueue>(
        std::make_shared<execution::SerialQueue>(execution.executor().makeQueue(Lane::Utility)))};
    bool accepting{true};
    std::deque<std::function<void()>> gui;
    QString persisted{"original"};
    int reads{0}, validations{0}, writes{0}, queries{0};
    bool fail{false}, unknown{false}, throw_after_admission{false};
    Model::Write actual;
    std::unique_ptr<Model> editor;
    explicit Fixture(std::function<void()> release = {})
    {
        Model::Ports ports;
        ports.post = [this](auto turn) { gui.push_back(std::move(turn)); };
        ports.postWork = [this](auto work) { return execution.outcomes.post(std::move(work)); };
        ports.value_cleanup = [this](auto cleanup) { return values->hold(std::move(cleanup)); };
        ports.validate = [this, owner = release ? std::make_shared<ReleaseNotice>(std::move(release)) : nullptr](const QString& value) {
            (void)owner; ++validations; return value.size() <= 20;
        };
        ports.read = [this](auto done) {
            return submit(execution.core, [this] {
                ++reads;
                return Model::Read{persisted, {}};
            }, std::move(done));
        };
        ports.admitWrite = [this]() -> Model::Writer {
            if (!accepting) return {};
            return [this](const auto& attempt, auto done) {
                const auto submitted = submit(execution.core, [this, attempt] {
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
                }, std::move(done));
                if (throw_after_admission) throw std::runtime_error{"Writer transport threw after submitted"};
                return submitted;
            };
        };
        ports.query = [this](const auto&, auto done) {
            return submit(execution.outcomes, [this] {
                ++queries;
                return actual;
            }, std::move(done));
        };
        editor = std::make_unique<Model>("original-address", std::move(ports));
    }
    template <typename Work, typename Done>
    execution::SubmitResult submit(execution::SerialQueue& queue, Work work, Done done)
    {
        const bool accepted = queue.post([this, work = std::move(work), done = std::move(done)]() mutable {
            auto result = work();
            QVERIFY(execution.outcomes.post([done = std::move(done), result = std::move(result)]() mutable {
                done(std::move(result));
            }));
        });
        return {{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
    }
    void publish()
    {
        while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
    }
    void drain() { execution.drain(); publish(); }
    void utility() { while (execution.step(Lane::Utility)) {} publish(); }
    ~Fixture()
    {
        editor.reset(); drain();
        bool retired{false}; values->close([&] { retired = true; });
        drain();
        QVERIFY(retired); QVERIFY(execution.quiescent());
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
        f.utility(); QCOMPARE(f.writes, 0); QCOMPARE(f.editor->phase(), Model::Saving);
        f.editor->text()->edit("successor"); f.utility();
        QVERIFY(f.execution.step(Lane::Core)); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"accepted"}); QCOMPARE(f.editor->text()->text(), QString{"successor"});
        QVERIFY(f.editor->dirty()); QVERIFY(f.editor->canSave());
        QCOMPARE(f.editor->saveResult().value, QString{"accepted"}); QVERIFY(f.editor->saveResult().persisted);
    }
    void typedInputDuringLoadingIsReportedBeforeTheWorkerSeesIt()
    {
        // A view that shows a fallback while Loading must switch to text()
        // on the first edit. The worker's publish of that edit arrives while
        // the read is still outstanding and re-evaluates such a view; if the
        // view still chose the fallback then, the user's text was replaced.
        Fixture f; f.utility(); QCOMPARE(f.editor->phase(), Model::Loading);
        QVERIFY(!f.editor->hasLocalInput());
        QSignalSpy changed{f.editor.get(), &Model::changed};
        f.editor->text()->edit("");
        QVERIFY(f.editor->hasLocalInput()); QCOMPARE(changed.size(), 1);
        QCOMPARE(f.editor->text()->text(), QString{});
        f.utility(); // Publish while still Loading: the input is not projected yet.
        QCOMPARE(f.reads, 0); QCOMPARE(f.editor->phase(), Model::Loading);
        QVERIFY(f.editor->hasLocalInput()); QCOMPARE(f.editor->text()->text(), QString{});
        f.editor->text()->edit("typed while loading"); f.utility();
        QCOMPARE(f.editor->text()->text(), QString{"typed while loading"});
        f.drain();
        QCOMPARE(f.reads, 1); QCOMPARE(f.editor->phase(), Model::Editing);
        QCOMPARE(f.editor->text()->text(), QString{"typed while loading"}); QVERIFY(f.editor->dirty());
        Fixture untouched; untouched.drain();
        QVERIFY(!untouched.editor->hasLocalInput()); QCOMPARE(untouched.editor->text()->text(), QString{"original"});
    }
    void closeWhileLoadingAsksAboutTypedInput()
    {
        // Text typed before the current value arrives is unsaved. Close waits
        // for the value and then asks instead of dropping that text.
        Fixture f; f.utility(); QCOMPARE(f.reads, 0); QCOMPARE(f.editor->phase(), Model::Loading);
        f.editor->text()->edit("typed while loading"); f.utility();
        QVERIFY(f.editor->dirty());
        f.editor->requestClose(); f.utility();
        QVERIFY(f.editor->phase() != Model::Closed); QVERIFY(!f.editor->closePrompt());
        f.drain();
        QCOMPARE(f.reads, 1); QVERIFY(f.editor->phase() != Model::Closed); QVERIFY(f.editor->closePrompt());
        QCOMPARE(f.editor->text()->text(), QString{"typed while loading"}); QVERIFY(f.editor->dirty());
        // Without typed input, Close during Loading still closes at once.
        Fixture untouched; untouched.utility();
        untouched.editor->requestClose(); untouched.utility();
        QCOMPARE(untouched.editor->phase(), Model::Closed);
    }
    void initialReadKeepsCapturedSaveAndSuccessorDistinct()
    {
        Fixture f; f.utility(); QCOMPARE(f.reads, 0);
        f.editor->text()->edit("first"); f.editor->save(); f.editor->text()->edit("later"); f.utility();
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
        f.editor->text()->edit("accepted"); f.utility();
        f.editor->refresh(); f.utility(); // Hold the actual Core read.
        QCOMPARE(f.reads, 1); f.editor->save(); f.utility(); QVERIFY(f.editor->busy());
        f.editor->text()->edit("successor"); f.utility();
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
        f.editor->text()->edit("corrected"); f.utility(); QCOMPARE(f.editor->error().code, TextEditError::None);
        f.fail = false; f.editor->save(); f.drain(); QCOMPARE(f.persisted, QString{"corrected"}); QCOMPARE(f.writes, 2);
    }
    void changedBackendAtWriteBoundaryRequiresExplicitResolution()
    {
        Fixture f; f.drain(); f.editor->text()->edit("my note"); f.editor->save(); f.utility();
        f.persisted = "rpc note"; f.drain();
        QCOMPARE(f.persisted, QString{"rpc note"}); QCOMPARE(f.editor->text()->text(), QString{"my note"});
        QVERIFY(f.editor->conflict()); QCOMPARE(f.editor->committedText(), QString{"rpc note"});
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 1);
        f.editor->resolveConflict("text", Model::KeepEdit); f.utility();
        f.editor->save(); f.drain(); QCOMPARE(f.persisted, QString{"my note"}); QCOMPARE(f.writes, 2);
    }
    void observationDoesNotWriteUntouchedOrOverrideDirtyIntent()
    {
        Fixture f; f.drain(); f.persisted = "fresh"; f.editor->refresh(); f.drain();
        QCOMPARE(f.editor->text()->text(), QString{"fresh"}); QVERIFY(!f.editor->dirty());
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 0);
        f.editor->text()->edit("draft"); f.utility(); f.persisted = "rpc"; f.editor->refresh(); f.drain();
        QCOMPARE(f.editor->text()->text(), QString{"draft"}); QVERIFY(f.editor->conflict());
        f.editor->resolveConflict("text", Model::UseCurrent); f.utility();
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
        QFETCH(int, choice); Fixture f; f.drain(); f.editor->text()->edit("draft"); f.editor->requestClose(); f.utility();
        const QPointer<TextEditClosePrompt> old{f.editor->closePrompt()}; QVERIFY(old);
        f.editor->text()->edit("successor"); f.utility();
        QVERIFY(f.editor->closePrompt()); QVERIFY(f.editor->closePrompt() != old.data());
        if (old) old->answer(TextEditClosePrompt::Discard);
        f.utility(); QVERIFY(f.editor->dirty()); QVERIFY(f.editor->closePrompt());
        f.editor->closePrompt()->answer(static_cast<TextEditClosePrompt::Choice>(choice)); f.drain();
        QCOMPARE(f.writes, choice == Model::Save ? 1 : 0);
        QCOMPARE(f.persisted, choice == Model::Save ? QString{"successor"} : QString{"original"});
        QCOMPARE(f.editor->phase(), choice == Model::KeepEditing ? Model::Editing : Model::Closed);
        if (choice == Model::KeepEditing) QCOMPARE(f.editor->text()->text(), QString{"successor"});
    }
    void retireWhileSavingDrainsAndStillPublishesActualReceipt()
    {
        Fixture f; f.drain(); f.editor->text()->edit("saved"); f.editor->save(); f.utility();
        f.editor->retire(); f.editor->retire(); f.utility(); QCOMPARE(f.editor->phase(), Model::Retiring);
        f.editor->text()->edit("late"); f.editor->save(); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"saved"}); QCOMPARE(f.editor->phase(), Model::Closed);
        QVERIFY(f.editor->saveResult().persisted); QCOMPARE(f.editor->saveResult().value, QString{"saved"});
    }
    void retirementKeepsTheSuccessorDraftAndOriginalWriteReceiptSeparate()
    {
        Fixture f; f.drain(); f.editor->text()->edit("accepted"); f.editor->save();
        QVERIFY(f.editor->busy()); f.editor->text()->edit("successor"); f.utility();
        QSignalSpy refused{f.editor.get(), &Model::commandRejected};
        f.editor->save(); QCOMPARE(refused.size(), 1);
        f.editor->retire(); f.utility(); QVERIFY(f.editor->busy());
        f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"accepted"});
        QCOMPARE(f.editor->saveResult().value, QString{"accepted"});
        QCOMPARE(f.editor->text()->text(), QString{"successor"}); QCOMPARE(f.editor->phase(), Model::Closed);
        QVERIFY(!f.editor->busy());
    }
    void destroyedViewCannotCancelOrRetargetAnAdmittedSave()
    {
        Fixture f; f.drain(); f.editor->text()->edit("saved"); f.editor->save(); f.utility();
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
        f.editor->discard(); f.utility(); QCOMPARE(f.editor->text()->text(), QString{"original"});
    }
    void retiredProjectionDoesNotHoldAnySchedulerResource()
    {
        Fixture f; f.drain(); f.editor->text()->edit("unsaved"); f.utility();
        f.editor->retire(); f.drain();
        QVERIFY(f.execution.quiescent()); QCOMPARE(f.editor->phase(), Model::Closed);
        QCOMPARE(f.editor->text()->text(), QString{"unsaved"}); QVERIFY(!f.editor->busy());
        QCOMPARE(f.writes, 0);
    }
    void retirementReleasesResourcesEvenWhenItsDomainPostIsRejected()
    {
        Fixture f; f.drain(); f.execution.reject_utility = true;
        f.editor->retire(); f.drain();
        QVERIFY(f.execution.quiescent()); QCOMPARE(f.editor->phase(), Model::Closed); QVERIFY(!f.editor->busy());
        QCOMPARE(f.writes, 0);
    }
    void presentationCoalescesWithoutDroppingTheLatestOwnedInput()
    {
        Fixture f; f.drain();
        for (int i{0}; i < 64; ++i) {
            f.editor->text()->edit(QString::number(i));
            while (f.execution.step(Lane::Utility)) {}
            QVERIFY(f.gui.size() <= 1);
        }
        f.publish(); QCOMPARE(f.editor->text()->text(), QString{"63"}); QVERIFY(f.editor->dirty());
        f.editor->save(); QVERIFY(f.editor->busy()); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"63"});
    }
    void aRejectedEditCannotMakeSaveUseAnOlderProjection()
    {
        Fixture f; f.drain(); f.execution.reject_utility = true;
        f.editor->text()->edit("latest literal"); f.drain();
        QCOMPARE(f.writes, 0); QCOMPARE(f.editor->text()->text(), QString{"latest literal"});
        f.editor->save(); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"latest literal"});
        QCOMPARE(f.editor->saveResult().value, QString{"latest literal"});
    }
    void overBudgetInputMustBeCorrectedBeforeAnOlderDraftCanSave()
    {
        Fixture f; f.drain(); f.editor->text()->edit("draft"); f.utility();
        f.editor->text()->edit(QString(32769, 'x')); QVERIFY(!f.editor->valid());
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 0); QCOMPARE(f.persisted, QString{"original"});
        f.editor->text()->edit("draft"); f.utility(); QVERIFY(f.editor->valid());
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"draft"});
    }
    void writerTransportExceptionCannotEraseOrReplayItsAcceptedEffect()
    {
        Fixture f; f.drain(); f.throw_after_admission = true;
        f.editor->text()->edit("accepted"); f.editor->save(); f.utility();
        QCOMPARE(f.writes, 0); QCOMPARE(f.editor->phase(), Model::Uncertain);
        QVERIFY(!f.editor->saveResult().outcomeKnown);
        f.editor->save(); f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.persisted, QString{"accepted"});
        QVERIFY(f.editor->saveResult().outcomeKnown); QVERIFY(f.editor->saveResult().persisted);
    }
    void rootOrdinaryClosureRejectsANewSaveFromAnOldEditor()
    {
        Fixture f; f.drain(); f.editor->text()->edit("late"); f.utility();
        f.accepting = false;
        QSignalSpy refused{f.editor.get(), &Model::commandRejected};
        f.editor->save(); f.drain();
        QCOMPARE(f.writes, 0); QCOMPARE(f.persisted, QString{"original"});
        QCOMPARE(refused.size(), 1); QCOMPARE(f.editor->error().code, TextEditError::Refused);
        QCOMPARE(f.editor->phase(), Model::Editing); QCOMPARE(f.editor->text()->text(), QString{"late"});
    }
    void originalAdmittedSaveSurvivesRootOrdinaryClosure()
    {
        Fixture f; f.drain(); f.editor->text()->edit("accepted"); f.editor->save();
        QCOMPARE(f.writes, 0); f.accepting = false; f.drain();
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
            original.execution.reject_utility = true; original.editor->refresh();
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
                worker_turn = true; const bool stepped = (f.execution.step(Lane::Core) || f.execution.step(Lane::Utility)); worker_turn = false;
                if (!stepped) break;
            }
            // Hold every queued GUI publication inside this original signal.
            QCOMPARE(released, 1); QVERIFY(released_on_worker); QVERIFY(f.execution.quiescent());
        });
        f.execution.reject_utility = true; f.editor->refresh();
        QCOMPARE(rejected, 1); QCOMPARE(released, 1); QVERIFY(released_on_worker);
        QVERIFY(!f.gui.empty()); f.publish(); QVERIFY(f.execution.quiescent());
    }
    void realWorkersSeparateValidationPersistenceAndProjection()
    {
        execution::Executor executor{execution::Executor::Config{}};
        auto core = executor.makeQueue(Lane::Core);
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        const auto values = std::make_shared<execution::ValueCleanupQueue>(
            std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility)));
        const auto gui = QThread::currentThread();
        std::atomic<QThread*> read_thread{nullptr}, write_thread{nullptr}, validation_thread{nullptr};
        std::atomic<int> writes{0};
        QString persisted{"base"};
        QSemaphore core_drained, utility_drained, values_retired;
        const auto cleanup = qScopeGuard([&] {
            core.close();
            core.whenDrained([&] { core_drained.release(); });
            const bool drained_core = core_drained.tryAcquire(1, 5'000);
            utility->close();
            utility->whenDrained([&] { utility_drained.release(); });
            const bool drained_utility = utility_drained.tryAcquire(1, 5'000);
            values->close([&] { values_retired.release(); });
            const bool retired = values_retired.tryAcquire(1, 5'000);
            executor.stopAndJoin();
            QVERIFY(drained_core && drained_utility && retired);
        });
        Model::Ports ports;
        ports.postWork = [utility](auto work) { return utility->post(std::move(work)); };
        ports.value_cleanup = [values](auto release) { return values->hold(std::move(release)); };
        ports.validate = [&](const QString&) { validation_thread = QThread::currentThread(); return true; };
        ports.read = [&](auto done) {
            const bool accepted = core.post([&, done = std::move(done)] {
                read_thread = QThread::currentThread();
                utility->post([done, value = persisted] { done(Model::Read{value, {}}); });
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Closed};
        };
        ports.write = [&](const auto& attempt, auto done) {
            const bool accepted = core.post([&, done = std::move(done), value = attempt.values[0]] {
                write_thread = QThread::currentThread();
                ++writes;
                persisted = value;
                utility->post([done, value] {
                    done(Model::Write{persistentedit::Outcome::Saved, true, value, Model::Read{value, {}}, {}});
                });
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Closed};
        };
        auto editor = std::make_unique<Model>("original", std::move(ports));
        QTRY_COMPARE(editor->phase(), Model::Editing);
        editor->text()->edit("saved");
        editor->save();
        QTRY_VERIFY(editor->saveResult().persisted);
        QCOMPARE(writes.load(), 1);
        QVERIFY(read_thread.load());
        QVERIFY(read_thread.load() == write_thread.load());
        QVERIFY(write_thread.load() != gui);
        QVERIFY(validation_thread.load());
        QVERIFY(validation_thread.load() != gui);
        QVERIFY(validation_thread.load() != write_thread.load());
        QCOMPARE(editor->thread(), gui);
    }
};
BITCOINQML_REGISTER_QT_TEST(TextEditSessionModelTests)
#include <test_texteditsessionmodel.moc>
