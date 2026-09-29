// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/paymentrequest.h>

#include <qml/models/receiverequesthistorymodel.h>

#include <key_io.h>

#include <QString>

PaymentRequest::PaymentRequest(QObject* parent)
    : QObject(parent)
{
    m_amount = new BitcoinAmount(this);
    connect(m_amount, &BitcoinAmount::amountChanged, this, &PaymentRequest::qrPayloadChanged);
    connect(this, &PaymentRequest::addressChanged, this, &PaymentRequest::qrPayloadChanged);
    connect(this, &PaymentRequest::labelChanged, this, &PaymentRequest::qrPayloadChanged);
    connect(this, &PaymentRequest::messageChanged, this, &PaymentRequest::qrPayloadChanged);
}

QString PaymentRequest::address() const
{
    return QString::fromStdString(EncodeDestination(m_destination));
}

QString PaymentRequest::addressFormatted() const
{
    return FormatAddress(address());
}

QString PaymentRequest::addressType() const
{
    const QString derived_type{std::visit([](const auto& dest) -> QString {
        using T = std::decay_t<decltype(dest)>;
        if constexpr (std::is_same_v<T, PKHash>) {
            return QStringLiteral("Legacy");
        } else if constexpr (std::is_same_v<T, ScriptHash>) {
            return QStringLiteral("P2SH-SegWit");
        } else if constexpr (std::is_same_v<T, WitnessV0KeyHash> || std::is_same_v<T, WitnessV0ScriptHash>) {
            return QStringLiteral("Bech32");
        } else if constexpr (std::is_same_v<T, WitnessV1Taproot>) {
            return QStringLiteral("Bech32m");
        } else {
            return {};
        }
    }, m_destination)};
    return derived_type.isEmpty() ? m_address_type : derived_type;
}

QString PaymentRequest::label() const
{
    return m_label;
}

void PaymentRequest::setLabel(const QString& label)
{
    if (m_label == label) {
        return;
    }
    m_label = label;
    Q_EMIT labelChanged();
}

QString PaymentRequest::message() const
{
    return m_message;
}

void PaymentRequest::setMessage(const QString& message)
{
    if (m_message == message) {
        return;
    }
    m_message = message;
    Q_EMIT messageChanged();
}

void PaymentRequest::setAddressType(const QString& address_type)
{
    if (m_address_type == address_type) {
        return;
    }
    m_address_type = address_type;
    Q_EMIT addressTypeChanged();
}

QString PaymentRequest::noteSelf() const
{
    return m_noteSelf;
}

void PaymentRequest::setNoteSelf(const QString& note)
{
    if (m_noteSelf == note) {
        return;
    }
    m_noteSelf = note;
    Q_EMIT noteSelfChanged();
}

BitcoinAmount* PaymentRequest::amount() const
{
    return m_amount;
}

QString PaymentRequest::amountError() const
{
    return m_amountError;
}

void PaymentRequest::setAmountError(const QString& error)
{
    if (m_amountError == error) {
        return;
    }
    m_amountError = error;
    Q_EMIT amountErrorChanged();
}

QString PaymentRequest::id() const
{
    return m_id;
}

void PaymentRequest::setId(unsigned int id)
{
    const QString new_id = QString::number(id);
    if (m_id == new_id) {
        return;
    }
    m_id = new_id;
    Q_EMIT idChanged();
}

bool PaymentRequest::needsUnlock() const
{
    return m_needs_unlock;
}

void PaymentRequest::setNeedsUnlock(bool needs_unlock)
{
    if (m_needs_unlock == needs_unlock) {
        return;
    }
    m_needs_unlock = needs_unlock;
    Q_EMIT needsUnlockChanged();
}

QString PaymentRequest::unlockError() const
{
    return m_unlock_error;
}

void PaymentRequest::setUnlockError(const QString& error)
{
    if (m_unlock_error == error) {
        return;
    }
    m_unlock_error = error;
    Q_EMIT unlockErrorChanged();
}

void PaymentRequest::setDestination(const CTxDestination& destination)
{
    m_destination = destination;
    Q_EMIT addressChanged();
    Q_EMIT addressTypeChanged();
}

CTxDestination PaymentRequest::destination() const
{
    return m_destination;
}

QString PaymentRequest::qrPayload() const
{
    if (m_payment_received) return {};
    const QString addr = address();
    if (addr.isEmpty()) return {};
    return ReceiveRequestHistoryModel::BuildBitcoinUri(addr, m_amount->satoshi(), m_label, m_message);
}

QString PaymentRequest::createdIso() const
{
    return m_created.isValid() ? m_created.toString(Qt::ISODate) : QString();
}

QDateTime PaymentRequest::created() const
{
    return m_created;
}

void PaymentRequest::setCreated(const QDateTime& dt)
{
    if (m_created == dt) return;
    m_created = dt;
    Q_EMIT createdIsoChanged();
}

bool PaymentRequest::hasPaymentInfo() const
{
    return !m_label.isEmpty() || !m_message.isEmpty() || m_amount->satoshi() > 0;
}

bool PaymentRequest::isEditing() const
{
    return m_is_editing;
}

void PaymentRequest::setIsEditing(bool editing)
{
    if (m_is_editing == editing) return;
    m_is_editing = editing;
    Q_EMIT isEditingChanged();
}

void PaymentRequest::clear()
{
    m_payment_received = false;
    setReceivedAmountSatoshi(0);
    m_destination = CNoDestination();
    m_label.clear();
    m_message.clear();
    m_address_type.clear();
    m_noteSelf.clear();
    m_amount->clear();
    m_amountError.clear();
    m_id.clear();
    m_needs_unlock = false;
    m_unlock_error.clear();
    m_created = QDateTime();
    m_is_editing = true;
    Q_EMIT addressChanged();
    Q_EMIT labelChanged();
    Q_EMIT messageChanged();
    Q_EMIT addressTypeChanged();
    Q_EMIT noteSelfChanged();
    Q_EMIT amountErrorChanged();
    Q_EMIT idChanged();
    Q_EMIT needsUnlockChanged();
    Q_EMIT unlockErrorChanged();
    Q_EMIT createdIsoChanged();
    Q_EMIT isEditingChanged();
    Q_EMIT paymentReceivedChanged();
}

void PaymentRequest::setPaymentReceived(bool received)
{
    if (m_payment_received == received) return;
    m_payment_received = received;
    Q_EMIT paymentReceivedChanged();
    Q_EMIT qrPayloadChanged();
}

void PaymentRequest::setReceivedAmountSatoshi(qint64 amount)
{
    if (m_received_amount == amount) return;
    m_received_amount = amount;
    Q_EMIT receivedAmountChanged();
}

void PaymentRequest::edit()
{
    setIsEditing(true);
}

QString PaymentRequest::FormatAddress(const QString& address)
{
    QString formatted;
    formatted.reserve(address.length() + address.length() / 4);
    for (int i = 0; i < address.length(); ++i) {
        if (i > 0 && (i % 4) == 0) {
            formatted += QChar(' ');
        }
        formatted += address[i];
    }
    return formatted;
}
