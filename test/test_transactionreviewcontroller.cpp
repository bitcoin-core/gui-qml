// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transactionreviewcontroller.h>
#include <test/qt_test_registry.h>

#include <QPointer>
#include <QSignalSpy>
#include <QtTest/QtTest>

namespace {
using State = SendOperation::State;

class FakeSend : public SendOperation
{
public:
    explicit FakeSend(bool sign_only) : SendOperation(sign_only, nullptr) {}
    void set(State state, QString error = {}, QString txid = {}) { publish(state, std::move(error), std::move(txid)); }
    void confirm() override { ++confirms; }
    void unlock(const QString& passphrase) override { passphrases << passphrase; }
    void cancel() override { ++cancels; publish(State::Cancelled); }
    int confirms{0}, cancels{0};
    QStringList passphrases;
};

class FakePsbt : public PsbtOperation
{
public:
    explicit FakePsbt(Kind kind) : PsbtOperation(kind, nullptr) {}
    void set(State state, QString error = {}, QString path = {}, QString txid = {}) { publish(state, std::move(error), std::move(path), std::move(txid)); }
    void cancel() override { ++cancels; publish(State::Cancelled); }
    int cancels{0};
};

class FakeReviews : public TransactionReviewInterface
{
public:
    QObject* currentReview() const override { return review; }
    bool available() const override { return is_available; }
    SendOperation* startSend(bool sign_only) override
    {
        ++starts;
        if (refuse) return nullptr;
        last = new FakeSend(sign_only);
        return last;
    }
    PsbtOperation* startPsbt(PsbtOperation::Kind kind, const QString& path, const QString& signer_result) override
    {
        ++psbt_starts; last_path = path; last_signer_result = signer_result;
        if (refuse_psbt) return nullptr;
        last_psbt = new FakePsbt(kind);
        return last_psbt;
    }
    void replaceReview(QObject* next) { review = next; Q_EMIT currentReviewChanged(); }
    void setAvailable(bool available) { is_available = available; Q_EMIT availableChanged(); }
    QObject* review{nullptr};
    QString signer_result;
    bool is_available{true}, refuse{false};
    int starts{0}, psbt_starts{0};
    bool refuse_psbt{false};
    QString last_path, last_signer_result;
    QPointer<FakeSend> last;
    QPointer<FakePsbt> last_psbt;
};
} // namespace

class TransactionReviewControllerTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void startsOneSendAndReplacesItOnlyAfterItFinishes()
    {
        QObject review; FakeReviews reviews; reviews.review = &review;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        QCOMPARE(controller.sendState(), State::None);
        QSignalSpy changed{&controller, &TransactionReviewController::sendChanged};
        controller.send();
        QCOMPARE(reviews.starts, 1);
        const QPointer<FakeSend> first{reviews.last};
        QVERIFY(first); QCOMPARE(first->parent(), &controller);
        first->set(State::Pending);
        QCOMPARE(controller.sendState(), State::Pending); QVERIFY(controller.sending()); QVERIFY(!controller.signOnly());
        QVERIFY(changed.count() >= 2);
        // One send at a time.
        controller.signOnlyTransaction();
        QCOMPARE(reviews.starts, 1);
        first->set(State::Committed, {}, QStringLiteral("txid"));
        QCOMPARE(controller.txid(), QStringLiteral("txid")); QVERIFY(!controller.sending());
        controller.signOnlyTransaction();
        QCOMPARE(reviews.starts, 2); QVERIFY(!first); QVERIFY(controller.signOnly());
    }

    void reportsPromptsAndForwardsAnswers()
    {
        QObject review; FakeReviews reviews; reviews.review = &review;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.send();
        auto* send = reviews.last.data(); QVERIFY(send);
        QCOMPARE(controller.prompt(), TransactionReviewController::Prompt::None);
        send->set(State::NeedsConfirmation);
        QCOMPARE(controller.prompt(), TransactionReviewController::Prompt::SweepConsent);
        controller.confirm(); QCOMPARE(send->confirms, 1);
        send->set(State::NeedsUnlock, QStringLiteral("Enter your wallet password."));
        QCOMPARE(controller.prompt(), TransactionReviewController::Prompt::Password);
        QCOMPARE(controller.sendError(), QStringLiteral("Enter your wallet password."));
        controller.unlock(QStringLiteral("secret")); QCOMPARE(send->passphrases, QStringList{QStringLiteral("secret")});
        // The password prompt stays up while the password is checked.
        send->set(State::Pending);
        QCOMPARE(controller.prompt(), TransactionReviewController::Prompt::Password);
        send->set(State::Failed, QStringLiteral("failed"));
        QCOMPARE(controller.prompt(), TransactionReviewController::Prompt::None);
        QCOMPARE(controller.sendError(), QStringLiteral("failed"));
    }

    void anotherReviewWalletOrHiddenPageCancelsThePrompt_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("review") << 0; QTest::newRow("inactive") << 1; QTest::newRow("interface") << 2;
        QTest::newRow("unavailable") << 3; QTest::newRow("cancel") << 4;
    }
    void anotherReviewWalletOrHiddenPageCancelsThePrompt()
    {
        QFETCH(int, change);
        QObject review, other; FakeReviews reviews, second; reviews.review = &review;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.send();
        auto* send = reviews.last.data(); QVERIFY(send);
        send->set(State::NeedsConfirmation);
        if (change == 0) reviews.replaceReview(&other);
        if (change == 1) controller.setActive(false);
        if (change == 2) controller.setReviewInterface(&second);
        if (change == 3) reviews.setAvailable(false);
        if (change == 4) controller.cancel();
        QCOMPARE(send->cancels, 1);
        QCOMPARE(controller.sendState(), State::Cancelled);
        QCOMPARE(controller.prompt(), TransactionReviewController::Prompt::None);
    }

    void aFinishedSendKeepsItsOutcome()
    {
        QObject review, other; FakeReviews reviews; reviews.review = &review;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.send();
        auto* send = reviews.last.data(); QVERIFY(send);
        send->set(State::Committed, {}, QStringLiteral("txid"));
        reviews.replaceReview(&other); controller.setActive(false); controller.cancel();
        QCOMPARE(send->cancels, 0);
        QCOMPARE(controller.sendState(), State::Committed); QCOMPARE(controller.txid(), QStringLiteral("txid"));
    }

    void startsNothingWithoutAReviewOrWhileHidden()
    {
        QObject review; FakeReviews reviews;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.send(); QCOMPARE(reviews.starts, 0);
        reviews.review = &review; controller.setActive(false);
        controller.send(); QCOMPARE(reviews.starts, 0);
        controller.setActive(true); reviews.refuse = true;
        controller.send(); QCOMPARE(reviews.starts, 1); QCOMPARE(controller.sendState(), State::None);
    }

    void saveAsksForAFileForTheReviewItWasStartedFor()
    {
        QObject review, other; FakeReviews reviews; reviews.review = &review;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.beginPsbtSave(QStringLiteral("local:1:1"));
        QVERIFY(controller.savePending()); QVERIFY(controller.saveSignerResult());
        controller.choosePsbtSaveFile(QStringLiteral("/tmp/a.psbt"));
        QCOMPARE(reviews.psbt_starts, 1); QCOMPARE(reviews.last_path, QStringLiteral("/tmp/a.psbt"));
        QCOMPARE(reviews.last_signer_result, QStringLiteral("local:1:1")); QVERIFY(!controller.savePending());
        QCOMPARE(controller.psbtKind(), PsbtOperation::Kind::Save); QVERIFY(controller.psbtBusy());
        reviews.last_psbt->set(PsbtOperation::State::Saved, {}, QStringLiteral("/tmp/a.psbt"));
        QCOMPARE(controller.psbtState(), PsbtOperation::State::Saved); QVERIFY(!controller.psbtBusy());
        controller.dismissPsbtResult();
        QCOMPARE(controller.psbtState(), PsbtOperation::State::None);
        // A file chosen after the review changed saves nothing.
        controller.beginPsbtSave();
        reviews.replaceReview(&other);
        QVERIFY(!controller.savePending());
        controller.choosePsbtSaveFile(QStringLiteral("/tmp/b.psbt"));
        QCOMPARE(reviews.psbt_starts, 1);
        controller.beginPsbtSave(); controller.cancelPsbtSave(); controller.choosePsbtSaveFile(QStringLiteral("/tmp/c.psbt"));
        QCOMPARE(reviews.psbt_starts, 1);
    }

    void aRefusedPsbtActionSaysAnotherActionRuns()
    {
        QObject review; FakeReviews reviews; reviews.review = &review; reviews.refuse_psbt = true;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.broadcast();
        QCOMPARE(controller.psbtKind(), PsbtOperation::Kind::Broadcast);
        QCOMPARE(controller.psbtState(), PsbtOperation::State::Failed);
        QVERIFY(!controller.psbtError().isEmpty());
        controller.dismissPsbtResult();
        QCOMPARE(controller.psbtState(), PsbtOperation::State::None); QVERIFY(controller.psbtError().isEmpty());
    }

    void anotherReviewOrHiddenPageDropsAPendingSaveOrBroadcast_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("review") << 0; QTest::newRow("inactive") << 1; QTest::newRow("interface") << 2;
    }
    void anotherReviewOrHiddenPageDropsAPendingSaveOrBroadcast()
    {
        QFETCH(int, change);
        QObject review, other; FakeReviews reviews, second; reviews.review = &review;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.broadcast();
        QPointer<FakePsbt> broadcast{reviews.last_psbt}; QVERIFY(broadcast);
        int cancels{0};
        connect(broadcast, &PsbtOperation::changed, this, [&] { if (broadcast && broadcast->state() == PsbtOperation::State::Cancelled) ++cancels; });
        if (change == 0) reviews.replaceReview(&other);
        if (change == 1) controller.setActive(false);
        if (change == 2) controller.setReviewInterface(&second);
        QCOMPARE(cancels, 1);
        if (change == 1) {
            // Hiding keeps the outcome until the page is shown again.
            QCOMPARE(controller.psbtState(), PsbtOperation::State::Cancelled);
            controller.setActive(true);
        }
        QVERIFY(!broadcast);
        QCOMPARE(controller.psbtState(), PsbtOperation::State::None);
    }

    void aNoticeForTheSameReviewKeepsThePsbtAction()
    {
        QObject review; FakeReviews reviews; reviews.review = &review;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.broadcast();
        QPointer<FakePsbt> broadcast{reviews.last_psbt}; QVERIFY(broadcast);
        // A broadcast updates the same review before reporting its outcome.
        reviews.replaceReview(&review);
        QVERIFY(broadcast);
        broadcast->set(PsbtOperation::State::Submitted, {}, {}, QStringLiteral("txid"));
        QCOMPARE(controller.psbtState(), PsbtOperation::State::Submitted); QCOMPARE(controller.psbtTxid(), QStringLiteral("txid"));
    }

    void aRecheckSurvivesTheReviewItReplaces()
    {
        QObject review, other; FakeReviews reviews; reviews.review = &review;
        TransactionReviewController controller; controller.setReviewInterface(&reviews);
        controller.recheck();
        QPointer<FakePsbt> recheck{reviews.last_psbt}; QVERIFY(recheck);
        reviews.replaceReview(&other);
        QVERIFY(recheck); QCOMPARE(recheck->cancels, 0);
        recheck->set(PsbtOperation::State::Imported);
        QCOMPARE(controller.psbtState(), PsbtOperation::State::Imported);
    }

    void deletingTheControllerAnswersItsPrompt()
    {
        QObject review; FakeReviews reviews; reviews.review = &review;
        auto controller = std::make_unique<TransactionReviewController>(); controller->setReviewInterface(&reviews);
        controller->send();
        const QPointer<FakeSend> send{reviews.last};
        int cancels{0};
        QVERIFY(send);
        send->set(State::NeedsUnlock);
        connect(send, &SendOperation::changed, this, [&] { if (send->state() == State::Cancelled) ++cancels; });
        controller.reset();
        QCOMPARE(cancels, 1); QVERIFY(!send);
    }
};

BITCOINQML_REGISTER_QT_TEST(TransactionReviewControllerTests)
#include <test_transactionreviewcontroller.moc>
