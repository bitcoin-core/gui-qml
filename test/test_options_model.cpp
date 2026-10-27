// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <qml/backendexecutor.h>
#include <test/mocks/mocknode.h>
#include <chainparams.h>
#include <qml/core_settings.h>
#include <qml/datadir.h>
#include <qml/guiargs.h>
#include <qml/legacy_settings_migration.h>
#include <qml/models/core_settings_model.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/models/options_model.h>
#include <qml/models/settings_keys.h>
#include <qml/onboarding_settings.h>
#include <init.h>
#include <net_processing.h>
#include <common/args.h>
#include <common/settings.h>
#include <util/translation.h>
#include <util/string.h>

#include <atomic>
#include <stdexcept>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

class OptionsModelTests : public QObject
{
    Q_OBJECT

public:
    enum class LegacyDisplayUnit {
        BTC = 0,
        mBTC = 1,
        uBTC = 2,
        SAT = 3,
    };
    Q_ENUM(LegacyDisplayUnit)

private:
    QString m_original_organization;
    QString m_original_application;
private Q_SLOTS:
    void initTestCase()
    {
        SelectParams(ChainType::REGTEST);
        m_original_organization = QCoreApplication::organizationName();
        m_original_application = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(QStringLiteral("BitcoinQmlTests"));
        QCoreApplication::setApplicationName(QStringLiteral("OptionsModel"));
    }
    void cleanupTestCase()
    {
        QCoreApplication::setOrganizationName(m_original_organization);
        QCoreApplication::setApplicationName(m_original_application);
    }
    void proxyDisabledRemovesKey();
    void torDisabledRemovesKey();
    void proxyEnabledWritesAddress();
    void proxyDirtySetAtRuntime();
    void proxyDirtyResetWhenReverted();
    void mempoolSizeLoadedFromSettings();
    void mempoolSizeWritesSetting();
    void mempoolSizeDoesNotRewriteUnchangedSetting();
    void legacyNumericSettingsWriteStrings();
    void externalSignerPathWritesSigner();
    void externalSignerPathClearedRemovesKey();
    void walletSettingsDirtyTracksExternalSignerPath();
    void signerPathLoadedFromSettings();
    void signerPathWritesSetting();
    void signerDirtySetAtRuntime();
    void signerDirtyResetWhenReverted();
    void externalSignerPathValidationRejectsMissingPath();
    void externalSignerPathValidationAcceptsExecutablePath();
    void connectionDirtyTracksRestartSettings();
    void natpmpAppliesLiveWithoutRestartDirty();
    void storageDirtyIgnoresDisabledPruneSize();
    void pruneDisabledPreservesPreviousValue();
    void developerDirtyTracksRestartSettings();
    void mempoolDirtyTracksRestartSettings();
    void proxyValidationAndCommit();
    void proxyDisabledPreservesPreviousValue();
    void customDataDirValidationRejectsFile();
    void customDataDirSelectionCreatesDirectoryAndPersists();
    void customDataDirSelectionPreservesExistingWalletDiscovery();
    void guiDataDirSettingSoftSetsCustomPath();
    void guiDataDirSettingAbsolutizesSavedRelativePath();
    void guiDataDirSettingPreservesExistingWalletDiscovery();
    void guiDataDirSettingSkipsUnusableConfiguredDir();
    void guiDataDirSettingSkipsExplicitDatadir();
    void runtimeDataDirUsesExplicitDatadirOverSavedGuiSetting();
    void guiDataDirSettingLeavesDefaultOverridable();
    void legacyQtDataDirFallbackReadsOldQtSetting();
    void guiDataDirChooserShowsForMissingConfiguredDir();
    void guiDataDirChooserShowsForUnwritableConfiguredDir();
    void resetGuiSettingsClearsQSettings();
    void resetGuiSettingsClearsLegacyQtSettings();
    void resetGuiSettingsStartsOnboardingFromDefaultDataDir();
    void resetGuiSettingsClearsSettingsJson();
    void resetGuiSettingsPreviewIgnoresSelectedCustomDataDirSettingsJson();
    void resetGuiSettingsApplyClearsSelectedCustomDataDirSettingsJson();
    void resetGuiSettingsPreservesCommandLineOverrides();
    void resetGuiSettingsPreservesBitcoinConfOverrides();
    void resetGuiSettingsExplicitDatadirClearsThatDatadirSettingsJson();
    void qmlOnboardedProfileSkipsPreInitOnboarding();
    void qmlOnboardedCommandLineOverrideShowsPreInitOnboarding();
    void configuredDatadirOnboardingStatus_data();
    void configuredDatadirOnboardingStatus();
    void configuredDatadirPreviewKeepsConfigSource();
    void configuredDatadirApplyDoesNotPersistGuiDataDir();
    void explicitDatadirApplyDoesNotPersistGuiDataDir();
    void asynchronousOnboardingPreparation_data();
    void asynchronousOnboardingPreparation();
    void qmlOnboardedResetGuiSettingsShowsPreInitOnboarding();
    void qmlOnboardedChooseDataDirShowsPreInitOnboarding();
    void qmlOnboardedCurrentResetFlagShowsPreInitOnboarding();
    void qmlOnboardedLegacyResetFlagShowsPreInitOnboarding();
    void existingCoreProfileShowsFullOnboardingWithCurrentSettings();
    void freshExplicitDatadirPreviewReportsFreshProfile();
    void onboardingPreviewDetectsSettingsJsonProfile();
    void onboardingPreviewDetectsChainDataWithoutCreatingBlocksDir();
    void onboardingPreviewDetectsRootWalletProfile();
    void onboardingPreviewDetectsExplicitWalletDirProfile();
    void onboardingPreviewIgnoresEmptyExplicitWalletDir();
    void onboardingPreviewIgnoresUnrecognizedWalletsEntry();
    void freshExplicitDatadirShowsFullOnboarding();
    void onboardingApplyWithoutTouchedSettingsOnlyAddsQmlOnboardedMarker();
    void onboardingApplyCreatesWalletSubdirectoryForNewNetworkDataDir();
    void onboardingApplyPreservesExistingNetworkWalletDiscovery();
    void fullOnboardingApplyWritesQmlOnboardedMarker();
    void onboardingPreviewAppliesParameterInteractions();
    void storageSpaceCheckAcceptsExistingDirectory();
    void storageSpaceCheckRejectsExistingFile();
    void thirdPartyTransactionLinksParseValidUrls();
    void moneyFontChoicePersists();
    void displayUnitUsesQtCompatibleSettingsKey();
    void displayUnitUsesLegacyQtFallback();
    void displayUnitPrefersQmlSettingOverLegacyQtFallback();
    void sharedCoreSettingHelpersDeduplicateOverrides();
    void parameterInteractionOverridesPersistExplicitValues();
    void coreSettingsLegacyNumericOverridesWriteStrings();
    void coreSettingsModelEntryMutatesRuntimeModel();
    void coreSettingsSessionCommandLineSettingDoesNotWrite();
    void coreSettingsSessionDoesNotCopyUntouchedConfig();
    void coreSettingsSessionWritesTouchedConfigOverride();
    void coreSettingsSessionRevertingToDefaultDeletesOverride();
    void coreSettingsSessionPruneDisabledPreservesPreviousValue();
    void coreSettingsSessionPruneEnabledClearsPreviousValue();
    void coreSettingsSessionProxyDisabledPreservesPreviousValue();
    void coreSettingsLoadPersistentPrunePreviousValue();
    void coreSettingsSessionPreviewRefreshPreservesTouchedValues();
    void coreSettingsSessionChangeReportsFieldDiffs();
    void coreSettingsSessionProxyCommitAcceptsUnchangedAddressWithoutValueChange();
    void coreSettingStatusTracksSourcePrecedence();
    void runtimeCoreSettingStatusesRefreshAfterWrite();
    void commandLineOverriddenSettingDoesNotWrite();
    void runtimeCommandLineOverridesDisplayEffectiveValues();
    void runtimeParameterInteractionsDisplayEffectiveValues();
    void commandLineOverriddenSettingsPreservePersistentValues();
    void revertingToConfigValueDeletesRwOverride();
    void revertingToDefaultValueDeletesRwOverride();
    void languageCommandLineOverrideDoesNotPersist();
    void legacyQtSettingsMigrateToCoreSettings();
    void legacyQtSettingsPreviewDoesNotRemoveQSettings();
    void legacyQtSettingsCommandLineOverrideStillMigratesPersistentValue();
    void legacyQtSettingsBitcoinConfBlocksMigration();
    void onboardingApplyMigratesLegacySettingsBeforeTouchedOverrides();
    void onboardingPreviewHelperReadsSelectedDatadirConfig();
    void onboardingPreviewReadsSelectedDatadirConfig();
    void onboardingApplyDoesNotCopyUntouchedConfig();
    void onboardingApplyWritesTouchedConfigOverride();
    void onboardingApplyWritesTouchedParameterInteractionOverride();
};

// Convenience helpers for persistent setting values used by the node double.
static common::SettingsValue MakeAddress(const std::string& addr)
{
    return common::SettingsValue{addr};
}

static common::SettingsValue MakeInt(int value)
{
    return common::SettingsValue{value};
}

static void InstallPersistentSettings(MockNode& node, ArgsManager& args)
{
    node.get_persistent_setting_fn = [&args](const std::string& name) {
        return args.GetPersistentSetting(name);
    };
}

static void InstallRwSettingsWriter(MockNode& node, ArgsManager& args)
{
    node.update_rw_setting_fn = [&args](const std::string& name, const common::SettingsValue& value) {
        args.LockSettings([&](common::Settings& settings) {
            if (value.isNull()) {
                settings.rw_settings.erase(name);
            } else {
                settings.rw_settings[name] = value;
            }
        });
    };
}

static std::vector<std::string> TestArgv()
{
    return {std::string{"bitcoinqml"}, std::string{"-regtest"}};
}

static bool PrepareTestArgs(ArgsManager& args, const std::vector<std::string>& argv, std::string& error)
{
    SetupServerArgs(args, /*can_listen_ipc=*/false);
    SetupQmlGuiArgs(args);
    std::vector<const char*> raw_argv;
    raw_argv.reserve(argv.size());
    for (const std::string& arg : argv) raw_argv.push_back(arg.c_str());
    return args.ParseParameters(static_cast<int>(raw_argv.size()), raw_argv.data(), error);
}

class SavedGuiDataDirSettings
{
public:
    SavedGuiDataDirSettings()
    {
        QSettings settings;
        for (const QString& key : settings.allKeys()) {
            m_values.insert(key, settings.value(key));
        }
    }

    ~SavedGuiDataDirSettings()
    {
        QSettings settings;
        settings.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
            settings.setValue(it.key(), it.value());
        }
    }

private:
    QVariantMap m_values;
};

class CurrentDirectoryRestorer
{
public:
    CurrentDirectoryRestorer() : m_original{QDir::currentPath()} {}
    ~CurrentDirectoryRestorer() { QDir::setCurrent(m_original); }

private:
    const QString m_original;
};

class SavedSettingsFormat
{
public:
    explicit SavedSettingsFormat(QSettings::Format format)
        : m_format{QSettings::defaultFormat()}
    {
        QSettings::setDefaultFormat(format);
    }

    ~SavedSettingsFormat()
    {
        QSettings::setDefaultFormat(m_format);
    }

private:
    QSettings::Format m_format;
};

class SavedNamedSettings
{
public:
    SavedNamedSettings(const QString& org, const QString& app)
        : m_settings{QSettings::defaultFormat(), QSettings::UserScope, org, app}
    {
        for (const QString& key : m_settings.allKeys()) {
            m_values.insert(key, m_settings.value(key));
        }
        m_settings.clear();
    }

    ~SavedNamedSettings()
    {
        m_settings.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
            m_settings.setValue(it.key(), it.value());
        }
        m_settings.sync();
    }

    QSettings& settings() { return m_settings; }

private:
    QSettings m_settings;
    QVariantMap m_values;
};

class SavedRawNamedSettings
{
public:
    SavedRawNamedSettings(const QString& org, const QString& app)
        : m_format{QSettings::defaultFormat()}
        , m_org{org}
        , m_app{app}
    {
        QSettings settings{m_format, QSettings::UserScope, m_org, m_app};
        m_file_name = settings.fileName();
        for (const QString& key : settings.allKeys()) {
            m_values.insert(key, settings.value(key));
        }
        settings.clear();
        settings.sync();
    }

    ~SavedRawNamedSettings()
    {
        QSettings settings{m_format, QSettings::UserScope, m_org, m_app};
        settings.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
            settings.setValue(it.key(), it.value());
        }
        settings.sync();
    }

    QString fileName() const { return m_file_name; }

private:
    QSettings::Format m_format;
    QString m_org;
    QString m_app;
    QString m_file_name;
    QVariantMap m_values;
};

static std::vector<std::string> TestArgvWithDataDir(const QString& data_dir)
{
    return {
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-datadir=" + data_dir.toStdString(),
    };
}

static void WriteSqliteWalletMarker(const QString& wallet_dir)
{
    QVERIFY(QDir().mkpath(wallet_dir));
    QFile wallet_file(QDir(wallet_dir).filePath(QStringLiteral("wallet.dat")));
    QVERIFY(wallet_file.open(QIODevice::WriteOnly));

    QByteArray wallet_data(512, '\0');
    wallet_data.replace(0, 16, QByteArray("SQLite format 3\0", 16));
    const auto& message_start{Params().MessageStart()};
    for (qsizetype i = 0; i < static_cast<qsizetype>(message_start.size()); ++i) {
        wallet_data[68 + i] = static_cast<char>(message_start[i]);
    }

    QCOMPARE(wallet_file.write(wallet_data), wallet_data.size());
    wallet_file.close();
}

