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
#include <qml/legacy_settings_migration.h>
#include <qml/translationmanager.h>
#include <univalue.h>
#include <util/fs.h>
#include <util/fs_helpers.h>

#include <QFileInfo>

#include <map>
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

bool ReadSettingsFileIfPresent(ArgsManager& args, QString* error)
{
    fs::path settings_path;
    if (!args.GetSettingsPath(&settings_path) || !fs::exists(settings_path)) {
        if (error) error->clear();
        return true;
    }

    std::vector<std::string> settings_errors;
    if (!args.ReadSettingsFile(&settings_errors)) {
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

QmlOnboardingSettings::ProfileSummary BuildProfileSummary(const ArgsManager& args, bool config_file_path_available)
{
    QmlOnboardingSettings::ProfileSummary summary;
    fs::path settings_path;
    summary.has_settings_file = args.GetSettingsPath(&settings_path) && PathExists(settings_path);
    summary.has_config_file = config_file_path_available && PathExists(args.GetConfigFilePath());
    summary.has_chain_data = HasExistingChainData(args);
    summary.existing_profile = summary.has_settings_file ||
                               summary.has_config_file ||
                               summary.has_chain_data;
    return summary;
}

bool EnsureSettingsDirectory(ArgsManager& args, const fs::path& settings_path, QString* error)
{
    try {
        const fs::path network_data_dir{args.GetDataDirNet()};
        TryCreateDirectories(network_data_dir);
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

OnboardingStartupStatus ResolveOnboardingStartupStatus(const std::vector<std::string>& argv, bool can_listen_ipc)
{
    OnboardingStartupStatus status;

    ArgsManager preview_args;
    std::string parse_error;
    if (!PrepareArgs(preview_args, argv, can_listen_ipc, parse_error)) {
        status.error = QString::fromStdString(parse_error);
        return status;
    }
    status.language = TranslationManager::ResolveLanguage(preview_args);

    try {
        SelectParams(preview_args.GetChainType());
    } catch (const std::exception& e) {
        status.error = QString::fromStdString(e.what());
        return status;
    }

    const bool reset_gui_settings = preview_args.GetBoolArg("-resetguisettings", false);
    const bool explicit_datadir = HasExplicitDataDirArg(preview_args);
    const bool force_show_onboarding = reset_gui_settings || QmlDataDir::ShouldShowDataDirChooser(preview_args);
    if (explicit_datadir) {
        status.active_data_dir = ExplicitDataDirString(preview_args);
        status.data_dir_source = DataDirSource::ExplicitArg;
    } else if (reset_gui_settings) {
        status.active_data_dir = QmlDataDir::DefaultDataDirString();
        status.data_dir_source = DataDirSource::Default;
    } else {
        const QmlDataDir::GuiDataDir gui_data_dir = QmlDataDir::ReadGuiDataDirWithSource();
        status.active_data_dir = gui_data_dir.path;
        status.data_dir_source = ToOnboardingDataDirSource(gui_data_dir.source);
    }
    if (status.active_data_dir.isEmpty()) {
        status.active_data_dir = QmlDataDir::DefaultDataDirString();
    }

    const bool apply_datadir_before_config = ShouldApplyDataDirBeforeConfig(status.data_dir_source, explicit_datadir, status.active_data_dir);
    const bool custom_datadir_exists = apply_datadir_before_config && QFileInfo::exists(status.active_data_dir);
    const bool can_read_profile = !apply_datadir_before_config || custom_datadir_exists;
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
        try {
            SelectParams(preview_args.GetChainType());
            preview_args.SelectConfigNetwork(preview_args.GetChainTypeString());
        } catch (const std::exception& e) {
            status.error = QString::fromStdString(e.what());
            return status;
        }
        status.ok = true;
        status.should_show_onboarding = true;
        return status;
    }

    // Reuse the same read-only profile preview for startup translation, even
    // when onboarding is explicitly requested for an existing profile.
    if (!ReadSettingsFileIfPresent(preview_args, &read_error)) {
        status.error = read_error;
        return status;
    }
    status.language = TranslationManager::ResolveLanguage(preview_args);

    if (force_show_onboarding) {
        status.ok = true;
        status.qml_onboarded = false;
        status.should_show_onboarding = true;
        return status;
    }

    fs::path settings_path;
    if (!preview_args.GetSettingsPath(&settings_path)) {
        status.ok = true;
        status.settings_enabled = false;
        status.qml_onboarded = true;
        status.should_show_onboarding = false;
        return status;
    }

    status.qml_onboarded = CommandLineBoolArg(preview_args, QML_ONBOARDED_KEY)
        .value_or(SettingToBool(preview_args.GetPersistentSetting(QML_ONBOARDED_KEY), false));
    status.should_show_onboarding = !status.qml_onboarded;
    status.ok = true;
    return status;
}

PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const DataDirSelection& data_dir_selection)
{
    PreviewResult result;

    const QString data_dir = NormalizedDataDirPath(data_dir_selection);
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
    const bool apply_datadir_before_config = ShouldApplyDataDirBeforeConfig(data_dir_selection.source, explicit_datadir, data_dir);
    const bool custom_datadir_exists = apply_datadir_before_config && QFileInfo::exists(data_dir);
    bool config_file_path_available{false};
    if (custom_datadir_exists) {
        QmlDataDir::ApplyDataDirArg(preview_args, data_dir);
    }

    if (!apply_datadir_before_config || custom_datadir_exists) {
        std::string config_error;
        if (!preview_args.ReadConfigFiles(config_error, true)) {
            result.error = QString::fromStdString(config_error);
            return result;
        }
        config_file_path_available = true;
        try {
            SelectParams(preview_args.GetChainType());
            preview_args.SelectConfigNetwork(preview_args.GetChainTypeString());
        } catch (const std::exception& e) {
            result.error = QString::fromStdString(e.what());
            return result;
        }
        const bool reset_gui_settings = preview_args.GetBoolArg("-resetguisettings", false);
        if (!reset_gui_settings) {
            std::vector<std::string> settings_errors;
            if (!preview_args.ReadSettingsFile(&settings_errors)) {
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

bool ApplyToArgs(ArgsManager& args, const DataDirSelection& data_dir_selection, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, QString* error)
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

    std::vector<std::string> settings_errors;
    const bool reset_gui_settings = args.GetBoolArg("-resetguisettings", false);
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
        QmlLegacySettings::ClearLegacyGuiSettings(QString::fromStdString(args.GetChainTypeString()));
    } else {
        if (!args.ReadSettingsFile(&settings_errors)) {
            if (error) *error = QString::fromStdString(settings_errors.empty() ? std::string{"Settings file could not be read."} : settings_errors.front());
            return false;
        }
        const QmlLegacySettings::MigrationResult migration_result{
            QmlLegacySettings::MigrateCoreSettings(args, QmlLegacySettings::MigrationMode::Persist)
        };
        if (!migration_result.error.isEmpty()) {
            if (error) *error = migration_result.error;
            return false;
        }
    }

    std::map<std::string, common::SettingsValue> original_forced_settings;
    args.LockSettings([&](common::Settings& settings) {
        original_forced_settings = settings.forced_settings;
    });
    InitParameterInteraction(args);

    QmlCoreSettings::Session core_settings{values, QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::OnboardingCoreSettingNames())};
    core_settings.setTouchedSettings(touched_settings);
    const bool touched_settings_written = core_settings.writeTouchedToArgs(args);
    args.LockSettings([&](common::Settings& settings) {
        settings.forced_settings = std::move(original_forced_settings);
    });
    if (!touched_settings_written) {
        if (error) *error = QStringLiteral("One or more startup settings could not be written.");
        return false;
    }

    QmlCoreSettings::SetRwSetting(args, QString::fromLatin1(QML_ONBOARDED_KEY), common::SettingsValue{true});
    if (!WriteSettingsFile(args, error)) return false;

    if (ShouldPersistGuiDataDirSelection(data_dir_selection.source, explicit_datadir)) {
        if (QmlDataDir::IsDefaultDataDir(data_dir)) {
            QmlDataDir::PersistDefaultDataDirSelection();
        } else if (!QmlDataDir::PersistGuiDataDirSelection(data_dir, &data_dir_error)) {
            if (error) *error = data_dir_error;
            return false;
        }
    }
    return true;
}

bool ApplyToArgs(ArgsManager& args, const QString& data_dir, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, QString* error)
{
    return ApplyToArgs(args, DataDirSelection{data_dir, DataDirSource::UserSelection}, touched_settings, values, error);
}

} // namespace QmlOnboardingSettings
