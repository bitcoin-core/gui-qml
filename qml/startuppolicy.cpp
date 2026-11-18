// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/startuppolicy.h>

#include <chainparamsbase.h>
#include <common/args.h>
#include <util/fs.h>

#include <QEventLoop>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace startup {
namespace {
void MarkLost(const DiagnosticLoss& loss) { if (loss) *loss = true; }
bool Bounded(DiagnosticOrigin origin, const std::string& text)
{
    return origin == DiagnosticOrigin::Gui && text.size() > GUI_DIAGNOSTIC_BYTES;
}
QString Capture(const std::string& text, DiagnosticOrigin origin)
{
    return origin == DiagnosticOrigin::Gui
        ? QString::fromUtf8(text.data(), static_cast<qsizetype>(std::min(text.size(), GUI_DIAGNOSTIC_BYTES)))
        : QString::fromStdString(text);
}
}
bool RecordWarning(const std::shared_ptr<startupdiagnostics::Collection>& warnings,
                   const bilingual_str& message, DiagnosticOrigin origin, const DiagnosticLoss& loss)
{
    if (!warnings) { MarkLost(loss); return false; }
    auto warning = Capture(message.translated, origin);
    if (Bounded(origin, message.translated)) {
        MarkLost(loss);
        warning += QStringLiteral(" ") + QObject::tr("[complete warning rejected: Core startup work ran on GUI]");
    }
    const bool accepted = warnings->append(std::move(warning), true);
    if (!accepted) MarkLost(loss);
    return accepted;
}
ErrorPresentation PrepareError(const bilingual_str& message, DiagnosticOrigin origin,
                               const DiagnosticPorts& ports, const DiagnosticLoss& loss)
{
    if (!ports.create || !ports.critical) throw std::invalid_argument{"Missing startup diagnostic port"};
    auto details = ports.create();
    const auto prepared = Capture(message.translated, origin);
    if (details && !details->append(prepared, false)) details.reset();
    if (!details) MarkLost(loss);
    else details->seal();
    if (Bounded(origin, message.translated)) MarkLost(loss);
    ports.critical(Capture(message.original, origin));
    const QString summary = prepared.left(static_cast<qsizetype>(GUI_DIAGNOSTIC_BYTES))
        + (Bounded(origin, message.translated)
            ? QStringLiteral(" ") + QObject::tr("[complete error rejected: Core startup work ran on GUI]") : QString{})
        + (!details ? QStringLiteral(" ") + QObject::tr("[complete error unavailable: diagnostic capacity exhausted]") : QString{});
    return {summary, std::move(details)};
}
ErrorDetails PrepareErrorDetails(const execution::SnapshotServices& services, const QString& summary,
    std::shared_ptr<const startupdiagnostics::Collection> full_message,
    StartupDiagnosticsModel* existing, const DiagnosticLoss& loss)
{
    ErrorDetails result;
    result.model = existing;
    if (!existing) {
        result.owned = std::make_unique<StartupDiagnosticsModel>(services);
        result.model = result.owned.get();
        if (full_message) result.model->adopt(std::move(full_message));
        if (!result.model->project(false, summary, {}, {}, false)) {
            MarkLost(loss);
            result.replacement_message = result.model->error();
        }
    }
    return result;
}
void RecordProjectionOutcome(const StartupDiagnosticsModel& details, const DiagnosticLoss& loss)
{
    if (loss && !details.error().isEmpty()) *loss = true;
}
bool DeliverError(bool canceled, bool quitting, const std::function<bool()>& present)
{
    if (canceled || quitting) return false;
    return present();
}
UiPreferencesStore* InstallPreferences(QQmlApplicationEngine& engine,
    const execution::SnapshotServices& services, UiPreferencesNamespace identity, PreferencesPhase phase)
{
    auto* store = new UiPreferencesStore{services, std::move(identity), &engine};
    engine.rootContext()->setContextProperty("uiPreferencesStore", store);
    QEventLoop ready;
    QObject::connect(store, &UiPreferencesStore::stateChanged, &ready, &QEventLoop::quit);
    store->start();
    // Error producers may already be awaiting this GUI handoff on Native.
    while (phase == PreferencesPhase::BeforeWindow && !store->ready() && store->error().isEmpty()) ready.exec();
    return store;
}
void FinishPreferences(UiPreferencesStore* store, QQuickWindow* window)
{
    if (!store) return;
    if (window) window->contentItem()->setEnabled(false);
    store->close();
    QEventLoop phase;
    bool drained{false};
    store->whenDrained([&] { drained = true; phase.quit(); });
    while (!drained) phase.exec();
}
PreparationOutcome PrepareNode(const PreparationPorts& ports)
{
    if (!ports.configure || !ports.migrate || !ports.write_settings || !ports.error
        || !ports.initialize_logging || !ports.make_node || !ports.make_chain || !ports.initialize_base
        || !ports.reset_requested || !ports.reset_window_settings || !ports.warning)
        throw std::invalid_argument{"Missing node preparation port"};
    if (!ports.configure()) return PreparationOutcome::ConfigurationFailed;
    if (ports.profile_matches && !ports.profile_matches()) {
        ports.error(Untranslated("The effective startup profile changed after desktop request admission. Restart with the intended data directory and network."), {});
        return PreparationOutcome::ConfigurationFailed;
    }
    const auto migration = ports.migrate();
    if (!migration.error.isEmpty()) {
        ports.error(Untranslated(migration.error.toStdString()), {});
        return PreparationOutcome::MigrationFailed;
    }
    if (migration.settings_changed) {
        const auto written = ports.write_settings();
        if (!written.success) {
            ports.error(_("Settings file could not be written"), written.errors);
            return PreparationOutcome::SettingsWriteFailed;
        }
    }
    if (migration.complete) {
        const auto completed = migration.complete();
        if (!completed.success) {
            ports.error(_("Legacy settings migration could not be completed"), completed.errors);
            return PreparationOutcome::MigrationFailed;
        }
    }
    ports.initialize_logging();
    ports.make_node();
    ports.make_chain();
    if (!ports.initialize_base()) return PreparationOutcome::BaseInitializationFailed;
    if (ports.reset_requested() && !ports.reset_window_settings())
        ports.warning(Untranslated(QObject::tr("Unable to save window settings after reset.").toStdString()));
    return PreparationOutcome::Ready;
}
ProfileIdentity CurrentProfileIdentity(const ArgsManager& args)
{
    // Resolve an existing symlink prefix without creating a missing network
    // directory. All filesystem work stays in the caller's Native effect.
    return {QString::fromStdString(fs::PathToString(fs::weakly_canonical(args.GetDataDirNet()))),
        QString::fromStdString(args.GetChainTypeString())};
}
ProfileIdentity ReadProfileIdentity(ArgsManager& args)
{
    if (!CheckDataDirOption(args)) throw std::runtime_error{"The selected data directory does not exist."};
    std::string error;
    if (!args.ReadConfigFiles(error, true)) throw std::runtime_error{error};
    // Only the public base-chain/path identity is needed here. Full node
    // chain-parameter construction remains in Primary-only InitConfig; it
    // must not read unrelated global arguments for an independent resolver.
    // Malformed rw settings remain for that original configure/error policy.
    SelectBaseParams(args.GetChainType());
    return CurrentProfileIdentity(args);
}
bool ResetWindowSettings(const WindowSettingsPorts& ports)
{
    if (!ports.remove || !ports.sync) throw std::invalid_argument{"Missing window settings port"};
    for (const auto* key : {"fHideTrayIcon", "fMinimizeToTray", "fMinimizeOnClose"}) ports.remove(QString::fromLatin1(key));
    return ports.sync();
}
} // namespace startup
