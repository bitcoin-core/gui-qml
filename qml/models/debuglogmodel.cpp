// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/debuglogmodel.h>

#include <util/threadnames.h>

#include <algorithm>
#include <utility>

#include <QDateTime>
#include <QDesktopServices>
#include <QFile>
#include <QMetaObject>
#include <QObject>
#include <QRegularExpression>
#include <QTextStream>
#include <QThread>
#include <QTimer>
#include <QUrl>

static const QRegularExpression TIMESTAMP_RX(
    QStringLiteral(R"(^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z)\s*(.*)$)"));

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
    case LineNumberRole:  return line.lineNumber;
    case ContentRole:    return line.content;
    case RelativeTimeRole: return line.relativeTime;
    }
    return {};
}

QHash<int, QByteArray> DebugLogModel::roleNames() const
{
    return {
        {LineNumberRole,   "lineNumber"},
        {ContentRole,      "content"},
        {RelativeTimeRole, "relativeTime"},
    };
}

void DebugLogModel::setLoadLimit(int limit)
{
    if (m_load_limit == limit) return;
    m_load_limit = limit;
    Q_EMIT loadLimitChanged();
}

void DebugLogModel::setFilter(const QString& filter)
{
    if (m_filter == filter) return;
    m_filter = filter;
    Q_EMIT filterChanged();
    buildDisplayLines();
}

