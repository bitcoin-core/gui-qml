// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/networkcommand.h>
#include <qml/atomicsharedptr.h>
#include <qml/retainedsnapshot.h>

#include <algorithm>
#include <atomic>
#include <limits>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace networkcommand {
void Command::setAvailable(bool available, uint64_t revision)
{
    if (m_closed || revision <= m_availability) return;
    m_availability = revision;
    m_available = available;
    m_observed.reset();
    if (!available && m_queued) {
        m_queued.reset(); m_outcome = Outcome::Rejected; m_error = Error::Unavailable;
    }
}
bool Command::observe(Observation observation)
{
    if (!m_available || (m_observed && observation.generation <= m_observed->generation)) return false;
    m_observed = observation;
    if (!m_running && m_outcome == Outcome::Succeeded && m_effect.id == m_request_id
        && observation.generation > m_effect.after) m_outcome = Outcome::Superseded;
    if (m_outcome == Outcome::Idle && m_error == Error::Unavailable) m_error = Error::None;
    return true;
}
void Command::queryUnavailable()
{
    if (!m_closed && m_error == Error::None) m_error = Error::Unavailable;
}
std::optional<Intent> Command::start(Intent intent)
{
    // Even an apparent no-op needs a current Native fence/observation. A
    // delayed external notification must not turn old cache into success.
    intent.observation_only = m_observed->active == intent.active;
    m_running = intent;
    m_outcome = Outcome::Pending;
    return intent;
}
std::optional<Intent> Command::request(Intent intent)
{
    if (!intent.id || intent.id <= m_request_id) return {};
    m_request_id = intent.id;
    m_error = Error::None;
    if (m_closed) { m_outcome = Outcome::Closed; m_error = Error::Closed; return {}; }
    if (!m_available || !m_observed || intent.availability != m_availability) {
        m_outcome = Outcome::Rejected; m_error = Error::Unavailable; return {};
    }
    if (m_running) { m_queued = intent; m_outcome = Outcome::Pending; return {}; }
    if (intent.base != m_observed->generation) { m_outcome = Outcome::Superseded; return {}; }
    return start(intent);
}
std::optional<Intent> Command::finish(Effect effect)
{
    if (!m_running || effect.id != m_running->id) return {};
    const auto original = *m_running;
    m_running.reset();
    m_effect = effect;
    if (effect.observation) {
        const bool fresh_original_read = effect.status == EffectStatus::Applied && effect.own_frontier
            && effect.current == effect.after && effect.observation->generation == effect.after;
        if ((!m_observed || effect.observation->generation > m_observed->generation)
            || (fresh_original_read && effect.observation->generation == m_observed->generation)) {
            m_observed = effect.observation;
        } else if (!m_closed) observe(*effect.observation);
    }
    if (m_closed) { m_queued.reset(); m_outcome = Outcome::Closed; m_error = Error::Closed; return {}; }
    if (effect.status == EffectStatus::Uncertain) {
        const bool owns_latest = m_queued.has_value() || original.id == m_request_id;
        m_queued.reset();
        if (owns_latest) { m_outcome = Outcome::Uncertain; m_error = Error::Unknown; }
        return {};
    }
    if (m_queued) {
        auto next = *m_queued;
        m_queued.reset();
        if (!m_available || !m_observed || next.availability != m_availability) {
            m_outcome = Outcome::Rejected; m_error = Error::Unavailable; return {};
        }
        const bool only_own_advance = effect.status == EffectStatus::Applied && effect.own_frontier
            && effect.current == effect.after && m_observed->generation == effect.after && next.base == effect.before;
        if (m_observed->generation > next.base && !only_own_advance) { m_outcome = Outcome::Superseded; return {}; }
        // This is either a fresh intent based on the newer observation, or the
        // same original intent crossing only its predecessor's own notice.
        next.base = m_observed->generation;
        m_error = Error::None;
        return start(next);
    }
    if (original.id != m_request_id) return {}; // Retain a newer rejection.
    if (effect.status == EffectStatus::NotStarted) { m_outcome = Outcome::Rejected; m_error = Error::Admission; }
    else if (effect.status == EffectStatus::Superseded || !m_observed
             || effect.current != effect.after || m_observed->generation != effect.after) m_outcome = Outcome::Superseded;
    else if (effect.succeeded) { m_outcome = Outcome::Succeeded; m_error = Error::None; }
    else { m_outcome = Outcome::Rejected; m_error = Error::Mismatch; }
    return {};
}
void Command::close()
{
    m_closed = true; m_available = false; m_queued.reset();
    m_outcome = Outcome::Closed; m_error = Error::Closed;
}
Projection Command::projection() const
{
    Projection result;
    result.active = m_observed && m_observed->active;
    result.known = m_observed.has_value();
    result.generation = m_observed ? m_observed->generation : 0;
    result.ready = m_available && result.known && !m_closed;
    result.pending = m_running.has_value();
    result.closed = m_closed;
    result.request_id = m_request_id;
    result.availability = m_availability;
    result.outcome = m_outcome; result.error = m_error; result.effect = m_effect;
    return result;
}

