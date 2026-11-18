// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/runtimeprompt.h>

#include <node/interface_ui.h>

#include <algorithm>
#include <bit>
#include <limits>
#include <utility>

namespace runtimeprompt {
namespace {
uint32_t Buttons(uint32_t style)
{
    const auto buttons = style & CClientUIInterface::BTN_MASK;
    return buttons ? buttons : uint32_t{CClientUIInterface::BTN_OK};
}

bool Allowed(uint32_t button, uint32_t buttons)
{
    return std::has_single_bit(button) && (button & buttons) == button;
}
} // namespace

bool Answer::affirmative() const
{
    return completion == Completion::Answered && button == CClientUIInterface::BTN_OK;
}

AnswerCell::AnswerCell(uint32_t style) : m_buttons(Buttons(style)) {}

bool AnswerCell::bind(Id id)
{
    std::lock_guard lock{m_mutex};
    if (!id || m_id || m_result) return false;
    m_id = id;
    return true;
}

bool AnswerCell::bound() const
{
    std::lock_guard lock{m_mutex};
    return m_id != 0;
}

bool AnswerCell::complete(Completion completion, uint32_t button)
{
    {
        std::lock_guard lock{m_mutex};
        if (m_result) return false;
        m_result = Answer{m_id, completion, button};
    }
    m_condition.notify_all();
    return true;
}

Disposition AnswerCell::answer(uint32_t button)
{
    if (!Allowed(button, m_buttons)) return Disposition::InvalidButton;
    return complete(Completion::Answered, button) ? Disposition::Accepted : Disposition::AlreadyCompleted;
}

bool AnswerCell::cancel()
{
    return complete(Completion::Cancelled, CClientUIInterface::BTN_CANCEL);
}

std::optional<Answer> AnswerCell::result() const
{
    std::lock_guard lock{m_mutex};
    return m_result;
}

Answer AnswerCell::wait()
{
    std::unique_lock lock{m_mutex};
    m_condition.wait(lock, [this] { return m_result.has_value(); });
    return *m_result;
}

Request::Request(Origin request_origin, uint32_t request_style, bool request_question, QString request_summary,
                 std::shared_ptr<const QString> request_details, bool complete_diagnostics)
    : origin(request_origin), style(request_style), question(request_question), summary(std::move(request_summary)),
      details(std::move(request_details)), diagnostics_complete(complete_diagnostics),
      answer(std::make_shared<AnswerCell>(style))
{
}

Queue::Queue(Limits limits) : m_limits(limits) {}
Queue::~Queue() { if(!m_entries.empty())close(); }

bool Queue::closed() const
{
    return m_state == State::Stopping || m_state == State::Stopped;
}

std::shared_ptr<const Prompt> Queue::active() const
{
    if (closed() || m_entries.empty()) return {};
    return m_entries.front().prompt;
}

Admission Queue::enqueue(std::shared_ptr<const Request> request)
{
    // A duplicate intake must never cancel a request already owned by this or
    // another queue. A pre-completed request has no new continuation to admit.
    if (!request || request->answer->bound() || request->answer->result()) return {Disposition::InvalidRequest, 0};
    const auto reject = [&](Disposition reason) {
        request->answer->cancel();
        return Admission{reason, 0};
    };
    if (closed()) return reject(Disposition::Closed);
    if (m_entries.size() >= m_limits.prompts) return reject(Disposition::Overflow);
    const auto summary_characters = static_cast<size_t>(request->summary.size());
    if (summary_characters > MAX_SUMMARY_CHARACTERS) return reject(Disposition::InvalidRequest);
    const auto details_characters = request->details ? static_cast<size_t>(request->details->size()) : size_t{0};
    if (details_characters > m_limits.diagnostic_characters
        || summary_characters > m_limits.diagnostic_characters - details_characters) return reject(Disposition::DiagnosticCapacity);
    const auto characters = summary_characters + details_characters;
    if (characters > m_limits.diagnostic_characters - m_retained_characters) return reject(Disposition::DiagnosticCapacity);
    if (!m_next_id) return reject(Disposition::IdExhausted);

    const auto id = m_next_id;
    const auto prompt = std::make_shared<const Prompt>(Prompt{
        {id, request->origin, request->style, request->answer->buttons(), request->question, request->diagnostics_complete}, request});
    m_entries.push_back({prompt, characters});
    if (!request->answer->bind(id)) {
        m_entries.pop_back();
        return {Disposition::InvalidRequest, 0};
    }
    m_next_id = id == std::numeric_limits<Id>::max() ? 0 : id + 1;
    m_retained_characters += characters;
    if (m_state == State::Idle) m_state = State::Presenting;
    return {Disposition::Accepted, id};
}

Disposition Queue::current(Id id) const
{
    if (closed()) return Disposition::Closed;
    if (m_entries.empty() || id != m_entries.front().prompt->descriptor.id) return Disposition::StaleToken;
    return Disposition::Accepted;
}

Disposition Queue::visible(Id id)
{
    const auto check = current(id);
    if (check != Disposition::Accepted) return check;
    if (m_state != State::Presenting && m_state != State::AwaitingAnswer) return Disposition::NotReady;
    if (m_entries.front().prompt->request->answer->result()) {
        m_state = State::Completing;
        return Disposition::AlreadyCompleted;
    }
    m_state = State::AwaitingAnswer;
    return Disposition::Accepted;
}

Disposition Queue::answer(Id id, uint32_t button)
{
    const auto check = current(id);
    if (check != Disposition::Accepted) return check;
    if (m_state != State::AwaitingAnswer) return Disposition::NotReady;
    const auto outcome = m_entries.front().prompt->request->answer->answer(button);
    if (outcome == Disposition::Accepted || outcome == Disposition::AlreadyCompleted) m_state = State::Completing;
    return outcome;
}

Disposition Queue::dismiss(Id id)
{
    const auto check = current(id);
    if (check != Disposition::Accepted) return check;
    if (m_state != State::Presenting && m_state != State::AwaitingAnswer) return Disposition::AlreadyCompleted;
    const bool canceled = m_entries.front().prompt->request->answer->cancel();
    m_state = State::Completing;
    return canceled ? Disposition::Accepted : Disposition::AlreadyCompleted;
}

Disposition Queue::unavailable(Id id)
{
    const auto check = current(id);
    if (check != Disposition::Accepted) return check;
    if (m_state != State::Presenting) return Disposition::NotReady;
    return dismiss(id);
}

Disposition Queue::delivered(Id id)
{
    if (m_state == State::Stopped) return Disposition::Closed;
    auto entry = std::find_if(m_entries.begin(), m_entries.end(), [id](const auto& value) { return value.prompt->descriptor.id == id; });
    if (entry == m_entries.end()) return Disposition::StaleToken;
    if (!entry->prompt->request->answer->result()) return Disposition::NotReady;
    const bool was_active = entry == m_entries.begin();
    m_retained_characters -= entry->characters;
    m_entries.erase(entry);
    if (m_state == State::Stopping) {
        if (m_entries.empty()) m_state = State::Stopped;
    } else if (was_active) {
        m_state = m_entries.empty() ? State::Idle : State::Presenting;
    }
    return Disposition::Accepted;
}

void Queue::close()
{
    if (closed()) return;
    m_state = State::Stopping;
    for (const auto& entry : m_entries) entry.prompt->request->answer->cancel();
    if (m_entries.empty()) m_state = State::Stopped;
}

} // namespace runtimeprompt
