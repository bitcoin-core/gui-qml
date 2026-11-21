// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_THREADRECORDINGSCHEDULER_H
#define BITCOIN_QML_TEST_MOCKS_THREADRECORDINGSCHEDULER_H

#include <qml/execution.h>
#include <QThread>
#include <array>
#include <atomic>

class ThreadRecordingScheduler final : public execution::Scheduler
{
    using Threads = std::array<std::atomic<QThread*>, 4>;
    struct Recorded final : execution::Task {
        std::shared_ptr<execution::Task> task;
        std::shared_ptr<Threads> threads;
        execution::Lane lane;
        Recorded(std::shared_ptr<execution::Task> task, std::shared_ptr<Threads> threads, execution::Lane lane)
            : task(std::move(task)), threads(std::move(threads)), lane(lane) {}
        void execute() override { (*threads)[static_cast<size_t>(lane)] = QThread::currentThread(); task->execute(); }
        void deliver() override { task->deliver(); }
        void notStarted(execution::NotStarted::Cause cause) override { task->notStarted(cause); }
    };
    const std::shared_ptr<execution::Scheduler> m_inner;
    const std::shared_ptr<Threads> m_threads{std::make_shared<Threads>()};
    ThreadRecordingScheduler(std::shared_ptr<execution::Scheduler> inner, std::shared_ptr<Threads> threads)
        : m_inner(std::move(inner)), m_threads(std::move(threads)) {}
public:
    explicit ThreadRecordingScheduler(std::shared_ptr<execution::Scheduler> inner) : m_inner(std::move(inner)) {}
    QThread* thread(execution::Lane lane) const { return (*m_threads)[static_cast<size_t>(lane)].load(); }
    std::shared_ptr<execution::Scheduler> openActionScope() override {
        auto scope = m_inner->openActionScope();
        return scope ? std::shared_ptr<execution::Scheduler>{new ThreadRecordingScheduler(std::move(scope), m_threads)} : nullptr;
    }
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec = {}) override {
        return m_inner->submit(std::make_shared<Recorded>(std::move(task), m_threads, spec.lane), spec);
    }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return m_inner->reserveCleanup(std::move(task), spec); }
    bool activateCleanup(execution::TaskId id) override { return m_inner->activateCleanup(id); }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override { return m_inner->borrow(key); }
    execution::ResourceKey allocateKey() override { return m_inner->allocateKey(); }
    size_t pending() const override { return m_inner->pending(); }
    void closeOrdinaryAdmission() override { m_inner->closeOrdinaryAdmission(); }
    bool cancelQueued(execution::TaskId id) override { return m_inner->cancelQueued(id); }
};
#endif // BITCOIN_QML_TEST_MOCKS_THREADRECORDINGSCHEDULER_H
