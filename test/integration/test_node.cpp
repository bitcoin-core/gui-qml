// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>
#include <qml/models/rpcconsolemodel.h>
#include <univalue.h>

#include <QScopeGuard>
#include <QSignalSpy>
#include <QTimer>

class NodeIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    bool m_network_was_active{false};

public:
    explicit NodeIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}

private Q_SLOTS:
    void init() { m_network_was_active = qmltest::Call(m_app, &interfaces::Node::getNetworkActive); }
    void cleanup()
    {
        qmltest::Call(m_app, &interfaces::Node::setNetworkActive, m_network_was_active);
        QVERIFY(qmltest::ReturnToNode(m_app));
        QTRY_VERIFY(qmltest::TransitionsFinished(m_app.window()));
    }

    void blockClockPauseAndResume()
    {
        auto* clock = qmltest::Find(m_app, "blockClock");
        auto* toggle = qmltest::Find(m_app, "blockClockToggleArea");
        QVERIFY(clock);
        QVERIFY(toggle);
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("CONNECTING"));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Connecting"));
        QCOMPARE(clock->property("subText").toString(), QStringLiteral("Please wait"));
        QVERIFY(qmltest::Click(toggle));
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("PAUSE"));
        QTRY_VERIFY(!qmltest::Call(m_app, &interfaces::Node::getNetworkActive));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Paused"));
        QCOMPARE(clock->property("subText").toString(), QStringLiteral("Tap to resume"));
        QVERIFY(qmltest::Click(toggle));
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("CONNECTING"));
        QTRY_VERIFY(qmltest::Call(m_app, &interfaces::Node::getNetworkActive));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Connecting"));
        QCOMPARE(clock->property("subText").toString(), QStringLiteral("Please wait"));
    }

    void networkActionsAndCoreNotificationsAgree()
    {
        auto* model = &m_app.nodeModel();
        QVERIFY(model);
        model->setPause(true);
        QTRY_VERIFY(!qmltest::Call(m_app, &interfaces::Node::getNetworkActive));
        qmltest::Call(m_app, &interfaces::Node::setNetworkActive, true);
        QTRY_VERIFY(!model->pause());
        model->setPause(true);
        QTRY_VERIFY(!qmltest::Call(m_app, &interfaces::Node::getNetworkActive));
        QCOMPARE(model->numPeers(), static_cast<int>(qmltest::Call(m_app, &interfaces::Node::getNodeCount, ConnectionDirection::Both)));
    }

    void navigationStaysResponsiveDuringRpc()
    {
        auto* model = &m_app.nodeModel();
        auto* console = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QVERIFY(console);
        QVERIFY(console->submitCommand("waitfornewblock 10000"));
        const auto finish_rpc = qScopeGuard([&] {
            UniValue params{UniValue::VARR};
            params.push_back(1);
            params.push_back("raw(51)");
            qmltest::Call(m_app, &interfaces::Node::executeRpc, "generatetodescriptor", params, "");
            QVERIFY(QTest::qWaitFor([&] { return !console->executing(); }, 5000));
        });
        QTRY_VERIFY(qmltest::RpcCommandRunning(m_app, "waitfornewblock"));
        QVERIFY(qmltest::OpenSettings(m_app));
        QTRY_VERIFY(qmltest::Find(m_app, "settingsView"));
        QTRY_VERIFY(qmltest::TransitionsFinished(m_app.window()));
        bool delivered{false};
        QTimer::singleShot(0, model, [&] { delivered = true; });
        QTRY_VERIFY(delivered);
        QVERIFY(console->executing());
        QVERIFY(qmltest::ReturnToNode(m_app));
        QTRY_VERIFY(qmltest::TransitionsFinished(m_app.window()));
    }

    void chainSyncFollowsRealCoreBlockNotifications()
    {
        auto* model = &m_app.nodeModel();
        QVERIFY(model);
        const int previous_height = qmltest::Call(m_app, &interfaces::Node::getNumBlocks);
        QSignalSpy tips{model, &NodeModel::blockTipHeightChanged};
        UniValue params{UniValue::VARR};
        params.push_back(1);
        params.push_back("raw(51)");
        QCOMPARE(qmltest::Call(m_app, &interfaces::Node::executeRpc, "generatetodescriptor", params, "").size(), 1U);
        QTRY_COMPARE(model->blockTipHeight(), previous_height + 1);
        QCOMPARE(model->blockTipHeight(), qmltest::Call(m_app, &interfaces::Node::getNumBlocks));
        QVERIFY(!tips.isEmpty());
        QVERIFY(model->verificationProgress() > 0.0);
    }
};

int RunNodeTests(BitcoinQmlApplication& app, WalletMode, int argc, char* argv[])
{
    NodeIntegrationTests tests{app};
    return QTest::qExec(&tests, argc, argv);
}

#include <test_node.moc>
