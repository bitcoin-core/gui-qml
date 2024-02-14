// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_THEMEMANAGER_H
#define BITCOIN_QML_THEMEMANAGER_H

#include <QObject>
#include <QColor>
#include <QProcess>


class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool darkMode READ darkMode NOTIFY darkModeChanged)
    Q_PROPERTY(QColor systemBaseColor READ systemBaseColor WRITE setSystemBaseColor)
    Q_PROPERTY(bool systemThemeAvailable READ systemThemeAvailable WRITE setSystemThemeAvailable NOTIFY systemThemeAvailableChanged)

public:
    explicit ThemeManager(QObject* parent = nullptr);

    bool darkMode() const { return m_dark_mode; };
    QColor systemBaseColor() const { return m_system_base_color; };
    bool systemThemeAvailable() const { return m_system_theme_available; };

public Q_SLOTS:
    void setSystemBaseColor(QColor base_color);
    void setSystemThemeAvailable(bool available);

Q_SIGNALS:
    void darkModeChanged();
    void systemThemeAvailableChanged();

private:
    bool m_dark_mode{ true };
    QColor m_system_base_color;
    bool m_system_theme_available{ false };
};

#endif // BITCOIN_QML_THEMEMANAGER_H