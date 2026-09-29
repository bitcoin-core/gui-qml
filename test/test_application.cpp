// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>
#include <test/application_test_context.h>
#include <test/integration_test_registry.h>
#include <test/thread_audit.h>
#include <test/backend_barrier.h>
#include <qml/models/rpcconsolemodel.h>
#include <univalue.h>
#include <util/translation.h>

#include <QDir>
#include <QPointer>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <memory>
#include <vector>

const TranslateFn G_TRANSLATION_FUN{[](const char* text) {
    return QCoreApplication::translate("bitcoin-core", text).toStdString();
}};

class ApplicationTests : public QObject
{
    Q_OBJECT
    ApplicationTestContext& m_app;
    QSignalSpy& m_initialized;

public:
    ApplicationTests(ApplicationTestContext& app, QSignalSpy& initialized)
        : m_app(app), m_initialized(initialized) {}

private Q_SLOTS:
    void applicationStartsRealRegtestNode()
    {
        QTRY_VERIFY_WITH_TIMEOUT(!m_initialized.isEmpty(), 30'000);
        QVERIFY(!m_app.model<NodeModel>("nodeModel")->errorState());
        const auto info = m_app.call(&interfaces::Node::executeRpc, "getblockchaininfo", UniValue{UniValue::VARR}, "");
        QCOMPARE(info.find_value("chain").get_str(), std::string{"regtest"});
        QCOMPARE(m_app.call(&interfaces::Node::getNodeCount, ConnectionDirection::Both), 0U);
        QVERIFY(m_app.find("nodeRunner"));
        QVERIFY(m_app.find("blockClock"));
        QVERIFY(!m_app.find("desktopWalletsPage"));
        QVERIFY(QDir{QStringLiteral(":/translations")}.exists(QStringLiteral("bitcoin_es.qm")));
        QVERIFY(QDir{QStringLiteral(":/translations")}.exists(QStringLiteral("bitcoin_qml_es.qm")));
    }

    void shutdownShowsShutdownPage()
    {
        auto* model = m_app.model<NodeModel>("nodeModel");
        qmlintegration::ScopedAuditBarrier held{m_app.audit, "Node::getMempoolSize"};
        model->refreshMempoolInfo();
        QTRY_VERIFY(held.barrier->entered.load());
        auto* console = m_app.model<RpcConsoleModel>("rpcConsoleModel");
        QVERIFY(console);
        auto rpc_entered = std::make_shared<std::atomic<bool>>(false);
        m_app.audit->setObserver([gate = held.barrier, rpc_entered](const char* method) {
            if (std::string_view{method} == "Node::getMempoolSize") gate->enter();
            if (std::string_view{method} == "Node::executeRpc") *rpc_entered = true;
        });
        QVERIFY(console->submitCommand("waitfornewblock 0"));
        QTRY_VERIFY(rpc_entered->load());
        QVERIFY(console->executing());
        QSignalSpy requested{model, &NodeModel::requestedShutdown};
        QVERIFY(console->submitCommand("stop"));
        QCOMPARE(requested.count(), 1);
        QVERIFY(m_app.window()->property("shutdownInProgress").toBool());
        QVERIFY(m_app.find("shutdownPage"));
        model->requestShutdown();
        QCOMPARE(requested.count(), 1);
        bool delivered{false};
        QTimer::singleShot(0, model, [&] { delivered = true; });
        QTRY_VERIFY(delivered);
        QVERIFY(!held.barrier->timed_out);
        // Leaving this scope releases the read; application exit additionally
        // proves the blocking RPC was interrupted and every worker drained.
    }
};

int main(int argc, char* argv[])
{
    // Settings paths must be isolated before the production QApplication and
    // its QML settings objects are constructed.
    QTemporaryDir profile;
    if (!profile.isValid()) return EXIT_FAILURE;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    for (const auto format : {QSettings::IniFormat, QSettings::NativeFormat}) {
        QSettings::setPath(format, QSettings::UserScope, profile.filePath("gui"));
        QSettings::setPath(format, QSettings::SystemScope, profile.filePath("system"));
    }
    std::vector<QByteArray> arguments{
        argv[0], "-regtest", "-datadir=" + profile.path().toUtf8(), "-qml_onboarded=1",
        "-disablewallet", "-listen=0", "-listenonion=0", "-connect=0", "-dnsseed=0",
        "-fixedseeds=0", "-discover=0", "-natpmp=0", "-printtoconsole=0"};
    std::vector<char*> gui_argv;
    gui_argv.reserve(arguments.size() + 1);
    for (auto& argument : arguments) gui_argv.push_back(argument.data());
    gui_argv.push_back(nullptr);

    int status{EXIT_FAILURE};
    QPointer<QQmlApplicationEngine> engine_guard;
    std::shared_ptr<qmlintegration::ThreadAudit> audit;
    QmlApplicationHooks hooks;
    hooks.window_created = [&](interfaces::Node& node, QQmlApplicationEngine& engine) {
        audit->setPhase(qmlintegration::TestPhase::Running);
        engine_guard = &engine;
        ApplicationTestContext context{node, engine, audit};
        auto initialized = std::make_shared<QSignalSpy>(context.model<NodeModel>("nodeModel"), &NodeModel::nodeInitialized);
        QObject::connect(context.model<NodeModel>("nodeModel"), &NodeModel::requestedShutdown, &engine, [audit] { audit->setPhase(qmlintegration::TestPhase::Shutdown); });
        QTimer::singleShot(0, &engine, [&, context, initialized]() mutable {
            ApplicationTests lifecycle{context, *initialized};
            char startup_case[]{"applicationStartsRealRegtestNode"};
            char shutdown_case[]{"shutdownShowsShutdownPage"};
            char* lifecycle_argv[]{argv[0], startup_case, nullptr};
            status = QTest::qExec(&lifecycle, 2, lifecycle_argv);
            if (status == 0) {
                try {
                    for (const auto& entry : qmlintegration::SortedEntries()) status |= entry.run(context, argc, argv);
                } catch (const std::exception& error) {
                    std::cerr << "Integration test exception: " << error.what() << '\n';
                    status = EXIT_FAILURE;
                }
            }
            lifecycle_argv[1] = shutdown_case;
            status |= QTest::qExec(&lifecycle, 2, lifecycle_argv);
            // Also request shutdown after a failed shutdown assertion.
            context.model<NodeModel>("nodeModel")->requestShutdown();
        });
    };
    hooks.interfaces_created = [&](std::unique_ptr<interfaces::Node>& node, std::unique_ptr<interfaces::Chain>& chain) {
        audit = std::make_shared<qmlintegration::ThreadAudit>();
        node = qmlintegration::CheckNode(std::move(node), audit);
        chain = qmlintegration::CheckChain(std::move(chain), audit);
    };
    const int application_status = RunQmlApplication(static_cast<int>(arguments.size()), gui_argv.data(), hooks);
    // Returning from the real GUI entry point proves executor shutdown, worker
    // joins and QML destruction completed, including when a feature test fails.
    return status | application_status | (engine_guard ? EXIT_FAILURE : EXIT_SUCCESS);
}

#include <test_application.moc>
