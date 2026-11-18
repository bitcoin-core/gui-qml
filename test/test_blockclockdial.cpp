// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/components/blockclockdial.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>

#include <QImage>
#include <QPainter>
#include <QPoint>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QtMath>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <vector>

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
    void paint(QPainter* painter) override
    {
        BlockClockDial::paint(painter);
        ++paint_count;
    }
    std::atomic<int> paint_count{0};
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
    if (!QTest::qWaitFor([&] { return !dial.renderPending(); }, 10000) || !dial.renderError().isEmpty()) {
        const auto message = QStringLiteral("Native block overlay did not complete: pending=%1 error=%2")
            .arg(dial.renderPending()).arg(dial.renderError());
        QTest::qFail(qPrintable(message), __FILE__, __LINE__); return {};
    }
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
    QVERIFY2(ColorDistance(actual, expected) < 10,
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
    void initTestCase();
    void cleanupTestCase();
    void manualRendererCoalescesLatestViewportAndRejectsLatePublication();
    void servicesCloseTheirDials();
    void oversizedViewportUsesBoundedRasterWithoutDroppingBlocks();
    void invalidPenGeometryReportsNativeFailureAndRecovers_data();
    void invalidPenGeometryReportsNativeFailureAndRecovers();
    void heldEarlierFailureKeepsNewerRenderPending();
    void ibdProgressRendersImmediateHalfArc();
    void syncedGradientToggleChangesRenderedColors();
    void syncedGradientRunsFromOlderToNewerConfirmationColors();
    void syncedGradientSpansCurrentPeriodAndUpdatesWithTime();
    void syncedGradientUpdatesWhenConfirmationColorsChange();
    void connectingDelayControlsInitialAnimation();
    void inactiveDialStopsAnimationAndRetainsLatestState();
    void reactivatedStaticDialRequestsRepaint_data();
    void reactivatedStaticDialRequestsRepaint();
    void paintDoesNotAdvanceAnimationState();
    void equalBlockTimestampsPreserveConfirmationDepth();
    void distinctBlocksUseExactConfirmationColors();
    void futureBlocksPreserveConfirmationDepth_data();
    void futureBlocksPreserveConfirmationDepth();
    void coalescedBlocksUseConfirmationGradient();
    void coalescedBlocksSaturateAtHighestConfirmationColor();
    void blocksCoalescedWithCurrentTimePreserveConfirmationDepth();
    void blocksCoalescedWithPeriodStartDoNotShiftVisibleDepth();
private:
    std::unique_ptr<BackendExecutor> m_executor;
    std::unique_ptr<PresentationServices> m_services;
};

void BlockClockDialTests::initTestCase()
{
    m_executor = std::make_unique<BackendExecutor>(execution::Options{});
    m_services = std::make_unique<PresentationServices>(PresentationServices::Ports{m_executor->scheduler(), {}, {}, {}});
}
void BlockClockDialTests::cleanupTestCase()
{
    m_services->close(); m_services.reset();
    bool drained{false}; m_executor->whenDrained(this, [&] { drained = true; }); m_executor->shutdown();
    QTRY_VERIFY(drained); m_executor.reset();
}

