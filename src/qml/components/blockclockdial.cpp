// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/blockclockdial.h>

#include <QBrush>
#include <QColor>
#include <QPainterPath>
#include <QConicalGradient>
#include <QPen>
#include <QtMath>

BlockClockDial::BlockClockDial(QQuickItem *parent)
: QQuickPaintedItem(parent)
, m_time_ratio_list{0.0}
, m_background_color{QColor("#2D2D2D")}
, m_confirmation_colors{QList<QColor>{}}
, m_time_tick_color{QColor("#000000")}
, m_animation_timer{QTimer(this)}
, m_delay_timer{QTimer(this)}
{
    m_animation_timer.setTimerType(Qt::PreciseTimer);
    m_animation_timer.setInterval(16);
    m_delay_timer.setSingleShot(true);
    m_delay_timer.setInterval(5000);
    connect(&m_animation_timer, &QTimer::timeout,
            this, [=]() { this->update(); });
    connect(&m_delay_timer, &QTimer::timeout,
            this, [=]() { this->m_animation_timer.start(); });
    m_delay_timer.start();
}

void BlockClockDial::setupConnectingGradient(const QPen & pen)
{
    m_connecting_gradient.setCenter(getBoundsForPen(pen).center());
    m_connecting_gradient.setAngle(m_connecting_start_angle);
    m_connecting_gradient.setColorAt(0, m_confirmation_colors[5]);
    m_connecting_gradient.setColorAt(0.5, m_confirmation_colors[5]);
    m_connecting_gradient.setColorAt(0.6, m_confirmation_colors[4]);
    m_connecting_gradient.setColorAt(0.7, m_confirmation_colors[3]);
    m_connecting_gradient.setColorAt(1, "transparent");
}

qreal BlockClockDial::decrementGradientAngle(qreal angle)
{
    if (angle == -360) {
        return 0;
    } else {
        return angle -= 4;
    }
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

void BlockClockDial::setConnected(bool connected)
{
    if (m_is_connected != connected) {
        m_is_connected = connected;
        m_delay_timer.stop();
        if (m_is_connected) {
            m_animation_timer.stop();
        } else {
            m_delay_timer.start();
        }
    }
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

void BlockClockDial::setConfirmationColors(QList<QColor> colorList)
{
    m_confirmation_colors = colorList;
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

    QPen pen(m_confirmation_colors[5]);
    pen.setWidth(4);
    pen.setCapStyle(Qt::FlatCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    // The gap is calculated here and is used to create a
    // one pixel spacing between each block
    double gap = degreesPerPixel();

    // Paint blocks
    for (int i = 1; i < numberOfBlocks; i++) {
        if (numberOfBlocks - i <= 6) {
            QPen pen(m_confirmation_colors[numberOfBlocks - i - 1]);
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
    QPen pen(m_confirmation_colors[5]);
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

void BlockClockDial::paintConnectingAnimation(QPainter * painter)
{
    QPen pen;
    pen.setWidthF(4);
    setupConnectingGradient(pen);
    pen.setBrush(QBrush(m_connecting_gradient));
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);
    painter->drawArc(bounds, m_connecting_start_angle * 16, m_connecting_end_angle * 16);
    m_connecting_start_angle = decrementGradientAngle(m_connecting_start_angle);
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

    if (m_animation_timer.isActive()) {
        paintConnectingAnimation(painter);
        return;
    }

    if (synced() && connected()) {
        paintBlocks(painter);
    } else if (connected()) {
        paintProgress(painter);
    }
}
