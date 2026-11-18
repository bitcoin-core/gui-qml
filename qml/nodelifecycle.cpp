// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/nodelifecycle.h>

namespace nodelifecycle {
std::optional<uint64_t> Flow::start()
{
    if (m_phase != Phase::Constructed || m_attempt) return {};
    // The attempt is scoped to this owner, which admits initialization once.
    m_attempt = 1;
    m_phase = Phase::Initializing;
    return m_attempt;
}
bool Flow::initialized(Initialization receipt)
{
    if (!receipt.attempt || receipt.attempt != m_attempt || m_initialization || (receipt.success && !receipt.attempted)) return false;
    m_initialization = receipt;
    m_startup_failed = !receipt.success && !receipt.cancelled;
    if (m_phase == Phase::Initializing) {
        if (receipt.cancelled) {
            m_phase = Phase::Stopping;
        } else if (receipt.success) {
            m_phase = Phase::Ready;
        } else {
            m_phase = Phase::StartupFailed;
        }
    }
    return true; // Retain the raw effect even after a stop/fatal fence.
}
bool Flow::stop()
{
    if (stopping() || m_phase == Phase::Fatal) return false;
    m_phase = Phase::Stopping;
    return true;
}
bool Flow::beginInterrupt()
{
    if (m_phase == Phase::Fatal || m_phase == Phase::Stopped || m_interrupt_admitted) return false;
    stop();
    if (m_phase != Phase::Stopping) return false;
    m_interrupt_admitted = true;
    return true;
}
bool Flow::interrupted()
{
    if (m_phase != Phase::Stopping || !m_interrupt_admitted || m_interrupt_complete) return false;
    m_interrupt_complete = true;
    m_phase = Phase::Draining;
    return true;
}
bool Flow::beginShutdown()
{
    if (m_phase != Phase::Draining || !m_interrupt_complete || m_shutdown_admitted) return false;
    m_shutdown_admitted = true;
    return true;
}
bool Flow::coreStopped(int exit_status)
{
    if (m_phase != Phase::Draining || !m_shutdown_admitted || m_core_exit_status) return false;
    m_core_exit_status = exit_status;
    return true;
}
bool Flow::drained()
{
    if (m_phase != Phase::Draining || !m_core_exit_status) return false;
    m_phase = Phase::Stopped;
    return true;
}
bool Flow::fatal(QString reason)
{
    if (m_phase == Phase::Fatal || m_phase == Phase::Stopped) return false;
    m_phase = Phase::Fatal;
    m_error = reason.left(16'384);
    return true;
}
} // namespace nodelifecycle
