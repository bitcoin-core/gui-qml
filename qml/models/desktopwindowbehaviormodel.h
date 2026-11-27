// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DESKTOPWINDOWBEHAVIORMODEL_H
#define BITCOIN_QML_MODELS_DESKTOPWINDOWBEHAVIORMODEL_H

#include <qml/guipresentation.h>
#include <qml/preferenceswatch.h>
#include <qml/snapshotquery.h>

#include <QObject>

#include <map>
#include <utility>

namespace execution { class Executor; class SerialQueue; }

class DesktopWindowBehaviorModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool desktopPlatform READ desktopPlatform CONSTANT)
    Q_PROPERTY(bool showTrayIcon READ showTrayIcon WRITE setShowTrayIcon NOTIFY showTrayIconChanged)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray NOTIFY minimizeToTrayChanged)
    Q_PROPERTY(bool minimizeOnClose READ minimizeOnClose WRITE setMinimizeOnClose NOTIFY minimizeOnCloseChanged)
    Q_PROPERTY(bool settingsReady READ settingsReady NOTIFY settingsStateChanged)
    Q_PROPERTY(bool persistencePending READ persistencePending NOTIFY settingsStateChanged)
    Q_PROPERTY(QString persistenceError READ persistenceError NOTIFY settingsStateChanged)

public:
    explicit DesktopWindowBehaviorModel(QObject* parent = nullptr);

    // Constructor for unit-testing: allows injecting the platform flag without
    // relying on QGuiApplication::platformName().
    explicit DesktopWindowBehaviorModel(bool desktop_platform, QObject* parent = nullptr);
    struct Values { bool show_tray{true}, minimize_tray{false}, minimize_close{false}; };
    struct Store {
        std::function<Values()> read;
        std::function<void(Values)> write;
        std::function<Values(Values, unsigned)> write_fields;
        std::function<preferences::WatchPaths()> watch_paths;
        Store(std::function<Values()> read = {}, std::function<void(Values)> write = {},
              std::function<Values(Values, unsigned)> write_fields = {},
              std::function<preferences::WatchPaths()> watch_paths = {})
            : read(std::move(read)), write(std::move(write)), write_fields(std::move(write_fields)), watch_paths(std::move(watch_paths)) {}
    };
    DesktopWindowBehaviorModel(bool desktop_platform, presentation::Post post, Store store = {}, QObject* parent = nullptr,
                               std::shared_ptr<execution::SerialQueue> utility = {});
    ~DesktopWindowBehaviorModel() override;
    void close();
    Q_INVOKABLE void retryPersistence();
    Q_INVOKABLE void refreshSettings();
    bool settingsReady() const { return m_ready; }
    bool persistencePending() const { return !m_operations.empty(); }
    QString persistenceError() const { return m_error; }

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
    void settingsStateChanged();

private:
    // QSettings keys (kept compatible with legacy Bitcoin Qt GUI)
    static constexpr const char* KEY_HIDE_TRAY_ICON{"fHideTrayIcon"};
    static constexpr const char* KEY_MINIMIZE_TO_TRAY{"fMinimizeToTray"};
    static constexpr const char* KEY_MINIMIZE_ON_CLOSE{"fMinimizeOnClose"};

    bool submit(Values values, bool load, unsigned fields = 7);
    void finishRefresh();
    void publish(quint64 id, bool load);
    void apply(Values values);
    presentation::Post m_post;
    Store m_store;
    std::shared_ptr<execution::Executor> m_owned_executor;
    std::shared_ptr<execution::SerialQueue> m_utility;
    struct Result { Values values; preferences::WatchPaths watch; };
    struct Pending { bool load; std::shared_ptr<execution::OwnedCommand<Result>> command; };
    std::map<quint64, Pending> m_operations;
    quint64 m_next_id{0}, m_state_id{0};
    QString m_error;
    bool m_ready{false}, m_stopping{false}, m_refresh_requested{false};
    preferences::Watch m_watch{[this] { refreshSettings(); }};

    bool m_desktop_platform{false};
    bool m_show_tray_icon{true};
    bool m_minimize_to_tray{false};
    bool m_minimize_on_close{false};
};

#endif // BITCOIN_QML_MODELS_DESKTOPWINDOWBEHAVIORMODEL_H
