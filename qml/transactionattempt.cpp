// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/transactionattempt.h>
#include <qml/walletprepare.h>

#include <limits>
#include <utility>

namespace transactionattempt {

Ticket Preparation::begin(SessionId session, ReviewId current_review, std::uint64_t requested_id)
{
    if (m_phase == Phase::Closed || m_id == std::numeric_limits<std::uint64_t>::max()) return {};
    if (requested_id == 0) requested_id = m_id + 1;
    if (requested_id <= m_id) return {};
    m_id = requested_id;
    m_ticket = {m_id, m_intent_revision, session, current_review};
    m_phase = Phase::Preparing;
    return m_ticket;
}

void Preparation::intentChanged()
{
    if (m_intent_revision == std::numeric_limits<std::uint64_t>::max()) return;
    intentChanged(m_intent_revision + 1);
}

void Preparation::intentChanged(std::uint64_t revision)
{
    if (revision <= m_intent_revision) return;
    m_intent_revision = revision;
    if (m_phase == Phase::Reviewing && m_candidate_revision != m_intent_revision) {
        m_phase = Phase::NeedsRevalidation;
    }
}

void Preparation::reviewReplaced(ReviewId current_review)
{
    if (m_phase == Phase::Reviewing && m_candidate && m_candidate_review != current_review) {
        m_phase = Phase::NeedsRevalidation;
    }
}

Transition Preparation::assessPending(const Ticket& ticket, CurrentFacts facts)
{
    if (m_phase != Phase::Preparing || ticket.id == 0 || ticket != m_ticket) return Transition::Ignored;
    if (!facts.session_open) return Transition::Ignored;
    if (ticket.intent_revision != m_intent_revision || ticket.session != facts.session ||
        ticket.prior_review != facts.review) {
        m_phase = Phase::NeedsRevalidation;
        return Transition::Stale;
    }
    return Transition::Accepted;
}

Transition Preparation::assess(const Ticket& ticket, CurrentFacts facts)
{
    return assessPending(ticket, facts);
}

bool Preparation::matches(const Ticket& ticket, CurrentFacts facts) const
{
    return m_phase == Phase::Preparing && ticket.id != 0 && ticket == m_ticket &&
        facts.session_open && ticket.intent_revision == m_intent_revision &&
        ticket.session == facts.session && ticket.prior_review == facts.review;
}

Transition Preparation::install(const Ticket& ticket, CurrentFacts facts,
                                std::shared_ptr<const walletreview::Snapshot> candidate,
                                ReviewId installed_review)
{
    const auto result = assessPending(ticket, facts);
    if (result != Transition::Accepted) return result;
    if (!candidate) {
        m_phase = Phase::Failed;
        return Transition::Ignored;
    }
    m_candidate = std::move(candidate);
    m_candidate_revision = ticket.intent_revision;
    m_candidate_review = installed_review;
    m_phase = Phase::Reviewing;
    return Transition::Accepted;
}

Transition Preparation::fail(const Ticket& ticket, CurrentFacts facts, Phase outcome)
{
    const auto result = assessPending(ticket, facts);
    if (result != Transition::Accepted) return result;
    if (outcome != Phase::NeedsUnlock && outcome != Phase::Failed && outcome != Phase::Uncertain) return Transition::Ignored;
    m_phase = outcome;
    return Transition::Accepted;
}

Transition Preparation::reject(const Ticket& ticket, Phase outcome)
{
    if (m_phase != Phase::Preparing || ticket.id == 0 || ticket != m_ticket) return Transition::Ignored;
    if (outcome == Phase::Preparing || outcome == Phase::Reviewing || outcome == Phase::Idle) return Transition::Ignored;
    m_phase = outcome;
    return Transition::Accepted;
}

Transition Preparation::discard(const Ticket& ticket, Phase outcome)
{
    if (m_phase != Phase::Reviewing || ticket.id == 0 || ticket != m_ticket ||
        (outcome != Phase::Failed && outcome != Phase::Uncertain)) return Transition::Ignored;
    m_candidate.reset();
    m_candidate_revision = 0;
    m_candidate_review = 0;
    m_phase = outcome;
    return Transition::Accepted;
}

Transition Preparation::advance(const Ticket& ticket, CurrentFacts facts, Phase expected, Phase outcome)
{
    if (ticket.id == 0 || ticket != m_ticket || m_phase != expected
        || facts.session != ticket.session || (expected != Phase::Submitting && !facts.session_open)
        || facts.review != m_candidate_review) return Transition::Ignored;
    const bool allowed =
        (expected == Phase::Reviewing && (outcome == Phase::Submitting || outcome == Phase::NeedsRevalidation || outcome == Phase::Failed || outcome == Phase::Uncertain))
        || (expected == Phase::NeedsUnlock && (outcome == Phase::Submitting || outcome == Phase::NeedsRevalidation || outcome == Phase::Failed))
        || (expected == Phase::Submitting && (outcome == Phase::Settled || outcome == Phase::NeedsUnlock || outcome == Phase::NeedsRevalidation || outcome == Phase::Failed || outcome == Phase::Uncertain));
    if (!allowed) return Transition::Ignored;
    m_phase = outcome;
    if (outcome == Phase::NeedsRevalidation || outcome == Phase::Failed) {
        m_candidate.reset();
        m_candidate_revision = 0;
        m_candidate_review = 0;
    }
    return Transition::Accepted;
}

void Preparation::close()
{
    m_candidate.reset();
    m_candidate_revision = 0;
    m_candidate_review = 0;
    m_ticket = {};
    m_phase = Phase::Closed;
}

bool Preparation::candidateCurrent(ReviewId review) const
{
    return m_phase != Phase::Closed && m_candidate &&
        m_candidate_revision == m_intent_revision && m_candidate_review == review;
}

Owner::Owner(std::shared_ptr<execution::Scheduler> scheduler)
    : m_scheduler(std::move(scheduler)), m_key(m_scheduler ? m_scheduler->allocateKey() : execution::ResourceKey{})
{
}

Snapshot Owner::project(State& state)
{
    Snapshot result;
    result.kind = state.kind;
    result.sequence = state.sequence;
    result.id = state.preparation.id();
    result.intent_revision = state.preparation.intentRevision();
    result.ticket = state.preparation.ticket();
    result.phase = state.preparation.phase();
    result.candidate = state.preparation.candidate();
    result.candidate_review = state.preparation.candidateReview();
    result.current_review = state.facts.review;
    result.candidate_current = state.preparation.candidateCurrent(state.facts.review);
    result.original_txid = state.original_txid;
    return result;
}

execution::Admission Owner::submit(const std::shared_ptr<State>& state,
                                   const std::shared_ptr<execution::Scheduler>& scheduler,
                                   execution::ResourceKey key, Mutation mutation, Completion done)
{
    if (!scheduler) return {{}, execution::Rejection::Closed};
    if (!done) done = [](Result) {};
    return execution::Submit(*scheduler,
        [state, mutation = std::move(mutation)]() mutable {
            auto result = mutation(*state);
            ++state->sequence;
            result.snapshot = project(*state);
            return result;
        }, std::move(done), {}, {execution::Lane::Domain, key, execution::WorkClass::Action});
}

execution::Admission Owner::begin(StartInput input, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [input = std::move(input)](State& state) mutable {
            if (input.intent_revision < state.preparation.intentRevision()) {
                return Result{Transition::Stale, {}, AdmissionError::StaleIntent};
            }
            if (state.preparation.phase() == Phase::Closed || input.id == 0 || input.id <= state.preparation.id()) {
                return Result{Transition::Ignored, {}, AdmissionError::Closed};
            }
            state.kind = input.kind;
            state.original_txid.clear();
            state.preparation.intentChanged(input.intent_revision);
            state.facts = input.facts;
            const auto ticket = state.preparation.begin(input.facts.session, input.facts.review, input.id);
            if (ticket.id == 0) return Result{Transition::Ignored, {}, AdmissionError::Closed};
            AdmissionError error{AdmissionError::None};
            if (!input.facts.session_open) error = AdmissionError::Closed;
            else if (!input.draft_current || !input.draft || input.draft->state != senddraft::State::Ready ||
                     !input.draft->all_valid || input.draft->pending_count || input.draft->duplicate_destinations ||
                     !input.draft->amount_total_valid) error = AdmissionError::InvalidDraft;
            else if (input.selection_needs_reconciliation) error = AdmissionError::SelectionNeedsReconciliation;
            else if (input.manual_selection && !input.has_selected_coins) error = AdmissionError::ManualSelectionEmpty;
            else if (input.custom_fee_enabled && !input.custom_fee_rate_valid) error = AdmissionError::CustomFeeInvalid;
            if (error != AdmissionError::None) {
                const auto phase = error == AdmissionError::Closed ? Phase::Closed : Phase::Failed;
                state.preparation.reject(ticket, phase);
            }
            return Result{Transition::Accepted, {}, error};
        }, std::move(done));
}

