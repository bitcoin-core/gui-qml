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
        // Backend borrowers have drained before appShutdown. Value-only export
        // jobs can outlive their QML view and must finish before Qt exits too.
        BackendExecutor::shutdownAll(this, [this] { Q_EMIT finished(); });
    });
    connect(&executor, &QmlInitExecutor::interruptResult, this, &QmlShutdownCoordinator::drain);
}

void QmlShutdownCoordinator::requestShutdown()
{
    if (m_requested) return;
    m_requested = true;
    Q_EMIT shutdownStarted();
    // Accepted port-mapping work must finish before InterruptMapPort. Otherwise
    // a queued enable could restart the mapping thread during Core shutdown.
    m_pending = m_before_interrupt.size();
    if (m_pending == 0) {
        m_executor.interrupt();
        return;
    }
    m_before_draining = true;
    for (const auto& begin : m_before_interrupt) begin();
}

void QmlShutdownCoordinator::drain()
{
    if (m_draining) return;
    m_draining = true;
    m_pending = m_participants.size();
    if (m_pending == 0) {
        m_executor.shutdown();
        return;
    }
    for (const auto& begin : m_participants) begin();
}

void QmlShutdownCoordinator::participantFinished(bool before_interrupt)
{
    Q_ASSERT(m_pending > 0);
    if (--m_pending != 0) return;
    if (before_interrupt) {
        m_before_draining = false;
        m_executor.interrupt();
    } else {
        m_executor.shutdown();
    }
}
