// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletreview.h>
#include <qml/bitcoinunits.h>
#include <qml/models/bitcoinaddress.h>
#include <QVariantMap>

QVariantList walletreview::FormatRecipients(const QList<Recipient>& recipients, int display_unit)
{
    QVariantList rows;
    rows.reserve(recipients.size());
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(display_unit);
    for (const auto& recipient : recipients) {
        const QString formatted_amount = QmlBitcoinUnits::formatForDisplay(unit, recipient.amount) + QLatin1Char(' ') + QmlBitcoinUnits::label(unit, recipient.amount);
        rows.append(QVariantMap{
            {QStringLiteral("address"), recipient.address},
            {QStringLiteral("formattedAddress"), BitcoinAddress::formattedAddress(recipient.address)},
            {QStringLiteral("label"), recipient.label},
            {QStringLiteral("amountSat"), qint64(recipient.amount)},
            {QStringLiteral("amount"), formatted_amount},
            {QStringLiteral("hasPaymentRequest"), recipient.payment_request},
        });
    }
    return rows;
}
