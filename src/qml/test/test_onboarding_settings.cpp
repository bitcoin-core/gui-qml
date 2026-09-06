// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef Q_MOC_RUN
#include <chainparams.h>
#include <chainparamsbase.h>
#include <common/args.h>
#include <common/init.h>
#include <common/settings.h>
#include <qml/guiargs.h>
#include <qml/onboarding_settings.h>
#include <qml/test/qt_test_registry.h>
#include <qml/test/startupsettings_test_util.h>
#include <test/util/setup_common.h>
#include <univalue.h>
#include <util/fs.h>
#endif

#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMap>
#include <QTemporaryDir>
#include <QTest>

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

bool WriteFile(const QString& path, const QByteArray& contents)
{
    if (!QDir{}.mkpath(QFileInfo{path}.absolutePath())) return false;
    QFile file{path};
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

QMap<QString, QByteArray> ProfileFiles(const QString& path)
{
    QMap<QString, QByteArray> result;
    QDirIterator files{path, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot, QDirIterator::Subdirectories};
    while (files.hasNext()) {
        QFile file{files.next()};
        const QString relative{QDir{path}.relativeFilePath(file.fileName())};
        if (files.fileInfo().isDir()) {
            result.insert(relative + '/', {});
        } else if (file.open(QIODevice::ReadOnly)) {
            result.insert(relative, file.readAll());
        }
    }
    return result;
}

QString SignetDirectory(const char* challenge)
{
    ArgsManager args;
    args.ForceSetArg("-signetchallenge", challenge);
    return QString::fromStdString(CreateBaseChainParams(args, ChainType::SIGNET)->DataDir());
}

std::vector<std::string> ProfileArgs(const QString& path)
{
    return {"bitcoin-qt", "-datadir=" + path.toStdString()};
}

std::string GlobalSettings()
{
    UniValue result{UniValue::VARR};
    gArgs.LockSettings([&](const common::Settings& settings) {
        for (const auto& [name, value] : settings.forced_settings) {
            result.push_back(name);
            result.push_back(value);
        }
        for (const auto& [name, values] : settings.command_line_options) {
            result.push_back(name);
            for (const auto& value : values) result.push_back(value);
        }
        for (const auto& [name, value] : settings.rw_settings) {
            result.push_back(name);
            result.push_back(value);
        }
        for (const auto& [section, entries] : settings.ro_config) {
            result.push_back(section);
            for (const auto& [name, values] : entries) {
                result.push_back(name);
                for (const auto& value : values) result.push_back(value);
            }
        }
    });
    return result.write();
}

} // namespace

class OnboardingSettingsTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<BasicTestingSetup> m_setup;
    QString m_original_organization;
    QString m_original_application;

private Q_SLOTS:
    void initTestCase()
    {
        m_original_organization = QCoreApplication::organizationName();
        m_original_application = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName("BitcoinCoreAppTest");
        QCoreApplication::setApplicationName("OnboardingSettingsTests");
    }

    void cleanupTestCase()
    {
        QCoreApplication::setOrganizationName(m_original_organization);
        QCoreApplication::setApplicationName(m_original_application);
    }

    void init()
    {
        m_setup = std::make_unique<BasicTestingSetup>(ChainType::REGTEST);
        SetupQmlGuiArgs(gArgs);
    }

    void cleanup() { m_setup.reset(); }

    void customSignetProfilesDoNotChangeGlobalParameters();
    void customSignetSettingsPaths_data();
    void customSignetSettingsPaths();
    void customSignetChainDataAndNewDirectoryPreview();
    void customSignetPreviewMatchesCoreStartup();
    void customSignetOnboardingCompletes_data();
    void customSignetOnboardingCompletes();
};

