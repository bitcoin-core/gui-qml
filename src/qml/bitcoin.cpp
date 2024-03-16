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
#endif
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/models/chainmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/nodemodel.h>
#include <qml/models/onboardingmodel.h>
#include <qml/models/options_model.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/imageprovider.h>
#include <qml/util.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/initexecutor.h>
#include <qt/networkstyle.h>
#include <qt/optionsmodel.h>
#include <qt/peertablemodel.h>
#include <util/threadnames.h>
#include <util/translation.h>

#include <univalue.h>

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
    argsman.AddArg("-splash", strprintf("Show splash screen on startup (default: %u)", DEFAULT_SPLASHSCREEN), ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
}

bool InitErrorMessageBox(
    const bilingual_str& message,
    [[maybe_unused]] const std::string& caption,
    [[maybe_unused]] unsigned int style)
{
    QQmlApplicationEngine engine;
#ifdef __ANDROID__
    AppMode app_mode(AppMode::MOBILE);
#else
    AppMode app_mode(AppMode::DESKTOP);
#endif // __ANDROID__

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

// added this function to set custom data directory when starting node
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
} // namespace

BitcoinQmlApplication::BitcoinQmlApplication(int& argc, char* argv[])
    : QGuiApplication(argc, argv), m_gArgs(&gArgs), m_argc(argc), m_argv(argv)
{
}

bool BitcoinQmlApplication::createNode(QQmlApplicationEngine& engine, int& argc, char* argv[], ArgsManager& gArgs)
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

    // Added this signal to account for NodeModel being initialized later
    QObject::connect(m_onboarding_model, &OnboardingModel::requestedShutdown, m_executor, &InitExecutor::shutdown);

    m_network_traffic_tower = new NetworkTrafficTower{*m_node_model};
#ifdef __ANDROID__
    AndroidNotifier android_notifier{m_node_model};
#endif

    m_chain_model = new ChainModel{*m_chain};
    m_chain_model->setCurrentNetworkName(QString::fromStdString(ChainTypeToString(m_gArgs->GetChainType())));
    setupChainQSettings(this, m_chain_model->currentNetworkName());

    QObject::connect(m_node_model, &NodeModel::setTimeRatioList, m_chain_model, &ChainModel::setTimeRatioList);
    QObject::connect(m_node_model, &NodeModel::setTimeRatioListInitial, m_chain_model, &ChainModel::setTimeRatioListInitial);

    qGuiApp->setQuitOnLastWindowClosed(false);
    QObject::connect(qGuiApp, &QGuiApplication::lastWindowClosed, [&] {
        m_node->startShutdown();
    });

    m_peer_model = new PeerTableModel{*m_node, nullptr};
    m_peer_model_sort_proxy = new PeerListSortProxy{nullptr};
    m_peer_model_sort_proxy->setSourceModel(m_peer_model);

    m_engine->rootContext()->setContextProperty("networkTrafficTower", m_network_traffic_tower);
    m_engine->rootContext()->setContextProperty("nodeModel", m_node_model);
    m_engine->rootContext()->setContextProperty("chainModel", m_chain_model);
    m_engine->rootContext()->setContextProperty("peerTableModel", m_peer_model);
    m_engine->rootContext()->setContextProperty("peerListModelProxy", m_peer_model_sort_proxy);
    m_engine->rootContext()->setContextProperty("onboardingModel", m_onboarding_model);

    m_options_model = new OptionsQmlModel{*m_node, !m_isOnboarding};
    m_engine->rootContext()->setContextProperty("optionsModel", m_options_model);

    m_options_model->setPrune(m_onboarding_model->prune());
    m_options_model->setPruneSizeGB(m_onboarding_model->pruneSizeGB());
    m_options_model->setDbcacheSizeMiB(m_onboarding_model->dbcacheSizeMiB());
    m_options_model->setListen(m_onboarding_model->listen());
    m_options_model->setNatpmp(m_onboarding_model->natpmp());
    m_options_model->setScriptThreads(m_onboarding_model->scriptThreads());
    m_options_model->setServer(m_onboarding_model->server());
    m_options_model->setUpnp(m_onboarding_model->upnp());

    m_node_model->startShutdownPolling();

    return true;
}

