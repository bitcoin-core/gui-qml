// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinamount.h>
#include <qml/models/paymentrequest.h>

#include <addresstype.h>
#include <key_io.h>

PaymentRequest::PaymentRequest(QObject* parent)
    : QObject(parent)
{
    m_amount = new BitcoinAmount(this);
    m_label = "";
    m_message = "";
    m_id = "";
}

QString PaymentRequest::address() const
{
    return QString::fromStdString(EncodeDestination(m_destination));
}

QString PaymentRequest::label() const
{
    return m_label;
}

void PaymentRequest::setLabel(const QString& label)
{
    if (m_label == label)
        return;

    m_label = label;
    Q_EMIT labelChanged();
}

QString PaymentRequest::message() const
{
    return m_message;
}

void PaymentRequest::setMessage(const QString& message)
{
    if (m_message == message)
        return;

    m_message = message;
    Q_EMIT messageChanged();
}

BitcoinAmount* PaymentRequest::amount() const
{
    return m_amount;
}

QString PaymentRequest::id() const
{
    return m_id;
}

void PaymentRequest::setId(const unsigned int id)
{
    m_id = QString::number(id);
    Q_EMIT idChanged();
}

void PaymentRequest::setDestination(const CTxDestination& destination)
{
    m_destination = destination;
    Q_EMIT addressChanged();
}

CTxDestination PaymentRequest::destination() const
{
    return m_destination;
}

void PaymentRequest::setAmountError(const QString& error)
{
    if (m_amountError == error)
        return;

    m_amountError = error;
    Q_EMIT amountErrorChanged();
}

QString PaymentRequest::amountError() const
{
    return m_amountError;
}

void PaymentRequest::clear()
{
    m_destination = CNoDestination();
    m_label.clear();
    m_message.clear();
    m_amount->clear();
    m_amountError.clear();
    m_id.clear();
    Q_EMIT addressChanged();
    Q_EMIT labelChanged();
    Q_EMIT messageChanged();
    Q_EMIT amountErrorChanged();
    Q_EMIT idChanged();
}