static void PrepareArgsForDataDir(ArgsManager& args, const QString& data_dir)
{
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgvWithDataDir(data_dir), parse_error), parse_error.c_str());
    SelectParams(args.GetChainType());
    args.SelectConfigNetwork(args.GetChainTypeString());
}

static common::SettingsValue ReadRwSetting(ArgsManager& args, const std::string& name)
{
    common::SettingsValue value;
    args.LockSettings([&](common::Settings& settings) {
        const auto it = settings.rw_settings.find(name);
        if (it != settings.rw_settings.end()) value = it->second;
    });
    return value;
}

static void PrepareRuntimePersistence(MockNode& node, ArgsManager& args, const QTemporaryDir& directory)
{
    args.ForceSetArg("-settings", directory.filePath(QStringLiteral("runtime-settings.json")).toStdString());
    QStringList names = QmlCoreSettings::CoreSettingNames();
    names << QStringLiteral("prune-prev") << QStringLiteral("proxy-prev") << QStringLiteral("onion-prev");
    for (const auto& name : names) {
        const auto value = node.getPersistentSetting(name.toStdString());
        if (!value.isNull() && args.GetPersistentSetting(name.toStdString()).isNull()) {
            QmlCoreSettings::SetRwSetting(args, name, value);
        }
    }
    InstallPersistentSettings(node, args);
}

void OptionsModelTests::proxyDisabledRemovesKey()
{
    MockNode node;
    // Simulate a previously-saved proxy address so that m_proxy_enabled=true on construction.
    node.SetPersistentSetting("proxy", MakeAddress("127.0.0.1:9050"));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.proxyEnabled());

    // Disabling a proxy removes the override instead of storing an empty string.
    model.setProxyEnabled(false);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.proxyEnabled());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    QVERIFY(!model.settingsPending());
    const auto proxy_write = ReadRwSetting(model_args, "proxy");
    QVERIFY(proxy_write.isNull());
}

void OptionsModelTests::torDisabledRemovesKey()
{
    MockNode node;
    node.SetPersistentSetting("onion", MakeAddress("127.0.0.1:9150"));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.torEnabled());

    model.setTorEnabled(false);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.torEnabled());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    QVERIFY(!model.settingsPending());
    const auto onion_write = ReadRwSetting(model_args, "onion");
    QVERIFY(onion_write.isNull());
}

void OptionsModelTests::proxyEnabledWritesAddress()
{
    MockNode node;

    // Construct with no saved proxy — m_proxy_enabled=false, m_proxy_address="".
    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.proxyEnabled());

    // Pre-load an address into the model (as QML does before toggling the switch).
    model.setProxyAddress("10.0.0.1:9050");
    QTRY_VERIFY(!model.settingsPending());


    // Enabling proxy must write the address string to settings.
    model.setProxyEnabled(true);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.proxyEnabled());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto proxy_write = ReadRwSetting(model_args, "proxy");
    QVERIFY(proxy_write.isStr());
    QCOMPARE(proxy_write.get_str(), std::string{"10.0.0.1:9050"});
    const auto previous_write = ReadRwSetting(model_args, "proxy-prev");
    QVERIFY(previous_write.isNull());
}

void OptionsModelTests::proxyDirtySetAtRuntime()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.proxySettingsDirty());

    model.setProxyEnabled(true);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.proxySettingsDirty());
}

void OptionsModelTests::proxyDirtyResetWhenReverted()
{
    MockNode node;

    // Start onboarded with proxy disabled (no saved proxy).
    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.proxySettingsDirty());

    // Simulate what ProxySettings.qml does: set address before enabling.
    model.setProxyAddress("127.0.0.1:9050");
    QTRY_VERIFY(!model.settingsPending());
    // Address changed but proxy is still disabled — should NOT be dirty since
    // the address is irrelevant when proxy is off.
    QVERIFY(!model.proxySettingsDirty());

    // Enable proxy — now dirty (enabled differs from initial disabled).
    model.setProxyEnabled(true);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.proxySettingsDirty());

    // Revert enable state — dirty should clear even though address is populated,
    // because the address is ignored when proxy is disabled.
    model.setProxyEnabled(false);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.proxySettingsDirty());
}

void OptionsModelTests::mempoolSizeLoadedFromSettings()
{
    MockNode node;
    node.SetPersistentSetting("maxmempool", MakeInt(456));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.maxMempoolSizeMB(), 456);
}

void OptionsModelTests::mempoolSizeWritesSetting()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());

    model.setMaxMempoolSizeMB(456);
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.maxMempoolSizeMB(), 456);
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto mempool_write = ReadRwSetting(model_args, "maxmempool");
    QVERIFY(mempool_write.isNum());
    QCOMPARE(mempool_write.getInt<int64_t>(), int64_t{456});
}

void OptionsModelTests::mempoolSizeDoesNotRewriteUnchangedSetting()
{
    MockNode node;
    node.SetPersistentSetting("maxmempool", MakeInt(456));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());

    model.setMaxMempoolSizeMB(456);
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.maxMempoolSizeMB(), 456);
    QVERIFY(!model.settingsPending());
    QVERIFY(!QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::legacyNumericSettingsWriteStrings()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());

    model.setDbcacheSizeMiB(600);
    QTRY_VERIFY(!model.settingsPending());
    model.setScriptThreads(12);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto dbcache_write = ReadRwSetting(model_args, "dbcache");
    const auto par_write = ReadRwSetting(model_args, "par");
    QVERIFY(dbcache_write.isStr());
    QVERIFY(par_write.isStr());
    QCOMPARE(dbcache_write.get_str(), std::string{"600"});
    QCOMPARE(par_write.get_str(), std::string{"12"});
}

void OptionsModelTests::externalSignerPathWritesSigner()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());

    model.setExternalSignerPath("hwi");
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.externalSignerPath(), QString("hwi"));
    const auto signer_write = ReadRwSetting(model_args, "signer");
    QVERIFY(signer_write.isStr());
    QCOMPARE(signer_write.get_str(), std::string{"hwi"});
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::externalSignerPathClearedRemovesKey()
{
    MockNode node;
    node.SetPersistentSetting("signer", MakeAddress("hwi"));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.externalSignerPath(), QString("hwi"));

    model.setExternalSignerPath("");
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.externalSignerPath().isEmpty());
    const auto signer_write = ReadRwSetting(model_args, "signer");
    QVERIFY(signer_write.isNull());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::walletSettingsDirtyTracksExternalSignerPath()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("hwi");
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.walletSettingsDirty());

    model.setExternalSignerPath("");
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::signerPathLoadedFromSettings()
{
    MockNode node;
    node.SetPersistentSetting("signer", MakeAddress("ledger-signer"));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.externalSignerPath(), QString("ledger-signer"));
}

void OptionsModelTests::signerPathWritesSetting()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.externalSignerPath().isEmpty());

    model.setExternalSignerPath("ledger-signer");
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.externalSignerPath(), QString("ledger-signer"));
    const auto signer_write = ReadRwSetting(model_args, "signer");
    QVERIFY(signer_write.isStr());
    QCOMPARE(signer_write.get_str(), std::string{"ledger-signer"});
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::signerDirtySetAtRuntime()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("ledger-signer");
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.walletSettingsDirty());
}

void OptionsModelTests::signerDirtyResetWhenReverted()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("ledger-signer");
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.walletSettingsDirty());

    model.setExternalSignerPath("");
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::externalSignerPathValidationRejectsMissingPath()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    model.requestExternalSignerPathValidation("/definitely/not/a/real/signer");
    QTRY_VERIFY(!model.signerPathValidationPending());
    QCOMPARE(model.externalSignerPathValidationError("/definitely/not/a/real/signer"),
        QString("The configured signer path does not exist."));
}

void OptionsModelTests::externalSignerPathValidationAcceptsExecutablePath()
{
    MockNode node;

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QString script_path = temp_dir.filePath("fake-signer");
    QFile script(script_path);
    QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
    script.write("#!/bin/sh\nexit 0\n");
    script.close();
    QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    model.requestExternalSignerPathValidation(script_path);
    QTRY_VERIFY(!model.signerPathValidationPending());
    QVERIFY(model.externalSignerPathValidationError(script_path).isEmpty());
}

void OptionsModelTests::connectionDirtyTracksRestartSettings()
{
    MockNode node;
    node.SetPersistentSetting("listen", common::SettingsValue{true});

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.connectionSettingsDirty());
    QVERIFY(!model.restartRequired());

    model.setListen(false);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.connectionSettingsDirty());
    QVERIFY(model.restartRequired());

    model.setListen(true);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.connectionSettingsDirty());
    QVERIFY(!model.restartRequired());
}

void OptionsModelTests::natpmpAppliesLiveWithoutRestartDirty()
{
    MockNode node;
    ArgsManager args;
    QTemporaryDir directory;
    PrepareRuntimePersistence(node, args, directory);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    const bool initial = model.natpmp();
    std::vector<bool> applied;
    std::atomic<bool> saved_before_apply{true};
    node.map_port_fn = [&](bool enabled) {
        if (SettingToBool(args.GetSetting("-natpmp")).value_or(initial) != enabled) saved_before_apply = false;
        applied.push_back(enabled);
    };
    model.setNatpmp(!initial);
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.natpmp(), !initial);
    QVERIFY(!model.connectionSettingsDirty());
    QVERIFY(!model.restartRequired());
    model.setNatpmp(initial);
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.natpmp(), initial);
    QVERIFY(!model.connectionSettingsDirty());
    QVERIFY(!model.restartRequired());
    QCOMPARE(applied.size(), 2U);
    QVERIFY(saved_before_apply.load());
}

void OptionsModelTests::storageDirtyIgnoresDisabledPruneSize()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.prune());
    model.setPruneSizeGB(10);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.storageSettingsDirty());

    model.setPrune(true);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.storageSettingsDirty());
    QVERIFY(model.restartRequired());
}

void OptionsModelTests::pruneDisabledPreservesPreviousValue()
{
    MockNode node;
    node.SetPersistentSetting("prune", MakeInt(QmlCoreSettings::PruneGBToMiB(10)));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.prune());
    QCOMPARE(model.pruneSizeGB(), 10);

    model.setPrune(false);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.prune());
    QCOMPARE(model.pruneSizeGB(), 10);
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto previous_write = ReadRwSetting(model_args, "prune-prev");
    QVERIFY(previous_write.isStr());
    QCOMPARE(previous_write.get_str(), util::ToString(QmlCoreSettings::PruneGBToMiB(10)));
    const auto prune_write = ReadRwSetting(model_args, "prune");
    QVERIFY(prune_write.isNull());
}

void OptionsModelTests::developerDirtyTracksRestartSettings()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.developerSettingsDirty());

    model.setScriptThreads(model.scriptThreads() + 1);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.developerSettingsDirty());
    QVERIFY(model.restartRequired());

    model.setScriptThreads(model.scriptThreads() - 1);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.developerSettingsDirty());
    QVERIFY(!model.restartRequired());
}

void OptionsModelTests::mempoolDirtyTracksRestartSettings()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.developerSettingsDirty());
    QVERIFY(!model.mempoolSettingsDirty());
    QVERIFY(!model.restartRequired());

    model.setMaxMempoolSizeMB(model.maxMempoolSizeMB() + 1);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.developerSettingsDirty());
    QVERIFY(model.mempoolSettingsDirty());
    QVERIFY(model.restartRequired());

    model.setMaxMempoolSizeMB(model.maxMempoolSizeMB() - 1);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.developerSettingsDirty());
    QVERIFY(!model.mempoolSettingsDirty());
    QVERIFY(!model.restartRequired());
}

void OptionsModelTests::proxyValidationAndCommit()
{
    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.validateProxyLocation("127.0.0.1:9050").isEmpty());
    QVERIFY(model.validateProxyLocation("[::1]:9050").isEmpty());
    QVERIFY(model.validateProxyLocation("127.0.0.1").isEmpty());
    QVERIFY(model.validateProxyLocation("[::1]").isEmpty());
    QVERIFY(model.validateProxyLocation("proxy.example:9050").isEmpty());
    QVERIFY(model.validateProxyLocation("proxy.example").isEmpty());
#ifdef HAVE_SOCKADDR_UN
    QVERIFY(model.validateProxyLocation("unix:/tmp/bitcoin-core-proxy.sock").isEmpty());
#else
    QVERIFY(!model.validateProxyLocation("unix:/tmp/bitcoin-core-proxy.sock").isEmpty());
#endif
    QVERIFY(!model.validateProxyLocation("abc..abc:23456").isEmpty());
    QVERIFY(!model.validateProxyLocation("999.999.999.999:9050").isEmpty());
    QVERIFY(!model.validateProxyLocation("127.0.0.1:0").isEmpty());
    QVERIFY(!model.validateProxyLocation("127.0.0.1:65536").isEmpty());
    QVERIFY(!model.validateProxyLocation("127.0.0.1:9050=ipv4").isEmpty());
    QVERIFY(!model.validateProxyLocation("0=cjdns").isEmpty());
    QVERIFY(!model.commitProxyLocation("999.999.999.999:9050"));
    QVERIFY(model.proxyAddress().isEmpty());

    QVERIFY(model.commitProxyLocation("proxy.example"));
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.proxyAddress(), QString("proxy.example"));
}

