// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/caches.h>
#include <QtTest/QtTest>

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <QDir>
#include <QDataStream>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTemporaryFile>
#include <QThread>
#include <QTimer>
#include <qml/asyncjoin.h>
#include <qml/executor.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/drainexecutors.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualstartupexecution.h>
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

#include <algorithm>
#include <atomic>
#include <array>
#include <deque>
#include <functional>
#include <future>
#include <stdexcept>

#include <utility>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

namespace {
// Preview owns real Core/filesystem/process effects; this fixture waits for
// the original Native read without moving those effects onto the GUI lane.
template<typename... Args>
auto PreviewOnNative(Args&&... args)
{
    return std::async(std::launch::async, [&] {
        return QmlOnboardingSettings::Preview(std::forward<Args>(args)...);
    }).get();
}
template<typename Model>
QString CheckedProxyLiteral(Model& owner,const QString& literal)
{
    auto* model=qobject_cast<CoreSettingsModel*>(owner.coreSettings());if(!model){QTest::qFail("Missing Core settings adapter",__FILE__,__LINE__);return QStringLiteral("Missing adapter");}
    std::unique_ptr<CoreProxyDraft> draft{model->createProxyDraft()};if(!draft){QTest::qFail("Proxy draft rejected",__FILE__,__LINE__);return QStringLiteral("Draft rejected");}
    if(!QTest::qWaitFor([&]{return !draft->pending();})){QTest::qFail("Proxy draft opening timed out",__FILE__,__LINE__);return QStringLiteral("Draft timeout");}
    draft->setProxyEnabled(true);draft->setProxyAddress(literal);
    if(!QTest::qWaitFor([&]{return !draft->pending();})){QTest::qFail("Proxy validation timed out",__FILE__,__LINE__);return QStringLiteral("Validation timeout");}
    return draft->proxyError();
}
template<typename Model>
bool AwaitProxyLocation(Model& owner,bool tor,const QString& literal)
{
    auto* model=qobject_cast<CoreSettingsModel*>(owner.coreSettings());if(!model)return false;
    QSignalSpy outcomes{model,&CoreSettingsModel::operationFinished};
    const auto ticket=tor?model->changeTorLocation(literal):model->changeProxyLocation(literal);if(!ticket)return false;
    std::optional<CoreSettingsModel::OperationState> outcome;
    if(!QTest::qWaitFor([&]{for(const auto& row:outcomes)if(row[0].toULongLong()==ticket){outcome=row[1].value<CoreSettingsModel::OperationState>();return true;}return false;})){QTest::qFail("Original proxy operation did not deliver its outcome",__FILE__,__LINE__);return false;}
    return *outcome==CoreSettingsModel::Applied;
}
template<typename Model>
bool AwaitCoreSettled(Model& owner)
{
    auto* model=qobject_cast<CoreSettingsModel*>(owner.coreSettings());
    if(!model||!QTest::qWaitFor([&]{return !model->pending();})){QTest::qFail("Core settings outcome was not delivered",__FILE__,__LINE__);return false;}return true;
}
bool ApplyOnboardingModelRequest(const OnboardingOptionsModel& model,ArgsManager& args,QString* error)
{
    const auto request=model.applyRequest();
    return QmlOnboardingSettings::ApplyToArgs(args,request.data_dir,request.touched_settings,request.values,error);
}

struct ManualSnapshotGraph {
    ManualRoutedExecution transport;
    struct Publication {
        std::function<void()> callback;
        std::optional<execution::TaskId> origin;
        void operator()() { callback(); }
    };
    std::deque<Publication> posts;
    std::optional<execution::TaskId> executing;

    bool finish(execution::TaskId id)
    {
        executing = id;
        const bool completed = transport.finishWork(id);
        executing.reset();
        return completed;
    }

    std::optional<Publication> takeTaskPost(execution::TaskId id)
    {
        const auto found = std::find_if(posts.begin(), posts.end(), [id](const auto& post) { return post.origin == id; });
        if (found == posts.end()) return {};
        auto publication = std::move(*found);
        posts.erase(found);
        return publication;
    }

    execution::SnapshotServices services()
    {
        return {transport.scheduler, [this](auto callback) { posts.push_back({std::move(callback), executing}); }};
    }

    bool deliverOnePost()
    {
        if (posts.empty()) return false;
        auto callback = std::move(posts.front());
        posts.pop_front();
        callback();
        return true;
    }

    bool step(execution::Lane lane)
    {
        const auto id = transport.dispatchNext(lane);
        return id && finish(*id);
    }

    // Follow the exact admitted task rather than counting unrelated Domain
    // cleanup receipts produced by retained snapshots in the same schedule.
    std::optional<execution::TaskId> finishQueuedWork(execution::Lane lane)
    {
        std::optional<execution::TaskId> target;
        transport.queuedCountIf([&](const auto& turn) {
            if (!target && turn.lane == lane && turn.phase == execution::Phase::Queued) target = turn.id;
            return false;
        });
        if (!target) return {};
        for (size_t turn = 0; turn < 1024; ++turn) {
            const auto next = transport.dispatchNext(lane);
            if (!next || !finish(*next)) return {};
            if (*next == *target) return target;
        }
        return {};
    }

    bool deliverTaskOutcome(execution::TaskId target)
    {
        for (size_t turn = 0; turn < 1024; ++turn) {
            const auto next = transport.dispatchNext(execution::Lane::Domain);
            if (!next || !finish(*next)) return false;
            if (*next == target) return true;
        }
        return false;
    }

    size_t queued(execution::Lane lane) const
    {
        return transport.queuedCountIf([lane](const auto& turn) { return turn.lane == lane; });
    }

    // Drain work and GUI publications while the model still owns its cleanup
    // leases. Full quiescence is checked only after the model is destroyed.
    bool drainLive(ManualStartupExecution* shared = nullptr)
    {
        for (size_t turn = 0; turn < 1024; ++turn) {
            if (deliverOnePost()) continue;
            if (const auto lane = transport.nextLane()) {
                if (!transport.step(*lane)) return false;
                continue;
            }
            if (shared && !shared->quiescent()) {
                shared->drain();
                continue;
            }
            return posts.empty() && transport.transportIdle();
        }
        return false;
    }
};

struct OnboardingStorageWork {
    ManualStartupExecution worker;
    std::deque<std::function<void()>> posts;

    std::function<void()> takePost()
    {
        if (posts.empty()) return {};
        auto next = std::move(posts.front());
        posts.pop_front();
        return next;
    }

    bool publish()
    {
        auto next = takePost();
        if (!next) return false;
        next();
        return true;
    }

    bool settle()
    {
        for (int turn = 0; turn < 1024; ++turn) {
            if (!step()) return worker.quiescent() && posts.empty();
        }
        return false;
    }

    bool driveToStorageCheck(OnboardingOptionsModel& model, const QString& path)
    {
        for (int turn = 0; turn < 1024; ++turn) {
            if (model.dataDir() == path && !model.validationPending() && model.storageCheckPending() &&
                worker.hasWork(execution::Lane::Core) && !worker.hasWork(execution::Lane::Utility) && posts.empty()) {
                return true;
            }
            if (!step()) break;
        }
        return false;
    }

    bool step()
    {
        return publish() || worker.step(execution::Lane::Utility) || worker.step(execution::Lane::Core);
    }
};
}

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
    void directorySelectionHasOneValidatedCommand()
    {
        const auto& meta = OptionsQmlModel::staticMetaObject;
        QCOMPARE(meta.indexOfMethod("setCustomDataDirArgs(QString)"), -1);
        QCOMPARE(meta.indexOfMethod("setCustomDataDirString(QString)"), -1);
        QVERIFY(meta.indexOfMethod("selectCustomDataDir(QString)") >= 0);
        QVERIFY(meta.indexOfMethod("useDefaultDataDir()") >= 0);
    }
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
    void proxyDisabledPersistsDisabledOverride();
    void torDisabledPersistsDisabledOverride();
    void proxyEnabledWritesAddress();
    void proxyDirtySetAtRuntime();
    void proxyDirtyResetWhenReverted();
    void mempoolSizeLoadedFromSettings();
    void mempoolSizeWritesSetting();
    void acceptedRuntimeWriteDrainsOnSharedQueues();
    void acceptedCoreSettingCompletesAfterQuietClose();
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
    void signerValidationKeepsLatestManualIntent();
    void signerValidationReportsManualTransportRefusal();
    void signerValidationRunsOnCoreWorker();
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
    void onboardingApplyReportsDefaultDataDirSaveFailure();
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
#ifdef ENABLE_WALLET
    void onboardingPreviewDetectsRootWalletProfile();
#endif
#ifdef ENABLE_WALLET
    void onboardingPreviewDetectsExplicitWalletDirProfile();
#endif
#ifdef ENABLE_WALLET
    void onboardingPreviewIgnoresEmptyExplicitWalletDir();
#endif
    void onboardingPreviewIgnoresUnrecognizedWalletsEntry();
    void freshExplicitDatadirShowsFullOnboarding();
    void onboardingApplyWithoutTouchedSettingsOnlyAddsQmlOnboardedMarker();
    void onboardingApplyCreatesWalletSubdirectoryForNewNetworkDataDir();
    void onboardingApplyPreservesExistingNetworkWalletDiscovery();
    void fullOnboardingApplyWritesQmlOnboardedMarker();
    void onboardingPreviewAppliesParameterInteractions();
    void storageSpaceCheckAcceptsExistingDirectory();
    void storageSpaceCheckRejectsExistingFile();
    void onboardingStorageCheckDoesNotPublishStalePath();
    void onboardingStorageProjectionCoalescesPublicPruneChoice();
    void thirdPartyTransactionLinksParseValidUrls();
    void moneyFontChoicePersists();
    void rejectedDisplayRefreshDoesNotReenterItsFailure();
    void databaseCacheMatchesCoreDefaultAndRange();
    void unavailableLanguageDoesNotPersist_data();
    void unavailableLanguageDoesNotPersist();
    void displayReadbackRepairsInvalidStoredUnit_data();
    void displayReadbackRepairsInvalidStoredUnit();
    void languageReadbackRejectsUnavailablePersistedTag();
    void invalidCoreLanguageDoesNotEraseAnIndependentGuiPreference();
    void manualDisplayRefreshWaitsForPendingWrite();
    void externalDisplayWriterIsObservedWithoutUserEdit();
    void hideAmountsPersists();
    void displayUnitUsesQtCompatibleSettingsKey();
    void displayUnitUsesLegacyQtFallback();
    void displayUnitPrefersQmlSettingOverLegacyQtFallback();
    void sharedCoreSettingHelpersPreserveExplicitOverrides();
    void parameterInteractionOverridesPersistExplicitValues();
    void coreSettingsLegacyNumericOverridesWriteStrings();
    void coreSettingsModelEntryMutatesRuntimeModel();
    void coreSettingsSessionCommandLineSettingDoesNotWrite();
    void coreSettingsSessionDoesNotCopyUntouchedConfig();
    void coreSettingsSessionWritesTouchedConfigOverride();
    void coreSettingsSessionRevertingToDefaultPersistsExplicitOverride();
    void coreSettingsSessionPruneDisabledPreservesPreviousValue();
    void coreSettingsSessionPruneEnabledClearsPreviousValue();
    void coreSettingsSessionProxyDisabledPreservesPreviousValue();
    void coreSettingsLoadPersistentPrunePreviousValue();
    void coreSettingsSessionPreviewRefreshPreservesTouchedValues();
    void coreSettingsSessionChangeReportsFieldDiffs();
    void coreSettingsSessionProxyCommitAcceptsUnchangedAddressAsExplicitIntent();
    void coreSettingStatusTracksSourcePrecedence();
    void runtimeCoreSettingStatusesRefreshAfterWrite();
    void commandLineOverriddenSettingDoesNotWrite();
    void runtimeCommandLineOverridesDisplayEffectiveValues();
    void runtimeParameterInteractionsDisplayEffectiveValues();
    void commandLineOverriddenSettingsPreservePersistentValues();
    void revertingToConfigValuePersistsExplicitOverride();
    void revertingToDefaultValuePersistsExplicitOverride();
    void languageCommandLineOverrideDoesNotPersist();
    void legacyQtSettingsMigrateToCoreSettings();
    void knownSavedProfileFinishesOnlyRemainingDirectoryAndCleanup();
    void capturedResetKeepsOriginalBackupAcrossCompletion();
    void capturedResetRejectsChangedLegacySource();
    void corruptStoredPreferenceFailsWithoutDefaultFallback();
    void migrationChecksExactReplacementAndSources_data();
    void migrationChecksExactReplacementAndSources();
    void migrationCleanupOnlyFollowsDurableWriteAndIsIdempotent();
    void profileRecoveryDoesNotReplayChangedReplacement();
    void savedProfileReadFailureNeverSelectsDefault_data();
    void savedProfileReadFailureNeverSelectsDefault();
    void legacyNativeOrganizationDomainMatchesWriter();
    void failedProfileHandoffRetainsLegacyMigrationSource_data();
    void failedProfileHandoffRetainsLegacyMigrationSource();
    void failedOnboardingWriteKeepsLegacyProxyForRetry();
    void coreApiHistoricalCacheMigration_data();
    void coreApiHistoricalCacheMigration();
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

