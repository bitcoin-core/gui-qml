// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_ONBOARDING_SETTINGS_H
#define BITCOIN_QML_ONBOARDING_SETTINGS_H

#include <qml/core_settings.h>

#include <QSet>
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

struct ProfileSummary {
    bool existing_profile{false};
    bool has_settings_file{false};
    bool has_config_file{false};
    bool has_chain_data{false};
};

struct PreviewResult {
    bool ok{false};
    QString error;
    QmlCoreSettings::Values values;
    QVariantMap core_setting_statuses;
    int assumed_blockchain_size{0};
    int assumed_chainstate_size{0};
    ProfileSummary profile;
};

struct OnboardingStartupStatus {
    bool ok{false};
    QString error;
    QString language;
    QString active_data_dir;
    DataDirSource data_dir_source{DataDirSource::Default};
    bool settings_enabled{true};
    bool qml_onboarded{false};
    bool should_show_onboarding{true};
};

bool PrepareArgs(ArgsManager& args, const std::vector<std::string>& argv, bool can_listen_ipc, std::string& error);
OnboardingStartupStatus ResolveOnboardingStartupStatus(const std::vector<std::string>& argv, bool can_listen_ipc);
PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const DataDirSelection& data_dir);
PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const QString& data_dir);
bool MarkQmlOnboarded(ArgsManager& args, QString* error = nullptr);
bool ApplyToArgs(ArgsManager& args, const DataDirSelection& data_dir, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, QString* error = nullptr);
bool ApplyToArgs(ArgsManager& args, const QString& data_dir, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, QString* error = nullptr);

} // namespace QmlOnboardingSettings

#endif // BITCOIN_QML_ONBOARDING_SETTINGS_H
