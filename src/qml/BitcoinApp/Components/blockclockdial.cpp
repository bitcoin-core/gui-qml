// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/BitcoinApp/Components/blockclockdial.h>

#include <QColor>
#include <QPainterPath>
#include <QPen>
#include <QtMath>

BlockClockDial::BlockClockDial(QQuickItem *parent)
: QQuickPaintedItem(parent)
, m_time_ratio_list{0.0}
, m_background_color{QColor("#2D2D2D")}
, m_time_tick_color{QColor("#000000")}
{
}

void BlockClockDial::setTimeRatioList(QVariantList new_list)
{
    m_time_ratio_list = new_list;
    update();
}

void BlockClockDial::setVerificationProgress(double progress)
{
    m_verification_progress = progress;
    update();
}

void BlockClockDial::setSynced(bool synced)
{
    m_is_synced = synced;
    update();
}

void BlockClockDial::setPaused(bool paused)
{
    m_is_paused = paused;
    update();
}

void BlockClockDial::setBackgroundColor(QColor color)
{
    m_background_color = color;
    update();
}

void BlockClockDial::setTimeTickColor(QColor color)
{
    m_time_tick_color = color;
    update();
}

QRectF BlockClockDial::getBoundsForPen(const QPen & pen)
{
    const QRectF bounds = boundingRect();
    const qreal smallest = qMin(bounds.width(), bounds.height());
    QRectF rect = QRectF(pen.widthF() / 2.0 + 1, pen.widthF() / 2.0 + 1, smallest - pen.widthF() - 2, smallest - pen.widthF() - 2);
    rect.moveCenter(bounds.center());

    // Make sure the arc is aligned to whole pixels.
    if (rect.x() - int(rect.x()) > 0)
        rect.setX(qCeil(rect.x()));
    if (rect.y() - int(rect.y()) > 0)
        rect.setY(qCeil(rect.y()));
    if (rect.width() - int(rect.width()) > 0)
        rect.setWidth(qFloor(rect.width()));
    if (rect.height() - int(rect.height()) > 0)
        rect.setHeight(qFloor(rect.height()));

    return rect;
}

void BlockClockDial::paintBlocks(QPainter * painter)
{
    int numberOfBlocks = m_time_ratio_list.length();
    if (numberOfBlocks < 2) {
        return;
    }

    QPen pen(QColor("#F1D54A"));
    pen.setWidth(4);
    pen.setCapStyle(Qt::FlatCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    QColor confirmationColors[] = {
        QColor("#FF1C1C"), // red
        QColor("#ED6E46"),
        QColor("#EE8847"),
        QColor("#EFA148"),
        QColor("#F0BB49"),
        QColor("#F1D54A"), // yellow
    };

    // The gap is calculated here and is used to create a
    // one pixel spacing between each block
    double gap = degreesPerPixel();

    // Paint blocks
    for (int i = 1; i < numberOfBlocks; i++) {
        if (numberOfBlocks - i <= 6) {
            QPen pen(confirmationColors[numberOfBlocks - i - 1]);
            pen.setWidth(4);
            pen.setCapStyle(Qt::FlatCap);
            painter->setPen(pen);
        }

        const qreal startAngle = 90 + (-360 * m_time_ratio_list[i].toDouble());
        qreal nextAngle;
        if (i == numberOfBlocks - 1) {
            nextAngle = 90 + (-360 * m_time_ratio_list[0].toDouble());
        } else {
            nextAngle = 90 + (-360 * m_time_ratio_list[i+1].toDouble());
        }
        const qreal spanAngle = -1 * (startAngle - nextAngle) + gap;
        QPainterPath path;
        path.arcMoveTo(bounds, startAngle);
        path.arcTo(bounds, startAngle, spanAngle);
        painter->drawPath(path);
    }
}

void BlockClockDial::paintProgress(QPainter * painter)
{
    QPen pen(QColor("#F1D54A"));
    pen.setWidthF(4);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    // QPainter::drawArc uses positive values for counter clockwise - the opposite of our API -
    // so we must reverse the angles with * -1. Also, our angle origin is at 12 o'clock, whereas
    // QPainter's is 3 o'clock, hence - 90.
    const qreal startAngle = 90;
    const qreal spanAngle = verificationProgress() * -360;

    // QPainter::drawArc parameters are 1/16 of a degree
    painter->drawArc(bounds, startAngle * 16, spanAngle * 16);
}

void BlockClockDial::paintBackground(QPainter * painter)
{
    QPen pen(m_background_color);
    pen.setWidthF(4);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    painter->drawEllipse(bounds);
}

double BlockClockDial::degreesPerPixel()
{
    double circumference = width() * 3.1415926;
    return 360 / circumference;
}

void BlockClockDial::paintTimeTicks(QPainter * painter)
{
    QPen pen(m_time_tick_color);
    pen.setWidthF(4);
    // Calculate bound based on width of default pen
    const QRectF bounds = getBoundsForPen(pen);

    QPen time_tick_pen = QPen(m_time_tick_color);
    time_tick_pen.setWidth(2);
    time_tick_pen.setCapStyle(Qt::RoundCap);
    painter->setPen(time_tick_pen);
    for (double angle = 0; angle < 360; angle += 30) {
        QPainterPath path;
        path.arcMoveTo(bounds, angle);
        path.arcTo(bounds, angle, degreesPerPixel());
        painter->drawPath(path);
    }
}

void BlockClockDial::paint(QPainter * painter)
{
    if (width() <= 0 || height() <= 0) {
        return;
    }
    painter->setRenderHint(QPainter::Antialiasing);

    paintBackground(painter);
    paintTimeTicks(painter);

    if (paused()) return;

    if (synced()) {
        paintBlocks(painter);
    } else {
        paintProgress(painter);
    }
}
