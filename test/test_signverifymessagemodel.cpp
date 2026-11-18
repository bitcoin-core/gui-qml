// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/signverifymessagemodel.h>
#include <qml/executor.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/mocks/walletsession.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>
#include <common/signmessage.h>
#include <key.h>
#include <key_io.h>
#include <pubkey.h>
#include <QScopeGuard>
#include <QThread>
#include <QtTest/QtTest>
#include <stdexcept>

namespace {
QString Address() { return QString::fromStdString(EncodeDestination(PKHash{uint160{std::vector<unsigned char>(20, 1)}})); }
struct MessageWallet : StubWallet {
    bool encrypted{false}, locked{true}, throw_sign{false}, throw_unlock{false}, fail_lock{false}, watch_only{false};
    int signs{0}, unlocks{0}, locks{0}, internals{0};
    QString last_message;
    size_t long_secret_chars{0};
    wallet::CWallet* wallet() override { ++internals; throw std::runtime_error{"Private access forbidden"}; }
    bool isCrypted() override { return encrypted; }
    bool privateKeysDisabled() override { return watch_only; }
    bool isLocked() override { return locked; }
    util::Expected<void, wallet::WalletError> unlock(const SecureString& passphrase) override {
        ++unlocks;
        if (throw_unlock) { locked = false; throw std::runtime_error{"Unlock outcome lost"}; }
        if (long_secret_chars ? passphrase != SecureString(long_secret_chars, 'p') : passphrase != "secret") return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("Incorrect password")}};
        locked = false; return {};
    }
    bool lock() override { ++locks; if (fail_lock) return false; locked = true; return true; }
    SigningResult signMessage(const std::string& message, const PKHash&, std::string& signature) override {
        ++signs; last_message = QString::fromStdString(message);
        if (throw_sign) throw std::runtime_error{"Sign outcome lost"};
        signature = "owned-signature"; return SigningResult::OK;
    }
};
struct Fixture {
    ManualStartupExecution commands;
    std::shared_ptr<MessageWallet> wallet{std::make_shared<MessageWallet>()};
    std::shared_ptr<walletopening::Session> opening;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<SignVerifyMessageModel> model;
    Fixture() {
        opening = MakeWalletSession(commands.executor(), [] {});
        opening->start(wallet); commands.drain(); opening->take();
        model = std::make_unique<SignVerifyMessageModel>();
        model->setOpening(opening, [this](std::function<void()> turn) { publications.push_back(std::move(turn)); });
        finish();
    }
    ~Fixture() { if (model) model->close(); commands.drain(); model.reset(); opening->stop(false, {}); commands.drain(); }
    void publish() { while (!publications.empty()) { auto turn = std::move(publications.front()); publications.pop_front(); turn(); } }
    void finish() { commands.drain(); publish(); }
    void setInput(const QString& message) { model->setSigningAddress(Address()); model->setSigningMessage(message); finish(); }
};
} // namespace

class SignVerifyMessageModelTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<ECC_Context> m_ecc;
private Q_SLOTS:
    void initTestCase() { m_ecc = std::make_unique<ECC_Context>(); }
    void cleanupTestCase() { m_ecc.reset(); }
    void noWalletRefusalKeepsExplicitOutcome_data() {
        QTest::addColumn<bool>("password");
        QTest::newRow("plain") << false;
        QTest::newRow("password") << true;
    }
    void noWalletRefusalKeepsExplicitOutcome() {
        QFETCH(bool, password);
        SignVerifyMessageModel model;
        const auto ticket = password ? model.requestSignMessageWithPassphrase(Address(), "statement", "secret")
                                     : model.requestSignMessage(Address(), "statement");
        QCOMPARE(ticket, quint64{0});
        QCOMPARE(model.signingId(), quint64{0});
        QCOMPARE(model.signingState(), SignVerifyMessageModel::SigningFailed);
        QCOMPARE(model.signingError(), QStringLiteral("No wallet is selected."));
        QVERIFY(model.signature().isEmpty());
        QVERIFY(!model.signingNeedsUnlock());
        QVERIFY(!model.signingBusy());
        QVERIFY(!model.signingOperation()); // No accepted Core/security effect exists.
    }
    void quietClosedNoWalletRejectsWithoutPublication() {
        SignVerifyMessageModel model;
        model.close();
        QSignalSpy errors{&model, &SignVerifyMessageModel::signingErrorChanged};
        QSignalSpy signatures{&model, &SignVerifyMessageModel::signatureChanged};
        QSignalSpy states{&model, &SignVerifyMessageModel::signingStateChanged};
        QCOMPARE(model.requestSignMessage(Address(), "late"), quint64{0});
        QCOMPARE(model.requestSignMessageWithPassphrase(Address(), "late", "secret"), quint64{0});
        QCOMPARE(model.signingState(), SignVerifyMessageModel::SigningClosed);
        QVERIFY(model.signingError().isEmpty());
        QVERIFY(model.signingAddress().isEmpty());
        QVERIFY(model.signingMessage().isEmpty());
        QVERIFY(!model.signingOperation());
        QCOMPARE(errors.count(), 0);
        QCOMPARE(signatures.count(), 0);
        QCOMPARE(states.count(), 0);
    }
    void unavailableOriginalOpeningDoesNotBecomeNoWalletRejection() {
        Fixture f;
        f.setInput("statement");
        f.opening->retire();
        QSignalSpy errors{f.model.get(), &SignVerifyMessageModel::signingErrorChanged};
        QCOMPARE(f.model->requestSignMessage(Address(), "statement"), quint64{0});
        QCOMPARE(f.model->requestSignMessageWithPassphrase(Address(), "statement", "secret"), quint64{0});
        QVERIFY(f.model->signingError().isEmpty());
        QVERIFY(!f.model->signingOperation());
        QCOMPARE(errors.count(), 0);
        QCOMPARE(f.wallet->signs, 0);
        QCOMPARE(f.wallet->unlocks, 0);
        QCOMPARE(f.wallet->locks, 0);
    }
    void noWalletRejectionMayLoseItsObserver() {
        auto model = std::make_unique<SignVerifyMessageModel>();
        auto* original = model.get();
        QObject receiver;
        bool saw_failure{false};
        connect(original, &SignVerifyMessageModel::signingErrorChanged, &receiver, [&] {
            if (original->signingState() != SignVerifyMessageModel::SigningFailed) return;
            saw_failure = original->signingError() == QStringLiteral("No wallet is selected.");
            model.reset();
        });
        QCOMPARE(original->requestSignMessage(Address(), "statement"), quint64{0});
        QVERIFY(saw_failure);
        QVERIFY(!model);
    }
    void noWalletRejectionCannotPublishPastNewInput() {
        SignVerifyMessageModel model;
        bool replaced{false};
        int successor_states{0};
        connect(&model, &SignVerifyMessageModel::signingStateChanged, &model, [&] {
            if (replaced) ++successor_states;
        });
        connect(&model, &SignVerifyMessageModel::signingErrorChanged, &model, [&] {
            if (replaced || model.signingState() != SignVerifyMessageModel::SigningFailed) return;
            replaced = true;
            model.setSigningMessage("successor statement");
        });
        QCOMPARE(model.requestSignMessage(Address(), "original statement"), quint64{0});
        QVERIFY(replaced);
        QCOMPARE(model.signingMessage(), QStringLiteral("successor statement"));
        QCOMPARE(model.signingState(), SignVerifyMessageModel::SigningIdle);
        QVERIFY(model.signingError().isEmpty());
        QCOMPARE(successor_states, 1); // Exactly the successor's notification, no original continuation.
    }
    void noWalletRejectionCannotPublishAfterQuietClose() {
        SignVerifyMessageModel model;
        bool closed{false};
        int late_signatures{0}, late_states{0};
        connect(&model, &SignVerifyMessageModel::signatureChanged, &model, [&] { if (closed) ++late_signatures; });
        connect(&model, &SignVerifyMessageModel::signingStateChanged, &model, [&] { if (closed) ++late_states; });
        connect(&model, &SignVerifyMessageModel::signingErrorChanged, &model, [&] {
            if (model.signingState() != SignVerifyMessageModel::SigningFailed) return;
            closed = true;
            model.close();
        });
        QCOMPARE(model.requestSignMessage(Address(), "statement"), quint64{0});
        QVERIFY(closed);
        QCOMPARE(model.signingState(), SignVerifyMessageModel::SigningClosed);
        QCOMPARE(model.signingError(), QStringLiteral("No wallet is selected."));
        QCOMPARE(late_signatures, 0);
        QCOMPARE(late_states, 0);
    }
    void originalLongPasswordIsMaterializedByTheAcceptedNativeAction_data() {
        QTest::addColumn<bool>("close_first");
        QTest::newRow("held-gui") << false; QTest::newRow("close-before-effect") << true;
    }
    void originalLongPasswordIsMaterializedByTheAcceptedNativeAction() {
        QFETCH(bool, close_first); Fixture f; f.wallet->encrypted = true; f.wallet->long_secret_chars = 40'000;
        f.setInput("original statement"); QString password(40'000, QChar{'p'});
        const auto ticket = f.model->requestSignMessageWithPassphrase(Address(), "original statement", password); QVERIFY(ticket);
        const auto owner = f.model->signingOperation(); password = "changed";
        QCOMPARE(f.wallet->unlocks, 0); QCOMPARE(f.wallet->signs, 0);
        if (close_first) f.model->close();
        QVERIFY(f.commands.step(execution::Lane::Core));
        QCOMPARE(f.wallet->unlocks, 1); QCOMPARE(f.wallet->signs, 1); QCOMPARE(f.wallet->locks, 1); QVERIFY(f.wallet->locked);
        QVERIFY(!owner->result()); f.commands.drain();
        QVERIFY(owner->result()); QCOMPARE(owner->result()->value->error, walletmessage::Error::None);
        f.publish();
        if (!close_first) { QCOMPARE(f.model->signingId(), ticket); QCOMPARE(f.model->signingState(), SignVerifyMessageModel::SigningSucceeded); }
        else QCOMPARE(f.model->signingState(), SignVerifyMessageModel::SigningClosed);
    }
    void manualSignSeparatesEffectOutcomeAndGui()
    {
        Fixture f; f.setInput("original statement");
        const auto id = f.model->requestSignMessage(Address(), "original statement"); QVERIFY(id);
        QCOMPARE(f.wallet->signs, 0); QVERIFY(f.model->signature().isEmpty());
        QVERIFY(f.commands.step(execution::Lane::Core));
        QCOMPARE(f.wallet->signs, 1); QVERIFY(f.model->signature().isEmpty());
        QVERIFY(!f.model->signingOperation()->result());
        QVERIFY(f.commands.step(execution::Lane::Utility));
        QVERIFY(f.model->signingOperation()->result());
        QVERIFY(f.model->signature().isEmpty()); QVERIFY(f.model->signingBusy());
        f.publish();
        QCOMPARE(f.model->signingId(), id);
        QCOMPARE(f.model->signingState(), SignVerifyMessageModel::SigningSucceeded);
        QCOMPARE(f.model->signature(), QStringLiteral("owned-signature"));
        QCOMPARE(f.wallet->internals, 0);
    }
    void failedGuiTransportRetainsResultAndRetriesOnlyProjection()
    {
        Fixture f; f.setInput("original statement");
        bool fail{false};
        f.model->setOpening(f.opening, [&f, &fail](std::function<void()> turn) {
            if (std::exchange(fail, false)) throw std::runtime_error{"GUI transport unavailable"};
            f.publications.push_back(std::move(turn));
        });
        f.finish(); fail = true;
        QVERIFY(f.model->requestSignMessage(Address(), "original statement"));
        f.commands.drain();
        QVERIFY(f.model->signingBusy()); QCOMPARE(f.wallet->signs, 1);
        const auto owner = f.model->signingOperation();
        QVERIFY(owner->result()); QVERIFY(owner->result()->publication_failed);
        QCOMPARE(owner->result()->value->signature, QStringLiteral("owned-signature"));
        f.model->retryPublications();
        QVERIFY(f.model->signingSucceeded()); QCOMPARE(f.wallet->signs, 1);
        f.model->retryPublications(); f.publish(); QCOMPARE(f.wallet->signs, 1);
    }
    void successorDraftCannotReceiveTheOldSignature()
    {
        Fixture f; f.setInput("original statement");
        QVERIFY(f.model->requestSignMessage(Address(), "original statement"));
        const auto owner = f.model->signingOperation();
        f.model->setSigningMessage("successor statement");
        QCOMPARE(f.model->signingState(), SignVerifyMessageModel::SigningDiscarding);
        QVERIFY(!f.model->requestSignMessage(Address(), "successor statement"));
        f.finish();
        QCOMPARE(f.wallet->last_message, QStringLiteral("original statement"));
        QVERIFY(owner->result()); QCOMPARE(owner->result()->value->signature, QStringLiteral("owned-signature"));
        QVERIFY(f.model->signature().isEmpty());
        QCOMPARE(f.model->signingState(), SignVerifyMessageModel::SigningIdle);
        QVERIFY(f.model->requestSignMessage(Address(), "successor statement")); f.finish();
        QCOMPARE(f.wallet->signs, 2); QCOMPARE(f.wallet->last_message, QStringLiteral("successor statement"));
    }
    void closeAndObserverDestructionDoNotEraseAcceptedSigning()
    {
        Fixture f; f.setInput("original statement");
        QVERIFY(f.model->requestSignMessage(Address(), "original statement"));
        const auto owner = f.model->signingOperation();
        f.model->close(); f.model.reset(); f.opening->close();
        QVERIFY(f.commands.step(execution::Lane::Core));
        QCOMPARE(f.wallet->signs, 1); QVERIFY(!owner->result());
        QVERIFY(f.commands.step(execution::Lane::Utility));
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        QCOMPARE(owner->result()->value->signature, QStringLiteral("owned-signature"));
        f.publish(); QCOMPARE(f.wallet->signs, 1); QCOMPARE(f.wallet->internals, 0);
    }
    void unlockAndRestoreOutcomesAreExplicit_data()
    {
        QTest::addColumn<int>("scenario");
        for (int scenario = 0; scenario < 6; ++scenario) QTest::newRow(qPrintable(QString::number(scenario))) << scenario;
    }
    void unlockAndRestoreOutcomesAreExplicit()
    {
        QFETCH(int, scenario);
        Fixture f; f.setInput("statement"); f.wallet->encrypted = true;
        f.wallet->throw_sign = scenario == 3;
        f.wallet->fail_lock = scenario == 4;
        f.wallet->throw_unlock = scenario == 5;
        const auto id = scenario == 0 ? f.model->requestSignMessage(Address(), "statement")
            : f.model->requestSignMessageWithPassphrase(Address(), "statement", scenario == 1 ? "wrong" : "secret");
        QVERIFY(id); f.finish();
        const walletmessage::Error errors[] = {walletmessage::Error::NeedsUnlock, walletmessage::Error::IncorrectPassphrase,
            walletmessage::Error::None, walletmessage::Error::Uncertain, walletmessage::Error::RelockFailed, walletmessage::Error::Uncertain};
        QVERIFY(f.model->signingResult()); QCOMPARE(f.model->signingResult()->error, errors[scenario]);
        QCOMPARE(f.wallet->locks, scenario <= 1 ? 0 : 1);
        QCOMPARE(f.wallet->signs, scenario == 2 || scenario == 3 || scenario == 4 ? 1 : 0);
        QCOMPARE(f.model->signingSucceeded(), scenario == 2);
        if (scenario == 4) {
            QCOMPARE(f.model->signingResult()->signature, QStringLiteral("owned-signature"));
            QVERIFY(f.model->signature().isEmpty());
        }
        QCOMPARE(f.wallet->internals, 0);
    }
    void walletlessVerificationIsAsynchronousAndDraftScoped()
    {
        Fixture f;
        f.model->close();
        f.model = std::make_unique<SignVerifyMessageModel>();
        f.model->setOpening({}, [&f](std::function<void()> turn) { f.publications.push_back(std::move(turn)); }, f.opening->utilityQueue());
        f.finish();
        CKey key; key.MakeNewKey(true);
        const QString address = QString::fromStdString(EncodeDestination(PKHash{key.GetPubKey()}));
        std::string signature; QVERIFY(MessageSign(key, "exact statement", signature));
        const QString encoded = QString::fromStdString(signature);
        f.model->setVerificationAddress(address); f.model->setVerificationMessage("exact statement");
        f.model->setVerificationSignature(encoded); f.finish();
        const auto id = f.model->requestVerifyMessage(address, "exact statement", encoded); QVERIFY(id);
        QVERIFY(f.model->verificationBusy()); QVERIFY(!f.model->verificationValid());
        QVERIFY(f.commands.step(execution::Lane::Utility));
        QVERIFY(!f.publications.empty());
        QVERIFY(!f.model->verificationValid());
        f.publish();
        QCOMPARE(f.model->verificationId(), id); QVERIFY(f.model->verificationValid());
        f.model->setVerificationMessage("successor statement");
        QVERIFY(!f.model->verificationValid()); QVERIFY(f.model->verificationText().isEmpty());
        QVERIFY(f.model->requestVerifyMessage(address, "successor statement", encoded));
        f.commands.drain();
        f.model->setVerificationMessage("third statement");
        f.publish();
        QCOMPARE(f.model->verificationState(), SignVerifyMessageModel::VerificationIdle);
        QVERIFY(!f.model->verificationValid()); QVERIFY(f.model->verificationText().isEmpty());
    }
    void walletlessVerificationUsesUtilityWithSchedulerClosed()
    {
        int unexpected_dispatches{0};
        std::deque<std::function<void()>> utility, publications;
        execution::Executor executor{[&](execution::Lane lane, execution::UtilityWorker worker, auto turn) {
            if (lane != execution::Lane::Utility || worker != execution::UtilityWorker::IO) {
                ++unexpected_dispatches;
                return false;
            }
            utility.push_back(std::move(turn));
            return true;
        }};
        const auto drain = [](auto& turns) {
            while (!turns.empty()) {
                auto turn = std::move(turns.front());
                turns.pop_front();
                turn();
            }
        };
        auto outcomes = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        const auto cleanup = qScopeGuard([&] {
            outcomes->close();
            drain(utility);
        });
        SignVerifyMessageModel model;
        model.setOpening({}, [&](auto turn) { publications.push_back(std::move(turn)); }, outcomes);
        CKey key;
        key.MakeNewKey(true);
        const QString address = QString::fromStdString(EncodeDestination(PKHash{key.GetPubKey()}));
        std::string signature;
        QVERIFY(MessageSign(key, "exact statement", signature));
        const QString encoded = QString::fromStdString(signature);
        model.setVerificationAddress(address);
        model.setVerificationMessage("exact statement");
        model.setVerificationSignature(encoded);
        drain(utility);
        drain(publications);
        QVERIFY(model.canVerify());

        const auto id = model.requestVerifyMessage(address, "exact statement", encoded);
        QVERIFY(id);
        QVERIFY(model.verificationBusy());
        drain(utility);
        QVERIFY(!model.verificationValid());
        drain(publications);
        QCOMPARE(model.verificationId(), id);
        QVERIFY(model.verificationValid());

        QVERIFY(model.requestVerifyMessage(address, "exact statement", encoded));
        drain(utility);
        model.setVerificationMessage("successor statement");
        drain(publications);
        QCOMPARE(model.verificationState(), SignVerifyMessageModel::VerificationIdle);
        QVERIFY(!model.verificationValid());
        QVERIFY(model.verificationText().isEmpty());
        QCOMPARE(unexpected_dispatches, 0);
    }

    void watchOnlySigningRefusesWithoutAskingForCredentials()
    {
        Fixture f; f.wallet->watch_only = true; f.setInput("statement");
        QVERIFY(f.model->requestSignMessage(Address(), "statement")); f.finish();
        QCOMPARE(f.model->signingResult()->error, walletmessage::Error::Signing);
        QVERIFY(!f.model->signingPrompt()); QVERIFY(!f.model->signingNeedsUnlock());
        QCOMPARE(f.wallet->signs, 0); QCOMPARE(f.wallet->unlocks, 0); QCOMPARE(f.wallet->locks, 0);
        QCOMPARE(f.model->error().code, MessageSigningError::Signing); QVERIFY(!f.model->error().message.isEmpty());
    }
    void cancellingAuthorizationRetiresOnlyItsObsoleteError_data()
    {
        QTest::addColumn<bool>("wrong_password");
        QTest::newRow("initial-question") << false; QTest::newRow("incorrect-password") << true;
    }
    void cancellingAuthorizationRetiresOnlyItsObsoleteError()
    {
        QFETCH(bool, wrong_password); Fixture f; f.wallet->encrypted = true; f.setInput("statement");
        QVERIFY(f.model->requestSignMessage(Address(), "statement")); f.finish();
        QPointer<MessageSigningPrompt> prompt = f.model->signingPrompt(); QVERIFY(prompt); QVERIFY(prompt->pending());
        if (wrong_password) { QVERIFY(!prompt->submit("wrong").isEmpty()); f.finish(); QVERIFY(!prompt->error().isEmpty()); }
        const auto actual = f.model->signingResult(); QVERIFY(actual);
        QVERIFY(prompt->cancel()); f.finish();
        QVERIFY(!f.model->signingNeedsUnlock()); QVERIFY(f.model->signingError().isEmpty()); QVERIFY(!f.model->signingPrompt());
        QCOMPARE(f.model->signingResult()->error, actual->error); QCOMPARE(f.wallet->signs, 0); QVERIFY(f.wallet->locked);
        if (prompt) QVERIFY(prompt->submit("stale secret").isEmpty());
        QVERIFY(f.model->requestSignMessage(Address(), "statement")); f.finish();
        QVERIFY(f.model->signingPrompt());
        QCOMPARE(f.model->error().code, MessageSigningError::NeedsUnlock);
        QCOMPARE(f.model->signingPrompt()->error(), f.model->signingError());
        QVERIFY(!f.model->signingPrompt()->error().contains("incorrect", Qt::CaseInsensitive));
        QCOMPARE(f.wallet->unlocks, wrong_password ? 1 : 0);
    }
    void cancelledReauthorizationPreservesTheOriginalValidSignature()
    {
        Fixture f; f.wallet->encrypted = true; f.setInput("statement");
        QVERIFY(f.model->requestSignMessageWithPassphrase(Address(), "statement", "secret")); f.finish();
        const auto signature = f.model->signature(); QVERIFY(!signature.isEmpty()); QVERIFY(f.wallet->locked);
        QVERIFY(f.model->requestSignMessage(Address(), "statement")); f.finish();
        QPointer<MessageSigningPrompt> prompt = f.model->signingPrompt(); QVERIFY(prompt); QVERIFY(!f.model->signingSucceeded());
        QVERIFY(prompt->cancel()); f.finish();
        QCOMPARE(f.model->signature(), signature); QVERIFY(f.model->signingSucceeded());
        QCOMPARE(f.wallet->signs, 1); QCOMPARE(f.wallet->unlocks, 1); QCOMPARE(f.wallet->locks, 1);
    }
    void editedInputRejectsTheOriginalPromptCapability()
    {
        Fixture f; f.wallet->encrypted = true; f.setInput("original");
        QVERIFY(f.model->requestSignMessage(Address(), "original")); f.finish();
        QPointer<MessageSigningPrompt> prompt = f.model->signingPrompt(); QVERIFY(prompt);
        f.model->setSigningMessage("successor");
        QVERIFY(!prompt || !prompt->pending()); if (prompt) QVERIFY(prompt->submit("secret").isEmpty()); f.finish();
        QVERIFY(!f.model->signingPrompt()); QCOMPARE(f.wallet->unlocks, 0); QCOMPARE(f.wallet->signs, 0);
        QVERIFY(!f.model->signingNeedsUnlock()); QVERIFY(f.model->signingError().isEmpty());
    }
    void retiringAViewDrainsItsOriginalAuthorizationWithoutInstallingIt()
    {
        Fixture f; f.wallet->encrypted = true; f.setInput("statement");
        QVERIFY(f.model->requestSignMessage(Address(), "statement")); f.finish();
        QPointer<MessageSigningPrompt> prompt = f.model->signingPrompt(); QVERIFY(prompt); QVERIFY(!prompt->submit("secret").isEmpty());
        QVERIFY(f.model->signingBusy()); QVERIFY(!prompt->cancel());
        prompt->retire(); QVERIFY(!prompt->pending()); QVERIFY(f.model->signingBusy());
        QVERIFY(prompt->submit("late").isEmpty()); f.finish();
        QVERIFY(!f.model->signingBusy()); QVERIFY(f.model->signature().isEmpty());
        QVERIFY(f.model->signingResult()); QCOMPARE(f.model->signingResult()->signature, QStringLiteral("owned-signature"));
        QCOMPARE(f.wallet->unlocks, 1); QCOMPARE(f.wallet->signs, 1); QCOMPARE(f.wallet->locks, 1); QVERIFY(f.wallet->locked);
    }
    void clearDuringSigningDoesNotEraseOrReplaceTheAcceptedInput()
    {
        Fixture f; f.setInput("original");
        QVERIFY(f.model->requestSignMessage(Address(), "original")); QVERIFY(f.model->signingBusy());
        f.model->clearSigningStatus();
        QCOMPARE(f.model->signingMessage(), QStringLiteral("original")); QVERIFY(f.model->signingBusy());
        f.finish(); QVERIFY(f.model->signingSucceeded()); QCOMPARE(f.wallet->last_message, QStringLiteral("original"));
        QCOMPARE(f.wallet->signs, 1);
    }
    void narrowCommandsRejectDuplicateAndClosedIntents()
    {
        Fixture f; f.setInput("original"); QSignalSpy rejected{f.model.get(), &SignVerifyMessageModel::commandRejected};
        QVERIFY(!f.model->sign().isEmpty()); const auto owner = f.model->signingOperation();
        QVERIFY(f.model->sign().isEmpty()); QCOMPARE(rejected.size(), 1); QCOMPARE(f.wallet->signs, 0);
        f.model->cancel(); QVERIFY(f.model->signingBusy()); f.finish();
        QCOMPARE(f.wallet->signs, 1); QVERIFY(owner->result()->value); QCOMPARE(owner->result()->value->signature, QStringLiteral("owned-signature"));
        QVERIFY(f.model->signature().isEmpty()); QVERIFY(!f.model->signingBusy());
        QVERIFY(f.model->verify(QString(32769, 'x')).isEmpty()); QCOMPARE(rejected.size(), 2); QVERIFY(!f.model->verificationOperation());
        f.model->close(); QVERIFY(f.model->sign().isEmpty()); QVERIFY(f.model->verify("signature").isEmpty());
        QCOMPARE(rejected.size(), 4); QCOMPARE(f.wallet->signs, 1);
    }
    void cancelledVerificationDrainsItsOriginalCpuReceipt()
    {
        Fixture f; CKey key; key.MakeNewKey(true); const QString address = QString::fromStdString(EncodeDestination(PKHash{key.GetPubKey()}));
        std::string signature; QVERIFY(MessageSign(key, "original verification", signature));
        f.model->setVerificationAddress(address); f.model->setVerificationMessage("original verification"); f.finish();
        QVERIFY(!f.model->verify(QString::fromStdString(signature)).isEmpty()); const auto owner = f.model->verificationOperation(); QVERIFY(owner);
        f.model->cancel(); QVERIFY(f.model->verificationBusy()); QCOMPARE(f.model->verificationState(), SignVerifyMessageModel::VerificationDiscarding);
        f.finish(); QVERIFY(owner->result()->value); QVERIFY(owner->result()->value->valid);
        QCOMPARE(f.model->verificationState(), SignVerifyMessageModel::VerificationIdle); QVERIFY(!f.model->verification().valid);
        QVERIFY(!f.model->verificationBusy()); QCOMPARE(f.wallet->signs, 0); QCOMPARE(f.wallet->unlocks, 0);
    }
    void realThreadSigningNeverCallsCoreOnGui()
    {
        struct Wallet : MessageWallet {
            std::atomic<QThread*> effect{nullptr}, unlock_thread{nullptr};
            util::Expected<void, wallet::WalletError> unlock(const SecureString& passphrase) override {
                unlock_thread = QThread::currentThread(); return MessageWallet::unlock(passphrase);
            }
            SigningResult signMessage(const std::string& message, const PKHash& address, std::string& signature) override {
                effect = QThread::currentThread(); return MessageWallet::signMessage(message, address, signature);
            }
        };
        using execution::Lane;
        execution::Executor executor{execution::Executor::Config{}};
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
        const auto outcomes = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        const auto cleanup = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        auto wallet = std::make_shared<Wallet>(); wallet->encrypted = true; wallet->long_secret_chars = 40'000;
        auto opening = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            walletopening::Session::Queues{cleanup, [core, outcomes](auto done) {
                core->close();
                core->whenDrained([outcomes, done = std::move(done)] {
                    outcomes->close();
                    outcomes->whenDrained(done);
                });
            }, core, outcomes});
        const auto stopped = std::make_shared<std::atomic_bool>(false);
        const auto stop = qScopeGuard([&] {
            opening->stop(false, [stopped] { *stopped = true; });
            QVERIFY(QTest::qWaitFor([&] { return stopped->load(); }, 5000));
        });
        const auto core_thread = std::make_shared<std::atomic<QThread*>>(nullptr);
        QVERIFY(core->post([core_thread] { *core_thread = QThread::currentThread(); }));
        QVERIFY(opening->start(wallet));
        QTRY_VERIFY(opening->available());
        {
            SignVerifyMessageModel model; model.setOpening(opening, {});
            QVERIFY(model.requestSignMessageWithPassphrase(Address(), "real worker statement", QString(40'000, QChar{'p'})));
            QTRY_VERIFY(!model.signingBusy());
            QCOMPARE(model.signingState(), SignVerifyMessageModel::SigningSucceeded);
            QVERIFY(core_thread->load());
            QVERIFY(core_thread->load() != QThread::currentThread());
            QVERIFY(wallet->effect.load() == core_thread->load());
            QVERIFY(wallet->unlock_thread.load() == core_thread->load());
            QCOMPARE(wallet->unlocks, 1); QCOMPARE(wallet->locks, 1);
            model.close();
        }
        QCOMPARE(wallet->internals, 0);
    }
};
BITCOINQML_REGISTER_QT_TEST(SignVerifyMessageModelTests)
#include <test_signverifymessagemodel.moc>
