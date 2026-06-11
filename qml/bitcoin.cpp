// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <common/args.h>
#include <common/init.h>
#include <common/system.h>
#include <chainparams.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <logging.h>
#include <node/context.h>
#include <node/interface_ui.h>
#include <noui.h>
#include <qml/appmode.h>
#include <qml/bitcoinamount.h>
#include <qml/buildinfo.h>
#include <qml/clipboard.h>
#ifdef __ANDROID__
#include <qml/androidnotifier.h>
#endif
#include <qml/components/blockclockdial.h>
#include <qml/controls/linegraph.h>
#include <qml/guiconstants.h>
#include <qml/imageprovider.h>
#include <qml/initexecutor.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/banlistmodel.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/bitcoinurimodel.h>
#include <qml/models/bumptransactionmodel.h>
#include <qml/models/chainmodel.h>
#include <qml/models/debuglogmodel.h>
#include <qml/models/networktraffictower.h>
#include <qml/models/networkstatusmodel.h>
#include <qml/models/nodemodel.h>
#include <qml/models/options_model.h>
#include <qml/models/paymentrequest.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/walletlistmodel.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/qrimageprovider.h>
#include <qml/networkstyle.h>
#include <qml/util.h>
#include <qml/walletqmlcontroller.h>
#ifdef ENABLE_TEST_AUTOMATION
#include <qml/test/testbridge.h>
#endif
#include <util/threadnames.h>
#include <util/translation.h>

#include <boost/signals2/connection.hpp>
#include <cassert>
#include <memory>
#include <tuple>

#include <QDebug>
#include <QCoreApplication>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QString>
#include <QStyleHints>
#include <QTranslator>
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
#ifdef ENABLE_TEST_AUTOMATION
    argsman.AddArg("-test-automation=<path>", "Enable test automation bridge on the given Unix socket path", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
    argsman.AddArg("-test-settings-dir=<dir>", "Store QSettings in this directory while test automation is enabled", ArgsManager::ALLOW_ANY, OptionsCategory::GUI);
#endif
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
        wallet_enabled = !gArgs.GetBoolArg("-disablewallet", false);
    #else
        wallet_enabled = false;
    #endif // ENABLE_WALLET

    return AppMode(mode, wallet_enabled);
}

bool InitErrorMessageBox(
    const bilingual_str& message,
    [[maybe_unused]] unsigned int style)
{
    QQmlApplicationEngine engine;

    AppMode app_mode = SetupAppMode();
    BuildInfo build_info;

    qmlRegisterSingletonInstance<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
    qmlRegisterSingletonInstance<BuildInfo>("org.bitcoincore.qt", 1, 0, "BuildInfo", &build_info);
    engine.rootContext()->setContextProperty("message", QString::fromStdString(message.translated));
    engine.load(QUrl(QStringLiteral("qrc:///qml/pages/initerrormessage.qml")));
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }
    qGuiApp->exec();
    return false;
}

void RecordStartupWarning(QStringList& startup_warnings, const bilingual_str& message)
{
    const QString warning{QString::fromStdString(message.translated).trimmed()};
    if (!warning.isEmpty() && !startup_warnings.contains(warning)) {
        startup_warnings.push_back(warning);
    }
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

void LoadFontResource(const QString& path)
{
    if (QFontDatabase::addApplicationFont(path) < 0) {
        qWarning() << "Failed to load font resource:" << path;
    }
}

#ifdef ENABLE_TEST_AUTOMATION
void ApplyTestSettingsDir()
{
    const std::string settings_dir{gArgs.GetArg("-test-settings-dir", "")};
    if (settings_dir.empty()) {
        return;
    }

    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromStdString(settings_dir));
}
#endif
} // namespace


