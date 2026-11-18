// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_DRAINEXECUTORS_H
#define BITCOIN_QML_TEST_MOCKS_DRAINEXECUTORS_H
#include <qml/backendexecutor.h>
#include <QEventLoop>
#include <QScopeGuard>

// Declare after a stack native Node and before its GUI observers. Observer
// destructors close admission without waiting; this test owner keeps the
// borrowed Node alive until independently retained worker packets retire.
inline auto DrainExecutorsBeforeNativeOwnerDestruction()
{
    return qScopeGuard([] {
        QEventLoop loop;
        bool drained{false};
        BackendExecutor::shutdownAll(&loop, [&] { drained = true; loop.quit(); });
        while (!drained) loop.exec();
    });
}
#endif // BITCOIN_QML_TEST_MOCKS_DRAINEXECUTORS_H
