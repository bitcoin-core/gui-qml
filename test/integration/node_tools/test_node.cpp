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
#include <QLocale>

#include <stdexcept>
#include <QDateTime>
#include <QVariantMap>
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

    void invalidatedHeaderReadbackMatchesCore_data()
    {
        QTest::addColumn<int>("depth");
        QTest::newRow("shallow-20") << 20;
        QTest::newRow("deep-30") << 30;
    }

    void invalidatedHeaderReadbackMatchesCore()
    {
        QFETCH(int, depth);
        auto* model = &m_app.nodeModel();
        const auto rpc = [&](const char* method, const UniValue& params = UniValue{UniValue::VARR}) {
            try { return qmltest::Call(m_app, &interfaces::Node::executeRpc, method, params, ""); }
            catch (const UniValue& error) { throw std::runtime_error{std::string{"R38 public RPC "} + method + ": " + error.write()}; }
        };
        const auto generate = [&](int count, const char* descriptor = "raw(51)") {
            UniValue params{UniValue::VARR}; params.push_back(count); params.push_back(descriptor);
            return rpc("generatetodescriptor", params);
        };
        const auto information = [&](const QString& label) {
            for (const auto& row : model->nodeInformationRows()) {
                const auto values = row.toMap();
                if (values.value("label").toString() == label) return values.value("value").toString();
            }
            return QString{};
        };
        QVERIFY(qmltest::ReturnToNode(m_app));
        QVERIFY(qmltest::WaitForVisible(m_app, "blockClock"));
        auto* clock = qmltest::Find(m_app, "blockClock"); QVERIFY(clock);
        const int original_height = rpc("getblockcount").getInt<int>();
        QCOMPARE(generate(200).size(), 200U);
        const int height = original_height + 200;
        QTRY_COMPARE(model->blockTipHeight(), height);
        QTRY_COMPARE(information("Header height"), QString::number(height));
        QTRY_VERIFY(model->initialSyncComplete());
        QVERIFY(qmltest::Click(m_app, "nodeInformationButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "nodeInformationPopup"));
        QPointer<QObject> popup{qmltest::Find(m_app, "nodeInformationPopup")}; QVERIFY(popup);
        const auto displayed = [&](const QString& label) {
            const auto rows = model->nodeInformationRows();
            for (int row = 0; row < rows.size(); ++row) {
                if (rows[row].toMap().value("label").toString() != label) continue;
                auto* value = popup ? qmltest::Find(popup, QStringLiteral("nodeInformationValue_%1").arg(row)) : nullptr;
                return value ? value->property("text").toString() : QString{};
            }
            return QString{};
        };
        const auto observed = [&](int expected_height, int64_t expected_time) {
            return model->blockTipHeight() == expected_height && model->initialSyncComplete() &&
                information("Block height") == QString::number(expected_height) &&
                information("Header height") == QString::number(expected_height) &&
                information("Header time") == QLocale{}.toString(QDateTime::fromSecsSinceEpoch(expected_time), QLocale::ShortFormat) &&
                displayed("Block height") == QString::number(expected_height) &&
                displayed("Header height") == QString::number(expected_height) &&
                displayed("Header time") == information("Header time") && clock->property("synced").toBool();
        };
        UniValue hash_params{UniValue::VARR}; hash_params.push_back(height - depth + 1);
        const auto invalidated_hash = rpc("getblockhash", hash_params).get_str();
        UniValue invalidated{UniValue::VARR}; invalidated.push_back(invalidated_hash);
        bool invalid{false};
        const auto cleanup = qScopeGuard([&] {
            if (invalid) rpc("reconsiderblock", invalidated);
            if (popup) QMetaObject::invokeMethod(popup, "close");
        });
        const auto invalidate = [&] { rpc("invalidateblock", invalidated); invalid = true; };
        const auto reconsider = [&] { rpc("reconsiderblock", invalidated); invalid = false; };
        const auto current_header_time = [&] {
            const auto chain = rpc("getblockchaininfo");
            UniValue params{UniValue::VARR}; params.push_back(chain.find_value("bestblockhash").get_str());
            return rpc("getblockheader", params).find_value("time").getInt<int64_t>();
        };
        const auto core_matches = [&](int expected) {
            const auto chain = rpc("getblockchaininfo");
            return chain.find_value("blocks").getInt<int>() == expected &&
                chain.find_value("headers").getInt<int>() == expected &&
                !chain.find_value("initialblockdownload").get_bool();
        };
        invalidate();
        QVERIFY(core_matches(height - depth));
        const auto lowered_time = current_header_time();
        // Collect the original oracle without returning before both recovery
        // controls run. Red output therefore records genuine executed controls.
        const bool lowered = QTest::qWaitFor([&] { return observed(height - depth, lowered_time); });
        const auto observed_header = information("Header height");
        const bool observed_sync = model->initialSyncComplete();
        reconsider(); QVERIFY(core_matches(height));
        const auto restored_time = current_header_time();
        const bool reconsidered = QTest::qWaitFor([&] { return observed(height, restored_time); });
        invalidate(); QVERIFY(core_matches(height - depth));
        // A different coinbase script guarantees a new block instead of
        // recreating the same now-invalid hash within the same clock second.
        QCOMPARE(generate(1, "raw(52)").size(), 1U);
        QVERIFY(core_matches(height - depth + 1));
        const auto replacement_time = current_header_time();
        const bool new_block = QTest::qWaitFor([&] { return observed(height - depth + 1, replacement_time); });
        qInfo() << "R38 public header invalidation" << "depth" << depth << "Core height" << height - depth
                << "observed header" << observed_header << "observed synced" << observed_sync
                << "reconsider recovery" << reconsidered << "new block recovery" << new_block;
        QVERIFY(reconsidered); QVERIFY(new_block); QVERIFY(lowered);
        // The fixture has no peers. This proves the real clock's synced binding,
        // not the peer-dependent Downloading/99.9% wording in the old report.
    }

    void rpcObservationDoesNotReplayAnEarlierGuiDesire()
    {
        auto* model=&m_app.nodeModel();QTRY_VERIFY(model->networkActionReady());
        model->setPause(true);QTRY_VERIFY(!model->networkActionPending());QTRY_VERIFY(model->pause());
        const auto set_rpc=[&](bool active){UniValue params{UniValue::VARR};params.push_back(active);qmltest::Call(m_app,&interfaces::Node::executeRpc,"setnetworkactive",params,"");};
        set_rpc(true);QTRY_VERIFY(!model->pause());
        set_rpc(false);QTRY_VERIFY(model->pause());
        QVERIFY(!model->networkActionPending());
        QCOMPARE(qmltest::Call(m_app,&interfaces::Node::getNetworkActive),false);
        model->setPause(false);QTRY_VERIFY(!model->networkActionPending());QTRY_VERIFY(!model->pause());
        QCOMPARE(qmltest::Call(m_app,&interfaces::Node::getNetworkActive),true);
    }

    void runtimeQuestionMakesTheHiddenNativeWindowReachable()
    {
        auto* window=m_app.window();QVERIFY(window);window->hide();QVERIFY(!window->isVisible());
        auto answer=std::async(std::launch::async,[]{return uiInterface.ThreadSafeQuestion(Untranslated("Original hidden-window question"),"",CClientUIInterface::MSG_ERROR|CClientUIInterface::BTN_ABORT);});
        const auto release=qScopeGuard([&]{m_app.nodeModel().answerRuntimeDialog(CClientUIInterface::BTN_OK);window->show();});
        QTRY_VERIFY(window->isVisible());
        QVERIFY(qmltest::WaitForVisible(m_app,"nodeRuntimeDialogButtonOk"));
        QCOMPARE(m_app.nodeModel().runtimeDialogMessage(),QStringLiteral("Original hidden-window question"));
        QVERIFY(qmltest::Click(m_app,"nodeRuntimeDialogButtonOk"));
        QTRY_VERIFY(answer.wait_for(std::chrono::milliseconds{0})==std::future_status::ready);
        QVERIFY(answer.get());QTRY_VERIFY(!m_app.nodeModel().runtimeDialogVisible());
    }

    // #1138: a Core question that arrives while the window is minimized must
    // un-minimize it, or the node waits behind a message nobody can see.
    void runtimeQuestionRestoresTheMinimizedNativeWindow()
    {
        auto* window=m_app.window();QVERIFY(window);window->showMinimized();QTRY_VERIFY(window->windowStates().testFlag(Qt::WindowMinimized));QVERIFY(window->isVisible());
        auto answer=std::async(std::launch::async,[]{return uiInterface.ThreadSafeQuestion(Untranslated("Original minimized-window question"),"",CClientUIInterface::MSG_ERROR|CClientUIInterface::BTN_ABORT);});
        const auto release=qScopeGuard([&]{m_app.nodeModel().answerRuntimeDialog(CClientUIInterface::BTN_OK);window->showNormal();});
        QTRY_VERIFY(m_app.nodeModel().runtimeDialogVisible());
        QTRY_VERIFY(!window->windowStates().testFlag(Qt::WindowMinimized));QVERIFY(window->isVisible());
        QVERIFY(qmltest::WaitForVisible(m_app,"nodeRuntimeDialogButtonOk"));
        QCOMPARE(m_app.nodeModel().runtimeDialogMessage(),QStringLiteral("Original minimized-window question"));
        QVERIFY(qmltest::Click(m_app,"nodeRuntimeDialogButtonOk"));
        QTRY_VERIFY(answer.wait_for(std::chrono::milliseconds{0})==std::future_status::ready);
        QVERIFY(answer.get());QTRY_VERIFY(!m_app.nodeModel().runtimeDialogVisible());
    }

    void navigationStaysResponsiveDuringRpc()
    {
        auto* model = &m_app.nodeModel();
        auto* console = qmltest::Model<RpcConsoleModel>(m_app, "rpcConsoleModel");
        QVERIFY(console);
        const auto tip = qmltest::Call(m_app, &interfaces::Node::executeRpc, "getbestblockhash", UniValue{UniValue::VARR}, "").get_str();
        // Keep the RPC active until cleanup mines a block, even if the GUI is slow.
        QVERIFY(console->submitNodeCommand(QString::fromStdString("waitfornewblock 0 " + tip)));
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
        // The dialog is removed by a GUI publication after Core got its answer.
        QTRY_VERIFY(!m_app.nodeModel().runtimeDialogVisible());
        QVERIFY(!result.get());
    }

};

std::unique_ptr<QObject> CreateNodeTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<NodeIntegrationTests>(app);
}

#include <test_node.moc>