struct Service::Publication {
    mutable std::mutex mutex;
    bool closed{false};
    ownership::AtomicSharedPtr<const Projection> value;
    // Fixed scalar receipt: snapshot-capacity rejection cannot erase an
    // accepted outcome or leave ingress pending. No structural GUI payload.
    Projection fallback;
};

struct Service::State {
    struct Completion { std::function<void()> done; ~Completion() { if (done) done(); } };
    // Last destroyed: every original port, effect and retained projection has
    // physically retired before the owner's completion acknowledgment.
    Completion completion;
    struct Snapshot { Projection value; std::shared_ptr<State> original; };
    struct Input { Intent intent; std::shared_ptr<execution::Scheduler> action; };
    struct Availability { bool available; uint64_t revision; };
    struct Demand {
        std::optional<Intent> effect;
        std::shared_ptr<execution::Scheduler> read_scope;
    };
    const execution::SnapshotServices execution;
    const Ports ports;
    const std::shared_ptr<Publication> publication;
    const std::function<void()> changed;
    std::weak_ptr<State> owner;
    ownership::AtomicSharedPtr<execution::Scheduler> lifetime;
    const execution::ResourceKey domain, native;
    std::atomic_bool stopping{false}, available{false};
    std::atomic<uint64_t> availability{0};
    std::mutex inbox_mutex;
    std::optional<Input> next;
    std::optional<Observation> observation;
    std::optional<Availability> availability_input;
    bool scheduled{false};
    Command command;
    std::map<uint64_t, std::shared_ptr<execution::Scheduler>> actions;
    uint64_t acknowledged{0};
    bool reading{false}, read_again{false};

    State(execution::SnapshotServices execution, Ports ports, std::shared_ptr<Publication> publication,
          std::function<void()> changed, std::function<void()> drained)
        : completion{std::move(drained)}, execution(std::move(execution)), ports(std::move(ports)),
          publication(std::move(publication)), changed(std::move(changed)),
          lifetime(this->execution.scheduler->openActionScope()),
          domain(this->execution.scheduler->allocateKey()), native(this->execution.scheduler->allocateKey()) {}