void DebugLogModel::refresh(bool full_load)
{
    if (m_stopping) return;

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

    const QString prev_top_content = m_all_lines.isEmpty()
        ? QString{}
        : m_all_lines.first().content;

    const fs::path path = m_log_path;
    const int load_limit = m_load_limit;

    if (!m_reader || !m_reader_thread || !m_reader_thread->isRunning()) {
        m_read_in_flight = false;
        return;
    }

    const bool queued = QMetaObject::invokeMethod(m_reader,
        [this, path, load_limit, full_load, prev_top_content]() mutable {
            if (m_read_cancelled.load(std::memory_order_relaxed)) return;

            ReadResult result = ReadAndFilter(path, load_limit, full_load, m_read_cancelled);
            if (m_read_cancelled.load(std::memory_order_relaxed)) return;

            QMetaObject::invokeMethod(this,
                [this,
                 result = std::move(result),
                 prev_top_content,
                 full_load]() mutable {
                    if (m_stopping || m_read_cancelled.load(std::memory_order_relaxed)) return;
                    onReadCompleted(result, prev_top_content, full_load);
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
    if (m_display_lines.isEmpty()) return;
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();

    // Update cached relative time strings in all_lines.
    for (LogLine& line : m_all_lines) {
        if (line.timestamp_ms >= 0)
            line.relativeTime = RelativeTimeLabelStatic(line.timestamp_ms, now_ms);
    }

    // Rebuild display so delegates see the new strings.
    buildDisplayLines();
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

DebugLogModel::ReadResult DebugLogModel::ReadAndFilter(const fs::path& log_path,
                                                      int load_limit,
                                                      bool full_load,
                                                      const std::atomic_bool& cancelled)
{
    ReadResult result;
    if (cancelled.load(std::memory_order_relaxed)) return result;

    const QString path_str = QString::fromStdString(log_path.utf8string());
    QFile probe(path_str);
    if (!probe.open(QIODevice::ReadOnly | QIODevice::Text)) {
        result.error_message = fs::exists(log_path)
            ? QObject::tr("Could not open debug log file: %1").arg(path_str)
            : QObject::tr("Debug log file not found: %1").arg(path_str);
        return result;
    }
    probe.close();
    result.file_opened = true;

    // Doubling loop for a full load: ensure load_limit non-blank lines even
    // when the tail of the file is dominated by header/banner blanks. For an
    // incremental refresh we just take the last load_limit raw lines.
    QList<LogLine> filtered;
    int fetch_size = load_limit;
    while (true) {
        if (cancelled.load(std::memory_order_relaxed)) return {};

        const QList<LogLine> raw = ReadRawLines(log_path, fetch_size, cancelled);
        if (cancelled.load(std::memory_order_relaxed)) return {};

        filtered.clear();
        for (const LogLine& l : raw) {
            if (cancelled.load(std::memory_order_relaxed)) return {};
            if (!l.content.trimmed().isEmpty() || l.timestamp_ms >= 0)
                filtered.append(l);
        }
        if (!full_load) break;
        if (filtered.size() >= load_limit || raw.size() < fetch_size) break;
        fetch_size *= 2;
    }
    result.filtered = std::move(filtered);
    return result;
}

QList<DebugLogModel::LogLine> DebugLogModel::ReadRawLines(const fs::path& log_path,
                                                         int max_lines,
                                                         const std::atomic_bool& cancelled)
{
    if (cancelled.load(std::memory_order_relaxed)) return {};

    const QString path_str = QString::fromStdString(log_path.utf8string());
    QFile file(path_str);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return {};

    // Seek near the end to avoid reading the entire file. debug.log on an
    // active mainnet node can exceed 100 MB; estimate 500 bytes per line.
    const qint64 seek_pos = std::max(qint64{0},
        file.size() - static_cast<qint64>(max_lines) * 500);
    if (seek_pos > 0) file.seek(seek_pos);

    QTextStream in(&file);
    if (seek_pos > 0) in.readLine(); // discard potentially partial first line

    QStringList raw;
    raw.reserve(max_lines);
    while (!in.atEnd()) {
        if (cancelled.load(std::memory_order_relaxed)) return {};
        raw.append(in.readLine());
    }
    if (raw.size() > max_lines)
        raw = raw.mid(raw.size() - max_lines);

    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();
    QList<LogLine> result;
    result.reserve(raw.size());
    for (const QString& line : raw) {
        if (cancelled.load(std::memory_order_relaxed)) return {};

        LogLine entry;
        const QRegularExpressionMatch m = TIMESTAMP_RX.match(line);
        if (m.hasMatch()) {
            const QDateTime dt = QDateTime::fromString(m.captured(1), Qt::ISODateWithMs);
            entry.timestamp_ms = dt.isValid() ? dt.toMSecsSinceEpoch() : -1;
            entry.content      = m.captured(2).toHtmlEscaped();
        } else {
            entry.timestamp_ms = -1;
            entry.content      = line.toHtmlEscaped();
        }
        entry.relativeTime = entry.timestamp_ms >= 0
            ? RelativeTimeLabelStatic(entry.timestamp_ms, now_ms)
            : QString{};
        result.append(entry);
    }
    return result;
}

void DebugLogModel::onReadCompleted(const ReadResult& result,
                                   const QString& prev_top_content,
                                   bool full_load)
{
    if (m_stopping) return;

    // Propagate open-error state from the background read.
    if (!result.file_opened) {
        if (m_open_error != result.error_message) {
            m_open_error = result.error_message;
            Q_EMIT openErrorChanged();
        }
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
    const QString path_str = QString::fromStdString(m_log_path.utf8string());
    if (!m_watcher.files().contains(path_str)) {
        m_watcher.addPath(path_str);
    }

    const QList<LogLine>& filtered = result.filtered;

    if (full_load || m_all_lines.isEmpty()) {
        const bool new_has_more = filtered.size() > m_load_limit
                                  || m_load_limit >= kMaxLoadLimit;
        const int start = filtered.size() > m_load_limit
            ? filtered.size() - m_load_limit : 0;

        QList<LogLine> new_lines;
        new_lines.reserve(std::min<int>(m_load_limit, filtered.size()));
        for (int i = filtered.size() - 1; i >= start; i--)
            new_lines.append(filtered[i]);

        m_all_lines = std::move(new_lines);

        // At the hard ceiling, stop advertising "has more".
        const bool announced_has_more = new_has_more && m_load_limit < kMaxLoadLimit;
        if (m_has_more_lines != announced_has_more) {
            m_has_more_lines = announced_has_more;
            Q_EMIT hasMoreLinesChanged();
        }
    } else {
        // Incremental refresh: prepend lines newer than the previous top entry.
        if (!prev_top_content.isEmpty()) {
            QList<LogLine> new_entries;
            bool found_prev = false;
            for (int j = filtered.size() - 1; j >= 0; j--) {
                if (filtered[j].content == prev_top_content) {
                    found_prev = true;
                    break;
                }
                new_entries.append(filtered[j]);
            }
            if (found_prev && !new_entries.isEmpty()) {
                m_all_lines = new_entries + m_all_lines;
            } else if (!found_prev) {
                // Log rotated or missed window — full reset from the fresh read.
                const bool new_has_more = filtered.size() > m_load_limit;
                const int start = new_has_more ? filtered.size() - m_load_limit : 0;
                m_all_lines.clear();
                m_all_lines.reserve(m_load_limit);
                for (int i = filtered.size() - 1; i >= start; i--)
                    m_all_lines.append(filtered[i]);
                const bool announced_has_more = new_has_more && m_load_limit < kMaxLoadLimit;
                if (m_has_more_lines != announced_has_more) {
                    m_has_more_lines = announced_has_more;
                    Q_EMIT hasMoreLinesChanged();
                }
            }
        }
    }

    // Emit newLinesAdded for the "new entries" pill.
    if (!prev_top_content.isEmpty()) {
        for (int k = 0; k < m_all_lines.size(); k++) {
            if (m_all_lines[k].content == prev_top_content) {
                if (k > 0) Q_EMIT newLinesAdded(k);
                break;
            }
        }
    }

    buildDisplayLines();

    m_read_in_flight = false;
    // If changes arrived while we were reading, run one trailing refresh.
    if (!m_stopping && m_refresh_pending) {
        m_refresh_pending = false;
        const bool do_full = m_pending_full_load;
        m_pending_full_load = false;
        refresh(do_full);
    }
}

void DebugLogModel::connectFileWatcher()
{
    const QString path_str = QString::fromStdString(m_log_path.utf8string());
    if (path_str.isEmpty()) return;
    m_watcher.addPath(path_str);
    connect(&m_watcher, &QFileSystemWatcher::fileChanged,
            this, [this](const QString& path) {
                if (m_stopping) return;
                m_watcher.addPath(path); // re-add in case of log rotation
                m_debounce.start();
            });
}

void DebugLogModel::buildDisplayLines()
{
    // Apply search filter.
    QList<LogLine> filtered;
    if (m_filter.isEmpty()) {
        filtered = m_all_lines;
    } else {
        const QString f = m_filter.toLower();
        for (const LogLine& line : m_all_lines) {
            if (line.content.toLower().contains(f))
                filtered.append(line);
        }
    }

    // Assign display line numbers (1-based, in display order = newest-first).
    for (int i = 0; i < filtered.size(); i++)
        filtered[i].lineNumber = QString::number(i + 1);

    beginResetModel();
    m_display_lines = filtered;
    endResetModel();
}

QString DebugLogModel::relativeTimeLabel(qint64 timestamp_ms, qint64 now_ms) const
{
    return RelativeTimeLabelStatic(timestamp_ms, now_ms);
}

QString DebugLogModel::RelativeTimeLabelStatic(qint64 timestamp_ms, qint64 now_ms)
{
    const qint64 diff = (now_ms - timestamp_ms) / 1000;
    if (diff < 60)    return QObject::tr("just now");
    if (diff < 3600)  return QObject::tr("%1 min ago").arg(diff / 60);
    if (diff < 86400) return QObject::tr("%1 hr ago").arg(diff / 3600);
    return QObject::tr("%1 d ago").arg(diff / 86400);
}
