// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/blockclockdial.h>

#include <algorithm>
#include <utility>

#include <QBrush>
#include <QColor>
#include <QPainterPath>
#include <QConicalGradient>
#include <QPen>
#include <QQuickWindow>
#include <QtMath>
#include <QtGlobal>

BlockClockDial::BlockClockDial(QQuickItem *parent)
    : QQuickPaintedItem(parent),
      m_animation_timer{this},
      m_delay_timer{this}
{
    m_animation_timer.setTimerType(Qt::PreciseTimer);
    m_animation_timer.setInterval(16);
    m_delay_timer.setSingleShot(true);
    m_delay_timer.setInterval(m_connecting_animation_delay_ms);
    connect(&m_delay_timer, &QTimer::timeout,
            this, [this] {
                if (presentationActive() && m_animate_dial && !m_is_paused) {
                    m_animation_timer.start();
                }
            });
    connect(&m_animation_timer, &QTimer::timeout,
            this, &BlockClockDial::advanceAnimation);
    connect(this, &QQuickItem::visibleChanged, this, &BlockClockDial::syncAnimationState);
    connect(this, &QQuickItem::windowChanged, this, [this] {
        connectWindowVisibility();
        syncAnimationState();
    });
}

void BlockClockDial::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    m_component_complete = true;
    connectWindowVisibility();
    syncAnimationState();
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

void BlockClockDial::setupSyncedGradient(const QRectF& bounds)
{
    if (!m_synced_gradient_needs_update && m_synced_gradient_bounds == bounds) {
        return;
    }

    m_synced_gradient_bounds = bounds;
    m_synced_gradient.setCenter(bounds.center());
    m_synced_gradient.setAngle(90);
    m_synced_gradient.setColorAt(0, m_confirmation_colors[5]);
    m_synced_gradient.setColorAt(0.16, m_confirmation_colors[5]);
    m_synced_gradient.setColorAt(0.32, m_confirmation_colors[4]);
    m_synced_gradient.setColorAt(0.48, m_confirmation_colors[3]);
    m_synced_gradient.setColorAt(0.64, m_confirmation_colors[2]);
    m_synced_gradient.setColorAt(0.8, m_confirmation_colors[1]);
    m_synced_gradient.setColorAt(1, m_confirmation_colors[0]);
    m_synced_gradient_needs_update = false;
}

void BlockClockDial::invalidateSyncedGradient()
{
    m_synced_gradient_needs_update = true;
}

qreal BlockClockDial::decrementGradientAngle(qreal angle) const
{
    if (angle == -360) {
        return 0;
    } else {
        return angle -= 4;
    }
}

qreal BlockClockDial::getTargetAnimationAngle() const
{
    if (connected() && synced()) {
        return m_current_time_fraction * 360;
    } else if (connected()) {
        return syncProgress() * 360;
    } else {
        return 360;
    }
}

bool BlockClockDial::presentationActive() const
{
    if (!m_component_complete || !m_rendering_active || !isVisible()) return false;
    if (!window()) return true;

    const QWindow::Visibility visibility{window()->visibility()};
    return window()->isVisible() &&
           visibility != QWindow::Hidden &&
           visibility != QWindow::Minimized;
}

void BlockClockDial::requestRepaint()
{
    if (presentationActive()) update();
}

void BlockClockDial::advanceAnimation()
{
    if (!presentationActive() || !m_animate_dial || m_is_paused) {
        m_animation_timer.stop();
        return;
    }

    if (connected()) {
        const qreal target{getTargetAnimationAngle()};
        m_animating_max_angle += (target - m_animating_max_angle) * 0.05;
        if (qAbs(target - m_animating_max_angle) < 1.0) {
            m_animating_max_angle = target;
            m_animation_timer.stop();
        }
    } else {
        m_animating_max_angle = qMin<qreal>(360.0, m_animating_max_angle + 4.0);
        if (m_animating_max_angle >= m_connecting_end_angle * -1) {
            m_connecting_start_angle = decrementGradientAngle(m_connecting_start_angle);
        }
    }

    requestRepaint();
}

