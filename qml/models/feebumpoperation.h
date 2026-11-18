// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_FEEBUMPOPERATION_H
#define BITCOIN_QML_MODELS_FEEBUMPOPERATION_H

#include <QObject>
#include <QString>

/** One fee bump of one transaction: its review, confirmation and outcome.
 * BumpTransactionModel implements it; QML tests use a fake. A page reaches it
 * through its FeeBumpController; the wallet keeps it past its commit point
 * until a page has shown its result (FeeBumpInterface). */
class FeeBumpOperation : public QObject
{
    Q_OBJECT
public:
    enum State { Idle, Preparing, NeedsConfirmation, Committing, Succeeded, Failed, Discarding, Uncertain, NeedsRevalidation, Closed };
    Q_ENUM(State)
    enum WalletRecordState { WalletRecordUnknown, WalletRecordAbsentAtObservation, WalletRecordRecorded };
    Q_ENUM(WalletRecordState)
    enum WalletSubmissionState { WalletSubmissionUnknown, WalletSubmissionMempool, WalletSubmissionConfirmed,
                                 WalletSubmissionNotInMempool, WalletSubmissionConflicted, WalletSubmissionAbandoned };
    Q_ENUM(WalletSubmissionState)
    Q_PROPERTY(State state READ state NOTIFY stateChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY stateChanged)
    Q_PROPERTY(QString oldFee READ oldFee NOTIFY resultChanged)
    Q_PROPERTY(QString newFee READ newFee NOTIFY resultChanged)
    Q_PROPERTY(QString feeIncrease READ feeIncrease NOTIFY resultChanged)
    Q_PROPERTY(QString oldTxid READ oldTxid NOTIFY resultChanged)
    Q_PROPERTY(QString newTxid READ newTxid NOTIFY resultChanged)
    Q_PROPERTY(WalletRecordState walletRecordState READ walletRecordState NOTIFY resultChanged)
    Q_PROPERTY(WalletSubmissionState walletSubmissionState READ walletSubmissionState NOTIFY resultChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY resultChanged)
    Q_PROPERTY(QString unlockErrorText READ unlockErrorText NOTIFY resultChanged)
    Q_PROPERTY(bool needsUnlock READ needsUnlock NOTIFY needsUnlockChanged)
    Q_PROPERTY(bool canRecheckOutcome READ canRecheckOutcome NOTIFY stateChanged)

    virtual State state() const = 0;
    virtual bool busy() const = 0;
    virtual QString oldFee() const = 0;
    virtual QString newFee() const = 0;
    virtual QString feeIncrease() const = 0;
    virtual QString oldTxid() const = 0;
    virtual QString newTxid() const = 0;
    virtual WalletRecordState walletRecordState() const = 0;
    virtual WalletSubmissionState walletSubmissionState() const = 0;
    virtual QString errorText() const = 0;
    virtual QString unlockErrorText() const = 0;
    virtual bool needsUnlock() const = 0;
    virtual bool canRecheckOutcome() const = 0;
    //! A commit was submitted: its outcome must be known and shown before
    //! another bump of this transaction can commit.
    virtual bool committed() const = 0;

    //! Starts reviewing a replacement for `txid`; false when refused.
    virtual bool review(const QString& txid, unsigned target_blocks) = 0;
    //! Prepares the review again after it no longer met policy.
    virtual bool prepareAgain(const QString& txid, unsigned target_blocks) = 0;
    virtual bool confirm() = 0;
    virtual bool confirmWithPassphrase(const QString& passphrase) = 0;
    //! Checks again whether an uncertain replacement was committed.
    virtual bool recheck() = 0;
    //! Ends a review that has not been committed; no effect after the commit.
    virtual void discard() = 0;
    //! Configuration the wallet forwards to every attempt.
    virtual void setDisplayUnit(int) {}
    virtual void retryPublications() {}
    virtual void close() {}

Q_SIGNALS:
    void stateChanged();
    void resultChanged();
    void needsUnlockChanged();

protected:
    using QObject::QObject;
};

#endif // BITCOIN_QML_MODELS_FEEBUMPOPERATION_H
