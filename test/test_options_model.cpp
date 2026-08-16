// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <test/mocks/mocknode.h>
#include <chainparams.h>
#include <qml/core_settings.h>
#include <qml/datadir.h>
#include <qml/guiargs.h>
#include <qml/guiconstants.h>
#include <qml/legacy_settings_migration.h>
#include <qml/models/core_settings_model.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/models/options_model.h>
#include <qml/models/settings_keys.h>
#include <qml/onboarding_settings.h>
#include <common/init.h>
#include <init.h>
#include <net_processing.h>
#include <common/args.h>
#include <common/settings.h>
#include <util/translation.h>

#include <array>
#include <memory>

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

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
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
    void guiDataDirChooserShowsForUnreadableConfiguredDir();
    void unreadableConfigDoesNotEscapeStartupResolution();
    void invalidExplicitDataDirReturnsErrorWithoutReadingProfile();
    void validRelativeExplicitDataDirResolvesAgainstWorkingDirectory();
    void resetGuiSettingsClearsAndBacksUpQSettings();
    void resetGuiSettingsClearsLegacyQtSettings();
    void resetGuiSettingsClearsAndBacksUpSettingsJson();
    void resetGuiSettingsHonorsFinalSourcePrecedence();
    void resetGuiSettingsAllowsUnreadableSettingsProfile();
    void resetLegacyCleanupRollsBackOnWriteFailure();
    void resetGuiSettingsPreviewIgnoresSelectedCustomDataDirSettingsJson();
    void resetGuiSettingsApplyClearsSelectedCustomDataDirSettingsJson();
    void resetGuiSettingsPreservesCommandLineOverrides();
    void resetGuiSettingsPreservesBitcoinConfOverrides();
    void qmlOnboardedProfileSkipsPreInitOnboarding();
    void qmlOnboardedCommandLineOverrideShowsPreInitOnboarding();
    void qmlOnboardedConfiguredDatadirProfileSkipsPreInitOnboarding();
    void configuredDatadirPreviewKeepsConfigSource();
    void configuredDatadirApplyDoesNotPersistGuiDataDir();
    void guiDatadirTakesPrecedenceOverConfigDatadir();
    void guiDatadirConfigUserSelectionPersistsNewPath();
    void explicitDatadirApplyDoesNotPersistGuiDataDir();
    void resetGuiSettingsPreservesSavedDatadirOverConfigDatadir();
    void qmlOnboardedResetGuiSettingsShowsPreInitOnboarding();
    void qmlOnboardedChooseDataDirShowsPreInitOnboarding();
    void qmlOnboardedCurrentResetFlagShowsPreInitOnboarding();
    void qmlOnboardedResolvedNetworkResetFlagShowsPreInitOnboarding();
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
    void onboardingApplyCreatesNewCustomDataDir();
    void onboardingApplyPreservesExistingNetworkWalletDiscovery();
    void onboardingPreviewAppliesParameterInteractions();
    void onboardingStorageCheckUsesResolvedDataDir();
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
    void onboardingApplyRollsBackWhenLegacyCleanupFails();
    void onboardingApplyRollsBackWhenSettingsWriteFails();
    void onboardingApplyWithSettingsDisabledPreservesLegacyCoreValues();
    void onboardingPreviewHelperReadsSelectedDatadirConfig();
    void onboardingPreviewReadsSelectedDatadirConfig();
    void onboardingApplyWritesTouchedConfigOverride();
    void onboardingApplyWritesTouchedParameterInteractionOverride();
    void onboardingApplyRetainsListenChoiceWhenDisablingProxy();
    void onboardingApplyRejectsProfileDrift();
    void onboardingFinalizeIgnoresUnusedBootstrapStore();
    void onboardingApplyClearsResetFlagInBootstrapAndActiveStores();

private:
    std::unique_ptr<QTemporaryDir> m_qsettings_dir;
    QSettings::Format m_previous_settings_format{QSettings::NativeFormat};
    QString m_previous_organization_name;
    QString m_previous_organization_domain;
    QString m_previous_application_name;
};

void OptionsModelTests::initTestCase()
{
    m_previous_settings_format = QSettings::defaultFormat();
    m_previous_organization_name = QCoreApplication::organizationName();
    m_previous_organization_domain = QCoreApplication::organizationDomain();
    m_previous_application_name = QCoreApplication::applicationName();

    m_qsettings_dir = std::make_unique<QTemporaryDir>();
    QVERIFY(m_qsettings_dir->isValid());
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_qsettings_dir->path());
    QCoreApplication::setOrganizationName(QStringLiteral("BitcoinCoreAppTest"));
    QCoreApplication::setOrganizationDomain({});
    QCoreApplication::setApplicationName(QStringLiteral("OptionsModelTests"));
}

