// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_NATIVELISTQUEUES_H
#define BITCOIN_QML_TEST_MOCKS_NATIVELISTQUEUES_H

#include <qml/executor.h>
#include <qml/nativelistmodel.h>

inline NativeListModel::Queues NativeListQueues(execution::Executor& executor)
{
    auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Core));
    auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
    auto cleanup = std::make_shared<execution::ValueCleanupQueue>(
        std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility)));
    return {core, utility, {}, [cleanup](auto retire) { return cleanup->hold(std::move(retire)); }};
}

#endif // BITCOIN_QML_TEST_MOCKS_NATIVELISTQUEUES_H