    void publish()
    {
        auto value = command.projection(); value.acknowledged = acknowledged;
        const auto scope = lifetime.load();
        const auto self = owner.lock();
        const auto retained = scope ? execution::RetainSnapshot<Snapshot>(scope, [&] {
            return std::make_unique<Snapshot>(Snapshot{value, self});
        }) : std::shared_ptr<Snapshot>{};
        {
            std::lock_guard lock{publication->mutex};
            publication->fallback = value;
            if (!publication->closed) publication->value.store(retained ? std::shared_ptr<const Projection>{retained, &retained->value} : nullptr);
        }
        try { if (changed) changed(); } catch (...) {} // The actual receipt remains owned.
    }
    bool wake(const std::shared_ptr<execution::Scheduler>& scope)
    {
        { std::lock_guard lock{inbox_mutex}; if (scheduled) return true; scheduled = true; }
        const auto self = owner.lock();
        if (scope && execution::Submit(*scope, [self] { self->consume(); }, [] {}, {},
                {execution::Lane::Domain, domain, execution::WorkClass::Action})) return true;
        std::lock_guard lock{inbox_mutex}; scheduled = false;
        return false;
    }
    void prune()
    {
        for (auto entry = actions.begin(); entry != actions.end();) {
            const bool running = command.running() && command.running()->id == entry->first;
            const bool queued = command.queued() && command.queued()->id == entry->first;
            if (!running && !queued) entry = actions.erase(entry); else ++entry;
        }
    }
    Demand takeInputs()
    {
        Demand demand;
        std::optional<Input> input;
        std::optional<Observation> observed;
        std::optional<Availability> status;
        {
            std::lock_guard lock{inbox_mutex};
            input = std::exchange(next, {}); observed = std::exchange(observation, {});
            status = std::exchange(availability_input, {}); scheduled = false;
        }
        if (stopping) {
            command.close(); if (input) acknowledged = std::max(acknowledged, input->intent.id);
            actions.clear(); lifetime.store({}); return demand;
        }
        if (status) command.setAvailable(status->available, status->revision);
        if (observed) command.observe(*observed);
        if (input) {
            acknowledged = std::max(acknowledged, input->intent.id);
            actions[input->intent.id] = std::move(input->action);
            demand.effect = command.request(input->intent);
            prune();
        }
        if (status && status->available && !command.projection().known) demand.read_scope = lifetime.load();
        return demand;
    }
    void consume() { advance(takeInputs()); }
    void advance(Demand demand)
    {
        // Synchronous rejection accounts for known non-execution in this
        // original Domain turn. It neither calls an asynchronous completion
        // inline nor admits another turn to rescue an accepted outcome.
        // No inbox is consumed in this loop, so only the already owned running
        // intent and its one coalesced successor can be considered here.
        if (stopping) command.close();
        while (demand.effect) {
            std::optional<Effect> rejected;
            if (stopping) {
                command.close();
                Effect not_started; not_started.id = demand.effect->id; not_started.status = EffectStatus::NotStarted;
                rejected = not_started;
            } else {
                rejected = dispatch(*demand.effect);
            }
            if (!rejected) break;
            demand.effect = command.finish(*rejected);
            prune();
        }
        if (demand.read_scope) read(demand.read_scope);
        if (stopping) command.close();
        publish();
    }
    std::optional<Effect> dispatch(Intent intent)
    {
        const auto self = owner.lock();
        const auto action = actions.at(intent.id);
        const auto work = [self, intent] {
            Effect result; result.id = intent.id;
            try {
                if (self->stopping || !self->available || self->availability != intent.availability) return result;
                result.before = self->ports.generation();
                if (result.before != intent.base) {
                    result.status = EffectStatus::Superseded;
                    result.observation = self->ports.observe();
                    result.after = result.current = self->ports.generation();
                    return result;
                }
                // This fence is not an atomic Core/RPC compare-and-set. A
                // concurrent RPC may still run between this check and set.
                if (self->stopping || !self->available || self->availability != intent.availability) return result;
                if (intent.observation_only) {
                    result.observation = self->ports.observe();
                    result.after = result.observation->generation;
                    result.current = self->ports.generation();
                    if (result.after != result.before || result.current != result.before) {
                        result.status = EffectStatus::Superseded;
                        return result;
                    }
                    // The cached no-op hint is not authority to skip a current
                    // Native read. A changed value without a delivered notice
                    // still needs the admitted original intent's effect.
                    if (result.observation->active == intent.active) {
                        result.succeeded = true;
                        result.own_frontier = true;
                        result.status = EffectStatus::Applied;
                        return result;
                    }
                }
                if (self->stopping || !self->available || self->availability != intent.availability) return result;
                result.attempted = true;
                self->ports.set_active(intent.active);
                result.after = self->ports.generation();
                result.observation = self->ports.observe();
                result.current = self->ports.generation();
                result.succeeded = result.observation->active == intent.active;
                result.own_frontier = result.observation->generation == result.after
                    && (result.after == result.before || (result.before != std::numeric_limits<uint64_t>::max()
                        && result.after == result.before + 1 && result.succeeded));
                result.status = EffectStatus::Applied;
            } catch (...) { result.status = result.attempted ? EffectStatus::Uncertain : EffectStatus::NotStarted; }
            return result;
        };
        const auto done = [self, action](Effect result) {
            // Reconcile latest ingress before accounting, without starting
            // effects from inside the outcome reducer.
            auto demand = self->takeInputs();
            const auto next_effect = self->command.finish(result);
            self->prune();
            if (next_effect) demand.effect = next_effect;
            if (result.status == EffectStatus::Uncertain && !self->stopping) demand.read_scope = action;
            self->advance(std::move(demand));
        };
        const auto failed = [done, id=intent.id](std::exception_ptr error) {
            Effect result; result.id = id; result.status = EffectStatus::Uncertain; result.attempted = true;
            try { if (error) std::rethrow_exception(error); }
            catch (const execution::NotStarted&) { result.status = EffectStatus::NotStarted; result.attempted = false; }
            catch (...) {}
            done(result);
        };
        if (execution::Submit(*action, work, done, failed, {execution::Lane::Core, native, execution::WorkClass::Action})) return {};
        Effect rejected; rejected.id = intent.id; rejected.status = EffectStatus::NotStarted;
        return rejected;
    }
    void read(const std::shared_ptr<execution::Scheduler>& scope)
    {
        if (!scope || stopping || !available) return;
        if (reading) { read_again = true; return; }
        reading = true;
        const auto revision = availability.load();
        const auto self = owner.lock();
        struct Read { std::optional<Observation> value; };
        const auto finish = [self, scope, revision](Read result) {
            self->reading = false;
            if (!self->stopping && self->availability == revision && result.value) self->command.observe(*result.value);
            else if (!self->stopping && self->availability == revision) self->command.queryUnavailable();
            const bool again = std::exchange(self->read_again, false);
            self->publish();
            if (again && !self->stopping) self->read(scope);
        };
        if (!execution::Submit(*scope, [self, revision] {
            Read result;
            if (self->stopping || !self->available || self->availability != revision) return result;
            try { result.value = self->ports.observe(); } catch (...) {}
            return result;
        }, finish, [finish](std::exception_ptr) { finish({}); },
            {execution::Lane::Core, native, execution::WorkClass::Observation})) {
            // A read that never started has no outcome callback to follow.
            // Account its failure here; only an admitted asynchronous read's
            // completion may dispatch the one coalesced follow-up.
            reading = false;
            if (!stopping && availability == revision) command.queryUnavailable();
            publish();
        }
    }
};

