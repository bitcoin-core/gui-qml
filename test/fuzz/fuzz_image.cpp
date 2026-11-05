// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QByteArray>
#include <QApplication>
#include <QImage>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_image";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));

    QImage image;
    if (!image.loadFromData(raw)) return 0;

    // Exercise pixel access and format conversion paths.
    (void)image.width();
    (void)image.height();
    (void)image.format();
    (void)image.convertToFormat(QImage::Format_ARGB32);
    if (image.width() > 0 && image.height() > 0) {
        (void)image.pixel(0, 0);
    }
    return 0;
}
