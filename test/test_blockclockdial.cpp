// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/components/blockclockdial.h>

#include <QImage>
#include <QPainter>
#include <QPoint>

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
} // namespace

class BlockClockDialTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void ibdProgressRendersImmediateHalfArc();
    void syncedGradientToggleChangesRenderedColors();
    void connectingDelayControlsInitialAnimation();
};

void BlockClockDialTests::ibdProgressRendersImmediateHalfArc()
{
    BlockClockDial dial;
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(false);
    dial.setVerificationProgress(0.5);

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
    dial.setTimeRatioList({1.0, 0.0});

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

void BlockClockDialTests::connectingDelayControlsInitialAnimation()
{
    BlockClockDial delayed_dial;
    ConfigureDial(delayed_dial);
    delayed_dial.setConnected(false);
    delayed_dial.setAnimateDial(true);
    delayed_dial.setConnectingAnimationDelayMs(5000);
    QCOMPARE(CountConfirmationPixels(RenderDial(delayed_dial)), 0);

    BlockClockDial immediate_dial;
    ConfigureDial(immediate_dial);
    immediate_dial.setConnected(false);
    immediate_dial.setAnimateDial(true);
    immediate_dial.setConnectingAnimationDelayMs(0);

    RenderDial(immediate_dial);
    QVERIFY(CountConfirmationPixels(RenderDial(immediate_dial)) > 0);
}

int RunBlockClockDialTests(int argc, char* argv[])
{
    BlockClockDialTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#ifndef BITCOINQML_NO_TEST_MAIN
QTEST_MAIN(BlockClockDialTests)
#endif
#include "test_blockclockdial.moc"