int QmlGuiMain(int argc, char* argv[])
{
#ifdef WIN32
    common::WinCmdLineArgs winArgs;
    std::tie(argc, argv) = winArgs.get();
#endif // WIN32

    Q_INIT_RESOURCE(bitcoin_qml);
    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");

    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    QGuiApplication app(argc, argv);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    std::unique_ptr<interfaces::Init> init = interfaces::MakeGuiInit(argc, argv);
    QStringList startup_warnings;
    auto handler_message_box = ::uiInterface.ThreadSafeMessageBox_connect(
        [&startup_warnings](const bilingual_str& message, unsigned int style) {
            if (style & CClientUIInterface::ICON_WARNING) {
                RecordStartupWarning(startup_warnings, message);
                return false;
            }
            return InitErrorMessageBox(message, style);
        });

    SetupEnvironment();
    util::ThreadSetInternalName("main");

    // must be set before parsing command-line options; otherwise,
    // if invalid parameters were passed, QSetting initialization would fail
    // and the error will be displayed on terminal.
    // must be set before OptionsModel is initialized or translations are loaded,
    // as it is used to locate QSettings
    app.setOrganizationName(QAPP_ORG_NAME);
    app.setOrganizationDomain(QAPP_ORG_DOMAIN);
    app.setApplicationName(QAPP_APP_NAME_DEFAULT);

    // Manages translators swapped on runtime language changes.
    // app_translator: bitcoin-qt strings (shared C++ layer)
    // qml_translator: QML-app-specific strings
    std::unique_ptr<QTranslator> app_translator;
    std::unique_ptr<QTranslator> qml_translator;
    const auto reset_translator = [](std::unique_ptr<QTranslator>& t) {
        if (t) { QCoreApplication::removeTranslator(t.get()); t.reset(); }
    };
    const auto install_language = [&](const QString& lang) {
        reset_translator(app_translator);
        reset_translator(qml_translator);
        if (!lang.isEmpty()) {
            auto t = std::make_unique<QTranslator>();
            if (t->load(QStringLiteral(":/translations/bitcoin_%1.qm").arg(lang)))
                { QCoreApplication::installTranslator(t.get()); app_translator = std::move(t); }

            auto tq = std::make_unique<QTranslator>();
            if (tq->load(QStringLiteral(":/translations/bitcoin_qml_%1.qm").arg(lang)))
                { QCoreApplication::installTranslator(tq.get()); qml_translator = std::move(tq); }
        }
    };

    // Parse command-line options. We do this after qt in order to show an error if there are problems parsing these.
    SetupServerArgs(gArgs, init->canListenIpc());

    SetupUIArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(Untranslated(strprintf("Cannot parse command line arguments: %s\n", error)));
        return EXIT_FAILURE;
    }
#ifdef ENABLE_TEST_AUTOMATION
    ApplyTestSettingsDir();
#endif

    if (auto error = common::InitConfig(
            gArgs,
            [](const bilingual_str& msg, const std::vector<std::string>& details) {
                return InitError(msg, details);
            })) {
        return EXIT_FAILURE;
    }

    // legacy GUI: parameterSetup()
    // Default printtoconsole to false for the GUI. GUI programs should not
    // print to the console unnecessarily.
    gArgs.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(gArgs);
    InitParameterInteraction(gArgs);

    QVariant need_onboarding(true);
    if (gArgs.IsArgSet("-datadir") && !gArgs.GetPathArg("-datadir").empty()) {
        need_onboarding.setValue(false);
    } else if (ConfigurationFileExists(gArgs)) {
        need_onboarding.setValue(false);
    }

    if (gArgs.IsArgSet("-resetguisettings")) {
        need_onboarding.setValue(true);
    }

    // legacy GUI: createNode()
    std::unique_ptr<interfaces::Node> node = init->makeNode();
    std::unique_ptr<interfaces::Chain> chain = init->makeChain();

    // legacy GUI: baseInitialize()
    if (!node->baseInitialize()) {
        // A dialog with detailed error will have been shown by InitError().
        return EXIT_FAILURE;
    }

    handler_message_box.disconnect();

    AppMode app_mode = SetupAppMode();
#ifdef ENABLE_WALLET
    const bool wallet_enabled = app_mode.walletEnabled();
