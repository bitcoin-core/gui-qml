// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BITCOINURIMODEL_H
#define BITCOIN_QML_MODELS_BITCOINURIMODEL_H

#include <QObject>
#include <QVariantMap>

/**
 * QML-accessible singleton that wraps the BitcoinUri static parser.
 * Registered as "BitcoinUri" in the org.bitcoincore.qt 1.0 module.
 *
 * URI parsing is wallet-agnostic; this class deliberately does not hold
 * a wallet reference so callers do not need a loaded wallet to parse URIs.
 */
class BitcoinUriModel : public QObject
{
    Q_OBJECT
public:
    explicit BitcoinUriModel(QObject* parent = nullptr);

    /** Parse a bitcoin: URI string. Returns a QVariantMap with keys:
     *  success, error, address, amountSats, hasAmount,
     *  label, hasLabel, uriMessage, hasMessage. */
    Q_INVOKABLE QVariantMap parseBitcoinUri(const QString& uri_text);

    /** Read a local file and parse its contents as a bitcoin: URI.
     *  Accepts plain paths and file:// URLs (QUrl::toLocalFile is used
     *  for platform-correct conversion). Capped at 1 MiB. */
    Q_INVOKABLE QVariantMap parseBitcoinUriFromFile(const QString& source_path);
};

#endif // BITCOIN_QML_MODELS_BITCOINURIMODEL_H