void BlockClockDial::syncAnimationState()
{
    const bool active{presentationActive()};
    const bool becoming_active{active && !m_presentation_was_active};
    m_presentation_was_active = active;

    if (!active || !m_animate_dial || m_is_paused) {
        m_animation_timer.stop();
        m_delay_timer.stop();
        if (!m_animate_dial) m_animating_max_angle = getTargetAnimationAngle();
        return;
    }

    if (becoming_active) {
        // Backing models continued to advance while hidden. Connected clocks
        // resume at the latest value rather than replaying stale animation.
        m_animating_max_angle = connected() ? getTargetAnimationAngle() : 0.0;
        if (!connected()) m_connecting_start_angle = 90.0;
        requestRepaint();
    }

    if (!connected()) {
        if (m_animation_timer.isActive() || m_delay_timer.isActive()) return;
        if (m_connecting_animation_delay_ms == 0) {
            m_animation_timer.start();
        } else {
            m_delay_timer.start(m_connecting_animation_delay_ms);
        }
        return;
    }

    m_delay_timer.stop();
    const qreal target{getTargetAnimationAngle()};
    if (qAbs(target - m_animating_max_angle) < 1.0) {
        m_animating_max_angle = target;
        m_animation_timer.stop();
    } else if (!m_animation_timer.isActive()) {
        m_animation_timer.start();
    }
}

void BlockClockDial::connectWindowVisibility()
{
    disconnect(m_window_visibility_connection);
    if (!window()) return;
    m_window_visibility_connection = connect(window(), &QWindow::visibilityChanged, this, [this] {
        syncAnimationState();
    });
}

void BlockClockDial::setCurrentTimeFraction(qreal fraction)
{
    fraction = qBound<qreal>(0.0, fraction, 1.0);
    if (qFuzzyCompare(m_current_time_fraction + 1.0, fraction + 1.0)) return;
    m_current_time_fraction = fraction;
    syncAnimationState();
    requestRepaint();
}

void BlockClockDial::setBlockTimeFractions(QList<qreal> fractions)
{
    for (qreal& fraction : fractions) fraction = qBound<qreal>(0.0, fraction, 1.0);
    std::sort(fractions.begin(), fractions.end());
    fractions.erase(std::unique(fractions.begin(), fractions.end()), fractions.end());
    if (m_block_time_fractions == fractions) return;
    m_block_time_fractions = std::move(fractions);
    requestRepaint();
}

void BlockClockDial::setSyncProgress(double progress)
{
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_sync_progress + 1.0, progress + 1.0)) return;
    m_sync_progress = progress;
    syncAnimationState();
    requestRepaint();
}

