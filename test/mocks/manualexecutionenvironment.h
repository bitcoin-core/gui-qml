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

class ManualExecutionEnvironment final : public FeeReadTimer
{
    struct State {
        mutable std::mutex mutex;
        std::deque<std::shared_ptr<std::function<void()>>> publications;
        std::vector<std::string> trace;
        std::chrono::milliseconds now{0}, due{0};
        std::shared_ptr<std::function<void()>> expired;
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
    ~ManualExecutionEnvironment() override { stop(); }
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
        return !m_state->expired && m_state->publications.empty() && m_state->running == 0;
    }
    bool quiescent() const { return transportIdle(); }
    std::string trace() const
    {
        std::lock_guard<std::mutex> lock{m_state->mutex};
        std::string result;
        for (const auto& entry : m_state->trace) { if (!result.empty()) result += ' '; result += entry; }
        return result;
    }
};

#endif // BITCOIN_QML_TEST_MOCKS_MANUALEXECUTIONENVIRONMENT_H
