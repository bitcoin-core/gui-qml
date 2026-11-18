// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONREVIEWINTERFACE_H
#define BITCOIN_QML_MODELS_TRANSACTIONREVIEWINTERFACE_H

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
public:
    using WalletInterface::WalletInterface;

    //! The transaction under review, or null.
    virtual QObject* currentReview() const = 0;
    //! Starts sending, or only signing, the current review. Returns null when
    //! the wallet can't start one now; the caller owns the result.
    virtual SendOperation* startSend(bool sign_only) = 0;
    //! Starts a PSBT action. \p path is the file for Save and Import;
    //! \p signer_result names an unverified signer result to save instead of
    //! the review. Returns null while another wallet action runs.
    virtual PsbtOperation* startPsbt(PsbtOperation::Kind kind, const QString& path, const QString& signer_result) = 0;

Q_SIGNALS:
    void currentReviewChanged();
};

#endif // BITCOIN_QML_MODELS_TRANSACTIONREVIEWINTERFACE_H
