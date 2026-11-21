// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/onboarding_settings.h>
#include <qml/datadir.h>
#include <qml/models/settings_keys.h>
#include <test/qt_test_registry.h>

#include <chainparams.h>
#include <chainparamsbase.h>
#include <common/args.h>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <future>
#include <string>
#include <utility>
#include <vector>

namespace {
using namespace QmlOnboardingSettings;
bool Write(const QString& path, const QByteArray& bytes)
{
    QFile file{path};
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QByteArray TreeBytes(const QString& root)
{
    QStringList names;
    QDirIterator entries{root, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories};
    while (entries.hasNext()) names.push_back(entries.next());
    names.sort();
    QByteArray result;
    for (const auto& name : names) {
        result += QDir{root}.relativeFilePath(name).toUtf8(); result += '\0';
        if (QFileInfo{name}.isDir()) { result += "directory"; result += '\0'; continue; }
        QFile file{name};
        if (!file.open(QIODevice::ReadOnly)) { result += "unreadable"; result += '\0'; continue; }
        const auto bytes = file.readAll();
        result += QByteArray::number(bytes.size()); result += ':'; result += bytes; result += '\0';
    }
    return result;
}
struct ProfileFixture {
    QTemporaryDir root;
    QString data{root.filePath(QStringLiteral("profile"))};
    QString config{root.filePath(QStringLiteral("bitcoin.conf"))};
    UiPreferencesNamespace identity;
    ProfileFixture()
    {
        identity.format = QSettings::IniFormat;
        identity.file = root.filePath(QStringLiteral("preferences.ini"));
    }
    bool initialize(const QByteArray& configuration = {}) const
    {
        return root.isValid() && QDir{}.mkpath(data) && Write(config, configuration)
            && preferences(QmlDataDir::DefaultDataDirString());
    }
    bool preferences(const QString& directory) const
    {
        QSettings settings{identity.file, identity.format}; settings.setFallbacksEnabled(false);
        settings.setValue(SettingsKeys::DATA_DIR, directory);
        settings.setValue(QStringLiteral("untouched"), QStringLiteral("original preference value"));
        settings.sync();
        return settings.status() == QSettings::NoError;
    }
    std::vector<std::string> arguments(bool explicit_directory = true) const
    {
        std::vector<std::string> result{"startup-role-profile", "-conf=" + config.toStdString()};
        if (explicit_directory) result.push_back("-datadir=" + data.toStdString());
        return result;
    }
    bool marked() const
    {
        return QDir{}.mkpath(data + QStringLiteral("/regtest"))
            && Write(data + QStringLiteral("/regtest/settings.json"), "{\"qml_onboarded\":true,\"listen\":false}\n");
    }
    OnboardingStartupStatus resolve(std::vector<std::string> arguments, StartupReadMode mode = StartupReadMode::Onboarding) const
    {
        // The production resolver is Native-only; the fixture supplies owned
        // argv and preference identity without involving a live global node.
        return std::async(std::launch::async, [arguments = std::move(arguments), identity = identity, mode] {
            return ResolveOnboardingStartupStatus(arguments, false, &identity, mode);
        }).get();
    }
};
} // namespace

class StartupRoleProfileTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void resetKeepsTheMarkedProfileEligibleWithoutSkippingOnboarding()
    {
        ProfileFixture fixture; QVERIFY(fixture.initialize("regtest=1\n")); QVERIFY(fixture.marked());
        const auto before = TreeBytes(fixture.root.path());
        for (bool reset : {false, true}) {
            auto arguments = fixture.arguments();
            if (reset) arguments.emplace_back("-resetguisettings");
            const auto full = fixture.resolve(arguments);
            QVERIFY2(full.ok, qPrintable(full.error)); QVERIFY(full.probe_eligible);
            QCOMPARE(full.network, QStringLiteral("regtest"));
            QCOMPARE(full.network_data_dir, QFileInfo{fixture.data}.canonicalFilePath() + QStringLiteral("/regtest"));
            QCOMPARE(full.should_show_onboarding, reset); QCOMPARE(full.qml_onboarded, !reset);
            const auto profile = fixture.resolve(arguments, StartupReadMode::ProfileOnly);
            QVERIFY2(profile.ok, qPrintable(profile.error)); QVERIFY(profile.probe_eligible);
            QCOMPARE(profile.network_data_dir, full.network_data_dir);
            QVERIFY(!profile.qml_onboarded); QVERIFY(profile.should_show_onboarding);
        }
        QCOMPARE(TreeBytes(fixture.root.path()), before);
    }
    void profileOnlyIgnoresMalformedDynamicSettings_data()
    {
        QTest::addColumn<bool>("explicit_settings");
        QTest::newRow("network-settings") << false;
        QTest::newRow("explicit-settings-file") << true;
    }
    void profileOnlyIgnoresMalformedDynamicSettings()
    {
        QFETCH(bool, explicit_settings); ProfileFixture fixture; QVERIFY(fixture.initialize("regtest=1\n"));
        QVERIFY(QDir{}.mkpath(fixture.data + QStringLiteral("/regtest")));
        const auto settings = explicit_settings ? fixture.root.filePath(QStringLiteral("chosen-settings.json"))
            : fixture.data + QStringLiteral("/regtest/settings.json");
        QVERIFY(Write(settings, "{malformed: original dynamic settings}\n"));
        auto arguments = fixture.arguments();
        if (explicit_settings) arguments.push_back("-settings=" + settings.toStdString());
        const auto before = TreeBytes(fixture.root.path());
        const auto profile = fixture.resolve(arguments, StartupReadMode::ProfileOnly);
        QVERIFY2(profile.ok, qPrintable(profile.error)); QVERIFY(profile.probe_eligible);
        QCOMPARE(profile.network, QStringLiteral("regtest"));
        const auto full = fixture.resolve(arguments);
        QVERIFY(!full.ok); QVERIFY(!full.error.isEmpty());
        QCOMPARE(TreeBytes(fixture.root.path()), before);
    }
    void configuredProfileUsesItsOwnNetworkWithoutChangingGlobalParams_data()
    {
        QTest::addColumn<QString>("network"); QTest::addColumn<QString>("suffix"); QTest::addColumn<QString>("challenge");
        QTest::newRow("main") << QStringLiteral("main") << QString{} << QString{};
        QTest::newRow("testnet") << QStringLiteral("test") << QStringLiteral("testnet3") << QString{};
        QTest::newRow("testnet4") << QStringLiteral("testnet4") << QStringLiteral("testnet4") << QString{};
        QTest::newRow("regtest") << QStringLiteral("regtest") << QStringLiteral("regtest") << QString{};
        QTest::newRow("signet") << QStringLiteral("signet") << QStringLiteral("signet") << QString{};
        QTest::newRow("custom-signet") << QStringLiteral("signet") << QString{} << QStringLiteral("51");
    }
    void configuredProfileUsesItsOwnNetworkWithoutChangingGlobalParams()
    {
        QFETCH(QString, network); QFETCH(QString, suffix); QFETCH(QString, challenge);
        ProfileFixture fixture; QVERIFY(fixture.initialize());
        QByteArray configuration = "datadir=" + fixture.data.toUtf8() + "\nchain=" + network.toUtf8() + '\n';
        if (!challenge.isEmpty()) configuration += "signetchallenge=" + challenge.toUtf8() + '\n';
        QVERIFY(Write(fixture.config, configuration));
        if (!challenge.isEmpty()) {
            ArgsManager expected; std::string error;
            QVERIFY2(PrepareArgs(expected, {"profile-suffix", "-chain=signet", "-signetchallenge=" + challenge.toStdString()}, false, error), error.c_str());
            suffix = QString::fromStdString(CreateBaseChainParams(expected, ChainType::SIGNET)->DataDir());
            QVERIFY(suffix.startsWith(QStringLiteral("signet_")));
        }
        const auto original_chain = gArgs.GetChainType();
        const auto restore = qScopeGuard([original_chain] { SelectParams(original_chain); });
        SelectParams(network == QStringLiteral("main") ? ChainType::REGTEST : ChainType::MAIN);
        const auto* global_params = &Params(); const auto* global_base = &BaseParams();
        const auto before = TreeBytes(fixture.root.path());
        const auto status = fixture.resolve(fixture.arguments(false), StartupReadMode::ProfileOnly);
        QVERIFY2(status.ok, qPrintable(status.error)); QVERIFY(status.probe_eligible);
        QCOMPARE(status.data_dir_source, DataDirSource::Config); QCOMPARE(status.network, network);
        QCOMPARE(status.active_data_dir, fixture.data);
        const auto expected = QDir{QFileInfo{fixture.data}.canonicalFilePath()}.filePath(suffix);
        QCOMPARE(status.network_data_dir, QDir::cleanPath(expected));
        QCOMPARE(&Params(), global_params); QCOMPARE(&BaseParams(), global_base);
        QCOMPARE(TreeBytes(fixture.root.path()), before);
    }
    void explicitChooserKeepsPrecedenceExceptForFixedCliProfile_data()
    {
        QTest::addColumn<int>("source"); QTest::addColumn<bool>("reset");
        QTest::newRow("saved-profile-with-chooser") << 0 << false;
        QTest::newRow("config-profile-with-chooser") << 1 << false;
        QTest::newRow("fixed-cli-profile-with-chooser") << 2 << false;
        QTest::newRow("fixed-cli-profile-with-reset-and-chooser") << 2 << true;
    }
    void explicitChooserKeepsPrecedenceExceptForFixedCliProfile()
    {
        QFETCH(int, source); QFETCH(bool, reset); ProfileFixture fixture;
        QVERIFY(fixture.initialize("regtest=1\n")); QVERIFY(fixture.marked());
        if (source == 0) QVERIFY(fixture.preferences(fixture.data));
        if (source == 1) QVERIFY(Write(fixture.config, "regtest=1\ndatadir=" + fixture.data.toUtf8() + '\n'));
        auto arguments = fixture.arguments(source == 2); arguments.emplace_back("-choosedatadir");
        if (reset) arguments.emplace_back("-resetguisettings");
        const auto before = TreeBytes(fixture.root.path());
        const auto profile = fixture.resolve(arguments, StartupReadMode::ProfileOnly);
        QVERIFY2(profile.ok, qPrintable(profile.error)); QCOMPARE(profile.probe_eligible, source == 2);
        QCOMPARE(profile.network, QStringLiteral("regtest"));
        const auto full = fixture.resolve(arguments);
        QVERIFY2(full.ok, qPrintable(full.error)); QCOMPARE(full.probe_eligible, source == 2);
        QCOMPARE(full.should_show_onboarding, source != 2 || reset);
        QCOMPARE(TreeBytes(fixture.root.path()), before);
    }
    void missingCustomProfileIsAnErrorInEitherReadMode_data()
    {
        QTest::addColumn<bool>("explicit_directory"); QTest::addColumn<bool>("profile_only");
        QTest::newRow("missing-cli-profile-only") << true << true;
        QTest::newRow("missing-cli-onboarding") << true << false;
        QTest::newRow("unavailable-saved-profile-only") << false << true;
        QTest::newRow("unavailable-saved-onboarding") << false << false;
    }
    void missingCustomProfileIsAnErrorInEitherReadMode()
    {
        QFETCH(bool, explicit_directory); QFETCH(bool, profile_only); ProfileFixture fixture;
        QVERIFY(fixture.initialize("regtest=1\n"));
        const auto missing = fixture.root.filePath(QStringLiteral("unavailable-profile"));
        QVERIFY(fixture.preferences(missing));
        auto arguments = fixture.arguments(false);
        if (explicit_directory) arguments.push_back("-datadir=" + missing.toStdString());
        const auto before = TreeBytes(fixture.root.path());
        const auto status = fixture.resolve(arguments, profile_only ? StartupReadMode::ProfileOnly : StartupReadMode::Onboarding);
        QVERIFY(!status.ok); QVERIFY(!status.error.isEmpty()); QVERIFY(!status.probe_eligible);
        QVERIFY(status.network_data_dir.isEmpty()); QVERIFY(!QFileInfo::exists(missing));
        QCOMPARE(TreeBytes(fixture.root.path()), before);
    }
    void resolutionNeverCreatesMissingNetworkOrSettingsDirectories_data()
    {
        QTest::addColumn<bool>("profile_only");
        QTest::newRow("profile-only") << true; QTest::newRow("onboarding") << false;
    }
    void resolutionNeverCreatesMissingNetworkOrSettingsDirectories()
    {
        QFETCH(bool, profile_only); ProfileFixture fixture; QVERIFY(fixture.initialize("regtest=1\n"));
        const auto before = TreeBytes(fixture.root.path());
        const auto status = fixture.resolve(fixture.arguments(), profile_only ? StartupReadMode::ProfileOnly : StartupReadMode::Onboarding);
        QVERIFY2(status.ok, qPrintable(status.error)); QVERIFY(status.probe_eligible);
        QCOMPARE(status.network_data_dir, QFileInfo{fixture.data}.canonicalFilePath() + QStringLiteral("/regtest"));
        QVERIFY(!QFileInfo::exists(status.network_data_dir));
        QCOMPARE(TreeBytes(fixture.root.path()), before);
    }
    void disabledDynamicSettingsKeepExistingFullStartupPolicy()
    {
        ProfileFixture fixture; QVERIFY(fixture.initialize("regtest=1\n")); QVERIFY(fixture.marked());
        QVERIFY(Write(fixture.data + QStringLiteral("/regtest/settings.json"), "not-json\n"));
        auto arguments = fixture.arguments(); arguments.emplace_back("-nosettings");
        const auto before = TreeBytes(fixture.root.path());
        const auto full = fixture.resolve(arguments);
        QVERIFY2(full.ok, qPrintable(full.error)); QVERIFY(full.probe_eligible);
        QVERIFY(!full.settings_enabled); QVERIFY(full.qml_onboarded); QVERIFY(!full.should_show_onboarding);
        const auto profile = fixture.resolve(arguments, StartupReadMode::ProfileOnly);
        QVERIFY2(profile.ok, qPrintable(profile.error)); QCOMPARE(profile.network_data_dir, full.network_data_dir);
        QCOMPARE(TreeBytes(fixture.root.path()), before);
    }
    void canonicalNetworkPathResolvesAnExistingSymlinkPrefix()
    {
#ifdef Q_OS_UNIX
        ProfileFixture fixture; QVERIFY(fixture.initialize("regtest=1\n"));
        const auto alias = fixture.root.filePath(QStringLiteral("profile-alias"));
        QVERIFY(QFile::link(fixture.data, alias));
        auto arguments = fixture.arguments(false); arguments.push_back("-datadir=" + alias.toStdString());
        const auto before = TreeBytes(fixture.root.path());
        const auto status = fixture.resolve(arguments, StartupReadMode::ProfileOnly);
        QVERIFY2(status.ok, qPrintable(status.error)); QVERIFY(status.probe_eligible);
        QCOMPARE(status.network_data_dir, QFileInfo{fixture.data}.canonicalFilePath() + QStringLiteral("/regtest"));
        QVERIFY(!QFileInfo::exists(status.network_data_dir)); QCOMPARE(TreeBytes(fixture.root.path()), before);
#else
        QSKIP("The fixture requires a Unix directory symlink.");
#endif
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(StartupRoleProfileTests)
#else
QTEST_MAIN(StartupRoleProfileTests)
#endif
#include <test_startuproleprofile.moc>
