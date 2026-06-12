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
#include <chainparams.h>
#include <node/interface_ui.h>
#include <util/translation.h>
#include <util/time.h>
#include <validation.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include <QVariantMap>
#include <QTimer>

namespace {
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;
using ::testing::Truly;

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

struct PeerCountState {
    std::atomic<size_t> total{0};
    std::atomic<size_t> inbound{0};
    std::atomic<size_t> outbound{0};
};

void InstallDefaultHandlers(NiceMock<MockNode>& node)
{
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyBlockTipFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyHeaderTip(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyHeaderTipFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyNumConnectionsChangedFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyNetworkActiveChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyNetworkActiveChangedFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleNotifyAlertChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::NotifyAlertChangedFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::MessageBoxFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleQuestion(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::QuestionFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleBannedListChanged(testing::_))
        .WillByDefault(Invoke([](interfaces::Node::BannedListChangedFn) {
            return MakeNoopHandler();
        }));
    ON_CALL(node, getWarnings()).WillByDefault(Return(Untranslated("")));
}

void InstallPeerCountGetters(NiceMock<MockNode>& node, PeerCountState& peers)
{
    ON_CALL(node, getNodeCount(testing::_)).WillByDefault(Invoke([&peers](ConnectionDirection direction) {
        switch (direction) {
        case ConnectionDirection::Both:
            return peers.total.load();
        case ConnectionDirection::In:
            return peers.inbound.load();
        case ConnectionDirection::Out:
            return peers.outbound.load();
        case ConnectionDirection::None:
            return size_t{0};
        }
        return size_t{0};
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
    void peerCountsInitializeAndRefreshFromDirectionalNodeCounts();
    void disconnectPeerReturnsNodeResult();
    void banPeerRejectsInvalidInputs();
    void banPeerReturnsFalseWithoutDisconnectWhenBackendFails();
    void banPeerDisconnectsAddressAfterSuccessfulBan();
    void requestShutdownEmitsOnlyOnce();
    void initializationFailureRequestsShutdownWhenCoreWasInterrupted();
    void initializationFailureWithoutCoreInterruptOnlySetsErrorState();
    void initializationSuccessDuringCoreShutdownSkipsReadyState();
    void destructorUnsubscribesCoreSignalsBeforeStoppingPolling();
    void nodeNotificationHandlersUpdateModelThroughQueuedSignals();
    void blockTipUpdatesQueuedAcrossThreadsRetainPayloadValues();
    void blockSyncActiveFollowsInitializationAndBlockTipState();
    void alertNotificationsRefreshWarningList();
    void headerTipNotificationsExposeHeaderSyncProgress();
    void startupWarningsAreShownOnceAndDoNotBecomeCurrentWarnings();
    void runtimeMessageHandlerOpensAfterInitialization();
    void runtimeQuestionHandlerBlocksForAnswerAndReturnsResult();
    void runtimeStartupQuestionFailureLetsInitializeResultRequestShutdown();
    void runtimeStartupErrorDialogLetsInitializeResultRequestShutdown();
    void runtimeDialogDefaultsToOkWhenNoButtonsAreSpecified();
    void runtimeDialogExposesFullCoreButtonMask();
    void runtimeBlockingDialogsAreQueued();
    void runtimeNonBlockingDialogsAreQueued();
    void initializeFailureShowsStartupWarningsWithoutMakingThemCurrentWarnings();
    void initializeFailureUsesNodeErrorMessages();
    void runawayExceptionSetsFatalStartupError();
    void nodeInformationRowsAvoidChainmanBeforeInitialization();
    void nodeInformationRowsExposeDiagnostics();
    void initEmitsRequestedInitialize();
    void initGuardBlocksSecondEmission();
    void shutdownPollingStartsShutdownBeforeEmittingSignal();
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

void NodeModelTests::peerCountsInitializeAndRefreshFromDirectionalNodeCounts()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    PeerCountState peers;
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed_fn;

    peers.total = 3;
    peers.inbound = 1;
    peers.outbound = 2;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    InstallPeerCountGetters(node, peers);
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyNumConnectionsChangedFn fn) {
            connections_changed_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(connections_changed_fn);
    QCOMPARE(model.numPeers(), 3);
    QCOMPARE(model.numInboundPeers(), 1);
    QCOMPARE(model.numOutboundPeers(), 2);

    QSignalSpy peers_spy{&model, &NodeModel::numPeersChanged};
    QSignalSpy inbound_spy{&model, &NodeModel::numInboundPeersChanged};
    QSignalSpy outbound_spy{&model, &NodeModel::numOutboundPeersChanged};

    peers.total = 7;
    peers.inbound = 5;
    peers.outbound = 2;
    connections_changed_fn(99);

    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(inbound_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(outbound_spy.count(), 0);
    QCOMPARE(model.numPeers(), 7);
    QCOMPARE(model.numInboundPeers(), 5);
    QCOMPARE(model.numOutboundPeers(), 2);

    peers.total = 8;
    peers.inbound = 5;
    peers.outbound = 3;
    connections_changed_fn(1);

    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 2, ASYNC_TIMEOUT_MS);
    QCOMPARE(inbound_spy.count(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(outbound_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.numPeers(), 8);
    QCOMPARE(model.numInboundPeers(), 5);
    QCOMPARE(model.numOutboundPeers(), 3);
}

void NodeModelTests::disconnectPeerReturnsNodeResult()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    EXPECT_CALL(node, disconnectById(7)).Times(1).WillOnce(Return(true));
    QVERIFY(model.disconnectPeer(7));

    EXPECT_CALL(node, disconnectById(8)).Times(1).WillOnce(Return(false));
    QVERIFY(!model.disconnectPeer(8));
}

void NodeModelTests::banPeerRejectsInvalidInputs()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    EXPECT_CALL(node, ban(testing::_, testing::_)).Times(0);
    EXPECT_CALL(node, disconnectByAddress(testing::_)).Times(0);

    QVERIFY(!model.banPeer(QStringLiteral("not an address"), 3600));
    QVERIFY(!model.banPeer(QStringLiteral("127.0.0.1"), 0));
    QVERIFY(!model.banPeer(QStringLiteral("127.0.0.1"), -1));
}

void NodeModelTests::banPeerReturnsFalseWithoutDisconnectWhenBackendFails()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    EXPECT_CALL(node, ban(Truly([](const CNetAddr& value) {
        return value.ToStringAddr() == "127.0.0.1";
    }), 3600)).Times(1).WillOnce(Return(false));
    EXPECT_CALL(node, disconnectByAddress(testing::_)).Times(0);

    QVERIFY(!model.banPeer(QStringLiteral("127.0.0.1"), 3600));
}

void NodeModelTests::banPeerDisconnectsAddressAfterSuccessfulBan()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    auto is_loopback = [](const CNetAddr& value) {
        return value.ToStringAddr() == "127.0.0.1";
    };
    EXPECT_CALL(node, ban(Truly(is_loopback), 3600)).Times(1).WillOnce(Return(true));
    EXPECT_CALL(node, disconnectByAddress(Truly(is_loopback))).Times(1).WillOnce(Return(true));

    QVERIFY(model.banPeer(QStringLiteral("127.0.0.1"), 3600));
}

void NodeModelTests::requestShutdownEmitsOnlyOnce()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    model.requestShutdown();
    model.requestShutdown();

    QCOMPARE(shutdown_spy.count(), 1);
}

void NodeModelTests::initializationFailureRequestsShutdownWhenCoreWasInterrupted()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, shutdownRequested()).WillByDefault(Return(true));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy error_state_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};
    model.initializeResult(false, {});

    QCOMPARE(error_state_spy.count(), 1);
    QVERIFY(model.errorState());
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(initialized_spy.count(), 1);
}

