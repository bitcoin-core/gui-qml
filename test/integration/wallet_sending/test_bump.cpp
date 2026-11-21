// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <test/mocks/messagebumptest.h>
#include <rpc/util.h>
#include <qml/models/options_model.h>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <set>

namespace {
std::set<std::string> Mempool(BitcoinQmlApplication& app)
{
    std::set<std::string> ids;
    const auto mempool = qmltest::Rpc(app, "getrawmempool");
    for (const auto& id : mempool.getValues()) ids.insert(id.get_str());
    return ids;
}
QString SendReplaceable(BitcoinQmlApplication& app, WalletQmlModel& wallet, const QString& destination)
{
    UniValue params{UniValue::VOBJ};
    params.pushKV("address", destination.toStdString());
    params.pushKV("amount", UniValue(UniValue::VNUM, "0.01"));
    params.pushKV("replaceable", true);
    params.pushKV("fee_rate", 1);
    return QString::fromStdString(qmltest::Rpc(app, "sendtoaddress", params, wallet.name()).get_str());
}
QString SpendWholeCoin(BitcoinQmlApplication& app, const QString& wallet, const UniValue& coin, const QString& address)
{
    UniValue input{UniValue::VOBJ};
    input.pushKV("txid", coin.find_value("txid")); input.pushKV("vout", coin.find_value("vout"));
    input.pushKV("sequence", int64_t{0xfffffffd});
    UniValue inputs{UniValue::VARR}; inputs.push_back(input);
    UniValue outputs{UniValue::VOBJ}; outputs.pushKV(address.toStdString(), coin.find_value("amount"));
    UniValue create{UniValue::VARR}; create.push_back(inputs); create.push_back(outputs);
    const auto raw = qmltest::Rpc(app, "createrawtransaction", create);
    UniValue options{UniValue::VOBJ}; options.pushKV("add_inputs", false); options.pushKV("fee_rate", 1);
    UniValue subtract{UniValue::VARR}; subtract.push_back(0); options.pushKV("subtractFeeFromOutputs", subtract);
    UniValue fund{UniValue::VARR}; fund.push_back(raw); fund.push_back(options);
    const auto funded = qmltest::Rpc(app, "fundrawtransaction", fund, wallet);
    UniValue sign{UniValue::VARR}; sign.push_back(funded.find_value("hex"));
    const auto signed_transaction = qmltest::Rpc(app, "signrawtransactionwithwallet", sign, wallet);
    if (!signed_transaction.find_value("complete").get_bool()) throw std::runtime_error("fixture signature incomplete");
    UniValue send{UniValue::VARR}; send.push_back(signed_transaction.find_value("hex"));
    return QString::fromStdString(qmltest::Rpc(app, "sendrawtransaction", send).get_str());
}
// A signer helper for one key family: enumerate either names the device or
// reports the device error Core turns into an exception.
struct FailingSigner {
    QTemporaryDir directory;
    QString state{directory.filePath("state.json")}, script{directory.filePath("signer.py")};
    bool write(const QString& path, const QByteArray& bytes) const { QFile file{path}; return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size(); }
    bool set(const QJsonObject& value) const { return write(state, QJsonDocument{value}.toJson()); }
    bool install() const {
        return directory.isValid() && write(script, R"py(import json, sys
from pathlib import Path
state = json.loads(Path(sys.argv[1]).read_text())
if "enumerate" in sys.argv[2:]:
    print(json.dumps([{"fingerprint": state["fingerprint"], "error": state["error"]} if state["error"] else {"fingerprint": state["fingerprint"], "model": "bump device"}]))
elif "getdescriptors" in sys.argv[2:]:
    print(json.dumps({"receive": state["receive"], "internal": state["internal"]}))
else:
    raise RuntimeError("unexpected signer command")
)py");
    }
    QString command() const { return QStandardPaths::findExecutable("python3") + QLatin1Char(' ') + script + QLatin1Char(' ') + state; }
};
QJsonObject SignerFamily(BitcoinQmlApplication& app, const QString& wallet)
{
    QJsonArray receive, internal; QString fingerprint;
    const auto descriptors = qmltest::Rpc(app, "listdescriptors", {}, wallet);
    for (const auto& entry : descriptors.find_value("descriptors").getValues()) {
        const auto descriptor = QString::fromStdString(entry.find_value("desc").get_str());
        fingerprint = descriptor.mid(descriptor.indexOf('[') + 1, 8);
        (entry.find_value("internal").get_bool() ? internal : receive).push_back(descriptor);
    }
    return {{"fingerprint", fingerprint}, {"receive", receive}, {"internal", internal}, {"error", QString{}}};
}
} // namespace

class FeeBumpIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
public:
    explicit FeeBumpIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}
private Q_SLOTS:
    void counterpartySpendingThePaymentInvalidatesTheOpenReview()
    {
        auto* destination = qmltest::CreateWallet(m_app, "descendant_destination"); QVERIFY(destination);
        auto* wallet = qmltest::CreateWallet(m_app, "descendant_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto address = qmltest::NewAddress(m_app, destination->name());
        const auto original = SendReplaceable(m_app, *wallet, address);
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        const auto received = qmltest::Rpc(m_app, "listunspent", qmltest::Json("[0]"), destination->name());
        UniValue payment;
        for (const auto& coin : received.getValues())
            if (coin.find_value("txid").get_str() == original.toStdString()) payment = coin;
        QVERIFY(payment.isObject());
        const auto child = SpendWholeCoin(m_app, destination->name(), payment, qmltest::NewAddress(m_app, destination->name()));
        const auto before = Mempool(m_app); QVERIFY(before.contains(original.toStdString())); QVERIFY(before.contains(child.toStdString()));
        QVERIFY(!AwaitBumpCommit(*bump));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsRevalidation);
        QVERIFY(!bump->errorText().isEmpty());
        const auto outcome = bump->commitResult(); QVERIFY(outcome); QVERIFY(!outcome->committed);
        QCOMPARE(outcome->error, walletbump::Error::Ineligible);
        QCOMPARE(Mempool(m_app), before);
    }
    void independentSpendOfAddedInputCannotReplaceTheUnrelatedPayment()
    {
        auto* destination = qmltest::CreateWallet(m_app, "added_input_destination"); QVERIFY(destination);
        auto* wallet = qmltest::CreateWallet(m_app, "added_input_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto mining = qmltest::NewAddress(m_app, wallet->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        const auto coins = qmltest::Rpc(m_app, "listunspent", qmltest::Json("[101]"), wallet->name());
        QCOMPARE(coins.size(), size_t{2});
        const auto address = qmltest::NewAddress(m_app, destination->name());
        const auto original = SpendWholeCoin(m_app, wallet->name(), coins[0], address);
        const auto remaining = qmltest::Rpc(m_app, "listunspent", qmltest::Json("[101]"), wallet->name());
        QCOMPARE(remaining.size(), size_t{1});
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        const auto preparation = bump->preparationOperation()->result(); QVERIFY(preparation && preparation->value);
        const auto review = preparation->value->review; QVERIFY(review);
        QCOMPARE(review->added_inputs.size(), size_t{1});
        QCOMPARE(review->added_inputs.front().hash.GetHex(), remaining[0].find_value("txid").get_str());
        QCOMPARE(review->added_inputs.front().n, remaining[0].find_value("vout").getInt<uint32_t>());
        const auto unrelated = SpendWholeCoin(m_app, wallet->name(), remaining[0], address);
        const auto before = Mempool(m_app); QVERIFY(before.contains(original.toStdString())); QVERIFY(before.contains(unrelated.toStdString()));
        QVERIFY(!AwaitBumpCommit(*bump));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsRevalidation);
        const auto outcome = bump->commitResult(); QVERIFY(outcome); QVERIFY(!outcome->committed);
        QCOMPARE(outcome->error, walletbump::Error::Ineligible);
        QCOMPARE(Mempool(m_app), before);
    }
    void futureLockRemainsInReviewAndIndependentAbandonmentInvalidatesIt()
    {
        auto* destination = qmltest::CreateWallet(m_app, "abandon_destination"); QVERIFY(destination);
        auto* wallet = qmltest::CreateWallet(m_app, "abandon_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const int height = qmltest::Rpc(m_app, "getblockcount").getInt<int>();
        UniValue outputs{UniValue::VOBJ};
        outputs.pushKV(qmltest::NewAddress(m_app, destination->name()).toStdString(), UniValue{UniValue::VNUM, "0.01"});
        UniValue options{UniValue::VOBJ}; options.pushKV("locktime", height + 100);
        UniValue params{UniValue::VOBJ}; params.pushKV("outputs", outputs); params.pushKV("fee_rate", 1); params.pushKV("options", options);
        const auto sent = qmltest::Rpc(m_app, "send", params, wallet->name());
        QVERIFY(sent.find_value("complete").get_bool());
        const auto original = QString::fromStdString(sent.find_value("txid").get_str());
        QVERIFY(!Mempool(m_app).contains(original.toStdString()));
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        const auto preparation = bump->preparationOperation()->result(); QVERIFY(preparation && preparation->value);
        const auto review = preparation->value->review; QVERIFY(review);
        QCOMPARE(review->transaction.nLockTime, uint32_t(height + 100));
        qmltest::Rpc(m_app, "abandontransaction", qmltest::Json("[\"" + original.toStdString() + "\"]"), wallet->name());
        QVERIFY(!AwaitBumpCommit(*bump));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsRevalidation);
        const auto outcome = bump->commitResult(); QVERIFY(outcome); QVERIFY(!outcome->committed);
        QCOMPARE(outcome->error, walletbump::Error::Ineligible);
        QVERIFY(Mempool(m_app).empty());
    }
    void committedReplacementPreservesPaymentAndIsObservedByRpc()
    {
        auto* destination = qmltest::CreateWallet(m_app, "bump_destination"); QVERIFY(destination);
        const auto address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::CreateWallet(m_app, "bump_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto original = SendReplaceable(m_app, *wallet, address);
        QVERIFY(Mempool(m_app).contains(original.toStdString()));
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        QCOMPARE(bump->oldTxid(), original);
        const auto attempt = bump->attemptId();
        const auto commit = bump->requestConfirmFeeBump(); QVERIFY(commit);
        QVERIFY(!bump->requestConfirmFeeBump());
        QTRY_VERIFY(!bump->busy());
        QCOMPARE(bump->attemptId(), attempt); QCOMPARE(bump->commitId(), commit);
        QCOMPARE(bump->state(), BumpTransactionModel::Succeeded);
        const auto replacement = bump->newTxid(); QVERIFY(!replacement.isEmpty()); QVERIFY(replacement != original);
        const auto outcome = bump->commitResult(); QVERIFY(outcome); QVERIFY(outcome->committed);
        QCOMPARE(QString::fromStdString(outcome->original.GetHex()), original);
        QCOMPARE(QString::fromStdString(outcome->txid.GetHex()), replacement);
        QCOMPARE(outcome->record, walletbump::Record::Recorded);
        QCOMPARE(outcome->submission, walletbump::Submission::Mempool);
        QCOMPARE(outcome->relay, walletbump::Relay::Unknown);
        QCOMPARE(bump->walletRecordState(), BumpTransactionModel::WalletRecordRecorded);
        QCOMPARE(bump->walletSubmissionState(), BumpTransactionModel::WalletSubmissionMempool);
        QCOMPARE(bump->relayState(), BumpTransactionModel::RelayUnknown);
        const auto mempool = Mempool(m_app);
        QVERIFY(mempool.contains(replacement.toStdString())); QVERIFY(!mempool.contains(original.toStdString()));
        const auto transaction = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + replacement.toStdString() + "\"]"), wallet->name());
        QCOMPARE(transaction.find_value("replaces_txid").get_str(), original.toStdString());
        const auto previous = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + original.toStdString() + "\"]"), wallet->name());
        QCOMPARE(previous.find_value("replaced_by_txid").get_str(), replacement.toStdString());
        const auto decoded = qmltest::Rpc(m_app, "decoderawtransaction", qmltest::Json("[\"" + transaction.find_value("hex").get_str() + "\"]"));
        int matched{0};
        for (const auto& output : decoded.find_value("vout").getValues()) {
            const auto& script = output.find_value("scriptPubKey");
            if (script.find_value("address").isStr() && script.find_value("address").get_str() == address.toStdString()) {
                QCOMPARE(AmountFromValue(output.find_value("value")), CAmount{1'000'000}); ++matched;
            }
        }
        QCOMPARE(matched, 1);
    }
    // #977: a signer exception during Speed Up is a definite pre-commit
    // failure. It must not leave the bump model Uncertain with no exit.
    void failingExternalSignerEndsSpeedUpWithARetryableFailure()
    {
        auto* keys = qmltest::CreateWallet(m_app, "signer_keys"); QVERIFY(keys);
        auto family = SignerFamily(m_app, keys->name());
        FailingSigner signer; QVERIFY(signer.install()); QVERIFY(signer.set(family));
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController"); QVERIFY(controller);
        auto* discovery = controller->signers(); QVERIFY(discovery); QTRY_VERIFY(discovery->configurationKnown());
        discovery->editPath(signer.command()); QVERIFY(discovery->apply()); QTRY_VERIFY(!discovery->pending()); QVERIFY(discovery->fresh());
        UniValue create{UniValue::VOBJ}; create.pushKV("wallet_name", "signer_device"); create.pushKV("disable_private_keys", true); create.pushKV("external_signer", true);
        qmltest::Rpc(m_app, "createwallet", create);
        auto* wallet = qmltest::SelectOpenWalletForSetup(m_app, "signer_device"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *keys)); QTRY_VERIFY(wallet->balanceSatoshi() > 0);
        // The key-holding twin signs the original; the device wallet owns its inputs.
        const auto original = SendReplaceable(m_app, *keys, qmltest::NewAddress(m_app, keys->name()));
        // The device wallet learns of the payment from Core's notification
        // queue; until then gettransaction throws rather than returning.
        QVERIFY(QTest::qWaitFor([&] {
            try { return qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + original.toStdString() + "\"]"), wallet->name()).find_value("txid").isStr(); }
            catch (const std::runtime_error&) { return false; }
        }, 30'000));
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        family["error"] = "device unavailable"; QVERIFY(signer.set(family));
        QVERIFY(!AwaitBumpCommit(*bump));
        QCOMPARE(bump->state(), BumpTransactionModel::Failed);
        QVERIFY2(!bump->errorText().isEmpty(), "The signer failure must be reported.");
        const auto result = bump->commitResult(); QVERIFY(result); QVERIFY(!result->committed); QVERIFY(!result->commit_attempted);
        QCOMPARE(result->error, walletbump::Error::Sign);
        QVERIFY(bump->newTxid().isEmpty());
        QVERIFY(Mempool(m_app) == std::set<std::string>{original.toStdString()});
        // Speed Up can be closed and prepared again for the same payment.
        bump->reset(); QCOMPARE(bump->state(), BumpTransactionModel::Idle);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
    }
    // #1106: with sats selected, Speed Up opened from Activity shows Core's original and replacement fees in sats,
    // and follows a global display-unit change while it is open.
    void speedUpFeesFollowTheGlobalDisplayUnit()
    {
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options);
        const auto restore = qScopeGuard([options] { options->setDisplayUnit(0); });
        auto* destination = qmltest::CreateWallet(m_app, "unit_destination"); QVERIFY(destination);
        const auto address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::CreateWallet(m_app, "unit_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto original = SendReplaceable(m_app, *wallet, address);
        const auto old_fee = AmountFromValue(qmltest::Rpc(m_app, "getmempoolentry", qmltest::Json("[\"" + original.toStdString() + "\"]")).find_value("fees").find_value("base"));
        QVERIFY(old_fee > 0);
        options->setDisplayUnit(3); QTRY_COMPARE(wallet->displayUnit(), 3);
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", original));
        auto* row = qmltest::WaitForVisible(m_app, qPrintable("activityItem_" + original)); QVERIFY(row);
        QVERIFY(qmltest::Click(qmltest::Find(row, "activityRowOpenButton")));
        QVERIFY(qmltest::WaitForVisible(m_app, "activityDetailsPage"));
        QVERIFY(qmltest::Click(m_app, "speedUpBannerPrimaryButton"));
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QTRY_COMPARE_WITH_TIMEOUT(bump->state(), BumpTransactionModel::NeedsConfirmation, 10'000);
        QTRY_VERIFY(qmltest::Find(m_app, "speedUpOverlay")->property("opened").toBool());
        const auto preparation = bump->preparationOperation()->result(); QVERIFY(preparation && preparation->value && preparation->value->review);
        const auto& review = *preparation->value->review; QCOMPARE(review.old_fee, old_fee); QVERIFY(review.new_fee > old_fee);
        const auto sats = [](CAmount amount) { return QLocale().toString(qlonglong{amount}) + " sats"; };
        const auto btc = [](CAmount amount) { return QStringLiteral("0") + QLocale().decimalPoint() + QString::number(amount).rightJustified(8, '0') + " BTC"; };
        auto* original_row = qmltest::Find(m_app, "speedUpOriginalFeeRow"); auto* new_row = qmltest::Find(m_app, "speedUpNewFeeRow");
        QVERIFY(original_row && new_row);
        QTRY_COMPARE(original_row->property("value").toString(), sats(review.old_fee));
        QCOMPARE(new_row->property("value").toString(), sats(review.new_fee));
        options->setDisplayUnit(0);
        QTRY_COMPARE(original_row->property("value").toString(), btc(review.old_fee));
        QCOMPARE(new_row->property("value").toString(), btc(review.new_fee));
        QVERIFY(qmltest::Click(m_app, "speedUpCloseButton"));
        QTRY_VERIFY(!qmltest::Find(m_app, "speedUpOverlay")->property("visible").toBool());
        QVERIFY(Mempool(m_app).contains(original.toStdString()));
    }
    void independentConfirmationInvalidatesReviewedReplacement()
    {
        auto* destination = qmltest::CreateWallet(m_app, "confirmed_destination"); QVERIFY(destination);
        const auto address = qmltest::NewAddress(m_app, destination->name());
        auto* wallet = qmltest::CreateWallet(m_app, "confirmed_sender"); QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto original = SendReplaceable(m_app, *wallet, address);
        auto* bump = wallet->bumpModel(); QVERIFY(bump);
        QVERIFY2(AwaitBumpPreparation(*bump, original, 1), qPrintable(bump->errorText()));
        // The real RPC state changes independently after review, before the
        // model dispatches its accepted sign/commit effect.
        const auto mining = qmltest::NewAddress(m_app, wallet->name());
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[1,\"" + mining.toStdString() + "\"]"));
        const auto transaction = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + original.toStdString() + "\"]"), wallet->name());
        QVERIFY(transaction.find_value("confirmations").getInt<int>() >= 1);
        QVERIFY(!AwaitBumpCommit(*bump));
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsRevalidation);
        QVERIFY(bump->newTxid().isEmpty());
        const auto result = bump->commitResult(); QVERIFY(result); QVERIFY(!result->committed);
        QCOMPARE(result->error, walletbump::Error::Ineligible);
        QVERIFY(!Mempool(m_app).contains(original.toStdString()));
    }
};

std::unique_ptr<QObject> CreateFeeBumpTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<FeeBumpIntegrationTests>(app);
}
#include <test_bump.moc>
