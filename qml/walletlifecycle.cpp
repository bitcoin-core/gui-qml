// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletlifecycle.h>
#include <qml/walletpublication.h>
#include <qml/retainedvalue.h>

#include <QScopeGuard>

#include <algorithm>
#include <iterator>
#include <limits>
#include <stdexcept>

namespace walletlife {
LoadSubscription::LoadSubscription(execution::Executor* executor, Register registration, Capture capture, Notify notify,
                                   std::function<uint64_t()> ingress_stamp, std::function<std::string(interfaces::Wallet&)> read_name)
    : m_core(executor ? std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Core)) : nullptr),
      m_utility(executor ? std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility)) : nullptr),
      m_outcomes(executor ? std::make_shared<execution::ValueCleanupQueue>(
          std::make_shared<execution::SerialQueue>(executor->makeQueue(execution::Lane::Utility))) : nullptr),
      m_retirements(m_utility ? std::make_shared<execution::ValueCleanupQueue>(m_utility) : nullptr),
      m_registration(std::move(registration)), m_capture(std::move(capture)), m_notify(std::move(notify)),
      m_ingress_stamp(std::move(ingress_stamp)),
      m_read_name(read_name ? std::move(read_name) : [](interfaces::Wallet& wallet) { return wallet.getWalletName(); })
{
    if (!m_core || !m_registration || !m_capture || !m_notify) throw std::invalid_argument("Missing wallet lifecycle port");
    auto owner = std::make_shared<std::shared_ptr<LoadSubscription>>();
    auto failed = std::make_shared<bool>(false);
    auto finish = m_retirements->hold([owner, failed] {
        if (*owner) (*owner)->cleanupFinished(*failed);
    });
    if (!finish) throw std::invalid_argument("Wallet lifecycle queue is closed");
    auto cleanup = m_retirements->hold([owner, failed, finish = std::move(finish)]() mutable {
        if (*owner) *failed = (*owner)->cleanup();
        // Releasing the notification context queues its retirement before completion.
        finish.reset();
        if (*owner) (*owner)->m_utility->close();
    });
    if (!cleanup) throw std::invalid_argument("Wallet lifecycle queue is closed");
    m_retire_on_utility = [owner, cleanup = std::move(cleanup)](auto self) mutable {
        *owner = std::move(self);
        cleanup.reset();
    };
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
    if (postCore(&LoadSubscription::registerWallets, &LoadSubscription::registered)) return {};
    if (m_stopping) return {{}, execution::Rejection::Closed};
    requestFailure(FailureReason::Backend);
    return {{}, execution::Rejection::Transport};
}

void LoadSubscription::registerWallets()
{
    if (m_stopping) return;
    auto handler = m_registration([weak = weak_from_this()](std::unique_ptr<interfaces::Wallet> wallet) {
        if (const auto owner = weak.lock()) owner->ingress(std::move(wallet));
    });
    if (!handler) throw std::runtime_error("Wallet notification producer unavailable");
    {
        std::lock_guard lock{m_mutex};
        m_handler = std::move(handler);
    }
    if (m_stopping) return;
    auto entries = captureEntries(m_capture());
    std::deque<Entry> initial{std::make_move_iterator(entries.begin()), std::make_move_iterator(entries.end())};
    std::lock_guard lock{m_mutex};
    m_initial.swap(initial);
}

struct LoadSubscription::ActiveWork {
    std::shared_ptr<LoadSubscription> owner;
    ~ActiveWork() { if (owner) owner->endWork(); }
};

std::shared_ptr<LoadSubscription::ActiveWork> LoadSubscription::beginWork()
{
    auto work = std::make_shared<ActiveWork>();
    std::lock_guard lock{m_mutex};
    if (m_stopping) return {};
    ++m_active_work;
    work->owner = shared_from_this();
    return work;
}

void LoadSubscription::endWork()
{
    {
        std::lock_guard lock{m_mutex};
        --m_active_work;
    }
    scheduleCleanup();
}

bool LoadSubscription::postCore(void (LoadSubscription::*work)(), void (LoadSubscription::*done)())
{
    // Claim the live subscription when the turn starts. A queued scan must
    // never delay disposal of notifications that Core itself is waiting for.
    auto active = std::make_shared<std::shared_ptr<ActiveWork>>();
    auto task = execution::MakeTask([weak = weak_from_this(), active, work] {
        if (auto self = weak.lock()) {
            *active = self->beginWork();
            if (*active) ((*self).*work)();
        }
    }, [active, done] {
        if (*active) ((*(*active)->owner).*done)();
    }, [active](std::exception_ptr) {
        if (*active) (*active)->owner->failed();
    });
    return bool(m_outcomes->postWithOutcome(*m_core, std::move(task)));
}

void LoadSubscription::scheduleCleanup()
{
    decltype(m_retire_on_utility) retire;
    {
        std::lock_guard lock{m_mutex};
        if (!m_stopping || m_active_work) return;
        retire = std::move(m_retire_on_utility);
    }
    if (retire) retire(shared_from_this());
}

