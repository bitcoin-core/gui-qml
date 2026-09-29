// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_BACKEND_BARRIER_H
#define BITCOIN_QML_TEST_BACKEND_BARRIER_H

#include <test/thread_audit.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <string>

namespace qmlintegration {
class Barrier
{
public:
    Barrier() : m_watchdog{[this] {
        std::unique_lock lock{m_mutex};
        if (!m_condition.wait_for(lock, std::chrono::seconds{10}, [this] { return m_released; })) {
            timed_out = true;
            m_released = true;
            m_condition.notify_all();
        }
    }} {}
    ~Barrier()
    {
        release();
        m_watchdog.join();
    }
    void enter()
    {
        std::unique_lock lock{m_mutex};
        entered = true;
        m_condition.wait(lock, [this] { return m_released; });
    }
    void release()
    {
        std::lock_guard lock{m_mutex};
        m_released = true;
        m_condition.notify_all();
    }
    std::atomic<bool> entered{false};
    std::atomic<bool> timed_out{false};

private:
    std::mutex m_mutex;
    std::condition_variable m_condition;
    bool m_released{false};
    // Joined by the destructor before its synchronization state dies.
    std::thread m_watchdog;
};


class ScopedAuditBarrier
{
public:
    ScopedAuditBarrier(std::shared_ptr<ThreadAudit> audit, std::string method)
        : m_audit{std::move(audit)}, barrier{std::make_shared<Barrier>()}
    {
        m_audit->setObserver([gate = barrier, method = std::move(method)](const char* called) {
            if (method == called) gate->enter();
        });
    }
    ~ScopedAuditBarrier() { release(); }
    void release()
    {
        m_audit->setObserver({});
        barrier->release();
    }
private:
    std::shared_ptr<ThreadAudit> m_audit;
public:
    std::shared_ptr<Barrier> barrier;
};
} // namespace qmlintegration
#endif // BITCOIN_QML_TEST_BACKEND_BARRIER_H
