// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qrimageprovider.h>

#include <QByteArray>
#include <QApplication>
#include <QSize>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_qrimageprovider";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const QString id = QString::fromUtf8(raw);

    QRImageProvider provider;
    QSize size_out;
    (void)provider.requestImage(id, &size_out, QSize());
    return 0;
}
