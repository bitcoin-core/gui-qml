// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/debuglogmodel.h>

#include <util/threadnames.h>

#include <algorithm>
#include <utility>

#include <QDateTime>
#include <QDesktopServices>
#include <QFileInfo>
#include <QMetaObject>
#include <QObject>
#include <QThread>
#include <QTimer>
#include <QUrl>

DebugLogModel::DebugLogModel(const fs::path& log_path, QObject* parent)
    : QAbstractListModel(parent)
    , m_log_path(log_path)
{
    m_reader = new QObject;
    m_reader_thread = new QThread(this);
    m_reader->moveToThread(m_reader_thread);
    connect(m_reader_thread, &QThread::finished, m_reader, &QObject::deleteLater);
    m_reader_thread->start();
    QTimer::singleShot(0, m_reader, [] {
        util::ThreadRename("qml-debuglog");
    });

    m_debounce.setSingleShot(true);
    m_debounce.setInterval(500);
    connect(&m_debounce, &QTimer::timeout, this, [this]() { refresh(); });

    connectFileWatcher();
}

DebugLogModel::~DebugLogModel()
{
    stop();
    if (m_reader_thread) {
        m_reader_thread->wait();
    }
}

int DebugLogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_display_lines.size();
}

QVariant DebugLogModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_display_lines.size())
        return {};

    const LogLine& line = m_display_lines.at(index.row());
    switch (role) {
    // The number is derived from the model row. Prepending new records no
    // longer requires copying and renumbering every stored LogLine.
    case LineNumberRole:   return QString::number(index.row() + 1);
    case ContentRole:      return line.content;
    case RelativeTimeRole: return line.relativeTime;
    case CommandRole:      return line.command;
    case MessageRole:      return line.message;
    case DateLabelRole:    return line.relativeTime;
    case SeverityRole:     return line.severity;
    }
    return {};
}

QHash<int, QByteArray> DebugLogModel::roleNames() const
{
    return {
        {LineNumberRole,   "lineNumber"},
        {ContentRole,      "content"},
        {RelativeTimeRole, "relativeTime"},
        {CommandRole,      "command"},
        {MessageRole,      "message"},
        {DateLabelRole,    "dateLabel"},
        {SeverityRole,     "severity"},
    };
}

void DebugLogModel::setLoadLimit(int limit)
{
    limit = std::clamp(limit, 1, kMaxLoadLimit);
    if (m_load_limit == limit) return;
    const int previous_limit = m_load_limit;
    m_load_limit = limit;
    Q_EMIT loadLimitChanged();

    if (m_all_lines.size() > m_load_limit) {
        QList<LogLine> retained = m_all_lines.first(m_load_limit);
        applyLines(std::move(retained), /*force_reset=*/false);
        m_loaded_limit = std::min(m_loaded_limit, m_load_limit);
        const bool has_more = m_load_limit < kMaxLoadLimit;
        if (m_has_more_lines != has_more) {
            m_has_more_lines = has_more;
            Q_EMIT hasMoreLinesChanged();
        }
    }

    if (m_load_limit > previous_limit) {
        if (m_active) {
            refresh(/*full_load=*/true);
        }
    }
}

void DebugLogModel::setActive(bool active)
{
    if (m_active == active || m_stopping) return;

    m_active = active;
    ++m_activation_generation;
    Q_EMIT activeChanged();

    if (m_active) {
        watchLogPath();
        // Retained rows can paint immediately on reactivation. Catch up from
        // the saved file offset unless the retained tail is too narrow.
        refresh(/*full_load=*/m_loaded_limit < m_load_limit);
        return;
    }

    m_debounce.stop();
    const auto watched_files = m_watcher.files();
    if (!watched_files.isEmpty()) {
        m_watcher.removePaths(watched_files);
    }
    const auto watched_directories = m_watcher.directories();
    if (!watched_directories.isEmpty()) {
        m_watcher.removePaths(watched_directories);
    }
    m_refresh_pending = false;
    m_pending_full_load = false;
}

