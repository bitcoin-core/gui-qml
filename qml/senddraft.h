// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SENDDRAFT_H
#define BITCOIN_QML_SENDDRAFT_H

#include <addresstype.h>
#include <consensus/amount.h>
#include <QString>
#include <QtGlobal>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace senddraft {
enum class AmountMode { Fixed, Maximum };
enum class State { Empty, Editing, Ready, Retired };
struct Limits {
    int recipients{25};
    static constexpr qsizetype note{256}, message{2048};
    static constexpr size_t pending_modes{128}, pending_recipients{128};
};
struct PaymentRequest {
    QString address, label, message, private_note;
    std::optional<CAmount> amount;
    bool replace_private_note{false};
};
struct Recipient {
    quint64 token{0}, revision{0}, amount_intent_revision{0};
    QString address, label, message, request_label, request_address, address_error, amount_error, input_error;
    AmountMode mode{AmountMode::Fixed};
    bool clear_amount{false};
    CAmount amount{0};
    CTxDestination destination;
    bool valid{false}, address_valid{false}, input_acceptable{true}, payment_request{false}, validation_pending{true}, amount_set{false};
};
struct AppliedImport {
    quint64 owner{0}, child{0}, source{0}, expected_intent{0}, applied_intent{0};
    Recipient recipient;
    QString formatted_address;
    bool amount_changed{false}, private_note_changed{false};
};
struct Snapshot {
    std::vector<Recipient> recipients;
    std::map<quint64, size_t> positions;
    quint64 revision{0}, intent_revision{0}, maximum{0};
    State state{State::Empty};
    CAmount total{0};
    QString total_text, error;
    int invalid_count{0}, pending_count{0};
    bool all_valid{false}, duplicate_destinations{false}, amount_total_valid{true};
    std::shared_ptr<const AppliedImport> applied_import;
};

// Confined to the Domain lane. GUI editors submit values and explicit mode
// events; the projection never decides mode from numeric equality. A late
// validation echo cannot restore the computed remainder cleared at handover.
class Draft {
public:
    struct InputRevision { quint64 validation{0}, amount_intent{0}; };
    using InputFence = std::map<quint64, InputRevision>;
    explicit Draft(Limits limits = {}) : m_limits(limits) {}
    const std::map<quint64, Recipient>& recipients() const { return m_recipients; }
    quint64 maximum() const { return m_maximum; }
    void clear() { m_recipients.clear(); m_cleared.clear(); m_intents.clear(); m_maximum = 0; }
    void remove(quint64 token) {
        m_recipients.erase(token); m_cleared.erase(token); m_intents.erase(token);
        if (m_maximum == token) m_maximum = 0;
    }
    bool update(Recipient value) {
        const auto token = value.token;
        if (!token || (!m_recipients.contains(token) && m_recipients.size() >= static_cast<size_t>(m_limits.recipients))) return false;
        if (const auto found = m_recipients.find(token); found != m_recipients.end() && found->second.revision > value.revision) return false;
        if (value.amount_intent_revision > m_intents[token]) {
            m_intents[token] = value.amount_intent_revision;
            m_cleared.erase(token); // Only an explicit new amount may replace the cleared remainder.
            if (m_maximum == token) m_maximum = 0;
        }
        value.mode = token == m_maximum ? AmountMode::Maximum : AmountMode::Fixed;
        if (const auto cleared = m_cleared.find(token); cleared != m_cleared.end()) {
            if (value.revision > cleared->second && !value.amount_set && value.amount == 0) m_cleared.erase(cleared);
            else clearAmount(value);
        }
        m_recipients.insert_or_assign(token, std::move(value)); return true;
    }
    bool choose(quint64 token, AmountMode mode, quint64 amount_intent_revision, const InputFence& inputs) {
        auto target = m_recipients.find(token);
        if (target == m_recipients.end()) return false;
        m_intents[token] = std::max(m_intents[token], amount_intent_revision);
        if (mode == AmountMode::Fixed) {
            target->second.mode = mode;
            if (m_maximum == token) m_maximum = 0;
            return true;
        }
        if (m_maximum && m_maximum != token) {
            auto& previous = m_recipients.at(m_maximum);
            const auto submitted = inputs.find(m_maximum);
            const auto input = submitted == inputs.end() ? InputRevision{previous.revision, previous.amount_intent_revision} : submitted->second;
            previous.mode = AmountMode::Fixed;
            if (input.amount_intent > m_intents[m_maximum]) {
                // A Fixed edit captured before this handover wins even when
                // its replaceable validation echo is delivered after the mode.
                m_intents[m_maximum] = input.amount_intent; m_cleared.erase(m_maximum);
            } else {
                m_cleared.insert_or_assign(m_maximum, std::max(previous.revision, input.validation));
                clearAmount(previous);
            }
        }
        m_cleared.erase(token); target->second.clear_amount = false;
        m_maximum = token; target->second.mode = mode; return true;
    }
private:
    static void clearAmount(Recipient& value) {
        value.amount = 0; value.amount_set = false; value.valid = false;
        value.clear_amount = true; value.validation_pending = true;
    }
    Limits m_limits;
    quint64 m_maximum{0};
    std::map<quint64, Recipient> m_recipients;
    std::map<quint64, quint64> m_cleared, m_intents;
};

// Run with the captured immutable draft on the background executor. The
// maximum recipient's previous amount does not participate in its replacement.
inline std::optional<CAmount> GrossRemainder(const Snapshot& draft, quint64 maximum, CAmount available)
{
    if (!maximum || !MoneyRange(available) || !draft.positions.contains(maximum)) return std::nullopt;
    CAmount other{0};
    for (const auto& recipient : draft.recipients) {
        if (recipient.token == maximum) continue;
        if (!recipient.valid || recipient.validation_pending || !MoneyRange(recipient.amount) || recipient.amount > MAX_MONEY - other) return std::nullopt;
        other += recipient.amount;
    }
    return std::max<CAmount>(0, available - other);
}
} // namespace senddraft
#endif // BITCOIN_QML_SENDDRAFT_H