execution::Admission Owner::beginReplacement(ReplacementStart input, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [input = std::move(input)](State& state) mutable {
            if (input.intent_revision < state.preparation.intentRevision()) {
                return Result{Transition::Stale, {}, AdmissionError::StaleIntent};
            }
            if (state.preparation.phase() == Phase::Closed || input.id == 0 || input.id <= state.preparation.id()) {
                return Result{Transition::Ignored, {}, AdmissionError::Closed};
            }
            state.kind = Kind::Replacement;
            state.original_txid = input.original_txid;
            state.preparation.intentChanged(input.intent_revision);
            state.facts = input.facts;
            const auto ticket = state.preparation.begin(input.facts.session, input.facts.review, input.id);
            if (ticket.id == 0) return Result{Transition::Ignored, {}, AdmissionError::Closed};
            if (!input.facts.session_open) {
                state.preparation.reject(ticket, Phase::Closed);
                return Result{Transition::Accepted, {}, AdmissionError::Closed};
            }
            if (input.original_txid.size() != 64) {
                state.preparation.reject(ticket, Phase::Failed);
                return Result{Transition::Accepted, {}, AdmissionError::InvalidDraft};
            }
            return Result{Transition::Accepted};
        }, std::move(done));
}

execution::Admission Owner::intentChanged(std::uint64_t revision, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [revision](State& state) {
            state.preparation.intentChanged(revision);
            return Result{Transition::Accepted};
        }, std::move(done));
}