void DebugLogModel::setFilter(const QString& filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    Q_EMIT filterChanged();
    // Cached rows remain observable while inactive, so keep the display
    // projection in sync even when the page is currently unloaded.
    buildDisplayLines(/*force_reset=*/true);
}

void DebugLogModel::refresh(bool full_load)
{
    if (!m_active || m_stopping) return;

    // Single-read-in-flight guard. If a read is already running, fold this
    // request into a trailing re-run rather than piling another job onto the
    // worker thread. A burst of watcher events on a noisy node therefore
    // collapses to at most two reads: the one in flight, plus one trailer
    // that sees the final file state.
    if (m_read_in_flight) {
        m_refresh_pending = true;
        // Upgrade a pending partial refresh to a full load if any caller
        // requested full; otherwise leave the flag alone.
        if (full_load) m_pending_full_load = true;
        return;
    }
    m_read_in_flight = true;

    const fs::path path = m_log_path;
    const int load_limit = m_load_limit;
    const qint64 previous_file_size = m_file_size;
    const QByteArray previous_partial = m_trailing_partial;
    const bool previous_discarding_oversized_line = m_discarding_oversized_line;
    const QByteArray previous_anchor = m_file_anchor;
    const quint64 activation_generation = m_activation_generation;

    if (!m_reader || !m_reader_thread || !m_reader_thread->isRunning()) {
        m_read_in_flight = false;
        return;
    }

    const bool queued = QMetaObject::invokeMethod(m_reader,
        [this,
         path,
         load_limit,
         full_load,
         previous_file_size,
         previous_partial,
         previous_discarding_oversized_line,
         previous_anchor,
         activation_generation]() mutable {
            if (m_read_cancelled.load(std::memory_order_relaxed)) return;

            ReadResult result = DebugLogReader::ReadAndFilter(path,
                                              load_limit,
                                              full_load,
                                              previous_file_size,
                                              previous_partial,
                                              previous_discarding_oversized_line,
                                              previous_anchor,
                                              m_read_cancelled);
            if (m_read_cancelled.load(std::memory_order_relaxed)) return;

            QMetaObject::invokeMethod(this,
                [this,
                 result = std::move(result),
                 full_load,
                 activation_generation]() mutable {
                    if (m_stopping || m_read_cancelled.load(std::memory_order_relaxed)) return;
                    if (!m_active || activation_generation != m_activation_generation) {
                        m_read_in_flight = false;
                        if (m_active && m_refresh_pending) {
                            m_refresh_pending = false;
                            const bool do_full = m_pending_full_load;
                            m_pending_full_load = false;
                            refresh(do_full);
                        }
                        return;
                    }
                    onReadCompleted(result, full_load);
                },
                Qt::QueuedConnection);
        },
        Qt::QueuedConnection);
    if (!queued) {
        m_read_in_flight = false;
    }
}

void DebugLogModel::loadMore()
{
    if (m_load_limit >= kMaxLoadLimit) {
        if (m_has_more_lines) {
            m_has_more_lines = false;
            Q_EMIT hasMoreLinesChanged();
        }
        return;
    }
    m_load_limit = std::min(m_load_limit + 1000, kMaxLoadLimit);
    Q_EMIT loadLimitChanged();
    refresh(true);
}

bool DebugLogModel::openLogFile()
{
    const QString path_str = QString::fromStdString(m_log_path.utf8string());
    if (!fs::exists(m_log_path)) {
        m_open_error = tr("Debug log file not found: %1").arg(path_str);
        Q_EMIT openErrorChanged();
        return false;
    }
    const bool ok = QDesktopServices::openUrl(QUrl::fromLocalFile(path_str));
    if (!ok) {
        m_open_error = tr("Could not open debug log file. "
                          "No application is associated with this file type.");
        Q_EMIT openErrorChanged();
        return false;
    }
    if (!m_open_error.isEmpty()) {
        m_open_error.clear();
        Q_EMIT openErrorChanged();
    }
    return true;
}

