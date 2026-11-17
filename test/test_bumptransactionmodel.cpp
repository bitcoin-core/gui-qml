// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mockwallet.h>
#include <qml/models/bumptransactionmodel.h>

#include <QSignalSpy>

#include <memory>

namespace {
const auto TEST_TXID = QStringLiteral("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa");

std::unique_ptr<MockWallet> MakeBumpableWallet()
{
    auto wallet = std::make_unique<MockWallet>();
    wallet->transaction_can_be_bumped_fn = [](const Txid&) { return true; };
    return wallet;
}
} // namespace

class BumpTransactionModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void stateIsIdleByDefault()
    {
        auto wallet = MakeBumpableWallet();
        BumpTransactionModel model(wallet.get());
        QCOMPARE(model.state(), BumpTransactionModel::Idle);
    }

    void prepareFeeBump_transitionsToNeedsConfirmation()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&, std::vector<bip352::SilentPaymentsDestination>&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };

        BumpTransactionModel model(wallet.get());
        QSignalSpy stateSpy(&model, &BumpTransactionModel::stateChanged);

        model.prepareFeeBump(TEST_TXID, 1);

        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);
        QVERIFY(stateSpy.count() >= 2); // Idle -> Preparing -> NeedsConfirmation
    }

    void prepareFeeBump_populatesFees()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&, std::vector<bip352::SilentPaymentsDestination>&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);

        QVERIFY(!model.oldFee().isEmpty());
        QVERIFY(!model.newFee().isEmpty());
        QVERIFY(!model.feeIncrease().isEmpty());
    }

    void prepareFeeBump_failureSetsErrorState()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>& errors,
                                                CAmount&, CAmount&, CMutableTransaction&, std::vector<bip352::SilentPaymentsDestination>&) {
            errors.emplace_back(Untranslated("insufficient fee"));
            return false;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QVERIFY(!model.errorText().isEmpty());
    }

    void prepareFeeBump_invalidTxidFails()
    {
        auto wallet = MakeBumpableWallet();
        BumpTransactionModel model(wallet.get());

        model.prepareFeeBump(QStringLiteral("not-a-txid"), 1);

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
    }

    void prepareFeeBump_nullWalletFails()
    {
        BumpTransactionModel model(nullptr);

        model.prepareFeeBump(TEST_TXID, 1);

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
    }

    void confirmFeeBump_signsAndCommits()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&, std::vector<bip352::SilentPaymentsDestination>&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return true; };
        wallet->commit_bump_transaction_fn = [](const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& bumped_txid, const std::vector<bip352::SilentPaymentsDestination>&) {
            bumped_txid = Txid::FromUint256(*uint256::FromHex("bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb"));
            return true;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);
        QCOMPARE(model.state(), BumpTransactionModel::NeedsConfirmation);

        model.confirmFeeBump();

        QCOMPARE(model.state(), BumpTransactionModel::Succeeded);
        QVERIFY(!model.newTxid().isEmpty());
    }

    void confirmFeeBump_signFailureDoesNotCommit()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&, std::vector<bip352::SilentPaymentsDestination>&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };
        wallet->sign_bump_transaction_fn = [](CMutableTransaction&) { return false; };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);

        model.confirmFeeBump();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
        QCOMPARE(wallet->calls.commitBumpTransaction.load(), 0);
    }

    void confirmFeeBump_rejectsWhenNotReady()
    {
        auto wallet = MakeBumpableWallet();
        BumpTransactionModel model(wallet.get());

        model.confirmFeeBump();

        QCOMPARE(model.state(), BumpTransactionModel::Idle);
    }

    void confirmFeeBump_rechecksEligibility()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&, std::vector<bip352::SilentPaymentsDestination>&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);

        wallet->transaction_can_be_bumped_fn = [](const Txid&) { return false; };
        model.confirmFeeBump();

        QCOMPARE(model.state(), BumpTransactionModel::Failed);
    }

    void reset_clearsState()
    {
        auto wallet = MakeBumpableWallet();
        wallet->create_bump_transaction_fn = [](const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&,
                                                CAmount& old_fee, CAmount& new_fee, CMutableTransaction&, std::vector<bip352::SilentPaymentsDestination>&) {
            old_fee = 500;
            new_fee = 1000;
            return true;
        };

        BumpTransactionModel model(wallet.get());
        model.prepareFeeBump(TEST_TXID, 1);
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
#include "test_bumptransactionmodel.moc"