void OptionsModelTests::cleanupTestCase()
{
    QSettings settings;
    settings.setFallbacksEnabled(false);
    settings.clear();
    settings.sync();

    QCoreApplication::setOrganizationName(m_previous_organization_name);
    QCoreApplication::setOrganizationDomain(m_previous_organization_domain);
    QCoreApplication::setApplicationName(m_previous_application_name);
    QSettings::setDefaultFormat(m_previous_settings_format);
    m_qsettings_dir.reset();
}

// Convenience helpers for persistent setting values used by the node double.
static common::SettingsValue MakeAddress(const std::string& addr)
{
    return common::SettingsValue{addr};
}

static common::SettingsValue MakeInt(int value)
{
    return common::SettingsValue{value};
}

static int SettingWriteCount(const MockNode& node, const std::string& name)
{
    return std::count_if(node.update_rw_setting_arguments.begin(), node.update_rw_setting_arguments.end(), [&](const auto& write) {
        return write.first == name;
    });
}

static const common::SettingsValue* FindSettingWrite(const MockNode& node, const std::string& name)
{
    const auto it{std::find_if(node.update_rw_setting_arguments.rbegin(), node.update_rw_setting_arguments.rend(), [&](const auto& write) {
        return write.first == name;
    })};
    return it == node.update_rw_setting_arguments.rend() ? nullptr : &it->second;
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
        settings.setFallbacksEnabled(false);
        for (const QString& key : settings.allKeys()) {
            m_values.insert(key, settings.value(key));
        }
    }

    ~SavedGuiDataDirSettings()
    {
        QSettings settings;
        settings.setFallbacksEnabled(false);
        settings.clear();
        for (auto it = m_values.cbegin(); it != m_values.cend(); ++it) {
            settings.setValue(it.key(), it.value());
        }
        settings.sync();
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

static void ReadSettingsForDataDir(ArgsManager& args, const QString& data_dir)
{
    PrepareArgsForDataDir(args, data_dir);
    std::vector<std::string> settings_errors;
    QVERIFY2(args.ReadSettingsFile(&settings_errors), settings_errors.empty() ? "" : settings_errors.front().c_str());
}

static void InitializeAndFinalizeSettings(
    ArgsManager& args,
    const QmlOnboardingSettings::GuiSettingsStore& bootstrap_gui_settings,
    const QmlOnboardingSettings::PendingApply* pending = nullptr,
    QmlOnboardingSettings::FinalizeResult* result = nullptr)
{
    const std::optional<common::ConfigError> init_error{
        common::InitConfig(args, [](const bilingual_str&, const std::vector<std::string>&) {
            return true;
        })
    };
    QVERIFY2(!init_error, init_error ? init_error->message.original.c_str() : "");
    args.SelectConfigNetwork(args.GetChainTypeString());

    QString finalize_error;
    QVERIFY2(
        QmlOnboardingSettings::FinalizeStartupSettings(
            args,
            bootstrap_gui_settings,
            pending,
            result,
            &finalize_error),
        qPrintable(finalize_error));
}

static void PrepareAndFinalizeModelApply(OnboardingOptionsModel& model, ArgsManager& args)
{
    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QmlOnboardingSettings::PendingApply pending;
    QString apply_error;
    QVERIFY2(model.prepareApplyToArgs(args, pending, &apply_error), qPrintable(apply_error));
    InitializeAndFinalizeSettings(args, bootstrap_gui_settings, &pending);
}

static void PrepareAndFinalizeApply(
    ArgsManager& args,
    const QmlOnboardingSettings::DataDirSelection& data_dir,
    const QString& resolved_data_dir,
    const QSet<QString>& touched_settings,
    const QmlCoreSettings::Values& values)
{
    const QmlOnboardingSettings::GuiSettingsStore bootstrap_gui_settings{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    QmlOnboardingSettings::PendingApply pending;
    QString apply_error;
    QVERIFY2(
        QmlOnboardingSettings::PrepareApplyToArgs(
            args,
            data_dir,
            resolved_data_dir,
            touched_settings,
            values,
            /*effective_reset=*/false,
            pending,
            &apply_error),
        qPrintable(apply_error));
    InitializeAndFinalizeSettings(args, bootstrap_gui_settings, &pending);
}

void OptionsModelTests::proxyDisabledRemovesKey()
{
    MockNode node;
    // Simulate a previously-saved proxy address so that m_proxy_enabled=true on construction.
    node.SetPersistentSetting("proxy", MakeAddress("127.0.0.1:9050"));

    OptionsQmlModel model(node);
    QVERIFY(model.proxyEnabled());

    // When proxy is disabled, updateRwSetting must be called with a null (not
    // empty-string) SettingsValue so that the key is erased from settings.json.
    model.setProxyEnabled(false);
    QVERIFY(!model.proxyEnabled());
    QCOMPARE(node.update_rw_setting_arguments.size(), 2U);
    QCOMPARE(SettingWriteCount(node, "proxy-prev"), 1);
    const auto* proxy_write{FindSettingWrite(node, "proxy")};
    QVERIFY(proxy_write != nullptr);
    QVERIFY(proxy_write->isNull());
}

void OptionsModelTests::torDisabledRemovesKey()
{
    MockNode node;
    node.SetPersistentSetting("onion", MakeAddress("127.0.0.1:9150"));

    OptionsQmlModel model(node);
    QVERIFY(model.torEnabled());

    model.setTorEnabled(false);
    QVERIFY(!model.torEnabled());
    QCOMPARE(node.update_rw_setting_arguments.size(), 2U);
    QCOMPARE(SettingWriteCount(node, "onion-prev"), 1);
    const auto* onion_write{FindSettingWrite(node, "onion")};
    QVERIFY(onion_write != nullptr);
    QVERIFY(onion_write->isNull());
}

void OptionsModelTests::proxyEnabledWritesAddress()
{
    MockNode node;

    // Construct with no saved proxy — m_proxy_enabled=false, m_proxy_address="".
    OptionsQmlModel model(node);
    QVERIFY(!model.proxyEnabled());

    // Pre-load an address into the model (as QML does before toggling the switch).
    model.setProxyAddress("10.0.0.1:9050");
    node.update_rw_setting_arguments.clear();

    // Enabling proxy must write the address string to settings.
    model.setProxyEnabled(true);
    QVERIFY(model.proxyEnabled());
    QCOMPARE(node.update_rw_setting_arguments.size(), 2U);
    const auto* proxy_write{FindSettingWrite(node, "proxy")};
    QVERIFY(proxy_write != nullptr);
    QVERIFY(proxy_write->isStr());
    QCOMPARE(proxy_write->get_str(), std::string{"10.0.0.1:9050"});
    const auto* previous_write{FindSettingWrite(node, "proxy-prev")};
    QVERIFY(previous_write != nullptr);
    QVERIFY(previous_write->isNull());
}

void OptionsModelTests::proxyDirtySetAtRuntime()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(!model.proxySettingsDirty());

    model.setProxyEnabled(true);
    QVERIFY(model.proxySettingsDirty());
}

void OptionsModelTests::proxyDirtyResetWhenReverted()
{
    MockNode node;

    // Start onboarded with proxy disabled (no saved proxy).
    OptionsQmlModel model(node);
    QVERIFY(!model.proxySettingsDirty());

    // Simulate what ProxySettings.qml does: set address before enabling.
    model.setProxyAddress("127.0.0.1:9050");
    // Address changed but proxy is still disabled — should NOT be dirty since
    // the address is irrelevant when proxy is off.
    QVERIFY(!model.proxySettingsDirty());

    // Enable proxy — now dirty (enabled differs from initial disabled).
    model.setProxyEnabled(true);
    QVERIFY(model.proxySettingsDirty());

    // Revert enable state — dirty should clear even though address is populated,
    // because the address is ignored when proxy is disabled.
    model.setProxyEnabled(false);
    QVERIFY(!model.proxySettingsDirty());
}

void OptionsModelTests::mempoolSizeLoadedFromSettings()
{
    MockNode node;
    node.SetPersistentSetting("maxmempool", MakeInt(456));

    OptionsQmlModel model(node);
    QCOMPARE(model.maxMempoolSizeMB(), 456);
}

void OptionsModelTests::mempoolSizeWritesSetting()
{
    MockNode node;

    OptionsQmlModel model(node);

    model.setMaxMempoolSizeMB(456);
    QCOMPARE(model.maxMempoolSizeMB(), 456);
    QCOMPARE(node.update_rw_setting_arguments.size(), 1U);
    const auto* mempool_write{FindSettingWrite(node, "maxmempool")};
    QVERIFY(mempool_write != nullptr);
    QVERIFY(mempool_write->isNum());
    QCOMPARE(mempool_write->getInt<int64_t>(), int64_t{456});
}

void OptionsModelTests::mempoolSizeDoesNotRewriteUnchangedSetting()
{
    MockNode node;
    node.SetPersistentSetting("maxmempool", MakeInt(456));

    OptionsQmlModel model(node);

    model.setMaxMempoolSizeMB(456);
    QCOMPARE(model.maxMempoolSizeMB(), 456);
    QCOMPARE(SettingWriteCount(node, "maxmempool"), 0);
}

void OptionsModelTests::legacyNumericSettingsWriteStrings()
{
    MockNode node;

    OptionsQmlModel model(node);

    model.setDbcacheSizeMiB(600);
    model.setScriptThreads(12);
    QCOMPARE(node.update_rw_setting_arguments.size(), 2U);
    const auto* dbcache_write{FindSettingWrite(node, "dbcache")};
    const auto* par_write{FindSettingWrite(node, "par")};
    QVERIFY(dbcache_write != nullptr && dbcache_write->isStr());
    QVERIFY(par_write != nullptr && par_write->isStr());
    QCOMPARE(dbcache_write->get_str(), std::string{"600"});
    QCOMPARE(par_write->get_str(), std::string{"12"});
}

void OptionsModelTests::externalSignerPathWritesSigner()
{
    MockNode node;

    OptionsQmlModel model(node);

    model.setExternalSignerPath("/usr/local/bin/hwi");
    QCOMPARE(model.externalSignerPath(), QString("/usr/local/bin/hwi"));
    const auto* signer_write{FindSettingWrite(node, "signer")};
    QVERIFY(signer_write != nullptr && signer_write->isStr());
    QCOMPARE(signer_write->get_str(), std::string{"/usr/local/bin/hwi"});
    QCOMPARE(node.update_rw_setting_arguments.size(), 1U);
}

void OptionsModelTests::externalSignerPathClearedRemovesKey()
{
    MockNode node;
    node.SetPersistentSetting("signer", MakeAddress("/usr/local/bin/hwi"));

    OptionsQmlModel model(node);
    QCOMPARE(model.externalSignerPath(), QString("/usr/local/bin/hwi"));

    model.setExternalSignerPath("");
    QVERIFY(model.externalSignerPath().isEmpty());
    const auto* signer_write{FindSettingWrite(node, "signer")};
    QVERIFY(signer_write != nullptr);
    QVERIFY(signer_write->isNull());
    QCOMPARE(node.update_rw_setting_arguments.size(), 1U);
}

void OptionsModelTests::walletSettingsDirtyTracksExternalSignerPath()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("/usr/local/bin/hwi");
    QVERIFY(model.walletSettingsDirty());

    model.setExternalSignerPath("");
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::signerPathLoadedFromSettings()
{
    MockNode node;
    node.SetPersistentSetting("signer", MakeAddress("/opt/hwi/ledger.py"));

    OptionsQmlModel model(node);
    QCOMPARE(model.externalSignerPath(), QString("/opt/hwi/ledger.py"));
}

void OptionsModelTests::signerPathWritesSetting()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(model.externalSignerPath().isEmpty());

    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QCOMPARE(model.externalSignerPath(), QString("/opt/hwi/ledger.py"));
    const auto* signer_write{FindSettingWrite(node, "signer")};
    QVERIFY(signer_write != nullptr && signer_write->isStr());
    QCOMPARE(signer_write->get_str(), std::string{"/opt/hwi/ledger.py"});
    QCOMPARE(node.update_rw_setting_arguments.size(), 1U);
}

