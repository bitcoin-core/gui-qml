// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or https://opensource.org/license/mit/.

#include <qml/BitcoinApp/bitcoin.h>

#include <chainparams.h>
#include <common/args.h>
#include <common/system.h>
#include <init.h>
#include <interfaces/node.h>
#include <interfaces/init.h>
#include <logging.h>
#include <node/interface_ui.h>
#include <node/context.h>
#include <noui.h>
#include <qml/BitcoinApp/imageprovider.h>
#include <qml/BitcoinApp/nodemodel.h>
#include <qml/BitcoinApp/util.h>
#include <qt/guiconstants.h>
#include <qt/initexecutor.h>
#include <qt/networkstyle.h>
#include <util/translation.h>
#include <util/threadnames.h>

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
    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/BitcoinApp/initerrormessage.qml")));
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
        LogDebug(BCLog::QT, "GUI: %s\n", msg.toStdString());
    } else {
        LogInfo("GUI: %s\n", msg.toStdString());
    }
}
} // namespace


int QmlGuiMain(int argc, char* argv[])
{
#ifdef WIN32
    util::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif

    QGuiApplication app(argc, argv);

    auto handler_message_box = ::uiInterface.ThreadSafeMessageBox_connect(InitErrorMessageBox);

    std::unique_ptr<interfaces::Init> init = interfaces::MakeGuiInit(argc, argv);

    /// Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
    SetupServerArgs(gArgs);
    SetupUIArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(Untranslated(strprintf("Error parsing command line arguments: %s", error)));
        return EXIT_FAILURE;
    }

    /// Determine availability of data directory.
    if (!CheckDataDirOption(gArgs)) {
        InitError(Untranslated(strprintf("Specified data directory \"%s\" does not exist.\n", gArgs.GetArg("-datadir", ""))));
        return EXIT_FAILURE;
    }

    /// Read and parse bitcoin.conf file.
    if (!gArgs.ReadConfigFiles(error, true)) {
        InitError(Untranslated(strprintf("Cannot parse configuration file: %s\n", error)));
        return EXIT_FAILURE;
    }

    /// Check for chain settings (Params() calls are only valid after this clause).
    try {
        SelectParams(gArgs.GetChainType());
    } catch(std::exception &e) {
        InitError(Untranslated(strprintf("%s\n", e.what())));
        return EXIT_FAILURE;
    }

    // Default printtoconsole to false for the GUI. GUI programs should not
    // print to the console unnecessarily.
    gArgs.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(gArgs);
    InitParameterInteraction(gArgs);

    std::unique_ptr<interfaces::Node> node = init->makeNode();
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
    engine.addImportPath(QStringLiteral(":/qt/qml"));

    QScopedPointer<const NetworkStyle> network_style{NetworkStyle::instantiate(Params().GetChainType())};
    assert(!network_style.isNull());
    engine.addImageProvider(QStringLiteral("images"), new ImageProvider{network_style.data()});

    engine.rootContext()->setContextProperty("nodeModel", &node_model);

    engine.load(QUrl(QStringLiteral("qrc:/qt/qml/BitcoinApp/stub.qml")));
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
