// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qml/onboarding_settings.h>

#include <chainparams.h>
#include <chainparamsbase.h>
#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <init.h>
#include <mapport.h>
#include <net.h>
#include <qml/core_settings.h>
#include <qml/effectivesettings.h>
#include <qml/datadir.h>
#include <qml/guiargs.h>
#include <qml/legacy_settings_migration.h>
#include <univalue.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#ifdef ENABLE_WALLET
#include <wallet/db.h>
#endif

#include <QCoreApplication>
#include <QFileInfo>
#include <QScopeGuard>

#include <map>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

namespace QmlOnboardingSettings {
struct ProfileRecovery {
    fs::path target;
    std::map<std::string, common::SettingsValue> expected;
    DataDirSelection directory;
    UiPreferencesNamespace settings_namespace;
    QmlLegacySettings::MigrationResult migration;
    std::shared_ptr<QmlLegacySettings::DirectoryCleanup> directory_cleanup;
    bool persist_directory{false};
    bool reset{false};
};
}

namespace {
constexpr const char* QML_ONBOARDED_KEY{"qml_onboarded"};

bool HasExplicitDataDirArg(const ArgsManager& args)
{
    return args.IsArgSet("-datadir") && !args.GetPathArg("-datadir").empty();
}

QmlOnboardingSettings::DataDirSource ToOnboardingDataDirSource(QmlDataDir::GuiDataDirSource source)
{
    switch (source) {
    case QmlDataDir::GuiDataDirSource::Settings:
        return QmlOnboardingSettings::DataDirSource::GuiSetting;
    case QmlDataDir::GuiDataDirSource::Invalid: return QmlOnboardingSettings::DataDirSource::Default;
    case QmlDataDir::GuiDataDirSource::LegacySettings:
        return QmlOnboardingSettings::DataDirSource::LegacyGuiSetting;
    case QmlDataDir::GuiDataDirSource::Default:
        return QmlOnboardingSettings::DataDirSource::Default;
    }
    return QmlOnboardingSettings::DataDirSource::Default;
}

bool IsGuiOwnedDataDirSource(QmlOnboardingSettings::DataDirSource source)
{
    return source == QmlOnboardingSettings::DataDirSource::GuiSetting ||
           source == QmlOnboardingSettings::DataDirSource::LegacyGuiSetting ||
           source == QmlOnboardingSettings::DataDirSource::UserSelection;
}

QString NormalizedDataDirPath(const QmlOnboardingSettings::DataDirSelection& selection)
{
    const QString normalized = QmlDataDir::NormalizeLocalPath(selection.path);
    return normalized.isEmpty() ? QmlDataDir::DefaultDataDirString() : normalized;
}

bool ShouldApplyDataDirBeforeConfig(QmlOnboardingSettings::DataDirSource source, bool explicit_datadir_arg, const QString& data_dir)
{
    return !explicit_datadir_arg &&
           IsGuiOwnedDataDirSource(source) &&
           !QmlDataDir::IsDefaultDataDir(data_dir);
}

bool ShouldPersistGuiDataDirSelection(QmlOnboardingSettings::DataDirSource source, bool explicit_datadir_arg)
{
    return !explicit_datadir_arg && IsGuiOwnedDataDirSource(source);
}

QString ExplicitDataDirString(ArgsManager& args)
{
    return QmlDataDir::NormalizeLocalPath(QString::fromStdString(fs::PathToString(args.GetPathArg("-datadir"))));
}

QString ActiveDataDirString(const ArgsManager& args)
{
    const fs::path data_dir = args.GetDataDirBase();
    if (data_dir.empty()) return {};
    return QmlDataDir::NormalizeLocalPath(QString::fromStdString(fs::PathToString(data_dir)));
}

// Preview has its own network paths and never changes process-wide Params().
fs::path PreviewNetworkDataDir(const ArgsManager& args)
{
    return args.GetDataDirBase() / fs::PathFromString(CreateBaseChainParams(args, args.GetChainType())->DataDir());
}

std::optional<fs::path> PreviewSettingsPath(const ArgsManager& args)
{
    const fs::path setting = args.GetPathArg("-settings", BITCOIN_SETTINGS_FILENAME);
    if (setting.empty()) return std::nullopt;
    return fsbridge::AbsPathJoin(PreviewNetworkDataDir(args), setting);
}

bool ReadPreviewSettings(ArgsManager& args, std::vector<std::string>& errors)
{
    const auto path = PreviewSettingsPath(args);
    if (!path) return true;
    std::map<std::string, common::SettingsValue> values;
    if (!common::ReadSettings(*path, values, errors)) return false;
    args.LockSettings([&](common::Settings& settings) { settings.rw_settings = std::move(values); });
    return true;
}

bool ReadConfigAndSelectNetwork(ArgsManager& args, QString* error)
{
    try {
        if (!CheckDataDirOption(args)) {
            if (error) *error=QCoreApplication::translate("Onboarding","Specified data directory \"%1\" does not exist.").arg(QString::fromStdString(args.GetArg("-datadir","")));
            return false;
        }
        std::string config_error;
        if (!args.ReadConfigFiles(config_error, true)) {
            if (error) *error=QString::fromStdString(config_error);
            return false;
        }
        if (!CheckDataDirOption(args)) {
            if (error) *error=QCoreApplication::translate("Onboarding","Specified data directory \"%1\" does not exist.").arg(QString::fromStdString(args.GetArg("-datadir","")));
            return false;
        }
        args.SelectConfigNetwork(args.GetChainTypeString());
    } catch (const std::exception& e) {
        if (error) *error=QString::fromUtf8(e.what());
        return false;
    }
    if (error) error->clear();
    return true;
}

bool ReadSettingsFileIfPresent(ArgsManager& args, QString* error)
{
    const auto settings_path = PreviewSettingsPath(args);
    if (!settings_path || !fs::exists(*settings_path)) {
        if (error) error->clear();
        return true;
    }

    std::vector<std::string> settings_errors;
    if (!ReadPreviewSettings(args, settings_errors)) {
        if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file could not be read."} : settings_errors.front());
        return false;
    }
    if (error) error->clear();
    return true;
}

bool PathExists(const fs::path& path)
{
    if (path.empty()) return false;
    std::error_code ec;
    return fs::exists(fs::status(path, ec)) && !ec;
}

bool DirectoryExists(const fs::path& path)
{
    if (path.empty()) return false;
    std::error_code ec;
    return fs::is_directory(path, ec) && !ec;
}

bool IsHiddenPath(const fs::path& path)
{
    const std::string filename{fs::PathToString(path.filename())};
    return !filename.empty() && filename.front() == '.';
}

bool DirectoryHasNonHiddenEntry(const fs::path& path)
{
    if (!DirectoryExists(path)) return false;

    std::error_code ec;
    for (fs::directory_iterator it(path, ec); it != fs::directory_iterator(); it.increment(ec)) {
        if (ec) return false;
        if (!IsHiddenPath(it->path())) return true;
    }
    return false;
}

fs::path BlocksDirPathNoCreate(const ArgsManager& args)
{
    fs::path path;
    if (args.IsArgSet("-blocksdir")) {
        path = fs::absolute(args.GetPathArg("-blocksdir"));
        if (!DirectoryExists(path)) return {};
    } else {
        path = args.GetDataDirBase();
    }

    if (path.empty()) return {};
    path /= fs::PathFromString(CreateBaseChainParams(args, args.GetChainType())->DataDir());
    path /= "blocks";
    return path;
}

bool HasExistingChainData(const ArgsManager& args)
{
    const fs::path network_data_dir{PreviewNetworkDataDir(args)};
    if (DirectoryHasNonHiddenEntry(network_data_dir / "chainstate")) return true;
    if (DirectoryHasNonHiddenEntry(network_data_dir / "chainstate_snapshot")) return true;
    if (DirectoryHasNonHiddenEntry(network_data_dir / "indexes")) return true;
    return DirectoryHasNonHiddenEntry(BlocksDirPathNoCreate(args));
}

#ifdef ENABLE_WALLET
std::optional<fs::path> EffectiveWalletDirNoCreate(const ArgsManager& args)
{
    if (args.IsArgSet("-walletdir")) {
        const fs::path wallet_dir{args.GetPathArg("-walletdir")};
        std::error_code ec;
        fs::path canonical_wallet_dir{fs::canonical(wallet_dir, ec)};
        if (ec || !DirectoryExists(canonical_wallet_dir) || !wallet_dir.is_absolute()) return std::nullopt;
        return canonical_wallet_dir;
    }

    fs::path wallet_dir{PreviewNetworkDataDir(args)};
    if (DirectoryExists(wallet_dir / "wallets")) {
        wallet_dir /= "wallets";
    }
    if (!DirectoryExists(wallet_dir)) return std::nullopt;
    return wallet_dir;
}

bool HasExistingWalletData(const ArgsManager& args)
{
    const std::optional<fs::path> wallet_dir{EffectiveWalletDirNoCreate(args)};
    return wallet_dir && !wallet::ListDatabases(*wallet_dir).empty();
}

#endif // ENABLE_WALLET

QmlOnboardingSettings::ProfileSummary BuildProfileSummary(const ArgsManager& args, bool config_file_path_available)
{
    QmlOnboardingSettings::ProfileSummary summary;
    const auto settings_path = PreviewSettingsPath(args);
    summary.has_settings_file = settings_path && PathExists(*settings_path);
    summary.has_config_file = config_file_path_available && PathExists(args.GetConfigFilePath());
    summary.has_chain_data = HasExistingChainData(args);
#ifdef ENABLE_WALLET
    summary.has_wallet_data = HasExistingWalletData(args);
#endif
    summary.existing_profile = summary.has_settings_file ||
                               summary.has_config_file ||
                               summary.has_chain_data ||
                               summary.has_wallet_data;
    return summary;
}

bool EnsureSettingsDirectory(ArgsManager& args, const fs::path& settings_path, QString* error)
{
    try {
        const fs::path network_data_dir{args.GetDataDirNet()};
        if (TryCreateDirectories(network_data_dir)) {
            TryCreateDirectories(network_data_dir / "wallets");
        }
        TryCreateDirectories(settings_path.parent_path());
    } catch (const fs::filesystem_error& e) {
        if (error) *error = QString::fromStdString(e.what());
        return false;
    }
    if (error) error->clear();
    return true;
}



std::optional<bool> CommandLineBoolArg(ArgsManager& args, const std::string& name)
{
    std::optional<bool> value;
    args.LockSettings([&](const common::Settings& settings) {
        const auto* options = common::FindKey(settings.command_line_options, name);
        if (options && !options->empty()) {
            value = SettingToBool(options->back());
        }
    });
    return value;
}

QString CheckIntent(ArgsManager& args, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, bool accept_redownload)
{
    effectivesettings::Batch batch;batch.accept_redownload=accept_redownload;
    for(const auto key:{effectivesettings::Key::Listen,effectivesettings::Key::Natpmp,effectivesettings::Key::Server,effectivesettings::Key::Prune,effectivesettings::Key::Proxy,effectivesettings::Key::Onion}){
        const auto name=effectivesettings::Name(key);if(!touched_settings.contains(name))continue;
        effectivesettings::Value value;
        if(key==effectivesettings::Key::Listen)value=values.listen;
        else if(key==effectivesettings::Key::Natpmp)value=values.natpmp;
        else if(key==effectivesettings::Key::Server)value=values.server;
        else if(key==effectivesettings::Key::Prune)value=SettingTo<int64_t>(QmlCoreSettings::PruneSetting(values)).value_or(0);
        else if(key==effectivesettings::Key::Proxy)value=values.proxy_enabled?values.proxy_address:QStringLiteral("0");
        else value=values.tor_enabled?values.tor_address:QStringLiteral("0");
        batch.edits.push_back({key,std::move(value)});
    }
    const auto checked=effectivesettings::ReviewStaged(args,batch);
    return checked.valid ? QString{} : checked.error;
}

} // namespace

