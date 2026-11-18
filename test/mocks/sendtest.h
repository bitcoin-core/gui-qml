// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_SENDTEST_H
#define BITCOIN_QML_TEST_MOCKS_SENDTEST_H

#include <qml/models/walletqmlmodel.h>
#include <qml/models/walletsendoperation.h>
#include <qml/walletsend.h>
#include <QtTest/QtTest>

#include <memory>
#include <optional>

//! Starts a send of the model's current review, as a review page would.
//! Null when the wallet can't start one now.
inline std::unique_ptr<WalletSendOperation> StartSend(WalletQmlModel& model, bool sign_only = false,
                                                      std::optional<QString> passphrase = {})
{
    auto operation = std::make_unique<WalletSendOperation>(model, sign_only);
    if (!operation->start(std::move(passphrase))) return nullptr;
    return operation;
}

//! The send state of a review page's TransactionReviewController.
inline SendOperation::State ReviewSendState(const QObject& controller)
{
    return controller.property("sendState").value<SendOperation::State>();
}

// A commit result, not a mempool/broadcast assertion. Tests of observer loss
// retain the domain operation explicitly instead of borrowing a deleted facade.
inline bool AwaitCommittedSend(WalletQmlModel& model, std::optional<QString> passphrase = {})
{
    const QPointer<WalletQmlModel> guard{&model};
    auto send = StartSend(model, false, passphrase);
    if (!send) return false;
    const auto original = guard ? guard->sendOperation() : nullptr;
    using State = SendOperation::State;
    if (!QTest::qWaitFor([&] { return !guard || send->state() != State::Pending; }, 5000)) {
        QTest::qFail("Accepted send did not reach its first prompt or native outcome", __FILE__, __LINE__);
        return false;
    }
    if (send->state() == State::NeedsConfirmation) send->confirm();
    if (send->state() == State::NeedsUnlock) return false;
    if (original && !QTest::qWaitFor([&] { return original->result().has_value(); }, 5000)) {
        QTest::qFail("Accepted send did not produce its original domain outcome", __FILE__, __LINE__);
        return false;
    }
    if (!QTest::qWaitFor([&] { return !guard || send->finished(); }, 5000)) {
        QTest::qFail("Send did not reach its original matching terminal outcome", __FILE__, __LINE__);
        return false;
    }
    return original ? bool(original->result()->committed) : send->state() == State::Committed;
}

#endif // BITCOIN_QML_TEST_MOCKS_SENDTEST_H