void OptionsModelTests::signerDirtySetAtRuntime()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QVERIFY(model.walletSettingsDirty());
}

void OptionsModelTests::signerDirtyResetWhenReverted()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath("/opt/hwi/ledger.py");
    QVERIFY(model.walletSettingsDirty());

    model.setExternalSignerPath("");
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::externalSignerPathValidationRejectsMissingPath()
{
    MockNode node;

    OptionsQmlModel model(node);
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

    OptionsQmlModel model(node);
    QVERIFY(model.externalSignerPathValidationError(script_path).isEmpty());
}

void OptionsModelTests::connectionDirtyTracksRestartSettings()
{
    MockNode node;
    node.SetPersistentSetting("listen", common::SettingsValue{true});

    OptionsQmlModel model(node);
    QVERIFY(!model.connectionSettingsDirty());
    QVERIFY(!model.restartRequired());

    model.setListen(false);
    QVERIFY(model.connectionSettingsDirty());
    QVERIFY(model.restartRequired());

    model.setListen(true);
    QVERIFY(!model.connectionSettingsDirty());
    QVERIFY(!model.restartRequired());
}

void OptionsModelTests::natpmpAppliesLiveWithoutRestartDirty()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(model.natpmp());
    std::vector<QString> events;
    node.update_rw_setting_fn = [&](const std::string& name, const common::SettingsValue& value) {
        if (name == "natpmp") {
            const auto parsed{SettingToBool(value)};
            events.push_back(value.isNull() ? QStringLiteral("write:null") : parsed && *parsed ? QStringLiteral("write:true") :
                                                                                                 QStringLiteral("write:false"));
        }
    };
    node.map_port_fn = [&](bool enabled) {
        events.push_back(enabled ? QStringLiteral("map:true") : QStringLiteral("map:false"));
    };

    model.setNatpmp(false);
    QVERIFY(!model.natpmp());
    QVERIFY(!model.connectionSettingsDirty());
    QVERIFY(!model.restartRequired());
    QTest::qWait(300);

    model.setNatpmp(true);
    QVERIFY(model.natpmp());
    QVERIFY(!model.connectionSettingsDirty());
    QVERIFY(!model.restartRequired());
    QTest::qWait(300);
    QCOMPARE(events.size(), 4U);
    QCOMPARE(events.at(0), QStringLiteral("write:false"));
    QCOMPARE(events.at(1), QStringLiteral("map:false"));
    QVERIFY(events.at(2) == QStringLiteral("write:null") || events.at(2) == QStringLiteral("write:true"));
    QCOMPARE(events.at(3), QStringLiteral("map:true"));
}

