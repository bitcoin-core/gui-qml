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
    m_minimize_to_tray = settings.value(KEY_MINIMIZE_TO_TRAY, false).toBool();
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

    // Cascade: cannot minimize to tray or on close without a tray icon
    if (!show && m_minimize_to_tray) {
        setMinimizeToTray(false);
    }
    if (!show && m_minimize_on_close) {
        setMinimizeOnClose(false);
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
    if (!m_show_tray_icon && minimize) return;
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
