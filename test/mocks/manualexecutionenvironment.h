// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H
#define BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H

#include <qml/execution.h>
#include <qml/models/feereadservices.h>

#include <array>
#include <algorithm>
#ifndef Q_MOC_RUN
#include <charconv>
#endif
#include <chrono>
#include <deque>
#include <mutex>
#include <string>
#include <vector>

namespace manualexecution_detail {
    template <typename Integer>
    std::string Number(Integer value) {
        std::array<char, 32> buffer{};
        const auto result = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
        return {buffer.data(), result.ptr};
    }
}

/** Manual transport only: production TaskQueue owns admission/key decisions. */
class ManualRoutedExecution
{
public:
    struct Turn { execution::Lane lane; execution::Phase phase; execution::TaskId id; std::function<void()> run; };
    struct TurnInfo { execution::Lane lane; execution::Phase phase; execution::TaskId id; };
private:
    struct State {
        mutable std::mutex mutex;
        std::optional<execution::Lane> reject_lane;
        size_t reject_core_number{0}, core_submissions{0}, running{0};
        std::deque<std::shared_ptr<Turn>> queued;
        std::map<execution::TaskId, std::shared_ptr<Turn>> active;
        std::vector<std::string> trace;
    };
    std::shared_ptr<State> m_state{std::make_shared<State>()};
    struct Running {
        std::shared_ptr<State> state;
        std::function<void()> callback;
        ~Running()
        {
            callback = {};
            std::lock_guard<std::mutex> lock{state->mutex};
            --state->running;
        }
    };
public:
    explicit ManualRoutedExecution(execution::Options limits = execution::Options{}, size_t core_slots = 1)
    {
        scheduler = std::make_shared<execution::TaskQueue>(
            [state = m_state](execution::Lane lane, execution::Phase phase, execution::TaskId id, std::function<void()> run) {
                auto packet = std::make_shared<Turn>(Turn{lane, phase, id, std::move(run)});
                std::lock_guard<std::mutex> lock{state->mutex};
                const bool numbered_rejection = phase == execution::Phase::Queued && lane == execution::Lane::Core
                    && ++state->core_submissions == state->reject_core_number;
                if (numbered_rejection || (phase == execution::Phase::Queued && state->reject_lane == lane)) {
                    state->reject_lane.reset();
                    state->trace.push_back("reject:" + manualexecution_detail::Number(id.value));
                    return false;
                }
                state->queued.push_back(std::move(packet));
                state->trace.push_back("enqueue:" + manualexecution_detail::Number(static_cast<int>(lane)) + ":" + manualexecution_detail::Number(id.value));
                return true;
            }, limits, core_slots);
    }
    void rejectLane(std::optional<execution::Lane> lane)
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        m_state->reject_lane = lane;
    }
    void rejectCoreNumber(size_t number)
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        m_state->reject_core_number = number;
    }
    size_t coreSubmissions() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        return m_state->core_submissions;
    }
    std::optional<execution::TaskId> dispatchNext(execution::Lane lane)
    {
        std::shared_ptr<Turn> packet;
        std::lock_guard<std::mutex> lock{m_state->mutex};
        if (lane != execution::Lane::Core && std::any_of(m_state->active.begin(), m_state->active.end(), [lane](const auto& entry) { return entry.second->lane == lane; })) return {};
        const auto next = std::find_if(m_state->queued.begin(), m_state->queued.end(), [lane](const auto& turn) { return turn->lane == lane; });
        if (next == m_state->queued.end()) return {};
        const auto id = (*next)->id;
        packet = *next;
        m_state->active.emplace(id, packet);
        m_state->queued.erase(next);
        m_state->trace.push_back("dispatch:" + manualexecution_detail::Number(id.value));
        return id;
    }
    bool finishWork(execution::TaskId id)
    {
        const auto state = m_state;
        std::shared_ptr<Turn> turn;
        {
            std::lock_guard<std::mutex> lock{state->mutex};
            const auto next = state->active.find(id);
            if (next == state->active.end()) return false;
            turn = std::move(next->second);
            state->active.erase(next);
            state->trace.push_back("finish:" + manualexecution_detail::Number(id.value));
            ++state->running;
        }
        Running running{state, [turn = std::move(turn)] { turn->run(); }};
        running.callback();
        return true;
    }
    std::optional<execution::TaskId> deliverOutcome()
    {
        std::optional<execution::TaskId> next_id;
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            const auto next = std::find_if(m_state->queued.begin(), m_state->queued.end(), [](const auto& turn) { return turn->lane == execution::Lane::Domain; });
            if (next == m_state->queued.end() || (*next)->phase != execution::Phase::OutcomeReady) return {};
            next_id = (*next)->id;
        }
        // Only the manual consumer removes queued turns; Native ingress appends.
        const auto dispatched = dispatchNext(execution::Lane::Domain);
        return dispatched == next_id ? dispatched : std::nullopt;
    }
    bool stepMailbox(execution::TaskId id)
    {
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            const auto next = m_state->active.find(id);
            if (next == m_state->active.end() || next->second->lane != execution::Lane::Domain) return false;
        }
        return finishWork(id);
    }
    bool step(execution::Lane lane)
    {
        const auto id = dispatchNext(lane);
        return id && finishWork(*id);
    }
    std::optional<execution::Lane> nextLane() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        if (m_state->queued.empty()) return {};
        return m_state->queued.front()->lane;
    }
    bool stepNext()
    {
        const auto lane = nextLane();
        return lane && step(*lane);
    }
    void drain()
    {
        for (;;) {
            std::optional<execution::TaskId> active;
            {
                std::lock_guard<std::mutex> lock{m_state->mutex};
                if (!m_state->active.empty()) active = m_state->active.begin()->first;
            }
            if (active) { finishWork(*active); continue; }
            if (!stepNext()) return;
        }
    }
    bool queuedIdle() const { return queuedCount() == 0; }
    size_t queuedCount() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        return m_state->queued.size();
    }
    bool activeIdle() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        return m_state->active.empty();
    }
    template <typename Predicate> auto queuedCountIf(Predicate predicate) const
    {
        std::vector<TurnInfo> snapshot;
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            snapshot.reserve(m_state->queued.size());
            for (const auto& turn : m_state->queued) snapshot.push_back({turn->lane, turn->phase, turn->id});
        }
        return std::count_if(snapshot.begin(), snapshot.end(), std::move(predicate));
    }
    std::vector<std::string> traceSnapshot() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        return m_state->trace;
    }
    bool transportIdle() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        return m_state->queued.empty() && m_state->active.empty() && m_state->running == 0;
    }
    bool quiescent() const { return transportIdle() && scheduler->pending() == 0 && transportIdle(); }
    std::shared_ptr<execution::TaskQueue> scheduler;
};

