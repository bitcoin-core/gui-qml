// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mockwallet.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/signertransactions.h>
#include <qml/models/bumptransactionmodel.h>
#include <wallet/coincontrol.h>

#include <QLocale>
#include <QSignalSpy>
#include <QScopeGuard>

#include <memory>
#include <stdexcept>

namespace {
const CTransactionRef TEST_ORIGINAL = [] {
    CMutableTransaction transaction{*signerfixture::Review().GetUnsignedTx()};
    transaction.vout.front().nValue += 100;
    return MakeTransactionRef(std::move(transaction));
}();
const auto TEST_TXID = QString::fromStdString(TEST_ORIGINAL->GetHash().GetHex());

CMutableTransaction ApprovedReplacement()
{
    return *signerfixture::Review().GetUnsignedTx();
}

void ConfigureBumpableWallet(MockWallet& wallet)
{
    wallet.transaction_can_be_bumped_fn = [](const Txid&) { return true; };
    wallet.try_get_tx_status_fn = [](const Txid&, interfaces::WalletTxStatus& status, int&, int64_t&) {
        status = {}; return true;
    };
    wallet.get_tx_fn = [](const Txid& txid) { return txid == TEST_ORIGINAL->GetHash() ? TEST_ORIGINAL : CTransactionRef{}; };
    wallet.create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
        CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
        old_fee = 500;
        new_fee = 1000;
        tx = ApprovedReplacement();
        return true;
    };
}

std::shared_ptr<MockWallet> MakeBumpableWallet()
{
    auto wallet = std::make_shared<MockWallet>();
    ConfigureBumpableWallet(*wallet);
    return wallet;
}
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<walletopening::Session> opening;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<BumpTransactionModel> model;
    explicit Fixture(const std::shared_ptr<MockWallet>& wallet, walletbump::Services bump_services = {}) {
        opening = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        opening->start(wallet); commands.drain(); opening->take();
        model = std::make_unique<BumpTransactionModel>();
        model->setOpening(opening, {commands.scheduler, [this](std::function<void()> turn) { publications.push_back(std::move(turn)); }}, std::move(bump_services));
    }
    ~Fixture() { if (model) model->close(); commands.drain(); model.reset(); opening->retire(); commands.drain(); retirement.drain(); }
    void publish() { while (!publications.empty()) { auto turn = std::move(publications.front()); publications.pop_front(); turn(); } }
    void admitCommitNative() {
        // M04 authorizes the exact review on Domain before admitting the
        // Native command. Hold that effect and its receipt independently.
        for (int turn = 0; turn < 16 && commands.nextLane() == execution::Lane::Domain; ++turn)
            QVERIFY(commands.step(execution::Lane::Domain));
        QCOMPARE(commands.nextLane(), std::optional{execution::Lane::Core});
    }
    void finish() {
        for (int turn = 0; turn < 16; ++turn) {
            commands.drain();
            publish();
            // A published review deliberately owns a reserved Native cleanup
            // receipt. Settled delivery does not imply physical retirement.
            if (commands.transportIdle() && publications.empty()) return;
        }
        throw std::logic_error{"fee-bump fixture did not settle"};
    }
    bool confirm() {
        const auto attempt = model->attemptId();
        const auto id = model->requestConfirmFeeBump();
        if (!id) return false;
        finish();
        return model->attemptId() == attempt && model->commitId() == id && model->state() == BumpTransactionModel::Succeeded;
    }
};
} // namespace

class BumpTransactionModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void uncertainReplacementCanOnlyObserveItsExactCandidate_data() {
        QTest::addColumn<int>("mode");
        const char* names[] = {"false-after-record", "false-without-record", "throw-after-record", "foreign-record", "different-witness", "read-throws"};
        for (int mode = 0; mode < 6; ++mode) QTest::newRow(names[mode]) << mode;
    }
    void uncertainReplacementCanOnlyObserveItsExactCandidate() {
        QFETCH(int, mode);
        struct OutcomeWallet : MockWallet {
            CTransactionRef recorded;
            bool throw_read{false}; int reads{0};
            interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus& status,
                std::vector<std::string>&, std::vector<std::string>&, bool& mempool, int&) override {
                ++reads;
                if (throw_read) throw std::runtime_error("observation failed");
                status = {}; mempool = true;
                interfaces::WalletTx result{}; result.tx = recorded; return result;
            }
        };
        auto wallet = std::make_shared<OutcomeWallet>(); ConfigureBumpableWallet(*wallet);
        wallet->sign_bump_transaction_fn = [](CMutableTransaction& tx) { for (auto& in : tx.vin) in.scriptSig = CScript{} << OP_0; return true; };
        wallet->commit_bump_transaction_fn = [native = wallet.get(), mode](const Txid&, CMutableTransaction&& tx, std::vector<bilingual_str>&, Txid&) {
            if (mode != 1) {
                if (mode == 3) ++tx.vout.front().nValue;
                if (mode == 4) tx.vin.front().scriptWitness.stack.push_back({1});
                native->recorded = MakeTransactionRef(std::move(tx));
            }
            if (mode == 2) throw std::runtime_error("commit returned no receipt");
            return false;
        };
        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        QVERIFY(fixture.model->requestConfirmFeeBump()); fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::Uncertain);
        const auto attempt = fixture.model->attemptId();
        QVERIFY(fixture.model->commitResult()->commit_attempted);
        fixture.model->reset();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::Uncertain);
        QVERIFY(!fixture.model->requestPrepareFeeBump(TEST_TXID, 1));
        QCOMPARE(fixture.model->requestReviewFeeBump(TEST_TXID, 1), attempt);
        QVERIFY(!fixture.model->requestReviewFeeBump(QStringLiteral("another-transaction"), 1));
        wallet->throw_read = mode == 5;
        QVERIFY(fixture.model->canRecheckOutcome());
        QVERIFY(fixture.model->requestRecheckFeeBumpOutcome());
        QVERIFY(!fixture.model->requestRecheckFeeBumpOutcome());
        auto observation = fixture.model->outcomeObservation(); QVERIFY(observation);
        QCOMPARE(wallet->reads, 0);
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(wallet->reads, 1); QVERIFY(!observation->result());
        for (int turn = 0; turn < 8 && !observation->result(); ++turn) QVERIFY(fixture.commands.step(execution::Lane::Domain));
        QVERIFY(observation->result()); QVERIFY(fixture.model->busy());
        fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::Uncertain);
        const bool found = mode == 0 || mode == 2;
        QCOMPARE(fixture.model->walletRecordState() == BumpTransactionModel::WalletRecordRecorded, found);
        QCOMPARE(fixture.model->newTxid().isEmpty(), !found);
        QVERIFY(!fixture.model->commitResult()->committed); // A read cannot invent the missing mutation receipt.
        QVERIFY(!fixture.model->requestConfirmFeeBump());
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
    }
    void admittedOutcomeObservationSurvivesQuietClose() {
        auto wallet = MakeBumpableWallet();
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid&) { return false; };
        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        QVERIFY(fixture.model->requestConfirmFeeBump()); fixture.finish();
        QVERIFY(fixture.model->requestRecheckFeeBumpOutcome());
        auto observation = fixture.model->outcomeObservation(); QVERIFY(observation);
        fixture.model->close(); fixture.model.reset(); fixture.opening->retire();
        fixture.commands.drain(); fixture.retirement.drain(); fixture.commands.drain(); fixture.publish();
        QVERIFY(observation->result()); QVERIFY(observation->result()->value);
        QVERIFY(!observation->result()->value->committed);
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        QVERIFY(fixture.commands.quiescent()); QVERIFY(fixture.retirement.quiescent());
    }
    void originalTimingIsPassedToThePublicBumpBuilder_data() {
        QTest::addColumn<uint32_t>("sequence");
        QTest::newRow("absolute-lock") << uint32_t{0xfffffffd};
        QTest::newRow("relative-lock") << uint32_t{17};
    }
    void originalTimingIsPassedToThePublicBumpBuilder() {
        QFETCH(uint32_t, sequence);
        auto wallet = MakeBumpableWallet();
        auto transaction = CMutableTransaction{*TEST_ORIGINAL};
        transaction.version = 3; transaction.nLockTime = 900'000;
        transaction.vin.front().nSequence = sequence;
        const auto original = MakeTransactionRef(transaction);
        wallet->get_tx_fn = [original](const Txid& id) { return id == original->GetHash() ? original : CTransactionRef{}; };
        bool exact_timing{false};
        wallet->create_bump_transaction_fn = [original, &exact_timing](const Txid&, const wallet::CCoinControl& control,
            std::vector<bilingual_str>&, CAmount& old_fee, CAmount& new_fee, CMutableTransaction& candidate) {
            exact_timing = control.m_version == original->version && control.m_locktime == original->nLockTime;
            for (const auto& input : original->vin) exact_timing &= control.GetSequence(input.prevout) == input.nSequence;
            candidate = CMutableTransaction{*original}; candidate.vout.front().nValue -= 100;
            old_fee = 500; new_fee = 600; return true;
        };
        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(QString::fromStdString(original->GetHash().GetHex()), 1)); fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsConfirmation);
        QVERIFY(exact_timing);
    }
    void aBusyWalletDoesNotFailThePreparation() {
        struct BusyWallet final : MockWallet {
            int reads{0};
            interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus& status,
                std::vector<std::string>&, std::vector<std::string>&, bool& mempool, int&) override {
                ++reads; status = {}; mempool = true;
                interfaces::WalletTx result{}; result.tx = TEST_ORIGINAL; return result;
            }
        };
        auto wallet = std::make_shared<BusyWallet>(); ConfigureBumpableWallet(*wallet);
        wallet->try_get_tx_status_fn = [](const Txid&, interfaces::WalletTxStatus&, int&, int64_t&) { return false; };
        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        QVERIFY2(fixture.model->errorText().isEmpty(), qPrintable(fixture.model->errorText()));
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsConfirmation);
        QCOMPARE(wallet->reads, 1);
    }
    void abandonedOriginalCannotBeRevivedByTheOldApproval_data() {
        QTest::addColumn<bool>("during_signing");
        QTest::newRow("before-signing") << false;
        QTest::newRow("during-signing") << true;
    }
    void abandonedOriginalCannotBeRevivedByTheOldApproval() {
        QFETCH(bool, during_signing);
        auto wallet = MakeBumpableWallet(); bool abandoned{false};
        wallet->try_get_tx_status_fn = [&abandoned](const Txid&, interfaces::WalletTxStatus& status, int&, int64_t&) {
            status = {}; status.is_abandoned = abandoned; return true;
        };
        wallet->sign_bump_transaction_fn = [&abandoned, during_signing](CMutableTransaction&) {
            if (during_signing) abandoned = true;
            return true;
        };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&& tx, std::vector<bilingual_str>&, Txid& id) { id = tx.GetHash(); return true; };
        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        if (!during_signing) abandoned = true;
        QVERIFY(fixture.model->requestConfirmFeeBump()); fixture.finish();
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), during_signing ? 1 : 0);
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsRevalidation);
    }
    void externalReplacementRequiresVerifiedSignatures_data() {
        QTest::addColumn<bool>("valid");
        QTest::newRow("valid-original-input-signatures") << true;
        QTest::newRow("invalid-unchanged-unsigned-terms") << false;
    }
    void externalReplacementRequiresVerifiedSignatures() {
        QFETCH(bool, valid);
        struct ExternalWallet : MockWallet { bool hasExternalSigner() override { return true; } };
        auto wallet = std::make_shared<ExternalWallet>();
        ConfigureBumpableWallet(*wallet);
        const auto funding = signerfixture::Review().inputs.front().non_witness_utxo;
        wallet->get_tx_fn = [funding](const Txid& id) {
            return id == TEST_ORIGINAL->GetHash() ? TEST_ORIGINAL
                : id == funding->GetHash() ? funding : CTransactionRef{};
        };
        wallet->get_coins_fn = [funding](const std::vector<COutPoint>& inputs) {
            std::vector<interfaces::WalletTxOut> result;
            for (const auto& input : inputs) {
                interfaces::WalletTxOut coin{};
                if (input.hash == funding->GetHash() && input.n < funding->vout.size())
                    coin.txout = funding->vout[input.n];
                result.push_back(std::move(coin));
            }
            return result;
        };
        wallet->sign_bump_transaction_fn = [valid](CMutableTransaction& transaction) {
            for (auto& input : transaction.vin) input.scriptSig = valid ? CScript{} << OP_0 : CScript{};
            return true;
        };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&& tx, std::vector<bilingual_str>&, Txid& id) {
            id = tx.GetHash(); return true;
        };
        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        QVERIFY(fixture.model->requestConfirmFeeBump()); fixture.finish();
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), valid ? 1 : 0);
        QCOMPARE(fixture.model->state(), valid ? BumpTransactionModel::Succeeded : BumpTransactionModel::Failed);
    }
    void quietCloseRetiresPreparedReviewBeforeFacadeDestruction_data() {
        QTest::addColumn<int>("stage");
        QTest::newRow("before-native")<<0;QTest::newRow("held-outcome")<<1;QTest::newRow("published-review")<<2;
    }
    void quietCloseRetiresPreparedReviewBeforeFacadeDestruction() {
        QFETCH(int,stage);auto wallet=MakeBumpableWallet();
        wallet->create_bump_transaction_fn=[](const Txid&,const wallet::CCoinControl&,std::vector<bilingual_str>&,CAmount& old_fee,CAmount& new_fee,CMutableTransaction& tx){
            old_fee=500;new_fee=1000;tx=*signerfixture::Review().GetUnsignedTx();return true;
        };
        Fixture fixture{wallet};QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID,1));
        if(stage==1)fixture.commands.drain();
        if(stage==2){fixture.finish();QCOMPARE(fixture.model->state(),BumpTransactionModel::NeedsConfirmation);}
        QSignalSpy notices{fixture.model.get(),&BumpTransactionModel::stateChanged};
        fixture.model->close();fixture.opening->retire();fixture.finish();fixture.commands.drain();fixture.retirement.drain();
        QCOMPARE(fixture.model->state(),BumpTransactionModel::Closed);QVERIFY(!fixture.model->busy());QCOMPARE(notices.count(),0);
        QCOMPARE(wallet->calls.createBumpTransaction.load(),1);QCOMPARE(wallet->calls.commitBumpTransaction.load(),0);
        QCOMPARE(fixture.commands.scheduler->pending(),size_t{0});QVERIFY(fixture.commands.quiescent());
    }
    void longCapturedPasswordKeepsOriginalCommitAndLockOwnership_data() {
        QTest::addColumn<bool>("close_first");
        QTest::newRow("held-gui") << false; QTest::newRow("close-before-effect") << true;
    }
    void longCapturedPasswordKeepsOriginalCommitAndLockOwnership() {
        QFETCH(bool, close_first);
        struct Wallet final : MockWallet {
            bool locked{true}; int unlocks{0}, locks{0};
            bool isCrypted() override { return true; } bool isLocked() override { return locked; }
            util::Expected<void, wallet::WalletError> unlock(const SecureString& value) override {
                ++unlocks;
                if (value != SecureString(40'000, 'p')) return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("Wrong original")}};
                locked = false; return {};
            }
            bool lock() override { ++locks; locked = true; return true; }
        };
        const auto wallet = std::make_shared<Wallet>(); ConfigureBumpableWallet(*wallet);
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        bool committed_locked{false};
        wallet->commit_bump_transaction_fn = [&](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& id) {
            committed_locked = wallet->locked; id = Txid::FromUint256(uint256::ONE); return true;
        };
        Fixture fixture{wallet}; QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsConfirmation);
        QString password(40'000, QChar{'p'});
        const auto ticket = fixture.model->requestConfirmFeeBumpWithPassphrase(password); QVERIFY(ticket);
        const auto owner = fixture.model->commitOperation(); password = "changed editor";
        QCOMPARE(wallet->unlocks, 0); QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
        if (close_first) fixture.model->close();
        fixture.admitCommitNative();
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(wallet->unlocks, 1); QCOMPARE(wallet->locks, 1); QVERIFY(committed_locked);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1); QVERIFY(!owner->result());
        fixture.commands.drain(); QVERIFY(owner->result()); QVERIFY(owner->result()->value->committed);
        fixture.finish();
        QCOMPARE(fixture.model->state(), close_first ? BumpTransactionModel::Closed : BumpTransactionModel::Succeeded);
    }
    void manualPreparationDoesNotPublishBeforeItsOutcome()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        Fixture fixture{wallet}; auto& model = *fixture.model;
        QVERIFY(model.requestPrepareFeeBump(TEST_TXID, 1));
        QCOMPARE(model.state(), BumpTransactionModel::Preparing);
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 0);
        QVERIFY(fixture.commands.step(execution::Lane::Domain)); // Begin replacement owner action.
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 0);
        QCOMPARE(model.state(), BumpTransactionModel::Preparing);
        QVERIFY(fixture.commands.step(execution::Lane::Domain)); // Admit its captured Core effect.
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 0);
        QVERIFY(fixture.publications.empty());
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1);
        QCOMPARE(model.state(), BumpTransactionModel::Preparing);
        QVERIFY(fixture.publications.empty());
        QVERIFY(fixture.commands.step(execution::Lane::Domain));
        QCOMPARE(fixture.publications.size(), size_t{1});
        fixture.publish();
        QCOMPARE(model.state(), BumpTransactionModel::Preparing);
        QVERIFY(model.busy());
        QVERIFY(fixture.commands.step(execution::Lane::Domain)); // Install approved candidate in Domain.
        QVERIFY(fixture.commands.step(execution::Lane::Domain)); // Deliver installation outcome.
        QCOMPARE(model.state(), BumpTransactionModel::Preparing);
        QCOMPARE(fixture.publications.size(), size_t{1});
        fixture.publish();
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);
        QVERIFY(!model.busy());
    }
    void committedOutcomeSurvivesResetCloseAndObserverLoss()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& id) {
            id = Txid::FromUint256(uint256::ONE); return true;
        };
        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        const auto commit = fixture.model->requestConfirmFeeBump(); QVERIFY(commit);
        QVERIFY(!fixture.model->requestConfirmFeeBump());
        const auto owner = fixture.model->commitOperation(); QVERIFY(owner);
        fixture.model->reset();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::Committing);
        QVERIFY(!fixture.model->requestPrepareFeeBump(TEST_TXID, 1));
        fixture.model->close();
        fixture.model.reset();
        fixture.opening->close();
        fixture.admitCommitNative();
        QVERIFY(fixture.commands.step(execution::Lane::Core));
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        QVERIFY(!owner->result());
        for (int turn = 0; turn < 16 && !owner->result(); ++turn)
            QVERIFY(fixture.commands.step(execution::Lane::Domain));
        QVERIFY(owner->result()); QVERIFY(owner->result()->value);
        const auto result = *owner->result()->value;
        QVERIFY(result.committed);
        QCOMPARE(result.txid, Txid::FromUint256(uint256::ONE));
        QCOMPARE(result.original.GetHex(), TEST_TXID.toStdString());
        fixture.publish();
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
    }
    void failedGuiTransportDoesNotRepeatCommittedMutation()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& id) {
            id = Txid::FromUint256(uint256::ONE); return true;
        };
        Fixture fixture{wallet}; bool fail{false};
        fixture.model->setOpening(fixture.opening, {fixture.commands.scheduler, [&fixture, &fail](std::function<void()> turn) {
            if (std::exchange(fail, false)) throw std::runtime_error{"GUI transport unavailable"};
            fixture.publications.push_back(std::move(turn));
        }});
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        fail = true; QVERIFY(fixture.model->requestConfirmFeeBump()); fixture.commands.drain();
        QVERIFY(fixture.model->busy());
        const auto owner = fixture.model->commitOperation();
        QVERIFY(owner->result()); QVERIFY(owner->result()->publication_failed); QVERIFY(owner->result()->value->committed);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        fixture.model->retryPublications();
        fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::Succeeded);
        fixture.model->retryPublications(); fixture.publish();
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
    }
    void signerSubstitutionCannotCommitReplacement_data()
    {
        QTest::addColumn<int>("mutation");
        for (size_t i = 0; i < signerfixture::CASES.size(); ++i)
            QTest::newRow(signerfixture::CASES[i].first) << int(i);
    }
    void signerSubstitutionCannotCommitReplacement()
    {
        QFETCH(int, mutation);
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                               CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000;
            tx = *signerfixture::Review().GetUnsignedTx();
            return true;
        };
        wallet->sign_bump_transaction_fn = [mutation](CMutableTransaction& tx) {
            signerfixture::Change(tx, signerfixture::CASES.at(mutation).second);
            return true;
        };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid&) { return true; };
        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QVERIFY(!fixture.confirm());
        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
        QVERIFY(model.newTxid().isEmpty());
    }
    void temporaryUnlockRestoresBeforeCommit_data()
    {
        QTest::addColumn<int>("scenario");
        for (int scenario = 0; scenario < 6; ++scenario) QTest::newRow(qPrintable(QString::number(scenario))) << scenario;
    }
    void temporaryUnlockRestoresBeforeCommit()
    {
        QFETCH(int, scenario);
        struct Wallet : MockWallet {
            int unlocks{0}, locks{0};
            bool locked{true}, throw_unlock{false}, fail_lock{false};
            bool isCrypted() override { return true; }
            bool isLocked() override { return locked; }
            util::Expected<void, wallet::WalletError> unlock(const SecureString& passphrase) override {
                ++unlocks;
                if (throw_unlock) { locked = false; throw std::runtime_error{"Unlock outcome lost"}; }
                if (passphrase != "secret") return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("Incorrect password")}};
                locked = false; return {};
            }
            bool lock() override { ++locks; if (fail_lock) return false; locked = true; return true; }
        };
        const auto wallet = std::make_shared<Wallet>(); ConfigureBumpableWallet(*wallet);
        wallet->throw_unlock = scenario == 5; wallet->fail_lock = scenario == 4;
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [scenario](CMutableTransaction&) {
            if (scenario == 3) throw std::runtime_error{"Sign outcome lost"};
            return true;
        };
        bool committed_locked{false};
        wallet->commit_bump_transaction_fn = [&](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& id) {
            committed_locked = wallet->locked; id = Txid::FromUint256(uint256::ONE); return true;
        };
        Fixture fixture{wallet}; QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        const auto id = scenario == 0 ? fixture.model->requestConfirmFeeBump()
            : fixture.model->requestConfirmFeeBumpWithPassphrase(scenario == 1 ? "wrong" : "secret");
        QVERIFY(id); fixture.finish();
        // #977: an exception from signing precedes any commit, so it is a
        // definite Sign failure (scenario 3), not an uncertain outcome.
        const walletbump::Error errors[] = {walletbump::Error::NeedsUnlock, walletbump::Error::IncorrectPassphrase,
            walletbump::Error::None, walletbump::Error::Sign, walletbump::Error::RelockFailed, walletbump::Error::Uncertain};
        const auto result = fixture.model->commitResult(); QVERIFY(result);
        QCOMPARE(result->error, errors[scenario]); QCOMPARE(wallet->locks, scenario <= 1 ? 0 : 1);
        QCOMPARE(fixture.model->unlockErrorText().isEmpty(), scenario != 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), scenario == 2 ? 1 : 0);
        QCOMPARE(result->committed, scenario == 2);
        if (scenario == 2) QVERIFY(committed_locked);
    }
    void eligibilityChangingDuringSigningCannotCommit()
    {
        const auto wallet = MakeBumpableWallet();
        bool eligible{true};
        wallet->transaction_can_be_bumped_fn = [&](const Txid&) { return eligible; };
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [&](CMutableTransaction&) { eligible = false; return true; };
        Fixture fixture{wallet}; QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1)); fixture.finish();
        QVERIFY(!fixture.confirm());
        QCOMPARE(fixture.model->commitResult()->error, walletbump::Error::Ineligible);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
    }

    void replacementAttemptKeepsApprovedInputScopeAndRequiresFreshIntent_data()
    {
        QTest::addColumn<int>("stale_stage");
        QTest::newRow("added-input-spent-before-signing") << 0;
        QTest::newRow("added-input-spent-during-signing") << 1;
    }

    void replacementAttemptKeepsApprovedInputScopeAndRequiresFreshIntent()
    {
        QFETCH(int, stale_stage);
        auto wallet = MakeBumpableWallet();
        const COutPoint added{Txid::FromUint256(uint256::FromHex("cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc").value()), 7};
        bool added_spent{false};
        bool spend_during_sign{stale_stage == 1};
        bool coins_scope_exact{true};
        bool commit_original_exact{true};
        std::vector<COutPoint> prepared_inputs, signed_inputs, committed_inputs;
        wallet->get_coins_fn = [&](const std::vector<COutPoint>& inputs) {
            coins_scope_exact &= inputs.size() == 1 && inputs.front() == added;
            interfaces::WalletTxOut coin{};
            coin.depth_in_main_chain = 6;
            coin.is_spent = added_spent;
            return std::vector<interfaces::WalletTxOut>{coin};
        };
        wallet->create_bump_transaction_fn = [&](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000;
            tx = ApprovedReplacement();
            tx.vin.emplace_back(added);
            prepared_inputs.clear();
            for (const auto& input : tx.vin) prepared_inputs.push_back(input.prevout);
            return true;
        };
        wallet->sign_bump_transaction_fn = [&](CMutableTransaction& tx) {
            signed_inputs.clear();
            for (const auto& input : tx.vin) signed_inputs.push_back(input.prevout);
            if (spend_during_sign) added_spent = true;
            return true;
        };
        wallet->commit_bump_transaction_fn = [&](const Txid& original, CMutableTransaction&& tx,
            std::vector<bilingual_str>&, Txid& result) {
            commit_original_exact &= original == TEST_ORIGINAL->GetHash();
            committed_inputs.clear();
            for (const auto& input : tx.vin) committed_inputs.push_back(input.prevout);
            result = tx.GetHash();
            return true;
        };

        Fixture fixture{wallet};
        const auto first_attempt = fixture.model->requestPrepareFeeBump(TEST_TXID, 2);
        QVERIFY(first_attempt);
        fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsConfirmation);
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1);
        QVERIFY(coins_scope_exact);
        QVERIFY(commit_original_exact);
        QCOMPARE(prepared_inputs.size(), size_t{3});
        QVERIFY(prepared_inputs.back() == added);

        if (stale_stage == 0) added_spent = true;
        QVERIFY(fixture.model->requestConfirmFeeBump());
        fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsRevalidation);
        QVERIFY(!fixture.model->errorText().isEmpty());
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
        QCOMPARE(wallet->calls.signBumpTransaction.load(), stale_stage == 0 ? 0 : 1);
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1); // No silent candidate reconstruction.
        if (stale_stage == 0) QVERIFY(signed_inputs.empty());
        else QVERIFY(signed_inputs == prepared_inputs);

        // Only a new explicit prepare intent may construct and approve another
        // candidate after the old added input becomes unavailable.
        added_spent = false;
        spend_during_sign = false;
        const auto second_attempt = fixture.model->requestPrepareFeeBump(TEST_TXID, 2);
        QVERIFY(second_attempt > first_attempt);
        fixture.finish();
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsConfirmation);
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 2);
        QVERIFY(fixture.confirm());
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        QVERIFY(signed_inputs == prepared_inputs);
        QVERIFY(committed_inputs == prepared_inputs);
    }

    void knownReplacementPolicyConflictRequiresExplicitReprepare()
    {
        auto wallet = MakeBumpableWallet();
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        int reads{0};
        Txid inspected_hash;
        CAmount inspected_fee{0};
        bool inspected_candidate{false};
        walletbump::Services services;
        services.admission = [&](CTransactionRef candidate, CAmount fee) {
            ++reads;
            inspected_candidate = bool(candidate);
            if (candidate) inspected_hash = candidate->GetHash();
            inspected_fee = fee;
            psbtpolicy::Facts facts;
            facts.admission = psbtpolicy::Admission::Rejected;
            facts.admission_error = QStringLiteral("txn-mempool-conflict");
            return facts;
        };
        Fixture fixture{wallet, std::move(services)};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 2));
        fixture.finish();
        QVERIFY(fixture.model->requestConfirmFeeBump());
        fixture.finish();
        QCOMPARE(reads, 1);
        QVERIFY(inspected_candidate);
        QCOMPARE(inspected_hash, ApprovedReplacement().GetHash());
        QCOMPARE(inspected_fee, CAmount{1000});
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
        QVERIFY(fixture.model->commitResult());
        QCOMPARE(fixture.model->commitResult()->error, walletbump::Error::Policy);
        QCOMPARE(fixture.model->state(), BumpTransactionModel::NeedsRevalidation);
        QVERIFY(fixture.model->errorText().contains(QStringLiteral("txn-mempool-conflict")));
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1);
    }
    void stateIsIdleByDefault()
    {
        auto wallet = MakeBumpableWallet();
        Fixture fixture{wallet};
        auto& model = *fixture.model;
        QCOMPARE(model.state(), BumpTransactionModel::Idle);
    }

    void prepareFeeBump_transitionsToNeedsConfirmation()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500;
            new_fee = 1000;
            tx = ApprovedReplacement();
            return true;
        };

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        QSignalSpy stateSpy(&model, &BumpTransactionModel::stateChanged);

        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);
        QVERIFY(stateSpy.count() >= 2); // Idle -> Preparing -> NeedsConfirmation
    }

    void feePresentationOwnsFormattedValues_data()
    {
        QTest::addColumn<QString>("locale");
        QTest::addColumn<int>("unit");
        QTest::addColumn<QString>("old_fee");
        QTest::addColumn<QString>("new_fee");
        QTest::addColumn<QString>("increase");
        QTest::newRow("btc-en") << QString{"en_US"} << 0 << QString{"0.00000001 BTC"} << QString{"0.00023456 BTC"} << QString{"0.00023455 BTC"};
        QTest::newRow("mbtc-en") << QString{"en_US"} << 1 << QString{"0.00001 mBTC"} << QString{"0.23456 mBTC"} << QString{"0.23455 mBTC"};
        QTest::newRow("bits-en") << QString{"en_US"} << 2 << QString{"0.01 bits"} << QString{"234.56 bits"} << QString{"234.55 bits"};
        QTest::newRow("sats-en") << QString{"en_US"} << 3 << QString{"1 sat"} << QString{"23,456 sats"} << QString{"23,455 sats"};
        QTest::newRow("btc-de") << QString{"de_DE"} << 0 << QString{"0,00000001 BTC"} << QString{"0,00023456 BTC"} << QString{"0,00023455 BTC"};
        QTest::newRow("mbtc-de") << QString{"de_DE"} << 1 << QString{"0,00001 mBTC"} << QString{"0,23456 mBTC"} << QString{"0,23455 mBTC"};
        QTest::newRow("bits-de") << QString{"de_DE"} << 2 << QString{"0,01 bits"} << QString{"234,56 bits"} << QString{"234,55 bits"};
        QTest::newRow("sats-de") << QString{"de_DE"} << 3 << QString{"1 sat"} << QString{"23.456 sats"} << QString{"23.455 sats"};
    }

    void feePresentationOwnsFormattedValues()
    {
        QFETCH(QString, locale);
        QFETCH(int, unit);
        QFETCH(QString, old_fee);
        QFETCH(QString, new_fee);
        QFETCH(QString, increase);
        const QLocale previous_locale;
        const auto restore_locale = qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
        QLocale::setDefault(QLocale{locale});
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_amount, CAmount& new_amount, CMutableTransaction& tx) {
            old_amount = 1;
            new_amount = 23'456;
            tx = ApprovedReplacement();
            return true;
        };
        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.setDisplayUnit(unit);
        QVERIFY(model.requestPrepareFeeBump(TEST_TXID, 1));
        fixture.finish();
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);
        QCOMPARE(model.oldFee(), old_fee);
        QCOMPARE(model.newFee(), new_fee);
        QCOMPARE(model.feeIncrease(), increase);
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1);
        // A subsequent presentation update uses the retained original amounts,
        // independently of the lifetime of the formatter's temporary strings.
        model.setDisplayUnit((unit + 1) % 4);
        model.setDisplayUnit(unit);
        QCOMPARE(model.oldFee(), old_fee);
        QCOMPARE(model.newFee(), new_fee);
        QCOMPARE(model.feeIncrease(), increase);
        QCOMPARE(wallet->calls.createBumpTransaction.load(), 1);
    }

    void prepareFeeBump_populatesFees()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500;
            new_fee = 1000;
            tx = ApprovedReplacement();
            return true;
        };

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

        QVERIFY(!model.oldFee().isEmpty());
        QVERIFY(!model.newFee().isEmpty());
        QVERIFY(!model.feeIncrease().isEmpty());
    }

    void prepareFeeBump_failureSetsErrorState()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>& errors,
                                                CAmount&, CAmount&, CMutableTransaction&) {
            errors.emplace_back(Untranslated("insufficient fee"));
            return false;
        };

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QVERIFY(!model.errorText().isEmpty());
    }

    void prepareFeeBump_invalidTxidFails()
    {
        auto wallet = MakeBumpableWallet();
        Fixture fixture{wallet};
        auto& model = *fixture.model;

        model.prepareFeeBump(QStringLiteral("not-a-txid"), 1);
        fixture.finish();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
    }

    void prepareFeeBump_nullWalletFails()
    {
        BumpTransactionModel model;

        model.prepareFeeBump(TEST_TXID, 1);

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
    }

    void confirmFeeBump_signsAndCommits()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500;
            new_fee = 1000;
            tx = ApprovedReplacement();
            return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& bumped_txid) {
            bumped_txid = Txid::FromUint256(*uint256::FromHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
            return true;
        };

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);

        fixture.confirm();

        QCOMPARE(model.state(), BumpTransactionModel::Succeeded);
        QVERIFY(!model.newTxid().isEmpty());
    }

    void confirmFeeBumpAllowsSignatureDataAndKeepsOriginalConflict()
    {
        auto wallet = MakeBumpableWallet();
        const auto approved = *signerfixture::Review().GetUnsignedTx();
        wallet->create_bump_transaction_fn = [approved](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                       CAmount&, CAmount&, CMutableTransaction& tx) { tx = approved; return true; };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction& tx) {
            for (auto& input : tx.vin) { input.scriptSig << OP_TRUE; input.scriptWitness.stack = {{1, 2}}; }
            return true;
        };
        std::optional<Txid> original;
        std::optional<CMutableTransaction> committed;
        wallet->commit_bump_transaction_fn = [&](const Txid& txid, CMutableTransaction&& tx, std::vector<bilingual_str>&, Txid&) {
            original = txid; committed = std::move(tx); return true;
        };
        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QVERIFY(fixture.confirm());
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        QVERIFY(original); QVERIFY(committed);
        QCOMPARE(original->GetHex(), TEST_TXID.toStdString());
        QVERIFY(!committed->vin[0].scriptSig.empty());
        QVERIFY(!committed->vin[0].scriptWitness.IsNull());
        for (auto& input : committed->vin) { input.scriptSig.clear(); input.scriptWitness.SetNull(); }
        QCOMPARE(committed->GetHash(), approved.GetHash());
    }
    void committedFeeBumpSeparatesWalletRecordFromMempoolObservation_data()
    {
        QTest::addColumn<int>("scenario");
        const char* names[] = {"mempool", "confirmed", "not-in-mempool", "conflicted", "abandoned", "missing", "foreign-record", "read-error"};
        for (int scenario = 0; scenario < 8; ++scenario) QTest::newRow(names[scenario]) << scenario;
    }
    void committedFeeBumpSeparatesWalletRecordFromMempoolObservation()
    {
        QFETCH(int, scenario);
        struct Wallet final : MockWallet {
            int observation{0};
            CTransactionRef replacement;
            interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus& status,
                std::vector<std::string>&, std::vector<std::string>&, bool& in_mempool, int&) override
            {
                if (observation == 7) throw std::runtime_error{"wallet status unavailable"};
                status = {}; in_mempool = observation == 0;
                status.depth_in_main_chain = observation == 1 ? 2 : observation == 3 ? -1 : 0;
                status.is_abandoned = observation == 4;
                interfaces::WalletTx record{};
                if (observation == 5) return record;
                record.tx = replacement;
                if (observation == 6) {
                    CMutableTransaction foreign{*replacement}; ++foreign.vout[0].nValue;
                    record.tx = MakeTransactionRef(std::move(foreign));
                }
                return record;
            }
        };
        const auto wallet = std::make_shared<Wallet>();
        wallet->observation = scenario;
        ConfigureBumpableWallet(*wallet);
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
            CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500; new_fee = 1000; tx = *signerfixture::Review().GetUnsignedTx(); return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [wallet_ptr = wallet.get()](const Txid&, CMutableTransaction&& tx,
            std::vector<bilingual_str>&, Txid& txid) {
            wallet_ptr->replacement = MakeTransactionRef(std::move(tx));
            txid = wallet_ptr->replacement->GetHash();
            return true;
        };

        Fixture fixture{wallet};
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1));
        fixture.finish();
        QVERIFY(fixture.confirm());
        QCOMPARE(fixture.model->state(), BumpTransactionModel::Succeeded);
        const auto result = fixture.model->commitResult(); QVERIFY(result);
        const walletbump::Record expected_record[] = {walletbump::Record::Recorded, walletbump::Record::Recorded,
            walletbump::Record::Recorded, walletbump::Record::Recorded, walletbump::Record::Recorded,
            walletbump::Record::AbsentAtObservation, walletbump::Record::AbsentAtObservation, walletbump::Record::Unknown};
        const walletbump::Submission expected_submission[] = {walletbump::Submission::Mempool, walletbump::Submission::Confirmed,
            walletbump::Submission::NotInMempool, walletbump::Submission::Conflicted, walletbump::Submission::Abandoned,
            walletbump::Submission::Unknown, walletbump::Submission::Unknown, walletbump::Submission::Unknown};
        QCOMPARE(result->record, expected_record[scenario]);
        QCOMPARE(result->submission, expected_submission[scenario]);
        QCOMPARE(result->relay, walletbump::Relay::Unknown);
        QCOMPARE(fixture.model->walletRecordState(), static_cast<BumpTransactionModel::WalletRecordState>(expected_record[scenario]));
        QCOMPARE(fixture.model->walletSubmissionState(), static_cast<BumpTransactionModel::WalletSubmissionState>(expected_submission[scenario]));
        QCOMPARE(fixture.model->relayState(), BumpTransactionModel::RelayUnknown);
        QCOMPARE(result->original.GetHex(), TEST_TXID.toStdString());
        QCOMPARE(result->txid, wallet->replacement->GetHash());

        fixture.model->reset();
        QVERIFY(fixture.model->requestPrepareFeeBump(TEST_TXID, 1));
        QCOMPARE(fixture.model->walletRecordState(), BumpTransactionModel::WalletRecordUnknown);
        QCOMPARE(fixture.model->walletSubmissionState(), BumpTransactionModel::WalletSubmissionUnknown);
    }
    void confirmFeeBumpReportsCommitFailure_data()
    {
        QTest::addColumn<bool>("explicit_error");
        QTest::newRow("default-error") << false;
        QTest::newRow("core-error") << true;
    }
    void confirmFeeBumpReportsCommitFailure()
    {
        QFETCH(bool, explicit_error);
        auto wallet = MakeBumpableWallet();
        const auto approved = *signerfixture::Review().GetUnsignedTx();
        wallet->create_bump_transaction_fn = [approved](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                       CAmount&, CAmount&, CMutableTransaction& tx) { tx = approved; return true; };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction& tx) { tx.vin[0].scriptSig << OP_TRUE; return true; };
        std::optional<Txid> original;
        std::optional<CMutableTransaction> attempted;
        wallet->commit_bump_transaction_fn = [&](const Txid& txid, CMutableTransaction&& tx, std::vector<bilingual_str>& errors, Txid& bumped_txid) {
            original = txid;
            attempted = std::move(tx); // Core may consume its argument even when commit fails.
            bumped_txid = Txid::FromUint256(uint256::ONE);
            if (explicit_error) errors.emplace_back(Untranslated("commit refused"));
            return false;
        };
        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QVERIFY(!fixture.confirm());
        QCOMPARE(model.state(), BumpTransactionModel::Uncertain);
        QCOMPARE(model.errorText(), explicit_error ? QString("commit refused") : QString("The replacement may not have been sent. Check the transaction before trying again."));
        model.reset();
        QVERIFY(!model.requestPrepareFeeBump(TEST_TXID, 1));
        QVERIFY(model.newTxid().isEmpty());
        QCOMPARE(wallet->calls.signBumpTransaction.load(), 1);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
        QVERIFY(original); QVERIFY(attempted);
        QCOMPARE(original->GetHex(), TEST_TXID.toStdString());
        QVERIFY(!attempted->vin[0].scriptSig.empty());
        attempted->vin[0].scriptSig.clear();
        QCOMPARE(attempted->GetHash(), approved.GetHash());
        QVERIFY(!fixture.confirm()); // Failure is terminal until a new review.
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 1);
    }
    void confirmFeeBump_signFailureDoesNotCommit()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500;
            new_fee = 1000;
            tx = ApprovedReplacement();
            return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return false; };

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

        fixture.confirm();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
    }

    void confirmFeeBump_rejectsWhenNotReady()
    {
        auto wallet = MakeBumpableWallet();
        Fixture fixture{wallet};
        auto& model = *fixture.model;

        fixture.confirm();

        QCOMPARE(model.state(), BumpTransactionModel::Idle);
    }

    void confirmFeeBump_rechecksEligibility()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500;
            new_fee = 1000;
            tx = ApprovedReplacement();
            return true;
        };

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();

        wallet->transaction_can_be_bumped_fn = [](const Txid&) { return false; };
        fixture.confirm();

        QCOMPARE(model.state(), BumpTransactionModel::NeedsRevalidation);
    }

    void reset_clearsState()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction& tx) {
            old_fee = 500;
            new_fee = 1000;
            tx = ApprovedReplacement();
            return true;
        };

        Fixture fixture{wallet};
        auto& model = *fixture.model;
        model.prepareFeeBump(TEST_TXID, 1);
        fixture.finish();
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);

        model.reset();

        QCOMPARE(model.state(), BumpTransactionModel::Idle);
        QVERIFY(model.newTxid().isEmpty());
        QVERIFY(model.errorText().isEmpty());
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BumpTransactionModelTests)
#else
QTEST_MAIN(BumpTransactionModelTests)
#endif
#include <test_bumptransactionmodel.moc>
