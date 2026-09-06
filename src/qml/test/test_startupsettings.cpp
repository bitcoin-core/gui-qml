// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/startupsettings_test_util.h>
#include <qml/test/qt_test_registry.h>

#include <QDataStream>

using namespace startupsettingstest;

namespace {
// Fail when migration removes fListen, while allowing the original values to
// be written back during rollback. This also works as root and on Windows.
QSettings::Format CleanupFailureFormat()
{
    static const QSettings::Format format{QSettings::registerFormat(
        QStringLiteral("cleanup-failure"),
        [](QIODevice& device, QSettings::SettingsMap& values) {
            QDataStream stream{&device};
            stream >> values;
            return stream.status() == QDataStream::Ok;
        },
        [](QIODevice& device, const QSettings::SettingsMap& values) {
            if (values.value(QStringLiteral("failCleanup")).toBool() && !values.contains(QStringLiteral("fListen"))) return false;
            QDataStream stream{&device};
            stream << values;
            return stream.status() == QDataStream::Ok;
        })};
    return format;
}
} // namespace

class StartupSettingsTests : public QObject
{
    Q_OBJECT
    QString m_original_organization;
    QString m_original_application;

private Q_SLOTS:
    void initTestCase()
    {
        m_original_organization = QCoreApplication::organizationName();
        m_original_application = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName("BitcoinCoreAppTest");
        QCoreApplication::setApplicationName("StartupSettingsTests");
    }
    void cleanupTestCase()
    {
        QCoreApplication::setOrganizationName(m_original_organization);
        QCoreApplication::setApplicationName(m_original_application);
    }
    void resetGuiSettingsClearsAndBacksUpQSettings();
    void resetGuiSettingsClearsLegacyQtSettings();
    void resetGuiSettingsClearsAndBacksUpSettingsJson();
    void resetGuiSettingsHonorsFinalSourcePrecedence();
    void resetGuiSettingsAllowsUnreadableSettingsProfile();
    void resetGuiSettingsPreservesMalformedSettingsBackup();
    void resetLegacyCleanupRollsBackOnWriteFailure();
    void resetGuiSettingsPreviewIgnoresSelectedCustomDataDirSettingsJson();
    void resetGuiSettingsApplyClearsSelectedCustomDataDirSettingsJson();
    void resetGuiSettingsPreservesCommandLineOverrides();
    void resetGuiSettingsPreservesBitcoinConfOverrides();
    void resetGuiSettingsPreservesSavedDatadirOverConfigDatadir();
    void onboardingApplyRollsBackWhenLegacyCleanupFails();
    void onboardingApplyRollsBackWhenSettingsWriteFails();
    void onboardingApplyWithSettingsDisabledPreservesLegacyCoreValues();
    void onboardingApplyRejectsProfileDrift();
    void onboardingFinalizeIgnoresUnusedBootstrapStore();
    void onboardingFinalizeIgnoresUnusedActiveStore();
    void onboardingApplyClearsResetFlagInBootstrapAndActiveStores();
};

void StartupSettingsTests::resetGuiSettingsClearsAndBacksUpQSettings()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QSettings settings;
    settings.setValue(SettingsKeys::DATA_DIR, QStringLiteral("/tmp/old-bitcoin-data"));
    settings.setValue(SettingsKeys::LANGUAGE, QStringLiteral("de"));
    settings.setValue(SettingsKeys::DISPLAY_UNIT, 3);
    settings.setValue("strThirdPartyTxUrls", QStringLiteral("https://example.com/%s"));
    settings.setValue("FontForMoney", QStringLiteral("best_system"));
    settings.setValue("fReset", true);
    settings.sync();

    std::vector<std::string> argv = TestArgvWithDataDir(data_dir.path());
    argv.emplace_back("-resetguisettings");
    argv.emplace_back("-nosettings");
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    QVERIFY(!args.GetSettingsPath());

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QmlOnboardingSettings::FinalizeResult result;
    InitializeAndFinalizeSettings(args, bootstrap_gui_settings, /*pending=*/nullptr, &result);

    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), QStringLiteral("/tmp/old-bitcoin-data"));
    QVERIFY(!settings.contains(SettingsKeys::LANGUAGE));
    QVERIFY(!settings.contains(SettingsKeys::DISPLAY_UNIT));
    QVERIFY(!settings.contains("strThirdPartyTxUrls"));
    QVERIFY(!settings.contains("FontForMoney"));
    QCOMPARE(settings.value("fReset").toBool(), true);
    QVERIFY(result.reset_applied);

    QSettings backup{
        QDir(data_dir.path()).filePath(QStringLiteral("regtest/guisettings.ini.bak")),
        QSettings::IniFormat,
    };
    QCOMPARE(backup.value(SettingsKeys::DATA_DIR).toString(), QStringLiteral("/tmp/old-bitcoin-data"));
    QCOMPARE(backup.value(SettingsKeys::LANGUAGE).toString(), QStringLiteral("de"));
    QCOMPARE(backup.value(SettingsKeys::DISPLAY_UNIT).toInt(), 3);
}

