// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SECURITYOPERATION_H
#define BITCOIN_QML_MODELS_SECURITYOPERATION_H

#include <QObject>
#include <QString>

#include <utility>

/** Setting or changing a wallet password. It waits for the passwords, then
 * runs; once finished it no longer changes. */
class SecurityOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(SecurityOperation::Kind kind READ kind CONSTANT)
    Q_PROPERTY(SecurityOperation::Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(bool terminal READ terminal NOTIFY changed)
    Q_PROPERTY(bool outcomeKnown READ outcomeKnown NOTIFY changed)
    Q_PROPERTY(bool canSubmit READ canSubmit NOTIFY changed)
    Q_PROPERTY(bool canCancel READ canSubmit NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    enum class Kind { Encrypt, ChangePassphrase };
    Q_ENUM(Kind)
    enum class Phase { AwaitingCredentials, Pending, Succeeded, Failed, Uncertain, Cancelled };
    Q_ENUM(Phase)
    Kind kind() const { return m_kind; }
    Phase phase() const { return m_phase; }
    bool busy() const { return m_phase == Phase::Pending; }
    bool terminal() const { return m_phase != Phase::AwaitingCredentials && !busy(); }
    bool outcomeKnown() const { return terminal() && m_phase != Phase::Uncertain; }
    //! The operation still waits for its passwords.
    virtual bool canSubmit() const { return m_phase == Phase::AwaitingCredentials; }
    QString error() const { return m_error; }
    //! Submits the passwords this operation asks for.
    Q_INVOKABLE virtual bool submit(const QString& current, const QString& replacement, const QString& confirmation) = 0;
    //! Cancels an operation that still waits for its passwords.
    Q_INVOKABLE virtual bool cancel() = 0;
Q_SIGNALS:
    void changed();
protected:
    SecurityOperation(Kind kind, QObject* parent) : QObject(parent), m_kind(kind) {}
    //! Publishes where the operation is.
    void publish(Phase phase, QString error = {})
    {
        m_phase = phase;
        m_error = std::move(error);
        Q_EMIT changed();
    }
private:
    const Kind m_kind;
    Phase m_phase{Phase::AwaitingCredentials};
    QString m_error;
};

#endif // BITCOIN_QML_MODELS_SECURITYOPERATION_H