void OptionsModelTests::storageDirtyIgnoresDisabledPruneSize()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(!model.prune());
    model.setPruneSizeGB(10);
    QVERIFY(!model.storageSettingsDirty());

    model.setPrune(true);
    QVERIFY(model.storageSettingsDirty());
    QVERIFY(model.restartRequired());
}

void OptionsModelTests::pruneDisabledPreservesPreviousValue()
{
    MockNode node;
    node.SetPersistentSetting("prune", MakeInt(QmlCoreSettings::PruneGBToMiB(10)));

    OptionsQmlModel model(node);
    QVERIFY(model.prune());
    QCOMPARE(model.pruneSizeGB(), 10);

    model.setPrune(false);
    QVERIFY(!model.prune());
    QCOMPARE(model.pruneSizeGB(), 10);
    QCOMPARE(node.update_rw_setting_arguments.size(), 2U);
    const auto* previous_write{FindSettingWrite(node, "prune-prev")};
    QVERIFY(previous_write != nullptr && previous_write->isStr());
    QCOMPARE(previous_write->get_str(), std::to_string(QmlCoreSettings::PruneGBToMiB(10)));
    const auto* prune_write{FindSettingWrite(node, "prune")};
    QVERIFY(prune_write != nullptr);
    QVERIFY(prune_write->isNull());
}

