// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/startupsettings_test_util.h>
#include <qml/test/qt_test_registry.h>

using namespace startupsettingstest;

class OnboardingProfileTests : public QObject
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
        QCoreApplication::setApplicationName("OnboardingProfileTests");
    }
    void cleanupTestCase()
    {
        QCoreApplication::setOrganizationName(m_original_organization);
        QCoreApplication::setApplicationName(m_original_application);
    }
    void guiDataDirChooserShowsForUnreadableConfiguredDir();
    void unreadableConfigDoesNotEscapeStartupResolution();
    void invalidExplicitDataDirReturnsErrorWithoutReadingProfile();
    void validRelativeExplicitDataDirResolvesAgainstWorkingDirectory();
    void qmlOnboardedProfileSkipsPreInitOnboarding();
    void qmlOnboardedCommandLineOverrideShowsPreInitOnboarding();
    void qmlOnboardedConfiguredDatadirProfileSkipsPreInitOnboarding();
    void configuredDatadirPreviewKeepsConfigSource();
    void configuredDatadirApplyDoesNotPersistGuiDataDir();
    void guiDatadirTakesPrecedenceOverConfigDatadir();
    void guiDatadirConfigUserSelectionPersistsNewPath();
    void explicitDatadirApplyDoesNotPersistGuiDataDir();
    void qmlOnboardedResetGuiSettingsShowsPreInitOnboarding();
    void qmlOnboardedChooseDataDirShowsPreInitOnboarding();
    void qmlOnboardedCurrentResetFlagShowsPreInitOnboarding();
    void qmlOnboardedResolvedNetworkResetFlagShowsPreInitOnboarding();
    void qmlOnboardedLegacyResetFlagShowsPreInitOnboarding();
    void onboardingApplyWithoutTouchedSettingsOnlyAddsQmlOnboardedMarker();
    void onboardingApplyCreatesNewCustomDataDir();
    void onboardingStorageCheckUsesResolvedDataDir();
    void onboardingApplyMigratesLegacySettingsBeforeTouchedOverrides();
    void onboardingPreviewHelperReadsSelectedDatadirConfig();
    void onboardingPreviewReadsSelectedDatadirConfig();
    void onboardingApplyWritesTouchedConfigOverride();
    void onboardingApplyWritesTouchedParameterInteractionOverride();
    void onboardingApplyRetainsListenChoiceWhenDisablingProxy();
};

