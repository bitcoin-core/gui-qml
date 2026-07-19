// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/components/blockclockdial.h>

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QtMath>

#include <cmath>

namespace {
constexpr int DIAL_SIZE{120};
const QColor BACKGROUND_COLOR{QStringLiteral("#202020")};
const QList<QColor> CONFIRMATION_COLORS{
    QColor{QStringLiteral("#FF1C1C")},
    QColor{QStringLiteral("#ED6E46")},
    QColor{QStringLiteral("#EE8847")},
    QColor{QStringLiteral("#EFA148")},
    QColor{QStringLiteral("#F0BB49")},
    QColor{QStringLiteral("#F1D54A")},
};

class TestableBlockClockDial : public BlockClockDial
{
public:
    using BlockClockDial::BlockClockDial;
    void complete() { componentComplete(); }
};

int ColorDistance(const QColor& a, const QColor& b)
{
    return std::abs(a.red() - b.red()) +
           std::abs(a.green() - b.green()) +
           std::abs(a.blue() - b.blue());
}

void ConfigureDial(BlockClockDial& dial)
{
    dial.setWidth(DIAL_SIZE);
    dial.setHeight(DIAL_SIZE);
    dial.setPenWidth(12);
    dial.setBackgroundColor(BACKGROUND_COLOR);
    dial.setConfirmationColors(CONFIRMATION_COLORS);
    dial.setTimeTickColor(Qt::black);
    dial.setShowTimeTicks(false);
}

void ConfigureSyncedDial(BlockClockDial& dial, qreal current_fraction,
                         const QList<qreal>& block_fractions)
{
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setCurrentTimeFraction(current_fraction);
    dial.setBlockTimeFractions(block_fractions);
}

QImage RenderDial(BlockClockDial& dial)
{
    QImage image{DIAL_SIZE, DIAL_SIZE, QImage::Format_ARGB32_Premultiplied};
    image.fill(Qt::transparent);

    QPainter painter{&image};
    dial.paint(&painter);
    return image;
}

int CountConfirmationPixels(const QImage& image)
{
    int count{0};
    for (int y{0}; y < image.height(); ++y) {
        for (int x{0}; x < image.width(); ++x) {
            const QColor pixel{image.pixelColor(x, y)};
            if (pixel.alpha() == 0) continue;
            for (const QColor& confirmation_color : CONFIRMATION_COLORS) {
                if (ColorDistance(pixel, confirmation_color) < 80) {
                    ++count;
                    break;
                }
            }
        }
    }
    return count;
}

QPoint DialPoint(qreal fraction)
{
    constexpr qreal center{DIAL_SIZE / 2.0};
    constexpr qreal radius{DIAL_SIZE / 2.0 - 7.0};
    const qreal angle{fraction * 2.0 * M_PI};
    return {qRound(center + qSin(angle) * radius), qRound(center - qCos(angle) * radius)};
}

void VerifyDialColor(const QImage& image, qreal fraction, const QColor& expected)
{
    const QColor actual{image.pixelColor(DialPoint(fraction))};
    QVERIFY2(ColorDistance(actual, expected) < 35,
             qPrintable(QStringLiteral("expected %1 at %2, got %3")
                            .arg(expected.name(QColor::HexArgb))
                            .arg(fraction)
                            .arg(actual.name(QColor::HexArgb))));
}
} // namespace

class BlockClockDialTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ibdProgressRendersImmediateHalfArc();
    void syncedGradientToggleChangesRenderedColors();
    void syncedGradientUpdatesWhenConfirmationColorsChange();
    void connectingDelayControlsInitialAnimation();
    void inactiveDialStopsAnimationAndRetainsLatestState();
    void paintDoesNotAdvanceAnimationState();
    void distinctBlocksUseExactConfirmationColors();
    void coalescedBlocksUseConfirmationGradient();
    void coalescedBlocksSaturateAtHighestConfirmationColor();
    void blocksCoalescedWithCurrentTimePreserveConfirmationDepth();
    void blocksCoalescedWithPeriodStartDoNotShiftVisibleDepth();
};