QString HistoricalSettingsOrganization(QString organization)
{
#ifdef Q_OS_MACOS
    if(QSettings::defaultFormat()==QSettings::NativeFormat){
        if(organization==QStringLiteral("Bitcoin"))return QStringLiteral("bitcoin.org");
        if(organization==QStringLiteral("BitcoinCore"))return QStringLiteral("bitcoincore.org");
    }
#endif
    return organization;
}
class SavedNamedSettings
{
public:
    SavedNamedSettings(const QString& org, const QString& app)
        : m_settings{QSettings::defaultFormat(), QSettings::UserScope, HistoricalSettingsOrganization(org), app}
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

#ifdef ENABLE_WALLET
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

#endif

static void PrepareArgsForDataDir(ArgsManager& args, const QString& data_dir)
{
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, TestArgvWithDataDir(data_dir), parse_error), parse_error.c_str());
    SelectParams(args.GetChainType());
    args.SelectConfigNetwork(args.GetChainTypeString());
}

// Staging is deliberately read-only. Successful replacement-write fixtures own
// their physical target directory; failure fixtures prepare their own barrier.
static bool EnsureReplacementParent(ArgsManager& args)
{
    fs::path path;
    return args.GetSettingsPath(&path) && QDir{}.mkpath(QFileInfo{QString::fromStdString(path.utf8string())}.absolutePath());
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

void OptionsModelTests::proxyDisabledPersistsDisabledOverride()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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

    // Disabling is an explicit false override; removing it is a separate editor intent.
    model.setProxyEnabled(false);
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.proxyEnabled());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
    QVERIFY(!model.settingsPending());
    const auto proxy_write = ReadRwSetting(model_args, "proxy");
    QVERIFY(proxy_write.isBool());
    QVERIFY(!proxy_write.get_bool());
}

void OptionsModelTests::torDisabledPersistsDisabledOverride()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    QVERIFY(onion_write.isBool());
    QVERIFY(!onion_write.get_bool());
}

void OptionsModelTests::proxyEnabledWritesAddress()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

    // Start onboarded with proxy disabled (no saved proxy).
    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.proxySettingsDirty());

    // Simulate what ProxySettingsPage.qml does: set address before enabling.
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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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

void OptionsModelTests::acceptedRuntimeWriteDrainsOnSharedQueues()
{
    MockNode node;
    ArgsManager args;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PrepareRuntimePersistence(node, args, directory);
    ManualSnapshotGraph graph;
    ManualStartupExecution shared;
    {
        OptionsQmlModel model{node, args, graph.services(), &shared.executor()};
        const auto cleanup = qScopeGuard([&] { model.beginShutdown(); graph.drainLive(&shared); });
        QVERIFY(graph.drainLive(&shared));
        QVERIFY(model.settingsReady());
        QVERIFY(!model.settingsPending());
        QVERIFY(model.dbcacheSizeMiB() != 701);
        const auto path = directory.filePath(QStringLiteral("runtime-settings.json"));
        QVERIFY(!QFile::exists(path));

        model.setDbcacheSizeMiB(701);
        QCOMPARE(model.dbcacheSizeMiB(), 701);
        QVERIFY(model.settingsPending());
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsPending);
        QVERIFY(!QFile::exists(path));
        QSignalSpy drained{&model, &OptionsQmlModel::shutdownFinished};
        QSignalSpy published{&model, &OptionsQmlModel::dbcacheSizeMiBChanged};
        model.beginShutdown();
        QCOMPARE(drained.size(), 0);
        QVERIFY(shared.step(execution::Lane::Core));
        QFile settings_file{path};
        QVERIFY(settings_file.open(QIODevice::ReadOnly));
        UniValue persisted;
        const auto bytes = settings_file.readAll();
        QVERIFY(persisted.read(std::string{bytes.constData(), size_t(bytes.size())}));
        QCOMPARE(SettingToString(persisted["dbcache"]).value_or(""), std::string{"701"});
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsApplied);
        QCOMPARE(drained.size(), 0);

        QVERIFY(shared.step(execution::Lane::Utility));
        QVERIFY(graph.drainLive());
        QCOMPARE(drained.size(), 0); // Physical queue drain still has to run.
        QVERIFY(graph.drainLive(&shared));
        QCOMPARE(drained.size(), 1);
        QCOMPARE(published.size(), 0);
        QCOMPARE(model.dbcacheSizeMiB(), 701);
        model.beginShutdown();
        QCOMPARE(drained.size(), 1);
    }
    QVERIFY(graph.drainLive(&shared));
    QVERIFY(graph.transport.quiescent());
}

void OptionsModelTests::acceptedCoreSettingCompletesAfterQuietClose()
{
    MockNode node;
    ArgsManager args;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    PrepareRuntimePersistence(node, args, directory);
    ManualSnapshotGraph graph;
    ManualStartupExecution shared;
    {
        OptionsQmlModel model{node, args, graph.services(), &shared.executor()};
        const auto cleanup = qScopeGuard([&] { model.beginShutdown(); graph.drainLive(&shared); });
        QVERIFY(graph.drainLive(&shared));
        QVERIFY(model.settingsReady());
        QVERIFY(!model.settingsPending());
        const bool original = model.natpmp();
        QVERIFY(args.GetPersistentSetting("natpmp").isNull());

        model.setNatpmp(!original);
        QVERIFY(model.settingsPending());
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsPending);
        QSignalSpy drained{&model, &OptionsQmlModel::shutdownFinished};
        QSignalSpy published{&model, &OptionsQmlModel::natpmpChanged};
        model.beginShutdown();
        QCOMPARE(drained.size(), 0);

        // The original Utility decision can still queue the persistence
        // effect after GUI admission closes.
        while (shared.step(execution::Lane::Utility)) {}
        QVERIFY(graph.drainLive());
        QVERIFY(args.GetPersistentSetting("natpmp").isNull());
        QCOMPARE(drained.size(), 0);
        QVERIFY(shared.step(execution::Lane::Core));
        const auto persisted = args.GetPersistentSetting("natpmp");
        QVERIFY(persisted.isBool());
        QCOMPARE(persisted.isTrue(), !original);
        QCOMPARE(node.calls.mapPort.load(), 1);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsApplied);
        QVERIFY(graph.drainLive());
        QCOMPARE(drained.size(), 0);

        QVERIFY(graph.drainLive(&shared));
        QCOMPARE(drained.size(), 1);
        QCOMPARE(published.size(), 0);
        QCOMPARE(model.natpmp(), original);
        model.beginShutdown();
        QCOMPARE(drained.size(), 1);
    }
    QVERIFY(graph.drainLive(&shared));
    QVERIFY(graph.transport.quiescent());
}

void OptionsModelTests::mempoolSizeDoesNotRewriteUnchangedSetting()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());

    model.setExternalSignerPath(QCoreApplication::applicationFilePath());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.externalSignerPath(), QCoreApplication::applicationFilePath());
    const auto signer_write = ReadRwSetting(model_args, "signer");
    QVERIFY(signer_write.isStr());
    QCOMPARE(signer_write.get_str(), QCoreApplication::applicationFilePath().toStdString());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::externalSignerPathClearedRemovesKey()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath(QCoreApplication::applicationFilePath());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.walletSettingsDirty());
    QVERIFY2(!model.restartRequired(), "A confirmed live signer-path application must not request a restart.");

    model.setExternalSignerPath("");
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::signerPathLoadedFromSettings()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.externalSignerPath().isEmpty());

    model.setExternalSignerPath(QCoreApplication::applicationFilePath());
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.externalSignerPath(), QCoreApplication::applicationFilePath());
    const auto signer_write = ReadRwSetting(model_args, "signer");
    QVERIFY(signer_write.isStr());
    QCOMPARE(signer_write.get_str(), QCoreApplication::applicationFilePath().toStdString());
    QVERIFY(QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::signerDirtySetAtRuntime()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath(QCoreApplication::applicationFilePath());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.walletSettingsDirty());
}

void OptionsModelTests::signerDirtyResetWhenReverted()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());

    model.setExternalSignerPath(QCoreApplication::applicationFilePath());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.walletSettingsDirty());

    model.setExternalSignerPath("");
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(!model.walletSettingsDirty());
}

void OptionsModelTests::externalSignerPathValidationRejectsMissingPath()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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

void OptionsModelTests::signerValidationKeepsLatestManualIntent()
{
    ManualSnapshotGraph graph; ManualStartupExecution shared;
    MockNode node;
    const auto drain_node_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);

    QTemporaryDir missing_paths;
    {
        OptionsQmlModel model(node, model_args, graph.services(), &shared.executor());
        const auto shutdown = qScopeGuard([&] { model.beginShutdown(); graph.drainLive(&shared); });
        QVERIFY(graph.drainLive(&shared));
        QVERIFY(graph.transport.transportIdle());
        QVERIFY(graph.transport.scheduler->pending() > 0);
        QVERIFY(model.settingsReady());
        QVERIFY(!model.settingsPending());
        QVERIFY(!model.signerPathValidationPending());

        QVERIFY(missing_paths.isValid());
        const QString first_path = missing_paths.filePath(QStringLiteral("first-missing-signer"));
        const QString latest_path = missing_paths.filePath(QStringLiteral("latest-missing-signer"));
        QVERIFY(!QFileInfo::exists(first_path));
        QVERIFY(!QFileInfo::exists(latest_path));
        QSignalSpy validation_changed{&model, &OptionsQmlModel::signerPathValidationChanged};

        const auto core_submissions = shared.coreSubmissions();
        model.requestExternalSignerPathValidation(first_path);
        QVERIFY(model.signerPathValidationPending());
        QCOMPARE(validation_changed.size(), 1);
        QCOMPARE(shared.coreSubmissions(), core_submissions + 1);

        model.requestExternalSignerPathValidation(latest_path);
        QVERIFY(model.signerPathValidationPending());
        QCOMPARE(validation_changed.size(), 2);
        QCOMPARE(shared.coreSubmissions(), core_submissions + 1);

        QVERIFY(shared.step(execution::Lane::Core));
        QVERIFY(model.signerPathValidationPending());
        QVERIFY(graph.posts.empty());
        // The original effect has finished; its owned outcome is still held
        // on Utility, independently of the subsequent GUI publication.
        QVERIFY(shared.step(execution::Lane::Utility));
        QVERIFY(model.signerPathValidationPending());
        QCOMPARE(graph.posts.size(), size_t{1});
        QVERIFY(graph.deliverOnePost());
        QVERIFY(model.signerPathValidationPending());
        QVERIFY(model.signerPathError().isEmpty());
        QCOMPARE(graph.posts.size(), size_t{1});

        QVERIFY(graph.deliverOnePost());
        QVERIFY(model.signerPathValidationPending());
        QCOMPARE(shared.coreSubmissions(), core_submissions + 2);
        QVERIFY(shared.step(execution::Lane::Core));
        QVERIFY(model.signerPathValidationPending());
        QVERIFY(graph.posts.empty());
        QVERIFY(shared.step(execution::Lane::Utility));
        QVERIFY(model.signerPathValidationPending());
        QCOMPARE(graph.posts.size(), size_t{1});
        QVERIFY(graph.deliverOnePost());

        QVERIFY(!model.signerPathValidationPending());
        QCOMPARE(model.signerPathError(), QStringLiteral("The configured signer path does not exist."));
        QCOMPARE(model.externalSignerPathValidationError(first_path), QString{});
        QCOMPARE(model.externalSignerPathValidationError(latest_path), QStringLiteral("The configured signer path does not exist."));
        QCOMPARE(validation_changed.size(), 3);
        QVERIFY(graph.drainLive(&shared));
        QVERIFY(graph.transport.transportIdle());
        QVERIFY(graph.transport.scheduler->pending() > 0);
        model.beginShutdown();
        QVERIFY(graph.drainLive(&shared));
    }
    QVERIFY(graph.drainLive(&shared));
    QVERIFY(graph.transport.quiescent());
}

