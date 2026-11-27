// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_IMAGEPROVIDER_H
#define BITCOIN_QML_IMAGEPROVIDER_H

#include <QPixmap>
#include <QQuickImageProvider>

class NetworkStyle;

QT_BEGIN_NAMESPACE
class QSize;
class QString;
QT_END_NAMESPACE

class ImageProvider : public QQuickImageProvider
{
public:
    explicit ImageProvider(const NetworkStyle* network_style);

    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requested_size) override;

private:
    const NetworkStyle* m_network_style;
};

#endif // BITCOIN_QML_IMAGEPROVIDER_H