void OptionsModelTests::developerDirtyTracksRestartSettings()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(!model.developerSettingsDirty());

    model.setScriptThreads(model.scriptThreads() + 1);
    QVERIFY(model.developerSettingsDirty());
    QVERIFY(model.restartRequired());

    model.setScriptThreads(model.scriptThreads() - 1);
    QVERIFY(!model.developerSettingsDirty());
    QVERIFY(!model.restartRequired());
}

void OptionsModelTests::mempoolDirtyTracksRestartSettings()
{
    MockNode node;

    OptionsQmlModel model(node);
    QVERIFY(!model.developerSettingsDirty());
    QVERIFY(!model.mempoolSettingsDirty());
    QVERIFY(!model.restartRequired());

    model.setMaxMempoolSizeMB(model.maxMempoolSizeMB() + 1);
    QVERIFY(!model.developerSettingsDirty());
    QVERIFY(model.mempoolSettingsDirty());
    QVERIFY(model.restartRequired());

    model.setMaxMempoolSizeMB(model.maxMempoolSizeMB() - 1);
    QVERIFY(!model.developerSettingsDirty());
    QVERIFY(!model.mempoolSettingsDirty());
    QVERIFY(!model.restartRequired());
}

void OptionsModelTests::proxyValidationAndCommit()
{
    MockNode node;

    OptionsQmlModel model(node);
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
    QCOMPARE(model.proxyAddress(), QString("proxy.example"));
}

void OptionsModelTests::proxyDisabledPreservesPreviousValue()
{
    MockNode node;
    node.SetPersistentSetting("proxy", MakeAddress("127.0.0.1:9050"));

    OptionsQmlModel model(node);
    model.setProxyEnabled(false);
    QVERIFY(!model.proxyEnabled());
    QCOMPARE(node.update_rw_setting_arguments.size(), 2U);
    const auto* previous_write{FindSettingWrite(node, "proxy-prev")};
    QVERIFY(previous_write != nullptr && previous_write->isStr());
    QCOMPARE(previous_write->get_str(), std::string{"127.0.0.1:9050"});
    const auto* proxy_write{FindSettingWrite(node, "proxy")};
    QVERIFY(proxy_write != nullptr);
    QVERIFY(proxy_write->isNull());
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

    OptionsQmlModel model(node);
    QVERIFY(!model.validateCustomDataDir(file_path).isEmpty());
    QVERIFY(!model.selectCustomDataDir(file_path));
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

    OptionsQmlModel model(node);
    QVERIFY(model.validateCustomDataDir(data_dir).isEmpty());
    QVERIFY(model.selectCustomDataDir(data_dir));

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

    OptionsQmlModel model(node);
    QVERIFY(model.validateCustomDataDir(data_dir).isEmpty());
    QVERIFY(model.selectCustomDataDir(data_dir));

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

    OptionsQmlModel model(node, args);
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
    QVERIFY(QmlDataDir::ShouldShowDataDirChooser(args));

    args.ForceSetArg("-datadir", QDir(temp_dir.path()).filePath("explicit-data-dir").toStdString());
    QVERIFY(!QmlDataDir::ShouldShowDataDirChooser(args));
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
    const bool should_show = QmlDataDir::ShouldShowDataDirChooser(args);

    args.ForceSetArg("-datadir", QDir(temp_dir.path()).filePath("explicit-data-dir").toStdString());
    const bool explicit_datadir_should_show = QmlDataDir::ShouldShowDataDirChooser(args);

    QVERIFY(QFile(data_dir).setPermissions(original_permissions));
    QVERIFY(should_show);
    QVERIFY(!explicit_datadir_should_show);
}

