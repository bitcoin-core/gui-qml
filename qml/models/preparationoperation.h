// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PREPARATIONOPERATION_H
#define BITCOIN_QML_MODELS_PREPARATIONOPERATION_H

#include <QObject>
#include <QString>

/** One request to prepare the send draft for review. A locked wallet asks
 * for its password first. Once finished it no longer changes. */
class PreparationOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(PreparationOperation::State state READ state NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    //! None is never an operation's state: a controller reports it when it
    //! has no operation. Ready means the wallet holds the review.
    enum class State { None, Pending, NeedsUnlock, Ready, Stale, Failed, Cancelled };
    Q_ENUM(State)

    State state() const { return m_state; }
    bool pending() const { return m_state == State::Pending; }
    bool finished() const { return m_state != State::None && m_state != State::Pending && m_state != State::NeedsUnlock; }
    QString error() const { return m_error; }
    //! True once the wallet asked for its password, until the preparation is
    //! ready or dropped; a failure after a password keeps the prompt.
    bool passwordRequested() const
    {
        return m_password_requested && m_state != State::Ready && m_state != State::Stale && m_state != State::Cancelled;
    }

    //! Answers the password prompt and prepares again.
    virtual void unlock(const QString& passphrase) = 0;
    //! Stops following the preparation. Work that already started finishes.
    virtual void cancel() = 0;

Q_SIGNALS:
    void changed();

protected:
    explicit PreparationOperation(QObject* parent) : QObject(parent) {}
    //! A preparation that starts with a password answers a prompt.
    void markPasswordRequested() { m_password_requested = true; }
    //! Publishes where the preparation is; a final state is kept.
    void publish(State state, QString error = {})
    {
        if (finished() || state == State::None) return;
        if (state == State::NeedsUnlock) m_password_requested = true;
        m_state = state;
        m_error = std::move(error);
        Q_EMIT changed();
    }

private:
    State m_state{State::Pending};
    bool m_password_requested{false};
    QString m_error;
};

#endif // BITCOIN_QML_MODELS_PREPARATIONOPERATION_H
