// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletlifecycle.h>
#include <qml/walletpublication.h>
#include <qml/retainedsnapshot.h>

#include <QScopeGuard>

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace walletlife {
LoadSubscription::LoadSubscription(std::shared_ptr<execution::Scheduler> scheduler, Register registration, Capture capture, Notify notify, std::function<uint64_t()> ingress_stamp, std::function<std::string(interfaces::Wallet&)> read_name)
    : m_scheduler(std::move(scheduler)), m_registration(std::move(registration)), m_capture(std::move(capture)), m_notify(std::move(notify)), m_ingress_stamp(std::move(ingress_stamp)), m_read_name(read_name ? std::move(read_name) : [](interfaces::Wallet& wallet) { return wallet.getWalletName(); }), m_key(m_scheduler ? m_scheduler->allocateKey() : execution::ResourceKey{})
{
    if (!m_scheduler || !m_registration || !m_capture || !m_notify) throw std::invalid_argument("Missing wallet lifecycle port");
}

std::shared_ptr<LoadSubscription::UnloadObservation> LoadSubscription::subscribeUnload(interfaces::Wallet& wallet)
{
    // Observe unload before posting the wallet to the GUI.
    auto observation = std::make_shared<UnloadObservation>();
    observation->handler = wallet.handleUnload([unloaded = observation->unloaded, stamp = m_ingress_stamp] {
        uint64_t sequence{1};
        try { if (stamp) sequence = stamp(); } catch (...) { sequence = 0; }
        uint64_t empty{0};
        unloaded->compare_exchange_strong(empty, sequence ? sequence : std::numeric_limits<uint64_t>::max());
    });
    if (!observation->handler) throw std::runtime_error("Wallet unload notification producer unavailable");
    return observation;
}

std::vector<LoadSubscription::Entry> LoadSubscription::captureEntries(std::vector<std::unique_ptr<interfaces::Wallet>> wallets)
{
    std::vector<Entry> entries;
    entries.reserve(wallets.size());
    for (auto& wallet : wallets) {
        if (!wallet) throw std::runtime_error("Invalid wallet snapshot");
        Entry entry;
        entry.initial_wallet = std::move(wallet);
        entry.unload_observation = subscribeUnload(*entry.initial_wallet);
        entry.name = m_read_name(*entry.initial_wallet);
        entries.push_back(std::move(entry));
    }
    return entries;
}

execution::SubmitResult LoadSubscription::start()
{
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping) return {{}, execution::Rejection::Closed};
        if (m_started) return {{}, execution::Rejection::Invalid};
        m_started = true;
    }
    const auto self = shared_from_this();
    const auto cleanup = execution::Submit(*m_scheduler, [self] { return self->cleanup(); }, [self](bool cleanup_failed) {
        auto reason = self->m_failure.load();
        if (self->m_failed) {
            if (reason == FailureReason::None) reason = FailureReason::Backend;
            if (const auto publication = self->m_state.apply(Event::RegistrationFailed)) self->m_notify({*publication, false, reason});
        }
        self->m_state.apply(Event::Close);
        if (const auto publication = self->m_state.apply(Event::CleanupFinished)) self->m_notify({*publication, cleanup_failed, reason});
    }, {}, {execution::Lane::Cleanup, m_key}, true);
    if (!cleanup) {
        m_stopping = true;
        return cleanup;
    }
    {
        std::lock_guard lock{m_mutex};
        m_cleanup_id = cleanup.id;
    }
    if (m_stopping) {
        m_scheduler->activateCleanup(cleanup.id);
        return {{}, execution::Rejection::Closed};
    }
    const auto admitted = execution::Submit(*m_scheduler, [self] {
        if (self->m_stopping) return;
        auto handler = self->m_registration([weak = self->weak_from_this()](std::unique_ptr<interfaces::Wallet> wallet) {
            if (const auto owner = weak.lock()) owner->ingress(std::move(wallet));
        });
        if (!handler) throw std::runtime_error("Wallet notification producer unavailable");
        {
            std::lock_guard lock{self->m_mutex};
            self->m_handler = std::move(handler);
        }
        if (self->m_stopping) return;
        auto entries = self->captureEntries(self->m_capture());
        std::deque<Entry> initial{std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end())};
        std::lock_guard lock{self->m_mutex};
        self->m_initial.swap(initial);
    }, [self] { self->registered(); }, [self](std::exception_ptr) { self->failed(); }, {execution::Lane::Core, m_key});
    if (!admitted) {
        m_failed = true;
        retire();
    }
    return admitted;
}

void LoadSubscription::registered()
{
    if (m_stopping) {
        m_state.apply(Event::Close);
        return;
    }
    if (const auto publication = m_state.apply(Event::Registered)) {
        {
            std::lock_guard lock{m_mutex};
            m_initial_ready = true;
        }
        m_notify({*publication, false});
    }
}

