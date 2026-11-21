// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/imageprovider.h>

#include <qml/networkstyle.h>

#include <QColor>
#include <QFile>
#include <QIcon>
#include <QPainter>
#include <QPixmap>
#include <QQuickImageProvider>
#include <QRegularExpression>
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

    // "<icon>?color=<rrggbb>" draws a monochrome icon in that color. Icon
    // recolors at runtime, but an image placed inside text, such as
    // LinkText's external-link icon, can't be recolored after loading.
    QString name{id};
    QColor color;
    const qsizetype query_start = id.indexOf(QLatin1Char('?'));
    if (query_start >= 0) {
        name = id.left(query_start);
        static const QRegularExpression COLOR_RE{QStringLiteral(R"(^color=([0-9A-Fa-f]{6})$)")};
        const QRegularExpressionMatch match = COLOR_RE.match(id.mid(query_start + 1));
        if (!match.hasMatch()) {
            return {};
        }
        color = QColor{QLatin1Char('#') + match.captured(1)};
    }

    if (QFile::exists(":/icons/" + name)) {
        *size = requested_size;
        QPixmap pixmap = QIcon(":/icons/" + name).pixmap(requested_size);
        if (color.isValid() && !pixmap.isNull()) {
            QPainter painter{&pixmap};
            painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            painter.fillRect(pixmap.rect(), color);
        }
        return pixmap;
    }

    return {};
}