void OptionsModelTests::proxyDisabledPreservesPreviousValue()
{
    MockNode node;
    node.SetPersistentSetting("proxy", MakeAddress("127.0.0.1:9050"));

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    model.setProxyEnabled(false);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.proxyEnabled());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto previous_write = ReadRwSetting(model_args, "proxy-prev");
    QVERIFY(previous_write.isStr());
    QCOMPARE(previous_write.get_str(), std::string{"127.0.0.1:9050"});
    const auto proxy_write = ReadRwSetting(model_args, "proxy");
    QVERIFY(proxy_write.isNull());
}

void OptionsModelTests::customDataDirValidationRejectsFile()
{

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString file_path = QDir(temp_dir.path()).filePath("not-a-directory");
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.selectCustomDataDir(file_path));
    QTRY_VERIFY(!model.validationPending());
    QVERIFY(!model.validateCustomDataDir(file_path).isEmpty());
    QVERIFY(!model.settingsError().isEmpty());
}

void OptionsModelTests::customDataDirSelectionCreatesDirectoryAndPersists()
{

    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    settings.remove(SettingsKeys::DATA_DIR);
    settings.remove("fReset");

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString data_dir = QDir(temp_dir.path()).filePath("selected-data-dir");
    QVERIFY(!QFileInfo::exists(data_dir));

    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.validateCustomDataDir(data_dir).isEmpty());
    QVERIFY(model.selectCustomDataDir(data_dir));
    QTRY_VERIFY(!model.validationPending());

    QVERIFY(QFileInfo(data_dir).isDir());
    QVERIFY(QFileInfo(QDir(data_dir).filePath("wallets")).isDir());
    QCOMPARE(model.dataDir(), data_dir);
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), data_dir);
    QCOMPARE(settings.value("fReset").toBool(), false);
}

void OptionsModelTests::customDataDirSelectionPreservesExistingWalletDiscovery()
{

    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    settings.remove(SettingsKeys::DATA_DIR);
    settings.remove("fReset");

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString data_dir = QDir(temp_dir.path()).filePath("existing-data-dir");
    QVERIFY(QDir().mkpath(data_dir));
    const QString wallets_dir = QDir(data_dir).filePath("wallets");
    QVERIFY(!QFileInfo::exists(wallets_dir));

    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.validateCustomDataDir(data_dir).isEmpty());
    QVERIFY(model.selectCustomDataDir(data_dir));
    QTRY_VERIFY(!model.validationPending());

    QVERIFY(QFileInfo(data_dir).isDir());
    QVERIFY(!QFileInfo::exists(wallets_dir));
    QCOMPARE(model.dataDir(), data_dir);
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), data_dir);
    QCOMPARE(settings.value("fReset").toBool(), false);
}

void OptionsModelTests::guiDataDirSettingSoftSetsCustomPath()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString data_dir = QDir(temp_dir.path()).filePath("selected-data-dir");
    settings.setValue(SettingsKeys::DATA_DIR, data_dir);

    ArgsManager args;
    QVERIFY(QmlDataDir::ApplyGuiDataDirSetting(args));
    QVERIFY(args.IsArgSet("-datadir"));
    QCOMPARE(QString::fromStdString(args.GetArg("-datadir", "")), data_dir);
    QVERIFY(QFileInfo(data_dir).isDir());
    QVERIFY(QFileInfo(QDir(data_dir).filePath("wallets")).isDir());
}

void OptionsModelTests::guiDataDirSettingAbsolutizesSavedRelativePath()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    CurrentDirectoryRestorer restore_current_dir;
    QVERIFY(QDir::setCurrent(temp_dir.path()));

    const QString relative_data_dir = QStringLiteral("relative-data-dir");
    const QString data_dir = QDir::current().absoluteFilePath(relative_data_dir);
    settings.setValue(SettingsKeys::DATA_DIR, relative_data_dir);

    QCOMPARE(QmlDataDir::ReadGuiDataDir(), data_dir);

    ArgsManager args;
    QVERIFY(QmlDataDir::ApplyGuiDataDirSetting(args));
    QVERIFY(args.IsArgSet("-datadir"));
    QCOMPARE(QString::fromStdString(args.GetArg("-datadir", "")), data_dir);
    QVERIFY(QFileInfo(data_dir).isDir());
    QVERIFY(QFileInfo(QDir(data_dir).filePath("wallets")).isDir());
}

void OptionsModelTests::guiDataDirSettingPreservesExistingWalletDiscovery()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString data_dir = QDir(temp_dir.path()).filePath("existing-data-dir");
    QVERIFY(QDir().mkpath(data_dir));
    const QString wallets_dir = QDir(data_dir).filePath("wallets");
    QVERIFY(!QFileInfo::exists(wallets_dir));
    settings.setValue(SettingsKeys::DATA_DIR, data_dir);

    ArgsManager args;
    QVERIFY(QmlDataDir::ApplyGuiDataDirSetting(args));
    QVERIFY(args.IsArgSet("-datadir"));
    QCOMPARE(QString::fromStdString(args.GetArg("-datadir", "")), data_dir);
    QVERIFY(QFileInfo(data_dir).isDir());
    QVERIFY(!QFileInfo::exists(wallets_dir));
}

void OptionsModelTests::guiDataDirSettingSkipsUnusableConfiguredDir()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString file_path = QDir(temp_dir.path()).filePath("not-a-directory");
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();
    settings.setValue(SettingsKeys::DATA_DIR, file_path);

    ArgsManager args;
    QVERIFY(!QmlDataDir::ApplyGuiDataDirSetting(args));
    QVERIFY(!args.IsArgSet("-datadir"));
}

void OptionsModelTests::guiDataDirSettingSkipsExplicitDatadir()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString saved_data_dir = QDir(temp_dir.path()).filePath("saved-data-dir");
    const QString explicit_data_dir = QDir(temp_dir.path()).filePath("explicit-data-dir");
    settings.setValue(SettingsKeys::DATA_DIR, saved_data_dir);

    ArgsManager args;
    args.ForceSetArg("-datadir", explicit_data_dir.toStdString());
    QVERIFY(!QmlDataDir::ApplyGuiDataDirSetting(args));
    QCOMPARE(QString::fromStdString(args.GetArg("-datadir", "")), explicit_data_dir);
    QVERIFY(!QFileInfo(saved_data_dir).exists());
}

void OptionsModelTests::runtimeDataDirUsesExplicitDatadirOverSavedGuiSetting()
{

    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir saved_data_dir;
    QTemporaryDir explicit_data_dir;
    QVERIFY(saved_data_dir.isValid());
    QVERIFY(explicit_data_dir.isValid());
    settings.setValue(SettingsKeys::DATA_DIR, saved_data_dir.path());

    ArgsManager args;
    PrepareArgsForDataDir(args, explicit_data_dir.path());

    MockNode node;
    InstallPersistentSettings(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.dataDir(), explicit_data_dir.path());
    QCOMPARE(model.getCustomDataDirString(), explicit_data_dir.path());
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), saved_data_dir.path());
}

void OptionsModelTests::guiDataDirSettingLeavesDefaultOverridable()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    settings.setValue(SettingsKeys::DATA_DIR, QmlDataDir::DefaultDataDirString());

    ArgsManager args;
    QVERIFY(!QmlDataDir::ApplyGuiDataDirSetting(args));
    QVERIFY(!args.IsArgSet("-datadir"));
}

void OptionsModelTests::legacyQtDataDirFallbackReadsOldQtSetting()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_default_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt")};
    QSettings qml_settings;
    qml_settings.remove(SettingsKeys::DATA_DIR);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString legacy_data_dir = QDir(temp_dir.path()).filePath(QStringLiteral("legacy-data-dir"));
    legacy_default_settings.settings().setValue(SettingsKeys::DATA_DIR, legacy_data_dir);

    QCOMPARE(QmlDataDir::ReadGuiDataDir(), legacy_data_dir);

    QmlDataDir::PersistDefaultDataDirSelection();
    QVERIFY(!legacy_default_settings.settings().contains(SettingsKeys::DATA_DIR));
}

void OptionsModelTests::guiDataDirChooserShowsForMissingConfiguredDir()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString missing_data_dir = QDir(temp_dir.path()).filePath("missing-data-dir");
    settings.setValue(SettingsKeys::DATA_DIR, missing_data_dir);
    QVERIFY(!QFileInfo::exists(missing_data_dir));

    ArgsManager args;
    QVERIFY(QmlDataDir::ShouldShowDataDirChooser(args, missing_data_dir));
}

void OptionsModelTests::guiDataDirChooserShowsForUnwritableConfiguredDir()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString data_dir = QDir(temp_dir.path()).filePath("unwritable-data-dir");
    QVERIFY(QDir().mkpath(data_dir));
    settings.setValue(SettingsKeys::DATA_DIR, data_dir);

    const QFileDevice::Permissions original_permissions = QFileInfo(data_dir).permissions();
    QVERIFY(QFile(data_dir).setPermissions(QFileDevice::ReadOwner | QFileDevice::ExeOwner));
    if (QFileInfo(data_dir).isWritable()) {
        QVERIFY(QFile(data_dir).setPermissions(original_permissions));
        QSKIP("Cannot make temporary data directory unwritable on this platform.");
    }

    ArgsManager args;
    const bool should_show = QmlDataDir::ShouldShowDataDirChooser(args, data_dir);

    QVERIFY(QFile(data_dir).setPermissions(original_permissions));
    QVERIFY(should_show);
}

void OptionsModelTests::resetGuiSettingsClearsQSettings()
{
    SavedGuiDataDirSettings saved_settings;
    QSettings settings;
    settings.setValue(SettingsKeys::DATA_DIR, QStringLiteral("/tmp/old-bitcoin-data"));
    settings.setValue(SettingsKeys::LANGUAGE, QStringLiteral("de"));
    settings.setValue(SettingsKeys::DISPLAY_UNIT, 3);
    settings.setValue(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, QStringLiteral("https://example.com/%s"));
    settings.setValue(SettingsKeys::MONEY_FONT_CHOICE, QStringLiteral("best_system"));
    settings.setValue("fReset", true);

    std::vector<std::string> argv = TestArgv();
    argv.emplace_back("-settings=");
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());

    QString reset_error;
    QVERIFY2(QmlDataDir::ResetGuiSettings(args, &reset_error), qPrintable(reset_error));

    QVERIFY(!settings.contains(SettingsKeys::DATA_DIR));
    QVERIFY(!settings.contains(SettingsKeys::LANGUAGE));
    QVERIFY(!settings.contains(SettingsKeys::DISPLAY_UNIT));
    QVERIFY(!settings.contains(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS));
    QVERIFY(!settings.contains(SettingsKeys::MONEY_FONT_CHOICE));
    QCOMPARE(settings.value("fReset").toBool(), false);
}

void OptionsModelTests::resetGuiSettingsClearsLegacyQtSettings()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_core_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    SavedNamedSettings legacy_default_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt")};
    legacy_core_settings.settings().setValue(QStringLiteral("fListen"), false);
    legacy_core_settings.settings().setValue(QStringLiteral("addrProxy"), QStringLiteral("10.0.0.1:9050"));
    legacy_core_settings.settings().setValue(SettingsKeys::DISPLAY_UNIT, 3);
    legacy_default_settings.settings().setValue(SettingsKeys::DATA_DIR, QStringLiteral("/tmp/legacy-bitcoin-data"));

    std::vector<std::string> argv = TestArgv();
    argv.emplace_back("-settings=");
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());

    QString reset_error;
    QVERIFY2(QmlDataDir::ResetGuiSettings(args, &reset_error), qPrintable(reset_error));

    QVERIFY(!legacy_core_settings.settings().contains(QStringLiteral("fListen")));
    QVERIFY(!legacy_core_settings.settings().contains(QStringLiteral("addrProxy")));
    QVERIFY(!legacy_core_settings.settings().contains(SettingsKeys::DISPLAY_UNIT));
    QVERIFY(!legacy_default_settings.settings().contains(SettingsKeys::DATA_DIR));
}

void OptionsModelTests::resetGuiSettingsStartsOnboardingFromDefaultDataDir()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir old_data_dir;
    QVERIFY(old_data_dir.isValid());
    QSettings settings;
    settings.setValue(SettingsKeys::DATA_DIR, old_data_dir.path());

    std::vector<std::string> argv = TestArgv();
    argv.emplace_back("-settings=");
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());

    QString reset_error;
    QVERIFY2(QmlDataDir::ResetGuiSettings(args, &reset_error), qPrintable(reset_error));

    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QTRY_VERIFY(!model.validationPending());
    QCOMPARE(model.dataDir(), QmlDataDir::DefaultDataDirString());
    QVERIFY(model.getCustomDataDirString().isEmpty());
}

