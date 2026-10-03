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

/** Coordinates interruption and feature retirement before application exit.
 * Forward native Quit from QmlQuitHandler only after registering participants.
 * Participants must keep their backend borrowers alive until completion and
 * emit finished once no accepted work can access node state anymore.
 */
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

    /** Drain live port-mapping/settings commands before Core interrupts mapping. */
    template <typename Sender, typename Signal>
    void addBeforeInterruptParticipant(Sender* sender, Signal finished, std::function<void()> begin)
    {
        add(sender, finished, std::move(begin), true);
    }

    void requestShutdown();
    bool isStopping() const { return m_requested; }

Q_SIGNALS:
    void shutdownStarted();
    void finished();

private:
    template <typename Sender, typename Signal>
    void add(Sender* sender, Signal finished, std::function<void()> begin, bool before_interrupt)
    {
        Q_ASSERT(!m_requested);
        auto completed = std::make_shared<bool>(false);
        connect(sender, finished, this, [this, completed, before_interrupt] {
            if (!(before_interrupt ? m_before_draining : m_draining) || std::exchange(*completed, true)) return;
            participantFinished(before_interrupt);
        });
        (before_interrupt ? m_before_interrupt : m_participants).push_back(std::move(begin));
    }

    void drain();
    void participantFinished(bool before_interrupt);

    QmlInitExecutor& m_executor;
    std::vector<std::function<void()>> m_participants;
    std::vector<std::function<void()>> m_before_interrupt;
    size_t m_pending{0};
    bool m_requested{false};
    bool m_draining{false};
    bool m_before_draining{false};
};

#endif // BITCOIN_QML_SHUTDOWNCOORDINATOR_H