void OnboardingProfileTests::guiDataDirChooserShowsForUnreadableConfiguredDir()
{
#ifdef Q_OS_WIN
    QSKIP("This test relies on POSIX directory permissions.");
#else
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString data_dir = QDir(temp_dir.path()).filePath("unreadable-data-dir");
    QVERIFY(QDir().mkpath(data_dir));

    QFile config_file{QDir(data_dir).filePath(QStringLiteral("bitcoin.conf"))};
    QVERIFY(config_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(config_file.write("regtest=1\n") > 0);
    config_file.close();
    settings.setValue(SettingsKeys::DATA_DIR, data_dir);

    const QFileDevice::Permissions original_permissions = QFileInfo(data_dir).permissions();
    [[maybe_unused]] const auto restore_permissions = qScopeGuard([&] {
        QFile(data_dir).setPermissions(original_permissions);
    });
    QVERIFY(QFile(data_dir).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    const QFileInfo inaccessible_info{data_dir};
    if (inaccessible_info.isExecutable()) {
        QSKIP("Cannot make temporary data directory untraversable on this platform.");
    }

    QVERIFY(!QmlDataDir::ValidateCustomDataDir(data_dir).isEmpty());
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(
            TestArgv(),
            /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.should_show_onboarding);
    QCOMPARE(status.selected_data_dir, data_dir);
    QCOMPARE(status.resolved_data_dir, data_dir);
#endif
}

void OnboardingProfileTests::unreadableConfigDoesNotEscapeStartupResolution()
{
#ifdef Q_OS_WIN
    QSKIP("This test relies on POSIX directory permissions.");
#else
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString data_dir = QDir(temp_dir.path()).filePath("data-dir");
    const QString protected_dir = QDir(temp_dir.path()).filePath("protected");
    QVERIFY(QDir().mkpath(data_dir));
    QVERIFY(QDir().mkpath(protected_dir));

    const QString protected_config = QDir(protected_dir).filePath(QStringLiteral("bitcoin.conf"));
    QFile config_file{protected_config};
    QVERIFY(config_file.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(config_file.write("regtest=1\n") > 0);
    config_file.close();
    QVERIFY(QFile::link(protected_config, QDir(data_dir).filePath(QStringLiteral("bitcoin.conf"))));
    settings.setValue(SettingsKeys::DATA_DIR, data_dir);

    const QFileDevice::Permissions original_permissions = QFileInfo(protected_dir).permissions();
    [[maybe_unused]] const auto restore_permissions = qScopeGuard([&] {
        QFile(protected_dir).setPermissions(original_permissions);
    });
    QVERIFY(QFile(protected_dir).setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
    if (QFileInfo(protected_config).exists()) {
        QSKIP("Cannot make the linked configuration file inaccessible on this platform.");
    }

    QVERIFY(QmlDataDir::ValidateCustomDataDir(data_dir).isEmpty());
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(
            TestArgv(),
            /*can_listen_ipc=*/false)
    };
    QVERIFY(!status.ok);
    QVERIFY(!status.error.isEmpty());
#endif
}

void OnboardingProfileTests::invalidExplicitDataDirReturnsErrorWithoutReadingProfile()
{
    CurrentDirectoryRestorer restore_current_dir;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir::setCurrent(temp_dir.path()));

    for (const std::string& data_dir : {
             std::string{"relative-missing"},
             std::string{"~/altdatadir"},
         }) {
        std::vector<std::string> argv{TestArgv()};
        argv.emplace_back("-datadir=" + data_dir);

        const QmlOnboardingSettings::OnboardingStartupStatus status{
            QmlOnboardingSettings::ResolveOnboardingStartupStatus(
                argv,
                /*can_listen_ipc=*/false)
        };
        QVERIFY(!status.ok);
        QVERIFY2(status.error.contains(QStringLiteral("does not exist")), qPrintable(status.error));

        const QmlOnboardingSettings::PreviewResult preview{
            QmlOnboardingSettings::Preview(
                argv,
                /*can_listen_ipc=*/false,
                temp_dir.path())
        };
        QVERIFY(!preview.ok);
        QVERIFY2(preview.error.contains(QStringLiteral("does not exist")), qPrintable(preview.error));
    }
}

void OnboardingProfileTests::validRelativeExplicitDataDirResolvesAgainstWorkingDirectory()
{
    CurrentDirectoryRestorer restore_current_dir;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QVERIFY(QDir::setCurrent(temp_dir.path()));

    const QString relative_data_dir{QStringLiteral("relative-data-dir")};
    const QString absolute_data_dir{QDir(temp_dir.path()).filePath(relative_data_dir)};
    QVERIFY(QDir().mkpath(relative_data_dir));

    std::vector<std::string> argv{TestArgv()};
    argv.emplace_back("-datadir=" + relative_data_dir.toStdString());

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(
            argv,
            /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QCOMPARE(status.selected_data_dir, absolute_data_dir);
    QCOMPARE(status.selected_data_dir_source, QmlOnboardingSettings::DataDirSource::ExplicitArg);
    QCOMPARE(status.resolved_data_dir, absolute_data_dir);
    QCOMPARE(status.resolved_data_dir_source, QmlOnboardingSettings::DataDirSource::ExplicitArg);
    QVERIFY(!status.config_redirected_data_dir);

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(
            argv,
            /*can_listen_ipc=*/false,
            temp_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QCOMPARE(preview.selected_data_dir, absolute_data_dir);
    QCOMPARE(preview.selected_data_dir_source, QmlOnboardingSettings::DataDirSource::ExplicitArg);
    QCOMPARE(preview.resolved_data_dir, absolute_data_dir);
    QCOMPARE(preview.resolved_data_dir_source, QmlOnboardingSettings::DataDirSource::ExplicitArg);
    QVERIFY(!preview.config_redirected_data_dir);
}

void OnboardingProfileTests::qmlOnboardedProfileSkipsPreInitOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    ArgsManager write_args;
    PrepareArgsForDataDir(write_args, data_dir.path());
    QString write_error;
    QVERIFY2(QmlOnboardingSettings::MarkQmlOnboarded(write_args, &write_error), qPrintable(write_error));

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.settings_enabled);
    QVERIFY(status.qml_onboarded);
    QVERIFY(!status.should_show_onboarding);
    QCOMPARE(status.active_data_dir, data_dir.path());
}

void OnboardingProfileTests::qmlOnboardedCommandLineOverrideShowsPreInitOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    ArgsManager write_args;
    PrepareArgsForDataDir(write_args, data_dir.path());
    QString write_error;
    QVERIFY2(QmlOnboardingSettings::MarkQmlOnboarded(write_args, &write_error), qPrintable(write_error));

    std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    argv.emplace_back("-qml_onboarded=0");
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.settings_enabled);
    QVERIFY(!status.qml_onboarded);
    QVERIFY(status.should_show_onboarding);
    QCOMPARE(status.active_data_dir, data_dir.path());
}

