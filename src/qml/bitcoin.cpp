// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <chainparams.h>
#include <clientversion.h>
#include <common/args.h>
#include <common/init.h>
#include <common/license_info.h>
#include <common/system.h>
#include <interfaces/init.h>
#include <init.h>
#include <node/interface_ui.h>
#include <noui.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/datadir.h>
#include <qml/guiargs.h>
#include <qml/guiconstants.h>
#include <qml/imageprovider.h>
#include <qml/legacy_settings_migration.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/models/settings_keys.h>
#include <qml/networkstyle.h>
#include <qml/onboarding_settings.h>
#include <qml/test/testbridge.h>
#include <qml/translationmanager.h>
#include <util/strencodings.h>
#include <util/threadnames.h>
#include <util/translation.h>

#include <QEventLoop>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QString>
#include <QStringLiteral>
#include <QStyleHints>
#include <QUrl>

#include <cassert>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

namespace {
void SetupChainQSettings(QGuiApplication& app, const QString& chain)
{
    const QString upper{chain.toUpper()};
    if (upper == QLatin1String("MAIN")) app.setApplicationName(QAPP_APP_NAME_DEFAULT);
    else if (upper == QLatin1String("TEST")) app.setApplicationName(QAPP_APP_NAME_TESTNET);
    else if (upper == QLatin1String("TESTNET4")) app.setApplicationName(QAPP_APP_NAME_TESTNET4);
    else if (upper == QLatin1String("SIGNET")) app.setApplicationName(QAPP_APP_NAME_SIGNET);
    else if (upper == QLatin1String("REGTEST")) app.setApplicationName(QAPP_APP_NAME_REGTEST);
}

void LoadFontResource(const QString& path)
{
    if (QFontDatabase::addApplicationFont(path) < 0) {
        qWarning("Failed to load font resource: %s", qPrintable(path));
    }
}

void ApplyTestSettingsDir()
{
    const std::string settings_dir{gArgs.GetArg("-test-settings-dir", "")};
    if (settings_dir.empty()) return;
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, QString::fromStdString(settings_dir));
}

enum class PreInitOnboardingStatus {
    NOT_SHOWN,
    COMPLETED,
    CANCELED,
    FAILED,
};

struct PreInitOnboardingContext {
    std::unique_ptr<OnboardingOptionsModel> options_model;
    std::unique_ptr<const NetworkStyle> network_style;
    std::unique_ptr<QQmlApplicationEngine> engine;
    std::unique_ptr<TestBridge> test_bridge;
    QPointer<QQuickWindow> window;

    void close()
    {
        test_bridge.reset();
        if (window) window->close();
        engine.reset();
        network_style.reset();
        options_model.reset();
    }
};

PreInitOnboardingStatus RunPreInitOnboarding(
    PreInitOnboardingContext& context,
    TranslationManager& translations,
    const std::vector<std::string>& argv,
    bool can_listen_ipc,
    const std::optional<std::string>& test_automation_socket)
{
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, can_listen_ipc)};
    translations.setLanguage(status.language);
    if (status.ok && !status.should_show_onboarding) {
        QmlDataDir::ApplyGuiDataDirSetting(gArgs);
        return PreInitOnboardingStatus::NOT_SHOWN;
    }

    try {
        SelectParams(gArgs.GetChainType());
    } catch (const std::exception& e) {
        InitError(Untranslated(e.what()));
        return PreInitOnboardingStatus::FAILED;
    }

    context.options_model = std::make_unique<OnboardingOptionsModel>(argv, can_listen_ipc);
    context.network_style.reset(NetworkStyle::instantiate(Params().GetChainType()));
    assert(context.network_style);
    context.engine = std::make_unique<QQmlApplicationEngine>();
    translations.attachEngine(*context.engine);
    context.engine->addImageProvider(QStringLiteral("images"), new ImageProvider{context.network_style.get()});
    context.engine->rootContext()->setContextProperty(QStringLiteral("optionsModel"), context.options_model.get());
    context.engine->load(QUrl{QStringLiteral("qrc:///qml/pages/preinit.qml")});
    if (context.engine->rootObjects().isEmpty()) return PreInitOnboardingStatus::FAILED;

    if (test_automation_socket) {
        context.test_bridge = std::make_unique<TestBridge>(
            *context.engine, QString::fromStdString(*test_automation_socket));
        if (!context.test_bridge->isListening()) return PreInitOnboardingStatus::FAILED;
    }

    context.window = qobject_cast<QQuickWindow*>(context.engine->rootObjects().constFirst());
    if (!context.window) return PreInitOnboardingStatus::FAILED;

    QEventLoop loop;
    QObject::connect(context.engine->rootObjects().constFirst(), SIGNAL(finished()), &loop, SLOT(quit()));
    QObject::connect(context.window, SIGNAL(closing(QQuickCloseEvent*)), &loop, SLOT(quit()));
    loop.exec();

    const bool completed{context.engine->rootObjects().constFirst()->property("completed").toBool()};
    if (!completed) {
        context.close();
        return PreInitOnboardingStatus::CANCELED;
    }

    QString error;
    if (!context.options_model->applyToArgs(gArgs, &error)) {
        InitError(Untranslated(error.toStdString()));
        context.close();
        return PreInitOnboardingStatus::FAILED;
    }
    return PreInitOnboardingStatus::COMPLETED;
}
} // namespace

