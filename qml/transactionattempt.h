// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TRANSACTIONATTEMPT_H
#define BITCOIN_QML_TRANSACTIONATTEMPT_H

#include <qml/walletopeningstate.h>
#include <qml/walletreview.h>
#include <qml/senddraft.h>
#include <qml/execution.h>

#include <cstdint>
#include <functional>
#include <memory>

namespace walletprepare { enum class Error; }

namespace transactionattempt {

using SessionId = walletopening::Opening;
using ReviewId = std::uint64_t;

enum class Phase {
    Idle,
    Preparing,
    NeedsUnlock,
    Reviewing,
    NeedsRevalidation,
    Submitting,
    Settled,
    Failed,
    Closed,
    Uncertain,
};

enum class Kind { Payment, Replacement };

struct Ticket {
    std::uint64_t id{0};
    std::uint64_t intent_revision{0};
    SessionId session{};
    ReviewId prior_review{0};
    friend bool operator==(const Ticket&, const Ticket&) = default;
};

struct CurrentFacts {
    SessionId session{};
    ReviewId review{0};
    bool session_open{false};
};

enum class Transition { Accepted, Stale, Ignored };

// Immutable value returned by the serialized attempt owner for GUI
// projection. The owner mutates its Preparation only on the Domain lane.
struct Snapshot {
    Kind kind{Kind::Payment};
    std::uint64_t sequence{0};
    std::uint64_t id{0};
    std::uint64_t intent_revision{0};
    Ticket ticket{};
    Phase phase{Phase::Idle};
    std::shared_ptr<const walletreview::Snapshot> candidate;
    ReviewId candidate_review{0};
    ReviewId current_review{0};
    bool candidate_current{false};
    QString original_txid;
};

enum class StartError {
    None,
    Closed,
    InvalidDraft,
    SelectionNeedsReconciliation,
    ManualSelectionEmpty,
    CustomFeeInvalid,
    StaleIntent,
};

struct StartInput {
    std::uint64_t id{0};
    std::uint64_t intent_revision{0};
    CurrentFacts facts{};
    std::shared_ptr<const senddraft::Snapshot> draft;
    bool draft_current{false};
    QString draft_error;
    bool selection_needs_reconciliation{false};
    bool manual_selection{false};
    bool has_selected_coins{false};
    bool custom_fee_enabled{false};
    bool custom_fee_rate_valid{false};
    Kind kind{Kind::Payment};
};

struct ReplacementStart {
    std::uint64_t id{0};
    std::uint64_t intent_revision{0};
    CurrentFacts facts{};
    QString original_txid;
};

struct Result {
    Transition transition{Transition::Ignored};
    Snapshot snapshot{};
    StartError admission_error{StartError::None};
};

// Owns the deterministic Prepare -> Review -> NeedsRevalidation state and the
// immutable candidate value. Callers acquire wallet/session facts and perform
// effects outside this reducer, then pass those facts back with the result.
class Preparation
{
public:
    Ticket begin(SessionId session, ReviewId current_review, std::uint64_t requested_id = 0);
    void intentChanged();
    void intentChanged(std::uint64_t revision);
    void reviewReplaced(ReviewId current_review);
    bool matches(const Ticket& ticket, CurrentFacts facts) const;
    Transition assess(const Ticket& ticket, CurrentFacts facts);
    Transition install(const Ticket& ticket, CurrentFacts facts,
                       std::shared_ptr<const walletreview::Snapshot> candidate,
                       ReviewId installed_review);
    Transition fail(const Ticket& ticket, CurrentFacts facts, Phase outcome);
    Transition reject(const Ticket& ticket, Phase outcome);
    Transition discard(const Ticket& ticket, Phase outcome);
    Transition advance(const Ticket& ticket, CurrentFacts facts, Phase expected, Phase outcome);
    void close();

    Phase phase() const { return m_phase; }
    std::uint64_t id() const { return m_id; }
    std::uint64_t intentRevision() const { return m_intent_revision; }
    const Ticket& ticket() const { return m_ticket; }
    const std::shared_ptr<const walletreview::Snapshot>& candidate() const { return m_candidate; }
    ReviewId candidateReview() const { return m_candidate_review; }
    bool candidateCurrent(ReviewId review) const;

private:
    Transition assessPending(const Ticket& ticket, CurrentFacts facts);

    Phase m_phase{Phase::Idle};
    std::uint64_t m_id{0};
    std::uint64_t m_intent_revision{0};
    Ticket m_ticket{};
    std::uint64_t m_candidate_revision{0};
    ReviewId m_candidate_review{0};
    std::shared_ptr<const walletreview::Snapshot> m_candidate;
};

/** Serializes preparation-state decisions on an injected Domain executor.
 * This value owner has no QObject, GUI callback, wallet handle, or Core effect.
 * Callers pass captured facts and receive immutable projection snapshots.
 */
class Owner final
{
public:
    using Completion = std::function<void(Result)>;

    explicit Owner(std::shared_ptr<execution::Scheduler> scheduler);
    execution::SubmitResult begin(StartInput input, Completion done);
    execution::SubmitResult beginReplacement(ReplacementStart input, Completion done);
    execution::SubmitResult intentChanged(std::uint64_t revision, Completion done);
    execution::SubmitResult reviewReplaced(ReviewId current_review, Completion done);
    execution::SubmitResult assess(Ticket ticket, CurrentFacts facts, Completion done);
    execution::SubmitResult install(Ticket ticket, CurrentFacts facts,
                                 std::shared_ptr<const walletreview::Snapshot> candidate,
                                 ReviewId installed_review, Completion done);
    execution::SubmitResult resolve(Ticket ticket, CurrentFacts facts, walletprepare::Error outcome,
                                 std::shared_ptr<const walletreview::Snapshot> candidate,
                                 ReviewId installed_review, Completion done);
    execution::SubmitResult fail(Ticket ticket, CurrentFacts facts, Phase outcome, Completion done);
    execution::SubmitResult reject(Ticket ticket, Phase outcome, Completion done);
    execution::SubmitResult discard(Ticket ticket, Phase outcome, Completion done);
    execution::SubmitResult advance(Ticket ticket, CurrentFacts facts, Phase expected, Phase outcome, Completion done);
    execution::SubmitResult close(Completion done = {});

private:
    struct State {
        Preparation preparation;
        CurrentFacts facts;
        Kind kind{Kind::Payment};
        QString original_txid;
        std::uint64_t sequence{0};
    };
    using Mutation = std::function<Result(State&)>;
    static Snapshot project(State& state);
    static execution::SubmitResult submit(const std::shared_ptr<State>& state,
                                      const std::shared_ptr<execution::Scheduler>& scheduler,
                                      execution::ResourceKey key, Mutation mutation, Completion done);

    const std::shared_ptr<execution::Scheduler> m_scheduler;
    const execution::ResourceKey m_key;
    const std::shared_ptr<State> m_state{std::make_shared<State>()};
};

} // namespace transactionattempt

#endif // BITCOIN_QML_TRANSACTIONATTEMPT_H
