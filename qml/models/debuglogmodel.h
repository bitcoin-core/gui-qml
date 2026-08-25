// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DEBUGLOGMODEL_H
#define BITCOIN_QML_MODELS_DEBUGLOGMODEL_H

#include <util/fs.h>

#include <QAbstractListModel>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QList>
#include <QString>
#include <QTimer>

#include <atomic>
#include <functional>
#include <utility>

class QThread;

//! List model for the in-app debug.log viewer.
//!
//! Exposes log lines as list items with display roles:
//!   - LineNumberRole  — 1-based line number as a display string ("1", "2", …)
//!   - ContentRole     — HTML-escaped message text (no inline style; colours
//!                       are applied by the QML delegate)
//!   - RelativeTimeRole — human-readable age string ("just now", "3 min ago",
//!                        …) updated by updateRelativeTimes()
//!   - CommandRole      — parsed message prefix before ":" when present
//!   - MessageRole      — parsed message body after the prefix
//!   - DateLabelRole    — label shown in the row's right-hand date slot
//!   - SeverityRole     — display severity used by the QML delegate
//!
//! Pagination: only the most recent `loadLimit` lines are kept in memory.
//! Call loadMore() to increase the limit by 1000, up to kMaxLoadLimit.
//!
//! File watching: the model only watches the log while active and coalesces
//! rapid writes with a 500 ms debounce timer before calling refresh(). Reads
//! themselves run on a dedicated worker thread so a busy, noisy node cannot
//! stall the UI on every debug-log burst.
class DebugLogModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool hasMoreLines READ hasMoreLines NOTIFY hasMoreLinesChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(int  loadLimit   READ loadLimit    WRITE setLoadLimit   NOTIFY loadLimitChanged)
    Q_PROPERTY(QString filter   READ filter       WRITE setFilter      NOTIFY filterChanged)
    Q_PROPERTY(QString openError READ openError   NOTIFY openErrorChanged)
    Q_PROPERTY(bool logAvailable READ logAvailable NOTIFY logAvailableChanged)

public:
    enum Role {
        LineNumberRole  = Qt::UserRole + 1,
        ContentRole,
        RelativeTimeRole,
        CommandRole,
        MessageRole,
        DateLabelRole,
        SeverityRole,
    };
    Q_ENUM(Role)

    enum Severity {
        InfoSeverity = 0,
        WarningSeverity,
        ErrorSeverity,
    };
    Q_ENUM(Severity)

    //! Hard ceiling on loadLimit to protect against unbounded memory growth.
    static constexpr int kMaxLoadLimit = 50'000;

    //! Individual physical log lines larger than this are omitted from the
    //! in-app viewer. The debug.log file itself is never modified.
    static constexpr qsizetype kMaxLogLineBytes = 1024 * 1024;

    explicit DebugLogModel(const fs::path& log_path, QObject* parent = nullptr);
    ~DebugLogModel() override;

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool hasMoreLines() const { return m_has_more_lines; }

    bool active() const { return m_active; }
    void setActive(bool active);

    int loadLimit() const { return m_load_limit; }
    void setLoadLimit(int limit);

    QString filter() const { return m_filter; }
    void setFilter(const QString& filter);

    //! Two failures can be pending, and they have deliberately different
    //! lifetimes.
    //!
    //! m_external_open_error is the outcome of a single user action (handing
    //! the log to another application). It is transient: it clears on a later
    //! successful openLogFile(), and on clearOpenError() when the page is
    //! re-entered, so a stale click failure does not follow the user around.
    //!
    //! m_open_error describes the state of the log itself (missing or
    //! unreadable). That state outlives any one visit, so it must survive
    //! navigation and is owned solely by the background reader.
    //!
    //! The action error wins while it is set, because it answers the question
    //! the user just asked.
    QString openError() const { return m_external_open_error.isEmpty() ? m_open_error : m_external_open_error; }

    //! False while the log cannot be read. Controls that only make sense
    //! against readable log content (searching it, opening it in another
    //! application) are disabled on this.
    bool logAvailable() const { return m_log_available; }

    Q_INVOKABLE void refresh(bool full_load = false);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE bool openLogFile();
    //! Clear the external-open error. Called when re-entering the page so a
    //! stale openLogFile() failure does not persist across navigation; the
    //! banner reflects the last open attempt, not the log content.
    Q_INVOKABLE void clearOpenError();
    Q_INVOKABLE void updateRelativeTimes();
    void stop();

    using OpenLocalFileFn = std::function<bool(const QString&)>;
    //! Replaces the hand-off to the desktop's file association, so tests can
    //! drive the "no application is associated" failure without depending on
    //! the machine having (or not having) a handler for .log files.
    void setOpenLocalFileFnForTesting(OpenLocalFileFn fn) { m_open_local_file_fn = std::move(fn); }

Q_SIGNALS:
    void hasMoreLinesChanged();
    void activeChanged();
    void loadLimitChanged();
    void filterChanged();
    void openErrorChanged();
    void logAvailableChanged();
    //! Emitted when new lines are prepended at the top during an auto-refresh.
    void newLinesAdded(int count);

