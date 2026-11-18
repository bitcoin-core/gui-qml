// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/senddraftcontroller.h>
#include <test/qt_test_registry.h>

#include <QPointer>
#include <QtTest/QtTest>

namespace {
using State = PreparationOperation::State;

class FakePreparation : public PreparationOperation
{
public:
    explicit FakePreparation(bool with_password) : PreparationOperation(nullptr) { if (with_password) markPasswordRequested(); }
    void set(State state, QString error = {}) { publish(state, std::move(error)); }
    void unlock(const QString& passphrase) override { passphrases << passphrase; publish(State::Pending); }
    void cancel() override { ++cancels; publish(State::Cancelled); }
    QStringList passphrases;
    int cancels{0};
};

class FakeImport : public PsbtOperation
{
public:
    FakeImport() : PsbtOperation(Kind::Import, nullptr) {}
    void set(State state, int result) { publish(state, {}, {}, {}, result); }
    void cancel() override { publish(State::Cancelled); }
};

class FakeDraft : public SendDraftInterface
{
public:
    bool available() const override { return is_available; }
    QString draftRevision() const override { return revision; }
    bool selectionPending() const override { return selection_pending; }
    PreparationOperation* startPreparation(std::optional<QString> passphrase) override
    {
        ++preparations; last_passphrase = passphrase;
        if (refuse) return nullptr;
        last = new FakePreparation(passphrase.has_value());
        return last;
    }
    PsbtOperation* startImport(const QString& path) override
    {
        last_path = path;
        if (refuse) return nullptr;
        last_import = new FakeImport;
        return last_import;
    }
    void edit(QString next) { revision = std::move(next); Q_EMIT draftChanged(); }
    bool is_available{true}, selection_pending{false}, refuse{false};
    QString revision{QStringLiteral("1:1")}, last_path;
    std::optional<QString> last_passphrase;
    int preparations{0};
    QPointer<FakePreparation> last;
    QPointer<FakeImport> last_import;
};
} // namespace

class SendDraftControllerTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void preparesAndAsksForThePasswordOfTheSamePreparation()
    {
        FakeDraft draft; SendDraftController controller; controller.setDraftInterface(&draft);
        controller.prepare();
        QCOMPARE(draft.preparations, 1); QVERIFY(controller.preparing());
        auto* preparation = draft.last.data(); QVERIFY(preparation); QCOMPARE(preparation->parent(), &controller);
        controller.prepare(); QCOMPARE(draft.preparations, 1);
        preparation->set(State::NeedsUnlock);
        QVERIFY(controller.passwordRequested()); QVERIFY(!controller.preparing());
        controller.unlock(QStringLiteral("secret"));
        QCOMPARE(preparation->passphrases, QStringList{QStringLiteral("secret")});
        QVERIFY(controller.passwordRequested()); QVERIFY(controller.preparing());
        preparation->set(State::Ready);
        QCOMPARE(controller.preparationState(), State::Ready); QVERIFY(!controller.passwordRequested());
    }

    void aFailedPasswordRetryKeepsThePromptAndPreparesAgain()
    {
        FakeDraft draft; SendDraftController controller; controller.setDraftInterface(&draft);
        controller.prepare();
        draft.last->set(State::NeedsUnlock);
        controller.unlock(QStringLiteral("wrong"));
        draft.last->set(State::Failed, QStringLiteral("The wallet password is incorrect."));
        QVERIFY(controller.passwordRequested());
        QCOMPARE(controller.preparationError(), QStringLiteral("The wallet password is incorrect."));
        // A refused retry keeps the failed preparation and its prompt.
        draft.refuse = true;
        controller.unlock(QStringLiteral("again"));
        QCOMPARE(draft.preparations, 2); QVERIFY(controller.passwordRequested());
        draft.refuse = false;
        controller.unlock(QStringLiteral("right"));
        QCOMPARE(draft.preparations, 3); QCOMPARE(draft.last_passphrase, std::optional<QString>{QStringLiteral("right")});
        QVERIFY(controller.preparing()); QVERIFY(controller.passwordRequested());
        controller.cancelPreparation();
        QCOMPARE(controller.preparationState(), State::Cancelled); QVERIFY(!controller.passwordRequested());
    }

    void editingTheDraftHidingThePageOrAnotherWalletDropsTheResult_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("draft") << 0; QTest::newRow("inactive") << 1; QTest::newRow("interface") << 2; QTest::newRow("unavailable") << 3;
    }
    void editingTheDraftHidingThePageOrAnotherWalletDropsTheResult()
    {
        QFETCH(int, change);
        FakeDraft draft, other; SendDraftController controller; controller.setDraftInterface(&draft);
        controller.prepare();
        QPointer<FakePreparation> preparation{draft.last};
        controller.importPsbt(QStringLiteral("a.psbt"));
        QPointer<FakeImport> import{draft.last_import};
        controller.askSweepConsent(); QVERIFY(controller.sweepConsentRequested());
        if (change == 0) draft.edit(QStringLiteral("2:1"));
        if (change == 1) controller.setActive(false);
        if (change == 2) controller.setDraftInterface(&other);
        if (change == 3) { draft.is_available = false; Q_EMIT draft.availableChanged(); }
        QVERIFY(!preparation); QVERIFY(!import);
        QCOMPARE(controller.preparationState(), State::None);
        QCOMPARE(controller.importState(), PsbtOperation::State::None);
        QVERIFY(!controller.sweepConsentRequested());
    }

    void aSameDraftNoticeKeepsTheResult()
    {
        FakeDraft draft; SendDraftController controller; controller.setDraftInterface(&draft);
        controller.prepare();
        draft.last->set(State::Failed, QStringLiteral("failed"));
        draft.edit(QStringLiteral("1:1"));
        QCOMPARE(controller.preparationState(), State::Failed);
        controller.dismissPreparationResult();
        QCOMPARE(controller.preparationState(), State::None);
    }

    void aRefusedPreparationSaysAnotherActionRuns()
    {
        FakeDraft draft; draft.refuse = true; SendDraftController controller; controller.setDraftInterface(&draft);
        controller.prepare();
        QCOMPARE(controller.preparationState(), State::Failed); QVERIFY(!controller.preparationError().isEmpty());
        QVERIFY(!controller.passwordRequested());
    }

    void sweepConsentPreparesOnlyTheDraftItWasAskedFor()
    {
        FakeDraft draft; SendDraftController controller; controller.setDraftInterface(&draft);
        controller.askSweepConsent(); controller.confirmSweepConsent();
        QCOMPARE(draft.preparations, 1); QVERIFY(!controller.sweepConsentRequested());
        draft.last->set(State::Ready);
        controller.askSweepConsent(); draft.edit(QStringLiteral("2:1"));
        QVERIFY(!controller.sweepConsentRequested());
        controller.confirmSweepConsent();
        QCOMPARE(draft.preparations, 1);
    }

    void anImportResultWaitsForTheCoinSelection()
    {
        FakeDraft draft; SendDraftController controller; controller.setDraftInterface(&draft);
        controller.importPsbt(QStringLiteral("a.psbt"));
        QCOMPARE(draft.last_path, QStringLiteral("a.psbt")); QVERIFY(controller.importing());
        draft.selection_pending = true;
        draft.last_import->set(PsbtOperation::State::Imported, 2);
        QCOMPARE(controller.importState(), PsbtOperation::State::None);
        draft.selection_pending = false;
        controller.importPsbt(QStringLiteral("b.psbt"));
        draft.last_import->set(PsbtOperation::State::Imported, 2);
        QCOMPARE(controller.importState(), PsbtOperation::State::Imported); QCOMPARE(controller.importResult(), 2);
    }
};

BITCOINQML_REGISTER_QT_TEST(SendDraftControllerTests)
#include <test_senddraftcontroller.moc>
