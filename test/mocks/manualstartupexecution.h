// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MANUALSTARTUPEXECUTION_H
#define BITCOIN_QML_TEST_MOCKS_MANUALSTARTUPEXECUTION_H

#include <qml/execution.h>
#include <qml/executor.h>

#include <deque>
#include <functional>
#include <utility>

class ManualStartupExecution
{
    using Turn = std::function<void()>;
    std::deque<Turn> m_effects, m_outcomes;
    size_t m_core_submissions{0};
    execution::Executor m_executor{[this](execution::Lane lane, execution::UtilityWorker, Turn turn) {
        if (lane == execution::Lane::Core) {
            ++m_core_submissions;
            if (std::exchange(reject_core, false)) return false;
        } else if (std::exchange(reject_utility, false)) {
            return false;
        }
        turns(lane).push_back(std::move(turn));
        return true;
    }};

    std::deque<Turn>& turns(execution::Lane lane)
    {
        return lane == execution::Lane::Core ? m_effects : m_outcomes;
    }

public:
    bool reject_core{false}, reject_utility{false};
    execution::SerialQueue core = m_executor.makeQueue(execution::Lane::Core);
    execution::SerialQueue outcomes = m_executor.makeQueue(execution::Lane::Utility);

    ~ManualStartupExecution() { drain(); }

    execution::Executor& executor() { return m_executor; }

    bool step(execution::Lane lane)
    {
        auto& pending = turns(lane);
        if (pending.empty()) return false;
        auto turn = std::move(pending.front());
        pending.pop_front();
        turn();
        return true;
    }

    void drain()
    {
        while (step(execution::Lane::Core) || step(execution::Lane::Utility)) {}
    }

    size_t coreSubmissions() const { return m_core_submissions; }

    bool quiescent() const { return m_effects.empty() && m_outcomes.empty(); }
};

#endif // BITCOIN_QML_TEST_MOCKS_MANUALSTARTUPEXECUTION_H
