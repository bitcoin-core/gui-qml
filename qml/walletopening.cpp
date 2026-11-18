// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletopening.h>
#include <qml/executor.h>
#include <qml/retainedvalue.h>

#include <interfaces/chain.h>
#include <addresstype.h>
#include <outputtype.h>
#include <univalue.h>

#include <algorithm>
#include <cassert>
#include <exception>
#include <stdexcept>
#include <utility>

namespace walletopening {
struct Session::Resources {
    std::unique_ptr<interfaces::Wallet> wallet;
    std::shared_ptr<interfaces::Wallet> shared_wallet;
    std::unique_ptr<interfaces::Handler> unload_handler;
    std::vector<std::unique_ptr<interfaces::Handler>> handlers;
    bool acquired{false};

    interfaces::Wallet* get() const { return wallet ? wallet.get() : shared_wallet.get(); }
};

namespace {
Metadata ReadMetadata(interfaces::Wallet& wallet, const Session::Preferences& preferences, std::vector<std::string> unavailable_receive_types)
{
    auto name = wallet.getWalletName();
    if (name.size() > 32'768) throw std::length_error("Wallet name is too long");
    auto saved = preferences.read ? preferences.read(name) : std::string{};
    // IDs are a closed vocabulary; never copy arbitrary persistent input into
    // the bounded metadata transport. Invalid values use the supported default.
    if (saved != "bech32m" && saved != "bech32" && saved != "p2sh-segwit" && saved != "legacy") saved.clear();
    return {std::move(name), wallet.privateKeysDisabled(), wallet.hasExternalSigner(), wallet.isCrypted(), wallet.isLocked(),
            wallet.canGetAddresses(), wallet.taprootEnabled(), std::move(saved), std::move(unavailable_receive_types)};
}

execution::SubmitResult PostUtility(execution::SerialQueue& queue, std::function<void()> work)
{
    const bool accepted = queue.post([work = std::move(work)] {
        // These turns have no outcome callback. A failed turn must still
        // leave the worker available for the next one.
        try {
            work();
        } catch (...) {}
    });
    return {{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
}
}

Session::Session(std::shared_ptr<std::atomic_bool> closed, Notify notify, Preferences preferences, Clock clock,
                 std::shared_ptr<std::atomic_bool> shutdown_gate, NativeEvents native_events, Queues queues)
    : m_closed(std::move(closed)), m_shutdown_gate(std::move(shutdown_gate)), m_notify(std::move(notify)), m_preferences(std::move(preferences)), m_clock(std::move(clock)), m_native_events(std::move(native_events)),
      m_cleanup_queue(std::move(queues.cleanup)), m_core_queue(std::move(queues.core)), m_outcomes(std::move(queues.outcomes)),
      m_value_cleanup(std::make_shared<execution::ValueCleanupQueue>(m_cleanup_queue)),
      m_native_cleanup(std::make_shared<execution::ValueCleanupQueue>(queues.native_cleanup ? queues.native_cleanup : m_cleanup_queue)),
      m_outcome_delivery(std::make_shared<execution::ValueCleanupQueue>(m_outcomes)),
      m_stop_work(std::move(queues.stop_work))
{
    assert(m_cleanup_queue && m_core_queue && m_outcomes && m_stop_work);
}

Session::Start Session::start(const std::shared_ptr<interfaces::Wallet>& wallet)
{
    return startWith([&](Resources& resources) { resources.shared_wallet = wallet; });
}

Session::Start Session::start(std::unique_ptr<interfaces::Wallet>& wallet, std::unique_ptr<interfaces::Handler>& unload_handler)
{
    return startWith([&](Resources& resources) {
        resources.wallet = std::move(wallet);
        resources.unload_handler = std::move(unload_handler);
    });
}

Session::Start Session::start(Source& source, std::unique_ptr<interfaces::Handler>& unload_handler)
{
    if (auto* wallet = std::get_if<std::unique_ptr<interfaces::Wallet>>(&source)) return start(*wallet, unload_handler);
    auto& wallet = std::get<std::shared_ptr<interfaces::Wallet>>(source);
    return startWith([&](Resources& resources) {
        resources.shared_wallet = std::move(wallet);
        resources.unload_handler = std::move(unload_handler);
    });
}

void Session::retireUnclaimed(Source& source, std::unique_ptr<interfaces::Handler>& unload_handler)
{
    struct UnclaimedWallet {
        Source source;
        std::unique_ptr<interfaces::Handler> handler;

        ~UnclaimedWallet()
        {
            try {
                if (handler) handler->disconnect();
            } catch (...) {
                // Release both handles even when disconnection fails.
            }
        }
    };
    const auto make = [] { return std::make_unique<UnclaimedWallet>(); };
    const auto unused = walletopening::RetainValues<UnclaimedWallet>(shared_from_this(), make);
    if (!unused) std::terminate();
    unused->source = std::move(source);
    unused->handler = std::move(unload_handler);
}

Session::Start Session::startWith(const std::function<void(Resources&)>& acquire)
{
    const auto self = shared_from_this();
    std::shared_ptr<Resources> resources;
    {
        std::lock_guard lock{m_mutex};
        if (m_started) return {{{}, execution::Rejection::Invalid}, {}};
        if (m_drained || m_closed->load()) return {{{}, execution::Rejection::Closed}, {}};
        auto native = std::make_shared<Resources>();
        auto hold = m_native_cleanup->hold([self, native] {
            if (native->acquired) self->completeStop(self->cleanup(*native));
        });
        if (!hold) return {{{}, execution::Rejection::Closed}, {}};
        resources = std::shared_ptr<Resources>{std::move(hold), native.get()};
        acquire(*resources);
        resources->acquired = true;
        m_resources = resources;
        m_refresh_pending = true;
        m_started = true;
    }
    auto client = borrow();
    const auto admitted = read([self](interfaces::Wallet& value) {
        const auto weak = self->weak_from_this();
        auto& handlers = self->m_resources->handlers;
        handlers.push_back(value.handleStatusChanged([weak] { if (const auto owner = weak.lock()) owner->ingress(0); }));
        handlers.push_back(value.handleAddressBookChanged([weak](const CTxDestination&, const std::string&, bool, wallet::AddressPurpose, ChangeType) {
            if (const auto owner = weak.lock()) owner->ingress(1);
        }));
        handlers.push_back(value.handleTransactionChanged([weak](const Txid& id, ChangeType change) {
            if (const auto owner = weak.lock()) owner->ingress(2, id, change);
        }));
        handlers.push_back(value.handleCanGetAddressesChanged([weak] { if (const auto owner = weak.lock()) owner->ingress(0); }));
        handlers.push_back(value.handleUnload([weak] { if (const auto owner = weak.lock()) owner->ingress(3); }));
        // Ingress may have seen the only unload before this Session existed.
        // Its retained observation closes the subscription-installation gap.
        if (self->m_native_events.unloaded && self->m_native_events.unloaded->load()) {
            self->ingress(3);
            throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
        }
        return ReadMetadata(value, self->m_preferences, self->unavailableReceiveTypes());
    }, [self](Metadata metadata) { self->refreshed(std::move(metadata), {}); }, [self](std::exception_ptr) {
        { std::lock_guard lock{self->m_mutex}; self->m_refresh_pending = false; }
        Update failed; failed.failed = true; self->publish(std::move(failed));
        self->retire();
    });
    if (!admitted) retire();
    return {admitted, std::move(client)};
}

std::shared_ptr<interfaces::Wallet> Session::borrow() const
{
    std::lock_guard lock{m_mutex};
    if (!observationOpen() || !m_resources || !m_resources->get()) return {};
    return {m_resources, m_resources->get()};
}

void Session::close()
{
    std::lock_guard lock{m_mutex};
    m_actions_closed.store(true);
    m_source_epoch->closed.store(true);
}

bool Session::admitAction()
{
    std::lock_guard lock{m_mutex};
    if (!available()) return false;
    ++m_consequences;
    return true;
}

bool Session::control(std::function<void()> effect)
{
    return bool(PostUtility(*m_outcomes, std::move(effect)));
}

execution::SubmitResult Session::postDomain(std::function<void()> work, execution::WorkClass kind)
{
    if (kind == execution::WorkClass::Action ? !admitAction() : !available()) return {{}, execution::Rejection::Closed};
    const auto self = shared_from_this();
    auto run = [self, work = std::move(work), kind] {
        // Accepted command planning must consume its outcome even after close.
        if (kind == execution::WorkClass::Action || self->available()) work();
    };
    return PostUtility(*m_outcomes, std::move(run));
}

execution::SubmitResult Session::postDomainTask(std::shared_ptr<execution::Task> packet, execution::WorkClass)
{
    if (!observationOpen()) return {{}, execution::Rejection::Closed};
    return PostUtility(*m_outcomes, [packet = std::move(packet)] {
        packet->execute();
        packet->deliver();
    });
}

std::function<execution::SubmitResult(std::shared_ptr<execution::Task>)> Session::observationPost(
    execution::Lane lane)
{
    assert(lane == execution::Lane::Core || lane == execution::Lane::Domain);
    return [self = shared_from_this(), lane](std::shared_ptr<execution::Task> packet) {
        if (lane == execution::Lane::Domain) return self->postDomainTask(std::move(packet), execution::WorkClass::Observation);
        if (!self->observationOpen()) return execution::SubmitResult{{}, execution::Rejection::Closed};
        return self->postCore(std::move(packet));
    };
}

execution::SubmitResult Session::retireValues(std::function<void()> work)
{
    return PostUtility(*m_outcomes, std::move(work));
}

std::shared_ptr<void> Session::holdValues(std::function<void()> cleanup)
{
    // A rejected callback can release an action and reenter releaseWork().
    // Keep its captures alive until the admission lock has been released.
    auto retire = std::make_shared<Notify>(std::move(cleanup));
    std::lock_guard lock{m_mutex};
    if (m_stop_requested) return {};
    auto held = m_value_cleanup->hold([self = shared_from_this(), retire] {
        try { (*retire)(); } catch (...) {}
        *retire = {};
        self->releaseWork();
    });
    if (held) ++m_work_references;
    return held;
}

uint64_t Session::transactionRevision() const
{
    std::lock_guard lock{m_mutex};
    return m_transaction_revision;
}

std::function<bool()> Session::observationGuard() const
{
    const auto epoch = m_source_epoch;
    const auto expected = epoch->value.load();
    return [epoch, expected, admitted = available(), closed = m_closed, shutdown = m_shutdown_gate, unloaded = m_native_events.unloaded] {
        return admitted && epoch->current(expected) && !closed->load() && (!shutdown || !shutdown->load()) && (!unloaded || !unloaded->load());
    };
}

Session::Action::~Action()
{
    if (m_owner) m_owner->releaseWork();
}

std::shared_ptr<const Session::Action> Session::startAction()
{
    auto action = std::shared_ptr<Action>{new Action{m_identity}};
    std::lock_guard lock{m_mutex};
    if (!available() || !m_resources || !m_resources->get()) return {};
    action->m_owner = shared_from_this();
    ++m_work_references;
    ++m_consequences;
    return action;
}

void Session::releaseWork()
{
    std::function<void(Notify)> stop_work;
    {
        std::lock_guard lock{m_mutex};
        assert(m_work_references > 0);
        if (--m_work_references == 0 && m_stop_requested) stop_work = std::exchange(m_stop_work, {});
    }
    if (stop_work) drainWork(std::move(stop_work));
}

interfaces::Wallet& Session::nativeWallet() const
{
    std::lock_guard lock{m_mutex};
    auto* wallet = m_resources ? m_resources->get() : nullptr;
    if (!wallet) throw std::logic_error{"The wallet opening has no native wallet"};
    return *wallet;
}

execution::SubmitResult Session::postCore(std::shared_ptr<execution::Task> packet)
{
    // Reserve Utility delivery before admitting Core work. A rejected Core
    // submission releases the reservation with no completed packet.
    return m_outcome_delivery->postWithOutcome(*m_core_queue, std::move(packet));
}

execution::SubmitResult Session::planAction(const std::shared_ptr<const Action>& action,
                                        std::function<void()> work)
{
    if (!action || action->m_identity != m_identity) return {{}, execution::Rejection::Invalid};
    auto run = [action, work = std::move(work)] { work(); };
    return PostUtility(*m_outcomes, std::move(run));
}

void Session::onTransactions(Transactions consumer)
{
    Update seed;
    {
        std::lock_guard lock{m_mutex};
        m_transactions_consumer = consumer;
        seed.transaction_revision = m_transaction_revision;
        if (m_latest) seed.transactions = m_latest->transactions;
    }
    // Covers an event between native subscription and consumer installation,
    // including a Domain publication that had already copied the old consumer.
    if (consumer && seed.transaction_revision && !postDomain([consumer = std::move(consumer), seed] {
        consumer(seed.transaction_revision, seed.transactions, true);
    })) {
        {
            std::lock_guard lock{m_mutex};
            m_pending.rescan = true;
            m_pending.transaction_revision = m_transaction_revision;
            m_transactions_pending = true;
        }
        scheduleRefresh();
    }
}

void Session::requestClose()
{
    const auto self = shared_from_this();
    if (!control([self] {
        if (self->available()) self->m_state.requestClose(self->m_consequences.load());
        self->publishState();
    })) { Update failed; failed.failed = true; publish(std::move(failed)); }
}

void Session::answerClose(Question question, bool accept)
{
    const auto self = shared_from_this();
    if (!control([self, question = std::move(question), accept] {
        Answer answer;
        {
            std::lock_guard lock{self->m_mutex};
            if (!self->available()) return;
            answer = self->m_state.answer(question, self->m_consequences.load(), accept);
            if (answer == Answer::Close) self->m_closed->store(true);
        }
        if (answer == Answer::Close) self->retire(true);
        self->publishState();
    })) { Update failed; failed.failed = true; publish(std::move(failed)); }
}

void Session::retire(bool remove_wallet)
{
    // Native ingress can report an unload while the facade still needs to
    // stop its producers. The facade calls stop() after that handoff.
    std::lock_guard lock{m_mutex};
    m_closed->store(true);
    m_remove |= remove_wallet;
    m_pending = {};
}

void Session::stop(bool remove_wallet, Notify done)
{
    std::function<void(Notify)> stop_work;
    {
        std::lock_guard lock{m_mutex};
        m_closed->store(true);
        m_remove |= remove_wallet;
        m_pending = {};
        if (!m_drained) {
            if (done) m_stop_callbacks.push_back(std::exchange(done, {}));
            m_stop_requested = true;
            // Accepted actions can still schedule follow-ups, and retained
            // values can still post their final cleanup on Utility.
            if (m_work_references == 0) stop_work = std::exchange(m_stop_work, {});
        }
    }
    if (stop_work) drainWork(std::move(stop_work));
    if (done) done();
}

void Session::drainWork(std::function<void(Notify)> stop_work)
{
    stop_work([self = shared_from_this()] {
        std::shared_ptr<Resources> resources;
        {
            std::lock_guard lock{self->m_mutex};
            resources = std::move(self->m_resources);
        }
        if (!resources) self->completeStop(false);
        // The last borrower releases the wallet on its cleanup worker.
    });
}

void Session::completeStop(bool failed)
{
    std::vector<Notify> callbacks;
    {
        std::lock_guard lock{m_mutex};
        m_closed->store(true);
        m_drained = true;
        callbacks.swap(m_stop_callbacks);
    }
    Update update;
    update.phase = Phase::Closed;
    update.failed = failed;
    publish(std::move(update));
    for (const auto& callback : callbacks) callback();
}

bool Session::cleanup(Resources& resources)
{
    m_closed->store(true);
    bool failed{false};
    for (auto& handler : resources.handlers) {
        try { if (handler) handler->disconnect(); } catch (...) { failed = true; }
    }
    resources.handlers.clear();
    bool remove;
    { std::lock_guard lock{m_mutex}; remove = m_remove; }
    try { if (auto* wallet = resources.get(); wallet && remove) wallet->remove(); } catch (...) { failed = true; }
    // The original load observation records the removal before disconnecting.
    try { if (resources.unload_handler) resources.unload_handler->disconnect(); } catch (...) { failed = true; }
    resources.unload_handler.reset();
    resources.wallet.reset();
    resources.shared_wallet.reset();
    return failed;
}

void Session::ingress(int kind, const Txid& txid, ChangeType change)
{
    m_source_epoch->advance();
    if (kind == 3) m_native_events.observeUnload();
    std::shared_ptr<Resources> resources;
    {
        std::lock_guard lock{m_mutex};
        resources = m_resources;
    }
    if (kind == 3) {
        retire();
        Update closing; closing.unloaded = true; closing.phase = Phase::Closing;
        publish(std::move(closing));
        const auto self = shared_from_this();
        control([self] {
            self->m_state.retire();
            Update update; update.unloaded = true;
            update.phase = self->m_state.phase();
            self->publish(std::move(update));
        });
        return;
    }
    if (!resources) {
        if (available()) { retire(); Update failed; failed.failed = true; publish(std::move(failed)); }
        return;
    }
    {
        std::lock_guard lock{m_mutex};
        if (!observationOpen()) return;
        if (kind == 4) m_metadata_dirty = true;
        else if (kind == 0) m_pending.status = true;
        else if (kind == 1) m_pending.addresses = true;
        else {
            m_transactions_pending = true;
            m_pending.transaction_revision = ++m_transaction_revision;
            if (m_pending.transactions.size() < 256 || m_pending.transactions.contains(txid)) m_pending.transactions[txid] = change;
            else m_pending.rescan = true;
        }
    }
    scheduleRefresh();
}

void Session::scheduleRefresh()
{
    Update events;
    {
        std::lock_guard lock{m_mutex};
        if (!observationOpen() || m_refresh_pending || m_clock() < m_retry_after) return;
        if (!m_metadata_dirty && !m_pending.status && !m_pending.addresses && !m_pending.rescan && m_pending.transactions.empty()) return;
        m_refresh_pending = true;
        events = std::exchange(m_pending, {});
        m_metadata_dirty = false;
    }
    const auto self = shared_from_this();
    const bool transactions = events.rescan || !events.transactions.empty();
    const auto admitted = read([self](interfaces::Wallet& wallet) { return ReadMetadata(wallet, self->m_preferences, self->unavailableReceiveTypes()); },
        [self, events = std::move(events)](Metadata metadata) mutable { self->refreshed(std::move(metadata), std::move(events)); },
        [self, transactions](std::exception_ptr) { self->refreshFailed(transactions); }, execution::WorkClass::Observation);
    if (!admitted) refreshFailed(transactions);
}

void Session::refreshFailed(bool transactions)
{
    {
        std::lock_guard lock{m_mutex};
        m_refresh_pending = false;
        if (!observationOpen()) return;
        // Reconcile a failed/undelivered notice instead of dropping its
        // consequences. Retry only after the bounded backoff, on a new notice
        // or an explicit clock tick; never spin on admission failure.
        m_pending.status = m_pending.addresses = true;
        m_pending.rescan |= transactions;
        if (transactions) m_pending.transaction_revision = m_transaction_revision;
        m_retry_after = m_clock() + std::chrono::seconds{std::min(30U, 1U << m_refresh_failures)};
        m_refresh_failures = std::min(5U, m_refresh_failures + 1);
    }
    Update update; update.failed = true; publish(std::move(update));
}

OutputType Session::preferredReceiveType(interfaces::Wallet& wallet) const
{
    const auto saved = m_preferences.read ? m_preferences.read(wallet.getWalletName()) : std::string{};
    const auto parsed = ParseOutputType(saved);
    const OutputType preferred = parsed ? *parsed : wallet.getDefaultAddressType();
    std::lock_guard lock{m_receive_types_mutex};
    if (!m_unavailable_receive_types.contains(preferred)) return preferred;
    // #1088: fall back in the order Receive lists types. Without a Core
    // capability query, a type is only known unavailable once it has failed.
    for (const OutputType type : {OutputType::BECH32M, OutputType::BECH32, OutputType::P2SH_SEGWIT, OutputType::LEGACY}) {
        if (type == OutputType::BECH32M && !wallet.taprootEnabled()) continue;
        if (!m_unavailable_receive_types.contains(type)) return type;
    }
    return preferred;
}

void Session::markReceiveTypeUnavailable(OutputType type)
{
    std::lock_guard lock{m_receive_types_mutex};
    m_unavailable_receive_types.insert(type);
}

std::vector<std::string> Session::unavailableReceiveTypes() const
{
    std::lock_guard lock{m_receive_types_mutex};
    std::vector<std::string> ids;
    ids.reserve(m_unavailable_receive_types.size());
    for (const OutputType type : m_unavailable_receive_types) ids.push_back(FormatOutputType(type));
    return ids;
}

execution::SubmitResult Session::setReceivePreference(std::string type, std::shared_ptr<const Action> original, std::function<void(bool)> done)
{
    if (type != "bech32m" && type != "bech32" && type != "p2sh-segwit" && type != "legacy") return {{}, execution::Rejection::Invalid};
    if (original && original->identity() != identity()) return {{}, execution::Rejection::Invalid};
    const auto revision = ++m_preference_revision;
    const auto self = shared_from_this();
    const auto work = [self, type](interfaces::Wallet& wallet) {
        const bool supported = type != "bech32m" || wallet.taprootEnabled();
        const bool persisted = supported && self->m_preferences.write && self->m_preferences.write(wallet.getWalletName(), type);
        auto metadata = ReadMetadata(wallet, self->m_preferences, self->unavailableReceiveTypes());
        const bool saved = persisted && metadata.receive_preference == type;
        return std::pair{std::move(metadata), saved};
    };
    const auto complete = [self, revision, done](std::pair<Metadata, bool> result) {
        const bool saved = result.second;
        Update update; update.metadata = std::move(result.first); update.preference_saved = result.second; update.preference_revision = revision;
        update.phase = self->m_state.phase(); update.question = self->m_state.question();
        self->publish(std::move(update));
        try { if (done) done(saved); } catch (...) { }
    };
    const auto failed = [self, revision, done](std::exception_ptr) {
        Update update; update.preference_saved = false; update.preference_revision = revision;
        update.phase = self->m_state.phase(); update.question = self->m_state.question();
        self->publish(std::move(update));
        try { if (done) done(false); } catch (...) { }
    };
    const auto submitted = original ? perform(original, work, complete, failed)
                                    : read(work, complete, failed, execution::WorkClass::Action);
    if (!submitted) {
        Update update; update.preference_saved = false; update.preference_revision = revision;
        publish(std::move(update));
    }
    return submitted;
}

void Session::refreshed(Metadata metadata, Update events)
{
    Transactions consumer;
    { std::lock_guard lock{m_mutex}; m_refresh_pending = false; m_refresh_failures = 0; m_retry_after = {}; consumer = m_transactions_consumer; }
    if (!available()) return;
    if (consumer && (!events.transactions.empty() || events.rescan)) consumer(events.transaction_revision, events.transactions, events.rescan);
    events.metadata = std::move(metadata);
    events.phase = m_state.phase();
    events.question = m_state.question();
    publish(std::move(events));
    scheduleRefresh();
}

void Session::publishState()
{
    Update update;
    update.phase = m_state.phase();
    update.question = m_state.question();
    publish(std::move(update));
}

void Session::publish(Update update)
{
    bool notify;
    {
        std::lock_guard lock{m_mutex};
        if (m_latest) {
            update.transaction_revision = std::max(update.transaction_revision, m_latest->transaction_revision);
            if (!update.metadata) update.metadata = std::move(m_latest->metadata);
            if (!update.preference_saved) {
                update.preference_saved = m_latest->preference_saved;
                update.preference_revision = m_latest->preference_revision;
            }
            for (const auto& [id, change] : m_latest->transactions) {
                if (update.transactions.size() < 256) update.transactions.try_emplace(id, change);
                else update.rescan = true;
            }
            update.addresses |= m_latest->addresses;
            update.status |= m_latest->status;
            update.rescan |= m_latest->rescan;
            update.unloaded |= m_latest->unloaded;
            update.failed |= m_latest->failed;
        }
        if (m_closed->load()) {
            update.phase = m_drained.load() ? Phase::Closed : Phase::Closing;
            update.question.reset();
        }
        m_latest = std::move(update);
        notify = !std::exchange(m_notice_pending, true);
    }
    if (notify) m_notify();
}

std::optional<Update> Session::take()
{
    std::lock_guard lock{m_mutex};
    m_notice_pending = false;
    return std::exchange(m_latest, {});
}

void Session::acknowledgeTransactions()
{
    std::lock_guard lock{m_mutex};
    if (!m_refresh_pending && m_pending.transactions.empty() && !m_pending.rescan &&
        (!m_latest || (m_latest->transactions.empty() && !m_latest->rescan))) m_transactions_pending = false;
}
Session::Preferences ChainPreferences(interfaces::Chain& chain)
{
    return {
        [&chain](const std::string& name) {
            const auto values = chain.getRwSetting("qml_receive_address_types");
            const auto& value = values[name];
            return value.isStr() ? value.get_str() : std::string{};
        },
        [&chain](const std::string& name, const std::string& type) {
            return chain.updateRwSetting("qml_receive_address_types", [&](common::SettingsValue& values) {
                if (!values.isObject()) values = common::SettingsValue{UniValue::VOBJ};
                values.pushKV(name, type);
                return interfaces::SettingsAction::WRITE;
            });
        }};
}
} // namespace walletopening