void OptionsModelTests::resetGuiSettingsClearsSettingsJson()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));

    const std::vector<std::string> argv{
        std::string{"bitcoinqml"},
        std::string{"-regtest"},
        "-datadir=" + data_dir.path().toStdString(),
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

    QString reset_error;
    QVERIFY2(QmlDataDir::ResetGuiSettings(args, &reset_error), qPrintable(reset_error));

    fs::path backup_path;
    QVERIFY(args.GetSettingsPath(&backup_path, /*temp=*/false, /*backup=*/true));
    QVERIFY(fs::exists(backup_path));

    ArgsManager check_args;
    QVERIFY2(PrepareTestArgs(check_args, argv, parse_error), parse_error.c_str());
    SelectParams(check_args.GetChainType());
    check_args.SelectConfigNetwork(check_args.GetChainTypeString());
    QVERIFY2(check_args.ReadSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());
    check_args.LockSettings([](common::Settings& settings) {
        QVERIFY(settings.rw_settings.count("prune") == 0);
        QVERIFY(settings.rw_settings.count("proxy") == 0);
        QVERIFY(settings.rw_settings.count("onion") == 0);
    });
}

void OptionsModelTests::resetGuiSettingsPreviewIgnoresSelectedCustomDataDirSettingsJson()
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

void OptionsModelTests::resetGuiSettingsApplyClearsSelectedCustomDataDirSettingsJson()
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
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(write_args.WriteSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());

    std::vector<std::string> argv = TestArgv();
    argv.emplace_back("-resetguisettings");
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QTRY_VERIFY(!model.validationPending());
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QTRY_VERIFY(!model.validationPending());
    QCOMPARE(model.previewError(), QString{});
    QVERIFY(model.listen());
    QVERIFY(model.natpmp());
    QVERIFY(!model.server());
    QVERIFY(!model.proxyEnabled());

    ArgsManager apply_args;
    QVERIFY2(PrepareTestArgs(apply_args, argv, parse_error), parse_error.c_str());
    QString apply_error;
    QVERIFY2(model.applyToArgs(apply_args, &apply_error), qPrintable(apply_error));

    fs::path backup_path;
    QVERIFY(apply_args.GetSettingsPath(&backup_path, /*temp=*/false, /*backup=*/true));
    QVERIFY(fs::exists(backup_path));

    ArgsManager check_args;
    QVERIFY2(PrepareTestArgs(check_args, write_argv, parse_error), parse_error.c_str());
    SelectParams(check_args.GetChainType());
    check_args.SelectConfigNetwork(check_args.GetChainTypeString());
    QVERIFY2(check_args.ReadSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());
    check_args.LockSettings([](common::Settings& settings) {
        QVERIFY(settings.rw_settings.count("listen") == 0);
        QVERIFY(settings.rw_settings.count("natpmp") == 0);
        QVERIFY(settings.rw_settings.count("server") == 0);
        QVERIFY(settings.rw_settings.count("proxy") == 0);
    });
}

void OptionsModelTests::resetGuiSettingsPreservesCommandLineOverrides()
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

    QString reset_error;
    QVERIFY2(QmlDataDir::ResetGuiSettings(args, &reset_error), qPrintable(reset_error));

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(argv, /*can_listen_ipc=*/false, QmlDataDir::DefaultDataDirString())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.proxy_enabled);
    QCOMPARE(preview.values.proxy_address, QStringLiteral("10.0.0.2:9050"));
    QVERIFY(preview.values.prune);
    QCOMPARE(preview.values.prune_size_gb, QmlCoreSettings::PruneMiBToGB(2048));
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("proxy")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("command_line"));
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("prune")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("command_line"));
}

void OptionsModelTests::resetGuiSettingsPreservesBitcoinConfOverrides()
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

void OptionsModelTests::resetGuiSettingsExplicitDatadirClearsThatDatadirSettingsJson()
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
        settings.rw_settings["server"] = common::SettingsValue{true};
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"10.0.0.1:9050"}};
    });
    std::vector<std::string> settings_errors;
    QVERIFY2(args.WriteSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());

    QString reset_error;
    QVERIFY2(QmlDataDir::ResetGuiSettings(args, &reset_error), qPrintable(reset_error));

    ArgsManager check_args;
    QVERIFY2(PrepareTestArgs(check_args, argv, parse_error), parse_error.c_str());
    SelectParams(check_args.GetChainType());
    check_args.SelectConfigNetwork(check_args.GetChainTypeString());
    QVERIFY2(check_args.ReadSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());
    check_args.LockSettings([](common::Settings& settings) {
        QVERIFY(settings.rw_settings.count("server") == 0);
        QVERIFY(settings.rw_settings.count("proxy") == 0);
    });
}

void OptionsModelTests::qmlOnboardedProfileSkipsPreInitOnboarding()
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

void OptionsModelTests::qmlOnboardedCommandLineOverrideShowsPreInitOnboarding()
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

void OptionsModelTests::configuredDatadirOnboardingStatus_data()
{
    QTest::addColumn<QString>("option");
    QTest::addColumn<bool>("explicit_datadir");
    QTest::addColumn<bool>("expected_onboarding");

    QTest::newRow("config-profile") << QString{} << false << false;
    QTest::newRow("config-choose") << QStringLiteral("-choosedatadir") << false << true;
    QTest::newRow("config-reset") << QStringLiteral("-resetguisettings") << false << true;
    QTest::newRow("config-current-reset") << QStringLiteral("current-reset") << false << true;
    QTest::newRow("config-legacy-reset") << QStringLiteral("legacy-reset") << false << true;
    QTest::newRow("config-marker-override") << QStringLiteral("-qml_onboarded=0") << false << true;
    QTest::newRow("explicit-profile") << QString{} << true << false;
    QTest::newRow("explicit-choose") << QStringLiteral("-choosedatadir") << true << false;
    QTest::newRow("explicit-reset") << QStringLiteral("-resetguisettings") << true << true;
    QTest::newRow("explicit-current-reset") << QStringLiteral("current-reset") << true << false;
    QTest::newRow("explicit-legacy-reset") << QStringLiteral("legacy-reset") << true << false;
    QTest::newRow("explicit-marker-override") << QStringLiteral("-qml_onboarded=0") << true << true;
}

void OptionsModelTests::configuredDatadirOnboardingStatus()
{
    QFETCH(QString, option);
    QFETCH(bool, explicit_datadir);
    QFETCH(bool, expected_onboarding);

    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings legacy_default_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt")};
    QSettings settings;
    settings.clear();
    // Pin the fallback selection independently of any legacy directory setting.
    settings.setValue(SettingsKeys::DATA_DIR, QmlDataDir::DefaultDataDirString());
    if (option == QStringLiteral("current-reset")) {
        settings.setValue(QStringLiteral("fReset"), true);
    } else if (option == QStringLiteral("legacy-reset")) {
        legacy_default_settings.settings().setValue(QStringLiteral("fReset"), true);
    }
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QTemporaryDir configured_data_dir;
    QVERIFY(configured_data_dir.isValid());

    const QString conf_path = QDir(temp_dir.path()).filePath(QStringLiteral("bitcoin.conf"));
    QFile conf(conf_path);
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write(QStringLiteral("regtest=1\ndatadir=%1\n[regtest]\nserver=1\n").arg(configured_data_dir.path()).toUtf8()) > 0);
    conf.close();

    std::vector<std::string> argv{
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

    if (option.startsWith('-')) argv.push_back(option.toStdString());
    if (explicit_datadir) argv.push_back("-datadir=" + configured_data_dir.path().toStdString());
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QCOMPARE(status.qml_onboarded, !expected_onboarding);
    QCOMPARE(status.should_show_onboarding, expected_onboarding);
    QCOMPARE(status.active_data_dir, configured_data_dir.path());
    QCOMPARE(status.data_dir_source, explicit_datadir ? QmlOnboardingSettings::DataDirSource::ExplicitArg : QmlOnboardingSettings::DataDirSource::Config);
}

void OptionsModelTests::configuredDatadirPreviewKeepsConfigSource()
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

void OptionsModelTests::configuredDatadirApplyDoesNotPersistGuiDataDir()
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
    QString apply_error;
    QVERIFY2(QmlOnboardingSettings::ApplyToArgs(
        args,
        QmlOnboardingSettings::DataDirSelection{status.active_data_dir, status.data_dir_source},
        {},
        preview.values,
        &apply_error), qPrintable(apply_error));

    QVERIFY(!settings.contains(SettingsKeys::DATA_DIR));
    QCOMPARE(SettingToBool(args.GetPersistentSetting("qml_onboarded")), true);
    QVERIFY(QFileInfo(QDir(configured_data_dir.path()).filePath(QStringLiteral("regtest/settings.json"))).isFile());
}

void OptionsModelTests::explicitDatadirApplyDoesNotPersistGuiDataDir()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QSettings settings;
    settings.remove(SettingsKeys::DATA_DIR);

    const std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QTRY_VERIFY(!model.validationPending());
    QCOMPARE(model.dataDir(), data_dir.path());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    QString apply_error;
    QVERIFY2(model.applyToArgs(args, &apply_error), qPrintable(apply_error));

    QVERIFY(!settings.contains(SettingsKeys::DATA_DIR));
    QCOMPARE(SettingToBool(args.GetPersistentSetting("qml_onboarded")), true);
}

void OptionsModelTests::asynchronousOnboardingPreparation_data()
{
    QTest::addColumn<QString>("outcome");
    for (const auto* outcome : {"success", "failure", "exception", "unknown", "settings-error"}) {
        QTest::newRow(outcome) << QString::fromLatin1(outcome);
    }
}

void OptionsModelTests::asynchronousOnboardingPreparation()
{
    QFETCH(QString, outcome);
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    const auto argv = TestArgvWithDataDir(data_dir.path());
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    QSemaphore entered, release;
    std::atomic_int calls{0};
    std::atomic_bool off_gui{false};
    const auto gui_thread = QThread::currentThread();
    const auto drain_before_fixture_destruction = qScopeGuard([&] {
        release.release();
        bool drained{false};
        BackendExecutor::shutdownAll(this, [&] { drained = true; });
        QVERIFY(QTest::qWaitFor([&] { return drained; }, 5000));
    });
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    if (outcome == "settings-error") {
        QVERIFY(QDir().mkpath(data_dir.filePath("regtest/settings.json")));
    }
    QSignalSpy prepared{&model, &OnboardingOptionsModel::nodePrepared};
    model.prepareNode(args, [&] {
        ++calls;
        off_gui = QThread::currentThread() != gui_thread;
        entered.release();
        release.acquire();
        if (outcome == "exception") throw std::runtime_error{"preparation failed"};
        if (outcome == "unknown") throw 42;
        return outcome == "success";
    });
    model.prepareNode(args, [&] { ++calls; return true; });
    if (outcome != "settings-error") {
        QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, 5000);
        bool heartbeat{false};
        QTimer::singleShot(0, &model, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QVERIFY(prepared.empty());
        QVERIFY(!model.canFinish());
        release.release();
    }
    QTRY_COMPARE_WITH_TIMEOUT(prepared.size(), 1, 5000);
    QCOMPARE(calls.load(), outcome == "settings-error" ? 0 : 1);
    QCOMPARE(off_gui.load(), outcome != "settings-error");
    QCOMPARE(prepared.first().at(0).toBool(), outcome == "success");
    const auto error = prepared.first().at(1).toString();
    if (outcome == "exception") QCOMPARE(error, QStringLiteral("preparation failed"));
    else if (outcome == "unknown") QVERIFY(error.contains("Unknown exception"));
    else if (outcome == "settings-error") QVERIFY(!error.isEmpty());
    else QVERIFY(error.isEmpty());
}

void OptionsModelTests::qmlOnboardedResetGuiSettingsShowsPreInitOnboarding()
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

void OptionsModelTests::qmlOnboardedChooseDataDirShowsPreInitOnboarding()
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

void OptionsModelTests::qmlOnboardedCurrentResetFlagShowsPreInitOnboarding()
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

void OptionsModelTests::qmlOnboardedLegacyResetFlagShowsPreInitOnboarding()
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

void OptionsModelTests::existingCoreProfileShowsFullOnboardingWithCurrentSettings()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("regtest=1\n[regtest]\nserver=1\nprune=4096\n") > 0);
    conf.close();

    const std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.should_show_onboarding);
    QVERIFY(!status.qml_onboarded);
    QCOMPARE(status.active_data_dir, data_dir.path());

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.server);
    QVERIFY(preview.values.prune);
    QCOMPARE(preview.values.prune_size_gb, 5);
    QVERIFY(preview.profile.existing_profile);
    QVERIFY(preview.profile.has_config_file);
}

void OptionsModelTests::freshExplicitDatadirPreviewReportsFreshProfile()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.profile.existing_profile);
    QVERIFY(!preview.profile.has_settings_file);
    QVERIFY(!preview.profile.has_config_file);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(!preview.profile.has_wallet_data);
}

void OptionsModelTests::onboardingPreviewDetectsSettingsJsonProfile()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    ArgsManager write_args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(write_args, TestArgvWithDataDir(data_dir.path()), parse_error), parse_error.c_str());
    SelectParams(write_args.GetChainType());
    write_args.SelectConfigNetwork(write_args.GetChainTypeString());
    write_args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["listen"] = common::SettingsValue{false};
    });
    QVERIFY(QDir(data_dir.path()).mkpath(QStringLiteral("regtest")));
    std::vector<std::string> settings_errors;
    QVERIFY2(write_args.WriteSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.existing_profile);
    QVERIFY(preview.profile.has_settings_file);
    QVERIFY(!preview.profile.has_config_file);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(!preview.profile.has_wallet_data);
}

