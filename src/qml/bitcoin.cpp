// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <chainparams.h>
#include <common/args.h>
#include <common/system.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <logging.h>
#include <node/interface_ui.h>
#include <noui.h>
#include <qml/appmode.h>
#ifdef __ANDROID__
#include <qml/androidnotifier.h>
#include <qml/androidcustomdatadir.h>
#endif
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/guiconstants.h>
#include <qml/models/chainmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/nodemodel.h>
#include <qml/models/options_model.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/walletlistmodel.h>
#include <qml/imageprovider.h>
#include <qml/util.h>
#include <qml/walletcontroller.h>
#include <qt/guiutil.h>
#include <qt/initexecutor.h>
#include <qt/networkstyle.h>
#include <qt/peertablemodel.h>
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
#include <QSettings>
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
Q_IMPORT_PLUGIN(QtQuick2DialogsPlugin)
Q_IMPORT_PLUGIN(QtQuick2DialogsPrivatePlugin)
Q_IMPORT_PLUGIN(QtQuick2Plugin)
Q_IMPORT_PLUGIN(QtQuick2WindowPlugin)
Q_IMPORT_PLUGIN(QtQuickControls1Plugin)
Q_IMPORT_PLUGIN(QmlFolderListModelPlugin)
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
}

AppMode SetupAppMode()
{
    bool wallet_enabled;
    AppMode::Mode mode;
    #ifdef __ANDROID__
        mode = AppMode::MOBILE;
    #else
        mode = AppMode::DESKTOP;
    #endif // __ANDROID__

    #ifdef ENABLE_WALLET
        wallet_enabled = true;
    #else
        wallet_enabled = false;
    #endif // ENABLE_WALLET

    return AppMode(mode, wallet_enabled);
}

