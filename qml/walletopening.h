// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETOPENING_H
#define BITCOIN_QML_WALLETOPENING_H

#include <qml/sourceepoch.h>

#include <qml/execution.h>
#include <qml/walletopeningstate.h>

#include <interfaces/handler.h>
#include <interfaces/wallet.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <map>
#include <limits>
#include <mutex>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace interfaces { class Chain; }
namespace execution { class SerialQueue; class ValueCleanupQueue; }

namespace walletopening {
using Source = std::variant<std::shared_ptr<interfaces::Wallet>, std::unique_ptr<interfaces::Wallet>>;
/** Order observed application callbacks, never infer a Core backend identity. */
struct NativeEventClock {
    std::atomic<uint64_t> value{0};
    uint64_t next() noexcept {
        auto previous = value.load();
        while (previous != std::numeric_limits<uint64_t>::max()) {
            if (value.compare_exchange_weak(previous, previous + 1)) return previous + 1;
        }
        return 0;
    }
};
struct NativeEvents {
    std::shared_ptr<NativeEventClock> clock;
    std::shared_ptr<std::atomic<uint64_t>> unloaded;
    void observeUnload() const noexcept {
        if (!clock || !unloaded) return;
        const auto stamp = clock->next();
        uint64_t empty{0};
        unloaded->compare_exchange_strong(empty, stamp ? stamp : std::numeric_limits<uint64_t>::max());
    }
};

/** Shared application admission/observation fence. An accepted Action owns its
 * original permit; this fence never cancels that action or its outcome. */
class ClosedFlag {
public:
    ClosedFlag() = default;
    explicit ClosedFlag(std::shared_ptr<std::atomic_bool> gate) : m_gate(std::move(gate)) {}
    operator bool() const { return m_local || (m_gate && m_gate->load()); }
    ClosedFlag& operator=(bool closed) { m_local = closed; return *this; }
    std::shared_ptr<std::atomic_bool> shared() const { return m_gate; }
private:
    std::shared_ptr<std::atomic_bool> m_gate;
    bool m_local{false};
};
// Application policy supplied by public interface composition, read on Core.
// Wallet consumers never reach through interfaces::Wallet to CWallet/chain.
struct FeePolicy {
    std::function<CAmount()> minimum_relay;
};
struct Metadata {
    std::string name;
    bool private_keys_disabled{false}, external_signer{false};
    bool encrypted{false}, locked{false};
    bool can_get_addresses{false}, taproot{false};
    std::string receive_preference;
    // Types Core reported it has no active descriptor for during this opening
    // (#1088). Learned from failed generation only: interfaces::Wallet has no
    // non-mutating per-type capability query, so untried types stay unknown.
    std::vector<std::string> unavailable_receive_types;
};
struct Update {
    std::optional<Metadata> metadata;
    std::optional<bool> preference_saved;
    uint64_t preference_revision{0};
    uint64_t transaction_revision{0};
    std::map<Txid, ChangeType> transactions;
    bool addresses{false}, status{false}, rescan{false}, unloaded{false}, failed{false};
    Phase phase{Phase::Open};
    std::optional<Question> question;
};

/** One retained owner per opening. No Qt objects or private Core access.
 * Each application opening uses a Core queue on the shared executor, with
 * Utility retiring its original wallet and handlers.
 */
class Session : public std::enable_shared_from_this<Session>
{
public:
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    using Notify = std::function<void()>; // enqueue-only retained GUI transport
    struct Preferences {
        std::function<std::string(const std::string&)> read;
        std::function<bool(const std::string&, const std::string&)> write;
    };
    struct Queues {
        std::shared_ptr<execution::SerialQueue> cleanup;
        std::function<void(Notify)> stop_work;
        std::shared_ptr<execution::SerialQueue> core;
        std::shared_ptr<execution::SerialQueue> outcomes;
        // Releases the native wallet, which can wait for Core. Defaults to cleanup.
        std::shared_ptr<execution::SerialQueue> native_cleanup{};
    };
    Session(std::shared_ptr<std::atomic_bool> closed, Notify notify, Preferences preferences, Clock clock,
            std::shared_ptr<std::atomic_bool> shutdown_gate, NativeEvents native_events, Queues queues);
    struct Start {
        execution::SubmitResult submitted;
        std::shared_ptr<interfaces::Wallet> borrower;
        explicit operator bool() const { return bool(submitted); }
    };
    Start start(const std::shared_ptr<interfaces::Wallet>& wallet);
    // Leaves both handles in place if the opening cannot take ownership.
    Start start(std::unique_ptr<interfaces::Wallet>& wallet, std::unique_ptr<interfaces::Handler>& unload_handler);
    Start start(Source& source, std::unique_ptr<interfaces::Handler>& unload_handler);
    // Retire a handoff that start did not acquire, before requesting stop.
    void retireUnclaimed(Source& source, std::unique_ptr<interfaces::Handler>& unload_handler);
    // A counted public-interface borrower. The final interface and handler
    // release runs on the retirement worker.
    std::shared_ptr<interfaces::Wallet> borrow() const;
    Opening identity() const { return m_identity; }
    std::shared_ptr<execution::SerialQueue> coreQueue() const { return m_core_queue; }
    std::shared_ptr<execution::SerialQueue> utilityQueue() const { return m_outcomes; }
    std::shared_ptr<execution::SerialQueue> cleanupQueue() const { return m_cleanup_queue; }
    std::function<execution::SubmitResult(std::shared_ptr<execution::Task>)> observationPost(
        execution::Lane lane = execution::Lane::Core);
    std::shared_ptr<std::atomic_bool> closedFlag() const { return m_closed; }
    bool observationOpen() const { return !m_closed->load() && (!m_shutdown_gate || !m_shutdown_gate->load()) && (!m_native_events.unloaded || !m_native_events.unloaded->load()); }
    bool available() const { return !m_actions_closed.load() && observationOpen(); }
    void close();
    bool admitAction();
    class Action {
    public:
        ~Action();
        Action(const Action&) = delete;
        Action& operator=(const Action&) = delete;
        Opening identity() const { return m_identity; }
    private:
        friend class Session;
        explicit Action(Opening identity) : m_identity(std::move(identity)) {}
        const Opening m_identity;
        std::shared_ptr<Session> m_owner;
    };
    // App-side admission/lifetime only, not a Core/RPC authorization lease.
    std::shared_ptr<const Action> startAction();
    void requestClose();
    void refreshMetadata() { ingress(4); }
    void retryNotifications() { scheduleRefresh(); }
    execution::SubmitResult postDomain(std::function<void()> work, execution::WorkClass kind = execution::WorkClass::Observation);
    execution::SubmitResult postDomainTask(std::shared_ptr<execution::Task> packet, execution::WorkClass kind);
    // Value work with a completion reports its outcome once accepted.
    template <typename Work, typename Done>
    execution::SubmitResult postDomain(Work work, Done done, std::function<void(std::exception_ptr)> failed,
                                       execution::WorkClass kind = execution::WorkClass::Observation)
    {
        return postDomainTask(execution::MakeTask(std::move(work), std::move(done), std::move(failed)), kind);
    }
    // Value teardown runs on Utility and needs no GUI delivery.
    execution::SubmitResult retireValues(std::function<void()> work);
    // Reserve value teardown before retaining a shared input. The returned
    // lease gates cleanup through rejection/closure, without a GUI completion.
    std::shared_ptr<void> holdValues(std::function<void()> cleanup);
    uint64_t transactionRevision() const;
    // Captured atomic facts only: no Session/Wallet lifetime or GUI Core read.
    std::function<bool()> observationGuard() const;
    execution::SubmitResult planAction(const std::shared_ptr<const Action>& action,
                                    std::function<void()> work);
    using Transactions = std::function<void(uint64_t, std::map<Txid, ChangeType>, bool)>;
    // Install the opening's backend receipt consumer, not a GUI subscription.
    // Its callback is invoked on Domain; it must retain only a weak owner.
    void onTransactions(Transactions consumer);
    execution::SubmitResult setReceivePreference(std::string type, std::shared_ptr<const Action> original = {}, std::function<void(bool)> done = {});
    // Native-only preference read; selecting a template never writes it.
    // Skips types already found unavailable in this opening.
    OutputType preferredReceiveType(interfaces::Wallet& wallet) const;
    // Record that Core has no active descriptor for this receive type.
    void markReceiveTypeUnavailable(OutputType type);
    std::vector<std::string> unavailableReceiveTypes() const;
    uint64_t preferenceRevision() const { return m_preference_revision.load(); }
    void answerClose(Question question, bool accept);
    void retire(bool remove_wallet = false);
    //! Closes the opening and acknowledges native cleanup once it finishes.
    //! The callback enqueues delivery to the caller's owning thread.
    void stop(bool remove_wallet, Notify done);
    std::optional<Update> take();
    void acknowledgeTransactions();
    bool notificationsPending() const { return m_transactions_pending.load(); }