void OptionsModelTests::signerValidationReportsManualTransportRefusal()
{
    ManualSnapshotGraph graph; ManualStartupExecution shared;
    MockNode node;
    const auto drain_node_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);

    {
        OptionsQmlModel model(node, model_args, graph.services(), &shared.executor());
        const auto shutdown = qScopeGuard([&] { model.beginShutdown(); graph.drainLive(&shared); });
        QVERIFY(graph.drainLive(&shared));
        QVERIFY(graph.transport.transportIdle());
        QVERIFY(graph.transport.scheduler->pending() > 0);
        QVERIFY(model.settingsReady());
        QVERIFY(!model.settingsPending());

        const QString path = QStringLiteral("rejected-signer-observation");
        QSignalSpy validation_changed{&model, &OptionsQmlModel::signerPathValidationChanged};
        shared.reject_core = true;
        model.requestExternalSignerPathValidation(path);
        QVERIFY(model.signerPathValidationPending());
        QCOMPARE(validation_changed.size(), 1);
        QVERIFY(shared.quiescent());
        QCOMPARE(graph.posts.size(), size_t{1});

        QVERIFY(graph.deliverOnePost());
        QVERIFY(!model.signerPathValidationPending());
        QCOMPARE(model.signerPathError(), QStringLiteral("Unable to validate the signer path."));
        QCOMPARE(validation_changed.size(), 2);
        QVERIFY(graph.drainLive(&shared));
        QVERIFY(graph.transport.transportIdle());
        QVERIFY(graph.transport.scheduler->pending() > 0);
        model.beginShutdown();
        QVERIFY(graph.drainLive(&shared));
    }
    QVERIFY(graph.drainLive(&shared));
    QVERIFY(graph.transport.quiescent());
}

void OptionsModelTests::signerValidationRunsOnCoreWorker()
{
    QTemporaryDir settings_dir; QVERIFY(settings_dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    SavedSettingsFormat settings_format{QSettings::IniFormat};
    MockNode node;
    const auto drain_node_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);

    execution::Executor workers{execution::Executor::Config{}};
    auto core = workers.makeQueue(execution::Lane::Core);
    auto utility = workers.makeQueue(execution::Lane::Utility);
    std::atomic<QThread*> core_thread{nullptr};
    std::atomic<size_t> core_executions{0};
    execution::Executor executor{[&](auto lane, auto, auto turn) {
        if (lane != execution::Lane::Core) return utility.post(std::move(turn));
        return core.post([&, turn = std::move(turn)] {
            core_thread = QThread::currentThread();
            ++core_executions;
            turn();
        });
    }};
    OptionsQmlModel model(node, model_args, {}, &executor);
    QVERIFY(QTest::qWaitFor([&] { return model.settingsReady() && !model.settingsPending(); }, 5000));

    QTemporaryDir missing_paths;
    QVERIFY(missing_paths.isValid());
    const QString path = missing_paths.filePath(QStringLiteral("missing-signer"));
    QVERIFY(!QFileInfo::exists(path));
    const size_t core_executions_before = core_executions.load();
    model.requestExternalSignerPathValidation(path);
    QVERIFY(QTest::qWaitFor([&] { return !model.signerPathValidationPending(); }, 5000));
    QCOMPARE(model.signerPathError(), QStringLiteral("The configured signer path does not exist."));
    QCOMPARE(core_executions.load(), core_executions_before + 1);
    QVERIFY(core_thread.load());
    QVERIFY(core_thread.load() != QThread::currentThread());

    QSignalSpy shutdown_finished{&model, &OptionsQmlModel::shutdownFinished};
    model.beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(shutdown_finished.size(), 1, 5000);
    executor.stopAndJoin();
    workers.stopAndJoin();
}

void OptionsModelTests::connectionDirtyTracksRestartSettings()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    QVERIFY(prune_write.isStr());
    QCOMPARE(prune_write.get_str(), std::string{"0"});
}

void OptionsModelTests::developerDirtyTracksRestartSettings()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    OptionsQmlModel model(node, model_args);
    QTRY_VERIFY(model.settingsReady());
    QTRY_VERIFY(!model.settingsPending());
    QVERIFY(CheckedProxyLiteral(model,"127.0.0.1:9050").isEmpty());
    QVERIFY(CheckedProxyLiteral(model,"[::1]:9050").isEmpty());
    QVERIFY(CheckedProxyLiteral(model,"127.0.0.1").isEmpty());
    QVERIFY(CheckedProxyLiteral(model,"[::1]").isEmpty());
    QVERIFY(CheckedProxyLiteral(model,"proxy.example:9050").isEmpty());
    QVERIFY(CheckedProxyLiteral(model,"proxy.example").isEmpty());
#ifdef HAVE_SOCKADDR_UN
    QVERIFY(CheckedProxyLiteral(model,"unix:/tmp/bitcoin-core-proxy.sock").isEmpty());
#else
    QVERIFY(!CheckedProxyLiteral(model,"unix:/tmp/bitcoin-core-proxy.sock").isEmpty());
#endif
    QVERIFY(!CheckedProxyLiteral(model,"abc..abc:23456").isEmpty());
    QVERIFY(!CheckedProxyLiteral(model,"999.999.999.999:9050").isEmpty());
    QVERIFY(!CheckedProxyLiteral(model,"127.0.0.1:0").isEmpty());
    QVERIFY(!CheckedProxyLiteral(model,"127.0.0.1:65536").isEmpty());
    QVERIFY(!CheckedProxyLiteral(model,"127.0.0.1:9050=ipv4").isEmpty());
    QVERIFY(!CheckedProxyLiteral(model,"0=cjdns").isEmpty());
    QVERIFY(!AwaitProxyLocation(model,false,"999.999.999.999:9050"));
    QVERIFY(model.proxyAddress().isEmpty());

    QVERIFY(AwaitProxyLocation(model,false,"proxy.example"));
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.proxyAddress(), QString("proxy.example"));
}

void OptionsModelTests::proxyDisabledPreservesPreviousValue()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    QVERIFY(proxy_write.isBool());
    QVERIFY(!proxy_write.get_bool());
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

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    const auto drain_native_owners=DrainExecutorsBeforeNativeOwnerDestruction();
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
    const auto drain_native_owners=DrainExecutorsBeforeNativeOwnerDestruction();
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

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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

    QVERIFY(QmlDataDir::PersistDefaultDataDirSelection());
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
        QVERIFY(!settings.rw_settings.contains("prune"));
        QVERIFY(!settings.rw_settings.contains("proxy"));
        QVERIFY(!settings.rw_settings.contains("onion"));
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
        PreviewOnNative(preview_argv, /*can_listen_ipc=*/false, data_dir.path())
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
    QVERIFY2(ApplyOnboardingModelRequest(model,apply_args,&apply_error), qPrintable(apply_error));

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

    QTemporaryFile unavailable_selection;
    QVERIFY(unavailable_selection.open());
    const QmlOnboardingSettings::PreviewResult preview{
        PreviewOnNative(argv, /*can_listen_ipc=*/false, QmlOnboardingSettings::DataDirSelection{
            unavailable_selection.fileName(), QmlOnboardingSettings::DataDirSource::Default})
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
        PreviewOnNative(preview_argv, /*can_listen_ipc=*/false, data_dir.path())
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
        QVERIFY(!settings.rw_settings.contains("server"));
        QVERIFY(!settings.rw_settings.contains("proxy"));
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
        PreviewOnNative(
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
        PreviewOnNative(
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
    QVERIFY2(ApplyOnboardingModelRequest(model,args,&apply_error), qPrintable(apply_error));

    QVERIFY(!settings.contains(SettingsKeys::DATA_DIR));
    QCOMPARE(SettingToBool(args.GetPersistentSetting("qml_onboarded")), true);
}

void OptionsModelTests::onboardingApplyReportsDefaultDataDirSaveFailure()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    // Fail QSettings writes independently of filesystem permissions and platform.
    static const auto format = QSettings::registerFormat(QStringLiteral("onboarding-write-failure"),
        [](QIODevice&, QSettings::SettingsMap&) { return true; },
        [](QIODevice&, const QSettings::SettingsMap&) { return false; });
    QVERIFY(format != QSettings::InvalidFormat);
    QSettings::setPath(format, QSettings::UserScope, directory.path());
    SavedSettingsFormat settings_format{format};

    // Keep Core's files in the fixture while exercising the default GUI selection.
    const QString config_path = directory.filePath(QStringLiteral("bitcoin.conf"));
    QFile config{config_path};
    QVERIFY(config.open(QIODevice::WriteOnly));
    QVERIFY(config.write(QStringLiteral("regtest=1\ndatadir=%1\n").arg(directory.path()).toUtf8()) > 0);
    config.close();
    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, {"bitcoinqml", "-conf=" + config_path.toStdString()}, parse_error), parse_error.c_str());
    QString error;
    QVERIFY(!QmlOnboardingSettings::ApplyToArgs(args,
        QmlOnboardingSettings::DataDirSelection{QmlDataDir::DefaultDataDirString(), QmlOnboardingSettings::DataDirSource::UserSelection},
        {}, QmlCoreSettings::Values{}, &error));
    QCOMPARE(error, QStringLiteral("Unable to save the selected data directory."));
    QVERIFY(QFileInfo(directory.filePath(QStringLiteral("regtest/settings.json"))).isFile());
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
    const auto drain = DrainExecutorsBeforeNativeOwnerDestruction();
    OnboardingOptionsModel model(argv, /*can_listen_ipc=*/false);
    const auto unblock = qScopeGuard([&] { release.release(); });
    if (outcome == "settings-error") {
        QVERIFY(QDir().mkpath(data_dir.filePath("regtest/settings.json")));
    }
    QSignalSpy prepared{&model, &OnboardingOptionsModel::nodePrepared};
    if(outcome!="settings-error")QTRY_VERIFY(model.canFinish());
    model.prepareNode(args, [&] {
        ++calls;
        off_gui = QThread::currentThread() != gui_thread;
        entered.release();
        release.acquire();
        if (outcome == "exception") throw std::runtime_error{"preparation failed"};
        if (outcome == "unknown") throw 42;
        return outcome == "success";
    });
    if(outcome!="settings-error")model.prepareNode(args, [&] { ++calls; return true; });
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

    // Preparation has already drained the executor. Shutdown must still finish once.
    QSignalSpy shutdown_finished{&model, &OnboardingOptionsModel::shutdownFinished};
    model.beginShutdown();
    model.beginShutdown();
    QVERIFY(shutdown_finished.empty());
    QTRY_COMPARE(shutdown_finished.size(), 1);
    model.beginShutdown();
    QCoreApplication::processEvents();
    QCOMPARE(shutdown_finished.size(), 1);
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

    QVERIFY(QmlDataDir::PersistDefaultDataDirSelection());
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
        PreviewOnNative(argv, /*can_listen_ipc=*/false, data_dir.path())
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
        PreviewOnNative(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
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
        PreviewOnNative(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
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
        PreviewOnNative(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.existing_profile);
    QVERIFY(preview.profile.has_chain_data);
    QVERIFY(!preview.profile.has_wallet_data);
    QVERIFY(!QFileInfo::exists(blocks_dir));
}

#ifdef ENABLE_WALLET
void OptionsModelTests::onboardingPreviewDetectsRootWalletProfile()
{
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir data_dir;
    QVERIFY(data_dir.isValid());
    const QString wallet_dir = QDir(data_dir.path()).filePath(QStringLiteral("regtest/rootwallet"));
    WriteSqliteWalletMarker(wallet_dir);

    const QmlOnboardingSettings::PreviewResult preview{
        PreviewOnNative(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.existing_profile);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(preview.profile.has_wallet_data);
}

#endif

#ifdef ENABLE_WALLET
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
        PreviewOnNative(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(preview.profile.existing_profile);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(preview.profile.has_wallet_data);
}

#endif

#ifdef ENABLE_WALLET
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
        PreviewOnNative(argv, /*can_listen_ipc=*/false, data_dir.path())
    };
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.profile.existing_profile);
    QVERIFY(!preview.profile.has_chain_data);
    QVERIFY(!preview.profile.has_wallet_data);
}