void OptionsModelTests::guiDataDirChooserShowsForUnreadableConfiguredDir()
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

void OptionsModelTests::unreadableConfigDoesNotEscapeStartupResolution()
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

void OptionsModelTests::invalidExplicitDataDirReturnsErrorWithoutReadingProfile()
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

void OptionsModelTests::validRelativeExplicitDataDirResolvesAgainstWorkingDirectory()
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

void OptionsModelTests::resetGuiSettingsClearsAndBacksUpQSettings()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());

    QSettings settings;
    settings.setValue(SettingsKeys::DATA_DIR, QStringLiteral("/tmp/old-bitcoin-data"));
    settings.setValue(SettingsKeys::LANGUAGE, QStringLiteral("de"));
    settings.setValue(SettingsKeys::DISPLAY_UNIT, 3);
    settings.setValue(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, QStringLiteral("https://example.com/%s"));
    settings.setValue(SettingsKeys::MONEY_FONT_CHOICE, QStringLiteral("best_system"));
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
    QVERIFY(!settings.contains(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS));
    QVERIFY(!settings.contains(SettingsKeys::MONEY_FONT_CHOICE));
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

void OptionsModelTests::resetGuiSettingsClearsAndBacksUpSettingsJson()
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
        QVERIFY(settings.rw_settings.count("prune") == 0);
        QVERIFY(settings.rw_settings.count("proxy") == 0);
        QVERIFY(settings.rw_settings.count("onion") == 0);
    });
}

void OptionsModelTests::resetGuiSettingsHonorsFinalSourcePrecedence()
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

void OptionsModelTests::resetGuiSettingsAllowsUnreadableSettingsProfile()
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

