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

namespace execution { class SerialQueue; }

namespace desktopintent {
enum class Knowledge { Accepted, NotAccepted, Unknown };
struct InspectionReceipt {
    Knowledge knowledge{Knowledge::Unknown};
    uint64_t inspection{0};
};
/** One registered inspection receiver. Both methods retain/query the exact
 * key before acknowledging; query never creates another Source owner. */
struct Sink {
    uint64_t identity{0};
    using Completed = std::function<void(InspectionReceipt)>;
    std::function<execution::SubmitResult(Delivery, Completed)> inspect;
    std::function<execution::SubmitResult(DeliveryKey, Completed)> query;
    // Revoke unused presentation. Admitted Source/child effects keep their
    // own outcomes; this is not a rollback or cancellation acknowledgment.
    std::function<void(DeliveryKey)> retire;
};
struct Services {
    using Time = std::chrono::steady_clock::time_point;
    std::function<Time()> now{std::chrono::steady_clock::now};
    std::function<void(std::chrono::milliseconds, std::function<void()>)> wake;
    std::chrono::milliseconds receipt_timeout{250};
    // Native authority fences a terminal parent before GUI phase publication.
    std::function<bool()> available{};
};

/** Process-lifetime owner. Native and GUI entry points submit owned values.
 * Foreign acknowledgments keep the original delivery until it is accounted for. */
class Owner final : public std::enable_shared_from_this<Owner> {
public:
    using Notify = std::function<void(Snapshot)>;
    static std::shared_ptr<Owner> Create(Services services, Notify notify,
                                         std::shared_ptr<execution::SerialQueue> queue);
    execution::SubmitResult admit(std::vector<Input> inputs, std::function<void(Acceptance)> done);
    execution::SubmitResult prepareForwarding(std::vector<Input> arguments, std::function<void(ForwardingSnapshot)> done);
    execution::SubmitResult readiness(uint64_t revision, bool ready);
    execution::SubmitResult review(std::shared_ptr<const Sink> sink);
    execution::SubmitResult registerReceiver(std::shared_ptr<const Sink> sink);
    execution::SubmitResult releasePresentation(DeliveryKey key);
    execution::SubmitResult reconcile();
    execution::SubmitResult dismiss();
    execution::SubmitResult lookup(QString producer_key, std::function<void(std::optional<Receipt>)> done);
    execution::SubmitResult stop();
    execution::SubmitResult tick();
private:
    struct State;
    struct ReplyCell;
    enum class ReplyKind { Inspect, Query };
    Owner(Services services, std::shared_ptr<State> state, Notify notify, std::shared_ptr<execution::SerialQueue> queue);
    bool post(std::function<void()> decision, bool must_deliver = false);
    execution::SubmitResult submit(std::function<void()> decision);
    std::shared_ptr<ReplyCell> reserve(ReplyKind kind, DeliveryKey key);
    void received(uint64_t reply, ReplyKind kind, DeliveryKey key, InspectionReceipt receipt);
    bool reservePresentation(DeliveryKey key);
    void presentationReleased(DeliveryKey key);
    void stopped();
    void query();
    void deliver(std::shared_ptr<const Sink> sink);
    void pump();
    void retireObsolete();
    void wake();
    void publish();
    const Services m_services;
    const std::shared_ptr<execution::SerialQueue> m_queue;
    const std::shared_ptr<State> m_state;
    const Notify m_notify;
    const std::shared_ptr<std::atomic_bool> m_closed{std::make_shared<std::atomic_bool>(false)};
    // Orders admission with the forwarding fence. Test dispatch can run inline.
    std::recursive_mutex m_admission_mutex;
    std::recursive_mutex m_decision_mutex;
    std::atomic_bool m_forwarding_closed{false};
    std::mutex m_presentation_mutex;
    std::optional<DeliveryKey> m_presentation_release;
};
} // namespace desktopintent
#endif // BITCOIN_QML_DESKTOPINTENTOWNER_H
