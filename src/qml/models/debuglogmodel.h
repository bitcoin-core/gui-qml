// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DEBUGLOGMODEL_H
#define BITCOIN_QML_MODELS_DEBUGLOGMODEL_H

#include <qml/models/debuglogreader.h>

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
    static constexpr int kMaxLoadLimit = DebugLogReader::kMaxLoadLimit;

    //! Individual physical log lines larger than this are omitted from the
    //! in-app viewer. The debug.log file itself is never modified.
    static constexpr qsizetype kMaxLogLineBytes = DebugLogReader::kMaxLogLineBytes;

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

    QString openError() const { return m_open_error; }

    Q_INVOKABLE void refresh(bool full_load = false);
    Q_INVOKABLE void loadMore();
    Q_INVOKABLE bool openLogFile();
    Q_INVOKABLE void updateRelativeTimes();
    void stop();

Q_SIGNALS:
    void hasMoreLinesChanged();
    void activeChanged();
    void loadLimitChanged();
    void filterChanged();
    void openErrorChanged();
    //! Emitted when new lines are prepended at the top during an auto-refresh.
    void newLinesAdded(int count);

private:
    using LogLine = DebugLogReader::LogLine;
    using ReadResult = DebugLogReader::ReadResult;

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
