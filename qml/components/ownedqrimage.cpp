// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/components/ownedqrimage.h>
#include <QPainter>
#include <algorithm>

void OwnedQrImage::setPixels(QImage pixels)
{
    if (!pixels.isNull() && (pixels.width() <= 1 || pixels.width() != pixels.height() || pixels.width() > 736)) pixels = {};
    if (m_pixels.cacheKey() == pixels.cacheKey()) return;
    m_pixels = std::move(pixels); update(); Q_EMIT pixelsChanged();
}
void OwnedQrImage::paint(QPainter* painter)
{
    if (m_pixels.isNull()) return;
    const qreal extent = std::min(width(), height());
    painter->setRenderHint(QPainter::SmoothPixmapTransform, false);
    painter->drawImage(QRectF((width() - extent) / 2, (height() - extent) / 2, extent, extent), m_pixels);
}
