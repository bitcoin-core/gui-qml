// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendrecipient.h>

#include <qml/bitcoinamount.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/walletqmlmodel.h>

#include <key_io.h>
#include <policy/feerate.h>
#include <policy/policy.h>
#include <script/script.h>

SendRecipient::SendRecipient(WalletQmlModel* wallet, QObject* parent)
    : QObject(parent), m_wallet(wallet), m_address(new BitcoinAddress(this)), m_amount(new BitcoinAmount(this))
{
    connect(m_amount, &BitcoinAmount::amountChanged, this, &SendRecipient::validateAmount);
    connect(m_address, &BitcoinAddress::formattedAddressChanged, this, &SendRecipient::validateAddress);
}

BitcoinAddress* SendRecipient::address() const
{
    return m_address;
}

void SendRecipient::setAddress(const QString& address)
{
    if (m_address->address() != address) {
        m_address->setAddress(address, 0);
        Q_EMIT addressChanged();
        validateAddress();
    }
}

QString SendRecipient::addressError() const
{
    return m_addressError;
}

void SendRecipient::setAddressError(const QString& error)
{
    if (m_addressError != error) {
        m_addressError = error;
        Q_EMIT addressErrorChanged();
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

BitcoinAmount* SendRecipient::amount() const
{
    return m_amount;
}

QString SendRecipient::amountError() const
{
    return m_amountError;
}

void SendRecipient::setAmountError(const QString& error)
{
    if (m_amountError != error) {
        m_amountError = error;
        Q_EMIT amountErrorChanged();
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

void SendRecipient::setSubtractFeeFromAmount(bool subtract)
{
    if (m_subtractFeeFromAmount != subtract) {
        m_subtractFeeFromAmount = subtract;
        Q_EMIT subtractFeeFromAmountChanged();
    }
}

CAmount SendRecipient::cAmount() const
{
    return m_amount->satoshi();
}

bool SendRecipient::isDataOutput() const
{
    return m_isDataOutput;
}

QString SendRecipient::dataHex() const
{
    return m_dataHex;
}

void SendRecipient::setDataOutput(const QString& hex)
{
    if (!m_isDataOutput) {
        m_isDataOutput = true;
        Q_EMIT isDataOutputChanged();
    }
    if (m_dataHex != hex) {
        m_dataHex = hex;
        Q_EMIT dataHexChanged();
    }
    setAddressError("");
    setAmountError("");
    Q_EMIT isValidChanged();
}

void SendRecipient::clear()
{
    m_label = "";
    m_message = "";
    setSubtractFeeFromAmount(false);
    m_address->setAddress("", 0);
    m_amount->clear();
    if (m_isDataOutput) {
        m_isDataOutput = false;
        Q_EMIT isDataOutputChanged();
    }
    if (!m_dataHex.isEmpty()) {
        m_dataHex = "";
        Q_EMIT dataHexChanged();
    }
    Q_EMIT addressChanged();
    Q_EMIT labelChanged();
    Q_EMIT messageChanged();
}

void SendRecipient::validateAddress()
{
    if (!m_address->isEmpty() && !IsValidDestinationString(m_address->address().toStdString())) {
        if (IsValidDestinationString(m_address->address().toStdString(), *CChainParams::Main())) {
            setAddressError(tr("Address is valid for mainnet, not the current network"));
        } else if (IsValidDestinationString(m_address->address().toStdString(), *CChainParams::TestNet())) {
            setAddressError(tr("Address is valid for testnet, not the current network"));
        } else {
            setAddressError(tr("Invalid address format"));
        }
    } else {
        setAddressError("");
    }

    validateAmount();
}

void SendRecipient::validateAmount()
{
    if (m_isDataOutput) {
        setAmountError("");
        Q_EMIT isValidChanged();
        return;
    }
    if (m_amount->isSet()) {
        if (m_amount->satoshi() <= 0) {
            setAmountError(tr("Amount must be greater than zero"));
        } else if (m_amount->satoshi() > MAX_MONEY) {
            setAmountError(tr("Amount exceeds maximum limit of 21,000,000 BTC"));
        } else if (!m_address->isEmpty() && m_addressError.isEmpty() &&
                   IsDust(CTxOut{m_amount->satoshi(), GetScriptForDestination(DecodeDestination(m_address->address().toStdString()))},
                          m_wallet ? m_wallet->dustRelayFee() : CFeeRate{DUST_RELAY_TX_FEE})) {
            setAmountError(tr("Amount is too small to send."));
        } else if (m_wallet && m_amount->satoshi() > m_wallet->balanceSatoshi()) {
            setAmountError(tr("Amount exceeds available balance"));
        } else {
            setAmountError("");
        }
    } else {
        setAmountError("");
    }

    Q_EMIT isValidChanged();
}

bool SendRecipient::isValid() const
{
    if (m_isDataOutput) return true;
    return m_addressError.isEmpty() && m_amountError.isEmpty() && m_amount->satoshi() > 0 && !m_address->isEmpty();
}
