// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletlifecyclestate.h>

namespace walletlife {
std::optional<Publication> State::apply(Event event)
{
    switch (event) {
    case Event::Registered:
        if (m_phase != Phase::Registering) return {};
        m_phase = Phase::Active;
        return Publication::Ready;
    case Event::RegistrationFailed:
        if (m_phase != Phase::Registering && m_phase != Phase::Active) return {};
        m_phase = Phase::Failed;
        return Publication::Failed;
    case Event::Notification:
        if (m_phase == Phase::Active) return Publication::Changed;
        return {};
    case Event::Close:
        if (m_phase != Phase::Closed) m_phase = Phase::Closing;
        return {};
    case Event::CleanupFinished:
        if (m_phase != Phase::Closing) return {};
        m_phase = Phase::Closed;
        return Publication::Retired;
    }
    return {};
}
} // namespace walletlife
