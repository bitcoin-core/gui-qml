// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_COMPONENTS_OWNEDQRIMAGE_H
#define BITCOIN_QML_COMPONENTS_OWNEDQRIMAGE_H
#include <QImage>
#include <QQuickPaintedItem>

/** Presentation only. Encoding and file production never capture this item. */
class OwnedQrImage : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QImage pixels READ pixels WRITE setPixels NOTIFY pixelsChanged)
    Q_PROPERTY(QImage empty READ empty CONSTANT)
public:
    explicit OwnedQrImage(QQuickItem* parent = nullptr) : QQuickPaintedItem(parent) {}
    QImage pixels() const { return m_pixels; }
    QImage empty() const { return {}; }
    void setPixels(QImage pixels);
    void paint(QPainter* painter) override;
Q_SIGNALS:
    void pixelsChanged();
private:
    QImage m_pixels;
};
#endif // BITCOIN_QML_COMPONENTS_OWNEDQRIMAGE_H