void LoadSubscription::failed()
{
    if (m_stopping) return;
    m_failed = true;
    auto reason = FailureReason::None;
    m_failure.compare_exchange_strong(reason, FailureReason::Backend);
    if (const auto publication = m_state.apply(Event::RegistrationFailed)) m_notify({*publication, false, m_failure.load()});
    retire();
}

void LoadSubscription::publishChanged()
{
    const auto publication = m_state.apply(Event::Notification);
    if (!publication) return;
    {
        std::lock_guard lock{m_mutex};
        if (std::exchange(m_change_publication_pending, true)) return;
    }
    m_notify({*publication, false});
}

void LoadSubscription::ingress(std::unique_ptr<interfaces::Wallet> wallet)
{
    if (m_stopping) return;
    uint64_t delivery{0};
    std::shared_ptr<const NotificationContext> context;
    try {
        std::lock_guard lock{m_mutex};
        if (m_stopping || m_failed) return;
        if (m_notification_sequence != std::numeric_limits<uint64_t>::max()) {
            delivery = m_notification_sequence + 1;
            m_unpublished.insert(delivery);
            m_notification_sequence = delivery;
            context = m_notification_context;
        }
    } catch (...) { requestFailure(FailureReason::IngressCapacity); return; }
    if (!delivery) { requestFailure(FailureReason::IngressCapacity); return; }
    bool queued{false};
    const auto release_delivery = qScopeGuard([&] {
        if (!queued) { std::lock_guard lock{m_mutex}; m_unpublished.erase(delivery); }
    });
    uint64_t stamp{0};
    if (m_ingress_stamp) {
        try { stamp = m_ingress_stamp(); } catch (...) { requestFailure(FailureReason::Backend); return; }
        if (!stamp) { requestFailure(FailureReason::IngressCapacity); return; }
    }
    auto borrowed = m_scheduler->borrow(m_key);
    // Parameter destruction occurs after local destruction. Keep the incoming
    // handle local and after the lease so even a rejected handle is destroyed
    // before its counted callback can allow parent Cleanup/drain to finish.
    auto incoming = std::move(wallet);
    if (!borrowed) return;
    if (!incoming) { requestFailure(FailureReason::Backend); return; }
    try {
        auto observation = subscribeUnload(*incoming);
        const auto capture = context ? context->creation_observation : nullptr;
        auto handle = std::make_shared<walletpublication::ObservedLoad>(
            std::move(incoming), std::move(observation->handler), observation->unloaded, bool(capture));
        if (capture && !capture->capture(handle, stamp)) { requestFailure(FailureReason::IngressCapacity); return; }
        std::lock_guard lock{m_mutex};
        if (m_stopping || m_failed) return;
        m_pending.push_back({std::move(handle), std::move(context), stamp, delivery}); queued = true;
    } catch (...) { requestFailure(FailureReason::IngressCapacity); return; }
    requestScan();
}

void LoadSubscription::requestFailure(FailureReason reason)
{
    { std::lock_guard lock{m_mutex}; if (m_notification_context && m_notification_context->creation_observation) m_notification_context->creation_observation->fail(); }
    auto previous = FailureReason::None;
    m_failure.compare_exchange_strong(previous, reason);
    if (m_failed.exchange(true)) return;
    const auto self = shared_from_this();
    // A mandatory failure must not wait behind the Core work that prevented
    // ingress from making progress. The existing Cleanup owns the fallback if
    // even Control admission is rejected.
    const auto reported = execution::Submit(*m_scheduler, [] {}, [self] { self->failed(); }, {}, {execution::Lane::Control, m_key});
    if (!reported) retire();
}

void LoadSubscription::requestScan()
{
    const auto self = shared_from_this();
    execution::SubmitResult admitted;
    {
        // Publication is queued, so recording the accepted id under this mutex
        // lets retirement cancel it even when its Core transport is blocked.
        std::lock_guard lock{m_mutex};
        if (m_stopping || m_failed || m_scan_pending.exchange(true)) return;
        admitted = execution::Submit(*m_scheduler, [self] { self->scan(); }, [self] {
            { std::lock_guard finished_lock{self->m_mutex}; self->m_scan_id.reset(); }
            self->m_scan_pending = false;
            if (self->m_stopping || self->m_failed) return;
            self->publishChanged();
            bool more;
            {
                std::lock_guard pending_lock{self->m_mutex};
                more = !self->m_pending.empty();
            }
            if (more) self->requestScan();
        }, [self](std::exception_ptr) { self->failed(); }, {execution::Lane::Core, m_key, execution::WorkClass::Observation});
        if (admitted) m_scan_id = admitted.id;
    }
    if (!admitted && !m_stopping) {
        // Keep the coalescing fence set: a rejected mandatory refresh becomes
        // one explicit failure, rather than an unbounded sequence of retries.
        requestFailure(FailureReason::Backend);
    }
}

