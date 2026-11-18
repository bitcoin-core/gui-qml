// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PAYMENTIMPORTFLOW_H
#define BITCOIN_QML_PAYMENTIMPORTFLOW_H

#include <qml/walletopeningstate.h>

#include <cstdint>
#include <limits>
#include <optional>
#include <utility>

namespace paymentimport {
enum class Kind { Unknown, Payment, Psbt };
enum class Phase { Empty, Reading, Analyzing, Inspecting, AwaitingReplacement, Applying,
                   CreatingReview, Reconciling, Applied, ReviewOpened, Failed, Closing, Closed };
enum class Error { None, Source, Target, Rejected, Stale, Query, IdentityExhausted };
enum class ChildKind { Apply, CreateReview };
enum class Outcome { Applied, ReviewOpened, Rejected, Stale, Unknown };
enum class PromptReason { ConflictingIntent, ChangedRevision };

/** Facts supplied by the original target actor, independently of source reads.
 * A revision identifies intent, including same-value amount-mode changes. */
struct Target {
    walletopening::Opening opening;
    uint64_t draft{0}, revision{0}, recipient{0};
    bool available{false}, compatible{false}, conflicts{false};
    bool matches_request{false};
    bool operator==(const Target&) const = default;
};
struct Child {
    uint64_t id{0}, source{0};
    ChildKind kind{ChildKind::Apply};
    Target target;
    bool replace_existing{false};
    bool operator==(const Child&) const = default;
};
struct Prompt {
    uint64_t id{0}, source{0};
    Target target;
    PromptReason reason{PromptReason::ConflictingIntent};
    bool operator==(const Prompt&) const = default;
};
struct Receipt {
    Child child;
    Outcome outcome{Outcome::Unknown};
    uint64_t result{0}, revision{0};
};

/** Domain-confined M26 decisions. The asynchronous owner retains source values
 * and the original child's ledger with existing OwnedCommand/cleanup services.
 * A missing acknowledgment authorizes a query, never another mutation/factory. */
class Flow
{
public:
    Phase phase() const { return m_phase; }
    Kind kind() const { return m_kind; }
    Error error() const { return m_error; }
    uint64_t source() const { return m_source; }
    const std::optional<Target>& target() const { return m_target; }
    const std::optional<Prompt>& prompt() const { return m_prompt; }
    const std::optional<Child>& child() const { return m_child; }
    const std::optional<Receipt>& result() const { return m_result; }
    bool pending() const { return m_read_pending || bool(m_child); }
    bool canApply() const {
        return !m_child && m_phase == Phase::Inspecting && m_kind == Kind::Payment && usableTarget()
            && m_target->draft && m_target->recipient;
    }
    bool canReview() const {
        return !m_child && m_phase == Phase::Inspecting && m_kind == Kind::Psbt && usableTarget();
    }
    bool canExport() const {
        return m_kind == Kind::Psbt && (m_phase == Phase::Inspecting || m_phase == Phase::ReviewOpened ||
            m_phase == Phase::Failed);
    }
    bool canReconcile() const {
        return m_child && !m_query_pending && (m_phase == Phase::Reconciling || m_phase == Phase::Closing ||
            (m_result && m_result->outcome != Outcome::Unknown));
    }

