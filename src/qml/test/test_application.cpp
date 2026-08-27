// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <common/args.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/models/nodemodel.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/chaintype.h>
#include <util/fs.h>

#include <QObject>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QSignalSpy>
#include <QString>
#include <QStringLiteral>
#include <QTest>

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
        m_app.parameterSetup();
        QVERIFY(m_app.baseInitialize());
        QVERIFY(m_app.createWindow());

        QSignalSpy initialized{&m_app.nodeModel(), &NodeModel::initializationFinished};
        m_app.requestInitialize();
        if (initialized.isEmpty()) QVERIFY(initialized.wait(NODE_LIFECYCLE_TIMEOUT_MS));
        QCOMPARE(m_app.nodeModel().state(), NodeModel::RUNNING);
        QCOMPARE(initialized.constFirst().at(0).toBool(), true);

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

        QSignalSpy shutdown_complete{&m_app.nodeModel(), &NodeModel::shutdownComplete};
        m_app.requestShutdown();
        if (shutdown_complete.isEmpty()) QVERIFY(shutdown_complete.wait(NODE_LIFECYCLE_TIMEOUT_MS));
        QCOMPARE(m_app.nodeModel().state(), NodeModel::STOPPED);
    }

private:
    BitcoinQmlApplication& m_app;
};

int RunApplicationTests(int argc, char* argv[])
{
    Q_INIT_RESOURCE(bitcoin_qml);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

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
    ApplicationTests tests{app};
    return QTest::qExec(&tests, argc, argv);
}

#include <test_application.moc>
