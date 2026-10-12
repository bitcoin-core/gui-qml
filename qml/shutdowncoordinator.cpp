// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/initexecutor.h>
#include <qml/shutdowncoordinator.h>
#include <qml/backendexecutor.h>

QmlShutdownCoordinator::QmlShutdownCoordinator(QmlInitExecutor& executor, QObject* parent)
    : QObject(parent), m_executor(executor)
{
    connect(&executor, &QmlInitExecutor::shutdownResult, this, [this] {
        BackendExecutor::shutdownAll(this, [this] { Q_EMIT finished(); });
    });
    connect(&executor, &QmlInitExecutor::interruptResult, this, &QmlShutdownCoordinator::drainAfterInterrupt);
}

void QmlShutdownCoordinator::requestShutdown()
{
    if (m_shutdown_requested) return;
    m_shutdown_requested = true;
    Q_EMIT shutdownStarted();
    m_pending_participants = m_before_interrupt_participants.size();
    if (m_pending_participants == 0) {
        m_executor.interrupt();
        return;
    }
    m_draining_before_interrupt = true;
    for (const auto& begin : m_before_interrupt_participants) begin();
}

void QmlShutdownCoordinator::drainAfterInterrupt()
{
    if (m_draining_after_interrupt) return;
    m_draining_after_interrupt = true;
    m_pending_participants = m_after_interrupt_participants.size();
    if (m_pending_participants == 0) {
        m_executor.shutdown();
        return;
    }
    for (const auto& begin : m_after_interrupt_participants) begin();
}

void QmlShutdownCoordinator::participantFinished(bool before_interrupt)
{
    Q_ASSERT(m_pending_participants > 0);
    if (--m_pending_participants != 0) return;
    if (before_interrupt) {
        m_draining_before_interrupt = false;
        m_executor.interrupt();
    } else {
        m_executor.shutdown();
    }
}