void LoadSubscription::scan()
{
    std::vector<Pending> pending;
    {
        std::lock_guard lock{m_mutex};
        pending.swap(m_pending);
    }
    if (m_stopping) return;
    std::vector<Entry> entries;
    entries.reserve(pending.size());
    for (auto& notification : pending) {
        auto name = notification.wallet->describe(m_read_name).name;
        const auto& context=notification.context;
        if(context&&!context->deferred_name.isEmpty()){const auto& deferred=context->nativeName();if(!deferred.empty()&&name==deferred){std::lock_guard lock{m_mutex};m_unpublished.erase(notification.notification_sequence);continue;}}
        entries.push_back({std::move(name),std::move(notification.wallet),context?context->select_wallet:true,context?context->selection_revision:0,context?context->adoption_origin:std::shared_ptr<void>{},notification.ingress_sequence,notification.notification_sequence,notification.notification_sequence,{}, {}});
    }
    std::vector<Entry> retired;
    retired.reserve(entries.size());
    {
        std::lock_guard lock{m_mutex};
        for (auto& entry : entries) {
            const auto slot = m_ready.try_emplace(entry.name).first;
            if (slot->second.wallet) {
                // Transfer the older observation's unsettled dependency to
                // its replacement; a fenced result cannot overtake this slot.
                const auto first = std::min(slot->second.settlement_sequence, entry.settlement_sequence);
                const auto last = std::max(slot->second.settlement_sequence, entry.settlement_sequence);
                m_unpublished.erase(last);
                slot->second.settlement_sequence = first;
                entry.settlement_sequence = first;
            }
            if (slot->second.wallet && slot->second.ingress_sequence > entry.ingress_sequence) {
                retired.push_back(std::move(entry));
                continue;
            }
            retired.push_back(std::move(slot->second));
            slot->second = std::move(entry);
        }
    }
    // Replaced backend handles die outside the mailbox mutex, on Core.
}

const std::string& LoadSubscription::NotificationContext::nativeName()const
{
    // Only the subscription's keyed Native scan calls this method.
    if(!native_name)native_name.emplace(deferred_name.trimmed().toStdString());
    return *native_name;
}
bool LoadSubscription::setNotificationContext(QString deferred_name,bool action_pending,uint64_t selection_revision,std::shared_ptr<void> adoption_origin,std::shared_ptr<walletpublication::NativeObservation> creation_observation)
{
    if(m_stopping||m_failed)return false;
    std::shared_ptr<NotificationContext> context;
    try{context=execution::RetainSnapshot<NotificationContext>(m_scheduler,[&]{
        auto value=std::make_unique<NotificationContext>();
        value->deferred_name=std::move(deferred_name);value->select_wallet=!action_pending;value->selection_revision=selection_revision;value->adoption_origin=std::move(adoption_origin);value->creation_observation=std::move(creation_observation);return value;
    });}catch(...){requestFailure(FailureReason::IngressCapacity);return false;}
    if(!context){requestFailure(FailureReason::IngressCapacity);return false;}
    std::shared_ptr<const NotificationContext> published=std::move(context);
    {std::lock_guard lock{m_mutex};if(m_stopping||m_failed)return false;m_notification_context.swap(published);}
    // The replaced context's QString/cache last release belongs to Cleanup,
    // even when there was no Native ingress retaining that old origin.
    return true;
}

void LoadSubscription::rejectNotificationContext()
{
    requestFailure(FailureReason::IngressCapacity);
}

void LoadSubscription::retire()
{
    m_stopping = true;
    std::optional<execution::TaskId> cleanup, scan;
    { std::lock_guard lock{m_mutex}; cleanup = m_cleanup_id; scan = m_scan_id; }
    if (scan) m_scheduler->cancelQueued(*scan);
    if (cleanup) m_scheduler->activateCleanup(*cleanup);
}

bool LoadSubscription::cleanup()
{
    m_stopping = true;
    // The pre-admitted reservation waits for every same-owner outcome and
    // counted callback/handoff lease. No worker waits for another queued turn.
    bool cleanup_failed{false};
    std::unique_ptr<interfaces::Handler> handler;
    { std::lock_guard lock{m_mutex}; handler = std::move(m_handler); }
    if (handler) {
        try { handler->disconnect(); }
        catch (...) { cleanup_failed = true; }
        handler.reset();
    }
    std::vector<Pending> pending;
    std::deque<Entry> initial;
    std::map<std::string, Entry> ready;
    std::set<uint64_t> unpublished;
    std::shared_ptr<const NotificationContext> context;
    {
        std::lock_guard lock{m_mutex};
        context=std::move(m_notification_context);
        pending.swap(m_pending);
        initial.swap(m_initial);
        ready.swap(m_ready);
        unpublished.swap(m_unpublished);
    }
    return cleanup_failed;
}

