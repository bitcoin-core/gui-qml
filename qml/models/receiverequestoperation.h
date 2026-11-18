// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RECEIVEREQUESTOPERATION_H
#define BITCOIN_QML_MODELS_RECEIVEREQUESTOPERATION_H

#include <qml/walletrequests.h>

#include <QObject>

/** Presentation of one command's retained outcome. This object owns neither a
 * wallet nor an Action lease. Losing its view cannot cancel or retarget a write.
 * QML owns returned objects; native callers own and delete their projections. */
class ReceiveRequestOperation : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool requestSaved READ requestSaved NOTIFY changed)
    Q_PROPERTY(bool requestLoaded READ requestLoaded NOTIFY changed)
    Q_PROPERTY(bool erased READ erased NOTIFY changed)
    Q_PROPERTY(bool labelSaved READ labelSaved NOTIFY changed)
    Q_PROPERTY(QString requestId READ requestId NOTIFY changed)
    Q_PROPERTY(Error error READ error NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
    Q_PROPERTY(bool addressReady READ addressReady NOTIFY changed)
    Q_PROPERTY(QString addressToken READ addressToken NOTIFY changed)
    Q_PROPERTY(quint64 viewRevision READ viewRevision NOTIFY changed)
public:
    enum class Error { None, Loading, Checking, Invalid, Missing, Changed, Paid, IdExhausted, NeedsUnlock,
                       Rejected, Closed, Persistence, LabelPersistence, Partial, Uncertain, Unsupported, AlreadySaved,
                       IncorrectPassphrase, AddressCreation, ReservationPersistence, PreferencePersistence };
    Q_ENUM(Error)
    explicit ReceiveRequestOperation(QObject* parent = nullptr) : QObject(parent) {}
    void start(const std::shared_ptr<walletrequests::Store>& store, walletrequests::Command command,
               const std::shared_ptr<QObject>& bridge);
    bool pending() const { return !m_result; }
    bool requestSaved() const { return m_result && m_result->request_saved; }
    bool requestLoaded() const { return m_result && m_result->request_loaded; }
    bool erased() const { return m_result && m_result->erased; }
    bool labelSaved() const { return m_result && m_result->label_saved; }
    QString requestId() const { return m_result && m_result->id > 0 ? QString::number(m_result->id) : QString{}; }
    Error error() const {
        // Compatibility consumers keep their prompt category. The typed
        // receive owner exposes the exact locked-attempt failure separately.
        return m_result && m_result->error == walletrequests::Error::LockedAttemptFailed ? Error::NeedsUnlock
            : m_result ? static_cast<Error>(m_result->error) : Error::None;
    }
    QString errorText() const;
    bool addressReady() const { return m_result && m_result->address && m_result->address->persisted && (error() == Error::None || error() == Error::PreferencePersistence); }
    QString addressToken() const { return m_address_intent ? QString::number(m_address_intent->token) : QString{}; }
    quint64 viewRevision() const { return m_address_intent ? m_address_intent->view_revision : 0; }
    std::shared_ptr<const walletrequests::AddressIntent> addressIntent() const { return m_address_intent; }
    static QString ErrorText(walletrequests::Error error, bool request_saved = false);
    const std::optional<walletrequests::Result>& result() const { return m_result; }
Q_SIGNALS:
    void changed();
private:
    void consume();
    bool m_started{false};
    std::shared_ptr<walletrequests::Operation> m_operation;
    std::shared_ptr<const walletrequests::AddressIntent> m_address_intent;
    std::optional<walletrequests::Result> m_result;
};

#endif // BITCOIN_QML_MODELS_RECEIVEREQUESTOPERATION_H