void OnboardingSettingsTests::customSignetProfilesDoNotChangeGlobalParameters()
{
    QTemporaryDir first;
    QTemporaryDir second;
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QVERIFY(WriteFile(first.filePath("bitcoin.conf"), "signet=1\n[signet]\nsignetchallenge=51\n"));
    QVERIFY(WriteFile(second.filePath("bitcoin.conf"), "signet=1\n[signet]\nsignetchallenge=52\n"));
    QVERIFY(WriteFile(first.filePath(SignetDirectory("51") + "/settings.json"), "{\"qml_onboarded\":true,\"prune\":2000,\"lang\":\"es\"}"));
    QVERIFY(WriteFile(second.filePath(SignetDirectory("52") + "/settings.json"), "{\"qml_onboarded\":false,\"prune\":0,\"lang\":\"de\"}"));
    QVERIFY(WriteFile(first.filePath("signet/settings.json"), "{\"qml_onboarded\":false,\"prune\":0,\"lang\":\"fr\"}"));
    QVERIFY(WriteFile(second.filePath("signet/settings.json"), "{\"qml_onboarded\":true,\"prune\":2000,\"lang\":\"fr\"}"));
    const auto first_files{ProfileFiles(first.path())};
    const auto second_files{ProfileFiles(second.path())};

    gArgs.LockSettings([](common::Settings& settings) {
        settings.ro_config["regtest"]["rpcport"] = {"19001"};
        settings.ro_config["signet"]["rpcport"] = {"19002"};
    });
    const auto global_settings{GlobalSettings()};
    const CChainParams* const chain_params{&Params()};
    const CBaseChainParams* const base_params{&BaseParams()};

    for (const bool use_first : {true, false, true}) {
        const QString path{use_first ? first.path() : second.path()};
        auto argv{ProfileArgs(path)};
        const auto status{QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, false)};
        QVERIFY2(status.ok, qPrintable(status.error));
        QCOMPARE(status.qml_onboarded, use_first);
        QCOMPARE(status.should_show_onboarding, !use_first);
        QCOMPARE(status.language, use_first ? QString{"es"} : QString{"de"});

        argv.emplace_back("-listen=0");
        const auto preview{QmlOnboardingSettings::Preview(argv, false, path)};
        QVERIFY2(preview.ok, qPrintable(preview.error));
        QCOMPARE(preview.values.prune, use_first);
        QVERIFY(preview.profile.has_settings_file);
        QVERIFY(!preview.values.listen);
        QCOMPARE(preview.core_setting_statuses.value("listen").toMap().value("source").toString(), QString{"command_line"});
        QCOMPARE(preview.core_setting_statuses.value("prune").toMap().value("source").toString(), QString{"settings_json"});

        QCOMPARE(&Params(), chain_params);
        QCOMPARE(&BaseParams(), base_params);
        QCOMPARE(Params().GetChainType(), ChainType::REGTEST);
        QCOMPARE(BaseParams().DataDir(), std::string{"regtest"});
        QCOMPARE(gArgs.GetIntArg("-rpcport", 0), 19001);
        QCOMPARE(GlobalSettings(), global_settings);
    }
    QCOMPARE(ProfileFiles(first.path()), first_files);
    QCOMPARE(ProfileFiles(second.path()), second_files);
}

void OnboardingSettingsTests::customSignetSettingsPaths_data()
{
    QTest::addColumn<QString>("settings_option");
    QTest::newRow("default") << QString{};
    QTest::newRow("relative") << QString{"-settings=alternate.json"};
    QTest::newRow("absolute") << QString{"absolute"};
    QTest::newRow("disabled") << QString{"-nosettings"};
}