    std::optional<uint64_t> inspect() {
        if ((m_phase != Phase::Empty && m_phase != Phase::Failed) || pending()) return {};
        const auto token = next(); if (!token) return {};
        m_source = *token; m_kind = Kind::Unknown; m_error = Error::None;
        m_prompt.reset(); m_result.reset(); m_read_pending = true; m_phase = Phase::Reading;
        return token;
    }
    bool parsed(uint64_t source, Kind kind) {
        if (source != m_source || !m_read_pending) return false;
        if (m_phase == Phase::Closing) { m_read_pending = false; settleClose(); return true; }
        if (m_phase != Phase::Reading) return false;
        if (kind == Kind::Unknown) return inspectionFailed(source);
        m_kind = kind;
        m_phase = kind == Kind::Psbt ? Phase::Analyzing : Phase::Inspecting;
        m_read_pending = kind == Kind::Psbt;
        return true;
    }
    bool analyzed(uint64_t source) {
        if (source != m_source || !m_read_pending) return false;
        if (m_phase == Phase::Closing) { m_read_pending = false; settleClose(); return true; }
        if (m_phase != Phase::Analyzing || m_kind != Kind::Psbt) return false;
        m_read_pending = false; m_phase = Phase::Inspecting;
        return true;
    }
    bool inspectionFailed(uint64_t source) {
        if (source != m_source || !m_read_pending) return false;
        m_read_pending = false;
        if (m_phase == Phase::Closing) settleClose();
        else { m_phase = Phase::Failed; m_error = Error::Source; }
        return true;
    }
    bool chooseTarget(std::optional<Target> target) {
        if (m_child || m_closing || (m_phase != Phase::Empty && m_phase != Phase::Reading &&
            m_phase != Phase::Analyzing && m_phase != Phase::Inspecting &&
            m_phase != Phase::AwaitingReplacement && m_phase != Phase::Failed)) return false;
        m_target = std::move(target); m_prompt.reset();
        if (m_phase == Phase::AwaitingReplacement) m_phase = Phase::Inspecting;
        return true;
    }
    std::optional<Child> apply() {
        if (!canApply()) return {};
        if (m_target->conflicts) { requireReplacement(); return {}; }
        return beginChild(ChildKind::Apply);
    }
    std::optional<Child> replace(uint64_t prompt) {
        if (m_phase != Phase::AwaitingReplacement || !m_prompt || m_prompt->id != prompt ||
            m_prompt->source != m_source || !m_target || m_prompt->target != *m_target || !usableTarget()) return {};
        return beginChild(ChildKind::Apply, true);
    }
    bool keepDraft(uint64_t prompt) {
        if (m_phase != Phase::AwaitingReplacement || !m_prompt || m_prompt->id != prompt) return false;
        m_prompt.reset(); m_phase = Phase::Inspecting;
        return true;
    }
    std::optional<Child> openReview() {
        return canReview() ? beginChild(ChildKind::CreateReview) : std::nullopt;
    }
    bool acknowledgmentLost(const Child& child) {
        if (m_child && *m_child == child && m_result && m_result->outcome != Outcome::Unknown) {
            m_error = Error::Query;
            return true;
        }
        if (!m_child || *m_child != child || (m_phase != Phase::Applying &&
            m_phase != Phase::CreatingReview && m_phase != Phase::Closing && m_phase != Phase::Reconciling)) return false;
        m_query_pending = false;
        if (!m_closing) m_phase = Phase::Reconciling;
        return true;
    }
    bool beginQuery() {
        if (!canReconcile()) return false;
        m_query_pending = true; m_error = Error::None;
        return true;
    }
    bool queryUnavailable(const Child& child) {
        if (!m_child || *m_child != child || !m_query_pending) return false;
        m_query_pending = false; m_error = Error::Query;
        return true;
    }
    bool finished(Receipt receipt, std::optional<Target> revised_target = {}) {
        if (!m_child || receipt.child != *m_child) return false;
        // A late callback/query cannot rewrite an already established effect.
        if (m_result && m_result->outcome != Outcome::Unknown) return false;
        if ((receipt.outcome == Outcome::Applied && receipt.child.kind != ChildKind::Apply) ||
            (receipt.outcome == Outcome::ReviewOpened && receipt.child.kind != ChildKind::CreateReview)) return false;
        if ((receipt.outcome == Outcome::Applied || receipt.outcome == Outcome::ReviewOpened) && !receipt.result) return false;
        m_query_pending = false;
        if (receipt.outcome == Outcome::Unknown) {
            m_result = std::move(receipt);
            if (!m_closing) m_phase = Phase::Reconciling;
            return true;
        }
        const auto original = *m_child;
        m_result = std::move(receipt); m_error = Error::None;
        if (m_closing) { settleClose(); return true; }
        switch (m_result->outcome) {
        case Outcome::Applied: m_phase = Phase::Applied; break;
        case Outcome::ReviewOpened: m_phase = Phase::ReviewOpened; break;
        case Outcome::Rejected: m_phase = Phase::Failed; m_error = Error::Rejected; break;
        case Outcome::Stale:
            m_error = Error::Stale; m_phase = Phase::Inspecting;
            if (revised_target && revised_target->opening == original.target.opening &&
                revised_target->draft == original.target.draft && revised_target->recipient == original.target.recipient) {
                m_target = std::move(revised_target);
                if (m_kind == Kind::Payment && usableTarget()) requireReplacement(PromptReason::ChangedRevision);
            } else if (m_target) m_target->available = false;
            break;
        case Outcome::Unknown: break;
        }
        return true;
    }
    bool acknowledged(const Child& child) {
        if (!m_child || *m_child != child || !m_result || m_result->outcome == Outcome::Unknown) return false;
        m_child.reset();
        if (m_error == Error::Query) m_error = m_result->outcome == Outcome::Rejected ? Error::Rejected
            : m_result->outcome == Outcome::Stale ? Error::Stale : Error::None;
        settleClose();
        return true;
    }
    void close() {
        if (m_phase == Phase::Closed) return;
        m_closing = true; m_prompt.reset(); m_phase = Phase::Closing;
        settleClose();
    }

private:
    bool usableTarget() const { return m_target && m_target->opening && m_target->available && m_target->compatible; }
    std::optional<uint64_t> next() {
        if (m_sequence == std::numeric_limits<uint64_t>::max()) {
            m_error = Error::IdentityExhausted; m_phase = Phase::Failed; return {};
        }
        return ++m_sequence;
    }
    void requireReplacement(PromptReason reason = PromptReason::ConflictingIntent) {
        const auto token = next(); if (!token || !m_target) return;
        m_prompt = Prompt{*token, m_source, *m_target, reason}; m_phase = Phase::AwaitingReplacement;
    }
    std::optional<Child> beginChild(ChildKind kind, bool replace_existing = false) {
        if (m_child) return {};
        const auto token = next(); if (!token || !m_target) return {};
        m_child = Child{*token, m_source, kind, *m_target, replace_existing}; m_prompt.reset(); m_result.reset();
        m_phase = kind == ChildKind::Apply ? Phase::Applying : Phase::CreatingReview;
        return m_child;
    }
    void settleClose() { if (m_closing && !pending()) m_phase = Phase::Closed; }
    Phase m_phase{Phase::Empty};
    Kind m_kind{Kind::Unknown};
    Error m_error{Error::None};
    uint64_t m_sequence{0}, m_source{0};
    bool m_read_pending{false}, m_query_pending{false}, m_closing{false};
    std::optional<Target> m_target;
    std::optional<Prompt> m_prompt;
    std::optional<Child> m_child;
    std::optional<Receipt> m_result;
};
} // namespace paymentimport
#endif // BITCOIN_QML_PAYMENTIMPORTFLOW_H
