// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_ONBOARDING_SETTINGS_H
#define BITCOIN_QML_ONBOARDING_SETTINGS_H

#include <qml/core_settings.h>
#include <qml/settingsnamespace.h>
#include <qml/profileinspection.h>

#include <QSet>
#include <QString>
#include <QVariantMap>

#include <memory>
#include <optional>
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

struct ApplyRequest {
    DataDirSelection data_dir;
    QSet<QString> touched_settings;
    QmlCoreSettings::Values values;
    bool accept_redownload{false};
    std::optional<UiPreferencesNamespace> settings_namespace{};
};

struct ProfileRecovery;
struct ProfileResult {
    bool completed{false};
    bool replacement_written{false};
    bool directory_saved{false};
    bool cleanup_completed{false};
    bool uncertain{false};
    QString error;
    // Native-only exact original attempt; no QObject or GUI projection.
    std::shared_ptr<ProfileRecovery> recovery;
};
ProfileResult PersistProfile(ArgsManager& args, const ApplyRequest& request);
ProfileResult QueryProfile(ArgsManager& args, const ProfileResult& original);
ProfileResult CompleteProfile(ArgsManager& args, const ProfileResult& original);

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
    QmlCoreSettings::Values values;
    QVariantMap core_setting_statuses;
    int assumed_blockchain_size{0};
    int assumed_chainstate_size{0};
    ProfileSummary profile;
};

struct OnboardingStartupStatus {
    bool ok{false};
    QString error;
    QString active_data_dir;
    DataDirSource data_dir_source{DataDirSource::Default};
    bool settings_enabled{true};
    bool qml_onboarded{false};
    bool should_show_onboarding{true};
    // Read-only identity for an existing-instance probe. The network path is
    // canonical even when its final directory does not exist yet.
    QString network{}, network_data_dir{};
    bool probe_eligible{false};
};

enum class StartupReadMode { Onboarding, ProfileOnly };
bool PrepareArgs(ArgsManager& args, const std::vector<std::string>& argv, bool can_listen_ipc, std::string& error);
// ProfileOnly resolves the identity without reading dynamic settings or the
// completion marker. Its onboarding flags retain their conservative defaults.
OnboardingStartupStatus ResolveOnboardingStartupStatus(const std::vector<std::string>& argv, bool can_listen_ipc, const UiPreferencesNamespace* identity = nullptr,
    StartupReadMode mode = StartupReadMode::Onboarding);
PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const DataDirSelection& data_dir, const ApplyRequest* intent = nullptr,
    const profileinspection::Port& inspect = {}, const profileinspection::Cancellation& cancelled = {});
PreviewResult Preview(const std::vector<std::string>& argv, bool can_listen_ipc, const QString& data_dir,
    const profileinspection::Port& inspect = {}, const profileinspection::Cancellation& cancelled = {});
bool SetQmlOnboarded(ArgsManager& args,bool completed,QString* error=nullptr);
bool MarkQmlOnboarded(ArgsManager& args, QString* error = nullptr);
bool ApplyToArgs(ArgsManager& args, const DataDirSelection& data_dir, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, QString* error = nullptr, bool accept_redownload = false, bool mark_completed = true, ProfileResult* receipt = nullptr, const UiPreferencesNamespace* identity = nullptr);
bool ApplyToArgs(ArgsManager& args, const QString& data_dir, const QSet<QString>& touched_settings, const QmlCoreSettings::Values& values, QString* error = nullptr, bool accept_redownload = false, bool mark_completed = true, ProfileResult* receipt = nullptr, const UiPreferencesNamespace* identity = nullptr);

} // namespace QmlOnboardingSettings

#endif // BITCOIN_QML_ONBOARDING_SETTINGS_H