void OnboardingProfileTests::qmlOnboardedConfiguredDatadirProfileSkipsPreInitOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QTemporaryDir configured_data_dir;
    QVERIFY(configured_data_dir.isValid());

    const QString conf_path = QDir(temp_dir.path()).filePath(QStringLiteral("bitcoin.conf"));
    QFile conf(conf_path);
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write(QStringLiteral("regtest=1\ndatadir=%1\n[regtest]\nserver=1\n").arg(configured_data_dir.path()).toUtf8()) > 0);
    conf.close();

    const std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-conf=" + conf_path.toStdString(),
    };

    ArgsManager write_args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(write_args, argv, parse_error), parse_error.c_str());
    std::string config_error;
    QVERIFY2(write_args.ReadConfigFiles(config_error, true), config_error.c_str());
    SelectParams(write_args.GetChainType());
    write_args.SelectConfigNetwork(write_args.GetChainTypeString());
    QString write_error;
    QVERIFY2(QmlOnboardingSettings::MarkQmlOnboarded(write_args, &write_error), qPrintable(write_error));

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.qml_onboarded);
    QVERIFY(!status.should_show_onboarding);
    QCOMPARE(status.active_data_dir, configured_data_dir.path());
    QVERIFY(status.data_dir_source == QmlOnboardingSettings::DataDirSource::Config);
}

void OnboardingProfileTests::configuredDatadirPreviewKeepsConfigSource()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QTemporaryDir configured_data_dir;
    QVERIFY(configured_data_dir.isValid());

    QSettings settings;
    settings.setValue(SettingsKeys::DATA_DIR, QmlDataDir::DefaultDataDirString());

    const QString conf_path = QDir(temp_dir.path()).filePath(QStringLiteral("bitcoin.conf"));
    QFile conf(conf_path);
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write(QStringLiteral("regtest=1\ndatadir=%1\n[regtest]\nserver=1\n").arg(configured_data_dir.path()).toUtf8()) > 0);
    conf.close();

    const std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-conf=" + conf_path.toStdString(),
    };

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QCOMPARE(status.active_data_dir, configured_data_dir.path());
    QVERIFY(status.data_dir_source == QmlOnboardingSettings::DataDirSource::Config);

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(
            argv,
            /*can_listen_ipc=*/false,
            QmlOnboardingSettings::DataDirSelection{status.active_data_dir, status.data_dir_source})
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.server);
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("server")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));
}

