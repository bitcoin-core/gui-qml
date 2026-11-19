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
#include <vector>

namespace interfaces { class Chain; }

namespace walletopening {
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
    std::shared_ptr<const void> unload_observation{}; // Reserved Native handler lifetime, including initialization.
    void observeUnload() const noexcept {
        if (!clock || !unloaded) return;
        const auto stamp = clock->next();
        uint64_t empty{0};
        unloaded->compare_exchange_strong(empty, stamp ? stamp : std::numeric_limits<uint64_t>::max());
    }
};

/** Shared application admission/observation fence. An accepted Action owns its
 * original permit; this fence never cancels that action or its outcome. */
class AdmissionFence {
public:
    AdmissionFence() = default;
    explicit AdmissionFence(std::shared_ptr<std::atomic_bool> gate) : m_gate(std::move(gate)) {}
    operator bool() const { return m_local || (m_gate && m_gate->load()); }
    AdmissionFence& operator=(bool closed) { m_local = closed; return *this; }
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
    // The wallet's default type when -addresstype was set explicitly, else
    // empty. Receive starts from it while no type has been chosen (#1137).
    std::string configured_receive_type;
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
 * Core and Domain use the injected shared executor. Retirement uses the
 * existing compatibility executor until its remaining borrowers migrate;
 * this creates no executor/thread and imposes no idle-wallet inventory cap
 * on the shared command queue's cleanup budget.
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
    Session(std::shared_ptr<execution::Scheduler> commands,
            std::shared_ptr<execution::Scheduler> retirement,
            std::shared_ptr<std::atomic_bool> closed, Notify notify, Preferences preferences = {}, Clock clock = std::chrono::steady_clock::now, std::shared_ptr<std::atomic_bool> shutdown_gate = {}, NativeEvents native_events = {});
    struct Start {
        execution::Admission admission;
        std::shared_ptr<interfaces::Wallet> borrower;
        explicit operator bool() const { return bool(admission); }
    };
    Start start(const std::shared_ptr<interfaces::Wallet>& wallet);
    // A counted public-interface borrower, retained through legacy work and
    // outcome destruction. The final interface/handler release is on Cleanup.
    std::shared_ptr<interfaces::Wallet> borrow() const;
    Opening identity() const { return m_identity; }
    std::shared_ptr<execution::Scheduler> scheduler() const { return m_commands; }
    std::shared_ptr<std::atomic_bool> closedFlag() const { return m_closed; }
    bool observationOpen() const { return !m_closed->load() && (!m_shutdown_gate || !m_shutdown_gate->load()) && (!m_native_events.unloaded || !m_native_events.unloaded->load()); }
    bool available() const { return !m_admission_closed.load() && observationOpen(); }
    void closeAdmission();
    bool admitAction();
    class Action {
    public:
        Opening identity() const { return m_identity; }
    private:
        friend class Session;
        Action(Opening identity, std::shared_ptr<execution::Scheduler> scope, execution::ResourceKey work_key,
               std::shared_ptr<interfaces::Wallet> wallet)
            : m_identity(std::move(identity)), m_scope(std::move(scope)), m_work_key(work_key), m_wallet(std::move(wallet)) {}
        const Opening m_identity;
        const std::shared_ptr<execution::Scheduler> m_scope; // Released after its original wallet borrower.
        const execution::ResourceKey m_work_key;
        const std::shared_ptr<interfaces::Wallet> m_wallet;
    };
    // App-side admission/lifetime only, not a Core/RPC authorization lease.
    std::shared_ptr<const Action> startAction();
    // File/export continuation work does not access Wallet. It shares the
    // opening's admission lifetime but uses a private action key so a
    // pre-reserved Native retirement receipt can run after queue pressure.
    execution::Admission submitActionWork(const std::shared_ptr<const Action>& action,
                                         std::function<void()> work);
    execution::Admission reserveActionCleanup(const std::shared_ptr<const Action>& action,
                                              std::function<void()> work);
    bool activateActionCleanup(const std::shared_ptr<const Action>& action, execution::TaskId id);
    void requestClose();
    void refreshMetadata() { ingress(4); }
    void retryNotifications() { scheduleRefresh(); }
    execution::Admission postDomain(std::function<void()> work, execution::WorkClass kind = execution::WorkClass::Observation);
    // Pure value teardown uses the shared Cleanup lane, independent of Core
    // and Domain. It owns no wallet handle and never waits for GUI delivery.
    execution::Admission retireValues(std::function<void()> work);
    // Reserve value teardown before retaining a shared input. The returned
    // lease gates cleanup through rejection/closure, without a GUI completion.
    execution::BorrowAdmission holdValues(std::function<void()> cleanup);
    uint64_t transactionRevision() const;
    // Captured atomic facts only: no Session/Wallet lifetime or GUI Core read.
    std::function<bool()> observationGuard() const;
    execution::Admission planAction(const std::shared_ptr<const Action>& action,
                                    std::function<void()> work);
    using Transactions = std::function<void(uint64_t, std::map<Txid, ChangeType>, bool)>;
    // Install the opening's backend receipt consumer, not a GUI subscription.
    // Its callback is invoked on Domain; it must retain only a weak owner.
    void onTransactions(Transactions consumer);
    execution::Admission setReceivePreference(std::string type, std::shared_ptr<const Action> original = {}, std::function<void(bool)> done = {});
    // Native-only preference read; selecting a template never writes it.
    // Skips types already found unavailable in this opening.
    OutputType preferredReceiveType(interfaces::Wallet& wallet) const;
    // Record that Core has no active descriptor for this receive type.
    void markReceiveTypeUnavailable(OutputType type);
    std::vector<std::string> unavailableReceiveTypes() const;
    uint64_t preferenceRevision() const { return m_preference_revision.load(); }
    void answerClose(Question question, bool accept);
    void retire(bool remove_wallet = false);
    std::optional<Update> take();
    void acknowledgeTransactions();
    bool notificationsPending() const { return m_transactions_pending.load(); }

