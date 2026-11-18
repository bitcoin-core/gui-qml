// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/admissionhookscheduler.h>
#include <test/mocks/threadrecordingscheduler.h>
#include <test/qt_test_registry.h>
#include <key_io.h>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <QPersistentModelIndex>
#include <QScopeGuard>
#include <limits>
#include <stdexcept>

namespace {
QString Address(unsigned char seed = 4) { return QString::fromStdString(EncodeDestination(PKHash{uint160{std::vector<unsigned char>(20, seed)}})); }
struct Fixture {
    ManualRoutedExecution commands;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<SendRecipientsListModel> recipients;
    bool fail_post{false};
    explicit Fixture(senddraft::Limits limits = {}) : recipients(std::make_unique<SendRecipientsListModel>(nullptr, limits)) {
        recipients->setOpening({}, {commands.scheduler, [this](std::function<void()> turn) {
            if (fail_post) throw std::runtime_error{"GUI transport unavailable"};
            publications.push_back(std::move(turn));
        }});
        finish();
    }
    ~Fixture() {
        if (recipients) recipients->closeAdmission();
        recipients.reset();
        // Independent bounded GUI retirement outlives the facade, including a
        // deliberately throwing primary transport and its real queued fallback.
        for (int turn{0}; turn < 1024 && (turn == 0 || commands.scheduler->pending()); ++turn) {
            finish(); QCoreApplication::processEvents(); commands.drain();
        }
    }
    bool publishOne() { if (publications.empty()) return false; auto turn = std::move(publications.front()); publications.pop_front(); turn(); return true; }
    void finish() { do { commands.drain(); while (publishOne()) {} } while (!commands.queuedIdle() || !commands.activeIdle()); }
    void fill(SendRecipient* recipient, unsigned char seed, CAmount amount) { recipient->setAddress(Address(seed)); recipient->amount()->setSatoshi(amount); }
};
}
class SendRecipientTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void sameCanonicalAmountIsAFixedIntent_data()
    {
        QTest::addColumn<int>("producer");
        QTest::newRow("canonical-equal") << 0;
        QTest::newRow("typed-equal") << 1;
        QTest::newRow("imported-equal") << 2;
        QTest::newRow("incomplete-edit") << 3;
    }
    void sameCanonicalAmountIsAFixedIntent()
    {
        QFETCH(int, producer); Fixture f;
        auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000); f.finish();
        recipient->chooseMaximum(); f.finish();
        QCOMPARE(f.recipients->maximumRecipient(), recipient);
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Maximum);
        const auto prior = f.recipients->draftSnapshot(); QVERIFY(prior);
        if (producer == 0) recipient->amount()->setSatoshi(100000);
        if (producer == 1) recipient->amount()->editInput(recipient->amount()->inputText());
        if (producer == 2) recipient->applyImportedPayment({Address(), "public", "message", {}, CAmount{100000}, false});
        if (producer == 3) recipient->amount()->editInput("1.");
        QVERIFY(!f.recipients->draftSnapshot()); QVERIFY(f.recipients->validationPending());
        f.commands.drain(); // Domain consumption is independent of GUI publication.
        QVERIFY(!f.recipients->draftSnapshot());
        f.finish();
        QCOMPARE(f.recipients->maximumRecipient(), nullptr);
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Fixed);
        QCOMPARE(recipient->amount()->satoshi(), CAmount{100000});
        QVERIFY(prior->maximum == recipient->identity()); // Immutable captured intent.
        QVERIFY(f.recipients->draftSnapshot());
        QCOMPARE(f.recipients->allValid(), producer != 3);
    }
    void importedAddressNormalizationRetainsOnlyItsOwnedProvenance_data()
    {
        QTest::addColumn<bool>("supersede");
        QTest::newRow("original-normalization") << false;
        QTest::newRow("newer-address-before-publication") << true;
    }
    void importedAddressNormalizationRetainsOnlyItsOwnedProvenance()
    {
        QFETCH(bool, supersede);
        Fixture f; auto* recipient = f.recipients->currentRecipient();
        recipient->applyImportedPayment({QString(600, QLatin1Char{' '}) + Address(), "Alice", "Lunch", {}, CAmount{100000}, false});
        QCOMPARE(recipient->address()->address(), Address(4)); // Normalized immediately.
        f.commands.drain();
        if (supersede) recipient->setAddress(Address(5));
        f.finish();
        QCOMPARE(recipient->address()->address(), Address(supersede ? 5 : 4));
        QCOMPARE(recipient->hasPaymentRequest(), !supersede);
        QCOMPARE(recipient->paymentRequestLabel(), supersede ? QString{} : QStringLiteral("Alice"));
        QCOMPARE(recipient->message(), supersede ? QString{} : QStringLiteral("Lunch"));
        QVERIFY(f.recipients->allValid());
    }
    void settledAddressFormattingCannotCreateAnUnownedPendingRevision()
    {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000); f.finish();
        const auto revision = f.recipients->revision();
        const auto snapshot = f.recipients->draftSnapshot(); QVERIFY(snapshot);
        recipient->address()->setAddress(recipient->address()->formattedAddress());
        QCOMPARE(f.recipients->revision(), revision);
        QVERIFY(!f.recipients->validationPending()); QVERIFY(f.recipients->allValid());
        QCOMPARE(f.recipients->draftSnapshot(), snapshot);
        recipient->address()->setAddress(recipient->address()->address());
        QCOMPARE(f.recipients->revision(), revision); QVERIFY(f.recipients->allValid());
    }
    void coupledModePublicationCannotExposeAnEarlierReadySnapshot()
    {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000); f.finish();
        recipient->chooseMaximum(); f.finish(); bool observed{false};
        connect(recipient, &SendRecipient::amountModeChanged, f.recipients.get(), [&] {
            if (recipient->amountMode() != SendRecipient::AmountMode::Fixed) return;
            observed = true; QVERIFY(f.recipients->validationPending());
            QVERIFY(!f.recipients->draftSnapshot()); QVERIFY(!f.recipients->allValid());
        });
        recipient->amount()->setSatoshi(100000); f.finish(); QVERIFY(observed);
        QVERIFY(!f.recipients->validationPending()); QVERIFY(f.recipients->allValid());
        QCOMPARE(f.recipients->maximumRecipient(), nullptr); QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Fixed);
    }
    void heldMaximumPublicationCannotOverrideNewerFixedText()
    {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000); f.finish();
        recipient->chooseMaximum(); f.commands.drain(); QVERIFY(!f.publications.empty());
        recipient->amount()->editInput("0.00100000");
        while (f.publishOne()) {} // Old mode publication cannot install itself.
        QVERIFY(!f.recipients->draftSnapshot());
        f.finish(); QCOMPARE(recipient->amount()->inputText(), QStringLiteral("0.00100000"));
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Fixed);
        QCOMPARE(f.recipients->maximumRecipient(), nullptr);
    }
    void movingMaximumClearsOnlyTheFormerComputedAmount()
    {
        Fixture f; auto* first = f.recipients->currentRecipient(); f.fill(first, 4, 5000);
        f.recipients->add(); auto* second = f.recipients->currentRecipient(); f.fill(second, 5, 3000); f.finish();
        first->chooseMaximum(); f.finish();
        second->chooseMaximum(); f.finish();
        QCOMPARE(first->amountMode(), SendRecipient::AmountMode::Fixed);
        QCOMPARE(first->amount()->satoshi(), CAmount{0}); QVERIFY(!first->amount()->isSet());
        QCOMPARE(second->amountMode(), SendRecipient::AmountMode::Maximum);
        QCOMPARE(second->amount()->satoshi(), CAmount{3000});
        QCOMPARE(f.recipients->maximumRecipient(), second);
        QVERIFY(!f.recipients->allValid()); QVERIFY(!f.recipients->validationError().isEmpty());
    }
    void explicitFixedEditBeforeHandoverPreservesItsAmount()
    {
        Fixture f; auto* first = f.recipients->currentRecipient(); f.fill(first, 4, 5000);
        f.recipients->add(); auto* second = f.recipients->currentRecipient(); f.fill(second, 5, 3000); f.finish();
        first->chooseMaximum(); f.finish();
        first->amount()->setSatoshi(5000); second->chooseMaximum(); // No intermediate Domain or GUI delivery.
        f.finish();
        QCOMPARE(first->amount()->satoshi(), CAmount{5000}); QCOMPARE(first->amountMode(), SendRecipient::AmountMode::Fixed);
        QCOMPARE(second->amountMode(), SendRecipient::AmountMode::Maximum); QVERIFY(f.recipients->allValid());
    }
    void reselectedMaximumOwnsFreshObservationsAfterUnpublishedHandover()
    {
        Fixture f; auto* first = f.recipients->currentRecipient(); f.fill(first, 4, 5000);
        f.recipients->add(); auto* second = f.recipients->currentRecipient(); f.fill(second, 5, 3000); f.finish();
        first->chooseMaximum(); f.finish();
        second->chooseMaximum(); f.commands.drain();
        first->chooseMaximum(); f.commands.drain(); f.finish();
        QCOMPARE(f.recipients->maximumRecipient(), first);
        QCOMPARE(first->amountMode(), SendRecipient::AmountMode::Maximum);
        QCOMPARE(second->amountMode(), SendRecipient::AmountMode::Fixed);
        QVERIFY(!second->amount()->isSet());
        second->amount()->setSatoshi(1000); f.finish();
        first->amount()->setSatoshi(7000, BitcoinAmount::Origin::Model); f.finish();
        QCOMPARE(first->amount()->satoshi(), CAmount{7000});
        const auto snapshot = f.recipients->draftSnapshot(); QVERIFY(snapshot);
        QCOMPARE(snapshot->recipients[snapshot->positions.at(first->identity())].amount, CAmount{7000});
        QCOMPARE(snapshot->maximum, first->identity()); QVERIFY(f.recipients->allValid());
    }
    void nonAmountEditDuringHandoverCannotReinstateComputedMoney()
    {
        Fixture f; auto* first = f.recipients->currentRecipient(); f.fill(first, 4, 5000);
        f.recipients->add(); auto* second = f.recipients->currentRecipient(); f.fill(second, 5, 3000); f.finish();
        first->chooseMaximum(); f.finish();
        second->chooseMaximum(); f.commands.drain(); // Hold the coupled GUI handover.
        QVERIFY(!f.publications.empty()); QCOMPARE(first->amount()->satoshi(), CAmount{5000});
        QVERIFY(first->editPrivateNote("memo captured during handover"));
        f.commands.drain(); f.finish();
        QCOMPARE(first->amount()->satoshi(), CAmount{0}); QVERIFY(!first->amount()->isSet());
        QCOMPARE(first->label(), QStringLiteral("memo captured during handover"));
        QCOMPARE(first->amountMode(), SendRecipient::AmountMode::Fixed);
        QCOMPARE(second->amountMode(), SendRecipient::AmountMode::Maximum);
        QVERIFY(!f.recipients->allValid());
        first->amount()->setSatoshi(5000); f.finish();
        QCOMPARE(first->amount()->satoshi(), CAmount{5000}); QVERIFY(f.recipients->allValid());
    }
    void lateValidationCannotRestoreTheFormerRemainder()
    {
        Fixture f; auto* first = f.recipients->currentRecipient(); f.fill(first, 4, 5000);
        f.recipients->add(); auto* second = f.recipients->currentRecipient(); f.fill(second, 5, 3000); f.finish();
        first->chooseMaximum(); f.finish();
        first->amount()->setSatoshi(6000, BitcoinAmount::Origin::Model); f.commands.drain(); // Old recipient validation GUI handoff held.
        second->chooseMaximum(); f.commands.drain();
        while (f.publishOne()) {} f.finish();
        QCOMPARE(first->amount()->satoshi(), CAmount{0}); QVERIFY(!first->amount()->isSet());
        QCOMPARE(second->amountMode(), SendRecipient::AmountMode::Maximum);
        const auto draft = f.recipients->draftSnapshot(); QVERIFY(draft);
        QCOMPARE(draft->recipients[draft->positions.at(first->identity())].amount, CAmount{0});
    }
    void importBatchPreservesPrivateNoteAndOwnsProvenance()
    {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000);
        recipient->editPrivateNote("private"); f.finish(); recipient->chooseMaximum(); f.finish();
        const auto revision = f.recipients->revision().toULongLong();
        recipient->applyImportedPayment({Address(), "Alice", "public message", "implicit note", CAmount{100000}, false});
        QCOMPARE(f.recipients->revision().toULongLong(), revision + 1);
        QVERIFY(!f.recipients->draftSnapshot()); f.finish();
        QCOMPARE(recipient->label(), QStringLiteral("private")); QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("Alice"));
        QCOMPARE(recipient->message(), QStringLiteral("public message")); QVERIFY(recipient->hasPaymentRequest());
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Fixed);
        recipient->setAddress(Address(7)); f.commands.drain(); f.finish();
        QVERIFY(!recipient->hasPaymentRequest()); QVERIFY(recipient->message().isEmpty()); QVERIFY(recipient->paymentRequestLabel().isEmpty());
        QCOMPARE(recipient->label(), QStringLiteral("private"));
        recipient->applyImportedPayment({Address(7), "Bob", "new message", "approved replacement", {}, true}); f.finish();
        QCOMPARE(recipient->label(), QStringLiteral("approved replacement"));
        recipient->clearImportedMetadata(); f.finish(); QVERIFY(!recipient->hasPaymentRequest());
        QCOMPARE(recipient->label(), QStringLiteral("approved replacement"));
    }
    void importProjectionRetainsNewerPublicIntentAndOwnerLoss_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::newRow("close-draft") << 0; QTest::newRow("delete-draft") << 1;
        QTest::newRow("newer-fixed-amount") << 2; QTest::newRow("newer-import") << 3; QTest::newRow("clear-draft") << 4;
    }
    void importProjectionRetainsNewerPublicIntentAndOwnerLoss()
    {
        QFETCH(int, outcome); Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000); f.finish();
        bool handled{false};
        connect(recipient->amount(), &BitcoinAmount::inputChanged, f.recipients.get(), [&] {
            if (handled) return;
            handled = true;
            if (outcome == 0) f.recipients->closeAdmission();
            if (outcome == 1) f.recipients.reset();
            if (outcome == 2) recipient->amount()->setSatoshi(9000);
            if (outcome == 3) recipient->applyImportedPayment({Address(7), "newer", "newer message", {}, CAmount{7000}, false});
            if (outcome == 4) recipient->clear();
        });
        recipient->applyImportedPayment({Address(6), "older", "older message", {}, CAmount{6000}, false});
        QVERIFY(handled); f.finish();
        if (outcome == 0) { QVERIFY(f.recipients); QCOMPARE(f.recipients->state(), SendRecipientsListModel::DraftState::Retired); }
        else if (outcome == 1) QVERIFY(!f.recipients);
        else if (outcome == 2) { QCOMPARE(recipient->amount()->satoshi(), CAmount{9000}); QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Fixed); }
        else if (outcome == 3) { QCOMPARE(recipient->amount()->satoshi(), CAmount{7000}); QCOMPARE(recipient->paymentRequestLabel(), QStringLiteral("newer")); QCOMPARE(recipient->address()->address(), Address(7)); }
        else { QVERIFY(!recipient->amount()->isSet()); QVERIFY(!recipient->hasPaymentRequest()); QVERIFY(recipient->address()->address().isEmpty()); }
    }
    void rejectedImportNeverPartiallyReplacesARecipient_data()
    {
        QTest::addColumn<int>("invalid"); QTest::newRow("public-label-budget") << 0;
        QTest::newRow("message-budget") << 1; QTest::newRow("money-range") << 2;
    }
    void rejectedImportNeverPartiallyReplacesARecipient()
    {
        QFETCH(int, invalid); Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000);
        recipient->editPrivateNote("private"); f.finish(); recipient->chooseMaximum(); f.finish();
        senddraft::PaymentRequest request{Address(7), "new", "new message", "new note", CAmount{200000}, true};
        if (invalid == 0) request.label = QString(int(senddraft::Limits::note) + 1, 'x');
        if (invalid == 1) request.message = QString(int(senddraft::Limits::message) + 1, 'x');
        if (invalid == 2) request.amount = MAX_MONEY + 1;
        recipient->applyImportedPayment(request); f.finish();
        QCOMPARE(recipient->address()->address(), Address()); QCOMPARE(recipient->amount()->satoshi(), CAmount{100000});
        QCOMPARE(recipient->label(), QStringLiteral("private")); QVERIFY(!recipient->hasPaymentRequest());
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Maximum);
        QVERIFY(!recipient->inputBudgetError().isEmpty()); QVERIFY(!f.recipients->allValid());
    }
    void rowBudgetRefusesBeforeCreatingAnExtraProjection()
    {
        Fixture f; QSignalSpy inserted{f.recipients.get(), &QAbstractItemModel::rowsInserted};
        QSignalSpy rejected{f.recipients.get(), &SendRecipientsListModel::commandRejected};
        for (int row{1}; row < f.recipients->maximumRecipients() + 16; ++row) f.recipients->add();
        QCOMPARE(f.recipients->count(), 25); QCOMPARE(inserted.size(), 24); QCOMPARE(rejected.size(), 16);
        QVERIFY(!f.recipients->canAddRecipient()); QVERIFY(!f.recipients->budgetError().isEmpty()); f.finish();
        const auto first_id = f.recipients->data(f.recipients->index(0), SendRecipientsListModel::RecipientIdRole).toString();
        f.recipients->removeRecipient(first_id); f.finish(); QCOMPARE(f.recipients->count(), 24); QVERIFY(f.recipients->canAddRecipient());
        QVERIFY(f.recipients->data(f.recipients->index(0), SendRecipientsListModel::RecipientIdRole).toString() != first_id);
    }
    void recipientChurnHasABoundedAdmissionWindow()
    {
        Fixture f; QSignalSpy rejected{f.recipients.get(), &SendRecipientsListModel::commandRejected};
        for (size_t turn{0}; turn < senddraft::Limits::pending_recipients; ++turn) {
            if (turn % 2 == 0) {
                f.recipients->add(); QCOMPARE(f.recipients->count(), 2);
                f.recipients->removeAt(1); QCOMPARE(f.recipients->count(), 1);
            } else f.recipients->clear();
        }
        QVERIFY(!f.recipients->canAddRecipient());
        auto* original = f.recipients->currentRecipient();
        QVERIFY(original->editPrivateNote("retain across admission refusal"));
        QSignalSpy inserted{f.recipients.get(), &QAbstractItemModel::rowsInserted};
        QSignalSpy reset{f.recipients.get(), &QAbstractItemModel::modelReset};
        f.recipients->add(); f.recipients->clear();
        QCOMPARE(rejected.size(), 2); QCOMPARE(inserted.size(), 0); QCOMPARE(reset.size(), 0);
        QCOMPARE(f.recipients->currentRecipient(), original);
        QCOMPARE(original->label(), QStringLiteral("retain across admission refusal"));
        QVERIFY(!f.recipients->budgetError().isEmpty());
        f.finish(); QVERIFY(f.recipients->canAddRecipient());
        f.recipients->add(); f.finish(); QCOMPARE(f.recipients->count(), 2);
        QVERIFY(f.recipients->budgetError().isEmpty());
    }
    void modeBackpressureCannotLoseAnExplicitFixedEdit()
    {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000); f.finish();
        QSignalSpy rejected{f.recipients.get(), &SendRecipientsListModel::commandRejected};
        for (size_t i{0}; i <= senddraft::Limits::pending_modes; ++i) recipient->chooseMaximum();
        QCOMPARE(rejected.size(), 1);
        recipient->amount()->setSatoshi(100000); // Fixed intent remains replaceable input, not a full mode mailbox.
        f.finish(); QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Fixed); QCOMPARE(f.recipients->maximumRecipient(), nullptr);
        QCOMPARE(recipient->amount()->satoshi(), CAmount{100000}); QVERIFY(f.recipients->allValid());
    }
    void privateNoteBudgetRejectsTheWholeInputAndRecovers()
    {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000);
        recipient->editPrivateNote("original"); f.finish();
        recipient->editPrivateNote(QString(int(senddraft::Limits::note) + 1, 'x')); f.finish();
        QCOMPARE(recipient->label(), QStringLiteral("original")); QVERIFY(!recipient->inputBudgetError().isEmpty()); QVERIFY(!f.recipients->allValid());
        recipient->editPrivateNote("recovered"); f.finish(); QCOMPARE(recipient->label(), QStringLiteral("recovered"));
        QVERIFY(recipient->inputBudgetError().isEmpty()); QVERIFY(f.recipients->allValid());
    }
    void draftStatesAndRetirementPreserveTheCapturedRevision()
    {
        Fixture f; QCOMPARE(f.recipients->state(), SendRecipientsListModel::DraftState::Empty);
        const auto id = f.recipients->draftId(); auto* recipient = f.recipients->currentRecipient();
        recipient->amount()->editInput("1."); f.finish(); QCOMPARE(f.recipients->state(), SendRecipientsListModel::DraftState::Editing);
        f.fill(recipient, 4, 100000); f.finish(); QCOMPARE(f.recipients->state(), SendRecipientsListModel::DraftState::Ready);
        const auto captured = f.recipients->draftSnapshot(); QVERIFY(captured); const auto revision = f.recipients->revision();
        f.recipients->clear(); f.finish(); QCOMPARE(f.recipients->draftId(), id);
        QCOMPARE(f.recipients->state(), SendRecipientsListModel::DraftState::Empty); QVERIFY(f.recipients->revision() != revision);
        QCOMPARE(captured->recipients.front().amount, CAmount{100000});
        f.recipients->currentRecipient()->chooseMaximum(); f.recipients->closeAdmission(); f.finish();
        QCOMPARE(f.recipients->state(), SendRecipientsListModel::DraftState::Retired); QCOMPARE(f.recipients->count(), 0);
        QCOMPARE(captured->recipients.front().amount, CAmount{100000});
    }
    void amountModeCannotTargetAForeignRecipient()
    {
        Fixture f, foreign; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000); f.finish();
        const auto revision = f.recipients->revision();
        QSignalSpy refused{f.recipients.get(), &SendRecipientsListModel::commandRejected};
        f.recipients->chooseAmountMode(foreign.recipients->currentRecipient(), SendRecipient::AmountMode::Maximum);
        QCOMPARE(refused.size(), 1); QCOMPARE(f.recipients->revision(), revision);
        QCOMPARE(f.recipients->maximumRecipient(), nullptr); f.finish();
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Fixed); QCOMPARE(recipient->amount()->satoshi(), CAmount{100000});
    }
    void formattingAndDisplayUnitNeverBecomeAmountIntent()
    {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 100000); f.finish();
        recipient->chooseMaximum(); f.finish();
        for (const auto unit : {BitcoinAmount::Unit::BTC, BitcoinAmount::Unit::MilliBTC, BitcoinAmount::Unit::MicroBTC, BitcoinAmount::Unit::SAT}) {
            recipient->amount()->setUnit(unit); recipient->amount()->format(); recipient->amount()->setSatoshi(100000, BitcoinAmount::Origin::Model); f.finish();
            QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Maximum); QCOMPARE(recipient->amount()->satoshi(), CAmount{100000});
        }
    }
    void validationAdmissionKeepsOriginalEditorIntent_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::newRow("quiet") << 0; QTest::newRow("delete") << 1; QTest::newRow("newer-input") << 2;
    }
    void validationAdmissionKeepsOriginalEditorIntent()
    {
        QFETCH(int, outcome);
        ManualRoutedExecution env;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
        std::deque<std::function<void()>> posts;
        auto recipient = std::make_unique<SendRecipient>(nullptr);
        const auto settle = [&] {
            for (int turn{0}; turn < 256 && (turn == 0 || !env.queuedIdle() || !posts.empty()); ++turn) {
                env.drain(); while (!posts.empty()) { auto work = std::move(posts.front()); posts.pop_front(); work(); }
            }
        };
        const auto cleanup = qScopeGuard([&] { scheduler->disarm(); if (recipient) recipient->closeAdmission(); recipient.reset(); settle(); });
        recipient->setOpening({}, {scheduler, [&](auto work) { posts.push_back(std::move(work)); }}); settle();
        recipient->setAddress(Address()); settle();
        QSignalSpy draft{recipient.get(), &SendRecipient::draftChanged};
        auto trigger = scheduler->armSubmit(execution::Lane::Domain, 1, [&] {
            if (outcome == 0) recipient->closeAdmission();
            else if (outcome == 1) recipient.reset();
            else recipient->amount()->setSatoshi(9000);
        });
        recipient->amount()->setSatoshi(7000);
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        // No original admission continuation may emit after quiet/deletion or
        // duplicate the distinct newer input's draft notification.
        QCOMPARE(draft.count(), outcome == 2 ? 1 : 0);
        settle();
        if (outcome == 2) {
            QVERIFY(recipient->isValid()); QVERIFY(recipient->validationSnapshot());
            QCOMPARE(recipient->validationSnapshot()->amount, CAmount{9000});
        } else if (outcome == 0) { QVERIFY(recipient); QVERIFY(!recipient->validationPending()); }
        else QVERIFY(!recipient);
        scheduler->disarm(); if (recipient) recipient->closeAdmission(); recipient.reset(); settle();
        QCOMPARE(env.scheduler->pending(), size_t{0});
    }
    void cleanupAdmissionKeepsOriginalValidationOwner_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::newRow("quiet") << 0;
        QTest::newRow("delete") << 1;
        QTest::newRow("same-port-successor") << 2;
    }
    void cleanupAdmissionKeepsOriginalValidationOwner()
    {
        QFETCH(int, outcome);
        ManualRoutedExecution env;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
        std::deque<std::function<void()>> old_posts, new_posts;
        size_t old_posted{0}, new_posted{0};
        auto recipient = std::make_unique<SendRecipient>(nullptr);
        const auto identity = recipient->identity();
        const auto settle = [&] {
            for (int turn = 0; turn < 128 && (turn == 0 || !env.queuedIdle() || !old_posts.empty() || !new_posts.empty()); ++turn) {
                env.drain();
                for (auto* queue : {&old_posts, &new_posts}) while (!queue->empty()) {
                    auto work = std::move(queue->front()); queue->pop_front(); work();
                }
            }
        };
        const auto cleanup = qScopeGuard([&] { scheduler->disarm(); if (recipient) recipient->closeAdmission(); recipient.reset(); settle(); });
        const auto trigger = scheduler->armCleanup(1, [&] {
            if (outcome == 0) recipient->closeAdmission();
            else if (outcome == 1) recipient.reset();
            else recipient->setOpening({}, {scheduler, [&](auto turn) { ++new_posted; new_posts.push_back(std::move(turn)); }});
        });
        recipient->setOpening({}, {scheduler, [&](auto turn) { ++old_posted; old_posts.push_back(std::move(turn)); }});
        QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        if (outcome == 2) {
            recipient->setAddress(Address()); recipient->amount()->setSatoshi(7'000); settle();
            QVERIFY(recipient->isValid()); QVERIFY(recipient->validationSnapshot());
            QCOMPARE(recipient->validationSnapshot()->token, identity);
            QCOMPARE(recipient->validationSnapshot()->amount, CAmount{7'000});
            QCOMPARE(old_posted, size_t{0}); QVERIFY(new_posted > 0);
        } else if (outcome == 0) { QVERIFY(recipient); QVERIFY(!recipient->validationPending()); }
        else QVERIFY(!recipient);
        scheduler->disarm(); if (recipient) recipient->closeAdmission(); recipient.reset(); settle();
        QCOMPARE(env.scheduler->pending(), size_t{0});
    }
    void quietCloseDuringQtMutation_data() {
        QTest::addColumn<int>("mutation"); QTest::addColumn<bool>("about");
        for (int mutation{0}; mutation < 3; ++mutation)
            for (bool about : {false, true}) QTest::newRow(qPrintable(QString::number(mutation) + (about ? "-about" : "-ended"))) << mutation << about;
    }
    void quietCloseDuringQtMutation() {
        QFETCH(int, mutation); QFETCH(bool, about); Fixture f; f.recipients->add(); f.finish();
        auto* model = f.recipients.get(); bool closed{false}; int at_end{-1};
        const auto close = [&] {
            const auto rows = model->rowCount(); model->closeAdmission();
            QCOMPARE(model->rowCount(), rows); closed = true;
        };
        if (mutation == 0) {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeInserted : &QAbstractItemModel::rowsInserted, this, close);
            connect(model, &QAbstractItemModel::rowsInserted, this, [&] { at_end = model->rowCount(); }); model->add();
        } else if (mutation == 1) {
            connect(model, about ? &QAbstractItemModel::rowsAboutToBeRemoved : &QAbstractItemModel::rowsRemoved, this, close);
            connect(model, &QAbstractItemModel::rowsRemoved, this, [&] { at_end = model->rowCount(); }); model->removeAt(1);
        } else {
            connect(model, about ? &QAbstractItemModel::modelAboutToBeReset : &QAbstractItemModel::modelReset, this, close);
            connect(model, &QAbstractItemModel::modelReset, this, [&] { at_end = model->rowCount(); }); model->clear();
        }
        QVERIFY(closed); QCOMPARE(at_end, mutation == 0 ? 3 : mutation == 1 ? 1 : about ? 2 : 1);
        QCOMPARE(model->rowCount(), 0);
        f.recipients.reset(); f.finish(); QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void quietCloseFencesAllEditorsThenRetiresBoundedBatchesWithoutFacade() {
        Fixture f{{128}};
        for (int i{0}; i < 96; ++i) f.recipients->add();
        f.finish();
        int destroyed{0};
        const auto count = f.recipients->count(); QCOMPARE(count, 97);
        const auto rows = f.recipients->recipients();
        QPointer<SendRecipient> first{rows.first()};
        auto* amount = first->amount(); amount->setSatoshi(4000); f.finish();
        QSignalSpy intents{first.data(), &SendRecipient::inputIntentSubmitted};
        QSignalSpy drafts{first.data(), &SendRecipient::draftChanged};
        QSignalSpy validity{first.data(), &SendRecipient::isValidChanged};
        for (auto* recipient : rows)
            connect(recipient, &QObject::destroyed, this, [&] { ++destroyed; });
        f.recipients->closeAdmission();
        QCOMPARE(f.recipients->count(), 0); QVERIFY(first);
        const auto address = first->address()->address();
        first->setAddress(Address(9));
        QCOMPARE(first->address()->address(), address);
        QSignalSpy address_signals{first.data(), &SendRecipient::addressChanged};
        first->address()->pasteAddress(QString(100000, QLatin1Char(' ')) + Address(9), 0, 0);
        QVERIFY(address_signals.empty());
        // A closed recipient ignores its amount: edits submit no intent,
        // start no validation and queue no work.
        const auto pending = f.commands.scheduler->pending();
        amount->setSatoshi(9000); amount->fromDisplay("5"); amount->setUnit(BitcoinAmount::Unit::SAT);
        amount->resetInput(); amount->format(); amount->paste("9", 0, 0);
        QVERIFY(intents.empty()); QVERIFY(drafts.empty()); QVERIFY(validity.empty());
        QVERIFY(!first->validationPending()); QVERIFY(!first->isValid());
        QCOMPARE(f.commands.scheduler->pending(), pending);
        f.commands.scheduler->closeAdmission(); f.commands.drain();
        QCOMPARE(destroyed, 0); QVERIFY(f.commands.scheduler->pending() > 0);
        QVERIFY(f.publishOne()); QCOMPARE(destroyed, 32); QVERIFY(first);
        f.recipients.reset(); // Remaining GUI handoffs have an independent owner.
        f.finish();
        QCOMPARE(destroyed, count); QVERIFY(!first);
        QCOMPARE(f.commands.scheduler->pending(), size_t{0});
    }
    void validationAndAggregateDoNotBecomeReadyBeforeTheirGuiPublication() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000);
        QVERIFY(recipient->validationPending()); QVERIFY(!recipient->isValid()); QVERIFY(f.recipients->validationPending());
        f.commands.drain(); QVERIFY(!recipient->isValid()); QVERIFY(!f.recipients->allValid());
        while (f.publishOne()) {}
        QVERIFY(recipient->isValid()); // The row result is now acknowledged.
        QVERIFY(f.recipients->validationPending()); QVERIFY(!f.recipients->draftSnapshot());
        f.finish(); QVERIFY(f.recipients->allValid());
        const auto snapshot = f.recipients->draftSnapshot(); QVERIFY(snapshot);
        QCOMPARE(snapshot->total, CAmount{1000}); QCOMPARE(snapshot->recipients.size(), size_t{1});
        QCOMPARE(snapshot->recipients[0].destination, DecodeDestination(Address().toStdString()));
    }
    void interimAggregateRetainsPendingRowsUntilTheirOriginalValidationPublishes() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000);
        f.commands.drain(); QVERIFY(f.publications.size() >= 2);
        auto row_publication = std::move(f.publications.front()); f.publications.pop_front();
        while (f.publishOne()) {}
        QVERIFY(recipient->validationPending());
        QVERIFY(f.recipients->validationPending()); QVERIFY(!f.recipients->draftSnapshot());
        row_publication(); f.finish();
        QVERIFY(!f.recipients->validationPending()); QVERIFY(f.recipients->allValid());
        QCOMPARE(f.recipients->draftSnapshot()->pending_count, 0);
    }
    void staleDraftCannotAcquireAnEarlierValidDestination() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000);
        f.commands.drain();
        recipient->setAddress("malformed");
        while (f.publishOne()) {}
        QVERIFY(!recipient->isValid()); QVERIFY(!f.recipients->allValid());
        f.finish(); QVERIFY(!recipient->addressError().isEmpty()); QVERIFY(!f.recipients->allValid());
    }
    void dustAndAmountDecisionsAreOwnedByDomain_data() {
        QTest::addColumn<qint64>("amount"); QTest::addColumn<QString>("error");
        QTest::newRow("dust") << qint64{1} << QStringLiteral("too small");
        QTest::newRow("zero") << qint64{0} << QStringLiteral("greater than zero");
        QTest::newRow("over-max") << qint64{MAX_MONEY + 1} << QStringLiteral("maximum limit");
    }
    void dustAndAmountDecisionsAreOwnedByDomain() {
        QFETCH(qint64, amount); QFETCH(QString, error);
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, amount); f.finish();
        QVERIFY(!recipient->isValid()); QVERIFY2(recipient->amountError().contains(error), qPrintable(recipient->amountError()));
        QVERIFY(!f.recipients->allValid());
    }
    void duplicateDestinationsAndTotalsUseTheImmutableDomainDraft() {
        Fixture f; f.fill(f.recipients->currentRecipient(), 4, 1000);
        f.recipients->add(); f.fill(f.recipients->currentRecipient(), 4, 2000); f.finish();
        QCOMPARE(f.recipients->totalAmountSatoshi(), qint64{3000}); QVERIFY(!f.recipients->allValid());
        QCOMPARE(f.recipients->validationError(), QStringLiteral("Recipient addresses must be unique."));
        const auto old = f.recipients->draftSnapshot(); QVERIFY(old);
        f.recipients->currentRecipient()->setAddress(Address(5)); f.finish();
        QVERIFY(f.recipients->allValid()); QCOMPARE(old->recipients[1].address, Address(4));
        QCOMPARE(f.recipients->draftSnapshot()->recipients[1].address, Address(5));
    }
    void changedEditorFieldsCannotReuseTheLastValidatedDraft() {
        Fixture f; auto* recipient=f.recipients->currentRecipient();f.fill(recipient,4,1000);f.finish();
        const auto original=f.recipients->draftSnapshot();QVERIFY(original);QVERIFY(original->all_valid);
        recipient->setLabel("new label");recipient->setMessage("new message");f.fill(recipient,5,2000);
        QVERIFY(recipient->validationPending());QVERIFY(!recipient->isValid());QVERIFY(!f.recipients->draftSnapshot());
        f.commands.drain();QVERIFY(recipient->validationPending());QVERIFY(!f.recipients->draftSnapshot());
        f.finish();const auto current=f.recipients->draftSnapshot();QVERIFY(current);QVERIFY(current->all_valid);
        QCOMPARE(current->recipients[0].address,Address(5));QCOMPARE(current->recipients[0].amount,CAmount{2000});
        QCOMPARE(current->recipients[0].label,QStringLiteral("new label"));QCOMPARE(current->recipients[0].message,QStringLiteral("new message"));
        QCOMPARE(original->recipients[0].address,Address(4));QCOMPARE(original->recipients[0].amount,CAmount{1000});
        QVERIFY(original->recipients[0].label.isEmpty());QVERIFY(original->recipients[0].message.isEmpty());
    }
    void aggregateRejectsMonetaryOverflowWithoutPublishingAZeroEstimate_data() {
        QTest::addColumn<qint64>("amount");
        QTest::newRow("monetary-range") << qint64{MAX_MONEY};
        QTest::newRow("signed-overflow") << std::numeric_limits<qint64>::max();
    }
    void aggregateRejectsMonetaryOverflowWithoutPublishingAZeroEstimate() {
        QFETCH(qint64, amount); Fixture f; f.fill(f.recipients->currentRecipient(), 4, amount);
        f.recipients->add(); f.fill(f.recipients->currentRecipient(), 5, amount); f.finish();
        const auto snapshot = f.recipients->draftSnapshot(); QVERIFY(snapshot);
        QVERIFY(!snapshot->amount_total_valid); QVERIFY(!snapshot->all_valid);
        QVERIFY(snapshot->total != 0); QCOMPARE(snapshot->total_text, QStringLiteral("—"));
        QVERIFY(!snapshot->error.isEmpty());
    }
    void manyRecipientsHaveNoCleanupSlotCapAndNativePresentationYields() {
        Fixture f{{128}}; f.fill(f.recipients->currentRecipient(), 4, 1000);
        for (int i = 1; i < 65; ++i) { f.recipients->add(); f.fill(f.recipients->currentRecipient(), static_cast<unsigned char>(4 + i), 1000); }
        f.finish(); QVERIFY(f.recipients->allValid()); QCOMPARE(f.recipients->draftSnapshot()->recipients.size(), size_t{65});
        QPersistentModelIndex front{f.recipients->index(0)};
        const auto original = front.data(SendRecipientsListModel::RecipientRole);
        f.recipients->clearToFront();
        QCOMPARE(f.recipients->count(), 33); QVERIFY(f.recipients->validationPending());
        f.finish(); QCOMPARE(f.recipients->count(), 1); QVERIFY(front.isValid());
        QCOMPARE(front.data(SendRecipientsListModel::RecipientRole), original);
        QVERIFY(f.recipients->allValid()); QCOMPARE(f.recipients->totalAmountSatoshi(), qint64{1000});
    }
    void supersedingUnitIntentCannotBeReplacedByAnOlderQueuedChunk() {
        Fixture f{{128}};
        for (int i = 1; i < 65; ++i) f.recipients->add();
        f.finish(); f.recipients->setCurrentIndex(0);
        f.recipients->currentRecipient()->amount()->setUnit(BitcoinAmount::Unit::SAT);
        QCOMPARE(f.recipients->data(f.recipients->index(32), SendRecipientsListModel::AmountUnitLabelRole).toString(), QStringLiteral("BTC"));
        f.recipients->currentRecipient()->amount()->setUnit(BitcoinAmount::Unit::MilliBTC);
        f.finish();
        const auto rows = f.recipients->recipients();
        for (const auto* recipient : rows) QCOMPARE(recipient->amount()->unit(), BitcoinAmount::Unit::MilliBTC);
    }
    void removedRecipientHeldOutcomeCannotRejoinTheList() {
        Fixture f; f.fill(f.recipients->currentRecipient(), 4, 1000); f.finish();
        f.recipients->add(); auto* removed = f.recipients->currentRecipient(); f.fill(removed, 5, 2000);
        const auto token = removed->identity();
        f.commands.drain(); f.recipients->removeAt(1); f.finish();
        QCOMPARE(f.recipients->count(), 1); QCOMPARE(f.recipients->totalAmountSatoshi(), qint64{1000});
        QVERIFY(f.recipients->allValid()); QCOMPARE(f.recipients->draftSnapshot()->positions.count(token), size_t{0});
    }
    void rejectedDomainWorkNeverAdmitsTheLastValidDraft() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000); f.finish();
        QVERIFY(f.recipients->allValid());
        f.commands.rejectLane(execution::Lane::Domain);
        recipient->setAddress(Address(5)); f.finish();
        QVERIFY(!recipient->isValid()); QVERIFY(!f.recipients->allValid()); QVERIFY(!recipient->addressError().isEmpty());
        recipient->retryPublications(); f.finish(); QVERIFY(recipient->isValid()); QVERIFY(f.recipients->allValid());
    }
    void lostGuiPublicationRetriesWithoutRepeatingFieldIntent() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fail_post = true;
        f.fill(recipient, 4, 1000); f.commands.drain(); QVERIFY(!recipient->isValid());
        f.fail_post = false; f.recipients->retryPublications(); f.finish();
        QVERIFY(recipient->isValid()); QVERIFY(f.recipients->allValid());
        QCOMPARE(recipient->address()->address(), Address());
    }
    void quietCloseAndObserverLossDoNotPublishAcceptedValidation() {
        Fixture f; auto* recipient = f.recipients->currentRecipient(); f.fill(recipient, 4, 1000);
        QSignalSpy changed{recipient, &SendRecipient::isValidChanged};
        recipient->closeAdmission(); f.recipients->closeAdmission(); f.recipients.reset();
        f.commands.drain(); while (f.publishOne()) {}
        QCOMPARE(changed.count(), 0);
    }
    void realValidationUsesSharedDomainThread() {
        BackendExecutor executor;
        const auto recording = std::make_shared<ThreadRecordingScheduler>(executor.scheduler());
        {
            SendRecipient recipient{nullptr}; recipient.setOpening({}, {recording, {}});
            recipient.setAddress(Address()); recipient.amount()->setSatoshi(1000);
            QTRY_VERIFY(!recipient.validationPending()); QVERIFY(recipient.isValid());
            QVERIFY(recording->thread(execution::Lane::Domain));
            QVERIFY(recording->thread(execution::Lane::Domain) != QThread::currentThread());
            recipient.closeAdmission();
        }
        executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(SendRecipientTests)
#include <test_sendrecipient.moc>
