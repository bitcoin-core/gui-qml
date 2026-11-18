// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/requestdeletecontroller.h>
#include <test/mocks/receiveeditortest.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QSignalSpy>

namespace {
struct Fixture {
    ReceiveEditorPorts ports;
    std::unique_ptr<WalletReceiveModel> receive{std::make_unique<WalletReceiveModel>(2, ports.all())};
    Fixture() { ports.drain(); }
    ~Fixture() { receive.reset(); ports.drain(); QVERIFY(ports.execution.quiescent()); }
    void ready() { QVERIFY(receive->ensureAddress()); ports.drain(); QVERIFY(receive->reservation()->available()); QVERIFY(receive->editor()->canSave()); }
    std::unique_ptr<RequestOpenOperation> open(const QString& id) { return std::unique_ptr<RequestOpenOperation>{receive->openRequest(id, nullptr)}; }
    void discardClose()
    {
        ports.drain(); QVERIFY(receive->editor()->closePrompt());
        receive->editor()->closePrompt()->answer(TextEditClosePrompt::Discard); ports.drain();
    }
};
}
class WalletReceiveModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void activitySharingKeepsTheSavedIdentityWithoutReplacingTheCreationDraft()
    {
        Fixture f; f.ready(); f.receive->editor()->message()->edit("Unrelated unsaved creation draft"); f.ports.drain();
        const auto creation = f.receive->editor()->editorId(); QObject view;
        auto* sharing = f.receive->sharingRequest("17", &view); QVERIFY(sharing); sharing->setActive(true); f.ports.drain();
        QVERIFY(sharing->canCopyUri());
        auto operation = std::unique_ptr<ShareOperationModel>{sharing->copyUri()}; f.ports.drain();
        QVERIFY(operation->result().performed); QCOMPARE(operation->result().requestId, QStringLiteral("17"));
        QCOMPARE(f.ports.clipboard, 1); QCOMPARE(f.ports.writes, 0);
        QCOMPARE(f.receive->editor()->editorId(), creation); QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("Unrelated unsaved creation draft"));
        QVERIFY(f.receive->editor()->dirty());
    }
    void paymentUseDuringAnActivityCopyRevokesTheOriginalQueuedHandoff()
    {
        Fixture f; QObject view;
        auto* sharing = f.receive->sharingRequest("17", &view); QVERIFY(sharing); sharing->setActive(true); f.ports.drain();
        auto operation = std::unique_ptr<ShareOperationModel>{sharing->copyUri()};
        f.ports.execution.drain(); // Hold GUI delivery after source preparation.
        f.ports.useSaved(17); f.receive->refresh(); f.ports.drain();
        QVERIFY(!operation->result().performed); QVERIFY(!operation->result().admitted);
        QCOMPARE(f.ports.clipboard, 0); QCOMPARE(operation->result().requestId, QStringLiteral("17"));
        QVERIFY(!sharing->canCopyUri()); QVERIFY(!sharing->canCopyQr());
    }
    void activityMenuSuccessorCannotReuseAnOriginalSharingGrant()
    {
        Fixture f; QObject view;
        auto second = f.ports.records.at(17); second.id = 29; second.address = "another-original-address"; f.ports.records.emplace(29, second);
        auto* first = f.receive->sharingRequest("17", &view); QVERIFY(first); first->setActive(true); f.ports.drain();
        auto operation = std::unique_ptr<ShareOperationModel>{first->copyUri()}; f.ports.execution.drain();
        auto* successor = f.receive->sharingRequest("29", &view); QVERIFY(successor); QVERIFY(successor != first);
        successor->setActive(true); f.ports.drain();
        QCOMPARE(first->phase(), RequestSharingModel::Closed); QVERIFY(!operation->result().performed);
        QCOMPARE(operation->result().requestId, QStringLiteral("17")); QCOMPARE(f.ports.clipboard, 0);
        QCOMPARE(successor->requestId(), QStringLiteral("29"));
        QCOMPARE(f.receive->sharingRequest("29", &view), successor); QCOMPARE(f.ports.writes, 0);
    }
    void openReceiptWaitsForTheActualEditorsOriginalSourceProjection()
    {
        Fixture f; const auto open = f.open("17"); QVERIFY(open); QVERIFY(open->pending());
        f.ports.execution.drain(); f.ports.publish();
        // The source read is known, but the newly created facade has not yet
        // consumed its own Domain projection. A modal cannot consume it yet.
        QVERIFY(open->pending()); f.ports.drain(); QCOMPARE(open->state(), RequestOpenOperation::State::Opened);
        auto* editor = open->editor(); QVERIFY(editor);
        QCOMPARE(editor->requestId(), QStringLiteral("17")); QCOMPARE(editor->phase(), PaymentRequestEditorModel::Editing);
        QCOMPARE(editor->message()->text(), QStringLiteral("original message")); QCOMPARE(f.ports.writes, 0);
    }
    void producerNoticeDuringAHeldOpenReadIsNotLost()
    {
        Fixture f; f.ports.records.at(17).current = false;
        const auto open = f.open("17"); QVERIFY(open);
        f.ports.execution.drain(); // Hold the old source's GUI delivery.
        f.ports.records.at(17).current = true; f.ports.epoch->advance(); f.receive->refresh();
        f.ports.drain(); QCOMPARE(open->state(), RequestOpenOperation::State::Opened);
        QCOMPARE(f.ports.writes, 0); QCOMPARE(f.ports.generations, 0);
    }
    void blankDraftReplacementDoesNotInventAnUnsavedFieldsQuestion()
    {
        Fixture f; f.ready(); QVERIFY(!f.receive->editor()->dirty());
        const auto original = f.receive->editor()->editorId(); QVERIFY(f.receive->useAddress("bcrt1q-selected")); f.ports.drain();
        QVERIFY(!f.receive->replacementPending()); QVERIFY(f.receive->editor()->editorId() != original);
        QCOMPARE(f.receive->reservation()->address(), QStringLiteral("bcrt1q-selected")); QCOMPARE(f.ports.writes, 0);
    }
    void cancelledOpenCannotCancelASuccessorForTheSameRequest()
    {
        Fixture f; f.ports.records.at(17).current = false;
        auto first = f.open("17"); QVERIFY(first); f.ports.drain();
        first->cancel(); QCOMPARE(first->state(), RequestOpenOperation::State::Cancelled);
        const auto second = f.open("17"); QVERIFY(second); QVERIFY(second->pending());
        first->cancel(); first.reset(); f.ports.drain(); QVERIFY(second->pending());
        f.ports.records.at(17).current = true; f.receive->refresh(); f.ports.drain();
        QCOMPARE(second->state(), RequestOpenOperation::State::Opened); QCOMPARE(f.ports.writes, 0);
    }
    void aRefusedDeleteReportsWhy()
    {
        Fixture f; RequestDeleteController deletion; deletion.setReceive(f.receive.get());
        QSignalSpy changed{&deletion, &RequestDeleteController::changed};
        QVERIFY(!deletion.request(QStringLiteral("not-an-id")));
        QVERIFY(deletion.failed()); QVERIFY(!deletion.asking()); QVERIFY(!deletion.error().isEmpty()); QVERIFY(changed.count() > 0);
        deletion.dismiss(); QVERIFY(!deletion.failed()); QVERIFY(deletion.error().isEmpty());
        // The receive model's own reason reaches the delete question's owner.
        std::vector<QPointer<PaymentRequestEditorModel>> open;
        while (auto* editor = f.receive->deleteRequest(QStringLiteral("17"))) open.emplace_back(editor);
        const auto reason = f.receive->error().message; QVERIFY(!reason.isEmpty());
        QVERIFY(!deletion.request(QStringLiteral("17")));
        QVERIFY(deletion.failed()); QCOMPARE(deletion.error(), reason);
        deletion.reset(); QVERIFY(!deletion.failed());
        for (const auto& editor : open) if (editor) editor->retire();
        f.ports.drain();
    }
    void aDeletedOpenStopsAndAllowsTheNextOne()
    {
        Fixture f; f.ports.records.at(17).current = false;
        auto first = f.open("17"); QVERIFY(first); f.ports.drain();
        QVERIFY(!f.open("17")); // One open at a time.
        first.reset();
        const auto second = f.open("17"); QVERIFY(second);
        f.ports.records.at(17).current = true; f.receive->refresh(); f.ports.drain();
        QCOMPARE(second->state(), RequestOpenOperation::State::Opened);
    }
    void rejectedTypeDisarmsThePreviouslyDisplayedReservationImmediately()
    {
        Fixture f; f.ready(); const auto address = f.receive->reservation()->address();
        f.ports.next_generation = walletrequests::Error::Unsupported;
        QVERIFY(f.receive->chooseAddressType("legacy")); QVERIFY(!f.receive->editor()->canSave());
        f.receive->editor()->save(); f.ports.drain();
        QCOMPARE(f.receive->reservation()->generationError(), walletrequests::Error::Unsupported);
        QVERIFY(!f.receive->reservation()->available()); QCOMPARE(f.receive->reservation()->address(), address);
        QCOMPARE(f.ports.writes, 0); QVERIFY(!f.receive->ensureAddress()); f.ports.drain(); QCOMPARE(f.ports.generations, 1);
    }
    void incomingPaymentDoesNotRotateOrRebindAComposedDraft()
    {
        Fixture f; f.ready(); f.receive->editor()->message()->edit("composed before payment"); f.ports.drain();
        const auto original = f.receive->reservation()->address(); const auto editor = f.receive->editor()->editorId();
        f.ports.useDraft(); f.receive->refresh(); f.ports.drain();
        QCOMPARE(f.ports.generations, 1); QCOMPARE(f.receive->reservation()->address(), original); QVERIFY(!f.receive->reservation()->available());
        QVERIFY(!f.receive->ensureAddress()); f.ports.drain(); QCOMPARE(f.ports.generations, 1);
        QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("composed before payment"));
        QVERIFY(f.receive->nextAddress()); f.ports.drain(); QCOMPARE(f.ports.generations, 2);
        QCOMPARE(f.receive->editor()->editorId(), editor); QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("composed before payment"));
    }
    void observingThePaidAddressDoesNotCancelTheNextOne()
    {
        Fixture f; f.ready(); const auto paid = f.receive->reservation()->address();
        f.ports.useDraft(); f.receive->refresh(); f.ports.drain();
        QVERIFY(f.receive->reservation()->historicallyUsed()); QVERIFY(!f.receive->reservation()->available());
        // A wallet notification reads the shown, paid address while the next
        // address is generated. It describes the old address, not the new one.
        QVERIFY(f.receive->nextAddress()); f.receive->refresh(); f.ports.domain(); f.ports.drain();
        QCOMPARE(f.ports.generations, 2); QVERIFY(f.receive->reservation()->available());
        QVERIFY(f.receive->reservation()->address() != paid); QVERIFY(!f.receive->reservation()->historicallyUsed());
        QCOMPARE(f.receive->reservation()->error().code, TextEditError::None);
    }
    void dirtyAddressNavigationCapturesOneOriginalTarget()
    {
        Fixture f; f.ready(); f.receive->editor()->message()->edit("keep until answer"); f.ports.drain();
        const auto original_editor = f.receive->editor()->editorId();
        QVERIFY(f.receive->useAddress("bcrt1q-selected-a")); f.ports.drain(); QVERIFY(f.receive->replacementPending());
        QVERIFY(!f.receive->useAddress("bcrt1q-selected-b")); QCOMPARE(f.receive->editor()->editorId(), original_editor);
        f.discardClose();
        QVERIFY(!f.receive->replacementPending()); QCOMPARE(f.receive->reservation()->address(), QStringLiteral("bcrt1q-selected-a"));
        QVERIFY(f.receive->editor()->editorId() != original_editor); QCOMPARE(f.ports.writes, 0);
    }
    void sameAddressNavigationPreservesTheExistingDraft()
    {
        Fixture f; f.ready(); f.receive->editor()->message()->edit("same-address draft"); f.ports.drain();
        const auto original = f.receive->editor(); QVERIFY(f.receive->useAddress(f.receive->reservation()->address())); f.ports.drain();
        QCOMPARE(f.receive->editor(), original); QVERIFY(!f.receive->replacementPending());
        QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("same-address draft")); QCOMPARE(f.ports.writes, 0);
    }
    void keepEditingCancelsTheCapturedReplacement()
    {
        Fixture f; f.ready(); f.receive->editor()->message()->edit("original draft"); f.ports.drain();
        auto* original = f.receive->editor(); QVERIFY(f.receive->useTemplate("17")); f.ports.drain();
        QVERIFY(original->closePrompt()); original->closePrompt()->answer(TextEditClosePrompt::KeepEditing); f.ports.drain();
        QVERIFY(!f.receive->replacementPending()); QCOMPARE(f.receive->editor(), original);
        QCOMPARE(original->message()->text(), QStringLiteral("original draft")); QCOMPARE(f.ports.generations, 1); QCOMPARE(f.ports.writes, 0);
    }
    void templateWaitsForABusyStore_data()
    {
        QTest::addColumn<bool>("notice_during_read");
        QTest::newRow("after-read") << false;
        QTest::newRow("during-read") << true;
    }
    void templateWaitsForABusyStore()
    {
        QFETCH(bool, notice_during_read);
        Fixture f; f.ready(); f.ports.records.at(17).current = false;
        QSignalSpy rejected{f.receive.get(), &WalletReceiveModel::commandRejected};
        QVERIFY(f.receive->useTemplate("17"));
        if (notice_during_read) f.ports.drainWorkers(); else f.ports.drain();
        QVERIFY(f.receive->replacementPending()); QCOMPARE(rejected.count(), 0);
        f.ports.records.at(17).current = true; f.ports.epoch->advance(); f.receive->refresh(); f.ports.drain();
        QCOMPARE(rejected.count(), 0); QVERIFY(!f.receive->replacementPending());
        QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("original message")); QCOMPARE(f.ports.writes, 0);
    }
    void templateTypeNeverPersistsTheWalletPreferenceAndNewDraftUsesDefault()
    {
        Fixture f; f.ready(); f.ports.records.at(17).address_type = "legacy";
        f.receive->editor()->message()->edit("original draft needing consent"); f.ports.drain();
        QVERIFY(f.receive->useTemplate("17")); f.discardClose();
        QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("original message"));
        QCOMPARE(f.receive->reservation()->addressType(), QStringLiteral("legacy")); QCOMPARE(f.ports.preferences, 0);
        const auto template_editor = f.receive->editor()->editorId();
        QVERIFY(f.receive->newRequest()); f.discardClose();
        QVERIFY(f.receive->editor()->editorId() != template_editor); QCOMPARE(f.receive->reservation()->addressType(), QStringLiteral("bech32"));
        QCOMPARE(f.ports.preferences, 0); QVERIFY(f.receive->editor()->message()->text().isEmpty()); QCOMPARE(f.receive->editor()->amount()->satoshi(), CAmount{0});
    }
    void explicitPreferenceFailureKeepsTheAddressAndSeparateActualFacts()
    {
        Fixture f; f.ready(); f.ports.preference_saves = false;
        QVERIFY(f.receive->chooseAddressType("legacy")); f.ports.drain();
        QVERIFY(f.receive->reservation()->available()); QVERIFY(f.receive->reservation()->generationPersisted());
        QVERIFY(f.receive->reservation()->preferenceRequested()); QVERIFY(!f.receive->reservation()->preferenceSaved());
        QCOMPARE(f.receive->reservation()->generationError(), walletrequests::Error::PreferencePersistence);
        QCOMPARE(f.receive->reservation()->addressType(), QStringLiteral("legacy")); QVERIFY(f.receive->editor()->canSave());
    }
    void authorizationCancellationClearsErrorAndNeverReplaysOnViewReopen()
    {
        Fixture f; f.ports.next_generation = walletrequests::Error::IncorrectPassphrase;
        QVERIFY(f.receive->ensureAddress()); f.ports.drain();
        auto* reservation = f.receive->reservation(); QVERIFY(reservation->prompt());
        QPointer<ReceiveAuthorizationPrompt> old{reservation->prompt()}; QVERIFY(!old->errorText().isEmpty());
        old->cancel(); f.ports.drain(); QCOMPARE(reservation->phase(), ReceiveReservationModel::Cancelled); QVERIFY(reservation->error().message.isEmpty());
        QVERIFY(!f.receive->ensureAddress()); f.ports.drain(); QCOMPARE(f.ports.generations, 0); QCOMPARE(f.ports.authorizations, 0);
        if (old) old->answer("stale answer");
        f.ports.drain(); QCOMPARE(f.ports.authorizations, 0);
        reservation->retry(); f.ports.drain(); QVERIFY(reservation->available()); QVERIFY(reservation->error().message.isEmpty()); QCOMPARE(f.ports.generations, 1);
    }
    void metadataReconciliationDoesNotRetireAnOriginalPasswordQuestion()
    {
        Fixture f; f.ports.next_generation = walletrequests::Error::NeedsUnlock; QVERIFY(f.receive->ensureAddress()); f.ports.drain();
        auto* reservation = f.receive->reservation(); QPointer<ReceiveAuthorizationPrompt> question{reservation->prompt()}; QVERIFY(question);
        reservation->observe(false, false); QVERIFY(question->active());
        question->answer("accepted original password"); f.ports.drain();
        QVERIFY(reservation->available()); QCOMPARE(f.ports.authorizations, 1); QVERIFY(!reservation->prompt());
    }
    void failedOpenDoesNotPublishAPhantomEditor()
    {
        Fixture f; f.ports.records.erase(17); const auto open = f.open("17"); QVERIFY(open); f.ports.drain();
        QCOMPARE(open->state(), RequestOpenOperation::State::Failed); QVERIFY(!open->editor());
        QVERIFY(!open->error().message.isEmpty()); QCOMPARE(f.ports.writes, 0);
    }
    void requestAgainGuardsBothOriginalModalAndExistingCreationDraft()
    {
        Fixture f; f.ready(); f.receive->editor()->message()->edit("main draft"); f.ports.drain();
        const auto open = f.open("17"); QVERIFY(open); f.ports.drain();
        QPointer<PaymentRequestEditorModel> modal{open->editor()}; QVERIFY(modal);
        modal->privateNote()->edit("modal draft"); f.ports.drain();
        QVERIFY(f.receive->requestAgain(modal)); f.ports.drain(); QVERIFY(modal->closePrompt());
        QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("main draft"));
        modal->closePrompt()->answer(TextEditClosePrompt::Discard); f.ports.drain();
        QVERIFY(f.receive->editor()->closePrompt()); QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("main draft"));
        f.discardClose(); QCOMPARE(f.receive->editor()->message()->text(), QStringLiteral("original message"));
        QCOMPARE(f.ports.preferences, 0); QCOMPARE(f.ports.writes, 0);
    }
    void delayedDeleteOfOriginalRequestDoesNotCloseANewerEditor()
    {
        Fixture f; auto second = f.ports.records.at(17); second.id = 19; f.ports.records.emplace(19, second);
        auto* deletion = f.receive->deleteRequest("17"); QVERIFY(deletion); f.ports.drain(); QVERIFY(deletion->closePrompt());
        deletion->closePrompt()->answer(TextEditClosePrompt::Save); f.ports.domain(); QCOMPARE(f.ports.writes, 0);
        const auto open = f.open("19"); QVERIFY(open);
        // Hold the original Core delete while the independent source/facade
        // reads and their GUI projections complete their controlled turns.
        for (size_t turn{0}; open->pending() && turn < 32; ++turn) f.ports.domain();
        QPointer<PaymentRequestEditorModel> newer{open->editor()}; QVERIFY(newer); QCOMPARE(f.ports.writes, 0); QVERIFY(!deletion->saveResult().present);
        f.ports.drain(); QVERIFY(deletion->saveResult().erased); QVERIFY(!f.ports.records.contains(17));
        QVERIFY(newer->available()); QCOMPARE(newer->requestId(), QStringLiteral("19")); QVERIFY(f.ports.records.contains(19));
    }
    void sharingFactoryKeepsStableIdentityPerViewAndSeparateViews()
    {
        Fixture f; const auto open = f.open("17"); QVERIFY(open); f.ports.drain();
        QPointer<PaymentRequestEditorModel> saved{open->editor()}; QVERIFY(saved);
        QObject first_view, second_view;
        auto* first = f.receive->sharing(saved, &first_view); QVERIFY(first);
        for (int index{0}; index < 100; ++index) QCOMPARE(f.receive->sharing(saved, &first_view), first);
        auto* second = f.receive->sharing(saved, &second_view); QVERIFY(second && second != first);
        first->setActive(true); second->setActive(true); f.ports.drain();
        first->setActive(false); QVERIFY(!first->canCopyUri()); QVERIFY(second->canCopyUri());
        first->retire(); auto* reopened = f.receive->sharing(saved, &first_view);
        QVERIFY(reopened && reopened != first); QCOMPARE(reopened->phase(), RequestSharingModel::Inactive);
        reopened->setActive(true); first->setActive(true); f.ports.drain();
        QCOMPARE(first->phase(), RequestSharingModel::Closed); QVERIFY(reopened->canCopyUri()); QVERIFY(second->canCopyUri());
    }
};
BITCOINQML_REGISTER_QT_TEST(WalletReceiveModelTests)
#include <test_walletreceivemodel.moc>
