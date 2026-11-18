// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PAYMENTIMPORT_H
#define BITCOIN_QML_PAYMENTIMPORT_H

#include <qml/paymentimportports.h>
#include <qml/models/psbtfeeservices.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <memory>
#include <QUrl>

namespace paymentimport {
struct Input {
    enum class Kind { Text, File, Drop };
    Kind kind{Kind::Text};
    QString value;
    SourceField field{SourceField::WholeRequest};
    int url_count{0};
    QUrl url{};
};
struct Services {
    using Clock = std::function<std::chrono::steady_clock::time_point()>;
    using Wake = std::function<void(std::chrono::milliseconds, std::function<void()>)>;
    std::shared_ptr<execution::Scheduler> scheduler;
    std::function<Inspection(const Input&)> read;
    std::function<Inspection(const Inspection&)> analyze;
    Clock now{std::chrono::steady_clock::now};
    // Entry transport only. The callback enqueues Domain work; time and retry
    // decisions remain on Domain and are controlled independently in tests.
    Wake wake;
};
Inspection ReadSource(const Input& input);
Inspection AnalyzeSource(const Inspection& source, const psbtfee::Lookup& lookup = {});

struct Snapshot {
    uint64_t sequence{0}, owner{0}, source{0};
    Phase phase{Phase::Empty};
    Kind kind{Kind::Unknown};
    Error error_code{Error::None};
    QString error;
    std::optional<Target> target;
    std::optional<Prompt> prompt;
    std::optional<Receipt> result;
    std::shared_ptr<const Inspection> inspection;
    std::shared_ptr<const ChildReply> child_result;
    bool pending{false}, can_apply{false}, can_review{false}, can_export{false}, can_reconcile{false};
};

/** One original source, independent of its optional target. All state changes
 * run on its Domain lane. Foreign target callbacks fill reserved reply cells;
 * they never execute this owner's decisions on another executor's Domain. */
class Owner final : public std::enable_shared_from_this<Owner>
{
public:
    using Notify = std::function<void(Snapshot)>;
    using Retired = std::function<void(uint64_t)>;
    static std::shared_ptr<Owner> Create(uint64_t identity, Services services, Notify notify, Retired retired = {});
    execution::SubmitResult inspect(Input input);
    execution::SubmitResult retry();
    execution::SubmitResult chooseTarget(std::shared_ptr<const TargetPort> target);
    execution::SubmitResult apply();
    execution::SubmitResult use();
    execution::SubmitResult replace(uint64_t prompt);
    execution::SubmitResult keepDraft(uint64_t prompt);
    execution::SubmitResult openReview();
    execution::SubmitResult reconcile();
    execution::SubmitResult close();
    execution::SubmitResult tick();

private:
    struct State;
    struct Delivery;
    struct Close;
    enum class ReplyKind { Observe, Dispatch, Query };
    struct Reply;
    Owner(uint64_t identity, Services services, std::shared_ptr<execution::Scheduler> scope,
          Notify notify, Retired retired, std::shared_ptr<State> state);
    execution::SubmitResult submit(std::function<void()> decision);
    void publish();
    template <bool Analysis> void read(uint64_t source);
    template <bool Analysis> void consumed(uint64_t source, std::shared_ptr<const Inspection> output, std::exception_ptr error = {});
    void observe();
    void useReady();
    void dispatch(Child child);
    void query();
    void account();
    void finish(Reply reply, ReplyKind kind, uint64_t source, uint64_t target, std::optional<Child> child, uint64_t delivery);
    std::shared_ptr<Delivery> reserve(ReplyKind kind, std::optional<Child> child = {});
    void cancelObsolete();
    void wake();
    void poll();
    void closeOnDomain();
    const uint64_t m_identity;
    const Services m_services;
    const std::shared_ptr<execution::Scheduler> m_scope;
    const execution::ResourceKey m_key;
    const Notify m_notify;
    const Retired m_retired;
    const std::shared_ptr<State> m_state;
    const std::shared_ptr<Close> m_close;
    execution::TaskId m_close_task;
    std::atomic_bool m_close_requested{false};
    // close() revokes this before queuing the Domain lifecycle decision. A
    // queued target GUI handoff must not outlive its Source view.
    const std::shared_ptr<std::atomic_bool> m_presentation_allowed{std::make_shared<std::atomic_bool>(true)};
};
} // namespace paymentimport
#endif // BITCOIN_QML_PAYMENTIMPORT_H
