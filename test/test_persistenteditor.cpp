// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/persistenteditor.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>

using Editor = persistentedit::Editor<QString, 2>;
using persistentedit::Phase;
using persistentedit::Outcome;
using persistentedit::CloseChoice;
namespace {
Editor Ready() { Editor editor{{"original-editor", "original-entity"}}; editor.observe({"base-a", "base-b"}, 1); return editor; }
Editor::Receipt Saved(const Editor::Attempt& attempt) { return {attempt, Outcome::Saved, attempt.dirty, attempt.values, {}}; }
}
class PersistentEditorTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void independentFieldsKeepUntouchedFreshValues()
    {
        auto editor = Ready(); QVERIFY(editor.edit(0, "my-a"));
        QVERIFY(editor.observe({"base-a", "fresh-b"}, 2));
        QCOMPARE(editor.draft()[0], QString{"my-a"}); QCOMPARE(editor.draft()[1], QString{"fresh-b"});
        QVERIFY(editor.canSave()); const auto attempt = editor.check(); QVERIFY(attempt);
        QCOMPARE(attempt->values[1], QString{"fresh-b"}); QVERIFY(!attempt->dirty[1]);
    }
    void allConflictsRequireIndividualConsent()
    {
        auto editor = Ready(); editor.edit(0, "my-a"); editor.edit(1, "my-b");
        editor.observe({"external-a", "external-b"}, 2);
        QCOMPARE(editor.phase(), Phase::Conflict); QVERIFY(!editor.check());
        QVERIFY(editor.resolve(0, true)); QVERIFY(!editor.check());
        QVERIFY(editor.resolve(1, false)); QVERIFY(editor.canSave());
        QCOMPARE(editor.draft()[0], QString{"my-a"}); QCOMPARE(editor.draft()[1], QString{"external-b"});
    }
    void originalReceiptDoesNotEraseSuccessor_data()
    {
        QTest::addColumn<bool>("newer_read"); QTest::addColumn<bool>("successor");
        QTest::newRow("plain") << false << false;
        QTest::newRow("successor") << false << true;
        QTest::newRow("newer-prewrite-read") << true << false;
        QTest::newRow("successor-and-prewrite-read") << true << true;
    }
    void originalReceiptDoesNotEraseSuccessor()
    {
        QFETCH(bool, newer_read); QFETCH(bool, successor);
        auto editor = Ready(); editor.edit(0, "saved-a"); auto attempt = editor.check(); QVERIFY(attempt);
        QVERIFY(editor.validated(attempt->token, true)); attempt = editor.active();
        if (newer_read) editor.observe({"prewrite-external-a", "fresh-b"}, 50);
        if (successor) editor.edit(0, "successor-a");
        QVERIFY(editor.finished(Saved(*attempt)));
        QCOMPARE(editor.base()[0], QString{"saved-a"});
        QCOMPARE(editor.draft()[0], successor ? QString{"successor-a"} : QString{"saved-a"});
        QCOMPARE(editor.dirty(), successor); QVERIFY(editor.conflicts().none());
        QCOMPARE(editor.base()[1], newer_read ? QString{"fresh-b"} : QString{"base-b"});
        QVERIFY(editor.result()); QCOMPARE(editor.result()->attempt.values[0], QString{"saved-a"});
    }
    void causalPostwriteObservationCanExposeARealConflict()
    {
        auto editor = Ready(); editor.edit(0, "saved"); auto attempt = editor.check(); QVERIFY(attempt);
        QVERIFY(editor.validated(attempt->token, true)); attempt = editor.active(); editor.edit(0, "successor");
        auto receipt = Saved(*attempt); receipt.observed = Editor::Observation{{"external-after-write", "base-b"}, 2, attempt->token};
        QVERIFY(editor.finished(receipt)); QCOMPARE(editor.base()[0], QString{"external-after-write"});
        QCOMPARE(editor.draft()[0], QString{"successor"}); QVERIFY(editor.conflicts()[0]);
        QCOMPARE(editor.result()->values[0], QString{"saved"});
    }
    void mismatchedAttemptOrUnfencedReadCannotInstall()
    {
        auto editor = Ready(); editor.edit(0, "save"); auto attempt = editor.check(); QVERIFY(attempt);
        QVERIFY(editor.validated(attempt->token, true)); attempt = editor.active();
        auto wrong = Saved(*attempt); wrong.attempt.identity.entity = "successor-entity";
        QVERIFY(!editor.finished(wrong)); QCOMPARE(editor.phase(), Phase::Saving);
        wrong = Saved(*attempt); wrong.observed = Editor::Observation{{"read", "base-b"}, 2};
        QVERIFY(!editor.finished(wrong)); QVERIFY(editor.finished(Saved(*attempt)));
        QVERIFY(!editor.finished(Saved(*attempt)));
    }
    void unknownNeverBecomesSuccessFromMatchingRead_data()
    {
        QTest::addColumn<bool>("partial"); QTest::newRow("unconfirmed") << false; QTest::newRow("known-partial") << true;
    }
    void unknownNeverBecomesSuccessFromMatchingRead()
    {
        QFETCH(bool, partial); auto editor = Ready(); editor.edit(0, "save-a"); editor.edit(1, "save-b");
        auto attempt = editor.check(); QVERIFY(attempt); QVERIFY(editor.validated(attempt->token, true)); attempt = editor.active();
        auto receipt = Saved(*attempt); receipt.outcome = Outcome::Unknown; receipt.persisted.reset(); receipt.persisted[0] = partial;
        QVERIFY(editor.finished(receipt)); QCOMPARE(editor.phase(), Phase::Uncertain); QVERIFY(editor.active());
        editor.observe(attempt->values, 2); QVERIFY(!editor.canSave()); QVERIFY(editor.active());
        QCOMPARE(editor.result()->outcome, Outcome::Unknown); QCOMPARE(editor.result()->persisted[0], partial);
        QVERIFY(editor.reconciled(Saved(*attempt))); QCOMPARE(editor.phase(), Phase::Editing); QVERIFY(!editor.dirty());
    }
    void retireIsIdempotentAndPreservesAdmittedOutcome_data()
    {
        QTest::addColumn<bool>("unknown"); QTest::newRow("saving") << false; QTest::newRow("uncertain") << true;
    }
    void retireIsIdempotentAndPreservesAdmittedOutcome()
    {
        QFETCH(bool, unknown); auto editor = Ready(); editor.edit(0, "save"); auto attempt = editor.check(); QVERIFY(attempt);
        QVERIFY(editor.validated(attempt->token, true)); attempt = editor.active();
        if (unknown) { auto receipt = Saved(*attempt); receipt.outcome = Outcome::Unknown; receipt.persisted.reset(); QVERIFY(editor.finished(receipt)); }
        editor.retire(); editor.retire(); QVERIFY(editor.active()); QVERIFY(!editor.edit(0, "late"));
        QVERIFY(editor.finished(Saved(*attempt))); QCOMPARE(editor.phase(), Phase::Closed); QVERIFY(editor.result()->persisted[0]);
        QVERIFY(editor.requestClose()); QVERIFY(editor.requestClose());
    }
    void promptConsentCapturesBaseAndEditRevisions_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("successor-edit") << 0; QTest::newRow("changed-base") << 1; QTest::newRow("conflict-resolution") << 2;
    }
    void promptConsentCapturesBaseAndEditRevisions()
    {
        QFETCH(int, change); auto editor = Ready(); editor.edit(0, "draft");
        if (change == 2) editor.observe({"external", "base-b"}, 2);
        QVERIFY(!editor.requestClose()); const auto token = editor.closeToken();
        QVERIFY(!editor.requestClose()); QCOMPARE(editor.closeToken(), token);
        if (change == 0) editor.edit(0, "successor");
        if (change == 1) editor.observe({"external", "base-b"}, 2);
        if (change == 2) editor.resolve(0, true);
        QVERIFY(editor.closeToken() != token); QVERIFY(!editor.answerClose(token, CloseChoice::Discard));
        QVERIFY(editor.dirty()); QVERIFY(editor.answerClose(editor.closeToken(), CloseChoice::KeepEditing));
        QVERIFY(!editor.closeQuestion()); QVERIFY(editor.dirty());
    }
    void failedSaveKeepsDraftAndValidationRejectsStaleCheck()
    {
        auto editor = Ready(); editor.edit(0, "attempt"); auto attempt = editor.check(); QVERIFY(attempt);
        editor.edit(0, "successor"); QVERIFY(!editor.validated(attempt->token, true));
        QCOMPARE(editor.result()->outcome, Outcome::Rejected); QCOMPARE(editor.draft()[0], QString{"successor"});
        attempt = editor.check(); QVERIFY(attempt); QVERIFY(editor.validated(attempt->token, true)); attempt = editor.active();
        QVERIFY(editor.finished({*attempt, Outcome::Failed, {}, {}, {}}));
        QCOMPARE(editor.draft()[0], QString{"successor"}); QCOMPARE(editor.base()[0], QString{"base-a"}); QVERIFY(editor.canSave());
    }
    void refusedSaveDoesNotCloseALaterSave()
    {
        auto editor = Ready(); editor.edit(0, "mine"); QVERIFY(!editor.requestClose());
        editor.observe({"external", "base-b"}, 2); // A conflict keeps the save from starting.
        QVERIFY(editor.answerClose(editor.closeToken(), CloseChoice::Save)); QVERIFY(!editor.check());
        QVERIFY(editor.resolve(0, true));
        auto attempt = editor.check(); QVERIFY(attempt); QVERIFY(editor.validated(attempt->token, true)); attempt = editor.active();
        QVERIFY(editor.finished(Saved(*attempt)));
        QCOMPARE(editor.phase(), Phase::Editing); QVERIFY(!editor.dirty());
    }
    void refusedSaveKeepsTheCloseOfASaveInFlight()
    {
        auto editor = Ready(); editor.edit(0, "mine"); QVERIFY(!editor.requestClose());
        QVERIFY(editor.answerClose(editor.closeToken(), CloseChoice::Save));
        auto attempt = editor.check(); QVERIFY(attempt); QVERIFY(!editor.check());
        QVERIFY(editor.validated(attempt->token, true)); QVERIFY(!editor.check()); attempt = editor.active();
        QVERIFY(editor.finished(Saved(*attempt))); QCOMPARE(editor.phase(), Phase::Closed);
    }
    void saveAndCloseOwnsOriginalResultWithoutClosingSuccessor_data()
    {
        QTest::addColumn<int>("schedule"); QTest::newRow("saved") << 0; QTest::newRow("failed") << 1; QTest::newRow("successor") << 2;
    }
    void saveAndCloseOwnsOriginalResultWithoutClosingSuccessor()
    {
        QFETCH(int, schedule); auto editor = Ready(); editor.edit(0, "attempt"); QVERIFY(!editor.requestClose());
        QVERIFY(editor.answerClose(editor.closeToken(), CloseChoice::Save)); auto attempt = editor.check(); QVERIFY(attempt);
        QVERIFY(editor.validated(attempt->token, true)); attempt = editor.active();
        QVERIFY(!editor.discard()); QVERIFY(!editor.requestClose());
        if (schedule == 2) editor.edit(0, "successor");
        auto result = Saved(*attempt); if (schedule == 1) { result.outcome = Outcome::Failed; result.persisted.reset(); }
        QVERIFY(editor.finished(result));
        QCOMPARE(editor.phase(), schedule == 0 ? Phase::Closed : Phase::Editing);
        QCOMPARE(editor.draft()[0], schedule == 2 ? QString{"successor"} : QString{"attempt"});
        QCOMPARE(editor.dirty(), schedule != 0); QVERIFY(editor.result());
    }
};
BITCOINQML_REGISTER_QT_TEST(PersistentEditorTests)
#include <test_persistenteditor.moc>