void DebugLogModel::updateRelativeTimes()
{
    if (!m_active || (m_all_lines.isEmpty() && m_display_lines.isEmpty())) return;
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();

    for (LogLine& line : m_all_lines) {
        if (line.timestamp_ms >= 0)
            line.relativeTime = DebugLogReader::RelativeTimeLabelStatic(line.timestamp_ms, now_ms);
    }

    int first_changed = -1;
    int last_changed = -1;
    for (int i = 0; i < m_display_lines.size(); ++i) {
        LogLine& line = m_display_lines[i];
        if (line.timestamp_ms < 0) continue;
        const QString next_label = DebugLogReader::RelativeTimeLabelStatic(line.timestamp_ms, now_ms);
        if (line.relativeTime == next_label) continue;
        line.relativeTime = next_label;
        if (first_changed < 0) first_changed = i;
        last_changed = i;
    }

    if (first_changed >= 0) {
        Q_EMIT dataChanged(index(first_changed, 0), index(last_changed, 0),
                           {RelativeTimeRole, DateLabelRole});
    }
}

void DebugLogModel::stop()
{
    if (m_stopping) return;

    m_stopping = true;
    m_read_cancelled.store(true, std::memory_order_relaxed);
    m_debounce.stop();

    const auto watched_files = m_watcher.files();
    if (!watched_files.isEmpty()) {
        m_watcher.removePaths(watched_files);
    }
    const auto watched_directories = m_watcher.directories();
    if (!watched_directories.isEmpty()) {
        m_watcher.removePaths(watched_directories);
    }

    m_refresh_pending = false;
    m_pending_full_load = false;
    m_read_in_flight = false;

    if (m_reader_thread) {
        m_reader_thread->quit();
        if (QThread::currentThread() != m_reader_thread) {
            m_reader_thread->wait();
        }
    }
}

// ── Private ──────────────────────────────────────────────────────────────────

void DebugLogModel::onReadCompleted(const ReadResult& result,
                                   bool full_load)
{
    Q_UNUSED(full_load);
    if (!m_active || m_stopping) return;

    // Propagate open-error state from the background read.
    if (!result.file_opened) {
        if (m_open_error != result.error_message) {
            m_open_error = result.error_message;
            Q_EMIT openErrorChanged();
        }
        watchLogPath();
        m_read_in_flight = false;
        // Trailing refresh re-arm still honoured so a recovered file reloads.
        if (m_refresh_pending) {
            m_refresh_pending = false;
            const bool do_full = m_pending_full_load;
            m_pending_full_load = false;
            refresh(do_full);
        }
        return;
    }
    if (!m_open_error.isEmpty()) {
        m_open_error.clear();
        Q_EMIT openErrorChanged();
    }
    // The watcher may have failed to register the path at construction time
    // if the log file did not exist yet. Re-add after a successful read so
    // auto-refresh works from here on.
    watchLogPath();

    m_file_size = result.file_size;
    m_trailing_partial = result.trailing_partial;
    m_discarding_oversized_line = result.discarding_oversized_line;
    m_file_anchor = result.file_anchor;

    int newly_completed{0};
    bool next_has_more{m_has_more_lines};
    bool force_reset{result.continuity_lost};

    if (result.full_snapshot) {
        QList<LogLine> next_lines = result.lines;
        if (next_lines.size() > m_load_limit) next_lines.resize(m_load_limit);
        next_has_more = (result.has_more_lines || result.lines.size() > m_load_limit)
            && m_load_limit < kMaxLoadLimit;
        force_reset = force_reset || m_all_lines.isEmpty();
        applyLines(std::move(next_lines), force_reset);
        // A result captured before a rapid limit change only satisfies the
        // smaller of its request and the current retained capacity.
        m_loaded_limit = std::min(result.snapshot_limit, m_load_limit);
    } else {
        newly_completed = result.lines.size();
        const bool pruned = applyDelta(result.lines);
        next_has_more = (m_has_more_lines || result.has_more_lines || pruned)
            && m_load_limit < kMaxLoadLimit;
    }

    if (m_has_more_lines != next_has_more) {
        m_has_more_lines = next_has_more;
        Q_EMIT hasMoreLinesChanged();
    }
    if (newly_completed > 0) {
        Q_EMIT newLinesAdded(std::min(newly_completed, m_load_limit));
    }

    const bool needs_wider_tail = m_loaded_limit < m_load_limit;
    const bool run_trailing_refresh = m_refresh_pending || needs_wider_tail;
    const bool do_full = m_pending_full_load || needs_wider_tail;
    m_refresh_pending = false;
    m_pending_full_load = false;
    m_read_in_flight = false;
    if (!m_stopping && run_trailing_refresh) refresh(do_full);
}

