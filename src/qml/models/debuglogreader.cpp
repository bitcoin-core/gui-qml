// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/debuglogreader.h>

#include <QDateTime>
#include <QFile>
#include <QObject>
#include <QRegularExpression>

#include <algorithm>
#include <utility>

static const QRegularExpression TIMESTAMP_RX(
    QStringLiteral(R"(^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2}(?:\.\d+)?Z)\s*(.*)$)"));
static const QRegularExpression COMMAND_PREFIX_RX(
    QStringLiteral(R"(^([^:]{1,80}):\s+(.*)$)"));

namespace {
constexpr qint64 TAIL_READ_BLOCK_SIZE{64 * 1024};
constexpr qint64 FILE_ANCHOR_SIZE{256};
constexpr qint64 MAX_DELTA_BYTES{8 * 1024 * 1024};

QByteArray ReadAnchor(QFile& file, qint64 file_size)
{
    const qint64 anchor_size = std::min(file_size, FILE_ANCHOR_SIZE);
    if (anchor_size <= 0 || !file.seek(file_size - anchor_size)) return {};
    return file.read(anchor_size);
}
} // namespace

DebugLogReader::ReadResult DebugLogReader::ReadAndFilter(const fs::path& log_path,
                                                      int load_limit,
                                                      bool full_load,
                                                      qint64 previous_file_size,
                                                      const QByteArray& previous_partial,
                                                      bool previous_discarding_oversized_line,
                                                      const QByteArray& previous_anchor,
                                                      const std::atomic_bool& cancelled)
{
    if (cancelled.load(std::memory_order_relaxed)) return {};
    if (full_load || previous_file_size < 0) {
        return ReadTail(log_path, load_limit, cancelled);
    }

    const QString path_str = QString::fromStdString(log_path.utf8string());
    QFile file(path_str);
    ReadResult result;
    if (!file.open(QIODevice::ReadOnly)) {
        result.error_message = fs::exists(log_path)
            ? QObject::tr("Could not open debug log file: %1").arg(path_str)
            : QObject::tr("Debug log file not found: %1").arg(path_str);
        return result;
    }
    result.file_opened = true;

    // Validate bytes at the old end-of-file before trusting the saved offset.
    // This detects truncation and the common rotate-and-recreate case without
    // relying on platform-specific inode APIs. On failure, rebuild from a
    // bounded tail snapshot.
    const qint64 snapshot_size = file.size();
    bool continuity_valid = snapshot_size >= previous_file_size
        && previous_partial.size() <= previous_file_size;
    if (continuity_valid && previous_file_size > 0) {
        continuity_valid = !previous_anchor.isEmpty()
            && file.seek(previous_file_size - previous_anchor.size())
            && file.read(previous_anchor.size()) == previous_anchor;
    }
    if (!continuity_valid) {
        file.close();
        result = ReadTail(log_path, load_limit, cancelled);
        result.continuity_lost = result.file_opened;
        return result;
    }

    const qint64 appended_size = snapshot_size - previous_file_size;
    if (appended_size > MAX_DELTA_BYTES
        || previous_partial.size() > MAX_DELTA_BYTES - appended_size) {
        file.close();
        return ReadTail(log_path, load_limit, cancelled);
    }

    if (cancelled.load(std::memory_order_relaxed)
        || !file.seek(previous_file_size)) {
        return {};
    }
    const QByteArray appended = file.read(appended_size);
    if (appended.size() != appended_size) {
        file.close();
        result = ReadTail(log_path, load_limit, cancelled);
        result.continuity_lost = result.file_opened;
        return result;
    }

    QByteArray combined;
    qint64 combined_offset{previous_file_size - previous_partial.size()};
    if (previous_discarding_oversized_line) {
        const qsizetype newline = appended.indexOf('\n');
        if (newline < 0) {
            result.full_snapshot = false;
            result.file_size = snapshot_size;
            result.file_anchor = ReadAnchor(file, snapshot_size);
            result.discarding_oversized_line = true;
            return result;
        }
        combined = appended.mid(newline + 1);
        combined_offset = previous_file_size + newline + 1;
    } else {
        combined = previous_partial + appended;
    }

    const qsizetype last_newline = combined.lastIndexOf('\n');
    result.full_snapshot = false;
    result.file_size = snapshot_size;
    result.file_anchor = ReadAnchor(file, snapshot_size);
    if (last_newline < 0) {
        if (combined.size() <= kMaxLogLineBytes) {
            result.trailing_partial = combined;
        } else {
            result.discarding_oversized_line = true;
        }
        return result;
    }

    const qsizetype complete_size = last_newline + 1;
    result.trailing_partial = combined.mid(complete_size);
    if (result.trailing_partial.size() > kMaxLogLineBytes) {
        result.trailing_partial.clear();
        result.discarding_oversized_line = true;
    }
    result.lines = ParseCompleteLines(
        combined.first(complete_size),
        combined_offset,
        std::min(load_limit, kMaxLoadLimit) + 1,
        cancelled);
    result.has_more_lines = result.lines.size() > load_limit;
    return result;
}