namespace QmlOnboardingSettings {

bool PrepareArgs(ArgsManager& args, const std::vector<std::string>& argv, bool can_listen_ipc, std::string& error)
{
    SetupServerArgs(args, can_listen_ipc);
    SetupQmlGuiArgs(args);
    std::vector<const char*> raw_argv;
    raw_argv.reserve(argv.size());
    for (const std::string& arg : argv) raw_argv.push_back(arg.c_str());
    return args.ParseParameters(static_cast<int>(raw_argv.size()), raw_argv.data(), error);
}

OnboardingStartupStatus ResolveOnboardingStartupStatus(const std::vector<std::string>& argv, bool can_listen_ipc, const UiPreferencesNamespace* identity, StartupReadMode mode)
{
    OnboardingStartupStatus status;

    ArgsManager preview_args;
    std::string parse_error;
    if (!PrepareArgs(preview_args, argv, can_listen_ipc, parse_error)) {
        status.error = QString::fromStdString(parse_error);
        return status;
    }

    try {
        CreateChainParams(preview_args, preview_args.GetChainType());
    } catch (const std::exception& e) {
        status.error = QString::fromStdString(e.what());
        return status;
    }

    const bool reset_gui_settings = preview_args.GetBoolArg("-resetguisettings", false);
    const bool explicit_datadir = HasExplicitDataDirArg(preview_args);
    if (explicit_datadir) {
        status.active_data_dir = ExplicitDataDirString(preview_args);
        status.data_dir_source = DataDirSource::ExplicitArg;
    } else if (reset_gui_settings) {
        status.active_data_dir = QmlDataDir::DefaultDataDirString();
        status.data_dir_source = DataDirSource::Default;
    } else {
        const QmlDataDir::GuiDataDir gui_data_dir = QmlDataDir::ReadGuiDataDirWithSource(identity);
        if (!gui_data_dir.error.isEmpty()) { status.error = gui_data_dir.error; return status; }
        status.active_data_dir = gui_data_dir.path;
        status.data_dir_source = ToOnboardingDataDirSource(gui_data_dir.source);
    }
    if (status.active_data_dir.isEmpty()) {
        status.active_data_dir = QmlDataDir::DefaultDataDirString();
    }

    const bool apply_datadir_before_config = ShouldApplyDataDirBeforeConfig(status.data_dir_source, explicit_datadir, status.active_data_dir);
    const bool custom_datadir_exists = apply_datadir_before_config && QFileInfo::exists(status.active_data_dir);
    const bool can_read_profile = explicit_datadir || !apply_datadir_before_config || custom_datadir_exists;
    if (custom_datadir_exists) {
        QmlDataDir::ApplyDataDirArg(preview_args, status.active_data_dir);
    }

    QString read_error;
    if (can_read_profile) {
        if (!ReadConfigAndSelectNetwork(preview_args, &read_error)) {
            status.error = read_error;
            return status;
        }
        const QString resolved_data_dir = ActiveDataDirString(preview_args);
        if (!resolved_data_dir.isEmpty()) {
            status.active_data_dir = resolved_data_dir;
            if (status.data_dir_source == DataDirSource::Default && !QmlDataDir::IsDefaultDataDir(resolved_data_dir)) {
                status.data_dir_source = DataDirSource::Config;
            }
        }
    } else {
        if (status.data_dir_source == DataDirSource::GuiSetting || status.data_dir_source == DataDirSource::LegacyGuiSetting) {
            status.error = QCoreApplication::translate("Onboarding", "The saved data directory is not available: %1. Reconnect the drive or restore this directory, then start again. Your selection is kept.").arg(status.active_data_dir);
            return status;
        }
        try {
            CreateChainParams(preview_args, preview_args.GetChainType());
            preview_args.SelectConfigNetwork(preview_args.GetChainTypeString());
        } catch (const std::exception& e) {
            status.error = QString::fromStdString(e.what());
            return status;
        }
        status.ok = true;
        status.should_show_onboarding = true;
        return status;
    }

    try {
        const QString network = QString::fromStdString(preview_args.GetChainTypeString());
        std::error_code path_error;
        const fs::path canonical = fs::weakly_canonical(PreviewNetworkDataDir(preview_args), path_error);
        const QString network_data_dir = QString::fromStdString(fs::PathToString(canonical));
        if (path_error || canonical.empty() || network.isEmpty() || network.size() > 64 || network_data_dir.size() > 32768) {
            status.error = QCoreApplication::translate("Onboarding", "The startup settings could not be read.");
            return status;
        }
        status.network = network;
        status.network_data_dir = network_data_dir;
        // A fixed CLI profile wins over the chooser, including during reset.
        // A chooser without that fixed profile must run before any forwarding.
        status.probe_eligible = explicit_datadir || !preview_args.GetBoolArg("-choosedatadir", false);
    } catch (const std::exception& e) {
        status.error = QString::fromUtf8(e.what()).left(16384);
        return status;
    }
    if (mode == StartupReadMode::ProfileOnly) {
        status.ok = true;
        return status;
    }

    // A config file can select an existing profile even when the default data
    // directory is absent. Check the resolved path, retaining command-line
    // -datadir precedence over chooser requests (except -resetguisettings).
    if (reset_gui_settings || (!explicit_datadir && QmlDataDir::ShouldShowDataDirChooser(preview_args, status.active_data_dir))) {
        status.ok = true;
        status.qml_onboarded = false;
        status.should_show_onboarding = true;
        return status;
    }

    // Only check whether settings are enabled here. Resolving a Core settings
    // path would consult global BaseParams(), which startup has not selected.
    if (!preview_args.GetSettingsPath()) {
        status.ok = true;
        status.settings_enabled = false;
        status.qml_onboarded = true;
        status.should_show_onboarding = false;
        return status;
    }

    if (!ReadSettingsFileIfPresent(preview_args, &read_error)) {
        status.error = read_error;
        return status;
    }

    status.qml_onboarded = CommandLineBoolArg(preview_args, QML_ONBOARDED_KEY)
        .value_or(SettingToBool(preview_args.GetPersistentSetting(QML_ONBOARDED_KEY)).value_or(false));
    status.should_show_onboarding = !status.qml_onboarded;
    status.ok = true;
    return status;
}

PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const DataDirSelection& data_dir_selection, const ApplyRequest* intent)
{
    PreviewResult result;

    const QString data_dir = NormalizedDataDirPath(data_dir_selection);
    ArgsManager preview_args;
    std::string parse_error;
    if (!PrepareArgs(preview_args, argv, can_listen_ipc, parse_error)) {
        result.error = QString::fromStdString(parse_error);
        return result;
    }

    try {
        CreateChainParams(preview_args, preview_args.GetChainType());
    } catch (const std::exception& e) {
        result.error = QString::fromStdString(e.what());
        return result;
    }

    const bool explicit_datadir = HasExplicitDataDirArg(preview_args);
    if (!explicit_datadir || data_dir_selection.source != DataDirSource::Default) {
        result.error = QmlDataDir::ValidateCustomDataDir(data_dir);
        if (!result.error.isEmpty()) return result;
    }
    const bool apply_datadir_before_config = ShouldApplyDataDirBeforeConfig(data_dir_selection.source, explicit_datadir, data_dir);
    const bool custom_datadir_exists = apply_datadir_before_config && QFileInfo::exists(data_dir);
    bool config_file_path_available{false};
    if (custom_datadir_exists) {
        QmlDataDir::ApplyDataDirArg(preview_args, data_dir);
    }

    if (explicit_datadir || !apply_datadir_before_config || custom_datadir_exists) {
        if (!ReadConfigAndSelectNetwork(preview_args,&result.error)) return result;
        config_file_path_available = true;
        try {
            CreateChainParams(preview_args, preview_args.GetChainType());
            preview_args.SelectConfigNetwork(preview_args.GetChainTypeString());
        } catch (const std::exception& e) {
            result.error = QString::fromStdString(e.what());
            return result;
        }
        const bool reset_gui_settings = preview_args.GetBoolArg("-resetguisettings", false);
        if (!reset_gui_settings) {
            std::vector<std::string> settings_errors;
            if (!ReadPreviewSettings(preview_args, settings_errors)) {
                result.error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file could not be read."} : settings_errors.front());
                return result;
            }
            const QmlLegacySettings::MigrationResult migration_result{
                QmlLegacySettings::MigrateCoreSettings(preview_args, QmlLegacySettings::MigrationMode::Preview)
            };
            if (!migration_result.error.isEmpty()) {
                result.error = migration_result.error;
                return result;
            }
        }
    } else {
        preview_args.SelectConfigNetwork(preview_args.GetChainTypeString());
        if (!preview_args.GetBoolArg("-resetguisettings", false)) {
            const QmlLegacySettings::MigrationResult migration_result{
                QmlLegacySettings::MigrateCoreSettings(preview_args, QmlLegacySettings::MigrationMode::Preview)
            };
            if (!migration_result.error.isEmpty()) {
                result.error = migration_result.error;
                return result;
            }
        }
    }

    if (intent) {
        result.error = CheckIntent(preview_args, intent->touched_settings, intent->values, intent->accept_redownload);
        if (!result.error.isEmpty()) return result;
    }

    // Preview should show the settings the node will run with. In reset mode,
    // old GUI-owned settings.json overrides are intentionally skipped first,
    // then core parameter interactions are applied on top of command-line and
    // bitcoin.conf values.
    InitParameterInteraction(preview_args);

    const auto chain_params = CreateChainParams(preview_args, preview_args.GetChainType());
    result.assumed_blockchain_size = static_cast<int>(chain_params->AssumedBlockchainSize());
    result.assumed_chainstate_size = static_cast<int>(chain_params->AssumedChainStateSize());
    result.profile = BuildProfileSummary(preview_args, config_file_path_available);
    result.core_setting_statuses = QmlCoreSettings::BuildCoreSettingStatuses(preview_args, QmlCoreSettings::OnboardingCoreSettingNames());
    result.values = QmlCoreSettings::LoadEffectiveValues(preview_args);
    result.ok = true;
    return result;
}

PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const QString& data_dir)
{
    return Preview(argv, can_listen_ipc, DataDirSelection{data_dir, DataDirSource::UserSelection});
}

bool SetQmlOnboarded(ArgsManager& args,bool completed,QString* error)
{
    fs::path path;if(args.GetSettingsPath(&path)&&!EnsureSettingsDirectory(args,path,error))return false;
    return QmlCoreSettings::PersistSettings(args,{QString::fromLatin1(QML_ONBOARDED_KEY)},[&]{QmlCoreSettings::SetRwSetting(args,QString::fromLatin1(QML_ONBOARDED_KEY),common::SettingsValue{completed});return true;},error);
}

bool MarkQmlOnboarded(ArgsManager& args,QString* error){return SetQmlOnboarded(args,true,error);}

bool ApplyToArgs(ArgsManager& args, const DataDirSelection& data_dir_selection, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, QString* error, bool accept_redownload, bool mark_completed, ProfileResult* receipt, const UiPreferencesNamespace* identity)
{
    if (error) error->clear();
    ProfileResult local_receipt;
    if (!receipt) receipt = &local_receipt;
    *receipt = {};
    receipt->recovery = std::make_shared<ProfileRecovery>();
    const auto recovery = receipt->recovery;
    recovery->settings_namespace=identity?*identity:UiPreferencesNamespace::Capture();
    const auto record_error = qScopeGuard([&] { if (error) receipt->error = *error; });

    const QString data_dir = NormalizedDataDirPath(data_dir_selection);
    const bool explicit_datadir = HasExplicitDataDirArg(args);
    const bool apply_datadir_before_config = ShouldApplyDataDirBeforeConfig(data_dir_selection.source, explicit_datadir, data_dir);
    QString data_dir_error;
    if (apply_datadir_before_config && !QmlDataDir::EnsureDataDir(data_dir, &data_dir_error)) {
        if (error) *error = data_dir_error;
        return false;
    }
    if (apply_datadir_before_config) {
        QmlDataDir::ApplyDataDirArg(args, data_dir);
    }

    if (!ReadConfigAndSelectNetwork(args,error)) return false;
    try {
        SelectParams(args.GetChainType());
        args.SelectConfigNetwork(args.GetChainTypeString());
    } catch (const std::exception& e) {
        if (error) *error = QString::fromStdString(e.what());
        return false;
    }

    std::vector<std::string> settings_errors;
    const bool reset_gui_settings = args.GetBoolArg("-resetguisettings", false);
    recovery->reset = reset_gui_settings;
    QmlLegacySettings::MigrationResult migration_result;
    if (reset_gui_settings) {
        fs::path settings_path;
        if (args.GetSettingsPath(&settings_path) && fs::exists(settings_path)) {
            if (!args.ReadSettingsFile(&settings_errors)) {
                if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file could not be read."} : settings_errors.front());
                return false;
            }
            if (!args.WriteSettingsFile(&settings_errors, /*backup=*/true)) {
                if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file backup could not be written."} : settings_errors.front());
                return false;
            }
        }
        args.LockSettings([](common::Settings& settings) {
            settings.rw_settings.clear();
        });
        migration_result=QmlLegacySettings::CaptureResetSettings(args);
        if(!migration_result.error.isEmpty()){if(error)*error=migration_result.error;return false;}
    } else {
        if (!args.ReadSettingsFile(&settings_errors)) {
            if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file could not be read."} : settings_errors.front());
            return false;
        }
        migration_result = QmlLegacySettings::MigrateCoreSettings(args, QmlLegacySettings::MigrationMode::Persist);
        if (!migration_result.error.isEmpty()) {
            if (error) *error = migration_result.error;
            return false;
        }
    }

    const QString intent_error = CheckIntent(args, touched_settings, values, accept_redownload);
    if (!intent_error.isEmpty()) { if (error) *error = intent_error; return false; }
    fs::path settings_path;
    if (args.GetSettingsPath(&settings_path) && !EnsureSettingsDirectory(args, settings_path, error)) return false;
    QmlCoreSettings::Session core_settings{values, QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())};
    core_settings.setTouchedSettings(touched_settings);
    QStringList keys{QString::fromLatin1(QML_ONBOARDED_KEY)};
    for(const auto& name:touched_settings){keys.push_back(name);keys.push_back(name+QStringLiteral("-prev"));}
    recovery->directory = {data_dir, data_dir_selection.source};
    recovery->persist_directory = ShouldPersistGuiDataDirSelection(data_dir_selection.source, explicit_datadir);
    recovery->migration = migration_result;
    recovery->directory_cleanup=QmlLegacySettings::CaptureLegacyGuiDataDir();
    const auto persistence = QmlCoreSettings::PersistTransaction(args, keys, [&] {
        if (!core_settings.writeTouchedToArgs(args)) return false;
        QmlCoreSettings::SetRwSetting(args, QString::fromLatin1(QML_ONBOARDED_KEY), common::SettingsValue{mark_completed});
        QmlLegacySettings::CaptureMigrationTarget(args, migration_result);
        args.GetSettingsPath(&recovery->target);
        args.LockSettings([&](const common::Settings& settings) { recovery->expected = settings.rw_settings; });
        return true;
    });
    receipt->replacement_written = persistence.persisted;
    if (!persistence.completed) {
        if (error) *error = persistence.error.isEmpty() ? QStringLiteral("The setup settings could not be saved.") : persistence.error;
        return false;
    }
    // This is an ordered handoff, not a cross-file transaction. Once the Core
    // replacement is known, retries may finish only this captured handoff.
    auto finished = CompleteProfile(args, *receipt);
    *receipt = std::move(finished);
    if (error) *error = receipt->error;
    return receipt->completed;
}