uint64_t LoadSubscription::notificationFence() const
{
    std::lock_guard lock{m_mutex};
    return m_notification_sequence;
}
LoadSubscription::Settlement LoadSubscription::settlementThrough(uint64_t fence) const
{
    std::lock_guard lock{m_mutex};
    if (m_stopping || m_failed) return Settlement::Unavailable;
    return !m_unpublished.empty() && *m_unpublished.begin() <= fence ? Settlement::Pending : Settlement::Settled;
}

struct LoadSubscription::ReturnPacket {
    std::vector<Entry> entries;
    std::optional<execution::ResourceLease> lifetime;
    explicit ReturnPacket(std::optional<execution::ResourceLease> lease) : lifetime(std::move(lease)) {}
};

std::unique_ptr<LoadSubscription::Handoff> LoadSubscription::take(bool initial)
{
    if (!initial) acknowledgeChange();
    auto borrowed = m_scheduler->borrow(m_key);
    if (!borrowed) return {};
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping || m_failed || !m_initial_ready || m_handoff_disposal_pending) return {};
        if (initial ? m_initial.empty() : m_ready.empty()) return std::unique_ptr<Handoff>{new Handoff};
        m_handoff_disposal_pending = true;
    }
    const auto self = shared_from_this();
    const auto packet = std::make_shared<ReturnPacket>(std::move(borrowed.lease));
    const auto key = m_scheduler->allocateKey();
    const auto disposal = execution::Submit(*m_scheduler, [packet] { packet->entries.clear(); }, [self, initial, packet] {
        (void)packet; // Retain the parent lease through outcome consumption.
        { std::lock_guard lock{self->m_mutex}; self->m_handoff_disposal_pending = false; }
        if (self->m_stopping) return;
        if (initial) self->m_notify({Publication::InitialHandoff, false});
        else self->publishChanged();
    }, {}, {execution::Lane::Cleanup, key}, true);
    if (!disposal) {
        { std::lock_guard lock{m_mutex}; m_handoff_disposal_pending = false; }
        m_failed = true; retire(); return {};
    }
    auto guarded = m_scheduler->borrow(key);
    if (!guarded) {
        m_scheduler->activateCleanup(disposal.id);
        if (!m_stopping) { m_failed = true; retire(); }
        return {};
    }
    auto result = std::unique_ptr<Handoff>{new Handoff(self, packet, disposal.id, std::move(guarded.lease))};
    std::lock_guard lock{m_mutex};
    if (m_stopping) {
        result->m_owner.reset();
        m_scheduler->activateCleanup(disposal.id);
        return {};
    }
    while (result->entries.size() < GUI_HANDOFF) {
        if (initial) {
            if (m_initial.empty()) break;
            result->entries.push_back(std::move(m_initial.front()));
            m_initial.pop_front();
        } else {
            if (m_ready.empty()) break;
            auto node = m_ready.extract(m_ready.begin());
            const auto delivery = node.mapped().settlement_sequence;
            result->entries.push_back(std::move(node.mapped()));
            m_unpublished.erase(delivery);
        }
    }
    result->more = initial ? !m_initial.empty() : !m_ready.empty();
    return result;
}

void LoadSubscription::acknowledgeChange()
{
    // Delivery can occur during a nested GUI event loop while the previous
    // handoff is still active. Acknowledge the wakeup even if data cannot yet
    // be claimed, so background disposal can publish the next continuation.
    std::lock_guard lock{m_mutex};
    m_change_publication_pending = false;
}

std::unique_ptr<LoadSubscription::Handoff> LoadSubscription::takeInitial() { return take(true); }
std::unique_ptr<LoadSubscription::Handoff> LoadSubscription::takeNotifications() { return take(false); }
LoadSubscription::Handoff::~Handoff() { if (m_owner) m_owner->returnHandoff(std::move(entries), std::move(m_packet), m_disposal); }
void LoadSubscription::returnHandoff(std::vector<Entry> entries, std::shared_ptr<ReturnPacket> packet, execution::TaskId disposal)
{
    packet->entries = std::move(entries);
    m_scheduler->activateCleanup(disposal);
    // The scoped disposal lease drops after this method returns. Its reserved
    // cleanup then releases every shadow backend reference off GUI, including
    // duplicates and unclaimed entries. The parent lease lasts through it.
}
} // namespace walletlife
