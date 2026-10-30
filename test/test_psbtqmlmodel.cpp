// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/psbtqmlmodel.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/mockwallet.h>

#include <chainparams.h>
#include <key_io.h>
#include <node/types.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <script/script.h>

#include <QFile>
#include <QTemporaryDir>

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace {
const auto VALID_MAINNET_ADDRESS = QStringLiteral("1BoatSLRHtKNngkdXEeobR76b53LETtpyT");

class PsbtTestWallet : public StubWallet
{
public:
    bool private_keys_disabled{false};
    std::vector<bool> sign_args;

    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options,
                                              size_t* n_signed,
                                              PartiallySignedTransaction&,
                                              bool& complete) override
    {
        sign_args.push_back(options.sign);
        if (n_signed) {
            *n_signed = 1;
        }
        complete = false;
        return std::nullopt;
    }

    bool privateKeysDisabled() override { return private_keys_disabled; }
};

PartiallySignedTransaction MakeUnsignedPsbt()
{
    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    tx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));

    PartiallySignedTransaction psbt{tx};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, CScript{}};
    return psbt;
}

PartiallySignedTransaction MakeCompletePsbt()
{
    CMutableTransaction previous_tx;
    previous_tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous_tx.vout.emplace_back(2'000, CScript{} << OP_DROP << OP_TRUE);
    const CTransactionRef previous{MakeTransactionRef(previous_tx)};

    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{previous->GetHash(), 0});
    tx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));

    PartiallySignedTransaction psbt{tx};
    psbt.inputs[0].non_witness_utxo = previous;
    psbt.inputs[0].final_script_sig = CScript{} << std::vector<unsigned char>{};
    return psbt;
}

std::optional<PartiallySignedTransaction> DecodeRawPsbt(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) {
        return std::nullopt;
    }
    const QByteArray bytes{file.readAll()};
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char byte : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }

    const auto psbt{DecodeRawPSBT(std::span<const std::byte>{raw.data(), raw.size()})};
    if (!psbt) {
        return std::nullopt;
    }
    return *psbt;
}
} // namespace

class PsbtQmlModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void loadsRawAndBase64Psbt();
    void invalidFileSetsErrorAndClearResetsState();
    void signUsesInjectedWallet();
    void savePreservesPsbtMetadata();
    void broadcastsCompletePsbt();
};

void PsbtQmlModelTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
}

void PsbtQmlModelTests::loadsRawAndBase64Psbt()
{
    PsbtTestWallet wallet;
    PsbtQmlModel model{&wallet, nullptr};
    const PartiallySignedTransaction psbt{MakeUnsignedPsbt()};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString raw_path{temp_dir.filePath(QStringLiteral("raw.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, raw_path), QString{});
    QCOMPARE(model.loadFromFile(raw_path), QString{});
    QVERIFY(model.loaded());
    QVERIFY(model.canSign());
    QCOMPARE(model.couldSignInputCount(), 1);
    QCOMPARE(model.unsignedInputCount(), 1);

    const QString base64_path{temp_dir.filePath(QStringLiteral("base64.psbt"))};
    QFile base64_file{base64_path};
    QVERIFY(base64_file.open(QIODevice::WriteOnly));
    const QByteArray base64{PsbtQmlModel::SerializePsbtRaw(psbt).toBase64()};
    QCOMPARE(base64_file.write(base64), base64.size());
    base64_file.close();

    model.clear();
    QCOMPARE(model.loadFromFile(base64_path), QString{});
    QVERIFY(model.loaded());
    QCOMPARE(model.unsignedInputCount(), 1);
}

void PsbtQmlModelTests::invalidFileSetsErrorAndClearResetsState()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("invalid.psbt"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("not a psbt"), 10LL);
    file.close();

    PsbtQmlModel model{nullptr, nullptr};
    QVERIFY(!model.loadFromFile(path).isEmpty());
    QVERIFY(!model.loaded());
    QVERIFY(!model.error().isEmpty());

    model.clear();
    QVERIFY(model.error().isEmpty());
    QVERIFY(model.status().isEmpty());
}

void PsbtQmlModelTests::signUsesInjectedWallet()
{
    PsbtTestWallet wallet;
    PsbtQmlModel model{&wallet, nullptr};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("unsigned.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeUnsignedPsbt(), path), QString{});
    QCOMPARE(model.loadFromFile(path), QString{});

    model.sign();
    QCOMPARE(wallet.sign_args, std::vector<bool>({false, true, false}));
    QVERIFY(model.status().contains(QStringLiteral("Signed 1 input")));
}

void PsbtQmlModelTests::savePreservesPsbtMetadata()
{
    PartiallySignedTransaction psbt{MakeUnsignedPsbt()};
    psbt.unknown[{0x50}] = {0x01};
    psbt.inputs[0].unknown[{0x51}] = {0x02};
    psbt.outputs[0].unknown[{0x52}] = {0x03};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{temp_dir.filePath(QStringLiteral("source.psbt"))};
    const QString saved_path{temp_dir.filePath(QStringLiteral("saved.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(psbt, source_path), QString{});

    PsbtQmlModel model{nullptr, nullptr};
    QCOMPARE(model.loadFromFile(source_path), QString{});
    QCOMPARE(model.saveToFile(saved_path), QString{});

    const auto saved{DecodeRawPsbt(saved_path)};
    QVERIFY(saved.has_value());
    QCOMPARE(saved->unknown, psbt.unknown);
    QCOMPARE(saved->inputs[0].unknown, psbt.inputs[0].unknown);
    QCOMPARE(saved->outputs[0].unknown, psbt.outputs[0].unknown);
}

void PsbtQmlModelTests::broadcastsCompletePsbt()
{
    MockNode node;
    node.broadcast_transaction_fn = [](CTransactionRef, CAmount, CFeeRate, std::string&) {
        return node::TransactionError::OK;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path{temp_dir.filePath(QStringLiteral("complete.psbt"))};
    QCOMPARE(PsbtQmlModel::SavePsbtToFile(MakeCompletePsbt(), path), QString{});

    PsbtQmlModel model{nullptr, &node};
    QCOMPARE(model.loadFromFile(path), QString{});
    QVERIFY(model.complete());
    QVERIFY(model.canBroadcast());

    model.broadcast();
    QVERIFY(model.status().startsWith(QStringLiteral("Transaction broadcast successfully.")));
    QCOMPARE(node.calls.broadcastTransaction.load(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PsbtQmlModelTests)
#else
QTEST_MAIN(PsbtQmlModelTests)
#endif
#include "test_psbtqmlmodel.moc"
