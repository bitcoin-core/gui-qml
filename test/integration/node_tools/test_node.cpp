// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>
#include <qml/models/rpcconsolemodel.h>
#include <univalue.h>
#include <node/interface_ui.h>
#include <util/translation.h>

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

    void networkActionsAndCoreNotificationsAgree_data()
    {
        QTest::addColumn<bool>("active");
        QTest::newRow("enable-network") << true;
        QTest::newRow("disable-network") << false;
    }

    void networkActionsAndCoreNotificationsAgree()
    {
        QFETCH(bool, active);
        auto* model = &m_app.nodeModel();
        QVERIFY(model);
        QTRY_VERIFY(model->networkActionReady());
        model->setPause(!active);
        QTRY_VERIFY(!model->networkActionPending());
        QTRY_COMPARE(model->pause(), !active);
        QTRY_COMPARE(qmltest::Call(m_app, &interfaces::Node::getNetworkActive), active);
        qmltest::Call(m_app, &interfaces::Node::setNetworkActive, !active);
        QTRY_COMPARE(model->pause(), active);
        model->setPause(!active);
        QTRY_VERIFY(!model->networkActionPending());
        QTRY_COMPARE(model->pause(), !active);
        QTRY_COMPARE(qmltest::Call(m_app, &interfaces::Node::getNetworkActive), active);
        QCOMPARE(model->numPeers(), static_cast<int>(qmltest::Call(m_app, &interfaces::Node::getNodeCount, ConnectionDirection::Both)));
    }

    void navigationStaysResponsiveDuringRpc()
    {
        auto* model = &m_app.nodeModel();
        auto* console = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QVERIFY(console);
        const auto tip = qmltest::Call(m_app, &interfaces::Node::executeRpc, "getbestblockhash", UniValue{UniValue::VARR}, "").get_str();
        // Keep the RPC active until cleanup mines a block, even if the GUI is slow.
        QVERIFY(console->submitCommand(QString::fromStdString("waitfornewblock 0 " + tip)));
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


    void runtimeDialogsRoundTripCoreQuestions_data()
    {
        QTest::addColumn<QString>("message");
        QTest::addColumn<unsigned int>("style");
        QTest::addColumn<bool>("question");
        QTest::addColumn<QStringList>("buttons");
        QTest::addColumn<QString>("dismiss");
        using UI = CClientUIInterface;
        QTest::newRow("database-read-error") << "Error reading from database, shutting down."
            << static_cast<unsigned int>(UI::MSG_ERROR) << false << QStringList{"Ok"} << "Ok";
        QTest::newRow("deprecated-checkpoints-warning") << "Option '-checkpoints' is set but checkpoints were removed. This option has no effect."
            << static_cast<unsigned int>(UI::MSG_WARNING) << false << QStringList{"Ok"} << "Ok";
        QTest::newRow("reindex-question") << "Error opening block database.\n\nDo you want to rebuild the databases now?"
            << static_cast<unsigned int>(UI::MSG_ERROR | UI::BTN_ABORT) << true << QStringList{"Ok", "Abort"} << "Abort";
        QTest::newRow("network-options-error") << "Cannot provide specific connections and have addrman find outgoing connections at the same time."
            << static_cast<unsigned int>(UI::MSG_ERROR) << false << QStringList{"Ok"} << "Ok";
        QTest::newRow("abort-retry-ignore") << "Failed to listen on any port. Use -listen=0 if you want this."
            << static_cast<unsigned int>(UI::ICON_ERROR | UI::MODAL | UI::BTN_ABORT | UI::BTN_RETRY | UI::BTN_IGNORE)
            << true << QStringList{"Abort", "Retry", "Ignore"} << "Abort";
    }

    void runtimeDialogsRoundTripCoreQuestions()
    {
        QFETCH(QString, message);
        QFETCH(unsigned int, style);
        QFETCH(bool, question);
        QFETCH(QStringList, buttons);
        QFETCH(QString, dismiss);
        QObject observer;
        bool answered{false};
        connect(&m_app.nodeModel(), &NodeModel::runtimeDialogChanged, &observer, [&] {
            if (!m_app.nodeModel().runtimeDialogVisible()) return;
            QTimer::singleShot(0, &observer, [&] {
                const auto close_dialog = qScopeGuard([&] { m_app.nodeModel().answerRuntimeDialog(CClientUIInterface::BTN_ABORT); });
                QCOMPARE(m_app.nodeModel().runtimeDialogMessage(), message);
                QCOMPARE(m_app.nodeModel().runtimeDialogQuestion(), question);
                for (const auto& button : buttons) {
                    QVERIFY(qmltest::WaitForVisible(m_app, qPrintable("nodeRuntimeDialogButton" + button)));
                }
                QVERIFY(qmltest::Click(m_app, qPrintable("nodeRuntimeDialogButton" + dismiss)));
                answered = true;
            });
        });
        auto result = std::async(std::launch::async, [=] {
            if (question) return uiInterface.ThreadSafeQuestion(Untranslated(message.toStdString()), "", style);
            uiInterface.ThreadSafeMessageBox(Untranslated(message.toStdString()), style);
            return false;
        });
        QTRY_VERIFY(result.wait_for(std::chrono::milliseconds{0}) == std::future_status::ready);
        QVERIFY(answered);
        QVERIFY(!m_app.nodeModel().runtimeDialogVisible());
        QVERIFY(!result.get());
    }

};

std::unique_ptr<QObject> CreateNodeTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<NodeIntegrationTests>(app);
}

#include <test_node.moc>
