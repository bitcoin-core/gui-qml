// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/gmocktestfixture.h>
#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>

#include <qml/models/nodemodel.h>

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <validation.h>

#include <atomic>
#include <functional>

namespace {
using ::testing::Invoke;
using ::testing::NiceMock;

constexpr int ASYNC_TIMEOUT_MS{1'000};

std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

struct MempoolState {
    std::atomic<size_t> count{0};
    std::atomic<size_t> usage_bytes{0};
    std::atomic<size_t> max_bytes{0};
    std::atomic<int> max_usage_calls{0};
};

void InstallDefaultHandlers(NiceMock<MockNode>& node)
{
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyBlockTipFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyNumConnectionsChangedFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleBannedListChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::BannedListChangedFn) {
            return MakeNoopHandler();
        }));
}

void InstallMempoolGetters(NiceMock<MockNode>& node, MempoolState& mempool)
{
    ON_CALL(node, getMempoolSize()).WillByDefault(Invoke([&mempool] {
        return mempool.count.load();
    }));
    ON_CALL(node, getMempoolDynamicUsage()).WillByDefault(Invoke([&mempool] {
        return mempool.usage_bytes.load();
    }));
    ON_CALL(node, getMempoolMaxUsage()).WillByDefault(Invoke([&mempool] {
        ++mempool.max_usage_calls;
        return mempool.max_bytes.load();
    }));
}

void WaitForInitialMempoolRefresh(MempoolState& mempool)
{
    QTRY_VERIFY_WITH_TIMEOUT(mempool.max_usage_calls.load() >= 1, ASYNC_TIMEOUT_MS);
    QCoreApplication::processEvents();
}
} // namespace

class NodeModelTests : public GmockTestFixture
{
    Q_OBJECT

private Q_SLOTS:
    void refreshMempoolInfoUpdatesProperties();
    void activatingMempoolPollingEmitsSignalsAndRefreshesImmediately();
    void nodeNotificationHandlersUpdateModelThroughQueuedSignals();
    void initEmitsRequestedInitialize();
    void initGuardBlocksSecondEmission();
};

void NodeModelTests::refreshMempoolInfoUpdatesProperties()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    mempool.count = 12;
    mempool.usage_bytes = 12'500'000;
    mempool.max_bytes = 300'000'000;

    QSignalSpy mempool_info_spy{&model, &NodeModel::mempoolInfoChanged};
    model.refreshMempoolInfo();

    QTRY_COMPARE_WITH_TIMEOUT(mempool_info_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.mempoolTransactionCount(), 12);
    QCOMPARE(model.mempoolUsageMB(), 12.5);
    QCOMPARE(model.mempoolMaxUsageMB(), 300.0);
}

void NodeModelTests::activatingMempoolPollingEmitsSignalsAndRefreshesImmediately()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    mempool.count = 3;
    mempool.usage_bytes = 4'000'000;
    mempool.max_bytes = 500'000'000;

    QSignalSpy polling_active_spy{&model, &NodeModel::mempoolInfoPollingActiveChanged};
    QSignalSpy mempool_info_spy{&model, &NodeModel::mempoolInfoChanged};
    model.setMempoolInfoPollingActive(true);

    QCOMPARE(model.mempoolInfoPollingActive(), true);
    QCOMPARE(polling_active_spy.count(), 1);
    QCOMPARE(polling_active_spy.takeFirst().at(0).toBool(), true);
    QTRY_COMPARE_WITH_TIMEOUT(mempool_info_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.mempoolTransactionCount(), 3);
    QCOMPARE(model.mempoolUsageMB(), 4.0);
    QCOMPARE(model.mempoolMaxUsageMB(), 500.0);

    model.setMempoolInfoPollingActive(false);
    QCOMPARE(model.mempoolInfoPollingActive(), false);
    QCOMPARE(polling_active_spy.count(), 1);
    QCOMPARE(polling_active_spy.takeFirst().at(0).toBool(), false);
}

void NodeModelTests::nodeNotificationHandlersUpdateModelThroughQueuedSignals()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed_fn;
    interfaces::Node::BannedListChangedFn banned_list_changed_fn;

    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyBlockTipFn fn) {
            block_tip_fn = std::move(fn);
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyNumConnectionsChangedFn fn) {
            connections_changed_fn = std::move(fn);
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleBannedListChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::BannedListChangedFn fn) {
            banned_list_changed_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);
    QVERIFY(connections_changed_fn);
    QVERIFY(banned_list_changed_fn);

    QSignalSpy block_tip_height_spy{&model, &NodeModel::blockTipHeightChanged};
    QSignalSpy verification_progress_spy{&model, &NodeModel::verificationProgressChanged};
    QSignalSpy time_ratio_spy{&model, &NodeModel::setTimeRatioList};
    QSignalSpy outbound_peers_spy{&model, &NodeModel::numOutboundPeersChanged};
    QSignalSpy banned_list_spy{&model, &NodeModel::bannedListChanged};

    block_tip_fn(SynchronizationState::POST_INIT, interfaces::BlockTip{144, 1'700'000'000, uint256{}}, 0.42);
    connections_changed_fn(7);
    banned_list_changed_fn();

    QTRY_COMPARE_WITH_TIMEOUT(block_tip_height_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(verification_progress_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(time_ratio_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(outbound_peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(banned_list_spy.count(), 1, ASYNC_TIMEOUT_MS);

    QCOMPARE(model.blockTipHeight(), 144);
    QCOMPARE(model.verificationProgress(), 0.42);
    QCOMPARE(time_ratio_spy.takeFirst().at(0).toInt(), 1'700'000'000);
    QCOMPARE(model.numOutboundPeers(), 7);
}

void NodeModelTests::initEmitsRequestedInitialize()
{
    NiceMock<MockNode> node;
    NodeModel model(node);

    QSignalSpy spy(&model, &NodeModel::requestedInitialize);
    model.startNodeInitializionThread();
    QCOMPARE(spy.count(), 1);
}

void NodeModelTests::initGuardBlocksSecondEmission()
{
    NiceMock<MockNode> node;
    NodeModel model(node);

    QSignalSpy spy(&model, &NodeModel::requestedInitialize);
    model.startNodeInitializionThread();
    model.startNodeInitializionThread();
    QCOMPARE(spy.count(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NodeModelTests)
#else
QTEST_MAIN(NodeModelTests)
#endif
#include "test_nodemodel.moc"
