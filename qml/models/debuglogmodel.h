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

    bool warningsAndErrorsOnly() const { return m_warnings_and_errors_only; }
    void setWarningsAndErrorsOnly(bool warnings_and_errors_only);

    QString openError() const { return m_open_error; }

    Q_INVOKABLE void refresh(bool full_load = false);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE bool openLogFile();
    void stop();

Q_SIGNALS:
    void hasMoreLinesChanged();
    void activeChanged();
    void loadLimitChanged();
    void filterChanged();
    void warningsAndErrorsOnlyChanged();
    void openErrorChanged();
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