void StartupSettingsTests::resetGuiSettingsClearsLegacyQtSettings()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_core_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    SavedNamedSettings legacy_default_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt")};
    legacy_core_settings.settings().setValue(QStringLiteral("fListen"), false);
    legacy_core_settings.settings().setValue(QStringLiteral("addrProxy"), QStringLiteral("10.0.0.1:9050"));
    legacy_core_settings.settings().setValue(SettingsKeys::DISPLAY_UNIT, 3);
    legacy_default_settings.settings().setValue(SettingsKeys::DATA_DIR, QStringLiteral("/tmp/legacy-bitcoin-data"));

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    std::vector<std::string> argv = TestArgvWithDataDir(data_dir.path());
    argv.emplace_back("-resetguisettings");
    argv.emplace_back("-settings=");
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    InitializeAndFinalizeSettings(args, bootstrap_gui_settings);

    QVERIFY(!legacy_core_settings.settings().contains(QStringLiteral("fListen")));
    QVERIFY(!legacy_core_settings.settings().contains(QStringLiteral("addrProxy")));
    QVERIFY(!legacy_core_settings.settings().contains(SettingsKeys::DISPLAY_UNIT));
    QVERIFY(!legacy_default_settings.settings().contains(SettingsKeys::DATA_DIR));
}

void StartupSettingsTests::resetGuiSettingsClearsAndBacksUpSettingsJson()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    const std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-datadir=" + data_dir.path().toStdString(),
        std::string{"-resetguisettings"},
    };

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    SelectParams(args.GetChainType());
    args.SelectConfigNetwork(args.GetChainTypeString());
    args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["prune"] = MakeInt(2048);
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"10.0.0.1:9050"}};
        settings.rw_settings["onion"] = common::SettingsValue{std::string{"127.0.0.1:9150"}};
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(args.WriteSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    InitializeAndFinalizeSettings(args, bootstrap_gui_settings);

    fs::path backup_path;
    QVERIFY(args.GetSettingsPath(&backup_path, /*temp=*/false, /*backup=*/true));
    QVERIFY(fs::exists(backup_path));

    ArgsManager check_args;
    QVERIFY2(PrepareTestArgs(check_args, argv, parse_error), parse_error.c_str());
    SelectParams(check_args.GetChainType());
    check_args.SelectConfigNetwork(check_args.GetChainTypeString());
    QVERIFY2(check_args.ReadSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());
    check_args.LockSettings([](common::Settings& settings) {
        QVERIFY(!settings.rw_settings.contains("prune"));
        QVERIFY(!settings.rw_settings.contains("proxy"));
        QVERIFY(!settings.rw_settings.contains("onion"));
    });
}

void StartupSettingsTests::resetGuiSettingsHonorsFinalSourcePrecedence()
{
    struct ResetCase {
        const char* name;
        bool config_value;
        std::optional<bool> settings_value;
        std::optional<bool> command_line_value;
        bool expected_reset;
    };
    const std::array cases{
        ResetCase{"config-only true", true, std::nullopt, std::nullopt, true},
        ResetCase{"settings false overrides config true", true, false, std::nullopt, false},
        ResetCase{"settings true overrides config false", false, true, std::nullopt, true},
        ResetCase{"command line false overrides settings true", true, true, false, false},
        ResetCase{"command line true overrides settings false", false, false, true, true},
    };

    SavedGuiDataDirSettings saved_settings;
    for (const ResetCase& test_case : cases) {
        QTemporaryDir data_dir;
        QVERIFY2(data_dir.isValid(), test_case.name);
        QVERIFY2(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")), test_case.name);

        QFile conf{QDir(data_dir.path()).filePath(QStringLiteral("bitcoin.conf"))};
        QVERIFY2(conf.open(QIODevice::WriteOnly | QIODevice::Text), test_case.name);
        const QByteArray config{
            QByteArrayLiteral("regtest=1\n[regtest]\nresetguisettings=") +
            (test_case.config_value ? QByteArrayLiteral("1\n") : QByteArrayLiteral("0\n"))};
        QCOMPARE(conf.write(config), config.size());
        conf.close();

        const std::vector<std::string> seed_argv{TestArgvWithDataDir(data_dir.path())};
        ArgsManager seed_args;
        std::string parse_error;
        QVERIFY2(PrepareTestArgs(seed_args, seed_argv, parse_error), parse_error.c_str());
        SelectParams(seed_args.GetChainType());
        seed_args.SelectConfigNetwork(seed_args.GetChainTypeString());
        seed_args.LockSettings([&](common::Settings& settings) {
            settings.rw_settings["qml_onboarded"] = common::SettingsValue{true};
            if (test_case.settings_value) {
                settings.rw_settings["resetguisettings"] = common::SettingsValue{*test_case.settings_value};
            }
        });
        std::vector<std::string> settings_errors;
        QVERIFY2(
            seed_args.WriteSettingsFile(&settings_errors),
            settings_errors.empty() ? test_case.name : settings_errors.front().c_str());

        QSettings gui_settings;
        gui_settings.setFallbacksEnabled(false);
        gui_settings.clear();
        gui_settings.sync();

        std::vector<std::string> argv{seed_argv};
        if (test_case.command_line_value) {
            argv.emplace_back(*test_case.command_line_value
                    ? "-resetguisettings=1"
                    : "-resetguisettings=0");
        }
        const QmlOnboardingSettings::OnboardingStartupStatus startup_status{
            QmlOnboardingSettings::ResolveOnboardingStartupStatus(
                argv,
                /*can_listen_ipc=*/false)
        };
        QVERIFY2(startup_status.ok, qPrintable(startup_status.error));
        QVERIFY2(
            startup_status.should_show_onboarding == test_case.expected_reset,
            test_case.name);
        QVERIFY2(
            startup_status.qml_onboarded != test_case.expected_reset,
            test_case.name);

        ArgsManager args;
        QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
        const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
            QmlOnboardingSettings::CurrentGuiSettingsStore()
        };
        const std::optional<common::ConfigError> init_error{
            common::InitConfig(args, [](const bilingual_str&, const std::vector<std::string>&) {
                return true;
            })
        };
        QVERIFY2(
            !init_error,
            init_error ? init_error->message.original.c_str() : "");
        args.SelectConfigNetwork(args.GetChainTypeString());
        QVERIFY2(
            args.GetBoolArg("-resetguisettings", false) ==
                test_case.expected_reset,
            test_case.name);

        QmlOnboardingSettings::FinalizeResult result;
        QString finalize_error;
        QVERIFY2(
            QmlOnboardingSettings::FinalizeStartupSettings(
                args,
                bootstrap_gui_settings,
                /*pending=*/nullptr,
                &result,
                &finalize_error),
            qPrintable(finalize_error));
        QVERIFY2(result.reset_applied == test_case.expected_reset, test_case.name);
    }
}

