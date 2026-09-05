// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>
#include <qml/models/chainsyncmodel.h>
#include <qml/models/nodenetworkmodel.h>
#include <qml/test/integration_test_registry.h>
#include <interfaces/node.h>
#include <netbase.h>
#include <univalue.h>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QTest>

class NodeIntegrationTests : public QObject
{
    Q_OBJECT
public:
    explicit NodeIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}
private:
    template <typename T> T* model(const char* name)
    {
        return qobject_cast<T*>(m_app.engine().rootContext()->contextProperty(QString::fromLatin1(name)).value<QObject*>());
    }
    BitcoinQmlApplication& m_app;
    bool m_network_was_active{false};
private Q_SLOTS:
    void init()
    {
        m_network_was_active = m_app.node().getNetworkActive();
    }

    void cleanup()
    {
        // Restore only this case's node changes, also following failed assertions.
        m_app.node().setNetworkActive(m_network_was_active);
    }

    void networkActionsAndCoreNotificationsAgree()
    {
        auto* network = model<NodeNetworkModel>("nodeNetworkModel");
        QVERIFY(network);
        network->setPause(true);
        QVERIFY(!m_app.node().getNetworkActive());
        m_app.node().setNetworkActive(true);
        QTRY_VERIFY(!network->pause());
        network->setPause(true);
        QVERIFY(!m_app.node().getNetworkActive());
        QCOMPARE(network->numPeers(), static_cast<int>(m_app.node().getNodeCount(ConnectionDirection::Both)));
    }

    void chainSyncFollowsRealCoreBlockNotifications()
    {
        auto* sync = model<ChainSyncModel>("chainSyncModel");
        QVERIFY(sync);
        const int previous_height{m_app.node().getNumBlocks()};
        UniValue params{UniValue::VARR};
        params.push_back(1);
        params.push_back("raw(51)");
        const UniValue blocks{m_app.node().executeRpc("generatetodescriptor", params, "")};
        QCOMPARE(blocks.size(), 1U);
        QTRY_COMPARE(sync->blockTipHeight(), previous_height + 1);
        QCOMPARE(sync->blockTipHeight(), m_app.node().getNumBlocks());
        QVERIFY(sync->verificationProgress() > 0.0);
    }

};
BITCOINQML_REGISTER_INTEGRATION_TEST(NodeIntegrationTests)
#include <test_node_integration.moc>
