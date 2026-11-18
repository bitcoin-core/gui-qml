// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/nodelifecycleworker.h>
#include <qml/atomicsharedptr.h>
#include <qml/retainedsnapshot.h>
#include <qml/retainedvalue.h>

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace nodelifecycle {
namespace {
enum class Operation { Initialize, Interrupt, Shutdown };
struct Result {
    NativeInitialization initialization{};
    int exit_status{0};
    bool fatal{false};
};
} // namespace

struct Driver::Mailbox {
    mutable std::mutex mutex;
    Projection value;
    uint64_t revision{0};
    bool closed{false};

    Projection read() const
    {
        std::lock_guard lock{mutex};
        return value;
    }
    void record(Projection next)
    {
        std::lock_guard lock{mutex};
        if (closed) {
            // Close releases Native diagnostic borrowers/capabilities before
            // physical drain. Existing projection consumers retain their own
            // original diagnostic; the mailbox keeps scalar effect receipts.
            next.diagnostic.reset(); next.diagnostic_scope.reset();
            if (next.phase == Phase::Ready) next.phase = Phase::Stopping;
        }
        value = std::move(next);
        ++revision;
    }
    bool deliver(const Publish& publish, bool stopping)
    {
        Projection current;
        uint64_t original;
        {
            std::lock_guard lock{mutex};
            if (closed) return false;
            current = value; original = revision;
        }
        if (stopping && current.phase == Phase::Ready) current.phase = Phase::Stopping;
        bool failed{false};
        try { if (publish) publish(std::move(current)); }
        catch (...) { failed = true; }
        std::lock_guard lock{mutex};
        if (revision == original && !closed) value.publication_failed = failed;
        return !failed;
    }
    void close()
    {
        std::lock_guard lock{mutex};
        if (closed) return;
        closed = true;
        value.diagnostic.reset(); value.diagnostic_scope.reset();
        if (value.phase != Phase::Fatal && value.phase != Phase::Stopped) {
            if (value.phase != Phase::Draining) value.phase = Phase::Stopping;
            value.event = Event::State;
        }
        ++revision;
    }
};

struct Driver::State {
    State(execution::SnapshotServices services, Effects effect_ports, Publish projection,
          std::shared_ptr<std::atomic_bool> stop, std::shared_ptr<Mailbox> mailbox)
        : execution(std::move(services)), effects(std::move(effect_ports)), publish(std::move(projection)),
          stopping(std::move(stop)), mailbox(std::move(mailbox)), scope(execution.scheduler->openActionScope()),
          domain_key(execution.scheduler->allocateKey()), native_key(execution.scheduler->allocateKey()),
          control_key(execution.scheduler->allocateKey()) {}
    const execution::SnapshotServices execution;
    const Effects effects;
    const Publish publish;
    const std::shared_ptr<std::atomic_bool> stopping;
    const std::shared_ptr<Mailbox> mailbox;
    ownership::AtomicSharedPtr<execution::Scheduler> scope;
    const execution::ResourceKey domain_key, native_key, control_key;
    std::weak_ptr<State> owner;
    Flow flow;
    std::optional<NativeInitialization> initialization;
    std::shared_ptr<execution::Scheduler> shutdown_action;
    bool retired{false}, initialization_pending{false}, shutdown_requested{false};

    bool notify(Event event = Event::State, std::shared_ptr<const QString> diagnostic = {},
                std::shared_ptr<execution::Scheduler> diagnostic_scope = {}, bool deliver = true)
    {
        Projection value;
        if (flow.phase() == Phase::Fatal && event != Event::Fatal) {
            // A late accepted result must not replace the first fatal receipt
            // while its original notification is awaiting explicit retry.
            value = mailbox->read(); value.event = Event::Fatal;
        } else {
            value.event = event; value.diagnostic = std::move(diagnostic);
            value.diagnostic_scope = std::move(diagnostic_scope);
        }
        value.phase = flow.phase(); value.initialization = initialization;
        value.core_exit_status = flow.coreExitStatus();
        mailbox->record(std::move(value));
        return deliver && mailbox->deliver(publish, stopping->load());
    }
    bool post(const std::shared_ptr<execution::Scheduler>& action, std::function<void(State&)> command,
              std::function<void(State&, std::exception_ptr)> failure = {})
    {
        const auto self = owner.lock();
        if (!self || !action) return false;
        return bool(execution::Submit(*action, [self, command = std::move(command)] { command(*self); }, [] {},
            [self, failure = std::move(failure)](std::exception_ptr error) { if (failure) failure(*self, error); },
            {execution::Lane::Domain, domain_key, execution::WorkClass::Action}));
    }
    bool postControl(const std::shared_ptr<execution::Scheduler>& action, std::function<void(State&)> command)
    {
        const auto self = owner.lock();
        if (!self || !action) return false;
        // Its reserved receipt carries this decision into Domain even when
        // ordinary admission is closed/full. The Native preparation turn
        // performs no Core operation and changes no Domain state.
        return bool(execution::Submit(*execution.scheduler, [] {},
            [self, action, command = std::move(command)] { command(*self); },
            [self, action](std::exception_ptr error) { self->rejected(Operation::Interrupt, self->flow.attempt(), error, action); },
            {execution::Lane::Control, control_key, execution::WorkClass::Action}));
    }
    bool terminal() const { return retired || flow.phase() == Phase::Fatal || flow.phase() == Phase::Stopped; }
    void begin(Operation operation, const std::shared_ptr<execution::Scheduler>& action)
    {
        if (terminal()) return;
        uint64_t attempt = flow.attempt();
        if (operation == Operation::Initialize) {
            const auto accepted = flow.start();
            if (!accepted) return;
            attempt = *accepted; initialization_pending = true;
            if (stopping->load()) flow.stop();
        } else if (operation == Operation::Interrupt) {
            if (!flow.beginInterrupt()) return;
        } else {
            if (!flow.beginShutdown()) return;
        }
        notify();
        dispatch(operation, attempt, action);
    }
    void requestShutdown(const std::shared_ptr<execution::Scheduler>& action)
    {
        if (terminal()) return;
        if (!shutdown_requested) { shutdown_requested = true; shutdown_action = action; }
        flow.stop();
        if (flow.phase() == Phase::Draining) maybeShutdown();
        else begin(Operation::Interrupt, action);
    }
    void maybeShutdown()
    {
        if (!terminal() && shutdown_requested && !initialization_pending && shutdown_action)
            begin(Operation::Shutdown, shutdown_action);
    }
    void dispatch(Operation operation, uint64_t attempt, const std::shared_ptr<execution::Scheduler>& action)
    {
        const auto self = owner.lock();
        const auto diagnostic = execution::RetainValues<QString>(action, [] { return std::make_unique<QString>(); });
        if (!diagnostic) {
            rejected(operation, attempt, std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}), action);
            return;
        }
        const auto work = [self, operation, diagnostic] {
            Result result{};
            try {
                switch (operation) {
                case Operation::Initialize: result.initialization = self->effects.initialize(*self->stopping); break;
                case Operation::Interrupt: self->effects.interrupt(); break;
                case Operation::Shutdown: result.exit_status = self->effects.shutdown(); break;
                }
            } catch (...) {
                result.fatal = true;
                try { *diagnostic = self->effects.describe ? self->effects.describe(std::current_exception()) : QStringLiteral("The node lifecycle effect failed."); }
                catch (...) { *diagnostic = QStringLiteral("The node lifecycle diagnostic could not be prepared."); }
            }
            return result;
        };
        const auto done = [self, operation, attempt, diagnostic, action](Result result) {
            self->completed(operation, attempt, std::move(result), diagnostic, action);
        };
        const auto failed = [self, operation, attempt, action](std::exception_ptr error) {
            self->rejected(operation, attempt, error, action);
            // This is the already admitted asynchronous outcome turn. Only
            // it may advance a shutdown waiting for initialization accounting.
            self->maybeShutdown();
        };
        const bool control = operation == Operation::Interrupt;
        const auto scheduler = control ? execution.scheduler : action;
        // Action scopes deliberately reject Control. The root's Control
        // reservation owns this effect; its diagnostic/action capability is
        // independently captured by the completion and failure receipts.
        if (!execution::Submit(*scheduler, work, done, failed,
                {control ? execution::Lane::Control : execution::Lane::Core, control ? control_key : native_key, execution::WorkClass::Action}))
            rejected(operation, attempt, std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}), action);
    }
    void fatal(const std::shared_ptr<const QString>& diagnostic, const std::shared_ptr<execution::Scheduler>& action)
    {
        shutdown_action.reset();
        if (flow.fatal(diagnostic ? *diagnostic : QStringLiteral("The result of the node action could not be shown.")))
            notify(Event::Fatal, diagnostic, action);
    }
    void completed(Operation operation, uint64_t attempt, Result result, const std::shared_ptr<QString>& diagnostic,
                   const std::shared_ptr<execution::Scheduler>& action)
    {
        if (operation == Operation::Initialize) initialization_pending = false;
        if (result.fatal) { fatal(diagnostic, action); return; }
        switch (operation) {
        case Operation::Initialize:
            // Stop is an ingress fence, including before its Domain command
            // arrives. The raw successful Native receipt stays unchanged.
            if (stopping->load()) flow.stop();
            initialization = result.initialization;
            if (flow.initialized({attempt, result.initialization.attempted, result.initialization.success, result.initialization.cancelled})) {
                notify(Event::Initialized, {}, {}, flow.phase() != Phase::Fatal);
            } else {
                const auto invalid = execution::RetainValues<QString>(action, [] {
                    return std::make_unique<QString>(QStringLiteral("The saved startup record is not valid."));
                });
                fatal(invalid, action);
            }
            maybeShutdown();
            break;
        case Operation::Interrupt:
            if (flow.interrupted()) notify(Event::Interrupted);
            maybeShutdown();
            break;
        case Operation::Shutdown:
            if (flow.coreStopped(result.exit_status) && flow.drained()) {
                retired = true; shutdown_action.reset();
                // Failed terminal notification retains only its original
                // read-only publication capability until retry or close.
                if (notify(Event::Shutdown)) scope.store({});
            }
            break;
        }
    }
    void rejected(Operation operation, uint64_t attempt, std::exception_ptr error,
                  const std::shared_ptr<execution::Scheduler>& action)
    {
        // Leaf accounting also serves synchronous dispatch rejection. Forward
        // shutdown progress belongs to the already admitted outcome callback,
        // never to this path back into the Native dispatcher.
        if (operation == Operation::Initialize) initialization_pending = false;
        if (const auto cause = execution::NotStartedCause(error); operation == Operation::Initialize && cause) {
            if (stopping->load()) flow.stop();
            const bool cancelled = cause == execution::NotStarted::Cause::Cancelled;
            if (flow.initialized({attempt, false, false, cancelled})) {
                initialization = NativeInitialization{};
                initialization->cancelled = cancelled;
                notify(Event::Initialized, {}, {}, flow.phase() != Phase::Fatal);
            }
            return;
        }
        const auto text = execution::RetainValues<QString>(action, [] {
            return std::make_unique<QString>(QStringLiteral("The node action may not have finished."));
        });
        fatal(text, action);
    }
    void rejectedBeforeStart(std::exception_ptr error, const std::shared_ptr<execution::Scheduler>& action)
    {
        if (flow.attempt() || terminal()) return;
        const auto attempt = flow.start();
        if (attempt) {
            rejected(Operation::Initialize, *attempt, error, action);
            maybeShutdown();
        } else {
            initialization = NativeInitialization{};
            initialization->cancelled = stopping->load();
            notify(Event::Initialized);
        }
    }
    void retryNotification()
    {
        if (mailbox->deliver(publish, stopping->load()) && flow.phase() == Phase::Stopped) scope.store({});
    }
};

