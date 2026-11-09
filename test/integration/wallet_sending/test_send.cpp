// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <rpc/util.h>
#include <sync.h>
#include <wallet/spend.h>
#include <wallet/wallet.h>
#include <qt/bitcoinunits.h>

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <cmath>
#include <optional>

class SendIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

    struct Sweep {
        WalletQmlModel* wallet;
        WalletQmlModel* destination_wallet;
        QString address;
        UniValue locked_input;
        CAmount expected_amount;
    };

    std::optional<Sweep> prepareSweep(bool manual, bool self_send = false, bool fund_destination = false)
    {
        auto* destination = qmltest::CreateWallet(m_app, "destination");
        if (!destination || (fund_destination && !qmltest::FundWallet(m_app, *destination))) return std::nullopt;
        const auto external_address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::CreateWallet(m_app, "sender");
        if (!wallet || !qmltest::FundWallet(m_app, *wallet)) return std::nullopt;
        const auto mining_address = qmltest::NewAddress(m_app, wallet->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[2,\"" + mining_address.toStdString() + "\"]"));
        const auto address = self_send ? qmltest::NewAddress(m_app, wallet->name()) : external_address;
        const auto coins = qmltest::Rpc(m_app, "listunspent", {}, wallet->name());
        if (coins.size() != 3) return std::nullopt;
        UniValue inputs{UniValue::VARR};
        UniValue locked{UniValue::VARR};
        for (size_t i{0}; i < coins.size(); ++i) {
            UniValue input{UniValue::VOBJ};
            input.pushKV("txid", coins[i].find_value("txid"));
            input.pushKV("vout", coins[i].find_value("vout"));
            (i == 0 ? locked : inputs).push_back(input);
        }
        qmltest::Rpc(m_app, "lockunspent", qmltest::Json("[false," + locked.write() + "]"), wallet->name());
        const auto reference = qmltest::Rpc(m_app, "sendall", qmltest::Json(
            "{\"recipients\":[\"" + address.toStdString() + "\"],\"fee_rate\":1,\"options\":{\"inputs\":" + inputs.write() + ",\"add_to_wallet\":false}}"), wallet->name());
        const auto transaction = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + reference.find_value("hex").get_str() + "\"]"));
        const auto expected_amount = AmountFromValue(transaction.find_value("vout")[0].find_value("value"));

        if (!qmltest::Click(m_app, "sendTabButton") || !qmltest::Click(m_app, "feeSelectionPickerButton") ||
            !qmltest::Click(m_app, "feeSelectionOption3") || !qmltest::SetText(m_app, "feeSelectionCustomRateInput", "1")) return std::nullopt;
        if (manual) {
            if (!qmltest::Click(m_app, "sendCoinControlPickerOption_1")) return std::nullopt;
            auto* model = wallet->coinsListModel();
            model->update();
            if (!QTest::qWaitFor([&] { return model->rowCount() >= 2; }, 30'000)) return std::nullopt;
            model->beginSelection();
            for (const auto& input : inputs.getValues()) {
                const QString id = QString::fromStdString(input.find_value("txid").get_str()) + ":" + QString::number(input.find_value("vout").getInt<int>());
                if (!model->toggleCoinSelectionById(id)) return std::nullopt;
            }
            model->applySelection();
        }
        return Sweep{wallet, destination, address, locked, expected_amount};
    }

public:
    explicit SendIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void customFeesRespectRelayMinimum_data()
    {
        QTest::addColumn<QString>("rate");
        QTest::addColumn<bool>("reject_low_rate");
        QTest::newRow("override-wallet-minimum") << "0.1" << false;
        QTest::newRow("respect-relay-minimum") << "1" << true;
    }

    void customFeesRespectRelayMinimum()
    {
        QFETCH(QString, rate);
        QFETCH(bool, reject_low_rate);
        auto* wallet = qmltest::CreateWallet(m_app, "custom_fee");
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto destination = qmltest::NewAddress(m_app, wallet->name());

        // Choose a custom fee for a payment from the funded wallet.
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", destination));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.01"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionOption3"));

        // A rate below the relay minimum must fail before review or broadcast.
        if (reject_low_rate) {
            QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "0.1"));
            QTRY_VERIFY(!wallet->customFeeRateValid());
            QTRY_VERIFY(!wallet->feeEstimatePending());
            QCOMPARE(wallet->estimatedFee(), QString{});
            QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
            QCOMPARE(qmltest::Find(m_app, "feeSelectionEstimateLabel")->property("value").toString(), QString::fromUtf8("—"));
            QCOMPARE(qmltest::Find(m_app, "feeSelectionCustomRateRow")->property("errorText").toString(), QStringLiteral("Enter a fee rate of at least 1 sat/vB."));
            QVERIFY(!wallet->prepareTransaction());
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        }

        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", rate));
        QTRY_VERIFY(wallet->customFeeRateValid());
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QVERIFY(wallet->currentTransaction());
        const auto reviewed_fee = wallet->currentTransaction()->getTransactionFee();

        // Derive the expected fee independently using Core's size estimate.
        auto* core_wallet = wallet->wallet()->wallet();
        QVERIFY(core_wallet);
        int64_t maximum_signed_vsize;
        {
            LOCK(core_wallet->cs_wallet);
            maximum_signed_vsize = wallet::CalculateMaximumSignedTxSize(*wallet->currentTransaction()->getWtx(), core_wallet).vsize;
        }
        QVERIFY(maximum_signed_vsize > 0);

        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        auto* send_complete_page = qmltest::WaitForVisible(m_app, "sendCompletePage");
        QVERIFY(send_complete_page);
        const auto txid = send_complete_page->property("txid").toString();
        const auto mempool = qmltest::Rpc(m_app, "getrawmempool");
        QCOMPARE(mempool.size(), 1U);
        QCOMPARE(QString::fromStdString(mempool[0].get_str()), txid);
        UniValue params{UniValue::VARR};
        params.push_back(txid.toStdString());
        const auto mempool_entry = qmltest::Rpc(m_app, "getmempoolentry", params);
        const auto signed_vsize = mempool_entry.find_value("vsize").getInt<int64_t>();
        QVERIFY(signed_vsize <= maximum_signed_vsize);

        // Core prices the conservative signed size; actual ECDSA signatures can be shorter.
        const auto fee_rate_sat_per_vbyte = rate.toDouble();
        const auto expected_fee = static_cast<CAmount>(std::ceil(fee_rate_sat_per_vbyte * maximum_signed_vsize));
        const auto broadcast_fee = AmountFromValue(mempool_entry.find_value("fees").find_value("base"));
        QCOMPARE(reviewed_fee, expected_fee);
        QCOMPARE(broadcast_fee, expected_fee);

        // Match Python's assert_fee_amount: at most two vbytes of estimation slack.
        constexpr int SIGNATURE_SIZE_SLACK_VBYTES{2};
        const auto minimum_fee = static_cast<CAmount>(std::ceil(fee_rate_sat_per_vbyte * signed_vsize));
        const auto maximum_fee = static_cast<CAmount>(std::ceil(fee_rate_sat_per_vbyte * (signed_vsize + SIGNATURE_SIZE_SLACK_VBYTES)));
        QVERIFY(reviewed_fee >= minimum_fee);
        QVERIFY(reviewed_fee <= maximum_fee);
    }

    void coinSelectionCancelApplyAndLockReachTheWallet()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        auto* coins = wallet->coinsListModel();
        coins->update();
        QTRY_VERIFY(coins->rowCount() > 0);
        const auto coin_id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();

        // Filtering keeps the pending selection; cancelling discards it.
        coins->beginSelection();
        QVERIFY(coins->toggleCoinSelectionById(coin_id));
        QCOMPARE(coins->selectedCoinsCount(), 1);
        coins->setSearchText("no matching note");
        QCOMPARE(coins->rowCount(), 0);
        QCOMPARE(coins->selectedCoinsCount(), 1);
        coins->cancelSelection();
        QCOMPARE(coins->selectedCoinsCount(), 0);
        QVERIFY(wallet->listSelectedCoins().empty());

        // Applying commits the selection to the wallet.
        coins->setSearchText("");
        coins->beginSelection();
        QVERIFY(coins->toggleCoinSelectionById(coin_id));
        coins->applySelection();
        QCOMPARE(wallet->listSelectedCoins().size(), 1U);

        // Lock changes must reach Core, independently of the selection state.
        QVERIFY(coins->setCoinsLocked({coin_id}, true));
        QCOMPARE(qmltest::Rpc(m_app, "listlockunspent", {}, wallet->name()).size(), 1U);
        QVERIFY(coins->setCoinsLocked({coin_id}, false));
        QCOMPARE(qmltest::Rpc(m_app, "listlockunspent", {}, wallet->name()).size(), 0U);
    }

    void maximumAndSweepReviewRespectOwnershipAndSelection_data()
    {
        QTest::addColumn<bool>("manual");
        QTest::addColumn<bool>("self_send");
        QTest::newRow("automatic-external") << false << false;
        QTest::newRow("manual-external") << true << false;
        QTest::newRow("automatic-self-send") << false << true;
        QTest::newRow("manual-self-send") << true << true;
    }

    void coinSelectionDialogCommitsOrDiscardsInputsAndRepricesTheMaximum()
    {
        const auto setup = prepareSweep(true);
        QVERIFY(setup);
        auto* wallet = setup->wallet;
        auto* coins = wallet->coinsListModel();
        wallet->clearSelectedCoins();
        coins->update();
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));

        // Apply one real input through the dialog's production controls.
        QVERIFY(qmltest::Click(m_app, "sendSelectInputsButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "coinSelectionPopup")->property("opened").toBool());
        int selectable{-1};
        for (int row{0}; row < coins->rowCount(); ++row) {
            if (!coins->data(coins->index(row), CoinsListModel::LockedRole).toBool()) {
                selectable = row;
                break;
            }
        }
        QVERIFY(selectable >= 0);
        QVERIFY(qmltest::Click(m_app, qPrintable(QString{"coinSelectionItem_%1"}.arg(selectable))));
        QTRY_COMPARE(coins->selectedCoinsCount(), 1);
        QVERIFY(qmltest::Click(m_app, "coinSelectionDoneButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "coinSelectionPopup")->property("visible").toBool());
        QCOMPARE(wallet->listSelectedCoins().size(), 1U);
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QTRY_VERIFY(wallet->estimatedFeeSatoshi() > 0);
        QTRY_COMPARE(wallet->sendRecipientList()->totalAmountSatoshi() + wallet->estimatedFeeSatoshi(), 50 * COIN);
        const auto original_amount = wallet->sendRecipientList()->totalAmountSatoshi();
        const auto original_fee = wallet->estimatedFeeSatoshi();

        // Cancelling a pending deselection retains both the committed input and maximum.
        QVERIFY(qmltest::Click(m_app, "sendSelectInputsButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "coinSelectionPopup")->property("opened").toBool());
        QVERIFY(qmltest::Click(m_app, qPrintable(QString{"coinSelectionItem_%1"}.arg(selectable))));
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        QVERIFY(qmltest::Click(m_app, "coinSelectionCancelButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "coinSelectionPopup")->property("visible").toBool());
        QCOMPARE(wallet->listSelectedCoins().size(), 1U);
        QTRY_COMPARE(wallet->sendRecipientList()->totalAmountSatoshi(), original_amount);

        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "2"));
        QTRY_COMPARE(wallet->estimatedFeeSatoshi(), 2 * original_fee);
        QTRY_COMPARE(wallet->sendRecipientList()->totalAmountSatoshi() + wallet->estimatedFeeSatoshi(), 50 * COIN);
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "1"));
        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "3"));
        QTRY_VERIFY(!wallet->feeEstimatePending());
        QCOMPARE(wallet->sendRecipientList()->totalAmountSatoshi(), COIN);
    }

    void maximumAndSweepReviewRespectOwnershipAndSelection()
    {
        QFETCH(bool, manual);
        QFETCH(bool, self_send);
        const auto setup = prepareSweep(manual, self_send);
        QVERIFY(setup);
        auto* wallet = setup->wallet;
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QTRY_COMPARE(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), 100 * COIN);
        QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        QTRY_COMPARE_WITH_TIMEOUT(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), setup->expected_amount, 30'000);
        QCOMPARE(qmltest::Find(m_app, "sendCoinControlPicker")->property("currentIndex").toInt(), manual ? 1 : 0);
        QCOMPARE(qmltest::Find(m_app, "sendUseMaximumButton")->property("text").toString(),
                 manual ? QStringLiteral("Use maximum from selected coins") : QStringLiteral("Use maximum"));
        QTRY_COMPARE(wallet->sendDraftSweepsWallet(), !self_send);
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        if (!self_send) {
            QVERIFY(qmltest::WaitForVisible(m_app, "sendReviewSweepAlert"));
            QVERIFY(qmltest::Click(m_app, "sendReviewSweepCancelButton"));
            QTRY_VERIFY(!qmltest::Find(m_app, "sendReviewSweepAlert")->property("visible").toBool());
            QCOMPARE(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), setup->expected_amount);
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
            QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
            QVERIFY(qmltest::Click(m_app, "sendReviewSweepConfirmButton"));
        }
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QTRY_VERIFY(!qmltest::Find(m_app, "sendReviewSweepAlert")->property("visible").toBool());
        QCOMPARE(wallet->currentTransactionSweepsWallet(), !self_send);
        QCOMPARE(wallet->currentTransaction()->getWtx()->vin.size(), 2U);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        const auto labels = qmltest::Rpc(m_app, "listlabels", {}, wallet->name());
        for (const auto& label : labels.getValues()) {
            QVERIFY(label.get_str() != "qml-fee-preview");
        }
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewCloseButton"));
    }

    void fixedManualSweepWarnsBeforeUnlockAndBeforeBroadcast()
    {
        const auto setup = prepareSweep(true);
        QVERIFY(setup);
        auto* wallet = setup->wallet;
        QVERIFY(wallet->encryptWallet("review password"));
        QTRY_VERIFY(wallet->isLocked());
        QCOMPARE(wallet->listSelectedCoins().size(), 2U);
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        const auto amount = BitcoinUnits::format(BitcoinUnits::Unit::BTC, setup->expected_amount - 100, false, BitcoinUnits::SeparatorStyle::NEVER);
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", amount));
        QTRY_COMPARE(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), setup->expected_amount - 100);
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "sendReviewButton"), "forceActiveFocus"));
        QTRY_VERIFY_WITH_TIMEOUT(wallet->sendDraftSweepsWallet(), 30'000);
        QTRY_VERIFY(!wallet->feeEstimatePending());
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendReviewSweepAlert"));
        QVERIFY(!qmltest::Find(m_app, "reviewPassphrasePopup")->property("visible").toBool());
        QVERIFY(qmltest::Click(m_app, "sendReviewSweepCancelButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "sendReviewSweepAlert")->property("visible").toBool());
        QVERIFY(!qmltest::Find(m_app, "reviewPassphrasePopup")->property("visible").toBool());
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::Click(m_app, "sendReviewSweepConfirmButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "reviewPassphrasePopup"));
        // onOpened clears the passphrase; wait for it before entering text.
        QTRY_VERIFY(qmltest::Find(m_app, "reviewPassphrasePopup")->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "reviewPassphraseField", "review password"));
        QVERIFY(qmltest::Click(m_app, "reviewPassphraseConfirmButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QVERIFY(wallet->isLocked());
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendSweepAlert"));
        QVERIFY(qmltest::Click(m_app, "sendSweepCancelButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "sendSweepAlert")->property("visible").toBool());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        QVERIFY(qmltest::Click(m_app, "sendSweepConfirmButton"));
        auto* complete = qmltest::WaitForVisible(m_app, "sendCompletePage");
        QVERIFY(complete);
        const auto txid = complete->property("txid").toString();
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 1U);
        QCOMPARE(AmountFromValue(qmltest::Rpc(m_app, "getbalance", {}, wallet->name())), 50 * COIN);
        QCOMPARE(qmltest::Rpc(m_app, "listlockunspent", {}, wallet->name()).write(), setup->locked_input.write());
        QVERIFY(qmltest::Click(m_app, "sendResultDoneButton"));
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", txid));
        auto* filter = qmltest::Find(m_app, "activityFilterProxyModel");
        QVERIFY(filter);
        QTRY_COMPARE(filter->property("count").toInt(), 1);
        auto* row = qmltest::WaitForVisible(m_app, qPrintable("activityItem_" + txid));
        QVERIFY(row);
        QCOMPARE(row->property("netAmountSat").toLongLong(), -100 * COIN);
        QVERIFY(!row->property("incoming").toBool());
    }

    void preparedReviewAndExportRemainStableWhenMoreFundsArrive()
    {
        const auto setup = prepareSweep(false, false, true);
        QVERIFY(setup);
        auto* wallet = setup->wallet;
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::Click(m_app, "sendReviewSweepConfirmButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        auto* amount = qmltest::Find(m_app, "sendTransactionReviewRecipient0Amount");
        auto* destination = qmltest::Find(m_app, "sendTransactionReviewRecipient0Description");
        QVERIFY(amount && destination);
        const auto reviewed_amount = amount->property("text").toString();
        const auto reviewed_destination = destination->property("text").toString();
        const auto incoming = qmltest::NewAddress(m_app, wallet->name());
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + incoming.toStdString() + "\",\"amount\":0.5,\"fee_rate\":1}"), setup->destination_wallet->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + setup->address.toStdString() + "\"]"));
        QTRY_VERIFY_WITH_TIMEOUT(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi() > setup->expected_amount, 30'000);
        QCOMPARE(amount->property("text").toString(), reviewed_amount);
        QCOMPARE(destination->property("text").toString(), reviewed_destination);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("review-snapshot.psbt");
        auto* save_path = qmltest::Find(m_app, "sendTransactionReviewSavePsbtPathField");
        QVERIFY(save_path && save_path->setProperty("text", path));
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSaveButton"));
        QTRY_VERIFY(QFileInfo{path}.size() > 0);
        QFile psbt{path};
        QVERIFY(psbt.open(QIODevice::ReadOnly));
        const auto decoded = qmltest::Rpc(m_app, "decodepsbt", qmltest::Json("[\"" + psbt.readAll().toBase64().toStdString() + "\"]"));
        CAmount exported_amount{0};
        const bool has_transaction = !decoded.find_value("tx").isNull();
        const auto& outputs = has_transaction ? decoded.find_value("tx").find_value("vout") : decoded.find_value("outputs");
        for (const auto& output : outputs.getValues()) {
            const auto& address = output.find_value(has_transaction ? "scriptPubKey" : "script").find_value("address");
            if (address.isStr() && address.get_str() == setup->address.toStdString()) {
                exported_amount += AmountFromValue(output.find_value(has_transaction ? "value" : "amount"));
            }
        }
        QCOMPARE(exported_amount, setup->expected_amount);
    }

    void multipleRecipientsShareTheMaximumAndKeepTheirDraftOnCancel()
    {
        const auto setup = prepareSweep(false);
        QVERIFY(setup);
        auto* wallet = setup->wallet;
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QVERIFY(qmltest::Click(m_app, "sendAddRecipientButton"));
        const auto second_address = qmltest::NewAddress(m_app, setup->destination_wallet->name());
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", second_address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "2"));
        QTRY_COMPARE(wallet->sendRecipientList()->count(), 2);
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "sendReviewButton"), "forceActiveFocus"));
        QTRY_VERIFY(!wallet->feeEstimatePending());
        QTRY_COMPARE(wallet->sendRecipientList()->totalAmountSatoshi() + wallet->estimatedFeeSatoshi(), 100 * COIN);
        QTRY_VERIFY_WITH_TIMEOUT(wallet->sendDraftSweepsWallet(), 30'000);
        const auto first_amount = wallet->sendRecipientList()->recipients()[0]->amount()->satoshi();
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendReviewSweepAlert"));
        QCOMPARE(qmltest::Find(m_app, "sendReviewSweepAlert")->property("title").toString(), QStringLiteral("Use all available funds?"));
        QVERIFY(qmltest::Click(m_app, "sendReviewSweepCancelButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "sendReviewSweepAlert")->property("visible").toBool());
        QCOMPARE(wallet->sendRecipientList()->recipients()[0]->amount()->satoshi(), first_amount);
        QCOMPARE(wallet->sendRecipientList()->recipients()[1]->amount()->satoshi(), 2 * COIN);
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::Click(m_app, "sendReviewSweepConfirmButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QCOMPARE(wallet->currentTransaction()->getWtx()->vout.size(), 2U);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void lockedHardenedDescriptorWalletRebuildsReviewAfterUnlock()
    {
        auto* destination = qmltest::CreateWallet(m_app, "destination");
        QVERIFY(destination);
        const auto address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::CreateLockedHardenedWallet(m_app, "fallback", "fallback password");
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto info = qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name());
        const auto change_keys = info.find_value("keypoolsize_hd_internal").getInt<int>();
        for (int i{0}; i < change_keys; ++i) qmltest::Rpc(m_app, "getrawchangeaddress", {}, wallet->name());
        try {
            qmltest::Rpc(m_app, "getrawchangeaddress", {}, wallet->name());
            QFAIL("The locked hardened change keypool must be exhausted");
        } catch (const std::runtime_error& error) {
            QVERIFY(QString::fromUtf8(error.what()).contains("Keypool ran out"));
        }

        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionOption3"));
        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "1"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "1"));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "sendReviewButton"), "forceActiveFocus"));
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "reviewPassphrasePopup"));
        QTRY_VERIFY(qmltest::Find(m_app, "reviewPassphrasePopup")->property("opened").toBool());
        QVERIFY(qmltest::SetText(m_app, "reviewPassphraseField", "fallback password"));
        QVERIFY(qmltest::Click(m_app, "reviewPassphraseConfirmButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(wallet->isLocked());
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendCompletePage"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 1U);
        QVERIFY(wallet->isLocked());
    }
};

QStringList SendTestArguments(const QString& selector)
{
    QStringList arguments{"-mintxfee=0.00001000", "-minrelaytxfee=0.00000100"};
    if (selector.endsWith(":respect-relay-minimum")) arguments = {"-mintxfee=0.00000100", "-minrelaytxfee=0.00001000"};
    // One cached change key is enough to exercise exhaustion and recovery.
    if (selector == "lockedHardenedDescriptorWalletRebuildsReviewAfterUnlock") arguments.append("-keypool=1");
    return arguments;
}

std::unique_ptr<QObject> CreateSendTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<SendIntegrationTests>(app);
}

#include <test_send.moc>
