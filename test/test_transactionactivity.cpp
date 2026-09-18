// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/transactionactivity.h>
#include <qml/models/transactionflow.h>

#include <chainparams.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <script/solver.h>

#include <algorithm>
#include <utility>

#include <QSet>
#include <QtTest/QtTest>

Q_DECLARE_METATYPE(interfaces::WalletTx)
Q_DECLARE_METATYPE(TransactionActivity::Type)

namespace {
using Type = TransactionActivity::Type;
using Direction = TransactionAction::Direction;
using Source = TransactionAction::Source;

struct Input {
    CAmount amount;
    bool mine;
};

struct Output {
    CAmount amount;
    bool mine;
    bool change{false};
    bool data{false};
    unsigned char address_id{0};
};

interfaces::WalletTx MakeWalletTx(const std::vector<Input>& inputs, const std::vector<Output>& outputs, bool coinbase = false)
{
    CMutableTransaction tx;
    interfaces::WalletTx wtx{};
    for (size_t i{0}; i < inputs.size(); ++i) {
        tx.vin.emplace_back(coinbase ? COutPoint{} : COutPoint{Txid::FromUint256(uint256{static_cast<uint8_t>(i + 1)}), 0});
        wtx.txin_is_mine.push_back(inputs[i].mine);
        if (inputs[i].mine) wtx.debit += inputs[i].amount;
    }
    for (size_t i{0}; i < outputs.size(); ++i) {
        const auto& output = outputs[i];
        uint160 key;
        key.begin()[0] = output.address_id ? output.address_id : static_cast<unsigned char>(i + 1);
        const CTxDestination destination = output.data ? CTxDestination{CNoDestination{}} : CTxDestination{PKHash{key}};
        tx.vout.emplace_back(output.amount, output.data ? CScript{} << OP_RETURN : GetScriptForDestination(destination));
        wtx.txout_address.push_back(destination);
        wtx.txout_is_mine.push_back(output.mine);
        wtx.txout_address_is_mine.push_back(output.mine && !output.data);
        wtx.txout_is_change.push_back(output.change);
        if (output.mine) wtx.credit += output.amount;
        if (output.change) wtx.change += output.amount;
    }
    wtx.tx = MakeTransactionRef(std::move(tx));
    wtx.time = 1'789'200'000;
    wtx.is_coinbase = coinbase;
    return wtx;
}
} // namespace

class TransactionActivityTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void classifiesWalletActivity_data();
    void classifiesWalletActivity();
    void batchAmountsExcludeFeesAndChange();
    void internalOutputsAreNotDuplicatedOrHidden();
    void selfSendExcludesChangeAndKeepsRecipientIdentity();
    void incomingChangeAddressIsStillAReceipt();
    void mixedContributionDoesNotClaimForeignPayments();
    void mixedReceiptsRetainChange();
    void repeatedAddressKeepsDistinctOutputIdentities();
    void identitiesSurviveMetadataChanges();
    void dataOutputsDoNotBecomePayments();
    void valueBearingNonAddressOutputRemainsVisible();
    void knownZeroFeeDiffersFromUnknownFee();
    void ignoresTransactionsWithoutWalletOwnership();
    void rejectsIncompleteSnapshots();
    void flowKeepsAllInputsOutputsAndUnknownAmounts();
    void flowPreviewCollapsesOnlyHiddenOutputs_data();
    void flowPreviewCollapsesOnlyHiddenOutputs();
    void flowHandlesCoinbaseDataOutputsAndZeroFees();
    void flowDecodesDataOutputs_data();
    void flowDecodesDataOutputs();
    void flowIncludesSerializedTransactionMetadata();
};

void TransactionActivityTests::initTestCase()
{
    SelectParams(ChainType::REGTEST);
}

void TransactionActivityTests::flowPreviewCollapsesOnlyHiddenOutputs_data()
{
    QTest::addColumn<int>("count");
    QTest::addColumn<int>("owned");
    QTest::addColumn<int>("visible");
    QTest::newRow("below-threshold") << 9 << 2 << 9;
    QTest::newRow("threshold") << 10 << 2 << 10;
    QTest::newRow("collapsed") << 11 << 2 << 3;
    QTest::newRow("large-payment") << 1001 << 1 << 2;
    QTest::newRow("all-external") << 11 << 0 << 1;
    QTest::newRow("all-owned") << 11 << 11 << 11;
    QTest::newRow("single-external") << 11 << 10 << 11;
}

