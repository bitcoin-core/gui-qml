// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QByteArray>
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QSize>
#include <QSvgRenderer>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_svg";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));

    QSvgRenderer renderer(raw);
    if (!renderer.isValid()) return 0;

    const QSize sz = renderer.defaultSize();
    if (sz.width() <= 0 || sz.height() <= 0 || sz.width() > 4096 || sz.height() > 4096) return 0;

    QImage image(sz, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    renderer.render(&painter);
    painter.end();
    (void)image;
    return 0;
}