void NodeModelTests::initializationFailureWithoutCoreInterruptOnlySetsErrorState()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, shutdownRequested()).WillByDefault(Return(false));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy error_state_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};
    model.initializeResult(false, {});

    QCOMPARE(error_state_spy.count(), 1);
    QVERIFY(model.errorState());
    QCOMPARE(shutdown_spy.count(), 0);
    QCOMPARE(initialized_spy.count(), 1);
}

void NodeModelTests::initializationSuccessDuringCoreShutdownSkipsReadyState()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, shutdownRequested()).WillByDefault(Return(true));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};
    QSignalSpy ready_state_spy{&model, &NodeModel::setTimeRatioListInitial};
    model.initializeResult(true, {});

    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(initialized_spy.count(), 0);
    QCOMPARE(ready_state_spy.count(), 0);
}

void NodeModelTests::destructorUnsubscribesCoreSignalsBeforeStoppingPolling()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    std::atomic<int> disconnected_handlers{0};
    auto counted_handler = [&] {
        return interfaces::MakeCleanupHandler([&] { ++disconnected_handlers; });
    };

    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyBlockTipFn) {
            return counted_handler();
        }));
    ON_CALL(node, handleNotifyHeaderTip(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyHeaderTipFn) {
            return counted_handler();
        }));
    ON_CALL(node, handleNotifyNumConnectionsChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyNumConnectionsChangedFn) {
            return counted_handler();
        }));
    ON_CALL(node, handleNotifyNetworkActiveChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyNetworkActiveChangedFn) {
            return counted_handler();
        }));
    ON_CALL(node, handleNotifyAlertChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyAlertChangedFn) {
            return counted_handler();
        }));
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::MessageBoxFn) {
            return counted_handler();
        }));
    ON_CALL(node, handleQuestion(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::QuestionFn) {
            return counted_handler();
        }));
    ON_CALL(node, handleBannedListChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::BannedListChangedFn) {
            return counted_handler();
        }));

    auto model{std::make_unique<NodeModel>(node)};
    WaitForInitialMempoolRefresh(mempool);

    const int refresh_calls = mempool.max_usage_calls.load();
    model->setMempoolInfoPollingActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(mempool.max_usage_calls.load() > refresh_calls, ASYNC_TIMEOUT_MS);

    bool checked_shutdown_order{false};
    QObject::connect(model.get(), &NodeModel::mempoolInfoPollingActiveChanged, [&](bool active) {
        if (!active) {
            checked_shutdown_order = true;
            QCOMPARE(disconnected_handlers.load(), 8);
        }
    });

    model.reset();

    QVERIFY(checked_shutdown_order);
    QCOMPARE(disconnected_handlers.load(), 8);
}

