// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/bitcoinuri.h>
#include <qml/models/bitcoinurimodel.h>

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_bitcoin_uri_parsing";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));

    const QString uri_text = QString::fromUtf8(raw);
    (void)BitcoinUri::Parse(uri_text);

    static QTemporaryDir temp_dir;
    if (temp_dir.isValid()) {
        const QString file_path = temp_dir.filePath(QStringLiteral("uri.txt"));
        QFile file(file_path);
        if (file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            file.write(raw);
            file.close();
            BitcoinUriModel model;
            (void)model.parseBitcoinUri(uri_text);
            (void)model.parseBitcoinUriFromFile(file_path);
            (void)model.parseBitcoinUriFromFile(QStringLiteral("file://") + file_path);
        }
    }

    return 0;
}
