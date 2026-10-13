// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/fixture/test_application.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>

#include <QDir>
#include <QObject>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSignalSpy>

class ApplicationLifecycleTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    WalletMode m_wallet_mode;

public:
    explicit ApplicationLifecycleTests(BitcoinQmlApplication& app, WalletMode wallet_mode)
        : m_app{app}, m_wallet_mode{wallet_mode} {}

private Q_SLOTS:
    void startupShowsNodePage()
    {
        QVERIFY(!m_app.nodeModel().errorState());
        auto* window = m_app.window();
        QVERIFY(window);
        QTRY_VERIFY(window->isVisible());
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(qmltest::TransitionsFinished(window));
        const auto* shell = m_wallet_mode == WalletMode::Enabled ? "desktopWalletsPage" : "nodeRunner";
        for (const auto* name : {shell, "blockClock"}) {
            auto* item = qobject_cast<QQuickItem*>(qmltest::Find(m_app, name));
            QVERIFY2(item, name);
            QTRY_VERIFY2(item->isVisible(), name);
            QTRY_VERIFY2(item->width() > 0 && item->height() > 0, name);
            QCOMPARE(item->window(), window);
            QVERIFY2(item->mapRectToScene(item->boundingRect()).intersects(window->contentItem()->boundingRect()), name);
        }
        QObject frame_observer;
        bool rendered{false};
        connect(window, &QQuickWindow::afterRendering, &frame_observer,
                [&] { rendered = true; }, Qt::QueuedConnection);
        window->update();
        QTRY_VERIFY(rendered);

        const auto info = qmltest::Call(m_app, &interfaces::Node::executeRpc, "getblockchaininfo", UniValue{UniValue::VARR}, "");
        QCOMPARE(info.find_value("chain").get_str(), std::string{"regtest"});
        QCOMPARE(qmltest::Call(m_app, &interfaces::Node::getNodeCount, ConnectionDirection::Both), 0U);
        QVERIFY(QDir{QStringLiteral(":/translations")}.exists(QStringLiteral("bitcoin_es.qm")));
        QVERIFY(QDir{QStringLiteral(":/translations")}.exists(QStringLiteral("bitcoin_qml_es.qm")));
    }

    void startupUsesRequestedWalletMode()
    {
        const bool enabled = m_wallet_mode == WalletMode::Enabled;
        QCOMPARE(m_app.window()->property("walletAvailableForUi").toBool(), enabled);
        QCOMPARE(m_app.window()->property("desktopWalletMode").toBool(), enabled);
        QCOMPARE(qmltest::Find(m_app, "desktopWalletsPage") != nullptr, enabled);
        QCOMPARE(qmltest::Model<QObject>(m_app, "walletController") != nullptr, enabled);
        QCOMPARE(qmltest::Model<QObject>(m_app, "walletListModel") != nullptr, enabled);
        if (enabled) {
            const auto wallets = qmltest::Call(m_app, &interfaces::Node::executeRpc, "listwallets", UniValue{UniValue::VARR}, "");
            QVERIFY(wallets.isArray());
            QCOMPARE(wallets.size(), 0U);
        }
    }

    void closingWindowShutsDownApplication()
    {
        auto* model = &m_app.nodeModel();
        auto* window = m_app.window();
        QVERIFY(window);
        QVERIFY(window->isVisible());
        QSignalSpy requested{model, &NodeModel::requestedShutdown};
        window->close();
        QTRY_COMPARE(requested.count(), 1);
        QVERIFY(window->property("shutdownInProgress").toBool());
        auto* shutdown_page = qobject_cast<QQuickItem*>(qmltest::Find(m_app, "shutdownPage"));
        QVERIFY(shutdown_page);
        QTRY_VERIFY(shutdown_page->isVisible());
    }
};

int RunLifecycleTests(BitcoinQmlApplication& app, WalletMode wallet_mode, int argc, char* argv[])
{
    ApplicationLifecycleTests tests{app, wallet_mode};
    return QTest::qExec(&tests, argc, argv);
}

#include <test_lifecycle.moc>