void StartupSettingsTests::resetGuiSettingsAllowsUnreadableSettingsProfile()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    QFile settings_file{
        QDir(data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))
    };
    QVERIFY(settings_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(settings_file.write("{not valid json") > 0);
    settings_file.close();

    std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    const QmlOnboardingSettings::OnboardingStartupStatus unreadable_status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(
            argv,
            /*can_listen_ipc=*/false)
    };
    QVERIFY(!unreadable_status.ok);
    QVERIFY(unreadable_status.settings_file_unreadable);

    argv.emplace_back("-resetguisettings");
    const QmlOnboardingSettings::OnboardingStartupStatus reset_status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(
            argv,
            /*can_listen_ipc=*/false)
    };
    QVERIFY2(reset_status.ok, qPrintable(reset_status.error));
    QVERIFY(!reset_status.settings_file_unreadable);
    QVERIFY(reset_status.should_show_onboarding);

    QFile config_file{QDir(data_dir.path()).filePath(QStringLiteral("bitcoin.conf"))};
    QVERIFY(config_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(config_file.write("regtest=1\nresetguisettings=1\n") > 0);
    config_file.close();

    const std::vector<std::string> config_argv{
        std::string{"bitcoinqml"},
        "-datadir=" + data_dir.path().toStdString(),
    };
    const QmlOnboardingSettings::OnboardingStartupStatus config_reset_status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(
            config_argv,
            /*can_listen_ipc=*/false)
    };
    QVERIFY2(config_reset_status.ok, qPrintable(config_reset_status.error));
    QVERIFY(config_reset_status.should_show_onboarding);
}

void StartupSettingsTests::resetGuiSettingsPreservesMalformedSettingsBackup()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    const QByteArray malformed_settings{"{not valid json"};
    QFile settings_file{
        QDir(data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))
    };
    QVERIFY(settings_file.open(QIODevice::WriteOnly));
    QCOMPARE(settings_file.write(malformed_settings), malformed_settings.size());
    settings_file.close();

    const std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());

    QmlOnboardingSettings::SettingsFileBackup captured_backup;
    QString capture_error;
    bool capture_called{false};
    const std::optional<common::ConfigError> init_error{
        common::InitConfig(
            args,
            [&](const bilingual_str&, const std::vector<std::string>&) {
                capture_called = true;
                return !QmlOnboardingSettings::CaptureSettingsFileBackup(
                    args,
                    captured_backup,
                    &capture_error);
            })
    };
    QVERIFY(capture_called);
    QVERIFY2(capture_error.isEmpty(), qPrintable(capture_error));
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    args.SelectConfigNetwork(args.GetChainTypeString());

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QString finalize_error;
    QVERIFY2(
        QmlOnboardingSettings::FinalizeStartupSettings(
            args,
            bootstrap_gui_settings,
            /*pending=*/nullptr,
            /*result=*/nullptr,
            &finalize_error,
            &captured_backup),
        qPrintable(finalize_error));

    fs::path backup_path;
    QVERIFY(args.GetSettingsPath(&backup_path, /*temp=*/false, /*backup=*/true));
    QFile backup_file{QString::fromStdString(fs::PathToString(backup_path))};
    QVERIFY(backup_file.open(QIODevice::ReadOnly));
    QCOMPARE(backup_file.readAll(), malformed_settings);
}

