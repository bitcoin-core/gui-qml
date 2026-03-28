// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinuri.h>

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
    static QString tr(const char* s) { return QCoreApplication::translate("BitcoinUri", s); }
};
} // namespace

BitcoinUriParseResult BitcoinUri::Parse(const QString& uri_text)
{
    const QString raw = uri_text.trimmed();
    if (raw.isEmpty()) {
        return BuildError(Tr::tr("Enter a bitcoin: payment URI."));
    }

    if (raw.startsWith(QStringLiteral("bitcoin://"), Qt::CaseInsensitive)) {
        return BuildError(Tr::tr("'bitcoin://' is not a valid URI. Use 'bitcoin:' instead."));
    }

    const QUrl uri(raw);
    // QUrl normalises the scheme to lowercase (RFC 3986 §3.1), so a plain
    // equality check is sufficient and correctly accepts BITCOIN:, Bitcoin:, etc.
    if (!uri.isValid() || uri.scheme() != QStringLiteral("bitcoin")) {
        return BuildError(Tr::tr("URI cannot be parsed. Use a valid bitcoin: payment URI."));
    }

    BitcoinUriParseResult result;
    result.address = uri.path();
    if (result.address.endsWith('/')) {
        result.address.chop(1);
    }

    std::string decode_error;
    const CTxDestination destination = DecodeDestination(result.address.toStdString(), decode_error);
    if (!IsValidDestination(destination)) {
        const QString message = decode_error.empty()
            ? Tr::tr("URI cannot be parsed. Use a valid bitcoin: payment URI.")
            : QString::fromStdString(decode_error);
        return BuildError(message);
    }

    const QUrlQuery query(uri);
    const auto items = query.queryItems(QUrl::FullyDecoded);
    for (const auto& item : items) {
        QString key = item.first;
        const QString value = item.second;

        bool required = false;
        if (key.startsWith(QStringLiteral("req-"))) {
            required = true;
            key.remove(0, 4);
        }

        bool handled = false;
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
                    return BuildError(Tr::tr("URI cannot be parsed. Invalid bitcoin amount."));
                }
                result.amount_sats = *amount_sats;
                result.has_amount = true;
            }
            handled = true;
        }

        if (required && !handled) {
            return BuildError(Tr::tr("URI cannot be parsed. Unsupported required parameter: %1").arg(key));
        }
    }

    result.success = true;
    return result;
}
