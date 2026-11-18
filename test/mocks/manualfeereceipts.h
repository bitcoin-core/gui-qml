// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_MANUALFEERECEIPTS_H
#define BITCOIN_QML_TEST_MOCKS_MANUALFEERECEIPTS_H

#include <test/mocks/manualexecutionenvironment.h>
#include <QScopeGuard>
#include <QThread>
#include <set>

// A receipt is identified by the actual read and original TaskQueue TaskId.
// Unrelated Native facts retain their ordinary, independently controlled posts.
class ManualFeeReceipts
{
public:
    explicit ManualFeeReceipts(std::shared_ptr<ManualExecutionEnvironment> environment)
        : m_environment(std::move(environment)), m_consumer(QThread::currentThread()) {}
    void readStarted() { if (m_current) m_reads.insert(*m_current); }
    // File effects have no wallet callback to mark their start. A test can
    // identify the actual completed packet by its observable file write,
    // then retain only that packet's still-undelivered Domain/GUI receipt.
    bool observeReady(execution::TaskId id)
    {
        if (QThread::currentThread() != m_consumer ||
            m_environment->scheduler->phase(id) != execution::Phase::OutcomeReady) return false;
        m_reads.insert(id);
        return true;
    }
    void post(std::function<void()> publication)
    {
        // Native producers never inspect the consumer-thread receipt window.
        if (QThread::currentThread() == m_consumer && m_fee_outcome && m_current)
            m_publications.push_back({*m_current, std::move(publication)});
        else m_environment->post(std::move(publication));
    }
    std::optional<execution::TaskId> startRead()
    {
        const auto before = m_reads.size();
        for (int budget = 0; budget < 256; ++budget) {
            auto packet = m_environment->takeTurnFront();
            if (!packet) return {};
            const auto id = packet->id;
            run(std::move(*packet));
            if (m_reads.size() != before) return id;
        }
        return {};
    }
    bool finishOutcome(execution::TaskId id)
    {
        if (m_environment->scheduler->phase(id) != execution::Phase::OutcomeReady) return false;
        auto packet = m_environment->takeTurn(id);
        if (!packet) return false;
        run(std::move(*packet));
        return true;
    }
    void drain()
    {
        while (auto packet = m_environment->takeTurnFront()) run(std::move(*packet));
    }
    size_t publicationCount() const { return m_publications.size(); }
    bool publish(execution::TaskId id)
    {
        const auto found = std::find_if(m_publications.begin(), m_publications.end(),
            [id](const auto& publication) { return publication.id == id; });
        if (found == m_publications.end()) return false;
        auto callback = std::move(found->callback);
        m_publications.erase(found);
        callback();
        return true;
    }
    void publishAll()
    {
        while (!m_publications.empty()) publish(m_publications.front().id);
    }
private:
    void run(ManualExecutionEnvironment::Turn packet)
    {
        const auto previous = m_current;
        const auto previous_outcome = m_fee_outcome;
        m_current = packet.id;
        m_fee_outcome = packet.phase == execution::Phase::OutcomeReady && m_reads.contains(packet.id);
        const auto restore = qScopeGuard([&] { m_current = previous; m_fee_outcome = previous_outcome; });
        packet.turn();
    }
    struct Publication { execution::TaskId id; std::function<void()> callback; };
    std::shared_ptr<ManualExecutionEnvironment> m_environment;
    QThread* const m_consumer;
    std::optional<execution::TaskId> m_current;
    bool m_fee_outcome{false};
    std::set<execution::TaskId> m_reads;
    std::deque<Publication> m_publications;
};
#endif // BITCOIN_QML_TEST_MOCKS_MANUALFEERECEIPTS_H
