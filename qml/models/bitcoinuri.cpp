// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinuri.h>

#include <common/paymentdestination.h>
#include <key_io.h>
#include <util/moneystr.h>

#include <QCoreApplication>
#include <QUrl>
#include <QUrlQuery>

namespace {
BitcoinUriParseResult BuildError(const QString& message)
{
    BitcoinUriParseResult result;
    result.success = false;
    result.error = message;
    return result;
}

struct Tr {
    Q_DECLARE_TR_FUNCTIONS(BitcoinUri)
};
} // namespace

BitcoinUriParseResult BitcoinUri::Parse(const QString& uri_text)
{
    const QString raw = uri_text.trimmed();
    if (raw.isEmpty()) {
        return BuildError(Tr::tr("Enter a Bitcoin payment URI."));
    }

    if (raw.startsWith(QStringLiteral("bitcoin://"), Qt::CaseInsensitive)) {
        return BuildError(Tr::tr("'bitcoin://' is not a valid URI. Use 'bitcoin:' instead."));
    }

    const QUrl uri(raw);
    // QUrl normalises the scheme to lowercase (RFC 3986 §3.1), so a plain
    // equality check is sufficient and correctly accepts BITCOIN:, Bitcoin:, etc.
    if (!uri.isValid() || uri.scheme() != QStringLiteral("bitcoin")) {
        return BuildError(Tr::tr("Not a valid Bitcoin payment URI."));
    }

    const QUrlQuery query(uri);
    const auto items = query.queryItems(QUrl::FullyDecoded);
    QString silent_payment_address;
    for (const auto& item : items) {
        if (item.first.compare(QLatin1String("sp"), Qt::CaseInsensitive) == 0 ||
            item.first.compare(QLatin1String("req-sp"), Qt::CaseInsensitive) == 0) {
            if (!silent_payment_address.isEmpty()) {
                // TODO: Select between multiple SP payment instructions.
                //: Error when a payment URI contains multiple alternative Silent Payments instructions.
                return BuildError(Tr::tr("Multiple silent payment instructions are not supported yet."));
            }
            const auto destination = PaymentDestination::FromString(item.second.toStdString());
            if (!destination || !destination->IsSilentPayment()) {
                //: Error when a payment URI's sp parameter is not a Silent Payments address for the current network.
                return BuildError(Tr::tr("Not a valid silent payment address."));
            }
            silent_payment_address = item.second;
        }
    }

    BitcoinUriParseResult result;
    result.address = uri.path();
    if (result.address.endsWith('/')) {
        result.address.chop(1);
    }

    const CTxDestination destination = DecodeDestination(result.address.toStdString());
    if ((!result.address.isEmpty() || silent_payment_address.isEmpty()) && !IsValidDestination(destination)) {
        return BuildError(Tr::tr("Not a valid Bitcoin address."));
    }
    if (!silent_payment_address.isEmpty()) result.address = silent_payment_address;
    result.is_silent_payment = !silent_payment_address.isEmpty();

    // TODO: Reject duplicate amount, label and message parameters per BIP321.
    for (const auto& item : items) {
        QString key = item.first.toLower();
        const QString value = item.second;

        bool required = false;
        if (key.startsWith(QStringLiteral("req-"))) {
            required = true;
            key.remove(0, 4);
        }

        bool handled = key == QLatin1String("sp");
        if (key == QLatin1String("label")) {
            result.label = value;
            result.has_label = true;
            handled = true;
        } else if (key == QLatin1String("message")) {
            result.message = value;
            result.has_message = true;
            handled = true;
        } else if (key == QLatin1String("amount")) {
            if (!value.trimmed().isEmpty()) {
                const auto amount_sats = ParseMoney(value.toStdString());
                if (!amount_sats.has_value()) {
                    return BuildError(Tr::tr("Invalid Bitcoin amount."));
                }
                result.amount_sats = *amount_sats;
                result.has_amount = true;
            }
            handled = true;
        }

        if (required && !handled) {
            return BuildError(Tr::tr("Unsupported required parameter: %1").arg(key));
        }
    }

    result.success = true;
    return result;
}
