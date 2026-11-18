// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETOPENINGSTATE_H
#define BITCOIN_QML_WALLETOPENINGSTATE_H

#include <cstdint>
#include <memory>
#include <optional>

namespace walletopening {
struct Identity {};
using Opening = std::shared_ptr<const Identity>;
enum class Phase { Open, Closing, Closed };
struct Question {
    Opening opening;
    uint64_t number;
    uint64_t consequences;
};
enum class Answer { Ignored, Dismissed, Changed, Close };

/** Deterministic opening decisions, consumed only by the domain owner. */
class State
{
public:
    explicit State(Opening opening) : m_opening(std::move(opening)) {}
    Phase phase() const { return m_phase; }
    const std::optional<Question>& question() const { return m_question; }
    bool requestClose(uint64_t consequences)
    {
        if (m_phase != Phase::Open) return false;
        m_question = Question{m_opening, ++m_next_question, consequences};
        return true;
    }
    Answer answer(const Question& question, uint64_t consequences, bool accept)
    {
        if (m_phase != Phase::Open || !m_question || question.opening != m_opening || question.number != m_question->number) return Answer::Ignored;
        if (!accept) { m_question.reset(); return Answer::Dismissed; }
        if (question.consequences != consequences) {
            requestClose(consequences);
            return Answer::Changed;
        }
        retire();
        return Answer::Close;
    }
    void retire()
    {
        m_phase = Phase::Closing;
        m_question.reset();
    }
private:
    const Opening m_opening;
    Phase m_phase{Phase::Open};
    uint64_t m_next_question{0};
    std::optional<Question> m_question;
};
} // namespace walletopening

#endif // BITCOIN_QML_WALLETOPENINGSTATE_H
