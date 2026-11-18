// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/backuppolicy.h>

bool backup::Pending(Phase phase)
{
    return phase == Phase::Queued || phase == Phase::Inspecting || phase == Phase::AwaitingConfirmation
        || phase == Phase::Ready || phase == Phase::Writing;
}

backup::Decision backup::Advance(const Snapshot& state, Event event, const Proposal& proposal, WriteResult result)
{
    Decision decision{state};
    if (event < Event::Start || event > Event::Fault || state.phase < Phase::Queued || state.phase > Phase::Retired) return decision;
    auto& next = decision.snapshot;
    switch (event) {
    case Event::Start:
        if (state.phase != Phase::Queued) return decision;
        next.phase = Phase::Inspecting;
        decision.effect = Effect::Inspect;
        break;
    case Event::Inspected:
        if (state.phase != Phase::Inspecting) return decision;
        next.target = proposal;
        if (proposal.error != Error::None || proposal.path.empty()) {
            next.phase = Phase::Failed;
            next.error = proposal.error == Error::None ? Error::InvalidPath : proposal.error;
        } else {
            next.phase = proposal.exists || proposal.renamed ? Phase::AwaitingConfirmation : Phase::Ready;
        }
        break;
    case Event::Approve:
        if (state.phase != Phase::AwaitingConfirmation) return decision;
        next.destination_approved = true;
        next.phase = Phase::Ready;
        break;
    case Event::Refuse:
        if (state.phase != Phase::AwaitingConfirmation) return decision;
        next.phase = Phase::Declined;
        break;
    case Event::AdmitWrite:
        if (state.phase != Phase::Ready || state.target.path.empty() || state.target.error != Error::None
            || ((state.target.exists || state.target.renamed) && !state.destination_approved)) return decision;
        next.phase = Phase::Writing;
        decision.effect = Effect::Write;
        break;
    case Event::Written:
        if (state.phase != Phase::Writing) return decision;
        next.error = result.error;
        next.phase = result.error == Error::None ? Phase::Succeeded : Phase::Failed;
        break;
    case Event::Retire:
        // A dispatched write retains its actual outcome, independent of UI.
        if (!Pending(state.phase) || state.phase == Phase::Writing) return decision;
        next.phase = Phase::Retired;
        break;
    case Event::Fault:
        if (!Pending(state.phase)) return decision;
        next.phase = Phase::Failed;
        next.error = result.error == Error::None ? Error::Unknown : result.error;
        break;
    }
    ++next.sequence;
    decision.accepted = true;
    return decision;
}
