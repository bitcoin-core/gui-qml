// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_RUNTIMEPROMPT_H
#define BITCOIN_QML_RUNTIMEPROMPT_H

#include <QString>

#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <optional>

namespace runtimeprompt {

using Id = uint64_t;
// Existing presentation prefix plus bounded truncation/capacity annotations.
inline constexpr size_t MAX_SUMMARY_CHARACTERS{16'384 + 256};

enum class Origin { Startup, Runtime, StartupSummary };
enum class State { Idle, Presenting, AwaitingAnswer, Completing, Stopping, Stopped };
enum class Disposition {
    Accepted,
    Closed,
    Overflow,
    StaleToken,
    InvalidButton,
    NotReady,
    AlreadyCompleted,
    InvalidRequest,
    DiagnosticCapacity,
    IdExhausted,
};

enum class Completion { Answered, Cancelled };
struct Answer {
    Id id{0};
    Completion completion{Completion::Cancelled};
    uint32_t button{0};
    // Match Core/Widgets: only an explicitly answered BTN_OK means true.
    bool affirmative() const;
};

/** Shared with the original Native callback. Completion is exact-once, even
 * when answer and shutdown race. Only the callback's appropriate Native path
 * may wait(); Domain, GUI and the interruption lane must never wait for a user.
 */
class AnswerCell final
{
public:
    explicit AnswerCell(uint32_t style);
    uint32_t buttons() const { return m_buttons; }
    Disposition answer(uint32_t button);
    bool cancel();
    std::optional<Answer> result() const;
    Answer wait();

private:
    friend class Queue;
    bool bind(Id id);
    bool bound() const;
    bool complete(Completion completion, uint32_t button);

    const uint32_t m_buttons;
    mutable std::mutex m_mutex;
    std::condition_variable m_condition;
    Id m_id{0};
    std::optional<Answer> m_result;
};

/** Construct on Native after bounded diagnostic capture. Details must use the
 * existing retained diagnostic owner when physical retirement needs Cleanup.
 * The original callback retains this request through its wait/return; the
 * Domain queue independently retains it until completion is acknowledged.
 */
struct Request final {
    Request(Origin origin, uint32_t style, bool question, QString summary,
            std::shared_ptr<const QString> details = {}, bool diagnostics_complete = true);

    const Origin origin;
    const uint32_t style;
    const bool question;
    const QString summary;
    const std::shared_ptr<const QString> details;
    const bool diagnostics_complete;
    const std::shared_ptr<AnswerCell> answer;
};

// Scalar presentation descriptor; its containing Prompt is immutable.
struct Descriptor {
    Id id{0};
    Origin origin{Origin::Runtime};
    uint32_t style{0};
    uint32_t buttons{0};
    bool question{false};
    bool diagnostics_complete{true};
};
struct Prompt final {
    const Descriptor descriptor;
    const std::shared_ptr<const Request> request;
};

struct Limits {
    size_t prompts{32};
    // QString characters, including summaries and retained details. The
    // ingress/diagnostic owner separately bounds work awaiting Domain intake.
    // Native full text retains the existing 4M-character shared TextBudget. Account
    // separately for each bounded presentation prefix rather than rejecting
    // a valid full-capacity original message because its summary repeats it.
    size_t diagnostic_characters{4 * 1024 * 1024 + 32 * MAX_SUMMARY_CHARACTERS};
};
struct Admission {
    Disposition disposition{Disposition::InvalidRequest};
    Id id{0};
    explicit operator bool() const { return disposition == Disposition::Accepted; }
};

/** Single Domain-owned FIFO. No QObject, executor or callbacks are accessed.
 * Every decision belongs to that owner; only AnswerCell is
 * shared across threads. Destruction of a nonempty queue also belongs to
 * Domain. After all continuations acknowledge and the queue is empty, its
 * allocation may retire on Cleanup without another decision. Close cancels unanswered cells but retains original
 * requests until delivered(id) accounts for each continuation. A presentation
 * disappearance never serves as a continuation/drain acknowledgment.
 */
class Queue final
{
public:
    explicit Queue(Limits limits = {});
    ~Queue();
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;

    State state() const { return m_state; }
    size_t size() const { return m_entries.size(); }
    size_t retainedCharacters() const { return m_retained_characters; }
    std::shared_ptr<const Prompt> active() const;
    Admission enqueue(std::shared_ptr<const Request> request);
    Disposition visible(Id id);
    Disposition answer(Id id, uint32_t button);
    Disposition dismiss(Id id);
    Disposition unavailable(Id id);
    // Adapter acknowledgment: the original completion has been delivered or
    // independently retained by its continuation owner. A producer-canceled
    // queued entry can retire without replacing the active prompt. After
    // close, entries may acknowledge in Native completion order.
    Disposition delivered(Id id);
    void close();

private:
    struct Entry {
        std::shared_ptr<const Prompt> prompt;
        size_t characters;
    };
    bool closed() const;
    Disposition current(Id id) const;

    const Limits m_limits;
    State m_state{State::Idle};
    Id m_next_id{1};
    size_t m_retained_characters{0};
    std::deque<Entry> m_entries;
};

} // namespace runtimeprompt

#endif // BITCOIN_QML_RUNTIMEPROMPT_H