#endif

    NodeModel node_model{*node};
    node_model.addStartupWarnings(startup_warnings);
    QmlInitExecutor init_executor{*node};
    bool shutdown_requested{false};
    DebugLogModel debug_log_model{gArgs.GetDataDirNet() / "debug.log"};
#ifdef ENABLE_WALLET
    std::unique_ptr<WalletQmlController> wallet_controller;
    if (wallet_enabled) {
        wallet_controller = std::make_unique<WalletQmlController>(*node);
        QObject::connect(
            &init_executor, &QmlInitExecutor::initializeResult, wallet_controller.get(),
            [wallet_controller = wallet_controller.get(), node = node.get(), &shutdown_requested](bool success) {
                if (success && !shutdown_requested && !node->shutdownRequested()) {
                    wallet_controller->initialize();
                }
            });
    }
#endif
    QObject::connect(&node_model, &NodeModel::requestedInitialize, &init_executor, &QmlInitExecutor::initialize);
    QObject::connect(&node_model, &NodeModel::requestedShutdown, [&] {
        if (shutdown_requested) {
            return;
        }
        shutdown_requested = true;
#ifdef ENABLE_WALLET
        if (wallet_controller) {
            wallet_controller->unloadWallets();
        }
#endif
        init_executor.shutdown();
    });
    QObject::connect(&init_executor, &QmlInitExecutor::initializeResult, &node_model, &NodeModel::initializeResult);
    QObject::connect(&init_executor, &QmlInitExecutor::shutdownResult, qGuiApp, [] {
        QCoreApplication::exit(0);
    }, Qt::QueuedConnection);
    QObject::connect(&init_executor, &QmlInitExecutor::runawayException, &node_model, &NodeModel::handleRunawayException);

    NetworkTrafficTower network_traffic_tower{node_model};
    NetworkStatusModel network_status_model;
#ifdef __ANDROID__
    AndroidNotifier android_notifier{node_model};
#endif

    ChainModel chain_model{*chain};
    chain_model.setCurrentNetworkName(QString::fromStdString(gArgs.GetChainTypeString()));
    setupChainQSettings(&app, chain_model.currentNetworkName());

    QObject::connect(&node_model, &NodeModel::setTimeRatioList, &chain_model, &ChainModel::setTimeRatioList);
    QObject::connect(&node_model, &NodeModel::setTimeRatioListInitial, &chain_model, &ChainModel::setTimeRatioListInitial);


    qGuiApp->setQuitOnLastWindowClosed(false);
    QObject::connect(qGuiApp, &QGuiApplication::lastWindowClosed, [&] {
        node_model.requestShutdown();
    });

    PeerListModel peer_model{*node, nullptr};
    PeerListSortProxy peer_model_sort_proxy{nullptr};
    peer_model_sort_proxy.setSourceModel(&peer_model);

    BanListModel ban_list_model{*node, nullptr};
    QObject::connect(&node_model, &NodeModel::bannedListChanged,
                     &ban_list_model, &BanListModel::refresh);
    QObject::connect(&node_model, &NodeModel::nodeInitialized,
                     &ban_list_model, &BanListModel::refresh);

    LoadFontResource(":/fonts/bitcoincoresans/regular");
    LoadFontResource(":/fonts/bitcoincoresans/semibold");
    LoadFontResource(":/fonts/robotomono/regular");

    auto engine = std::make_unique<QQmlApplicationEngine>();

    QScopedPointer<const NetworkStyle> network_style{NetworkStyle::instantiate(Params().GetChainType())};
    assert(!network_style.isNull());
    engine->addImageProvider(QStringLiteral("images"), new ImageProvider{network_style.data()});
    engine->addImageProvider(QStringLiteral("qr"), new QRImageProvider);

    engine->rootContext()->setContextProperty("networkTrafficTower", &network_traffic_tower);
    engine->rootContext()->setContextProperty("networkStatusModel", &network_status_model);
    engine->rootContext()->setContextProperty("nodeModel", &node_model);
    engine->rootContext()->setContextProperty("chainModel", &chain_model);
    engine->rootContext()->setContextProperty("peerTableModel", &peer_model);
    engine->rootContext()->setContextProperty("peerListModelProxy", &peer_model_sort_proxy);
    engine->rootContext()->setContextProperty("banListModel", &ban_list_model);
    engine->rootContext()->setContextProperty("debugLogModel", &debug_log_model);

