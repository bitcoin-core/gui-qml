// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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
#include <qml/datadir.h>
#include <qml/guiargs.h>
#include <qml/guiconstants.h>
#include <qml/legacy_settings_migration.h>
#include <qml/models/settings_keys.h>
#include <univalue.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <wallet/db.h>

#include <QDir>
#include <QFileInfo>
#include <QSettings>
#include <QVariantMap>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <utility>

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

QString ComparableDataDirPath(const QString& path)
{
    return QDir::cleanPath(QmlDataDir::NormalizeLocalPath(path));
}

bool SameDataDirPath(const QString& a, const QString& b)
{
    return ComparableDataDirPath(a) == ComparableDataDirPath(b);
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

QmlOnboardingSettings::DataDirSource ResolvedDataDirSource(
    QmlOnboardingSettings::DataDirSource selected_source,
    const QString& selected_data_dir,
    const QString& resolved_data_dir,
    bool explicit_datadir_arg)
{
    if (explicit_datadir_arg) return QmlOnboardingSettings::DataDirSource::ExplicitArg;
    if (!SameDataDirPath(selected_data_dir, resolved_data_dir)) return QmlOnboardingSettings::DataDirSource::Config;
    return selected_source;
}

bool ShouldDisplayResolvedConfigDataDir(
    QmlOnboardingSettings::DataDirSource selected_source,
    const QString& selected_data_dir,
    const QString& resolved_data_dir,
    bool explicit_datadir_arg)
{
    return !explicit_datadir_arg &&
           selected_source == QmlOnboardingSettings::DataDirSource::Default &&
           !QmlDataDir::IsDefaultDataDir(resolved_data_dir) &&
           !SameDataDirPath(selected_data_dir, resolved_data_dir);
}

void SetStartupDataDirs(
    QmlOnboardingSettings::OnboardingStartupStatus& status,
    QString selected_data_dir,
    QmlOnboardingSettings::DataDirSource selected_source,
    QString resolved_data_dir,
    bool explicit_datadir_arg)
{
    if (selected_data_dir.isEmpty()) selected_data_dir = QmlDataDir::DefaultDataDirString();
    if (resolved_data_dir.isEmpty()) resolved_data_dir = selected_data_dir;

    const bool display_resolved_config_data_dir{
        ShouldDisplayResolvedConfigDataDir(selected_source, selected_data_dir, resolved_data_dir, explicit_datadir_arg)
    };
    status.selected_data_dir = display_resolved_config_data_dir ? resolved_data_dir : selected_data_dir;
    status.selected_data_dir_source = display_resolved_config_data_dir ? QmlOnboardingSettings::DataDirSource::Config : selected_source;
    status.resolved_data_dir = resolved_data_dir;
    status.resolved_data_dir_source = ResolvedDataDirSource(selected_source, selected_data_dir, resolved_data_dir, explicit_datadir_arg);
    status.config_redirected_data_dir = !SameDataDirPath(selected_data_dir, resolved_data_dir);

    // Compatibility fields keep existing callers working while new callers can
    // distinguish the editable selection from the resolved Core datadir.
    status.active_data_dir = status.resolved_data_dir;
    status.data_dir_source = status.resolved_data_dir_source;
}

void SetPreviewDataDirs(
    QmlOnboardingSettings::PreviewResult& result,
    QString selected_data_dir,
    QmlOnboardingSettings::DataDirSource selected_source,
    QString resolved_data_dir,
    bool explicit_datadir_arg)
{
    if (selected_data_dir.isEmpty()) selected_data_dir = QmlDataDir::DefaultDataDirString();
    if (resolved_data_dir.isEmpty()) resolved_data_dir = selected_data_dir;

    const bool display_resolved_config_data_dir{
        ShouldDisplayResolvedConfigDataDir(selected_source, selected_data_dir, resolved_data_dir, explicit_datadir_arg)
    };
    result.selected_data_dir = display_resolved_config_data_dir ? resolved_data_dir : selected_data_dir;
    result.selected_data_dir_source = display_resolved_config_data_dir ? QmlOnboardingSettings::DataDirSource::Config : selected_source;
    result.resolved_data_dir = resolved_data_dir;
    result.resolved_data_dir_source = ResolvedDataDirSource(selected_source, selected_data_dir, resolved_data_dir, explicit_datadir_arg);
    result.config_redirected_data_dir = !SameDataDirPath(selected_data_dir, resolved_data_dir);
}

bool ReadConfigAndSelectNetwork(ArgsManager& args, QString* error)
{
    std::string config_error;
    if (!args.ReadConfigFiles(config_error, true)) {
        if (error) *error = QString::fromStdString(config_error);
        return false;
    }
    try {
        SelectParams(args.GetChainType());
        args.SelectConfigNetwork(args.GetChainTypeString());
    } catch (const std::exception& e) {
        if (error) *error = QString::fromStdString(e.what());
        return false;
    }
    if (error) error->clear();
    return true;
}

struct ReadOnlyProfileResult {
    bool settings_enabled{true};
    bool config_file_path_available{false};
    bool settings_file_unreadable{false};
};

bool ReadResolvedProfile(ArgsManager& args, ReadOnlyProfileResult& result, QString* error)
{
    result = {};
    if (!ReadConfigAndSelectNetwork(args, error)) return false;
    result.config_file_path_available = true;

    fs::path settings_path;
    if (!args.GetSettingsPath(&settings_path)) {
        result.settings_enabled = false;
        if (error) error->clear();
        return true;
    }

    const bool reset_without_settings{
        args.GetBoolArg("-resetguisettings", false)
    };
    std::vector<std::string> settings_errors;
    if (!args.ReadSettingsFile(&settings_errors)) {
        // Preserve settings.json precedence when it is readable, while still
        // allowing an already-selected reset to recover an unreadable file.
        if (reset_without_settings) {
            if (error) error->clear();
            return true;
        }
        result.settings_file_unreadable = true;
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
    return std::filesystem::exists(path, ec) && !ec;
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
    path /= fs::PathFromString(BaseParams().DataDir());
    path /= "blocks";
    return path;
}

bool HasExistingChainData(const ArgsManager& args)
{
    const fs::path network_data_dir{args.GetDataDirNet()};
    if (DirectoryHasNonHiddenEntry(network_data_dir / "chainstate")) return true;
    if (DirectoryHasNonHiddenEntry(network_data_dir / "chainstate_snapshot")) return true;
    if (DirectoryHasNonHiddenEntry(network_data_dir / "indexes")) return true;
    return DirectoryHasNonHiddenEntry(BlocksDirPathNoCreate(args));
}

std::optional<fs::path> EffectiveWalletDirNoCreate(const ArgsManager& args)
{
    if (args.IsArgSet("-walletdir")) {
        const fs::path wallet_dir{args.GetPathArg("-walletdir")};
        std::error_code ec;
        const fs::path canonical_wallet_dir{fs::canonical(wallet_dir, ec)};
        if (ec || !DirectoryExists(canonical_wallet_dir) || !wallet_dir.is_absolute()) return std::nullopt;
        return canonical_wallet_dir;
    }

    fs::path wallet_dir{args.GetDataDirNet()};
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

QmlOnboardingSettings::ProfileSummary BuildProfileSummary(const ArgsManager& args, bool config_file_path_available)
{
    QmlOnboardingSettings::ProfileSummary summary;
    fs::path settings_path;
    summary.has_settings_file = args.GetSettingsPath(&settings_path) && PathExists(settings_path);
    summary.has_config_file = config_file_path_available && PathExists(args.GetConfigFilePath());
    summary.has_chain_data = HasExistingChainData(args);
    summary.has_wallet_data = HasExistingWalletData(args);
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

bool WriteSettingsFile(ArgsManager& args, QString* error)
{
    fs::path settings_path;
    if (!args.GetSettingsPath(&settings_path)) {
        if (error) error->clear();
        return true;
    }
    if (!EnsureSettingsDirectory(args, settings_path, error)) return false;
    std::vector<std::string> settings_errors;
    if (!args.WriteSettingsFile(&settings_errors)) {
        if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file could not be written."} : settings_errors.front());
        return false;
    }
    if (error) error->clear();
    return true;
}

bool BackupSettingsFile(ArgsManager& args, QString* error)
{
    fs::path settings_path;
    if (!args.GetSettingsPath(&settings_path)) {
        if (error) error->clear();
        return true;
    }

    std::vector<std::string> settings_errors;
    if (!args.WriteSettingsFile(&settings_errors, /*backup=*/true)) {
        if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file backup could not be written."} : settings_errors.front());
        return false;
    }
    if (error) error->clear();
    return true;
}

std::unique_ptr<QSettings> OpenGuiSettings(const QmlOnboardingSettings::GuiSettingsStore& store)
{
    std::unique_ptr<QSettings> settings;
    if (!store.organization_name.isEmpty() && !store.application_name.isEmpty()) {
        settings = std::make_unique<QSettings>(
            store.format,
            store.scope,
            store.organization_name,
            store.application_name);
    } else {
        settings = std::make_unique<QSettings>(store.file_name, store.format);
    }
    settings->setFallbacksEnabled(false);
    return settings;
}

QString GuiApplicationNameForChain(const QString& chain)
{
    const QString normalized{chain.toLower()};
    if (normalized == QStringLiteral("test")) return QStringLiteral(QAPP_APP_NAME_TESTNET);
    if (normalized == QStringLiteral("testnet4")) return QStringLiteral(QAPP_APP_NAME_TESTNET4);
    if (normalized == QStringLiteral("signet")) return QStringLiteral(QAPP_APP_NAME_SIGNET);
    if (normalized == QStringLiteral("regtest")) return QStringLiteral(QAPP_APP_NAME_REGTEST);
    return QStringLiteral(QAPP_APP_NAME_DEFAULT);
}

bool ReadResolvedGuiReset(const ArgsManager& args)
{
    QmlOnboardingSettings::GuiSettingsStore store{
        QmlOnboardingSettings::CurrentGuiSettingsStore()
    };
    store.application_name = GuiApplicationNameForChain(
        QString::fromStdString(args.GetChainTypeString()));
    const std::unique_ptr<QSettings> settings{OpenGuiSettings(store)};
    return settings->value(QStringLiteral("fReset"), false).toBool();
}

bool SameGuiSettingsStore(
    const QmlOnboardingSettings::GuiSettingsStore& left,
    const QmlOnboardingSettings::GuiSettingsStore& right)
{
    return left.file_name == right.file_name && left.format == right.format;
}

QVariantMap SnapshotGuiSettings(const QSettings& settings)
{
    QVariantMap values;
    for (const QString& key : settings.allKeys()) {
        values.insert(key, settings.value(key));
    }
    return values;
}

bool SyncGuiSettings(QSettings& settings, const QString& action, QString* error)
{
    settings.sync();
    if (settings.status() == QSettings::NoError) return true;
    if (error) *error = QStringLiteral("%1 failed for %2.").arg(action, settings.fileName());
    return false;
}

bool WriteGuiSettings(QSettings& settings, const QVariantMap& values, const QString& action, QString* error)
{
    settings.clear();
    for (auto it = values.cbegin(); it != values.cend(); ++it) {
        settings.setValue(it.key(), it.value());
    }
    return SyncGuiSettings(settings, action, error);
}

void RestoreGuiSettings(
    const QmlOnboardingSettings::GuiSettingsStore& store,
    const QVariantMap& values,
    QString* error)
{
    std::unique_ptr<QSettings> settings{OpenGuiSettings(store)};
    QString rollback_error;
    if (!WriteGuiSettings(*settings, values, QStringLiteral("GUI settings rollback"), &rollback_error) && error) {
        *error += QStringLiteral(" %1").arg(rollback_error);
    }
}

bool BackupGuiSettings(QSettings& source, const fs::path& backup_path, QString* error)
{
    QSettings backup{
        QString::fromStdString(fs::PathToString(backup_path)),
        QSettings::IniFormat,
    };
    backup.setFallbacksEnabled(false);
    return WriteGuiSettings(
        backup,
        SnapshotGuiSettings(source),
        QStringLiteral("GUI settings backup"),
        error);
}

std::map<std::string, common::SettingsValue> SnapshotRwSettings(ArgsManager& args)
{
    std::map<std::string, common::SettingsValue> values;
    args.LockSettings([&](common::Settings& settings) {
        values = settings.rw_settings;
    });
    return values;
}

bool RwSettingsEqual(
    const std::map<std::string, common::SettingsValue>& left,
    const std::map<std::string, common::SettingsValue>& right)
{
    if (left.size() != right.size()) return false;
    auto left_it = left.cbegin();
    auto right_it = right.cbegin();
    for (; left_it != left.cend(); ++left_it, ++right_it) {
        if (left_it->first != right_it->first ||
            left_it->second.write() != right_it->second.write()) {
            return false;
        }
    }
    return true;
}

void RestoreRwSettings(ArgsManager& args, const std::map<std::string, common::SettingsValue>& values)
{
    args.LockSettings([&](common::Settings& settings) {
        settings.rw_settings = values;
    });
}

bool RollBackSettingsFile(ArgsManager& args, const std::map<std::string, common::SettingsValue>& values, QString* error)
{
    RestoreRwSettings(args, values);
    QString rollback_error;
    if (WriteSettingsFile(args, &rollback_error)) return true;
    if (error) {
        *error += QStringLiteral(" Settings rollback failed: %1").arg(rollback_error);
    }
    return false;
}

void RestoreForcedSettings(
    ArgsManager& args,
    const std::map<std::string, common::SettingsValue>& forced_settings)
{
    args.LockSettings([&](common::Settings& settings) {
        settings.forced_settings = forced_settings;
    });
}

bool ApplyPendingCoreSettings(ArgsManager& args, const QmlOnboardingSettings::PendingApply& pending, QString* error)
{
    std::map<std::string, common::SettingsValue> original_forced_settings;
    args.LockSettings([&](common::Settings& settings) {
        original_forced_settings = settings.forced_settings;
    });

    QmlCoreSettings::Session core_settings{
        pending.values,
        QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames()),
    };
    const auto write_setting = [&](const QString& name) {
        return !pending.touched_settings.contains(name) || core_settings.writeToArgs(args, name);
    };
    const auto recompute_interactions = [&] {
        RestoreForcedSettings(args, original_forced_settings);
        InitParameterInteraction(args);
    };

    bool settings_written{true};
    for (const QString& name : {
             QStringLiteral("proxy"),
             QStringLiteral("onion"),
             QStringLiteral("server"),
             QStringLiteral("prune"),
         }) {
        settings_written = write_setting(name) && settings_written;
    }
    recompute_interactions();
    settings_written = write_setting(QStringLiteral("listen")) && settings_written;
    recompute_interactions();
    settings_written = write_setting(QStringLiteral("natpmp")) && settings_written;
    RestoreForcedSettings(args, original_forced_settings);

    if (!settings_written) {
        if (error) *error = QStringLiteral("One or more startup settings could not be written.");
        return false;
    }

    QmlCoreSettings::SetRwSetting(args, QString::fromLatin1(QML_ONBOARDED_KEY), common::SettingsValue{true});
    return true;
}

bool SameOptionalPath(const QString& left, const QString& right)
{
    if (left.isEmpty() || right.isEmpty()) return left.isEmpty() && right.isEmpty();
    return SameDataDirPath(left, right);
}

QString SettingsPathString(ArgsManager& args)
{
    fs::path settings_path;
    if (!args.GetSettingsPath(&settings_path)) return {};
    return QmlDataDir::NormalizeLocalPath(
        QString::fromStdString(fs::PathToString(settings_path)));
}

QString SettingsPathForNewDataDir(ArgsManager& args, const QString& data_dir)
{
    const fs::path settings{args.GetPathArg("-settings", BITCOIN_SETTINGS_FILENAME)};
    if (settings.empty()) return {};

    fs::path network_data_dir{QmlDataDir::QStringToPath(data_dir)};
    if (!BaseParams().DataDir().empty()) {
        network_data_dir /= fs::PathFromString(BaseParams().DataDir());
    }
    return QmlDataDir::NormalizeLocalPath(
        QString::fromStdString(
            fs::PathToString(fsbridge::AbsPathJoin(network_data_dir, settings))));
}

bool ValidatePendingApply(
    ArgsManager& args,
    const QmlOnboardingSettings::PendingApply& pending,
    QString* error)
{
    if (pending.effective_reset != args.GetBoolArg("-resetguisettings", false)) {
        if (error) {
            *error = QStringLiteral(
                "The selected Bitcoin profile changed while onboarding was open. "
                "Restart and review the current data directory, network, and settings before applying.");
        }
        return false;
    }

    if (!pending.target_complete) return true;

    const QString actual_data_dir{ActiveDataDirString(args)};
    const QString actual_chain{QString::fromStdString(args.GetChainTypeString())};
    const QString actual_settings_path{SettingsPathString(args)};
    if (SameDataDirPath(actual_data_dir, pending.resolved_data_dir) &&
        actual_chain == pending.resolved_chain &&
        SameOptionalPath(actual_settings_path, pending.resolved_settings_path)) {
        return true;
    }

    if (error) {
        *error = QStringLiteral(
            "The selected Bitcoin profile changed while onboarding was open. "
            "Restart and review the current data directory, network, and settings before applying.");
    }
    return false;
}

bool ShouldUpdateBootstrapDataDir(const QmlOnboardingSettings::PendingApply& pending)
{
    return ShouldPersistGuiDataDirSelection(pending.data_dir.source, pending.explicit_datadir_arg);
}

void ApplyBootstrapGuiSettings(QVariantMap& settings, const QmlOnboardingSettings::PendingApply& pending)
{
    if (ShouldUpdateBootstrapDataDir(pending)) {
        if (QmlDataDir::IsDefaultDataDir(pending.data_dir.path)) {
            settings.remove(QString::fromUtf8(SettingsKeys::DATA_DIR));
        } else {
            settings.insert(QString::fromUtf8(SettingsKeys::DATA_DIR), pending.data_dir.path);
        }
    }
    settings.insert(QStringLiteral("fReset"), false);
}

QVariantMap ResetGuiSettingsValues(
    const QVariantMap& original,
    bool onboarding_completed)
{
    QVariantMap values;
    const QString data_dir_key{QString::fromUtf8(SettingsKeys::DATA_DIR)};
    if (original.contains(data_dir_key)) {
        values.insert(data_dir_key, original.value(data_dir_key));
    }
    values.insert(QStringLiteral("fReset"), !onboarding_completed);
    return values;
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

GuiSettingsStore CurrentGuiSettingsStore()
{
    QSettings settings;
    settings.setFallbacksEnabled(false);
    return {
        settings.organizationName(),
        settings.applicationName(),
        settings.fileName(),
        settings.format(),
        settings.scope(),
    };
}

OnboardingStartupStatus ResolveOnboardingStartupStatus(const std::vector<std::string>& argv, bool can_listen_ipc)
{
    OnboardingStartupStatus status;

    ArgsManager preview_args;
    std::string parse_error;
    if (!PrepareArgs(preview_args, argv, can_listen_ipc, parse_error)) {
        status.error = QString::fromStdString(parse_error);
        return status;
    }

    try {
        SelectParams(preview_args.GetChainType());
    } catch (const std::exception& e) {
        status.error = QString::fromStdString(e.what());
        return status;
    }

    // Capture chooser state before a saved GUI datadir is soft-set as
    // -datadir. The effective reset value is resolved separately after config
    // and settings have been read.
    const bool force_data_dir_chooser{
        QmlDataDir::ShouldShowDataDirChooser(preview_args)
    };
    const bool explicit_datadir = HasExplicitDataDirArg(preview_args);
    QString selected_data_dir;
    DataDirSource selected_data_dir_source{DataDirSource::Default};
    if (explicit_datadir) {
        selected_data_dir = ExplicitDataDirString(preview_args);
        selected_data_dir_source = DataDirSource::ExplicitArg;
    } else {
        const QmlDataDir::GuiDataDir gui_data_dir = QmlDataDir::ReadGuiDataDirWithSource();
        selected_data_dir = gui_data_dir.path;
        selected_data_dir_source = ToOnboardingDataDirSource(gui_data_dir.source);
    }
    if (selected_data_dir.isEmpty()) {
        selected_data_dir = QmlDataDir::DefaultDataDirString();
    }
    SetStartupDataDirs(status, selected_data_dir, selected_data_dir_source, selected_data_dir, explicit_datadir);

    const bool apply_datadir_before_config = ShouldApplyDataDirBeforeConfig(selected_data_dir_source, explicit_datadir, selected_data_dir);
    const bool custom_datadir_exists = apply_datadir_before_config && QFileInfo::exists(selected_data_dir);
    const bool can_read_profile = !apply_datadir_before_config || custom_datadir_exists;
    if (custom_datadir_exists) {
        QmlDataDir::ApplyDataDirArg(preview_args, selected_data_dir);
    }

    QString read_error;
    ReadOnlyProfileResult profile_read;
    if (can_read_profile) {
        if (!ReadResolvedProfile(preview_args, profile_read, &read_error)) {
            status.error = read_error;
            status.settings_file_unreadable = profile_read.settings_file_unreadable;
            return status;
        }
        const QString resolved_data_dir = ActiveDataDirString(preview_args);
        SetStartupDataDirs(status, selected_data_dir, selected_data_dir_source, resolved_data_dir, explicit_datadir);
    } else {
        try {
            SelectParams(preview_args.GetChainType());
            preview_args.SelectConfigNetwork(preview_args.GetChainTypeString());
        } catch (const std::exception& e) {
            status.error = QString::fromStdString(e.what());
            return status;
        }
        SetStartupDataDirs(status, selected_data_dir, selected_data_dir_source, selected_data_dir, explicit_datadir);
        status.ok = true;
        status.should_show_onboarding = true;
        return status;
    }

    const bool force_show_onboarding{
        preview_args.GetBoolArg("-resetguisettings", false) ||
        force_data_dir_chooser ||
        (!explicit_datadir && ReadResolvedGuiReset(preview_args))
    };
    status.settings_enabled = profile_read.settings_enabled;
    if (!status.settings_enabled) {
        status.ok = true;
        status.qml_onboarded = !force_show_onboarding;
        status.should_show_onboarding = force_show_onboarding;
        return status;
    }

    const bool qml_onboarded = CommandLineBoolArg(preview_args, QML_ONBOARDED_KEY)
        .value_or(SettingToBool(preview_args.GetPersistentSetting(QML_ONBOARDED_KEY), false));
    status.qml_onboarded = force_show_onboarding ? false : qml_onboarded;
    status.should_show_onboarding = force_show_onboarding || !qml_onboarded;
    status.ok = true;
    return status;
}

PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const DataDirSelection& data_dir_selection)
{
    PreviewResult result;

    const QString data_dir = NormalizedDataDirPath(data_dir_selection);
    result.selected_data_dir = data_dir;
    result.selected_data_dir_source = data_dir_selection.source;
    result.resolved_data_dir = data_dir;
    result.resolved_data_dir_source = data_dir_selection.source;
    const QString validation_error = QmlDataDir::ValidateCustomDataDir(data_dir);
    if (!validation_error.isEmpty()) {
        result.error = validation_error;
        return result;
    }

    ArgsManager preview_args;
    std::string parse_error;
    if (!PrepareArgs(preview_args, argv, can_listen_ipc, parse_error)) {
        result.error = QString::fromStdString(parse_error);
        return result;
    }

    try {
        SelectParams(preview_args.GetChainType());
    } catch (const std::exception& e) {
        result.error = QString::fromStdString(e.what());
        return result;
    }

    const bool explicit_datadir = HasExplicitDataDirArg(preview_args);
    const QString selected_data_dir = explicit_datadir ? ExplicitDataDirString(preview_args) : data_dir;
    const DataDirSource selected_data_dir_source = explicit_datadir ? DataDirSource::ExplicitArg : data_dir_selection.source;
    SetPreviewDataDirs(result, selected_data_dir, selected_data_dir_source, selected_data_dir, explicit_datadir);
    const bool apply_datadir_before_config = ShouldApplyDataDirBeforeConfig(data_dir_selection.source, explicit_datadir, data_dir);
    const bool custom_datadir_exists = apply_datadir_before_config && QFileInfo::exists(data_dir);
    bool config_file_path_available{false};
    if (custom_datadir_exists) {
        QmlDataDir::ApplyDataDirArg(preview_args, data_dir);
    }

    if (!apply_datadir_before_config || custom_datadir_exists) {
        ReadOnlyProfileResult profile_read;
        QString read_error;
        if (!ReadResolvedProfile(preview_args, profile_read, &read_error)) {
            result.error = read_error;
            return result;
        }
        config_file_path_available = profile_read.config_file_path_available;
        SetPreviewDataDirs(result, selected_data_dir, selected_data_dir_source, ActiveDataDirString(preview_args), explicit_datadir);
        result.effective_reset = preview_args.GetBoolArg("-resetguisettings", false);
        if (result.effective_reset) {
            preview_args.LockSettings([](common::Settings& settings) {
                settings.rw_settings.clear();
            });
        } else {
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
        SetPreviewDataDirs(result, selected_data_dir, selected_data_dir_source, selected_data_dir, explicit_datadir);
        result.effective_reset = preview_args.GetBoolArg("-resetguisettings", false);
        if (!result.effective_reset) {
            const QmlLegacySettings::MigrationResult migration_result{
                QmlLegacySettings::MigrateCoreSettings(preview_args, QmlLegacySettings::MigrationMode::Preview)
            };
            if (!migration_result.error.isEmpty()) {
                result.error = migration_result.error;
                return result;
            }
        }
    }

    // Preview should show the settings the node will run with. In reset mode,
    // old GUI-owned settings.json overrides are intentionally skipped first,
    // then core parameter interactions are applied on top of command-line and
    // bitcoin.conf values.
    InitParameterInteraction(preview_args);

    result.assumed_blockchain_size = static_cast<int>(Params().AssumedBlockchainSize());
    result.assumed_chainstate_size = static_cast<int>(Params().AssumedChainStateSize());
    result.profile = BuildProfileSummary(preview_args, config_file_path_available);
    result.core_setting_statuses = QmlCoreSettings::BuildCoreSettingStatuses(preview_args, QmlCoreSettings::OnboardingCoreSettingNames());
    result.values = QmlCoreSettings::LoadEffectiveValues(preview_args);
    result.resolved_chain = QString::fromStdString(preview_args.GetChainTypeString());
    if (apply_datadir_before_config && !custom_datadir_exists) {
        result.resolved_settings_path = SettingsPathForNewDataDir(preview_args, result.resolved_data_dir);
    } else {
        result.resolved_settings_path = SettingsPathString(preview_args);
    }
    result.ok = true;
    return result;
}

PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const QString& data_dir)
{
    return Preview(argv, can_listen_ipc, DataDirSelection{data_dir, DataDirSource::UserSelection});
}

bool MarkQmlOnboarded(ArgsManager& args, QString* error)
{
    QmlCoreSettings::SetRwSetting(args, QString::fromLatin1(QML_ONBOARDED_KEY), common::SettingsValue{true});
    return WriteSettingsFile(args, error);
}

bool PrepareApplyToArgs(ArgsManager& args, const DataDirSelection& data_dir_selection, const QString& resolved_data_dir, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, bool effective_reset, PendingApply& pending, QString* error)
{
    if (error) error->clear();

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

    pending.data_dir = {
        data_dir,
        data_dir_selection.source,
    };
    pending.resolved_data_dir = QmlDataDir::NormalizeLocalPath(resolved_data_dir);
    pending.touched_settings = touched_settings;
    pending.values = values;
    pending.explicit_datadir_arg = explicit_datadir;
    pending.effective_reset = effective_reset;
    return true;
}

bool FinalizeStartupSettings(ArgsManager& args, const GuiSettingsStore& bootstrap_gui_settings, const PendingApply* pending, FinalizeResult* result, QString* error)
{
    if (error) error->clear();
    if (result) *result = {};

    if (pending && !ValidatePendingApply(args, *pending, error)) return false;

    const bool reset_gui_settings{args.GetBoolArg("-resetguisettings", false)};
    const std::map<std::string, common::SettingsValue> original_rw_settings{SnapshotRwSettings(args)};

    const GuiSettingsStore active_gui_settings_store{CurrentGuiSettingsStore()};
    std::unique_ptr<QSettings> active_gui_settings{OpenGuiSettings(active_gui_settings_store)};
    const QVariantMap original_active_gui_settings{SnapshotGuiSettings(*active_gui_settings)};
    if (active_gui_settings->status() != QSettings::NoError) {
        if (error) {
            *error = QStringLiteral("GUI settings could not be read from %1.")
                         .arg(active_gui_settings->fileName());
        }
        return false;
    }
    const bool bootstrap_is_active{SameGuiSettingsStore(active_gui_settings_store, bootstrap_gui_settings)};
    const bool bootstrap_settings_needed{
        !bootstrap_is_active && (pending != nullptr || reset_gui_settings)
    };
    std::unique_ptr<QSettings> bootstrap_gui_settings_owner;
    QSettings* bootstrap_settings{bootstrap_is_active ? active_gui_settings.get() : nullptr};
    QVariantMap original_bootstrap_gui_settings;
    if (bootstrap_settings_needed) {
        bootstrap_gui_settings_owner = OpenGuiSettings(bootstrap_gui_settings);
        bootstrap_settings = bootstrap_gui_settings_owner.get();
        original_bootstrap_gui_settings = SnapshotGuiSettings(*bootstrap_settings);
        if (bootstrap_settings->status() != QSettings::NoError) {
            if (error) {
                *error = QStringLiteral("Bootstrap GUI settings could not be read from %1.")
                             .arg(bootstrap_settings->fileName());
            }
            return false;
        }
    }

    if (reset_gui_settings) {
        if (!BackupSettingsFile(args, error)) return false;
        if (!BackupGuiSettings(*active_gui_settings, args.GetDataDirNet() / "guisettings.ini.bak", error)) return false;
        args.LockSettings([](common::Settings& settings) {
            settings.rw_settings.clear();
        });
    }

    if (!reset_gui_settings) {
        const QmlLegacySettings::MigrationResult migration_result{
            QmlLegacySettings::MigrateCoreSettings(args, QmlLegacySettings::MigrationMode::Preview)
        };
        if (!migration_result.error.isEmpty()) {
            if (error) *error = migration_result.error;
            RestoreRwSettings(args, original_rw_settings);
            return false;
        }
    }

    if (pending) {
        if (!ApplyPendingCoreSettings(args, *pending, error)) {
            RestoreRwSettings(args, original_rw_settings);
            return false;
        }
    }

    const bool settings_changed{!RwSettingsEqual(SnapshotRwSettings(args), original_rw_settings)};
    if (settings_changed && !WriteSettingsFile(args, error)) {
        RestoreRwSettings(args, original_rw_settings);
        return false;
    }

    QVariantMap active_gui_settings_values{original_active_gui_settings};
    QVariantMap bootstrap_gui_settings_values{original_bootstrap_gui_settings};
    if (reset_gui_settings) {
        active_gui_settings_values = ResetGuiSettingsValues(
            original_active_gui_settings,
            /*onboarding_completed=*/pending != nullptr);
    }

    if (pending) {
        active_gui_settings_values.insert(QStringLiteral("fReset"), false);
        if (bootstrap_is_active) {
            ApplyBootstrapGuiSettings(active_gui_settings_values, *pending);
        } else {
            ApplyBootstrapGuiSettings(bootstrap_gui_settings_values, *pending);
        }
    } else if (reset_gui_settings && !bootstrap_is_active) {
        bootstrap_gui_settings_values.insert(QStringLiteral("fReset"), true);
    }

    const bool active_gui_settings_changed{active_gui_settings_values != original_active_gui_settings};
    const bool bootstrap_gui_settings_changed{
        bootstrap_settings_needed && bootstrap_gui_settings_values != original_bootstrap_gui_settings
    };
    if (active_gui_settings_changed &&
        !WriteGuiSettings(
            *active_gui_settings,
            active_gui_settings_values,
            QStringLiteral("GUI settings update"),
            error)) {
        RestoreGuiSettings(active_gui_settings_store, original_active_gui_settings, error);
        RollBackSettingsFile(args, original_rw_settings, error);
        return false;
    }
    if (bootstrap_gui_settings_changed &&
        !WriteGuiSettings(
            *bootstrap_settings,
            bootstrap_gui_settings_values,
            QStringLiteral("Bootstrap GUI settings update"),
            error)) {
        if (active_gui_settings_changed) {
            RestoreGuiSettings(active_gui_settings_store, original_active_gui_settings, error);
        }
        RestoreGuiSettings(bootstrap_gui_settings, original_bootstrap_gui_settings, error);
        RollBackSettingsFile(args, original_rw_settings, error);
        return false;
    }

    QString legacy_cleanup_error;
    bool legacy_cleanup_ok{true};
    if (reset_gui_settings) {
        legacy_cleanup_ok = QmlLegacySettings::ClearLegacyGuiSettings(
            QString::fromStdString(args.GetChainTypeString()),
            &legacy_cleanup_error);
    } else {
        QmlLegacySettings::GuiCleanup cleanup{QmlLegacySettings::GuiCleanup::None};
        if (pending && ShouldUpdateBootstrapDataDir(*pending)) {
            cleanup = QmlLegacySettings::GuiCleanup::DataDirAndReset;
        } else if (pending) {
            cleanup = QmlLegacySettings::GuiCleanup::ResetOnly;
        }

        const std::map<std::string, common::SettingsValue> committed_rw_settings{
            SnapshotRwSettings(args)
        };
        const QmlLegacySettings::MigrationResult migration_result{
            QmlLegacySettings::MigrateCoreSettings(
                args,
                QmlLegacySettings::MigrationMode::Persist,
                cleanup)
        };
        // Persist removes the staged legacy keys, but the values already
        // committed above remain authoritative for this process.
        RestoreRwSettings(args, committed_rw_settings);
        legacy_cleanup_ok = migration_result.error.isEmpty();
        legacy_cleanup_error = migration_result.error;
    }

    if (!legacy_cleanup_ok) {
        if (error) *error = legacy_cleanup_error;
        if (bootstrap_gui_settings_changed) {
            RestoreGuiSettings(bootstrap_gui_settings, original_bootstrap_gui_settings, error);
        }
        if (active_gui_settings_changed) {
            RestoreGuiSettings(active_gui_settings_store, original_active_gui_settings, error);
        }
        RollBackSettingsFile(args, original_rw_settings, error);
        return false;
    }

    if (result) {
        result->reset_applied = reset_gui_settings;
        result->settings_changed = settings_changed;
    }
    return true;
}

} // namespace QmlOnboardingSettings