void StartupSettingsTests::resetLegacyCleanupRollsBackOnWriteFailure()
{
    QTemporaryDir settings_dir;
    QVERIFY(settings_dir.isValid());
    const QSettings::Format format{CleanupFailureFormat()};
    QSettings::setPath(format, QSettings::UserScope, settings_dir.path());
    SavedSettingsFormat saved_format{format};
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    qml_core_settings.settings().setValue(QStringLiteral("server"), true);
    legacy_settings.settings().setValue(QStringLiteral("fListen"), false);
    legacy_settings.settings().setValue(QStringLiteral("failCleanup"), true);
    qml_core_settings.settings().sync();
    legacy_settings.settings().sync();
    QCOMPARE(qml_core_settings.settings().status(), QSettings::NoError);
    QCOMPARE(legacy_settings.settings().status(), QSettings::NoError);

    bool cleared{true};
    QString clear_error;
    cleared = QmlLegacySettings::ClearLegacyGuiSettings(
        QStringLiteral("regtest"),
        &clear_error);

    QVERIFY(!cleared);
    QVERIFY(clear_error.contains(QStringLiteral("Legacy GUI settings cleanup")));
    qml_core_settings.settings().sync();
    legacy_settings.settings().sync();
    QCOMPARE(qml_core_settings.settings().value(QStringLiteral("server")).toBool(), true);
    QCOMPARE(legacy_settings.settings().value(QStringLiteral("fListen")).toBool(), false);
}

void StartupSettingsTests::resetGuiSettingsPreviewIgnoresSelectedCustomDataDirSettingsJson()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    const std::vector<std::string> write_argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-datadir=" + data_dir.path().toStdString(),
    };
    ArgsManager write_args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(write_args, write_argv, parse_error), parse_error.c_str());
    SelectParams(write_args.GetChainType());
    write_args.SelectConfigNetwork(write_args.GetChainTypeString());
    write_args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["listen"] = common::SettingsValue{false};
        settings.rw_settings["natpmp"] = common::SettingsValue{true};
        settings.rw_settings["server"] = common::SettingsValue{true};
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"10.0.0.1:9050"}};
        settings.rw_settings["onion"] = common::SettingsValue{std::string{"127.0.0.1:9150"}};
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(write_args.WriteSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());

    std::vector<std::string> preview_argv = TestArgv();
    preview_argv.emplace_back("-resetguisettings");
    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(preview_argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.listen);
    QVERIFY(preview.values.natpmp);
    QVERIFY(!preview.values.server);
    QVERIFY(!preview.values.proxy_enabled);
    QVERIFY(!preview.values.tor_enabled);
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("proxy")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("default"));
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("server")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("default"));
}

void StartupSettingsTests::resetGuiSettingsApplyClearsSelectedCustomDataDirSettingsJson()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir old_data_dir;
    QTemporaryDir data_dir;
    QVERIFY(old_data_dir.isValid());
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    QSettings gui_settings;
    gui_settings.setValue(SettingsKeys::DATA_DIR, old_data_dir.path());
    gui_settings.setValue(SettingsKeys::LANGUAGE, QStringLiteral("de"));
    gui_settings.setValue(QStringLiteral("fReset"), true);

    const std::vector<std::string> write_argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-datadir=" + data_dir.path().toStdString(),
    };
    ArgsManager write_args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(write_args, write_argv, parse_error), parse_error.c_str());
    SelectParams(write_args.GetChainType());
    write_args.SelectConfigNetwork(write_args.GetChainTypeString());
    write_args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["listen"] = common::SettingsValue{false};
        settings.rw_settings["natpmp"] = common::SettingsValue{true};
        settings.rw_settings["server"] = common::SettingsValue{true};
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"10.0.0.1:9050"}};
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(write_args.WriteSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());

    std::vector<std::string> argv = TestArgv();
    argv.emplace_back("-resetguisettings");
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QCOMPARE(model.dataDir(), old_data_dir.path());
    QCOMPARE(gui_settings.value(SettingsKeys::DATA_DIR).toString(), old_data_dir.path());
    QCOMPARE(gui_settings.value(SettingsKeys::LANGUAGE).toString(), QStringLiteral("de"));
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QCOMPARE(model.previewError(), QString{});
    QVERIFY(model.listen());
    QVERIFY(model.natpmp());
    QVERIFY(!model.server());
    QVERIFY(!model.proxyEnabled());

    ArgsManager apply_args;
    QVERIFY2(PrepareTestArgs(apply_args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeModelApply(model, apply_args);

    QCOMPARE(gui_settings.value(SettingsKeys::DATA_DIR).toString(), data_dir.path());
    QVERIFY(!gui_settings.contains(SettingsKeys::LANGUAGE));
    QCOMPARE(gui_settings.value(QStringLiteral("fReset")).toBool(), false);

    fs::path backup_path;
    QVERIFY(apply_args.GetSettingsPath(&backup_path, /*temp=*/false, /*backup=*/true));
    QVERIFY(fs::exists(backup_path));

    ArgsManager check_args;
    QVERIFY2(PrepareTestArgs(check_args, write_argv, parse_error), parse_error.c_str());
    SelectParams(check_args.GetChainType());
    check_args.SelectConfigNetwork(check_args.GetChainTypeString());
    QVERIFY2(check_args.ReadSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());
    check_args.LockSettings([](common::Settings& settings) {
        QVERIFY(!settings.rw_settings.contains("listen"));
        QVERIFY(!settings.rw_settings.contains("natpmp"));
        QVERIFY(!settings.rw_settings.contains("server"));
        QVERIFY(!settings.rw_settings.contains("proxy"));
    });
}

void StartupSettingsTests::resetGuiSettingsPreservesCommandLineOverrides()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    const std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-datadir=" + data_dir.path().toStdString(),
        std::string{"-proxy=10.0.0.2:9050"},
        std::string{"-prune=2048"},
        std::string{"-resetguisettings"},
    };

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    SelectParams(args.GetChainType());
    args.SelectConfigNetwork(args.GetChainTypeString());
    args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"10.0.0.1:9050"}};
        settings.rw_settings["prune"] = MakeInt(4096);
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(args.WriteSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.proxy_enabled);
    QCOMPARE(preview.values.proxy_address, QStringLiteral("10.0.0.2:9050"));
    QVERIFY(preview.values.prune);
    QCOMPARE(preview.values.prune_size_gb, QmlCoreSettings::PruneMiBToGB(2048));
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("proxy")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("command_line"));
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("prune")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("command_line"));
}

