// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_STARTUPEFFECTS_H
#define BITCOIN_QML_STARTUPEFFECTS_H

#include <qml/snapshotquery.h>
#include <qml/guipresentation.h>
#include <qml/backendexecutor.h>
#include <QCoreApplication>
#include <QEventLoop>
#include <QPointer>
#include <stdexcept>

namespace startup {
/** Drive the pre-main-window startup phase while Native owns blocking work.
 * The GUI continues to process window, dialog and quit events. An accepted
 * operation must reach its Domain outcome before borrowed startup arguments
 * can leave scope, including when a quit arrives during the phase.
 * This phase runner is never used by model getters, actions or destructors.
 */
template <typename Work>
auto Run(const execution::SnapshotServices& services, Work work)
{
    using Value = std::invoke_result_t<Work>;
    if (!services.scheduler) throw std::invalid_argument{"Startup execution services are unavailable"};
    QEventLoop phase;
    const QPointer<QEventLoop> observer{&phase};
    auto operation = std::make_shared<execution::OwnedCommand<Value>>(services.scheduler,
        [post = services.post, fallback = presentation::QueuedPost(), observer] {
            const auto wake = [observer] { if (observer) observer->quit(); };
            // Enqueue ports must either retain the turn or throw; silent drops
            // violate the port contract. A throwing primary cannot strand this
            // borrowed startup phase after its actual Domain outcome exists.
            try { if (post) post(wake); else fallback(wake); }
            catch (...) { fallback(wake); }
        });
    const auto admitted = operation->start(std::move(work),
        {execution::Lane::Core, services.scheduler->allocateKey(), execution::WorkClass::Action});
    if (!admitted) throw std::runtime_error{"Startup work could not be queued"};
    auto result = operation->result();
    while (!result) { phase.exec(); result = operation->result(); }
    if (result->error) std::rethrow_exception(result->error);
    if (!result->value) throw std::runtime_error{"Startup work has no outcome"};
    return std::move(*result->value);
}
/** Application lifecycle only: interfaces outlive every producer and join.
 * A fresh executor then retires them on Native. Keeping the owner outside all
 * execution queues avoids holding a cleanup lease across the first drain. */
inline void DrainWorkers()
{
    QEventLoop loop; bool drained{false};
    BackendExecutor::shutdownAll(&loop, [&] { drained = true; loop.quit(); });
    while (!drained) loop.exec();
    presentation::FlushRetirements();
}
template <typename Retire>
void DrainThenRetire(Retire retire)
{
    DrainWorkers();
    BackendExecutor cleanup{execution::Options{}};
    cleanup.setObjectName(QStringLiteral("application-interface-cleanup"));
    Run({cleanup.scheduler(), presentation::QueuedPost()}, [retire = std::move(retire)]() mutable { retire(); return true; });
    cleanup.shutdown();
    DrainWorkers();
}
} // namespace startup
#endif // BITCOIN_QML_STARTUPEFFECTS_H