#endif

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
        PreviewOnNative(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
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
        has_listen_override = settings.rw_settings.contains("listen");
        has_prune_override = settings.rw_settings.contains("prune");
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
    QVERIFY2(ApplyOnboardingModelRequest(model,args,&apply_error), qPrintable(apply_error));
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
        PreviewOnNative(TestArgv(), /*can_listen_ipc=*/false, data_dir.path())
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

void OptionsModelTests::onboardingStorageCheckDoesNotPublishStalePath()
{
    OnboardingStorageWork graph;
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir directories;
    QVERIFY(directories.isValid());
    const QString startup_path = QDir(directories.path()).filePath(QStringLiteral("startup"));
    const QString latest_path = QDir(directories.path()).filePath(QStringLiteral("latest-existing"));
    const QString stale_path = QDir(directories.path()).filePath(QStringLiteral("stale-new"));
    QVERIFY(QDir().mkpath(startup_path));
    QVERIFY(QDir().mkpath(latest_path));
    QVERIFY(!QFileInfo::exists(stale_path));

    {
        OnboardingOptionsModel model{TestArgvWithDataDir(startup_path), /*can_listen_ipc=*/false, nullptr,
            [&](auto turn) { graph.posts.push_back(std::move(turn)); }, &graph.worker.executor()};
        const auto shutdown = qScopeGuard([&] { model.beginShutdown(); graph.settle(); });
        QVERIFY(graph.settle());
        QVERIFY(graph.worker.quiescent());
        QVERIFY(!model.validationPending());
        QVERIFY(!model.storageCheckPending());
        QCOMPARE(model.dataDir(), startup_path);
        QCOMPARE(model.storagePathMessage(), QStringLiteral("Directory already exists."));

        QSignalSpy storage_changed{&model, &OnboardingOptionsModel::storageStatusChanged};
        QVERIFY(model.selectCustomDataDir(stale_path));
        QVERIFY(graph.driveToStorageCheck(model, stale_path));
        QVERIFY(!QFileInfo::exists(stale_path));
        QVERIFY(model.storageCheckPending());
        QCOMPARE(model.storagePathMessage(), QStringLiteral("Directory already exists."));

        QVERIFY(model.selectCustomDataDir(latest_path));
        QVERIFY(model.validationPending());
        const int changes_before_stale_result = storage_changed.size();

        // The first queued Core task is the stale-path disk result. Its outcome
        // must not publish while the replacement path is being previewed.
        QVERIFY(graph.worker.step(execution::Lane::Core));
        QVERIFY(graph.posts.empty());
        QVERIFY(graph.worker.step(execution::Lane::Utility));
        auto disk_publication = graph.takePost();
        QVERIFY(disk_publication);
        disk_publication();
        QVERIFY(model.validationPending());
        QVERIFY(model.storageCheckPending());
        QCOMPARE(model.storagePathMessage(), QStringLiteral("Directory already exists."));
        QCOMPARE(storage_changed.size(), changes_before_stale_result);

        QVERIFY(graph.driveToStorageCheck(model, latest_path));
        QVERIFY(graph.worker.step(execution::Lane::Core));
        QVERIFY(graph.posts.empty());
        QVERIFY(graph.worker.step(execution::Lane::Utility));
        QVERIFY(graph.publish());
        QVERIFY(graph.settle());

        QCOMPARE(model.dataDir(), latest_path);
        QVERIFY(!model.validationPending());
        QVERIFY(!model.storageCheckPending());
        QCOMPARE(model.storagePathMessage(), QStringLiteral("Directory already exists."));
        QVERIFY(model.storageErrorText().isEmpty());
        QVERIFY(storage_changed.size() > changes_before_stale_result);
        QVERIFY(graph.worker.quiescent());
        model.beginShutdown();
        QVERIFY(graph.settle());
    }
    QVERIFY(graph.settle());
    QVERIFY(graph.worker.quiescent());
}

void OptionsModelTests::onboardingStorageProjectionCoalescesPublicPruneChoice()
{
    OnboardingStorageWork graph;
    SavedGuiDataDirSettings saved_settings;
    QTemporaryDir directories;
    QVERIFY(directories.isValid());
    const QString startup_path = QDir(directories.path()).filePath(QStringLiteral("startup"));
    const QString selected_path = QDir(directories.path()).filePath(QStringLiteral("selected-new"));
    QVERIFY(QDir().mkpath(startup_path));
    QVERIFY(!QFileInfo::exists(selected_path));

    {
        OnboardingOptionsModel model{TestArgvWithDataDir(startup_path), /*can_listen_ipc=*/false, nullptr,
            [&](auto turn) { graph.posts.push_back(std::move(turn)); }, &graph.worker.executor()};
        const auto shutdown = qScopeGuard([&] { model.beginShutdown(); graph.settle(); });
        QVERIFY(graph.settle());
        QVERIFY(graph.worker.quiescent());
        QVERIFY(!model.validationPending());
        QVERIFY(!model.storageCheckPending());

        QSignalSpy storage_changed{&model, &OnboardingOptionsModel::storageStatusChanged};
        QSignalSpy can_finish_changed{&model, &OnboardingOptionsModel::canFinishChanged};
        QVERIFY(model.selectCustomDataDir(selected_path));
        QVERIFY(graph.driveToStorageCheck(model, selected_path));
        QVERIFY(graph.worker.step(execution::Lane::Core));
        QVERIFY(graph.posts.empty());
        QVERIFY(graph.worker.step(execution::Lane::Utility));
        QVERIFY(graph.publish());
        // Other lifecycle publications share this post port. Release them
        // while holding every further Utility effect and its projection.
        while (graph.publish()) {}
        // The disk read is complete, but its derived presentation is pending.
        QVERIFY(model.storageCheckPending());
        QVERIFY(graph.posts.empty());

        // Run the queued immutable projection and hold its GUI publication.
        QVERIFY(graph.worker.step(execution::Lane::Utility));
        QVERIFY(model.storageCheckPending());
        auto old_projection = graph.takePost();
        QVERIFY(old_projection);

        const int statuses_before_user_choice = storage_changed.size();
        const bool selected_prune_choice = !model.prune();
        model.setPrune(selected_prune_choice);
        QVERIFY(graph.settle());
        QCOMPARE(model.prune(), selected_prune_choice);
        QVERIFY(storage_changed.size() > statuses_before_user_choice);
        const int statuses_before_old_publish = storage_changed.size();
        const int finish_signals_before_old_publish = can_finish_changed.size();

        old_projection();
        QCOMPARE(model.prune(), selected_prune_choice);
        QCOMPARE(storage_changed.size(), statuses_before_old_publish);
        QCOMPARE(can_finish_changed.size(), finish_signals_before_old_publish);
        QCOMPARE(graph.posts.size(), size_t{1});
        QVERIFY(graph.publish()); // Admit the queued successor projection.
        QVERIFY(graph.worker.step(execution::Lane::Utility));
        auto successor_publication = graph.takePost();
        QVERIFY(successor_publication);
        const auto statuses_before_successor = storage_changed.size();
        const auto finish_before_successor = can_finish_changed.size();
        successor_publication();
        QCOMPARE(storage_changed.size(), statuses_before_successor + 1);
        QCOMPARE(can_finish_changed.size(), finish_before_successor + 1);
        QVERIFY(graph.settle()); // Independent flow/pending transitions may notify readiness.

        QCOMPARE(model.prune(), selected_prune_choice);
        QVERIFY(!model.storageCheckPending());
        QVERIFY(model.storageErrorText().isEmpty());
        const int expected_selected_storage_gb = selected_prune_choice && model.pruneSizeGB() <= model.assumedBlockchainSize()
            ? model.prunedStorageRequiredGB()
            : model.fullStorageRequiredGB();
        QCOMPARE(model.selectedStorageRequiredGB(), expected_selected_storage_gb);
        QCOMPARE(storage_changed.size(), statuses_before_old_publish + 1);
        QVERIFY(graph.worker.quiescent());
        model.beginShutdown();
        QVERIFY(graph.settle());
    }
    QVERIFY(graph.settle());
    QVERIFY(graph.worker.quiescent());
}

void OptionsModelTests::thirdPartyTransactionLinksParseValidUrls()
{

    QSettings settings;
    const bool had_urls_setting = settings.contains(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS);
    const QVariant previous_urls_setting = settings.value(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS);

    MockNode node;

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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
    std::unique_ptr<OptionsTransactionLinks> link_model{model.createTransactionLinks()};
    QVERIFY(link_model);link_model->setTxid("abc");QTRY_VERIFY(!link_model->pending());
    QVariantList links;for(int row=0;row<link_model->rowCount();++row)links.push_back(QVariantMap{
        {"host",link_model->data(link_model->index(row),OptionsTransactionLinks::HostRole)},
        {"url",link_model->data(link_model->index(row),OptionsTransactionLinks::UrlRole)}});
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

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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



void OptionsModelTests::rejectedDisplayRefreshDoesNotReenterItsFailure()
{
    SavedGuiDataDirSettings saved;MockNode node;ArgsManager args;QTemporaryDir directory;QVERIFY(directory.isValid());PrepareRuntimePersistence(node,args,directory);ManualSnapshotGraph graph; ManualStartupExecution shared;
    {OptionsQmlModel model{node,args,graph.services(),&shared.executor()};bool delivering{false},reentered{false};int notices{0};
     const auto finish=qScopeGuard([&]{disconnect(&model,nullptr,this,nullptr);model.beginShutdown();graph.drainLive(&shared);});
     QVERIFY(graph.drainLive(&shared));shared.reject_core = true;
     QVERIFY(model.settingsReady());
     QCOMPARE(model.settingsOperationState(),OptionsQmlModel::SettingsApplied);
     const auto original=model.settingsOperationId();
     QSignalSpy operations{&model,&OptionsQmlModel::settingsOperationChanged};
     connect(&model,&OptionsQmlModel::settingsErrorChanged,this,[&]{reentered|=delivering;delivering=true;if(++notices<=2)model.refreshDisplaySettings();delivering=false;});
     model.refreshDisplaySettings();QVERIFY(graph.drainLive(&shared));QVERIFY(!reentered);QCOMPARE(notices,1);QCOMPARE(model.settingsOperationId(),original);QVERIFY(!model.settingsPending());QVERIFY(!model.settingsError().isEmpty());
     // A refused observation reports its diagnostic without inventing a user
     // operation or replacing the original accepted operation's receipt.
     QCOMPARE(model.settingsOperationState(),OptionsQmlModel::SettingsApplied);
     QCOMPARE(operations.count(),0);
    }
    QVERIFY(graph.drainLive(&shared));QVERIFY(graph.transport.quiescent());
}

void OptionsModelTests::databaseCacheMatchesCoreDefaultAndRange()
{
    SavedGuiDataDirSettings saved;
    MockNode node;
    const auto drain = DrainExecutorsBeforeNativeOwnerDestruction();
    ArgsManager args; QTemporaryDir directory; QVERIFY(directory.isValid());
    PrepareRuntimePersistence(node, args, directory);
    OptionsQmlModel model{node, args};
    QTRY_VERIFY(model.settingsReady()); QTRY_VERIFY(!model.settingsPending());
    // Core's public default is an independent oracle; the 8 MiB coins
    // sub-cache is not the total -dbcache bound.
    const auto expected = node::GetDefaultDBCache() / 1_MiB;
    QCOMPARE(model.dbcacheSizeMiB(), int(expected));
    QVERIFY(model.maxDbcacheSizeMiB() >= model.dbcacheSizeMiB());
    QVERIFY(model.maxDbcacheSizeMiB() >= 1636);
    QCOMPARE(SettingTo<int64_t>(QmlCoreSettings::DefaultCoreSettingValue(QStringLiteral("dbcache"))).value_or(-1), int64_t(expected));
    model.setDbcacheSizeMiB(1636);
    QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("dbcache")).value_or(-1), int64_t{1636});
}
void OptionsModelTests::unavailableLanguageDoesNotPersist_data()
{
    QTest::addColumn<QString>("literal");
    QTest::newRow("not-installed") << QStringLiteral("not_an_installed_translation");
    QTest::newRow("over-budget") << QString(65, QLatin1Char('x'));
    QTest::newRow("embedded-null") << QString{QChar::Null};
}
void OptionsModelTests::unavailableLanguageDoesNotPersist()
{
    QFETCH(QString, literal);
    SavedGuiDataDirSettings saved; QSettings settings;
    settings.setValue(SettingsKeys::LANGUAGE, QString{}); settings.sync();
    MockNode node; ArgsManager args; QTemporaryDir directory; QVERIFY(directory.isValid());
    PrepareRuntimePersistence(node, args, directory);
    ManualSnapshotGraph graph;
    { OptionsQmlModel model{node, args, graph.services()};
      const auto finish = qScopeGuard([&] { model.beginShutdown(); graph.drainLive(); });
      QVERIFY(graph.drainLive()); QCOMPARE(model.language(), QString{});
      model.setLanguage(literal); QVERIFY(graph.drainLive());
      QCOMPARE(model.language(), QString{});
      QVERIFY(!model.settingsError().isEmpty());
      QVERIFY(args.GetPersistentSetting("lang").isNull());
      settings.sync(); QCOMPARE(settings.value(SettingsKeys::LANGUAGE).toString(), QString{});
    }
    QVERIFY(graph.drainLive()); QVERIFY(graph.transport.quiescent());
}
void OptionsModelTests::displayReadbackRepairsInvalidStoredUnit_data()
{
    QTest::addColumn<QVariant>("stored"); QTest::addColumn<int>("expected");
    QTest::newRow("negative") << QVariant{-1} << 0;
    QTest::newRow("above-catalog") << QVariant{99} << 0;
    QTest::newRow("text") << QVariant{QStringLiteral("not-a-unit")} << 0;
    QTest::newRow("integer-overflow") << QVariant{QStringLiteral("99999999999999999999")} << 0;
    QTest::newRow("fractional") << QVariant{1.5} << 0;
    QTest::newRow("valid-sat") << QVariant{3} << 3;
}
void OptionsModelTests::displayReadbackRepairsInvalidStoredUnit()
{
    QFETCH(QVariant, stored); QFETCH(int, expected);
    SavedGuiDataDirSettings saved; QSettings settings;
    settings.setValue(SettingsKeys::DISPLAY_UNIT, stored); settings.sync();
    MockNode node; const auto drain = DrainExecutorsBeforeNativeOwnerDestruction();
    ArgsManager args; QTemporaryDir directory; QVERIFY(directory.isValid());
    PrepareRuntimePersistence(node, args, directory);
    OptionsQmlModel model{node, args};
    QTRY_VERIFY(model.settingsReady()); QTRY_VERIFY(!model.settingsPending());
    QCOMPARE(model.displayUnit(), expected);
    settings.sync(); QCOMPARE(settings.value(SettingsKeys::DISPLAY_UNIT).toInt(), expected);
    bool valid{false}; settings.value(SettingsKeys::DISPLAY_UNIT).toInt(&valid); QVERIFY(valid);
    // A second independent model/restart sees the same canonical persisted unit.
    OptionsQmlModel restarted{node, args}; QTRY_VERIFY(restarted.settingsReady());
    QTRY_VERIFY(!restarted.settingsPending()); QCOMPARE(restarted.displayUnit(), expected);
}
void OptionsModelTests::languageReadbackRejectsUnavailablePersistedTag()
{
    SavedGuiDataDirSettings saved; QSettings settings;
    settings.setValue(SettingsKeys::LANGUAGE, QStringLiteral("invalid_locale_that_has_no_translation")); settings.sync();
    MockNode node; const auto drain = DrainExecutorsBeforeNativeOwnerDestruction();
    ArgsManager args; QTemporaryDir directory; QVERIFY(directory.isValid());
    PrepareRuntimePersistence(node, args, directory);
    OptionsQmlModel model{node, args}; QTRY_VERIFY(model.settingsReady()); QTRY_VERIFY(!model.settingsPending());
    QVERIFY(model.availableLanguages().contains(model.language()));
    QCOMPARE(model.language(), QString{});
    settings.sync(); QCOMPARE(settings.value(SettingsKeys::LANGUAGE).toString(), QString{});
    QVERIFY(args.GetPersistentSetting("lang").isNull());
}

