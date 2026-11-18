// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/nodelifecycleworker.h>

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
            // Existing projection copies keep the original diagnostic.
            // The closed mailbox retains the effect outcomes.
            next.diagnostic.reset();
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
        value.diagnostic.reset();
        if (value.phase != Phase::Fatal && value.phase != Phase::Stopped) {
            if (value.phase != Phase::Draining) value.phase = Phase::Stopping;
            value.event = Event::State;
        }
        ++revision;
    }
};

struct Driver::State : std::enable_shared_from_this<State> {
    State(Post native, Post priority, Effects effects, Publish publish,
          std::shared_ptr<std::atomic_bool> stopping, std::shared_ptr<Mailbox> mailbox)
        : native(std::move(native)), priority(std::move(priority)), effects(std::move(effects)),
          publish(std::move(publish)), stopping(std::move(stopping)), mailbox(std::move(mailbox)) {}

    ~State() { if (effects.retire) effects.retire(); }

    const Post native;
    const Post priority;
    const Effects effects;
    const Publish publish;
    const std::shared_ptr<std::atomic_bool> stopping;
    const std::shared_ptr<Mailbox> mailbox;
    std::mutex mutex;
    Flow flow;
    std::optional<NativeInitialization> initialization;
    bool initialization_pending{false}, shutdown_requested{false};

    void notify(Event event = Event::State, std::shared_ptr<const QString> diagnostic = {}, bool deliver = true)
    {
        Projection value;
        if (flow.phase() == Phase::Fatal && event != Event::Fatal) {
            // A late initialization receipt must preserve the original fatal text.
            value = mailbox->read();
            value.event = Event::Fatal;
        } else {
            value.event = event;
            value.diagnostic = std::move(diagnostic);
        }
        value.phase = flow.phase();
        value.initialization = initialization;
        value.core_exit_status = flow.coreExitStatus();
        mailbox->record(std::move(value));
        if (deliver) mailbox->deliver(publish, stopping->load());
    }

    bool post(const Post& queue, std::function<void(State&)> command,
              std::function<void(State&, std::exception_ptr)> failure = {})
    {
        const auto self = shared_from_this();
        return queue(execution::MakeTask([self, command = std::move(command)] { command(*self); }, [] {},
            [self, failure = std::move(failure)](std::exception_ptr error) {
                if (failure) failure(*self, error);
            }));
    }

    bool terminal() const { return flow.phase() == Phase::Fatal || flow.phase() == Phase::Stopped; }

    bool initialize()
    {
        uint64_t attempt;
        {
            std::lock_guard lock{mutex};
            const auto accepted = flow.start();
            if (!accepted) return false;
            attempt = *accepted;
            initialization_pending = true;
            notify();
        }
        if (post(native, [attempt](State& state) {
                if (!state.dispatch(Operation::Initialize, attempt)) {
                    state.rejected(Operation::Initialize, attempt,
                        std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
                }
            },
                [attempt](State& state, std::exception_ptr error) {
                    state.rejected(Operation::Initialize, attempt, error);
                })) return true;
        rejected(Operation::Initialize, attempt, std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
        return false;
    }

    void begin(Operation operation)
    {
        uint64_t attempt;
        {
            std::lock_guard lock{mutex};
            if (terminal()) return;
            attempt = flow.attempt();
            if (operation == Operation::Interrupt) {
                if (!flow.beginInterrupt()) return;
            } else {
                if (!flow.beginShutdown()) return;
            }
            notify();
        }
        if (dispatch(operation, attempt)) return;
        std::lock_guard lock{mutex};
        fatal(std::make_shared<const QString>(QStringLiteral("The node action may not have finished.")));
    }

    void requestShutdown()
    {
        bool interrupted;
        {
            std::lock_guard lock{mutex};
            if (terminal()) return;
            shutdown_requested = true;
            flow.stop();
            interrupted = flow.phase() == Phase::Draining;
        }
        if (interrupted) maybeShutdown();
        else begin(Operation::Interrupt);
    }

    void maybeShutdown()
    {
        {
            std::lock_guard lock{mutex};
            if (terminal() || !shutdown_requested || initialization_pending) return;
        }
        begin(Operation::Shutdown);
    }

    bool dispatch(Operation operation, uint64_t attempt)
    {
        const auto self = shared_from_this();
        const auto diagnostic = std::make_shared<QString>();
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
                try {
                    *diagnostic = self->effects.describe
                        ? self->effects.describe(std::current_exception())
                        : QStringLiteral("The node lifecycle effect failed.");
                } catch (...) {
                    *diagnostic = QStringLiteral("The node lifecycle diagnostic could not be prepared.");
                }
            }
            return result;
        };
        const auto done = [self, operation, attempt, diagnostic](Result result) {
            self->completed(operation, attempt, std::move(result), diagnostic);
        };
        const auto failed = [self, operation, attempt](std::exception_ptr error) {
            self->rejected(operation, attempt, error);
        };
        const auto& queue = operation == Operation::Interrupt ? priority : native;
        return queue(execution::MakeTask(work, done, failed));
    }