bool InitErrorMessageBox(
    const bilingual_str& message,
    [[maybe_unused]] const std::string& caption,
    [[maybe_unused]] unsigned int style)
{
    QQmlApplicationEngine engine;

    AppMode app_mode = SetupAppMode();

    qmlRegisterSingletonInstance<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
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

bool setCustomDataDir(QString strDataDir)
{
    if(fs::exists(GUIUtil::QStringToPath(strDataDir))){
        gArgs.SoftSetArg("-datadir", fs::PathToString(GUIUtil::QStringToPath(strDataDir)));
        gArgs.ClearPathCache();
    return true;
    } else {
        return false;
    }
}

QGuiApplication* m_app;
QQmlApplicationEngine* m_engine;
boost::signals2::connection m_handler_message_box;
std::unique_ptr<interfaces::Init> m_init;
std::unique_ptr<interfaces::Node> m_node;
std::unique_ptr<interfaces::Chain> m_chain;
NodeModel* m_node_model{nullptr};
InitExecutor* m_executor{nullptr};
ChainModel* m_chain_model{nullptr};
OptionsQmlModel* m_options_model{nullptr};
int m_argc;
char** m_argv;
NetworkTrafficTower* m_network_traffic_tower;
PeerTableModel* m_peer_model;
PeerListSortProxy* m_peer_model_sort_proxy;
bool m_isOnboarded;
WalletController *m_wallet_controller;
WalletListModel *m_wallet_list_model;

bool createNode(QGuiApplication& app, QQmlApplicationEngine& engine, int& argc, char* argv[], ArgsManager& gArgs)
{
    m_engine = &engine;

    InitLogging(gArgs);
    InitParameterInteraction(gArgs);

    m_init = interfaces::MakeGuiInit(argc, argv);

    m_node = m_init->makeNode();
    m_chain = m_init->makeChain();

    if (!m_node->baseInitialize()) {
        // A dialog with detailed error will have been shown by InitError().
        return EXIT_FAILURE;
    }

    m_handler_message_box.disconnect();

    m_node_model = new NodeModel{*m_node};
    m_executor = new InitExecutor{*m_node};
    QObject::connect(m_node_model, &NodeModel::requestedInitialize, m_executor, &InitExecutor::initialize);
    QObject::connect(m_node_model, &NodeModel::requestedShutdown, m_executor, &InitExecutor::shutdown);
    QObject::connect(m_executor, &InitExecutor::initializeResult, m_node_model, &NodeModel::initializeResult);
    QObject::connect(m_executor, &InitExecutor::shutdownResult, qGuiApp, &QGuiApplication::quit, Qt::QueuedConnection);

    m_network_traffic_tower = new NetworkTrafficTower{*m_node_model};
#ifdef __ANDROID__
    AndroidNotifier android_notifier{*m_node_model};
#endif

    m_chain_model = new ChainModel{*m_chain};
    m_chain_model->setCurrentNetworkName(QString::fromStdString(ChainTypeToString(gArgs.GetChainType())));
    setupChainQSettings(m_app, m_chain_model->currentNetworkName());

    QObject::connect(m_node_model, &NodeModel::setTimeRatioList, m_chain_model, &ChainModel::setTimeRatioList);
    QObject::connect(m_node_model, &NodeModel::setTimeRatioListInitial, m_chain_model, &ChainModel::setTimeRatioListInitial);

    qGuiApp->setQuitOnLastWindowClosed(false);
    QObject::connect(qGuiApp, &QGuiApplication::lastWindowClosed, [&] {
        m_node->startShutdown();
    });

    m_peer_model = new PeerTableModel{*m_node, nullptr};
    m_peer_model_sort_proxy = new PeerListSortProxy{nullptr};
    m_peer_model_sort_proxy->setSourceModel(m_peer_model);

    m_wallet_controller = new WalletController{*m_node};

    m_wallet_list_model = new WalletListModel{*m_node, nullptr};

    m_engine->rootContext()->setContextProperty("networkTrafficTower", m_network_traffic_tower);
    m_engine->rootContext()->setContextProperty("nodeModel", m_node_model);
    m_engine->rootContext()->setContextProperty("chainModel", m_chain_model);
    m_engine->rootContext()->setContextProperty("peerTableModel", m_peer_model);
    m_engine->rootContext()->setContextProperty("peerListModelProxy", m_peer_model_sort_proxy);
    m_engine->rootContext()->setContextProperty("walletController", m_wallet_controller);
    m_engine->rootContext()->setContextProperty("walletListModel", m_wallet_list_model);

    m_options_model->setNode(&(*m_node), m_isOnboarded);

    QObject::connect(m_options_model, &OptionsQmlModel::requestedShutdown, m_executor, &InitExecutor::shutdown);

    m_engine->rootContext()->setContextProperty("optionsModel", m_options_model);

    m_node_model->startShutdownPolling();

    return true;
}

void startNodeAndTransitionSlot() { createNode(*m_app, *m_engine, m_argc, m_argv, gArgs); }

int initializeAndRunApplication(QGuiApplication* app, QQmlApplicationEngine* m_engine) {
    AppMode app_mode = SetupAppMode();

    // Register the singleton instance for AppMode with the QML engine
    qmlRegisterSingletonInstance<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);

    // Register custom QML types
    qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
    qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");

    // Load the main QML file
    m_engine->load(QUrl(QStringLiteral("qrc:///qml/pages/main.qml")));

    // Check if the QML engine failed to load the main QML file
    if (m_engine->rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    // Get the first root object as a QQuickWindow
    auto window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
    if (!window) {
        return EXIT_FAILURE;
    }

    // Install the custom message handler for qDebug()
    qInstallMessageHandler(DebugMessageHandler);

    // Log the graphics API in use
    qInfo() << "Graphics API in use:" << QmlUtil::GraphicsApi(window);

    // Execute the application
    return qGuiApp->exec();
}

bool startNode(QGuiApplication& app, QQmlApplicationEngine& engine, int& argc, char* argv[])
{
    m_engine = &engine;
    QScopedPointer<const NetworkStyle> network_style{NetworkStyle::instantiate(Params().GetChainType())};
    assert(!network_style.isNull());
    m_engine->addImageProvider(QStringLiteral("images"), new ImageProvider{network_style.data()});

    m_isOnboarded = true;

    m_options_model = new OptionsQmlModel{nullptr, m_isOnboarded};
    m_engine->rootContext()->setContextProperty("optionsModel", m_options_model);

    // the settings.json file is read and parsed before creating the node
    std::string error;
    /// Read and parse settings.json file.
    std::vector<std::string> errors;
    if (!gArgs.ReadSettingsFile(&errors)) {
        error = strprintf("Failed loading settings file:\n%s\n", MakeUnorderedList(errors));
        InitError(Untranslated(error));
        return EXIT_FAILURE;
    }

    createNode(*m_app, *m_engine, argc, argv, gArgs);

    initializeAndRunApplication(&app, m_engine);
    return true;
}

bool startOnboarding(QGuiApplication& app, QQmlApplicationEngine& engine, ArgsManager& gArgs)
{
    m_engine = &engine;
    QScopedPointer<const NetworkStyle> network_style{NetworkStyle::instantiate(Params().GetChainType())};
    assert(!network_style.isNull());
    m_engine->addImageProvider(QStringLiteral("images"), new ImageProvider{network_style.data()});

    m_isOnboarded = false;

    m_options_model = new OptionsQmlModel{nullptr, m_isOnboarded};

    if (gArgs.IsArgSet("-resetguisettings")) {
        m_options_model->defaultReset();
    }

    m_engine->rootContext()->setContextProperty("optionsModel", m_options_model);

    QObject::connect(m_options_model, &OptionsQmlModel::onboardingFinished, startNodeAndTransitionSlot);

    initializeAndRunApplication(&app, m_engine);

    return true;
}
} // namespace


