// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_PREPARATIONTEST_H
#define BITCOIN_QML_TEST_MOCKS_PREPARATIONTEST_H

#include <qml/models/walletqmlmodel.h>
#include <QtTest/QtTest>

// Integration/compatibility assertions wait for an actual result, never
// interpret command admission as a prepared review. Controlled unit schedules
// exercise Core/outcome/presentation separately without this convenience wait.
inline bool AwaitPreparation(WalletQmlModel& model, std::optional<QString> passphrase = {}, std::function<void()> pump = {})
{
    const QPointer<WalletQmlModel> guard{&model};
    if (!QTest::qWaitFor([&] { if (pump) pump(); return !guard || (!guard->coinSelectionPending() && !guard->sendRecipientList()->validationPending() && !guard->customFeeTargetPending()); }, 5000) || !guard) return false;
    const auto ticket = passphrase ? model.requestPrepareTransactionWithPassphrase(*passphrase) : model.requestPrepareTransaction();
    if (!ticket) return false;
    return QTest::qWaitFor([&] { if (pump) pump(); return !guard || !guard->preparationBusy(); }, 5000) && guard &&
        guard->preparationId() == ticket && guard->preparationState() == WalletQmlModel::PreparationState::PreparationReady;
}

#endif // BITCOIN_QML_TEST_MOCKS_PREPARATIONTEST_H