void OnboardingProfileTests::configuredDatadirApplyDoesNotPersistGuiDataDir()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QTemporaryDir configured_data_dir;
    QVERIFY(configured_data_dir.isValid());

    QSettings settings;
    settings.remove(SettingsKeys::DATA_DIR);

    const QString conf_path = QDir(temp_dir.path()).filePath(QStringLiteral("bitcoin.conf"));
    QFile conf(conf_path);
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write(QStringLiteral("regtest=1\ndatadir=%1\n[regtest]\nserver=1\n").arg(configured_data_dir.path()).toUtf8()) > 0);
    conf.close();

    const std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-conf=" + conf_path.toStdString(),
    };

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QCOMPARE(status.active_data_dir, configured_data_dir.path());
    QVERIFY(status.data_dir_source == QmlOnboardingSettings::DataDirSource::Config);

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(
            argv,
            /*can_listen_ipc=*/false,
            QmlOnboardingSettings::DataDirSelection{status.active_data_dir, status.data_dir_source})
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.server);

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeApply(
        args,
        QmlOnboardingSettings::DataDirSelection{status.active_data_dir, status.data_dir_source},
        preview.resolved_data_dir,
        {},
        preview.values);

    QVERIFY(!settings.contains(SettingsKeys::DATA_DIR));
    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, configured_data_dir.path());
    QCOMPARE(SettingToBool(check_args.GetPersistentSetting("qml_onboarded")), true);
    QVERIFY(QFileInfo(QDir(configured_data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))).isFile());
}

void OnboardingProfileTests::guiDatadirTakesPrecedenceOverConfigDatadir()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir selected_data_dir;
    QTemporaryDir resolved_data_dir;
    QVERIFY(selected_data_dir.isValid());
    QVERIFY(resolved_data_dir.isValid());
    settings.setValue(SettingsKeys::DATA_DIR, selected_data_dir.path());

    QFile conf(QDir(selected_data_dir.path()).filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write(QStringLiteral("regtest=1\ndatadir=%1\n[regtest]\nserver=1\n").arg(resolved_data_dir.path()).toUtf8()) > 0);
    conf.close();

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(TestArgv(), /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QCOMPARE(status.selected_data_dir, selected_data_dir.path());
    QVERIFY(status.selected_data_dir_source == QmlOnboardingSettings::DataDirSource::GuiSetting);

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(
            TestArgv(),
            /*can_listen_ipc=*/false,
            QmlOnboardingSettings::DataDirSelection{status.selected_data_dir, status.selected_data_dir_source})
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QCOMPARE(preview.resolved_data_dir, selected_data_dir.path());
    QVERIFY(preview.values.server);
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("server")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));

    OnboardingOptionsModel model(TestArgv(), /*can_listen_ipc=*/false);
    QCOMPARE(model.dataDir(), selected_data_dir.path());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgv(), parse_error), parse_error.c_str());
    PrepareAndFinalizeModelApply(model, args);
    QCOMPARE(QmlDataDir::NormalizeLocalPath(QString::fromStdString(fs::PathToString(args.GetDataDirBase()))), selected_data_dir.path());
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), selected_data_dir.path());
    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, selected_data_dir.path());
    QCOMPARE(SettingToBool(check_args.GetPersistentSetting("qml_onboarded")), true);
    QVERIFY(QFileInfo(QDir(selected_data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))).isFile());
    QVERIFY(!QFileInfo(QDir(resolved_data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))).exists());
}

void OnboardingProfileTests::guiDatadirConfigUserSelectionPersistsNewPath()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir selected_data_dir;
    QTemporaryDir resolved_data_dir;
    QTemporaryDir new_data_dir;
    QVERIFY(selected_data_dir.isValid());
    QVERIFY(resolved_data_dir.isValid());
    QVERIFY(new_data_dir.isValid());
    settings.setValue(SettingsKeys::DATA_DIR, selected_data_dir.path());

    QFile conf(QDir(selected_data_dir.path()).filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write(QStringLiteral("regtest=1\ndatadir=%1\n[regtest]\nserver=1\n").arg(resolved_data_dir.path()).toUtf8()) > 0);
    conf.close();

    OnboardingOptionsModel model(TestArgv(), /*can_listen_ipc=*/false);
    QCOMPARE(model.dataDir(), selected_data_dir.path());
    QVERIFY(model.selectCustomDataDir(new_data_dir.path()));

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgv(), parse_error), parse_error.c_str());
    PrepareAndFinalizeModelApply(model, args);
    QCOMPARE(QmlDataDir::NormalizeLocalPath(QString::fromStdString(fs::PathToString(args.GetDataDirBase()))), new_data_dir.path());
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), new_data_dir.path());
    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, new_data_dir.path());
    QCOMPARE(SettingToBool(check_args.GetPersistentSetting("qml_onboarded")), true);
    QVERIFY(QFileInfo(QDir(new_data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))).isFile());
    QVERIFY(!QFileInfo(QDir(resolved_data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))).exists());
}