void OptionsModelTests::onboardingPreviewDetectsChainDataWithoutCreatingBlocksDir()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    const QString network_dir = QDir(data_dir.path()).filePath(QStringLiteral("regtest"));
    const QString chainstate_dir = QDir(network_dir).filePath(QStringLiteral("chainstate"));
    const QString blocks_dir = QDir(network_dir).filePath(QStringLiteral("blocks"));
    QVERIFY(QDir().mkpath(chainstate_dir));
    QFile chainstate_marker(QDir(chainstate_dir).filePath(QStringLiteral("CURRENT")));
    QVERIFY(chainstate_marker.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(chainstate_marker.write("manifest\n") > 0);
    chainstate_marker.close();
    QVERIFY(!QFileInfo::exists(blocks_dir));

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.existing_profile);
    QVERIFY(preview.profile.has_chain_data);
    QVERIFY(!preview.profile.has_wallet_data);
    QVERIFY(!QFileInfo::exists(blocks_dir));
}

void OptionsModelTests::onboardingPreviewDetectsRootWalletProfile()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    const QString wallet_dir = QDir(data_dir.path()).filePath(QStringLiteral("regtest/rootwallet"));
    WriteSqliteWalletMarker(wallet_dir);

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.existing_profile);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(preview.profile.has_wallet_data);
}

void OptionsModelTests::onboardingPreviewDetectsExplicitWalletDirProfile()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QTemporaryDir wallet_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(wallet_dir.isValid());
    WriteSqliteWalletMarker(QDir(wallet_dir.path()).filePath(QStringLiteral("customwallet")));

    std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    argv.push_back("-walletdir=" + wallet_dir.path().toStdString());
    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.existing_profile);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(preview.profile.has_wallet_data);
}

void OptionsModelTests::onboardingPreviewIgnoresEmptyExplicitWalletDir()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QTemporaryDir wallet_dir;
    QVERIFY(data_dir.isValid());
    QVERIFY(wallet_dir.isValid());

    std::vector<std::string> argv{TestArgvWithDataDir(data_dir.path())};
    argv.push_back("-walletdir=" + wallet_dir.path().toStdString());
    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.profile.existing_profile);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(!preview.profile.has_wallet_data);
}

void OptionsModelTests::onboardingPreviewIgnoresUnrecognizedWalletsEntry()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    const QString wallets_dir = QDir(data_dir.path()).filePath(QStringLiteral("regtest/wallets"));
    QVERIFY(QDir().mkpath(wallets_dir));
    QFile junk_file(QDir(wallets_dir).filePath(QStringLiteral("not-a-wallet")));
    QVERIFY(junk_file.open(QIODevice::WriteOnly));
    QVERIFY(junk_file.write("not wallet data\n") > 0);
    junk_file.close();

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.profile.existing_profile);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(!preview.profile.has_wallet_data);
}

void OptionsModelTests::freshExplicitDatadirShowsFullOnboarding()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString data_dir = QDir(temp_dir.path()).filePath(QStringLiteral("fresh-data-dir"));
    QVERIFY(QDir().mkpath(data_dir));

    const QmlOnboardingSettings::OnboardingStartupStatus status{
        QmlOnboardingSettings::ResolveOnboardingStartupStatus(TestArgvWithDataDir(data_dir), /*can_listen_ipc=*/false)
    };
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.should_show_onboarding);
    QVERIFY(!status.qml_onboarded);
    QCOMPARE(status.active_data_dir, data_dir);
}

void OptionsModelTests::onboardingApplyWithoutTouchedSettingsOnlyAddsQmlOnboardedMarker()
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
    QVERIFY2(PrepareTestArgs(args, TestArgvWithDataDir(data_dir.path()), parse_error), parse_error.c_str());

    QString apply_error;
    QVERIFY2(QmlOnboardingSettings::ApplyToArgs(args, data_dir.path(), {}, QmlCoreSettings::Values{}, &apply_error), qPrintable(apply_error));

    QCOMPARE(SettingToBool(args.GetPersistentSetting("qml_onboarded")), true);
    bool has_listen_override{true};
    bool has_prune_override{true};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
        has_prune_override = settings.rw_settings.count("prune") > 0;
    });
    QVERIFY(!has_listen_override);
    QVERIFY(!has_prune_override);
}

void OptionsModelTests::onboardingApplyCreatesWalletSubdirectoryForNewNetworkDataDir()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    const QString network_wallets_dir = QDir(data_dir.path()).filePath(QStringLiteral("regtest/wallets"));
    QVERIFY(!QFileInfo::exists(network_wallets_dir));

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgvWithDataDir(data_dir.path()), parse_error), parse_error.c_str());

    QString apply_error;
    QVERIFY2(QmlOnboardingSettings::ApplyToArgs(args, data_dir.path(), {}, QmlCoreSettings::Values{}, &apply_error), qPrintable(apply_error));

    QCOMPARE(SettingToBool(args.GetPersistentSetting("qml_onboarded")), true);
    QVERIFY(QFileInfo(network_wallets_dir).isDir());
}

void OptionsModelTests::onboardingApplyPreservesExistingNetworkWalletDiscovery()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    const QString network_dir = QDir(data_dir.path()).filePath(QStringLiteral("regtest"));
    const QString network_wallets_dir = QDir(network_dir).filePath(QStringLiteral("wallets"));
    QVERIFY(QDir().mkpath(network_dir));
    QVERIFY(!QFileInfo::exists(network_wallets_dir));

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgvWithDataDir(data_dir.path()), parse_error), parse_error.c_str());

    QString apply_error;
    QVERIFY2(QmlOnboardingSettings::ApplyToArgs(args, data_dir.path(), {}, QmlCoreSettings::Values{}, &apply_error), qPrintable(apply_error));

    QCOMPARE(SettingToBool(args.GetPersistentSetting("qml_onboarded")), true);
    QVERIFY(QFileInfo(QDir(network_dir).filePath(QStringLiteral("settings.json"))).isFile());
    QVERIFY(!QFileInfo::exists(network_wallets_dir));
}

void OptionsModelTests::fullOnboardingApplyWritesQmlOnboardedMarker()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QSettings settings;
    settings.remove(SettingsKeys::DATA_DIR);

    const std::vector<std::string> argv = TestArgv();
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QTRY_VERIFY(!model.validationPending());
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QTRY_VERIFY(!model.validationPending());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    QString apply_error;
    QVERIFY2(model.applyToArgs(args, &apply_error), qPrintable(apply_error));
    QCOMPARE(SettingToBool(args.GetPersistentSetting("qml_onboarded")), true);
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR).toString(), data_dir.path());
}

void OptionsModelTests::onboardingPreviewAppliesParameterInteractions()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(QDir(data_dir.path()).filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("regtest=1\n[regtest]\nproxy=10.0.0.3:9050\n") > 0);
    conf.close();

    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(TestArgv(), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.values.proxy_enabled);
    QCOMPARE(preview.values.proxy_address, QStringLiteral("10.0.0.3:9050"));
    QVERIFY(!preview.values.listen);
    QVERIFY(!preview.values.natpmp);
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("proxy")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));
}

void OptionsModelTests::storageSpaceCheckAcceptsExistingDirectory()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());

    const QmlDataDir::StorageSpaceResult result = QmlDataDir::CheckStorageSpace(temp_dir.path());
    QVERIFY(result.ok);
    QVERIFY(result.path_exists);
    QVERIFY(result.path_is_directory);
    QVERIFY(!result.checked_path.isEmpty());
    QVERIFY(result.capacity_bytes >= result.available_bytes);
}

void OptionsModelTests::storageSpaceCheckRejectsExistingFile()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString file_path = QDir(temp_dir.path()).filePath("not-a-directory");
    QFile file(file_path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.close();

    const QmlDataDir::StorageSpaceResult result = QmlDataDir::CheckStorageSpace(file_path);
    QVERIFY(!result.ok);
    QVERIFY(result.path_exists);
    QVERIFY(!result.path_is_directory);
    QVERIFY(!result.message.isEmpty());
}

void OptionsModelTests::thirdPartyTransactionLinksParseValidUrls()
{

    QSettings settings;
    const bool had_urls_setting = settings.contains(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS);
    const QVariant previous_urls_setting = settings.value(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS);

    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    model.setThirdPartyTransactionUrls(
        "https://example.com/tx/%s|"
        "http://example.net/tx/%s|"
        "file://host/tmp/%s|"
        "bitcoin://host/tx/%s|"
        "ftp://example.org/tx/%s|"
        "https://example.org/tx|"
        "not-a-url|"
        "https:///tx/%s");
    QTRY_VERIFY(!model.settingsPending());
    const QVariantList links = model.thirdPartyTransactionLinks("abc");
    QCOMPARE(links.size(), 2);
    const QVariantMap https_link = links.at(0).toMap();
    QCOMPARE(https_link.value("host").toString(), QString("example.com"));
    QCOMPARE(https_link.value("url").toString(), QString("https://example.com/tx/abc"));
    const QVariantMap http_link = links.at(1).toMap();
    QCOMPARE(http_link.value("host").toString(), QString("example.net"));
    QCOMPARE(http_link.value("url").toString(), QString("http://example.net/tx/abc"));

    if (had_urls_setting) {
        settings.setValue(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, previous_urls_setting);
    } else {
        settings.remove(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS);
    }
}

void OptionsModelTests::moneyFontChoicePersists()
{

    QSettings settings;
    const bool had_font_setting = settings.contains(SettingsKeys::MONEY_FONT_CHOICE);
    const QVariant previous_font_setting = settings.value(SettingsKeys::MONEY_FONT_CHOICE);
    settings.remove(SettingsKeys::MONEY_FONT_CHOICE);

    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.moneyFontChoice(), QString("embedded"));
    model.setMoneyFontChoice("best_system");
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.moneyFontChoice(), QString("best_system"));
    QCOMPARE(settings.value(SettingsKeys::MONEY_FONT_CHOICE).toString(), QString("best_system"));

    if (had_font_setting) {
        settings.setValue(SettingsKeys::MONEY_FONT_CHOICE, previous_font_setting);
    } else {
        settings.remove(SettingsKeys::MONEY_FONT_CHOICE);
    }
}

void OptionsModelTests::displayUnitUsesQtCompatibleSettingsKey()
{

    QSettings settings;
    const bool had_display_unit_setting = settings.contains(SettingsKeys::DISPLAY_UNIT);
    const QVariant previous_display_unit_setting = settings.value(SettingsKeys::DISPLAY_UNIT);
    settings.setValue(SettingsKeys::DISPLAY_UNIT, QVariant::fromValue(LegacyDisplayUnit::SAT));

    MockNode node;

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(QString::fromUtf8(SettingsKeys::DISPLAY_UNIT), QStringLiteral("DisplayBitcoinUnit"));
    QCOMPARE(model.displayUnit(), 3);
    QCOMPARE(model.displayUnitLabel(), QStringLiteral("sat"));
    QCOMPARE(model.displayUnitLabelForAmount(2), QStringLiteral("sats"));

    model.setDisplayUnit(1);
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.displayUnit(), 1);
    QCOMPARE(model.displayUnitLabel(), QStringLiteral("mBTC"));
    QCOMPARE(model.displayUnitLabelForAmount(2), QStringLiteral("mBTC"));
    QCOMPARE(settings.value(SettingsKeys::DISPLAY_UNIT).toInt(), 1);

    model.setDisplayUnit(2);
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.displayUnit(), 2);
    QCOMPARE(model.displayUnitLabel(), QStringLiteral("bits"));
    QCOMPARE(model.displayUnitLabelForAmount(2), QStringLiteral("bits"));
    QCOMPARE(settings.value(SettingsKeys::DISPLAY_UNIT).toInt(), 2);

    model.setDisplayUnit(9);
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.displayUnit(), 0);
    QCOMPARE(settings.value(SettingsKeys::DISPLAY_UNIT).toInt(), 0);

    if (had_display_unit_setting) {
        settings.setValue(SettingsKeys::DISPLAY_UNIT, previous_display_unit_setting);
    } else {
        settings.remove(SettingsKeys::DISPLAY_UNIT);
    }
}

void OptionsModelTests::displayUnitUsesLegacyQtFallback()
{

    SavedSettingsFormat settings_format{QSettings::IniFormat};
    SavedGuiDataDirSettings saved_settings;
    SavedRawNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};

    QSettings settings;
    settings.remove(SettingsKeys::DISPLAY_UNIT);

    QFile settings_file{legacy_settings.fileName()};
    QVERIFY(QDir().mkpath(QFileInfo(settings_file).absolutePath()));
    QVERIFY(settings_file.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
    QVERIFY(settings_file.write(R"ini([General]
DisplayBitcoinUnit=@Variant(\0\0\0\x7f\0\0\0\fBitcoinUnit\0\x3)
)ini") > 0);
    settings_file.close();

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgv(), parse_error), parse_error.c_str());
    args.SelectConfigNetwork(args.GetChainTypeString());

    MockNode node;
    InstallPersistentSettings(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.displayUnit(), 3);
    QCOMPARE(model.displayUnitLabel(), QStringLiteral("sat"));
    QCOMPARE(model.displayUnitLabelForAmount(2), QStringLiteral("sats"));
}

void OptionsModelTests::displayUnitPrefersQmlSettingOverLegacyQtFallback()
{

    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};

    QSettings settings;
    settings.setValue(SettingsKeys::DISPLAY_UNIT, 1);
    legacy_settings.settings().setValue(SettingsKeys::DISPLAY_UNIT, 3);

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgv(), parse_error), parse_error.c_str());
    args.SelectConfigNetwork(args.GetChainTypeString());

    MockNode node;
    InstallPersistentSettings(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.displayUnit(), 1);
    QCOMPARE(model.displayUnitLabel(), QStringLiteral("mBTC"));
}

