// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/thememanager.h>

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
}

void ThemeManager::setSystemBaseColor(QColor base_color)
{
    // Convert QColor's 8-bit RGB values to linear RGB values
    double linearR = base_color.redF();
    double linearG = base_color.greenF();
    double linearB = base_color.blueF();

    // Constants for the luminance formula
    const double RED_FACTOR = 0.2126;
    const double GREEN_FACTOR = 0.7152;
    const double BLUE_FACTOR = 0.0722;

    // Calculate luminance using the formula
    double luminance = RED_FACTOR * linearR + GREEN_FACTOR * linearG + BLUE_FACTOR * linearB;

    if (luminance <= 0.5) {
        m_dark_mode = true;
    } else {
        m_dark_mode = false;
    }

    m_system_base_color = base_color;

    #ifdef Q_OS_MAC
    setSystemThemeAvailable(true);
    #endif

    Q_EMIT darkModeChanged();
}

void ThemeManager::setSystemThemeAvailable(bool available)
{
    if (m_system_theme_available != available) {
        m_system_theme_available = available;
        Q_EMIT systemThemeAvailableChanged();
    }
}