void TransactionActivityTests::flowPreviewCollapsesOnlyHiddenOutputs()
{
    QFETCH(int, count);
    QFETCH(int, owned);
    QFETCH(int, visible);
    std::vector<Output> outputs(count, {1'000, false});
    for (int i = 0; i < owned; ++i) outputs[i].mine = true;
    const auto wtx = MakeWalletTx({{count * 1'000 + 500, false}}, outputs);
    const auto preview = BuildTransactionFlow(wtx, {}, true);
    const auto entries = preview.value("outputs").toList();
    QCOMPARE(preview.value("outputCount").toInt(), count);
    QCOMPARE(entries.size(), visible);
    qint64 total{0};
    int wallet_outputs{0};
    for (const auto& value : entries) {
        const auto entry = value.toMap();
        total += entry.value("amountSat").toLongLong();
        if (entry.value("ownership") == "wallet") {
            ++wallet_outputs;
            const auto index = entry.value("index").toInt();
            QCOMPARE(entry.value("id").toString(), QStringLiteral("output:%1").arg(index));
            QVERIFY(DecodeDestination(entry.value("address").toString().toStdString()) == wtx.txout_address[index]);
        }
        if (entry.value("kind") == "output-group") {
            QCOMPARE(entry.value("outputCount").toInt(), count - owned);
            QVERIFY(entry.value("address").toString().isEmpty());
        }
    }
    QCOMPARE(wallet_outputs, owned);
    QCOMPARE(total, qint64(count) * 1'000);
    QVERIFY(!preview.value("feeKnown").toBool());
    QVERIFY(!preview.value("complete").toBool());
}

void TransactionActivityTests::flowKeepsAllInputsOutputsAndUnknownAmounts()
{
    const auto tx = MakeWalletTx({{100'000, true}, {50'000, false}},
        {{49'000, true, true}, {100'000, false}});
    auto flow = BuildTransactionFlow(tx, {CTxOut{100'000, CScript{}}, std::nullopt});
    const auto inputs = flow.value("inputs").toList();
    const auto outputs = flow.value("outputs").toList();
    QCOMPARE(inputs.size(), 2);
    QCOMPARE(outputs.size(), 2);
    QCOMPARE(inputs[0].toMap().value("amountSat").toLongLong(), 100'000);
    QVERIFY(!inputs[1].toMap().value("amountKnown").toBool());
    QVERIFY(inputs[1].toMap().value("amountSat").isNull());
    QCOMPARE(inputs[1].toMap().value("ownership").toString(), QString("external"));
    QVERIFY(outputs[0].toMap().value("isChange").toBool());
    QCOMPARE(outputs[1].toMap().value("amountSat").toLongLong(), 100'000);
    QVERIFY(!flow.value("complete").toBool());
    QVERIFY(!flow.value("feeKnown").toBool());
    QVERIFY(flow.value("feeSat").isNull());
    flow = BuildTransactionFlow(tx, {CTxOut{100'000, CScript{}}, CTxOut{50'000, CScript{}}});
    QVERIFY(flow.value("complete").toBool());
    QCOMPARE(flow.value("totalInputSat").toLongLong(), 150'000);
    QCOMPARE(flow.value("feeSat").toLongLong(), 1'000);
    QVERIFY(flow.value("virtualSize").toLongLong() > 0);

    // Even when an individual wallet prevout cannot be resolved, its cached
    // aggregate debit can give us the fee without fabricating input widths.
    const auto send = MakeWalletTx({{100'000, true}}, {{70'000, false}, {29'000, true, true}});
    flow = BuildTransactionFlow(send, {});
    QVERIFY(flow.value("feeKnown").toBool());
    QCOMPARE(flow.value("feeSat").toLongLong(), 1'000);
    QVERIFY(!flow.value("complete").toBool());
    QVERIFY(flow.value("totalInputSat").isNull());
    QCOMPARE(flow.value("outputs").toList().size(), 2);
}

void TransactionActivityTests::flowHandlesCoinbaseDataOutputsAndZeroFees()
{
    auto tx = MakeWalletTx({{100'000, true}}, {{100'000, false}, {0, false, false, true}});
    auto flow = BuildTransactionFlow(tx, {CTxOut{100'000, CScript{}}});
    QVERIFY(flow.value("feeKnown").toBool());
    QCOMPARE(flow.value("feeSat").toLongLong(), 0);
    QCOMPARE(flow.value("outputs").toList().size(), 2);
    QCOMPARE(flow.value("outputs").toList()[1].toMap().value("kind").toString(), QString("data"));
    tx = MakeWalletTx({{0, false}}, {{50 * COIN, true}}, true);
    flow = BuildTransactionFlow(tx, {});
    QVERIFY(flow.value("coinbase").toBool());
    QVERIFY(flow.value("complete").toBool());
    QVERIFY(!flow.value("feeKnown").toBool());
    QCOMPARE(flow.value("inputs").toList()[0].toMap().value("kind").toString(), QString("coinbase"));
    QCOMPARE(flow.value("totalInputSat").toLongLong(), 50 * COIN);
    tx.txout_is_mine.clear();
    QVERIFY(BuildTransactionFlow(tx, {}).isEmpty());
}

void TransactionActivityTests::flowIncludesSerializedTransactionMetadata()
{
    auto wtx = MakeWalletTx({{100'000, true}}, {{99'000, false}});
    CMutableTransaction tx{*wtx.tx};
    tx.version = 3;
    tx.nLockTime = 840'000;
    tx.vin[0].nSequence = 0xfffffffd;
    tx.vin[0].scriptWitness.stack = {{0x01, 0x02, 0x03}};
    tx.vout[0].scriptPubKey = CScript{} << OP_TRUE;
    wtx.tx = MakeTransactionRef(tx);
    // 61 stripped bytes + 2 marker/flag bytes + 5 witness bytes.
    const auto flow = BuildTransactionFlow(wtx, {});
    QCOMPARE(flow.value("size").toLongLong(), 68);
    QCOMPARE(flow.value("weight").toLongLong(), 251);
    QCOMPARE(flow.value("virtualSize").toLongLong(), 63);
    QCOMPARE(flow.value("version").toLongLong(), 3);
    QCOMPARE(flow.value("lockTime").toLongLong(), 840'000);
    QVERIFY(flow.value("signalsRbf").toBool());
    QVERIFY(!flow.value("complete").toBool());
}

void TransactionActivityTests::flowDecodesDataOutputs_data()
{
    QTest::addColumn<QByteArray>("script_hex");
    QTest::addColumn<QString>("text");
    QTest::addColumn<QString>("payload_hex");
    QTest::newRow("text") << QByteArray{"6a0b48656c6c6f20776f726c64"} << QStringLiteral("Hello world") << QStringLiteral("48656c6c6f20776f726c64");
    QTest::newRow("unicode") << QByteArray{"6a0841e299a5f09f9492"}
        << QString::fromUtf8(QByteArray::fromHex("41e299a5f09f9492")) << QStringLiteral("41e299a5f09f9492");
    QTest::newRow("multiple_pushes") << QByteArray{"6a0548656c6c6f05576f726c64"} << QStringLiteral("Hello\nWorld") << QStringLiteral("48656c6c6f 576f726c64");
    QTest::newRow("pushdata1") << QByteArray{"6a4c50"} + QByteArray(80, 'x').toHex() << QString(80, 'x') << QString::fromLatin1(QByteArray(80, 'x').toHex());
    QTest::newRow("newlines") << QByteArray{"6a03610a62"} << QStringLiteral("a\nb") << QStringLiteral("610a62");
    QTest::newRow("empty") << QByteArray{"6a"} << QStringLiteral("") << QStringLiteral("");
    QTest::newRow("empty_push") << QByteArray{"6a00"} << QStringLiteral("") << QStringLiteral("");
    QTest::newRow("binary") << QByteArray{"6a0400ff0102"} << QString{} << QStringLiteral("00ff0102");
    QTest::newRow("control_character") << QByteArray{"6a03610062"} << QString{} << QStringLiteral("610062");
    QTest::newRow("invalid_utf8") << QByteArray{"6a02c328"} << QString{} << QStringLiteral("c328");
    QTest::newRow("truncated_push") << QByteArray{"6a4c054142"} << QString{} << QString{};
    QTest::newRow("other_opcodes") << QByteArray{"6a51026162"} << QString{} << QString{};
}

void TransactionActivityTests::flowDecodesDataOutputs()
{
    QFETCH(QByteArray, script_hex);
    QFETCH(QString, text);
    QFETCH(QString, payload_hex);
    auto wtx = MakeWalletTx({{100'000, true}}, {{99'000, false}, {0, false, false, true}});
    CMutableTransaction tx{*wtx.tx};
    const auto bytes = QByteArray::fromHex(script_hex);
    tx.vout[1].scriptPubKey = CScript(bytes.begin(), bytes.end());
    wtx.tx = MakeTransactionRef(tx);
    const auto outputs = BuildTransactionFlow(wtx, {}).value("outputs").toList();
    QVERIFY(!outputs[0].toMap().contains("dataType"));
    const auto data = outputs[1].toMap();
    QCOMPARE(data.value("kind").toString(), QStringLiteral("data"));
    QCOMPARE(data.value("dataType").toString(), QStringLiteral("OP_RETURN"));
    QCOMPARE(data.value("scriptHex").toString(), QString::fromLatin1(script_hex));
    QCOMPARE(data.contains("dataText"), !text.isNull());
    QCOMPARE(data.contains("dataHex"), !payload_hex.isNull());
    if (!text.isNull()) QCOMPARE(data.value("dataText").toString(), text);
    if (!payload_hex.isNull()) QCOMPARE(data.value("dataHex").toString(), payload_hex);
}

void TransactionActivityTests::classifiesWalletActivity_data()
{
    QTest::addColumn<interfaces::WalletTx>("wtx");
    QTest::addColumn<Type>("type");
    QTest::addColumn<qint64>("impact");
    QTest::addColumn<qint64>("fee"); // -1 means unknown/not applicable.
    QTest::addColumn<int>("sends");
    QTest::addColumn<int>("receives");
    QTest::addColumn<int>("internal");

    QTest::newRow("send-with-change")
        << MakeWalletTx({{100'000, true}}, {{70'000, false}, {29'000, true, true}})
        << Type::Send << qint64{-71'000} << qint64{1'000} << 1 << 0 << 0;
    QTest::newRow("multiple-owned-inputs-are-not-multiple-actions")
        << MakeWalletTx({{100'000, true}, {50'000, true}}, {{120'000, false}, {29'000, true, true}})
        << Type::Send << qint64{-121'000} << qint64{1'000} << 1 << 0 << 0;
    QTest::newRow("batch-send")
        << MakeWalletTx({{100'000, true}}, {{60'000, false}, {30'000, false}, {9'000, true, true}})
        << Type::Multiple << qint64{-91'000} << qint64{1'000} << 2 << 0 << 0;
    QTest::newRow("receive")
        << MakeWalletTx({{100'000, false}}, {{20'000, true}, {79'000, false}})
        << Type::Receive << qint64{20'000} << qint64{-1} << 0 << 1 << 0;
    QTest::newRow("multiple-foreign-inputs-are-not-multiple-actions")
        << MakeWalletTx({{100'000, false}, {100'000, false}}, {{10'000, true}, {189'000, false}})
        << Type::Receive << qint64{10'000} << qint64{-1} << 0 << 1 << 0;
    QTest::newRow("multiple-receives")
        << MakeWalletTx({{100'000, false}}, {{20'000, true}, {30'000, true}, {49'000, false}})
        << Type::Multiple << qint64{50'000} << qint64{-1} << 0 << 2 << 0;
    QTest::newRow("consolidation-to-change-address")
        << MakeWalletTx({{60'000, true}, {40'000, true}}, {{99'000, true, true}})
        << Type::Consolidation << qint64{-1'000} << qint64{1'000} << 0 << 0 << 1;
    QTest::newRow("consolidation-to-multiple-outputs")
        << MakeWalletTx({{40'000, true}, {30'000, true}, {30'000, true}}, {{50'000, true}, {49'000, true}})
        << Type::Consolidation << qint64{-1'000} << qint64{1'000} << 0 << 0 << 2;
    QTest::newRow("consolidation-to-recipient-and-change")
        << MakeWalletTx({{40'000, true}, {30'000, true}, {30'000, true}}, {{50'000, true}, {49'000, true, true}})
        << Type::Consolidation << qint64{-1'000} << qint64{1'000} << 0 << 0 << 2;
    QTest::newRow("consolidation-ignores-data-outputs")
        << MakeWalletTx({{60'000, true}, {40'000, true}}, {{99'000, true}, {0, false, false, true}})
        << Type::Consolidation << qint64{-1'000} << qint64{1'000} << 0 << 0 << 1;
    QTest::newRow("self-send-with-change")
        << MakeWalletTx({{100'000, true}}, {{60'000, true}, {39'000, true, true}})
        << Type::InternalTransfer << qint64{-1'000} << qint64{1'000} << 0 << 0 << 1;
    QTest::newRow("split-to-two-recipients")
        << MakeWalletTx({{100'000, true}}, {{60'000, true}, {39'000, true}})
        << Type::Split << qint64{-1'000} << qint64{1'000} << 0 << 0 << 2;
    QTest::newRow("split-to-two-recipients-with-change")
        << MakeWalletTx({{100'000, true}}, {{30'000, true}, {30'000, true}, {39'000, true, true}})
        << Type::Split << qint64{-1'000} << qint64{1'000} << 0 << 0 << 3;
    QTest::newRow("split-with-multiple-inputs")
        << MakeWalletTx({{60'000, true}, {40'000, true}}, {{30'000, true}, {30'000, true}, {39'000, true}})
        << Type::Split << qint64{-1'000} << qint64{1'000} << 0 << 0 << 3;
    QTest::newRow("one-to-one-internal-transfer")
        << MakeWalletTx({{100'000, true}}, {{99'000, true}})
        << Type::InternalTransfer << qint64{-1'000} << qint64{1'000} << 0 << 0 << 1;
    QTest::newRow("self-send-with-multiple-inputs-and-change")
        << MakeWalletTx({{60'000, true}, {40'000, true}}, {{40'000, true}, {59'000, true, true}})
        << Type::InternalTransfer << qint64{-1'000} << qint64{1'000} << 0 << 0 << 1;
    QTest::newRow("consolidation-to-receive-address")
        << MakeWalletTx({{60'000, true}, {40'000, true}}, {{99'000, true}})
        << Type::Consolidation << qint64{-1'000} << qint64{1'000} << 0 << 0 << 1;
    QTest::newRow("many-to-many-internal-transfer")
        << MakeWalletTx({{60'000, true}, {40'000, true}}, {{40'000, true}, {59'000, true}})
        << Type::InternalTransfer << qint64{-1'000} << qint64{1'000} << 0 << 0 << 2;
    QTest::newRow("external-payment-and-explicit-internal-output")
        << MakeWalletTx({{100'000, true}}, {{30'000, false}, {10'000, true}, {59'000, true, true}})
        << Type::Multiple << qint64{-31'000} << qint64{1'000} << 1 << 0 << 1;
    QTest::newRow("mixed-net-receive")
        << MakeWalletTx({{100'000, true}, {50'000, false}}, {{119'000, true}, {30'000, false}})
        << Type::Multiple << qint64{19'000} << qint64{-1} << 1 << 1 << 0;
    QTest::newRow("mixed-net-send")
        << MakeWalletTx({{100'000, true}, {50'000, false}}, {{49'000, true}, {100'000, false}})
        << Type::Multiple << qint64{-51'000} << qint64{-1} << 1 << 1 << 0;
    QTest::newRow("mixed-zero-wallet-impact")
        << MakeWalletTx({{100'000, true}, {50'000, false}}, {{100'000, true}, {49'000, false}})
        << Type::Multiple << qint64{0} << qint64{-1} << 1 << 1 << 0;
    QTest::newRow("mixed-inputs-with-only-one-wallet-action")
        << MakeWalletTx({{100'000, true}, {50'000, false}}, {{149'000, false}})
        << Type::Send << qint64{-100'000} << qint64{-1} << 1 << 0 << 0;
    QTest::newRow("coinbase")
        << MakeWalletTx({{0, false}}, {{312'500'000, true}}, true)
        << Type::Mined << qint64{312'500'000} << qint64{-1} << 0 << 1 << 0;
    QTest::newRow("coinbase-with-multiple-wallet-outputs")
        << MakeWalletTx({{0, false}}, {{100'000'000, true}, {212'500'000, true}}, true)
        << Type::Mined << qint64{312'500'000} << qint64{-1} << 0 << 2 << 0;
    QTest::newRow("fee-only-transaction-keeps-parent")
        << MakeWalletTx({{1'000, true}}, {{0, false, false, true}})
        << Type::Other << qint64{-1'000} << qint64{1'000} << 0 << 0 << 0;
}

void TransactionActivityTests::classifiesWalletActivity()
{
    QFETCH(interfaces::WalletTx, wtx);
    QFETCH(Type, type);
    QFETCH(qint64, impact);
    QFETCH(qint64, fee);
    QFETCH(int, sends);
    QFETCH(int, receives);
    QFETCH(int, internal);

    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity.has_value());
    QCOMPARE(activity->type, type);
    QCOMPARE(activity->walletImpact(), impact);
    QCOMPARE(activity->fee.has_value(), fee >= 0);
    if (fee >= 0) QCOMPARE(*activity->fee, fee);
    QCOMPARE(activity->actions.size(), size_t(sends + receives + internal));
    const auto count = [&](Direction direction) {
        return std::count_if(activity->actions.begin(), activity->actions.end(),
            [direction](const TransactionAction& action) { return action.direction == direction; });
    };
    QCOMPARE(count(Direction::Send), sends);
    QCOMPARE(count(Direction::Receive), receives);
    QCOMPARE(count(Direction::Internal), internal);
}

void TransactionActivityTests::batchAmountsExcludeFeesAndChange()
{
    // Put change first so it cannot accidentally absorb the fee or become a child.
    const auto wtx = MakeWalletTx({{100'000, true}}, {{9'000, true, true}, {60'000, false}, {30'000, false}});
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->actions.size(), size_t{2});
    QCOMPARE(activity->actions[0].output_index, 1);
    QCOMPARE(activity->actions[0].amount, CAmount{60'000});
    QVERIFY(DecodeDestination(activity->actions[0].address.toStdString()) == wtx.txout_address[1]);
    QCOMPARE(activity->actions[1].output_index, 2);
    QCOMPARE(activity->actions[1].amount, CAmount{30'000});
    QVERIFY(DecodeDestination(activity->actions[1].address.toStdString()) == wtx.txout_address[2]);
    QCOMPARE(activity->walletImpact(), CAmount{-91'000});
    QVERIFY(activity->fee.has_value());
    QCOMPARE(*activity->fee, CAmount{1'000});
}

void TransactionActivityTests::internalOutputsAreNotDuplicatedOrHidden()
{
    const auto wtx = MakeWalletTx({{100'000, true}}, {{60'000, true, true}, {39'000, true, true}});
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->type, Type::Split);
    QCOMPARE(activity->actions.size(), size_t{2});
    QCOMPARE(activity->actions[0].amount, CAmount{60'000});
    QCOMPARE(activity->actions[1].amount, CAmount{39'000});
    for (const auto& action : activity->actions) QCOMPARE(action.direction, Direction::Internal);
    QCOMPARE(activity->walletImpact(), CAmount{-1'000});
}

void TransactionActivityTests::selfSendExcludesChangeAndKeepsRecipientIdentity()
{
    // Change and data precede the explicit receive address in the transaction.
    const auto wtx = MakeWalletTx({{100'000, true}},
        {{39'000, true, true}, {0, false, false, true}, {60'000, true}});
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->type, Type::InternalTransfer);
    QCOMPARE(activity->actions.size(), size_t{1});
    const auto& recipient = activity->actions.front();
    QCOMPARE(recipient.direction, Direction::Internal);
    QCOMPARE(recipient.output_index, 2);
    QCOMPARE(recipient.id, activity->txid + ":output:2");
    QCOMPARE(recipient.amount, CAmount{60'000});
    QVERIFY(DecodeDestination(recipient.address.toStdString()) == wtx.txout_address[2]);
    QCOMPARE(activity->walletImpact(), CAmount{-1'000});
    QVERIFY(activity->fee.has_value());
    QCOMPARE(*activity->fee, CAmount{1'000});
}

void TransactionActivityTests::incomingChangeAddressIsStillAReceipt()
{
    const auto wtx = MakeWalletTx({{21'000, false}}, {{20'000, true, true}});
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->type, Type::Receive);
    QCOMPARE(activity->actions.size(), size_t{1});
    QCOMPARE(activity->actions[0].amount, CAmount{20'000});
    QCOMPARE(activity->actions[0].direction, Direction::Receive);
}

void TransactionActivityTests::mixedContributionDoesNotClaimForeignPayments()
{
    const auto wtx = MakeWalletTx({{60'000, true}, {50'000, false}, {40'000, true}}, {{100'000, false}, {49'000, true}});
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->type, Type::Multiple);
    QCOMPARE(activity->actions.size(), size_t{2});
    const auto& contribution = activity->actions[0];
    QCOMPARE(contribution.source, Source::WalletInputs);
    QCOMPARE(contribution.direction, Direction::Send);
    QCOMPARE(contribution.amount, CAmount{100'000});
    QCOMPARE(contribution.output_index, -1);
    QVERIFY(contribution.address.isEmpty());
    QCOMPARE(contribution.inputs.size(), size_t{2});
    QVERIFY(contribution.inputs[0] == wtx.tx->vin[0].prevout);
    QVERIFY(contribution.inputs[1] == wtx.tx->vin[2].prevout);
    const auto& receipt = activity->actions[1];
    QCOMPARE(receipt.source, Source::Output);
    QCOMPARE(receipt.direction, Direction::Receive);
    QCOMPARE(receipt.output_index, 1);
    QCOMPARE(receipt.amount, CAmount{49'000});
    QVERIFY(receipt.inputs.empty());
    QCOMPARE(activity->walletImpact(), CAmount{-51'000});
    QVERIFY(!activity->fee.has_value());
}

void TransactionActivityTests::mixedReceiptsRetainChange()
{
    const auto wtx = MakeWalletTx({{100'000, true}, {50'000, false}}, {{49'000, true, true}, {10'000, true}, {90'000, false}});
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->actions.size(), size_t{3});
    QCOMPARE(activity->actions[0].amount, CAmount{100'000});
    QCOMPARE(activity->actions[1].amount, CAmount{49'000});
    QCOMPARE(activity->actions[2].amount, CAmount{10'000});
    QCOMPARE(activity->walletImpact(), CAmount{-41'000});
    QCOMPARE(activity->actions[1].direction, Direction::Receive);
    QCOMPARE(activity->actions[2].direction, Direction::Receive);
}

void TransactionActivityTests::repeatedAddressKeepsDistinctOutputIdentities()
{
    const auto wtx = MakeWalletTx({{100'000, true}}, {{30'000, false, false, false, 7}, {20'000, false, false, false, 7}, {49'000, true, true}});
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->actions.size(), size_t{2});
    QCOMPARE(activity->actions[0].address, activity->actions[1].address);
    QVERIFY(activity->actions[0].id != activity->actions[1].id);
    QCOMPARE(activity->actions[0].output_index, 0);
    QCOMPARE(activity->actions[1].output_index, 1);
}

void TransactionActivityTests::identitiesSurviveMetadataChanges()
{
    auto wtx = MakeWalletTx({{100'000, true}, {50'000, false}}, {{20'000, true}, {30'000, true}, {99'000, false}});
    const auto before = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(before);
    ++wtx.time;
    wtx.value_map["replaces_txid"] = std::string(64, '1');
    wtx.value_map["replaced_by_txid"] = std::string(64, '2');
    const auto after = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(after);
    QCOMPARE(after->txid, QString::fromStdString(wtx.tx->GetHash().GetHex()));
    QCOMPARE(after->txid, before->txid);
    QCOMPARE(after->timestamp, wtx.time);
    QCOMPARE(after->replaces_txid, QString(64, '1'));
    QCOMPARE(after->replaced_by_txid, QString(64, '2'));
    QSet<QString> ids;
    for (size_t i{0}; i < after->actions.size(); ++i) {
        QCOMPARE(after->actions[i].id, before->actions[i].id);
        QVERIFY(!after->actions[i].id.isEmpty());
        ids.insert(after->actions[i].id);
    }
    QCOMPARE(size_t(ids.size()), after->actions.size());
    QCOMPARE(after->actions[0].id, after->txid + ":wallet-inputs");
    QCOMPARE(after->actions[1].id, after->txid + ":output:0");
    QCOMPARE(after->actions[2].id, after->txid + ":output:1");
}

void TransactionActivityTests::dataOutputsDoNotBecomePayments()
{
    const auto wtx = MakeWalletTx({{60'000, true}, {40'000, true}}, {{0, false, false, true}, {99'000, true, true}});
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->type, Type::Consolidation);
    QCOMPARE(activity->actions.size(), size_t{1});
    QCOMPARE(activity->actions[0].output_index, 1);
    QCOMPARE(activity->actions[0].amount, CAmount{99'000});
}

void TransactionActivityTests::valueBearingNonAddressOutputRemainsVisible()
{
    auto wtx = MakeWalletTx({{10'000, true}}, {{9'000, false, false, true}});
    wtx.value_map["to"] = "not an address";
    const auto activity = TransactionActivity::fromWalletTx(wtx);
    QVERIFY(activity);
    QCOMPARE(activity->type, Type::Send);
    QCOMPARE(activity->actions.size(), size_t{1});
    QCOMPARE(activity->actions[0].amount, CAmount{9'000});
    QCOMPARE(activity->actions[0].output_index, 0);
    QVERIFY(activity->actions[0].address.isEmpty());
    QCOMPARE(activity->walletImpact(), CAmount{-10'000});
}

void TransactionActivityTests::knownZeroFeeDiffersFromUnknownFee()
{
    const auto send = TransactionActivity::fromWalletTx(MakeWalletTx({{100'000, true}}, {{20'000, false}, {80'000, true, true}}));
    QVERIFY(send);
    QVERIFY(send->fee.has_value());
    QCOMPARE(*send->fee, CAmount{0});
    const auto receive = TransactionActivity::fromWalletTx(MakeWalletTx({{100'000, false}}, {{20'000, true}, {80'000, false}}));
    QVERIFY(receive);
    QVERIFY(!receive->fee.has_value());
}

void TransactionActivityTests::ignoresTransactionsWithoutWalletOwnership()
{
    const auto wtx = MakeWalletTx({{100'000, false}}, {{99'000, false}});
    QVERIFY(!TransactionActivity::fromWalletTx(wtx));
}

void TransactionActivityTests::rejectsIncompleteSnapshots()
{
    QVERIFY(!TransactionActivity::fromWalletTx(interfaces::WalletTx{}));
    const auto valid = MakeWalletTx({{100'000, true}}, {{99'000, false}});
    auto wtx = valid;
    wtx.txin_is_mine.clear();
    QVERIFY(!TransactionActivity::fromWalletTx(wtx));
    wtx = valid;
    wtx.txout_is_mine.clear();
    QVERIFY(!TransactionActivity::fromWalletTx(wtx));
    wtx = valid;
    wtx.txout_is_change.clear();
    QVERIFY(!TransactionActivity::fromWalletTx(wtx));
    wtx = valid;
    wtx.txout_address.clear();
    QVERIFY(!TransactionActivity::fromWalletTx(wtx));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(TransactionActivityTests)
#else
QTEST_MAIN(TransactionActivityTests)
#endif
#include "test_transactionactivity.moc"
