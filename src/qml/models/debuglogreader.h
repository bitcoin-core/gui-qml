// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_DEBUGLOGREADER_H
#define BITCOIN_QML_MODELS_DEBUGLOGREADER_H

#include <util/fs.h>

#include <QByteArray>
#include <QList>
#include <QString>

#include <atomic>

//! Bounded, stateless debug.log reader. Owns no QObject or model state.
//! A caller supplies the previous snapshot cursor and cancellation flag;
//! results can be produced off-thread and applied only by the owning model.
class DebugLogReader
{
public:
    enum Severity { InfoSeverity = 0, WarningSeverity, ErrorSeverity };
    static constexpr int kMaxLoadLimit = 50'000;
    static constexpr qsizetype kMaxLogLineBytes = 1024 * 1024;

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

    static void PopulateParsedFields(LogLine& entry, const QString& raw_message);
    static QString RelativeTimeLabelStatic(qint64 timestamp_ms, qint64 now_ms);
};

#endif // BITCOIN_QML_MODELS_DEBUGLOGREADER_H
