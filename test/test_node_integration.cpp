// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/application_test_context.h>
#include <test/integration_test_registry.h>
#include <test/backend_barrier.h>
#include <univalue.h>

#include <QSignalSpy>
#include <QTimer>

class NodeIntegrationTests : public QObject
{
    Q_OBJECT
    ApplicationTestContext& m_app;
    bool m_network_was_active{false};

public:
    explicit NodeIntegrationTests(ApplicationTestContext& app) : m_app(app) {}

private Q_SLOTS:
    void init() { m_network_was_active = m_app.call(&interfaces::Node::getNetworkActive); }
    void cleanup() { m_app.call(&interfaces::Node::setNetworkActive, m_network_was_active); }

    void blockClockPauseAndResume()
    {
        auto* clock = m_app.find("blockClock");
        auto* toggle = m_app.find("blockClockToggleArea");
        QVERIFY(clock);
        QVERIFY(toggle);
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("CONNECTING"));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Connecting"));
        QCOMPARE(clock->property("subText").toString(), QStringLiteral("Please wait"));
        QVERIFY(ApplicationTestContext::click(toggle));
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("PAUSE"));
        QTRY_VERIFY(!m_app.call(&interfaces::Node::getNetworkActive));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Paused"));
        QCOMPARE(clock->property("subText").toString(), QStringLiteral("Tap to resume"));
        QVERIFY(ApplicationTestContext::click(toggle));
        QTRY_COMPARE(clock->property("state").toString(), QStringLiteral("CONNECTING"));
        QTRY_VERIFY(m_app.call(&interfaces::Node::getNetworkActive));
        QCOMPARE(clock->property("header").toString(), QStringLiteral("Connecting"));
        QCOMPARE(clock->property("subText").toString(), QStringLiteral("Please wait"));
    }

    void networkActionsAndCoreNotificationsAgree()
    {
        auto* model = m_app.model<NodeModel>("nodeModel");
        QVERIFY(model);
        model->setPause(true);
        QTRY_VERIFY(!m_app.call(&interfaces::Node::getNetworkActive));
        m_app.call(&interfaces::Node::setNetworkActive, true);
        QTRY_VERIFY(!model->pause());
        model->setPause(true);
        QTRY_VERIFY(!m_app.call(&interfaces::Node::getNetworkActive));
        QCOMPARE(model->numPeers(), static_cast<int>(m_app.call(&interfaces::Node::getNodeCount, ConnectionDirection::Both)));
    }

    void navigationStaysResponsiveDuringBackendRead()
    {
        auto* model = m_app.model<NodeModel>("nodeModel");
        qmlintegration::ScopedAuditBarrier held{m_app.audit, "Node::getMempoolSize"};
        model->refreshMempoolInfo();
        QTRY_VERIFY(held.barrier->entered.load());
        auto* settings = m_app.find("nodeSettingsButton");
        QVERIFY(settings);
        QVERIFY(ApplicationTestContext::click(settings));
        QTRY_VERIFY(m_app.find("settingsView"));
        QTRY_VERIFY(ApplicationTestContext::transitionsFinished(m_app.window()));
        bool delivered{false};
        QTimer::singleShot(0, model, [&] { delivered = true; });
        QTRY_VERIFY(delivered);
        QVERIFY(!held.barrier->timed_out);
        held.release();
        auto* back = m_app.find("settingsDoneButton");
        QVERIFY(back);
        QVERIFY(ApplicationTestContext::click(back));
        QTRY_VERIFY(ApplicationTestContext::transitionsFinished(m_app.window()));
    }

    void chainSyncFollowsRealCoreBlockNotifications()
    {
        auto* model = m_app.model<NodeModel>("nodeModel");
        QVERIFY(model);
        const int previous_height = m_app.call(&interfaces::Node::getNumBlocks);
        QSignalSpy tips{model, &NodeModel::blockTipHeightChanged};
        UniValue params{UniValue::VARR};
        params.push_back(1);
        params.push_back("raw(51)");
        QCOMPARE(m_app.call(&interfaces::Node::executeRpc, "generatetodescriptor", params, "").size(), 1U);
        QTRY_COMPARE(model->blockTipHeight(), previous_height + 1);
        QCOMPARE(model->blockTipHeight(), m_app.call(&interfaces::Node::getNumBlocks));
        QVERIFY(!tips.isEmpty());
        QVERIFY(model->verificationProgress() > 0.0);
    }
};

BITCOINQML_REGISTER_INTEGRATION_TEST(NodeIntegrationTests)
#include <test_node_integration.moc>
