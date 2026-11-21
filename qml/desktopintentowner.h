// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_DESKTOPINTENTOWNER_H
#define BITCOIN_QML_DESKTOPINTENTOWNER_H

#include <qml/desktopintent.h>
#include <qml/execution.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <mutex>

namespace desktopintent {
enum class Knowledge { Accepted, NotAccepted, Unknown };
struct InspectionReceipt {
    Knowledge knowledge{Knowledge::Unknown};
    uint64_t inspection{0};
};
/** One registered inspection receiver. Both methods retain/query the exact
 * ticket before acknowledging; query never creates another Source owner. */
struct Sink {
    uint64_t identity{0};
    using Completed = std::function<void(InspectionReceipt)>;
    std::function<execution::Admission(Delivery, Completed)> inspect;
    std::function<execution::Admission(Ticket, Completed)> query;
    // Revoke unused presentation. Admitted Source/child effects keep their
    // own outcomes; this is not a rollback or cancellation acknowledgment.
    std::function<void(Ticket)> retire;
};
struct Services {
    using Time = std::chrono::steady_clock::time_point;
    std::shared_ptr<execution::Scheduler> scheduler;
    std::function<Time()> now{std::chrono::steady_clock::now};
    std::function<void(std::chrono::milliseconds, std::function<void()>)> wake;
    std::chrono::milliseconds receipt_timeout{250};
    // Native authority fences a terminal parent before GUI phase publication.
    std::function<bool()> available{};
};

/** Process-lifetime Domain owner. Native/GUI entry points only submit owned
 * values; foreign acknowledgments enter distinct reserved reply cells. */
class Owner final : public std::enable_shared_from_this<Owner> {
public:
    using Notify = std::function<void(Snapshot)>;
    static std::shared_ptr<Owner> Create(Services services, Notify notify);
    execution::Admission admit(std::vector<Input> inputs, std::function<void(Admission)> done);
    execution::Admission prepareForwarding(std::vector<Input> arguments, std::function<void(ForwardingSnapshot)> done);
    execution::Admission readiness(uint64_t revision, bool ready);
    execution::Admission review(std::shared_ptr<const Sink> sink);
    execution::Admission registerReceiver(std::shared_ptr<const Sink> sink);
    execution::Admission releasePresentation(Ticket ticket);
    execution::Admission reconcile();
    execution::Admission dismiss();
    execution::Admission lookup(QString producer_key, std::function<void(std::optional<Receipt>)> done);
    execution::Admission stop();
    execution::Admission tick();
private:
    struct State;
    struct ReplyCell;
    struct StopCell;
    enum class ReplyKind { Inspect, Query };
    Owner(Services services, std::shared_ptr<execution::Scheduler> scope,
          std::shared_ptr<State> state, Notify notify);
    execution::Admission submit(std::function<void()> decision);
    std::shared_ptr<ReplyCell> reserve(ReplyKind kind, Ticket ticket);
    void received(uint64_t reply, ReplyKind kind, Ticket ticket, InspectionReceipt receipt);
    bool reservePresentation(Ticket ticket);
    void presentationReleased(Ticket ticket);
    bool reserveStop();
    void stopped();
    void query();
    void deliver(std::shared_ptr<const Sink> sink);
    void pump();
    void retireObsolete();
    void wake();
    void publish();
    const Services m_services;
    const std::shared_ptr<execution::Scheduler> m_scope;
    const execution::ResourceKey m_key;
    const std::shared_ptr<State> m_state;
    const Notify m_notify;
    const std::shared_ptr<std::atomic_bool> m_closed{std::make_shared<std::atomic_bool>(false)};
    std::mutex m_admission_mutex; // Bounded submission only; never held across Native work or callbacks.
    std::atomic_bool m_forwarding_closed{false};
    std::mutex m_presentation_mutex;
    std::optional<std::pair<Ticket, execution::TaskId>> m_presentation_release;
    std::shared_ptr<StopCell> m_stop;
};
} // namespace desktopintent
#endif // BITCOIN_QML_DESKTOPINTENTOWNER_H
