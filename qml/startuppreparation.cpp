// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/startuppreparation.h>
#include <qml/executor.h>

#include <atomic>
#include <cassert>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace startup::preparation {
namespace {
execution::SubmitResult Rejected(execution::Rejection reason) { return {{}, reason}; }
bool Cancelled(const std::exception_ptr& error) { return execution::NotStartedCause(error) == execution::NotStarted::Cause::Cancelled; }
QString Error(std::exception_ptr error, const QString& fallback)
{
    try { if (error) std::rethrow_exception(error); }
    catch (const std::exception& exception) {
        const char* message = exception.what();
        if (!message) return fallback;
        qsizetype length{0};
        while (length < 16384 && message[length] != '\0') ++length;
        return QString::fromUtf8(message, length);
    }
    catch (...) {}
    return fallback;
}
}
struct Coordinator::Publication {
    mutable std::mutex mutex;
    std::shared_ptr<const Result> result;
    std::atomic<Phase> phase{Phase::Idle};
};
struct Coordinator::State {
    struct Queues : std::enable_shared_from_this<Queues> {
        explicit Queues(execution::Executor& executor)
            : core(executor.makeQueue(execution::Lane::Core)),
              utility(std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility))) {}

        ~Queues()
        {
            core.close();
            core.whenDrained([utility = utility] { utility->close(); });
        }

        bool post(Done command)
        {
            return utility->post(serialize(std::move(command)));
        }

        void deliver(Done command)
        {
            const auto delivery = serialize(std::move(command));
            if (!utility->post(delivery)) delivery();
        }

        template <typename T>
        std::shared_ptr<T> retain(std::unique_ptr<T> value)
        {
            return {value.release(), [queues = shared_from_this()](T* value) mutable {
                auto owner = std::exchange(queues, {});
                owner->deliver([value] { delete value; });
            }};
        }

        execution::SerialQueue core;
        const std::shared_ptr<execution::SerialQueue> utility;

    private:
        Done serialize(Done command)
        {
            return [self = shared_from_this(), command = std::move(command)] {
                std::lock_guard lock{self->mutex};
                command();
            };
        }

        std::recursive_mutex mutex;
    };
    // A consumed or rejected reply releases its original owner, even when
    // the transport retains a copy of the callback.
    struct Reply {
        explicit Reply(std::shared_ptr<State> original) : original(std::move(original)) {}
        std::shared_ptr<State> take()
        {
            std::lock_guard lock{mutex};
            return std::exchange(original, {});
        }

    private:
        std::mutex mutex;
        std::shared_ptr<State> original;
    };
    struct Prepared { bool started{false}; std::optional<startup::PreparationOutcome> outcome; };
    const std::shared_ptr<Queues> queues;
    const Ports ports;
    const std::shared_ptr<Publication> publication;
    const std::shared_ptr<Result> first, stopped;
    std::weak_ptr<State> owner;
    std::atomic_bool started{false}, closing{false}, native_entered{false};
    struct Input { Context context; CompleteResult completed; };
    std::mutex ingress;
    std::optional<Input> input;
    bool admission_pending{false}, admission_accepted{false};
    Result value;
    CompleteResult completed;
    bool decided{false}, published{false}, stopping_published{false}, retiring{false}, endpoint_retired{false}, preparing{false};

    State(Ports ports, std::shared_ptr<Publication> publication, std::shared_ptr<Result> first,
          std::shared_ptr<Result> stopped, std::shared_ptr<Queues> queues)
        : queues(std::move(queues)), ports(std::move(ports)), publication(std::move(publication)),
          first(std::move(first)), stopped(std::move(stopped)) {}

    void deliver(Done command) { queues->deliver(std::move(command)); }

    execution::SubmitResult post(Done command)
    {
        return {{}, queues->post(std::move(command)) ? execution::Rejection::None : execution::Rejection::Transport};
    }

    template <typename Work, typename Complete>
    bool submitCore(Work work, Complete complete, std::function<void(std::exception_ptr)> failed)
    {
        auto task = execution::MakeTask(std::move(work), std::move(complete), std::move(failed));
        return queues->core.post([queues = queues, task = std::move(task)]() mutable {
            task->execute();
            queues->deliver([task = std::move(task)] { task->deliver(); });
        });
    }

    void stage(Phase phase) { publication->phase = phase; }
    bool physicallyRetired()
    {
        try { return endpoint_retired || ports.transport_retired(); } catch (...) { return false; }
    }
    void publishTerminal()
    {
        if (!decided) return;
        value.closing = closing;
        value.endpoint_retired = endpoint_retired;
        if (published && (!value.closing || stopping_published || !endpoint_retired)) return;
        const auto slot = published ? stopped : first;
        *slot = value;
        {
            std::lock_guard lock{publication->mutex};
            publication->result = slot;
        }
        const bool notify = !published;
        published = true;
        stopping_published = value.closing;
        stage(Phase::Completed);
        if (notify) {
            auto callback = std::exchange(completed, {});
            try { if (callback) callback(slot); } catch (...) {} // The exact result remains queryable.
        }
    }
    void retired()
    {
        retiring = false;
        if (!physicallyRetired()) { stage(Phase::RetirementBlocked); return; }
        endpoint_retired = true;
        if (decided) publishTerminal();
    }
    void retire()
    {
        if (preparing) return;
        if (physicallyRetired()) {
            endpoint_retired = true;
            if (decided) publishTerminal();
            return;
        }
        if (retiring) return;
        retiring = true;
        stage(Phase::Retiring);
        const auto reply = std::make_shared<Reply>(owner.lock());
        try {
            const auto submitted = ports.retire_transport([reply] {
                if (const auto self = reply->take()) self->deliver([self] { self->retired(); });
            });
            if (submitted) return;
        } catch (...) {}
        if (!reply->take()) return; // The original physical receipt owns the outcome.
        // The transport can finish stopping between the first check and
        // registering the callback. Its physical retirement is sufficient.
        if (physicallyRetired()) {
            retired();
            return;
        }
        retiring = false;
        stage(Phase::RetirementBlocked);
    }
    void settle(Disposition disposition, QString error = {})
    {
        if (decided) return;
        decided = true;
        value.disposition = disposition;
        value.error = error.left(16384);
        if (disposition == Disposition::Ready && !closing) publishTerminal();
        else retire();
    }
    void cancel(QString error)
    {
        if (decided) return;
        value.cancelled = true;
        settle(Disposition::Failed, std::move(error));
    }
    void failedBeforeNative(std::exception_ptr error, const QString& fallback)
    {
        if (decided) return;
        value.cancelled = closing && !native_entered && Cancelled(error)
            && (!value.saved_profile || (value.saved_profile->completed && !value.saved_profile->uncertain));
        settle(Disposition::Failed, Error(error, fallback));
    }
    void prepared(Prepared receipt)
    {
        preparing = false;
        if (decided) return;
        value.native_started = receipt.started;
        value.native = receipt.outcome;
        value.cancelled = !receipt.started; // Only the closing fence returns an unstarted receipt.
        if (receipt.outcome == startup::PreparationOutcome::Ready) settle(Disposition::Ready);
        else settle(Disposition::Failed, receipt.started ? QStringLiteral("Node preparation failed.")
            : QStringLiteral("Startup was closed before node preparation started."));
    }
    void prepare()
    {
        if (closing) { cancel(QStringLiteral("Startup was closed before node preparation started.")); return; }
        preparing = true;
        stage(Phase::Preparing);
        const auto self = owner.lock();
        const auto work = [self, profile = *value.profile] {
            Prepared receipt;
            if (self->closing) return receipt;
            receipt.started = true;
            self->native_entered = true;
            receipt.outcome = self->ports.prepare_native(profile);
            return receipt;
        };
        const auto failed = [self](std::exception_ptr error) {
            self->preparing = false;
            if (self->decided) return;
            self->value.native_started = self->native_entered;
            self->value.cancelled = self->closing && !self->value.native_started && Cancelled(error);
            self->settle(self->value.native_started ? Disposition::Uncertain : Disposition::Failed,
                Error(error, QStringLiteral("The node preparation result is not available.")));
        };
        if (!submitCore(work, [self](Prepared result) { self->prepared(std::move(result)); }, failed)) {
            failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
        }
    }
    void transported(transport::StartResult receipt)
    {
        if (value.transport || decided) return;
        value.transport = std::move(receipt);
        switch (value.transport->status) {
        case transport::StartStatus::Primary: prepare(); break;
        case transport::StartStatus::Forwarded: settle(Disposition::Forwarded, value.transport->error); break;
        case transport::StartStatus::Rejected: settle(Disposition::Failed, value.transport->error); break;
        case transport::StartStatus::Unknown: settle(Disposition::Uncertain, value.transport->error); break;
        }
    }
    void read(Profile profile)
    {
        if (decided) return;
        value.profile = std::move(profile);
        if (value.profile->directory.isEmpty() || value.profile->directory.size() > 32768
            || value.profile->network.isEmpty() || value.profile->network.size() > 64) {
            settle(Disposition::Failed, QStringLiteral("The resolved startup profile is invalid or too large.")); return;
        }
        if (closing) { cancel(QStringLiteral("Startup was closed before the request was sent.")); return; }
        stage(Phase::StartingTransport);
        const auto reply = std::make_shared<Reply>(owner.lock());
        try {
            const auto submitted = ports.start_transport(*value.profile, [reply](transport::StartResult result) {
                if (const auto self = reply->take()) {
                    self->deliver([self, result = std::move(result)]() mutable { self->transported(std::move(result)); });
                }
            });
            if (submitted || !reply->take()) return;
            settle(Disposition::Failed, QStringLiteral("The request could not be sent to the running app."));
        } catch (...) {
            if (!reply->take()) return;
            settle(Disposition::Uncertain, Error(std::current_exception(), QStringLiteral("It is not known whether the request reached the running app.")));
        }
    }
    void consumeInput()
    {
        // Public ingress can come from the onboarding Domain while a GUI
        // close is in flight. Only this original Domain consumes the Native
        // context and installs its terminal callback.
        {
            std::lock_guard lock{ingress};
            if (input) {
                value.saved_profile = std::move(input->context.saved_profile);
                completed = std::move(input->completed);
                input.reset();
            }
        }
    }
    void begin()
    {
        consumeInput();
        // A later quit cannot hide an earlier partial or uncertain saved write.
        if (value.saved_profile && (!value.saved_profile->completed || value.saved_profile->uncertain)) {
            settle(value.saved_profile->uncertain ? Disposition::Uncertain : Disposition::Failed,
                QStringLiteral("Finish the earlier setup step before starting.")); return;
        }
        if (closing) { cancel(QStringLiteral("Startup was closed before profile inspection.")); return; }
        stage(Phase::Reading);
        const auto self = owner.lock();
        const auto failed = [self](std::exception_ptr error) {
            self->failedBeforeNative(error, QStringLiteral("The startup profile could not be read."));
        };
        if (!submitCore([self] {
                if (self->closing) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                return self->ports.read_profile();
            },
                [self](Profile profile) { self->read(std::move(profile)); }, failed)) {
            failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
        }
    }
    void stop()
    {
        {
            std::lock_guard lock{ingress};
            // The admitted begin turn consumes the original callback before
            // accounting cancellation. A rejected admission has no callback.
            if (input && (admission_pending || admission_accepted)) return;
        }
        if (!started) { cancel(QStringLiteral("Startup was closed.")); return; }
        // Keep the primary namespace until an admitted Native write/base call
        // has returned its factual outcome. Earlier phases can stop immediately.
        if (preparing) return;
        retire();
    }
};

