// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PAYMENTREQUEST_H
#define BITCOIN_QML_MODELS_PAYMENTREQUEST_H

#include <qml/bitcoinamount.h>

#include <addresstype.h>

#include <QDateTime>
#include <QObject>
#include <QString>
#include <functional>

namespace walletrequests { struct PayloadInput; }

class PaymentRequest : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString address READ address NOTIFY addressChanged)
    Q_PROPERTY(QString addressFormatted READ addressFormatted NOTIFY addressChanged)
    Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
    Q_PROPERTY(QString message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(QString addressType READ addressType WRITE setAddressType NOTIFY addressTypeChanged)
    Q_PROPERTY(QString noteSelf READ noteSelf WRITE setNoteSelf NOTIFY noteSelfChanged)
    Q_PROPERTY(BitcoinAmount* amount READ amount CONSTANT)
    Q_PROPERTY(QString amountError READ amountError NOTIFY amountErrorChanged)
    Q_PROPERTY(QString id READ id NOTIFY idChanged)
    Q_PROPERTY(bool needsUnlock READ needsUnlock NOTIFY needsUnlockChanged)
    Q_PROPERTY(QString unlockError READ unlockError NOTIFY unlockErrorChanged)
    Q_PROPERTY(QString qrPayload READ qrPayload NOTIFY qrPayloadChanged)
    Q_PROPERTY(QString createdIso READ createdIso NOTIFY createdIsoChanged)
    Q_PROPERTY(bool hasPaymentInfo READ hasPaymentInfo NOTIFY qrPayloadChanged)
    Q_PROPERTY(bool isEditing READ isEditing WRITE setIsEditing NOTIFY isEditingChanged)
    Q_PROPERTY(bool paymentReceived READ paymentReceived NOTIFY paymentReceivedChanged)
    Q_PROPERTY(qint64 receivedAmountSatoshi READ receivedAmountSatoshi NOTIFY receivedAmountChanged)
    Q_PROPERTY(quint64 generation READ generation NOTIFY generationChanged)
    Q_PROPERTY(quint64 revision READ revision NOTIFY draftChanged)

public:
    explicit PaymentRequest(QObject* parent = nullptr);

    QString address() const;
    QString addressFormatted() const;

    QString label() const;
    void setLabel(const QString& label);

    QString message() const;
    void setMessage(const QString& message);

    QString addressType() const;
    void setAddressType(const QString& address_type);

    QString noteSelf() const;
    void setNoteSelf(const QString& note);
    void setNoteSelfBaseline(const QString& note, bool address_prefill = false);
    bool notePrefilled() const { return m_note_prefilled; }
    bool noteDirty() const { return m_noteSelf != m_note_baseline; }

    BitcoinAmount* amount() const;
    QString amountError() const;
    void setAmountError(const QString& error);

    QString id() const;
    void setId(qint64 id);

    bool needsUnlock() const;
    void setNeedsUnlock(bool needs_unlock);

    QString unlockError() const;
    void setUnlockError(const QString& error);

    void setDestination(const CTxDestination& destination);
    void setPreparedDestination(const CTxDestination& destination, QString address);
    CTxDestination destination() const;
    uint64_t revision() const { return m_revision; }
    quint64 generation() const { return m_generation; }

    QString qrPayload() const;
    // A configured wallet view prepares payload text on its injected Domain
    // port and delivers this callback on GUI. Standalone value tests can use
    // the pure formatting helper without a wallet/executor.
    using PayloadPreparer = std::function<void(walletrequests::PayloadInput, std::function<void(QString)>)>;
    void setPayloadPreparer(PayloadPreparer prepare);
    void setPreparedPayload(QString payload);
    void setStoredRevision(uint64_t revision);
    bool matchesStoredRevision(uint64_t revision) const;

    QString createdIso() const;
    QDateTime created() const;
    void setCreated(const QDateTime& dt);
    bool hasPaymentInfo() const;

    bool isEditing() const;
    void setIsEditing(bool editing);
    bool paymentReceived() const { return m_payment_received; }
    void setPaymentReceived(bool received);
    qint64 receivedAmountSatoshi() const { return m_received_amount; }
    void setReceivedAmountSatoshi(qint64 amount);

    Q_INVOKABLE void clear();
    Q_INVOKABLE void edit();

Q_SIGNALS:
    void addressChanged();
    void labelChanged();
    void messageChanged();
    void addressTypeChanged();
    void noteSelfChanged();
    void amountErrorChanged();
    void idChanged();
    void needsUnlockChanged();
    void unlockErrorChanged();
    void qrPayloadChanged();
    void createdIsoChanged();
    void isEditingChanged();
    void paymentReceivedChanged();
    void receivedAmountChanged();
    void generationChanged();
    void draftChanged();

private:
    static QString FormatAddress(const QString& address);
    void refreshPayload();
    void preparePayload();

    CTxDestination m_destination;
    QString m_address;
    uint64_t m_revision{0};
    quint64 m_generation{0};
    QString m_label;
    QString m_message;
    QString m_address_type;
    QString m_noteSelf;
    QString m_note_baseline;
    bool m_note_prefilled{false};
    QString m_amountError;
    BitcoinAmount* m_amount;
    QString m_id;
    bool m_needs_unlock{false};
    QString m_unlock_error;
    QDateTime m_created;
    bool m_is_editing{true};
    bool m_payment_received{false};
    qint64 m_received_amount{0};
    PayloadPreparer m_prepare_payload;
    QString m_prepared_payload;
    uint64_t m_payload_revision{0};
    bool m_payload_pending{false}, m_payload_current{false};
    uint64_t m_stored_revision{0}, m_stored_edit_revision{0}, m_stored_payload_revision{0};
};

#endif // BITCOIN_QML_MODELS_PAYMENTREQUEST_H
