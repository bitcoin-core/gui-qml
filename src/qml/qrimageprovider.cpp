// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qrimageprovider.h>

#if defined(HAVE_CONFIG_H)
#include <config/bitcoin-config.h> /* for USE_QRCODE */
#endif

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

QRImageProvider::QRImageProvider()
    : QQuickImageProvider{QQuickImageProvider::Image}
{
}

QImage QRImageProvider::requestImage(const QString& id, QSize* size, const QSize& requested_size)
{
#ifdef USE_QRCODE
    const QUrl url{"image:///" + id};
    const QUrlQuery query{url};
    const QString data{url.path().mid(1)};
    const QColor fg{query.queryItemValue("fg")};
    const QColor bg{query.queryItemValue("bg")};

    QRcode* code = QRcode_encodeString(data.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);

    if (code) {
        QImage image{code->width, code->width, QImage::Format_ARGB32};
        unsigned char* p = code->data;
        for (int y = 0; y < code->width; ++y) {
            for (int x = 0; x < code->width; ++x) {
                image.setPixelColor(x, y, (*p & 1) ? fg : bg);
                ++p;
            }
        }
        *size = QSize(code->width, code->width);
        QRcode_free(code);
        return image;
    }
#endif // USE_QRCODE
    QImage pixel{1, 1, QImage::Format_ARGB32};
    pixel.setPixelColor(0, 0, QColorConstants::Transparent);
    *size = QSize(1, 1);
    return pixel;
}
