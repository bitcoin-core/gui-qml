// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/desktopwindowbehaviormodel.h>

#include <QSettings>

void DesktopWindowBehaviorModel::loadFromSettings()
{
    QSettings settings;
    // fHideTrayIcon stores the inverse of showTrayIcon (default: show = true → hide = false)
    m_show_tray_icon = !settings.value(KEY_HIDE_TRAY_ICON, false).toBool();
    // Treat persisted settings as untrusted external input: a stale or manually
    // edited QSettings could hold minimizeToTray=true while the tray is hidden,
    // a state unreachable through the UI. Clamp it and rewrite the bad value so
    // runtime can never observe showTrayIcon=false && minimizeToTray=true.
    const bool raw_minimize_to_tray = settings.value(KEY_MINIMIZE_TO_TRAY, false).toBool();
    m_minimize_to_tray = raw_minimize_to_tray && m_show_tray_icon;
    if (raw_minimize_to_tray && !m_show_tray_icon) {
        settings.setValue(KEY_MINIMIZE_TO_TRAY, false);
    }
    m_minimize_on_close = settings.value(KEY_MINIMIZE_ON_CLOSE, false).toBool();

    if (!m_desktop_platform) {
        m_show_tray_icon = false;
        m_minimize_to_tray = false;
        m_minimize_on_close = false;
    }
}

DesktopWindowBehaviorModel::DesktopWindowBehaviorModel(QObject* parent)
    : QObject(parent)
{
#ifdef Q_OS_ANDROID
    m_desktop_platform = false;
#else
    m_desktop_platform = true;
#endif
    loadFromSettings();
}

DesktopWindowBehaviorModel::DesktopWindowBehaviorModel(bool desktop_platform, QObject* parent)
    : QObject(parent)
    , m_desktop_platform(desktop_platform)
{
    loadFromSettings();
}

bool DesktopWindowBehaviorModel::desktopPlatform() const
{
    return m_desktop_platform;
}

bool DesktopWindowBehaviorModel::showTrayIcon() const
{
    return m_show_tray_icon;
}

void DesktopWindowBehaviorModel::setShowTrayIcon(bool show)
{
    if (m_show_tray_icon == show) return;

    m_show_tray_icon = show;
    QSettings settings;
    settings.setValue(KEY_HIDE_TRAY_ICON, !show);

    if (!show && m_minimize_to_tray) {
        setMinimizeToTray(false);
    }

    Q_EMIT showTrayIconChanged(m_show_tray_icon);
}

bool DesktopWindowBehaviorModel::minimizeToTray() const
{
    return m_minimize_to_tray;
}

void DesktopWindowBehaviorModel::setMinimizeToTray(bool minimize)
{
    // No-op when tray icon is disabled
    if (!m_show_tray_icon && minimize) return;
    if (m_minimize_to_tray == minimize) return;

    m_minimize_to_tray = minimize;
    QSettings settings;
    settings.setValue(KEY_MINIMIZE_TO_TRAY, minimize);
    Q_EMIT minimizeToTrayChanged(m_minimize_to_tray);
}

bool DesktopWindowBehaviorModel::minimizeOnClose() const
{
    return m_minimize_on_close;
}

void DesktopWindowBehaviorModel::setMinimizeOnClose(bool minimize)
{
    if (m_minimize_on_close == minimize) return;

    m_minimize_on_close = minimize;
    QSettings settings;
    settings.setValue(KEY_MINIMIZE_ON_CLOSE, minimize);
    Q_EMIT minimizeOnCloseChanged(m_minimize_on_close);
}

bool DesktopWindowBehaviorModel::shouldHideToTrayOnMinimize() const
{
    return m_desktop_platform && m_show_tray_icon && m_minimize_to_tray;
}

bool DesktopWindowBehaviorModel::shouldMinimizeWindowOnClose() const
{
    return m_desktop_platform && m_minimize_on_close;
}
