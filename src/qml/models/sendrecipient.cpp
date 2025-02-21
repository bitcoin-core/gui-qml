// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendrecipient.h>
#include <qobjectdefs.h>

SendRecipient::SendRecipient(QObject* parent)
    : QObject(parent)
    , m_address("")
    , m_label("")
    , m_amount("")
    , m_message("")
{
}

QString SendRecipient::address() const
{
    return m_address;
}

void SendRecipient::setAddress(const QString& address)
{
    if (m_address != address) {
        m_address = address;
        Q_EMIT addressChanged();
    }
}

QString SendRecipient::label() const
{
    return m_label;
}

void SendRecipient::setLabel(const QString& label)
{
    if (m_label != label) {
        m_label = label;
        Q_EMIT labelChanged();
    }
}

QString SendRecipient::amount() const
{
    return m_amount;
}

void SendRecipient::setAmount(const QString& amount)
{
    if (m_amount != amount) {
        m_amount = amount;
        Q_EMIT amountChanged();
    }
}

QString SendRecipient::message() const
{
    return m_message;
}

void SendRecipient::setMessage(const QString& message)
{
    if (m_message != message) {
        m_message = message;
        Q_EMIT messageChanged();
    }
}

bool SendRecipient::subtractFeeFromAmount() const
{
    return m_subtractFeeFromAmount;
}

CAmount SendRecipient::cAmount() const
{
    // TODO: Figure out who owns the parsing of SendRecipient::amount to CAmount
    return m_amount.toLongLong();
}

void SendRecipient::clear()
{
    m_address = "";
    m_label = "";
    m_amount = "";
    m_message = "";
    m_subtractFeeFromAmount = false;
    Q_EMIT addressChanged();
    Q_EMIT labelChanged();
    Q_EMIT amountChanged();
    Q_EMIT messageChanged();
}