void OptionsModelTests::sharedCoreSettingHelpersDeduplicateOverrides()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["listen"].push_back(common::SettingsValue{false});
    });

    QVariantMap status = QmlCoreSettings::CoreSettingStatus(args, QStringLiteral("listen"));
    QCOMPARE(status.value("source").toString(), QString("bitcoin_conf"));
    QCOMPARE(status.value("canEdit").toBool(), true);

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("listen"), common::SettingsValue{false}));
    bool has_listen_override{true};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
    });
    QVERIFY(!has_listen_override);

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("listen"), common::SettingsValue{true}));
    common::SettingsValue listen_override;
    args.LockSettings([&](common::Settings& settings) {
        listen_override = settings.rw_settings.at("listen");
    });
    QVERIFY(listen_override.isBool());
    QVERIFY(listen_override.get_bool());

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("maxmempool"), common::SettingsValue{DEFAULT_MAX_MEMPOOL_SIZE_MB}));
    bool has_mempool_override{true};
    args.LockSettings([&](common::Settings& settings) {
        has_mempool_override = settings.rw_settings.count("maxmempool") > 0;
    });
    QVERIFY(!has_mempool_override);
}

void OptionsModelTests::parameterInteractionOverridesPersistExplicitValues()
{
    ArgsManager listen_args;
    listen_args.SelectConfigNetwork("main");
    listen_args.ForceSetArg("-listen", "0");

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(listen_args, QStringLiteral("listen"), common::SettingsValue{true}));
    common::SettingsValue listen_override;
    listen_args.LockSettings([&](common::Settings& settings) {
        listen_override = settings.rw_settings.at("listen");
    });
    QVERIFY(listen_override.isBool());
    QVERIFY(listen_override.get_bool());

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(listen_args, QStringLiteral("listen"), common::SettingsValue{false}));
    bool has_listen_override{true};
    listen_args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
    });
    QVERIFY(!has_listen_override);

    ArgsManager mempool_args;
    mempool_args.SelectConfigNetwork("main");
    mempool_args.ForceSetArg("-maxmempool", "5");

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(mempool_args, QStringLiteral("maxmempool"), common::SettingsValue{DEFAULT_MAX_MEMPOOL_SIZE_MB}));
    common::SettingsValue mempool_override;
    mempool_args.LockSettings([&](common::Settings& settings) {
        mempool_override = settings.rw_settings.at("maxmempool");
    });
    QVERIFY(mempool_override.isNum());
    QCOMPARE(mempool_override.getInt<int64_t>(), int64_t{DEFAULT_MAX_MEMPOOL_SIZE_MB});

    ArgsManager signer_args;
    signer_args.SelectConfigNetwork("main");
    signer_args.ForceSetArg("-signer", "runtime-signer");

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(signer_args, QStringLiteral("signer"), common::SettingsValue{std::string{"runtime-signer"}}));
    common::SettingsValue signer_override;
    signer_args.LockSettings([&](common::Settings& settings) {
        signer_override = settings.rw_settings.at("signer");
    });
    QVERIFY(signer_override.isStr());
    QCOMPARE(QString::fromStdString(signer_override.get_str()), QStringLiteral("runtime-signer"));
}

void OptionsModelTests::coreSettingsLegacyNumericOverridesWriteStrings()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("dbcache"), common::SettingsValue{600}));
    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("par"), common::SettingsValue{12}));
    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("maxmempool"), common::SettingsValue{456}));

    common::SettingsValue dbcache;
    common::SettingsValue script_threads;
    common::SettingsValue maxmempool;
    args.LockSettings([&](common::Settings& settings) {
        dbcache = settings.rw_settings.at("dbcache");
        script_threads = settings.rw_settings.at("par");
        maxmempool = settings.rw_settings.at("maxmempool");
    });

    QVERIFY(dbcache.isStr());
    QCOMPARE(QString::fromStdString(dbcache.get_str()), QStringLiteral("600"));
    QVERIFY(script_threads.isStr());
    QCOMPARE(QString::fromStdString(script_threads.get_str()), QStringLiteral("12"));
    QVERIFY(maxmempool.isNum());
    QCOMPARE(maxmempool.getInt<int64_t>(), 456);
}

void OptionsModelTests::coreSettingsModelEntryMutatesRuntimeModel()
{

    ArgsManager args;
    std::string error;
    QVERIFY(PrepareTestArgs(args, TestArgv(), error));

    MockNode node;
    InstallPersistentSettings(node, args);
    InstallRwSettingsWriter(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    auto* core_settings = qobject_cast<CoreSettingsModel*>(model.coreSettings());
    QVERIFY(core_settings);
    auto* listen_entry = qobject_cast<CoreSettingEntryModel*>(core_settings->entry(QStringLiteral("listen")));
    QVERIFY(listen_entry);

    QVERIFY(model.listen());
    QVERIFY(!model.connectionSettingsDirty());

    listen_entry->setValue(false);
    QTRY_VERIFY(!model.settingsPending());

    QVERIFY(!model.listen());
    QVERIFY(model.connectionSettingsDirty());
    QCOMPARE(SettingToBool(args.GetPersistentSetting("listen")), false);
}

void OptionsModelTests::coreSettingsSessionCommandLineSettingDoesNotWrite()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.command_line_options["listen"].push_back(common::SettingsValue{true});
    });

    QmlCoreSettings::Values values;
    values.listen = false;
    QmlCoreSettings::Session session{values, QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())};

    QVERIFY(!session.canEdit(QStringLiteral("listen")));
    QVERIFY(!session.setListen(true));
    session.markTouched(QStringLiteral("listen"));
    QVERIFY(!session.writeTouchedToArgs(args));

    bool has_listen_override{false};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
    });
    QVERIFY(!has_listen_override);
}

void OptionsModelTests::coreSettingsSessionDoesNotCopyUntouchedConfig()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["listen"].push_back(common::SettingsValue{false});
    });

    QmlCoreSettings::Session session{
        QmlCoreSettings::Values{.listen = false},
        QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())
    };
    QVERIFY(session.writeTouchedToArgs(args));

    bool has_listen_override{false};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
    });
    QVERIFY(!has_listen_override);
}

void OptionsModelTests::coreSettingsSessionWritesTouchedConfigOverride()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["listen"].push_back(common::SettingsValue{false});
    });

    QmlCoreSettings::Session session{
        QmlCoreSettings::Values{.listen = true},
        QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())
    };
    session.markTouched(QStringLiteral("listen"));
    QVERIFY(session.writeTouchedToArgs(args));

    common::SettingsValue listen_override;
    args.LockSettings([&](common::Settings& settings) {
        listen_override = settings.rw_settings.at("listen");
    });
    QVERIFY(listen_override.isBool());
    QVERIFY(listen_override.get_bool());
}

void OptionsModelTests::coreSettingsSessionRevertingToDefaultDeletesOverride()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["listen"] = common::SettingsValue{false};
    });

    QmlCoreSettings::Values values;
    values.listen = DEFAULT_LISTEN;
    QmlCoreSettings::Session session{values, QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())};
    session.markTouched(QStringLiteral("listen"));
    QVERIFY(session.writeTouchedToArgs(args));

    bool has_listen_override{true};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
    });
    QVERIFY(!has_listen_override);
}

void OptionsModelTests::coreSettingsSessionPruneDisabledPreservesPreviousValue()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");

    QmlCoreSettings::Values values;
    values.prune = false;
    values.prune_size_gb = 10;
    QmlCoreSettings::Session session{values, QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())};
    session.markTouched(QStringLiteral("prune"));
    QVERIFY(session.writeTouchedToArgs(args));

    common::SettingsValue prune_prev;
    bool has_prune_override{true};
    args.LockSettings([&](common::Settings& settings) {
        prune_prev = settings.rw_settings.at("prune-prev");
        has_prune_override = settings.rw_settings.count("prune") > 0;
    });
    QVERIFY(prune_prev.isStr());
    QCOMPARE(QString::fromStdString(prune_prev.get_str()), QString::number(QmlCoreSettings::PruneGBToMiB(10)));
    QVERIFY(!has_prune_override);
}

void OptionsModelTests::coreSettingsSessionPruneEnabledClearsPreviousValue()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["prune-prev"] = MakeInt(QmlCoreSettings::PruneGBToMiB(10));
    });

    QmlCoreSettings::Values values;
    values.prune = true;
    values.prune_size_gb = 10;
    QmlCoreSettings::Session session{values, QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())};
    session.markTouched(QStringLiteral("prune"));
    QVERIFY(session.writeTouchedToArgs(args));

    common::SettingsValue prune_setting;
    bool has_prune_prev{true};
    args.LockSettings([&](common::Settings& settings) {
        prune_setting = settings.rw_settings.at("prune");
        has_prune_prev = settings.rw_settings.count("prune-prev") > 0;
    });
    QVERIFY(prune_setting.isStr());
    QCOMPARE(QString::fromStdString(prune_setting.get_str()), QString::number(QmlCoreSettings::PruneGBToMiB(10)));
    QVERIFY(!has_prune_prev);
}

void OptionsModelTests::coreSettingsSessionProxyDisabledPreservesPreviousValue()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");

    QmlCoreSettings::Values values;
    values.proxy_enabled = false;
    values.proxy_address = QStringLiteral("10.0.0.2:9050");
    QmlCoreSettings::Session session{values, QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())};
    session.markTouched(QStringLiteral("proxy"));
    QVERIFY(session.writeTouchedToArgs(args));

    common::SettingsValue proxy_prev;
    bool has_proxy_override{true};
    args.LockSettings([&](common::Settings& settings) {
        proxy_prev = settings.rw_settings.at("proxy-prev");
        has_proxy_override = settings.rw_settings.count("proxy") > 0;
    });
    QCOMPARE(QString::fromStdString(proxy_prev.get_str()), QStringLiteral("10.0.0.2:9050"));
    QVERIFY(!has_proxy_override);
}

void OptionsModelTests::coreSettingsLoadPersistentPrunePreviousValue()
{
    MockNode node;
    node.SetPersistentSetting("prune-prev", MakeInt(QmlCoreSettings::PruneGBToMiB(10)));

    const QmlCoreSettings::Values values = QmlCoreSettings::LoadPersistentValues(node);
    QVERIFY(!values.prune);
    QCOMPARE(values.prune_size_gb, 10);
}

void OptionsModelTests::coreSettingsSessionPreviewRefreshPreservesTouchedValues()
{
    QmlCoreSettings::Values initial;
    initial.listen = true;
    initial.natpmp = false;
    QmlCoreSettings::Session session{initial};

    QVERIFY(session.setListen(false));

    QmlCoreSettings::Values preview;
    preview.listen = true;
    preview.natpmp = true;
    session.applyPreviewValuesPreservingTouched(preview, {});

    QVERIFY(!session.values().listen);
    QVERIFY(session.values().natpmp);
    QVERIFY(session.isTouched(QStringLiteral("listen")));
    QVERIFY(!session.isTouched(QStringLiteral("natpmp")));
}

void OptionsModelTests::coreSettingsSessionChangeReportsFieldDiffs()
{
    QmlCoreSettings::Session session;

    const QmlCoreSettings::Change change = session.changeListen(false);

    QVERIFY(change.accepted);
    QCOMPARE(change.setting_name, QStringLiteral("listen"));
    QVERIFY(QmlCoreSettings::ValuesChanged(change));
    QVERIFY(QmlCoreSettings::ListenChanged(change));
    QVERIFY(!QmlCoreSettings::NatpmpChanged(change));
    QVERIFY(!QmlCoreSettings::StatusesChanged(change));
    QVERIFY(session.isTouched(QStringLiteral("listen")));
}

void OptionsModelTests::coreSettingsSessionProxyCommitAcceptsUnchangedAddressWithoutValueChange()
{
    QmlCoreSettings::Session session;
    const QString address = session.defaultProxyAddress();

    const QmlCoreSettings::Change change = session.changeProxyLocation(address);

    QVERIFY(change.accepted);
    QCOMPARE(change.setting_name, QStringLiteral("proxy"));
    QVERIFY(!QmlCoreSettings::ValuesChanged(change));
    QVERIFY(!QmlCoreSettings::ProxyAddressChanged(change));
    QVERIFY(!session.isTouched(QStringLiteral("proxy")));
}

