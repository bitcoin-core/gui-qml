// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <init.h>
#include <interfaces/node.h>
#include <node/context.h>
#include <node/ui_interface.h>
#include <noui.h>
#include <qt/guiconstants.h>
#include <util/system.h>
#include <util/translation.h>

#include <boost/signals2/connection.hpp>
#include <memory>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
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
    NodeContext node_context;
    std::unique_ptr<interfaces::Node> node = interfaces::MakeNode(&node_context);

    // Subscribe to global signals from core
    boost::signals2::scoped_connection handler_message_box = ::uiInterface.ThreadSafeMessageBox_connect(noui_ThreadSafeMessageBox);
    boost::signals2::scoped_connection handler_question = ::uiInterface.ThreadSafeQuestion_connect(noui_ThreadSafeQuestion);
    boost::signals2::scoped_connection handler_init_message = ::uiInterface.InitMessage_connect(noui_InitMessage);

    Q_INIT_RESOURCE(bitcoin_qml);

    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication app(argc, argv);
    QQmlApplicationEngine engine;
    engine.load(QUrl(QStringLiteral("qrc:///qml/pages/stub.qml")));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    // Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
    SetupServerArgs(gArgs);
    SetupUIArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(strprintf(Untranslated("Error parsing command line arguments: %s\n"), error));
        return EXIT_FAILURE;
    }

    return app.exec();
}