void NodeModelTests::nodeNotificationHandlersUpdateModelThroughQueuedSignals()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    PeerCountState peers;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;
    interfaces::Node::NotifyNumConnectionsChangedFn connections_changed_fn;
    interfaces::Node::BannedListChangedFn banned_list_changed_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    InstallPeerCountGetters(node, peers);
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
    QSignalSpy peers_spy{&model, &NodeModel::numPeersChanged};
    QSignalSpy inbound_peers_spy{&model, &NodeModel::numInboundPeersChanged};
    QSignalSpy outbound_peers_spy{&model, &NodeModel::numOutboundPeersChanged};
    QSignalSpy banned_list_spy{&model, &NodeModel::bannedListChanged};

    block_tip_fn(SynchronizationState::POST_INIT, interfaces::BlockTip{144, 1'700'000'000, uint256{}}, 0.42);
    peers.total = 9;
    peers.inbound = 2;
    peers.outbound = 7;
    connections_changed_fn(7);
    banned_list_changed_fn();

    QTRY_COMPARE_WITH_TIMEOUT(block_tip_height_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(verification_progress_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(time_ratio_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(inbound_peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(outbound_peers_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(banned_list_spy.count(), 1, ASYNC_TIMEOUT_MS);

    QCOMPARE(model.blockTipHeight(), 144);
    QCOMPARE(model.verificationProgress(), 0.42);
    QCOMPARE(time_ratio_spy.takeFirst().at(0).toInt(), 1'700'000'000);
    QCOMPARE(model.numPeers(), 9);
    QCOMPARE(model.numInboundPeers(), 2);
    QCOMPARE(model.numOutboundPeers(), 7);
}

void NodeModelTests::blockTipUpdatesQueuedAcrossThreadsRetainPayloadValues()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyBlockTipFn fn) {
            block_tip_fn = std::move(fn);
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

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);

    std::vector<double> seen_progress;
    std::vector<int> seen_heights;
    std::vector<int> seen_times;

    QObject::connect(&model, &NodeModel::verificationProgressChanged, &model, [&] {
        seen_progress.push_back(model.verificationProgress());
    });
    QObject::connect(&model, &NodeModel::blockTipHeightChanged, &model, [&] {
        seen_heights.push_back(model.blockTipHeight());
    });
    QObject::connect(&model, &NodeModel::setTimeRatioList, &model, [&](int block_time) {
        seen_times.push_back(block_time);
    });

    std::thread worker([&] {
        block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{123, 1'700'000'001, uint256{}}, 0.51);
        block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{456, 1'700'000'099, uint256{}}, 0.75);
    });
    worker.join();

    QTRY_COMPARE_WITH_TIMEOUT(seen_progress.size(), size_t{2}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(seen_heights.size(), size_t{2}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(seen_times.size(), size_t{2}, ASYNC_TIMEOUT_MS);

    QVERIFY(qFuzzyCompare(seen_progress.at(0), 0.51));
    QVERIFY(qFuzzyCompare(seen_progress.at(1), 0.75));
    QCOMPARE(seen_heights.at(0), 123);
    QCOMPARE(seen_heights.at(1), 456);
    QCOMPARE(seen_times.at(0), 1'700'000'001);
    QCOMPARE(seen_times.at(1), 1'700'000'099);
    QCOMPARE(model.blockTipHeight(), 456);
    QVERIFY(qFuzzyCompare(model.verificationProgress(), 0.75));
}

void NodeModelTests::blockSyncActiveFollowsInitializationAndBlockTipState()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;
    bool initial_block_download{true};

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, isInitialBlockDownload()).WillByDefault(Invoke([&] { return initial_block_download; }));
    ON_CALL(node, handleNotifyBlockTip(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyBlockTipFn fn) {
            block_tip_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(block_tip_fn);
    QVERIFY(!model.blockSyncActive());

    QSignalSpy block_sync_spy{&model, &NodeModel::blockSyncActiveChanged};
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 0,
        .block_time = 1'700'000'000,
        .header_height = 100,
        .header_time = GetTime(),
        .verification_progress = 0.25,
    });

    QCOMPARE(block_sync_spy.count(), 0);
    QVERIFY(!model.blockSyncActive());

    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 100,
        .block_time = 1'700'000'000,
        .header_height = 100,
        .header_time = GetTime(),
        .verification_progress = 0.25,
    });

    QCOMPARE(block_sync_spy.count(), 1);
    QVERIFY(model.blockSyncActive());

    block_tip_fn(SynchronizationState::POST_INIT, interfaces::BlockTip{101, 1'700'000'001, uint256{}}, 0.25);
    QTRY_COMPARE_WITH_TIMEOUT(block_sync_spy.count(), 2, ASYNC_TIMEOUT_MS);
    QVERIFY(!model.blockSyncActive());

    block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{102, 1'700'000'002, uint256{}}, 0.26);
    QTRY_COMPARE_WITH_TIMEOUT(block_sync_spy.count(), 3, ASYNC_TIMEOUT_MS);
    QVERIFY(model.blockSyncActive());
}

void NodeModelTests::alertNotificationsRefreshWarningList()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::NotifyAlertChangedFn alert_changed_fn;
    bilingual_str warnings{Untranslated("")};

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, getWarnings()).WillByDefault(Invoke([&] { return warnings; }));
    ON_CALL(node, handleNotifyAlertChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyAlertChangedFn fn) {
            alert_changed_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(alert_changed_fn);
    QVERIFY(!model.hasWarnings());

    QSignalSpy warnings_spy{&model, &NodeModel::warningsChanged};
    warnings = bilingual_str{"first<hr />second", "translated first<hr />translated second"};
    alert_changed_fn();

    QTRY_COMPARE_WITH_TIMEOUT(warnings_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.warningList(), QStringList({QStringLiteral("translated first"), QStringLiteral("translated second")}));
    QCOMPARE(model.warnings(), QStringLiteral("translated first<hr />translated second"));
    QVERIFY(model.hasWarnings());
}

void NodeModelTests::headerTipNotificationsExposeHeaderSyncProgress()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::NotifyHeaderTipFn header_tip_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleNotifyHeaderTip(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyHeaderTipFn fn) {
            header_tip_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(header_tip_fn);

    QSignalSpy header_spy{&model, &NodeModel::headerSyncChanged};
    QSignalSpy block_sync_spy{&model, &NodeModel::blockSyncActiveChanged};
    const int height{100};
    const int64_t block_time{GetTime() - 100 * Params().GetConsensus().nPowTargetSpacing};
    header_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{height, block_time, uint256{}}, /*presync=*/true);

    QTRY_COMPARE_WITH_TIMEOUT(header_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(block_sync_spy.count(), 0);
    QVERIFY(model.headerSyncActive());
    QVERIFY(model.headerPresync());
    QVERIFY(model.headerSyncProgress() > 0.45);
    QVERIFY(model.headerSyncProgress() < 0.55);
    QVERIFY(!model.blockSyncActive());
}

void NodeModelTests::startupWarningsAreShownOnceAndDoNotBecomeCurrentWarnings()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;
    interfaces::Node::NotifyAlertChangedFn alert_changed_fn;
    bilingual_str warnings{"network warning", "Translated network warning"};

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, getWarnings()).WillByDefault(Invoke([&] { return warnings; }));
    ON_CALL(node, handleNotifyAlertChanged(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::NotifyAlertChangedFn fn) {
            alert_changed_fn = std::move(fn);
            return MakeNoopHandler();
        }));
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::MessageBoxFn fn) {
            message_box_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    model.addStartupWarnings({QStringLiteral("Translated early startup warning")});
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(alert_changed_fn);
    QVERIFY(message_box_fn);

    QSignalSpy runtime_dialog_spy{&model, &NodeModel::runtimeDialogChanged};
    QVERIFY(!message_box_fn(
        bilingual_str{"Startup warning", "Translated startup warning"},
        CClientUIInterface::MSG_WARNING));
    QCOMPARE(runtime_dialog_spy.count(), 0);

    model.initializeResult(true, {});

    QCOMPARE(runtime_dialog_spy.count(), 1);
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Warning"));
    QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated early startup warning\n\nTranslated startup warning"));
    QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK));

    QCOMPARE(model.warningList(), QStringList({QStringLiteral("Translated network warning")}));
    QVERIFY(model.hasWarnings());
    QVERIFY(model.startupError().isEmpty());

    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    QCOMPARE(runtime_dialog_spy.count(), 2);
    QVERIFY(!model.runtimeDialogVisible());

    QSignalSpy warnings_spy{&model, &NodeModel::warningsChanged};
    warnings = Untranslated("");
    alert_changed_fn();

    QTRY_COMPARE_WITH_TIMEOUT(warnings_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(model.warningList(), QStringList());
    QVERIFY(!model.hasWarnings());
}

void NodeModelTests::runtimeMessageHandlerOpensAfterInitialization()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::MessageBoxFn fn) {
            message_box_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);
    model.initializeResult(true, {});

    std::atomic<bool> result{false};
    std::atomic<bool> finished{false};
    int prompt_count{0};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        ++prompt_count;
        QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Error"));
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated runtime error"));
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK));
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        });
    });

    std::thread worker([&] {
        result = message_box_fn(
            bilingual_str{"Runtime error", "Translated runtime error"},
            CClientUIInterface::MSG_ERROR);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QCOMPARE(prompt_count, 1);
    QVERIFY(result.load());
    QVERIFY(!model.runtimeDialogVisible());
}

void NodeModelTests::runtimeQuestionHandlerBlocksForAnswerAndReturnsResult()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::QuestionFn question_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleQuestion(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::QuestionFn fn) {
            question_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(question_fn);

    std::atomic<bool> result{false};
    std::atomic<bool> finished{false};
    int prompt_count{0};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        ++prompt_count;
        QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Error"));
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated rebuild?"));
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK | CClientUIInterface::BTN_ABORT));
        QVERIFY(model.runtimeDialogQuestion());
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        });
    });

    std::thread worker([&] {
        result = question_fn(
            bilingual_str{"Rebuild?", "Translated rebuild?"},
            "Non interactive",
            CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QCOMPARE(prompt_count, 1);
    QVERIFY(result.load());
    QVERIFY(!model.runtimeDialogVisible());
}

void NodeModelTests::runtimeStartupQuestionFailureLetsInitializeResultRequestShutdown()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::QuestionFn question_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleQuestion(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::QuestionFn fn) {
            question_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(question_fn);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy faulted_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy startup_error_spy{&model, &NodeModel::startupErrorChanged};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};

    std::atomic<bool> result{true};
    std::atomic<bool> finished{false};
    int prompt_count{0};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        ++prompt_count;
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated rebuild?"));
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK | CClientUIInterface::BTN_ABORT));
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_ABORT);
        });
    });

    std::thread worker([&] {
        result = question_fn(
            bilingual_str{"Rebuild?", "Translated rebuild?"},
            "Non interactive",
            CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QCOMPARE(prompt_count, 1);
    QVERIFY(!result.load());
    QCOMPARE(shutdown_spy.count(), 0);
    QVERIFY(!model.runtimeDialogVisible());

    model.initializeResult(false, {});
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(initialized_spy.count(), 1);
    QCOMPARE(faulted_spy.count(), 0);
    QCOMPARE(startup_error_spy.count(), 0);
    QVERIFY(!model.errorState());
    QVERIFY(model.startupError().isEmpty());
}

void NodeModelTests::runtimeStartupErrorDialogLetsInitializeResultRequestShutdown()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::MessageBoxFn fn) {
            message_box_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    QSignalSpy faulted_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy startup_error_spy{&model, &NodeModel::startupErrorChanged};
    QSignalSpy initialized_spy{&model, &NodeModel::nodeInitialized};

    std::atomic<bool> result{false};
    std::atomic<bool> finished{false};
    int prompt_count{0};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        ++prompt_count;
        QCOMPARE(model.runtimeDialogTitle(), QStringLiteral("Error"));
        QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated failed to initialize"));
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK));
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        });
    });

    std::thread worker([&] {
        result = message_box_fn(
            bilingual_str{"Failed to initialize", "Translated failed to initialize"},
            CClientUIInterface::MSG_ERROR);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QCOMPARE(prompt_count, 1);
    QVERIFY(result.load());
    QCOMPARE(shutdown_spy.count(), 0);
    QVERIFY(!model.runtimeDialogVisible());

    model.initializeResult(false, {});
    QCOMPARE(shutdown_spy.count(), 1);
    QCOMPARE(initialized_spy.count(), 1);
    QCOMPARE(faulted_spy.count(), 0);
    QCOMPARE(startup_error_spy.count(), 0);
    QVERIFY(!model.errorState());
    QVERIFY(model.startupError().isEmpty());
}

void NodeModelTests::runtimeDialogDefaultsToOkWhenNoButtonsAreSpecified()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::MessageBoxFn fn) {
            message_box_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);
    model.initializeResult(true, {});

    std::atomic<bool> result{false};
    std::atomic<bool> finished{false};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        QCOMPARE(model.runtimeDialogButtons(), static_cast<unsigned int>(CClientUIInterface::BTN_OK));
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
        });
    });

    std::thread worker([&] {
        result = message_box_fn(
            bilingual_str{"Information", "Translated information"},
            CClientUIInterface::ICON_INFORMATION | CClientUIInterface::MODAL);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QVERIFY(result.load());
    QVERIFY(!model.runtimeDialogVisible());
}

void NodeModelTests::runtimeDialogExposesFullCoreButtonMask()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::MessageBoxFn fn) {
            message_box_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);
    model.initializeResult(true, {});

    const unsigned int full_button_mask{CClientUIInterface::BTN_MASK};
    std::atomic<bool> result{false};
    std::atomic<bool> finished{false};
    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;
        QCOMPARE(model.runtimeDialogButtons(), full_button_mask);
        QVERIFY(model.runtimeDialogButtons() & CClientUIInterface::BTN_IGNORE);
        QVERIFY(model.runtimeDialogButtons() & CClientUIInterface::BTN_HELP);
        QVERIFY(model.runtimeDialogButtons() & CClientUIInterface::BTN_RESET);
        QTimer::singleShot(0, &model, [&model] {
            model.answerRuntimeDialog(CClientUIInterface::BTN_RESET);
        });
    });

    std::thread worker([&] {
        result = message_box_fn(
            bilingual_str{"Full button mask", "Translated full button mask"},
            CClientUIInterface::ICON_WARNING | CClientUIInterface::MODAL | full_button_mask);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();

    QVERIFY(!result.load());
    QVERIFY(!model.runtimeDialogVisible());
}

void NodeModelTests::runtimeBlockingDialogsAreQueued()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::QuestionFn question_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleQuestion(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::QuestionFn fn) {
            question_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(question_fn);
    model.initializeResult(true, {});

    QStringList prompts;
    bool first_result{false};
    bool second_result{true};

    QObject::connect(&model, &NodeModel::runtimeDialogChanged, &model, [&] {
        if (!model.runtimeDialogVisible()) return;

        prompts.push_back(model.runtimeDialogMessage());
        if (model.runtimeDialogMessage() == QStringLiteral("Translated first?")) {
            QTimer::singleShot(0, &model, [&model] {
                model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
            });
            second_result = question_fn(
                bilingual_str{"Second?", "Translated second?"},
                "Non interactive",
                CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);
        } else if (model.runtimeDialogMessage() == QStringLiteral("Translated second?")) {
            QTimer::singleShot(0, &model, [&model] {
                model.answerRuntimeDialog(CClientUIInterface::BTN_ABORT);
            });
        }
    });

    first_result = question_fn(
        bilingual_str{"First?", "Translated first?"},
        "Non interactive",
        CClientUIInterface::MSG_ERROR | CClientUIInterface::BTN_ABORT);

    QCOMPARE(prompts, QStringList({QStringLiteral("Translated first?"), QStringLiteral("Translated second?")}));
    QVERIFY(first_result);
    QVERIFY(!second_result);
    QVERIFY(!model.runtimeDialogVisible());
}

void NodeModelTests::runtimeNonBlockingDialogsAreQueued()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::MessageBoxFn fn) {
            message_box_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);
    model.initializeResult(true, {});

    QSignalSpy runtime_dialog_spy{&model, &NodeModel::runtimeDialogChanged};
    QVERIFY(!message_box_fn(
        bilingual_str{"First", "Translated first"},
        CClientUIInterface::ICON_INFORMATION));
    QCOMPARE(runtime_dialog_spy.count(), 1);
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated first"));

    QVERIFY(!message_box_fn(
        bilingual_str{"Second", "Translated second"},
        CClientUIInterface::ICON_WARNING));
    QCOMPARE(runtime_dialog_spy.count(), 1);
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated first"));

    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    QCOMPARE(runtime_dialog_spy.count(), 2);
    QVERIFY(model.runtimeDialogVisible());
    QCOMPARE(model.runtimeDialogMessage(), QStringLiteral("Translated second"));

    model.answerRuntimeDialog(CClientUIInterface::BTN_OK);
    QCOMPARE(runtime_dialog_spy.count(), 3);
    QVERIFY(!model.runtimeDialogVisible());
}

