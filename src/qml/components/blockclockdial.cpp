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
#include <QtGlobal>

BlockClockDial::BlockClockDial(QQuickItem *parent)
    : QQuickPaintedItem(parent)
{
    m_animation_timer.setTimerType(Qt::PreciseTimer);
    m_animation_timer.setInterval(16);
    m_delay_timer.setSingleShot(true);
    m_delay_timer.setInterval(5000);
    connect(&m_delay_timer, &QTimer::timeout,
            this, [=]() { this->m_animation_timer.start(); });
    connect(&m_animation_timer, &QTimer::timeout,
            this, [=]() {
                if (m_is_connected
                    && getTargetAnimationAngle() - m_animating_max_angle < 1) {
                    m_animation_timer.stop();
                }
                this->update();
            });
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

qreal BlockClockDial::getTargetAnimationAngle()
{
    if (connected() && synced()) {
        return m_time_ratio_list[0].toDouble() * 360;
    } else if (connected()) {
        return verificationProgress() * 360;
    } else {
        return 360;
    }
}

qreal BlockClockDial::incrementAnimatingMaxAngle(qreal angle)
{
    if (connected()) {
        return angle += (getTargetAnimationAngle() - angle) * 0.05;
    } else {
        // Use linear growth for the "Connecting" animation
        if (angle >= 360) {
            return 360;
        } else {
            return angle += 4;
        }
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
        m_animating_max_angle = 0;
        if (m_is_connected) {
            m_animation_timer.start();
        } else {
            m_animation_timer.stop();
            m_delay_timer.start();
        }
        update();
    }
}

void BlockClockDial::setSynced(bool is_synced)
{
    if (m_is_synced != is_synced) {
        m_is_synced = is_synced;
        m_animating_max_angle = 0;
        if (m_is_synced && connected()) {
            m_animation_timer.start();
        }
        update();
    }
}

void BlockClockDial::setPaused(bool paused)
{
    if (m_is_paused != paused) {
        m_is_paused = paused;
        if (m_is_paused) {
            m_animation_timer.stop();
        }
        m_animating_max_angle = 0;
        update();
    }
}

void BlockClockDial::setPenWidth(qreal width)
{
    m_pen_width = width;
    update();
}

void BlockClockDial::setScale(qreal scale)
{
    m_scale = scale;
    update();

    Q_EMIT scaleChanged();
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
    QRectF rect = QRectF(
        pen.widthF() / 2.0 + 1,
        pen.widthF() / 2.0 + 1,
        smallest - pen.widthF() - 2,
        smallest - pen.widthF() - 2
    );
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
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::FlatCap);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    // The gap is calculated here and is used to create a
    // one pixel spacing between each block
    double gap = degreesPerPixel();

    // Paint blocks
    for (int i = 1; i < numberOfBlocks; i++) {
        if (numberOfBlocks - i <= 6) {
            QPen pen(m_confirmation_colors[numberOfBlocks - i - 1]);
            pen.setWidthF(m_pen_width);
            if (i == numberOfBlocks - 1) {
                pen.setCapStyle(Qt::RoundCap);
            } else {
                pen.setCapStyle(Qt::FlatCap);
            }
            painter->setPen(pen);
        }


        const qreal startAngle = 90 + (-360 * m_time_ratio_list[i].toDouble());
        qreal nextAngle;
        if (i == numberOfBlocks - 1) {
            nextAngle = 90 + (-360 * m_time_ratio_list[0].toDouble());
        } else {
            nextAngle = 90 + (-360 * m_time_ratio_list[i+1].toDouble());
        }

        QPainterPath path;
        if (i == numberOfBlocks - 1) {
            // the last block is rounded so nudge it forward
            // to prevent it from bleeding into the previous
            path.arcMoveTo(bounds, startAngle - 2 * gap);
        } else {
            path.arcMoveTo(bounds, startAngle);
        }

        if (-1 * nextAngle + 90 > m_animating_max_angle) {
            nextAngle = -1 * m_animating_max_angle + 90;
            // end the loop early
            i = numberOfBlocks;
        }

        const qreal spanAngle = -1 * (startAngle - nextAngle);
        if (i == numberOfBlocks - 1) {
            // nudge the last block foward
            path.arcTo(bounds, startAngle - 2 * gap, spanAngle);
        } else {
            path.arcTo(bounds, startAngle, spanAngle);
        }
        painter->drawPath(path);
    }

    QPen gapPen(m_background_color);
    gapPen.setWidthF(4);
    gapPen.setCapStyle(Qt::FlatCap);
    const QRectF gapBounds = getBoundsForPen(gapPen);
    painter->setPen(gapPen);

    // Paint Gaps
    for (int i = 1; i < numberOfBlocks - 1; i++) {
        qreal nextAngle = 90 + (-360 * m_time_ratio_list[i+1].toDouble());

        QPainterPath path;
        path.arcMoveTo(gapBounds, nextAngle + gap);

        if (-1 * nextAngle + 90 > m_animating_max_angle) {
            nextAngle = -1 * m_animating_max_angle + 90;
            // end the loop early
            i = numberOfBlocks;
        }

        path.arcTo(gapBounds, nextAngle, gap * 2);
        painter->drawPath(path);
    }
}

void BlockClockDial::paintProgress(QPainter * painter)
{
    QPen pen(m_confirmation_colors[5]);
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    // QPainter::drawArc uses positive values for counter clockwise - the opposite of our API -
    // so we must reverse the angles with * -1. Also, our angle origin is at 12 o'clock, whereas
    // QPainter's is 3 o'clock, hence - 90.
    const qreal startAngle = 90;
    qreal spanAngle;
    if (verificationProgress() * 360 > m_animating_max_angle) {
        spanAngle = m_animating_max_angle * -1;
    } else {
        spanAngle = verificationProgress() * -360;
    }

    // QPainter::drawArc parameters are 1/16 of a degree
    painter->drawArc(bounds, startAngle * 16, spanAngle * 16);
}

void BlockClockDial::paintConnectingAnimation(QPainter * painter)
{
    QPen pen;
    pen.setWidthF(m_pen_width);
    setupConnectingGradient(pen);
    pen.setBrush(QBrush(m_connecting_gradient));
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);
    if (m_animating_max_angle < m_connecting_end_angle * -1) {
        painter->drawArc(bounds, m_connecting_start_angle * 16, m_animating_max_angle * -16);
    } else {
        painter->drawArc(bounds, m_connecting_start_angle * 16, m_connecting_end_angle * 16);
        m_connecting_start_angle = decrementGradientAngle(m_connecting_start_angle);
    }
}

void BlockClockDial::paintBackground(QPainter * painter)
{
    QPen pen(m_background_color);
    pen.setWidthF(m_pen_width);
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
    pen.setWidthF(m_pen_width);
    // Calculate bound based on width of default pen
    const QRectF bounds = getBoundsForPen(pen);

    QPen time_tick_pen = QPen(m_time_tick_color);
    time_tick_pen.setWidthF(m_pen_width / 2);
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

    if (connected() && synced()) {
        paintBlocks(painter);
    } else if (connected()) {
        paintProgress(painter);
    } else if (m_animation_timer.isActive()) {
        paintConnectingAnimation(painter);
    }
    m_animating_max_angle = incrementAnimatingMaxAngle(m_animating_max_angle);
}
