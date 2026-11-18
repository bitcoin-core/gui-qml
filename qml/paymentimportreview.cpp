// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/paymentimportreview.h>
#include <qml/retainedvalue.h>
#include <atomic>
#include <map>
#include <mutex>
#include <utility>
namespace paymentimport {
namespace {
struct ReviewCall {
    Child child;
    std::mutex mutex;
    std::optional<ChildReply> result;
    bool acknowledging{false};
    std::shared_ptr<std::atomic_bool> presentation_allowed{std::make_shared<std::atomic_bool>(true)};
};
struct Reviews { std::mutex mutex; std::map<CommandKey, std::shared_ptr<ReviewCall>> calls; };

void ForgetReview(Reviews& routes, CommandKey key, const std::shared_ptr<ReviewCall>& call)
{
    std::lock_guard lock{routes.mutex};
    const auto found = routes.calls.find(key);
    if (found != routes.calls.end() && found->second == call) routes.calls.erase(found);
}

execution::SubmitResult PostReply(const ReviewServices& services, std::shared_ptr<execution::Task> task)
{
    // Each admitted call keeps its original outcome queue alive through the
    // receipt, even after the target has closed admission.
    return services.retire([task = std::move(task)] {
        task->execute();
        task->deliver();
    });
}
}
std::shared_ptr<const TargetPort> WithReviewFactory(std::shared_ptr<const TargetPort> payment, ReviewServices services)
{
    if (!payment || !services.current || !services.prepare || !services.observe || !services.retire || !services.hold) return {};
    const auto routes = execution::RetainValues<Reviews>(services.hold, [] { return std::make_unique<Reviews>(); });
    if (!routes) return {};
    auto port = std::make_shared<TargetPort>(*payment);
    port->observe = [payment, services](auto source, auto done, auto failed) {
        if (!source || source->kind != Kind::Psbt) return payment->observe(std::move(source), std::move(done), std::move(failed));
        auto task = execution::MakeTask([payment, services] {
            const bool current = services.current();
            return Target{payment->opening, payment->draft, services.view, payment->recipient, current, current, false};
        }, std::move(done), std::move(failed));
        return services.observe(std::move(task));
    };
    const auto query = [routes, services](CommandKey key, Child child, TargetPort::Completed done, TargetPort::Failed failed) {
        std::shared_ptr<ReviewCall> call;
        { std::lock_guard lock{routes->mutex}; const auto found = routes->calls.find(key); if (found != routes->calls.end()) call = found->second; }
        if (!call || call->child != child) return execution::SubmitResult{{}, execution::Rejection::Invalid};
        return PostReply(services, execution::MakeTask([call, child] {
            std::lock_guard lock{call->mutex};
            return call->result.value_or(ChildReply{Receipt{child, Outcome::Unknown, 0, child.target.revision}, {}, {}, {}});
        }, std::move(done), std::move(failed)));
    };
    port->query = [payment, query](auto key, auto child, auto done, auto failed) {
        return child.kind == ChildKind::Apply ? payment->query(key, child, std::move(done), std::move(failed))
            : query(key, child, std::move(done), std::move(failed));
    };
    port->acknowledge = [payment, routes, retire = services.retire](auto key, auto child) {
        if (child.kind == ChildKind::Apply) return payment->acknowledge(key, child);
        std::shared_ptr<ReviewCall> call;
        { std::lock_guard lock{routes->mutex}; const auto found = routes->calls.find(key); if (found != routes->calls.end()) call = found->second; }
        if (!call || call->child != child) return execution::SubmitResult{{}, execution::Rejection::Invalid};
        {
            std::lock_guard lock{call->mutex};
            if (!call->result) return execution::SubmitResult{{}, execution::Rejection::Invalid};
            if (std::exchange(call->acknowledging, true)) return execution::SubmitResult{{}, execution::Rejection::Closed};
        }
        const auto submitted = retire([routes, key, call] { ForgetReview(*routes, key, call); });
        if (!submitted) {
            std::lock_guard lock{call->mutex};
            call->acknowledging = false;
        }
        return submitted;
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
            return execution::SubmitResult{{}, execution::Rejection::Invalid};
        bool duplicate;
        { std::lock_guard lock{routes->mutex}; duplicate = routes->calls.contains(key); }
        if (duplicate) return query(key, child, std::move(done), std::move(failed));
        const auto make_call = [] { return std::make_unique<ReviewCall>(); };
        const auto make_output = [] { return std::make_unique<psbtwork::ImportResult>(); };
        auto call = execution::RetainValues<ReviewCall>(services.hold, make_call);
        auto output = execution::RetainValues<psbtwork::ImportResult>(services.hold, make_output);
        if (!call || !output) return execution::SubmitResult{{}, execution::Rejection::Closed};
        if (source->presentation_allowed) call->presentation_allowed = source->presentation_allowed;
        call->child = child;
        const auto discard = [&] { ForgetReview(*routes, key, call); };
        bool inserted{false};
        try { std::lock_guard lock{routes->mutex}; inserted = routes->calls.emplace(key, call).second; }
        catch (...) { discard(); throw; }
        if (!inserted) { discard(); return execution::SubmitResult{{}, execution::Rejection::Capacity}; }
        const auto completed = [call, output, services, key, child, done](psbtwork::ImportResult result) {
            const bool current = services.current() && child.target.revision == services.view;
            ChildReply reply;
            {
                std::lock_guard lock{call->mutex};
                if (call->result) return;
                *output = std::move(result);
                if (output->disposition == psbtwork::ImportResult::Disposition::Known && output->message.isEmpty())
                    output->message = QStringLiteral("This transaction is already in the wallet.");
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
            psbtwork::ImportResult result; result.message = QStringLiteral("The wallet review could not be prepared."); completed(std::move(result));
        };
        if (!services.current() || child.target.opening != payment->opening || child.target.draft != payment->draft ||
            child.target.recipient != payment->recipient || child.target.revision != services.view) {
            const auto submitted = PostReply(services,
                execution::MakeTask([] { return psbtwork::ImportResult{}; }, completed, rejected));
            if (!submitted) discard();
            return submitted;
        }
        execution::SubmitResult submitted;
        try { submitted = services.prepare(std::move(source), completed, rejected); }
        catch (...) { rejected(std::current_exception()); return execution::SubmitResult{{}, execution::Rejection::None}; }
        if (!submitted) discard();
        return submitted;
    };
    return port;
}
} // namespace paymentimport