void OnboardingSettingsTests::customSignetSettingsPaths()
{
    QFETCH(QString, settings_option);
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    QVERIFY(WriteFile(profile.filePath("bitcoin.conf"), "signet=1\n[signet]\nsignetchallenge=51\n"));
    auto argv{ProfileArgs(profile.path())};
    const bool disabled{settings_option == "-nosettings"};
    QString settings_path{profile.filePath(SignetDirectory("51") + "/settings.json")};
    if (settings_option == "absolute") {
        settings_path = profile.filePath("absolute.json");
        settings_option = "-settings=" + settings_path;
    } else if (settings_option.startsWith("-settings=")) {
        settings_path = profile.filePath(SignetDirectory("51") + "/alternate.json");
    }
    if (!settings_option.isEmpty()) argv.push_back(settings_option.toStdString());
    QVERIFY(WriteFile(settings_path, "{\"qml_onboarded\":true,\"prune\":2000}"));
    const auto before{ProfileFiles(profile.path())};

    const auto status{QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, false)};
    QVERIFY2(status.ok, qPrintable(status.error));
    QCOMPARE(status.settings_enabled, !disabled);
    QVERIFY(!status.should_show_onboarding);
    const auto preview{QmlOnboardingSettings::Preview(argv, false, profile.path())};
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QCOMPARE(preview.profile.has_settings_file, !disabled);
    QCOMPARE(preview.values.prune, !disabled);
    QCOMPARE(ProfileFiles(profile.path()), before);
}

void OnboardingSettingsTests::customSignetChainDataAndNewDirectoryPreview()
{
    QTemporaryDir profile;
    QTemporaryDir blocks;
    QVERIFY(profile.isValid());
    QVERIFY(blocks.isValid());
    const QString network{SignetDirectory("51")};
    auto argv{ProfileArgs(profile.path())};
    argv.insert(argv.end(), {"-signet", "-signetchallenge=51", "-blocksdir=" + blocks.path().toStdString()});
    QVERIFY(WriteFile(profile.filePath("signet/chainstate/CURRENT"), "public signet"));
    QVERIFY(WriteFile(blocks.filePath("signet/blocks/blk00000.dat"), "public signet"));
    auto preview{QmlOnboardingSettings::Preview(argv, false, profile.path())};
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.profile.has_chain_data);

    QVERIFY(WriteFile(profile.filePath(network + "/chainstate/CURRENT"), "custom signet"));
    preview = QmlOnboardingSettings::Preview(argv, false, profile.path());
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.has_chain_data);
    QVERIFY(QFile::remove(profile.filePath(network + "/chainstate/CURRENT")));
    QVERIFY(WriteFile(blocks.filePath(network + "/blocks/blk00000.dat"), "custom signet"));
    const auto profile_before{ProfileFiles(profile.path())};
    const auto blocks_before{ProfileFiles(blocks.path())};
    preview = QmlOnboardingSettings::Preview(argv, false, profile.path());
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.has_chain_data);
    QVERIFY(preview.profile.existing_profile);
    QCOMPARE(ProfileFiles(profile.path()), profile_before);
    QCOMPARE(ProfileFiles(blocks.path()), blocks_before);

    const QString new_directory{profile.filePath("new-profile")};
    preview = QmlOnboardingSettings::Preview({"bitcoin-qt", "-signet", "-signetchallenge=52"}, false, new_directory);
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.profile.existing_profile);
    QVERIFY(!QFileInfo::exists(new_directory));
    QCOMPARE(ProfileFiles(profile.path()), profile_before);
}

void OnboardingSettingsTests::customSignetPreviewMatchesCoreStartup()
{
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    QVERIFY(WriteFile(profile.filePath("bitcoin.conf"), "signet=1\n[signet]\nsignetchallenge=51\n"));
    const QString settings_path{profile.filePath(SignetDirectory("51") + "/settings.json")};
    QVERIFY(WriteFile(settings_path, "{\"qml_onboarded\":true,\"prune\":2000}"));
    const auto preview{QmlOnboardingSettings::Preview(ProfileArgs(profile.path()), false, profile.path())};
    QVERIFY2(preview.ok, qPrintable(preview.error));

    gArgs.ForceSetArg("-datadir", profile.path().toStdString());
    gArgs.ClearPathCache();
    const auto error{common::InitConfig(gArgs)};
    QVERIFY2(!error, error ? error->message.original.c_str() : "");
    fs::path startup_settings_path;
    QVERIFY(gArgs.GetSettingsPath(&startup_settings_path));
    QCOMPARE(QDir::fromNativeSeparators(QString::fromStdString(fs::PathToString(startup_settings_path))), settings_path);
    const auto started_values{QmlCoreSettings::LoadEffectiveValues(gArgs)};
    QCOMPARE(preview.values.prune, started_values.prune);
    QCOMPARE(preview.values.prune_size_gb, started_values.prune_size_gb);
    QVERIFY(gArgs.GetPersistentSetting("qml_onboarded").get_bool());
}