void BlockClockDialTests::manualRendererCoalescesLatestViewportAndRejectsLatePublication()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> publications;
    const auto publish_all = [&] { while (!publications.empty()) { auto publish = std::move(publications.front()); publications.pop_front(); publish(); } };
    PresentationServices services{{env.scheduler, [&](auto publish) { publications.push_back(std::move(publish)); }, {}, {}}};
    BlockClockDial dial{&services};
    ConfigureSyncedDial(dial, 0.75, {0.5, 0.25, 0.5});
    env.drain(); // Native normalized and rendered original geometry; GUI output remains held.
    QVERIFY(dial.renderPending());
    dial.setWidth(240);
    publish_all(); QVERIFY(dial.renderPending()); // The old viewport's image cannot become current.
    for (int turn = 0; turn < 32 && (!env.queuedIdle() || !publications.empty()); ++turn) { env.drain(); publish_all(); }
    QVERIFY(!dial.renderPending()); QVERIFY(dial.renderError().isEmpty());
    QCOMPARE(dial.renderedPixelSize(), QSize(240, DIAL_SIZE));
    const QList<qreal> expected{0.25, 0.5, 0.5}; QCOMPARE(dial.blockTimeFractions(), expected);
    dial.setWidth(241);
    const auto active = env.dispatchNext(execution::Lane::Domain); QVERIFY(active);
    for (int width = 242; width < 400; ++width) dial.setWidth(width);
    QCOMPARE(env.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Domain && turn.phase == execution::Phase::Queued; }), 0);
    QVERIFY(env.finishWork(*active));
    for (int turn = 0; turn < 32 && (!env.queuedIdle() || !publications.empty()); ++turn) { env.drain(); publish_all(); }
    QCOMPARE(dial.renderedPixelSize(), QSize(399, DIAL_SIZE));
    QSignalSpy changes{&dial, &BlockClockDial::renderStateChanged};
    dial.setWidth(400); env.drain(); dial.closeAdmission();
    const int before = changes.count(); publish_all(); env.drain();
    QCOMPARE(changes.count(), before); QVERIFY(env.quiescent());
}
void BlockClockDialTests::servicesCloseTheirDials()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> publications;
    const auto publish_all = [&] { while (!publications.empty()) { auto publish = std::move(publications.front()); publications.pop_front(); publish(); } };
    PresentationServices services{{env.scheduler, [&](auto publish) { publications.push_back(std::move(publish)); }, {}, {}}};
    BlockClockDial dial{&services};
    QCOMPARE(dial.services(), &services);
    ConfigureSyncedDial(dial, 0.75, {0.5, 0.25, 0.5});
    env.drain();
    QVERIFY(dial.renderPending());
    QSignalSpy changes{&dial, &BlockClockDial::renderStateChanged};
    services.close(); // Closes the dial through closed().
    const int before = changes.count(); publish_all(); env.drain();
    QCOMPARE(changes.count(), before); QVERIFY(env.quiescent()); QVERIFY(!dial.renderAvailable());
    // A dial given closed services starts closed and queues no background work.
    BlockClockDial late{&services};
    ConfigureSyncedDial(late, 0.75, {0.5});
    QVERIFY(env.queuedIdle()); QVERIFY(publications.empty()); QVERIFY(!late.renderPending());
}
void BlockClockDialTests::invalidPenGeometryReportsNativeFailureAndRecovers_data()
{
    QTest::addColumn<qreal>("pen");
    QTest::newRow("negative") << qreal{-1};
    QTest::newRow("not-finite") << std::numeric_limits<qreal>::quiet_NaN();
    QTest::newRow("infinite") << std::numeric_limits<qreal>::infinity();
}
void BlockClockDialTests::invalidPenGeometryReportsNativeFailureAndRecovers()
{
    QFETCH(qreal, pen);
#if QT_VERSION >= QT_VERSION_CHECK(6, 3, 0)
    QTest::failOnWarning(QRegularExpression{QStringLiteral("^QPen::setWidthF:.*")});
#endif
    ManualRoutedExecution env;
    std::deque<std::function<void()>> publications;
    const auto settle = [&] {
        for (int turn = 0; turn < 32 && (!env.queuedIdle() || !publications.empty()); ++turn) {
            env.drain();
            while (!publications.empty()) {
                auto publish = std::move(publications.front()); publications.pop_front(); publish();
            }
        }
    };
    PresentationServices services{{env.scheduler, [&](auto publish) { publications.push_back(std::move(publish)); }, {}, {}}};
    BlockClockDial dial{&services};
    ConfigureSyncedDial(dial, 0.75, {0.5, 0.25}); settle();
    QVERIFY(dial.renderAvailable());
    dial.setPenWidth(pen);
    QImage empty{DIAL_SIZE, DIAL_SIZE, QImage::Format_ARGB32_Premultiplied};
    empty.fill(Qt::transparent);
    {
        QPainter painter{&empty};
        dial.paint(&painter); // No Qt pen construction for invalid presentation input.
    }
    QCOMPARE(empty.pixelColor(DIAL_SIZE / 2, DIAL_SIZE / 2), QColor(Qt::transparent));
    env.drain(); QVERIFY(dial.renderPending());
    settle(); QVERIFY(!dial.renderPending()); QVERIFY(!dial.renderError().isEmpty());
    QVERIFY(!dial.renderAvailable());
    dial.setPenWidth(12); settle();
    QVERIFY(dial.renderError().isEmpty()); QVERIFY(dial.renderAvailable());
    dial.closeAdmission(); settle(); QVERIFY(env.quiescent());
}
void BlockClockDialTests::heldEarlierFailureKeepsNewerRenderPending()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> publications;
    const auto publish_all = [&] { while (!publications.empty()) { auto publish = std::move(publications.front()); publications.pop_front(); publish(); } };
    PresentationServices services{{env.scheduler, [&](auto publish) { publications.push_back(std::move(publish)); }, {}, {}}};
    BlockClockDial dial{&services};
    ConfigureSyncedDial(dial, 0.75, {std::numeric_limits<qreal>::quiet_NaN()});
    env.drain(); // R1 typed failure exists; its GUI notice remains held.
    dial.retryRendering(); // Same content epoch, newer R2 is admitted but not executed.
    publish_all();
    QVERIFY(dial.renderPending()); QVERIFY(dial.renderError().isEmpty());
    env.drain(); publish_all();
    QVERIFY(!dial.renderPending()); QVERIFY(!dial.renderError().isEmpty());
    dial.closeAdmission(); env.drain(); publish_all(); env.drain(); QVERIFY(env.quiescent());
}
void BlockClockDialTests::oversizedViewportUsesBoundedRasterWithoutDroppingBlocks()
{
    BlockClockDial dial{m_services.get()}; ConfigureSyncedDial(dial, 0.75, {0.25, 0.5, 0.5});
    dial.setWidth(1.0e12); dial.setHeight(1.0e12);
    QVERIFY(QTest::qWaitFor([&] { return !dial.renderPending(); }, 10000));
    QVERIFY(dial.renderError().isEmpty());
    const auto pixels = dial.renderedPixelSize();
    QVERIFY(pixels.width() > 0 && pixels.height() > 0);
    QVERIFY(pixels.width() <= 1024 && pixels.height() <= 1024);
    QVERIFY(qint64(pixels.width()) * pixels.height() <= 512 * 1024);
    QCOMPARE(dial.blockTimeFractions(), (QList<qreal>{0.25, 0.5, 0.5}));
}