void NodeModelTests::initializeFailureShowsStartupWarningsWithoutMakingThemCurrentWarnings()
{
    NiceMock<MockNode> node;
    MempoolState mempool;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, getWarnings()).WillByDefault(Return(bilingual_str{"pre-release warning", "Translated pre-release warning"}));

    NodeModel model{node};
    model.addStartupWarnings({QStringLiteral("Translated startup warning")});
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy faulted_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy startup_error_spy{&model, &NodeModel::startupErrorChanged};
    model.initializeResult(false, {});

    QCOMPARE(faulted_spy.count(), 1);
    QCOMPARE(startup_error_spy.count(), 1);
    QVERIFY(model.errorState());
    QCOMPARE(model.warningList(), QStringList({QStringLiteral("Translated pre-release warning")}));
    QCOMPARE(model.startupError(), QStringLiteral("Startup warnings:\nTranslated startup warning\n\nNode initialization failed."));
}

void NodeModelTests::initializeFailureUsesNodeErrorMessages()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    interfaces::Node::MessageBoxFn message_box_fn;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, getWarnings()).WillByDefault(Return(bilingual_str{"pre-release warning", "Translated pre-release warning"}));
    ON_CALL(node, handleMessageBox(testing::_))
        .WillByDefault(Invoke([&](interfaces::Node::MessageBoxFn fn) {
            message_box_fn = std::move(fn);
            return MakeNoopHandler();
        }));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    QVERIFY(message_box_fn);

    QSignalSpy runtime_dialog_spy{&model, &NodeModel::runtimeDialogChanged};

    std::atomic<bool> finished{false};
    std::thread worker([&] {
        message_box_fn(
            bilingual_str{"Unable to bind original", "Translated unable to bind"},
            CClientUIInterface::ICON_ERROR);
        message_box_fn(
            bilingual_str{"Failed to listen original", "Translated failed to listen"},
            CClientUIInterface::ICON_ERROR);
        finished = true;
    });

    QTRY_VERIFY_WITH_TIMEOUT(finished.load(), ASYNC_TIMEOUT_MS);
    worker.join();
    QCOMPARE(runtime_dialog_spy.count(), 0);

    QSignalSpy faulted_spy{&model, &NodeModel::errorStateChanged};
    QSignalSpy startup_error_spy{&model, &NodeModel::startupErrorChanged};
    model.initializeResult(false, {});

    QCOMPARE(faulted_spy.count(), 1);
    QCOMPARE(startup_error_spy.count(), 1);
    QVERIFY(model.errorState());
    QCOMPARE(model.warningList(), QStringList({QStringLiteral("Translated pre-release warning")}));
    QCOMPARE(model.startupError(), QStringLiteral("Translated unable to bind\n\nTranslated failed to listen"));
}

