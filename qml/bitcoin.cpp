// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <chainparams.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <logging.h>
#include <node/interface_ui.h>
#include <noui.h>
#include <qml/appmode.h>
#include <qml/chainmodel.h>
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/networktraffictower.h>
#include <qml/imageprovider.h>
#include <qml/nodemodel.h>
#include <qml/options_model.h>
#include <qml/peerlistsortproxy.h>
#include <qml/util.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/initexecutor.h>
#include <qt/networkstyle.h>
#include <qt/peertablemodel.h>
#include <util/system.h>
#include <util/threadnames.h>
#include <util/translation.h>

#include <boost/signals2/connection.hpp>
#include <cassert>
#include <memory>
#include <tuple>

#include <QDebug>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickWindow>
#include <QString>
#include <QStyleHints>
#include <QUrl>

QT_BEGIN_NAMESPACE
class QMessageLogContext;
QT_END_NAMESPACE

#if defined(QT_STATICPLUGIN)
#include <QtPlugin>
Q_IMPORT_PLUGIN(QtQmlPlugin)
Q_IMPORT_PLUGIN(QtQmlModelsPlugin)
Q_IMPORT_PLUGIN(QtQuick2Plugin)
Q_IMPORT_PLUGIN(QtQuick2WindowPlugin)
Q_IMPORT_PLUGIN(QtQuickControls1Plugin)
Q_IMPORT_PLUGIN(QmlSettingsPlugin)
Q_IMPORT_PLUGIN(QtQuickLayoutsPlugin)
Q_IMPORT_PLUGIN(QtQuickControls2Plugin)
Q_IMPORT_PLUGIN(QtQuickTemplates2Plugin)
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

bool ConfigurationFileExists(ArgsManager& argsman)
{
    fs::path settings_path;
    if (!argsman.GetSettingsPath(&settings_path)) {
        // settings file is disabled
        return true;
    }
    if (fs::exists(settings_path)) {
        return true;
    }

    const fs::path rel_config_path = argsman.GetPathArg("-conf", BITCOIN_CONF_FILENAME);
    const fs::path abs_config_path = AbsPathForConfigVal(argsman, rel_config_path, true);
    if (fs::exists(abs_config_path)) {
        return true;
    }

    return false;
}

void setupChainQSettings(QGuiApplication* app, QString chain)
{
    if (chain.compare("MAIN") == 0) {
        app->setApplicationName(QAPP_APP_NAME_DEFAULT);
    } else if (chain.compare("TEST") == 0) {
        app->setApplicationName(QAPP_APP_NAME_TESTNET);
    } else if (chain.compare("SIGNET") == 0) {
        app->setApplicationName(QAPP_APP_NAME_SIGNET);
    } else if (chain.compare("REGTEST") == 0) {
        app->setApplicationName(QAPP_APP_NAME_REGTEST);
    }
}
} // namespace


int QmlGuiMain(int argc, char* argv[])
{
#ifdef WIN32
    util::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif // WIN32

    Q_INIT_RESOURCE(bitcoin_qml);
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");

    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    QGuiApplication app(argc, argv);

    auto handler_message_box = ::uiInterface.ThreadSafeMessageBox_connect(InitErrorMessageBox);

    std::unique_ptr<interfaces::Init> init = interfaces::MakeGuiInit(argc, argv);

    SetupEnvironment();
    util::ThreadSetInternalName("main");

    /// Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
    SetupServerArgs(gArgs);
    SetupUIArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(strprintf(Untranslated("Cannot parse command line arguments: %s\n"), error));
        return EXIT_FAILURE;
    }

    // must be set before OptionsModel is initialized or translations are loaded,
    // as it is used to locate QSettings
    app.setOrganizationName(QAPP_ORG_NAME);
    app.setOrganizationDomain(QAPP_ORG_DOMAIN);
    app.setApplicationName(QAPP_APP_NAME_DEFAULT);

    /// Determine availability of data directory.
    if (!CheckDataDirOption(gArgs)) {
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
    std::vector<std::string> errors;
    if (!gArgs.ReadSettingsFile(&errors)) {
        error = strprintf("Failed loading settings file:\n%s\n", MakeUnorderedList(errors));
        InitError(Untranslated(error));
        return EXIT_FAILURE;
    }

    QVariant need_onboarding(true);
    if (gArgs.IsArgSet("-datadir") && !gArgs.GetPathArg("-datadir").empty()) {
        need_onboarding.setValue(false);
    } else if (ConfigurationFileExists(gArgs)) {
        need_onboarding.setValue(false);
    }

    if (gArgs.IsArgSet("-resetguisettings")) {
        need_onboarding.setValue(true);
    }

    // Default printtoconsole to false for the GUI. GUI programs should not
    // print to the console unnecessarily.
    gArgs.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(gArgs);
    InitParameterInteraction(gArgs);

    GUIUtil::LogQtInfo();

    std::unique_ptr<interfaces::Node> node = init->makeNode();
    std::unique_ptr<interfaces::Chain> chain = init->makeChain();
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

    NetworkTrafficTower network_traffic_tower{node_model};

    ChainModel chain_model{*chain};
    chain_model.setCurrentNetworkName(QString::fromStdString(gArgs.GetChainName()));
    setupChainQSettings(&app, chain_model.currentNetworkName());

    QObject::connect(&node_model, &NodeModel::setTimeRatioList, &chain_model, &ChainModel::setTimeRatioList);
    QObject::connect(&node_model, &NodeModel::setTimeRatioListInitial, &chain_model, &ChainModel::setTimeRatioListInitial);

    qGuiApp->setQuitOnLastWindowClosed(false);
    QObject::connect(qGuiApp, &QGuiApplication::lastWindowClosed, [&] {
        node->startShutdown();
    });

    PeerTableModel peer_model{*node, nullptr};
    PeerListSortProxy peer_model_sort_proxy{nullptr};
    peer_model_sort_proxy.setSourceModel(&peer_model);

    GUIUtil::LoadFont(":/fonts/inter/regular");
    GUIUtil::LoadFont(":/fonts/inter/semibold");

    QQmlApplicationEngine engine;

    QScopedPointer<const NetworkStyle> network_style{NetworkStyle::instantiate(Params().NetworkIDString())};
    assert(!network_style.isNull());
    engine.addImageProvider(QStringLiteral("images"), new ImageProvider{network_style.data()});

    engine.rootContext()->setContextProperty("networkTrafficTower", &network_traffic_tower);
    engine.rootContext()->setContextProperty("nodeModel", &node_model);
    engine.rootContext()->setContextProperty("chainModel", &chain_model);
    engine.rootContext()->setContextProperty("peerTableModel", &peer_model);
    engine.rootContext()->setContextProperty("peerListModelProxy", &peer_model_sort_proxy);

    OptionsQmlModel options_model{*node};
    engine.rootContext()->setContextProperty("optionsModel", &options_model);

    engine.rootContext()->setContextProperty("needOnboarding", need_onboarding);
#ifdef __ANDROID__
    AppMode app_mode(AppMode::MOBILE);
#else
    AppMode app_mode(AppMode::DESKTOP);
#endif // __ANDROID__

    qmlRegisterSingletonInstance<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
    qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
    qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");

    engine.load(QUrl(QStringLiteral("qrc:///qml/pages/main.qml")));
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

    node_model.startShutdownPolling();
    return qGuiApp->exec();
}