#ifdef ENABLE_WALLET
    std::unique_ptr<WalletListModel> wallet_list_model;
    if (wallet_enabled) {
        wallet_list_model = std::make_unique<WalletListModel>(*node, nullptr);
        QObject::connect(wallet_controller.get(), &WalletQmlController::walletLoadStateChanged,
                         wallet_list_model.get(), &WalletListModel::setWalletLoadState);
        QObject::connect(wallet_controller.get(), &WalletQmlController::walletInfoChanged,
                         wallet_list_model.get(), &WalletListModel::setWalletInfo);
        QObject::connect(wallet_controller.get(), &WalletQmlController::walletDisplayNamesChanged,
                         wallet_list_model.get(), &WalletListModel::refreshDisplayNames);
        QObject::connect(wallet_list_model.get(), &WalletListModel::walletListChanged,
                         wallet_controller.get(), [controller = wallet_controller.get()](bool has_wallets) {
                             controller->setNoWalletsFound(!has_wallets);
                         });
        // listWalletDir() rebuilds m_items from scratch — any per-row info
        // pushed earlier (e.g. at controller initialize() time, before the
        // picker was ever opened) has nowhere to land. Re-publish open wallets'
        // info every time the model resets so newly created rows pick it up.
        QObject::connect(wallet_list_model.get(), &QAbstractItemModel::modelReset,
                         wallet_controller.get(), &WalletQmlController::publishOpenWalletsInfo);
        QObject::connect(wallet_controller.get(), &WalletQmlController::initializedChanged,
                         wallet_list_model.get(), [controller = wallet_controller.get(), list_model = wallet_list_model.get()]() {
                             if (controller->initialized()) {
                                 list_model->listWalletDir();
                             }
                         });
        engine->rootContext()->setContextProperty("walletController", wallet_controller.get());
        engine->rootContext()->setContextProperty("walletListModel", wallet_list_model.get());
    }
#endif

    OptionsQmlModel options_model(*node, !need_onboarding.toBool());
    engine->rootContext()->setContextProperty("optionsModel", &options_model);
    engine->rootContext()->setContextProperty("needOnboarding", need_onboarding);
#ifdef ENABLE_TEST_AUTOMATION
    engine->rootContext()->setContextProperty("testAutomationEnabled", true);
#else
    engine->rootContext()->setContextProperty("testAutomationEnabled", false);
