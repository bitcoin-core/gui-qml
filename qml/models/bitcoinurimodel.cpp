// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinurimodel.h>

#include <qml/models/bitcoinuri.h>

#include <QFile>
#include <QUrl>
#include <QVariantMap>

namespace {
QVariantMap BuildBitcoinUriResultMap(const BitcoinUriParseResult& r)
{
    return {
        {QStringLiteral("success"),    r.success},
        {QStringLiteral("error"),      r.error},
        {QStringLiteral("address"),    r.address},
        {QStringLiteral("amountSats"), static_cast<qlonglong>(r.amount_sats)},
        {QStringLiteral("hasAmount"),  r.has_amount},
        {QStringLiteral("label"),      r.label},
        {QStringLiteral("hasLabel"),   r.has_label},
        // Key is "uriMessage" (not "message") to avoid shadowing the JavaScript
        // built-in Error.message property when this map is used in QML.
        {QStringLiteral("uriMessage"), r.message},
        {QStringLiteral("hasMessage"), r.has_message},
    };
}
} // namespace

BitcoinUriModel::BitcoinUriModel(QObject* parent)
    : QObject(parent)
{
}

QVariantMap BitcoinUriModel::parseBitcoinUri(const QString& uri_text)
{
    return BuildBitcoinUriResultMap(BitcoinUri::Parse(uri_text));
}

QVariantMap BitcoinUriModel::parseBitcoinUriFromFile(const QString& source_path)
{
    constexpr qint64 MAX_FILE_SIZE = 1024 * 1024; // 1 MiB

    // Accept either a local file path or a file:// URL (e.g. from a DropArea).
    // QUrl::toLocalFile() handles the platform-specific conversion correctly,
    // including the extra leading slash in file:///C:/path on Windows.
    QString local_path = source_path;
    const QUrl url(source_path);
    if (url.isLocalFile()) {
        local_path = url.toLocalFile();
    }

    // File is read synchronously on the GUI thread. For local storage the
    // 1 MiB guard makes this acceptable. For network-mounted paths (NFS, SMB)
    // this could stall the UI — see issue #541 for the async fix using
    // QtConcurrent::run.
    QFile file(local_path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        BitcoinUriParseResult err;
        err.error = tr("Cannot open file: %1").arg(file.errorString());
        return BuildBitcoinUriResultMap(err);
    }
    if (file.size() > MAX_FILE_SIZE) {
        BitcoinUriParseResult err;
        err.error = tr("File is too large to be a payment URI.");
        return BuildBitcoinUriResultMap(err);
    }
    const QString content = QString::fromUtf8(file.readAll()).trimmed();
    return BuildBitcoinUriResultMap(BitcoinUri::Parse(content));
}
