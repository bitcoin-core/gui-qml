// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qrimageprovider.h>

#include <bitcoin-build-config.h> // IWYU pragma: keep

#ifdef USE_QRCODE
#include <qrencode.h>
#endif

#include <QImage>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#ifdef USE_QRCODE
namespace {
QRcode* EncodeQRCode(const QString& data)
{
    return QRcode_encodeString(data.toUtf8().constData(), 0, QR_ECLEVEL_L, QR_MODE_8, 1);
}
} // namespace
#endif // USE_QRCODE

bool CanEncodeQRCode(const QString& data)
{
#ifdef USE_QRCODE
    QRcode* code = EncodeQRCode(data);
    if (!code) return false;
    QRcode_free(code);
    return true;
#else
    return false;
#endif // USE_QRCODE
}

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

    QRcode* code = EncodeQRCode(data);

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