int QmlGuiMain(int argc, char* argv[])
{
#ifdef WIN32
    common::WinCmdLineArgs win_args;
    std::tie(argc, argv) = win_args.get();
#endif

    Q_INIT_RESOURCE(bitcoin_qml);
    Q_INIT_RESOURCE(bitcoin_compat);
    QQuickStyle::setStyle(QStringLiteral("Basic"));

    BitcoinQmlApplication app{argc, argv};
    QGuiApplication::styleHints()->setTabFocusBehavior(Qt::TabFocusAllControls);
    app.setOrganizationName(QAPP_ORG_NAME);
    app.setOrganizationDomain(QAPP_ORG_DOMAIN);
    app.setApplicationName(QAPP_APP_NAME_DEFAULT);

    SetupEnvironment();
    util::ThreadSetInternalName("main");
    noui_connect();

    std::unique_ptr<interfaces::Init> init{interfaces::MakeGuiInit(argc, argv)};
    SetupServerArgs(gArgs, init->canListenIpc());
    SetupQmlGuiArgs(gArgs);
    std::string error;
    if (!gArgs.ParseParameters(argc, argv, error)) {
        InitError(Untranslated(strprintf("Error parsing command line arguments: %s", error)));
        return EXIT_FAILURE;
    }

    const auto test_automation_socket{gArgs.GetArg("-test-automation")};
    if (test_automation_socket && test_automation_socket->empty()) {
        InitError(Untranslated("The -test-automation option requires a socket path."));
        return EXIT_FAILURE;
    }

    if (HelpRequested(gArgs) || gArgs.GetBoolArg("-version", false)) {
        std::cout << init->exeName() << " " << FormatFullVersion() << "\n";
        if (gArgs.GetBoolArg("-version", false)) std::cout << FormatParagraph(LicenseInfo());
        else std::cout << "\n" << gArgs.GetHelpMessage();
        return EXIT_SUCCESS;
    }
    for (int i = 1; i < argc; ++i) {
        if (!IsSwitchChar(argv[i][0])) {
            InitError(Untranslated(strprintf(
                "Command line contains unexpected token '%s', see %s -h for a list of options.",
                argv[i], init->exeName())));
            return EXIT_FAILURE;
        }
    }

    // Validate the test bridge against command-line and configuration-file
    // chain selection before any onboarding window can expose the socket.
    // Normal startup still defers InitConfig until onboarding has chosen the
    // data directory.
    bool config_initialized{false};
    if (test_automation_socket) {
        if (auto config_error{common::InitConfig(gArgs)}) {
            InitError(config_error->message, config_error->details);
            return EXIT_FAILURE;
        }
        config_initialized = true;
        if (gArgs.GetChainTypeString() != "regtest") {
            InitError(Untranslated("The -test-automation option is only available on regtest."));
            return EXIT_FAILURE;
        }
    }

    ApplyTestSettingsDir();
    SetupChainQSettings(app, QString::fromStdString(gArgs.GetChainTypeString()));
    if (gArgs.GetBoolArg("-resetguisettings", false)) {
        QString reset_error;
        if (!QmlDataDir::ResetGuiSettings(gArgs, &reset_error)) {
            InitError(Untranslated(reset_error.toStdString()));
            return EXIT_FAILURE;
        }
    }

    LoadFontResource(QStringLiteral(":/fonts/bitcoincoresans/regular"));
    LoadFontResource(QStringLiteral(":/fonts/bitcoincoresans/semibold"));
    LoadFontResource(QStringLiteral(":/fonts/robotomono/regular"));

    app.installLanguage(TranslationManager::ResolveLanguage(gArgs));

    std::vector<std::string> command_line_args;
    command_line_args.reserve(argc);
    for (int i = 0; i < argc; ++i) command_line_args.emplace_back(argv[i]);

    PreInitOnboardingContext pre_init_context;
    const PreInitOnboardingStatus onboarding_status{RunPreInitOnboarding(
        pre_init_context, app.translations(), command_line_args, init->canListenIpc(), test_automation_socket)};
    if (onboarding_status == PreInitOnboardingStatus::CANCELED) return EXIT_SUCCESS;
    if (onboarding_status == PreInitOnboardingStatus::FAILED) return EXIT_FAILURE;

    if (!config_initialized) {
        if (auto config_error{common::InitConfig(gArgs)}) {
            InitError(config_error->message, config_error->details);
            return EXIT_FAILURE;
        }
    }

    const QmlLegacySettings::MigrationResult migration{
        QmlLegacySettings::MigrateCoreSettings(gArgs, QmlLegacySettings::MigrationMode::Persist)};
    if (!migration.error.isEmpty()) {
        InitError(Untranslated(migration.error.toStdString()));
        return EXIT_FAILURE;
    }
    if (migration.settings_changed) {
        std::vector<std::string> settings_errors;
        if (!gArgs.WriteSettingsFile(&settings_errors)) {
            InitError(_("Settings file could not be written"), settings_errors);
            return EXIT_FAILURE;
        }
    }
    app.parameterSetup();
    app.createNode(*init);
    if (!app.baseInitialize()) return EXIT_FAILURE;

    if (pre_init_context.window) app.setInitialWindowGeometry(pre_init_context.window->geometry());
    pre_init_context.close();
    if (!app.createWindow()) return EXIT_FAILURE;

    if (test_automation_socket &&
        !app.createTestBridge(QString::fromStdString(*test_automation_socket))) {
        return EXIT_FAILURE;
    }

    app.requestInitialize();
    return app.exec();
}
