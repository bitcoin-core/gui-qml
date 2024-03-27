// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_TESTIMAGEPROVIDER_H
#define BITCOIN_QML_TEST_TESTIMAGEPROVIDER_H

#include <QPixmap>
#include <QQuickImageProvider>

QT_BEGIN_NAMESPACE
class QSize;
class QString;
QT_END_NAMESPACE

class TestImageProvider : public QQuickImageProvider
{
public:
    explicit TestImageProvider();

    QPixmap requestPixmap(const QString& id, QSize* size, const QSize& requested_size) override;

};

#endif // BITCOIN_QML_TEST_TESTIMAGEPROVIDER_H