int QmlGuiMain(int argc, char* argv[])
{
#ifdef WIN32
    common::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif // WIN32

    Q_INIT_RESOURCE(bitcoin_qml);
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");

    QGuiApplication::setAttribute(Qt::AA_EnableHighDpiScaling);
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    QGuiApplication app(argc, argv);

    auto m_handler_message_box = ::uiInterface.ThreadSafeMessageBox_connect(InitErrorMessageBox);

    SetupEnvironment();
    util::ThreadSetInternalName("main");

    // must be set before parsing command-line options; otherwise,
    // if invalid parameters were passed, QSetting initialization would fail
    // and the error will be displayed on terminal
    app.setOrganizationName(QAPP_ORG_NAME);
    app.setOrganizationDomain(QAPP_ORG_DOMAIN);
    app.setApplicationName(QAPP_APP_NAME_DEFAULT);

    QSettings settings;
    QString dataDir;
    dataDir = settings.value("strDataDir", dataDir).toString();

    /// Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
    SetupServerArgs(gArgs);
    SetupUIArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(strprintf(Untranslated("Cannot parse command line arguments: %s\n"), error));
        return EXIT_FAILURE;
    }

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
        SelectParams(gArgs.GetChainType());
    } catch(std::exception &e) {
        InitError(Untranslated(strprintf("%s\n", e.what())));
        return EXIT_FAILURE;
    }

    QVariant need_onboarding(true);
#ifdef __ANDROID__
    AndroidCustomDataDir custom_data_dir;
    QString storePath = custom_data_dir.readCustomDataDir();
    if (!storePath.isEmpty()) {
        custom_data_dir.setDataDir(storePath);
        need_onboarding.setValue(false);
    } else if (ConfigurationFileExists(gArgs)) {
        need_onboarding.setValue(false);
    }
#else
    if ((gArgs.IsArgSet("-datadir") && !gArgs.GetPathArg("-datadir").empty()) || fs::exists(GUIUtil::QStringToPath(dataDir)) ) {
        setCustomDataDir(dataDir);
        need_onboarding.setValue(false);
    } else if (ConfigurationFileExists(gArgs)) {
        need_onboarding.setValue(false);
    }
#endif // __ANDROID__

    if (gArgs.IsArgSet("-resetguisettings")) {
        need_onboarding.setValue(true);
    }

    // Default printtoconsole to false for the GUI. GUI programs should not
    // print to the console unnecessarily.
    gArgs.SoftSetBoolArg("-printtoconsole", false);

    GUIUtil::LogQtInfo();
    GUIUtil::LoadFont(":/fonts/inter/regular");
    GUIUtil::LoadFont(":/fonts/inter/semibold");

    m_app = &app;

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("needOnboarding", need_onboarding);

    if(need_onboarding.toBool()) {
        startOnboarding(*m_app, engine, gArgs);
    } else {
        startNode(*m_app, engine, argc, argv);
    }

    return 0;
}
