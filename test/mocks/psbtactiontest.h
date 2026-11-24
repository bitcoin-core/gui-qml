// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_PSBTACTIONTEST_H
#define BITCOIN_QML_TEST_MOCKS_PSBTACTIONTEST_H
#include <qml/models/walletmodel.h>
#include <QtTest/QtTest>
inline bool AwaitPsbtAction(WalletModel& model, quint64 ticket)
{
    const QPointer<WalletModel> guard{&model};
    if (!ticket) return false;
    if (!QTest::qWaitFor([&] { return !guard || !guard->psbtActionBusy(); }, 5000) || !guard || guard->psbtActionId() != ticket) {
        QTest::qFail("PSBT action did not reach its original matching terminal outcome", __FILE__, __LINE__);
        return false;
    }
    return true;
}
inline WalletModel::PsbtImportResult AwaitPsbtImport(WalletModel& model, const QString& path)
{
    if (!AwaitPsbtAction(model, model.requestImportPsbtFromFile(path))) {
        QTest::qFail("PSBT import did not reach a matching terminal outcome", __FILE__, __LINE__);
        return WalletModel::PsbtImportResult::PsbtUnsupported;
    }
    return model.psbtImportResult();
}
inline bool AwaitPsbtSave(WalletModel& model, const QString& path)
{
    return AwaitPsbtAction(model, model.requestSaveCurrentTransactionAsPsbt(path)) && model.psbtActionState() == WalletModel::PsbtActionState::PsbtSaved;
}
inline QString AwaitPsbtSaveError(WalletModel& model, const QString& path)
{
    if (!AwaitPsbtAction(model, model.requestSaveCurrentTransactionAsPsbt(path))) return QStringLiteral("PSBT save did not complete");
    if (model.psbtActionState() == WalletModel::PsbtActionState::PsbtSaved) return {};
    return model.psbtActionError().isEmpty() ? QStringLiteral("PSBT save did not succeed") : model.psbtActionError();
}
inline bool AwaitPsbtBroadcast(WalletModel& model)
{
    return AwaitPsbtAction(model, model.requestBroadcastCurrentTransaction()) && model.psbtActionState() == WalletModel::PsbtActionState::PsbtSubmitted;
}
#endif // BITCOIN_QML_TEST_MOCKS_PSBTACTIONTEST_H