ProfileResult QueryProfile(ArgsManager& args, const ProfileResult& original)
{
    auto result = original;
    const auto fail = [&](const QString& message) { result.completed = false; result.error = message; return result; };
    if (!result.recovery || !result.replacement_written) return result;
    fs::path target;
    const auto& recovery = *result.recovery;
    if (!args.GetSettingsPath(&target) || target != recovery.target)
        return fail(QCoreApplication::translate("Onboarding", "The saved setup changed. The earlier setup step was kept."));
    std::map<std::string, common::SettingsValue> observed;
    std::vector<std::string> errors;
    if (!common::ReadSettings(target, observed, errors) || observed.size() != recovery.expected.size())
        return fail(QCoreApplication::translate("Onboarding", "The saved setup could not be checked. Restore the earlier settings before trying again."));
    for (const auto& [key, value] : recovery.expected) {
        const auto found = observed.find(key);
        if (found == observed.end() || found->second.write() != value.write())
            return fail(QCoreApplication::translate("Onboarding", "The saved setup was changed by another program. Review the settings before continuing."));
    }
    result.error.clear();
    if (recovery.persist_directory) {
        const auto saved = QmlDataDir::ReadGuiDataDirWithSource(&recovery.settings_namespace);
        const bool matches=saved.stored&&saved.error.isEmpty()&&QmlDataDir::NormalizeLocalPath(saved.path)==recovery.directory.path;
        result.directory_saved = matches && (original.directory_saved || original.uncertain);
    } else result.directory_saved = true;
    result.completed = result.directory_saved && result.cleanup_completed;
    result.uncertain = false;
    if (result.completed) result.error.clear();
    return result;
}

