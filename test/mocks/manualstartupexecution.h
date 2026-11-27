// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MANUALSTARTUPEXECUTION_H
#define BITCOIN_QML_TEST_MOCKS_MANUALSTARTUPEXECUTION_H

#include <qml/execution.h>
#include <qml/executor.h>

#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <utility>

class ManualStartupExecution
{
    using Turn = std::function<void()>;
    mutable std::mutex m_mutex;
    std::deque<Turn> m_effects, m_outcomes, m_signer;
    size_t m_core_submissions{0};
    execution::Executor m_executor{[this](execution::Lane lane, execution::UtilityWorker worker, Turn turn) {
        std::lock_guard lock{m_mutex};
        if (lane == execution::Lane::Core) {
            ++m_core_submissions;
            if (reject_core.exchange(false)) return false;
        } else if (reject_utility.exchange(false)) {
            return false;
        }
        turns(lane, worker).push_back(std::move(turn));
        return true;
    }};

    std::deque<Turn>& turns(execution::Lane lane, execution::UtilityWorker worker)
    {
        if (lane == execution::Lane::Utility && worker == execution::UtilityWorker::Signer) return m_signer;
        return lane == execution::Lane::Core ? m_effects : m_outcomes;
    }

public:
    std::atomic_bool reject_core{false}, reject_utility{false};
    execution::SerialQueue core = m_executor.makeQueue(execution::Lane::Core);
    execution::SerialQueue outcomes = m_executor.makeQueue(execution::Lane::Utility);

    ~ManualStartupExecution() { drain(); }

    execution::Executor& executor() { return m_executor; }

    std::function<void()> take(execution::Lane lane, execution::UtilityWorker worker = execution::UtilityWorker::IO)
    {
        std::lock_guard lock{m_mutex};
        auto& pending = turns(lane, worker);
        if (pending.empty()) return {};
        auto turn = std::move(pending.front());
        pending.pop_front();
        return turn;
    }
    bool step(execution::Lane lane, execution::UtilityWorker worker = execution::UtilityWorker::IO)
    {
        auto turn = take(lane, worker);
        if (!turn) return false;
        turn();
        return true;
    }

    void drain()
    {
        while (step(execution::Lane::Core) || step(execution::Lane::Utility) || step(execution::Lane::Utility, execution::UtilityWorker::Signer)) {}
    }

    size_t queuedCount() const
    {
        std::lock_guard lock{m_mutex};
        return m_effects.size() + m_outcomes.size() + m_signer.size();
    }

    size_t coreSubmissions() const
    {
        std::lock_guard lock{m_mutex};
        return m_core_submissions;
    }

    bool hasWork(execution::Lane lane) const
    {
        std::lock_guard lock{m_mutex};
        return lane == execution::Lane::Core ? !m_effects.empty() : !m_outcomes.empty();
    }
    bool quiescent() const
    {
        std::lock_guard lock{m_mutex};
        return m_effects.empty() && m_outcomes.empty() && m_signer.empty();
    }
};

#endif // BITCOIN_QML_TEST_MOCKS_MANUALSTARTUPEXECUTION_H
