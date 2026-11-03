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
//! Exposes structured log records for the Debug Log V2 table:
//!   - MessageRole    — the message with recognised logging metadata removed
//!   - TimestampRole  — local wall-clock time preserving the file's precision
//!   - IsErrorRole    — true only for error-level records
//!   - IsWarningRole  — true only for warning-level records
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
    Q_PROPERTY(bool warningsAndErrorsOnly READ warningsAndErrorsOnly WRITE setWarningsAndErrorsOnly NOTIFY warningsAndErrorsOnlyChanged)
    Q_PROPERTY(QString openError READ openError   NOTIFY openErrorChanged)
    Q_PROPERTY(bool logAvailable READ logAvailable NOTIFY logAvailableChanged)
    Q_PROPERTY(bool fileLoggingEnabled READ fileLoggingEnabled CONSTANT)

public:
    enum Role {
        MessageRole = Qt::UserRole + 1,
        TimestampRole,
        IsErrorRole,
        IsWarningRole,
    };
    Q_ENUM(Role)

    //! Hard ceiling on loadLimit to protect against unbounded memory growth.
    static constexpr int kMaxLoadLimit = 50'000;

    //! Individual physical log lines larger than this are omitted from the
    //! in-app viewer. The debug.log file itself is never modified.
    static constexpr qsizetype kMaxLogLineBytes = 1024 * 1024;

    //! log_path is the file the node writes to. Pass log_to_file = false when
    //! the node writes no log file (-nodebuglogfile): the model then reads
    //! nothing, since any file at log_path is left over from an earlier run.
    explicit DebugLogModel(const fs::path& log_path, bool log_to_file = true, QObject* parent = nullptr);
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

    bool warningsAndErrorsOnly() const { return m_warnings_and_errors_only; }
    void setWarningsAndErrorsOnly(bool warnings_and_errors_only);

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

    //! False when the node writes no log file. That is the user's own setting,
    //! not a failure, so the page reports it as information.
    bool fileLoggingEnabled() const { return m_log_to_file; }

    Q_INVOKABLE void refresh(bool full_load = false);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE bool openLogFile();
    //! Clear the external-open error. Called when re-entering the page so a
    //! stale openLogFile() failure does not persist across navigation; the
    //! banner reflects the last open attempt, not the log content.
    Q_INVOKABLE void clearOpenError();
    void stop();

    using OpenLocalFileFn = std::function<bool(const QString&)>;
    //! Replaces the hand-off to the desktop's file association, so tests can
    //! drive the "no application is associated" failure without depending on
    //! the machine having (or not having) a handler for .log files.
    void setOpenLocalFileFnForTesting(OpenLocalFileFn fn) { m_open_local_file_fn = std::move(fn); }

Q_SIGNALS:
    void drained();
    void hasMoreLinesChanged();
    void activeChanged();
    void loadLimitChanged();
    void filterChanged();
    void warningsAndErrorsOnlyChanged();
    void openErrorChanged();
    void logAvailableChanged();
    //! Emitted when new lines are appended during an auto-refresh.
    void newLinesAdded(int count);

private:
    struct LogLine {
        QString message;
        QString timestamp;
        qint64  source_offset{-1}; // byte offset in debug.log (stable across appends)
        bool is_error{false};
        bool is_warning{false};

        bool operator==(const LogLine& o) const
        {
            return source_offset == o.source_offset
                && message == o.message
                && timestamp == o.timestamp
                && is_error == o.is_error
                && is_warning == o.is_warning;
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

    static void PopulateParsedFields(LogLine& entry, const QString& raw_message);
    static QString FormatTime(const QString& utc_seconds);

    fs::path m_log_path;
    bool m_log_to_file{true};

    //! All loaded lines stored chronologically (index 0 = oldest loaded).
    QList<LogLine> m_all_lines;

    //! Filtered subset of m_all_lines, also chronological.
    QList<LogLine> m_display_lines;

    QString m_filter;
    bool m_warnings_and_errors_only{false};
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
