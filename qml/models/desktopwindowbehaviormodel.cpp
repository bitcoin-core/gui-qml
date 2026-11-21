// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/desktopwindowbehaviormodel.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/settingsnamespace.h>

#include <QPointer>
#include <QSettings>
#include <stdexcept>
#include <array>

namespace {
constexpr bool DesktopPlatform()
{
#ifdef Q_OS_ANDROID
    return false;
#else
    return true;
#endif
}
}

DesktopWindowBehaviorModel::DesktopWindowBehaviorModel(QObject* parent)
    : DesktopWindowBehaviorModel(DesktopPlatform(), {}, {}, parent) {}
DesktopWindowBehaviorModel::DesktopWindowBehaviorModel(bool desktop_platform, QObject* parent)
    : DesktopWindowBehaviorModel(desktop_platform, {}, {}, parent) {}
DesktopWindowBehaviorModel::DesktopWindowBehaviorModel(bool desktop_platform, execution::SnapshotServices services, Store store, QObject* parent)
    : QObject(parent), m_services(std::move(services)), m_store(std::move(store)), m_desktop_platform(desktop_platform)
{
    if (!m_services.scheduler) {
        m_executor = std::make_unique<BackendExecutor>(execution::Options{});
        m_executor->setObjectName(QStringLiteral("desktop-preferences"));
        m_services.scheduler = m_executor->scheduler();
    }
    if (!m_services.post) m_services.post = presentation::QueuedPost();
    m_key = m_services.scheduler->allocateKey();
    const bool public_store = !m_store.read && !m_store.write && !m_store.write_fields;
    const auto identity=UiPreferencesNamespace::Capture();
    const auto open=[identity]{return OpenPreferences(identity);};
    if (!m_store.read) m_store.read = [open] {
        const auto owner=open();QSettings& settings=*owner;
        settings.sync();
        Values values{!settings.value(KEY_HIDE_TRAY_ICON, false).toBool(),
            settings.value(KEY_MINIMIZE_TO_TRAY, false).toBool(), settings.value(KEY_MINIMIZE_ON_CLOSE, false).toBool()};
        if (!values.show_tray && values.minimize_tray) {
            values.minimize_tray = false;
            settings.setValue(KEY_MINIMIZE_TO_TRAY, false);
            settings.sync();
        }
        if (settings.status() != QSettings::NoError) throw std::runtime_error("Desktop preferences could not be read");
        return values;
    };
    if (!m_store.write && !m_store.write_fields) m_store.write_fields = [open](Values values, unsigned fields) {
        const auto owner=open();QSettings& settings=*owner;
        settings.sync();
        if (settings.status() != QSettings::NoError) throw std::runtime_error("Desktop preferences could not be read before saving");
        if (fields & 1) settings.setValue(KEY_HIDE_TRAY_ICON, !values.show_tray);
        if (fields & 2) settings.setValue(KEY_MINIMIZE_TO_TRAY, values.minimize_tray);
        if (fields & 4) settings.setValue(KEY_MINIMIZE_ON_CLOSE, values.minimize_close);
        settings.sync();
        if (settings.status() != QSettings::NoError) throw std::runtime_error("Desktop preferences could not be saved");
        return Values{!settings.value(KEY_HIDE_TRAY_ICON, false).toBool(),
            settings.value(KEY_MINIMIZE_TO_TRAY, false).toBool(), settings.value(KEY_MINIMIZE_ON_CLOSE, false).toBool()};
    };
    if (!m_store.watch_paths) m_store.watch_paths = [public_store,open] { if (!public_store) return preferences::WatchPaths{}; const auto settings=open(); return preferences::Paths(*settings); };
    if (!desktop_platform) apply({false, false, false});
    submit({}, true);
}
DesktopWindowBehaviorModel::~DesktopWindowBehaviorModel() { close(); }
void DesktopWindowBehaviorModel::retryPersistence()
{
    if (m_stopping) return;
    // A failed GUI transport does not erase an accepted write's actual result.
    // Recover bounded cached outcomes first; successful writes are never replayed.
    std::array<std::pair<quint64, bool>, 128> ready{};
    size_t count{0};
    for (const auto& [id, operation] : m_operations) if (operation.command->result()) ready[count++] = {id, operation.load};
    const QPointer<DesktopWindowBehaviorModel> guard{this};
    for (size_t i = 0; i < count; ++i) {
        publish(ready[i].first, ready[i].second);
        if (!guard || m_stopping) return;
    }
    if (!m_operations.empty() || (count > 0 && m_error.isEmpty())) return;
    refreshSettings();
}
void DesktopWindowBehaviorModel::refreshSettings()
{
    if (m_stopping) return;
    m_refresh_requested = true;
    finishRefresh();
}
void DesktopWindowBehaviorModel::finishRefresh()
{
    if (m_stopping || !m_operations.empty() || !m_refresh_requested) return;
    m_refresh_requested = false;
    submit({}, true);
}
void DesktopWindowBehaviorModel::close()
{
    if (std::exchange(m_stopping, true)) return;
    m_watch.stop();
    // Accepted writes own their captured values and finish independently.
    if (m_executor) m_executor->shutdown();
}
bool DesktopWindowBehaviorModel::submit(Values values, bool load, unsigned fields)
{
    if (m_stopping) return false;
    const auto id = ++m_next_id;
    m_state_id = id;
    if (m_operations.size() >= 128) {
        m_error = tr("Desktop preferences are busy. Try again after saving finishes.");
        Q_EMIT settingsStateChanged(); return false;
    }
    const QPointer<DesktopWindowBehaviorModel> observer{this};
    auto operation = std::make_shared<execution::OwnedCommand<Result>>(m_services.scheduler,
        [post = m_services.post, observer, id, load] {
            post([observer, id, load] { if (observer && !observer->m_stopping) observer->publish(id, load); });
        });
    const auto admitted = operation->start([store = m_store, values, load, fields] {
        const auto watch = store.watch_paths();
        Values actual;
        if (load) actual = store.read();
        else if (store.write_fields) actual = store.write_fields(values, fields);
        else { store.write(values); actual = values; }
        return Result{actual, watch};
    }, {execution::Lane::Core, m_key, execution::WorkClass::Action});
    if (!admitted) {
        m_error = tr("Desktop preferences could not be queued.");
        Q_EMIT settingsStateChanged();
        return false;
    }
    m_operations.emplace(id, Pending{load, std::move(operation)});
    m_error.clear();
    const QPointer<DesktopWindowBehaviorModel> guard{this};
    if (!load) apply(values);
    if (guard) Q_EMIT settingsStateChanged();
    return true;
}
void DesktopWindowBehaviorModel::publish(quint64 id, bool load)
{
    const auto found = m_operations.find(id);
    if (found == m_operations.end()) return;
    const auto result = found->second.command->result();
    if (!result) return;
    m_operations.erase(found);
    const QPointer<DesktopWindowBehaviorModel> guard{this};
    if (id == m_state_id) {
        if (result->error || !result->value) m_error = load ? tr("Desktop preferences could not be read.") : tr("Desktop preferences could not be saved.");
        else {
            m_error.clear();
            m_watch.update(result->value->watch);
            if (load) m_ready = true;
            apply(m_desktop_platform ? result->value->values : Values{false, false, false});
        }
    }
    if (guard) Q_EMIT settingsStateChanged();
    if (guard && !m_stopping) finishRefresh();
}
void DesktopWindowBehaviorModel::apply(Values values)
{
    values.minimize_tray = values.minimize_tray && values.show_tray;
    const auto revision = m_state_id;
    const QPointer<DesktopWindowBehaviorModel> guard{this};
    const bool show_changed = m_show_tray_icon != values.show_tray;
    const bool tray_changed = m_minimize_to_tray != values.minimize_tray;
    const bool close_changed = m_minimize_on_close != values.minimize_close;
    m_show_tray_icon = values.show_tray;
    m_minimize_to_tray = values.minimize_tray;
    m_minimize_on_close = values.minimize_close;
    if (tray_changed) Q_EMIT minimizeToTrayChanged(m_minimize_to_tray);
    if (!guard || m_stopping || revision != m_state_id) return;
    if (show_changed) Q_EMIT showTrayIconChanged(m_show_tray_icon);
    if (!guard || m_stopping || revision != m_state_id) return;
    if (close_changed) Q_EMIT minimizeOnCloseChanged(m_minimize_on_close);
}
bool DesktopWindowBehaviorModel::desktopPlatform() const { return m_desktop_platform; }
bool DesktopWindowBehaviorModel::showTrayIcon() const { return m_show_tray_icon; }
bool DesktopWindowBehaviorModel::minimizeToTray() const { return m_minimize_to_tray; }
bool DesktopWindowBehaviorModel::minimizeOnClose() const { return m_minimize_on_close; }
void DesktopWindowBehaviorModel::setShowTrayIcon(bool show)
{
    if (!m_ready || !m_desktop_platform || m_show_tray_icon == show) return;
    submit({show, show && m_minimize_to_tray, m_minimize_on_close}, false, show ? 1 : 3);
}
void DesktopWindowBehaviorModel::setMinimizeToTray(bool minimize)
{
    if (!m_ready || !m_desktop_platform || (!m_show_tray_icon && minimize) || m_minimize_to_tray == minimize) return;
    submit({m_show_tray_icon, minimize, m_minimize_on_close}, false, 2);
}
void DesktopWindowBehaviorModel::setMinimizeOnClose(bool minimize)
{
    if (!m_ready || !m_desktop_platform || m_minimize_on_close == minimize) return;
    submit({m_show_tray_icon, m_minimize_to_tray, minimize}, false, 4);
}
bool DesktopWindowBehaviorModel::shouldHideToTrayOnMinimize() const { return m_desktop_platform && m_show_tray_icon && m_minimize_to_tray; }
bool DesktopWindowBehaviorModel::shouldMinimizeWindowOnClose() const { return m_desktop_platform && m_minimize_on_close; }
