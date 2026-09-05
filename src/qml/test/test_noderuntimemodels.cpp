// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/chainsyncmodel.h>
#include <qml/models/nodenetworkmodel.h>
#include <qml/test/mocks/mocknode.h>
#include <qml/test/qt_test_registry.h>
#include <validation.h>
#include <QSignalSpy>
#include <QTest>
#include <limits>
#include <thread>

namespace {
class NetworkNode : public MockNode
{
public:
    NotifyNumConnectionsChangedFn counts_callback;
    NotifyNetworkActiveChangedFn active_callback;
    bool active{true};
    int inbound{2}, outbound{3}, set_calls{0};
    size_t getNodeCount(ConnectionDirection direction) override
    {
        return direction == ConnectionDirection::In ? inbound :
               direction == ConnectionDirection::Out ? outbound : inbound + outbound;
    }
    bool getNetworkActive() override { return active; }
    void setNetworkActive(bool value) override { active = value; ++set_calls; }
    std::unique_ptr<interfaces::Handler> handleNotifyNumConnectionsChanged(NotifyNumConnectionsChangedFn fn) override
    {
        counts_callback = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { counts_callback = {}; });
    }
    std::unique_ptr<interfaces::Handler> handleNotifyNetworkActiveChanged(NotifyNetworkActiveChangedFn fn) override
    {
        active_callback = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { active_callback = {}; });
    }
};
}

class NodeRuntimeModelsTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void initializationPublishesSyncState()
    {
        MockNode node;
        ChainSyncModel sync{node};
        QSignalSpy progress{&sync, &ChainSyncModel::verificationProgressChanged};
        interfaces::BlockAndHeaderTipInfo initial_tip{};
        initial_tip.block_height = 321;
        initial_tip.verification_progress = 0.75;
        sync.initializeResult(true, initial_tip);
        QCOMPARE(sync.blockTipHeight(), 321);
        QCOMPARE(sync.verificationProgress(), 0.75);
        QCOMPARE(progress.count(), 1);
    }

    void blockCallbacksAreQueuedAndIgnoredAfterStop()
    {
        MockNode node;
        ChainSyncModel sync{node};
        std::thread notifier{[&] {
            interfaces::BlockTip tip{};
            tip.block_height = 2;
            node.notify_block_tip_fn(SynchronizationState{}, tip, 0.2);
        }};
        notifier.join();
        QCOMPARE(sync.blockTipHeight(), 0);
        QTRY_COMPARE(sync.blockTipHeight(), 2);
        QCOMPARE(sync.verificationProgress(), 0.2);
        interfaces::BlockTip later_tip{};
        later_tip.block_height = 3;
        node.notify_block_tip_fn(SynchronizationState{}, later_tip, 0.3);
        sync.stop();
        QCoreApplication::processEvents();
        QCOMPARE(sync.blockTipHeight(), 2);
        QVERIFY(!node.notify_block_tip_fn);
    }

    void progressRejectsNonFiniteValuesAndClampsRange()
    {
        MockNode node;
        ChainSyncModel sync{node};
        sync.setVerificationProgress(std::numeric_limits<double>::quiet_NaN());
        QCOMPARE(sync.verificationProgress(), 0.0);
        sync.setVerificationProgress(2.0);
        QCOMPARE(sync.verificationProgress(), 1.0);
        sync.setVerificationProgress(-1.0);
        QCOMPARE(sync.verificationProgress(), 0.0);
        QCOMPARE(sync.remainingSyncTime(), 0);
    }

    void networkCountsAndPauseHaveOneOwner()
    {
        NetworkNode node;
        NodeNetworkModel network{node};
        QCOMPARE(network.numPeers(), 5);
        QCOMPARE(network.numInboundPeers(), 2);
        QCOMPARE(network.numOutboundPeers(), 3);
        QVERIFY(!network.pause());
        network.setPause(true);
        network.setPause(true);
        QCOMPARE(node.set_calls, 1);
        QVERIFY(!node.active);
        node.active = true;
        node.active_callback(true);
        QTRY_VERIFY(!network.pause());
        QCOMPARE(node.set_calls, 1); // Core notifications never echo a write back.
        node.inbound = 0;
        node.counts_callback(3);
        QTRY_COMPARE(network.numPeers(), 3);
        network.stop();
        network.setPause(true);
        QCOMPARE(node.set_calls, 1);
        QVERIFY(!node.active_callback);
        QVERIFY(!node.counts_callback);
    }

    void staleNotificationsDoNotOverwriteNewerNetworkActions()
    {
        NetworkNode node;
        NodeNetworkModel network{node};
        node.active_callback(true); // Queued before a newer GUI change.
        network.setPause(true);
        QCoreApplication::processEvents();
        QVERIFY(network.pause());
        QVERIFY(!node.active);
        // A backend initialized after model construction must also be reconciled.
        node.active = true;
        network.refreshPeerCounts();
        QVERIFY(!network.pause());
    }
};
BITCOINQML_REGISTER_QT_TEST(NodeRuntimeModelsTests)
#include <test_noderuntimemodels.moc>
