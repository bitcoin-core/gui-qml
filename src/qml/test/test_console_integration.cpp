// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applicationrouter.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/models/rpcconsolemodel.h>
#include <qml/test/integration_test_registry.h>

#include <QAbstractItemModelTester>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QSignalSpy>
#include <QStyleHints>
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

// Exercise the actual Keys handlers, not the bridge's direct submit helper.
class ConsoleKeyboardIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    RpcConsoleModel* m_console{nullptr};
    QQuickWindow* m_window{nullptr};
    QQuickItem* m_input{nullptr};
    QObject* m_popup{nullptr};
    Qt::TabFocusBehavior m_original_tab_focus{};

    int requestCount() const
    {
        const auto* rows = m_console->outputModel();
        int count{0};
        for (int row{0}; row < rows->rowCount(); ++row) {
            if (rows->data(rows->index(row), RpcOutputListModel::CategoryRole).toInt() == RpcConsoleModel::CMD_REQUEST) ++count;
        }
        return count;
    }

public:
    explicit ConsoleKeyboardIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}

private Q_SLOTS:
    void initTestCase()
    {
        m_original_tab_focus = QGuiApplication::styleHints()->tabFocusBehavior();
        // Match the production application's keyboard navigation policy.
        QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    }

    void init()
    {
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
        // This diagnostic is a Qt logging warning, not QQmlEngine::warnings.
        QTest::failOnWarning(QRegularExpression{".*Injection of parameters.*"});
#endif
        auto& engine = m_app.engine();
        m_console = qobject_cast<RpcConsoleModel*>(engine.rootContext()->contextProperty("rpcConsoleModel").value<QObject*>());
        QVERIFY(m_console);
        QTRY_VERIFY(!m_console->executing());
        m_console->clear();
        QVERIFY(m_app.router().navigate("console"));
        m_window = qobject_cast<QQuickWindow*>(engine.rootObjects().constFirst());
        QVERIFY(m_window);
        QTRY_VERIFY(m_window->findChild<QQuickItem*>("consoleInput"));
        m_input = m_window->findChild<QQuickItem*>("consoleInput");
        m_popup = m_window->findChild<QObject*>("consoleAutocompletePopup");
        QVERIFY(m_popup);
        // Finish the page's deferred initial-focus request before sending keys.
        QCoreApplication::processEvents();
        m_window->requestActivate();
        m_input->forceActiveFocus();
        QTRY_VERIFY(m_input->hasActiveFocus());
    }

    void cleanup()
    {
        if (m_console) {
            QTRY_VERIFY_WITH_TIMEOUT(!m_console->executing(), 10'000);
            m_console->clear();
        }
        QVERIFY(m_app.router().navigate("node"));
    }

    void cleanupTestCase()
    {
        QGuiApplication::styleHints()->setTabFocusBehavior(m_original_tab_focus);
    }

    void enterSubmitsExactlyOnce_data()
    {
        QTest::addColumn<int>("key");
        QTest::addColumn<QString>("input");
        QTest::addColumn<QString>("command");
        QTest::addColumn<bool>("completion");
        QTest::newRow("return-command") << int(Qt::Key_Return) << QString("getblockhash 0") << QString("getblockhash 0") << false;
        QTest::newRow("enter-command") << int(Qt::Key_Enter) << QString("getblockhash 0") << QString("getblockhash 0") << false;
        QTest::newRow("return-completion") << int(Qt::Key_Return) << QString("getblockcou") << QString("getblockcount") << true;
        QTest::newRow("enter-completion") << int(Qt::Key_Enter) << QString("getblockcou") << QString("getblockcount") << true;
    }

    void enterSubmitsExactlyOnce()
    {
        QFETCH(int, key);
        QFETCH(QString, input);
        QFETCH(QString, command);
        QFETCH(bool, completion);
        QSignalSpy warnings(&m_app.engine(), &QQmlEngine::warnings);
        QVERIFY(m_input->setProperty("text", input));
        QTRY_COMPARE(m_popup->property("visible").toBool(), completion);
        const int first_row = m_console->outputModel()->rowCount();
        QTest::keyClick(m_window, static_cast<Qt::Key>(key));
        QTRY_COMPARE(requestCount(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(!m_console->executing(), 10'000);
        const auto* rows = m_console->outputModel();
        QCOMPARE(rows->rowCount(), first_row + 2);
        QVERIFY(rows->data(rows->index(first_row), RpcOutputListModel::ContentRole).toString().contains(command));
        QCOMPARE(rows->data(rows->index(first_row + 1), RpcOutputListModel::CategoryRole).toInt(), int(RpcConsoleModel::CMD_REPLY));
        QCOMPARE(m_input->property("text").toString(), QString{});
        QVERIFY(!m_popup->property("visible").toBool());
        QVERIFY(m_input->hasActiveFocus());
        QCOMPARE(warnings.count(), 0);
    }

    void tabCompletesWithoutSubmitting()
    {
        QSignalSpy warnings(&m_app.engine(), &QQmlEngine::warnings);
        QVERIFY(m_input->setProperty("text", "getblockcou"));
        QTRY_VERIFY(m_popup->property("visible").toBool());
        QTest::keyClick(m_window, Qt::Key_Tab);
        QCOMPARE(m_input->property("text").toString(), QString("getblockcount "));
        QCOMPARE(requestCount(), 0);
        QVERIFY(!m_console->executing());
        QVERIFY(!m_popup->property("visible").toBool());
        QVERIFY(m_input->hasActiveFocus());
        QCOMPARE(warnings.count(), 0);
    }

    void tabWithoutCompletionMovesFocus()
    {
        QSignalSpy warnings(&m_app.engine(), &QQmlEngine::warnings);
        QVERIFY(m_input->setProperty("text", "not-a-command"));
        QVERIFY(!m_popup->property("visible").toBool());
        QTest::keyClick(m_window, Qt::Key_Tab);
        QTRY_VERIFY(!m_input->hasActiveFocus());
        QCOMPARE(m_input->property("text").toString(), QString("not-a-command"));
        QCOMPARE(requestCount(), 0);
        QCOMPARE(warnings.count(), 0);
    }
};

BITCOINQML_REGISTER_INTEGRATION_TEST(ConsoleIntegrationTests)
BITCOINQML_REGISTER_INTEGRATION_TEST(ConsoleKeyboardIntegrationTests)
#include <test_console_integration.moc>