void OptionsModelTests::resetLegacyCleanupRollsBackOnWriteFailure()
{
#ifdef Q_OS_WIN
    QSKIP("This test relies on POSIX directory permissions.");
#else
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    qml_core_settings.settings().setValue(QStringLiteral("server"), true);
    legacy_settings.settings().setValue(QStringLiteral("fListen"), false);
    qml_core_settings.settings().sync();
    legacy_settings.settings().sync();
    QCOMPARE(qml_core_settings.settings().status(), QSettings::NoError);
    QCOMPARE(legacy_settings.settings().status(), QSettings::NoError);

    const QString legacy_file{legacy_settings.settings().fileName()};
    const QString legacy_dir{QFileInfo(legacy_file).absolutePath()};
    const QFileDevice::Permissions file_permissions{QFile::permissions(legacy_file)};
    const QFileDevice::Permissions dir_permissions{QFile::permissions(legacy_dir)};
    bool cleared{true};
    QString clear_error;
    {
        [[maybe_unused]] const auto restore_permissions = qScopeGuard([&] {
            QFile::setPermissions(legacy_dir, dir_permissions);
            QFile::setPermissions(legacy_file, file_permissions);
        });
        QVERIFY(QFile::setPermissions(legacy_file, QFileDevice::ReadOwner));
        QVERIFY(QFile::setPermissions(
            legacy_dir,
            QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        cleared = QmlLegacySettings::ClearLegacyGuiSettings(
            QStringLiteral("regtest"),
            &clear_error);
    }

    QVERIFY(!cleared);
    QVERIFY(clear_error.contains(QStringLiteral("Legacy GUI settings cleanup")));
    qml_core_settings.settings().sync();
    legacy_settings.settings().sync();
    QCOMPARE(qml_core_settings.settings().value(QStringLiteral("server")).toBool(), true);
    QCOMPARE(legacy_settings.settings().value(QStringLiteral("fListen")).toBool(), false);
#endif
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

void OptionsModelTests::qmlOnboardedConfiguredDatadirProfileSkipsPreInitOnboarding()
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

void OptionsModelTests::guiDatadirTakesPrecedenceOverConfigDatadir()
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

void OptionsModelTests::guiDatadirConfigUserSelectionPersistsNewPath()
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

void OptionsModelTests::explicitDatadirApplyDoesNotPersistGuiDataDir()
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

void OptionsModelTests::resetGuiSettingsPreservesSavedDatadirOverConfigDatadir()
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

void OptionsModelTests::qmlOnboardedResolvedNetworkResetFlagShowsPreInitOnboarding()
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

void OptionsModelTests::onboardingApplyCreatesNewCustomDataDir()
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
    QVERIFY(QFileInfo(QDir(network_dir).filePath(QStringLiteral("settings.json"))).isFile());
    QVERIFY(!QFileInfo::exists(network_wallets_dir));
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

void OptionsModelTests::onboardingStorageCheckUsesResolvedDataDir()
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

    OptionsQmlModel model(node);
    model.setThirdPartyTransactionUrls(
        "https://example.com/tx/%s|"
        "http://example.net/tx/%s|"
        "file://host/tmp/%s|"
        "bitcoin://host/tx/%s|"
        "ftp://example.org/tx/%s|"
        "https://example.org/tx|"
        "not-a-url|"
        "https:///tx/%s");

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

    OptionsQmlModel model(node);
    QCOMPARE(model.moneyFontChoice(), QString("embedded"));
    model.setMoneyFontChoice("best_system");
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

    OptionsQmlModel model(node);
    QCOMPARE(QString::fromUtf8(SettingsKeys::DISPLAY_UNIT), QStringLiteral("DisplayBitcoinUnit"));
    QCOMPARE(model.displayUnit(), 3);
    QCOMPARE(model.displayUnitLabel(), QStringLiteral("sat"));
    QCOMPARE(model.displayUnitLabelForAmount(2), QStringLiteral("sats"));

    model.setDisplayUnit(1);
    QCOMPARE(model.displayUnit(), 1);
    QCOMPARE(model.displayUnitLabel(), QStringLiteral("mBTC"));
    QCOMPARE(model.displayUnitLabelForAmount(2), QStringLiteral("mBTC"));
    QCOMPARE(settings.value(SettingsKeys::DISPLAY_UNIT).toInt(), 1);

    model.setDisplayUnit(2);
    QCOMPARE(model.displayUnit(), 2);
    QCOMPARE(model.displayUnitLabel(), QStringLiteral("bits"));
    QCOMPARE(model.displayUnitLabelForAmount(2), QStringLiteral("bits"));
    QCOMPARE(settings.value(SettingsKeys::DISPLAY_UNIT).toInt(), 2);

    model.setDisplayUnit(9);
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

    OptionsQmlModel model(node, args);
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

    OptionsQmlModel model(node, args);
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

    OptionsQmlModel model(node, args);
    auto* core_settings = qobject_cast<CoreSettingsModel*>(model.coreSettings());
    QVERIFY(core_settings);
    auto* listen_entry = qobject_cast<CoreSettingEntryModel*>(core_settings->entry(QStringLiteral("listen")));
    QVERIFY(listen_entry);

    QVERIFY(model.listen());
    QVERIFY(!model.connectionSettingsDirty());

    listen_entry->setValue(false);

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

        OptionsQmlModel model(node, args);
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

        OptionsQmlModel model(node, args);
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

        OptionsQmlModel model(node, args);
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

    OptionsQmlModel model(node, args);
    QCOMPARE(model.coreSettingStatus(QStringLiteral("maxmempool")).value("source").toString(), QString("default"));

    QSignalSpy status_spy(&model, &OptionsQmlModel::coreSettingStatusesChanged);
    model.setMaxMempoolSizeMB(456);

    QCOMPARE(status_spy.count(), 1);
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

    OptionsQmlModel model(node, args);
    QVERIFY(!model.listen());
    QCOMPARE(model.coreSettingStatus(QStringLiteral("listen")).value("source").toString(), QString("command_line"));
    QCOMPARE(model.coreSettingStatus(QStringLiteral("listen")).value("canEdit").toBool(), false);

    node.update_rw_setting_arguments.clear();
    model.setListen(true);

    QVERIFY(!model.listen());
    QCOMPARE(SettingWriteCount(node, "listen"), 0);
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

    OptionsQmlModel model(node, args);
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

    OptionsQmlModel proxy_model(proxy_node, proxy_args);
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

    proxy_node.update_rw_setting_arguments.clear();
    proxy_model.setListen(true);
    QVERIFY(proxy_model.listen());
    QCOMPARE(SettingToBool(proxy_args.GetPersistentSetting("listen")), true);
    QCOMPARE(proxy_model.coreSettingStatus(QStringLiteral("listen")).value("hasRwSetting").toBool(), true);
    QCOMPARE(listen_entry->status().value("hasRwSetting").toBool(), true);
    QVERIFY(listen_entry_status_spy.count() >= 1);
    QCOMPARE(proxy_node.update_rw_setting_arguments.size(), 1U);
    const auto* enabled_write{FindSettingWrite(proxy_node, "listen")};
    QVERIFY(enabled_write != nullptr && enabled_write->isBool());
    QVERIFY(enabled_write->get_bool());

    proxy_node.update_rw_setting_arguments.clear();
    proxy_model.setListen(false);
    QVERIFY(!proxy_model.listen());
    QCOMPARE(proxy_node.update_rw_setting_arguments.size(), 1U);
    const auto* disabled_write{FindSettingWrite(proxy_node, "listen")};
    QVERIFY(disabled_write != nullptr);
    QVERIFY(disabled_write->isNull());

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

    OptionsQmlModel blocksonly_model(blocksonly_node, blocksonly_args);
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

    OptionsQmlModel model(node, args);
    node.update_rw_setting_arguments.clear();
    node.force_setting_arguments.clear();

    model.setServer(false);
    model.setPrune(false);
    model.setProxyEnabled(false);
    model.setDbcacheSizeMiB(700);
    model.setScriptThreads(8);
    model.setMaxMempoolSizeMB(700);
    model.setExternalSignerPath(QStringLiteral("changed-signer"));

    QCOMPARE(SettingToBool(args.GetPersistentSetting("server")), false);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("prune"), -1), 0);
    QCOMPARE(QString::fromStdString(SettingToString(args.GetPersistentSetting("proxy"), "")), QString("127.0.0.1:9050"));
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("dbcache"), -1), 300);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("par"), -1), 1);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("maxmempool"), -1), 300);
    QCOMPARE(QString::fromStdString(SettingToString(args.GetPersistentSetting("signer"), "")), QString("saved-signer"));
    QVERIFY(node.update_rw_setting_arguments.empty());
    QVERIFY(node.force_setting_arguments.empty());
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

    OptionsQmlModel model(node, args);
    QVERIFY(!model.listen());

    node.update_rw_setting_arguments.clear();
    model.setListen(true);
    QCOMPARE(node.update_rw_setting_arguments.size(), 1U);
    const auto* listen_write{FindSettingWrite(node, "listen")};
    QVERIFY(listen_write != nullptr);
    QVERIFY(listen_write->isNull());
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

    OptionsQmlModel model(node, args);
    QCOMPARE(model.maxMempoolSizeMB(), 456);

    node.update_rw_setting_arguments.clear();
    model.setMaxMempoolSizeMB(DEFAULT_MAX_MEMPOOL_SIZE_MB);
    QCOMPARE(node.update_rw_setting_arguments.size(), 1U);
    const auto* mempool_write{FindSettingWrite(node, "maxmempool")};
    QVERIFY(mempool_write != nullptr);
    QVERIFY(mempool_write->isNull());
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

    OptionsQmlModel model(node, args);
    QCOMPARE(model.language(), QString("fr"));
    QCOMPARE(model.coreSettingStatus(QStringLiteral("lang")).value("canEdit").toBool(), false);

    model.setLanguage(QStringLiteral("es"));
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
    OptionsQmlModel model(node, args);
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