DebugLogReader::ReadResult DebugLogReader::ReadTail(const fs::path& log_path,
                                                 int load_limit,
                                                 const std::atomic_bool& cancelled)
{
    ReadResult result;
    if (cancelled.load(std::memory_order_relaxed)) return result;

    const QString path_str = QString::fromStdString(log_path.utf8string());
    QFile file(path_str);
    if (!file.open(QIODevice::ReadOnly)) {
        result.error_message = fs::exists(log_path)
            ? QObject::tr("Could not open debug log file: %1").arg(path_str)
            : QObject::tr("Debug log file not found: %1").arg(path_str);
        return result;
    }

    result.file_opened = true;
    result.full_snapshot = true;
    result.snapshot_limit = std::min(load_limit, kMaxLoadLimit);
    result.file_size = file.size();
    result.file_anchor = ReadAnchor(file, result.file_size);

    // Find the final newline without accumulating the unfinished suffix on
    // every backward step. Bitcoin Core normally writes newline-terminated
    // entries, but the viewer can observe an in-progress or interrupted write.
    qint64 complete_end{-1};
    qint64 cursor = result.file_size;
    while (cursor > 0) {
        if (cancelled.load(std::memory_order_relaxed)) return {};
        const qint64 start = std::max(qint64{0}, cursor - TAIL_READ_BLOCK_SIZE);
        if (!file.seek(start)) return {};
        const QByteArray chunk = file.read(cursor - start);
        if (chunk.size() != cursor - start) return {};
        const qsizetype last_newline = chunk.lastIndexOf('\n');
        if (last_newline >= 0) {
            complete_end = start + last_newline + 1;
            break;
        }
        cursor = start;
    }

    const qint64 partial_start = std::max(qint64{0}, complete_end);
    const qint64 partial_size = result.file_size - partial_start;
    if (partial_size > kMaxLogLineBytes) {
        result.discarding_oversized_line = true;
    } else if (partial_size > 0) {
        if (!file.seek(partial_start)) return {};
        result.trailing_partial = file.read(partial_size);
        if (result.trailing_partial.size() != partial_size) return {};
    }
    if (complete_end < 0) {
        return result;
    }

    const int target = std::min(load_limit, kMaxLoadLimit) + 1;
    QList<LogLine> newest_first;
    newest_first.reserve(target);
    QByteArray carry;
    bool discarding_oversized_line{false};
    cursor = complete_end;

    // Process complete lines from newest to oldest. At most one boundary-
    // spanning line is carried. If it exceeds the viewer limit, discard its
    // remaining prefix until the preceding newline restores framing.
    while (cursor > 0 && newest_first.size() < target) {
        if (cancelled.load(std::memory_order_relaxed)) return {};
        const qint64 start = std::max(qint64{0}, cursor - TAIL_READ_BLOCK_SIZE);
        if (!file.seek(start)) return {};
        const QByteArray chunk = file.read(cursor - start);
        if (chunk.size() != cursor - start) return {};

        QByteArray data;
        if (discarding_oversized_line) {
            const qsizetype preceding_newline = chunk.lastIndexOf('\n');
            if (preceding_newline < 0) {
                cursor = start;
                continue;
            }
            data = chunk.first(preceding_newline + 1);
            discarding_oversized_line = false;
        } else {
            data = chunk + carry;
        }

        QByteArray complete_segment;
        qint64 segment_offset{start};
        if (start == 0) {
            complete_segment = data;
            carry.clear();
        } else {
            const qsizetype first_newline = data.indexOf('\n');
            if (first_newline < 0) {
                if (data.size() > kMaxLogLineBytes) {
                    carry.clear();
                    discarding_oversized_line = true;
                } else {
                    carry = data;
                }
                cursor = start;
                continue;
            }
            complete_segment = data.mid(first_newline + 1);
            segment_offset = start + first_newline + 1;
            carry = data.first(first_newline + 1);
            if (carry.size() > kMaxLogLineBytes) {
                carry.clear();
                discarding_oversized_line = true;
            }
        }

        QList<LogLine> parsed = ParseCompleteLines(
            complete_segment,
            segment_offset,
            target - newest_first.size(),
            cancelled);
        newest_first.append(std::move(parsed));
        cursor = start;
    }

    result.has_more_lines = newest_first.size() > load_limit;
    if (newest_first.size() > load_limit) newest_first.removeLast();
    result.lines = std::move(newest_first);
    return result;
}

