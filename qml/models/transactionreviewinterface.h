// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONREVIEWINTERFACE_H
#define BITCOIN_QML_MODELS_TRANSACTIONREVIEWINTERFACE_H

#include <qml/models/filesaveoperation.h>
#include <qml/models/walletinterface.h>
#include <qml/models/psbtoperation.h>

#include <QObject>
#include <QString>

class SendOperation;

/** What a TransactionReviewController needs from a wallet: the transaction
 * under review and a way to act on it. WalletQmlModel implements it; tests
 * use a fake. */
class TransactionReviewInterface : public WalletInterface
{
    Q_OBJECT
    Q_PROPERTY(bool currentTransactionIsImportedPsbt READ currentTransactionIsImportedPsbt NOTIFY currentReviewChanged)
    Q_PROPERTY(bool currentTransactionCanSend READ currentTransactionCanSend NOTIFY currentReviewChanged)
    Q_PROPERTY(bool currentTransactionCanSignOnly READ currentTransactionCanSignOnly NOTIFY currentReviewChanged)
    Q_PROPERTY(bool currentTransactionNeedsRevalidation READ currentTransactionNeedsRevalidation NOTIFY reviewDetailsChanged)
    Q_PROPERTY(bool transactionFeePending READ transactionFeePending NOTIFY currentReviewChanged)
    Q_PROPERTY(bool currentTransactionCanBroadcast READ currentTransactionCanBroadcast NOTIFY currentReviewChanged)
    Q_PROPERTY(QString currentTransactionReviewMessage READ currentTransactionReviewMessage NOTIFY currentReviewChanged)
public:
    using WalletInterface::WalletInterface;

    //! The transaction under review, or null.
    virtual QObject* currentReview() const = 0;
    virtual bool currentTransactionIsImportedPsbt() const = 0;
    virtual bool currentTransactionCanSend() const = 0;
    virtual bool currentTransactionCanSignOnly() const = 0;
    virtual bool currentTransactionNeedsRevalidation() const = 0;
    virtual bool transactionFeePending() const = 0;
    virtual bool currentTransactionCanBroadcast() const = 0;
    virtual QString currentTransactionReviewMessage() const = 0;
    //! Starts sending, or only signing, the current review. Returns null when
    //! the wallet can't start one now; the caller owns the result.
    virtual SendOperation* startSend(bool sign_only) = 0;
    //! Starts a PSBT action. \p path is the file for Save and Import;
    //! \p signer_result names an unverified signer result to save instead of
    //! the review. Returns null while another wallet action runs.
    virtual PsbtOperation* startPsbt(PsbtOperation::Kind kind, const QString& path, const QString& signer_result) = 0;
    //! The unverified signer result for the current review that can be
    //! saved, or empty.
    virtual QString unverifiedSignerResult() const = 0;
    virtual int signerApprovalState() const = 0;
    virtual QString signerApprovalError() const = 0;
    virtual bool signerNotFound() const = 0;
    virtual void approveSigner() = 0;
    virtual FileSaveOperation* fileExportOperation() const = 0;
    virtual QString reviewError() const = 0;

Q_SIGNALS:
    void currentReviewChanged();
    void unverifiedSignerResultChanged();
    void reviewDetailsChanged();
};

#endif // BITCOIN_QML_MODELS_TRANSACTIONREVIEWINTERFACE_H
