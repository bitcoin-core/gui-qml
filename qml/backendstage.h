// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BACKENDSTAGE_H
#define BITCOIN_QML_BACKENDSTAGE_H

#include <qml/backendexecutor.h>

#include <QEventLoop>

template <typename Work>
auto RunBackendStage(Work work)
{
    using Result = std::invoke_result_t<Work>;
    static_assert(!std::is_void_v<Result>);
    QEventLoop drain_loop;
    BackendExecutor executor;
    std::optional<Result> stage_result;
    std::exception_ptr stage_error;
    QObject::connect(&executor, &BackendExecutor::drained, &drain_loop, &QEventLoop::quit);
    executor.submit(&drain_loop, std::move(work), [&](Result value) {
        stage_result.emplace(std::move(value));
        executor.shutdown();
    }, [&](std::exception_ptr failure) {
        stage_error = failure;
        executor.shutdown();
    });
    while (!executor.isDrained()) drain_loop.exec();
    if (stage_error) std::rethrow_exception(stage_error);
    return std::move(*stage_result);
}

#endif // BITCOIN_QML_BACKENDSTAGE_H