void OnboardingProfileTests::explicitDatadirApplyDoesNotPersistGuiDataDir()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QSettings settings;
    settings.remove(SettingsKeys::DATA_DIR);

    const std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QCOMPARE(model.dataDir(), data_dir.path());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeModelApply(model, args);

    QVERIFY(!settings.contains(SettingsKeys::DATA_DIR));
    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, data_dir.path());
    QCOMPARE(SettingToBool(check_args.GetPersistentSetting("qml_onboarded")), true);
}

void OnboardingProfileTests::qmlOnboardedResetGuiSettingsShowsPreInitOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    ArgsManager write_args;
    PrepareArgsForDataDir(write_args, data_dir.path());
    QString write_error;
    QVERIFY2(QmlOnboardingSettings::MarkQmlOnboarded(write_args, &write_error), qPrintable(write_error));

    std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    argv.emplace_back("-resetguisettings");
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(!status.qml_onboarded);
    QVERIFY(status.should_show_onboarding);
    QCOMPARE(status.active_data_dir, data_dir.path());
}

void OnboardingProfileTests::qmlOnboardedChooseDataDirShowsPreInitOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    settings.setValue(SettingsKeys::DATA_DIR, data_dir.path());

    ArgsManager write_args;
    PrepareArgsForDataDir(write_args, data_dir.path());
    QString write_error;
    QVERIFY2(QmlOnboardingSettings::MarkQmlOnboarded(write_args, &write_error), qPrintable(write_error));

    std::vector<std::string> argv{TestArgv()};
    argv.emplace_back("-choosedatadir");
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(!status.qml_onboarded);
    QVERIFY(status.should_show_onboarding);
    QCOMPARE(status.active_data_dir, data_dir.path());
}

void OnboardingProfileTests::qmlOnboardedCurrentResetFlagShowsPreInitOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    settings.setValue(SettingsKeys::DATA_DIR, data_dir.path());
    settings.setValue(QStringLiteral("fReset"), true);

    ArgsManager write_args;
    PrepareArgsForDataDir(write_args, data_dir.path());
    QString write_error;
    QVERIFY2(QmlOnboardingSettings::MarkQmlOnboarded(write_args, &write_error), qPrintable(write_error));

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(TestArgv(), /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(!status.qml_onboarded);
    QVERIFY(status.should_show_onboarding);
    QCOMPARE(status.active_data_dir, data_dir.path());
}

void OnboardingProfileTests::qmlOnboardedResolvedNetworkResetFlagShowsPreInitOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings active_settings{
        QCoreApplication::organizationName(),
        QStringLiteral(QAPP_APP_NAME_REGTEST),
    };
    QSettings bootstrap_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    bootstrap_settings.setValue(SettingsKeys::DATA_DIR, data_dir.path());
    bootstrap_settings.setValue(QStringLiteral("fReset"), false);
    active_settings.settings().setValue(QStringLiteral("fReset"), true);
    active_settings.settings().sync();

    QFile conf{data_dir.filePath(QStringLiteral("bitcoin.conf"))};
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("regtest=1\n") > 0);
    conf.close();

    ArgsManager write_args;
    PrepareArgsForDataDir(write_args, data_dir.path());
    QString write_error;
    QVERIFY2(
        QmlOnboardingSettings::MarkQmlOnboarded(write_args, &write_error),
        qPrintable(write_error));

    const std::vector<std::string> argv{std::string{"bitcoinqml"}};
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(
            argv,
            /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(!status.qml_onboarded);
    QVERIFY(status.should_show_onboarding);
    QCOMPARE(status.active_data_dir, data_dir.path());

    active_settings.settings().sync();
    QCOMPARE(
        active_settings.settings().value(QStringLiteral("fReset")).toBool(),
        true);
}

