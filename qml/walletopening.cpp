// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletopening.h>

#include <common/args.h>
#include <interfaces/chain.h>
#include <addresstype.h>
#include <outputtype.h>
#include <univalue.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace walletopening {
struct Session::Resources {
    std::shared_ptr<interfaces::Wallet> wallet;
    std::vector<std::unique_ptr<interfaces::Handler>> handlers;
};
namespace {
struct Borrower {
    execution::ResourceLease lease;
    std::shared_ptr<interfaces::Wallet> wallet;
};
Metadata ReadMetadata(interfaces::Wallet& wallet, const Session::Preferences& preferences, std::vector<std::string> unavailable_receive_types)
{
    auto name = wallet.getWalletName();
    if (name.size() > 32'768) throw std::length_error("Wallet name is too long");
    auto saved = preferences.read ? preferences.read(name) : std::string{};
    // IDs are a closed vocabulary; never copy arbitrary persistent input into
    // the bounded metadata transport. Invalid values use the supported default.
    if (saved != "bech32m" && saved != "bech32" && saved != "p2sh-segwit" && saved != "legacy") saved.clear();
    // Core resolves -addresstype into the wallet's default type; only an
    // explicit setting overrides the app's own Taproot default.
    std::string configured{gArgs.IsArgSet("-addresstype") ? FormatOutputType(wallet.getDefaultAddressType()) : std::string{}};
    return {std::move(name), wallet.privateKeysDisabled(), wallet.hasExternalSigner(), wallet.isCrypted(), wallet.isLocked(),
            wallet.canGetAddresses(), wallet.taprootEnabled(), std::move(saved), std::move(configured), std::move(unavailable_receive_types)};
}
}

Session::Session(std::shared_ptr<execution::Scheduler> commands,
                 std::shared_ptr<execution::Scheduler> retirement,
                 std::shared_ptr<std::atomic_bool> closed, Notify notify, Preferences preferences, Clock clock, std::shared_ptr<std::atomic_bool> shutdown_gate, NativeEvents native_events)
    : m_commands(std::move(commands)), m_retirement(std::move(retirement)),
      m_command_key(m_commands->allocateKey()), m_retirement_key(m_retirement->allocateKey()),
      m_closed(std::move(closed)), m_shutdown_gate(std::move(shutdown_gate)), m_notify(std::move(notify)), m_preferences(std::move(preferences)), m_clock(std::move(clock)), m_native_events(std::move(native_events)), m_resources(std::make_shared<Resources>())
{
}

Session::Start Session::start(const std::shared_ptr<interfaces::Wallet>& wallet)
{
    {
        std::lock_guard lock{m_mutex};
        if (std::exchange(m_started, true)) return {{{}, execution::Rejection::Invalid}, {}};
    }
    const auto self = shared_from_this();
    const auto cleanup = execution::Submit(*m_retirement, [self] { return self->cleanup(); }, [self](bool failed) {
        self->m_drained = true;
        Update update; update.phase = Phase::Closed; update.failed = failed;
        self->publish(std::move(update));
    }, {}, {execution::Lane::Cleanup, m_retirement_key}, true);
    if (!cleanup) {
        m_closed->store(true);
        m_drained = true;
        Update failed; failed.phase = Phase::Closed; failed.failed = true; publish(std::move(failed));
        return {cleanup, {}};
    }
    {
        std::lock_guard lock{m_mutex};
        m_cleanup = cleanup.id;
    }
    // Pin initialization before acquiring a native resource. A concurrent
    // retirement-root shutdown may activate its reservation immediately.
    auto initialization = m_retirement->borrow(m_retirement_key);
    if (!initialization) {
        retire();
        return {{{}, initialization.rejection}, {}};
    }
    {
        std::lock_guard lock{m_mutex};
        m_resources->wallet = wallet;
        m_refresh_pending = true;
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
    if (!observationOpen() || !m_resources->wallet) return {};
    auto borrowed = m_retirement->borrow(m_retirement_key);
    if (!borrowed) return {};
    auto owner = std::make_shared<Borrower>(Borrower{std::move(*borrowed.lease), m_resources->wallet});
    return {std::move(owner), m_resources->wallet.get()};
}

void Session::close()
{
    std::lock_guard lock{m_mutex};
    m_admission_closed.store(true);
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
    return bool(execution::Submit(*m_commands, [] {}, std::move(effect), {},
                                 {execution::Lane::Control, m_command_key}));
}

execution::Admission Session::postDomain(std::function<void()> work, execution::WorkClass kind)
{
    if (kind == execution::WorkClass::Action ? !admitAction() : !available()) return {{}, execution::Rejection::Closed};
    const auto self = shared_from_this();
    return execution::Submit(*m_commands, [self, work = std::move(work), kind] {
        // Accepted command planning must consume its outcome even after close.
        if (kind == execution::WorkClass::Action || self->available()) work();
    }, [] {}, {}, {execution::Lane::Domain, m_command_key, kind});
}

execution::Admission Session::retireValues(std::function<void()> work)
{
    return execution::Submit(*m_commands, std::move(work), [] {}, {},
        {execution::Lane::Cleanup, {}, execution::WorkClass::Action});
}

execution::BorrowAdmission Session::holdValues(std::function<void()> cleanup)
{
    const auto key = m_commands->allocateKey();
    const auto reserved = execution::Submit(*m_commands, std::move(cleanup), [] {}, {},
        {execution::Lane::Cleanup, key, execution::WorkClass::Action}, true);
    if (!reserved) return {{}, reserved.rejection};
    auto held = m_commands->borrow(key);
    m_commands->activateCleanup(reserved.id);
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

std::shared_ptr<const Session::Action> Session::startAction()
{
    auto scope = m_commands->openActionScope();
    if (!scope) return {};
    execution::ResourceKey work_key;
    try { work_key = m_commands->allocateKey(); } catch (...) { return {}; }
    if (!admitAction()) return {};
    auto wallet = borrow();
    if (!wallet) return {};
    return std::shared_ptr<const Action>{new Action{m_identity, std::move(scope), work_key, std::move(wallet)}};
}

execution::Admission Session::submitActionWork(const std::shared_ptr<const Action>& action,
                                               std::function<void()> work)
{
    if (!action || action->m_identity != m_identity) return {{}, execution::Rejection::Invalid};
    return execution::Submit(*action->m_scope, [action, work = std::move(work)] { work(); }, [] {}, {},
        {execution::Lane::Core, action->m_work_key, execution::WorkClass::Action});
}

execution::Admission Session::reserveActionCleanup(const std::shared_ptr<const Action>& action,
                                                   std::function<void()> work)
{
    if (!action || action->m_identity != m_identity) return {{}, execution::Rejection::Invalid};
    return execution::Submit(*action->m_scope, [action, work = std::move(work)] { work(); }, [] {}, {},
        {execution::Lane::Cleanup, action->m_work_key, execution::WorkClass::Action}, true);
}

bool Session::activateActionCleanup(const std::shared_ptr<const Action>& action, execution::TaskId id)
{
    if (!action || action->m_identity != m_identity) return false;
    return action->m_scope->activateCleanup(id);
}

execution::Admission Session::planAction(const std::shared_ptr<const Action>& action,
                                        std::function<void()> work)
{
    if (!action || action->m_identity != m_identity) return {{}, execution::Rejection::Invalid};
    return execution::Submit(*action->m_scope, [action, work = std::move(work)] { work(); }, [] {}, {},
        {execution::Lane::Domain, m_command_key, execution::WorkClass::Action});
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
    std::optional<execution::TaskId> cleanup;
    {
        std::lock_guard lock{m_mutex};
        m_closed->store(true);
        m_remove |= remove_wallet;
        cleanup = m_cleanup;
        m_pending = {};
    }
    if (cleanup) m_retirement->activateCleanup(*cleanup);
}

bool Session::cleanup()
{
    m_closed->store(true);
    bool failed{false};
    for (auto& handler : m_resources->handlers) {
        try { if (handler) handler->disconnect(); } catch (...) { failed = true; }
    }
    m_resources->handlers.clear();
    bool remove;
    { std::lock_guard lock{m_mutex}; remove = m_remove; }
    try { if (m_resources->wallet && remove) m_resources->wallet->remove(); } catch (...) { failed = true; }
    m_resources->wallet.reset();
    return failed;
}

void Session::ingress(int kind, const Txid& txid, ChangeType change)
{
    m_source_epoch->advance();
    if (kind == 3) m_native_events.observeUnload();
    auto callback_lifetime = m_retirement->borrow(m_retirement_key);
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
    if (!callback_lifetime) {
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
    // A saved choice wins, then an explicit -addresstype, then Taproot, the
    // app's own default (#932, #1137). Core's implicit default is not used.
    const OutputType preferred = parsed ? *parsed
        : gArgs.IsArgSet("-addresstype") ? wallet.getDefaultAddressType()
        : wallet.taprootEnabled() ? OutputType::BECH32M : wallet.getDefaultAddressType();
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

execution::Admission Session::setReceivePreference(std::string type, std::shared_ptr<const Action> original, std::function<void(bool)> done)
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
    const auto admission = original ? perform(original, work, complete, failed)
                                    : read(work, complete, failed, execution::WorkClass::Action);
    if (!admission) {
        Update update; update.preference_saved = false; update.preference_revision = revision;
        publish(std::move(update));
    }
    return admission;
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