QList<DebugLogReader::LogLine> DebugLogReader::ParseCompleteLines(
    const QByteArray& bytes,
    qint64 base_offset,
    int max_filtered_lines,
    const std::atomic_bool& cancelled)
{
    QList<LogLine> result;
    result.reserve(std::min<int>(max_filtered_lines, 1024));
    const qint64 now_ms = QDateTime::currentMSecsSinceEpoch();

    qsizetype scan_end = bytes.size();
    while (scan_end > 0 && result.size() < max_filtered_lines) {
        if (cancelled.load(std::memory_order_relaxed)) return {};
        const qsizetype terminator = bytes.at(scan_end - 1) == '\n'
            ? scan_end - 1
            : scan_end;
        const qsizetype previous_newline = terminator > 0
            ? bytes.lastIndexOf('\n', terminator - 1)
            : -1;
        const qsizetype start = previous_newline + 1;
        const qsizetype line_size = terminator - start;
        if (line_size <= kMaxLogLineBytes) {
            QByteArray raw = bytes.mid(start, line_size);
            if (raw.endsWith('\r')) raw.chop(1);

            LogLine entry;
            entry.source_offset = base_offset + start;
            QString raw_message;
            const QString line = QString::fromUtf8(raw);
            const QRegularExpressionMatch match = TIMESTAMP_RX.match(line);
            if (match.hasMatch()) {
                const QDateTime dt = QDateTime::fromString(match.captured(1), Qt::ISODateWithMs);
                entry.timestamp_ms = dt.isValid() ? dt.toMSecsSinceEpoch() : -1;
                raw_message = match.captured(2);
            } else {
                entry.timestamp_ms = -1;
                raw_message = line;
            }
            PopulateParsedFields(entry, raw_message);
            entry.relativeTime = entry.timestamp_ms >= 0
                ? RelativeTimeLabelStatic(entry.timestamp_ms, now_ms)
                : QString{};
            if (!entry.content.trimmed().isEmpty() || entry.timestamp_ms >= 0) {
                result.append(std::move(entry));
            }
        }

        if (previous_newline < 0) break;
        scan_end = previous_newline + 1;
    }
    return result;
}

void DebugLogReader::PopulateParsedFields(LogLine& entry, const QString& raw_message)
{
    entry.content = raw_message.toHtmlEscaped();
    const QString trimmed = raw_message.trimmed();
    entry.command.clear();
    entry.message = trimmed;
    entry.severity = InfoSeverity;

    const QRegularExpressionMatch command_match = COMMAND_PREFIX_RX.match(trimmed);
    if (command_match.hasMatch()) {
        const QString command = command_match.captured(1).trimmed();
        const QString message = command_match.captured(2).trimmed();
        if (!command.isEmpty() && !message.isEmpty()) {
            entry.command = command;
            entry.message = message;
        }
    }

    const QString severity_source = entry.command.isEmpty() ? trimmed : entry.command;
    if (severity_source.compare(QLatin1String("ERROR"), Qt::CaseInsensitive) == 0) {
        entry.severity = ErrorSeverity;
    } else if (severity_source.compare(QLatin1String("WARNING"), Qt::CaseInsensitive) == 0) {
        entry.severity = WarningSeverity;
    }
}

QString DebugLogReader::RelativeTimeLabelStatic(qint64 timestamp_ms, qint64 now_ms)
{
    const qint64 diff = (now_ms - timestamp_ms) / 1000;
    if (diff < 60)    return QObject::tr("just now");
    if (diff < 3600)  return QObject::tr("%1 min ago").arg(diff / 60);
    if (diff < 86400) return QObject::tr("%1 hr ago").arg(diff / 3600);
    return QObject::tr("%1 d ago").arg(diff / 86400);
}
