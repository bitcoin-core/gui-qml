// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/preparationtest.h>
#include <test/mocks/sendtest.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <rpc/util.h>
#include <key_io.h>
#include <script/solver.h>
#include <primitives/transaction.h>
#include <wallet/wallet.h> // Public CRecipient value required by interfaces::Wallet.

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTimer>

#include <QLocale>
#include <QScopeGuard>
#include <QClipboard>
#include <QGuiApplication>
#include <QInputMethodEvent>
#include <core_io.h>
#include <util/string.h>
#include <qml/models/options_model.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <future>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

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
        auto* wallet = fund_destination ? qmltest::CreateWallet(m_app, "sender") : qmltest::LoadFundedWallet(m_app, "sender");
        if (!wallet || (fund_destination && !qmltest::FundWallet(m_app, *wallet))) return std::nullopt;
        const auto mining_address = qmltest::NewAddress(m_app, wallet->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[2,\"" + mining_address.toStdString() + "\"]"));
        // The wallet model observes the new blocks asynchronously. Recipient
        // validation depends on the balance, so a later observation would
        // make a review prepared before it stale. getbalances waits for the
        // wallet to reach the current chain.
        const auto trusted = AmountFromValue(qmltest::Rpc(m_app, "getbalances", {}, wallet->name()).find_value("mine").find_value("trusted"));
        if (!QTest::qWaitFor([&] { return wallet->balanceKnown() && wallet->displayedBalanceSatoshi() == trusted; }, 30'000)) return std::nullopt;
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
            // Mode selection and the post-lock Core catalog are separate owned
            // operations. Do not Apply against an earlier, still-loading catalog.
            if (!QTest::qWaitFor([&] { return !model->loading() && !wallet->coinSelectionPending() &&
                        model->manualInputSelection() && model->rowCount() == 3 && model->lockedCoinsCount() == 1; }, 30'000)) return std::nullopt;
            model->beginSelection();
            for (const auto& input : inputs.getValues()) {
                const QString id = QString::fromStdString(input.find_value("txid").get_str()) + ":" + QString::number(input.find_value("vout").getInt<int>());
                if (!model->toggleCoinSelectionById(id)) return std::nullopt;
            }
            model->applySelection();
            if (!QTest::qWaitFor([&] { return !wallet->coinSelectionPending() &&
                        wallet->selectedCoins()->size() == inputs.size() && model->totalSelectedSatoshi() == 100 * COIN; }, 30'000)) return std::nullopt;
            for (const auto& selected : *wallet->selectedCoins()) {
                bool matches{false};
                for (const auto& input : inputs.getValues()) {
                    if (selected.hash.GetHex() == input.find_value("txid").get_str() &&
                        selected.n == input.find_value("vout").getInt<unsigned>()) matches = true;
                }
                if (!matches) return std::nullopt;
            }
        }
        return Sweep{wallet, destination, address, locked, expected_amount};
    }

public:
    explicit SendIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void feePreviewLimitationKeepsCoreReviewAuthoritative_data()
    {
        QTest::addColumn<QString>("address_type");
        QTest::addColumn<qint64>("expected_preview"); QTest::addColumn<qint64>("expected_review");
        QTest::addColumn<QString>("expected_display");
        QTest::newRow("bech32-control") << QStringLiteral("bech32") << qint64{1410} << qint64{1410} << QStringLiteral("0.00001410 BTC");
        QTest::newRow("taproot-preview-deferral") << QStringLiteral("bech32m") << qint64{1530} << qint64{1650} << QStringLiteral("0.00001650 BTC");
    }
    void feePreviewLimitationKeepsCoreReviewAuthoritative()
    {
        QFETCH(QString, address_type); QFETCH(qint64, expected_preview); QFETCH(qint64, expected_review); QFETCH(QString, expected_display);
        auto* destination = qmltest::CreateWallet(m_app, "destination"); QVERIFY(destination);
        const auto address = QString::fromStdString(qmltest::Rpc(m_app, "getnewaddress",
            qmltest::Json("[\"\",\"" + address_type.toStdString() + "\"]"), destination->name()).get_str());
        auto* wallet = qmltest::LoadFundedWallet(m_app, "sender"); QVERIFY(wallet);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.02"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionOption3"));
        QTRY_VERIFY(!wallet->fees()->policyPending());
        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "10"));
        QTRY_VERIFY(!wallet->sendRecipientList()->validationPending() && wallet->sendRecipientList()->allValid());
        QTRY_VERIFY(!wallet->fees()->policyPending() && !wallet->feeEstimatePending() && wallet->customFeeRateValid() && wallet->estimatedFeeSatoshi() > 0);
        const auto preview = wallet->estimatedFeeSatoshi(); QCOMPARE(preview, expected_preview);
        QVERIFY(qmltest::Click(m_app, "sendReviewButton")); QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QCOMPARE(wallet->preparationState(), WalletQmlModel::PreparationState::PreparationReady);
        auto* prepared = wallet->currentTransaction(); QVERIFY(prepared);
        const auto actual = prepared->feeAmount()->satoshi(); QCOMPARE(actual, expected_review);
        QCOMPARE(prepared->reviewFeeRate(), QStringLiteral("10.000"));
        const auto transaction = prepared->getWtx(); QVERIFY(transaction);
        QTRY_COMPARE(qmltest::Find(m_app, "sendTransactionReviewFee")->property("value").toString(), expected_display);
        // Independent public RPC prevouts/outputs authenticate the actual paid
        // fee; the preview is deliberately not its authority.
        const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + EncodeHexTx(*transaction) + "\"]"));
        QCOMPARE(decoded.find_value("vin").size(), size_t{1});
        CAmount inputs{0}, outputs{0};
        for (const auto& input : decoded.find_value("vin").getValues()) {
            const auto coin = qmltest::Rpc(m_app, "gettxout", qmltest::Json("[\"" + input.find_value("txid").get_str() + "\"," + input.find_value("vout").write() + "]"));
            QVERIFY(!coin.isNull()); inputs += AmountFromValue(coin.find_value("value"));
        }
        for (const auto& output : decoded.find_value("vout").getValues()) outputs += AmountFromValue(output.find_value("value"));
        QCOMPARE(actual, inputs - outputs);
        qInfo() << "048 accepted preview limitation; authoritative displayed review" << address_type << preview << actual << expected_display;
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    void uppercaseBech32RecipientRemainsRecipientOutput()
    {
        auto* wallet = qmltest::LoadFundedWallet(m_app, "sender"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        const auto uppercase_address = address.toUpper();
        QVERIFY(address != uppercase_address);

        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", uppercase_address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.01"));
        QTRY_VERIFY(!wallet->sendRecipientList()->validationPending() && wallet->sendRecipientList()->allValid());
        auto* review_button = qmltest::Find(m_app, "sendReviewButton"); QVERIFY(review_button);
        QVERIFY(QMetaObject::invokeMethod(review_button, "forceActiveFocus"));
        QTRY_VERIFY(!qmltest::Find(m_app, "sendAddressInput")->property("activeFocus").toBool());
        QTRY_VERIFY(!wallet->sendRecipientList()->validationPending() && wallet->sendRecipientList()->allValid());
        QTRY_VERIFY(!wallet->feeEstimatePending() && !wallet->fees()->policyPending());
        QTRY_VERIFY_WITH_TIMEOUT(review_button->property("enabled").toBool(), 30'000);
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QCOMPARE(wallet->preparationState(), WalletQmlModel::PreparationState::PreparationReady);
        auto* review = wallet->currentTransaction(); QVERIFY(review && review->getWtx());

        const auto outputs = wallet->currentTransactionFlow().value("outputs").toList();
        bool found_recipient{false};
        for (const auto& value : outputs) {
            const auto output = value.toMap();
            if (output.value("address").toString() != address) continue;
            found_recipient = true;
            QVERIFY(!output.value("isChange").toBool());
        }
        QVERIFY(found_recipient);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    void explicitPublicChangePolicyMatchesThePreview_data()
    {
        QTest::addColumn<QString>("address_type"); QTest::addColumn<qint64>("expected_fee");
        QTest::newRow("bech32-control") << QStringLiteral("bech32") << qint64{1410};
        QTest::newRow("taproot-recipient") << QStringLiteral("bech32m") << qint64{1530};
        QTest::newRow("overrides-configured-legacy") << QStringLiteral("bech32") << qint64{1410};
    }
    void explicitPublicChangePolicyMatchesThePreview()
    {
        QFETCH(QString, address_type); QFETCH(qint64, expected_fee);
        auto* destination = qmltest::CreateWallet(m_app, "destination"); QVERIFY(destination);
        const auto address = qmltest::Rpc(m_app, "getnewaddress", qmltest::Json("[\"\",\"" + address_type.toStdString() + "\"]"), destination->name()).get_str();
        auto* wallet = qmltest::LoadFundedWallet(m_app, "sender"); QVERIFY(wallet);
        struct Facts { CAmount preview{0}, actual{0}; bool bech32_change{false}; OutputType chosen{OutputType::UNKNOWN}; QString error; };
        auto promise = std::make_shared<std::promise<Facts>>(); auto future = promise->get_future();
        const auto submitted = wallet->opening()->read([address](interfaces::Wallet& native) {
            Facts facts; facts.chosen = native.getDefaultAddressType();
            if (facts.chosen != OutputType::BECH32) { facts.error = "The fixture requires a bech32 receiving default."; return facts; }
            wallet::CCoinControl control; control.m_change_type = facts.chosen; control.m_feerate = CFeeRate{10000}; control.fOverrideFeeRate = true;
            const std::vector<wallet::CRecipient> recipients{{DecodeDestination(address), 2'000'000, false}};
            control.destChange = WitnessV0KeyHash{uint160{}};
            const auto preview = native.createTransaction(recipients, control, false, std::nullopt);
            if (!preview) { facts.error = QString::fromStdString(util::ErrorString(preview).original); return facts; }
            control.destChange = CNoDestination{};
            // Disposable test-only preparation may retain a real destination.
            // Production has not adopted this explicit policy choice.
            const auto actual = native.createTransaction(recipients, control, true, std::nullopt);
            if (!actual) { facts.error = QString::fromStdString(util::ErrorString(actual).original); return facts; }
            facts.preview = preview->fee; facts.actual = actual->fee;
            if (actual->change_pos && actual->tx && *actual->change_pos < actual->tx->vout.size()) {
                int version; std::vector<unsigned char> program;
                facts.bech32_change = actual->tx->vout[*actual->change_pos].scriptPubKey.IsWitnessProgram(version, program) && version == 0 && program.size() == 20;
            }
            return facts;
        }, [promise](Facts facts) { promise->set_value(std::move(facts)); }, [promise](std::exception_ptr error) { promise->set_exception(error); });
        QVERIFY(submitted); QTRY_VERIFY(future.wait_for(std::chrono::seconds{0}) == std::future_status::ready);
        const auto result = future.get(); QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
        QCOMPARE(result.chosen, OutputType::BECH32); QVERIFY(result.bech32_change);
        QCOMPARE(result.preview, expected_fee); QCOMPARE(result.actual, expected_fee);
        qInfo() << "048 explicit public change policy" << address_type << result.preview << result.actual << result.bech32_change;
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    void presetFeesHonorRaisedCoreRequirements_data()
    {
        QTest::addColumn<int>("preset");
        for (int preset{0}; preset < 3; ++preset) {
            QTest::newRow(qPrintable(QStringLiteral("wallet-min-%1").arg(preset))) << preset;
            QTest::newRow(qPrintable(QStringLiteral("relay-min-%1").arg(preset))) << preset;
        }
    }
    void presetFeesHonorRaisedCoreRequirements()
    {
        QFETCH(int, preset); auto* wallet = qmltest::LoadFundedWallet(m_app, "sender"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        QVERIFY(qmltest::Click(m_app, "sendTabButton")); QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.01"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton"));
        QVERIFY(qmltest::Click(m_app, qPrintable(QStringLiteral("feeSelectionOption%1").arg(preset))));
        QTRY_VERIFY(!wallet->fees()->policyPending() && !wallet->feeEstimatePending() && wallet->estimatedFeeSatoshi() > 0);
        QCOMPARE(wallet->estimatedFeeRate(), QStringLiteral("2")); QVERIFY(!wallet->customFeeEnabled());
        const auto ticket = wallet->requestPrepareTransaction(); QVERIFY(ticket); QTRY_VERIFY(!wallet->preparationBusy());
        QCOMPARE(wallet->preparationState(), WalletQmlModel::PreparationState::PreparationReady);
        auto* review = wallet->currentTransaction(); QVERIFY(review); QCOMPARE(review->reviewFeeRate(), QStringLiteral("2.000"));
        const auto tx = review->getWtx(); QVERIFY(tx); const auto fee = review->feeAmount()->satoshi();
        const auto rpc = qmltest::Rpc(m_app, "testmempoolaccept", qmltest::Json("[[\"" + EncodeHexTx(*tx) + "\"]]"));
        QVERIFY2(rpc[0].find_value("allowed").get_bool(), rpc.write().c_str());
        QCOMPARE(AmountFromValue(rpc[0].find_value("fees").find_value("base")), fee);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    void reselectingCustomPreservesTheReviewedExplicitRate()
    {
        auto* wallet = qmltest::LoadFundedWallet(m_app, "sender"); QVERIFY(wallet);
        const auto address = qmltest::NewAddress(m_app, wallet->name());
        QVERIFY(qmltest::Click(m_app, "sendTabButton")); QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.01"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton")); QVERIFY(qmltest::Click(m_app, "feeSelectionOption3"));
        QTRY_VERIFY(!wallet->fees()->policyPending()); QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "17"));
        QTRY_VERIFY(!wallet->fees()->policyPending() && !wallet->feeEstimatePending() && wallet->customFeeRateValid());
        QVERIFY(qmltest::Click(m_app, "sendReviewButton")); QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        const auto first = wallet->currentTransaction()->feeAmount()->satoshi();
        QCOMPARE(wallet->currentTransaction()->reviewFeeRate(), QStringLiteral("17.000"));
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewCloseButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton")); QVERIFY(qmltest::Click(m_app, "feeSelectionOption3"));
        QCOMPARE(wallet->customFeeRate(), QStringLiteral("17"));
        QTRY_VERIFY(!wallet->fees()->policyPending() && !wallet->feeEstimatePending());
        QVERIFY(qmltest::Click(m_app, "sendReviewButton")); QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QCOMPARE(wallet->currentTransaction()->reviewFeeRate(), QStringLiteral("17.000"));
        QCOMPARE(wallet->currentTransaction()->feeAmount()->satoshi(), first);
        const auto reviewed = wallet->currentTransaction();
        wallet->fees()->choosePreset(2); QTRY_VERIFY(!wallet->fees()->policyPending());
        QCOMPARE(reviewed->reviewFeeRate(), QStringLiteral("17.000")); QCOMPARE(reviewed->feeAmount()->satoshi(), first);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    void oversizedMaximumPreservesCoreCauseAndRecoversWithFewerCoins()
    {
        QElapsedTimer setup_time; setup_time.start();
        const auto milestone = [&](const char* phase) { qInfo() << "Oversized setup" << phase << "elapsed ms" << setup_time.elapsed(); };
        auto* source = qmltest::LoadFundedWallet(m_app, "sender"); QVERIFY(source);
        milestone("source-wallet-ready");
        const auto mining = qmltest::NewAddress(m_app, source->name());
        auto* wallet = qmltest::CreateWallet(m_app, "many-coins"); QVERIFY(wallet);
        milestone("receiving-wallet-ready");
        QVERIFY(!qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("avoid_reuse").get_bool());
        // The input-weight oracle requires 1600 distinct outpoints, not 1600
        // scripts. One real receiving script keeps setup bounded and stresses
        // exact vout identity for many coins belonging to the same address.
        const auto destination = DecodeDestination(qmltest::NewAddress(m_app, wallet->name()).toStdString());
        QVERIFY(IsValidDestination(destination));
        const auto script = GetScriptForDestination(destination);
        milestone("receiving-script-ready");
        CMutableTransaction funding;
        for (size_t coin{0}; coin < 1600; ++coin) funding.vout.emplace_back(100'000, script);
        UniValue fund{UniValue::VOBJ}, options{UniValue::VOBJ};
        fund.pushKV("hexstring", EncodeHexTx(CTransaction{funding}));
        options.pushKV("fee_rate", 2); fund.pushKV("options", options);
        const auto funded = qmltest::Rpc(m_app, "fundrawtransaction", fund, source->name());
        milestone("transaction-funded");
        UniValue sign{UniValue::VOBJ}; sign.pushKV("hexstring", funded.find_value("hex"));
        const auto signed_funding = qmltest::Rpc(m_app, "signrawtransactionwithwallet", sign, source->name());
        QVERIFY(signed_funding.find_value("complete").get_bool());
        milestone("transaction-signed");
        UniValue send{UniValue::VARR}; send.push_back(signed_funding.find_value("hex"));
        // Keep the original GUI event-loop/RPC helper semantics while separating
        // physical Core completion from delivery delayed by a long GUI event.
        const auto rpc_started = std::chrono::steady_clock::now();
        const auto native_ms = std::make_shared<std::atomic<qint64>>(-1);
        qint64 last_heartbeat{setup_time.elapsed()}, maximum_gui_gap{0};
        int heartbeats{0};
        QTimer heartbeat;
        heartbeat.setInterval(60);
        connect(&heartbeat, &QTimer::timeout, &heartbeat, [&] {
            const auto now = setup_time.elapsed();
            maximum_gui_gap = std::max(maximum_gui_gap, now - last_heartbeat);
            last_heartbeat = now;
            ++heartbeats;
        });
        const auto stop_heartbeat = qScopeGuard([&] { heartbeat.stop(); });
        heartbeat.start();
        const auto measured_rpc = [native_ms](interfaces::Node* node, UniValue arguments) {
            const auto started = std::chrono::steady_clock::now();
            const auto record_completion = qScopeGuard([&] {
                const auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                native_ms->store(duration, std::memory_order_relaxed);
                const std::string line = "R42_FUNDING_NATIVE_COMPLETE elapsed_ms=" + util::ToString(duration) + "\n";
                std::fwrite(line.data(), 1, line.size(), stdout);
                std::fflush(stdout);
            });
            try {
                return node->executeRpc("sendrawtransaction", arguments, "");
            } catch (const UniValue& error) {
                throw std::runtime_error{"RPC sendrawtransaction failed: " + error.write()};
            }
        };
        const auto funding_id = qmltest::Call(m_app, measured_rpc, send).get_str();
        maximum_gui_gap = std::max(maximum_gui_gap, setup_time.elapsed() - last_heartbeat);
        heartbeat.stop();
        qInfo() << "Oversized funding delivery native-ms" << native_ms->load(std::memory_order_relaxed)
                << "gui-return-ms" << std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::steady_clock::now() - rpc_started).count()
                << "heartbeats" << heartbeats << "maximum-gui-gap-ms" << maximum_gui_gap;
        milestone("transaction-published");
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        milestone("funding-mined");
        const auto coins = qmltest::Rpc(m_app, "listunspent", {}, wallet->name()); QCOMPARE(coins.size(), size_t{1600});
        milestone("unspent-catalog-read");
        std::set<int> output_indices;
        for (size_t coin{0}; coin < coins.size(); ++coin) {
            QCOMPARE(coins[coin].find_value("txid").get_str(), funding_id);
            QVERIFY(output_indices.insert(coins[coin].find_value("vout").getInt<int>()).second);
        }
        QCOMPARE(output_indices.size(), size_t{1600});
        QTRY_COMPARE(wallet->balanceSatoshi(), CAmount{160'000'000});
        QVERIFY(qmltest::Click(m_app, "sendTabButton")); QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QTRY_COMPARE(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), CAmount{160'000'000});
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", mining));
        QTRY_VERIFY(!wallet->feeEstimatePending() && !wallet->fees()->errorText().isEmpty());
        QCOMPARE(wallet->fees()->errorText(), QStringLiteral("The inputs size exceeds the maximum weight. Please try sending a smaller amount or manually consolidating your wallet's UTXOs"));
        QCOMPARE(qmltest::Find(m_app, "sendPrepareTransactionErrorText")->property("text").toString(), wallet->fees()->errorText());
        QCOMPARE(qmltest::Find(m_app, "sendAmountStatusText")->property("text").toString(), QStringLiteral("Previous amount; maximum could not be calculated."));
        QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QString rpc_error;
        try { qmltest::Rpc(m_app, "sendall", qmltest::Json("{\"recipients\":[\"" + mining.toStdString() + "\"],\"fee_rate\":2,\"options\":{\"add_to_wallet\":false}}"), wallet->name()); }
        catch (const std::runtime_error& error) { rpc_error = QString::fromUtf8(error.what()); }
        QVERIFY2(rpc_error.contains("Transaction too large"), qPrintable(rpc_error));
        // #1077: entering the address before Use maximum must surface the same
        // Core cause, not an empty amount and a generic recipient prompt.
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "")); QVERIFY(qmltest::SetText(m_app, "sendAddressInput", ""));
        QTRY_VERIFY(wallet->maximumRecipient() == nullptr && !wallet->feeEstimatePending() && wallet->fees()->errorText().isEmpty());
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", mining)); QTRY_VERIFY(!recipient->validationPending() && recipient->address()->address() == mining);
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(wallet->maximumRecipient(), recipient);
        QTRY_VERIFY(!wallet->feeEstimatePending() && !wallet->fees()->errorText().isEmpty());
        QCOMPARE(wallet->fees()->errorText(), QStringLiteral("The inputs size exceeds the maximum weight. Please try sending a smaller amount or manually consolidating your wallet's UTXOs"));
        QCOMPARE(qmltest::Find(m_app, "sendPrepareTransactionErrorText")->property("text").toString(), wallet->fees()->errorText());
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Maximum);
        QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        // A legal user-side consolidation/selection change is a new request;
        // retaining the first failure must not prevent a viable 100-coin sweep.
        UniValue locked{UniValue::VARR};
        for (size_t i{100}; i < coins.size(); ++i) { UniValue coin{UniValue::VOBJ}; coin.pushKV("txid", coins[i].find_value("txid")); coin.pushKV("vout", coins[i].find_value("vout")); locked.push_back(coin); }
        QVERIFY(qmltest::Rpc(m_app, "lockunspent", qmltest::Json("[false," + locked.write() + "]"), wallet->name()).get_bool());
        wallet->fees()->refresh();
        QTRY_VERIFY(!wallet->feeEstimatePending() && wallet->fees()->estimateFresh() && wallet->estimatedFeeSatoshi() > 0);
        QCOMPARE(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi() + wallet->estimatedFeeSatoshi(), CAmount{10'000'000});
        QVERIFY(wallet->fees()->errorText().isEmpty()); QTRY_VERIFY(qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    void sendMonetaryBoundaryPreservesEveryEnteredDigit_data()
    {
        QTest::addColumn<int>("unit"); QTest::addColumn<QString>("text"); QTest::addColumn<int>("producer");
        const QStringList texts{"21000000.00000000", "21000000000.00000", "21000000000000.00", "2100000000000000"};
        for (int unit{0}; unit < 4; ++unit) for (int producer{0}; producer < 3; ++producer)
            QTest::newRow(qPrintable(QString::number(unit) + "-" + QString::number(producer))) << unit << texts[unit] << producer;
    }
    void sendMonetaryBoundaryPreservesEveryEnteredDigit()
    {
        QFETCH(int, unit); QFETCH(QString, text); QFETCH(int, producer);
        auto* wallet = qmltest::CreateWallet(m_app, "boundary"); QVERIFY(wallet);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options); options->setDisplayUnit(unit);
        auto* input = qmltest::WaitForVisible(m_app, "sendAmountInput"); QVERIFY(input);
        auto* amount = wallet->sendRecipientList()->currentRecipient()->amount();
        QTRY_COMPARE(int(amount->unit()), unit);
        if (producer == 0) QVERIFY(qmltest::SetText(m_app, "sendAmountInput", text));
        else {
            QVERIFY(qmltest::Click(m_app, "sendAmountInput"));
            QTest::keyClick(m_app.window(), Qt::Key_A, Qt::ControlModifier, 1);
            QTest::keyClick(m_app.window(), Qt::Key_Backspace, Qt::NoModifier, 1);
            if (producer == 1) { QGuiApplication::clipboard()->setText(text); QTest::keyClick(m_app.window(), Qt::Key_V, Qt::ControlModifier, 1); }
            else for (const QChar key : text) QTest::keyClick(m_app.window(), static_cast<Qt::Key>(key.unicode()), Qt::NoModifier, 1);
        }
        QTRY_COMPARE(amount->inputState(), BitcoinAmount::InputState::Valid);
        QCOMPARE(amount->satoshi(), MAX_MONEY);
        QVERIFY(qmltest::Click(m_app, "sendAddressInput")); QCOMPARE(amount->satoshi(), MAX_MONEY);
        QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    void staleMaximumCannotReplaceAnOrdinaryAmountEdit()
    {
        const auto setup = prepareSweep(false); QVERIFY(setup); auto* wallet = setup->wallet;
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        QVERIFY(recipient->address()->address().isEmpty());
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QTRY_COMPARE(wallet->maximumRecipient(), recipient);
        QTRY_VERIFY(recipient->amount()->satoshi() > 0);
        const auto stale_maximum = recipient->amount()->satoshi();
        const auto coins = qmltest::Rpc(m_app, "listunspent", {}, wallet->name()); QVERIFY(coins.size() > 0);
        UniValue locked{UniValue::VARR}; UniValue input{UniValue::VOBJ};
        input.pushKV("txid", coins[0].find_value("txid")); input.pushKV("vout", coins[0].find_value("vout")); locked.push_back(input);
        QVERIFY(qmltest::Rpc(m_app, "lockunspent", qmltest::Json("[false," + locked.write() + "]"), wallet->name()).get_bool());
        // No GUI delivery has occurred since the independent RPC lock.
        QCOMPARE(recipient->amount()->satoshi(), stale_maximum);
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.01234567"));
        QTRY_COMPARE(wallet->maximumRecipient(), nullptr);
        QTRY_COMPARE(recipient->amount()->satoshi(), CAmount{1234567});
        wallet->scheduleFeeEstimates(); QTRY_VERIFY(!wallet->feeEstimatePending());
        QCOMPARE(recipient->amount()->satoshi(), CAmount{1234567});
        QVERIFY(!wallet->sendDraftSweepsWallet()); QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    void leavingAnUntouchedAmountFieldKeepsMaximum_data()
    {
        QTest::addColumn<bool>("tab_away"); QTest::newRow("click-away") << false; QTest::newRow("tab-away") << true;
    }
    void leavingAnUntouchedAmountFieldKeepsMaximum()
    {
        // Focusing and leaving the amount field without typing is not an
        // amount edit: Use maximum must stay active with its computed amount.
        QFETCH(bool, tab_away); const auto setup = prepareSweep(false); QVERIFY(setup); auto* wallet = setup->wallet;
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QTRY_COMPARE(wallet->maximumRecipient(), recipient);
        QTRY_VERIFY(recipient->amount()->satoshi() > 0);
        QTRY_VERIFY(!wallet->feeEstimatePending());
        const auto maximum = recipient->amount()->satoshi();
        const auto intent = recipient->amountIntentRevision();
        auto* input = qmltest::WaitForVisible(m_app, "sendAmountInput"); QVERIFY(input);
        QTRY_VERIFY(qmltest::Click(m_app, "sendAmountInput") && input->property("activeFocus").toBool());
        if (tab_away) QTest::keyClick(m_app.window(), Qt::Key_Tab, Qt::NoModifier, 1);
        else QVERIFY(qmltest::Click(m_app, "sendAddressInput"));
        QTRY_VERIFY(!input->property("activeFocus").toBool());
        QCoreApplication::processEvents();
        QCOMPARE(recipient->amountIntentRevision(), intent);
        QCOMPARE(wallet->maximumRecipient(), recipient);
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Maximum);
        wallet->scheduleFeeEstimates(); QTRY_VERIFY(!wallet->feeEstimatePending());
        QCOMPARE(wallet->maximumRecipient(), recipient);
        QCOMPARE(recipient->amount()->satoshi(), maximum);
    }
    void maximumHandoverClearsTheFormerRecipient_data()
    {
        QTest::addColumn<bool>("with_address"); QTest::newRow("gross-before-address") << false; QTest::newRow("net-after-address") << true;
    }
    void maximumHandoverClearsTheFormerRecipient()
    {
        QFETCH(bool, with_address); const auto setup = prepareSweep(false); QVERIFY(setup); auto* wallet = setup->wallet;
        auto* first = wallet->sendRecipientList()->currentRecipient();
        if (with_address) QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(wallet->maximumRecipient(), first);
        QTRY_VERIFY(first->amount()->satoshi() > 0);
        QVERIFY(qmltest::Click(m_app, "sendAddRecipientButton"));
        auto* second = wallet->sendRecipientList()->currentRecipient(); QVERIFY(second != first);
        const auto second_address = qmltest::NewAddress(m_app, setup->destination_wallet->name());
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", second_address));
        QTRY_COMPARE(second->address()->address(), second_address);
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(wallet->maximumRecipient(), second);
        QTRY_VERIFY(!first->amount()->isSet()); QCOMPARE(first->amount()->satoshi(), CAmount{0});
        QVERIFY(!wallet->sendRecipientList()->allValid());
        QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

    void localizedTypedSendPreparesExactOutput_data()
    {
        QTest::addColumn<QString>("text"); QTest::addColumn<int>("unit"); QTest::addColumn<int>("producer");
        QTest::newRow("german-comma") << "0,02" << 0 << 0;
        QTest::newRow("dot-control") << "0.02" << 0 << 0;
        QTest::newRow("native-clipboard-comma") << "0,02" << 0 << 1;
        QTest::newRow("ime-commit-comma") << "0,02" << 0 << 2;
        QTest::newRow("milli-unit") << "20" << 1 << 0;
        QTest::newRow("micro-unit") << "20000" << 2 << 0;
        QTest::newRow("sat-unit") << "2000000" << 3 << 0;
    }

    void localizedTypedSendPreparesExactOutput()
    {
        QFETCH(QString, text); QFETCH(int, unit); QFETCH(int, producer);
        const QLocale previous;
        const auto restore = qScopeGuard([previous] { QLocale::setDefault(previous); });
        QLocale::setDefault(QLocale("de_DE"));
        const auto setup = prepareSweep(false); QVERIFY(setup);
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options); options->setDisplayUnit(unit);
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        auto* input = qmltest::WaitForVisible(m_app, "sendAmountInput"); QVERIFY(input);
        QTRY_VERIFY(qmltest::Click(m_app, "sendAmountInput") && input->property("activeFocus").toBool());
        QTest::keyClick(m_app.window(), Qt::Key_A, Qt::ControlModifier, 1);
        QTest::keyClick(m_app.window(), Qt::Key_Backspace, Qt::NoModifier, 1);
        QCOMPARE(input->property("text").toString(), QString{});
        if (producer == 1) {
            QGuiApplication::clipboard()->setText(text);
            QTest::keyClick(m_app.window(), Qt::Key_V, Qt::ControlModifier, 1);
            QTRY_COMPARE(input->property("text").toString(), text);
        } else if (producer == 2) {
            const auto before_preedit = setup->wallet->sendRecipientList()->totalAmountSatoshi();
            QInputMethodEvent preedit(QStringLiteral("0,"), {});
            QCoreApplication::sendEvent(input, &preedit);
            QCOMPARE(input->property("preeditText").toString(), QString("0,"));
            QCOMPARE(setup->wallet->sendRecipientList()->totalAmountSatoshi(), before_preedit);
            QInputMethodEvent committed;
            committed.setCommitString(text);
            QCoreApplication::sendEvent(input, &committed);
            QVERIFY(committed.isAccepted());
        } else {
            for (QChar key : text) QTest::keyClick(m_app.window(), static_cast<Qt::Key>(key.unicode()), Qt::NoModifier, 1);
        }
        const QString entered = input->property("text").toString();
        QVERIFY(qmltest::Click(m_app, "sendAddressInput"));
        // A producer event is not yet the Domain draft and fee readback.
        // Await the exact validated snapshot before one physical review action.
        QTRY_VERIFY(([&] {
            const auto draft = setup->wallet->sendRecipientList()->draftSnapshot();
            return draft && setup->wallet->sendRecipientList()->allValid()
                && !setup->wallet->sendRecipientList()->validationPending()
                && setup->wallet->sendRecipientList()->totalAmountSatoshi() == CAmount{2'000'000}
                && !setup->wallet->fees()->policyPending() && !setup->wallet->feeEstimatePending()
                && setup->wallet->fees()->estimateFresh()
                && setup->wallet->fees()->estimateDraftRevision() == QString::number(draft->revision)
                && setup->wallet->fees()->estimatePolicyRevision() == setup->wallet->fees()->revision();
        }()));
        qInfo() << "Typed Send draft" << text << entered << "canonical sats" << setup->wallet->sendRecipientList()->totalAmountSatoshi()
                << "available sats" << setup->wallet->availableSendBalanceSatoshi() << "error" << setup->wallet->sendRecipientList()->validationError();
        QTRY_VERIFY_WITH_TIMEOUT(qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool(), 30'000);
        auto* review = qmltest::Find(m_app, "sendReviewButton"); QVERIFY(review);
        QSignalSpy clicked{review, SIGNAL(clicked())}; QVERIFY(clicked.isValid());
        const auto prior_preparation = setup->wallet->preparationId();
        const auto original_draft = setup->wallet->sendRecipientList()->revision();
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QTRY_COMPARE(clicked.count(), 1);
        QTRY_COMPARE(setup->wallet->preparationId(), prior_preparation + 1);
        const auto original_preparation = setup->wallet->preparationId();
        QTRY_VERIFY(!setup->wallet->preparationBusy());
        QCOMPARE(setup->wallet->preparationId(), original_preparation);
        QCOMPARE(setup->wallet->sendRecipientList()->revision(), original_draft);
        qInfo() << "Typed Send preparation" << int(setup->wallet->preparationState())
                << setup->wallet->transactionError() << "draft revision"
                << setup->wallet->sendRecipientList()->revision();
        QCOMPARE(setup->wallet->preparationState(), WalletQmlModel::PreparationState::PreparationReady);
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        auto* prepared = setup->wallet->currentTransaction(); QVERIFY(prepared && prepared->getWtx());
        const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + EncodeHexTx(*prepared->getWtx()) + "\"]"));
        CAmount recipient_amount{-1};
        for (const auto& output : decoded.find_value("vout").getValues()) {
            const auto& destination = output.find_value("scriptPubKey").find_value("address");
            if (destination.isStr() && destination.get_str() == setup->address.toStdString()) recipient_amount = AmountFromValue(output.find_value("value"));
        }
        qInfo() << "Typed Send amount oracle" << text << "accepted text" << entered << "decoded recipient sats" << recipient_amount;
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewCloseButton"));
        QCOMPARE(recipient_amount, CAmount{2'000'000});
    }
    // #1000: a balance or fee refresh while the untouched amount field has
    // focus updates the maximum; leaving the field must not write stale text.
    void refreshedMaximumSurvivesLeavingTheFocusedAmountField_data()
    {
        QTest::addColumn<bool>("incoming_coin"); QTest::newRow("incoming-coin") << true; QTest::newRow("fee-rate-change") << false;
    }
    void refreshedMaximumSurvivesLeavingTheFocusedAmountField()
    {
        QFETCH(bool, incoming_coin); const auto setup = prepareSweep(false, false, true); QVERIFY(setup); auto* wallet = setup->wallet;
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(wallet->maximumRecipient(), recipient);
        QTRY_COMPARE_WITH_TIMEOUT(recipient->amount()->satoshi(), setup->expected_amount, 30'000); QTRY_VERIFY(!wallet->feeEstimatePending());
        const auto intent = recipient->amountIntentRevision();
        auto* input = qmltest::WaitForVisible(m_app, "sendAmountInput"); QVERIFY(input);
        QTRY_VERIFY(qmltest::Click(m_app, "sendAmountInput") && input->property("activeFocus").toBool());
        const auto stale_text = input->property("text").toString(); QCOMPARE(stale_text, recipient->amount()->inputText());
        if (incoming_coin) {
            const auto incoming = qmltest::NewAddress(m_app, wallet->name());
            qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + incoming.toStdString() + "\",\"amount\":0.5,\"fee_rate\":1}"), setup->destination_wallet->name());
            qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + setup->address.toStdString() + "\"]"));
            QTRY_VERIFY_WITH_TIMEOUT(recipient->amount()->satoshi() > setup->expected_amount, 30'000);
        } else {
            wallet->fees()->customRate()->edit("5");
            QTRY_VERIFY_WITH_TIMEOUT(recipient->amount()->satoshi() < setup->expected_amount && recipient->amount()->satoshi() > 0, 30'000);
        }
        QTRY_VERIFY(!wallet->feeEstimatePending() && wallet->fees()->estimateFresh());
        const auto refreshed = recipient->amount()->satoshi();
        QVERIFY(input->property("activeFocus").toBool());
        QTRY_COMPARE(input->property("text").toString(), recipient->amount()->inputText()); QVERIFY(input->property("text").toString() != stale_text);
        QVERIFY(qmltest::Click(m_app, "sendNoteInput")); QTRY_VERIFY(!input->property("activeFocus").toBool());
        QCoreApplication::processEvents();
        QCOMPARE(recipient->amountIntentRevision(), intent); QCOMPARE(wallet->maximumRecipient(), recipient);
        QCOMPARE(recipient->amountMode(), SendRecipient::AmountMode::Maximum); QCOMPARE(recipient->amount()->satoshi(), refreshed);
        wallet->scheduleFeeEstimates(); QTRY_VERIFY(!wallet->feeEstimatePending());
        QCOMPARE(wallet->maximumRecipient(), recipient); QCOMPARE(recipient->amount()->satoshi(), refreshed);
        QTRY_VERIFY(qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

    // #1001: choosing Manual and selecting nothing shows "No inputs selected";
    // Review stays disabled and preparation refuses to pick wallet inputs.
    void emptyManualChoiceShowsNoInputsAndCannotReview()
    {
        auto* destination = qmltest::CreateWallet(m_app, "empty_choice_destination"); QVERIFY(destination);
        auto* wallet = qmltest::CreateWallet(m_app, "empty_choice_sender"); QVERIFY(wallet); QVERIFY(qmltest::FundWallet(m_app, *wallet));
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + qmltest::NewAddress(m_app, wallet->name()).toStdString() + "\"]"));
        const auto destination_address = qmltest::NewAddress(m_app, destination->name());
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", destination_address)); QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "1"));
        QTRY_VERIFY(!wallet->sendRecipientList()->validationPending() && wallet->sendRecipientList()->allValid());
        QTRY_VERIFY(!wallet->feeEstimatePending() && wallet->estimatedFeeSatoshi() > 0);
        auto* review = qmltest::Find(m_app, "sendReviewButton"); QVERIFY(review); QTRY_VERIFY(review->property("enabled").toBool());
        auto* picker = qmltest::Find(m_app, "sendCoinControlPicker"); QVERIFY(picker);
        QVERIFY(qmltest::Click(m_app, "sendCoinControlPickerOption_1")); QTRY_COMPARE(picker->property("currentIndex").toInt(), 1);
        auto* coins = wallet->coinsListModel(); coins->update();
        QTRY_VERIFY(!coins->loading() && coins->rowCount() >= 2 && coins->manualInputSelection() && !wallet->coinSelectionPending());
        QCOMPARE(coins->selectedCoinsCount(), 0); QVERIFY(!wallet->selectedCoins() || wallet->selectedCoins()->empty());
        QTRY_COMPARE(qmltest::Find(m_app, "sendInputsSelectedText")->property("text").toString(), QStringLiteral("No inputs selected"));
        QTRY_VERIFY(!wallet->feeEstimatePending());
        QVERIFY(!review->property("enabled").toBool());
        auto* review_popup = qmltest::Find(m_app, "transactionReviewPopup"); QVERIFY(review_popup);
        qmltest::Click(m_app, "sendReviewButton");
        // The model's own entry point must refuse too, not choose inputs.
        const auto ticket = wallet->requestPrepareTransaction();
        if (ticket) QTRY_VERIFY(!wallet->preparationBusy());
        QVERIFY(wallet->preparationState() != WalletQmlModel::PreparationState::PreparationReady);
        QVERIFY(!wallet->currentTransaction()); QVERIFY(!QTest::qWaitFor([&] { return review_popup->property("opened").toBool(); }, 1'000));
        QCOMPARE(picker->property("currentIndex").toInt(), 1); QVERIFY(!review->property("enabled").toBool()); QVERIFY(wallet->coinSelectionBlocked());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

    // #1087: with avoid_reuse on and a clean output available, the badge, Max
    // and the prepared inputs all exclude reused outputs. The disabled-policy
    // control shows those same outputs are otherwise spendable.
    void avoidReusePolicyGovernsMaximumBadgeAndPreparedInputs_data()
    {
        QTest::addColumn<bool>("avoid_reuse");
        QTest::newRow("wallet-policy-enabled") << true; QTest::newRow("wallet-policy-disabled-control") << false;
    }
    void avoidReusePolicyGovernsMaximumBadgeAndPreparedInputs()
    {
        QFETCH(bool, avoid_reuse);
        auto* destination = qmltest::CreateWallet(m_app, "reuse_destination"); QVERIFY(destination);
        const auto destination_address = qmltest::NewAddress(m_app, destination->name());
        const QString wallet_name = "reuse_sender";
        UniValue create_params{UniValue::VOBJ}; create_params.pushKV("wallet_name", wallet_name.toStdString()); create_params.pushKV("avoid_reuse", avoid_reuse);
        qmltest::Rpc(m_app, "createwallet", create_params);
        auto* wallet = qmltest::SelectOpenWalletForSetup(m_app, wallet_name); QVERIFY(wallet);
        const auto reused_address = qmltest::NewAddress(m_app, wallet_name);
        // Two mature outputs at one address; spending one dirties the rest,
        // while the spend's change is a clean output.
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[102,\"" + reused_address.toStdString() + "\"]"));
        QTRY_COMPARE(qmltest::Rpc(m_app, "listunspent", {}, wallet_name).size(), 2U);
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + destination_address.toStdString() + "\",\"amount\":1,\"fee_rate\":1}"), wallet_name);
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + reused_address.toStdString() + "\"]"));
        QCOMPARE(qmltest::Rpc(m_app, "getwalletinfo", {}, wallet_name).find_value("avoid_reuse").get_bool(), avoid_reuse);
        std::set<COutPoint> clean, dirty; CAmount clean_total{0}, dirty_total{0};
        const auto unspent = qmltest::Rpc(m_app, "listunspent", {}, wallet_name);
        for (const auto& coin : unspent.getValues()) {
            const COutPoint outpoint{Txid::FromHex(coin.find_value("txid").get_str()).value(), coin.find_value("vout").getInt<uint32_t>()};
            const auto value = AmountFromValue(coin.find_value("amount"));
            if (coin.find_value("address").get_str() == reused_address.toStdString()) {
                if (avoid_reuse) QVERIFY(coin.find_value("reused").get_bool());
                dirty.insert(outpoint); dirty_total += value;
            } else { clean.insert(outpoint); clean_total += value; }
        }
        QVERIFY(!dirty.empty()); QVERIFY(!clean.empty());
        const CAmount spendable = avoid_reuse ? clean_total : clean_total + dirty_total;
        QTRY_COMPARE_WITH_TIMEOUT(wallet->balanceSatoshi(), spendable, 30'000);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", destination_address));
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        QTRY_VERIFY(!recipient->validationPending() && recipient->address()->address() == destination_address);
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(wallet->maximumRecipient(), recipient);
        QTRY_VERIFY_WITH_TIMEOUT(wallet->availableSendBalanceKnown() && !wallet->feeEstimatePending() && wallet->estimatedFeeSatoshi() > 0 && recipient->amount()->satoshi() > 0, 30'000);
        // Max and the wallet badge agree on the same policy.
        QCOMPARE(wallet->availableSendBalanceSatoshi(), wallet->balanceSatoshi());
        QCOMPARE(recipient->amount()->satoshi() + wallet->estimatedFeeSatoshi(), spendable);
        QVERIFY(recipient->amountError().isEmpty()); QVERIFY(wallet->fees()->errorText().isEmpty());
        QTRY_VERIFY(wallet->sendDraftSweepsWallet());
        QTRY_VERIFY(qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QVERIFY(qmltest::Click(m_app, "sendReviewButton")); QVERIFY(qmltest::Click(m_app, "sendReviewSweepConfirmButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(wallet->currentTransaction());
        std::set<COutPoint> spent;
        for (const auto& in : wallet->currentTransaction()->getWtx()->vin) spent.insert(in.prevout);
        std::set<COutPoint> expected{clean};
        if (!avoid_reuse) expected.insert(dirty.begin(), dirty.end());
        QCOMPARE(spent.size(), expected.size()); QVERIFY(spent == expected);
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewCloseButton"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

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
        auto* wallet = qmltest::LoadFundedWallet(m_app, "custom_fee");
        QVERIFY(wallet);
        const auto destination = qmltest::NewAddress(m_app, wallet->name());

        // Choose a custom fee for a payment from the funded wallet.
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", destination));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.01"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionOption3"));

        // Finish the initial preset projection before editing the captured literal.
        QTRY_VERIFY(!wallet->customFeeTargetPending());
        QTRY_VERIFY(!wallet->feeEstimatePending());
        QTRY_VERIFY(wallet->availableSendBalanceKnown());
        QTRY_COMPARE(qmltest::Find(m_app, "feeSelectionCustomRateInput")->property("text").toString(), wallet->customFeeRate());
        const auto network = qmltest::Rpc(m_app, "getnetworkinfo");
        QCOMPARE(wallet->minimumCustomFeeRate(), static_cast<double>(AmountFromValue(network.find_value("relayfee"))) / 1000.0);

        // A rate below the relay minimum must fail before review or broadcast.
        if (reject_low_rate) {
            QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "0.1"));
            QTRY_COMPARE(wallet->customFeeRate(), QStringLiteral("0.1"));
            QTRY_VERIFY(!wallet->fees()->policyPending());
            QTRY_VERIFY(!wallet->customFeeRateValid());
            QTRY_VERIFY(!wallet->feeEstimatePending());
            QCOMPARE(wallet->estimatedFee(), QString{});
            QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
            QCOMPARE(qmltest::Find(m_app, "feeSelectionEstimateLabel")->property("value").toString(), QString::fromUtf8("—"));
            QCOMPARE(qmltest::Find(m_app, "feeSelectionCustomRateRow")->property("errorText").toString(), QStringLiteral("Enter a fee rate of at least 1 sat/vB."));
            QVERIFY(!AwaitPreparation(*wallet));
            QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        }

        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", rate));
        QTRY_COMPARE(wallet->customFeeRate(), rate);
        QTRY_VERIFY(wallet->customFeeRateValid());
        QTRY_VERIFY(!wallet->feeEstimatePending());
        QTRY_VERIFY(qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QVERIFY(wallet->currentTransaction());
        const auto reviewed_fee = wallet->currentTransaction()->getTransactionFee();

        // Reconstruct the original input value through independent public RPC reads.
        // Refund the fee into output zero, then force fundrawtransaction to price
        // the same immutable shape by subtracting its fee from that output.
        const auto reviewed = wallet->currentTransaction()->getWtx();
        const auto raw = qmltest::Call(m_app, [reviewed](interfaces::Node*) { return EncodeHexTx(*reviewed); });
        const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + raw + "\"]"));
        UniValue inputs{UniValue::VARR};
        UniValue outputs{UniValue::VARR};
        CAmount input_value{0};
        CAmount output_value{0};
        for (const auto& input : decoded.find_value("vin").getValues()) {
            UniValue entry{UniValue::VOBJ};
            entry.pushKV("txid", input.find_value("txid"));
            entry.pushKV("vout", input.find_value("vout"));
            entry.pushKV("sequence", input.find_value("sequence"));
            inputs.push_back(entry);
            const auto coin = qmltest::Rpc(m_app, "gettxout", qmltest::Json("[\"" + input.find_value("txid").get_str() + "\"," + input.find_value("vout").write() + "]"));
            QVERIFY(!coin.isNull());
            input_value += AmountFromValue(coin.find_value("value"));
        }
        for (const auto& output : decoded.find_value("vout").getValues()) output_value += AmountFromValue(output.find_value("value"));
        const auto independently_paid_fee = input_value - output_value;
        QCOMPARE(reviewed_fee, independently_paid_fee);
        for (size_t i{0}; i < decoded.find_value("vout").size(); ++i) {
            const auto& output = decoded.find_value("vout")[i];
            UniValue entry{UniValue::VOBJ};
            entry.pushKV(output.find_value("scriptPubKey").find_value("address").get_str(), ValueFromAmount(AmountFromValue(output.find_value("value")) + (i == 0 ? independently_paid_fee : 0)));
            outputs.push_back(entry);
        }
        UniValue raw_params{UniValue::VARR};
        raw_params.push_back(inputs);
        raw_params.push_back(outputs);
        raw_params.push_back(decoded.find_value("locktime"));
        const auto refunded_raw = qmltest::Rpc(m_app, "createrawtransaction", raw_params).get_str();
        const auto reference = [&](double fee_rate) {
            UniValue options{UniValue::VOBJ};
            options.pushKV("add_inputs", false);
            options.pushKV("lockUnspents", false);
            options.pushKV("fee_rate", fee_rate);
            options.pushKV("subtract_fee_from_outputs", qmltest::Json("[0]"));
            UniValue params{UniValue::VARR};
            params.push_back(refunded_raw);
            params.push_back(options);
            return qmltest::Rpc(m_app, "fundrawtransaction", params, wallet->name());
        };
        const auto size_reference = reference(1.0);
        const auto actual_reference = reference(rate.toDouble());
        QCOMPARE(size_reference.find_value("changepos").getInt<int>(), -1);
        QCOMPARE(actual_reference.find_value("changepos").getInt<int>(), -1);
        const auto maximum_signed_vsize = AmountFromValue(size_reference.find_value("fee"));
        QVERIFY(maximum_signed_vsize > 0);
        const auto funded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + actual_reference.find_value("hex").get_str() + "\"]"));
        const auto refunded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + refunded_raw + "\"]"));
        const auto sized = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + size_reference.find_value("hex").get_str() + "\"]"));
        QCOMPARE(sized.find_value("vin").write(), refunded.find_value("vin").write());
        QCOMPARE(sized.find_value("vout").size(), decoded.find_value("vout").size());
        for (size_t i{0}; i < decoded.find_value("vout").size(); ++i) {
            QCOMPARE(sized.find_value("vout")[i].find_value("scriptPubKey").write(), decoded.find_value("vout")[i].find_value("scriptPubKey").write());
        }
        QCOMPARE(funded.find_value("locktime").write(), decoded.find_value("locktime").write());
        QCOMPARE(funded.find_value("vin").write(), refunded.find_value("vin").write());
        QCOMPARE(funded.find_value("vout").write(), decoded.find_value("vout").write());
        QCOMPARE(AmountFromValue(actual_reference.find_value("fee")), reviewed_fee);

        QTRY_VERIFY(wallet->currentTransactionCanSend());
        QTRY_VERIFY(qmltest::Find(m_app, "sendTransactionReviewSendButton")->property("enabled").toBool());
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
        auto* wallet = qmltest::LoadFundedWallet(m_app);
        QVERIFY(wallet);
        auto* coins = wallet->coinsListModel();
        coins->update();
        QTRY_VERIFY(coins->rowCount() > 0);
        const auto coin_id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();

        // Filtering keeps the pending selection; cancelling discards it.
        coins->beginSelection();
        QVERIFY(coins->toggleCoinSelectionById(coin_id));
        QTRY_COMPARE(coins->selectedCoinsCount(), 1);
        coins->setSearchText("no matching note");
        QTRY_COMPARE(coins->rowCount(), 0);
        QTRY_COMPARE(coins->selectedCoinsCount(), 1);
        coins->cancelSelection();
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        QTRY_VERIFY(wallet->selectedCoins()->empty());

        // Applying commits the selection to the wallet.
        coins->setSearchText("");
        QTRY_VERIFY(!coins->loading());
        QTRY_VERIFY(coins->rowCount() > 0);
        coins->beginSelection();
        QVERIFY(coins->toggleCoinSelectionById(coin_id));
        coins->applySelection();
        QTRY_COMPARE(wallet->selectedCoins()->size(), 1U);

        // Lock changes must reach Core, independently of the selection state.
        QVERIFY(coins->setCoinsLocked({coin_id}, true));
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
        QCOMPARE(qmltest::Rpc(m_app, "listlockunspent", {}, wallet->name()).size(), 1U);
        QVERIFY(coins->setCoinsLocked({coin_id}, false));
        QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
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
        QTRY_VERIFY(!wallet->coinSelectionPending());
        QTRY_COMPARE(coins->selectedCoinsCount(), 0);
        QVERIFY(!coins->manualInputSelection());
        // Reset deliberately returns to Automatic. Re-enter Manual through the
        // visible intent before opening its explicit selection dialog.
        QVERIFY(qmltest::Click(m_app, "sendCoinControlPickerOption_1"));
        QTRY_VERIFY(!wallet->coinSelectionPending() && coins->manualInputSelection());
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
        QTRY_COMPARE(wallet->selectedCoins()->size(), 1U);
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
        QTRY_COMPARE(wallet->selectedCoins()->size(), 1U);
        QTRY_COMPARE(wallet->sendRecipientList()->totalAmountSatoshi(), original_amount);

        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "2"));
        QTRY_COMPARE(wallet->estimatedFeeSatoshi(), 2 * original_fee);
        QTRY_COMPARE(wallet->sendRecipientList()->totalAmountSatoshi() + wallet->estimatedFeeSatoshi(), 50 * COIN);
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "1"));
        QTRY_VERIFY(!wallet->sendRecipientList()->validationPending() &&
                    wallet->sendRecipientList()->totalAmountSatoshi() == COIN);
        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "3"));
        QTRY_VERIFY(!wallet->fees()->policyPending() && !wallet->feeEstimatePending() &&
                    wallet->fees()->estimateFresh() &&
                    wallet->fees()->estimatePolicyRevision() == wallet->fees()->revision());
        QCOMPARE(wallet->sendRecipientList()->totalAmountSatoshi(), COIN);
    }

    void maximumAndSweepReviewRespectOwnershipAndSelection()
    {
        QFETCH(bool, manual);
        QFETCH(bool, self_send);
        const auto setup = prepareSweep(manual, self_send);
        QVERIFY(setup);
        auto* wallet = setup->wallet;
        const QPointer<SendRecipient> original_recipient{wallet->sendRecipientList()->currentRecipient()};
        auto* maximum = qmltest::Find(m_app, "sendUseMaximumButton"); QVERIFY(maximum);
        QSignalSpy clicked{maximum, SIGNAL(clicked())}; QVERIFY(clicked.isValid());
        QSignalSpy rejected{wallet->sendRecipientList(), SIGNAL(commandRejected(QString))}; QVERIFY(rejected.isValid());
        const auto report_original = qScopeGuard([&] {
            const auto draft = wallet->sendRecipientList()->draftSnapshot();
            const auto selected = wallet->selectedCoins();
            qInfo() << "Original Maximum receipt" << "clicked" << clicked.count() << "rejected" << rejected.count()
                    << "rejection" << (rejected.isEmpty() ? QString{} : rejected.constFirst().value(0).toString())
                    << "same-recipient" << (original_recipient == wallet->sendRecipientList()->currentRecipient())
                    << "mode" << (original_recipient ? int(original_recipient->amountMode()) : -1)
                    << "maximum-owner" << (original_recipient && wallet->maximumRecipient() == original_recipient)
                    << "intent-revision" << wallet->sendRecipientList()->revision()
                    << "draft-revision" << (draft ? draft->revision : 0)
                    << "draft-maximum" << (draft ? draft->maximum : 0)
                    << "draft-pending" << wallet->sendRecipientList()->validationPending()
                    << "selection-pending" << wallet->coinSelectionPending()
                    << "selected-count" << (selected ? qint64(selected->size()) : qint64{-1})
                    << "selected-sats" << wallet->coinsListModel()->totalSelectedSatoshi()
                    << "available-sats" << wallet->availableSendBalanceSatoshi()
                    << "amount-sats" << (original_recipient ? original_recipient->amount()->satoshi() : -1)
                    << "error" << wallet->sendRecipientList()->validationError();
        });
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QTRY_COMPARE(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), 100 * COIN);
        QCOMPARE(clicked.count(), 1);
        QCOMPARE(rejected.count(), 0);
        QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        QTRY_VERIFY(!wallet->sendRecipientList()->currentRecipient()->validationPending() &&
                    wallet->sendRecipientList()->currentRecipient()->isValid());
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
        QTRY_COMPARE_WITH_TIMEOUT(wallet->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded, qmltest::kSecurityStateTimeoutMs);
        QTRY_VERIFY(wallet->isLocked());
        QTRY_COMPARE(wallet->selectedCoins()->size(), 2U);
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        const auto amount = QString::fromStdString(ValueFromAmount(setup->expected_amount - 100).getValStr());
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
        QVERIFY(qmltest::SecretTextLocations(m_app.window(), "review password").isEmpty());
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QTRY_VERIFY(wallet->isLocked());
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

    // #1015: with no estimator data, no Send screen may promise a block target. The presets only name the target they
    // request, and a typed Custom rate keeps its default 6-block slider value out of Review and the completion text.
    // (Core's own "Fallback fee" reason is covered by WalletQmlModelTests::coreFallbackFeeReachesPresetsAndReview;
    // this regtest app prices presets with its static policy instead.)
    void presetsAndCustomRateMakeNoBlockTargetPromise_data()
    {
        QTest::addColumn<int>("option"); QTest::newRow("priority") << 0; QTest::newRow("custom") << 3;
    }
    void presetsAndCustomRateMakeNoBlockTargetPromise()
    {
        QFETCH(int, option);
        auto* destination = qmltest::CreateWallet(m_app, "destination"); QVERIFY(destination);
        const auto address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::LoadFundedWallet(m_app, "sender"); QVERIFY(wallet);
        for (const auto* target : {"2", "6", "10"}) QVERIFY(!qmltest::Rpc(m_app, "estimatesmartfee", qmltest::Json(std::string{"["} + target + "]")).find_value("errors").isNull());
        QVERIFY(qmltest::Click(m_app, "sendTabButton")); QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.01"));
        const int targets[]{2, 6, 10};
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton"));
        for (int index{0}; index < 3; ++index) {
            auto* row = qmltest::WaitForVisible(m_app, qPrintable(QStringLiteral("feeSelectionOption%1").arg(index))); QVERIFY(row);
            QCOMPARE(row->property("subtitle").toString(), QStringLiteral("Requested target: %1 blocks").arg(targets[index]));
        }
        QVERIFY(qmltest::Click(m_app, qPrintable(QStringLiteral("feeSelectionOption%1").arg(option))));
        QTRY_VERIFY(!wallet->fees()->policyPending());
        if (option == 3) {
            QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "20"));
            QTRY_VERIFY(!wallet->fees()->policyPending() && wallet->customFeeRateValid());
            QCOMPARE(wallet->fees()->targetBlocks(), 6);
        } else QCOMPARE(wallet->fees()->targetBlocks(), 2);
        QTRY_VERIFY2(!wallet->feeEstimatePending() && wallet->fees()->estimateFresh(), qPrintable(wallet->fees()->errorText()));
        QVERIFY(!wallet->fees()->hasEstimatorTarget());
        QTRY_VERIFY_WITH_TIMEOUT(qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool(), 30'000);
        QVERIFY(qmltest::Click(m_app, "sendReviewButton")); QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        auto* review = wallet->currentTransaction(); QVERIFY(review);
        QVERIFY(review->reviewFeeSource() != static_cast<int>(FeeSelectionModel::Estimated));
        QVERIFY(!qmltest::Find(m_app, "sendTransactionReviewTargetBlocks")->property("visible").toBool());
        QTRY_VERIFY(qmltest::Find(m_app, "sendTransactionReviewSendButton")->property("enabled").toBool());
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        auto* complete = qmltest::WaitForVisible(m_app, "sendCompletePage"); QVERIFY(complete);
        QCOMPARE(complete->property("targetBlocks").toInt(), 0);
        QCOMPARE(complete->property("descriptionText").toString(), QStringLiteral("Your node accepted the transaction. Confirmation depends on its fee rate."));
        const auto entry = qmltest::Rpc(m_app, "getmempoolentry", qmltest::Json("[\"" + complete->property("txid").toString().toStdString() + "\"]"));
        QCOMPARE(AmountFromValue(entry.find_value("fees").find_value("base")), review->feeAmount()->satoshi());
    }
    // #1115: after Use maximum moves to recipient 2, recipient 1 is left without an amount. The form must name
    // recipient 1 as the one needing attention rather than asking to complete every recipient.
    void maximumHandoverNamesTheRecipientNeedingAttention()
    {
        const auto setup = prepareSweep(false); QVERIFY(setup); auto* wallet = setup->wallet;
        auto* first = wallet->sendRecipientList()->currentRecipient();
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(wallet->maximumRecipient(), first);
        QTRY_VERIFY(first->amount()->satoshi() > 0);
        QVERIFY(qmltest::Click(m_app, "sendAddRecipientButton"));
        auto* second = wallet->sendRecipientList()->currentRecipient(); QVERIFY(second != first);
        const auto second_address = qmltest::NewAddress(m_app, setup->destination_wallet->name());
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", second_address));
        QTRY_COMPARE(second->address()->address(), second_address);
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton")); QTRY_COMPARE(wallet->maximumRecipient(), second);
        QTRY_VERIFY(!first->amount()->isSet() && !wallet->sendRecipientList()->validationPending());
        const QString expected{"Complete recipient 1 before continuing."};
        QCOMPARE(wallet->sendRecipientList()->validationError(), expected);
        QTRY_COMPARE(qmltest::Find(m_app, "sendPrepareTransactionErrorText")->property("text").toString(), expected);
        QVERIFY(qmltest::Find(m_app, "sendPrepareTransactionErrorText")->property("visible").toBool());
        QVERIFY(!qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }
    // #1106: a send-all Review prepared in sats follows a global display-unit change made while it is open: its
    // amount rows and the final "Send all available funds?" warning switch to BTC.
    void sendAllReviewFollowsAGlobalDisplayUnitChange()
    {
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options);
        const auto restore = qScopeGuard([options] { options->setDisplayUnit(0); });
        const auto setup = prepareSweep(false); QVERIFY(setup); auto* wallet = setup->wallet;
        options->setDisplayUnit(3); QTRY_COMPARE(wallet->displayUnit(), 3);
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", setup->address));
        QVERIFY(qmltest::Click(m_app, "sendUseMaximumButton"));
        QTRY_COMPARE(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), setup->expected_amount);
        QTRY_VERIFY(!wallet->feeEstimatePending());
        QTRY_VERIFY_WITH_TIMEOUT(qmltest::Find(m_app, "sendReviewButton")->property("enabled").toBool(), 30'000);
        QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendReviewSweepAlert")); QVERIFY(qmltest::Click(m_app, "sendReviewSweepConfirmButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        auto* review = wallet->currentTransaction(); QVERIFY(review); const auto fee = review->feeAmount()->satoshi();
        QCOMPARE(review->amountAmount()->satoshi(), setup->expected_amount);
        const auto btc = [](CAmount amount) -> QString { return QString::fromStdString(ValueFromAmount(amount).getValStr()) + " BTC"; };
        auto* fee_row = qmltest::Find(m_app, "sendTransactionReviewFee"); auto* total_row = qmltest::Find(m_app, "sendTransactionReviewTotal");
        QTRY_VERIFY(fee_row->property("value").toString().endsWith(" sats")); QVERIFY(total_row->property("value").toString().endsWith(" sats"));
        options->setDisplayUnit(0);
        QTRY_COMPARE(fee_row->property("value").toString(), btc(fee));
        QTRY_COMPARE(total_row->property("value").toString(), btc(setup->expected_amount + fee));
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        auto* alert = qmltest::WaitForVisible(m_app, "sendSweepAlert"); QVERIFY(alert);
        const auto message = alert->property("message").toString();
        QVERIFY2(message.contains("(" + btc(setup->expected_amount) + ")"), qPrintable(message)); QVERIFY2(!message.contains("sat"), qPrintable(message));
        QVERIFY(qmltest::Click(m_app, "sendSweepCancelButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "sendSweepAlert")->property("visible").toBool());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

    // #1008: with wallet broadcasting enabled, a transaction the local mempool
    // rejects is never reported as sent and is not left in the wallet.
    void localMempoolRejectionIsNeverReportedAsSent()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "mempool-rejected"); QVERIFY(wallet); QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto name = wallet->name(), address = qmltest::NewAddress(m_app, name);
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        wallet->setCustomFeeEnabled(true); wallet->setCustomFeeRate("1");
        auto* recipient = wallet->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        recipient->address()->setAddress(address, 2); recipient->amount()->setSatoshi(100'000);
        QVERIFY(AwaitPreparation(*wallet)); const auto approved = wallet->currentTransaction()->getWtx(); QVERIFY(approved);
        const auto txid = approved->GetHash().GetHex();
        // A negative priority makes the node's own mempool refuse this exact
        // transaction for its fee, while the wallet still considers it valid.
        QVERIFY(qmltest::Rpc(m_app, "prioritisetransaction", qmltest::Json("[\"" + txid + "\",0,-1000000]")).get_bool());
        const auto probe = qmltest::Rpc(m_app, "testmempoolaccept", qmltest::Json("[[\"" + EncodeHexTx(*approved) + "\"]]"));
        QVERIFY(!probe[0].find_value("allowed").get_bool());
        auto* page = qmltest::Find(m_app, "sendPage"); QVERIFY(page); QVERIFY(QMetaObject::invokeMethod(page, "openTransactionReview"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        auto* sender = qmltest::Find(m_app, "sendTransactionReviewController"); QVERIFY(sender);
        QTRY_VERIFY_WITH_TIMEOUT(ReviewSendState(*sender) != SendOperation::State::None && ReviewSendState(*sender) != SendOperation::State::Pending, 30'000);
        QCOMPARE(ReviewSendState(*sender), SendOperation::State::Failed);
        QVERIFY2(wallet->transactionError().contains("fee"), qPrintable(wallet->transactionError()));
        QVERIFY2(sender->property("sendError").toString().contains("fee"), qPrintable(sender->property("sendError").toString()));
        QVERIFY(sender->property("txid").toString().isEmpty());
        auto* complete = qmltest::Find(m_app, "sendCompletePage"); QVERIFY(!complete || !complete->property("visible").toBool());
        const auto mempool = qmltest::Rpc(m_app, "getrawmempool"), history = qmltest::Rpc(m_app, "listtransactions", qmltest::Json("[\"*\",100,0,true]"), name);
        for (const auto& id : mempool.getValues()) QVERIFY(id.get_str() != txid);
        for (const auto& entry : history.getValues()) QVERIFY(entry.find_value("txid").get_str() != txid);
    }

    // draft-220-RC18: View coins derives its context action from the cached
    // row. A stale "Lock coin" over a Core temporary lock is refused at the
    // action boundary instead of silently persisting that lock.
    void staleCoinContextActionCannotPersistATemporaryLock()
    {
        QTRY_VERIFY(m_app.window()->property("preferenceGeometryRestored").toBool());
        m_app.window()->resize(1280, 900); QTRY_COMPARE(m_app.window()->width(), 1280);
        auto* wallet = qmltest::CreateWallet(m_app, "stale-context-lock"); QVERIFY(wallet); QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto unspent = qmltest::Rpc(m_app, "listunspent", {}, wallet->name()); QCOMPARE(unspent.size(), size_t{1});
        const QString id = QString::fromStdString(unspent[0].find_value("txid").get_str()) + QLatin1Char(':') + QString::number(unspent[0].find_value("vout").getInt<int>());
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::Click(m_app, "walletCoinsRow")); QVERIFY(qmltest::WaitForVisible(m_app, "walletCoinsPage"));
        auto* coins = wallet->coinsListModel(); QVERIFY(coins); QTRY_VERIFY(!coins->loading() && coins->rowCount() == 1);
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString(), id);
        UniValue input{UniValue::VOBJ}; input.pushKV("txid", unspent[0].find_value("txid")); input.pushKV("vout", unspent[0].find_value("vout"));
        UniValue inputs{UniValue::VARR}; inputs.push_back(input);
        QVERIFY(qmltest::Rpc(m_app, "lockunspent", qmltest::Json("[false," + inputs.write() + ",false]"), wallet->name()).get_bool());
        // Core has no lock notification, so the row keeps its unlocked flag.
        QVERIFY(!coins->data(coins->index(0), CoinsListModel::LockedRole).toBool());
        auto* page = qmltest::WaitForVisible(m_app, "walletCoinsPage"); QVERIFY(page);
        auto* page_item = qobject_cast<QQuickItem*>(page); QVERIFY(page_item);
        auto* row = qobject_cast<QQuickItem*>(qmltest::Find(page, QStringLiteral("coinSelectionItem_0"))); QVERIFY(row);
        // The wallet settings stack slides the page in. Send one right click
        // only once that stack and all transitions are idle, the row is
        // polished, and nothing outside the row covers the point.
        auto* stack = qmltest::Find(m_app, "walletSettingsStack");
        QVERIFY(QTest::qWaitFor([&] {
            return page_item->x() == 0.0
                && (!stack || !stack->property("busy").toBool())
                && qmltest::TransitionsFinished(m_app.window());
        }, 10'000));
        QVERIFY(qmltest::WaitForPolish(row));
        const auto point = row->mapToScene(row->boundingRect().center()).toPoint();
        QVERIFY(m_app.window()->contentItem()->contains(point));
        auto* menu = qmltest::Find(page, QStringLiteral("walletCoinContextMenu")); QVERIFY(menu);
        const auto context = [&] {
            const auto* topmost = qmltest::PointerTargetAt(m_app.window(), point, Qt::RightButton);
            return QStringLiteral("visible %1 opened %2 menu (%3, %4) window (%5x%6) active %7 row (%8, %9) enabled %10 stack busy %11 context coin %12 right-click target %13 (%14)")
                .arg(menu->property("visible").toBool()).arg(menu->property("opened").toBool())
                .arg(menu->property("x").toReal()).arg(menu->property("y").toReal())
                .arg(row->window()->width()).arg(row->window()->height()).arg(row->window()->isActive())
                .arg(point.x()).arg(point.y()).arg(row->isEnabled())
                .arg(stack ? stack->property("busy").toBool() : false)
                .arg(menu->parent() ? menu->parent()->property("contextCoinId").toString() : QStringLiteral("no list"))
                .arg(topmost ? topmost->objectName() : QStringLiteral("none"))
                .arg(topmost ? QString::fromUtf8(topmost->metaObject()->className()) : QString{});
        };
        QVERIFY2(qmltest::WaitUntilPointerTarget(row, point, Qt::RightButton, 10'000), qPrintable(context()));
        QTest::mousePress(row->window(), Qt::RightButton, Qt::NoModifier, point);
        QTest::mouseRelease(row->window(), Qt::RightButton, Qt::NoModifier, point);
        QVERIFY2(QTest::qWaitFor([&] { return menu->property("opened").toBool(); }, 10'000), qPrintable(context()));
        auto* toggle = qmltest::Find(menu, QStringLiteral("walletCoinLockToggle")); QVERIFY(toggle);
        QCOMPARE(toggle->property("text").toString(), QStringLiteral("Lock coin"));
        QVERIFY(qmltest::Click(toggle)); QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().contains(QStringLiteral("already locked")), qPrintable(coins->lockError()));
        QCOMPARE(coins->lockOperation()->result()->value->changed, size_t{0});
        // Only the temporary lock exists: reopening the wallet clears it.
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QPointer<WalletQmlModel> original{wallet}; controller->closeWallet(wallet->name()); QTRY_VERIFY(original.isNull());
        controller->setSelectedWallet("stale-context-lock"); QVERIFY(qmltest::WaitForOpenWallet(m_app, "stale-context-lock"));
        QCOMPARE(qmltest::Rpc(m_app, "listlockunspent", {}, QStringLiteral("stale-context-lock")).size(), size_t{0});
    }

    // #974: View coins drops a bulk mark once its output is spent, so the
    // footer cannot offer Lock/Unlock for a coin that no longer exists.
    void viewCoinsDropsTheMarkOfASpentCoin()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "spent-mark"); QVERIFY(wallet); QVERIFY(qmltest::FundWallet(m_app, *wallet));
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::Click(m_app, "walletCoinsRow")); QVERIFY(qmltest::WaitForVisible(m_app, "walletCoinsPage"));
        QTRY_COMPARE(qobject_cast<QQuickItem*>(qmltest::Find(m_app, "walletCoinsPage"))->x(), 0.0);
        auto* coins = wallet->coinsListModel(); QVERIFY(coins); QTRY_VERIFY(!coins->loading() && coins->rowCount() == 1);
        const auto marked = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(qmltest::Click(m_app, "walletCoinsSelectionButton")); QVERIFY(qmltest::Click(m_app, "coinSelectionItem_0"));
        QTRY_COMPARE(coins->markedCount(), 1);
        auto* lock = qmltest::Find(m_app, "coinsLockButton"); QVERIFY(lock); QTRY_VERIFY(lock->property("enabled").toBool());
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + qmltest::NewAddress(m_app, "spent-mark").toStdString() + "\",\"amount\":1,\"fee_rate\":1}"), wallet->name());
        QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coins->rowCount() >= 1 && coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString() != marked, 30'000);
        QTRY_COMPARE(coins->markedCount(), 0);
        QVERIFY(!lock->property("enabled").toBool());
        for (int row{0}; row < coins->rowCount(); ++row) QVERIFY(!coins->data(coins->index(row), CoinsListModel::MarkedRole).toBool());
    }

    // #974: a temporary reorg that removes the selected inputs keeps the
    // Manual selection; the same inputs return when the blocks reconnect.
    void temporaryReorgAbsenceKeepsTheManualSelection()
    {
        auto* other = qmltest::CreateWallet(m_app, "reorg-other"); QVERIFY(other);
        auto* wallet = qmltest::CreateWallet(m_app, "reorg-manual"); QVERIFY(wallet); QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto payment = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + qmltest::NewAddress(m_app, other->name()).toStdString() + "\",\"amount\":1,\"fee_rate\":1}"), wallet->name()).get_str();
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + qmltest::NewAddress(m_app, other->name()).toStdString() + "\"]"));
        // The confirmed change of the payment and the coinbase that just matured.
        const auto unspent = qmltest::Rpc(m_app, "listunspent", {}, wallet->name()); QCOMPARE(unspent.size(), size_t{2});
        QStringList ids; bool change{false};
        for (const auto& coin : unspent.getValues()) {
            ids << QString::fromStdString(coin.find_value("txid").get_str()) + QLatin1Char(':') + QString::number(coin.find_value("vout").getInt<int>());
            change |= coin.find_value("txid").get_str() == payment;
        }
        QVERIFY(change);
        auto* coins = wallet->coinsListModel(); QVERIFY(coins); coins->update(); QTRY_VERIFY(!coins->loading() && coins->coinCount() == 2);
        QVERIFY(coins->setManualInputSelection(true));
        coins->beginSelection(); for (const auto& id : ids) QVERIFY(coins->toggleCoinSelectionById(id)); coins->applySelection();
        QTRY_VERIFY(!wallet->coinSelectionPending()); QTRY_COMPARE(wallet->selectedCoins()->size(), size_t{2});
        const auto selected = *wallet->selectedCoins();
        // Disconnecting blocks 100 to 102 makes both coinbases immature again,
        // which drops the payment, and so its change, from the mempool.
        const auto block = qmltest::Rpc(m_app, "getblockhash", qmltest::Json("[100]")).get_str();
        qmltest::Rpc(m_app, "invalidateblock", qmltest::Json("[\"" + block + "\"]"));
        QCOMPARE(qmltest::Rpc(m_app, "listunspent", {}, wallet->name()).size(), size_t{0});
        coins->update(); QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coins->coinCount() == 0, 30'000);
        QTRY_COMPARE(coins->missingSelectedCoins(), 2);
        qmltest::Rpc(m_app, "reconsiderblock", qmltest::Json("[\"" + block + "\"]"));
        QCOMPARE(qmltest::Rpc(m_app, "listunspent", {}, wallet->name()).size(), size_t{2});
        coins->update(); QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coins->coinCount() == 2, 30'000);
        QTRY_COMPARE(coins->missingSelectedCoins(), 0);
        QVERIFY(coins->manualInputSelection()); QCOMPARE(coins->selectedCoinsCount(), 2);
        QTRY_VERIFY(!wallet->coinSelectionPending()); QCOMPARE(*wallet->selectedCoins(), selected);
    }

    // #974: a display-unit change does not rebuild the coin list of an open
    // wallet; only wallet content changes do.
    void displayUnitChangeDoesNotRefreshTheCoinList()
    {
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options);
        const auto restore = qScopeGuard([options] { options->setDisplayUnit(0); });
        auto* hidden = qmltest::CreateWallet(m_app, "unit-hidden"); QVERIFY(hidden); QVERIFY(qmltest::FundWallet(m_app, *hidden));
        auto* coins = hidden->coinsListModel(); QVERIFY(coins); coins->update(); QTRY_VERIFY(!coins->loading() && coins->coinCount() == 1);
        auto* shown = qmltest::CreateWallet(m_app, "unit-shown"); QVERIFY(shown);
        QTRY_VERIFY(!coins->loading());
        int refreshes{0};
        QObject context; connect(coins, &CoinsListModel::viewChanged, &context, [&] { if (coins->loading()) ++refreshes; });
        options->setDisplayUnit(3); QTRY_COMPARE(hidden->displayUnit(), 3); QTRY_COMPARE(shown->displayUnit(), 3);
        QCOMPARE(refreshes, 0);
        // Control: an actual wallet change still refreshes the hidden list.
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + qmltest::NewAddress(m_app, "unit-hidden").toStdString() + "\",\"amount\":1,\"fee_rate\":1}"), hidden->name());
        QTRY_VERIFY_WITH_TIMEOUT(refreshes > 0, 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coins->coinCount() == 2, 30'000);
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

    void manualSelectionKeepsItsOpeningAndRejectsMaturityReversal_data()
    {
        QTest::addColumn<bool>("reverse_maturity"); QTest::addColumn<bool>("external_lock");
        QTest::newRow("wallet-switch") << false << false;
        QTest::newRow("independent-rpc-reorg") << true << false;
        QTest::newRow("independent-rpc-lock") << false << true;
    }
    void manualSelectionKeepsItsOpeningAndRejectsMaturityReversal()
    {
        QFETCH(bool, reverse_maturity); QFETCH(bool, external_lock);
        auto* destination = qmltest::CreateWallet(m_app, "manual-destination"); QVERIFY(destination);
        const auto address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::CreateWallet(m_app, "manual-original"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + qmltest::NewAddress(m_app, wallet->name()).toStdString() + "\"]"));
        const auto available = qmltest::Rpc(m_app, "listunspent", {}, wallet->name());
        QVERIFY(available.size() >= 2);
        UniValue chosen;
        for (const auto& input : available.getValues())
            if (chosen.isNull() || input.find_value("confirmations").getInt<int>() < chosen.find_value("confirmations").getInt<int>()) chosen = input;
        QCOMPARE(chosen.find_value("confirmations").getInt<int>(), 101);
        const QString id = QString::fromStdString(chosen.find_value("txid").get_str()) + QLatin1Char(':') + QString::number(chosen.find_value("vout").getInt<int>());
        auto* coins = wallet->coinsListModel(); QVERIFY(coins); coins->update();
        QTRY_VERIFY(!coins->loading());
        QVERIFY(coins->setManualInputSelection(true));
        coins->beginSelection(); QVERIFY(coins->toggleCoinSelectionById(id)); coins->applySelection();
        QTRY_VERIFY(!wallet->coinSelectionPending()); QTRY_COMPARE(wallet->selectedCoins()->size(), size_t{1});
        const auto selected = *wallet->selectedCoins()->begin();
        QCOMPARE(selected.hash.GetHex(), chosen.find_value("txid").get_str());
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionOption3"));
        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "1"));
        QTRY_COMPARE(wallet->customFeeRate(), QStringLiteral("1"));
        QTRY_VERIFY(wallet->customFeeRateValid());
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "1"));
        QTRY_VERIFY(!wallet->sendRecipientList()->validationPending());
        if (reverse_maturity || external_lock) {
            if (external_lock) {
                UniValue input{UniValue::VOBJ}; input.pushKV("txid", selected.hash.GetHex()); input.pushKV("vout", selected.n);
                UniValue inputs{UniValue::VARR}; inputs.push_back(input);
                qmltest::Rpc(m_app, "lockunspent", qmltest::Json("[false," + inputs.write() + "]"), wallet->name());
                QCOMPARE(qmltest::Rpc(m_app, "listlockunspent", {}, wallet->name()).size(), size_t{1});
            } else {
                const auto tip = qmltest::Rpc(m_app, "getbestblockhash").get_str();
                qmltest::Rpc(m_app, "invalidateblock", qmltest::Json("[\"" + tip + "\"]"));
                const auto remaining = qmltest::Rpc(m_app, "listunspent", {}, wallet->name());
                QVERIFY(remaining.size() >= 1);
                for (const auto& input : remaining.getValues())
                    QVERIFY(input.find_value("txid").get_str() != selected.hash.GetHex() || input.find_value("vout").getInt<int>() != int(selected.n));
            }
            // For an external lock, try the original candidate before any
            // explicit catalog refresh: Native must check the public wallet.
            if (!external_lock) { coins->update(); QTRY_VERIFY(!coins->loading()); }
            QVERIFY2(!AwaitPreparation(*wallet), "An unavailable Manual input must not prepare with another wallet input.");
            QVERIFY(!wallet->currentTransaction());
            if (external_lock) {
                coins->update(); QTRY_VERIFY(!coins->loading());
                QTRY_VERIFY(coins->selectionReconciliationRequired());
            }
        } else {
            auto* other = qmltest::CreateWallet(m_app, "manual-other"); QVERIFY(other);
            QVERIFY(!other->coinsListModel()->manualInputSelection());
            QVERIFY(qmltest::Click(m_app, "walletBadge"));
            QVERIFY(qmltest::WaitForVisible(m_app, "walletSelectPopup"));
            QVERIFY(qmltest::Click(m_app, "walletSelectItem_manual_original"));
            QCOMPARE(qmltest::WaitForWallet(m_app, "manual-original"), wallet);
            QVERIFY(qmltest::Click(m_app, "sendTabButton"));
            QTRY_COMPARE(qmltest::Find(m_app, "sendCoinControlPicker")->property("currentIndex").toInt(), 1);
            QCOMPARE(wallet->selectedCoins()->size(), size_t{1});
            QVERIFY(wallet->selectedCoins()->contains(selected));
            QVERIFY(coins->manualInputSelection());
            QCOMPARE(wallet->sendRecipientList()->currentRecipient()->address()->address(), address);
            QCOMPARE(wallet->sendRecipientList()->currentRecipient()->amount()->satoshi(), qint64{COIN});
            QTRY_VERIFY(!wallet->customFeeTargetPending());
            QVERIFY2(AwaitPreparation(*wallet), qPrintable(QStringLiteral("preparation=%1, error=%2, feeValid=%3, selected=%4")
                .arg(int(wallet->preparationState())).arg(wallet->transactionError()).arg(wallet->customFeeRateValid()).arg(wallet->selectedCoins()->size())));
            const auto transaction = wallet->currentTransaction()->getWtx(); QVERIFY(transaction);
            QCOMPARE(transaction->vin.size(), size_t{1}); QCOMPARE(transaction->vin[0].prevout, selected);
            QVERIFY(!other->coinsListModel()->manualInputSelection());
        }
        QVERIFY(coins->manualInputSelection());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{0});
    }

    void temporaryCoreLockUpgradeDoesNotReturnAfterUnlock()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "temporary-lock-upgrade"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto unspent = qmltest::Rpc(m_app, "listunspent", {}, wallet->name()); QVERIFY(unspent.size() >= 1);
        UniValue input{UniValue::VOBJ}; input.pushKV("txid", unspent[0].find_value("txid")); input.pushKV("vout", unspent[0].find_value("vout"));
        UniValue inputs{UniValue::VARR}; inputs.push_back(input);
        const QString id = QString::fromStdString(unspent[0].find_value("txid").get_str()) + QLatin1Char(':') + QString::number(unspent[0].find_value("vout").getInt<int>());
        qmltest::Rpc(m_app, "lockunspent", qmltest::Json("[false," + inputs.write() + ",false]"), wallet->name());
        auto* coins = wallet->coinsListModel(); QVERIFY(coins); coins->update(); QTRY_VERIFY(!coins->loading());
        QVERIFY(coins->setCoinsLocked({id}, true)); QTRY_VERIFY(!coins->lockPending());
        // The adapter refuses a persistence upgrade it cannot identify safely.
        QVERIFY(coins->lockError().contains(QStringLiteral("already locked")));
        QVERIFY(coins->lockOperation()->result() && coins->lockOperation()->result()->value);
        QVERIFY(coins->lockOperation()->result()->value->already_locked);
        QCOMPARE(coins->lockOperation()->result()->value->changed, size_t{0});
        QVERIFY(coins->setCoinsLocked({id}, false)); QTRY_VERIFY(!coins->lockPending());
        QVERIFY2(coins->lockError().isEmpty(), qPrintable(coins->lockError()));
        QCOMPARE(qmltest::Rpc(m_app, "listlockunspent", {}, wallet->name()).size(), size_t{0});
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        QPointer<WalletQmlModel> original{wallet}; controller->closeWallet("temporary-lock-upgrade");
        QTRY_VERIFY(original.isNull());
        controller->setSelectedWallet("temporary-lock-upgrade");
        QVERIFY(qmltest::WaitForOpenWallet(m_app, "temporary-lock-upgrade"));
        const auto after = qmltest::Rpc(m_app, "listlockunspent", {}, QStringLiteral("temporary-lock-upgrade"));
        QVERIFY2(after.empty(), "Core persisted a temporary-lock upgrade but did not erase it on successful unlock; this needs an upstream Core disposition.");
    }

    void emptyManualSelectionCannotBroadenToWalletInputs()
    {
        auto* destination = qmltest::CreateWallet(m_app, "empty_manual_destination");
        QVERIFY(destination);
        auto* wallet = qmltest::CreateWallet(m_app, "empty_manual_sender");
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        // One further block matures a second independent input.
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + qmltest::NewAddress(m_app, wallet->name()).toStdString() + "\"]"));
        const auto destination_address = qmltest::NewAddress(m_app, destination->name());
        QVERIFY(qmltest::Rpc(m_app, "listunspent", {}, wallet->name()).size() >= 2);

        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionPickerButton"));
        QVERIFY(qmltest::Click(m_app, "feeSelectionOption3"));
        QVERIFY(qmltest::SetText(m_app, "feeSelectionCustomRateInput", "1"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", destination_address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "1"));
        QTRY_VERIFY(!wallet->sendRecipientList()->validationPending());
        auto* picker = qmltest::Find(m_app, "sendCoinControlPicker");
        QVERIFY(picker);
        QVERIFY(qmltest::Click(m_app, "sendCoinControlPickerOption_1"));
        QTRY_COMPARE(picker->property("currentIndex").toInt(), 1);

        auto* coins = wallet->coinsListModel();
        QVERIFY(coins);
        coins->update();
        QTRY_VERIFY(!coins->loading() && coins->rowCount() > 0);
        int selectable{-1};
        for (int row{0}; row < coins->rowCount(); ++row) {
            if (!coins->data(coins->index(row), CoinsListModel::LockedRole).toBool()) {
                selectable = row;
                break;
            }
        }
        QVERIFY(selectable >= 0);
        const auto selected_id = coins->data(coins->index(selectable), CoinsListModel::CoinIdRole).toString();
        QVERIFY(!selected_id.isEmpty());

        // Apply one real input through reachable production controls. Empty
        // Apply is disabled, so never force a click to manufacture that state.
        QVERIFY(qmltest::Click(m_app, "sendSelectInputsButton"));
        QTRY_VERIFY(qmltest::Find(m_app, "coinSelectionPopup")->property("opened").toBool());
        QVERIFY(qmltest::Click(m_app, qPrintable(QString{"coinSelectionItem_%1"}.arg(selectable))));
        QTRY_COMPARE(coins->selectedCoinsCount(), 1);
        QVERIFY(qmltest::Click(m_app, "coinSelectionDoneButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "coinSelectionPopup")->property("visible").toBool());
        QTRY_VERIFY(!wallet->coinSelectionPending());
        QTRY_COMPARE(wallet->selectedCoins()->size(), 1U);
        const auto selected = *wallet->selectedCoins()->begin();
        QCOMPARE(selected_id, QString::fromStdString(selected.hash.GetHex()) + QLatin1Char(':') + QString::number(selected.n));

        // An independent RPC writer spends exactly that input. At least one
        // other input remains, making wallet-wide fallback observably wrong.
        UniValue input{UniValue::VOBJ}; input.pushKV("txid", selected.hash.GetHex()); input.pushKV("vout", selected.n);
        UniValue inputs{UniValue::VARR}; inputs.push_back(input);
        const auto spent = qmltest::Rpc(m_app, "sendall", qmltest::Json(
            "{\"recipients\":[\"" + destination_address.toStdString() + "\"],\"fee_rate\":1,\"options\":{\"inputs\":" + inputs.write() + ",\"add_to_wallet\":false}}"), wallet->name());
        const auto hex = spent.find_value("hex").get_str();
        const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + hex + "\"]"));
        QCOMPARE(decoded.find_value("vin").size(), size_t{1});
        QCOMPARE(decoded.find_value("vin")[0].find_value("txid").get_str(), selected.hash.GetHex());
        QCOMPARE(decoded.find_value("vin")[0].find_value("vout").getInt<int>(), int(selected.n));
        const auto spent_txid = qmltest::Rpc(m_app, "sendrawtransaction", qmltest::Json("[\"" + hex + "\"]")).get_str();
        QVERIFY(!spent_txid.empty());
        const auto remaining = qmltest::Rpc(m_app, "listunspent", {}, wallet->name());
        QVERIFY(remaining.size() >= 1);
        for (const auto& output : remaining.getValues()) {
            QVERIFY(output.find_value("txid").get_str() != selected.hash.GetHex() || output.find_value("vout").getInt<int>() != int(selected.n));
        }
        coins->update();
        const auto selected_visible = [&] {
            for (int row{0}; row < coins->rowCount(); ++row)
                if (coins->data(coins->index(row), CoinsListModel::CoinIdRole).toString() == selected_id) return true;
            return false;
        };
        QTRY_VERIFY(!coins->loading() && !selected_visible());
        QTRY_VERIFY(!wallet->coinSelectionPending() && !wallet->feeEstimatePending());
        QCOMPARE(picker->property("currentIndex").toInt(), 1);

        auto* review = qmltest::Find(m_app, "sendReviewButton");
        auto* review_popup = qmltest::Find(m_app, "transactionReviewPopup");
        QVERIFY(review);
        QVERIFY(review_popup);
        if (review->property("enabled").toBool()) QVERIFY(qmltest::Click(m_app, "sendReviewButton"));
        const bool opened_review_with_wallet_inputs = QTest::qWaitFor(
            [&] { return review_popup->property("opened").toBool(); }, 2'000);
        if (opened_review_with_wallet_inputs) {
            QMetaObject::invokeMethod(review_popup, "close");
            wallet->discardCurrentTransaction();
            QTRY_VERIFY(!wallet->currentTransaction());
        }
        QVERIFY2(!opened_review_with_wallet_inputs,
                 "A spent explicit Manual input must not prepare using different wallet-wide inputs.");
        QVERIFY(!review->property("enabled").toBool());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), size_t{1});
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
        QVERIFY(qmltest::SecretTextLocations(m_app.window(), "fallback password").isEmpty());
        QVERIFY(wallet->isLocked());
        QVERIFY(qmltest::Click(m_app, "sendTransactionReviewSendButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendCompletePage"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 1U);
        QVERIFY(wallet->isLocked());
    }

    void avoidReusePolicyFiltersDirtyOnlyBalance_data()
    {
        QTest::addColumn<bool>("avoid_reuse");
        QTest::newRow("wallet-policy-enabled") << true;
        QTest::newRow("wallet-policy-disabled-control") << false;
    }

    void avoidReusePolicyFiltersDirtyOnlyBalance()
    {
        QFETCH(bool, avoid_reuse);
        auto* destination = qmltest::CreateWallet(m_app, avoid_reuse ? "avoid_destination" : "control_destination");
        QVERIFY(destination);
        const auto destination_address = qmltest::NewAddress(m_app, destination->name());

        const QString wallet_name = avoid_reuse ? "avoid_sender" : "control_sender";
        UniValue create_params{UniValue::VOBJ};
        create_params.pushKV("wallet_name", wallet_name.toStdString());
        create_params.pushKV("avoid_reuse", avoid_reuse);
        qmltest::Rpc(m_app, "createwallet", create_params);
        auto* wallet = qmltest::SelectOpenWalletForSetup(m_app, wallet_name);
        QVERIFY(wallet);

        const auto reused_address = qmltest::NewAddress(m_app, wallet_name);
        // Mine two mature outputs at this address: spending one dirties the
        // other, while its confirmed change remains clean. This establishes
        // the original dirty-only policy and independent clean-input lock.
        QElapsedTimer setup_time; setup_time.start();
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[102,\"" + reused_address.toStdString() + "\"]"));
        QTRY_COMPARE(qmltest::Rpc(m_app, "listunspent", {}, wallet_name).size(), 2U);
        qInfo() << "Avoid reuse mature outputs ready; elapsed ms" << setup_time.elapsed();

        // Spend from the repeated address once. Every remaining output at that
        // address is now dirty; the wallet's change output remains clean.
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + destination_address.toStdString() + "\",\"amount\":1,\"fee_rate\":1}"), wallet_name);
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + reused_address.toStdString() + "\"]"));
        qInfo() << "Avoid reuse spend confirmed; elapsed ms" << setup_time.elapsed();
        const auto wallet_info = qmltest::Rpc(m_app, "getwalletinfo", {}, wallet_name);
        QCOMPARE(wallet_info.find_value("avoid_reuse").get_bool(), avoid_reuse);

        const auto unspent = qmltest::Rpc(m_app, "listunspent", {}, wallet_name);
        UniValue clean_inputs{UniValue::VARR};
        size_t dirty_count{0};
        for (const auto& coin : unspent.getValues()) {
            if (coin.find_value("address").get_str() == reused_address.toStdString()) {
                ++dirty_count;
                if (avoid_reuse) QVERIFY(coin.find_value("reused").get_bool());
            } else {
                UniValue input{UniValue::VOBJ};
                input.pushKV("txid", coin.find_value("txid"));
                input.pushKV("vout", coin.find_value("vout"));
                clean_inputs.push_back(input);
            }
        }
        QVERIFY(dirty_count > 0);
        QVERIFY(!clean_inputs.empty());
        QVERIFY(qmltest::Rpc(m_app, "lockunspent", qmltest::Json("[false," + clean_inputs.write() + "]"), wallet_name).get_bool());

        wallet->coinsListModel()->update();
        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", destination_address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "1"));
        QTRY_VERIFY(wallet->availableSendBalanceKnown());
        QTRY_VERIFY(!wallet->feeEstimatePending());

        const auto* review = qmltest::Find(m_app, "sendReviewButton");
        QVERIFY(review);
        if (avoid_reuse) {
            QTRY_COMPARE(wallet->availableSendBalanceSatoshi(), qint64{0});
            QTRY_VERIFY(!review->property("enabled").toBool());
            QVERIFY(qmltest::Rpc(m_app, "getrawmempool").empty());
        } else {
            QTRY_VERIFY(wallet->availableSendBalanceSatoshi() > 0);
            QTRY_VERIFY(review->property("enabled").toBool());
        }
    }
};

