// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_NODELIFECYCLE_H
#define BITCOIN_QML_NODELIFECYCLE_H

#include <QString>
#include <QObject>
#include <cstdint>
#include <optional>

namespace nodelifecycle {
Q_NAMESPACE
enum class Phase { Constructed, Initializing, Ready, StartupFailed, Stopping, Draining, Stopped, Fatal };
Q_ENUM_NS(Phase)
struct Initialization {
    uint64_t attempt{0};
    bool attempted{false}, success{false}, cancelled{false};
};
/** One Domain-owned lifecycle. Infrastructure supplies actual effect/drain
 * receipts; neither progress estimates nor a window can authorize readiness. */
class Flow {
public:
    std::optional<uint64_t> start();
    bool initialized(Initialization receipt);
    bool stop();
    bool beginInterrupt();
    bool interrupted();
    bool beginShutdown();
    bool coreStopped(int exit_status);
    bool drained();
    bool fatal(QString reason);
    Phase phase() const { return m_phase; }
    bool ready() const { return m_phase == Phase::Ready; }
    bool stopping() const { return m_phase == Phase::Stopping || m_phase == Phase::Draining || m_phase == Phase::Stopped; }
    bool startupFailed() const { return m_startup_failed; }
    uint64_t attempt() const { return m_attempt; }
    const std::optional<Initialization>& initialization() const { return m_initialization; }
    std::optional<int> coreExitStatus() const { return m_core_exit_status; }
    const QString& error() const { return m_error; }
private:
    Phase m_phase{Phase::Constructed};
    uint64_t m_attempt{0};
    std::optional<Initialization> m_initialization;
    std::optional<int> m_core_exit_status;
    QString m_error;
    bool m_startup_failed{false}, m_interrupt_admitted{false}, m_interrupt_complete{false}, m_shutdown_admitted{false};
};
} // namespace nodelifecycle
#endif // BITCOIN_QML_NODELIFECYCLE_H
