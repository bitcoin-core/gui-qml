// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <interfaces/handler.h>
#include <qml/models/nodemodel.h>
#include <test/mocks/mocknode.h>
#include <util/translation.h>
#include <validation.h>

#include <thread>
#include <vector>

namespace {
constexpr auto SIGNAL_TIMEOUT{5'000};

class NoopHandler : public interfaces::Handler
{
public:
    void disconnect() override {}
};

std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return std::make_unique<NoopHandler>();
}
} // namespace

class NodeModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void blockTipUpdatesQueuedAcrossThreadsRetainPayloadValues();
};

void NodeModelTests::blockTipUpdatesQueuedAcrossThreadsRetainPayloadValues()
{
    using ::testing::_;
    using ::testing::Invoke;
    using ::testing::NiceMock;

    NiceMock<MockNode> node;
    interfaces::Node::NotifyBlockTipFn block_tip_fn;

    EXPECT_CALL(node, handleNotifyBlockTip(_)).WillOnce(Invoke([&](interfaces::Node::NotifyBlockTipFn fn) {
        block_tip_fn = std::move(fn);
        return MakeNoopHandler();
    }));
    EXPECT_CALL(node, handleNotifyNumConnectionsChanged(_)).WillOnce(Invoke([](interfaces::Node::NotifyNumConnectionsChangedFn) {
        return MakeNoopHandler();
    }));
    EXPECT_CALL(node, handleBannedListChanged(_)).WillOnce(Invoke([](interfaces::Node::BannedListChangedFn) {
        return MakeNoopHandler();
    }));

    NodeModel model{node};
    QVERIFY(block_tip_fn);

    std::vector<double> seen_progress;
    std::vector<int> seen_heights;

    QObject::connect(&model, &NodeModel::verificationProgressChanged, &model, [&] {
        seen_progress.push_back(model.verificationProgress());
    });
    QObject::connect(&model, &NodeModel::blockTipHeightChanged, &model, [&] {
        seen_heights.push_back(model.blockTipHeight());
    });

    std::thread worker([&] {
        block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{123, 1'700'000'001, uint256{}}, 0.51);
        block_tip_fn(SynchronizationState::INIT_DOWNLOAD, interfaces::BlockTip{456, 1'700'000'099, uint256{}}, 0.75);
    });
    worker.join();

    QTRY_COMPARE_WITH_TIMEOUT(seen_progress.size(), size_t{2}, SIGNAL_TIMEOUT);
    QTRY_COMPARE_WITH_TIMEOUT(seen_heights.size(), size_t{2}, SIGNAL_TIMEOUT);

    QVERIFY(qFuzzyCompare(seen_progress.at(0), 0.51));
    QVERIFY(qFuzzyCompare(seen_progress.at(1), 0.75));
    QCOMPARE(seen_heights.at(0), 123);
    QCOMPARE(seen_heights.at(1), 456);
    QCOMPARE(model.blockTipHeight(), 456);
    QVERIFY(qFuzzyCompare(model.verificationProgress(), 0.75));
}

int RunNodeModelTests(int argc, char* argv[])
{
    NodeModelTests tc;
    return QTest::qExec(&tc, argc, argv);
}

#include <test_nodemodel.moc>
