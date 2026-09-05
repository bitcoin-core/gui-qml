// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/models/nodemodel.h>
#include <qml/test/integration_test_registry.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/fs.h>

#include <QObject>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QSettings>
#include <QString>
#include <QStringLiteral>
#include <QTest>
#include <QTemporaryDir>

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {
constexpr int NODE_LIFECYCLE_TIMEOUT_MS{30'000};
} // namespace

class ApplicationTests : public QObject
{
    Q_OBJECT

public:
    explicit ApplicationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void applicationTests()
    {
        QVERIFY(QDir{QStringLiteral(":/translations")}.exists(QStringLiteral("bitcoin_es.qm")));
        QVERIFY(QDir{QStringLiteral(":/translations")}.exists(QStringLiteral("bitcoin_qml_es.qm")));
        QCOMPARE(m_app.nodeModel().state(), NodeModel::RUNNING);

        QObject* const root{m_app.engine().rootObjects().constFirst()};
        QTRY_COMPARE_WITH_TIMEOUT(
            root->property("nodeStatus").toString(),
            QStringLiteral("Node is running"),
            1'000);

        const UniValue blockchain_info{
            m_app.node().executeRpc("getblockchaininfo", UniValue{UniValue::VARR}, "")};
        QCOMPARE(
            QString::fromStdString(blockchain_info.find_value("chain").get_str()),
            QStringLiteral("regtest"));

    }

private:
    BitcoinQmlApplication& m_app;
};

int RunApplicationTests(int argc, char* argv[])
{
    Q_INIT_RESOURCE(bitcoin_qml);
    Q_INIT_RESOURCE(bitcoin_compat);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QTemporaryDir settings_dir;
    if (!settings_dir.isValid()) return EXIT_FAILURE;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, settings_dir.path());
    BitcoinQmlApplication app{argc, argv};

    fs::create_directories([] {
        BasicTestingSetup setup{ChainType::REGTEST};
        return gArgs.GetDataDirNet() / "blocks";
    }());

    std::unique_ptr<interfaces::Init> init{interfaces::MakeGuiInit(argc, argv)};
    gArgs.ForceSetArg("-listen", "0");
    gArgs.ForceSetArg("-listenonion", "0");
    gArgs.ForceSetArg("-discover", "0");
    gArgs.ForceSetArg("-dnsseed", "0");
    gArgs.ForceSetArg("-fixedseeds", "0");
    gArgs.ForceSetArg("-natpmp", "0");

    std::string error;
    if (!gArgs.ReadConfigFiles(error, true)) {
        std::cerr << error << '\n';
        return EXIT_FAILURE;
    }

    app.createNode(*init);
    app.parameterSetup();
    if (!app.baseInitialize() || !app.createWindow()) return EXIT_FAILURE;
    if (app.nodeModel().state() != NodeModel::IDLE) return EXIT_FAILURE;
    QSignalSpy initialized{&app.nodeModel(), &NodeModel::initializationFinished};
    app.requestInitialize();
    if (initialized.isEmpty() && !initialized.wait(NODE_LIFECYCLE_TIMEOUT_MS)) return EXIT_FAILURE;
    if (app.nodeModel().state() != NodeModel::RUNNING || !initialized.constFirst().at(0).toBool()) return EXIT_FAILURE;

    ApplicationTests tests{app};
    int status = QTest::qExec(&tests, argc, argv);
    for (const auto& entry : qmlintegration::SortedEntries()) status |= entry.run(app, argc, argv);

    QSignalSpy shutdown_complete{&app.nodeModel(), &NodeModel::shutdownComplete};
    app.requestShutdown();
    if (shutdown_complete.isEmpty() && !shutdown_complete.wait(NODE_LIFECYCLE_TIMEOUT_MS)) return EXIT_FAILURE;
    if (app.nodeModel().state() != NodeModel::STOPPED) return EXIT_FAILURE;
    return status;
}

#include <test_application.moc>
