// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_NODELIFECYCLEWORKER_H
#define BITCOIN_QML_NODELIFECYCLEWORKER_H

#include <interfaces/node.h>
#include <qml/nodelifecycle.h>
#include <qml/execution.h>
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
    // Releases the native thread after the last accepted turn.
    std::function<void()> retire;
};
enum class Event { State, Initialized, Interrupted, Shutdown, Fatal };
struct Projection {
    Phase phase{Phase::Constructed};
    Event event{Event::State};
    std::optional<NativeInitialization> initialization;
    std::optional<int> core_exit_status;
    std::shared_ptr<const QString> diagnostic;
    bool publication_failed{false};
};
/** Lifecycle decisions are serialized across the native thread and Priority.
 * Publish enqueues GUI delivery. The Qt adapter acknowledges final shutdown
 * after the native thread joins. */
class Driver {
public:
    using Post = std::function<bool(std::shared_ptr<execution::Task>)>;
    using Publish=std::function<void(Projection)>;
    Driver(Post native, Post priority, Effects effects, Publish publish);
    ~Driver();
    bool initialize();
    bool interrupt();
    bool shutdown();
    Projection projection() const;
    bool retryPublish();
    // Close releases mailbox diagnostics. Existing projection copies keep
    // their diagnostic text. Raw initialization and exit facts remain readable.
    void close();
private:
    struct Mailbox;
    struct State;
    std::shared_ptr<State> m_state;
    std::shared_ptr<std::atomic_bool> m_stopping;
    std::shared_ptr<Mailbox> m_mailbox;
};
} // namespace nodelifecycle
#endif // BITCOIN_QML_NODELIFECYCLEWORKER_H
