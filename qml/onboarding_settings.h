// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_ONBOARDING_SETTINGS_H
#define BITCOIN_QML_ONBOARDING_SETTINGS_H

#include <qml/core_settings.h>

#include <QByteArray>
#include <QSet>
#include <QSettings>
#include <QString>
#include <QVariantMap>

#include <string>
#include <vector>

class ArgsManager;

namespace QmlOnboardingSettings {

enum class DataDirSource {
    Default,
    ExplicitArg,
    GuiSetting,
    LegacyGuiSetting,
    Config,
    UserSelection,
};

struct DataDirSelection {
    QString path;
    DataDirSource source{DataDirSource::UserSelection};
};

struct GuiSettingsStore {
    QString organization_name;
    QString application_name;
    QString file_name;
    QSettings::Format format{QSettings::NativeFormat};
    QSettings::Scope scope{QSettings::UserScope};
};

struct SettingsFileBackup {
    QString source_path;
    QByteArray contents;
};

struct PendingApply {
    DataDirSelection data_dir;
    QString resolved_data_dir;
    QString resolved_chain;
    QString resolved_settings_path;
    QSet<QString> touched_settings;
    QmlCoreSettings::Values values;
    bool explicit_datadir_arg{false};
    bool effective_reset{false};
    bool target_complete{false};
};

struct FinalizeResult {
    bool reset_applied{false};
    bool settings_changed{false};
};

struct ProfileSummary {
    bool existing_profile{false};
    bool has_settings_file{false};
    bool has_config_file{false};
    bool has_chain_data{false};
    bool has_wallet_data{false};
};

struct PreviewResult {
    bool ok{false};
    QString error;
    QString selected_data_dir;
    DataDirSource selected_data_dir_source{DataDirSource::Default};
    QString resolved_data_dir;
    DataDirSource resolved_data_dir_source{DataDirSource::Default};
    bool config_redirected_data_dir{false};
    bool effective_reset{false};
    QString resolved_chain;
    QString resolved_settings_path;
    QmlCoreSettings::Values values;
    QVariantMap core_setting_statuses;
    int assumed_blockchain_size{0};
    int assumed_chainstate_size{0};
    ProfileSummary profile;
};

struct OnboardingStartupStatus {
    bool ok{false};
    QString error;
    bool settings_file_unreadable{false};
    QString selected_data_dir;
    DataDirSource selected_data_dir_source{DataDirSource::Default};
    QString resolved_data_dir;
    DataDirSource resolved_data_dir_source{DataDirSource::Default};
    bool config_redirected_data_dir{false};
    QString active_data_dir;
    DataDirSource data_dir_source{DataDirSource::Default};
    bool settings_enabled{true};
    bool qml_onboarded{false};
    bool should_show_onboarding{true};
};

bool PrepareArgs(ArgsManager& args, const std::vector<std::string>& argv, bool can_listen_ipc, std::string& error);
bool CaptureSettingsFileBackup(ArgsManager& args, SettingsFileBackup& backup, QString* error = nullptr);
GuiSettingsStore CurrentGuiSettingsStore();
OnboardingStartupStatus ResolveOnboardingStartupStatus(const std::vector<std::string>& argv, bool can_listen_ipc);
PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const DataDirSelection& data_dir);
PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const QString& data_dir);
bool MarkQmlOnboarded(ArgsManager& args, QString* error = nullptr);
bool PrepareApplyToArgs(ArgsManager& args, const DataDirSelection& data_dir, const QString& resolved_data_dir, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, bool effective_reset, PendingApply& pending, QString* error = nullptr);
bool FinalizeStartupSettings(ArgsManager& args, const GuiSettingsStore& bootstrap_gui_settings, const PendingApply* pending, FinalizeResult* result = nullptr, QString* error = nullptr, const SettingsFileBackup* settings_file_backup = nullptr);

} // namespace QmlOnboardingSettings

#endif // BITCOIN_QML_ONBOARDING_SETTINGS_H
