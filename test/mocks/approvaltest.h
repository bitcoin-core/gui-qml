// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_APPROVALTEST_H
#define BITCOIN_QML_TEST_MOCKS_APPROVALTEST_H
#include <qml/models/walletmodel.h>
#include <QtTest/QtTest>
inline bool AwaitSignerApprovalOutcome(WalletModel& model)
{
    const QPointer<WalletModel> guard{&model};
    const auto id = model.approveExternalSignerTransaction();
    if (!id) return false;
    return QTest::qWaitFor([&] {
        if (!guard) return true;
        const auto state = guard->signerApprovalState();
        return state == WalletModel::ApprovalState::ApprovalSigned || state == WalletModel::ApprovalState::ApprovalPartiallySigned ||
            state == WalletModel::ApprovalState::ApprovalFailed || state == WalletModel::ApprovalState::ApprovalUncertain ||
            state == WalletModel::ApprovalState::ApprovalUnverified;
    }, 5000) && guard && guard->signerApprovalId() == id;
}
#endif // BITCOIN_QML_TEST_MOCKS_APPROVALTEST_H
