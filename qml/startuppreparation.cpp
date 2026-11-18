// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/startuppreparation.h>
#include <qml/atomicsharedptr.h>
#include <qml/retainedsnapshot.h>

#include <atomic>
#include <exception>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace startup::preparation {
namespace {
execution::Admission Rejected(execution::Rejection reason) { return {{}, reason}; }
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
    // A consumed/rejected reply relinquishes the original owner and scope.
    // Retained duplicate callbacks therefore hold no executor capability.
    struct Reply {
        std::shared_ptr<State> original;
        std::shared_ptr<execution::Scheduler> scope;
        std::shared_ptr<State> take() { scope.reset(); return std::exchange(original, {}); }
    };
    struct Prepared { bool started{false}; std::optional<startup::PreparationOutcome> outcome; };
    const execution::SnapshotServices services;
    const Ports ports;
    const std::shared_ptr<Publication> publication;
    const std::shared_ptr<Result> first, stopped;
    std::weak_ptr<State> owner;
    ownership::AtomicSharedPtr<execution::Scheduler> action;
    const execution::ResourceKey domain, native;
    std::atomic_bool started{false}, closing{false}, native_entered{false};
    struct Input { Context context; CompleteResult completed; };
    std::mutex ingress;
    std::optional<Input> input;
    bool admission_pending{false}, admission_accepted{false};
    Result value;
    CompleteResult completed;
    bool decided{false}, published{false}, stopping_published{false}, retiring{false}, endpoint_retired{false}, preparing{false};