void BlockClockDial::setConnected(bool connected)
{
    if (m_is_connected != connected) {
        m_is_connected = connected;
        m_animating_max_angle = 0;
        if (!m_is_connected) m_connecting_start_angle = 90.0;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setSynced(bool is_synced)
{
    if (m_is_synced != is_synced) {
        m_is_synced = is_synced;
        m_animating_max_angle = 0;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setPaused(bool paused)
{
    if (m_is_paused != paused) {
        m_is_paused = paused;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setAnimateDial(bool animate_dial)
{
    if (m_animate_dial != animate_dial) {
        m_animate_dial = animate_dial;
        if (m_animate_dial) m_animating_max_angle = 0;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setRenderingActive(bool active)
{
    if (m_rendering_active == active) return;
    m_rendering_active = active;
    Q_EMIT renderingActiveChanged();
    syncAnimationState();
}

void BlockClockDial::setConnectingAnimationDelayMs(int connecting_animation_delay_ms)
{
    connecting_animation_delay_ms = qMax(connecting_animation_delay_ms, 0);
    if (m_connecting_animation_delay_ms != connecting_animation_delay_ms) {
        m_connecting_animation_delay_ms = connecting_animation_delay_ms;
        m_delay_timer.setInterval(m_connecting_animation_delay_ms);
        if (!m_is_connected) {
            m_animation_timer.stop();
            m_delay_timer.stop();
        }
        syncAnimationState();
    }
}

void BlockClockDial::setShowTimeTicks(bool show_time_ticks)
{
    if (m_show_time_ticks != show_time_ticks) {
        m_show_time_ticks = show_time_ticks;
        requestRepaint();
    }
}

void BlockClockDial::setShowBlockSegments(bool show_block_segments)
{
    if (m_show_block_segments != show_block_segments) {
        m_show_block_segments = show_block_segments;
        requestRepaint();
    }
}

void BlockClockDial::setUseGradientArcWhenSynced(bool use_gradient_arc_when_synced)
{
    if (m_use_gradient_arc_when_synced != use_gradient_arc_when_synced) {
        m_use_gradient_arc_when_synced = use_gradient_arc_when_synced;
        requestRepaint();
    }
}

void BlockClockDial::setPenWidth(qreal width)
{
    if (m_pen_width != width) {
        m_pen_width = width;
        invalidateSyncedGradient();
        requestRepaint();
    }
}

void BlockClockDial::setScale(qreal scale)
{
    if (m_scale != scale) {
        m_scale = scale;
        invalidateSyncedGradient();
        requestRepaint();

        Q_EMIT scaleChanged();
    }
}

void BlockClockDial::setBackgroundColor(QColor color)
{
    if (m_background_color == color) return;
    m_background_color = color;
    requestRepaint();
}

void BlockClockDial::setConfirmationColors(QList<QColor> colorList)
{
    if (m_confirmation_colors != colorList) {
        m_confirmation_colors = colorList;
        invalidateSyncedGradient();
        requestRepaint();
    }
}

void BlockClockDial::setTimeTickColor(QColor color)
{
    if (m_time_tick_color == color) return;
    m_time_tick_color = color;
    requestRepaint();
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
    if (m_confirmation_colors.size() < 6) return;

    QPen pen(m_confirmation_colors.constLast());
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::FlatCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    // Boundaries are explicit: the period starts at zero, each block closes
    // one confirmation segment, and the current time closes the newest one.
    QList<qreal> boundaries;
    boundaries.reserve(m_block_time_fractions.size() + 2);
    boundaries.push_back(0.0);
    boundaries.append(m_block_time_fractions);
    boundaries.push_back(m_current_time_fraction);

    const qreal gap{degreesPerPixel()};
    const qsizetype segment_count{boundaries.size() - 1};
    for (qsizetype segment{0}; segment < segment_count; ++segment) {
        const qsizetype color_index{qMin<qsizetype>(5, segment_count - segment - 1)};
        pen.setColor(m_confirmation_colors.at(color_index));
        painter->setPen(pen);

        const qreal startAngle{90 - 360 * boundaries.at(segment)};
        qreal nextAngle{90 - 360 * boundaries.at(segment + 1)};

        QPainterPath path;
        path.arcMoveTo(bounds, startAngle);

        if (-1 * nextAngle + 90 > m_animating_max_angle) {
            nextAngle = -1 * m_animating_max_angle + 90;
            segment = segment_count;
        }

        const qreal spanAngle = -1 * (startAngle - nextAngle) + gap;
        path.arcTo(bounds, startAngle, spanAngle);
        painter->drawPath(path);
    }
}

void BlockClockDial::paintProgress(QPainter * painter)
{
    if (m_confirmation_colors.size() < 6) return;
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
    if (syncProgress() * 360 > m_animating_max_angle) {
        spanAngle = m_animating_max_angle * -1;
    } else {
        spanAngle = syncProgress() * -360;
    }

    // QPainter::drawArc parameters are 1/16 of a degree
    painter->drawArc(bounds, startAngle * 16, spanAngle * 16);
}

void BlockClockDial::paintCurrentTimeArc(QPainter* painter)
{
    if (m_confirmation_colors.size() < 6) return;

    QPen pen(m_confirmation_colors[5]);
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    painter->setPen(pen);

    const qreal start_angle = 90;
    const qreal time_angle = m_current_time_fraction * 360;
    const qreal span_angle = -1 * qMin(time_angle, m_animating_max_angle);
    painter->drawArc(bounds, start_angle * 16, span_angle * 16);
}

void BlockClockDial::paintSyncedGradientArc(QPainter* painter)
{
    if (m_confirmation_colors.size() < 6) {
        paintCurrentTimeArc(painter);
        return;
    }

    QPen pen;
    pen.setWidthF(m_pen_width);
    pen.setCapStyle(Qt::RoundCap);
    const QRectF bounds = getBoundsForPen(pen);
    setupSyncedGradient(bounds);
    pen.setBrush(QBrush(m_synced_gradient));
    painter->setPen(pen);

    const qreal start_angle = 90;
    const qreal time_angle = m_current_time_fraction * 360;
    const qreal span_angle = -1 * qMin(time_angle, m_animating_max_angle);
    painter->drawArc(bounds, start_angle * 16, span_angle * 16);
}

void BlockClockDial::paintConnectingAnimation(QPainter * painter)
{
    if (m_confirmation_colors.size() < 6) return;

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
    if (showTimeTicks()) {
        paintTimeTicks(painter);
    }

    if (paused()) return;

    if (connected() && synced()) {
        if (showBlockSegments()) {
            paintBlocks(painter);
        } else if (useGradientArcWhenSynced()) {
            paintSyncedGradientArc(painter);
        } else {
            paintCurrentTimeArc(painter);
        }
    } else if (connected()) {
        paintProgress(painter);
    } else if (m_animation_timer.isActive()) {
        paintConnectingAnimation(painter);
    }
}
