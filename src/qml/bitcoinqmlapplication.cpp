// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>

#include <chainparams.h>
#include <clientversion.h>
#include <common/args.h>
#include <init.h>
#include <interfaces/chain.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <qml/appmode.h>
#include <qml/applicationrouter.h>
#include <qml/translationmanager.h>
#include <qml/buildinfo.h>
#include <qml/clipboard.h>
#include <qml/guiconstants.h>
#include <qml/imageprovider.h>
#include <qml/initexecutor.h>
#include <qml/models/nodemodel.h>
#include <qml/networkstyle.h>
#include <qml/test/testbridge.h>

#include <QMetaType>
#include <QJSEngine>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSettings>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>

#include <cassert>
#include <cstdlib>
#include <memory>

namespace {
void RegisterQmlTypes(AppMode& app_mode, BuildInfo& build_info, Clipboard& clipboard)
{
    static bool registered{false};
    static AppMode* app_mode_instance{nullptr};
    static BuildInfo* build_info_instance{nullptr};
    static Clipboard* clipboard_instance{nullptr};
    if (registered) return;

    app_mode_instance = &app_mode;
    build_info_instance = &build_info;
    clipboard_instance = &clipboard;
    qmlRegisterSingletonType<AppMode>("org.bitcoincore.qt", 1, 0, "AppMode", [](QQmlEngine*, QJSEngine*) -> QObject* {
        QQmlEngine::setObjectOwnership(app_mode_instance, QQmlEngine::CppOwnership);
        return app_mode_instance;
    });
    qmlRegisterSingletonType<BuildInfo>("org.bitcoincore.qt", 1, 0, "BuildInfo", [](QQmlEngine*, QJSEngine*) -> QObject* {
        QQmlEngine::setObjectOwnership(build_info_instance, QQmlEngine::CppOwnership);
        return build_info_instance;
    });
    qmlRegisterSingletonType<Clipboard>("org.bitcoincore.qt", 1, 0, "Clipboard", [](QQmlEngine*, QJSEngine*) -> QObject* {
        QQmlEngine::setObjectOwnership(clipboard_instance, QQmlEngine::CppOwnership);
        return clipboard_instance;
    });
    registered = true;
}
} // namespace

BitcoinQmlApplication::BitcoinQmlApplication(int& argc, char** argv)
    : QApplication{argc, argv}
{
    setOrganizationName(QStringLiteral(QAPP_ORG_NAME));
    setOrganizationDomain(QStringLiteral(QAPP_ORG_DOMAIN));
    setApplicationName(QStringLiteral(QAPP_APP_NAME_DEFAULT));
#ifdef __ANDROID__
    m_app_mode = std::make_unique<AppMode>(AppMode::MOBILE);
#else
    m_app_mode = std::make_unique<AppMode>(AppMode::DESKTOP);
#endif
    m_build_info = std::make_unique<BuildInfo>();
    m_clipboard = std::make_unique<Clipboard>();
    m_translations = std::make_unique<TranslationManager>();
    RegisterQmlTypes(*m_app_mode, *m_build_info, *m_clipboard);
    setApplicationDisplayName(translate("bitcoin-core", "Bitcoin Core"));
    setQuitOnLastWindowClosed(false);
}

BitcoinQmlApplication::~BitcoinQmlApplication()
{
    // Interrupt blocking RPCs before joining their worker during fallback
    // teardown (for example when QML window creation failed).
    if (m_node && m_base_initialized && !m_shutdown_complete) m_node->startShutdown();
    m_test_bridge.reset();
    m_engine.reset();
    m_navigation_model.reset();
    m_router.reset();
    m_network_style.reset();
    m_clipboard.reset();
    m_build_info.reset();
    m_app_mode.reset();
    m_init_executor.reset();
    m_node_model.reset();
    if (m_node && m_base_initialized && !m_shutdown_complete) {
        m_node->startShutdown();
        m_node->appShutdown();
    }
}

void BitcoinQmlApplication::parameterSetup()
{
    gArgs.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(gArgs);
    InitParameterInteraction(gArgs);
}

void BitcoinQmlApplication::createNode(interfaces::Init& init)
{
    assert(!m_node);
    m_node = init.makeNode();
}

bool BitcoinQmlApplication::baseInitialize()
{
    assert(m_node);
    m_base_initialized = m_node->baseInitialize();
    return m_base_initialized;
}

