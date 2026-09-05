// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
#define BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H

#include <QQuickPaintedItem>
#include <QConicalGradient>
#include <QPainter>
#include <QTimer>
#include <QtGlobal>

class BlockClockDial : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList timeRatioList READ timeRatioList WRITE setTimeRatioList)
    Q_PROPERTY(double verificationProgress READ verificationProgress WRITE setVerificationProgress)
    Q_PROPERTY(bool connected READ connected WRITE setConnected)
    Q_PROPERTY(bool synced READ synced WRITE setSynced)
    Q_PROPERTY(bool paused READ paused WRITE setPaused)
    Q_PROPERTY(bool animateDial READ animateDial WRITE setAnimateDial)
    Q_PROPERTY(int connectingAnimationDelayMs READ connectingAnimationDelayMs WRITE setConnectingAnimationDelayMs)
    Q_PROPERTY(bool showTimeTicks READ showTimeTicks WRITE setShowTimeTicks)
    Q_PROPERTY(bool showBlockSegments READ showBlockSegments WRITE setShowBlockSegments)
    Q_PROPERTY(bool useGradientArcWhenSynced READ useGradientArcWhenSynced WRITE setUseGradientArcWhenSynced)
    Q_PROPERTY(qreal penWidth READ penWidth WRITE setPenWidth)
    Q_PROPERTY(qreal scale READ scale WRITE setScale NOTIFY scaleChanged)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(QList<QColor> confirmationColors READ confirmationColors WRITE setConfirmationColors )
    Q_PROPERTY(QColor timeTickColor READ timeTickColor WRITE setTimeTickColor)

public:
    explicit BlockClockDial(QQuickItem * parent = nullptr);
    void paint(QPainter * painter) override;

    QVariantList timeRatioList() const { return m_time_ratio_list; };
    double verificationProgress() const { return m_verification_progress; };
    bool connected() const { return m_is_connected; };
    bool synced() const { return m_is_synced; };
    bool paused() const { return m_is_paused; };
    bool animateDial() const { return m_animate_dial; };
    int connectingAnimationDelayMs() const { return m_connecting_animation_delay_ms; };
    bool showTimeTicks() const { return m_show_time_ticks; };
    bool showBlockSegments() const { return m_show_block_segments; };
    bool useGradientArcWhenSynced() const { return m_use_gradient_arc_when_synced; };
    qreal penWidth() const { return m_pen_width; };
    qreal scale() const { return m_scale; };
    QColor backgroundColor() const { return m_background_color; };
    QList<QColor> confirmationColors() const { return m_confirmation_colors; };
    QColor timeTickColor() const { return m_time_tick_color; };

public Q_SLOTS:
    void setTimeRatioList(QVariantList new_time);
    void setVerificationProgress(double progress);
    void setConnected(bool connected);
    void setSynced(bool synced);
    void setPaused(bool paused);
    void setAnimateDial(bool animate_dial);
    void setConnectingAnimationDelayMs(int connecting_animation_delay_ms);
    void setShowTimeTicks(bool show_time_ticks);
    void setShowBlockSegments(bool show_block_segments);
    void setUseGradientArcWhenSynced(bool use_gradient_arc_when_synced);
    void setPenWidth(qreal width);
    void setScale(qreal scale);
    void setBackgroundColor(QColor color);
    void setConfirmationColors(QList<QColor> colorList);
    void setTimeTickColor(QColor color);

Q_SIGNALS:
    void scaleChanged();

private:
    void paintConnectingAnimation(QPainter * painter);
    void paintProgress(QPainter * painter);
    void paintCurrentTimeArc(QPainter* painter);
    void paintSyncedGradientArc(QPainter* painter);
    void paintBlocks(QPainter * painter);
    void paintBackground(QPainter * painter);
    void paintTimeTicks(QPainter * painter);
    QRectF getBoundsForPen(const QPen & pen);
    double degreesPerPixel();
    void setupConnectingGradient(const QPen & pen);
    void setupSyncedGradient(const QRectF& bounds);
    void invalidateSyncedGradient();
    qreal decrementGradientAngle(qreal angle);
    qreal incrementAnimatingMaxAngle(qreal angle);
    qreal getTargetAnimationAngle();

    QVariantList m_time_ratio_list{0.0};
    double m_verification_progress{0.0};
    bool m_is_connected{false};
    bool m_is_synced{false};
    bool m_is_paused{false};
    bool m_animate_dial{true};
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
    QTimer m_animation_timer{this};
    QTimer m_delay_timer;
    qreal m_animating_max_angle = 0;
};

#endif // BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
