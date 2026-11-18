// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MANUALNODEOBSERVATIONS_H
#define BITCOIN_QML_TEST_MOCKS_MANUALNODEOBSERVATIONS_H

#include <qml/executor.h>
#include <qml/guipresentation.h>
#include <qml/models/nodemodel.h>
#include <qml/presentationservices.h>

#include <algorithm>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

// Hold worker dispatch and GUI delivery independently, including when a Core
// notification arrives from a different thread. The actual queues own work.
class ManualNodeObservations
{
    using Turn = std::function<void()>;
    using Lane = execution::Lane;
    mutable std::mutex m_mutex;
    std::deque<Turn> m_gui;
    std::map<Lane, std::deque<Turn>> m_work;
    size_t m_core_submissions{0};
    std::optional<Lane> m_reject_lane;
    const std::function<void(Lane)> m_before_dispatch;
    execution::Executor m_workers{[this](Lane lane, execution::UtilityWorker, Turn turn) {
        if (m_before_dispatch) m_before_dispatch(lane);
        std::lock_guard lock{m_mutex};
        if (lane == Lane::Core) ++m_core_submissions;
        if (m_reject_lane == lane) {
            m_reject_lane.reset();
            return false;
        }
        m_work[lane].push_back(std::move(turn));
        return true;
    }};
    const presentation::Post m_post = [this](Turn turn) {
        std::lock_guard lock{m_mutex};
        m_gui.push_back(std::move(turn));
    };
    const std::shared_ptr<execution::SerialQueue> m_retirement =
        std::make_shared<execution::SerialQueue>(m_workers.makeQueue(execution::Lane::Utility));
    PresentationServices m_diagnostics{{
        m_post, [queue = m_retirement](auto retire) { return queue->post(std::move(retire)); },
        {}, std::make_shared<execution::SerialQueue>(m_workers.makeQueue(execution::Lane::Utility)),
    }};

public:
    explicit ManualNodeObservations(std::function<void(Lane)> before_dispatch = {})
        : m_before_dispatch(std::move(before_dispatch)) {}

    execution::Executor& executor() { return m_workers; }
    const presentation::Post& post() const { return m_post; }

    NodeModel::Services services()
    {
        NodeModel::Services ports;
        ports.post = m_post;
        ports.observation_executor = &m_workers;
        ports.diagnostics = &m_diagnostics;
        ports.network_retirement = m_diagnostics.valueRetirement();
        ports.schedule = [](auto, auto, auto) {};
        ports.cancel = [](auto) {};
        return ports;
    }

    void rejectNext(Lane lane)
    {
        std::lock_guard lock{m_mutex};
        m_reject_lane = lane;
    }
    bool hasWork(Lane lane) const
    {
        std::lock_guard lock{m_mutex};
        const auto found = m_work.find(lane);
        return found != m_work.end() && !found->second.empty();
    }

    Turn take(Lane lane)
    {
        std::lock_guard lock{m_mutex};
        auto& work = m_work[lane];
        if (work.empty()) return {};
        auto turn = std::move(work.front());
        work.pop_front();
        return turn;
    }
    bool step(Lane lane)
    {
        auto turn = take(lane);
        if (!turn) return false;
        turn();
        return true;
    }
    void drain()
    {
        while (step(Lane::Core) || step(Lane::Priority) || step(Lane::Utility)) {}
    }
    bool turnsIdle() const
    {
        std::lock_guard lock{m_mutex};
        return std::all_of(m_work.begin(), m_work.end(), [](const auto& entry) { return entry.second.empty(); });
    }
    size_t coreSubmissions() const
    {
        std::lock_guard lock{m_mutex};
        return m_core_submissions;
    }

    bool publicationsIdle() const
    {
        std::lock_guard lock{m_mutex};
        return m_gui.empty();
    }
    void publishAll()
    {
        for (;;) {
            Turn turn;
            {
                std::lock_guard lock{m_mutex};
                if (m_gui.empty()) return;
                turn = std::move(m_gui.front());
                m_gui.pop_front();
            }
            turn();
        }
    }
    Turn takePublicationBack()
    {
        std::lock_guard lock{m_mutex};
        if (m_gui.empty()) return {};
        auto turn = std::move(m_gui.back());
        m_gui.pop_back();
        return turn;
    }
};

inline bool DrainManualNodeTurns(ManualNodeObservations& env)
{
    // A Core, Utility or GUI turn can enqueue its successor.
    for (int turn = 0; turn < 32; ++turn) {
        if (env.turnsIdle() && env.publicationsIdle()) return true;
        env.drain(); env.publishAll();
    }
    return env.turnsIdle() && env.publicationsIdle();
}

#endif // BITCOIN_QML_TEST_MOCKS_MANUALNODEOBSERVATIONS_H
