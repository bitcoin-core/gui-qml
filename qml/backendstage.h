// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BACKENDSTAGE_H
#define BITCOIN_QML_BACKENDSTAGE_H

#include <qml/backendexecutor.h>

#include <QEventLoop>

/** Run a bootstrap stage while processing GUI events. The work may not access
 * GUI objects; collect messages in its result and display them after returning.
 * Backend borrowers stay in scope until both the job and worker have drained.
 */
template <typename Work>
auto RunBackendStage(Work work)
{
    using Result = std::invoke_result_t<Work>;
    static_assert(!std::is_void_v<Result>);
    QEventLoop loop;
    BackendExecutor executor;
    std::optional<Result> result;
    std::exception_ptr error;
    QObject::connect(&executor, &BackendExecutor::drained, &loop, &QEventLoop::quit);
    executor.submit(&loop, std::move(work), [&](Result value) {
        result.emplace(std::move(value));
        executor.shutdown();
    }, [&](std::exception_ptr failure) {
        error = failure;
        executor.shutdown();
    });
    while (!executor.isDrained()) loop.exec();
    if (error) std::rethrow_exception(error);
    return std::move(*result);
}

#endif // BITCOIN_QML_BACKENDSTAGE_H
