// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_MESSAGESIGNINGPROMPT_H
#define BITCOIN_QML_MODELS_MESSAGESIGNINGPROMPT_H
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUuid>
#include <functional>

class MessageSigningError {
    Q_GADGET
    Q_PROPERTY(Code code MEMBER code)
    Q_PROPERTY(QString message MEMBER message)
public:
    enum Code { None, InvalidAddress, NeedsUnlock, IncorrectPassword, Signing, RelockFailed, Uncertain, Refused, Closed };
    Q_ENUM(Code)
    Code code{None};
    QString message;
};
Q_DECLARE_METATYPE(MessageSigningError)
class MessageVerificationSnapshot {
    Q_GADGET
    Q_PROPERTY(State state MEMBER state)
    Q_PROPERTY(QString operationId MEMBER operationId)
    Q_PROPERTY(bool outcomeKnown MEMBER outcomeKnown)
    Q_PROPERTY(bool valid MEMBER valid)
public:
    enum State { Empty, Pending, Valid, Invalid, Failed, Closed };
    Q_ENUM(State)
    State state{Empty};
    QString operationId;
    bool outcomeKnown{false}, valid{false};
};
Q_DECLARE_METATYPE(MessageVerificationSnapshot)
/** Original-attempt presentation capability. Owns no Wallet, action or secret.
 * Its callback checks the captured model/input identity on admission. */
class MessageSigningPrompt final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString promptId READ promptId CONSTANT)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    using Submit = std::function<quint64(const QString&)>;
    using Cancel = std::function<void()>;
    MessageSigningPrompt(Submit submit, Cancel cancel, QObject* parent)
        : QObject(parent), m_submit(std::move(submit)), m_cancel(std::move(cancel)) {}
    QString promptId() const { return m_id; }
    bool pending() const { return m_pending; }
    bool busy() const { return m_busy; }
    QString error() const { return m_error; }
    Q_INVOKABLE QString submit(const QString& passphrase) {
        if (!m_pending || m_busy) return {};
        const auto submit = m_submit;
        const auto id = submit ? submit(passphrase) : 0;
        return id ? QString::number(id) : QString{};
    }
    Q_INVOKABLE bool cancel() { if (!m_pending || m_busy) return false; retire(); return true; }
    Q_INVOKABLE void retire() {
        if (!m_pending) return;
        const auto cancel = m_cancel;
        m_pending = false; m_busy = false;
        const QPointer<MessageSigningPrompt> guard{this};
        Q_EMIT changed();
        if (guard && cancel) cancel();
    }
    void project(bool pending, bool busy, const QString& error) {
        if (m_pending == pending && m_busy == busy && m_error == error) return;
        m_pending = pending; m_busy = busy; m_error = error; Q_EMIT changed();
    }
Q_SIGNALS:
    void changed();
private:
    const QString m_id{QUuid::createUuid().toString(QUuid::WithoutBraces)};
    const Submit m_submit;
    const Cancel m_cancel;
    bool m_pending{true}, m_busy{false};
    QString m_error;
};
#endif // BITCOIN_QML_MODELS_MESSAGESIGNINGPROMPT_H
