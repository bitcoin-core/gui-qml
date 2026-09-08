// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/appmode.h>

#include <qml/models/settings_keys.h>

#include <QSettings>

namespace {
constexpr bool AdaptiveSidebarLayoutAvailable()
{
#ifdef ENABLE_TABVIEW_SHELL
    return true;
#else
    return false;
#endif
}
} // namespace

AppMode::AppMode(Mode mode, bool wallet_enabled)
    : m_mode(mode)
    , m_wallet_enabled(wallet_enabled)
    , m_adaptive_sidebar_layout_available(AdaptiveSidebarLayoutAvailable())
{
    QSettings settings;
    m_adaptive_sidebar_layout = m_adaptive_sidebar_layout_available
        && settings.value(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT, false).toBool();
}

void AppMode::setAdaptiveSidebarLayout(bool enabled)
{
    if (enabled && !m_adaptive_sidebar_layout_available) return;
    if (m_adaptive_sidebar_layout == enabled) return;

    m_adaptive_sidebar_layout = enabled;
    QSettings settings;
    settings.setValue(SettingsKeys::ADAPTIVE_SIDEBAR_LAYOUT, enabled);
    Q_EMIT adaptiveSidebarLayoutChanged();
}
