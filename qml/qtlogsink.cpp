// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/qtlogsink.h>
#include <QCoreApplication>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
#include <utility>

namespace qtlog {
void EmergencyFatal(const QMessageLogContext& context, const QString& message, QtMsgType type)
{
    const QByteArray formatted{qFormatLogMessage(type, context, message).toUtf8() + '\n'};
    std::fwrite(formatted.constData(), 1, static_cast<size_t>(formatted.size()), stderr);
    std::fflush(stderr);
}
int FatalPolicy::parseCounter(const QByteArray& value, const QVersionNumber& runtime)
{
    if (value.isEmpty()) return 0;
    bool valid{false}; const auto count = value.toInt(&valid, 0);
    if (!valid || (runtime >= QVersionNumber{6, 10, 0} && count < 0)) return 1;
    return count;
}
FatalPolicy::Decision FatalPolicy::enter(QtMsgType type)
{
    std::unique_lock lock{m_mutex};
    if (m_fatal_started) return {std::move(lock), true, true};
    bool terminating = type == QtFatalMsg;
    if (type == QtWarningMsg && m_warnings > 0) terminating = --m_warnings == 0;
    if (type == QtCriticalMsg && m_criticals > 0) terminating = --m_criticals == 0;
    if (terminating) m_fatal_started = true;
    return {std::move(lock), terminating, false};
}
std::shared_ptr<FatalPolicy> CaptureFatalPolicy()
{
    // Process configuration is captured before QApplication/first log emission.
    // Use the loaded library version, including when it upgrades the compile SDK.
    const auto runtime = QVersionNumber::fromString(QString::fromLatin1(qVersion()));
    return std::make_shared<FatalPolicy>(FatalPolicy::parseCounter(qgetenv("QT_FATAL_WARNINGS"), runtime),
        FatalPolicy::parseCounter(qgetenv("QT_FATAL_CRITICALS"), runtime));
}
void Forward(QtMsgType type, const QMessageLogContext& context, const QString& message, Stage stage,
             const std::shared_ptr<FatalPolicy>& policy, const std::shared_ptr<Queue>& queue)
{
    auto decision = policy ? policy->enter(type) : FatalPolicy::Decision{};
    if (decision.already_fatal) std::abort();
    if (type == QtFatalMsg || decision.terminating) {
        EmergencyFatal(context, message, type);
        if (type != QtFatalMsg) std::abort();
        return; // Qt performs its own immediate QtFatalMsg termination.
    }
    if (queue) queue->enqueue(type, context, message, stage);
}
QString Message(const Record& record)
{
    return record.overflow
        ? QStringLiteral("Qt log queue overflow: %1 messages (%2 UTF-16 bytes) were rejected.").arg(record.rejected).arg(record.rejected_bytes)
        : record.context_truncated ? record.message + QStringLiteral(" [Qt log context truncated]") : record.message;
}
QByteArray FormatStderr(const Record& record)
{
    const QMessageLogContext context{record.has_file ? record.file.data() : nullptr, record.line,
        record.has_function ? record.function.data() : nullptr, record.has_category ? record.category.data() : nullptr};
    return qFormatLogMessage(record.type, context, Message(record)).toUtf8() + '\n';
}
Writer::Writer(WriterPorts ports) : m_ports(std::move(ports))
{
    if (!m_ports.stderr_write || !m_ports.stderr_flush || !m_ports.core_debug
        || !m_ports.core_info || !m_ports.benign_font_warning)
        throw std::invalid_argument{"Missing Qt log writer port"};
}
bool Writer::operator()(const Record& record) const
{
    if (record.overflow || (record.stage == Stage::Startup && record.type != QtDebugMsg)) {
        const auto formatted = FormatStderr(record);
        const auto written = m_ports.stderr_write(formatted);
        const auto flushed = m_ports.stderr_flush();
        return written == static_cast<size_t>(formatted.size()) && flushed;
    }
    const auto message = Message(record);
    if (record.type == QtDebugMsg || (record.type == QtWarningMsg && m_ports.benign_font_warning(message)))
        m_ports.core_debug(message);
    else m_ports.core_info(message);
    return true;
}
int FinalExitStatus(int original_status, const Receipt& receipt)
{
    return original_status != EXIT_SUCCESS ? original_status : receipt.failed || receipt.rejected ? EXIT_FAILURE : EXIT_SUCCESS;
}
Queue::Queue(Wake wake, Write write, Limits limits) : m_wake(std::move(wake)), m_write(std::move(write)), m_limits(limits)
{
    if (!m_wake || !m_write || !limits.messages || limits.bytes < sizeof(Record)) throw std::invalid_argument("Invalid Qt log ports or capacity");
}
Record Queue::capture(QtMsgType type, const QMessageLogContext& context, const QString& message, Stage stage)
{
    Record record; record.type = type; record.stage = stage; record.message = message; record.line = context.line;
    record.has_file = context.file != nullptr; record.has_function = context.function != nullptr; record.has_category = context.category != nullptr;
    const auto copy = [&](auto& output, const char* input) {
        if (!input) return;
        size_t i{0}; for (; i + 1 < output.size() && input[i]; ++i) output[i] = input[i];
        if (i + 1 == output.size() && input[i]) record.context_truncated = true;
    };
    copy(record.file, context.file); copy(record.function, context.function); copy(record.category, context.category);
    return record;
}
bool Queue::enqueue(QtMsgType type, const QMessageLogContext& context, const QString& message, Stage stage)
{
    bool accepted{false}, wake{false};
    {
        std::lock_guard lock{m_mutex};
        if (m_receipt.closed) { ++m_receipt.rejected; return false; } // A late caller still owns a real rejection.
        const auto sequence = ++m_sequence;
        const auto text_bytes = static_cast<size_t>(message.size()) * sizeof(QChar);
        const auto room = m_limits.bytes - m_bytes;
        if (!m_closing && m_messages.size() < m_limits.messages && room >= sizeof(Record) && text_bytes <= room - sizeof(Record)) {
            auto record = capture(type, context, message, stage); record.sequence = sequence;
            m_messages.push_back(std::move(record)); m_bytes += text_bytes + sizeof(Record); ++m_receipt.accepted; accepted = true;
        } else {
            ++m_receipt.rejected;
            if (!m_overflow) { m_overflow = Record{}; m_overflow->sequence = sequence; m_overflow->stage = stage; m_overflow->type = QtWarningMsg; m_overflow->overflow = true; }
            ++m_overflow->rejected; m_overflow->rejected_bytes += text_bytes;
        }
        if (!m_scheduled) { m_scheduled = true; wake = true; }
    }
    if (wake) m_wake();
    return accepted;
}
void Queue::drain()
{
    for (int budget = 0; budget < 16; ++budget) {
        std::optional<Record> record;
        {
            std::lock_guard lock{m_mutex};
            if (m_overflow && (m_messages.empty() || m_overflow->sequence < m_messages.front().sequence)) record = std::exchange(m_overflow, {});
            else if (!m_messages.empty()) {
                record = std::move(m_messages.front()); m_messages.pop_front();
                m_bytes -= static_cast<size_t>(record->message.size()) * sizeof(QChar) + sizeof(Record);
            }
            if (!record) { m_scheduled = false; if (m_closing) m_receipt.closed = true; return; }
        }
        bool written{false};
        try { written = m_write(*record); } catch (...) { /* Receipt owns the actual write failure. */ }
        { std::lock_guard lock{m_mutex}; if (written) ++m_receipt.written; else ++m_receipt.failed; }
    }
    bool again;
    {
        std::lock_guard lock{m_mutex}; again = !m_messages.empty() || m_overflow.has_value();
        if (!again) { m_scheduled = false; if (m_closing) m_receipt.closed = true; }
    }
    if (again) m_wake();
}
void Queue::close()
{
    bool wake{false};
    { std::lock_guard lock{m_mutex}; m_closing = true; if (!m_scheduled && !m_receipt.closed) { m_scheduled = true; wake = true; } }
    if (wake) m_wake();
}
Receipt Queue::receipt() const { std::lock_guard lock{m_mutex}; return m_receipt; }
struct NativeSink::Worker { std::mutex mutex; std::condition_variable ready; bool wake{false}; };
NativeSink::NativeSink(Queue::Write write, Queue::Limits limits) : m_worker(std::make_shared<Worker>())
{
    m_queue = std::make_shared<Queue>([worker = m_worker] {
        { std::lock_guard lock{worker->mutex}; worker->wake = true; } worker->ready.notify_one();
    }, std::move(write), limits);
    m_thread = std::thread([worker = m_worker, queue = m_queue] {
        for (;;) {
            { std::unique_lock lock{worker->mutex}; worker->ready.wait(lock, [&] { return worker->wake; }); worker->wake = false; }
            queue->drain(); if (queue->receipt().closed) return;
        }
    });
}
NativeSink::~NativeSink()
{
    if (!m_thread.joinable()) return;
    if (!QCoreApplication::instance()) finishAfterGui();
    else closeAsync({}, {});
}
void NativeSink::finishAfterGui()
{
    if (QCoreApplication::instance()) throw std::logic_error("Qt log process join requires GUI destruction first");
    m_queue->close(); if (m_thread.joinable()) m_thread.join();
}
void NativeSink::closeAsync(std::function<void(std::function<void()>)> post, std::function<void(Receipt)> done)
{
    if (!m_thread.joinable()) return;
    m_queue->close();
    std::thread([thread = std::move(m_thread), queue = m_queue, post = std::move(post), done = std::move(done)]() mutable {
        thread.join(); const auto result = queue->receipt();
        if (done && post) post([done = std::move(done), result] { done(result); });
    }).detach();
}
} // namespace qtlog
