// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MESSAGEBUMPTEST_H
#define BITCOIN_QML_TEST_MOCKS_MESSAGEBUMPTEST_H

#include <qml/models/signverifymessagemodel.h>
#include <qml/models/bumptransactionmodel.h>
#include <QPointer>
#include <QtTest/QtTest>

inline bool AwaitMessageSigning(SignVerifyMessageModel& model, const QString& address, const QString& message,
                                std::optional<QString> passphrase = {})
{
    const QPointer<SignVerifyMessageModel> guard{&model};
    const auto id = passphrase ? model.requestSignMessageWithPassphrase(address, message, *passphrase) : model.requestSignMessage(address, message);
    if (!id) return false;
    return QTest::qWaitFor([&] { return !guard || !guard->signingBusy(); }, 5000) && guard &&
        guard->signingId() == id && guard->signingState() == SignVerifyMessageModel::SigningSucceeded;
}
inline bool AwaitMessageVerification(SignVerifyMessageModel& model, const QString& address, const QString& message, const QString& signature)
{
    const QPointer<SignVerifyMessageModel> guard{&model};
    const auto id = model.requestVerifyMessage(address, message, signature);
    if (!id) return false;
    return QTest::qWaitFor([&] { return !guard || !guard->verificationBusy(); }, 5000) && guard &&
        guard->verificationId() == id && guard->verificationState() == SignVerifyMessageModel::VerificationValid;
}
inline bool AwaitMessageAddressValidation(SignVerifyMessageModel& model, const QString& address, bool signing = true)
{
    const QPointer<SignVerifyMessageModel> guard{&model};
    if (signing) model.setSigningAddress(address); else model.setVerificationAddress(address);
    return QTest::qWaitFor([&] { return !guard || !guard->addressValidationPending(); }, 5000) && guard &&
        (signing ? guard->signingAddressValid() : guard->verificationAddressValid());
}
inline bool AwaitBumpPreparation(BumpTransactionModel& model, const QString& txid, unsigned int target)
{
    const QPointer<BumpTransactionModel> guard{&model};
    const auto id = model.requestPrepareFeeBump(txid, target);
    if (!id) return false;
    return QTest::qWaitFor([&] { return !guard || !guard->busy(); }, 5000) && guard &&
        guard->attemptId() == id && guard->state() == BumpTransactionModel::NeedsConfirmation;
}
inline bool AwaitBumpCommit(BumpTransactionModel& model, std::optional<QString> passphrase = {})
{
    const QPointer<BumpTransactionModel> guard{&model};
    const auto attempt = model.attemptId();
    const auto id = passphrase ? model.requestConfirmFeeBumpWithPassphrase(*passphrase) : model.requestConfirmFeeBump();
    if (!id) return false;
    return QTest::qWaitFor([&] { return !guard || !guard->busy(); }, 5000) && guard &&
        guard->attemptId() == attempt && guard->commitId() == id && guard->state() == BumpTransactionModel::Succeeded;
}

#endif // BITCOIN_QML_TEST_MOCKS_MESSAGEBUMPTEST_H