private:
    struct LogLine {
        QString content;      // HTML-escaped full message text
        QString command;      // parsed message prefix, plain text
        QString message;      // parsed message body, plain text
        qint64  source_offset{-1}; // byte offset in debug.log (stable across appends)
        qint64  timestamp_ms; // epoch ms, -1 if not parseable
        QString relativeTime; // cached human-readable age
        Severity severity{InfoSeverity};

        // Identity for incremental display diffs. relativeTime is derived
        // (refreshed separately by the relative-time timer) and deliberately
        // excluded.
        bool operator==(const LogLine& o) const
        {
            return source_offset == o.source_offset
                && content == o.content
                && command == o.command
                && message == o.message
                && severity == o.severity
                && timestamp_ms == o.timestamp_ms;
        }
    };

    //! Result of a file read performed off the GUI thread. The worker must
    //! not touch any QObject state on the model — everything it discovers
    //! (including open errors) is returned here for the main thread to apply.
    struct ReadResult {
        bool file_opened{false};
        QString error_message;
        //! A full snapshot is newest-first and contains at most loadLimit
        //! entries. A delta contains only newly completed lines, newest-first.
        QList<LogLine> lines;
        bool full_snapshot{true};
        bool continuity_lost{false};
        bool has_more_lines{false};
        int snapshot_limit{0};
        qint64 file_size{-1};
        QByteArray trailing_partial;
        //! True after an unfinished line exceeds kMaxLogLineBytes. Subsequent
        //! bytes are ignored until its terminating newline restores framing.
        bool discarding_oversized_line{false};
        QByteArray file_anchor;
    };

    //! File-reading worker. Pure function — no QObject / signal access —
    //! so it can safely run on the dedicated worker thread.
    static ReadResult ReadAndFilter(const fs::path& log_path,
                                    int load_limit,
                                    bool full_load,
                                    qint64 previous_file_size,
                                    const QByteArray& previous_partial,
                                    bool previous_discarding_oversized_line,
                                    const QByteArray& previous_anchor,
                                    const std::atomic_bool& cancelled);

    //! Read a bounded tail snapshot by scanning backward in fixed-size blocks.
    static ReadResult ReadTail(const fs::path& log_path,
                               int load_limit,
                               const std::atomic_bool& cancelled);

    //! Parse complete newline-terminated records, returning newest first.
    static QList<LogLine> ParseCompleteLines(const QByteArray& bytes,
                                             qint64 base_offset,
                                             int max_filtered_lines,
                                             const std::atomic_bool& cancelled);

    //! Completion handler invoked on the GUI thread after ReadAndFilter
    //! returns. Applies the result to m_all_lines and rebuilds the display.
    void onReadCompleted(const ReadResult& result,
                         bool full_load);

    void connectFileWatcher();
    void watchLogPath();
    void buildDisplayLines(bool force_reset = false);
    void applyLines(QList<LogLine> lines, bool force_reset);
    bool applyDelta(QList<LogLine> lines);
    void applyDisplayLines(QList<LogLine> lines, bool force_reset);
    QList<LogLine> filteredLines(const QList<LogLine>& lines) const;
    QString relativeTimeLabel(qint64 timestamp_ms, qint64 now_ms) const;

    static void PopulateParsedFields(LogLine& entry, const QString& raw_message);
    static QString RelativeTimeLabelStatic(qint64 timestamp_ms, qint64 now_ms);

    fs::path m_log_path;

    //! All loaded lines stored newest-first (index 0 = newest).
    QList<LogLine> m_all_lines;

    //! Filtered subset of m_all_lines, also newest-first.
    QList<LogLine> m_display_lines;

    QString m_filter;
    int  m_load_limit{1000};
    bool m_has_more_lines{false};
    //! Tail capacity represented by m_all_lines. Kept separate from rowCount
    //! so empty/short logs and interrupted loadMore requests are unambiguous.
    int m_loaded_limit{0};
    QString m_open_error;
    //! Failure from openLogFile() (open the log in an external application),
    //! kept separate from m_open_error, which the background reader owns and
    //! clears on any successful read.
    QString m_external_open_error;

    //! Whether the last background read could open the log. Starts true so the
    //! page does not flash a disabled state before the first read lands.
    bool m_log_available{true};

    OpenLocalFileFn m_open_local_file_fn;

    //! End-of-file state from the last successful worker read. Normal
    //! refreshes validate the anchor, seek to file_size, and parse only bytes
    //! appended since then. A bounded partial final line is carried across
    //! reads; an oversized one is discarded through its terminating newline.
    qint64 m_file_size{-1};
    QByteArray m_trailing_partial;
    bool m_discarding_oversized_line{false};
    QByteArray m_file_anchor;

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QObject* m_reader{nullptr};
    QThread* m_reader_thread{nullptr};

    //! Single-read-in-flight guard so concurrent refresh() calls fold into
    //! one trailing read instead of piling up on the worker thread.
    bool m_read_in_flight{false};
    bool m_refresh_pending{false};
    bool m_pending_full_load{false};
    bool m_active{false};
    bool m_stopping{false};
    quint64 m_activation_generation{0};
    std::atomic_bool m_read_cancelled{false};
};

#endif // BITCOIN_QML_MODELS_DEBUGLOGMODEL_H
