// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/onboardingflow.h>
#include <qml/atomicsharedptr.h>
#include <qml/retainedsnapshot.h>

#include <limits>
#include <cassert>
#include <exception>
#include <mutex>
#include <utility>

namespace onboarding {
void Flow::edit(uint64_t revision)
{
    if (revision <= m_revision || pending() || m_closing || m_preparation_admitted || m_result.replacement_written || m_result.uncertain
        || m_phase == Phase::Completed || m_phase == Phase::PreparingNode) return;
    m_revision = revision;
    m_phase = Phase::Checking;
    m_error.clear();
    m_attempt.reset();
}
bool Flow::checked(Checks checks)
{
    if (checks.revision != m_revision || m_phase != Phase::Checking || pending() || m_closing) return false;
    m_error = checks.error.left(16384);
    if (!m_error.isEmpty()) m_phase = Phase::Failed;
    else if (checks.preview && checks.storage && checks.effective) m_phase = Phase::Ready;
    return true;
}
std::optional<Attempt> Flow::finish(uint64_t revision, QmlOnboardingSettings::ApplyRequest request)
{
    if (!canFinish() || revision != m_revision || m_next_attempt == std::numeric_limits<uint64_t>::max()) return {};
    if (request.data_dir.path.size() > 32768 || request.values.proxy_address.size() > 4096
        || request.values.tor_address.size() > 4096 || request.touched_settings.size() > 6) {
        m_phase = Phase::Failed;
        m_error = QStringLiteral("The setup input exceeds its supported size. Shorten the input and retry the checks.");
        return {};
    }
    m_attempt = Attempt{++m_next_attempt, revision, std::move(request)};
    m_result = {};
    m_error.clear();
    m_pending = true;
    m_phase = Phase::Persisting;
    return m_attempt;
}
bool Flow::account(uint64_t attempt, QmlOnboardingSettings::ProfileResult result)
{
    if (!m_attempt || m_attempt->id != attempt) return false;
    m_pending = false;
    m_query_pending = false;
    m_result = std::move(result);
    m_error = m_result.error.left(16384);
    if (m_closing) m_phase = m_result.uncertain ? Phase::Closing : Phase::Closed;
    else if (m_result.uncertain) m_phase = Phase::Reconciling;
    else if (m_result.completed) m_phase = Phase::PreparingNode;
    else m_phase = Phase::Failed;
    return true;
}
bool Flow::persisted(uint64_t attempt, QmlOnboardingSettings::ProfileResult result)
{
    if (!m_pending || m_phase != (m_closing ? Phase::Closing : Phase::Persisting)) return false;
    return account(attempt, std::move(result));
}
bool Flow::beginQuery()
{
    if (!m_attempt || pending() || (m_phase != Phase::Reconciling && m_phase != Phase::Closing)) return false;
    m_query_pending = true;
    return true;
}
bool Flow::reconciled(uint64_t attempt, QmlOnboardingSettings::ProfileResult result)
{
    if (!m_query_pending) return false;
    return account(attempt, std::move(result));
}
bool Flow::beginCompletion()
{
    if (!m_attempt || pending() || m_closing || m_phase != Phase::Failed || m_preparation_admitted || m_result.completed || !m_result.replacement_written || m_result.uncertain) return false;
    m_pending = true;
    m_phase = Phase::Persisting;
    return true;
}
bool Flow::beginPreparation()
{
    if (m_closing || pending() || m_phase != Phase::PreparingNode || !m_result.completed || m_preparation_admitted) return false;
    m_preparation_admitted = true;
    m_pending = true;
    return true;
}
void Flow::prepared(bool success, bool unknown, QString error)
{
    prepared(unknown ? startup::preparation::Disposition::Uncertain
        : success ? startup::preparation::Disposition::Ready : startup::preparation::Disposition::Failed, std::move(error));
}
void Flow::prepared(startup::preparation::Disposition disposition, QString error)
{
    if (!m_preparation_admitted || !m_pending) return;
    m_pending = false;
    const bool unknown = disposition == startup::preparation::Disposition::Uncertain;
    m_preparation_unknown = unknown;
    m_error = error.left(16384);
    if (m_closing) m_phase = unknown ? Phase::Closing : Phase::Closed;
    else m_phase = unknown ? Phase::Reconciling
        : disposition == startup::preparation::Disposition::Ready ? Phase::Completed
        : disposition == startup::preparation::Disposition::Forwarded ? Phase::Forwarded : Phase::Failed;
    // Preparation is never replayed: factories/base initialization may already
    // have executed even on a known failure. Recovery is an explicit restart.
}
void Flow::close()
{
    m_closing = true;
    m_phase = pending() || m_result.uncertain || m_preparation_unknown ? Phase::Closing : Phase::Closed;
}
void Flow::queryUnavailable(QString error)
{
    m_query_pending = false;
    m_error = error.left(16384);
}

struct Coordinator::State {
    struct PreparationReply {
        using Value = std::shared_ptr<const startup::preparation::Result>;
        using Complete = std::function<void(Value, std::exception_ptr)>;
        std::mutex mutex;
        Complete complete;
        Complete take() {
            std::lock_guard lock{mutex};
            return std::exchange(complete, {});
        }
        void accept(Value value) {
            if (auto callback = take()) callback(std::move(value), {});
        }
        void reject(std::exception_ptr error) {
            if (auto callback = take()) callback({}, std::move(error));
        }
    };
    struct Completion {
        std::function<void()> callback;
        ~Completion(){if(callback)callback();}
    };
    // Destroyed last, after every Native port, result, retained input and scope.
    Completion completion;
    std::weak_ptr<State> owner;
    State(execution::SnapshotServices execution, Publish publish, std::function<void()> drained)
        : completion{std::move(drained)}, execution(std::move(execution)), publish(std::move(publish)), scope(this->execution.scheduler->openActionScope()),
          key(this->execution.scheduler->allocateKey()), io(this->execution.scheduler->allocateKey()) {}
    const execution::SnapshotServices execution;
    const Publish publish;
    ownership::AtomicSharedPtr<execution::Scheduler> scope;
    const execution::ResourceKey key, io;
    Flow flow;
    Ports ports;
    std::optional<std::pair<Checks,std::function<QString()>>> next_check;
    bool checking{false}, retired{false};
    bool preparation_complete{false}, preparation_finished{false};
    std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&,bool)> after_write;
    std::optional<AsyncPreparation> after_write_async;
    AsyncPreparation active_preparation;
    std::shared_ptr<const startup::preparation::Result> preparation_result;
    std::optional<startup::preparation::Disposition> preparation_disposition;
    void requestPreparationClose(const AsyncPreparation& preparation) {
        try { if(preparation.close) preparation.close(); }
        catch(...) { flow.queryUnavailable(QStringLiteral("The startup cancellation handoff is unavailable. The original result remains owned; no preparation was repeated.")); }
    }
    void notify() {
        publish({flow.phase(), flow.revision(), flow.attempt() ? flow.attempt()->id : 0,
            flow.pending(), flow.result().replacement_written, flow.result().completed, preparation_complete,
            preparation_finished,flow.preparationUnknown(),flow.result().uncertain,flow.error(),
            preparation_disposition});
    }
    execution::Admission post(std::shared_ptr<execution::Scheduler> action, std::function<void(State&)> command) {
        if (!action) return {{}, execution::Rejection::Closed};
        const auto self = owner.lock();
        return execution::Submit(*action, [self, command = std::move(command)] { command(*self); }, [] {}, {},
            {execution::Lane::Domain, key, execution::WorkClass::Action});
    }
    void check(Checks value, std::function<QString()> validate) {
        if (retired || flow.pending() || flow.result().replacement_written || flow.result().uncertain) return;
        next_check = std::pair{std::move(value), std::move(validate)};
        if (!checking) startCheck();
    }
    void startCheck() {
        if (!next_check || retired) return;
        auto [value, validate] = std::move(*next_check);
        next_check.reset();
        flow.edit(value.revision);
        if (!value.preview || !value.storage || !value.effective || !value.error.isEmpty()) {
            flow.checked(std::move(value));
            notify();
            return;
        }
        checking = true;
        const auto self = owner.lock();
        const auto done = [self, original=value](QString error) {
            self->checking = false;
            auto value=original;
            value.error = std::move(error);
            value.effective = value.error.isEmpty();
            self->flow.checked(std::move(value));
            self->notify();
            // Queue one retained newer demand; synchronous rejection cannot
            // recurse back into the same failed Native dispatch.
            if (self->next_check && !self->retired) {
                if(!self->post(self->scope.load(), [](State& state) { state.startCheck(); })) {
                    auto rejected=std::move(self->next_check->first);self->next_check.reset();
                    self->flow.edit(rejected.revision);
                    rejected.error=QStringLiteral("The latest setup check was not admitted. Retry the checks.");
                    self->flow.checked(std::move(rejected));self->notify();
                }
            }
        };
        const auto failed = [done](std::exception_ptr) { done(QStringLiteral("The selected setup could not be validated. Retry the checks.")); };
        const auto action = scope.load();
        if (!action || !execution::Submit(*action, std::move(validate), done, failed,
                {execution::Lane::Core, io, execution::WorkClass::Observation})) failed({});
        notify();
    }
    void finish(uint64_t revision, QmlOnboardingSettings::ApplyRequest request, Ports effect_ports,
                const std::shared_ptr<execution::Scheduler>& action) {
        if (retired) return;
        const auto attempt = flow.finish(revision, std::move(request));
        if (!attempt) { notify(); return; }
        ports = std::move(effect_ports);
        write(*attempt, action, false);
    }
    void write(const Attempt& attempt, const std::shared_ptr<execution::Scheduler>& action, bool completion) {
        const auto self = owner.lock();
        const auto done = [self, id=attempt.id, action](QmlOnboardingSettings::ProfileResult result) {
            (void)action;
            self->flow.persisted(id, std::move(result));
            if (self->after_write) {
                auto effect=std::exchange(self->after_write,{});
                if(self->flow.phase()==Phase::PreparingNode) self->prepare(std::move(effect),action);
                else self->preparation_finished=true;
            }
            if (self->after_write_async) {
                auto preparation=std::exchange(self->after_write_async,{});
                if(self->flow.phase()==Phase::PreparingNode) self->prepareAsync(std::move(*preparation),action);
                else {
                    self->preparation_finished=true;
                    self->preparation_disposition=self->flow.result().uncertain
                        ? startup::preparation::Disposition::Uncertain : startup::preparation::Disposition::Failed;
                    self->requestPreparationClose(*preparation);
                }
            }
            self->notify();
        };
        const auto failed = [done,previous=flow.result(),completion](std::exception_ptr error) {
            QmlOnboardingSettings::ProfileResult result = completion ? previous : QmlOnboardingSettings::ProfileResult{};
            result.uncertain = !execution::NotStartedCause(error);
            result.error = QStringLiteral("The original setup outcome is unavailable. Query it before retrying.");
            done(std::move(result));
        };
        const auto work = [ports=ports, request=attempt.request, previous=flow.result(), completion] {
            return completion ? ports.complete(previous) : ports.write(request);
        };
        if ((!completion && !ports.write) || (completion && !ports.complete)
            || !execution::Submit(*action, work, done, failed, {execution::Lane::Core, io, execution::WorkClass::Action}))
            failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
        notify();
    }
    void recover(bool completion, const std::shared_ptr<execution::Scheduler>& action) {
        if (completion) {
            if (!flow.beginCompletion()) return;
            write(*flow.attempt(), action, true);
            return;
        }
        if (!flow.beginQuery()) return;
        if (flow.preparationAdmitted()) {
            flow.queryUnavailable(QStringLiteral("The original node preparation cannot be safely repeated. Close and restart to inspect the saved profile."));
            notify();
            return;
        }
        const auto self = owner.lock();
        const auto id = flow.attempt()->id;
        const auto done = [self,id,action](QmlOnboardingSettings::ProfileResult result) {
            (void)action;
            self->flow.reconciled(id, std::move(result));
            self->notify();
        };
        const auto failed = [self](std::exception_ptr) {
            self->flow.queryUnavailable(QStringLiteral("The original handoff could not be queried. No setup write was repeated."));
            self->notify();
        };
        const auto original = flow.result();
        if (!ports.query || !execution::Submit(*action, [query=ports.query,original] { return query(original); }, done, failed,
                {execution::Lane::Core, io, execution::WorkClass::Observation})) failed({});
        notify();
    }
    void prepare(std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&,bool)> effect,
                 const std::shared_ptr<execution::Scheduler>& action) {
        if (!flow.beginPreparation()) { notify(); return; }
        const auto self = owner.lock();
        const auto done = [self,action](std::pair<bool,QString> result) {
            (void)action;
            self->preparation_complete = result.first;
            self->preparation_finished = true;
            self->flow.prepared(result.first, false, std::move(result.second));
            self->notify();
        };
        const auto failed = [self](std::exception_ptr error) {
            bool unknown=true;QString message;
            try {if(error)std::rethrow_exception(error);}
            catch(const execution::NotStarted&){unknown=false;message=QStringLiteral("Node preparation did not start. The saved profile is retained; restart to continue.");}
            catch(const std::exception& e){message=QString::fromUtf8(e.what());}
            catch(...){message=QStringLiteral("Unknown exception preparing the node.");}
            self->preparation_finished=true;
            self->flow.prepared(false,unknown,std::move(message));
            self->notify();
        };
        const auto request = flow.attempt()->request;
        if (!execution::Submit(*action, [effect=std::move(effect),request] { return effect(request,true); }, done, failed,
                {execution::Lane::Core, io, execution::WorkClass::Action})) {
            preparation_finished=true;
            flow.prepared(false, false, QStringLiteral("Node preparation was not admitted. The saved profile is retained; restart to continue."));
        }
        notify();
    }
    void prepareAsync(AsyncPreparation preparation, const std::shared_ptr<execution::Scheduler>& action) {
        // A duplicate command cannot cancel or overwrite the original
        // preparation, including its known terminal outcome.
        if (flow.preparationAdmitted()) { notify(); return; }
        if (!flow.beginPreparation()) {
            preparation_finished=true;
            preparation_disposition=flow.result().uncertain
                ? startup::preparation::Disposition::Uncertain : startup::preparation::Disposition::Failed;
            flow.queryUnavailable(QStringLiteral("Node preparation did not start. The saved profile is retained; restart to continue."));
            requestPreparationClose(preparation);
            notify();return;
        }
        const auto self=owner.lock();
        const auto ticket=std::make_shared<execution::TaskId>();
        auto result=std::make_shared<execution::OwnedCommand<std::shared_ptr<const startup::preparation::Result>>>(action,
            [action,ticket] {
                const bool posted=ticket->value != 0 && action->activateCleanup(*ticket);
                assert(posted);
                if(!posted) std::terminate();
            });
        // The cross-scheduler completion is reserved before the external
        // effect. Cleanup executes a bounded no-op; its delivery runs on this
        // original Domain even after ordinary admission/facade retirement.
        const auto receiver=execution::Submit(*action, [] {}, [self,result,action] {
            (void)action;
            const auto actual=result->result();
            QString error;
            self->preparation_disposition=startup::preparation::Disposition::Uncertain;
            if (actual && actual->value && *actual->value) {
                self->preparation_result=*actual->value;
                self->preparation_disposition=self->preparation_result->disposition;
                error=self->preparation_result->error;
            } else {
                error=QStringLiteral("The original node preparation outcome is unavailable. No preparation was repeated.");
                try { if(actual&&actual->error)std::rethrow_exception(actual->error); }
                catch(const execution::NotStarted&){
                    self->preparation_disposition=startup::preparation::Disposition::Failed;
                    error=QStringLiteral("Node preparation was not admitted. The saved profile is retained; restart to continue.");
                } catch(...) {}
            }
            self->preparation_complete=self->preparation_disposition==startup::preparation::Disposition::Ready;
            self->preparation_finished=true;
            self->flow.prepared(*self->preparation_disposition,std::move(error));
            self->active_preparation={};
            self->notify();
        }, {}, {execution::Lane::Cleanup, execution.scheduler->allocateKey(), execution::WorkClass::Action}, true);
        if (!receiver) {
            preparation_finished=true;
            preparation_disposition=startup::preparation::Disposition::Failed;
            flow.prepared(false,false,QStringLiteral("Node preparation completion could not be reserved. The saved profile is retained."));
            requestPreparationClose(preparation);
            notify();return;
        }
        *ticket=receiver.id;
        active_preparation=std::move(preparation);
        const auto admitted=result->startWith([self](auto done,auto failed) {
            const auto reply=std::make_shared<PreparationReply>();
            reply->complete=[done,failed](auto value,std::exception_ptr error) {
                if(error) failed(std::move(error));
                else done(std::move(value));
            };
            try {
                const auto admission=self->active_preparation.start
                    ? self->active_preparation.start({self->flow.result()},[reply](auto value){reply->accept(std::move(value));})
                    : execution::Admission{{},execution::Rejection::Invalid};
                if (!admission) {
                    self->requestPreparationClose(self->active_preparation);
                    reply->reject(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
                }
                return admission;
            } catch (...) {
                const auto error=std::current_exception();
                self->requestPreparationClose(self->active_preparation);
                reply->reject(error);return execution::Admission{};
            }
        });
        (void)admitted;
        notify();
    }
    void retire() {
        retired=true;next_check.reset();flow.close();
        requestPreparationClose(active_preparation);
        scope.store({});notify();
    }
};
Coordinator::Coordinator(execution::SnapshotServices services, Publish publish, std::function<void()> drained):m_services(std::move(services))
{
    m_owner = execution::RetainSnapshot<State>(m_services.scheduler, [&] {
        return std::make_unique<State>(m_services, std::move(publish), std::move(drained));
    });
    if (m_owner) {m_state=m_owner;m_owner->owner=m_owner;}
    else {
        Projection rejected;rejected.phase=Phase::Failed;rejected.error=QStringLiteral("Setup could not reserve its background recovery owner. No setup effect was admitted.");
        publish(std::move(rejected));
        if(drained)drained();
    }
}
Coordinator::~Coordinator() { close(); }
bool Coordinator::check(Checks checks, std::function<QString()> validate)
{
    if (const auto state=m_state.lock()) return bool(state->post(m_services.scheduler,
        [checks=std::move(checks),validate=std::move(validate)](State& state) mutable { state.check(std::move(checks),std::move(validate)); }));
    return false;
}
bool Coordinator::finish(uint64_t revision, QmlOnboardingSettings::ApplyRequest request, Ports ports)
{
    const auto state=m_state.lock();
    const auto action=m_services.scheduler->openActionScope();
    return state && action && state->post(action,[revision,request=std::move(request),ports=std::move(ports),action](State& state) mutable {
        state.finish(revision,std::move(request),std::move(ports),action);
    });
}
bool Coordinator::finishAndPrepare(uint64_t revision,QmlOnboardingSettings::ApplyRequest request,Ports ports,
    std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&,bool)> effect)
{
    const auto state=m_state.lock();const auto action=m_services.scheduler->openActionScope();
    return state&&action&&state->post(action,[revision,request=std::move(request),ports=std::move(ports),effect=std::move(effect),action](State& state)mutable{
        state.after_write=std::move(effect);
        state.finish(revision,std::move(request),std::move(ports),action);
        if(!state.flow.pending()&&state.after_write){state.after_write={};state.preparation_finished=true;state.notify();}
    });
}
bool Coordinator::recover(bool complete)
{
    const auto state=m_state.lock();const auto action=m_services.scheduler->openActionScope();
    return state && action && state->post(action,[complete,action](State& state) { state.recover(complete,action); });
}
bool Coordinator::prepare(std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&,bool)> effect)
{
    const auto state=m_state.lock();const auto action=m_services.scheduler->openActionScope();
    return state && action && state->post(action,[effect=std::move(effect),action](State& state) mutable { state.prepare(std::move(effect),action); });
}
bool Coordinator::finishAndPrepareAsync(uint64_t revision,QmlOnboardingSettings::ApplyRequest request,Ports ports,AsyncPreparation preparation)
{
    const auto state=m_state.lock();const auto action=m_services.scheduler->openActionScope();
    return state&&action&&state->post(action,[revision,request=std::move(request),ports=std::move(ports),preparation=std::move(preparation),action](State& state)mutable{
        if(state.retired||!state.flow.canFinish()||revision!=state.flow.revision()){state.notify();return;}
        state.after_write_async=std::move(preparation);
        state.finish(revision,std::move(request),std::move(ports),action);
        if(!state.flow.pending()&&state.after_write_async){auto pending=std::exchange(state.after_write_async,{});state.requestPreparationClose(*pending);state.preparation_disposition=startup::preparation::Disposition::Failed;state.preparation_finished=true;state.notify();}
    });
}
bool Coordinator::prepareAsync(AsyncPreparation preparation)
{
    const auto state=m_state.lock();const auto action=m_services.scheduler->openActionScope();
    return state&&action&&state->post(action,[preparation=std::move(preparation),action](State& state)mutable{state.prepareAsync(std::move(preparation),action);});
}
void Coordinator::close()
{
    if (const auto state=m_state.lock()) state->post(state->scope.exchange({}),[](State& state) { state.retire(); });
    m_state.reset();m_owner.reset();
}
} // namespace onboarding