void OnboardingSettingsTests::customSignetOnboardingCompletes_data()
{
    QTest::addColumn<QByteArray>("challenge");
    QTest::newRow("51") << QByteArray{"51"};
    QTest::newRow("52") << QByteArray{"52"};
}

void OnboardingSettingsTests::customSignetOnboardingCompletes()
{
    QFETCH(QByteArray, challenge);
    startupsettingstest::SavedGuiDataDirSettings saved_settings;
    startupsettingstest::SavedNamedSettings qml_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-signet")};
    startupsettingstest::SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-signet")};
    QTemporaryDir profile;
    QVERIFY(profile.isValid());
    QVERIFY(WriteFile(profile.filePath("bitcoin.conf"), "signet=1\n[signet]\nsignetchallenge=" + challenge + '\n'));
    const QString settings_path{profile.filePath(SignetDirectory(challenge.constData()) + "/settings.json")};
    QVERIFY(WriteFile(settings_path, "{\"qml_onboarded\":false,\"prune\":0}"));
    QVERIFY(WriteFile(profile.filePath("signet/settings.json"), "{\"qml_onboarded\":false,\"prune\":1000}"));
    const auto public_signet_before{ProfileFiles(profile.filePath("signet"))};

    const auto argv{ProfileArgs(profile.path())};
    const auto preview{QmlOnboardingSettings::Preview(argv, false, profile.path())};
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QCOMPARE(preview.resolved_chain, QString{"signet"});
    QCOMPARE(QDir::fromNativeSeparators(preview.resolved_settings_path), settings_path);
    QVERIFY(!preview.values.prune);
    auto values{preview.values};
    values.prune = true;
    values.prune_size_gb = 3;

    gArgs.ForceSetArg("-datadir", profile.path().toStdString());
    gArgs.ClearPathCache();
    const auto bootstrap{QmlOnboardingSettings::CurrentGuiSettingsStore()};
    QmlOnboardingSettings::PendingApply pending;
    QString error;
    QVERIFY2(QmlOnboardingSettings::PrepareApplyToArgs(
                 gArgs, {profile.path(), QmlOnboardingSettings::DataDirSource::ExplicitArg},
                 preview.resolved_data_dir, {"prune"}, values, preview.effective_reset, pending, &error),
             qPrintable(error));
    pending.resolved_chain = preview.resolved_chain;
    pending.resolved_settings_path = preview.resolved_settings_path;
    pending.target_complete = true;

    const auto init_error{common::InitConfig(gArgs)};
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    QVERIFY2(QmlOnboardingSettings::FinalizeStartupSettings(gArgs, bootstrap, &pending, nullptr, &error), qPrintable(error));

    std::map<std::string, common::SettingsValue> persisted;
    std::vector<std::string> read_errors;
    QVERIFY(common::ReadSettings(fs::PathFromString(settings_path.toStdString()), persisted, read_errors));
    QVERIFY(persisted.at("qml_onboarded").get_bool());
    QCOMPARE(persisted.at("prune").get_str(), std::string{"2861"});
    const auto status{QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, false)};
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.qml_onboarded);
    QVERIFY(!status.should_show_onboarding);
    QCOMPARE(ProfileFiles(profile.filePath("signet")), public_signet_before);
}

BITCOINQML_REGISTER_QT_TEST(OnboardingSettingsTests)
#include <test_onboarding_settings.moc>