void StartupSettingsTests::resetGuiSettingsPreservesBitcoinConfOverrides()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    QFile conf(QDir(data_dir.path()).filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("regtest=1\n[regtest]\nserver=1\nproxy=10.0.0.3:9050\n") > 0);
    conf.close();

    const std::vector<std::string> write_argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-datadir=" + data_dir.path().toStdString(),
    };
    ArgsManager write_args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(write_args, write_argv, parse_error), parse_error.c_str());
    SelectParams(write_args.GetChainType());
    write_args.SelectConfigNetwork(write_args.GetChainTypeString());
    write_args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["server"] = common::SettingsValue{false};
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"10.0.0.1:9050"}};
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(write_args.WriteSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());

    std::vector<std::string> preview_argv = TestArgv();
    preview_argv.emplace_back("-resetguisettings");
    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(preview_argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.server);
    QVERIFY(preview.values.proxy_enabled);
    QCOMPARE(preview.values.proxy_address, QStringLiteral("10.0.0.3:9050"));
    QVERIFY(!preview.values.listen);
    QVERIFY(!preview.values.natpmp);
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("server")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("proxy")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));
}

void StartupSettingsTests::resetGuiSettingsPreservesSavedDatadirOverConfigDatadir()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir saved_data_dir;
    QTemporaryDir configured_data_dir;
    QTemporaryDir config_dir;
    QVERIFY(saved_data_dir.isValid());
    QVERIFY(configured_data_dir.isValid());
    QVERIFY(config_dir.isValid());
    settings.setValue(SettingsKeys::DATA_DIR, saved_data_dir.path());
    settings.setValue(QStringLiteral("fReset"), true);

    const QString conf_path = QDir(config_dir.path()).filePath(QStringLiteral("bitcoin.conf"));
    QFile conf(conf_path);
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write(QStringLiteral("regtest=1\ndatadir=%1\n[regtest]\nserver=1\n").arg(configured_data_dir.path()).toUtf8()) > 0);
    conf.close();

    std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-conf=" + conf_path.toStdString(),
        std::string{"-resetguisettings"},
    };

    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QCOMPARE(model.dataDir(), saved_data_dir.path());
    QCOMPARE(model.getCustomDataDirString(), saved_data_dir.path());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeModelApply(model, args);

    QCOMPARE(QmlDataDir::NormalizeLocalPath(QString::fromStdString(fs::PathToString(args.GetDataDirBase()))), saved_data_dir.path());
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), saved_data_dir.path());
    QCOMPARE(settings.value(QStringLiteral("fReset")).toBool(), false);
    QVERIFY(QFileInfo(QDir(saved_data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))).isFile());
    QVERIFY(!QFileInfo(QDir(configured_data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))).exists());

    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, saved_data_dir.path());
    QCOMPARE(SettingToBool(check_args.GetPersistentSetting("qml_onboarded")), true);
}