bool BitcoinQmlApplication::createWindow()
{
    assert(m_node);
    assert(!m_node_model);

    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");
    m_node_model = std::make_unique<NodeModel>(*m_node);
    m_router = std::make_unique<ApplicationRouter>();
    m_router->registerDestination({QStringLiteral("node"), QUrl{QStringLiteral("qrc:///qml/pages/node/NodeRunner.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Node"), true, QStringLiteral("")});
    m_router->registerDestination({QStringLiteral("shutdown"), QUrl{QStringLiteral("qrc:///qml/pages/node/Shutdown.qml")}, QT_TRANSLATE_NOOP("ApplicationRouter", "Shutting down"), false, QStringLiteral("")});
    m_router->navigate(QStringLiteral("node"));
    m_navigation_model = std::make_unique<NavigationModel>(*m_router);
    connect(m_translations.get(), &TranslationManager::languageChanged, m_router.get(), &ApplicationRouter::retranslate);
    connect(m_node_model.get(), &NodeModel::requestedShutdown, m_router.get(), &ApplicationRouter::beginShutdown);
    m_init_executor = std::make_unique<QmlInitExecutor>(*m_node);
    connect(m_node_model.get(), &NodeModel::requestedInitialize, m_init_executor.get(), &QmlInitExecutor::initialize);
    connect(m_init_executor.get(), &QmlInitExecutor::initializeResult, m_node_model.get(), &NodeModel::initializeResult);
    connect(m_init_executor.get(), &QmlInitExecutor::shutdownResult, m_node_model.get(), &NodeModel::shutdownResult);
    connect(m_init_executor.get(), &QmlInitExecutor::runawayException, this, &BitcoinQmlApplication::handleRunawayException);
    connect(m_node_model.get(), &NodeModel::shutdownComplete, this, [this] {
        m_shutdown_complete = true;
        exit(node().getExitStatus());
    });

    m_network_style.reset(NetworkStyle::instantiate(Params().GetChainType()));
    assert(m_network_style);
    setApplicationName(m_network_style->getAppName());
    setWindowIcon(m_network_style->getAppIcon());
    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_translations->attachEngine(*m_engine);
    m_engine->addImageProvider(QStringLiteral("images"), new ImageProvider{m_network_style.get()});
    QQmlContext* const context{m_engine->rootContext()};
    context->setContextProperty(QStringLiteral("nodeLifecycleModel"), m_node_model.get());
    context->setContextProperty(QStringLiteral("applicationRouter"), m_router.get());
    context->setContextProperty(QStringLiteral("navigationModel"), m_navigation_model.get());

    connect(this, &QGuiApplication::lastWindowClosed, this, &BitcoinQmlApplication::requestShutdown);
    m_engine->load(QUrl{QStringLiteral("qrc:///qml/pages/MainWindow.qml")});
    if (m_engine->rootObjects().isEmpty()) return false;

    auto* const window{qobject_cast<QQuickWindow*>(m_engine->rootObjects().constFirst())};
    if (!window) return false;
    if (m_initial_window_geometry.isValid()) window->setGeometry(m_initial_window_geometry);
    // NodeModel interrupts Core first. All preceding direct slots drain
    // feature workers before this last slot queues destruction of Core state.
    connect(m_node_model.get(), &NodeModel::requestedShutdown, m_init_executor.get(), &QmlInitExecutor::shutdown);
    return true;
}

bool BitcoinQmlApplication::createTestBridge(const QString& socket_path)
{
    assert(m_engine);
    assert(!m_test_bridge);
    m_test_bridge = std::make_unique<TestBridge>(*m_engine, socket_path);
    return m_test_bridge->isListening();
}

[[noreturn]] void BitcoinQmlApplication::handleRunawayException(const QString& message)
{
    m_node_model->handleRunawayException(message);
    ::exit(EXIT_FAILURE);
}

void BitcoinQmlApplication::requestInitialize()
{
    assert(m_node_model);
    QTimer::singleShot(0, m_node_model.get(), &NodeModel::start);
}

void BitcoinQmlApplication::requestShutdown()
{
    assert(m_node_model);
    m_node_model->requestShutdown();
}

void BitcoinQmlApplication::setInitialWindowGeometry(const QRect& geometry)
{
    m_initial_window_geometry = geometry;
}

void BitcoinQmlApplication::installLanguage(const QString& language)
{
    m_translations->setLanguage(language);
}

TranslationManager& BitcoinQmlApplication::translations() const
{
    return *m_translations;
}

ApplicationRouter& BitcoinQmlApplication::router() const
{
    assert(m_router);
    return *m_router;
}

interfaces::Node& BitcoinQmlApplication::node() const
{
    assert(m_node);
    return *m_node;
}

NodeModel& BitcoinQmlApplication::nodeModel() const
{
    assert(m_node_model);
    return *m_node_model;
}

QQmlApplicationEngine& BitcoinQmlApplication::engine() const
{
    assert(m_engine);
    return *m_engine;
}
