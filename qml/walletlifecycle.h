// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETLIFECYCLE_H
#define BITCOIN_QML_WALLETLIFECYCLE_H

#include <qml/execution.h>
#include <qml/executor.h>
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

namespace execution { class ValueCleanupQueue; }
class WalletLifecycleTests;
namespace walletpublication { class NativeObservation; class ObservedLoad; }

namespace walletlife {
/** One existing catalog subscription, not the full wallet/session model. */
class LoadSubscription : public std::enable_shared_from_this<LoadSubscription>
{
    // Unit tests inject a continuation paused at the private admission entry;
    // they retain the production scheduler and never mutate lifecycle state.
    friend class ::WalletLifecycleTests;
    struct ActiveWork;
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
    // Called on Utility. The implementation transports only this value to GUI.
    using Notify = std::function<void(Update)>;
    struct Handoff {
        ~Handoff();
        std::vector<Entry> entries;
        bool more{false};
    private:
        friend class LoadSubscription;
        Handoff() = default;
        std::shared_ptr<void> m_dispose;
        std::shared_ptr<std::vector<Entry>> m_retirement;
    };

    LoadSubscription(execution::Executor* executor, Register registration, Capture capture, Notify notify,
                     std::function<uint64_t()> ingress_stamp = {}, std::function<std::string(interfaces::Wallet&)> read_name = {});
    execution::SubmitResult start();
    void retire();
    // Replays after Utility cleanup.
    void whenRetired(std::function<void()> done);
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
    void registerWallets();
    std::shared_ptr<ActiveWork> beginWork();
    void endWork();
    bool postCore(void (LoadSubscription::*work)(), void (LoadSubscription::*done)());
    void scheduleCleanup();
    void cleanupFinished(bool cleanup_failed);
    void ingress(std::unique_ptr<interfaces::Wallet> wallet);
    void requestScan();
    void scan();
    void scanFinished();
    void registered();
    void publishChanged();
    void requestFailure(FailureReason reason);
    void failed();
    bool cleanup();
    std::unique_ptr<Handoff> take(bool initial);
    void handoffDisposed(bool initial);
    std::vector<Entry> captureEntries(std::vector<std::unique_ptr<interfaces::Wallet>> wallets);
    std::shared_ptr<UnloadObservation> subscribeUnload(interfaces::Wallet& wallet);

    const std::shared_ptr<execution::SerialQueue> m_core;
    const std::shared_ptr<execution::SerialQueue> m_utility;
    const std::shared_ptr<execution::ValueCleanupQueue> m_outcomes, m_retirements;
    const Register m_registration;
    const Capture m_capture;
    const Notify m_notify;
    const std::function<uint64_t()> m_ingress_stamp;
    const std::function<std::string(interfaces::Wallet&)> m_read_name;
    State m_state; // Accessed only by Domain outcome consumers.
    std::atomic<FailureReason> m_failure{FailureReason::None};
    std::atomic_bool m_stopping{false}, m_scan_pending{false}, m_failed{false};
    mutable std::mutex m_mutex;
    uint64_t m_notification_sequence{0};
    std::set<uint64_t> m_unpublished;
    bool m_started{false}, m_initial_ready{false}, m_handoff_disposal_pending{false}, m_change_publication_pending{false};
    size_t m_active_work{0};
    std::function<void(std::shared_ptr<LoadSubscription>)> m_retire_on_utility;
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