    template <typename Work, typename Done>
    execution::Admission perform(const std::shared_ptr<const Action>& action, Work work, Done done,
                                 std::function<void(std::exception_ptr)> failed = {})
    {
        if (!action || action->m_identity != m_identity) return {{}, execution::Rejection::Invalid};
        return execution::Submit(*action->m_scope, [action, work = std::move(work)]() mutable {
            return work(*action->m_wallet);
        }, [action, done = std::move(done)](auto result) mutable { done(std::move(result)); },
        [action, failed = std::move(failed)](std::exception_ptr error) { if (failed) failed(error); },
        {execution::Lane::Core, m_command_key, execution::WorkClass::Action});
    }


    template <typename Work, typename Done>
    execution::Admission read(Work work, Done done, std::function<void(std::exception_ptr)> failed = {}, execution::WorkClass kind = execution::WorkClass::Observation)
    {
        if (kind == execution::WorkClass::Action && !admitAction()) return {{}, execution::Rejection::Closed};
        auto wallet = borrow();
        if (!wallet) return {{}, execution::Rejection::Closed};
        const auto held = std::make_shared<std::shared_ptr<interfaces::Wallet>>(std::move(wallet));
        return execution::Submit(*m_commands, [held, work = std::move(work), closed = m_closed, gate = m_shutdown_gate, kind]() mutable {
            if (kind == execution::WorkClass::Observation && (closed->load() || (gate && gate->load()))) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
            return work(**held);
        }, [held, done = std::move(done)](auto result) mutable { done(std::move(result)); },
        [held, failed = std::move(failed)](std::exception_ptr error) { if (failed) failed(error); },
        {execution::Lane::Core, m_command_key, kind});
    }
private:
    struct Resources;
    void ingress(int kind, const Txid& txid = {}, ChangeType change = CT_UPDATED);
    void scheduleRefresh();
    void refreshed(Metadata metadata, Update events);
    void refreshFailed(bool transactions);
    void publish(Update update);
    void publishState();
    bool cleanup();
    bool control(std::function<void()> effect);

    const Opening m_identity{std::make_shared<const Identity>()};
    const std::shared_ptr<execution::Scheduler> m_commands, m_retirement;
    const execution::ResourceKey m_command_key, m_retirement_key;
    const std::shared_ptr<std::atomic_bool> m_closed;
    const std::shared_ptr<std::atomic_bool> m_shutdown_gate;
    std::atomic_bool m_admission_closed{false};
    const Notify m_notify;
    const Preferences m_preferences;
    const Clock m_clock;
    const NativeEvents m_native_events;
    const std::shared_ptr<SourceEpoch> m_source_epoch{std::make_shared<SourceEpoch>()};
    std::shared_ptr<Resources> m_resources;
    State m_state{m_identity}; // Shared command Domain only
    std::atomic<uint64_t> m_preference_revision{0};
    std::atomic<uint64_t> m_consequences{0}; // transport admission, not Core authority
    std::atomic_bool m_transactions_pending{false}, m_drained{false};
    bool m_started{false};
    mutable std::mutex m_mutex;
    mutable std::mutex m_receive_types_mutex;
    std::set<OutputType> m_unavailable_receive_types; // guarded by m_receive_types_mutex
    std::optional<execution::TaskId> m_cleanup;
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