void NodeModelTests::runawayExceptionSetsFatalStartupError()
{
    NiceMock<MockNode> node;
    MempoolState mempool;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    model.handleRunawayException(QStringLiteral("std::runtime_error: boom"));
    QVERIFY(model.errorState());
    QCOMPARE(model.startupError(), QStringLiteral("std::runtime_error: boom"));
}

void NodeModelTests::nodeInformationRowsAvoidChainmanBeforeInitialization()
{
    NiceMock<MockNode> node;
    MempoolState mempool;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    EXPECT_CALL(node, getHeaderTip(testing::_, testing::_)).Times(0);
    EXPECT_CALL(node, getNumBlocks()).Times(0);
    EXPECT_CALL(node, getLastBlockTime()).Times(0);
    EXPECT_CALL(node, getNetLocalAddresses()).Times(0);
    EXPECT_CALL(node, getNetworkActive()).Times(0);

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    const QVariantList rows = model.nodeInformationRows();
    QVERIFY(!rows.empty());

    bool saw_unknown_network_active{false};
    for (const QVariant& row : rows) {
        const QVariantMap map{row.toMap()};
        saw_unknown_network_active |=
            map.value(QStringLiteral("label")).toString() == QStringLiteral("Network active") &&
            map.value(QStringLiteral("value")).toString() == QStringLiteral("Unknown");
    }
    QVERIFY(saw_unknown_network_active);
}

