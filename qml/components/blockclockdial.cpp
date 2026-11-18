// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/atomicsharedptr.h>
#include <qml/components/blockclockdial.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <array>
#include <map>
#include <deque>
#include <QPointer>
#include <stdexcept>

#include <algorithm>
#include <cmath>
#include <utility>

#include <QBrush>
#include <QColor>
#include <QPainterPath>
#include <QConicalGradient>
#include <QPen>
#include <QQuickWindow>
#include <QtMath>
#include <QtGlobal>

struct BlockClockDial::Input {
    blockclock::Snapshot source;
    QSizeF size;
    QRectF bounds;
    std::array<QColor, 6> colors;
    qreal pen{0}, current{0}, angle{0};
    quint64 revision{0}, epoch{0};
    bool normalized{true}, colors_ready{false};
};
// Only the latest input is rendered: the query coalesces requests, so at most
// the displayed frame and the one being drawn exist.
struct BlockClockDial::Frame {
    enum class Failure { None, Retention, InvalidInput, Render };
    QImage image;
    blockclock::Snapshot normalized;
    quint64 revision{0}, epoch{0};
    Failure failure{Failure::None};
};
struct BlockClockDial::Native {
    std::atomic_bool closed{false};
    ownership::AtomicSharedPtr<const Input> input;
    blockclock::Snapshot original, normalized;
};
namespace {
struct RasterRetention final : std::exception {};
struct BlockSegment { qreal start_fraction, end_fraction; qsizetype start_confirmations, end_confirmations; };
qreal Degrees(qreal pixels, const QRectF& bounds) {
    const qreal radius = qMin(bounds.width(), bounds.height()) / 2.0;
    return radius <= 0 ? 360.0 : qRadiansToDegrees(pixels / radius);
}
template <typename Input>
QList<BlockSegment> Coalesce(const Input& input, const QList<qreal>& fractions, const QRectF& bounds)
{
    const qreal current_fraction{qBound<qreal>(0.0, input.current, 1.0)};
    if (current_fraction <= 0.0) return {};

    // A rounded segment needs room for both half-caps, the visual gap, and at
    // least one pixel of center line. Each visual boundary retains the number
    // of blocks in its cluster so painting can preserve confirmation depth.
    const qreal minimum_interval_fraction{
        Degrees(input.pen + qMax<qreal>(1.0, input.pen / 2.0) + 1.0, bounds) / 360.0};

    struct Boundary
    {
        qreal fraction;
        qsizetype block_count;
    };
    QList<Boundary> descending_boundaries{{current_fraction, 0}};
    qreal next_boundary{current_fraction};
    qsizetype future_confirmations{0};
    // Walk backward so an unrenderable block joins the newer visual boundary.
    // The current-time boundary can therefore carry recently mined blocks too.
    for (auto it{fractions.crbegin()}; it != fractions.crend(); ++it) {
        const qreal boundary{*it};
        if (boundary <= 0.0) continue;
        if (boundary >= current_fraction) {
            // Miner timestamps can be ahead of the local clock. These blocks
            // confirm every visible segment without adding a future arc or
            // a coalesced gradient that would fade their confirmations away.
            ++future_confirmations;
            continue;
        }
        if (next_boundary - boundary < minimum_interval_fraction) {
            ++descending_boundaries.last().block_count;
            continue;
        }

        descending_boundaries.push_back({boundary, 1});
        next_boundary = boundary;
    }

    // Do not leave an undersized first segment against the period boundary.
    if (descending_boundaries.size() > 1 &&
        descending_boundaries.constLast().fraction < minimum_interval_fraction) {
        descending_boundaries.removeLast();
    }
    std::reverse(descending_boundaries.begin(), descending_boundaries.end());

    // Convert weighted visual boundaries into chronological paint segments.
    // Crossing a boundary removes every confirmation represented by its cluster.
    qsizetype confirmations{future_confirmations};
    for (const Boundary& boundary : descending_boundaries) confirmations += boundary.block_count;

    QList<BlockSegment> segments;
    segments.reserve(descending_boundaries.size());
    qreal segment_start{0.0};
    for (const Boundary& boundary : descending_boundaries) {
        const qsizetype next_confirmations{confirmations - boundary.block_count};
        segments.push_back({segment_start, boundary.fraction, confirmations, next_confirmations});
        segment_start = boundary.fraction;
        confirmations = next_confirmations;
    }
    return segments;
}


template <typename Input>
void ValidateRenderInput(const Input& input)
{
    if (!std::isfinite(input.size.width()) || !std::isfinite(input.size.height()) || !std::isfinite(input.pen) ||
        !std::isfinite(input.current) || !std::isfinite(input.angle) || input.pen < 0)
        throw std::invalid_argument("Invalid dial geometry");
}
template <typename Input>
QImage RenderOverlay(const Input& input, const QList<qreal>& fractions)
{
    constexpr int MAX_EDGE{1024}, MAX_PIXELS{512 * 1024};
    const auto width = input.size.width(), height = input.size.height();
    ValidateRenderInput(input);
    if (width <= 0 || height <= 0 || !input.colors_ready) return {};
    qreal scale = std::min(qreal{1}, qreal(MAX_EDGE) / std::max(width, height));
    int raster_width = std::max(1, int(std::floor(width * scale)));
    int raster_height = std::max(1, int(std::floor(height * scale)));
    if (raster_width * raster_height > MAX_PIXELS) {
        scale *= std::sqrt(qreal(MAX_PIXELS) / (raster_width * raster_height));
        raster_width = std::max(1, int(std::floor(width * scale)));
        raster_height = std::max(1, int(std::floor(height * scale)));
    }
    QImage image(raster_width, raster_height, QImage::Format_ARGB32_Premultiplied);
    if (image.isNull()) throw std::bad_alloc{};
    image.fill(Qt::transparent);
    QPainter native_painter(&image);
    native_painter.setRenderHint(QPainter::Antialiasing);
    native_painter.scale(qreal(raster_width) / width, qreal(raster_height) / height);
    auto* painter = &native_painter;
    const auto& bounds = input.bounds;
    const auto segments = Coalesce(input, fractions, bounds);
    QPen pen(input.colors.back()); pen.setWidthF(input.pen); pen.setCapStyle(Qt::RoundCap);
    const qreal boundary_gap_degrees{
        Degrees(input.pen + qMax<qreal>(1.0, input.pen / 2.0), bounds)};
    const qreal half_boundary_gap{boundary_gap_degrees / 2.0};
    const qreal animated_fraction{qBound<qreal>(0.0, input.angle / 360.0, 1.0)};
    for (const BlockSegment& segment : segments) {
        const qreal painted_end{qMin(segment.end_fraction, animated_fraction)};
        const qreal available_degrees{(painted_end - segment.start_fraction) * 360.0};
        if (available_degrees <= boundary_gap_degrees) {
            if (segment.end_fraction > animated_fraction) break;
            continue;
        }

        const qsizetype confirmation_steps{segment.start_confirmations - segment.end_confirmations};
        const qsizetype start_color_index{qMin<qsizetype>(5, segment.start_confirmations)};
        const qsizetype end_color_index{qMin<qsizetype>(5, segment.end_confirmations)};
        if (confirmation_steps > 1 && start_color_index != end_color_index) {
            // Conical gradients run counter-clockwise, so anchor the first stop
            // at the newer end and interpolate back toward the segment start.
            const qreal gradient_angle{
                std::fmod(450.0 - 360.0 * segment.end_fraction, 360.0)};
            const qreal gradient_span{segment.end_fraction - segment.start_fraction};
            QConicalGradient gradient{bounds.center(), gradient_angle};
            gradient.setColorAt(0.0, input.colors.at(end_color_index));
            for (qsizetype confirmations{segment.end_confirmations + 1};
                 confirmations <= start_color_index;
                 ++confirmations) {
                const qreal stop{
                    gradient_span * (confirmations - segment.end_confirmations) /
                    confirmation_steps};
                gradient.setColorAt(stop, input.colors.at(confirmations));
            }
            gradient.setColorAt(gradient_span, input.colors.at(start_color_index));
            pen.setBrush(QBrush{gradient});
        } else {
            pen.setColor(input.colors.at(start_color_index));
        }
        painter->setPen(pen);

        const qreal start_angle{90 - 360 * segment.start_fraction - half_boundary_gap};
        const qreal end_angle{90 - 360 * painted_end + half_boundary_gap};

        QPainterPath path;
        path.arcMoveTo(bounds, start_angle);
        path.arcTo(bounds, start_angle, end_angle - start_angle);
        painter->drawPath(path);

        if (segment.end_fraction > animated_fraction) break;
    }
    painter->end();
    return image;
}
} // namespace

