// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_SHUTDOWNCOORDINATOR_H
#define BITCOIN_QML_SHUTDOWNCOORDINATOR_H

#include <QObject>

#include <functional>
#include <memory>
#include <utility>
#include <vector>

class QmlInitExecutor;

class QmlShutdownCoordinator : public QObject
{
    Q_OBJECT
public:
    explicit QmlShutdownCoordinator(QmlInitExecutor& executor, QObject* parent = nullptr);

    template <typename Sender, typename Signal>
    void addParticipant(Sender* sender, Signal finished, std::function<void()> begin)
    {
        add(sender, finished, std::move(begin), false);
    }

    template <typename Sender, typename Signal>
    void addBeforeInterruptParticipant(Sender* sender, Signal finished, std::function<void()> begin)
    {
        add(sender, finished, std::move(begin), true);
    }

    void requestShutdown();
    bool isStopping() const { return m_shutdown_requested; }

Q_SIGNALS:
    void shutdownStarted();
    void finished();

private:
    template <typename Sender, typename Signal>
    void add(Sender* sender, Signal finished, std::function<void()> begin, bool before_interrupt)
    {
        Q_ASSERT(!m_shutdown_requested);
        auto completed = std::make_shared<bool>(false);
        connect(sender, finished, this, [this, completed, before_interrupt] {
            const bool draining = before_interrupt ? m_draining_before_interrupt : m_draining_after_interrupt;
            if (!draining || std::exchange(*completed, true)) return;
            participantFinished(before_interrupt);
        });
        (before_interrupt ? m_before_interrupt_participants : m_after_interrupt_participants).push_back(std::move(begin));
    }

    void drainAfterInterrupt();
    void participantFinished(bool before_interrupt);

    QmlInitExecutor& m_executor;
    std::vector<std::function<void()>> m_after_interrupt_participants;
    std::vector<std::function<void()>> m_before_interrupt_participants;
    size_t m_pending_participants{0};
    bool m_shutdown_requested{false};
    bool m_draining_after_interrupt{false};
    bool m_draining_before_interrupt{false};
};

#endif // BITCOIN_QML_SHUTDOWNCOORDINATOR_H