void OptionsModelTests::invalidCoreLanguageDoesNotEraseAnIndependentGuiPreference()
{
    SavedGuiDataDirSettings saved;QSettings settings;settings.setValue(SettingsKeys::LANGUAGE,QStringLiteral("de"));settings.sync();
    MockNode node;const auto drain=DrainExecutorsBeforeNativeOwnerDestruction();ArgsManager args;QTemporaryDir directory;QVERIFY(directory.isValid());PrepareRuntimePersistence(node,args,directory);
    args.LockSettings([](common::Settings& values){values.rw_settings["lang"]=std::string{"unavailable_external_core_language"};});
    OptionsQmlModel model{node,args};QTRY_VERIFY(model.settingsReady());QTRY_VERIFY(!model.settingsPending());
    settings.sync();QCOMPARE(settings.value(SettingsKeys::LANGUAGE).toString(),QStringLiteral("de"));
    QCOMPARE(model.language(),QStringLiteral("unavailable_external_core_language"));QVERIFY(model.languageSummary().contains(QStringLiteral("Unavailable translation")));
    QCOMPARE(SettingToString(args.GetPersistentSetting("lang")).value_or(""),std::string{"unavailable_external_core_language"});
    model.setLanguage({});QTRY_VERIFY(!model.settingsPending());QCOMPARE(model.language(),QString{});
}

void OptionsModelTests::manualDisplayRefreshWaitsForPendingWrite()
{
    QTemporaryDir settings_dir; QVERIFY(settings_dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    SavedSettingsFormat settings_format{QSettings::IniFormat};
    SavedGuiDataDirSettings saved; QSettings settings;
    settings.setValue(SettingsKeys::DISPLAY_UNIT, 0); settings.sync();
    MockNode node; ArgsManager args; QTemporaryDir directory; QVERIFY(directory.isValid());
    PrepareRuntimePersistence(node, args, directory); ManualSnapshotGraph graph; ManualStartupExecution shared;
    { OptionsQmlModel model{node, args, graph.services(), &shared.executor()};
      const auto finish = qScopeGuard([&] { model.beginShutdown(); graph.drainLive(&shared); });
      QVERIFY(graph.drainLive(&shared)); model.setDisplayUnit(3);
      // Finish background work, retain its GUI outcome, then change the store.
      shared.drain(); QVERIFY(model.settingsPending());
      settings.setValue(SettingsKeys::DISPLAY_UNIT, 1); settings.sync();
      QVERIFY(QMetaObject::invokeMethod(&model, "refreshDisplaySettings", Qt::DirectConnection));
      QVERIFY(QMetaObject::invokeMethod(&model, "refreshDisplaySettings", Qt::DirectConnection));
      QCOMPARE(model.displayUnit(), 3); QVERIFY(model.settingsPending());
      QVERIFY(graph.drainLive(&shared)); QCOMPARE(model.displayUnit(), 1); QVERIFY(!model.settingsPending());
      settings.sync(); QCOMPARE(settings.value(SettingsKeys::DISPLAY_UNIT).toInt(), 1);
      model.beginShutdown(); QVERIFY(graph.drainLive(&shared));
      const auto pending = shared.coreSubmissions();
      QVERIFY(QMetaObject::invokeMethod(&model, "refreshDisplaySettings", Qt::DirectConnection));
      QCOMPARE(shared.coreSubmissions(), pending);
    }
    QVERIFY(graph.drainLive(&shared)); QVERIFY(graph.transport.quiescent());
}
void OptionsModelTests::externalDisplayWriterIsObservedWithoutUserEdit()
{
    // The model observes external writers through a file watch. With the
    // native macOS format the plist is written by cfprefsd some seconds
    // after sync(), so the wait measured the daemon, not the model. An INI
    // file is written by QSettings itself and the watch fires on sync().
    QTemporaryDir settings_dir; QVERIFY(settings_dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    SavedSettingsFormat settings_format{QSettings::IniFormat};
    SavedGuiDataDirSettings saved; QSettings settings;
    settings.setValue(SettingsKeys::DISPLAY_UNIT, 0); settings.sync();
    MockNode node; const auto drain = DrainExecutorsBeforeNativeOwnerDestruction();
    ArgsManager args; QTemporaryDir directory; QVERIFY(directory.isValid());
    PrepareRuntimePersistence(node, args, directory);
    OptionsQmlModel model{node, args}; QTRY_VERIFY(model.settingsReady()); QTRY_VERIFY(!model.settingsPending());
    QSignalSpy changes{&model, &OptionsQmlModel::displayUnitChanged};
    { QSettings external; external.setValue(SettingsKeys::DISPLAY_UNIT, 2); external.sync(); }
    QTRY_COMPARE(model.displayUnit(), 2); QTRY_VERIFY(!model.settingsPending()); QVERIFY(!changes.empty());
    { QSettings external; external.remove(SettingsKeys::DISPLAY_UNIT); external.sync(); }
    // Legacy fallback remains supported; removing a key is observed too.
    QTRY_COMPARE(model.displayUnit(), QmlLegacySettings::ReadLegacyGuiDisplayUnit(QString::fromStdString(args.GetChainTypeString()), 0));
}

void OptionsModelTests::hideAmountsPersists()
{
    QTemporaryDir settings_dir;
    QVERIFY(settings_dir.isValid());
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, settings_dir.path());
    SavedSettingsFormat settings_format{QSettings::IniFormat};
    QSettings settings;
    const bool had_setting = settings.contains(SettingsKeys::HIDE_AMOUNTS);
    const QVariant previous_setting = settings.value(SettingsKeys::HIDE_AMOUNTS);
    settings.remove(SettingsKeys::HIDE_AMOUNTS);

    MockNode node;
    ArgsManager model_args;
    QTemporaryDir model_settings_dir;
    QVERIFY(model_settings_dir.isValid());
    PrepareRuntimePersistence(node, model_args, model_settings_dir);
    {
        OptionsQmlModel model(node, model_args);
        QTRY_VERIFY(model.settingsReady());
        QCOMPARE(model.hideAmounts(), false);
        QSignalSpy spy(&model, &OptionsQmlModel::hideAmountsChanged);
        model.setHideAmounts(true);
        model.setHideAmounts(true);
        QCOMPARE(spy.count(), 1);
        QTRY_VERIFY(!model.settingsPending());
        QCOMPARE(settings.value(SettingsKeys::HIDE_AMOUNTS).toBool(), true);
    }
    OptionsQmlModel reloaded(node, model_args);
    QTRY_VERIFY(reloaded.settingsReady());
    QCOMPARE(reloaded.hideAmounts(), true);

    if (had_setting) {
        settings.setValue(SettingsKeys::HIDE_AMOUNTS, previous_setting);
    } else {
        settings.remove(SettingsKeys::HIDE_AMOUNTS);
    }
}

void OptionsModelTests::displayUnitUsesQtCompatibleSettingsKey()
{

    QSettings settings;
    const bool had_display_unit_setting = settings.contains(SettingsKeys::DISPLAY_UNIT);
    const QVariant previous_display_unit_setting = settings.value(SettingsKeys::DISPLAY_UNIT);
    settings.setValue(SettingsKeys::DISPLAY_UNIT, QVariant::fromValue(LegacyDisplayUnit::SAT));

    MockNode node;

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();

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

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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

void OptionsModelTests::sharedCoreSettingHelpersPreserveExplicitOverrides()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["listen"].emplace_back(false);
    });

    QVariantMap status = QmlCoreSettings::CoreSettingStatus(args, QStringLiteral("listen"));
    QCOMPARE(status.value("source").toString(), QString("bitcoin_conf"));
    QCOMPARE(status.value("canEdit").toBool(), true);

    QVERIFY(QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("listen"), common::SettingsValue{false}));
    bool has_listen_override{true};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.contains("listen");
    });
    QVERIFY(has_listen_override);
    QVERIFY(!SettingToBool(args.GetPersistentSetting("listen")).value_or(true));

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
        has_mempool_override = settings.rw_settings.contains("maxmempool");
    });
    QVERIFY(has_mempool_override);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("maxmempool")).value_or(-1), int64_t{DEFAULT_MAX_MEMPOOL_SIZE_MB});
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
        has_listen_override = settings.rw_settings.contains("listen");
    });
    QVERIFY(has_listen_override);
    QVERIFY(!SettingToBool(listen_args.GetPersistentSetting("listen")).value_or(true));

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

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
        settings.command_line_options["listen"].emplace_back(true);
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
        has_listen_override = settings.rw_settings.contains("listen");
    });
    QVERIFY(!has_listen_override);
}