    State(execution::SnapshotServices services, Ports ports, std::shared_ptr<Publication> publication,
          std::shared_ptr<Result> first, std::shared_ptr<Result> stopped)
        : services(std::move(services)), ports(std::move(ports)), publication(std::move(publication)),
          first(std::move(first)), stopped(std::move(stopped)),
          domain(this->services.scheduler->allocateKey()), native(this->services.scheduler->allocateKey()) {}

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
        action.store({});
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
        const auto reply = std::make_shared<Reply>(Reply{owner.lock(), action.load()});
        try {
            const auto admission = ports.retire_transport([reply] {
                if (const auto self = reply->take()) self->retired();
            });
            if (!reply->original) return; // An inline physical receipt already owns the outcome.
            if (admission) return;
        } catch (...) {}
        reply->take();
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
        else settle(Disposition::Failed, receipt.started ? QStringLiteral("Native node preparation failed.")
            : QStringLiteral("Startup was closed before Native preparation started."));
    }
    void prepare()
    {
        if (closing) { cancel(QStringLiteral("Startup was closed before Native preparation.")); return; }
        preparing = true;
        stage(Phase::Preparing);
        const auto self = owner.lock();
        const auto scope = action.load();
        const auto work = [self] {
            Prepared receipt;
            if (self->closing) return receipt;
            receipt.started = true;
            self->native_entered = true;
            receipt.outcome = self->ports.prepare_native(*self->value.profile);
            return receipt;
        };
        const auto failed = [self](std::exception_ptr error) {
            self->preparing = false;
            if (self->decided) return;
            self->value.native_started = self->native_entered;
            self->value.cancelled = self->closing && !self->value.native_started && Cancelled(error);
            self->settle(self->value.native_started ? Disposition::Uncertain : Disposition::Failed,
                Error(error, QStringLiteral("The original Native preparation outcome is unavailable.")));
        };
        if (!scope || !execution::Submit(*scope, work, [self](Prepared result) { self->prepared(std::move(result)); }, failed,
                {execution::Lane::Core, native, execution::WorkClass::Action})) {
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
        if (closing) { cancel(QStringLiteral("Startup was closed before transport admission.")); return; }
        stage(Phase::StartingTransport);
        const auto reply = std::make_shared<Reply>(Reply{owner.lock(), action.load()});
        try {
            const auto admission = ports.start_transport(*value.profile, [reply](transport::StartResult result) {
                if (const auto self = reply->take()) self->transported(std::move(result));
            });
            if (!reply->original) return;
            if (admission) return;
            reply->take();
            settle(Disposition::Failed, QStringLiteral("Desktop transport start was not admitted."));
        } catch (...) {
            if (!reply->take()) return;
            settle(Disposition::Uncertain, Error(std::current_exception(), QStringLiteral("Desktop transport admission is unknown.")));
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
                QStringLiteral("Resolve the original saved-profile handoff before startup.")); return;
        }
        if (closing) { cancel(QStringLiteral("Startup was closed before profile inspection.")); return; }
        stage(Phase::Reading);
        const auto self = owner.lock();
        const auto scope = action.load();
        const auto failed = [self](std::exception_ptr error) {
            self->failedBeforeNative(error, QStringLiteral("The startup profile could not be read."));
        };
        if (!scope || !execution::Submit(*scope, [self] {
                if (self->closing) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                return self->ports.read_profile();
            },
                [self](Profile profile) { self->read(std::move(profile)); }, failed,
                {execution::Lane::Core, native, execution::WorkClass::Observation})) {
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

Coordinator::Coordinator(execution::SnapshotServices services, Ports ports)
    : m_services(std::move(services)), m_publication(std::make_shared<Publication>())
{
    if (!m_services.scheduler || !ports.read_profile || !ports.start_transport || !ports.prepare_native
        || !ports.retire_transport || !ports.transport_retired) throw std::invalid_argument{"Missing startup preparation port"};
    const auto first = execution::RetainSnapshot<Result>(m_services.scheduler, [] { return std::make_unique<Result>(); });
    const auto stopped = execution::RetainSnapshot<Result>(m_services.scheduler, [] { return std::make_unique<Result>(); });
    if (!first || !stopped) return;
    m_owner = execution::RetainSnapshot<State>(m_services.scheduler, [&] {
        return std::make_unique<State>(m_services, std::move(ports), m_publication, first, stopped);
    });
    if (m_owner) m_owner->owner = m_owner;
}
Coordinator::~Coordinator() = default; // Observer loss does not cancel admitted work or a transferred endpoint.
execution::Admission Coordinator::start(Context context, CompleteResult completed)
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
    const auto action = m_services.scheduler->openActionScope();
    auto admitted = Rejected(execution::Rejection::Closed);
    if (action) {
        state->action.store(action);
        admitted = execution::Submit(*action, [state] { state->begin(); }, [] {},
            [state](std::exception_ptr error) { state->consumeInput(); state->failedBeforeNative(error, QStringLiteral("Startup did not start.")); },
            {execution::Lane::Domain, state->domain, execution::WorkClass::Action});
    }
    bool close_requested{false};
    {
        std::lock_guard lock{state->ingress};
        state->admission_pending = false;
        state->admission_accepted = bool(admitted);
        close_requested = state->closing;
    }
    if (!admitted) state->action.store({});
    if (close_requested && !postClose(false)) m_publication->phase = Phase::RetirementBlocked;
    return admitted;
}
execution::Admission Coordinator::postClose(bool retry)
{
    const auto state = m_owner;
    if (!state) return Rejected(execution::Rejection::Closed);
    const auto decision = [state, retry] { if (retry) state->retire(); else state->stop(); };
    if (const auto action = state->action.load()) {
        return execution::Submit(*action, decision, [] {},
            [state](std::exception_ptr) { state->stage(Phase::RetirementBlocked); },
            {execution::Lane::Domain, state->domain, execution::WorkClass::Action});
    }
    // Stopping an already published Ready origin is a distinct bounded control
    // request. It needs no idle lifetime scope and remains independent of the
    // closed ordinary lane; its outcome performs the decision on Domain.
    return execution::Submit(*m_services.scheduler, [] {}, decision,
        [state](std::exception_ptr) { state->stage(Phase::RetirementBlocked); },
        {execution::Lane::Control, state->domain, execution::WorkClass::Action});
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
execution::Admission Coordinator::retryRetirement()
{
    if (!m_owner || phase() != Phase::RetirementBlocked) return Rejected(execution::Rejection::Invalid);
    return postClose(true);
}
std::shared_ptr<const Result> Coordinator::result() const
{
    std::lock_guard lock{m_publication->mutex}; return m_publication->result;
}
Phase Coordinator::phase() const { return m_publication->phase; }
} // namespace startup::preparation
