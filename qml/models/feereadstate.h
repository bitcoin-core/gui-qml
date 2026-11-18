// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FEEREADSTATE_H
#define BITCOIN_QML_MODELS_FEEREADSTATE_H

#ifndef Q_MOC_RUN
#include <compare>
#endif
#include <cstdint>
#include <optional>

struct FeeReadRevision {
    uint64_t value{0};
    auto operator<=>(const FeeReadRevision&) const = default;
};

/** Deterministic guards owned exclusively by the serial Domain lane.
 * Native effects carry immutable inputs and return results; they never inspect
 * or mutate this decision state. */
class FeeReadState
{
public:
    enum class Kind { Invalidate, Dispatch, Complete, Close };
    struct Event { Kind kind; FeeReadRevision revision; };
    enum class Phase { Idle, Reading, Closed };
    bool apply(Event event)
    {
        if (m_closed) return false;
        switch (event.kind) {
        case Kind::Invalidate:
            if (event.revision <= m_revision) return false;
            m_revision = event.revision;
            m_pending.reset();
            return true;
        case Kind::Dispatch:
            if (event.revision <= m_revision) return false;
            m_revision = event.revision;
            m_pending = event.revision;
            return true;
        case Kind::Complete:
            if (m_pending != event.revision) return false;
            m_pending.reset();
            return true;
        case Kind::Close:
            m_closed = true;
            m_pending.reset();
            return true;
        }
        return false; // Reject an unknown/future event explicitly.
    }
    Phase phase() const { return m_closed ? Phase::Closed : m_pending ? Phase::Reading : Phase::Idle; }

private:
    FeeReadRevision m_revision;
    std::optional<FeeReadRevision> m_pending;
    bool m_closed{false};
};

#endif // BITCOIN_QML_MODELS_FEEREADSTATE_H
