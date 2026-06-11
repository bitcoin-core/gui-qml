// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DEBUGLOGMODEL_H
#define BITCOIN_QML_MODELS_DEBUGLOGMODEL_H

#include <util/fs.h>

#include <QAbstractListModel>
#include <QFileSystemWatcher>
#include <QList>
#include <QString>
#include <QTimer>

#include <atomic>

class QThread;

//! List model for the in-app debug.log viewer.
//!
//! Exposes log lines as list items with three roles:
//!   - LineNumberRole  — 1-based line number as a display string ("1", "2", …)
//!   - ContentRole     — HTML-escaped message text (no inline style; colours
//!                       are applied by the QML delegate)
//!   - RelativeTimeRole — human-readable age string ("just now", "3 min ago",
//!                        …) updated by updateRelativeTimes()
//!
//! Pagination: only the most recent `loadLimit` lines are kept in memory.
//! Call loadMore() to increase the limit by 1000, up to kMaxLoadLimit.
//!
//! File watching: the model connects a QFileSystemWatcher to the log file and
//! coalesces rapid writes with a 500 ms debounce timer before calling refresh().
//! Reads themselves run on a dedicated worker thread so a busy, noisy node cannot
//! stall the UI on every debug-log burst.
class DebugLogModel : public QAbstractListModel
{
    Q_OBJECT

    Q_PROPERTY(bool hasMoreLines READ hasMoreLines NOTIFY hasMoreLinesChanged)
    Q_PROPERTY(int  loadLimit   READ loadLimit    WRITE setLoadLimit   NOTIFY loadLimitChanged)
    Q_PROPERTY(QString filter   READ filter       WRITE setFilter      NOTIFY filterChanged)
    Q_PROPERTY(QString openError READ openError   NOTIFY openErrorChanged)

public:
    enum Role {
        LineNumberRole  = Qt::UserRole + 1,
        ContentRole,
        RelativeTimeRole,
    };
    Q_ENUM(Role)

    //! Hard ceiling on loadLimit to protect against unbounded memory growth.
    static constexpr int kMaxLoadLimit = 50'000;

    explicit DebugLogModel(const fs::path& log_path, QObject* parent = nullptr);
    ~DebugLogModel() override;

    // QAbstractListModel interface
    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    bool hasMoreLines() const { return m_has_more_lines; }

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
    void loadLimitChanged();
    void filterChanged();
    void openErrorChanged();
    //! Emitted when new lines are prepended at the top during an auto-refresh.
    void newLinesAdded(int count);

private:
    struct LogLine {
        QString lineNumber;
        QString content;      // HTML-escaped message text
        qint64  timestamp_ms; // epoch ms, -1 if not parseable
        QString relativeTime; // cached human-readable age
    };

    //! Result of a file read performed off the GUI thread. The worker must
    //! not touch any QObject state on the model — everything it discovers
    //! (including open errors) is returned here for the main thread to apply.
    struct ReadResult {
        bool file_opened{false};
        QString error_message;
        QList<LogLine> filtered; // oldest-first, already filtered of blank lines
    };

    //! File-reading worker. Pure function — no QObject / signal access —
    //! so it can safely run on the dedicated worker thread.
    static ReadResult ReadAndFilter(const fs::path& log_path,
                                    int load_limit,
                                    bool full_load,
                                    const std::atomic_bool& cancelled);

    //! Raw file read (one pass). Called from ReadAndFilter.
    static QList<LogLine> ReadRawLines(const fs::path& log_path,
                                       int max_lines,
                                       const std::atomic_bool& cancelled);

    //! Completion handler invoked on the GUI thread after ReadAndFilter
    //! returns. Applies the result to m_all_lines and rebuilds the display.
    void onReadCompleted(const ReadResult& result,
                         const QString& prev_top_content,
                         bool full_load);

    void connectFileWatcher();
    void buildDisplayLines();
    QString relativeTimeLabel(qint64 timestamp_ms, qint64 now_ms) const;

    static QString RelativeTimeLabelStatic(qint64 timestamp_ms, qint64 now_ms);

    fs::path m_log_path;

    //! All loaded lines stored newest-first (index 0 = newest).
    QList<LogLine> m_all_lines;

    //! Filtered subset of m_all_lines, also newest-first.
    QList<LogLine> m_display_lines;

    QString m_filter;
    int  m_load_limit{1000};
    bool m_has_more_lines{false};
    QString m_open_error;

    QFileSystemWatcher m_watcher;
    QTimer m_debounce;
    QObject* m_reader{nullptr};
    QThread* m_reader_thread{nullptr};

    //! Single-read-in-flight guard so concurrent refresh() calls fold into
    //! one trailing read instead of piling up on the worker thread.
    bool m_read_in_flight{false};
    bool m_refresh_pending{false};
    bool m_pending_full_load{false};
    bool m_stopping{false};
    std::atomic_bool m_read_cancelled{false};
};

#endif // BITCOIN_QML_MODELS_DEBUGLOGMODEL_H
