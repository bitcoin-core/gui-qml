// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_INPUTWORK_H
#define BITCOIN_QML_TEST_MOCKS_INPUTWORK_H

#include <qml/retainedvalue.h>
#include <test/mocks/manualstartupexecution.h>

struct InputWork {
    ManualStartupExecution worker;
    const std::shared_ptr<execution::ValueCleanupQueue> cleanup =
        std::make_shared<execution::ValueCleanupQueue>(
            std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(execution::Lane::Utility)));

    execution::ValueCleanup hold() const
    {
        return [cleanup = cleanup](auto work) { return cleanup->hold(std::move(work)); };
    }
};

#endif // BITCOIN_QML_TEST_MOCKS_INPUTWORK_H
