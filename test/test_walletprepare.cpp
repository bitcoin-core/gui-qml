// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletprepare.h>
#include <qml/backendexecutor.h>
#include <QSemaphore>
#include <QThread>
#include <string_view>
#include <qml/snapshotquery.h>
#include <qml/retainedvalue.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>
#include <chainparams.h>
#include <key_io.h>
#include <util/fees.h>
#include <wallet/wallet.h>
#include <QtTest/QtTest>

namespace {
struct PrepareWallet : StubWallet {
    bool encrypted{false}, locked{true}, fail_relock{false}, throw_create{false}, unknown_sweep{false}, coin_locked{false};
    int creates{0}, unlocks{0}, locks{0}, internals{0};
    FeeReason reason{FeeReason::FALLBACK};
    std::string expected_password{"secret"};std::atomic<QThread*> unlock_thread{nullptr};
    COutPoint input{Txid::FromUint256(uint256::ONE), 0};
    CScript own_script{CScript{} << OP_TRUE};
    wallet::CWallet* wallet() override { ++internals; throw std::runtime_error{"Private wallet access forbidden"}; }
    bool privateKeysDisabled() override { return false; }
    bool hasExternalSigner() override { return false; }
    bool isCrypted() override { return encrypted; }
    bool isLocked() override { return locked; }
    util::Expected<void, wallet::WalletError> unlock(const SecureString& passphrase) override {
        ++unlocks;unlock_thread=QThread::currentThread();
        if (std::string_view{passphrase.data(),passphrase.size()} != std::string_view{expected_password}) return util::Unexpected{wallet::WalletError{
            wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("Incorrect password")}};
        locked = false; return {};
    }
    bool lock() override { ++locks; if (fail_relock) return false; locked = true; return true; }
    interfaces::WalletTxOut output() {
        interfaces::WalletTxOut out;
        out.txout = CTxOut{50'000, own_script}; out.depth_in_main_chain = 6; out.time = 0;
        return out;
    }
    CAmount getAvailableBalance(const wallet::CCoinControl& control) override {
        const bool selected = control.IsSelected(input);
        if (unknown_sweep && selected) return 0;
        return (selected || control.m_allow_other_inputs) ? CAmount{50'000} : 0;
    }
    CTransactionRef getTx(const Txid& id) override {
        if (id != input.hash) return {};
        CMutableTransaction previous; previous.vout.push_back(output().txout);
        return MakeTransactionRef(std::move(previous));
    }
    CoinsList listCoins() override { throw std::logic_error{"Maximum construction must not enumerate/preset candidates"}; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>& inputs) override {
        std::vector<interfaces::WalletTxOut> outputs;
        outputs.reserve(inputs.size());
        for (const auto& selected : inputs) outputs.push_back(selected == input ? output() : interfaces::WalletTxOut{});
        return outputs;
    }
    bool txoutIsMine(const CTxOut& output) override { return output.scriptPubKey == own_script; }
    bool isLockedCoin(const COutPoint&) override { return coin_locked; }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>& recipients,
        const wallet::CCoinControl&, bool, std::optional<unsigned int>) override {
        ++creates;
        if (throw_create) throw std::runtime_error{"Construction failed"};
        CMutableTransaction tx; tx.vin.emplace_back(input);
        CAmount total{0};
        for (const auto& recipient : recipients) {
            const auto amount = recipient.nAmount - (recipient.fSubtractFeeFromAmount ? 100 : 0);
            tx.vout.emplace_back(amount, GetScriptForDestination(recipient.dest)); total += amount;
        }
        if (total + 100 > 50'000) return util::Error{Untranslated("Insufficient funds")};
        std::optional<unsigned int> change;
        if (total + 100 < 50'000) { change = tx.vout.size(); tx.vout.emplace_back(50'000 - total - 100, own_script); }
        return wallet::CreatedTransactionResult{MakeTransactionRef(tx), CAmount{100}, change, reason};
    }
};
walletprepare::Request Request()
{
    walletprepare::Request request;
    request.recipients.append({QStringLiteral("1BoatSLRHtKNngkdXEeobR76b53LETtpyT"), QStringLiteral("reviewed note"), 1000, true});
    request.target_blocks = 6; request.fee_rate = QStringLiteral("1");
    return request;
}
} // namespace

class WalletPrepareTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { SelectParams(ChainType::MAIN); }
    void reviewFeeFactsComeFromOriginalNativeRequest_data()
    {
        QTest::addColumn<int>("reason"); QTest::addColumn<bool>("explicit_rate");
        QTest::newRow("fallback-without-authoritative-rate") << int(FeeReason::FALLBACK) << false;
        QTest::newRow("estimator-without-authoritative-rate") << int(FeeReason::FEE_RATE_ESTIMATOR) << false;
        QTest::newRow("explicit-rate") << int(FeeReason::USER_SPECIFIED) << true;
    }
    void reviewFeeFactsComeFromOriginalNativeRequest()
    {
        QFETCH(int, reason); QFETCH(bool, explicit_rate); PrepareWallet wallet; wallet.reason = static_cast<FeeReason>(reason);
        auto request = Request(); request.fee_rate = QStringLiteral("777");
        if (explicit_rate) request.control.m_feerate = CFeeRate{2500};
        const auto result = walletprepare::Execute(wallet, request); QCOMPARE(result.error, walletprepare::Error::None); QVERIFY(result.review);
        QCOMPARE(result.review->fee, CAmount{100}); QCOMPARE(result.review->fee_source, feeestimate::ProvenanceFor(wallet.reason));
        QCOMPARE(result.review->fee_rate, explicit_rate ? QStringLiteral("2.500") : QString{});
        request.fee_rate = QStringLiteral("999"); request.target_blocks = 50; request.control.m_feerate = CFeeRate{17000};
        QCOMPARE(result.review->fee, CAmount{100}); QCOMPARE(result.review->target_blocks, 6);
        QCOMPARE(result.review->fee_rate, explicit_rate ? QStringLiteral("2.500") : QString{}); QCOMPARE(wallet.creates, 1); QCOMPARE(wallet.internals, 0);
    }
    void acknowledgedBalanceRefusesBeforeUnlock_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::newRow("insufficient-wallet") << 0;
        QTest::newRow("insufficient-selection") << 1;
        QTest::newRow("equal-amount-still-needs-core") << 2;
        QTest::newRow("cached-funds-do-not-authorize-core") << 3;
        QTest::newRow("unknown-balance-still-needs-core") << 4;
        QTest::newRow("maximum-uses-public-current-balance") << 5;
    }
    void acknowledgedBalanceRefusesBeforeUnlock()
    {
        QFETCH(int, scenario);
        PrepareWallet wallet;
        wallet.encrypted = true;
        auto request = Request();
        request.passphrase = SecureString{"secret"};
        if (scenario < 2) request.available_balance = 999;
        if (scenario == 1) {
            request.input_mode = coinselection::InputMode::Manual;
            request.selected = std::make_shared<const std::set<COutPoint>>(std::set<COutPoint>{wallet.input});
        }
        if (scenario == 2) request.available_balance = 1000;
        if (scenario == 3) { request.available_balance = 100'000; request.recipients.front().amount = 51'000; }
        if (scenario == 5) { request.available_balance = 0; request.remainder = 0; }
        const auto result = walletprepare::Execute(wallet, std::move(request));
        QCOMPARE(wallet.unlocks, scenario < 2 ? 0 : 1);
        QCOMPARE(wallet.creates, scenario < 2 ? 0 : 1);
        QCOMPARE(wallet.locks, scenario < 2 ? 0 : 1);
        QCOMPARE(wallet.internals, 0);
        if (scenario < 2) {
            QCOMPARE(result.error, scenario == 1 ? walletprepare::Error::InsufficientSelectedBalance : walletprepare::Error::InsufficientBalance);
            QVERIFY(!result.review);
        } else if (scenario == 3) {
            QCOMPARE(result.error, walletprepare::Error::Core);
            QVERIFY(!result.review);
        } else {
            QCOMPARE(result.error, walletprepare::Error::None);
            QVERIFY(result.review);
            QCOMPARE(result.review->amount, CAmount{scenario == 5 ? 49'900 : 1000});
        }
        QVERIFY(wallet.locked);
    }
    void preparationRejectsAggregateOverflowBeforeUnlock()
    {
        PrepareWallet wallet;
        wallet.encrypted = true;
        auto request = Request();
        request.passphrase = SecureString{"secret"};
        auto extra = request.recipients.front();
        extra.amount = MAX_MONEY;
        request.recipients.append(extra);
        const auto result = walletprepare::Execute(wallet, std::move(request));
        QCOMPARE(result.error, walletprepare::Error::Invalid);
        QVERIFY(!result.review);
        QCOMPARE(wallet.unlocks, 0);
        QCOMPARE(wallet.creates, 0);
        QCOMPARE(wallet.locks, 0);
    }

    void emptyManualSelectionCannotBroadenToWalletInputs()
    {
        PrepareWallet wallet;
        wallet.encrypted = true;
        auto request = Request();
        request.input_mode = coinselection::InputMode::Manual;
        request.available_balance = 50'000;
        request.passphrase = SecureString{"secret"};

        const auto result = walletprepare::Execute(wallet, std::move(request));
        QCOMPARE(result.error, walletprepare::Error::InsufficientSelectedBalance);
        QVERIFY(!result.review);
        QCOMPARE(wallet.unlocks, 0);
        QCOMPARE(wallet.creates, 0);
        QCOMPARE(wallet.locks, 0);
    }
    void selectedLockIsCheckedAtTheNativeBoundaryBeforeUnlock()
    {
        PrepareWallet wallet; wallet.encrypted = true; wallet.coin_locked = true;
        auto request = Request(); request.input_mode = coinselection::InputMode::Manual;
        request.selected = std::make_shared<const std::set<COutPoint>>(std::set<COutPoint>{wallet.input});
        request.available_balance = 50'000; request.passphrase = SecureString{"secret"};
        const auto result = walletprepare::Execute(wallet, std::move(request));
        QCOMPARE(result.error, walletprepare::Error::InsufficientSelectedBalance);
        QVERIFY(!result.review); QVERIFY(result.message.contains(QStringLiteral("locked")));
        QCOMPARE(wallet.unlocks, 0); QCOMPARE(wallet.creates, 0); QCOMPARE(wallet.locks, 0);
    }

    void capturedPasswordOwnsOriginalPreparation_data()
    {
        QTest::addColumn<int>("scenario");QTest::newRow("held-result")<<0;QTest::newRow("quiet-close-before-work")<<1;QTest::newRow("native-submitted-refused")<<2;
    }
    void capturedPasswordOwnsOriginalPreparation()
    {
        QFETCH(int,scenario);ManualRoutedExecution commands,retirement;
        auto wallet=std::make_shared<PrepareWallet>();wallet->encrypted=true;wallet->expected_password=std::string(40'000,'x');
        auto opening=std::make_shared<walletopening::Session>(commands.scheduler,retirement.scheduler,std::make_shared<std::atomic_bool>(false),[]{});
        QVERIFY(opening->start(wallet));commands.drain();opening->take();auto action=opening->startAction();QVERIFY(action);
        QString source=QString::fromStdString(wallet->expected_password);auto captured=secretinput::Capture(commands.scheduler,source);QVERIFY(captured);
        auto request=Request();request.captured_passphrase=captured.input;
        auto output=walletopening::RetainValues<walletprepare::Result>(opening,[]{return std::make_unique<walletprepare::Result>();});QVERIFY(output);
        auto operation=std::make_shared<execution::OwnedCommand<walletprepare::Result>>(commands.scheduler,[]{});
        if(scenario==2)commands.rejectLane(execution::Lane::Core);
        const auto admitted=operation->startWith([&,request=std::move(request)](auto done,auto failed)mutable{
            return opening->perform(action,[request=std::move(request),output](interfaces::Wallet& original)mutable{return walletprepare::ExecuteInto(original,std::move(request),output);},std::move(done),std::move(failed));
        });
        source.fill(QChar{'y'});QCOMPARE(wallet->unlocks,0);QCOMPARE(wallet->creates,0);
        if(scenario==2){QVERIFY(!admitted);commands.drain();QCOMPARE(wallet->unlocks,0);QCOMPARE(wallet->creates,0);}
        else{QVERIFY(admitted);if(scenario==1)opening->close();QVERIFY(commands.step(execution::Lane::Core));
            QCOMPARE(wallet->unlocks,1);QCOMPARE(wallet->creates,1);QCOMPARE(wallet->locks,1);QVERIFY(wallet->locked);QVERIFY(!operation->result());
            QVERIFY(commands.step(execution::Lane::Domain));QVERIFY(operation->result()->value);QCOMPARE(operation->result()->value->error,walletprepare::Error::None);QVERIFY(operation->result()->value->review);}
        captured.input.reset();operation.reset();output.reset();action.reset();opening->retire();commands.drain();retirement.drain();QVERIFY(commands.quiescent());QVERIFY(retirement.quiescent());
    }
    void capturedPreparationPasswordUnlocksOnRealWorker()
    {
        BackendExecutor commands{execution::Options{}},retirement{execution::Options{}};
        auto wallet=std::make_shared<PrepareWallet>();wallet->encrypted=true;wallet->expected_password=std::string(40'000,'p');
        auto opening=std::make_shared<walletopening::Session>(commands.scheduler(),retirement.scheduler(),std::make_shared<std::atomic_bool>(false),[]{});
        auto client=opening->start(wallet).borrower;QVERIFY(client);
        auto captured=secretinput::Capture(commands.scheduler(),QString::fromStdString(wallet->expected_password));QVERIFY(captured);
        auto request=Request();request.captured_passphrase=captured.input;
        auto output=walletopening::RetainValues<walletprepare::Result>(opening,[]{return std::make_unique<walletprepare::Result>();});QVERIFY(output);
        auto action=opening->startAction();QVERIFY(action);auto operation=std::make_shared<execution::OwnedCommand<walletprepare::Result>>(commands.scheduler(),[]{});
        QVERIFY(operation->startWith([&,request=std::move(request)](auto done,auto failed)mutable{return opening->perform(action,[request=std::move(request),output](interfaces::Wallet& original)mutable{return walletprepare::ExecuteInto(original,std::move(request),output);},std::move(done),std::move(failed));}));
        QTRY_VERIFY(operation->result().has_value());QVERIFY(operation->result()->value);QCOMPARE(operation->result()->value->error,walletprepare::Error::None);
        QVERIFY(wallet->unlock_thread.load());QVERIFY(wallet->unlock_thread.load()!=QThread::currentThread());QVERIFY(wallet->locked);
        captured.input.reset();operation.reset();output.reset();action.reset();opening->retire();client.reset();commands.shutdown();QTRY_VERIFY(commands.isDrained());opening.reset();retirement.shutdown();QTRY_VERIFY(retirement.isDrained());
    }
    void unknownSweepCannotPublishReviewAndRestoresLock()
    {
        PrepareWallet wallet; wallet.encrypted = true; wallet.unknown_sweep = true;
        auto request = Request(); request.remainder = 0; request.passphrase = SecureString{"secret"};
        const auto result = walletprepare::Execute(wallet, std::move(request));
        QCOMPARE(result.error, walletprepare::Error::SweepUnknown);
        QVERIFY(!result.review); QVERIFY(!result.message.isEmpty());
        QCOMPARE(wallet.unlocks, 1); QCOMPARE(wallet.locks, 1); QVERIFY(wallet.locked);
    }
    void maximumRemainderIgnoresOnlyTheReplacedAmount()
    {
        senddraft::Snapshot draft;
        senddraft::Recipient maximum, other;
        maximum.token = 1; maximum.amount = MAX_MONEY; maximum.valid = false;
        other.token = 2; other.amount = 200; other.valid = true; other.validation_pending = false;
        draft.recipients = {maximum, other}; draft.positions = {{1, 0}, {2, 1}};
        draft.amount_total_valid = false;
        QCOMPARE(senddraft::GrossRemainder(draft, 1, 1000), std::optional<CAmount>{800});
        draft.recipients[1].validation_pending = true;
        QVERIFY(!senddraft::GrossRemainder(draft, 1, 1000));
        draft.recipients[1].validation_pending = false;
        draft.recipients[1].amount = -1;
        QVERIFY(!senddraft::GrossRemainder(draft, 1, 1000));
        draft.recipients[1].amount = MAX_MONEY;
        other.token = 3; other.amount = 1;
        draft.recipients.push_back(other);
        QVERIFY(!senddraft::GrossRemainder(draft, 1, MAX_MONEY));
        QVERIFY(!senddraft::GrossRemainder(draft, 99, 1000));
    }
    void publicConstructionAndLockRestoration_data()
    {
        QTest::addColumn<int>("scenario");
        for (int scenario = 0; scenario < 7; ++scenario) QTest::newRow(qPrintable(QString::number(scenario))) << scenario;
    }
    void publicConstructionAndLockRestoration()
    {
        QFETCH(int, scenario);
        PrepareWallet wallet;
        auto request = Request();
        wallet.encrypted = scenario >= 2;
        if (scenario == 1) request.remainder = 0;
        if (scenario >= 3) request.passphrase = SecureString{scenario == 3 ? "wrong" : "secret"};
        wallet.throw_create = scenario == 5;
        wallet.fail_relock = scenario == 6;
        const auto result = walletprepare::Execute(wallet, std::move(request));
        const walletprepare::Error expected[] = {walletprepare::Error::None, walletprepare::Error::None,
            walletprepare::Error::NeedsUnlock, walletprepare::Error::IncorrectPassphrase,
            walletprepare::Error::None, walletprepare::Error::Failed, walletprepare::Error::RelockFailed};
        QCOMPARE(result.error, expected[scenario]);
        QCOMPARE(wallet.internals, 0);
        QCOMPARE(wallet.creates, scenario == 2 || scenario == 3 ? 0 : 1);
        QCOMPARE(bool(result.review), scenario == 0 || scenario == 1 || scenario == 4);
        QCOMPARE(result.relock_failed, scenario == 6);
        if (scenario == 3) QCOMPARE(wallet.locks, 0);
        if (scenario >= 4 && scenario != 6) { QVERIFY(wallet.locked); QCOMPARE(wallet.locks, 1); }
        if (result.review) {
            QCOMPARE(result.review->fee, CAmount{100});
            QCOMPARE(result.review->amount, CAmount{scenario == 1 ? 49'900 : 1000});
            QCOMPARE(result.review->recipients.front().label, QStringLiteral("reviewed note"));
            QVERIFY(result.review->recipients.front().payment_request);
            QCOMPARE(result.review->presentation.size(), 1);
            QCOMPARE(result.review->sweeps, scenario == 1);
            QCOMPARE(result.review->proof->state, psbtfee::State::Known);
        }
    }
    void retainedValueReleaseWaitsForCleanup()
    {
        ManualRoutedExecution commands, retirement;
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        int destroyed{0};
        struct Value { int* destroyed; ~Value() { ++*destroyed; } };
        auto value = walletopening::RetainValues<Value>(opening, [&] { return std::unique_ptr<Value>{new Value{&destroyed}}; });
        QVERIFY(value);
        auto alias = value;
        value.reset();
        commands.drain();
        QCOMPARE(destroyed, 0);
        alias.reset();
        QCOMPARE(destroyed, 0);
        QVERIFY(commands.step(execution::Lane::Cleanup));
        QCOMPARE(destroyed, 1);
        commands.drain();
    }
    void admittedPreparationOwnsReviewAfterFullClosure()
    {
        using namespace execution;
        ManualRoutedExecution commands, retirement;
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        PrepareWallet wallet;
        auto output = walletopening::RetainValues<walletprepare::Result>(opening, [] { return std::make_unique<walletprepare::Result>(); });
        QVERIFY(output);
        std::optional<walletprepare::Result> observed;
        const auto admitted = Submit(*commands.scheduler,
            [output, &wallet] { return walletprepare::ExecuteInto(wallet, Request(), output); },
            [&](auto value) { observed = std::move(value); });
        QVERIFY(admitted); output.reset();
        commands.scheduler->close();
        commands.drain();
        QVERIFY(observed); QCOMPARE(observed->error, walletprepare::Error::None);
        QVERIFY(observed->review); QVERIFY(observed->lifetime); QCOMPARE(wallet.creates, 1); QCOMPARE(wallet.internals, 0);
        auto original_review = observed->review;
        observed.reset(); commands.drain(); retirement.drain();
        QVERIFY(original_review->transaction); QCOMPARE(original_review->amount, CAmount{1000});
        original_review.reset(); commands.drain(); retirement.drain();
        QVERIFY(commands.quiescent()); QVERIFY(retirement.quiescent());
    }
    void closedCommandsDoNotCaptureValues()
    {
        ManualRoutedExecution commands, retirement;
        commands.scheduler->close();
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        int factories{0};
        const auto value = walletopening::RetainValues<int>(opening, [&] { ++factories; return std::make_unique<int>(1); });
        QVERIFY(!value);
        QCOMPARE(factories, 0);
    }
    void acceptedPreparationRetainsReviewAcrossCloseWithoutPresentation()
    {
        ManualRoutedExecution commands, retirement;
        auto wallet = std::make_shared<PrepareWallet>();
        auto opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        QVERIFY(opening->start(wallet)); commands.drain(); opening->take();
        auto action = opening->startAction();
        QVERIFY(action);
        int notices{0};
        auto operation = std::make_shared<execution::OwnedCommand<walletprepare::Result>>(commands.scheduler, [&] { ++notices; });
        QVERIFY(operation->startWith([&](auto done, auto failed) {
            return opening->perform(action, [](interfaces::Wallet& wallet) { return walletprepare::Execute(wallet, Request()); },
                std::move(done), std::move(failed));
        }));
        QCOMPARE(wallet->creates, 0); QVERIFY(!operation->result());
        QVERIFY(commands.step(execution::Lane::Core));
        QCOMPARE(wallet->creates, 1); QVERIFY(!operation->result());
        opening->close();
        QVERIFY(!opening->startAction());
        QVERIFY(commands.step(execution::Lane::Domain));
        QCOMPARE(notices, 1);
        QVERIFY(operation->result()->value->review);
        QCOMPARE(wallet->creates, 1); QCOMPARE(wallet->internals, 0);
        opening->retire(); operation.reset(); action.reset();
        commands.drain(); retirement.drain();
    }
};
BITCOINQML_REGISTER_QT_TEST(WalletPrepareTests)
#include <test_walletprepare.moc>