void OptionsModelTests::coreSettingsSessionDoesNotCopyUntouchedConfig()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["listen"].emplace_back(false);
    });

    QmlCoreSettings::Session session{
        QmlCoreSettings::Values{.listen = false},
        QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())
    };
    QVERIFY(session.writeTouchedToArgs(args));

    bool has_listen_override{false};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.contains("listen");
    });
    QVERIFY(!has_listen_override);
}

void OptionsModelTests::coreSettingsSessionWritesTouchedConfigOverride()
{
    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["listen"].emplace_back(false);
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

void OptionsModelTests::coreSettingsSessionRevertingToDefaultPersistsExplicitOverride()
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
        has_listen_override = settings.rw_settings.contains("listen");
    });
    QVERIFY(has_listen_override);
    QCOMPARE(SettingToBool(args.GetPersistentSetting("listen")).value_or(!DEFAULT_LISTEN), DEFAULT_LISTEN);
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
        has_prune_override = settings.rw_settings.contains("prune");
    });
    QVERIFY(prune_prev.isStr());
    QCOMPARE(QString::fromStdString(prune_prev.get_str()), QString::number(QmlCoreSettings::PruneGBToMiB(10)));
    QVERIFY(has_prune_override);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("prune")).value_or(-1), int64_t{0});
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
        has_prune_prev = settings.rw_settings.contains("prune-prev");
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
        has_proxy_override = settings.rw_settings.contains("proxy");
    });
    QCOMPARE(QString::fromStdString(proxy_prev.get_str()), QStringLiteral("10.0.0.2:9050"));
    QVERIFY(has_proxy_override);
    QVERIFY(!SettingToBool(args.GetPersistentSetting("proxy")).value_or(true));
}

void OptionsModelTests::coreSettingsLoadPersistentPrunePreviousValue()
{
    MockNode node;
    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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

void OptionsModelTests::coreSettingsSessionProxyCommitAcceptsUnchangedAddressAsExplicitIntent()
{
    QmlCoreSettings::Session session;
    const QString address = session.defaultProxyAddress();

    const QmlCoreSettings::Change change = session.changeProxyLocation(address);

    QVERIFY(change.accepted);
    QCOMPARE(change.setting_name, QStringLiteral("proxy"));
    QVERIFY(!QmlCoreSettings::ValuesChanged(change));
    QVERIFY(!QmlCoreSettings::ProxyAddressChanged(change));
    QVERIFY(session.isTouched(QStringLiteral("proxy")));
    QVERIFY(change.force_write);
}

void OptionsModelTests::coreSettingStatusTracksSourcePrecedence()
{

    {
        ArgsManager args;
        args.SelectConfigNetwork("main");
        args.LockSettings([](common::Settings& settings) {
            settings.ro_config[""]["listen"].emplace_back(true);
        });

        MockNode node;

        const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
            settings.ro_config[""]["listen"].emplace_back(true);
            settings.rw_settings["listen"] = common::SettingsValue{false};
        });

        MockNode node;

        const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
            settings.ro_config[""]["listen"].emplace_back(true);
            settings.rw_settings["listen"] = common::SettingsValue{false};
            settings.command_line_options["listen"].emplace_back(true);
        });

        MockNode node;

        const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
        settings.command_line_options["listen"].emplace_back(false);
        settings.ro_config[""]["listen"].emplace_back(true);
    });

    MockNode node;

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
        settings.command_line_options["server"].emplace_back(true);
        settings.rw_settings["prune"] = MakeInt(0);
        settings.command_line_options["prune"].push_back(MakeInt(QmlCoreSettings::PruneGBToMiB(10)));
        settings.rw_settings["proxy"] = common::SettingsValue{false};
        settings.command_line_options["proxy"].emplace_back(std::string{"10.0.0.1:9050"});
        settings.rw_settings["onion"] = common::SettingsValue{false};
        settings.command_line_options["onion"].emplace_back(std::string{"127.0.0.1:9150"});
        settings.rw_settings["dbcache"] = MakeInt(300);
        settings.command_line_options["dbcache"].push_back(MakeInt(600));
        settings.rw_settings["par"] = MakeInt(1);
        settings.command_line_options["par"].push_back(MakeInt(4));
        settings.rw_settings["maxmempool"] = MakeInt(300);
        settings.command_line_options["maxmempool"].push_back(MakeInt(500));
        settings.rw_settings["signer"] = common::SettingsValue{std::string{"saved-signer"}};
        settings.command_line_options["signer"].emplace_back(std::string{"cli-signer"});
    });

    MockNode node;

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    std::string parse_error;
    {
    std::vector<std::string> proxy_argv = TestArgv();
    proxy_argv.emplace_back("-proxy=127.0.0.1:9050");

    ArgsManager proxy_args;
    QVERIFY2(PrepareTestArgs(proxy_args, proxy_argv, parse_error), parse_error.c_str());
    SelectParams(proxy_args.GetChainType());
    proxy_args.SelectConfigNetwork(proxy_args.GetChainTypeString());
    InitParameterInteraction(proxy_args);

    MockNode proxy_node;

    const auto drain_proxy_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    QVERIFY(disabled_write.isBool());
    QVERIFY(!disabled_write.get_bool());

    bool has_listen_override{true};
    proxy_args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.contains("listen");
    });
    QVERIFY(has_listen_override);

    } // Retire the first facade before its borrowed Node's global drain.
    std::vector<std::string> blocksonly_argv = TestArgv();
    blocksonly_argv.emplace_back("-blocksonly");

    ArgsManager blocksonly_args;
    QVERIFY2(PrepareTestArgs(blocksonly_args, blocksonly_argv, parse_error), parse_error.c_str());
    SelectParams(blocksonly_args.GetChainType());
    blocksonly_args.SelectConfigNetwork(blocksonly_args.GetChainTypeString());
    InitParameterInteraction(blocksonly_args);

    MockNode blocksonly_node;

    const auto drain_blocksonly_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
        settings.command_line_options["server"].emplace_back(true);
        settings.rw_settings["prune"] = MakeInt(0);
        settings.command_line_options["prune"].push_back(MakeInt(QmlCoreSettings::PruneGBToMiB(10)));
        settings.rw_settings["proxy"] = common::SettingsValue{std::string{"127.0.0.1:9050"}};
        settings.command_line_options["proxy"].emplace_back(std::string{"10.0.0.1:9050"});
        settings.rw_settings["dbcache"] = MakeInt(300);
        settings.command_line_options["dbcache"].push_back(MakeInt(600));
        settings.rw_settings["par"] = MakeInt(1);
        settings.command_line_options["par"].push_back(MakeInt(4));
        settings.rw_settings["maxmempool"] = MakeInt(300);
        settings.command_line_options["maxmempool"].push_back(MakeInt(500));
        settings.rw_settings["signer"] = common::SettingsValue{std::string{"saved-signer"}};
        settings.command_line_options["signer"].emplace_back(std::string{"cli-signer"});
    });

    MockNode node;

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("prune")).value_or(-1), 0);
    QCOMPARE(QString::fromStdString(SettingToString(args.GetPersistentSetting("proxy")).value_or("")), QString("127.0.0.1:9050"));
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("dbcache")).value_or(-1), 300);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("par")).value_or(-1), 1);
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("maxmempool")).value_or(-1), 300);
    QCOMPARE(QString::fromStdString(SettingToString(args.GetPersistentSetting("signer")).value_or("")), QString("saved-signer"));
    QVERIFY(!model.settingsPending());
    QVERIFY(node.force_setting_arguments.empty());
    QVERIFY(!QFile::exists(model_settings_dir.filePath(QStringLiteral("runtime-settings.json"))));
}

void OptionsModelTests::revertingToConfigValuePersistsExplicitOverride()
{

    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.ro_config[""]["listen"].emplace_back(true);
        settings.rw_settings["listen"] = common::SettingsValue{false};
    });

    MockNode node;

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    QVERIFY(listen_write.isBool());
    QVERIFY(listen_write.get_bool());
}

void OptionsModelTests::revertingToDefaultValuePersistsExplicitOverride()
{

    ArgsManager args;
    args.SelectConfigNetwork("main");
    args.LockSettings([](common::Settings& settings) {
        settings.rw_settings["maxmempool"] = common::SettingsValue{456};
    });

    MockNode node;

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    // maxmempool is a native integer setting; only dbcache/par/prune use
    // the legacy numeric-string persistence representation.
    QVERIFY(mempool_write.isNum());
    QCOMPARE(mempool_write.getInt<int>(), DEFAULT_MAX_MEMPOOL_SIZE_MB);
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
        settings.command_line_options["lang"].emplace_back(std::string{"fr"});
    });

    MockNode node;

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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

void OptionsModelTests::failedProfileHandoffRetainsLegacyMigrationSource_data()
{
    QTest::addColumn<bool>("invalid_batch");
    QTest::newRow("replacement-write-fails") << false;
    QTest::newRow("whole-setup-validation-rejects") << true;
}