bool BitcoinQmlApplication::startNode(QQmlApplicationEngine& engine, int& argc, char* argv[])
{
    m_engine = &engine;
    QScopedPointer<const NetworkStyle> network_style{NetworkStyle::instantiate(Params().GetChainType())};
    assert(!network_style.isNull());
    m_engine->addImageProvider(QStringLiteral("images"), new ImageProvider{network_style.data()});

    m_onboarding_model = new OnboardingModel;
    m_engine->rootContext()->setContextProperty("onboardingModel", m_onboarding_model);

    createNode(*m_engine, argc, argv, gArgs);

    #ifdef __ANDROID__
    AppMode app_mode(AppMode::MOBILE);
#else
    AppMode app_mode(AppMode::DESKTOP);
#endif // __ANDROID__

    qmlRegisterSingletonInstance<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
    qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
    qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");

    m_engine->load(QUrl(QStringLiteral("qrc:///qml/pages/main.qml")));
    if (m_engine->rootObjects().isEmpty()) {
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

bool BitcoinQmlApplication::startOnboarding(QQmlApplicationEngine& engine, ArgsManager& gArgs)
{
    m_engine = &engine;
    QScopedPointer<const NetworkStyle> network_style{NetworkStyle::instantiate(Params().GetChainType())};
    assert(!network_style.isNull());
    m_engine->addImageProvider(QStringLiteral("images"), new ImageProvider{network_style.data()});

    m_onboarding_model = new OnboardingModel;
    m_engine->rootContext()->setContextProperty("onboardingModel", m_onboarding_model);

    QObject::connect(m_onboarding_model, &OnboardingModel::onboardingFinished, this, &BitcoinQmlApplication::startNodeAndTransitionSlot);

    #ifdef __ANDROID__
    AppMode app_mode(AppMode::MOBILE);
#else
    AppMode app_mode(AppMode::DESKTOP);
#endif // __ANDROID__

    qmlRegisterSingletonInstance<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
    qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
    qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");

    m_engine->load(QUrl(QStringLiteral("qrc:///qml/pages/main.qml")));
    if (m_engine->rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    auto window = qobject_cast<QQuickWindow*>(m_engine->rootObjects().first());
    if (!window) {
        return EXIT_FAILURE;
    }

    return qGuiApp->exec();
}

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

    BitcoinQmlApplication app(argc, argv);

    app.setHandlerMessageBox(::uiInterface.ThreadSafeMessageBox_connect(InitErrorMessageBox));

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

    /// Read and parse settings.json file.
    std::vector<std::string> errors;
    if (!gArgs.ReadSettingsFile(&errors)) {
        error = strprintf("Failed loading settings file:\n%s\n", MakeUnorderedList(errors));
        InitError(Untranslated(error));
        return EXIT_FAILURE;
    }

    // Added the or condition to check if the data directory exists
    QVariant need_onboarding(true);
    if ((gArgs.IsArgSet("-datadir") && !gArgs.GetPathArg("-datadir").empty()) || fs::exists(GUIUtil::QStringToPath(dataDir))) {
        setCustomDataDir(dataDir);
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

    GUIUtil::LogQtInfo();
    GUIUtil::LoadFont(":/fonts/inter/regular");
    GUIUtil::LoadFont(":/fonts/inter/semibold");

    QQmlApplicationEngine engine;

    engine.rootContext()->setContextProperty("needOnboarding", need_onboarding);

    OnboardingModel onboarding_model;
    app.setOnboardingModel(&onboarding_model);
    engine.rootContext()->setContextProperty("onboardingModel", &onboarding_model);

    app.setNeedsOnboarding(need_onboarding.toBool());

    if(need_onboarding.toBool()) {
        app.startOnboarding(engine, gArgs);
    } else {
        app.startNode(engine, argc, argv);
    }

    return 0;
}