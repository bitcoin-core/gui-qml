// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/paymentrequest.h>

#include <qml/walletrequestdata.h>

#include <key_io.h>

#include <QString>
#include <QPointer>

PaymentRequest::PaymentRequest(QObject* parent)
    : QObject(parent)
{
    m_amount = new BitcoinAmount(this);
    connect(m_amount, &BitcoinAmount::amountChanged, this, &PaymentRequest::refreshPayload);
    connect(this, &PaymentRequest::addressChanged, this, &PaymentRequest::refreshPayload);
    connect(this, &PaymentRequest::labelChanged, this, &PaymentRequest::refreshPayload);
    connect(this, &PaymentRequest::messageChanged, this, &PaymentRequest::refreshPayload);
    const auto changed = [this] { ++m_revision; Q_EMIT draftChanged(); };
    connect(m_amount, &BitcoinAmount::amountChanged, this, changed);
    connect(this, &PaymentRequest::addressChanged, this, changed);
    connect(this, &PaymentRequest::labelChanged, this, changed);
    connect(this, &PaymentRequest::messageChanged, this, changed);
    connect(this, &PaymentRequest::noteSelfChanged, this, changed);
    connect(this, &PaymentRequest::addressTypeChanged, this, changed);
    connect(this, &PaymentRequest::idChanged, this, changed);
}

QString PaymentRequest::address() const
{
    return m_address;
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

void PaymentRequest::setNoteSelfBaseline(const QString& note, bool address_prefill)
{
    m_note_baseline = note; m_note_prefilled = address_prefill;
    setNoteSelf(note);
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

void PaymentRequest::setId(qint64 id)
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
    setPreparedDestination(destination, QString::fromStdString(EncodeDestination(destination)));
}

void PaymentRequest::setPreparedDestination(const CTxDestination& destination, QString address)
{
    if (m_destination == destination && m_address == address) return;
    m_destination = destination;
    m_address = std::move(address);
    const QPointer<PaymentRequest> observer{this};
    Q_EMIT addressChanged();
    if (observer) Q_EMIT addressTypeChanged();
}

CTxDestination PaymentRequest::destination() const
{
    return m_destination;
}

QString PaymentRequest::qrPayload() const
{
    if (m_payment_received || m_address.isEmpty()) return {};
    if (m_prepare_payload) return m_prepared_payload;
    const QString addr = address();
    if (addr.isEmpty()) return {};
    return walletrequests::BuildBitcoinUri(addr, m_amount->satoshi(), m_label, m_message);
}

void PaymentRequest::setPayloadPreparer(PayloadPreparer prepare)
{
    m_prepare_payload = std::move(prepare);
    refreshPayload();
}

void PaymentRequest::setPreparedPayload(QString payload)
{
    if (payload.isEmpty() && m_prepare_payload && !m_payment_received) { refreshPayload(); return; }
    ++m_payload_revision; // Retire any in-flight formatter of earlier fields.
    m_prepared_payload = std::move(payload);
    m_payload_current = true;
    Q_EMIT qrPayloadChanged();
}
void PaymentRequest::setStoredRevision(uint64_t revision)
{
    m_stored_revision = revision; m_stored_edit_revision = m_revision; m_stored_payload_revision = m_payload_revision;
}
bool PaymentRequest::matchesStoredRevision(uint64_t revision) const
{
    return revision != 0 && m_stored_revision == revision && m_stored_edit_revision == m_revision && m_stored_payload_revision == m_payload_revision;
}

void PaymentRequest::refreshPayload()
{
    ++m_payload_revision;
    m_prepared_payload.clear();
    m_payload_current = false;
    const QPointer<PaymentRequest> observer{this};
    preparePayload();
    if (observer) Q_EMIT qrPayloadChanged();
}

void PaymentRequest::preparePayload()
{
    if (!m_prepare_payload || m_payload_current || m_payload_pending || m_address.isEmpty() || m_payment_received) return;
    m_payload_pending = true;
    const auto revision = m_payload_revision;
    const QPointer<PaymentRequest> observer{this};
    m_prepare_payload({m_address, m_label, m_message, m_amount->satoshi()}, [observer, revision](QString payload) {
        if (!observer) return;
        observer->m_payload_pending = false;
        if (observer->m_payload_revision != revision) { observer->preparePayload(); return; }
        observer->m_prepared_payload = std::move(payload);
        observer->m_payload_current = true;
        Q_EMIT observer->qrPayloadChanged();
    });
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
    const QPointer<PaymentRequest> observer{this};
    const auto generation = ++m_generation;
    const auto current = [&] { return observer && observer->m_generation == generation; };
    const bool received_changed = m_received_amount != 0;
    ++m_payload_revision; m_prepared_payload.clear(); m_payload_current = false;
    m_received_amount = 0;
    m_payment_received = false;
    m_destination = CNoDestination();
    m_address.clear();
    m_label.clear();
    m_message.clear();
    m_address_type.clear();
    m_noteSelf.clear();
    m_note_baseline.clear(); m_note_prefilled = false;
    m_amountError.clear();
    m_id.clear();
    m_needs_unlock = false;
    m_unlock_error.clear();
    m_created = QDateTime();
    m_is_editing = true;
    // Every property is coherent before a child or parent signal can reenter.
    m_amount->clear();
    if (!current()) return;
    if (received_changed) Q_EMIT receivedAmountChanged();
    if (!current()) return;
    Q_EMIT generationChanged();
    if (!current()) return;
    Q_EMIT addressChanged();
    if (!current()) return;
    Q_EMIT labelChanged();
    if (!current()) return;
    Q_EMIT messageChanged();
    if (!current()) return;
    Q_EMIT addressTypeChanged();
    if (!current()) return;
    Q_EMIT noteSelfChanged();
    if (!current()) return;
    Q_EMIT amountErrorChanged();
    if (!current()) return;
    Q_EMIT idChanged();
    if (!current()) return;
    Q_EMIT needsUnlockChanged();
    if (!current()) return;
    Q_EMIT unlockErrorChanged();
    if (!current()) return;
    Q_EMIT createdIsoChanged();
    if (!current()) return;
    Q_EMIT isEditingChanged();
    if (!current()) return;
    Q_EMIT paymentReceivedChanged();
}

void PaymentRequest::setPaymentReceived(bool received)
{
    if (m_payment_received == received) return;
    m_payment_received = received;
    const QPointer<PaymentRequest> observer{this};
    Q_EMIT paymentReceivedChanged();
    if (observer) refreshPayload();
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