void StartupSettingsTests::onboardingApplyRollsBackWhenLegacyCleanupFails()
{
    QTemporaryDir settings_dir;
    QVERIFY(settings_dir.isValid());
    const QSettings::Format format{CleanupFailureFormat()};
    QSettings::setPath(format, QSettings::UserScope, settings_dir.path());
    SavedSettingsFormat saved_format{format};
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    legacy_settings.settings().setValue(QStringLiteral("fListen"), false);
    legacy_settings.settings().setValue(QStringLiteral("failCleanup"), true);
    legacy_settings.settings().sync();
    QCOMPARE(legacy_settings.settings().status(), QSettings::NoError);

    QSettings gui_settings;
    gui_settings.setFallbacksEnabled(false);
    gui_settings.clear();
    gui_settings.setValue(QStringLiteral("rollbackSentinel"), QStringLiteral("keep"));
    gui_settings.sync();

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    const std::vector<std::string> argv{TestArgv()};
    OnboardingOptionsModel model{argv, /*can_listen_ipc=*/false};
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QVERIFY(!model.listen());
    model.setListen(true);

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QmlOnboardingSettings::PendingApply pending;
    QString prepare_error;
    QVERIFY2(model.prepareApplyToArgs(args, pending, &prepare_error), qPrintable(prepare_error));

    const std::optional<common::ConfigError> init_error{
        common::InitConfig(args, [](const bilingual_str&, const std::vector<std::string>&) {
            return true;
        })
    };
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    args.SelectConfigNetwork(args.GetChainTypeString());

    bool finalized{true};
    QString finalize_error;
    finalized = QmlOnboardingSettings::FinalizeStartupSettings(
        args,
        bootstrap_gui_settings,
        &pending,
        /*result=*/nullptr,
        &finalize_error);

    QVERIFY(!finalized);
    QVERIFY(finalize_error.contains(QStringLiteral("Legacy GUI settings migration")));
    QCOMPARE(args.GetBoolArg("-listen", true), true);
    QCOMPARE(SettingToBool(args.GetPersistentSetting("qml_onboarded")), std::nullopt);

    gui_settings.sync();
    QCOMPARE(gui_settings.value(QStringLiteral("rollbackSentinel")).toString(), QStringLiteral("keep"));
    QVERIFY(!gui_settings.contains(SettingsKeys::DATA_DIR));

    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, data_dir.path());
    QCOMPARE(SettingToBool(check_args.GetPersistentSetting("qml_onboarded")), std::nullopt);

    legacy_settings.settings().sync();
    QVERIFY(legacy_settings.settings().contains(QStringLiteral("fListen")));
}

void StartupSettingsTests::onboardingApplyRollsBackWhenSettingsWriteFails()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    ArgsManager seed_args;
    PrepareArgsForDataDir(seed_args, data_dir.path());
    seed_args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["server"] = common::SettingsValue{true};
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(
        seed_args.WriteSettingsFile(&settings_errors),
        settings_errors.empty() ? "" : settings_errors.front().c_str());

    fs::path settings_path;
    QVERIFY(seed_args.GetSettingsPath(&settings_path));
    QFile settings_file{
        QString::fromStdString(fs::PathToString(settings_path))
    };
    QVERIFY(settings_file.open(QIODevice::ReadOnly));
    const QByteArray original_settings{settings_file.readAll()};
    settings_file.close();

    QSettings gui_settings;
    gui_settings.setFallbacksEnabled(false);
    gui_settings.setValue(
        QStringLiteral("settingsWriteSentinel"),
        QStringLiteral("keep"));
    gui_settings.sync();

    const std::vector<std::string> argv{
        TestArgvWithDataDir(data_dir.path())
    };
    OnboardingOptionsModel model{argv, /*can_listen_ipc=*/false};
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QmlOnboardingSettings::PendingApply pending;
    QString prepare_error;
    QVERIFY2(
        model.prepareApplyToArgs(args, pending, &prepare_error),
        qPrintable(prepare_error));

    const std::optional<common::ConfigError> init_error{
        common::InitConfig(args, [](const bilingual_str&, const std::vector<std::string>&) {
            return true;
        })
    };
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    args.SelectConfigNetwork(args.GetChainTypeString());

    fs::path temporary_settings_path;
    QVERIFY(args.GetSettingsPath(&temporary_settings_path, /*temp=*/true));
    QVERIFY(QDir().mkpath(
        QString::fromStdString(fs::PathToString(temporary_settings_path))));

    QString finalize_error;
    QVERIFY(!QmlOnboardingSettings::FinalizeStartupSettings(
        args,
        bootstrap_gui_settings,
        &pending,
        /*result=*/nullptr,
        &finalize_error));
    QVERIFY(!finalize_error.isEmpty());

    QCOMPARE(args.GetBoolArg("-server", false), true);
    QCOMPARE(
        SettingToBool(args.GetPersistentSetting("qml_onboarded")),
        std::nullopt);

    QVERIFY(settings_file.open(QIODevice::ReadOnly));
    QCOMPARE(settings_file.readAll(), original_settings);
    settings_file.close();

    gui_settings.sync();
    QCOMPARE(
        gui_settings.value(QStringLiteral("settingsWriteSentinel")).toString(),
        QStringLiteral("keep"));
    QVERIFY(!gui_settings.contains(SettingsKeys::DATA_DIR));
}

