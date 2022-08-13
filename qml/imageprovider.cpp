// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/imageprovider.h>

#include <qt/networkstyle.h>

#include <QIcon>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QSize>
#include <QString>

ImageProvider::ImageProvider(const NetworkStyle* network_style)
    : QQuickImageProvider{QQuickImageProvider::Pixmap},
      m_network_style{network_style} {}

QPixmap ImageProvider::requestPixmap(const QString& id, QSize* size, const QSize& requested_size)
{
    if (!size || !requested_size.isValid()) {
        return {};
    }

    if (id == "arrow-down") {
        *size = requested_size;
        return QIcon(":/icons/arrow-down").pixmap(requested_size);
    }

    if (id == "arrow-up") {
        *size = requested_size;
        return QIcon(":/icons/arrow-up").pixmap(requested_size);
    }

    if (id == "app") {
        *size = requested_size;
        return m_network_style->getAppIcon().pixmap(requested_size);
    }

    if (id == "caret-left") {
        *size = requested_size;
        return QIcon(":/icons/caret-left").pixmap(requested_size);
    }

    if (id == "caret-right") {
        *size = requested_size;
        return QIcon(":/icons/caret-right").pixmap(requested_size);
    }

    if (id == "export") {
        *size = requested_size;
        return QIcon(":/icons/export").pixmap(requested_size);
    }

    if (id == "gear") {
        *size = requested_size;
        return QIcon(":/icons/gear").pixmap(requested_size);
    }

    if (id == "info") {
        *size = requested_size;
        return QIcon(":/icons/info").pixmap(requested_size);
    }

    return {};
}