#endif

    // -lang CLI flag overrides the persisted setting (bitcoin-qt compatibility).
    // Must be after gArgs.ParseParameters() and after setupChainQSettings() so
    // QSettings targets the correct chain-specific file.
    // Unlike bitcoin-qt which treats -lang as session-only, this persists the
    // selection so subsequent launches continue using the CLI-specified language.
    // To reset to system default, use the Settings UI or pass -lang= (empty).
    const QString cli_lang = QString::fromStdString(gArgs.GetArg("-lang", ""));
    if (!cli_lang.isEmpty()) {
        options_model.setLanguage(cli_lang);
    }

    // Install language before QML engine loads so that all qsTr() calls in QML
    // pick up the correct locale from the start.
    install_language(options_model.language());

    // Retranslate the QML UI immediately when the user picks a new language.
    QObject::connect(&options_model, &OptionsQmlModel::languageChanged, [&]() {
        install_language(options_model.language());
        engine->retranslate();
    });

    BuildInfo build_info;
    Clipboard clipboard;
    BitcoinUriModel bitcoin_uri_model;

    qmlRegisterSingletonInstance<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", &app_mode);
    qmlRegisterSingletonInstance<BuildInfo>("org.bitcoincore.qt", 1, 0, "BuildInfo", &build_info);
    qmlRegisterSingletonInstance<Clipboard>("org.bitcoincore.qt", 1, 0, "Clipboard", &clipboard);
    qmlRegisterSingletonInstance<BitcoinUriModel>("org.bitcoincore.qt", 1, 0, "BitcoinUri", &bitcoin_uri_model);
    qmlRegisterType<BlockClockDial>("org.bitcoincore.qt", 1, 0, "BlockClockDial");
    qmlRegisterType<LineGraph>("org.bitcoincore.qt", 1, 0, "LineGraph");
    qmlRegisterUncreatableType<PeerDetailsModel>("org.bitcoincore.qt", 1, 0, "PeerDetailsModel", "");
    qmlRegisterUncreatableType<DebugLogModel>("org.bitcoincore.qt", 1, 0, "DebugLogModel", "");
    qmlRegisterType<BitcoinAmount>("org.bitcoincore.qt", 1, 0, "BitcoinAmount");
    qmlRegisterType<BitcoinAddress>("org.bitcoincore.qt", 1, 0, "BitcoinAddress");
    qmlRegisterType<ActivityFilterProxyModel>("org.bitcoincore.qt", 1, 0, "ActivityFilterProxyModel");
    qmlRegisterUncreatableType<AddressListModel>("org.bitcoincore.qt", 1, 0, "AddressListModel", "");
    qmlRegisterType<PaymentRequest>("org.bitcoincore.qt", 1, 0, "PaymentRequest");
    qmlRegisterUncreatableType<Transaction>("org.bitcoincore.qt", 1, 0, "Transaction", "");
    qmlRegisterUncreatableType<SendRecipient>("org.bitcoincore.qt", 1, 0, "SendRecipient", "");

#ifdef ENABLE_WALLET
    qmlRegisterUncreatableType<BumpTransactionModel>("org.bitcoincore.qt", 1, 0, "BumpTransactionModel",
                                                      "BumpTransactionModel cannot be instantiated from QML");
    qmlRegisterUncreatableType<WalletQmlModel>("org.bitcoincore.qt", 1, 0, "WalletQmlModel",
                                               "WalletQmlModel cannot be instantiated from QML");
    qmlRegisterUncreatableType<WalletQmlModelTransaction>("org.bitcoincore.qt", 1, 0, "WalletQmlModelTransaction",
                                                          "WalletQmlModelTransaction cannot be instantiated from QML");
    qmlRegisterUncreatableType<WalletListModel>("org.bitcoincore.qt", 1, 0, "WalletListModel",
                                                "WalletListModel cannot be instantiated from QML");
#endif

    engine->load(QUrl(QStringLiteral("qrc:///qml/pages/main.qml")));
    if (engine->rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    auto window = qobject_cast<QQuickWindow*>(engine->rootObjects().first());
    if (!window) {
        return EXIT_FAILURE;
    }

#ifdef ENABLE_TEST_AUTOMATION
    std::unique_ptr<TestBridge> test_bridge;
    if (gArgs.IsArgSet("-test-automation")) {
        QString socket_path = QString::fromStdString(gArgs.GetArg("-test-automation", ""));
        if (socket_path.isEmpty()) {
            // Default to a socket in the data directory.
            socket_path = QString::fromStdString(
                (gArgs.GetDataDirNet() / "test_bridge.sock").utf8string());
        }
        test_bridge = std::make_unique<TestBridge>(engine.get(), socket_path);
    }
#endif

    // Install qDebug() message handler to route to debug.log
    qInstallMessageHandler(DebugMessageHandler);

    qInfo() << "Graphics API in use:" << QmlUtil::GraphicsApi(window);

    node_model.startShutdownPolling();
    const int exit_code{qGuiApp->exec()};
#ifdef ENABLE_TEST_AUTOMATION
    test_bridge.reset();
#endif
    engine.reset();
    return exit_code;
}