execution::Admission Owner::reviewReplaced(ReviewId current_review, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [current_review](State& state) {
            if (current_review < state.facts.review) return Result{Transition::Stale};
            state.facts.review = current_review;
            state.preparation.reviewReplaced(current_review);
            return Result{Transition::Accepted};
        }, std::move(done));
}

execution::Admission Owner::assess(Ticket ticket, CurrentFacts facts, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [ticket = std::move(ticket), facts = std::move(facts)](State& state) mutable {
            if (state.preparation.ticket() != ticket || state.preparation.phase() != Phase::Preparing)
                return Result{Transition::Ignored};
            state.facts = facts;
            return Result{state.preparation.assess(ticket, facts)};
        }, std::move(done));
}

execution::Admission Owner::install(Ticket ticket, CurrentFacts facts,
                                    std::shared_ptr<const walletreview::Snapshot> candidate,
                                    ReviewId installed_review, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [ticket = std::move(ticket), facts = std::move(facts), candidate = std::move(candidate), installed_review](State& state) mutable {
            if (state.preparation.ticket() != ticket || state.preparation.phase() != Phase::Preparing)
                return Result{Transition::Ignored};
            state.facts = facts;
            const auto transition = state.preparation.install(ticket, facts, std::move(candidate), installed_review);
            if (transition == Transition::Accepted) state.facts.review = installed_review;
            return Result{transition};
        }, std::move(done));
}

execution::Admission Owner::resolve(Ticket ticket, CurrentFacts facts, walletprepare::Error outcome,
                                    std::shared_ptr<const walletreview::Snapshot> candidate,
                                    ReviewId installed_review, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [ticket = std::move(ticket), facts = std::move(facts), outcome,
         candidate = std::move(candidate), installed_review](State& state) mutable {
            if (state.preparation.ticket() != ticket || state.preparation.phase() != Phase::Preparing)
                return Result{Transition::Ignored};
            state.facts = facts;
            if (outcome == walletprepare::Error::None && candidate) {
                const auto transition = state.preparation.install(ticket, facts, std::move(candidate), installed_review);
                if (transition == Transition::Accepted) state.facts.review = installed_review;
                return Result{transition};
            }
            const auto phase = outcome == walletprepare::Error::NeedsUnlock ? Phase::NeedsUnlock
                : outcome == walletprepare::Error::RelockFailed || outcome == walletprepare::Error::SweepUnknown ? Phase::Uncertain
                : Phase::Failed;
            return Result{state.preparation.fail(ticket, facts, phase)};
        }, std::move(done));
}

execution::Admission Owner::fail(Ticket ticket, CurrentFacts facts, Phase outcome, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [ticket = std::move(ticket), facts = std::move(facts), outcome](State& state) mutable {
            if (state.preparation.ticket() != ticket || state.preparation.phase() != Phase::Preparing)
                return Result{Transition::Ignored};
            state.facts = facts;
            return Result{state.preparation.fail(ticket, facts, outcome)};
        }, std::move(done));
}

execution::Admission Owner::reject(Ticket ticket, Phase outcome, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [ticket = std::move(ticket), outcome](State& state) mutable {
            return Result{state.preparation.reject(ticket, outcome)};
        }, std::move(done));
}

execution::Admission Owner::discard(Ticket ticket, Phase outcome, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [ticket = std::move(ticket), outcome](State& state) {
            return Result{state.preparation.discard(ticket, outcome)};
        }, std::move(done));
}

execution::Admission Owner::advance(Ticket ticket, CurrentFacts facts, Phase expected, Phase outcome, Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [ticket = std::move(ticket), facts = std::move(facts), expected, outcome](State& state) mutable {
            if (state.preparation.ticket() != ticket) return Result{Transition::Ignored};
            state.facts = facts;
            return Result{state.preparation.advance(ticket, facts, expected, outcome)};
        }, std::move(done));
}

execution::Admission Owner::close(Completion done)
{
    return submit(m_state, m_scheduler, m_key,
        [](State& state) { state.facts.session_open = false; state.preparation.close(); return Result{Transition::Accepted}; }, std::move(done));
}

} // namespace transactionattempt
