// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/imageprovider.h>

#include <qt/networkstyle.h>

#include <QFile>
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

    if (id == "app") {
        *size = requested_size;
        return m_network_style->getAppIcon().pixmap(requested_size);
    }

    if (QFile::exists(":/icons/" + id)) {
        *size = requested_size;
        return QIcon(":/icons/" + id).pixmap(requested_size);
    }

    if (id == "coinbase") {
        *size = requested_size;
        return QIcon(":/icons/coinbase").pixmap(requested_size);
    }

    if (id == "check") {
        *size = requested_size;
        return QIcon(":/icons/check").pixmap(requested_size);
    }

    if (id == "copy") {
        *size = requested_size;
        return QIcon(":/icons/copy").pixmap(requested_size);
    }

    if (id == "circle-file") {
        *size = requested_size;
        return QIcon(":/icons/circle-file").pixmap(requested_size);
    }

    if (id == "circle-green-check") {
        *size = requested_size;
        return QIcon(":/icons/circle-green-check").pixmap(requested_size);
    }

    if (id == "circle-red-cross") {
        *size = requested_size;
        return QIcon(":/icons/circle-red-cross").pixmap(requested_size);
    }

    if (id == "cross") {
        *size = requested_size;
        return QIcon(":/icons/cross").pixmap(requested_size);
    }

    if (id == "error") {
        *size = requested_size;
        return QIcon(":/icons/error").pixmap(requested_size);
    }

    if (id == "export") {
        *size = requested_size;
        return QIcon(":/icons/export").pixmap(requested_size);
    }

    if (id == "flip-vertical") {
        *size = requested_size;
        return QIcon(":/icons/flip-vertical").pixmap(requested_size);
    }

    if (id == "gear") {
        *size = requested_size;
        return QIcon(":/icons/gear").pixmap(requested_size);
    }

    if (id == "gear-outline") {
        *size = requested_size;
        return QIcon(":/icons/gear-outline").pixmap(requested_size);
    }

    if (id == "green-check") {
        *size = requested_size;
        return QIcon(":/icons/green-check").pixmap(requested_size);
    }

    if (id == "info") {
        *size = requested_size;
        return QIcon(":/icons/info").pixmap(requested_size);
    }

    if (id == "minus") {
        *size = requested_size;
        return QIcon(":/icons/minus").pixmap(requested_size);
    }

    if (id == "network-dark") {
        *size = requested_size;
        return QIcon(":/icons/network-dark").pixmap(requested_size);
    }

    if (id == "network-light") {
        *size = requested_size;
        return QIcon(":/icons/network-light").pixmap(requested_size);
    }

    if (id == "pending") {
        *size = requested_size;
        return QIcon(":/icons/pending").pixmap(requested_size);
    }

    if (id == "shutdown") {
        *size = requested_size;
        return QIcon(":/icons/shutdown").pixmap(requested_size);
    }

    if (id == "singlesig-wallet") {
        *size = requested_size;
        return QIcon(":/icons/singlesig-wallet").pixmap(requested_size);
    }

    if (id == "storage-dark") {
        *size = requested_size;
        return QIcon(":/icons/storage-dark").pixmap(requested_size);
    }

    if (id == "storage-light") {
        *size = requested_size;
        return QIcon(":/icons/storage-light").pixmap(requested_size);
    }

    if (id == "tooltip-arrow-dark") {
        *size = requested_size;
        return QIcon(":/icons/tooltip-arrow-dark").pixmap(requested_size);
    }

    if (id == "tooltip-arrow-light") {
        *size = requested_size;
        return QIcon(":/icons/tooltip-arrow-light").pixmap(requested_size);
    }

    if (id == "triangle-up") {
        *size = requested_size;
        return QIcon(":/icons/triangle-up").pixmap(requested_size);
    }

    if (id == "triangle-down") {
        *size = requested_size;
        return QIcon(":/icons/triangle-down").pixmap(requested_size);
    }

    if (id == "add-wallet-dark") {
        *size = requested_size;
        return QIcon(":/icons/add-wallet-dark").pixmap(requested_size);
    }

    if (id == "wallet") {
        *size = requested_size;
        return QIcon(":/icons/wallet").pixmap(requested_size);
    }

    if (id == "visible") {
        *size = requested_size;
        return QIcon(":/icons/visible").pixmap(requested_size);
    }

    if (id == "hidden") {
        *size = requested_size;
        return QIcon(":/icons/hidden").pixmap(requested_size);
    }

    if (id == "plus") {
        *size = requested_size;
        return QIcon(":/icons/plus").pixmap(requested_size);
    }

    if (id == "flip-vertical") {
        *size = requested_size;
        return QIcon(":/icons/flip-vertical").pixmap(requested_size);
    }
    return {};
}