void OnboardingProfileTests::qmlOnboardedLegacyResetFlagShowsPreInitOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings legacy_default_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt")};
    QSettings settings;
    settings.remove(SettingsKeys::DATA_DIR);
    settings.remove(QStringLiteral("fReset"));

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    legacy_default_settings.settings().setValue(SettingsKeys::DATA_DIR, data_dir.path());
    legacy_default_settings.settings().setValue(QStringLiteral("fReset"), true);

    ArgsManager write_args;
    PrepareArgsForDataDir(write_args, data_dir.path());
    QString write_error;
    QVERIFY2(QmlOnboardingSettings::MarkQmlOnboarded(write_args, &write_error), qPrintable(write_error));

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(TestArgv(), /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(!status.qml_onboarded);
    QVERIFY(status.should_show_onboarding);
    QCOMPARE(status.active_data_dir, data_dir.path());

    QmlDataDir::PersistDefaultDataDirSelection();
    QVERIFY(!legacy_default_settings.settings().contains(SettingsKeys::DATA_DIR));
    QVERIFY(!legacy_default_settings.settings().contains(QStringLiteral("fReset")));
}

void OnboardingProfileTests::onboardingApplyWithoutTouchedSettingsOnlyAddsQmlOnboardedMarker()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("regtest=1\n[regtest]\nlisten=0\n") > 0);
    conf.close();

    ArgsManager args;
    std::string parse_error;
    const std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());

    PrepareAndFinalizeApply(
        args,
        QmlOnboardingSettings::DataDirSelection{
            data_dir.path(),
            QmlOnboardingSettings::DataDirSource::UserSelection,
        },
        data_dir.path(),
        {},
        QmlCoreSettings::Values{});

    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, data_dir.path());
    QCOMPARE(SettingToBool(check_args.GetPersistentSetting("qml_onboarded")), true);
    bool has_listen_override{true};
    bool has_prune_override{true};
    check_args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
        has_prune_override = settings.rw_settings.count("prune") > 0;
    });
    QVERIFY(!has_listen_override);
    QVERIFY(!has_prune_override);
}

void OnboardingProfileTests::onboardingApplyCreatesNewCustomDataDir()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings gui_settings;
    gui_settings.remove(SettingsKeys::DATA_DIR);

    QTemporaryDir parent_dir;
    QVERIFY(parent_dir.isValid());
    const QString data_dir{parent_dir.filePath(QStringLiteral("new-data-dir"))};
    QVERIFY(!QFileInfo::exists(data_dir));

    const std::vector<std::string> argv{TestArgv()};
    OnboardingOptionsModel model{argv, /*can_listen_ipc=*/false};
    QVERIFY(model.selectCustomDataDir(data_dir));
    QCOMPARE(model.previewError(), QString{});

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeModelApply(model, args);

    QVERIFY(QFileInfo(data_dir).isDir());
    QVERIFY(QFileInfo(QDir(data_dir).filePath(QStringLiteral("regtest/wallets"))).isDir());
    QCOMPARE(gui_settings.value(SettingsKeys::DATA_DIR).toString(), data_dir);
    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, data_dir);
    QCOMPARE(SettingToBool(check_args.GetPersistentSetting("qml_onboarded")), true);
}