    void fatal(const std::shared_ptr<const QString>& diagnostic)
    {
        if (flow.fatal(*diagnostic)) notify(Event::Fatal, diagnostic);
    }

    void completed(Operation operation, uint64_t attempt, Result result, const std::shared_ptr<QString>& diagnostic)
    {
        {
            std::lock_guard lock{mutex};
            if (operation == Operation::Initialize) initialization_pending = false;
            if (result.fatal) {
                fatal(diagnostic);
                return;
            }
            switch (operation) {
            case Operation::Initialize:
                // The raw successful native outcome remains readable across a stop.
                if (stopping->load()) flow.stop();
                initialization = result.initialization;
                if (flow.initialized({attempt, result.initialization.attempted, result.initialization.success, result.initialization.cancelled})) {
                    notify(Event::Initialized, {}, flow.phase() != Phase::Fatal);
                } else {
                    fatal(std::make_shared<const QString>(QStringLiteral("The saved startup record is not valid.")));
                }
                break;
            case Operation::Interrupt:
                if (flow.interrupted()) notify(Event::Interrupted);
                break;
            case Operation::Shutdown:
                if (flow.coreStopped(result.exit_status) && flow.drained()) notify(Event::Shutdown);
                break;
            }
        }
        maybeShutdown();
    }

    void rejected(Operation operation, uint64_t attempt, std::exception_ptr error)
    {
        {
            std::lock_guard lock{mutex};
            if (operation == Operation::Initialize) {
                initialization_pending = false;
                if (const auto cause = execution::NotStartedCause(error)) {
                    if (stopping->load()) flow.stop();
                    const bool cancelled = *cause == execution::NotStarted::Cause::Cancelled;
                    if (flow.initialized({attempt, false, false, cancelled})) {
                        initialization = NativeInitialization{.cancelled = cancelled};
                        notify(Event::Initialized, {}, flow.phase() != Phase::Fatal);
                    }
                } else {
                    fatal(std::make_shared<const QString>(QStringLiteral("The node action may not have finished.")));
                }
            } else {
                fatal(std::make_shared<const QString>(QStringLiteral("The node action may not have finished.")));
            }
        }
        maybeShutdown();
    }

    void retryNotification()
    {
        std::lock_guard lock{mutex};
        mailbox->deliver(publish, stopping->load());
    }
};

Driver::Driver(Post native, Post priority, Effects effects, Publish publish)
    : m_stopping(std::make_shared<std::atomic_bool>(false)), m_mailbox(std::make_shared<Mailbox>())
{
    if (!native || !priority || !effects.initialize || !effects.interrupt || !effects.shutdown)
        throw std::invalid_argument{"Missing node lifecycle effect port"};
    m_state = std::make_shared<State>(std::move(native), std::move(priority), std::move(effects),
                                      std::move(publish), m_stopping, m_mailbox);
}
Driver::~Driver() { close(); }
bool Driver::initialize()
{
    const auto state = m_state;
    return state && !m_stopping->load() && state->initialize();
}
bool Driver::interrupt()
{
    m_stopping->store(true);
    const auto state = m_state;
    return state && state->post(state->priority, [](State& state) { state.begin(Operation::Interrupt); },
        [](State& state, std::exception_ptr error) { state.rejected(Operation::Interrupt, 0, error); });
}
bool Driver::shutdown()
{
    m_stopping->store(true);
    const auto state = m_state;
    return state && state->post(state->priority, [](State& state) { state.requestShutdown(); },
        [](State& state, std::exception_ptr error) { state.rejected(Operation::Shutdown, 0, error); });
}
Projection Driver::projection() const
{
    auto value = m_mailbox->read();
    if (m_stopping->load() && value.phase == Phase::Ready) value.phase = Phase::Stopping;
    return value;
}
bool Driver::retryPublish()
{
    const auto state = m_state;
    return state && state->post(state->native, [](State& state) { state.retryNotification(); });
}
void Driver::close()
{
    m_stopping->store(true);
    m_mailbox->close();
    if (const auto state = std::exchange(m_state, {})) {
        std::lock_guard lock{state->mutex};
        if (state->flow.phase() != Phase::Constructed) state->flow.stop();
    }
}
} // namespace nodelifecycle