void BlockClockDialTests::ibdProgressRendersImmediateHalfArc()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(false);
    dial.setSyncProgress(0.5);

    const QImage image{RenderDial(dial)};
    const QColor active_pixel{image.pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};
    const QColor inactive_pixel{image.pixelColor(QPoint{7, DIAL_SIZE / 2})};

    QVERIFY2(ColorDistance(active_pixel, CONFIRMATION_COLORS[5]) < 35,
             qPrintable(QStringLiteral("expected active IBD arc pixel, got %1").arg(active_pixel.name(QColor::HexArgb))));
    QVERIFY2(ColorDistance(inactive_pixel, BACKGROUND_COLOR) < 15,
             qPrintable(QStringLiteral("expected background outside IBD arc, got %1").arg(inactive_pixel.name(QColor::HexArgb))));
}

void BlockClockDialTests::syncedGradientToggleChangesRenderedColors()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setShowBlockSegments(false);
    dial.setCurrentTimeFraction(1.0);

    dial.setUseGradientArcWhenSynced(false);
    const QImage uniform_image{RenderDial(dial)};
    const QColor uniform_right{uniform_image.pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};
    const QColor uniform_bottom{uniform_image.pixelColor(QPoint{DIAL_SIZE / 2, DIAL_SIZE - 7})};
    QVERIFY2(ColorDistance(uniform_right, CONFIRMATION_COLORS[5]) < 35,
             qPrintable(QStringLiteral("expected uniform synced arc color, got %1").arg(uniform_right.name(QColor::HexArgb))));
    QVERIFY2(ColorDistance(uniform_bottom, CONFIRMATION_COLORS[5]) < 35,
             qPrintable(QStringLiteral("expected uniform synced arc color, got %1").arg(uniform_bottom.name(QColor::HexArgb))));

    dial.setUseGradientArcWhenSynced(true);
    const QImage gradient_image{RenderDial(dial)};
    const QColor gradient_right{gradient_image.pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};
    const QColor gradient_bottom{gradient_image.pixelColor(QPoint{DIAL_SIZE / 2, DIAL_SIZE - 7})};
    QVERIFY2(ColorDistance(gradient_right, gradient_bottom) > 25,
             qPrintable(QStringLiteral("expected gradient samples to differ, got %1 and %2")
                            .arg(gradient_right.name(QColor::HexArgb), gradient_bottom.name(QColor::HexArgb))));
}

void BlockClockDialTests::syncedGradientUpdatesWhenConfirmationColorsChange()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setShowBlockSegments(false);
    dial.setUseGradientArcWhenSynced(true);
    dial.setCurrentTimeFraction(1.0);

    const QColor initial_color{RenderDial(dial).pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};

    const QList<QColor> updated_colors{
        QColor{QStringLiteral("#3399FF")},
        QColor{QStringLiteral("#33CCFF")},
        QColor{QStringLiteral("#33FFCC")},
        QColor{QStringLiteral("#99FF33")},
        QColor{QStringLiteral("#FFFF33")},
        QColor{QStringLiteral("#FFFFFF")},
    };
    dial.setConfirmationColors(updated_colors);

    const QColor updated_color{RenderDial(dial).pixelColor(QPoint{DIAL_SIZE - 7, DIAL_SIZE / 2})};
    QVERIFY2(ColorDistance(initial_color, updated_color) > 50,
             qPrintable(QStringLiteral("expected gradient color to update, got %1 and %2")
                            .arg(initial_color.name(QColor::HexArgb), updated_color.name(QColor::HexArgb))));
}

void BlockClockDialTests::connectingDelayControlsInitialAnimation()
{
    TestableBlockClockDial delayed_dial;
    ConfigureDial(delayed_dial);
    delayed_dial.setConnected(false);
    delayed_dial.setAnimateDial(true);
    delayed_dial.setConnectingAnimationDelayMs(5000);
    delayed_dial.complete();
    QCOMPARE(CountConfirmationPixels(RenderDial(delayed_dial)), 0);

    TestableBlockClockDial immediate_dial;
    ConfigureDial(immediate_dial);
    immediate_dial.setConnected(false);
    immediate_dial.setAnimateDial(true);
    immediate_dial.setConnectingAnimationDelayMs(0);
    immediate_dial.complete();

    QTRY_VERIFY_WITH_TIMEOUT(CountConfirmationPixels(RenderDial(immediate_dial)) > 0, 250);
}

