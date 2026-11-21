// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_STARTUPPOLICY_H
#define BITCOIN_QML_STARTUPPOLICY_H

#include <qml/models/startupdiagnosticsmodel.h>
#include <qml/models/uipreferences.h>
#include <util/translation.h>

#include <atomic>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QQmlApplicationEngine;
class QQuickWindow;
class ArgsManager;

namespace startup {
using DiagnosticLoss = std::shared_ptr<std::atomic_bool>;
enum class DiagnosticOrigin { Native, Gui };
inline constexpr size_t GUI_DIAGNOSTIC_BYTES{16384};
bool RecordWarning(const std::shared_ptr<startupdiagnostics::Collection>& warnings,
                   const bilingual_str& message, DiagnosticOrigin origin, const DiagnosticLoss& loss);
struct DiagnosticPorts {
    std::function<std::shared_ptr<startupdiagnostics::Collection>()> create;
    std::function<void(const QString&)> critical;
};
struct ErrorPresentation {
    QString summary;
    std::shared_ptr<const startupdiagnostics::Collection> details;
};
ErrorPresentation PrepareError(const bilingual_str& message, DiagnosticOrigin origin,
                               const DiagnosticPorts& ports, const DiagnosticLoss& loss);

// The owner must be declared before its QQmlApplicationEngine: diagnostics
// outlive QML root destruction and singleton callbacks.
struct ErrorDetails {
    std::unique_ptr<StartupDiagnosticsModel> owned;
    StartupDiagnosticsModel* model{nullptr};
    std::optional<QString> replacement_message;
};
ErrorDetails PrepareErrorDetails(const execution::SnapshotServices& services, const QString& summary,
    std::shared_ptr<const startupdiagnostics::Collection> full_message,
    StartupDiagnosticsModel* existing, const DiagnosticLoss& loss);
void RecordProjectionOutcome(const StartupDiagnosticsModel& details, const DiagnosticLoss& loss);
bool DeliverError(bool canceled, bool quitting, const std::function<bool()>& present);

enum class PreferencesPhase { BeforeWindow, ErrorHandoff };
UiPreferencesStore* InstallPreferences(QQmlApplicationEngine& engine,
    const execution::SnapshotServices& services, UiPreferencesNamespace identity, PreferencesPhase phase);
void FinishPreferences(UiPreferencesStore* store, QQuickWindow* window);

struct SettingsWrite { bool success{false}; std::vector<std::string> errors; };
struct Migration {
    bool settings_changed{false};
    QString error;
    // Captured Native cleanup is admitted only after a successful target write.
    std::function<SettingsWrite()> complete{};
};
enum class PreparationOutcome { Ready, ConfigurationFailed, MigrationFailed, SettingsWriteFailed, BaseInitializationFailed };
// Native-only read/capture. Unlike common::InitConfig, this does not create
// directories or read/write the dynamic settings file. Its profile identity
// follows Core's config and chain selection before those mutation effects.
struct ProfileIdentity {
    QString directory;
    QString network;
    bool operator==(const ProfileIdentity&) const = default;
};
ProfileIdentity ReadProfileIdentity(ArgsManager& args);
ProfileIdentity CurrentProfileIdentity(const ArgsManager& args);
// The original preparation ports are required. The operation runs on its Native
// startup action; errors stop its effects in order and exceptions propagate to
// that action's owned outcome. No port is called while a GUI lock is held.
struct PreparationPorts {
    std::function<bool()> configure;
    std::function<Migration()> migrate;
    std::function<SettingsWrite()> write_settings;
    std::function<void(const bilingual_str&, const std::vector<std::string>&)> error;
    std::function<void()> initialize_logging;
    std::function<void()> make_node, make_chain;
    std::function<bool()> initialize_base;
    std::function<bool()> reset_requested;
    std::function<bool()> reset_window_settings;
    std::function<void(const bilingual_str&)> warning;
    // Optional for existing synchronous consumers. The async transport path
    // binds this to its captured identity and checks it after InitConfig,
    // before migration/base initialization. This is not an atomic config-file
    // transaction: InitConfig itself can already have written settings.
    std::function<bool()> profile_matches{};
};
PreparationOutcome PrepareNode(const PreparationPorts& ports);
struct WindowSettingsPorts {
    std::function<void(const QString&)> remove;
    std::function<bool()> sync;
};
bool ResetWindowSettings(const WindowSettingsPorts& ports);
} // namespace startup
#endif // BITCOIN_QML_STARTUPPOLICY_H