QString SendPreparedWallet(const QString& selector)
{
    const auto function = selector.section(':', 0, 0);
    if (function == "customFeesRespectRelayMinimum") return "custom_fee";
    if (function == "coinSelectionCancelApplyAndLockReachTheWallet") return "integration";
    for (const auto* name : {"localizedTypedSendPreparesExactOutput",
                             "uppercaseBech32RecipientRemainsRecipientOutput",
                             "feePreviewLimitationKeepsCoreReviewAuthoritative",
                             "explicitPublicChangePolicyMatchesThePreview",
                             "presetFeesHonorRaisedCoreRequirements",
                             "reselectingCustomPreservesTheReviewedExplicitRate",
                             "oversizedMaximumPreservesCoreCauseAndRecoversWithFewerCoins",
                             "staleMaximumCannotReplaceAnOrdinaryAmountEdit",
                             "maximumHandoverClearsTheFormerRecipient",
                             "maximumHandoverNamesTheRecipientNeedingAttention",
                             "presetsAndCustomRateMakeNoBlockTargetPromise",
                             "sendAllReviewFollowsAGlobalDisplayUnitChange",
                             "leavingAnUntouchedAmountFieldKeepsMaximum",
                             "coinSelectionDialogCommitsOrDiscardsInputsAndRepricesTheMaximum",
                             "maximumAndSweepReviewRespectOwnershipAndSelection",
                             "fixedManualSweepWarnsBeforeUnlockAndBeforeBroadcast",
                             "multipleRecipientsShareTheMaximumAndKeepTheirDraftOnCancel"}) {
        if (function == name) return "sender";
    }
    return {};
}

QStringList SendTestArguments(const QString& selector)
{
    QStringList arguments{"-mintxfee=0.00001000", "-minrelaytxfee=0.00000100"};
    if (selector.endsWith(":respect-relay-minimum")) arguments = {"-mintxfee=0.00000100", "-minrelaytxfee=0.00001000"};
    if (selector.startsWith("presetFeesHonorRaisedCoreRequirements:wallet-min-")) arguments = {"-mintxfee=0.00002000", "-minrelaytxfee=0.00000100"};
    if (selector.startsWith("presetFeesHonorRaisedCoreRequirements:relay-min-")) arguments = {"-mintxfee=0.00000100", "-minrelaytxfee=0.00002000"};
    if (selector == "explicitPublicChangePolicyMatchesThePreview:overrides-configured-legacy") arguments.append("-changetype=legacy");
    // One cached change key is enough to exercise exhaustion and recovery.
    if (selector == "lockedHardenedDescriptorWalletRebuildsReviewAfterUnlock") arguments.append("-keypool=1");
    return arguments;
}

std::unique_ptr<QObject> CreateSendTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<SendIntegrationTests>(app);
}

#include <test_send.moc>
