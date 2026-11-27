// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_QTLOGSINK_H
#define BITCOIN_QML_QTLOGSINK_H
#include <QString>
#include <QByteArray>
#include <QVersionNumber>
#include <QLoggingCategory>
#include <array>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

namespace qtlog {
enum class Stage { Startup, Main };
// Fatal-only emergency process handoff: Qt aborts immediately after the handler
// returns. Preserve its full stderr diagnostic without running further GUI work.
void EmergencyFatal(const QMessageLogContext& context, const QString& message, QtMsgType type = QtFatalMsg);
class FatalPolicy final {
public:
    struct Decision { std::unique_lock<std::mutex> ordering; bool terminating{false}, already_fatal{false}; };
    FatalPolicy(int warnings, int criticals) : m_warnings(warnings), m_criticals(criticals) {}
    static int parseCounter(const QByteArray& value, const QVersionNumber& runtime);
    Decision enter(QtMsgType type);
private:
    std::mutex m_mutex;
    int m_warnings, m_criticals;
    bool m_fatal_started{false};
};
std::shared_ptr<FatalPolicy> CaptureFatalPolicy();
struct Record {
    QtMsgType type{QtDebugMsg};
    Stage stage{Stage::Startup};
    QString message;
    std::array<char, 256> file{}, function{}, category{};
    int line{0};
    quint64 sequence{0};
    bool has_file{false}, has_function{false}, has_category{false};
    bool context_truncated{false}, overflow{false};
    quint64 rejected{0}, rejected_bytes{0};
};
QString Message(const Record& record);
QByteArray FormatStderr(const Record& record);
// Native output ports. Stdio acknowledgement includes both the exact write
// length and flush result; Core's void ports acknowledge handoff only.
struct WriterPorts {
    std::function<size_t(const QByteArray&)> stderr_write;
    std::function<bool()> stderr_flush;
    std::function<void(const QString&)> core_debug, core_info;
    std::function<bool(const QString&)> benign_font_warning;
};
class Writer final {
public:
    explicit Writer(WriterPorts ports);
    bool operator()(const Record& record) const;
private:
    WriterPorts m_ports;
};
struct Receipt {
    // written counts successful write-port acknowledgements. Core's void
    // logging API only acknowledges handoff, not debug.log durability/status.
    quint64 accepted{0}, written{0}, rejected{0}, failed{0};
    bool closed{false};
};
int FinalExitStatus(int original_status, const Receipt& receipt);
/** One bounded mailbox. Wake only schedules a Native drain turn; write and
 * formatting are never invoked by GUI admission/close. Wake is a retained,
 * non-rejecting native wake port (a condition-variable notification in production).
 * Tests control both. */
class Queue final {
public:
    using Wake = std::function<void()>;
    using Write = std::function<bool(const Record&)>;
    struct Limits { size_t messages{256}, bytes{4 * 1024 * 1024}; };
    Queue(Wake wake, Write write, Limits limits);
    bool enqueue(QtMsgType type, const QMessageLogContext& context, const QString& message, Stage stage);
    void drain(); // Native: at most16 writes per turn.
    void close();
    Receipt receipt() const;
private:
    static Record capture(QtMsgType type, const QMessageLogContext& context, const QString& message, Stage stage);
    const Wake m_wake;
    const Write m_write;
    const Limits m_limits;
    mutable std::mutex m_mutex;
    std::deque<Record> m_messages;
    size_t m_bytes{0};
    quint64 m_sequence{0};
    std::optional<Record> m_overflow;
    Receipt m_receipt;
    bool m_scheduled{false}, m_closing{false};
};

// Keep fatal ordering through the ordinary bounded enqueue. Configured fatal
// counters terminate inside the handler, before Qt's private post-handler
// counter can choose an unflushed message on a different emitting thread.
void Forward(QtMsgType type, const QMessageLogContext& context, const QString& message, Stage stage,
             const std::shared_ptr<FatalPolicy>& policy, const std::shared_ptr<Queue>& queue);

/** One process-owned writer, intentionally outside the model executor registry.
 * It remains open through all producer/interface cleanup and QApplication's
 * destructor. finishAfterGui() is a process handoff after GUI lifetime ends.
 * Tests may closeAsync() and observe its join without waiting on GUI. */
class NativeSink final {
public:
    explicit NativeSink(Queue::Write write, Queue::Limits limits = {256, 4 * 1024 * 1024});
    ~NativeSink();
    std::shared_ptr<Queue> queue() const { return m_queue; }
    void finishAfterGui();
    void closeAsync(std::function<void(std::function<void()>)> post, std::function<void(Receipt)> done);
private:
    struct Worker;
    std::shared_ptr<Worker> m_worker;
    std::shared_ptr<Queue> m_queue;
    std::thread m_thread;
};
} // namespace qtlog
#endif // BITCOIN_QML_QTLOGSINK_H