namespace {
void CheckPorts(const Ports& ports)
{
    if (!ports.read_profile || !ports.start_transport || !ports.prepare_native
        || !ports.retire_transport || !ports.transport_retired) throw std::invalid_argument{"Missing startup preparation port"};
}
} // namespace

Coordinator::Coordinator(execution::Executor& executor, Ports ports)
    : m_publication(std::make_shared<Publication>())
{
    CheckPorts(ports);
    const auto queues = std::make_shared<State::Queues>(executor);
    auto first = queues->retain(std::make_unique<Result>());
    auto stopped = queues->retain(std::make_unique<Result>());
    m_owner = queues->retain(std::make_unique<State>(std::move(ports), m_publication,
                                                     std::move(first), std::move(stopped), queues));
    m_owner->owner = m_owner;
}

Coordinator::~Coordinator() = default; // Observer loss does not cancel admitted work or a transferred endpoint.
execution::SubmitResult Coordinator::start(Context context, CompleteResult completed)
{
    const auto state = m_owner;
    if (!state) return Rejected(execution::Rejection::Closed);
    {
        std::lock_guard lock{state->ingress};
        if (state->closing) return Rejected(execution::Rejection::Closed);
        if (state->started.exchange(true)) return Rejected(execution::Rejection::Invalid);
        // Rejected scheduling still leaves this input in Native-retained
        // State. Caller-side rejection never destroys its recovery backing.
        state->input.emplace(State::Input{std::move(context), std::move(completed)});
        state->admission_pending = true;
    }
    const auto admitted = state->post([state] { state->begin(); });
    bool close_requested{false};
    {
        std::lock_guard lock{state->ingress};
        state->admission_pending = false;
        state->admission_accepted = bool(admitted);
        close_requested = state->closing;
    }
    if (close_requested && !postClose(false)) m_publication->phase = Phase::RetirementBlocked;
    return admitted;
}
execution::SubmitResult Coordinator::postClose(bool retry)
{
    const auto state = m_owner;
    if (!state) return Rejected(execution::Rejection::Closed);
    return state->post([state, retry] { if (retry) state->retire(); else state->stop(); });
}
void Coordinator::close()
{
    if (!m_owner) return;
    {
        std::lock_guard lock{m_owner->ingress};
        if (m_owner->closing.exchange(true)) return;
        if (m_owner->admission_pending) return; // start() posts the deferred close after admission.
    }
    if (!postClose(false)) m_publication->phase = Phase::RetirementBlocked;
}
execution::SubmitResult Coordinator::retryRetirement()
{
    if (!m_owner || phase() != Phase::RetirementBlocked) return Rejected(execution::Rejection::Invalid);
    return postClose(true);
}
void Coordinator::whenDrained(Done done)
{
    assert(m_owner && m_owner->queues);
    m_owner->queues->utility->whenDrained(std::move(done));
}
std::shared_ptr<const Result> Coordinator::result() const
{
    std::lock_guard lock{m_publication->mutex}; return m_publication->result;
}
Phase Coordinator::phase() const { return m_publication->phase; }
} // namespace startup::preparation
