// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/onboardingflow.h>
#include <qml/executor.h>

#include <limits>
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
    struct Queues {
        explicit Queues(execution::Executor& executor)
            : core(executor.makeQueue(execution::Lane::Core)), utility(executor.makeQueue(execution::Lane::Utility)) {}
        execution::SerialQueue core, utility;
        std::recursive_mutex reducer;

        void deliver(std::function<void()> callback)
        {
            if (!utility.post(callback)) callback();
        }
    };

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
    std::function<void()> drained;
    std::weak_ptr<State> owner;
    State(Publish publish, std::function<void()> drained, std::shared_ptr<Queues> queues)
        : drained(std::move(drained)), publish(std::move(publish)), queues(std::move(queues)) {}
    const Publish publish;
    const std::shared_ptr<Queues> queues;
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
        catch(...) { flow.queryUnavailable(QStringLiteral("Startup could not be cancelled cleanly. Nothing was repeated.")); }
    }
    void notify() {
        publish({flow.phase(), flow.revision(), flow.attempt() ? flow.attempt()->id : 0,
            flow.pending(), flow.result().replacement_written, flow.result().completed, preparation_complete,
            preparation_finished,flow.preparationUnknown(),flow.result().uncertain,flow.error(),
            preparation_disposition});
    }
    static void destroy(State* state)
    {
        const auto queues = state->queues;
        queues->deliver([state, queues] {
            auto drained = std::move(state->drained);
            delete state;
            queues->core.close();
            queues->core.whenDrained([queues, drained = std::move(drained)] {
                queues->utility.close();
                queues->utility.whenDrained(drained);
            });
        });
    }

    template <typename Work, typename Done>
    bool submitCore(Work work, Done done, std::function<void(std::exception_ptr)> failed)
    {
        auto task = execution::MakeTask(std::move(work), std::move(done), std::move(failed));
        return queues->core.post([queues = queues, task = std::move(task)]() mutable {
            task->execute();
            queues->deliver([queues, task = std::move(task)] {
                // Serialize fallback delivery with new Utility work.
                std::lock_guard lock{queues->reducer};
                task->deliver();
            });
        });
    }

    bool post(std::function<void(State&)> command)
    {
        auto task = execution::MakeTask(
            [self = owner.lock(), command = std::move(command)] { command(*self); }, [] {}, {});
        return queues->utility.post([queues = queues, task = std::move(task)] {
            std::lock_guard lock{queues->reducer};
            task->execute();
            task->deliver();
        });
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
                if(!self->post([](State& state) { state.startCheck(); })) {
                    auto rejected=std::move(self->next_check->first);self->next_check.reset();
                    self->flow.edit(rejected.revision);
                    rejected.error=QStringLiteral("The setup check could not start. Try again.");
                    self->flow.checked(std::move(rejected));self->notify();
                }
            }
        };
        const auto failed = [done](std::exception_ptr) { done(QStringLiteral("The selected setup could not be validated. Retry the checks.")); };
        if (!submitCore(std::move(validate), done, failed)) failed({});
        notify();
    }
    void finish(uint64_t revision, QmlOnboardingSettings::ApplyRequest request, Ports effect_ports) {
        if (retired) return;
        const auto attempt = flow.finish(revision, std::move(request));
        if (!attempt) { notify(); return; }
        ports = std::move(effect_ports);
        write(*attempt, false);
    }
    void write(const Attempt& attempt, bool completion) {
        const auto self = owner.lock();
        const auto done = [self, id=attempt.id](QmlOnboardingSettings::ProfileResult result) {
            self->flow.persisted(id, std::move(result));
            if (self->after_write) {
                auto effect=std::exchange(self->after_write,{});
                if(self->flow.phase()==Phase::PreparingNode) self->prepare(std::move(effect));
                else self->preparation_finished=true;
            }
            if (self->after_write_async) {
                auto preparation=std::exchange(self->after_write_async,{});
                if(self->flow.phase()==Phase::PreparingNode) self->prepareAsync(std::move(*preparation));
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
            result.error = QStringLiteral("The setup result is not available. Check it before trying again.");
            done(std::move(result));
        };
        const auto work = [ports=ports, request=attempt.request, previous=flow.result(), completion] {
            return completion ? ports.complete(previous) : ports.write(request);
        };
        if ((!completion && !ports.write) || (completion && !ports.complete)
            || !submitCore(work, done, failed))
            failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
        notify();
    }
    void recover(bool completion) {
        if (completion) {
            if (!flow.beginCompletion()) return;
            write(*flow.attempt(), true);
            return;
        }
        if (!flow.beginQuery()) return;
        if (flow.preparationAdmitted()) {
            flow.queryUnavailable(QStringLiteral("Node preparation cannot be safely repeated. Close and restart to check your saved settings."));
            notify();
            return;
        }
        const auto self = owner.lock();
        const auto id = flow.attempt()->id;
        const auto done = [self, id](QmlOnboardingSettings::ProfileResult result) {
            self->flow.reconciled(id, std::move(result));
            self->notify();
        };
        const auto failed = [self](std::exception_ptr) {
            self->flow.queryUnavailable(QStringLiteral("The earlier setup step could not be checked. Nothing was saved again."));
            self->notify();
        };
        const auto original = flow.result();
        if (!ports.query || !submitCore([query=ports.query,original] { return query(original); }, done, failed)) failed({});
        notify();
    }
    void prepare(std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&,bool)> effect) {
        if (!flow.beginPreparation()) { notify(); return; }
        const auto self = owner.lock();
        const auto done = [self](std::pair<bool,QString> result) {
            self->preparation_complete = result.first;
            self->preparation_finished = true;
            self->flow.prepared(result.first, false, std::move(result.second));
            self->notify();
        };
        const auto failed = [self](std::exception_ptr error) {
            bool unknown=true;QString message;
            try {if(error)std::rethrow_exception(error);}
            catch(const execution::NotStarted&){unknown=false;message=QStringLiteral("Node preparation did not start. Your saved settings are kept; restart to continue.");}
            catch(const std::exception& e){message=QString::fromUtf8(e.what());}
            catch(...){message=QStringLiteral("Unknown exception preparing the node.");}
            self->preparation_finished=true;
            self->flow.prepared(false,unknown,std::move(message));
            self->notify();
        };
        const auto request = flow.attempt()->request;
        if (!submitCore([effect=std::move(effect),request] { return effect(request,true); }, done, failed)) {
            preparation_finished=true;
            flow.prepared(false, false, QStringLiteral("The node could not start preparing. Your saved settings are kept; restart to continue."));
        }
        notify();
    }
    void completePreparation(PreparationReply::Value result, std::exception_ptr failure)
    {
        QString error;
        preparation_disposition = startup::preparation::Disposition::Uncertain;
        if (result) {
            preparation_result = std::move(result);
            preparation_disposition = preparation_result->disposition;
            error = preparation_result->error;
        } else {
            error = QStringLiteral("The node preparation result is not available. It was not repeated.");
            if (execution::NotStartedCause(failure)) {
                preparation_disposition = startup::preparation::Disposition::Failed;
                error = QStringLiteral("The node could not start preparing. Your saved settings are kept; restart to continue.");
            }
        }
        preparation_complete = preparation_disposition == startup::preparation::Disposition::Ready;
        preparation_finished = true;
        flow.prepared(*preparation_disposition, std::move(error));
        active_preparation = {};
        notify();
    }

    execution::SubmitResult startPreparation(const std::shared_ptr<PreparationReply>& reply)
    {
        try {
            const auto submitted = active_preparation.start
                ? active_preparation.start({flow.result()}, [reply](auto value) { reply->accept(std::move(value)); })
                : execution::SubmitResult{{}, execution::Rejection::Invalid};
            if (!submitted) {
                requestPreparationClose(active_preparation);
                reply->reject(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
            }
            return submitted;
        } catch (...) {
            const auto error = std::current_exception();
            requestPreparationClose(active_preparation);
            reply->reject(error);
            return {};
        }
    }

    void prepareAsync(AsyncPreparation preparation) {
        // A duplicate command cannot cancel or overwrite the original
        // preparation, including its known terminal outcome.
        if (flow.preparationAdmitted()) { notify(); return; }
        if (!flow.beginPreparation()) {
            preparation_finished=true;
            preparation_disposition=flow.result().uncertain
                ? startup::preparation::Disposition::Uncertain : startup::preparation::Disposition::Failed;
            flow.queryUnavailable(QStringLiteral("Node preparation did not start. Your saved settings are kept; restart to continue."));
            requestPreparationClose(preparation);
            notify();return;
        }
        const auto self = owner.lock();
        auto reply = std::make_shared<PreparationReply>();
        // The reply retains its original State and queue until preparation
        // reports once, including across quiet close.
        reply->complete = [self](auto result, std::exception_ptr failure) {
            const auto& queues = self->queues;
            queues->deliver([self, queues, result = std::move(result), failure] {
                std::lock_guard lock{queues->reducer};
                self->completePreparation(result, failure);
            });
        };
        active_preparation = std::move(preparation);
        startPreparation(reply);
        notify();
    }
    void retire() {
        if (std::exchange(retired, true)) return;
        next_check.reset();flow.close();
        requestPreparationClose(active_preparation);
        notify();
    }
};
Coordinator::Coordinator(execution::Executor& executor, Publish publish, std::function<void()> drained)
{
    auto queues = std::make_shared<State::Queues>(executor);
    m_owner = std::shared_ptr<State>{new State{std::move(publish), std::move(drained), std::move(queues)}, State::destroy};
    m_state = m_owner;
    m_owner->owner = m_owner;
}
Coordinator::~Coordinator() { close(); }
bool Coordinator::check(Checks checks, std::function<QString()> validate)
{
    if (const auto state=m_state.lock()) return state->post(
        [checks=std::move(checks),validate=std::move(validate)](State& state) mutable { state.check(std::move(checks),std::move(validate)); });
    return false;
}
bool Coordinator::finish(uint64_t revision, QmlOnboardingSettings::ApplyRequest request, Ports ports)
{
    const auto state=m_state.lock();
    return state && state->post([revision,request=std::move(request),ports=std::move(ports)](State& state) mutable {
        state.finish(revision,std::move(request),std::move(ports));
    });
}
bool Coordinator::finishAndPrepare(uint64_t revision,QmlOnboardingSettings::ApplyRequest request,Ports ports,
    std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&,bool)> effect)
{
    const auto state=m_state.lock();
    return state&&state->post([revision,request=std::move(request),ports=std::move(ports),effect=std::move(effect)](State& state)mutable{
        state.after_write=std::move(effect);
        state.finish(revision,std::move(request),std::move(ports));
        if(!state.flow.pending()&&state.after_write){state.after_write={};state.preparation_finished=true;state.notify();}
    });
}
bool Coordinator::recover(bool complete)
{
    const auto state=m_state.lock();
    return state && state->post([complete](State& state) { state.recover(complete); });
}
bool Coordinator::prepare(std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&,bool)> effect)
{
    const auto state=m_state.lock();
    return state && state->post([effect=std::move(effect)](State& state) mutable { state.prepare(std::move(effect)); });
}
bool Coordinator::finishAndPrepareAsync(uint64_t revision,QmlOnboardingSettings::ApplyRequest request,Ports ports,AsyncPreparation preparation)
{
    const auto state=m_state.lock();
    return state&&state->post([revision,request=std::move(request),ports=std::move(ports),preparation=std::move(preparation)](State& state)mutable{
        if(state.retired||!state.flow.canFinish()||revision!=state.flow.revision()){state.notify();return;}
        state.after_write_async=std::move(preparation);
        state.finish(revision,std::move(request),std::move(ports));
        if(!state.flow.pending()&&state.after_write_async){auto pending=std::exchange(state.after_write_async,{});state.requestPreparationClose(*pending);state.preparation_disposition=startup::preparation::Disposition::Failed;state.preparation_finished=true;state.notify();}
    });
}
bool Coordinator::prepareAsync(AsyncPreparation preparation)
{
    const auto state=m_state.lock();
    return state&&state->post([preparation=std::move(preparation)](State& state)mutable{state.prepareAsync(std::move(preparation));});
}
void Coordinator::close()
{
    const auto state = m_state.lock();
    m_state.reset();
    m_owner.reset();
    if (!state) return;
    if (!state->post([](State& state) { state.retire(); })) {
        std::lock_guard lock{state->queues->reducer};
        state->retire();
    }
}
} // namespace onboarding
