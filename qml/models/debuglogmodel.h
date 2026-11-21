// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DEBUGLOGMODEL_H
#define BITCOIN_QML_MODELS_DEBUGLOGMODEL_H

#include <util/fs.h>
#include <qml/backendexecutor.h>
#include <qml/snapshotquery.h>

#include <QAbstractListModel>
#include <QByteArray>
#include <QFileSystemWatcher>
#include <QList>
#include <QString>
#include <QTimer>

#include <atomic>
#include <functional>
#include <deque>
#include <mutex>
#include <memory>
#include <utility>
#include <array>
#include <optional>
#include <cstdint>

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
//! and retained-state filtering use the injected scheduler so a busy node cannot
//! stall the UI on every debug-log burst.
class DebugLogModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
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
    DebugLogModel(const fs::path& log_path, bool log_to_file, execution::SnapshotServices services, QObject* parent = nullptr);
    ~DebugLogModel() override;

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool busy() const { return m_read_in_flight || m_waiting_retirement; }
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
    QString openError() const { return !m_external_open_error.isEmpty() ? m_external_open_error : !m_retirement_error.isEmpty() ? m_retirement_error : m_open_error; }

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
    void busyChanged();
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
        int chunk{0};
        bool is_error{false};
        bool is_warning{false};

        bool operator==(const LogLine& o) const
        {
            return source_offset == o.source_offset && chunk == o.chunk
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
        std::optional<std::array<uint64_t, 4>> file_identity;
        bool file_opened{false};
        bool memory_limited{false};
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
                                    const std::optional<std::array<uint64_t, 4>>& previous_identity,
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

    struct State;
    struct Update;
    struct Mailbox;
    struct DrainState;
    struct Construction;
    static Construction prepareConstruction(const fs::path& path, execution::SnapshotServices services, QObject* parent);
    DebugLogModel(bool log_to_file, Construction construction);
    void requestUpdate(bool read_file, bool full_load, bool force_reset);
    void receiveUpdate();
    void applyUpdate();
    void finishDrain();
    void finishStop();
    static void retireValue(const std::shared_ptr<Mailbox>& mailbox, const execution::SnapshotServices& services, std::shared_ptr<const void> value, bool retry = false);
    void connectFileWatcher();
    void watchLogPath();
    void buildDisplayLines(bool force_reset = false);
    static std::shared_ptr<Update> Reduce(State& state, ReadResult read,
                                        bool did_read, int limit, const QString& filter,
                                        bool warnings_only, bool force_reset,
                                        const QString& path, quint64 generation);

    static void PopulateParsedFields(LogLine& entry, const QString& raw_message);
    static QString FormatTime(const QString& utc_seconds);

    fs::path m_log_path;
    bool m_log_to_file{true};

    //! Native-owned retained state; never inspected or reduced by the GUI.
    std::shared_ptr<State> m_state;

    //! Bounded presentation records, kept in a deque for bounded delta edits.
    std::deque<LogLine> m_display_lines;

    QString m_filter;
    bool m_warnings_and_errors_only{false};
    int  m_load_limit{1000};
    bool m_has_more_lines{false};
    //! Native tail capacity. Kept separate from rowCount
    //! so empty/short logs and interrupted loadMore requests are unambiguous.
    int m_loaded_limit{0};
    QString m_open_error;
    //! Failure from openLogFile() (open the log in an external application),
    //! kept separate from m_open_error, which the background reader owns and
    //! clears on any successful read.
    QString m_external_open_error;
    QString m_retirement_error;

    //! Whether the last background read could open the log. Initially unknown
    //! availability is represented as false; GUI never performs a stat.
    bool m_log_available{false};

    OpenLocalFileFn m_open_local_file_fn;

    QString m_watch_file, m_watch_parent;
    std::unique_ptr<BackendExecutor> m_executor;
    execution::SnapshotServices m_services;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<DrainState> m_drain_state;
    std::shared_ptr<Update> m_update;
    QTimer m_apply_timer;
    int m_apply_stage{0}, m_apply_position{0};
    bool m_projection_pending{false}, m_force_reset_pending{false}, m_waiting_retirement{false};
    bool m_drained{false};
    bool m_shutdown_started{false};
    bool m_changing_rows{false};
    bool m_destroying{false};
    quint64 m_work_generation{0};
    quint64 m_interest_revision{0};
    quint64 m_native_action_revision{0};
    QFileSystemWatcher m_watcher;
    QTimer m_debounce;

    //! Single-read-in-flight guard so concurrent refresh() calls fold into
    //! one trailing read instead of piling up on the worker thread.
    bool m_read_in_flight{false};
    bool m_refresh_pending{false};
    bool m_pending_full_load{false};
    bool m_active{false};
    bool m_stopping{false};
    quint64 m_activation_generation{0};
};

#endif // BITCOIN_QML_MODELS_DEBUGLOGMODEL_H
