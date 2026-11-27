// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETSENDHANDLE_H
#define BITCOIN_QML_MODELS_WALLETSENDHANDLE_H

#include <qml/models/sendoperation.h>
#include <qml/models/transactionreviewinterface.h>

#include <QPointer>

#include <memory>
#include <optional>

class WalletModel;
class TransactionReviewModel;
namespace walletsend {
class CommitConsent;
class SendTask;
struct Result;
} // namespace walletsend

//! What a finished send means for its operation and for the user.
struct SendResultText {
    SendOperation::State state;
    QString message;
};
SendResultText DescribeSendResult(const walletsend::Result& result, bool sign_only);

/** A send of one WalletModel review. It owns its walletsend::SendTask:
 * prompts and the outcome of that work are this operation's alone. Effects on
 * the wallet and the review after the work finishes are the wallet's, so they
 * happen even when the operation is gone. */
class WalletSendHandle final : public SendOperation
{
    Q_OBJECT
public:
    WalletSendHandle(WalletModel& wallet, bool sign_only, QObject* parent = nullptr);
    ~WalletSendHandle() override;

    //! Starts the work for the wallet's current review. False when the wallet
    //! can't start a send now; a send that can't proceed reports Failed.
    bool start(std::optional<QString> passphrase = std::nullopt);

    void confirm() override;
    void unlock(const QString& passphrase) override;
    void cancel() override;

    //! Called on the GUI thread when the work reports a prompt or finishes.
    void promptChanged();
    void workFinished(const walletsend::Result& result, bool review_current);

private:
    QPointer<WalletModel> m_wallet;
    QPointer<TransactionReviewModel> m_review;
    quint64 m_attempt_id{0};
    quint64 m_review_id{0};
    std::shared_ptr<walletsend::SendTask> m_work;
    //! Withdrawn by cancel() while the work runs; wins only before the commit.
    std::shared_ptr<walletsend::CommitConsent> m_consent;
};

/** WalletModel's TransactionReviewInterface. */
class WalletTransactionReview final : public TransactionReviewInterface
{
    Q_OBJECT
public:
    explicit WalletTransactionReview(WalletModel& wallet);
    QObject* currentReview() const override;
    bool currentTransactionIsImportedPsbt() const override;
    bool currentTransactionCanSend() const override;
    bool currentTransactionCanSignOnly() const override;
    bool currentTransactionNeedsRevalidation() const override;
    bool transactionFeePending() const override;
    bool currentTransactionCanBroadcast() const override;
    QString currentTransactionReviewMessage() const override;
    bool available() const override;
    SendOperation* startSend(bool sign_only) override;
    PsbtOperation* startPsbt(PsbtOperation::Kind kind, const QString& path, const QString& signer_result) override;
    QString unverifiedSignerResult() const override;
    int signerApprovalState() const override;
    QString signerApprovalError() const override;
    bool signerNotFound() const override;
    void approveSigner() override;
    FileSaveOperation* fileExportOperation() const override;
    QString reviewError() const override;

private:
    QPointer<WalletModel> m_wallet;
};

#endif // BITCOIN_QML_MODELS_WALLETSENDHANDLE_H