void StartupSettingsTests::onboardingApplyWithSettingsDisabledPreservesLegacyCoreValues()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings legacy_core_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    SavedNamedSettings legacy_default_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt")};
    legacy_core_settings.settings().setValue(QStringLiteral("fListen"), false);
    legacy_core_settings.settings().setValue(QStringLiteral("addrProxy"), QStringLiteral("10.0.0.1:9050"));
    legacy_default_settings.settings().setValue(SettingsKeys::DATA_DIR, QStringLiteral("/tmp/legacy-datadir"));
    legacy_default_settings.settings().setValue(QStringLiteral("fReset"), true);

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    std::vector<std::string> argv = TestArgv();
    argv.emplace_back("-nosettings");
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QmlOnboardingSettings::PendingApply pending;
    QString apply_error;
    QVERIFY2(
        QmlOnboardingSettings::PrepareApplyToArgs(
            args,
            {data_dir.path(), QmlOnboardingSettings::DataDirSource::UserSelection},
            data_dir.path(),
            {},
            QmlCoreSettings::Values{},
            /*effective_reset=*/false,
            pending,
            &apply_error),
        qPrintable(apply_error));
    QVERIFY(!args.GetSettingsPath());

    const std::optional<common::ConfigError> init_error{
        common::InitConfig(args, [](const bilingual_str&, const std::vector<std::string>&) {
            return true;
        })
    };
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    args.SelectConfigNetwork(args.GetChainTypeString());
    QVERIFY(!args.GetSettingsPath());
    QVERIFY(!args.GetBoolArg("-resetguisettings", false));

    QString finalize_error;
    QVERIFY2(
        QmlOnboardingSettings::FinalizeStartupSettings(
            args,
            bootstrap_gui_settings,
            &pending,
            nullptr,
            &finalize_error),
        qPrintable(finalize_error));

    QCOMPARE(
        legacy_core_settings.settings().value(QStringLiteral("fListen")).toBool(),
        false);
    QCOMPARE(
        legacy_core_settings.settings()
            .value(QStringLiteral("addrProxy"))
            .toString(),
        QStringLiteral("10.0.0.1:9050"));
    QVERIFY(!legacy_default_settings.settings().contains(SettingsKeys::DATA_DIR));
    QVERIFY(!legacy_default_settings.settings().contains(QStringLiteral("fReset")));

    QSettings settings;
    settings.setFallbacksEnabled(false);
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), data_dir.path());
}

void StartupSettingsTests::onboardingApplyRejectsProfileDrift()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    QSettings gui_settings;
    gui_settings.setFallbacksEnabled(false);
    gui_settings.setValue(
        QStringLiteral("profileDriftSentinel"),
        QStringLiteral("keep"));
    gui_settings.sync();

    const std::vector<std::string> argv{
        TestArgvWithDataDir(data_dir.path())
    };
    OnboardingOptionsModel model{argv, /*can_listen_ipc=*/false};
    QCOMPARE(model.previewError(), QString{});

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    QmlOnboardingSettings::PendingApply pending;
    QString prepare_error;
    QVERIFY2(
        model.prepareApplyToArgs(args, pending, &prepare_error),
        qPrintable(prepare_error));
    QVERIFY(pending.target_complete);

    const std::optional<common::ConfigError> init_error{
        common::InitConfig(args, [](const bilingual_str&, const std::vector<std::string>&) {
            return true;
        })
    };
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    args.SelectConfigNetwork(args.GetChainTypeString());

    fs::path settings_path;
    QVERIFY(args.GetSettingsPath(&settings_path));
    QFile settings_file{
        QString::fromStdString(fs::PathToString(settings_path))
    };
    QVERIFY(settings_file.open(QIODevice::ReadOnly));
    const QByteArray original_settings{settings_file.readAll()};
    settings_file.close();

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    enum class Drift {
        DataDir,
        Chain,
        SettingsPath,
        Reset,
    };
    const std::array drifts{
        Drift::DataDir,
        Drift::Chain,
        Drift::SettingsPath,
        Drift::Reset,
    };
    for (const Drift drift : drifts) {
        QmlOnboardingSettings::PendingApply changed_pending{pending};
        switch (drift) {
        case Drift::DataDir:
            changed_pending.resolved_data_dir =
                data_dir.filePath(QStringLiteral("other"));
            break;
        case Drift::Chain:
            changed_pending.resolved_chain = QStringLiteral("main");
            break;
        case Drift::SettingsPath:
            changed_pending.resolved_settings_path += QStringLiteral(".other");
            break;
        case Drift::Reset:
            changed_pending.effective_reset = !pending.effective_reset;
            break;
        }

        QString finalize_error;
        QVERIFY(!QmlOnboardingSettings::FinalizeStartupSettings(
            args,
            bootstrap_gui_settings,
            &changed_pending,
            /*result=*/nullptr,
            &finalize_error));
        QVERIFY(finalize_error.contains(QStringLiteral("profile changed")));
    }

    gui_settings.sync();
    QCOMPARE(
        gui_settings.value(QStringLiteral("profileDriftSentinel")).toString(),
        QStringLiteral("keep"));
    QVERIFY(settings_file.open(QIODevice::ReadOnly));
    QCOMPARE(settings_file.readAll(), original_settings);
    settings_file.close();

    fs::path settings_backup_path;
    QVERIFY(args.GetSettingsPath(
        &settings_backup_path,
        /*temp=*/false,
        /*backup=*/true));
    QVERIFY(!fs::exists(settings_backup_path));
    QVERIFY(!fs::exists(args.GetDataDirNet() / "guisettings.ini.bak"));
}

void StartupSettingsTests::onboardingFinalizeIgnoresUnusedBootstrapStore()
{
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    QTemporaryDir unusable_store_path;
    QVERIFY(unusable_store_path.isValid());
    QSettings unusable_store{unusable_store_path.path(), QSettings::IniFormat};
    unusable_store.setFallbacksEnabled(false);
    unusable_store.sync();
    QVERIFY(unusable_store.status() != QSettings::NoError);

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(
        PrepareTestArgs(args, TestArgvWithDataDir(data_dir.path()), parse_error),
        parse_error.c_str());

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        /*organization_name=*/{},
        /*application_name=*/{},
        /*file_name=*/unusable_store_path.path(),
        QSettings::IniFormat,
        QSettings::UserScope,
    };
    InitializeAndFinalizeSettings(
        args,
        bootstrap_gui_settings,
        /*pending=*/nullptr,
        /*result=*/nullptr);
}