void LoadSubscription::cleanupFinished(bool cleanup_failed)
{
    auto reason = m_failure.load();
    if (m_failed) {
        if (reason == FailureReason::None) reason = FailureReason::Backend;
        if (const auto publication = m_state.apply(Event::RegistrationFailed)) m_notify({*publication, false, reason});
    }
    m_state.apply(Event::Close);
    if (const auto publication = m_state.apply(Event::CleanupFinished)) m_notify({*publication, cleanup_failed, reason});
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
    std::shared_ptr<ActiveWork> active;
    try { active = beginWork(); }
    catch (...) { requestFailure(FailureReason::IngressCapacity); return; }
    if (!active) return;
    uint64_t delivery{0};
    bool queued{false};
    const auto release_delivery = qScopeGuard([&] {
        if (delivery && !queued) { std::lock_guard lock{m_mutex}; m_unpublished.erase(delivery); }
    });
    // The wallet must die before the callback releases its work.
    auto incoming = std::move(wallet);
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
    uint64_t stamp{0};
    if (m_ingress_stamp) {
        try { stamp = m_ingress_stamp(); } catch (...) { requestFailure(FailureReason::Backend); return; }
        if (!stamp) { requestFailure(FailureReason::IngressCapacity); return; }
    }
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
    if (!m_utility->post([self] { self->failed(); })) retire();
}

void LoadSubscription::requestScan()
{
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping || m_failed || m_scan_pending.exchange(true)) return;
    }
    if (!postCore(&LoadSubscription::scan, &LoadSubscription::scanFinished) && !m_stopping) {
        requestFailure(FailureReason::Backend);
    }
}

void LoadSubscription::scanFinished()
{
    {
        std::lock_guard lock{m_mutex};
        m_scan_pending = false;
    }
    if (m_stopping || m_failed) return;
    publishChanged();
    bool more;
    {
        std::lock_guard lock{m_mutex};
        more = !m_pending.empty();
    }
    if (more) requestScan();
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
    // Only the subscription's Core scan calls this method.
    if(!native_name)native_name.emplace(deferred_name.trimmed().toStdString());
    return *native_name;
}
bool LoadSubscription::setNotificationContext(QString deferred_name,bool action_pending,uint64_t selection_revision,std::shared_ptr<void> adoption_origin,std::shared_ptr<walletpublication::NativeObservation> creation_observation)
{
    if(m_stopping||m_failed)return false;
    std::shared_ptr<ActiveWork> active;
    std::shared_ptr<NotificationContext> context;
    const auto make = [&] {
        auto value = std::make_unique<NotificationContext>();
        value->deferred_name = std::move(deferred_name);
        value->select_wallet = !action_pending;
        value->selection_revision = selection_revision;
        value->adoption_origin = std::move(adoption_origin);
        value->creation_observation = std::move(creation_observation);
        return value;
    };
    try {
        active = beginWork();
        if (!active) return false;
        context = execution::RetainValues<NotificationContext>(
            [this](auto cleanup) { return m_retirements->hold(std::move(cleanup)); }, make);
    } catch (...) { requestFailure(FailureReason::IngressCapacity); return false; }
    if(!context){requestFailure(FailureReason::IngressCapacity);return false;}
    std::shared_ptr<const NotificationContext> published=std::move(context);
    {std::lock_guard lock{m_mutex};if(m_stopping||m_failed)return false;m_notification_context.swap(published);}
    // The replaced context retires on a worker even without a pending ingress.
    return true;
}

void LoadSubscription::rejectNotificationContext()
{
    requestFailure(FailureReason::IngressCapacity);
}

void LoadSubscription::retire()
{
    {
        std::lock_guard lock{m_mutex};
        if (m_stopping.exchange(true)) return;
    }
    m_core->close();
    scheduleCleanup();
}

void LoadSubscription::whenRetired(std::function<void()> done)
{
    m_utility->whenDrained(std::move(done));
}

bool LoadSubscription::cleanup()
{
    m_stopping = true;
    // All running callbacks, outcomes and claimed handoffs have released.
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

std::unique_ptr<LoadSubscription::Handoff> LoadSubscription::take(bool initial)
{
    if (!initial) acknowledgeChange();
    auto active = beginWork();
    if (!active) return {};

    auto result = std::unique_ptr<Handoff>{new Handoff};
    std::lock_guard lock{m_mutex};
    if (m_stopping || m_failed || !m_initial_ready || m_handoff_disposal_pending) return {};
    if (initial ? m_initial.empty() : m_ready.empty()) return result;
    result->entries.reserve(GUI_HANDOFF);
    result->m_retirement = std::make_shared<std::vector<Entry>>();
    result->m_dispose = m_retirements->hold([active, entries = result->m_retirement, initial] {
        entries->clear();
        active->owner->handoffDisposed(initial);
    });
    if (!result->m_dispose) return {};
    m_handoff_disposal_pending = true;

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
LoadSubscription::Handoff::~Handoff()
{
    if (m_dispose) *m_retirement = std::move(entries);
}

void LoadSubscription::handoffDisposed(bool initial)
{
    { std::lock_guard lock{m_mutex}; m_handoff_disposal_pending = false; }
    if (m_stopping) return;
    if (initial) m_notify({Publication::InitialHandoff, false});
    else publishChanged();
}
} // namespace walletlife
