// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
#define BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H

#include <QQuickPaintedItem>
#include <QPainter>

class BlockClockDial : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QVariantList timeRatioList READ timeRatioList WRITE setTimeRatioList)
    Q_PROPERTY(double verificationProgress READ verificationProgress WRITE setVerificationProgress)
    Q_PROPERTY(bool synced READ synced WRITE setSynced)
    Q_PROPERTY(bool paused READ paused WRITE setPaused)
    Q_PROPERTY(QColor backgroundColor READ backgroundColor WRITE setBackgroundColor)
    Q_PROPERTY(QColor timeTickColor READ timeTickColor WRITE setTimeTickColor)

public:
    explicit BlockClockDial(QQuickItem * parent = nullptr);
    void paint(QPainter * painter) override;

    QVariantList timeRatioList() const { return m_time_ratio_list; };
    double verificationProgress() const { return m_verification_progress; };
    bool synced() const { return m_is_synced; };
    bool paused() const { return m_is_paused; };
    QColor backgroundColor() const { return m_background_color; };
    QColor timeTickColor() const { return m_time_tick_color; };

public Q_SLOTS:
    void setTimeRatioList(QVariantList new_time);
    void setVerificationProgress(double progress);
    void setSynced(bool synced);
    void setPaused(bool paused);
    void setBackgroundColor(QColor color);
    void setTimeTickColor(QColor color);

private:
    void paintProgress(QPainter * painter);
    void paintBlocks(QPainter * painter);
    void paintBackground(QPainter * painter);
    void paintTimeTicks(QPainter * painter);
    QRectF getBoundsForPen(const QPen & pen);
    double degreesPerPixel();

    QVariantList m_time_ratio_list;
    double m_verification_progress;
    bool m_is_synced;
    bool m_is_paused;
    QColor m_background_color;
    QColor m_time_tick_color;
};

#endif // BITCOIN_QML_COMPONENTS_BLOCKCLOCKDIAL_H