void BlockClockDial::initializeNative()
{
    if (m_services.scheduler) m_native = execution::RetainSnapshot<Native>(m_services.scheduler, [] { return std::make_unique<Native>(); });
    if (!m_native) m_render_error = tr("Block clock background rendering is unavailable.");
}
void BlockClockDial::closeAdmission()
{
    if (std::exchange(m_closed, true)) return;
    m_animation_timer.stop(); m_delay_timer.stop();
    if (m_native) m_native->closed.store(true);
    if (m_query) m_query->close();
    m_query.reset(); m_input.reset(); m_frame.reset(); m_source.reset(); m_native.reset(); m_render_pending = false;
}
void BlockClockDial::setServices(PresentationServices* services)
{
    if (!services || m_presentation || m_closed) return;
    m_presentation = services;
    connect(services, &PresentationServices::closed, this, &BlockClockDial::closeAdmission);
    Q_EMIT servicesChanged();
    if (services->isClosed()) { closeAdmission(); return; }
    m_services = {services->ports().scheduler, presentation::ReliablePost(services->ports().post)};
    initializeNative();
    if (m_component_complete) { notifyRenderState(); requestRepaint(); }
}
QList<qreal> BlockClockDial::blockTimeFractions() const
{
    if (m_frame && m_frame->epoch == m_content_epoch && m_frame->normalized) return m_frame->normalized->fractions;
    return m_source ? m_source->fractions : QList<qreal>{};
}
QSize BlockClockDial::renderedPixelSize() const { return m_frame ? m_frame->image.size() : QSize{}; }
bool BlockClockDial::renderAvailable() const { return m_frame && m_frame->epoch == m_content_epoch && !m_frame->image.isNull(); }
void BlockClockDial::retryRendering() { if (!m_capture_rejected) requestRepaint(); }
void BlockClockDial::notifyRenderState()
{
    if (m_closed || std::exchange(m_pending_notice, true)) return;
    const QPointer<BlockClockDial> observer{this};
    try { m_services.post([observer] {
        if (!observer) return;
        observer->m_pending_notice = false;
        if (!observer->m_closed) Q_EMIT observer->renderStateChanged();
    }); } catch (...) { m_pending_notice = false; }
}
void BlockClockDial::requestFrame()
{
    if (m_closed || !m_native) return;
    Input input;
    input.source = m_source; input.normalized = m_source_normalized;
    input.size = QSizeF(width(), height()); input.pen = m_pen_width;
    // Preserve invalid geometry for the Native result without passing it to Qt.
    if (std::isfinite(input.pen) && input.pen >= 0) {
        QPen pen;
        pen.setWidthF(input.pen);
        input.bounds = getBoundsForPen(pen);
    }
    input.current = m_current_time_fraction; input.angle = m_animating_max_angle;
    input.colors_ready = m_confirmation_colors.size() >= 6;
    if (input.colors_ready) for (int i = 0; i < 6; ++i) input.colors[i] = m_confirmation_colors[i];
    if (!m_input || m_input->source != input.source || m_input->normalized != input.normalized || m_input->size != input.size ||
        m_input->bounds != input.bounds || m_input->pen != input.pen || m_input->current != input.current ||
        m_input->colors_ready != input.colors_ready || m_input->colors != input.colors) ++m_content_epoch;
    input.epoch = m_content_epoch; input.revision = ++m_render_revision;
    auto captured = execution::RetainSnapshot<Input>(m_services.scheduler, [&] { return std::make_unique<Input>(std::move(input)); });
    if (!captured) { m_render_pending = false; m_render_error = tr("Block clock rendering could not be retained. Retry after background work finishes."); notifyRenderState(); return; }
    m_input = std::move(captured); m_native->input.store(m_input);
    if (!m_query) {
        const QPointer<BlockClockDial> observer{this};
        m_query = std::make_shared<execution::SnapshotQuery<std::shared_ptr<const Frame>>>(m_services.scheduler,
            [state = m_native, scheduler = m_services.scheduler] {
                if (state->closed.load()) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                const auto input = state->input.load();
                if (!input) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                try {
                    ValidateRenderInput(*input);
                    auto source = input->source;
                    if (source && !input->normalized) {
                        if (state->original != source || !state->normalized) {
                            auto normalized = execution::RetainSnapshot<blockclock::Projection>(scheduler, [&] {
                                auto value = std::make_unique<blockclock::Projection>();
                                value->fractions = source->fractions;
                                for (auto& fraction : value->fractions) {
                                    if (!std::isfinite(fraction)) throw std::invalid_argument("Invalid block clock fraction");
                                    fraction = qBound<qreal>(0, fraction, 1);
                                }
                                std::sort(value->fractions.begin(), value->fractions.end()); return value;
                            });
                            if (!normalized) throw RasterRetention{};
                            state->original = source; state->normalized = std::move(normalized);
                        }
                        source = state->normalized;
                    }
                    auto frame = execution::RetainSnapshot<Frame>(scheduler, [&] {
                        auto value = std::make_unique<Frame>();
                        value->revision = input->revision; value->epoch = input->epoch; value->normalized = source;
                        value->image = RenderOverlay(*input, source ? source->fractions : QList<qreal>{});
                        return value;
                    });
                    if (!frame) throw RasterRetention{};
                    return std::shared_ptr<const Frame>{std::move(frame)};
                } catch (const RasterRetention&) {
                    auto failed = std::make_shared<Frame>(); failed->failure = Frame::Failure::Retention;
                    failed->revision = input->revision; failed->epoch = input->epoch; return std::shared_ptr<const Frame>{std::move(failed)};
                } catch (const std::invalid_argument&) {
                    auto failed = std::make_shared<Frame>(); failed->failure = Frame::Failure::InvalidInput;
                    failed->revision = input->revision; failed->epoch = input->epoch; return std::shared_ptr<const Frame>{std::move(failed)};
                } catch (...) {
                    auto failed = std::make_shared<Frame>(); failed->failure = Frame::Failure::Render; failed->revision = input->revision; failed->epoch = input->epoch;
                    return std::shared_ptr<const Frame>{std::move(failed)};
                }
            }, [observer, post = m_services.post] { post([observer] { if (observer) observer->publishFrame(); }); }, execution::Lane::Domain);
    }
    m_render_pending = true; if (!m_capture_rejected) m_render_error.clear();
    // Hidden presentation keeps only the latest immutable input. Standalone
    // render tests have no completed QML component and still prepare offscreen.
    if (!m_component_complete || presentationActive()) { m_query->discardReadFailure(); m_query->request(); }
    notifyRenderState();
}
void BlockClockDial::publishFrame()
{
    if (m_closed || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    if (!result->snapshot || !*result->snapshot) {
        m_render_pending = false; m_render_error = tr("Block clock rendering could not complete. Retry after background work finishes.");
        Q_EMIT renderStateChanged(); return;
    }
    const auto frame = *result->snapshot;
    if (frame->epoch != m_content_epoch || frame->revision < m_displayed_revision) return;
    if (frame->failure != Frame::Failure::None && frame->revision != m_render_revision) return;
    if (frame->failure != Frame::Failure::None) {
        m_render_pending = false;
        m_render_error = frame->failure == Frame::Failure::Retention
            ? tr("Block clock rendering is unavailable.")
            : frame->failure == Frame::Failure::InvalidInput
                ? tr("The block clock render input is invalid. Check its geometry and retry.")
                : tr("The block clock image could not be rendered. Retry after background work finishes.");
        Q_EMIT renderStateChanged(); return;
    }
    m_frame = frame; m_displayed_revision = frame->revision;
    m_render_pending = frame->revision != m_render_revision; if (!m_capture_rejected) m_render_error.clear();
    if (presentationActive()) update();
    Q_EMIT renderStateChanged();
}

BlockClockDial::BlockClockDial(QQuickItem* parent)
    : BlockClockDial(nullptr, parent) {}

BlockClockDial::BlockClockDial(PresentationServices* services, QQuickItem *parent)
    : QQuickPaintedItem(parent),
      m_animation_timer{this},
      m_delay_timer{this}
{
    m_services.post = presentation::ReliablePost({}); // Until services are assigned.
    connect(this, &QQuickItem::widthChanged, this, &BlockClockDial::requestRepaint);
    connect(this, &QQuickItem::heightChanged, this, &BlockClockDial::requestRepaint);
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
    setServices(services);
}

BlockClockDial::~BlockClockDial()
{
    // QQuickItem detaches from its window after derived members are destroyed.
    // Its visibility/window signals must no longer call back into this object.
    closeAdmission();
    disconnect(this, nullptr, this, nullptr);
    disconnect(m_window_visibility_connection);
}

void BlockClockDial::componentComplete()
{
    QQuickPaintedItem::componentComplete();
    m_component_complete = true;
    if (!m_presentation) initializeNative(); // Reports background rendering as unavailable.
    connectWindowVisibility();
    syncAnimationState();
}

void BlockClockDial::setupConnectingGradient(const QPen & pen)
{
    if (m_closed) return;
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
    if (m_closed) return;
    if (!m_synced_gradient_needs_update && m_synced_gradient_bounds == bounds) {
        return;
    }

    m_synced_gradient_bounds = bounds;
    m_synced_gradient.setCenter(bounds.center());
    m_synced_gradient.setAngle(90);
    // The dial advances clockwise while conical gradients advance
    // counter-clockwise. Mirror the stops so older time is more confirmed and
    // the color moves toward zero confirmations as the dial advances. Scale
    // the stops to the elapsed arc so its newest end is always red, and clear
    // the old stops when the time span changes.
    const qreal span{m_current_time_fraction};
    m_synced_gradient.setStops({});
    m_synced_gradient.setColorAt(0, m_confirmation_colors[0]);
    m_synced_gradient.setColorAt(1.0 - span, m_confirmation_colors[0]);
    m_synced_gradient.setColorAt(1.0 - 0.80 * span, m_confirmation_colors[1]);
    m_synced_gradient.setColorAt(1.0 - 0.64 * span, m_confirmation_colors[2]);
    m_synced_gradient.setColorAt(1.0 - 0.48 * span, m_confirmation_colors[3]);
    m_synced_gradient.setColorAt(1.0 - 0.32 * span, m_confirmation_colors[4]);
    m_synced_gradient.setColorAt(1.0 - 0.16 * span, m_confirmation_colors[5]);
    m_synced_gradient.setColorAt(1, m_confirmation_colors[5]);
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
    requestFrame();
    if (m_closed) return;
    if (presentationActive()) update();
}

void BlockClockDial::advanceAnimation()
{
    if (m_closed) { m_animation_timer.stop(); return; }
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
    if (m_closed) { m_animation_timer.stop(); m_delay_timer.stop(); return; }
    const bool active{presentationActive()};
    const bool becoming_active{active && !m_presentation_was_active};
    m_presentation_was_active = active;

    if (!active || !m_animate_dial || m_is_paused) {
        m_animation_timer.stop();
        m_delay_timer.stop();
        if (!m_animate_dial) m_animating_max_angle = getTargetAnimationAngle();
        // Static and paused dials also need their cached image refreshed after
        // backing data changed while presentation was inactive.
        if (becoming_active) requestRepaint();
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
    if (m_closed) return;
    fraction = qBound<qreal>(0.0, fraction, 1.0);
    if (qFuzzyCompare(m_current_time_fraction + 1.0, fraction + 1.0)) return;
    m_current_time_fraction = fraction;
    invalidateSyncedGradient();
    syncAnimationState();
    requestRepaint();
}

void BlockClockDial::setBlockTimeFractions(QList<qreal> fractions)
{
    if (m_closed || !m_native) return;
    if (fractions.size() > 1024 * 1024) { m_capture_rejected = true; m_render_error = tr("Block clock input is too large."); Q_EMIT renderStateChanged(); return; }
    auto source = execution::RetainSnapshot<blockclock::Projection>(m_services.scheduler, [&] {
        auto value = std::make_unique<blockclock::Projection>(); value->fractions = std::move(fractions); return value;
    });
    if (!source) { m_capture_rejected = true; m_render_error = tr("Block clock input could not be retained. Retry after background work finishes."); Q_EMIT renderStateChanged(); return; }
    m_capture_rejected = false; m_source = std::move(source); m_source_normalized = false;
    requestRepaint();
}
void BlockClockDial::setBlockSnapshot(const QVariant& value)
{
    if (m_closed) return;
    const auto weak = value.value<blockclock::WeakSnapshot>();
    const auto snapshot = weak.lock();
    const blockclock::WeakSnapshot empty;
    if (!snapshot && (weak.owner_before(empty) || empty.owner_before(weak))) {
        m_capture_rejected = true; m_render_error = tr("Block history is no longer available. Refresh the original history before retrying.");
        Q_EMIT renderStateChanged(); return;
    }
    if (snapshot == m_source && m_source_normalized) return;
    m_capture_rejected = false; m_source = snapshot; m_source_normalized = true; requestRepaint();
}

void BlockClockDial::setSyncProgress(double progress)
{
    if (m_closed) return;
    progress = qBound(0.0, progress, 1.0);
    if (qFuzzyCompare(m_sync_progress + 1.0, progress + 1.0)) return;
    m_sync_progress = progress;
    syncAnimationState();
    requestRepaint();
}

void BlockClockDial::setConnected(bool connected)
{
    if (m_closed) return;
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
    if (m_closed) return;
    if (m_is_synced != is_synced) {
        m_is_synced = is_synced;
        m_animating_max_angle = 0;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setPaused(bool paused)
{
    if (m_closed) return;
    if (m_is_paused != paused) {
        m_is_paused = paused;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setAnimateDial(bool animate_dial)
{
    if (m_closed) return;
    if (m_animate_dial != animate_dial) {
        m_animate_dial = animate_dial;
        if (m_animate_dial) m_animating_max_angle = 0;
        syncAnimationState();
        requestRepaint();
    }
}

void BlockClockDial::setRenderingActive(bool active)
{
    if (m_closed) return;
    if (m_rendering_active == active) return;
    m_rendering_active = active;
    Q_EMIT renderingActiveChanged();
    syncAnimationState();
}

void BlockClockDial::setConnectingAnimationDelayMs(int connecting_animation_delay_ms)
{
    if (m_closed) return;
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
    if (m_closed) return;
    if (m_show_time_ticks != show_time_ticks) {
        m_show_time_ticks = show_time_ticks;
        requestRepaint();
    }
}

void BlockClockDial::setShowBlockSegments(bool show_block_segments)
{
    if (m_closed) return;
    if (m_show_block_segments != show_block_segments) {
        m_show_block_segments = show_block_segments;
        requestRepaint();
    }
}

void BlockClockDial::setUseGradientArcWhenSynced(bool use_gradient_arc_when_synced)
{
    if (m_closed) return;
    if (m_use_gradient_arc_when_synced != use_gradient_arc_when_synced) {
        m_use_gradient_arc_when_synced = use_gradient_arc_when_synced;
        requestRepaint();
    }
}

void BlockClockDial::setPenWidth(qreal width)
{
    if (m_closed) return;
    if (m_pen_width != width) {
        m_pen_width = width;
        invalidateSyncedGradient();
        requestRepaint();
    }
}

void BlockClockDial::setScale(qreal scale)
{
    if (m_closed) return;
    if (m_scale != scale) {
        m_scale = scale;
        invalidateSyncedGradient();
        requestRepaint();

        Q_EMIT scaleChanged();
    }
}

void BlockClockDial::setBackgroundColor(QColor color)
{
    if (m_closed) return;
    if (m_background_color == color) return;
    m_background_color = color;
    requestRepaint();
}

void BlockClockDial::setConfirmationColors(QList<QColor> colorList)
{
    if (m_closed) return;
    if (m_confirmation_colors != colorList) {
        m_confirmation_colors = colorList;
        invalidateSyncedGradient();
        requestRepaint();
    }
}

void BlockClockDial::setTimeTickColor(QColor color)
{
    if (m_closed) return;
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
    if (rect.x() != std::floor(rect.x()))
        rect.setX(std::ceil(rect.x()));
    if (rect.y() != std::floor(rect.y()))
        rect.setY(std::ceil(rect.y()));
    if (rect.width() != std::floor(rect.width()))
        rect.setWidth(std::floor(rect.width()));
    if (rect.height() != std::floor(rect.height()))
        rect.setHeight(std::floor(rect.height()));

    return rect;
}

void BlockClockDial::paintBlocks(QPainter* painter)
{
    if (!m_frame || m_frame->epoch != m_content_epoch || m_frame->image.isNull()) return;
    painter->drawImage(boundingRect(), m_frame->image);
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

qreal BlockClockDial::degreesForArcPixels(qreal pixels, const QRectF& bounds) const
{
    const qreal radius{qMin(bounds.width(), bounds.height()) / 2.0};
    if (radius <= 0.0) return 360.0;
    return qRadiansToDegrees(pixels / radius);
}

qreal BlockClockDial::blockSegmentGapPixels() const
{
    // The Figma dial uses a gap around half the stroke width, with one device
    // pixel as the lower bound for small miniatures.
    return qMax<qreal>(1.0, m_pen_width / 2.0);
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
    if (!std::isfinite(width()) || !std::isfinite(height()) || !std::isfinite(m_pen_width) ||
        m_pen_width < 0 || width() <= 0 || height() <= 0) {
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
