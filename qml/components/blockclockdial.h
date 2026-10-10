// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
#define BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H

#include <QQuickPaintedItem>
#include <QConicalGradient>
#include <QMetaObject>
#include <QPainter>
#include <QTimer>
#include <QtGlobal>

/**
 * Paints the current twelve-hour block timeline clockwise from midnight or noon.
 *
 * A point on the dial has one confirmation for each later block boundary. The
 * newest interval therefore has zero confirmations, while older intervals move
 * through the confirmation color palette. Boundaries too close to render as
 * separate rounded segments are coalesced without losing their block count;
 * the resulting segment records both confirmation depths and paints the skipped
 * transitions as a continuous gradient.
 *
 * With layeredRings set, the dial instead paints the widget design: a track
 * with twelve hour dots inside it, block segments between consecutive blocks
 * of the last twelve hours with a hand at the current time, and, while
 * syncing, block progress on the track with header progress on a thin inner
 * ring. In this mode block fractions are chronological and may be negative,
 * for blocks of the previous period, and the ring geometry scales with the
 * item instead of following penWidth.
 */
class BlockClockDial : public QQuickPaintedItem
{
    Q_OBJECT
    /** Current position through the local twelve-hour period, in [0, 1]. */
    Q_PROPERTY(qreal currentTimeFraction READ currentTimeFraction WRITE setCurrentTimeFraction)
    /** Sorted block positions relative to the same period, each in [-1, 1]. */
    Q_PROPERTY(QList<qreal> blockTimeFractions READ blockTimeFractions WRITE setBlockTimeFractions)
    /** Progress of the synchronization phase currently presented by the dial. */
    Q_PROPERTY(double syncProgress READ syncProgress WRITE setSyncProgress)
    Q_PROPERTY(bool connected READ connected WRITE setConnected)
    Q_PROPERTY(bool synced READ synced WRITE setSynced)
    Q_PROPERTY(bool paused READ paused WRITE setPaused)
    Q_PROPERTY(bool animateDial READ animateDial WRITE setAnimateDial)
    Q_PROPERTY(bool renderingActive READ renderingActive WRITE setRenderingActive NOTIFY renderingActiveChanged)
    Q_PROPERTY(int connectingAnimationDelayMs READ connectingAnimationDelayMs WRITE setConnectingAnimationDelayMs)
    Q_PROPERTY(bool showTimeTicks READ showTimeTicks WRITE setShowTimeTicks)
    Q_PROPERTY(bool showBlockSegments READ showBlockSegments WRITE setShowBlockSegments)
    Q_PROPERTY(bool useGradientArcWhenSynced READ useGradientArcWhenSynced WRITE setUseGradientArcWhenSynced)
    Q_PROPERTY(qreal penWidth READ penWidth WRITE setPenWidth)
    Q_PROPERTY(qreal scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(QList<QColor> confirmationColors READ confirmationColors WRITE setConfirmationColors )
    Q_PROPERTY(QColor timeTickColor READ timeTickColor WRITE setTimeTickColor)
    Q_PROPERTY(bool layeredRings READ layeredRings WRITE setLayeredRings)
    /** Layered mode: header download progress on the inner ring, in [0, 1]. */
    Q_PROPERTY(double headerProgress READ headerProgress WRITE setHeaderProgress)
    /** Layered mode: block verification progress on the track, in [0, 1]. */
    Q_PROPERTY(double blockProgress READ blockProgress WRITE setBlockProgress)
    /** Layered mode: grey out rings and the hand while the node is not live. */
    Q_PROPERTY(bool dimmed READ dimmed WRITE setDimmed)
    /** Layered mode: show only the track and hour dots. */
    Q_PROPERTY(bool faulted READ faulted WRITE setFaulted)
    Q_PROPERTY(QColor headerProgressColor READ headerProgressColor WRITE setHeaderProgressColor)
    Q_PROPERTY(QColor progressColor READ progressColor WRITE setProgressColor)
    Q_PROPERTY(QColor handColor READ handColor WRITE setHandColor)
    Q_PROPERTY(QColor dimmedColor READ dimmedColor WRITE setDimmedColor)

public:
    explicit BlockClockDial(QQuickItem * parent = nullptr);
    ~BlockClockDial() override;
    void paint(QPainter * painter) override;

    qreal currentTimeFraction() const { return m_current_time_fraction; }
    QList<qreal> blockTimeFractions() const { return m_block_time_fractions; }
    double syncProgress() const { return m_sync_progress; };
    bool connected() const { return m_is_connected; };
    bool synced() const { return m_is_synced; };
    bool paused() const { return m_is_paused; };
    bool animateDial() const { return m_animate_dial; };
    bool renderingActive() const { return m_rendering_active; }
    int connectingAnimationDelayMs() const { return m_connecting_animation_delay_ms; };
    bool showTimeTicks() const { return m_show_time_ticks; };
    bool showBlockSegments() const { return m_show_block_segments; };
    bool useGradientArcWhenSynced() const { return m_use_gradient_arc_when_synced; };
    qreal penWidth() const { return m_pen_width; };
    qreal scale() const { return m_scale; };
    QColor backgroundColor() const { return m_background_color; };
    QList<QColor> confirmationColors() const { return m_confirmation_colors; };
    QColor timeTickColor() const { return m_time_tick_color; };
    bool layeredRings() const { return m_layered_rings; }
    double headerProgress() const { return m_header_progress; }
    double blockProgress() const { return m_block_progress; }
    bool dimmed() const { return m_dimmed; }
    bool faulted() const { return m_faulted; }
    QColor headerProgressColor() const { return m_header_progress_color; }
    QColor progressColor() const { return m_progress_color; }
    QColor handColor() const { return m_hand_color; }
    QColor dimmedColor() const { return m_dimmed_color; }

public Q_SLOTS:
    void setCurrentTimeFraction(qreal fraction);
    void setBlockTimeFractions(QList<qreal> fractions);
    void setSyncProgress(double progress);
    void setConnected(bool connected);
    void setSynced(bool synced);
    void setPaused(bool paused);
    void setAnimateDial(bool animate_dial);
    void setRenderingActive(bool active);
    void setConnectingAnimationDelayMs(int connecting_animation_delay_ms);
    void setShowTimeTicks(bool show_time_ticks);
    void setShowBlockSegments(bool show_block_segments);
    void setUseGradientArcWhenSynced(bool use_gradient_arc_when_synced);
    void setPenWidth(qreal width);
    void setScale(qreal scale);
    void setBackgroundColor(QColor color);
    void setConfirmationColors(QList<QColor> colorList);
    void setTimeTickColor(QColor color);
    void setLayeredRings(bool layered_rings);
    void setHeaderProgress(double progress);
    void setBlockProgress(double progress);
    void setDimmed(bool dimmed);
    void setFaulted(bool faulted);
    void setHeaderProgressColor(QColor color);
    void setProgressColor(QColor color);
    void setHandColor(QColor color);
    void setDimmedColor(QColor color);

Q_SIGNALS:
    void scaleChanged();
    void renderingActiveChanged();

protected:
    void componentComplete() override;

private:
    /** Drawable interval and confirmation depths around its ending block cluster. */
    struct BlockSegment
    {
        qreal start_fraction;
        qreal end_fraction;
        qsizetype start_confirmations;
        qsizetype end_confirmations;
    };

    void paintConnectingAnimation(QPainter * painter);
    void paintProgress(QPainter * painter);
    void paintCurrentTimeArc(QPainter* painter);
    void paintSyncedGradientArc(QPainter* painter);
    void paintBlocks(QPainter * painter);
    void paintBackground(QPainter * painter);
    void paintTimeTicks(QPainter * painter);
    void paintLayered(QPainter* painter);
    void paintLayeredBlocks(QPainter* painter, qreal unit);
    void paintLayeredProgress(QPainter* painter, qreal unit);
    QRectF circleBounds(qreal radius) const;
    void setColor(QColor& member, const QColor& color);
    QRectF getBoundsForPen(const QPen & pen);
    double degreesPerPixel();
    qreal degreesForArcPixels(qreal pixels, const QRectF& bounds) const;
    qreal blockSegmentGapPixels() const;
    QList<BlockSegment> coalescedBlockSegments(const QRectF& bounds) const;
    void setupConnectingGradient(const QPen & pen);
    void setupSyncedGradient(const QRectF& bounds);
    void invalidateSyncedGradient();
    qreal decrementGradientAngle(qreal angle) const;
    qreal getTargetAnimationAngle() const;
    bool presentationActive() const;
    void requestRepaint();
    void advanceAnimation();
    void syncAnimationState();
    void connectWindowVisibility();

    qreal m_current_time_fraction{0.0};
    QList<qreal> m_block_time_fractions;
    double m_sync_progress{0.0};
    bool m_is_connected{false};
    bool m_is_synced{false};
    bool m_is_paused{false};
    bool m_animate_dial{true};
    bool m_rendering_active{true};
    bool m_component_complete{false};
    bool m_presentation_was_active{false};
    int m_connecting_animation_delay_ms{5000};
    bool m_show_time_ticks{true};
    bool m_show_block_segments{true};
    bool m_use_gradient_arc_when_synced{false};
    qreal m_pen_width{4};
    qreal m_scale{5/12};
    QColor m_background_color{"#2D2D2D"};
    QConicalGradient m_connecting_gradient;
    QConicalGradient m_synced_gradient;
    QRectF m_synced_gradient_bounds;
    bool m_synced_gradient_needs_update{true};
    qreal m_connecting_start_angle = 90;
    const qreal m_connecting_end_angle = -180;
    QList<QColor> m_confirmation_colors{};
    QColor m_time_tick_color{"#000000"};
    bool m_layered_rings{false};
    double m_header_progress{0.0};
    double m_block_progress{0.0};
    bool m_dimmed{false};
    bool m_faulted{false};
    QColor m_header_progress_color{"#3CA3DE"};
    QColor m_progress_color{"#F89B2A"};
    QColor m_hand_color{"#FFFFFF"};
    QColor m_dimmed_color{"#5C5C5C"};
    QTimer m_animation_timer;
    QTimer m_delay_timer;
    QMetaObject::Connection m_window_visibility_connection;
    qreal m_animating_max_angle = 0;
};

#endif // BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