void OnboardingProfileTests::onboardingStorageCheckUsesResolvedDataDir()
{
#ifdef Q_OS_WIN
    QSKIP("This test isolates the default datadir through HOME.");
#else
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir home_dir;
    QTemporaryDir config_dir;
    QTemporaryDir resolved_parent;
    QVERIFY(home_dir.isValid());
    QVERIFY(config_dir.isValid());
    QVERIFY(resolved_parent.isValid());

    const bool had_home{qEnvironmentVariableIsSet("HOME")};
    const QByteArray original_home{qgetenv("HOME")};
    [[maybe_unused]] const auto restore_home{qScopeGuard([&] {
        if (had_home) {
            qputenv("HOME", original_home);
        } else {
            qunsetenv("HOME");
        }
    })};
    QVERIFY(qputenv("HOME", home_dir.path().toUtf8()));

    const QString selected_data_dir{QmlDataDir::DefaultDataDirString()};
    QVERIFY(!QFileInfo::exists(selected_data_dir));
    const QString resolved_data_dir{
        resolved_parent.filePath(QStringLiteral("resolved"))
    };
    QVERIFY(QDir().mkpath(resolved_data_dir));

    const QString config_path{
        config_dir.filePath(QStringLiteral("bitcoin.conf"))
    };
    QFile config_file{config_path};
    QVERIFY(config_file.open(QIODevice::WriteOnly | QIODevice::Text));
    const QByteArray config{
        QStringLiteral("regtest=1\ndatadir=%1\n")
            .arg(resolved_data_dir)
            .toUtf8()
    };
    QCOMPARE(config_file.write(config), config.size());
    config_file.close();

    QSettings settings;
    settings.remove(SettingsKeys::DATA_DIR);

    const std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-conf=" + config_path.toStdString(),
    };
    OnboardingOptionsModel model{argv, /*can_listen_ipc=*/false};
    model.useDefaultDataDir();
    QCOMPARE(model.dataDir(), selected_data_dir);
    QCOMPARE(model.previewError(), QString{});
    QTRY_VERIFY_WITH_TIMEOUT(!model.storageCheckPending(), 5000);
    QVERIFY2(
        model.storagePathMessage().contains(
            QStringLiteral("directory already exists"),
            Qt::CaseInsensitive),
        qPrintable(model.storagePathMessage()));
#endif
}

void OnboardingProfileTests::onboardingApplyMigratesLegacySettingsBeforeTouchedOverrides()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    legacy_settings.settings().setValue(QStringLiteral("fListen"), false);
    legacy_settings.settings().setValue(QStringLiteral("server"), true);

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    const std::vector<std::string> argv = TestArgv();
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QVERIFY(!model.listen());
    QVERIFY(model.server());

    model.setListen(true);
    QVERIFY(model.listen());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeModelApply(model, args);

    QCOMPARE(args.GetBoolArg("-listen", true), true);
    QCOMPARE(args.GetBoolArg("-server", false), true);

    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, data_dir.path());
    bool has_listen_override{true};
    common::SettingsValue server;
    check_args.LockSettings([&](common::Settings& core_settings) {
        has_listen_override = core_settings.rw_settings.count("listen") > 0;
        server = core_settings.rw_settings.at("server");
    });
    QVERIFY(!has_listen_override);
    QCOMPARE(SettingToBool(server), true);
    QVERIFY(!legacy_settings.settings().contains(QStringLiteral("fListen")));
    QVERIFY(!legacy_settings.settings().contains(QStringLiteral("server")));
}

void OnboardingProfileTests::onboardingPreviewHelperReadsSelectedDatadirConfig()
{
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("listen=0\n") > 0);
    conf.close();

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(TestArgv(), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("listen")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));
    QVERIFY(!preview.values.listen);
}

void OnboardingProfileTests::onboardingPreviewReadsSelectedDatadirConfig()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("listen=0\n") > 0);
    conf.close();

    OnboardingOptionsModel model(TestArgv(), /*can_listen_ipc=*/false);
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QCOMPARE(model.previewError(), QString{});
    QCOMPARE(model.coreSettingStatuses().value(QStringLiteral("listen")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));
    QVERIFY(!model.listen());
}

void OnboardingProfileTests::onboardingApplyWritesTouchedConfigOverride()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("listen=0\n") > 0);
    conf.close();

    const std::vector<std::string> argv = TestArgv();
    QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.values.listen);
    preview.values.listen = true;

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeApply(
        args,
        QmlOnboardingSettings::DataDirSelection{
            data_dir.path(),
            QmlOnboardingSettings::DataDirSource::UserSelection,
        },
        preview.resolved_data_dir,
        QSet<QString>{QStringLiteral("listen")},
        preview.values);

    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, data_dir.path());
    common::SettingsValue listen_override;
    check_args.LockSettings([&](common::Settings& settings) {
        if (const auto* value = common::FindKey(settings.rw_settings, "listen")) {
            listen_override = *value;
        }
    });
    QVERIFY(listen_override.isBool());
    QVERIFY(listen_override.get_bool());
}