    template <typename Work, typename Done>
    execution::SubmitResult perform(const std::shared_ptr<const Action>& action, Work work, Done done,
                                 std::function<void(std::exception_ptr)> failed = {})
    {
        if (!action || action->m_identity != m_identity) return {{}, execution::Rejection::Invalid};
        auto packet = execution::MakeTask([self = shared_from_this(), action, work = std::move(work)]() mutable {
            return work(self->nativeWallet());
        }, [action, done = std::move(done)](auto result) mutable { done(std::move(result)); },
        [action, failed = std::move(failed)](std::exception_ptr error) { if (failed) failed(error); });
        return postCore(std::move(packet));
    }


    template <typename Work, typename Done>
    execution::SubmitResult read(Work work, Done done, std::function<void(std::exception_ptr)> failed = {}, execution::WorkClass kind = execution::WorkClass::Observation)
    {
        if (kind == execution::WorkClass::Action) {
            const auto action = startAction();
            if (!action) return {{}, execution::Rejection::Closed};
            return perform(action, std::move(work), std::move(done), std::move(failed));
        }
        if (!borrow()) return {{}, execution::Rejection::Closed};
        return postCore(execution::MakeTask([self = shared_from_this(), work = std::move(work)]() mutable {
            if (self->m_closed->load() || (self->m_shutdown_gate && self->m_shutdown_gate->load())) {
                throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            }
            return work(self->nativeWallet());
        }, std::move(done), std::move(failed)));
    }
private:
    struct Resources;
    interfaces::Wallet& nativeWallet() const;
    execution::SubmitResult postCore(std::shared_ptr<execution::Task> packet);
    Start startWith(const std::function<void(Resources&)>& acquire);
    void ingress(int kind, const Txid& txid = {}, ChangeType change = CT_UPDATED);
    void scheduleRefresh();
    void refreshed(Metadata metadata, Update events);
    void refreshFailed(bool transactions);
    void publish(Update update);
    void publishState();
    void releaseWork();
    void drainWork(std::function<void(Notify)> stop_work);
    bool cleanup(Resources& resources);
    void completeStop(bool failed);
    bool control(std::function<void()> effect);

