// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DESKTOPWINDOWBEHAVIORMODEL_H
#define BITCOIN_QML_MODELS_DESKTOPWINDOWBEHAVIORMODEL_H

#include <QObject>

class DesktopWindowBehaviorModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool desktopPlatform READ desktopPlatform CONSTANT)
    Q_PROPERTY(bool showTrayIcon READ showTrayIcon WRITE setShowTrayIcon NOTIFY showTrayIconChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY minimizeToTrayChanged)
    Q_PROPERTY(bool minimizeOnClose READ minimizeOnClose WRITE setMinimizeOnClose NOTIFY minimizeOnCloseChanged)

public:
    explicit DesktopWindowBehaviorModel(QObject* parent = nullptr);

    // Constructor for unit-testing: allows injecting the platform flag without
    // relying on QGuiApplication::platformName().
    explicit DesktopWindowBehaviorModel(bool desktop_platform, QObject* parent = nullptr);

    bool desktopPlatform() const;

    bool showTrayIcon() const;
    void setShowTrayIcon(bool show);

    bool minimizeToTray() const;
    void setMinimizeToTray(bool minimize);

    bool minimizeOnClose() const;
    void setMinimizeOnClose(bool minimize);

    Q_INVOKABLE bool shouldHideToTrayOnMinimize() const;
    Q_INVOKABLE bool shouldMinimizeWindowOnClose() const;

Q_SIGNALS:
    void showTrayIconChanged(bool show);
    void minimizeToTrayChanged(bool minimize);
    void minimizeOnCloseChanged(bool minimize);

private:
    // QSettings keys (kept compatible with legacy Bitcoin Qt GUI)
    static constexpr const char* KEY_HIDE_TRAY_ICON{"fHideTrayIcon"};
    static constexpr const char* KEY_MINIMIZE_TO_TRAY{"fMinimizeToTray"};
    static constexpr const char* KEY_MINIMIZE_ON_CLOSE{"fMinimizeOnClose"};

    void loadFromSettings();

    bool m_desktop_platform{false};
    bool m_show_tray_icon{true};
    bool m_minimize_to_tray{false};
    bool m_minimize_on_close{false};
};

#endif // BITCOIN_QML_MODELS_DESKTOPWINDOWBEHAVIORMODEL_H
