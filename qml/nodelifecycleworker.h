// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_NODELIFECYCLEWORKER_H
#define BITCOIN_QML_NODELIFECYCLEWORKER_H

#include <interfaces/node.h>
#include <qml/nodelifecycle.h>
#include <qml/snapshotquery.h>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <optional>

namespace nodelifecycle {
struct NativeInitialization {
    bool attempted{false}, success{false}, cancelled{false}, initial_block_download{false}, shutdown_requested{false};
    interfaces::BlockAndHeaderTipInfo tip{};
};
struct Effects {
    std::function<NativeInitialization(const std::atomic_bool&)> initialize;
    std::function<void()> interrupt;
    std::function<int()> shutdown;
    // Called only inside a Native effect, where exception conversion/logging
    // and diagnostic I/O are permitted. Transport rejection uses bounded text.
    std::function<QString(std::exception_ptr)> describe;
};
enum class Event { State, Initialized, Interrupted, Shutdown, Fatal };
struct Projection {
    Phase phase{Phase::Constructed};
    Event event{Event::State};
    std::optional<NativeInitialization> initialization;
    std::optional<int> core_exit_status;
    std::shared_ptr<const QString> diagnostic;
    std::shared_ptr<execution::Scheduler> diagnostic_scope;
    bool publication_failed{false};
};
/** Slice-specific Domain owner. The Qt adapter retains its final projection
 * until its existing physical worker-drain receipt arrives. Public ingress is
 * owned by one composition thread; projection() is thread-safe. Publish runs
 * on Domain and must enqueue any GUI delivery. A notification exception never
 * erases the retained receipt or replays a Native effect. */
class Driver {
public:
    using Publish=std::function<void(Projection)>;
    Driver(execution::SnapshotServices execution,Effects effects,Publish publish);
    ~Driver();
    bool initialize();
    bool interrupt();
    bool shutdown();
    Projection projection() const;
    bool retryPublish();
    // Close retires mailbox diagnostic leases; existing projection copies keep
    // their original leases. Raw initialization/exit facts remain readable.
    void close();
private:
    struct Mailbox;
    struct State;
    execution::SnapshotServices m_execution;
    std::weak_ptr<State> m_state;
    std::shared_ptr<State> m_owner;
    std::shared_ptr<std::atomic_bool> m_stopping;
    std::shared_ptr<Mailbox> m_mailbox;
};
} // namespace nodelifecycle
#endif // BITCOIN_QML_NODELIFECYCLEWORKER_H
