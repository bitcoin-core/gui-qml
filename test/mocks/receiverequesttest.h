// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_RECEIVEREQUESTTEST_H
#define BITCOIN_QML_TEST_MOCKS_RECEIVEREQUESTTEST_H

#include <qml/models/receiverequestoperation.h>

#include <QtTest/QtTest>

#include <memory>

// Existing persistence oracles used the old synchronous bool API. Wait for the
// real terminal outcome before making their original assertions. New timing
// tests retain the operation and drive the manual executors explicitly.
inline bool AwaitReceiveRequest(ReceiveRequestOperation* raw)
{
    const std::unique_ptr<ReceiveRequestOperation> operation{raw};
    if (!operation) return false;
    if (!QTest::qWaitFor([&] { return !operation->pending(); }, 5000)) {
        QTest::qFail("Receive request operation did not reach an outcome", __FILE__, __LINE__);
        return false;
    }
    const bool saved = operation->error() == ReceiveRequestOperation::Error::None || operation->requestSaved() || operation->erased() || operation->labelSaved();
    if (!saved) qInfo() << "Receive request outcome:" << operation->error() << operation->errorText();
    return saved;
}

#endif // BITCOIN_QML_TEST_MOCKS_RECEIVEREQUESTTEST_H