Driver::Driver(execution::SnapshotServices services, Effects effects, Publish publish)
    : m_execution(std::move(services)), m_stopping(std::make_shared<std::atomic_bool>(false)), m_mailbox(std::make_shared<Mailbox>())
{
    if (!m_execution.scheduler || !effects.initialize || !effects.interrupt || !effects.shutdown)
        throw std::invalid_argument{"Missing node lifecycle effect port"};
    m_owner = execution::RetainSnapshot<State>(m_execution.scheduler, [&] {
        return std::make_unique<State>(m_execution, std::move(effects), std::move(publish), m_stopping, m_mailbox);
    });
    m_state = m_owner;
    if (m_owner) m_owner->owner = m_owner;
    else { Projection failure; failure.phase = Phase::Fatal; failure.event = Event::Fatal; failure.publication_failed = true; m_mailbox->record(std::move(failure)); }
}
Driver::~Driver() { close(); }
bool Driver::initialize()
{
    const auto state = m_state.lock();
    if (!state || m_stopping->load()) return false;
    const auto action = m_execution.scheduler->openActionScope();
    return action && state->post(action, [action](State& state) { state.begin(Operation::Initialize, action); },
        [action](State& state, std::exception_ptr error) { state.rejectedBeforeStart(error, action); });
}
bool Driver::interrupt()
{
    m_stopping->store(true);
    const auto state = m_state.lock();
    if (!state) return false;
    const auto action = state->scope.load();
    return state->postControl(action, [action](State& state) { state.begin(Operation::Interrupt, action); });
}
bool Driver::shutdown()
{
    m_stopping->store(true);
    const auto state = m_state.lock();
    if (!state) return false;
    const auto action = state->scope.load();
    return state->postControl(action, [action](State& state) { state.requestShutdown(action); });
}
Projection Driver::projection() const
{
    auto value = m_mailbox->read();
    if (m_stopping->load() && value.phase == Phase::Ready) value.phase = Phase::Stopping;
    return value;
}
bool Driver::retryPublish()
{
    const auto state = m_state.lock();
    return state && state->post(state->scope.load(), [](State& state) { state.retryNotification(); });
}
void Driver::close()
{
    m_stopping->store(true); m_mailbox->close();
    if (const auto state = m_state.lock()) {
        const auto action = state->scope.exchange({});
        state->post(action, [](State& state) {
            state.flow.stop(); state.notify(Event::State, {}, {}, false);
            // An already accepted shutdown retains its original action across
            // observer close. No new public ingress can reach this owner.
            state.maybeShutdown();
        });
    }
    m_owner.reset(); m_state.reset();
}
} // namespace nodelifecycle
