// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/paymentimportreview.h>
#include <qml/retainedvalue.h>
#include <atomic>
#include <map>
#include <mutex>
namespace paymentimport {
namespace {
struct ReviewCall {
    std::shared_ptr<execution::Scheduler> scope;
    execution::TaskId cleanup;
    Child child;
    std::mutex mutex;
    std::optional<ChildReply> result;
    std::shared_ptr<std::atomic_bool> presentation_allowed{std::make_shared<std::atomic_bool>(true)};
};
struct Reviews { std::mutex mutex; std::map<CommandKey, std::shared_ptr<ReviewCall>> calls; };
}
std::shared_ptr<const TargetPort> WithReviewFactory(std::shared_ptr<const TargetPort> payment, ReviewServices services)
{
    if (!payment || !services.scheduler || !services.current || !services.prepare) return {};
    const auto routes = execution::RetainValues<Reviews>(services.scheduler, [] { return std::make_unique<Reviews>(); });
    if (!routes) return {};
    auto port = std::make_shared<TargetPort>(*payment);
    port->observe = [payment, services](auto source, auto done, auto failed) {
        if (!source || source->kind != Kind::Psbt) return payment->observe(std::move(source), std::move(done), std::move(failed));
        return execution::Submit(*services.scheduler, [payment, services] {
            const bool current = services.current();
            return Target{payment->opening, payment->draft, services.view, payment->recipient, current, current, false};
        }, std::move(done), std::move(failed), {execution::Lane::Domain, {}, execution::WorkClass::Observation});
    };
    const auto query = [routes](CommandKey key, Child child, TargetPort::Completed done, TargetPort::Failed failed) {
        std::shared_ptr<ReviewCall> call;
        { std::lock_guard lock{routes->mutex}; const auto found = routes->calls.find(key); if (found != routes->calls.end()) call = found->second; }
        if (!call || call->child != child) return execution::Admission{{}, execution::Rejection::Invalid};
        return execution::Submit(*call->scope, [call, child] {
            std::lock_guard lock{call->mutex};
            return call->result.value_or(ChildReply{Receipt{child, Outcome::Unknown, 0, child.target.revision}, {}, {}, {}});
        }, std::move(done), std::move(failed), {execution::Lane::Domain});
    };
    port->query = [payment, query](auto key, auto child, auto done, auto failed) {
        return child.kind == ChildKind::Apply ? payment->query(key, child, std::move(done), std::move(failed))
            : query(key, child, std::move(done), std::move(failed));
    };
    port->acknowledge = [payment, routes](auto key, auto child) {
        if (child.kind == ChildKind::Apply) return payment->acknowledge(key, child);
        std::shared_ptr<ReviewCall> call;
        { std::lock_guard lock{routes->mutex}; const auto found = routes->calls.find(key); if (found != routes->calls.end()) call = found->second; }
        if (!call || call->child != child) return execution::Admission{{}, execution::Rejection::Invalid};
        { std::lock_guard lock{call->mutex}; if (!call->result) return execution::Admission{{}, execution::Rejection::Invalid}; }
        return call->scope->activateCleanup(call->cleanup) ? execution::Admission{call->cleanup, execution::Rejection::None}
            : execution::Admission{{}, execution::Rejection::Closed};
    };
    port->retire = [payment, routes](auto key, auto child) {
        if (child.kind == ChildKind::Apply) { if (payment->retire) payment->retire(key, child); return; }
        std::lock_guard lock{routes->mutex}; const auto found = routes->calls.find(key);
        if (found != routes->calls.end() && found->second->child == child) found->second->presentation_allowed->store(false);
    };
    port->dispatch = [payment, services, routes, query](CommandKey key, Child child, std::shared_ptr<const Inspection> source,
                                                     TargetPort::Completed done, TargetPort::Failed failed) {
        if (child.kind == ChildKind::Apply) return payment->dispatch(key, child, std::move(source), std::move(done), std::move(failed));
        if (!key.owner || key.child != child.id || !source || source->kind != Kind::Psbt || !source->psbt || !source->psbt->document)
            return execution::Admission{{}, execution::Rejection::Invalid};
        bool duplicate;
        { std::lock_guard lock{routes->mutex}; duplicate = routes->calls.contains(key); }
        if (duplicate) return query(key, child, std::move(done), std::move(failed));
        auto call = execution::RetainValues<ReviewCall>(services.scheduler, [] { return std::make_unique<ReviewCall>(); });
        auto output = execution::RetainValues<psbtwork::ImportResult>(services.scheduler, [] { return std::make_unique<psbtwork::ImportResult>(); });
        if (!call || !output) return execution::Admission{{}, execution::Rejection::Closed};
        if (source->presentation_allowed) call->presentation_allowed = source->presentation_allowed;
        call->scope = services.scheduler->openActionScope(); call->child = child;
        if (!call->scope) return execution::Admission{{}, execution::Rejection::Closed};
        const auto cleanup = execution::Submit(*call->scope, [] {}, [routes, key, weak = std::weak_ptr<ReviewCall>{call}] {
            std::lock_guard lock{routes->mutex}; const auto found = routes->calls.find(key);
            if (found != routes->calls.end() && found->second == weak.lock()) routes->calls.erase(found);
        }, {}, {execution::Lane::Cleanup}, true);
        if (!cleanup) return execution::Admission{{}, cleanup.rejection};
        call->cleanup = cleanup.id;
        bool inserted{false};
        try { std::lock_guard lock{routes->mutex}; inserted = routes->calls.emplace(key, call).second; }
        catch (...) { call->scope->activateCleanup(call->cleanup); throw; }
        if (!inserted) { call->scope->activateCleanup(call->cleanup); return execution::Admission{{}, execution::Rejection::Capacity}; }
        const auto completed = [call, output, services, key, child, done](psbtwork::ImportResult result) {
            const bool current = services.current() && child.target.revision == services.view;
            ChildReply reply;
            {
                std::lock_guard lock{call->mutex};
                if (call->result) return;
                *output = std::move(result);
                if (output->disposition == psbtwork::ImportResult::Disposition::Known && output->message.isEmpty())
                    output->message = QStringLiteral("This transaction is already known to the original wallet.");
                // Every admitted attempt records its exact outcome before a
                // callback can be lost; queries never prepare a second review.
                reply = ChildReply{Receipt{child, current ? (output->review ? Outcome::ReviewOpened : Outcome::Rejected) : Outcome::Stale,
                    current && output->review ? child.id : 0, services.view}, {}, {}, output, call->presentation_allowed};
                call->result = reply;
            }
            if (current && services.present) { try { services.present(key, child, output, call->presentation_allowed); } catch (...) {} }
            done(std::move(reply));
        };
        const auto rejected = [completed](std::exception_ptr) {
            psbtwork::ImportResult result; result.message = QStringLiteral("The original wallet review could not be prepared."); completed(std::move(result));
        };
        if (!services.current() || child.target.opening != payment->opening || child.target.draft != payment->draft ||
            child.target.recipient != payment->recipient || child.target.revision != services.view) {
            const auto admission = execution::Submit(*call->scope, [] { return psbtwork::ImportResult{}; }, completed, rejected, {execution::Lane::Domain});
            if (!admission) call->scope->activateCleanup(call->cleanup);
            return admission;
        }
        execution::Admission admission;
        try { admission = services.prepare(std::move(source), completed, rejected); }
        catch (...) { rejected(std::current_exception()); return execution::Admission{{}, execution::Rejection::None}; }
        if (!admission) { call->scope->activateCleanup(call->cleanup); }
        return admission;
    };
    return port;
}
} // namespace paymentimport
