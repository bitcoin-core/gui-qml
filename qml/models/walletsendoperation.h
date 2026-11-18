// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETSENDOPERATION_H
#define BITCOIN_QML_MODELS_WALLETSENDOPERATION_H

#include <qml/models/sendoperation.h>
#include <qml/models/transactionreviewinterface.h>

#include <QPointer>

#include <memory>
#include <optional>

class WalletQmlModel;
class WalletQmlModelTransaction;
namespace walletsend {
class CommitConsent;
class Operation;
struct Result;
} // namespace walletsend

//! What a finished send means for its operation and for the user.
struct SendResultText {
    SendOperation::State state;
    QString message;
};
SendResultText DescribeSendResult(const walletsend::Result& result, bool sign_only);

/** A send of one WalletQmlModel review. It owns its walletsend::Operation:
 * prompts and the outcome of that work are this operation's alone. Effects on
 * the wallet and the review after the work finishes are the wallet's, so they
 * happen even when the operation is gone. */
class WalletSendOperation final : public SendOperation
{
    Q_OBJECT
public:
    WalletSendOperation(WalletQmlModel& wallet, bool sign_only, QObject* parent = nullptr);
    ~WalletSendOperation() override;

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
    QPointer<WalletQmlModel> m_wallet;
    QPointer<WalletQmlModelTransaction> m_review;
    quint64 m_attempt_id{0};
    quint64 m_review_id{0};
    std::shared_ptr<walletsend::Operation> m_work;
    //! Withdrawn by cancel() while the work runs; wins only before the commit.
    std::shared_ptr<walletsend::CommitConsent> m_consent;
};

/** WalletQmlModel's TransactionReviewInterface. */
class WalletTransactionReviewInterface final : public TransactionReviewInterface
{
    Q_OBJECT
public:
    explicit WalletTransactionReviewInterface(WalletQmlModel& wallet);
    QObject* currentReview() const override;
    bool available() const override;
    SendOperation* startSend(bool sign_only) override;
    PsbtOperation* startPsbt(PsbtOperation::Kind kind, const QString& path, const QString& signer_result) override;

private:
    QPointer<WalletQmlModel> m_wallet;
};

#endif // BITCOIN_QML_MODELS_WALLETSENDOPERATION_H