void OptionsModelTests::coreSettingStatusTracksSourcePrecedence()
{

    {
        ArgsManager args;
        args.SelectConfigNetwork("main");
        args.LockSettings([](common::Settings& settings) {
            settings.ro_config[""]["listen"].push_back(common::SettingsValue{true});
        });

        MockNode node;
        InstallPersistentSettings(node, args);

        QTemporaryDir model_settings_dir;
        QVERIFY(model_settings_dir.isValid());
        PrepareRuntimePersistence(node, args, model_settings_dir);
        OptionsQmlModel model(node, args);
        QTRY_VERIFY(model.settingsReady());
        QTRY_VERIFY(!model.settingsPending());
        const QVariantMap status = model.coreSettingStatus(QStringLiteral("listen"));
        QCOMPARE(status.value("source").toString(), QString("bitcoin_conf"));
        QCOMPARE(status.value("canEdit").toBool(), true);
        QCOMPARE(status.value("createsGuiOverride").toBool(), true);
        QVERIFY(status.value("infoText").toString().contains("settings.json"));
    }

    {
        ArgsManager args;
        args.SelectConfigNetwork("main");
        args.LockSettings([](common::Settings& settings) {
            settings.ro_config[""]["listen"].push_back(common::SettingsValue{true});
            settings.rw_settings["listen"] = common::SettingsValue{false};
        });

        MockNode node;
        InstallPersistentSettings(node, args);

        QTemporaryDir model_settings_dir;
        QVERIFY(model_settings_dir.isValid());
        PrepareRuntimePersistence(node, args, model_settings_dir);
        OptionsQmlModel model(node, args);
        QTRY_VERIFY(model.settingsReady());
        QTRY_VERIFY(!model.settingsPending());
        const QVariantMap status = model.coreSettingStatus(QStringLiteral("listen"));
        QCOMPARE(status.value("source").toString(), QString("settings_json"));
        QCOMPARE(status.value("createsGuiOverride").toBool(), false);
    }

    {
        ArgsManager args;
        args.SelectConfigNetwork("main");
        args.LockSettings([](common::Settings& settings) {
            settings.ro_config[""]["listen"].push_back(common::SettingsValue{true});
            settings.rw_settings["listen"] = common::SettingsValue{false};
            settings.command_line_options["listen"].push_back(common::SettingsValue{true});
        });

        MockNode node;
        InstallPersistentSettings(node, args);

        QTemporaryDir model_settings_dir;
        QVERIFY(model_settings_dir.isValid());
        PrepareRuntimePersistence(node, args, model_settings_dir);
        OptionsQmlModel model(node, args);
        QTRY_VERIFY(model.settingsReady());
        QTRY_VERIFY(!model.settingsPending());
        const QVariantMap status = model.coreSettingStatus(QStringLiteral("listen"));
        QCOMPARE(status.value("source").toString(), QString("command_line"));
        QCOMPARE(status.value("canEdit").toBool(), false);
        QCOMPARE(status.value("commandLineOverridden").toBool(), true);
        QVERIFY(status.value("infoText").toString().contains("-listen"));
    }
}

void OptionsModelTests::runtimeCoreSettingStatusesRefreshAfterWrite()
{

    ArgsManager args;
    args.SelectConfigNetwork("main");

    MockNode node;
    InstallPersistentSettings(node, args);
    InstallRwSettingsWriter(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.coreSettingStatus(QStringLiteral("maxmempool")).value("source").toString(), QString("default"));

    QSignalSpy status_spy(&model, &OptionsQmlModel::coreSettingStatusesChanged);
    model.setMaxMempoolSizeMB(456);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(status_spy.count() > 0);
    QCOMPARE(model.coreSettingStatus(QStringLiteral("maxmempool")).value("source").toString(), QString("settings_json"));
    QCOMPARE(model.coreSettingStatuses().value(QStringLiteral("maxmempool")).toMap().value("source").toString(), QString("settings_json"));
}

void OptionsModelTests::commandLineOverriddenSettingDoesNotWrite()
{

    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.command_line_options["listen"].push_back(common::SettingsValue{false});
        settings.ro_config[""]["listen"].push_back(common::SettingsValue{true});
    });

    MockNode node;
    InstallPersistentSettings(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.listen());
    QCOMPARE(model.coreSettingStatus(QStringLiteral("listen")).value("source").toString(), QString("command_line"));
    QCOMPARE(model.coreSettingStatus(QStringLiteral("listen")).value("canEdit").toBool(), false);

    model.setListen(true);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.listen());
    QVERIFY(!model.settingsPending());
    QVERIFY(!QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::runtimeCommandLineOverridesDisplayEffectiveValues()
{

    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["server"] = common::SettingsValue{false};
        settings.command_line_options["server"].push_back(common::SettingsValue{true});
        settings.rw_settings["prune"] = MakeInt(0);
        settings.command_line_options["prune"].push_back(MakeInt(QmlCoreSettings::PruneGBToMiB(10)));
        settings.rw_settings["proxy"] = common::SettingsValue{false};
        settings.command_line_options["proxy"].push_back(common::SettingsValue{std::string{"10.0.0.1:9050"}});
        settings.rw_settings["onion"] = common::SettingsValue{false};
        settings.command_line_options["onion"].push_back(common::SettingsValue{std::string{"127.0.0.1:9150"}});
        settings.rw_settings["dbcache"] = MakeInt(300);
        settings.command_line_options["dbcache"].push_back(MakeInt(600));
        settings.rw_settings["par"] = MakeInt(1);
        settings.command_line_options["par"].push_back(MakeInt(4));
        settings.rw_settings["maxmempool"] = MakeInt(300);
        settings.command_line_options["maxmempool"].push_back(MakeInt(500));
        settings.rw_settings["signer"] = common::SettingsValue{std::string{"saved-signer"}};
        settings.command_line_options["signer"].push_back(common::SettingsValue{std::string{"cli-signer"}});
    });

    MockNode node;
    InstallPersistentSettings(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.server());
    QVERIFY(model.prune());
    QCOMPARE(model.pruneSizeGB(), 10);
    QVERIFY(model.proxyEnabled());
    QCOMPARE(model.proxyAddress(), QString("10.0.0.1:9050"));
    QVERIFY(model.torEnabled());
    QCOMPARE(model.torAddress(), QString("127.0.0.1:9150"));
    QCOMPARE(model.dbcacheSizeMiB(), 600);
    QCOMPARE(model.scriptThreads(), 4);
    QCOMPARE(model.maxMempoolSizeMB(), 500);
    QCOMPARE(model.externalSignerPath(), QString("cli-signer"));

    QCOMPARE(model.coreSettingStatus(QStringLiteral("server")).value("source").toString(), QString("command_line"));
    QCOMPARE(model.coreSettingStatus(QStringLiteral("prune")).value("canEdit").toBool(), false);
    QCOMPARE(model.coreSettingStatus(QStringLiteral("proxy")).value("canEdit").toBool(), false);
    QCOMPARE(model.coreSettingStatus(QStringLiteral("dbcache")).value("canEdit").toBool(), false);
    QCOMPARE(model.coreSettingStatus(QStringLiteral("signer")).value("canEdit").toBool(), false);
}

void OptionsModelTests::runtimeParameterInteractionsDisplayEffectiveValues()
{

    std::vector<std::string> proxy_argv = TestArgv();
    proxy_argv.emplace_back("-proxy=127.0.0.1:9050");

    ArgsManager proxy_args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(proxy_args, proxy_argv, parse_error), parse_error.c_str());
    SelectParams(proxy_args.GetChainType());
    proxy_args.SelectConfigNetwork(proxy_args.GetChainTypeString());
    InitParameterInteraction(proxy_args);

    MockNode proxy_node;
    InstallPersistentSettings(proxy_node, proxy_args);
    InstallRwSettingsWriter(proxy_node, proxy_args);

    QTemporaryDir proxy_model_settings_dir;
    QVERIFY(proxy_model_settings_dir.isValid());
    PrepareRuntimePersistence(proxy_node, proxy_args, proxy_model_settings_dir);
    OptionsQmlModel proxy_model(proxy_node, proxy_args);
    QTRY_VERIFY(proxy_model.settingsReady());
    QTRY_VERIFY(!proxy_model.settingsPending());
    QVERIFY(!proxy_model.listen());
    QVERIFY(proxy_model.proxyEnabled());
    QCOMPARE(proxy_model.proxyAddress(), QStringLiteral("127.0.0.1:9050"));

    QVariantMap listen_status = proxy_model.coreSettingStatus(QStringLiteral("listen"));
    QCOMPARE(listen_status.value("source").toString(), QString("startup"));
    QCOMPARE(listen_status.value("canEdit").toBool(), true);
    QCOMPARE(listen_status.value("startupAdjusted").toBool(), true);
    QVERIFY(listen_status.value("infoText").toString().contains("settings.json"));

    auto* core_settings = qobject_cast<CoreSettingsModel*>(proxy_model.coreSettings());
    QVERIFY(core_settings);
    auto* listen_entry = qobject_cast<CoreSettingEntryModel*>(core_settings->entry(QStringLiteral("listen")));
    QVERIFY(listen_entry);
    QCOMPARE(listen_entry->status().value("hasRwSetting").toBool(), false);
    QSignalSpy listen_entry_status_spy(listen_entry, &CoreSettingEntryModel::statusChanged);

    proxy_model.setListen(true);
    QTRY_VERIFY(!proxy_model.settingsPending());
    QVERIFY(proxy_model.listen());
    QCOMPARE(SettingToBool(proxy_args.GetPersistentSetting("listen")), true);
    QCOMPARE(proxy_model.coreSettingStatus(QStringLiteral("listen")).value("hasRwSetting").toBool(), true);
    QCOMPARE(listen_entry->status().value("hasRwSetting").toBool(), true);
    QVERIFY(listen_entry_status_spy.count() >= 1);
    QVERIFY(QFile::exists(proxy_model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto enabled_write = ReadRwSetting(proxy_args, "listen");
    QVERIFY(enabled_write.isBool());
    QVERIFY(enabled_write.get_bool());

    proxy_model.setListen(false);
    QTRY_VERIFY(!proxy_model.settingsPending());
    QVERIFY(!proxy_model.listen());
    QVERIFY(QFile::exists(proxy_model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto disabled_write = ReadRwSetting(proxy_args, "listen");
    QVERIFY(disabled_write.isNull());

    bool has_listen_override{true};
    proxy_args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
    });
    QVERIFY(!has_listen_override);

    std::vector<std::string> blocksonly_argv = TestArgv();
    blocksonly_argv.emplace_back("-blocksonly");

    ArgsManager blocksonly_args;
    QVERIFY2(PrepareTestArgs(blocksonly_args, blocksonly_argv, parse_error), parse_error.c_str());
    SelectParams(blocksonly_args.GetChainType());
    blocksonly_args.SelectConfigNetwork(blocksonly_args.GetChainTypeString());
    InitParameterInteraction(blocksonly_args);

    MockNode blocksonly_node;
    InstallPersistentSettings(blocksonly_node, blocksonly_args);

    QTemporaryDir blocksonly_model_settings_dir;
    QVERIFY(blocksonly_model_settings_dir.isValid());
    PrepareRuntimePersistence(blocksonly_node, blocksonly_args, blocksonly_model_settings_dir);
    OptionsQmlModel blocksonly_model(blocksonly_node, blocksonly_args);
    QTRY_VERIFY(blocksonly_model.settingsReady());
    QTRY_VERIFY(!blocksonly_model.settingsPending());
    QCOMPARE(blocksonly_model.maxMempoolSizeMB(), static_cast<int>(DEFAULT_BLOCKSONLY_MAX_MEMPOOL_SIZE_MB));

    QVariantMap mempool_status = blocksonly_model.coreSettingStatus(QStringLiteral("maxmempool"));
    QCOMPARE(mempool_status.value("source").toString(), QString("startup"));
    QCOMPARE(mempool_status.value("canEdit").toBool(), true);
    QCOMPARE(mempool_status.value("startupAdjusted").toBool(), true);
}

void OptionsModelTests::commandLineOverriddenSettingsPreservePersistentValues()
{

    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["server"] = common::SettingsValue{false};
        settings.command_line_options["server"].push_back(common::SettingsValue{true});
        settings.rw_settings["prune"] = MakeInt(0);
        settings.command_line_options["prune"].push_back(MakeInt(QmlCoreSettings::PruneGBToMiB(10)));
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"127.0.0.1:9050"}};
        settings.command_line_options["proxy"].push_back(common::SettingsValue{std::string{"10.0.0.1:9050"}});
        settings.rw_settings["dbcache"] = MakeInt(300);
        settings.command_line_options["dbcache"].push_back(MakeInt(600));
        settings.rw_settings["par"] = MakeInt(1);
        settings.command_line_options["par"].push_back(MakeInt(4));
        settings.rw_settings["maxmempool"] = MakeInt(300);
        settings.command_line_options["maxmempool"].push_back(MakeInt(500));
        settings.rw_settings["signer"] = common::SettingsValue{std::string{"saved-signer"}};
        settings.command_line_options["signer"].push_back(common::SettingsValue{std::string{"cli-signer"}});
    });

    MockNode node;
    InstallPersistentSettings(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());

    node.force_setting_arguments.clear();

    model.setServer(false);
    QTRY_VERIFY(!model.settingsPending());
    model.setPrune(false);
    QTRY_VERIFY(!model.settingsPending());
    model.setProxyEnabled(false);
    QTRY_VERIFY(!model.settingsPending());
    model.setDbcacheSizeMiB(700);
    QTRY_VERIFY(!model.settingsPending());
    model.setScriptThreads(8);
    QTRY_VERIFY(!model.settingsPending());
    model.setMaxMempoolSizeMB(700);
    QTRY_VERIFY(!model.settingsPending());
    model.setExternalSignerPath(QStringLiteral("changed-signer"));
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(SettingToBool(args.GetPersistentSetting("server")), false);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("prune"), -1), 0);
    QCOMPARE(QString::fromStdString(SettingToString(args.GetPersistentSetting("proxy"), "")), QString("127.0.0.1:9050"));
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("dbcache"), -1), 300);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("par"), -1), 1);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("maxmempool"), -1), 300);
    QCOMPARE(QString::fromStdString(SettingToString(args.GetPersistentSetting("signer"), "")), QString("saved-signer"));
    QVERIFY(!model.settingsPending());
    QVERIFY(node.force_setting_arguments.empty());
    QVERIFY(!QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::revertingToConfigValueDeletesRwOverride()
{

    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["listen"].push_back(common::SettingsValue{true});
        settings.rw_settings["listen"] = common::SettingsValue{false};
    });

    MockNode node;
    InstallPersistentSettings(node, args);
    InstallRwSettingsWriter(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.listen());

    model.setListen(true);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto listen_write = ReadRwSetting(args, "listen");
    QVERIFY(listen_write.isNull());
}

