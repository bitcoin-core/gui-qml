// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/psbtqmlmodel.h>

#include <QRegularExpression>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>

class WatchOnlyIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit WatchOnlyIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void createFromXpubAndPrepareUnsignedTransaction()
    {
        auto* source = qmltest::CreateWallet(m_app, "source");
        QVERIFY(source);
        const auto descriptors = qmltest::Rpc(m_app, "listdescriptors", {}, source->name());
        QString xpub;
        for (const auto& entry : descriptors.find_value("descriptors").getValues()) {
            const auto descriptor = QString::fromStdString(entry.find_value("desc").get_str());
            if (!descriptor.startsWith("wpkh(") || entry.find_value("internal").get_bool()) continue;
            xpub = QRegularExpression{QStringLiteral("([tx]pub[A-Za-z0-9]+)")}.match(descriptor).captured(1);
            break;
        }
        QVERIFY(!xpub.isEmpty());
        QVERIFY(qmltest::Click(m_app, "walletBadge"));
        QVERIFY(qmltest::Click(m_app, "walletSelectAddWalletButton"));
        QVERIFY(qmltest::Click(m_app, "walletTypeViewOnly"));
        QVERIFY(qmltest::SetText(m_app, "createWalletNameInput", "watchonly"));
        QVERIFY(qmltest::SetText(m_app, "watchOnlyXpubInput", xpub));
        QVERIFY(qmltest::Click(m_app, "createWalletFormCreateButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "walletCreationReadyPage"));
        QVERIFY(qmltest::Click(m_app, "createWalletReadyDoneButton"));
        auto* wallet = qmltest::WaitForWallet(m_app, "watchonly");
        QVERIFY(wallet);
        QCOMPARE(wallet->keySchemeKind(), WalletQmlModel::KeyScheme::WatchOnly);
        QVERIFY(!wallet->canManagePassphrase());
        QVERIFY(!qmltest::Rpc(m_app, "getwalletinfo", {}, wallet->name()).find_value("private_keys_enabled").get_bool());
        QVERIFY(qmltest::FundWallet(m_app, *wallet));
        auto* recipient = wallet->sendRecipientList()->currentRecipient();
        recipient->setAddress(qmltest::NewAddress(m_app, source->name()));
        recipient->amount()->setSatoshi(COIN);
        wallet->setCustomFeeEnabled(true);
        wallet->setCustomFeeRate("2");
        QVERIFY2(wallet->prepareTransaction(), qPrintable(wallet->transactionError()));
        QVERIFY(wallet->currentTransaction());
        QVERIFY(!wallet->currentTransactionCanSend());
        QVERIFY(!wallet->currentTransactionCanBroadcast());
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath("watchonly.psbt");
        QCOMPARE(wallet->saveCurrentTransactionAsPsbt(path), QString{});
        QVERIFY(!wallet->sendTransaction());
        QVERIFY(!wallet->broadcastCurrentTransaction());
        QCOMPARE(qmltest::Rpc(m_app, "getrawmempool").size(), 0U);
        PartiallySignedTransaction psbt{CMutableTransaction{}};
        QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(path, psbt), QString{});
        QVERIFY(!psbt.inputs.empty());
        for (const auto& input : psbt.inputs) QVERIFY(input.partial_sigs.empty());
    }

    void invalidXpubDoesNotCreateWallet()
    {
        auto* controller = qmltest::Model<WalletQmlController>(m_app, "walletController");
        QVERIFY(controller);
        QSignalSpy created{controller, &WalletQmlController::walletCreateSucceeded};
        controller->createWatchOnlyWallet("invalid", "not an extended public key");
        QVERIFY(!controller->walletLoadError().isEmpty());
        QVERIFY(!controller->walletLoadInProgress());
        QCOMPARE(created.count(), 0);
        QCOMPARE(qmltest::Rpc(m_app, "listwallets").size(), 0U);
        QVERIFY(!controller->walletPathExists("invalid"));
    }
};

std::unique_ptr<QObject> CreateWatchOnlyTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<WatchOnlyIntegrationTests>(app);
}

#include <test_watchonly.moc>
