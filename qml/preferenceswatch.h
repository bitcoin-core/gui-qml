// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_PREFERENCESWATCH_H
#define BITCOIN_QML_PREFERENCESWATCH_H

#include <QFileInfo>
#include <QDir>
#include <QFileSystemWatcher>
#include <QSettings>
#include <QTimer>
#include <functional>
#include <utility>

namespace preferences {
struct WatchPaths { QString file, directory; };
/** Native-lane facts from the same settings instance as the read. Registry
 * settings have no filesystem notification; activation refresh is portable. */
inline WatchPaths Paths(const QSettings& settings)
{
    const QFileInfo file{settings.fileName()};
    auto path = file.absolutePath();
    for (int remaining = 32; remaining > 0; --remaining) {
        if (QDir{path}.exists()) return {file.absoluteFilePath(), path};
        const auto parent = QFileInfo{path}.absolutePath();
        if (parent == path) break;
        path = parent;
    }
    return {file.absoluteFilePath(), {}};
}
/** One bounded Qt native watcher per application preference owner. Qt watcher
 * registration is a necessary GUI/native handoff; settings reads stay Native.
 * Watching the directory also observes replacement and deletion. */
class Watch final {
public:
    explicit Watch(std::function<void()> changed) : m_changed(std::move(changed))
    {
        const auto notify = [this] {
            if (m_stopped || m_queued) return;
            m_queued = true;
            QTimer::singleShot(0, &m_watcher, [this] {
                m_queued = false;
                if (m_stopped) return;
                install();
                const auto callback = m_changed;
                callback();
            });
        };
        QObject::connect(&m_watcher, &QFileSystemWatcher::fileChanged, &m_watcher, notify);
        QObject::connect(&m_watcher, &QFileSystemWatcher::directoryChanged, &m_watcher, notify);
    }
    void update(WatchPaths paths)
    {
        if (m_stopped) return;
        if (paths.file != m_paths.file || paths.directory != m_paths.directory) {
            const auto old = m_watcher.files() + m_watcher.directories();
            if (!old.isEmpty()) m_watcher.removePaths(old);
            m_paths = std::move(paths);
        }
        install();
    }
    void stop()
    {
        m_stopped = true;
        const auto old = m_watcher.files() + m_watcher.directories();
        if (!old.isEmpty()) m_watcher.removePaths(old);
    }
private:
    void install()
    {
        if (!m_paths.directory.isEmpty() && !m_watcher.directories().contains(m_paths.directory)) m_watcher.addPath(m_paths.directory);
        if (!m_paths.file.isEmpty() && !m_watcher.files().contains(m_paths.file)) m_watcher.addPath(m_paths.file);
    }
    QFileSystemWatcher m_watcher;
    WatchPaths m_paths;
    std::function<void()> m_changed;
    bool m_stopped{false}, m_queued{false};
};
} // namespace preferences
#endif // BITCOIN_QML_PREFERENCESWATCH_H