void OptionsModelTests::onboardingApplyRollsBackWhenLegacyCleanupFails()
{
#ifdef Q_OS_WIN
    QSKIP("This test relies on POSIX directory permissions.");
#else
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings qml_core_settings{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy_settings{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    legacy_settings.settings().setValue(QStringLiteral("fListen"), false);
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

    const QString legacy_file{legacy_settings.settings().fileName()};
    const QString legacy_dir{QFileInfo(legacy_file).absolutePath()};
    const QFileDevice::Permissions file_permissions{QFile::permissions(legacy_file)};
    const QFileDevice::Permissions dir_permissions{QFile::permissions(legacy_dir)};
    bool finalized{true};
    QString finalize_error;
    {
        [[maybe_unused]] const auto restore_permissions = qScopeGuard([&] {
            QFile::setPermissions(legacy_dir, dir_permissions);
            QFile::setPermissions(legacy_file, file_permissions);
        });
        QVERIFY(QFile::setPermissions(legacy_file, QFileDevice::ReadOwner));
        QVERIFY(QFile::setPermissions(
            legacy_dir,
            QFileDevice::ReadOwner | QFileDevice::ExeOwner));
        finalized = QmlOnboardingSettings::FinalizeStartupSettings(
            args,
            bootstrap_gui_settings,
            &pending,
            /*result=*/nullptr,
            &finalize_error);
    }

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
#endif
}

void OptionsModelTests::onboardingApplyRollsBackWhenSettingsWriteFails()
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

void OptionsModelTests::onboardingApplyWithSettingsDisabledPreservesLegacyCoreValues()
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
    QVERIFY(model.selectCustomDataDir(data_dir.path()));
    QCOMPARE(model.previewError(), QString{});
    QCOMPARE(model.coreSettingStatuses().value(QStringLiteral("listen")).toMap().value(QStringLiteral("source")).toString(), QStringLiteral("bitcoin_conf"));
    QVERIFY(!model.listen());
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

void OptionsModelTests::onboardingApplyRetainsListenChoiceWhenDisablingProxy()
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

void OptionsModelTests::onboardingApplyRejectsProfileDrift()
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

void OptionsModelTests::onboardingFinalizeIgnoresUnusedBootstrapStore()
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

void OptionsModelTests::onboardingApplyClearsResetFlagInBootstrapAndActiveStores()
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

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OptionsModelTests)
#else
QTEST_MAIN(OptionsModelTests)
#endif
#include "test_options_model.moc"
