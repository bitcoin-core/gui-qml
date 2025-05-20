// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_QRIMAGEPROVIDER_H
#define BITCOIN_QML_QRIMAGEPROVIDER_H

#include <QQuickImageProvider>

QT_BEGIN_NAMESPACE
class QImage;
class QSize;
class QString;
QT_END_NAMESPACE

class QRImageProvider : public QQuickImageProvider
{
public:
    explicit QRImageProvider();

    QImage requestImage(const QString& id, QSize* size, const QSize& requested_size) override;
};

#endif // BITCOIN_QML_QRIMAGEPROVIDER_H
