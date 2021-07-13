// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qml/BitcoinApp/bitcoin.h>

#include <common/args.h>
#include <common/system.h>
#include <init.h>
#include <interfaces/node.h>
#include <interfaces/init.h>
#include <node/interface_ui.h>
#include <node/context.h>
#include <noui.h>
#include <qml/BitcoinApp/nodemodel.h>
#include <qt/guiconstants.h>
#include <qt/initexecutor.h>
#include <util/translation.h>
#include <util/threadnames.h>

#include <boost/signals2/connection.hpp>
#include <memory>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QStringLiteral>
#include <QUrl>

namespace {
void SetupUIArgs(ArgsManager& argsman)
{
    argsman.AddArg("-lang=<lang>", "Set language, for example \"de_DE\" (default: system locale)", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-min", "Start minimized", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-resetguisettings", "Reset all settings changed in the GUI", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-splash", strprintf("Show splash screen on startup (default: %u)", DEFAULT_SPLASHSCREEN), ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
}
} // namespace


int QmlGuiMain(int argc, char* argv[])
{
#ifdef WIN32
    util::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif

    QGuiApplication app(argc, argv);

    // Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
    SetupServerArgs(gArgs);
    SetupUIArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(Untranslated(strprintf("Error parsing command line arguments: %s", error)));
        return EXIT_FAILURE;
    }

    CheckDataDirOption(gArgs);

    if (!gArgs.ReadConfigFiles(error, true)) {
        tfm::format(std::cerr, "Error reading configuration file: %s\n", error);
        return EXIT_FAILURE;
    }

    SelectParams(gArgs.GetChainType());

    // Default printtoconsole to false for the GUI. GUI programs should not
    // print to the console unnecessarily.
    gArgs.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(gArgs);
    InitParameterInteraction(gArgs);

    std::unique_ptr<interfaces::Init> init = interfaces::MakeGuiInit(argc, argv);
    std::unique_ptr<interfaces::Node> node = init->makeNode();
    node->baseInitialize();

    NodeModel node_model;
    InitExecutor init_executor{*node};
    QObject::connect(&node_model, &NodeModel::requestedInitialize, &init_executor, &InitExecutor::initialize);
    QObject::connect(&node_model, &NodeModel::requestedShutdown, &init_executor, &InitExecutor::shutdown);
    // QObject::connect(&init_executor, &InitExecutor::initializeResult, &node_model, &NodeModel::initializeResult);
    QObject::connect(&init_executor, &InitExecutor::shutdownResult, qGuiApp, &QGuiApplication::quit, Qt::QueuedConnection);
    // QObject::connect(&init_executor, &InitExecutor::runawayException, &node_model, &NodeModel::handleRunawayException);

    qGuiApp->setQuitOnLastWindowClosed(false);
    QObject::connect(qGuiApp, &QGuiApplication::lastWindowClosed, [&] {
        node->startShutdown();
        node_model.startNodeShutdown();
    });

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(":/qt/qml"));
    engine.rootContext()->setContextProperty("nodeModel", &node_model);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/BitcoinApp/stub.qml")));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    return qGuiApp->exec();
}