void StartupSettingsTests::onboardingFinalizeIgnoresUnusedActiveStore()
{
    const QString original_app_name{QCoreApplication::applicationName()};
    [[maybe_unused]] const auto restore_app_name{
        qScopeGuard([&] {
            QCoreApplication::setApplicationName(original_app_name);
        })
    };
    QCoreApplication::setApplicationName(QStringLiteral("UnreadableActiveStore"));

    QString active_settings_path;
    {
        QSettings active_settings;
        active_settings.setFallbacksEnabled(false);
        active_settings_path = active_settings.fileName();
    }
    QVERIFY(QFile::remove(active_settings_path) || !QFileInfo::exists(active_settings_path));
    QVERIFY(QDir().mkpath(active_settings_path));
    [[maybe_unused]] const auto remove_unreadable_store{
        qScopeGuard([&] {
            QDir(active_settings_path).removeRecursively();
        })
    };
    {
        QSettings unreadable_settings;
        unreadable_settings.setFallbacksEnabled(false);
        unreadable_settings.sync();
        QVERIFY(unreadable_settings.status() != QSettings::NoError);
    }

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(
        PrepareTestArgs(args, TestArgvWithDataDir(data_dir.path()), parse_error),
        parse_error.c_str());

    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    InitializeAndFinalizeSettings(
        args,
        bootstrap_gui_settings,
        /*pending=*/nullptr,
        /*result=*/nullptr);
}

void StartupSettingsTests::onboardingApplyClearsResetFlagInBootstrapAndActiveStores()
{
    const QString original_app_name{QCoreApplication::applicationName()};
    [[maybe_unused]] const auto restore_app_name{
        qScopeGuard([&] {
            QCoreApplication::setApplicationName(original_app_name);
        })
    };
    const QString organization_name{QCoreApplication::organizationName()};
    const QString bootstrap_app_name{QStringLiteral("ResetBootstrap")};
    const QString active_app_name{QStringLiteral("ResetActive")};
    SavedNamedSettings bootstrap_settings{organization_name, bootstrap_app_name};
    SavedNamedSettings active_settings{organization_name, active_app_name};

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    QCoreApplication::setApplicationName(bootstrap_app_name);
    bootstrap_settings.settings().setValue(SettingsKeys::DATA_DIR, data_dir.path());
    bootstrap_settings.settings().setValue(QStringLiteral("fReset"), true);
    bootstrap_settings.settings().setValue(
        QStringLiteral("bootstrapResetSentinel"),
        QStringLiteral("keep"));
    bootstrap_settings.settings().sync();
    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(
        PrepareTestArgs(args, TestArgvWithDataDir(data_dir.path()), parse_error),
        parse_error.c_str());
    QmlOnboardingSettings::PendingApply pending;
    QString prepare_error;
    QVERIFY2(
        QmlOnboardingSettings::PrepareApplyToArgs(
            args,
            {
                data_dir.path(),
                QmlOnboardingSettings::DataDirSource::ExplicitArg,
            },
            data_dir.path(),
            {},
            QmlCoreSettings::Values{},
            /*effective_reset=*/false,
            pending,
            &prepare_error),
        qPrintable(prepare_error));

    const std::optional<common::ConfigError> init_error{
        common::InitConfig(args, [](const bilingual_str&, const std::vector<std::string>&) {
            return true;
        })
    };
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    args.SelectConfigNetwork(args.GetChainTypeString());

    QCoreApplication::setApplicationName(active_app_name);
    active_settings.settings().setValue(SettingsKeys::DATA_DIR, data_dir.path());
    active_settings.settings().setValue(QStringLiteral("fReset"), true);
    active_settings.settings().setValue(
        QStringLiteral("activeResetSentinel"),
        QStringLiteral("keep"));
    active_settings.settings().sync();
    QVERIFY(
        QmlOnboardingSettings::CurrentGuiSettingsStore().file_name !=
        bootstrap_gui_settings.file_name);

    QString finalize_error;
    QVERIFY2(
        QmlOnboardingSettings::FinalizeStartupSettings(
            args,
            bootstrap_gui_settings,
            &pending,
            /*result=*/nullptr,
            &finalize_error),
        qPrintable(finalize_error));

    bootstrap_settings.settings().sync();
    QCOMPARE(
        bootstrap_settings.settings().value(QStringLiteral("fReset")).toBool(),
        false);
    QCOMPARE(
        bootstrap_settings.settings()
            .value(QStringLiteral("bootstrapResetSentinel"))
            .toString(),
        QStringLiteral("keep"));

    active_settings.settings().sync();
    QCOMPARE(
        active_settings.settings().value(QStringLiteral("fReset")).toBool(),
        false);
    QCOMPARE(
        active_settings.settings()
            .value(QStringLiteral("activeResetSentinel"))
            .toString(),
        QStringLiteral("keep"));
}

BITCOINQML_REGISTER_QT_TEST(StartupSettingsTests)
#include <test_startupsettings.moc>
