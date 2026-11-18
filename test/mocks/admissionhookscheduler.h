// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_ADMISSIONHOOKSCHEDULER_H
#define BITCOIN_QML_TEST_MOCKS_ADMISSIONHOOKSCHEDULER_H

#include <qml/atomicsharedptr.h>
#include <qml/execution.h>
#include <QThread>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>

// Force a GUI reentry at an actual accepted resource/task admission boundary.
// The real scheduler still owns TaskIds, effects, scope budgets and retirement.
class AdmissionHookScheduler final : public execution::Scheduler
{
public:
    struct Trigger {
        const bool cleanup;
        const execution::Lane lane;
        const size_t target;
        QThread* const consumer;
        const std::function<void()> callback;
        std::atomic<size_t> seen{0}, calls{0};
        std::atomic<uint64_t> originalId{0};
        std::atomic_bool fired{false};
        Trigger(bool reserved, execution::Lane selected, size_t nth, std::function<void()> effect)
            : cleanup(reserved), lane(selected), target(nth), consumer(QThread::currentThread()), callback(std::move(effect)) {}
    };
private:
    struct State { ownership::AtomicSharedPtr<Trigger> interest; };
    const std::shared_ptr<execution::Scheduler> m_inner;
    const std::shared_ptr<State> m_state;
    AdmissionHookScheduler(std::shared_ptr<execution::Scheduler> inner, std::shared_ptr<State> state)
        : m_inner(std::move(inner)), m_state(std::move(state)) {}
    std::shared_ptr<Trigger> arm(bool cleanup, execution::Lane lane, size_t nth, std::function<void()> effect)
    {
        if (!nth) throw std::invalid_argument("Admission hook requires a nonzero ordinal");
        auto trigger = std::make_shared<Trigger>(cleanup, lane, nth, std::move(effect));
        const auto old = m_state->interest.exchange(trigger);
        return trigger; // Any old callable is destroyed after atomic exchange.
    }
    void admitted(execution::SubmitResult submitted, bool cleanup, execution::Lane lane)
    {
        const auto trigger = m_state->interest.load();
        if (!submitted || !trigger || QThread::currentThread() != trigger->consumer ||
            trigger->cleanup != cleanup || trigger->lane != lane || trigger->fired.load()) return;
        if (trigger->seen.fetch_add(1) + 1 != trigger->target || trigger->fired.exchange(true)) return;
        trigger->originalId.store(submitted.id.value); ++trigger->calls;
        trigger->callback(); // The underlying ledger call has already returned.
    }
public:
    explicit AdmissionHookScheduler(std::shared_ptr<execution::Scheduler> inner)
        : m_inner(std::move(inner)), m_state(std::make_shared<State>()) {}
    std::shared_ptr<Trigger> armCleanup(size_t nth, std::function<void()> effect)
    {
        return arm(true, execution::Lane::Cleanup, nth, std::move(effect));
    }
    std::shared_ptr<Trigger> armSubmit(execution::Lane lane, size_t nth, std::function<void()> effect)
    {
        return arm(false, lane, nth, std::move(effect));
    }
    void disarm() { const auto old = m_state->interest.exchange({}); }
    std::shared_ptr<execution::Scheduler> openActionScope() override
    {
        auto scope = m_inner->openActionScope();
        return scope ? std::shared_ptr<execution::Scheduler>{new AdmissionHookScheduler(std::move(scope), m_state)} : nullptr;
    }
    execution::SubmitResult submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec = {}) override
    {
        const auto result = m_inner->submit(std::move(task), spec); admitted(result, false, spec.lane); return result;
    }
    execution::SubmitResult reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override
    {
        const auto result = m_inner->reserveCleanup(std::move(task), spec); admitted(result, true, spec.lane); return result;
    }
    bool activateCleanup(execution::TaskId id) override { return m_inner->activateCleanup(id); }
    execution::Borrow borrow(execution::ResourceKey key) override { return m_inner->borrow(key); }
    execution::ResourceKey allocateKey() override { return m_inner->allocateKey(); }
    size_t pending() const override { return m_inner->pending(); }
    void stopAcceptingWork() override { m_inner->stopAcceptingWork(); }
    bool cancelQueued(execution::TaskId id) override { return m_inner->cancelQueued(id); }
};
#endif // BITCOIN_QML_TEST_MOCKS_ADMISSIONHOOKSCHEDULER_H