void NodeModelTests::nodeInformationRowsExposeDiagnostics()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    PeerCountState peers;

    peers.total = 3;
    peers.inbound = 1;
    peers.outbound = 2;

    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    InstallPeerCountGetters(node, peers);
    ON_CALL(node, getNumBlocks()).WillByDefault(Return(321));
    ON_CALL(node, getHeaderTip(testing::_, testing::_))
        .WillByDefault(Invoke([](int& height, int64_t& block_time) {
            height = 333;
            block_time = 1'700'000'333;
            return true;
        }));
    ON_CALL(node, getLastBlockTime()).WillByDefault(Return(1'700'000'321));
    ON_CALL(node, getNetworkActive()).WillByDefault(Return(true));
    ON_CALL(node, getNetLocalAddresses()).WillByDefault(Return(std::map<CNetAddr, LocalServiceInfo>{}));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);
    model.initializeResult(true, interfaces::BlockAndHeaderTipInfo{
        .block_height = 320,
        .block_time = 1'700'000'321,
        .header_height = 333,
        .header_time = 1'700'000'333,
        .verification_progress = 0.5,
    });

    const QVariantList rows = model.nodeInformationRows();
    QVERIFY(!rows.empty());

    bool saw_network_active{false};
    bool saw_peer_counts{false};
    for (const QVariant& row : rows) {
        const QVariantMap map{row.toMap()};
        const QString value{map.value(QStringLiteral("value")).toString()};
        saw_network_active |= value == QStringLiteral("Yes");
        saw_peer_counts |= value == QStringLiteral("3 total (1 inbound, 2 outbound)");
    }
    QVERIFY(saw_network_active);
    QVERIFY(saw_peer_counts);
}