/** Timing/transport controller; executes the production ledger and decisions. */
class ManualExecutionEnvironment final : public FeeReadTimer
{
public:
    struct Turn { execution::Phase phase; execution::TaskId id; std::function<void()> turn; };
private:
    struct State {
        mutable std::mutex mutex;
        std::deque<std::shared_ptr<Turn>> turns;
        std::deque<std::shared_ptr<std::function<void()>>> publications;
        std::vector<std::string> trace;
        std::chrono::milliseconds now{0}, due{0};
        std::shared_ptr<std::function<void()>> expired;
        bool reject_next{false};
        size_t running{0};
    };
    std::shared_ptr<State> m_state{std::make_shared<State>()};
    // Destroy callback captures before publishing transport-idle. A callback
    // may release ledger leases or reenter this transport during destruction.
    struct Running {
        std::shared_ptr<State> state;
        std::function<void()> callback;
        ~Running()
        {
            callback = {};
            std::lock_guard<std::mutex> lock{state->mutex};
            --state->running;
        }
    };
public:
    ManualExecutionEnvironment()
        : scheduler(std::make_shared<execution::TaskQueue>(
            [state = m_state](execution::Phase phase, execution::TaskId id, std::function<void()> turn) {
                // Packet lifetime is outside the lock even on allocation failure.
                auto packet = std::make_shared<Turn>(Turn{phase, id, std::move(turn)});
                std::lock_guard<std::mutex> lock{state->mutex};
                if (phase == execution::Phase::Queued && std::exchange(state->reject_next, false)) {
                    state->trace.emplace_back("reject:" + manualexecution_detail::Number(id.value));
                    return false;
                }
                state->trace.emplace_back((phase == execution::Phase::Queued ? "enqueue-effect:" : "enqueue-outcome:") + manualexecution_detail::Number(id.value));
                state->turns.push_back(std::move(packet));
                return true;
            })) {}
    ~ManualExecutionEnvironment() override { scheduler->closeAdmission(); stop(); drain(); }
    void rejectNext(bool reject = true)
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        m_state->reject_next = reject;
    }
    void restart(std::function<void()> expired) override
    {
        std::shared_ptr<std::function<void()>> retained;
        if (expired) retained = std::make_shared<std::function<void()>>(std::move(expired));
        std::shared_ptr<std::function<void()>> retired;
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            retired.swap(m_state->expired);
            m_state->expired = std::move(retained);
            m_state->due = m_state->now + std::chrono::milliseconds{250};
            m_state->trace.emplace_back("deadline:" + manualexecution_detail::Number(m_state->due.count()));
        }
    }
    void stop() override
    {
        std::shared_ptr<std::function<void()>> retired;
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            retired.swap(m_state->expired);
            m_state->trace.emplace_back("stop-timer");
        }
    }
    void advance(std::chrono::milliseconds duration)
    {
        const auto state = m_state;
        std::shared_ptr<std::function<void()>> expired;
        {
            std::lock_guard<std::mutex> lock{state->mutex};
            state->now += duration;
            state->trace.emplace_back("clock:" + manualexecution_detail::Number(state->now.count()));
            if (state->expired && state->now >= state->due) {
                state->trace.emplace_back("expire");
                expired.swap(state->expired);
                ++state->running;
            }
        }
        if (expired) { Running running{state, [expired = std::move(expired)] { (*expired)(); }}; running.callback(); }
    }
    std::optional<Turn> takeTurnFront()
    {
        std::shared_ptr<Turn> next;
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            if (!m_state->turns.empty()) {
                next = std::move(m_state->turns.front());
                m_state->turns.pop_front();
            }
        }
        if (!next) return {};
        return std::move(*next);
    }
    std::optional<Turn> takeTurn(execution::TaskId id)
    {
        std::shared_ptr<Turn> next;
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            const auto found = std::find_if(m_state->turns.begin(), m_state->turns.end(),
                [id](const auto& turn) { return turn->id == id; });
            if (found == m_state->turns.end()) return {};
            next = std::move(*found);
            m_state->turns.erase(found);
        }
        return std::move(*next);
    }
    bool step()
    {
        const auto state = m_state;
        std::shared_ptr<Turn> next;
        {
            std::lock_guard<std::mutex> lock{state->mutex};
            if (state->turns.empty()) return false;
            next = std::move(state->turns.front());
            state->turns.pop_front();
            state->trace.emplace_back((next->phase == execution::Phase::Queued ? "effect:" : "outcome:") + manualexecution_detail::Number(next->id.value));
            ++state->running;
        }
        Running running{state, [next = std::move(next)] { next->turn(); }};
        running.callback();
        return true;
    }
    void drain() { while (step()) {} }
    void post(std::function<void()> publication)
    {
        auto retained = std::make_shared<std::function<void()>>(std::move(publication));
        std::lock_guard<std::mutex> lock{m_state->mutex};
        m_state->trace.emplace_back("post");
        m_state->publications.push_back(std::move(retained));
    }
    std::function<void()> takePublicationBack()
    {
        std::shared_ptr<std::function<void()>> publication;
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            if (m_state->publications.empty()) return {};
            publication = std::move(m_state->publications.back());
            m_state->publications.pop_back();
        }
        return [publication = std::move(publication)] { (*publication)(); };
    }
    bool duplicateFirstPublication()
    {
        std::shared_ptr<std::function<void()>> original;
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            if (m_state->publications.empty()) return false;
            original = m_state->publications.front();
        }
        // Preserve independent mutable captures, as the original test's
        // std::function copy did. Client copy/destructor hooks run unlocked.
        auto duplicate = std::make_shared<std::function<void()>>(*original);
        {
            std::lock_guard<std::mutex> lock{m_state->mutex};
            m_state->publications.push_back(std::move(duplicate));
        }
        return true;
    }
    bool publishNext()
    {
        const auto state = m_state;
        std::shared_ptr<std::function<void()>> publication;
        {
            std::lock_guard<std::mutex> lock{state->mutex};
            if (state->publications.empty()) return false;
            publication = std::move(state->publications.front());
            state->publications.pop_front();
            state->trace.emplace_back("publish");
            ++state->running;
        }
        Running running{state, [publication = std::move(publication)] { (*publication)(); }};
        running.callback();
        return true;
    }
    void publishAll() { while (publishNext()) {} }
    size_t queuedTurnCount() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        return m_state->turns.size();
    }
    bool turnsIdle() const { return queuedTurnCount() == 0; }
    size_t publicationCount() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        return m_state->publications.size();
    }
    bool publicationsIdle() const { return publicationCount() == 0; }
    // Live caches retain cleanup leases. No helper lock crosses pending(),
    // since ledger admission invokes the dispatcher while holding its own lock.
    bool transportIdle() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        return m_state->turns.empty() && !m_state->expired && m_state->publications.empty() && m_state->running == 0;
    }
    bool quiescent() const { return transportIdle() && scheduler->pending() == 0 && transportIdle(); }
    std::string trace() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        std::string result;
        for (const auto& entry : m_state->trace) { if (!result.empty()) result += ' '; result += entry; }
        return result;
    }
    std::shared_ptr<execution::TaskQueue> scheduler;
};

#endif // BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H