void DebugLogModel::connectFileWatcher()
{
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString&) {
                if (!m_active || m_stopping) return;
                watchLogPath();
                m_debounce.start();
            });
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged,
            this, [this](const QString&) {
                if (!m_active || m_stopping) return;
                watchLogPath();
                m_debounce.start();
            });
}

void DebugLogModel::watchLogPath()
{
    if (!m_active || m_stopping) return;

    const QString path = QString::fromStdString(m_log_path.utf8string());
    if (path.isEmpty()) return;

    const QFileInfo info(path);
    if (info.exists()) {
        if (!m_watcher.files().contains(path)) m_watcher.addPath(path);
        if (m_watcher.files().contains(path)) {
            const auto watched_directories = m_watcher.directories();
            if (!watched_directories.isEmpty()) {
                m_watcher.removePaths(watched_directories);
            }
            return;
        }
    }

    const auto watched_files = m_watcher.files();
    if (!watched_files.isEmpty()) m_watcher.removePaths(watched_files);
    const QString parent_path = info.absolutePath();
    if (!parent_path.isEmpty() && !m_watcher.directories().contains(parent_path)) {
        m_watcher.addPath(parent_path);
    }
}

QList<DebugLogModel::LogLine> DebugLogModel::filteredLines(
    const QList<LogLine>& lines) const
{
    QList<LogLine> filtered;
    if (m_filter.isEmpty()) {
        filtered = lines;
    } else {
        const QString f = m_filter.toLower();
        for (const LogLine& line : lines) {
            if (line.content.toLower().contains(f))
                filtered.append(line);
        }
    }
    return filtered;
}

void DebugLogModel::applyLines(QList<LogLine> lines, bool force_reset)
{
    QList<LogLine> display = filteredLines(lines);
    if (force_reset) {
        beginResetModel();
        m_all_lines = std::move(lines);
        m_display_lines = std::move(display);
        endResetModel();
        return;
    }

    m_all_lines = std::move(lines);
    applyDisplayLines(std::move(display), /*force_reset=*/false);
}

bool DebugLogModel::applyDelta(QList<LogLine> lines)
{
    if (lines.isEmpty()) return false;

    const bool omitted_new_lines = lines.size() > m_load_limit;
    if (omitted_new_lines) lines.resize(m_load_limit);

    const int old_size = static_cast<int>(m_all_lines.size());
    const int new_size = static_cast<int>(lines.size());
    const int old_keep_count = std::min(
        old_size, std::max(0, m_load_limit - new_size));
    const int old_remove_count = old_size - old_keep_count;

    int display_remove_count{0};
    if (m_filter.isEmpty()) {
        display_remove_count = old_remove_count;
    } else if (old_remove_count > 0) {
        const QString filter = m_filter.toLower();
        for (int i = old_keep_count; i < m_all_lines.size(); ++i) {
            if (m_all_lines.at(i).content.toLower().contains(filter)) {
                ++display_remove_count;
            }
        }
    }

    QList<LogLine> display_insert = filteredLines(lines);
    const int display_insert_count = display_insert.size();
    const int surviving_display_count = m_display_lines.size() - display_remove_count;

    // Publish the prepend first so a ListView can anchor the previously visible
    // row. Any cap-induced removal is confined to the oldest filtered suffix.
    if (!display_insert.isEmpty()) {
        beginInsertRows(QModelIndex{}, 0, display_insert.size() - 1);
        display_insert.reserve(display_insert.size() + m_display_lines.size());
        display_insert.append(m_display_lines);
        m_display_lines = std::move(display_insert);
        endInsertRows();
    }
    if (display_remove_count > 0) {
        const int first = display_insert_count + surviving_display_count;
        beginRemoveRows(QModelIndex{}, first, m_display_lines.size() - 1);
        m_display_lines.erase(m_display_lines.begin() + first,
                              m_display_lines.end());
        endRemoveRows();
    }

    lines.reserve(lines.size() + old_keep_count);
    lines.append(m_all_lines.cbegin(), m_all_lines.cbegin() + old_keep_count);
    m_all_lines = std::move(lines);

    if (display_insert_count > 0 && surviving_display_count > 0) {
        Q_EMIT dataChanged(index(display_insert_count, 0),
                           index(display_insert_count + surviving_display_count - 1, 0),
                           {LineNumberRole});
    }
    return omitted_new_lines || old_remove_count > 0;
}

