// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <init.h>
#include <interfaces/node.h>
#include <logging.h>
#include <node/context.h>
#include <node/ui_interface.h>
#include <noui.h>
#include <qml/nodemodel.h>
#include <qml/util.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/initexecutor.h>
#include <util/system.h>
#include <util/translation.h>

#include <boost/signals2/connection.hpp>
#include <memory>

#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QString>
#include <QStringLiteral>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QMessageLogContext;
QT_END_NAMESPACE

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QtQuick2DialogsPlugin);
Q_IMPORT_PLUGIN(QtQuick2Plugin);
Q_IMPORT_PLUGIN(QtQuick2WindowPlugin);
Q_IMPORT_PLUGIN(QtQuickControls1Plugin);
Q_IMPORT_PLUGIN(QtQuickControls2Plugin);
Q_IMPORT_PLUGIN(QtQuickTemplates2Plugin);
#endif

namespace {
void SetupUIArgs(ArgsManager& argsman)
{
    argsman.AddArg("-lang=<lang>", "Set language, for example \"de_DE\" (default: system locale)", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-min", "Start minimized", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-resetguisettings", "Reset all settings changed in the GUI", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-splash", strprintf("Show splash screen on startup (default: %u)", DEFAULT_SPLASHSCREEN), ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
}

bool InitErrorMessageBox(
    const bilingual_str& message,
    [[maybe_unused]] const std::string& caption,
    [[maybe_unused]] unsigned int style)
{
    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("message", QString::fromStdString(message.translated));
    engine.load(QUrl(QStringLiteral("qrc:///qml/pages/initerrormessage.qml")));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }
    qGuiApp->exec();
    return false;
}

/* qDebug() message handler --> debug.log */
void DebugMessageHandler(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    Q_UNUSED(context);
    if (type == QtDebugMsg) {
        LogPrint(BCLog::QT, "GUI: %s\n", msg.toStdString());
    } else {
        LogPrintf("GUI: %s\n", msg.toStdString());
    }
}
} // namespace


int QmlGuiMain(int argc, char* argv[])
{
    Q_INIT_RESOURCE(bitcoin_qml);

    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication app(argc, argv);

    auto handler_message_box = ::uiInterface.ThreadSafeMessageBox_connect(InitErrorMessageBox);

    NodeContext node_context;

    /// Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
    node_context.args = &gArgs;
    SetupServerArgs(gArgs);
    SetupUIArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(strprintf(Untranslated("Cannot parse command line arguments: %s\n"), error));
        return EXIT_FAILURE;
    }

    /// Determine availability of data directory.
    if (!CheckDataDirOption()) {
        InitError(strprintf(Untranslated("Specified data directory \"%s\" does not exist.\n"), gArgs.GetArg("-datadir", "")));
        return EXIT_FAILURE;
    }

    /// Read and parse bitcoin.conf file.
    if (!gArgs.ReadConfigFiles(error, true)) {
        InitError(strprintf(Untranslated("Cannot parse configuration file: %s\n"), error));
        return EXIT_FAILURE;
    }

    /// Check for chain settings (Params() calls are only valid after this clause).
    try {
        SelectParams(gArgs.GetChainName());
    } catch(std::exception &e) {
        InitError(Untranslated(strprintf("%s\n", e.what())));
        return EXIT_FAILURE;
    }

    /// Read and parse settings.json file.
    if (!gArgs.InitSettings(error)) {
        InitError(Untranslated(error));
        return EXIT_FAILURE;
    }

    // Default printtoconsole to false for the GUI. GUI programs should not
    // print to the console unnecessarily.
    gArgs.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(gArgs);
    InitParameterInteraction(gArgs);

    GUIUtil::LogQtInfo();

    std::unique_ptr<interfaces::Node> node = interfaces::MakeNode(&node_context);
    if (!node->baseInitialize()) {
        // A dialog with detailed error will have been shown by InitError().
        return EXIT_FAILURE;
    }

    handler_message_box.disconnect();

    NodeModel node_model{*node};
    InitExecutor init_executor{*node};
    QObject::connect(&node_model, &NodeModel::requestedInitialize, &init_executor, &InitExecutor::initialize);
    QObject::connect(&node_model, &NodeModel::requestedShutdown, &init_executor, &InitExecutor::shutdown);
    QObject::connect(&init_executor, &InitExecutor::initializeResult, &node_model, &NodeModel::initializeResult);
    QObject::connect(&init_executor, &InitExecutor::shutdownResult, qGuiApp, &QGuiApplication::quit, Qt::QueuedConnection);
    // QObject::connect(&init_executor, &InitExecutor::runawayException, &node_model, &NodeModel::handleRunawayException);

    qGuiApp->setQuitOnLastWindowClosed(false);
    QObject::connect(qGuiApp, &QGuiApplication::lastWindowClosed, [&] {
        node->startShutdown();
        node_model.startNodeShutdown();
    });

    QQmlApplicationEngine engine;
    engine.rootContext()->setContextProperty("nodeModel", &node_model);

    engine.load(QUrl(QStringLiteral("qrc:///qml/pages/stub.qml")));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    auto window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (!window) {
        return EXIT_FAILURE;
    }

    // Install qDebug() message handler to route to debug.log
    qInstallMessageHandler(DebugMessageHandler);

    qInfo() << "Graphics API in use:" << QmlUtil::GraphicsApi(window);

    return qGuiApp->exec();
}