void OptionsModelTests::revertingToDefaultValueDeletesRwOverride()
{

    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["maxmempool"] = common::SettingsValue{456};
    });

    MockNode node;
    InstallPersistentSettings(node, args);
    InstallRwSettingsWriter(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.maxMempoolSizeMB(), 456);

    model.setMaxMempoolSizeMB(DEFAULT_MAX_MEMPOOL_SIZE_MB);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    const auto mempool_write = ReadRwSetting(args, "maxmempool");
    QVERIFY(mempool_write.isNull());
}

void OptionsModelTests::languageCommandLineOverrideDoesNotPersist()
{

    QSettings settings;
    const bool had_language_setting = settings.contains(SettingsKeys::LANGUAGE);
    const QVariant previous_language_setting = settings.value(SettingsKeys::LANGUAGE);
    settings.setValue(SettingsKeys::LANGUAGE, QStringLiteral("de"));

    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.command_line_options["lang"].push_back(common::SettingsValue{std::string{"fr"}});
    });

    MockNode node;
    InstallPersistentSettings(node, args);

    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.language(), QString("fr"));
    QCOMPARE(model.coreSettingStatus(QStringLiteral("lang")).value("canEdit").toBool(), false);

    model.setLanguage(QStringLiteral("es"));
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.language(), QString("fr"));
    QCOMPARE(settings.value(SettingsKeys::LANGUAGE).toString(), QString("de"));

    if (had_language_setting) {
        settings.setValue(SettingsKeys::LANGUAGE, previous_language_setting);
    } else {
        settings.remove(SettingsKeys::LANGUAGE);
    }
}

void OptionsModelTests::legacyQtSettingsMigrateToCoreSettings()
{

    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    QSettings& settings = legacy_settings.settings();
    settings.setValue(QStringLiteral("nDatabaseCache"), 600);
    settings.setValue(QStringLiteral("nThreadsScriptVerif"), 12);
    settings.setValue(QStringLiteral("fListen"), false);
    settings.setValue(QStringLiteral("fUseNatpmp"), true);
    settings.setValue(QStringLiteral("server"), true);
    settings.setValue(QStringLiteral("bPrune"), true);
    settings.setValue(QStringLiteral("nPruneSize"), 10);
    settings.setValue(QStringLiteral("fUseProxy"), true);
    settings.setValue(QStringLiteral("addrProxy"), QStringLiteral("10.0.0.1:9050"));
    settings.setValue(QStringLiteral("fUseSeparateProxyTor"), false);
    settings.setValue(QStringLiteral("addrSeparateProxyTor"), QStringLiteral("10.0.0.2:9150"));
    settings.setValue(QStringLiteral("language"), QStringLiteral("de"));

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    ArgsManager args;
    PrepareArgsForDataDir(args, data_dir.path());

    const QmlLegacySettings::MigrationResult result{
        QmlLegacySettings::MigrateCoreSettings(args, QmlLegacySettings::MigrationMode::Persist)
    };
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(result.settings_changed);

    common::SettingsValue dbcache;
    common::SettingsValue script_threads;
    common::SettingsValue listen;
    common::SettingsValue server;
    common::SettingsValue prune;
    common::SettingsValue proxy;
    common::SettingsValue onion_prev;
    common::SettingsValue language;
    bool has_natpmp{true};
    bool has_onion{true};
    bool has_prune_prev{true};
    args.LockSettings([&](common::Settings& core_settings) {
        dbcache = core_settings.rw_settings.at("dbcache");
        script_threads = core_settings.rw_settings.at("par");
        listen = core_settings.rw_settings.at("listen");
        server = core_settings.rw_settings.at("server");
        prune = core_settings.rw_settings.at("prune");
        proxy = core_settings.rw_settings.at("proxy");
        onion_prev = core_settings.rw_settings.at("onion-prev");
        language = core_settings.rw_settings.at("lang");
        has_natpmp = core_settings.rw_settings.count("natpmp") > 0;
        has_onion = core_settings.rw_settings.count("onion") > 0;
        has_prune_prev = core_settings.rw_settings.count("prune-prev") > 0;
    });

    QVERIFY(dbcache.isStr());
    QCOMPARE(QString::fromStdString(dbcache.get_str()), QStringLiteral("600"));
    QVERIFY(script_threads.isStr());
    QCOMPARE(QString::fromStdString(script_threads.get_str()), QStringLiteral("12"));
    QCOMPARE(SettingToBool(listen), false);
    QVERIFY(!has_natpmp);
    QCOMPARE(SettingToBool(server), true);
    QVERIFY(prune.isStr());
    QCOMPARE(QString::fromStdString(prune.get_str()), QString::number(QmlCoreSettings::PruneGBToMiB(10)));
    QCOMPARE(QString::fromStdString(proxy.get_str()), QStringLiteral("10.0.0.1:9050"));
    QCOMPARE(QString::fromStdString(onion_prev.get_str()), QStringLiteral("10.0.0.2:9150"));
    QCOMPARE(QString::fromStdString(language.get_str()), QStringLiteral("de"));
    QVERIFY(!has_onion);
    QVERIFY(!has_prune_prev);

    QVERIFY(!settings.contains(QStringLiteral("nDatabaseCache")));
    QVERIFY(!settings.contains(QStringLiteral("nThreadsScriptVerif")));
    QVERIFY(!settings.contains(QStringLiteral("fListen")));
    QVERIFY(!settings.contains(QStringLiteral("fUseNatpmp")));
    QVERIFY(!settings.contains(QStringLiteral("server")));
    QVERIFY(!settings.contains(QStringLiteral("bPrune")));
    QVERIFY(!settings.contains(QStringLiteral("nPruneSize")));
    QVERIFY(!settings.contains(QStringLiteral("fUseProxy")));
    QVERIFY(!settings.contains(QStringLiteral("addrProxy")));
    QVERIFY(!settings.contains(QStringLiteral("fUseSeparateProxyTor")));
    QVERIFY(!settings.contains(QStringLiteral("addrSeparateProxyTor")));
    QVERIFY(!settings.contains(QStringLiteral("language")));

    MockNode node;
    InstallPersistentSettings(node, args);
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, args, model_settings_dir);
    OptionsQmlModel model(node, args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.language(), QStringLiteral("de"));
}

void OptionsModelTests::legacyQtSettingsPreviewDoesNotRemoveQSettings()
{
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    QSettings& settings = legacy_settings.settings();
    settings.setValue(QStringLiteral("fListen"), false);
    settings.setValue(QStringLiteral("bPrune"), true);
    settings.setValue(QStringLiteral("nPruneSize"), 10);

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    const QmlOnboardingSettings::PreviewResult preview{
        QmlOnboardingSettings::Preview(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
    };

    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.values.listen);
    QVERIFY(preview.values.prune);
    QCOMPARE(preview.values.prune_size_gb, 10);
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("listen")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("settings_json"));
    QCOMPARE(preview.core_setting_statuses.value(QStringLiteral("prune")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("settings_json"));
    QVERIFY(settings.contains(QStringLiteral("fListen")));
    QVERIFY(settings.contains(QStringLiteral("bPrune")));
    QVERIFY(settings.contains(QStringLiteral("nPruneSize")));
}

void OptionsModelTests::legacyQtSettingsCommandLineOverrideStillMigratesPersistentValue()
{
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    legacy_settings.settings().setValue(QStringLiteral("fListen"), false);

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    std::vector<std::string> argv = TestArgvWithDataDir(data_dir.path());
    argv.emplace_back("-listen=1");
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    SelectParams(args.GetChainType());
    args.SelectConfigNetwork(args.GetChainTypeString());

    const QmlLegacySettings::MigrationResult result{
        QmlLegacySettings::MigrateCoreSettings(args, QmlLegacySettings::MigrationMode::Persist)
    };
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(result.settings_changed);

    QCOMPARE(SettingToBool(args.GetPersistentSetting("listen")), false);
    QCOMPARE(args.GetBoolArg("-listen", false), true);
    QVERIFY(!legacy_settings.settings().contains(QStringLiteral("fListen")));
}

void OptionsModelTests::legacyQtSettingsBitcoinConfBlocksMigration()
{
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    legacy_settings.settings().setValue(QStringLiteral("fListen"), false);

    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("regtest=1\n[regtest]\nlisten=1\n") > 0);
    conf.close();

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgvWithDataDir(data_dir.path()), parse_error), parse_error.c_str());
    std::string config_error;
    QVERIFY2(args.ReadConfigFiles(config_error, true), config_error.c_str());
    SelectParams(args.GetChainType());
    args.SelectConfigNetwork(args.GetChainTypeString());

    const QmlLegacySettings::MigrationResult result{
        QmlLegacySettings::MigrateCoreSettings(args, QmlLegacySettings::MigrationMode::Persist)
    };
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    QVERIFY(!result.settings_changed);

    bool has_listen_override{true};
    args.LockSettings([&](common::Settings& core_settings) {
        has_listen_override = core_settings.rw_settings.count("listen") > 0;
    });
    QVERIFY(!has_listen_override);
    QVERIFY(!legacy_settings.settings().contains(QStringLiteral("fListen")));
}

void OptionsModelTests::onboardingApplyMigratesLegacySettingsBeforeTouchedOverrides()
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
    QTRY_VERIFY(!model.validationPending());
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QTRY_VERIFY(!model.validationPending());
    QVERIFY(!model.listen());
    QVERIFY(model.server());

    model.setListen(true);
    QVERIFY(model.listen());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    QString apply_error;
    QVERIFY2(model.applyToArgs(args, &apply_error), qPrintable(apply_error));

    bool has_listen_override{true};
    common::SettingsValue server;
    args.LockSettings([&](common::Settings& core_settings) {
        has_listen_override = core_settings.rw_settings.count("listen") > 0;
        server = core_settings.rw_settings.at("server");
    });
    QVERIFY(!has_listen_override);
    QCOMPARE(SettingToBool(server), true);
    QVERIFY(!legacy_settings.settings().contains(QStringLiteral("fListen")));
    QVERIFY(!legacy_settings.settings().contains(QStringLiteral("server")));
}

void OptionsModelTests::onboardingPreviewHelperReadsSelectedDatadirConfig()
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

void OptionsModelTests::onboardingPreviewReadsSelectedDatadirConfig()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("listen=0\n") > 0);
    conf.close();

    OnboardingOptionsModel model(TestArgv(), /*can_listen_ipc=*/false);
    QTRY_VERIFY(!model.validationPending());
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QTRY_VERIFY(!model.validationPending());
    QCOMPARE(model.previewError(), QString{});
    QCOMPARE(model.coreSettingStatuses().value(QStringLiteral("listen")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));
    QVERIFY(!model.listen());
}

void OptionsModelTests::onboardingApplyDoesNotCopyUntouchedConfig()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QFile conf(data_dir.filePath(QStringLiteral("bitcoin.conf")));
    QVERIFY(conf.open(QIODevice::WriteOnly | QIODevice::Text));
    QVERIFY(conf.write("listen=0\n") > 0);
    conf.close();

    const std::vector<std::string> argv = TestArgv();
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    QTRY_VERIFY(!model.validationPending());
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QTRY_VERIFY(!model.validationPending());
    QVERIFY(!model.listen());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    QString apply_error;
    QVERIFY2(model.applyToArgs(args, &apply_error), qPrintable(apply_error));

    bool has_listen_override{false};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.count("listen") > 0;
    });
    QVERIFY(!has_listen_override);
}

void OptionsModelTests::onboardingApplyWritesTouchedConfigOverride()
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
    QString apply_error;
    QVERIFY2(QmlOnboardingSettings::ApplyToArgs(args, data_dir.path(), QSet<QString>{QStringLiteral("listen")}, preview.values, &apply_error), qPrintable(apply_error));

    common::SettingsValue listen_override;
    args.LockSettings([&](common::Settings& settings) {
        if (const auto* value = common::FindKey(settings.rw_settings, "listen")) {
            listen_override = *value;
        }
    });
    QVERIFY(listen_override.isBool());
    QVERIFY(listen_override.get_bool());
}

void OptionsModelTests::onboardingApplyWritesTouchedParameterInteractionOverride()
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
    QString apply_error;
    QVERIFY2(QmlOnboardingSettings::ApplyToArgs(args, data_dir.path(), QSet<QString>{QStringLiteral("listen")}, preview.values, &apply_error), qPrintable(apply_error));

    common::SettingsValue listen_override;
    bool has_forced_listen{true};
    bool has_forced_natpmp{true};
    bool has_forced_discover{true};
    args.LockSettings([&](common::Settings& settings) {
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

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OptionsModelTests)
#else
QTEST_MAIN(OptionsModelTests)
#endif
#include "test_options_model.moc"