void DebugLogModel::applyDisplayLines(QList<LogLine> lines, bool force_reset)
{
    if (!force_reset && lines == m_display_lines) return;
    if (force_reset) {
        beginResetModel();
        m_display_lines = std::move(lines);
        endResetModel();
        return;
    }

    if (m_display_lines.isEmpty()) {
        if (lines.isEmpty()) return;
        beginInsertRows(QModelIndex{}, 0, lines.size() - 1);
        m_display_lines = std::move(lines);
        endInsertRows();
        return;
    }
    if (lines.isEmpty()) {
        beginRemoveRows(QModelIndex{}, 0, m_display_lines.size() - 1);
        m_display_lines.clear();
        endRemoveRows();
        return;
    }

    // Appends to debug.log can only add a prefix (newest rows) and pruning can
    // only remove a suffix. loadMore does the inverse operation at the bottom.
    // Locate the old first row in the new projection and preserve the largest
    // contiguous run from there. Stable byte offsets distinguish identical
    // timestamp/message duplicates.
    int prefix_count{-1};
    for (int i = 0; i < lines.size(); ++i) {
        if (lines.at(i) == m_display_lines.first()) {
            prefix_count = i;
            break;
        }
    }
    if (prefix_count < 0) {
        beginRemoveRows(QModelIndex{}, 0, m_display_lines.size() - 1);
        m_display_lines.clear();
        endRemoveRows();
        beginInsertRows(QModelIndex{}, 0, lines.size() - 1);
        m_display_lines = std::move(lines);
        endInsertRows();
        return;
    }

    int common_count{0};
    while (common_count < m_display_lines.size()
           && prefix_count + common_count < lines.size()
           && m_display_lines.at(common_count) == lines.at(prefix_count + common_count)) {
        ++common_count;
    }

    const int old_suffix_count = m_display_lines.size() - common_count;
    if (old_suffix_count > 0) {
        beginRemoveRows(QModelIndex{}, common_count, m_display_lines.size() - 1);
        m_display_lines.erase(m_display_lines.begin() + common_count,
                              m_display_lines.end());
        endRemoveRows();
    }

    const int new_suffix_start = prefix_count + common_count;
    if (new_suffix_start < lines.size()) {
        const int first = m_display_lines.size();
        const int count = lines.size() - new_suffix_start;
        beginInsertRows(QModelIndex{}, first, first + count - 1);
        for (int i = new_suffix_start; i < lines.size(); ++i) {
            m_display_lines.append(lines.at(i));
        }
        endInsertRows();
    }

    // Apply a racing loadMore suffix before a live-update prefix. The QML
    // view restores both anchors asynchronously; making the prepend the final
    // structural notification ensures its top-row anchor wins.
    if (prefix_count > 0) {
        beginInsertRows(QModelIndex{}, 0, prefix_count - 1);
        for (int i = prefix_count - 1; i >= 0; --i) {
            m_display_lines.prepend(lines.at(i));
        }
        endInsertRows();
    }

    if (prefix_count > 0 && common_count > 0) {
        Q_EMIT dataChanged(index(prefix_count, 0),
                           index(m_display_lines.size() - 1, 0),
                           {LineNumberRole});
    }
}

void DebugLogModel::buildDisplayLines(bool force_reset)
{
    applyDisplayLines(filteredLines(m_all_lines), force_reset);
}