void OptionsModelTests::failedProfileHandoffRetainsLegacyMigrationSource()
{
    QFETCH(bool, invalid_batch);
    SavedNamedSettings current{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    legacy.settings().setValue(QStringLiteral("nDatabaseCache"), 600);
    legacy.settings().setValue(QStringLiteral("fListen"), false);
    legacy.settings().sync();
    QCOMPARE(legacy.settings().status(), QSettings::NoError);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    ArgsManager args; PrepareArgsForDataDir(args, directory.path());
    if (invalid_batch) {
        auto values = QmlCoreSettings::LoadEffectiveValues(args);
        values.proxy_enabled = true;
        values.proxy_address = QStringLiteral("invalid:0");
        QString error;
        QVERIFY(!QmlOnboardingSettings::ApplyToArgs(args, directory.path(), {QStringLiteral("proxy")}, values, &error));
        QVERIFY(!error.isEmpty());
    } else {
        QVERIFY(EnsureReplacementParent(args));
        fs::path target; QVERIFY(args.GetSettingsPath(&target));
        QVERIFY(QDir{}.mkdir(QString::fromStdString(target.utf8string())+QStringLiteral(".tmp")));
        const auto migration = QmlLegacySettings::MigrateCoreSettings(args, QmlLegacySettings::MigrationMode::Persist);
        QVERIFY(migration.settings_changed);
        QVERIFY2(migration.error.isEmpty(), qPrintable(migration.error));
        std::vector<std::string> errors;
        QVERIFY(!args.WriteSettingsFile(&errors));
        QVERIFY(!errors.empty());
    }
    legacy.settings().sync();
    QCOMPARE(legacy.settings().value(QStringLiteral("nDatabaseCache")).toInt(), 600);
    QVERIFY(legacy.settings().contains(QStringLiteral("fListen")));
    QCOMPARE(legacy.settings().value(QStringLiteral("fListen")).toBool(), false);
}

// #1073: onboarding whose settings.json.tmp cannot be created must keep the
// legacy proxy, and a retry once the target is writable migrates it.
void OptionsModelTests::failedOnboardingWriteKeepsLegacyProxyForRetry()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings current{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    legacy.settings().setValue(QStringLiteral("fUseProxy"), true);
    legacy.settings().setValue(QStringLiteral("addrProxy"), QStringLiteral("10.23.45.67:9150"));
    legacy.settings().sync();
    QTemporaryDir directory; QVERIFY(directory.isValid());
    // Each attempt is a fresh onboarding: preview, then persist its values.
    const auto attempt = [&] {
        const auto preview = PreviewOnNative(TestArgvWithDataDir(directory.path()), /*can_listen_ipc=*/false, directory.path());
        ArgsManager args; PrepareArgsForDataDir(args, directory.path());
        QmlOnboardingSettings::ApplyRequest request; request.data_dir = {directory.path(), QmlOnboardingSettings::DataDirSource::ExplicitArg};
        request.values = preview.values;
        return std::pair{preview.values, QmlOnboardingSettings::PersistProfile(args, request)};
    };
    fs::path target;
    {ArgsManager args; PrepareArgsForDataDir(args, directory.path()); QVERIFY(EnsureReplacementParent(args)); QVERIFY(args.GetSettingsPath(&target));}
    const auto settings_file = QString::fromStdString(target.utf8string());
    // A directory in its place makes Core's settings.json.tmp write fail.
    QVERIFY(QDir{}.mkdir(settings_file + QStringLiteral(".tmp")));
    const auto [preview, failed] = attempt();
    QVERIFY(preview.proxy_enabled); QCOMPARE(preview.proxy_address, QStringLiteral("10.23.45.67:9150"));
    QVERIFY(!failed.completed); QVERIFY(!failed.replacement_written); QVERIFY(!failed.error.isEmpty());
    QVERIFY(!QFileInfo::exists(settings_file));
    legacy.settings().sync();
    QVERIFY(legacy.settings().value(QStringLiteral("fUseProxy")).toBool());
    QCOMPARE(legacy.settings().value(QStringLiteral("addrProxy")).toString(), QStringLiteral("10.23.45.67:9150"));

    QVERIFY(QDir{}.rmdir(settings_file + QStringLiteral(".tmp")));
    const auto [retry_preview, retried] = attempt();
    QVERIFY(retry_preview.proxy_enabled); QCOMPARE(retry_preview.proxy_address, QStringLiteral("10.23.45.67:9150"));
    QVERIFY2(retried.completed, qPrintable(retried.error)); QVERIFY(retried.replacement_written);
    ArgsManager args; PrepareArgsForDataDir(args, directory.path()); std::vector<std::string> errors; QVERIFY(args.ReadSettingsFile(&errors));
    QCOMPARE(QString::fromStdString(args.GetPersistentSetting("proxy").get_str()), QStringLiteral("10.23.45.67:9150"));
    legacy.settings().sync();
    QVERIFY(!legacy.settings().contains(QStringLiteral("fUseProxy"))); QVERIFY(!legacy.settings().contains(QStringLiteral("addrProxy")));
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
    std::vector<std::string> replacement_errors;
    QVERIFY(EnsureReplacementParent(args));
    QVERIFY(args.WriteSettingsFile(&replacement_errors));
    QString cleanup_error;
    QVERIFY2(QmlLegacySettings::CompleteCoreSettingsMigration(args,result,&cleanup_error),qPrintable(cleanup_error));
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
        has_natpmp = core_settings.rw_settings.contains("natpmp");
        has_onion = core_settings.rw_settings.contains("onion");
        has_prune_prev = core_settings.rw_settings.contains("prune-prev");
    });

    QVERIFY(dbcache.isStr());
    QCOMPARE(QString::fromStdString(dbcache.get_str()), QStringLiteral("600"));
    QVERIFY(script_threads.isStr());
    QCOMPARE(QString::fromStdString(script_threads.get_str()), QStringLiteral("12"));
    QCOMPARE(SettingToBool(listen).value(), false);
    QVERIFY(!has_natpmp);
    QCOMPARE(SettingToBool(server).value(), true);
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

    const auto drain_node_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
        PreviewOnNative(TestArgvWithDataDir(data_dir.path()), /*can_listen_ipc=*/false, data_dir.path())
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
    std::vector<std::string> replacement_errors;
    QVERIFY(EnsureReplacementParent(args));
    QVERIFY(args.WriteSettingsFile(&replacement_errors));
    QString cleanup_error;
    QVERIFY2(QmlLegacySettings::CompleteCoreSettingsMigration(args,result,&cleanup_error),qPrintable(cleanup_error));
    QVERIFY(result.settings_changed);

    QCOMPARE(SettingToBool(args.GetPersistentSetting("listen")).value(), false);
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
    std::vector<std::string> replacement_errors;
    QVERIFY(EnsureReplacementParent(args));
    QVERIFY(args.WriteSettingsFile(&replacement_errors));
    QString cleanup_error;
    QVERIFY2(QmlLegacySettings::CompleteCoreSettingsMigration(args,result,&cleanup_error),qPrintable(cleanup_error));
    QVERIFY(!result.settings_changed);

    bool has_listen_override{true};
    args.LockSettings([&](common::Settings& core_settings) {
        has_listen_override = core_settings.rw_settings.contains("listen");
    });
    QVERIFY(!has_listen_override);
    QVERIFY(legacy_settings.settings().contains(QStringLiteral("fListen"))); // Unimported source remains recoverable.
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
    QVERIFY(AwaitCoreSettled(model));
    QVERIFY(model.listen());

    ArgsManager args;
    std::string parse_error;
    QVERIFY2(PrepareTestArgs(args, argv, parse_error), parse_error.c_str());
    QString apply_error;
    QVERIFY2(ApplyOnboardingModelRequest(model,args,&apply_error), qPrintable(apply_error));

    bool has_listen_override{true};
    common::SettingsValue listen, server;
    args.LockSettings([&](common::Settings& core_settings) {
        has_listen_override = core_settings.rw_settings.contains("listen");
        listen = core_settings.rw_settings.at("listen");
        server = core_settings.rw_settings.at("server");
    });
    // R33 retains an explicit GUI command even when it equals the default.
    QVERIFY(has_listen_override);
    QCOMPARE(SettingToBool(listen).value(), true);
    QCOMPARE(SettingToBool(server).value(), true);
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
        PreviewOnNative(TestArgv(), /*can_listen_ipc=*/false, data_dir.path())
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
    QVERIFY2(ApplyOnboardingModelRequest(model,args,&apply_error), qPrintable(apply_error));

    bool has_listen_override{false};
    args.LockSettings([&](common::Settings& settings) {
        has_listen_override = settings.rw_settings.contains("listen");
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
        PreviewOnNative(argv, /*can_listen_ipc=*/false, data_dir.path())
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
        PreviewOnNative(argv, /*can_listen_ipc=*/false, data_dir.path())
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
        has_forced_listen = settings.forced_settings.contains("listen");
        has_forced_natpmp = settings.forced_settings.contains("natpmp");
        has_forced_discover = settings.forced_settings.contains("discover");
    });

    QVERIFY(listen_override.isBool());
    QVERIFY(listen_override.get_bool());
    QVERIFY(!has_forced_listen);
    QVERIFY(!has_forced_natpmp);
    QVERIFY(!has_forced_discover);
}