void BlockClockDialTests::inactiveDialStopsAnimationAndRetainsLatestState()
{
    TestableBlockClockDial dial;
    ConfigureDial(dial);
    dial.setConnectingAnimationDelayMs(0);
    dial.complete();
    QTest::qWait(20);

    QTimer* animation_timer{nullptr};
    for (QTimer* timer : dial.findChildren<QTimer*>()) {
        if (timer->interval() == 16) animation_timer = timer;
    }
    QVERIFY(animation_timer);
    QVERIFY(animation_timer->isActive());

    dial.setRenderingActive(false);
    QVERIFY(!animation_timer->isActive());

    dial.setCurrentTimeFraction(0.75);
    dial.setConnected(true);
    dial.setSynced(true);
    QCOMPARE(dial.currentTimeFraction(), 0.75);

    dial.setRenderingActive(true);
    QVERIFY(!animation_timer->isActive());
    const QImage image{RenderDial(dial)};
    QVERIFY(CountConfirmationPixels(image) > 0);
}

void BlockClockDialTests::paintDoesNotAdvanceAnimationState()
{
    TestableBlockClockDial dial;
    ConfigureDial(dial);
    dial.setRenderingActive(false);
    dial.complete();

    const QImage first{RenderDial(dial)};
    const QImage second{RenderDial(dial)};
    QCOMPARE(first, second);
}

void BlockClockDialTests::distinctBlocksUseExactConfirmationColors()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.25, 0.50});

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.125, CONFIRMATION_COLORS[2]);
    VerifyDialColor(image, 0.375, CONFIRMATION_COLORS[1]);
    VerifyDialColor(image, 0.625, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::coalescedBlocksUseConfirmationGradient()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.25, 0.2501, 0.2502});

    const QImage image{RenderDial(dial)};
    const QColor older_color{image.pixelColor(DialPoint(0.05))};
    const QColor middle_color{image.pixelColor(DialPoint(0.125))};
    const QColor newer_color{image.pixelColor(DialPoint(0.20))};

    QCOMPARE(dial.blockTimeFractions().size(), 3);
    QVERIFY(ColorDistance(older_color, CONFIRMATION_COLORS[3]) <
            ColorDistance(older_color, CONFIRMATION_COLORS[0]));
    QVERIFY(ColorDistance(newer_color, CONFIRMATION_COLORS[0]) <
            ColorDistance(newer_color, CONFIRMATION_COLORS[3]));
    QVERIFY(older_color.green() > middle_color.green());
    QVERIFY(middle_color.green() > newer_color.green());
    QVERIFY(ColorDistance(older_color, newer_color) > 50);
    VerifyDialColor(image, 0.50, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::coalescedBlocksSaturateAtHighestConfirmationColor()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.25, 0.2501, 0.2502, 0.2503, 0.2504, 0.2505});

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.025, CONFIRMATION_COLORS[5]);
    VerifyDialColor(image, 0.50, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::blocksCoalescedWithCurrentTimePreserveConfirmationDepth()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.7498, 0.7499});

    const QImage image{RenderDial(dial)};
    const QColor older_color{image.pixelColor(DialPoint(0.10))};
    const QColor current_color{image.pixelColor(DialPoint(0.70))};

    QVERIFY(ColorDistance(older_color, CONFIRMATION_COLORS[2]) <
            ColorDistance(older_color, CONFIRMATION_COLORS[0]));
    QVERIFY(ColorDistance(current_color, CONFIRMATION_COLORS[0]) <
            ColorDistance(current_color, CONFIRMATION_COLORS[2]));
}

void BlockClockDialTests::blocksCoalescedWithPeriodStartDoNotShiftVisibleDepth()
{
    BlockClockDial dial;
    ConfigureSyncedDial(dial, 0.75, {0.0001, 0.25});

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.125, CONFIRMATION_COLORS[1]);
    VerifyDialColor(image, 0.50, CONFIRMATION_COLORS[0]);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(BlockClockDialTests)
#else
QTEST_MAIN(BlockClockDialTests)
#endif
#include "test_blockclockdial.moc"