ProfileResult CompleteProfile(ArgsManager& args, const ProfileResult& original)
{
    auto result = QueryProfile(args, original);
    if (!result.replacement_written || !result.recovery || result.uncertain || !result.error.isEmpty()) return result;
    const auto& recovery = *result.recovery;
    if (!QmlLegacySettings::CommitTargetFile(recovery.target, &result.error)) return result;
    if (!result.directory_saved) {
        const bool saved = QmlDataDir::IsDefaultDataDir(recovery.directory.path)
            ? QmlDataDir::PersistDefaultDataDirSelection(&result.error,false,&recovery.settings_namespace)
            : QmlDataDir::PersistGuiDataDirSelection(recovery.directory.path, &result.error,false,&recovery.settings_namespace);
        result.directory_saved = saved;
        if (!saved) return result;
    }
    if (!result.cleanup_completed) {
        const bool cleaned=QmlLegacySettings::CompleteCoreSettingsMigration(args,recovery.migration,&result.error);
        if (!cleaned || (!recovery.reset && recovery.persist_directory && !QmlLegacySettings::CompleteLegacyGuiDataDir(recovery.directory_cleanup,&result.error))) return result;
        result.cleanup_completed = true;
    }
    result.completed = true;
    result.error.clear();
    return result;
}

ProfileResult PersistProfile(ArgsManager& args, const ApplyRequest& request)
{
    ProfileResult result;
    try {
        ApplyToArgs(args, request.data_dir, request.touched_settings, request.values, &result.error, request.accept_redownload, false, &result,request.settings_namespace?&*request.settings_namespace:nullptr);
    } catch (const std::exception& e) {
        result.uncertain = true;
        result.error = QString::fromUtf8(e.what());
    } catch (...) {
        result.uncertain = true;
        result.error = QCoreApplication::translate("Onboarding", "Setup may not have finished. Check it before doing anything else.");
    }
    return result;
}

bool ApplyToArgs(ArgsManager& args, const QString& data_dir, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, QString* error, bool accept_redownload, bool mark_completed, ProfileResult* receipt, const UiPreferencesNamespace* identity)
{
    return ApplyToArgs(args, DataDirSelection{data_dir, DataDirSource::UserSelection}, touched_settings, values, error, accept_redownload, mark_completed, receipt,identity);
}

} // namespace QmlOnboardingSettings