void NodeModelTests::initEmitsRequestedInitialize()
{
    NiceMock<MockNode> node;
    InstallDefaultHandlers(node);
    NodeModel model{node};

    QSignalSpy spy{&model, &NodeModel::requestedInitialize};
    model.startNodeInitializionThread();
    QCOMPARE(spy.count(), 1);
}

void NodeModelTests::initGuardBlocksSecondEmission()
{
    NiceMock<MockNode> node;
    InstallDefaultHandlers(node);
    NodeModel model{node};

    QSignalSpy spy{&model, &NodeModel::requestedInitialize};
    model.startNodeInitializionThread();
    model.startNodeInitializionThread();
    QCOMPARE(spy.count(), 1);
}

void NodeModelTests::shutdownPollingStartsShutdownBeforeEmittingSignal()
{
    NiceMock<MockNode> node;
    MempoolState mempool;
    InstallDefaultHandlers(node);
    InstallMempoolGetters(node, mempool);
    ON_CALL(node, shutdownRequested()).WillByDefault(Return(true));

    NodeModel model{node};
    WaitForInitialMempoolRefresh(mempool);

    QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
    bool started_before_signal{false};
    EXPECT_CALL(node, startShutdown()).WillOnce(Invoke([&] {
        started_before_signal = shutdown_spy.count() == 0;
    }));

    model.startShutdownPolling();

    QTRY_COMPARE_WITH_TIMEOUT(shutdown_spy.count(), 1, ASYNC_TIMEOUT_MS);
    QVERIFY(started_before_signal);

    model.requestShutdown();
    QCOMPARE(shutdown_spy.count(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NodeModelTests)
#else
QTEST_MAIN(NodeModelTests)
#endif
#include "test_nodemodel.moc"