    const Opening m_identity{std::make_shared<const Identity>()};
    const std::shared_ptr<std::atomic_bool> m_closed;
    const std::shared_ptr<std::atomic_bool> m_shutdown_gate;
    std::atomic_bool m_actions_closed{false};
    const Notify m_notify;
    const Preferences m_preferences;
    const Clock m_clock;
    const NativeEvents m_native_events;
    const std::shared_ptr<execution::SerialQueue> m_cleanup_queue, m_core_queue, m_outcomes;
    const std::shared_ptr<execution::ValueCleanupQueue> m_value_cleanup, m_native_cleanup, m_outcome_delivery;
    const std::shared_ptr<ChangeCounter> m_source_epoch{std::make_shared<ChangeCounter>()};
    std::shared_ptr<Resources> m_resources;
    State m_state{m_identity}; // Shared command Domain only
    std::atomic<uint64_t> m_preference_revision{0};
    std::atomic<uint64_t> m_consequences{0}; // transport admission, not Core authority
    std::atomic_bool m_transactions_pending{false}, m_drained{false};
    bool m_started{false};
    mutable std::mutex m_mutex;
    mutable std::mutex m_receive_types_mutex;
    std::set<OutputType> m_unavailable_receive_types; // guarded by m_receive_types_mutex
    size_t m_work_references{0}; // Accepted actions and retained values.
    bool m_stop_requested{false};
    std::function<void(Notify)> m_stop_work;
    std::vector<Notify> m_stop_callbacks;
    Update m_pending;
    std::optional<Update> m_latest;
    std::chrono::steady_clock::time_point m_retry_after{};
    unsigned int m_refresh_failures{0};
    uint64_t m_transaction_revision{0};
    Transactions m_transactions_consumer;
    bool m_metadata_dirty{false};
    bool m_refresh_pending{false}, m_notice_pending{false}, m_remove{false};
};
Session::Preferences ChainPreferences(interfaces::Chain& chain);
} // namespace walletopening

#endif // BITCOIN_QML_WALLETOPENING_H
