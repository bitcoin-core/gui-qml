// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/psbtqmlmodel.h>

#include <QFile>
#include <QTemporaryDir>

class PsbtIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit PsbtIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void failedImportPreservesTheDraftAndAllowsRetry_data()
    {
        QTest::addColumn<bool>("create_invalid_file");
        QTest::addColumn<QString>("expected_error");
        QTest::newRow("missing-file") << false << QStringLiteral("Could not open PSBT file");
        QTest::newRow("malformed-file") << true << QStringLiteral("Could not decode PSBT");
    }

    void failedImportPreservesTheDraftAndAllowsRetry()
    {
        QFETCH(bool, create_invalid_file);
        QFETCH(QString, expected_error);
        auto* recipient = qmltest::CreateWallet(m_app, "recipient");
        QVERIFY(recipient);
        const auto address = qmltest::NewAddress(m_app, recipient->name());
        auto* wallet = qmltest::CreateWallet(m_app, "signer");
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, wallet->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto invalid_path = directory.filePath("invalid.psbt");
        if (create_invalid_file) {
            QFile file{invalid_path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            const QByteArray invalid{"not a PSBT"};
            QCOMPARE(file.write(invalid), invalid.size());
        }
        QFile valid{directory.filePath("valid.psbt")};
        QVERIFY(valid.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(valid.write(bytes), bytes.size());
        valid.close();

        QVERIFY(qmltest::Click(m_app, "sendTabButton"));
        QVERIFY(qmltest::SetText(m_app, "sendAddressInput", address));
        QVERIFY(qmltest::SetText(m_app, "sendAmountInput", "0.25"));
        auto* draft = wallet->sendRecipientList()->currentRecipient();
        QCOMPARE(draft->address()->address(), address);
        QCOMPARE(draft->amount()->satoshi(), COIN / 4);

        // Inject only the file-picker result; the menu and error handling are production QML.
        auto* path = qmltest::Find(m_app, "psbtImportPathField");
        QVERIFY(path);
        QVERIFY(path->setProperty("text", invalid_path));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        auto* error = qmltest::WaitForVisible(m_app, "unsupportedPsbtPopup");
        QVERIFY(error);
        QTRY_VERIFY(error->property("opened").toBool());
        QVERIFY(qmltest::Find(m_app, "unsupportedPsbtPopupMessage")->property("text").toString().contains(expected_error));
        QVERIFY(!qmltest::Find(m_app, "transactionReviewPopup")->property("visible").toBool());
        QVERIFY(!wallet->currentTransaction());
        QCOMPARE(draft->address()->address(), address);
        QCOMPARE(draft->amount()->satoshi(), COIN / 4);
        QCOMPARE(qmltest::Find(m_app, "sendAddressInput")->property("text").toString(), draft->address()->formattedAddress());
        QCOMPARE(qmltest::Find(m_app, "sendAmountInput")->property("text").toString(), QStringLiteral("0.25000000"));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);

        // Closing the error must allow a valid import without restarting or clearing the draft.
        QVERIFY(qmltest::Click(m_app, "unsupportedPsbtPopupOkButton"));
        QTRY_VERIFY(!error->property("visible").toBool());
        QVERIFY(path->setProperty("text", valid.fileName()));
        QVERIFY(qmltest::Click(m_app, "sendOptionsButton"));
        QVERIFY(qmltest::Click(m_app, "sendImportPsbtFromFileButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "sendTransactionReviewPage"));
        QTRY_VERIFY(qmltest::Find(m_app, "transactionReviewPopup")->property("opened").toBool());
        QVERIFY(wallet->currentTransaction());
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        QVERIFY(wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QVERIFY(wallet->importedPsbt()->error().isEmpty());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void importSignBroadcastAndRecognizeKnownTransaction()
    {
        auto* wallet = qmltest::CreateWallet(m_app, "psbt");
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        auto* recipient = qmltest::CreateWallet(m_app, "recipient");
        QVERIFY(recipient);
        const auto address = qmltest::NewAddress(m_app, recipient->name());
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, wallet->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("unsigned.psbt");
        QFile file{path};
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QCOMPARE(wallet->importPsbtFromFile(path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(wallet->currentTransaction());
        QVERIFY(wallet->currentTransactionIsImportedPsbt());
        QVERIFY(wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        const auto exported_path = directory.filePath("exported.psbt");
        QCOMPARE(wallet->saveCurrentTransactionAsPsbt(exported_path), QString{});
        QVERIFY2(wallet->sendTransaction(), qPrintable(wallet->transactionError()));
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 1U);
        QCOMPARE(wallet->importPsbtFromFile(exported_path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
    }

    void foreignPsbtCanBeReviewedWithoutSigning()
    {
        auto* source = qmltest::CreateWallet(m_app, "source");
        QVERIFY(source);
        QVERIFY(qmltest::FundWallet(m_app, *source));
        auto* reviewer = qmltest::CreateWallet(m_app, "reviewer");
        QVERIFY(reviewer);
        const auto address = qmltest::NewAddress(m_app, reviewer->name());
        const auto params = qmltest::Json("[[], {\"" + address.toStdString() + "\": 1}, 0, {\"fee_rate\": 2}, true]");
        const auto psbt = qmltest::Rpc(m_app, "walletcreatefundedpsbt", params, source->name()).find_value("psbt").get_str();
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QFile file{directory.filePath("foreign.psbt")};
        QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes = QByteArray::fromBase64(QByteArray::fromStdString(psbt));
        QCOMPARE(file.write(bytes), bytes.size());
        file.close();
        QCOMPARE(reviewer->importPsbtFromFile(file.fileName()), WalletQmlModel::PsbtImportResult::WalletCannotSign);
        QVERIFY(reviewer->currentTransaction());
        QVERIFY(reviewer->currentTransactionIsImportedPsbt());
        QVERIFY(!reviewer->currentTransactionCanSend());
        QVERIFY(!reviewer->currentTransactionCanBroadcast());
        QVERIFY(!reviewer->sendTransaction());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
    }

    void feeReplacementRequiresConfirmation()
    {
        auto* wallet = qmltest::CreateWallet(m_app);
        QVERIFY(wallet);
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        recipient->setAddress(qmltest::NewAddress(m_app, wallet->name()));
        recipient->amount()->setSatoshi(COIN);
        wallet->setCustomFeeEnabled(true);
        wallet->setCustomFeeRate("1");
        QVERIFY2(wallet->prepareTransaction(), qPrintable(wallet->transactionError()));
        QVERIFY2(wallet->sendTransaction(), qPrintable(wallet->transactionError()));
        const auto txid = wallet->currentTransaction()->txid();
        auto* bump = wallet->bumpModel();
        bump->prepareFeeBump(txid, 2);
        QCOMPARE(bump->state(), BumpTransactionModel::NeedsConfirmation);
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool")[0].get_str(), txid.toStdString());
        QVERIFY2(bump->confirmFeeBump(), qPrintable(bump->errorText()));
        QCOMPARE(bump->state(), BumpTransactionModel::Succeeded);
        QVERIFY(bump->newTxid() != txid);
        const auto pool = qmltest::Rpc(m_app, "getrawmempool");
        QCOMPARE(pool.size(), 1U);
        QCOMPARE(pool[0].get_str(), bump->newTxid().toStdString());
    }
};

std::unique_ptr<QObject> CreatePsbtTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<PsbtIntegrationTests>(app);
}

#include <test_psbt.moc>