Service::Service(execution::SnapshotServices execution, Ports ports, std::function<void()> changed, std::function<void()> drained)
    : m_execution(std::move(execution)), m_publication(std::make_shared<Publication>())
{
    if (!m_execution.scheduler || !ports.observe || !ports.set_active || !ports.generation)
        throw std::invalid_argument{"Missing network command port"};
    m_owner = execution::RetainSnapshot<State>(m_execution.scheduler, [&] {
        return std::make_unique<State>(m_execution, std::move(ports), m_publication, std::move(changed), std::move(drained));
    });
    if (m_owner) { m_state = m_owner; m_owner->owner = m_owner; }
    else {
        m_publication->fallback.outcome = Outcome::Rejected; m_publication->fallback.error = Error::Admission;
        if (changed) changed();
        if (drained) drained();
    }
}
Service::~Service() { close(); }
bool Service::request(Intent intent)
{
    const auto state = m_state.lock();
    if (!state || state->stopping) return false;
    const auto action = m_execution.scheduler->openActionScope();
    if (!action) return false;
    {
        std::lock_guard lock{state->inbox_mutex};
        if (!state->next || intent.id > state->next->intent.id) state->next = State::Input{intent, action};
    }
    if (state->wake(action)) return true;
    std::lock_guard lock{state->inbox_mutex};
    if (state->next && state->next->intent.id == intent.id) state->next.reset();
    return false;
}
bool Service::observe(Observation observation)
{
    const auto state = m_state.lock(); if (!state || state->stopping) return false;
    { std::lock_guard lock{state->inbox_mutex};
      if (!state->observation || observation.generation > state->observation->generation) state->observation = observation; }
    return state->wake(state->lifetime.load());
}
bool Service::setAvailable(bool available, uint64_t revision)
{
    const auto state = m_state.lock(); if (!state || state->stopping) return false;
    if (revision <= state->availability.load()) return false;
    state->available = available; state->availability = revision;
    { std::lock_guard lock{state->inbox_mutex}; state->availability_input = State::Availability{available, revision}; }
    return state->wake(state->lifetime.load());
}
Projection Service::projection() const
{
    std::lock_guard lock{m_publication->mutex};
    const auto value = m_publication->value.load();
    return value ? *value : m_publication->fallback;
}
void Service::retire()
{
    // Release the publication alias before the original owner. Existing task
    // callbacks retain that owner through their accepted outcome chain; its
    // pre-reserved Native cleanup then retires ports and the lifetime scope.
    // Passive observer loss is not an implicit cancellation command.
    { std::lock_guard lock{m_publication->mutex}; m_publication->closed = true; m_publication->value.store({}); }
    m_state.reset();
    m_owner.reset();
}
void Service::close()
{
    if (const auto state = m_state.lock()) {
        state->stopping = true;
        { std::lock_guard lock{m_publication->mutex}; m_publication->closed = true; m_publication->value.store({}); }
        const auto scope = state->lifetime.exchange({});
        state->wake(scope);
    }
    m_state.reset(); m_owner.reset();
}
} // namespace networkcommand
