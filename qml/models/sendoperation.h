// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDOPERATION_H
#define BITCOIN_QML_MODELS_SENDOPERATION_H

#include <QObject>
#include <QString>

/** One request to send, or only sign, a reviewed transaction. The wallet
 * starts it for one review; the TransactionReviewController that asked for it
 * owns it. Once it reaches a final state it no longer changes. Work past the
 * commit boundary finishes even if the operation is deleted. */
class SendOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SendOperation::State state READ state NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool finished READ finished NOTIFY changed)
    Q_PROPERTY(bool signOnly READ signOnly CONSTANT)
    //! True from the first password prompt until the operation finishes.
    Q_PROPERTY(bool passwordRequested READ passwordRequested NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString txid READ txid NOTIFY changed)
    //! WalletModel::SendSubmission and SendRecord values.
    Q_PROPERTY(int submission READ submission NOTIFY changed)
    Q_PROPERTY(int record READ record NOTIFY changed)

public:
    //! None is never an operation's state: a controller reports it when it
    //! has no operation.
    enum class State { None, Pending, NeedsConfirmation, NeedsUnlock, Committed, SignedOnly, PartiallySigned,
                       Unverified, Uncertain, Failed, Cancelled };
    Q_ENUM(State)

    State state() const { return m_state; }
    bool pending() const { return !finished() && m_state == State::Pending; }
    bool finished() const
    {
        return m_state != State::None && m_state != State::Pending && m_state != State::NeedsConfirmation && m_state != State::NeedsUnlock;
    }
    bool signOnly() const { return m_sign_only; }
    bool passwordRequested() const { return m_password_requested && !finished(); }
    QString error() const { return m_error; }
    QString txid() const { return m_txid; }
    int submission() const { return m_submission; }
    int record() const { return m_record; }

    //! Sends a transaction that spends the whole wallet, after the user agreed.
    Q_INVOKABLE virtual void confirm() = 0;
    Q_INVOKABLE virtual void unlock(const QString& passphrase) = 0;
    //! Answers a prompt with no. A submission that already started continues.
    Q_INVOKABLE virtual void cancel() = 0;

Q_SIGNALS:
    void changed();

protected:
    SendOperation(bool sign_only, QObject* parent) : QObject(parent), m_sign_only{sign_only} {}
    //! Publishes where the request is; a final state is kept.
    void publish(State state, QString error = {}, QString txid = {}, int submission = 0, int record = 0)
    {
        if (finished() || state == State::None) return;
        if (state == State::NeedsUnlock) m_password_requested = true;
        m_state = state;
        m_error = std::move(error);
        m_txid = std::move(txid);
        m_submission = submission;
        m_record = record;
        Q_EMIT changed();
    }

private:
    const bool m_sign_only;
    State m_state{State::Pending};
    bool m_password_requested{false};
    QString m_error, m_txid;
    int m_submission{0}, m_record{0};
};

#endif // BITCOIN_QML_MODELS_SENDOPERATION_H
