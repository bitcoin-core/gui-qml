// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETLIFECYCLESTATE_H
#define BITCOIN_QML_WALLETLIFECYCLESTATE_H

#include <optional>

namespace walletlife {
enum class Phase { Registering, Active, Failed, Closing, Closed };
enum class Event { Registered, RegistrationFailed, Notification, Close, CleanupFinished };
enum class Publication { Ready, Changed, Failed, Retired, InitialHandoff };
enum class FailureReason { None, Backend, IngressCapacity };
struct Update { Publication kind; bool cleanup_failed{false}; FailureReason reason{FailureReason::None}; };

class State
{
public:
    std::optional<Publication> apply(Event event);
    Phase phase() const { return m_phase; }
private:
    Phase m_phase{Phase::Registering};
};
} // namespace walletlife

#endif // BITCOIN_QML_WALLETLIFECYCLESTATE_H
