// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETLIFECYCLE_H
#define BITCOIN_QML_WALLETLIFECYCLE_H

#include <qml/execution.h>
#include <qml/walletlifecyclestate.h>
#include <QString>

#include <interfaces/handler.h>
#include <interfaces/wallet.h>

#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

class WalletLifecycleTests;
namespace walletpublication { class NativeObservation; class ObservedLoad; }

namespace walletlife {
/** One existing catalog subscription, not the full wallet/session model. */
class LoadSubscription : public std::enable_shared_from_this<LoadSubscription>
{
    // Unit tests inject a continuation paused at the private admission entry;
    // they retain the production scheduler and never mutate lifecycle state.
    friend class ::WalletLifecycleTests;
    struct ReturnPacket;
public:
    struct UnloadObservation {
        std::shared_ptr<std::atomic<uint64_t>> unloaded{std::make_shared<std::atomic<uint64_t>>(0)};
        std::unique_ptr<interfaces::Handler> handler;
        ~UnloadObservation() { if (handler) handler->disconnect(); }
    };
    struct Entry {
        std::string name;
        std::shared_ptr<walletpublication::ObservedLoad> wallet;
        bool select_wallet{true};
        uint64_t selection_revision{0};
        std::shared_ptr<void> adoption_origin;
        uint64_t ingress_sequence{0}, notification_sequence{0}, settlement_sequence{0};
        std::unique_ptr<interfaces::Wallet> initial_wallet;
        // Destroy the unload handler before an unclaimed initial wallet.
        std::shared_ptr<UnloadObservation> unload_observation;
    };
    using Callback = std::function<void(std::unique_ptr<interfaces::Wallet>)>;
    using Register = std::function<std::unique_ptr<interfaces::Handler>(Callback)>;
    using Capture = std::function<std::vector<std::unique_ptr<interfaces::Wallet>>()>;
    // Called on Domain. The implementation transports only this value to GUI.
    using Notify = std::function<void(Update)>;
    struct Handoff {
        ~Handoff();
        std::vector<Entry> entries;
        bool more{false};
    private:
        friend class LoadSubscription;
        Handoff() = default;
        Handoff(std::shared_ptr<LoadSubscription> owner, std::shared_ptr<ReturnPacket> packet,
                execution::TaskId disposal, std::optional<execution::ResourceLease> lease)
            : m_owner(std::move(owner)), m_packet(std::move(packet)), m_disposal(disposal), m_lease(std::move(lease)) {}
        std::shared_ptr<LoadSubscription> m_owner;
        std::shared_ptr<ReturnPacket> m_packet;
        execution::TaskId m_disposal;
        std::optional<execution::ResourceLease> m_lease;
    };

    LoadSubscription(std::shared_ptr<execution::Scheduler> scheduler, Register registration, Capture capture, Notify notify, std::function<uint64_t()> ingress_stamp = {}, std::function<std::string(interfaces::Wallet&)> read_name = {});
    execution::SubmitResult start();
    void retire();
    bool setNotificationContext(QString deferred_name, bool action_pending, uint64_t selection_revision = 0, std::shared_ptr<void> adoption_origin = {}, std::shared_ptr<walletpublication::NativeObservation> creation_observation = {});
    // GUI adapter could not retain an original notification context.
    void rejectNotificationContext();
    void acknowledgeChange();
    std::unique_ptr<Handoff> takeInitial();
    std::unique_ptr<Handoff> takeNotifications();
    enum class Settlement { Pending, Settled, Unavailable };
    // A local delivery boundary, never a backend or opening identity. A Core
    // effect captures it after returning; later notifications cannot prolong it.
    uint64_t notificationFence() const;
    Settlement settlementThrough(uint64_t fence) const;

private:
    static constexpr size_t GUI_HANDOFF{16};
    void ingress(std::unique_ptr<interfaces::Wallet> wallet);
    void requestScan();
    void scan();
    void registered();
    void publishChanged();
    void requestFailure(FailureReason reason);
    void failed();
    bool cleanup();
    std::unique_ptr<Handoff> take(bool initial);
    void returnHandoff(std::vector<Entry> entries, std::shared_ptr<ReturnPacket> packet, execution::TaskId disposal);
    std::vector<Entry> captureEntries(std::vector<std::unique_ptr<interfaces::Wallet>> wallets);
    std::shared_ptr<UnloadObservation> subscribeUnload(interfaces::Wallet& wallet);

    const std::shared_ptr<execution::Scheduler> m_scheduler;
    const Register m_registration;
    const Capture m_capture;
    const Notify m_notify;
    const std::function<uint64_t()> m_ingress_stamp;
    const std::function<std::string(interfaces::Wallet&)> m_read_name;
    const execution::ResourceKey m_key;
    State m_state; // Accessed only by Domain outcome consumers.
    std::atomic<FailureReason> m_failure{FailureReason::None};
    std::atomic_bool m_stopping{false}, m_scan_pending{false}, m_failed{false};
    mutable std::mutex m_mutex;
    uint64_t m_notification_sequence{0};
    std::set<uint64_t> m_unpublished;
    bool m_started{false}, m_initial_ready{false}, m_handoff_disposal_pending{false}, m_change_publication_pending{false};
    std::optional<execution::TaskId> m_cleanup_id;
    std::optional<execution::TaskId> m_scan_id;
    // Contexts are captured on GUI without UTF8 conversion, then retained and
    // converted only by the original Native subscription scan.
    struct NotificationContext {
        QString deferred_name;bool select_wallet{true};uint64_t selection_revision{0};
        std::shared_ptr<void> adoption_origin;
        std::shared_ptr<walletpublication::NativeObservation> creation_observation;
        mutable std::optional<std::string> native_name;
        const std::string& nativeName()const;
    };
    struct Pending { std::shared_ptr<walletpublication::ObservedLoad> wallet; std::shared_ptr<const NotificationContext> context; uint64_t ingress_sequence{0}, notification_sequence{0}; };
    std::shared_ptr<const NotificationContext> m_notification_context;
    std::unique_ptr<interfaces::Handler> m_handler;
    std::vector<Pending> m_pending;
    std::deque<Entry> m_initial;
    std::map<std::string, Entry> m_ready;
};
} // namespace walletlife

#endif // BITCOIN_QML_WALLETLIFECYCLE_H