void OnboardingProfileTests::onboardingApplyWritesTouchedParameterInteractionOverride()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("regtest=1\n[regtest]\nproxy=10.0.0.3:9050\n") > 0);
    conf.close();

    const std::vector<std::string> argv = TestArgv();
    QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.proxy_enabled);
    QVERIFY(!preview.values.listen);

    const QVariantMap listen_status = preview.core_setting_statuses.value(QStringLiteral("listen")).toMap();
    QCOMPARE(listen_status.value(QStringLiteral("source")).toString(), QStringLiteral("startup"));
    QCOMPARE(listen_status.value(QStringLiteral("canEdit")).toBool(), true);

    preview.values.listen = true;

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeApply(
        args,
        QmlOnboardingSettings::DataDirSelection{
            data_dir.path(),
            QmlOnboardingSettings::DataDirSource::UserSelection,
        },
        preview.resolved_data_dir,
        QSet<QString>{QStringLiteral("listen")},
        preview.values);

    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, data_dir.path());
    common::SettingsValue listen_override;
    bool has_forced_listen{true};
    bool has_forced_natpmp{true};
    bool has_forced_discover{true};
    check_args.LockSettings([&](common::Settings& settings) {
        if (const auto* value = common::FindKey(settings.rw_settings, "listen")) {
            listen_override = *value;
        }
        has_forced_listen = settings.forced_settings.count("listen") > 0;
        has_forced_natpmp = settings.forced_settings.count("natpmp") > 0;
        has_forced_discover = settings.forced_settings.count("discover") > 0;
    });

    QVERIFY(listen_override.isBool());
    QVERIFY(listen_override.get_bool());
    QVERIFY(!has_forced_listen);
    QVERIFY(!has_forced_natpmp);
    QVERIFY(!has_forced_discover);
}

void OnboardingProfileTests::onboardingApplyRetainsListenChoiceWhenDisablingProxy()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    ArgsManager seed_args;
    std::string parse_error;
    const std::vector<std::string> seed_argv{TestArgvWithDataDir(data_dir.path())};
    QVERIFY2(PrepareTestArgs(seed_args, seed_argv, parse_error), parse_error.c_str());
    SelectParams(seed_args.GetChainType());
    seed_args.SelectConfigNetwork(seed_args.GetChainTypeString());
    seed_args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"10.0.0.3:9050"}};
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(
        seed_args.WriteSettingsFile(&settings_errors),
        settings_errors.empty() ? "" : settings_errors.front().c_str());

    const std::vector<std::string> argv{TestArgv()};
    QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.proxy_enabled);
    QVERIFY(!preview.values.listen);
    preview.values.proxy_enabled = false;
    preview.values.listen = false;

    ArgsManager args;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    PrepareAndFinalizeApply(
        args,
        QmlOnboardingSettings::DataDirSelection{
            data_dir.path(),
            QmlOnboardingSettings::DataDirSource::UserSelection,
        },
        preview.resolved_data_dir,
        QSet<QString>{
            QStringLiteral("proxy"),
            QStringLiteral("listen"),
        },
        preview.values);

    ArgsManager check_args;
    ReadSettingsForDataDir(check_args, data_dir.path());
    common::SettingsValue listen_override;
    bool has_proxy_override{true};
    check_args.LockSettings([&](common::Settings& settings) {
        if (const auto* value = common::FindKey(settings.rw_settings, "listen")) {
            listen_override = *value;
        }
        has_proxy_override = settings.rw_settings.count("proxy") > 0;
    });
    QVERIFY(listen_override.isBool());
    QVERIFY(!listen_override.get_bool());
    QVERIFY(!has_proxy_override);
}

BITCOINQML_REGISTER_QT_TEST(OnboardingProfileTests)
#include <test_onboarding_profile.moc>
