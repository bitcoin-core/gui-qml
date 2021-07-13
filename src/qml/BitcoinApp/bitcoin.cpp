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

    std::unique_ptr<interfaces::Init> init = interfaces::MakeGuiInit(argc, argv);

    SetupEnvironment();
    util::ThreadSetInternalName("main");

    // Subscribe to global signals from core
    boost::signals2::scoped_connection handler_message_box = ::uiInterface.ThreadSafeMessageBox_connect(noui_ThreadSafeMessageBox);
    boost::signals2::scoped_connection handler_question = ::uiInterface.ThreadSafeQuestion_connect(noui_ThreadSafeQuestion);
    boost::signals2::scoped_connection handler_init_message = ::uiInterface.InitMessage_connect(noui_InitMessage);

    QGuiApplication app(argc, argv);

    NodeModel node_model;

    QQmlApplicationEngine engine;
    engine.addImportPath(QStringLiteral(":/qt/qml"));
    engine.rootContext()->setContextProperty("nodeModel", &node_model);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/BitcoinApp/stub.qml")));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    // Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
    SetupServerArgs(gArgs);
    SetupUIArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(Untranslated(strprintf("Error parsing command line arguments: %s", error)));
        return EXIT_FAILURE;
    }

    return app.exec();
}
