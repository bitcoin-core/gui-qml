// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/desktopintentservice.h>
#include <qml/executor.h>

#include <limits>
#include <map>
#include <mutex>
#include <utility>

namespace desktopintent {
struct Service::ReplyCell {
    std::mutex mutex;
    bool ready{false};
    Sink::Completed deliver;
    DeliveryKey key;
    Services::Time deadline;
    void complete(InspectionReceipt value) {
        Sink::Completed completed;
        {
            std::lock_guard lock{mutex};
            if (ready) return;
            ready = true;
            completed = std::exchange(deliver, {});
        }
        if (completed) completed(value);
    }
};
struct Service::State {
    Queue queue;
    std::shared_ptr<const Sink> sink;
    std::shared_ptr<const Sink> receiver;
    std::optional<DeliveryKey> presented;
    std::map<uint64_t, std::shared_ptr<ReplyCell>> replies;
    uint64_t sequence{0}, query_reply{0}, publication{0};
    bool wake_pending{false};
};
Service::Service(Services services, std::shared_ptr<State> state, Notify notify, std::shared_ptr<execution::SerialQueue> queue)
    : m_services(std::move(services)), m_queue(std::move(queue)),
      m_state(std::move(state)), m_notify(std::move(notify)) {}

std::shared_ptr<Service> Service::Create(Services services, Notify notify, std::shared_ptr<execution::SerialQueue> queue)
{
    if (!queue || !services.now || services.receipt_timeout <= std::chrono::milliseconds::zero() ||
        services.receipt_timeout > std::chrono::seconds{30}) return {};
    return {new Service(std::move(services), std::make_shared<State>(), std::move(notify), queue),
        [queue](Service* owner) {
            const auto retire = [queue, owner] {
                delete owner;
                queue->close();
            };
            if (!queue->post(retire)) retire();
        }};
}

bool Service::post(std::function<void()> decision, bool must_deliver)
{
    const auto turn = [self = shared_from_this(), decision = std::move(decision)] {
        std::lock_guard lock{self->m_decision_mutex};
        try { decision(); } catch (...) {}
    };
    if (m_queue->post(turn)) return true;
    if (must_deliver) turn();
    return must_deliver;
}

execution::SubmitResult Service::submit(std::function<void()> decision)
{
    const auto turn = [self = shared_from_this(), decision = std::move(decision)] {
        decision(); self->retireObsolete(); self->pump(); self->publish();
    };
    return {{}, post(turn) ? execution::Rejection::None : execution::Rejection::Transport};
}

void Service::publish()
{
    if (m_state->publication == std::numeric_limits<uint64_t>::max()) return;
    auto snapshot = m_state->queue.snapshot();
    snapshot.sequence = ++m_state->publication;
    try { if (m_notify) m_notify(std::move(snapshot)); } catch (...) {}
}

execution::SubmitResult Service::admit(std::vector<Input> inputs, std::function<void(Acceptance)> done)
{
    std::lock_guard lock{m_admission_mutex};
    if (m_closed->load() || m_forwarding_closed.load()) return {{}, execution::Rejection::Closed};
    if (inputs.empty() || inputs.size() > Queue::MAX_PENDING) return {{}, execution::Rejection::Invalid};
    for (const auto& input : inputs) {
        if (input.value.size() > Queue::MAX_INPUT_CHARS || input.producer_key.size() > Queue::MAX_KEY_CHARS)
            return {{}, execution::Rejection::Capacity};
    }
    auto retained = std::make_shared<const std::vector<Input>>(std::move(inputs));
    return submit([self = shared_from_this(), retained, done = std::move(done)] {
        if (self->m_closed->load()) self->m_state->queue.stop();
        auto receipt = self->m_state->queue.admit(*retained);
        try { if (done) done(std::move(receipt)); } catch (...) {}
    });
}

execution::SubmitResult Service::prepareForwarding(std::vector<Input> arguments, std::function<void(ForwardingSnapshot)> done)
{
    std::lock_guard lock{m_admission_mutex};
    if (m_closed->load() || m_forwarding_closed.load()) return {{}, execution::Rejection::Closed};
    if (arguments.size() > Queue::MAX_PENDING) return {{}, execution::Rejection::Invalid};
    for (const auto& input : arguments) {
        if (input.value.size() > Queue::MAX_INPUT_CHARS || input.producer_key.size() > Queue::MAX_KEY_CHARS)
            return {{}, execution::Rejection::Capacity};
    }
    auto retained = std::make_shared<const std::vector<Input>>(std::move(arguments));
    m_forwarding_closed.store(true);
    const auto admitted = submit([self = shared_from_this(), retained, done = std::move(done)] {
        if (self->m_closed->load()) self->m_state->queue.stop();
        auto snapshot = self->m_state->queue.prepareForwarding(*retained);
        try { if (done) done(std::move(snapshot)); } catch (...) {}
    });
    if (!admitted) m_forwarding_closed.store(false);
    return admitted;
}

execution::SubmitResult Service::readiness(uint64_t revision, bool ready)
{
    std::lock_guard lock{m_admission_mutex};
    if (m_forwarding_closed.load()) return {{}, execution::Rejection::Closed};
    return submit([self = shared_from_this(), revision, ready] {
        self->m_state->queue.readiness(revision, ready && !self->m_closed->load());
    });
}

std::shared_ptr<Service::ReplyCell> Service::reserve(ReplyKind kind, DeliveryKey key)
{
    auto& state = *m_state;
    if (state.sequence == std::numeric_limits<uint64_t>::max()) return {};
    const auto sequence = ++state.sequence;
    auto cell = std::make_shared<ReplyCell>();
    cell->key = key;
    const auto now = m_services.now();
    const auto latest = Services::Time::max() - m_services.receipt_timeout;
    cell->deadline = now > latest ? Services::Time::max() : now + m_services.receipt_timeout;
    const auto consume = [self = shared_from_this(), sequence, kind, key](InspectionReceipt receipt) {
        self->received(sequence, kind, key, receipt);
    };
    cell->deliver = [self = shared_from_this(), consume](InspectionReceipt receipt) {
        self->post([consume, receipt] { consume(receipt); }, /*must_deliver=*/true);
    };
    try { state.replies.emplace(sequence, cell); }
    catch (...) { cell->complete({}); return {}; }
    if (kind == ReplyKind::Query) state.query_reply = sequence;
    return cell;
}

execution::SubmitResult Service::review(std::shared_ptr<const Sink> sink)
{
    return submit([self = shared_from_this(), sink = std::move(sink)] { self->deliver(sink); });
}
execution::SubmitResult Service::registerReceiver(std::shared_ptr<const Sink> sink)
{
    return submit([self = shared_from_this(), sink = std::move(sink)] {
        if (!self->m_closed->load() && sink && sink->identity && sink->inspect && sink->query) self->m_state->receiver = sink;
    });
}
execution::SubmitResult Service::releasePresentation(DeliveryKey key)
{
    {
        std::lock_guard lock{m_presentation_mutex};
        if (m_presentation_release != key) return {{}, execution::Rejection::Invalid};
        m_presentation_release.reset();
    }
    post([self = shared_from_this(), key] { self->presentationReleased(key); }, /*must_deliver=*/true);
    return {};
}
bool Service::reservePresentation(DeliveryKey key)
{
    std::lock_guard lock{m_presentation_mutex};
    m_presentation_release = key;
    return true;
}
void Service::presentationReleased(DeliveryKey key)
{
    {
        std::lock_guard lock{m_presentation_mutex};
        if (m_presentation_release == key) m_presentation_release.reset();
    }
    if (m_state->presented == key) {
        m_state->presented.reset();
        m_state->queue.receiverCapacity(true);
        if (m_state->queue.snapshot().active != key) m_state->sink.reset();
    }
    pump(); publish();
}
void Service::pump()
{
    const auto snapshot = m_state->queue.snapshot();
    if (m_state->receiver && !m_state->presented && snapshot.phase == Phase::Ready && snapshot.pending) deliver(m_state->receiver);
}
void Service::deliver(std::shared_ptr<const Sink> sink)
{
    auto& state = *m_state;
    if (m_closed->load() || m_forwarding_closed.load() || state.presented || !sink || !sink->identity || !sink->inspect || !sink->query) return;
    const auto delivery = state.queue.deliver(sink->identity);
    if (!delivery) return;
    try {
        if (m_services.available && !m_services.available()) {
            state.queue.rejected(delivery->key); return;
        }
    } catch (...) { state.queue.rejected(delivery->key); return; }
    std::shared_ptr<ReplyCell> cell;
    try { cell = reserve(ReplyKind::Inspect, delivery->key); } catch (...) {}
    if (!cell) { state.queue.rejected(delivery->key); return; }
    if (state.receiver && state.receiver->identity == sink->identity) {
        bool reserved{false};
        try { reserved = reservePresentation(delivery->key); } catch (...) {}
        if (!reserved) { cell->complete({Knowledge::NotAccepted, 0}); state.queue.rejected(delivery->key); return; }
        state.presented = delivery->key; state.queue.receiverCapacity(false);
    }
    state.sink = sink;
    try {
        if (!sink->inspect(*delivery, [cell](InspectionReceipt reply) { cell->complete(reply); }))
            cell->complete({Knowledge::NotAccepted, 0});
    } catch (...) { cell->complete({}); }
    wake();
}
void Service::received(uint64_t reply, ReplyKind kind, DeliveryKey key, InspectionReceipt receipt)
{
    auto& state = *m_state;
    state.replies.erase(reply);
    if (state.query_reply == reply) state.query_reply = 0;
    if (state.queue.snapshot().active != key) { retireObsolete(); publish(); return; }
    switch (receipt.knowledge) {
    case Knowledge::Accepted:
        if (!receipt.inspection) state.queue.unknown(key);
        else { state.queue.accepted(key, receipt.inspection); if (state.presented != key) state.sink.reset(); }
        break;
    case Knowledge::NotAccepted:
        state.queue.rejected(key); state.sink.reset();
        if (state.presented == key) releasePresentation(key);
        break;
    case Knowledge::Unknown:
        state.queue.unknown(key);
        break;
    }
    (void)kind;
    retireObsolete(); wake(); pump(); publish();
}

void Service::query()
{
    auto& state = *m_state;
    const auto current = state.queue.snapshot();
    if (!current.active || state.query_reply || !state.sink || !state.sink->query) return;
    const auto cell = reserve(ReplyKind::Query, *current.active);
    if (!cell) { state.queue.unknown(*current.active); wake(); return; }
    try {
        if (!state.sink->query(*current.active, [cell](InspectionReceipt reply) { cell->complete(reply); }))
            cell->complete({});
    } catch (...) { cell->complete({}); }
    wake();
}
void Service::retireObsolete()
{
    const auto active = m_state->queue.snapshot().active;
    for (const auto& [identity, cell] : m_state->replies) {
        (void)identity;
        if (active != cell->key) cell->complete({});
    }
}

void Service::wake()
{
    if (m_state->wake_pending || !m_state->queue.snapshot().active || !m_services.wake) return;
    m_state->wake_pending = true;
    try {
        m_services.wake(m_services.receipt_timeout, [weak = weak_from_this()] {
            if (const auto self = weak.lock()) self->tick();
        });
    } catch (...) {
        m_state->wake_pending = false;
        if (const auto active = m_state->queue.snapshot().active) m_state->queue.unknown(*active);
    }
}

execution::SubmitResult Service::tick()
{
    return submit([self = shared_from_this()] {
        auto& state = *self->m_state;
        state.wake_pending = false;
        const auto now = self->m_services.now();
        for (const auto& [identity, cell] : state.replies) {
            (void)identity;
            if (now >= cell->deadline) cell->complete({});
        }
        const auto snapshot = state.queue.snapshot();
        if (snapshot.phase == Phase::ReconcilingDelivery || snapshot.phase == Phase::Stopping) self->query();
        self->wake();
    });
}

execution::SubmitResult Service::reconcile() { return submit([self = shared_from_this()] { self->query(); }); }
execution::SubmitResult Service::dismiss() { return submit([self = shared_from_this()] { self->m_state->queue.dismiss(); }); }
execution::SubmitResult Service::lookup(QString producer_key, std::function<void(std::optional<Receipt>)> done)
{
    if (producer_key.size() > Queue::MAX_KEY_CHARS) return {{}, execution::Rejection::Invalid};
    return submit([self = shared_from_this(), producer_key = std::move(producer_key), done = std::move(done)] {
        try { if (done) done(self->m_state->queue.lookup(producer_key)); } catch (...) {}
    });
}
execution::SubmitResult Service::stop()
{
    if (!m_closed->exchange(true)) post([self = shared_from_this()] { self->stopped(); }, /*must_deliver=*/true);
    return {};
}
void Service::stopped()
{
    auto& state = *m_state;
    const auto active = state.queue.snapshot().active;
    const auto presented = state.presented;
    state.queue.stop();
    const auto retire = active ? active : presented;
    if (presented) releasePresentation(*presented);
    state.presented.reset(); state.receiver.reset();
    if (retire && state.sink && state.sink->retire) {
        try { state.sink->retire(*retire); } catch (...) {}
    }
    if (!active) state.sink.reset();
    query(); retireObsolete(); publish();
}
} // namespace desktopintent
