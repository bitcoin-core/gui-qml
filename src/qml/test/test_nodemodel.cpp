// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodemodel.h>
#include <qml/test/mocks/mocknode.h>
#include <qml/test/qt_test_registry.h>

#include <QSignalSpy>
#include <QTest>

#include <thread>

namespace {
interfaces::BlockAndHeaderTipInfo TipInfo(int height, double verification_progress)
{
    return {
        .block_height = height,
        .block_time = 0,
        .header_height = height,
        .header_time = 0,
        .verification_progress = verification_progress,
    };
}
} // namespace

class NodeModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void startsInitializationOnlyOnce()
    {
        MockNode node;
        NodeModel model{node};
        QSignalSpy state_spy{&model, &NodeModel::stateChanged};
        QSignalSpy initialize_spy{&model, &NodeModel::requestedInitialize};

        model.start();
        model.start();

        QCOMPARE(model.state(), NodeModel::INITIALIZING);
        QCOMPARE(state_spy.count(), 1);
        QCOMPARE(initialize_spy.count(), 1);
    }

    void initializationSuccessSetsRunningState()
    {
        MockNode node;
        NodeModel model{node};
        QSignalSpy block_tip_spy{&model, &NodeModel::blockTipChanged};
        QSignalSpy initialized_spy{&model, &NodeModel::initializationFinished};
        model.start();

        model.initializeResult(true, TipInfo(321, 0.75));

        QCOMPARE(model.state(), NodeModel::RUNNING);
        QCOMPARE(model.blockTipHeight(), 321);
        QCOMPARE(model.verificationProgress(), 0.75);
        QCOMPARE(block_tip_spy.count(), 1);
        QCOMPARE(initialized_spy.count(), 1);
        QCOMPARE(initialized_spy.takeFirst().at(0).toBool(), true);
    }

    void initializationFailureSetsErrorState()
    {
        MockNode node;
        NodeModel model{node};
        QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
        QSignalSpy initialized_spy{&model, &NodeModel::initializationFinished};
        model.start();

        model.initializeResult(false, {});

        QCOMPARE(model.state(), NodeModel::FAILED);
        QVERIFY(!model.errorMessage().isEmpty());
        QCOMPARE(shutdown_spy.count(), 0);
        QCOMPARE(initialized_spy.count(), 1);
        QCOMPARE(initialized_spy.takeFirst().at(0).toBool(), false);
    }

    void interruptedInitializationRequestsShutdown()
    {
        MockNode node;
        node.shutdown_requested_fn = [] { return true; };
        NodeModel model{node};
        QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};
        QSignalSpy initialized_spy{&model, &NodeModel::initializationFinished};
        model.start();

        model.initializeResult(false, {});

        QCOMPARE(model.state(), NodeModel::SHUTTING_DOWN);
        QCOMPARE(node.calls.startShutdown.load(), 1);
        QCOMPARE(shutdown_spy.count(), 1);
        QCOMPARE(initialized_spy.count(), 0);
    }

    void requestsShutdownOnlyOnce()
    {
        MockNode node;
        NodeModel model{node};
        QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};

        model.requestShutdown();
        model.requestShutdown();

        QCOMPARE(model.state(), NodeModel::SHUTTING_DOWN);
        QCOMPARE(node.calls.startShutdown.load(), 1);
        QCOMPARE(shutdown_spy.count(), 1);
    }

    void shutdownResultSetsStoppedState()
    {
        MockNode node;
        NodeModel model{node};
        QSignalSpy shutdown_complete_spy{&model, &NodeModel::shutdownComplete};
        model.requestShutdown();

        model.shutdownResult();

        QCOMPARE(model.state(), NodeModel::STOPPED);
        QCOMPARE(shutdown_complete_spy.count(), 1);
    }

    void runawayExceptionSetsErrorState()
    {
        MockNode node;
        NodeModel model{node};

        model.handleRunawayException(QStringLiteral("std::runtime_error: boom"));

        QCOMPARE(model.state(), NodeModel::FAILED);
        QCOMPARE(model.errorMessage(), QStringLiteral("std::runtime_error: boom"));
    }

    void blockTipNotificationUpdatesRunningNode()
    {
        MockNode node;
        NodeModel model{node};
        QCOMPARE(node.calls.handleNotifyBlockTip.load(), 1);
        model.start();
        model.initializeResult(true, TipInfo(1, 0.1));
        QSignalSpy block_tip_spy{&model, &NodeModel::blockTipChanged};

        std::thread notify_thread{[&] {
            node.notify_block_tip_fn(
                SynchronizationState{},
                {.block_height = 2, .block_time = 0, .block_hash = {}},
                0.2);
        }};
        notify_thread.join();

        QTRY_COMPARE_WITH_TIMEOUT(model.blockTipHeight(), 2, 1'000);
        QCOMPARE(model.verificationProgress(), 0.2);
        QCOMPARE(block_tip_spy.count(), 1);
    }

    void runningNodePollRequestsShutdown()
    {
        MockNode node;
        bool shutdown_requested{false};
        node.shutdown_requested_fn = [&] { return shutdown_requested; };
        NodeModel model{node};
        model.start();
        model.initializeResult(true, TipInfo(1, 0.1));
        QSignalSpy shutdown_spy{&model, &NodeModel::requestedShutdown};

        shutdown_requested = true;

        QTRY_COMPARE_WITH_TIMEOUT(model.state(), NodeModel::SHUTTING_DOWN, 1'000);
        QCOMPARE(node.calls.startShutdown.load(), 1);
        QCOMPARE(shutdown_spy.count(), 1);
    }
};

BITCOINQML_REGISTER_QT_TEST(NodeModelTests)

#include <test_nodemodel.moc>
