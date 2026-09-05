// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/test/integration_test_registry.h>

#include <QAbstractItemModelTester>
#include <QSignalSpy>
#include <QTest>

class ConsoleIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit ConsoleIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}

private Q_SLOTS:
    void commandsAndErrors()
    {
        RpcConsoleModel console(m_app.node());
        console.onNodeInitialized();
        QAbstractItemModelTester consistency(console.outputModel(), QAbstractItemModelTester::FailureReportingMode::QtTest);
        QVERIFY(console.availableCommands().contains("getblockchaininfo"));
        QVERIFY(!console.submitCommand("   "));
        QVERIFY(console.submitCommand("getblockchaininfo"));
        QVERIFY(console.executing());
        QVERIFY(!console.submitCommand("getblockcount"));
        QTRY_VERIFY_WITH_TIMEOUT(!console.executing(), 10'000);
        auto* rows = console.outputModel();
        const int reply_row = rows->rowCount() - 1;
        QCOMPARE(rows->data(rows->index(reply_row), RpcOutputListModel::CategoryRole).toInt(), int(RpcConsoleModel::CMD_REPLY));
        QVERIFY(rows->data(rows->index(reply_row), RpcOutputListModel::ContentRole).toString().contains("regtest"));
        QVERIFY(console.submitCommand("this_rpc_does_not_exist"));
        QTRY_VERIFY_WITH_TIMEOUT(!console.executing(), 10'000);
        QCOMPARE(rows->data(rows->index(rows->rowCount() - 1), RpcOutputListModel::CategoryRole).toInt(), int(RpcConsoleModel::CMD_ERROR));
        QVERIFY(console.submitCommand("help"));
        QTRY_VERIFY_WITH_TIMEOUT(!console.executing(), 10'000);
        QVERIFY(rows->data(rows->index(rows->rowCount() - 1), RpcOutputListModel::ContentRole).toString().contains("getblockchaininfo"));
        console.clear();
        const int welcome_rows = rows->rowCount();
        QVERIFY(welcome_rows > 0);
        console.ensureWelcomeMessage();
        QCOMPARE(rows->rowCount(), welcome_rows);
    }

    void historyRedactsSecretsAndRetainsDraft()
    {
        RpcConsoleModel console(m_app.node());
        console.onNodeInitialized();
        QVERIFY(console.submitCommand("walletpassphrase synthetic-test-only-secret 1"));
        QTRY_VERIFY_WITH_TIMEOUT(!console.executing(), 10'000);
        const QString recalled = console.browseHistory(1, "unfinished command");
        QVERIFY(recalled.contains("walletpassphrase"));
        QVERIFY(!recalled.contains("synthetic-test-only-secret"));
        QCOMPARE(console.browseHistory(-1, ""), QStringLiteral("unfinished command"));
        for (int row = 0; row < console.outputModel()->rowCount(); ++row) {
            QVERIFY(!console.outputModel()->data(console.outputModel()->index(row), RpcOutputListModel::ContentRole).toString().contains("synthetic-test-only-secret"));
        }
    }
};

BITCOINQML_REGISTER_INTEGRATION_TEST(ConsoleIntegrationTests)
#include <test_console_integration.moc>