void OptionsModelTests::coreApiHistoricalCacheMigration_data()
{
    QTest::addColumn<int>("version"); QTest::addColumn<int>("cache"); QTest::addColumn<qlonglong>("expected"); QTest::addColumn<bool>("persisted");
    QTest::newRow("old-default") << 120000 << 100 << static_cast<qlonglong>(node::GetDefaultDBCache() / 1_MiB) << false;
    QTest::newRow("old-custom") << 120000 << 101 << qlonglong{101} << true;
    QTest::newRow("new-custom") << 130000 << 100 << qlonglong{100} << true;
}
void OptionsModelTests::coreApiHistoricalCacheMigration()
{
    QFETCH(int, version); QFETCH(int, cache); QFETCH(qlonglong, expected); QFETCH(bool, persisted);
    SavedNamedSettings current{QStringLiteral("BitcoinCore"), QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy{QStringLiteral("Bitcoin"), QStringLiteral("Bitcoin-Qt-regtest")};
    legacy.settings().setValue(QStringLiteral("nSettingsVersion"), version);
    legacy.settings().setValue(QStringLiteral("nDatabaseCache"), cache);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    ArgsManager args; PrepareArgsForDataDir(args, directory.path());
    const auto result = QmlLegacySettings::MigrateCoreSettings(args, QmlLegacySettings::MigrationMode::Persist);
    QVERIFY2(result.error.isEmpty(), qPrintable(result.error));
    std::vector<std::string> replacement_errors;
    QVERIFY(EnsureReplacementParent(args));
    QVERIFY(args.WriteSettingsFile(&replacement_errors));
    QString cleanup_error;
    QVERIFY2(QmlLegacySettings::CompleteCoreSettingsMigration(args,result,&cleanup_error),qPrintable(cleanup_error));
    QCOMPARE(result.settings_changed, persisted);
    const auto stored = args.GetPersistentSetting("dbcache");
    if (persisted) QCOMPARE(SettingTo<int64_t>(stored).value_or(-1), expected);
    else {
        QVERIFY(stored.isNull());
        QCOMPARE(SettingTo<int64_t>(QmlCoreSettings::DefaultCoreSettingValue(QStringLiteral("dbcache"))).value_or(-1), expected);
    }
    QVERIFY(!legacy.settings().contains(QStringLiteral("nDatabaseCache")));
}

void OptionsModelTests::knownSavedProfileFinishesOnlyRemainingDirectoryAndCleanup()
{
    QTemporaryDir directory;QVERIFY(directory.isValid());
    static bool reject=true;
    static const auto format=QSettings::registerFormat(QStringLiteral("r35-recoverable-write"),
        [](QIODevice& device,QSettings::SettingsMap& values){if(device.size()==0)return true;QDataStream stream{&device};stream>>values;return stream.status()==QDataStream::Ok;},
        [](QIODevice& device,const QSettings::SettingsMap& values){if(reject)return false;QDataStream stream{&device};stream<<values;return stream.status()==QDataStream::Ok;});
    QVERIFY(format!=QSettings::InvalidFormat);QSettings::setPath(format,QSettings::UserScope,directory.filePath(QStringLiteral("gui")));
    SavedSettingsFormat saved_format{format};reject=true;const auto restore=qScopeGuard([]{reject=true;});
    const auto config_path=directory.filePath(QStringLiteral("bitcoin.conf"));QFile config{config_path};QVERIFY(config.open(QIODevice::WriteOnly));
    QVERIFY(config.write(QStringLiteral("regtest=1\ndatadir=%1\n").arg(directory.path()).toUtf8())>0);config.close();
    ArgsManager args;std::string error;QVERIFY2(PrepareTestArgs(args,{"bitcoinqml","-conf="+config_path.toStdString()},error),error.c_str());
    QmlOnboardingSettings::ApplyRequest request;request.data_dir={QmlDataDir::DefaultDataDirString(),QmlOnboardingSettings::DataDirSource::UserSelection};
    auto original=QmlOnboardingSettings::PersistProfile(args,request);
    QVERIFY(!original.completed);QVERIFY(original.replacement_written);QVERIFY(!original.directory_saved);QVERIFY(!original.error.isEmpty());
    fs::path path;QVERIFY(args.GetSettingsPath(&path));QFile file{QString::fromStdString(path.utf8string())};QVERIFY(file.open(QIODevice::ReadOnly));const auto replacement=file.readAll();file.close();
    reject=false;const auto finished=QmlOnboardingSettings::CompleteProfile(args,original);
    QVERIFY2(finished.completed,qPrintable(finished.error));QVERIFY(finished.directory_saved);QVERIFY(finished.cleanup_completed);
    QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),replacement);
}
void OptionsModelTests::capturedResetKeepsOriginalBackupAcrossCompletion()
{
    SavedNamedSettings current{QStringLiteral("BitcoinCore"),QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy{QStringLiteral("Bitcoin"),QStringLiteral("Bitcoin-Qt-regtest")};
    SavedNamedSettings legacy_default{QStringLiteral("Bitcoin"),QStringLiteral("Bitcoin-Qt")};
    QTemporaryDir directory;QVERIFY(directory.isValid());ArgsManager args;PrepareArgsForDataDir(args,directory.path());
    QmlCoreSettings::SetRwSetting(args,QStringLiteral("dbcache"),common::SettingsValue{int64_t{512}});
    QmlCoreSettings::SetRwSetting(args,QStringLiteral("prune"),common::SettingsValue{int64_t{953}});
    std::vector<std::string> errors;QVERIFY(EnsureReplacementParent(args));QVERIFY(args.WriteSettingsFile(&errors));
    fs::path path;QVERIFY(args.GetSettingsPath(&path));QFile before{QString::fromStdString(path.utf8string())};QVERIFY(before.open(QIODevice::ReadOnly));const auto original_bytes=before.readAll();before.close();
    args.ForceSetArg("-resetguisettings","1");QmlOnboardingSettings::ApplyRequest request;request.data_dir={directory.path(),QmlOnboardingSettings::DataDirSource::ExplicitArg};
    const auto original=QmlOnboardingSettings::PersistProfile(args,request);QVERIFY2(original.completed,qPrintable(original.error));
    const auto complete=QmlOnboardingSettings::CompleteProfile(args,original);QVERIFY2(complete.completed,qPrintable(complete.error));
    QFile backup{QString::fromStdString(path.utf8string())+QStringLiteral(".bak")};QVERIFY(backup.open(QIODevice::ReadOnly));QCOMPARE(backup.readAll(),original_bytes);
    QVERIFY(args.GetPersistentSetting("dbcache").isNull());QVERIFY(args.GetPersistentSetting("prune").isNull());
}
void OptionsModelTests::capturedResetRejectsChangedLegacySource()
{
    SavedNamedSettings current{QStringLiteral("BitcoinCore"),QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy{QStringLiteral("Bitcoin"),QStringLiteral("Bitcoin-Qt-regtest")};
    SavedNamedSettings legacy_default{QStringLiteral("Bitcoin"),QStringLiteral("Bitcoin-Qt")};
    legacy.settings().setValue(QStringLiteral("nDatabaseCache"),600);legacy.settings().sync();
    QTemporaryDir directory;QVERIFY(directory.isValid());ArgsManager args;PrepareArgsForDataDir(args,directory.path());
    const auto reset=QmlLegacySettings::CaptureResetSettings(args);QVERIFY(reset.error.isEmpty());
    QmlCoreSettings::SetRwSetting(args,QStringLiteral("qml_onboarded"),common::SettingsValue{false});
    QmlLegacySettings::CaptureMigrationTarget(args,reset);std::vector<std::string> errors;QVERIFY(EnsureReplacementParent(args));QVERIFY(args.WriteSettingsFile(&errors));
    legacy.settings().setValue(QStringLiteral("nDatabaseCache"),601);legacy.settings().sync();QString error;
    QVERIFY(!QmlLegacySettings::CompleteCoreSettingsMigration(args,reset,&error));QVERIFY(!error.isEmpty());
    QCOMPARE(legacy.settings().value(QStringLiteral("nDatabaseCache")).toInt(),601);
}

void OptionsModelTests::corruptStoredPreferenceFailsWithoutDefaultFallback()
{
    SavedSettingsFormat format{QSettings::IniFormat};const auto application=QCoreApplication::applicationName();
    QCoreApplication::setApplicationName(QStringLiteral("R35MalformedStoredProfile"));
    const auto restore=qScopeGuard([&]{QCoreApplication::setApplicationName(application);});
    QSettings settings;const auto path=settings.fileName();QVERIFY(QDir{}.mkpath(QFileInfo(path).absolutePath()));
    QFile corrupt{path};QVERIFY(corrupt.open(QIODevice::WriteOnly));QVERIFY(corrupt.write("[unterminated\nstrDataDir=/original/profile\n")>0);corrupt.close();
    const auto remove=qScopeGuard([&]{QFile::remove(path);});
    const auto source=QmlDataDir::ReadGuiDataDirWithSource();QVERIFY(!source.error.isEmpty());QVERIFY(source.path.isEmpty());
    const auto startup=QmlOnboardingSettings::ResolveOnboardingStartupStatus({"bitcoin-qt","-regtest"},false);
    QVERIFY(!startup.ok);QVERIFY(!startup.error.isEmpty());
}

void OptionsModelTests::migrationChecksExactReplacementAndSources_data()
{
    QTest::addColumn<QString>("change");
    for(const auto* name:{"replacement-missing","replacement-changed","proxy-enabled-inserted","source-version-changed","source-value-changed"})
        QTest::newRow(name)<<QString::fromLatin1(name);
}
void OptionsModelTests::migrationChecksExactReplacementAndSources()
{
    QFETCH(QString,change);
    SavedNamedSettings current{QStringLiteral("BitcoinCore"),QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy{QStringLiteral("Bitcoin"),QStringLiteral("Bitcoin-Qt-regtest")};
    legacy.settings().setValue("nDatabaseCache",600);
    legacy.settings().setValue("addrProxy",QStringLiteral("10.23.45.67:9150"));legacy.settings().sync();
    QTemporaryDir directory;QVERIFY(directory.isValid());ArgsManager args;PrepareArgsForDataDir(args,directory.path());
    const auto migration=QmlLegacySettings::MigrateCoreSettings(args,QmlLegacySettings::MigrationMode::Persist);
    QVERIFY2(migration.error.isEmpty(),qPrintable(migration.error));QVERIFY(migration.settings_changed);
    // Source remains intact even after staging has supplied Core's next values.
    QCOMPARE(legacy.settings().value("nDatabaseCache").toInt(),600);
    std::vector<std::string> errors;QVERIFY(EnsureReplacementParent(args));QVERIFY(args.WriteSettingsFile(&errors));
    fs::path target;QVERIFY(args.GetSettingsPath(&target));
    if(change=="replacement-missing")QVERIFY(QFile::remove(QString::fromStdString(target.utf8string())));
    else if(change=="replacement-changed"){QmlCoreSettings::SetRwSetting(args,QStringLiteral("dbcache"),common::SettingsValue{int64_t{700}});QVERIFY(args.WriteSettingsFile(&errors));}
    else if(change=="proxy-enabled-inserted")legacy.settings().setValue("fUseProxy",true);
    else if(change=="source-version-changed")legacy.settings().setValue("nSettingsVersion",1);
    else legacy.settings().setValue("nDatabaseCache",601);
    legacy.settings().sync();QString error;
    QVERIFY(!QmlLegacySettings::CompleteCoreSettingsMigration(args,migration,&error));QVERIFY(!error.isEmpty());
    QVERIFY(legacy.settings().contains("nDatabaseCache"));QVERIFY(legacy.settings().contains("addrProxy"));
}
void OptionsModelTests::migrationCleanupOnlyFollowsDurableWriteAndIsIdempotent()
{
    SavedNamedSettings current{QStringLiteral("BitcoinCore"),QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy{QStringLiteral("Bitcoin"),QStringLiteral("Bitcoin-Qt-regtest")};
    legacy.settings().setValue("nDatabaseCache",600);legacy.settings().setValue("fListen",false);legacy.settings().sync();
    QTemporaryDir directory;QVERIFY(directory.isValid());ArgsManager args;PrepareArgsForDataDir(args,directory.path());
    const auto migration=QmlLegacySettings::MigrateCoreSettings(args,QmlLegacySettings::MigrationMode::Persist);
    QVERIFY(migration.settings_changed);QString error;
    QVERIFY(!QmlLegacySettings::CompleteCoreSettingsMigration(args,migration,&error));
    QCOMPARE(legacy.settings().value("nDatabaseCache").toInt(),600);
    std::vector<std::string> errors;QVERIFY(EnsureReplacementParent(args));QVERIFY(args.WriteSettingsFile(&errors));
    QVERIFY2(QmlLegacySettings::CompleteCoreSettingsMigration(args,migration,&error),qPrintable(error));
    legacy.settings().sync();QVERIFY(!legacy.settings().contains("nDatabaseCache"));
    QVERIFY(!legacy.settings().contains("fListen"));
    QVERIFY2(QmlLegacySettings::CompleteCoreSettingsMigration(args,migration,&error),qPrintable(error));
    QVERIFY(args.ReadSettingsFile(&errors));
    QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("dbcache")).value_or(-1),int64_t{600});
    QCOMPARE(SettingToBool(args.GetPersistentSetting("listen")).value_or(true),false);
}
void OptionsModelTests::profileRecoveryDoesNotReplayChangedReplacement()
{
    SavedGuiDataDirSettings saved_settings;
    SavedNamedSettings current{QStringLiteral("BitcoinCore"),QStringLiteral("BitcoinCore-App-regtest")};
    SavedNamedSettings legacy{QStringLiteral("Bitcoin"),QStringLiteral("Bitcoin-Qt-regtest")};
    legacy.settings().setValue("nDatabaseCache",600);legacy.settings().sync();
    QTemporaryDir directory;QVERIFY(directory.isValid());ArgsManager args;PrepareArgsForDataDir(args,directory.path());
    QmlOnboardingSettings::ApplyRequest request;request.data_dir={directory.path(),QmlOnboardingSettings::DataDirSource::ExplicitArg};
    request.values=QmlCoreSettings::LoadEffectiveValues(args);
    const auto original=QmlOnboardingSettings::PersistProfile(args,request);
    QVERIFY2(original.completed,qPrintable(original.error));QVERIFY(original.replacement_written);
    QmlCoreSettings::SetRwSetting(args,QStringLiteral("dbcache"),common::SettingsValue{int64_t{701}});
    std::vector<std::string> errors;QVERIFY(EnsureReplacementParent(args));QVERIFY(args.WriteSettingsFile(&errors));
    const auto queried=QmlOnboardingSettings::QueryProfile(args,original);
    QVERIFY(!queried.completed);QVERIFY(queried.replacement_written);QVERIFY(!queried.error.isEmpty());
    const auto completed=QmlOnboardingSettings::CompleteProfile(args,original);
    QVERIFY(!completed.completed);QVERIFY(completed.replacement_written);
    QVERIFY(args.ReadSettingsFile(&errors));QCOMPARE(SettingTo<int64_t>(args.GetPersistentSetting("dbcache")).value_or(-1),int64_t{701});
}
void OptionsModelTests::savedProfileReadFailureNeverSelectsDefault_data()
{
    QTest::addColumn<QVariant>("stored");
    QTest::newRow("empty-present")<<QVariant{QStringLiteral("")};
    QTest::newRow("wrong-type")<<QVariant{42};
    QTest::newRow("unavailable-drive")<<QVariant{QStringLiteral("/a-disconnected-bitcoin-profile-r35")};
}
void OptionsModelTests::savedProfileReadFailureNeverSelectsDefault()
{
    QFETCH(QVariant,stored);SavedGuiDataDirSettings saved;QSettings settings;
    settings.setValue(SettingsKeys::DATA_DIR,stored);settings.sync();
    const auto status=QmlOnboardingSettings::ResolveOnboardingStartupStatus({"bitcoin-qt","-regtest"},false);
    QVERIFY(!status.ok);QVERIFY(!status.error.isEmpty());
    QCOMPARE(settings.value(SettingsKeys::DATA_DIR),stored);
    if(stored.typeId()==QMetaType::QString&&!stored.toString().isEmpty())QVERIFY(status.error.contains(stored.toString()));
}
void OptionsModelTests::legacyNativeOrganizationDomainMatchesWriter()
{
    // This is a real NativeFormat component control, including Darwin's
    // organizationDomain selection. Restore the one historical key exactly.
    SavedSettingsFormat format{QSettings::NativeFormat};
    const auto organization=QCoreApplication::organizationName();const auto domain=QCoreApplication::organizationDomain();const auto application=QCoreApplication::applicationName();
    QCoreApplication::setOrganizationName(QStringLiteral("Bitcoin"));QCoreApplication::setOrganizationDomain(QStringLiteral("bitcoin.org"));QCoreApplication::setApplicationName(QStringLiteral("Bitcoin-Qt"));
    QSettings writer;const bool present=writer.contains(SettingsKeys::DATA_DIR);const auto original=writer.value(SettingsKeys::DATA_DIR);
    const auto restore=qScopeGuard([&]{if(present)writer.setValue(SettingsKeys::DATA_DIR,original);else writer.remove(SettingsKeys::DATA_DIR);writer.sync();QCoreApplication::setOrganizationName(organization);QCoreApplication::setOrganizationDomain(domain);QCoreApplication::setApplicationName(application);});
    QTemporaryDir directory;QVERIFY(directory.isValid());writer.setValue(SettingsKeys::DATA_DIR,directory.path());writer.sync();QCOMPARE(writer.status(),QSettings::NoError);
    QCOMPARE(QmlLegacySettings::ReadLegacyGuiDataDir(),directory.path());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OptionsModelTests)
#else
QTEST_MAIN(OptionsModelTests)
#endif
#include <test_options_model.moc>