void BlockClockDialTests::ibdProgressRendersImmediateHalfArc()
{
    BlockClockDial dial{m_services.get()};
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
    BlockClockDial dial{m_services.get()};
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

void BlockClockDialTests::syncedGradientRunsFromOlderToNewerConfirmationColors()
{
    BlockClockDial dial{m_services.get()};
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setShowBlockSegments(false);
    dial.setUseGradientArcWhenSynced(true);
    dial.setCurrentTimeFraction(0.75);

    const QImage image{RenderDial(dial)};
    const QColor older_color{image.pixelColor(DialPoint(0.05))};
    const QColor middle_color{image.pixelColor(DialPoint(0.375))};
    const QColor newer_color{image.pixelColor(DialPoint(0.70))};

    QVERIFY(ColorDistance(older_color, CONFIRMATION_COLORS[5]) <
            ColorDistance(older_color, CONFIRMATION_COLORS[0]));
    QVERIFY(ColorDistance(newer_color, CONFIRMATION_COLORS[0]) <
            ColorDistance(older_color, CONFIRMATION_COLORS[0]));
    QVERIFY(ColorDistance(older_color, CONFIRMATION_COLORS[5]) <
            ColorDistance(newer_color, CONFIRMATION_COLORS[5]));
    QVERIFY(older_color.green() > middle_color.green());
    QVERIFY(middle_color.green() > newer_color.green());
}

void BlockClockDialTests::syncedGradientUpdatesWhenConfirmationColorsChange()
{
    BlockClockDial dial{m_services.get()};
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

void BlockClockDialTests::syncedGradientSpansCurrentPeriodAndUpdatesWithTime()
{
    BlockClockDial dial{m_services.get()};
    ConfigureSyncedDial(dial, 1.0, {});
    dial.setShowBlockSegments(false);
    dial.setUseGradientArcWhenSynced(true);

    // Reuse the dial across time changes, including a period rollover, so stale
    // cached gradients or stops from a previous span cannot pass this test.
    for (const qreal current : {0.25, 0.50, 0.75, 1.0, 0.25}) {
        dial.setCurrentTimeFraction(current);
        const QImage image{RenderDial(dial)};
        VerifyDialColor(image, current * 0.08, CONFIRMATION_COLORS[5]);
        VerifyDialColor(image, current * 0.32, CONFIRMATION_COLORS[4]);
        VerifyDialColor(image, current * 0.48, CONFIRMATION_COLORS[3]);
        VerifyDialColor(image, current * 0.64, CONFIRMATION_COLORS[2]);
        VerifyDialColor(image, current * 0.80, CONFIRMATION_COLORS[1]);
        VerifyDialColor(image, current * 0.995, CONFIRMATION_COLORS[0]);
        if (current < 1.0) VerifyDialColor(image, current + 0.10, BACKGROUND_COLOR);
    }
}

void BlockClockDialTests::connectingDelayControlsInitialAnimation()
{
    TestableBlockClockDial delayed_dial{m_services.get()};
    ConfigureDial(delayed_dial);
    delayed_dial.setConnected(false);
    delayed_dial.setAnimateDial(true);
    delayed_dial.setConnectingAnimationDelayMs(5000);
    delayed_dial.complete();
    QCOMPARE(CountConfirmationPixels(RenderDial(delayed_dial)), 0);

    TestableBlockClockDial immediate_dial{m_services.get()};
    ConfigureDial(immediate_dial);
    immediate_dial.setConnected(false);
    immediate_dial.setAnimateDial(true);
    immediate_dial.setConnectingAnimationDelayMs(0);
    immediate_dial.complete();

    QTRY_VERIFY_WITH_TIMEOUT(CountConfirmationPixels(RenderDial(immediate_dial)) > 0, 250);
}

void BlockClockDialTests::inactiveDialStopsAnimationAndRetainsLatestState()
{
    TestableBlockClockDial dial{m_services.get()};
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

void BlockClockDialTests::reactivatedStaticDialRequestsRepaint_data()
{
    QTest::addColumn<bool>("paused");
    QTest::newRow("non-animated") << false;
    QTest::newRow("paused") << true;
}

void BlockClockDialTests::reactivatedStaticDialRequestsRepaint()
{
    QFETCH(bool, paused);
    QQuickWindow window;
    window.resize(DIAL_SIZE, DIAL_SIZE);
    TestableBlockClockDial dial{m_services.get(), window.contentItem()};
    ConfigureDial(dial);
    dial.setAnimateDial(paused);
    dial.setPaused(paused);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setShowBlockSegments(false);
    dial.setCurrentTimeFraction(0.25);
    dial.complete();
    window.show();
    QTRY_VERIFY(dial.paint_count.load() > 0);
    QTest::qWait(50);

    dial.setRenderingActive(false);
    const int paints_before{dial.paint_count.load()};
    dial.setCurrentTimeFraction(0.75);
    QTest::qWait(50);
    QCOMPARE(dial.paint_count.load(), paints_before);

    dial.setRenderingActive(true);
    QTRY_VERIFY(dial.paint_count.load() > paints_before);
    for (const QTimer* timer : dial.findChildren<QTimer*>()) {
        QVERIFY(!timer->isActive());
    }
}

void BlockClockDialTests::equalBlockTimestampsPreserveConfirmationDepth()
{
    BlockClockDial dial{m_services.get()};
    ConfigureDial(dial);
    dial.setAnimateDial(false);
    dial.setConnected(true);
    dial.setSynced(true);
    dial.setCurrentTimeFraction(0.75);
    dial.setBlockTimeFractions({0.25, 0.50, 0.50});
    QCOMPARE(dial.blockTimeFractions().size(), 3);
    const QImage image{RenderDial(dial)};
    QVERIFY(ColorDistance(image.pixelColor(DialPoint(0.125)), CONFIRMATION_COLORS[3]) < 10);
    QVERIFY(ColorDistance(image.pixelColor(DialPoint(0.625)), CONFIRMATION_COLORS[0]) < 10);
}

void BlockClockDialTests::paintDoesNotAdvanceAnimationState()
{
    TestableBlockClockDial dial{m_services.get()};
    ConfigureDial(dial);
    dial.setRenderingActive(false);
    dial.complete();

    const QImage first{RenderDial(dial)};
    const QImage second{RenderDial(dial)};
    QCOMPARE(first, second);
}

void BlockClockDialTests::distinctBlocksUseExactConfirmationColors()
{
    BlockClockDial dial{m_services.get()};
    ConfigureSyncedDial(dial, 0.90, {0.15, 0.30, 0.45, 0.60, 0.75});

    const QImage image{RenderDial(dial)};
    for (int i{0}; i < 6; ++i) {
        VerifyDialColor(image, 0.075 + 0.15 * i, CONFIRMATION_COLORS[5 - i]);
    }
}

void BlockClockDialTests::futureBlocksPreserveConfirmationDepth_data()
{
    QTest::addColumn<QList<qreal>>("blocks");
    QTest::addColumn<int>("future_confirmations");

    QTest::newRow("ahead-of-now") << QList<qreal>{0.25, 0.50, 0.76} << 1;
    QTest::newRow("equal-future-timestamps") << QList<qreal>{0.25, 0.50, 0.76, 0.76} << 2;
    QTest::newRow("exactly-now") << QList<qreal>{0.25, 0.50, 0.75} << 1;
    QTest::newRow("period-end") << QList<qreal>{0.25, 0.50, 1.0} << 1;
    QTest::newRow("several-future-blocks") << QList<qreal>{0.25, 0.50, 0.76, 0.80} << 2;
    QTest::newRow("saturated") << QList<qreal>{0.25, 0.50, 0.76, 0.80, 0.85, 0.90, 0.95, 1.0} << 6;
}

void BlockClockDialTests::futureBlocksPreserveConfirmationDepth()
{
    QFETCH(QList<qreal>, blocks);
    QFETCH(int, future_confirmations);
    BlockClockDial dial{m_services.get()};
    ConfigureSyncedDial(dial, 0.75, blocks);

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.125, CONFIRMATION_COLORS[qMin(5, future_confirmations + 2)]);
    VerifyDialColor(image, 0.375, CONFIRMATION_COLORS[qMin(5, future_confirmations + 1)]);
    VerifyDialColor(image, 0.625, CONFIRMATION_COLORS[qMin(5, future_confirmations)]);
    VerifyDialColor(image, 0.875, BACKGROUND_COLOR);
}

void BlockClockDialTests::coalescedBlocksUseConfirmationGradient()
{
    BlockClockDial dial{m_services.get()};
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
    BlockClockDial dial{m_services.get()};
    ConfigureSyncedDial(dial, 0.75, {0.25, 0.2501, 0.2502, 0.2503, 0.2504, 0.2505});

    const QImage image{RenderDial(dial)};
    VerifyDialColor(image, 0.025, CONFIRMATION_COLORS[5]);
    VerifyDialColor(image, 0.50, CONFIRMATION_COLORS[0]);
}

void BlockClockDialTests::blocksCoalescedWithCurrentTimePreserveConfirmationDepth()
{
    BlockClockDial dial{m_services.get()};
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
    BlockClockDial dial{m_services.get()};
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
#include <test_blockclockdial.moc>
