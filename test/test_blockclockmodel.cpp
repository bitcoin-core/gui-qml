// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockclockhistory.h>
#include <qml/models/blockclockmodel.h>

#include <kernel/chain.h>
#include <kernel/types.h>
#include <primitives/block.h>
#include <test/qt_test_registry.h>
#include <uint256.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include <QtTest/QtTest>
#include <QEvent>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QTimeZone>

namespace {
class QueuedDeliveryCounter : public QObject
{
public:
    int deliveries{0};
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::MetaCall) ++deliveries;
        return false;
    }
};

QDateTime UtcTime(int hour, int minute = 0, int second = 0)
{
    return QDateTime{QDate{2026, 7, 17}, QTime{hour, minute, second}, QTimeZone::utc()};
}

void ConnectBlock(BlockClockHistory& history, uint8_t id, qint64 timestamp)
{
    CBlock block;
    block.nTime = static_cast<uint32_t>(timestamp);
    const uint256 hash{id};
    interfaces::BlockInfo info{hash};
    info.data = &block;
    history.blockConnected(kernel::ChainstateRole{}, info);
}

void DisconnectBlock(BlockClockHistory& history, uint8_t id, qint64 timestamp)
{
    CBlock block;
    block.nTime = static_cast<uint32_t>(timestamp);
    const uint256 hash{id};
    interfaces::BlockInfo info{hash};
    info.data = &block;
    history.blockDisconnected(info);
}
} // namespace

class BlockClockModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void currentTimeUsesNamedTwelveHourFraction();
    void unchangedSecondDoesNotRepublishCurrentTime();
    void blockArrivalRefreshesCurrentTime();
    void blockHistoryPreservesEqualTimestampsAndPeriodBounds();
    void initialHistoryCanArriveAfterTheModel();
    void historyInitializedBeforeModelIsPublished();
    void workerNotificationsPublishWithoutHistoryTimer();
    void notificationDuringPublicationSchedulesFollowup();
    void idleHistoryDoesNotScheduleRepeatedUpdates();
    void destroyedModelIgnoresPendingAndLateWorkerNotifications();
    void cachedHistorySurvivesPeriodRollover();
    void blockNotificationsAreCoalesced();
    void repeatedBlockNotificationDoesNotDuplicateHistory();
    void disconnectRemovesOnlyTheMatchingBlock();
    void oldIbdNotificationsDoNotRepublishHistory();
    void blockArrivalAtRolloverIsNotCountedTwice();
    void emptyHistoryDoesNotEmitRedundantChanges();
    void timersHaveModelOwnershipAndCanBeDisabledForTests();
    void timerAlignsToNextMinute();
    void minuteTimerAdvancesWithoutBlockNotifications();
    void stoppedModelDoesNotConsumeHistoryOrRestartTimers();
};

void BlockClockModelTests::currentTimeUsesNamedTwelveHourFraction()
{
    BlockClockModel model{{}, false};
    model.updateCurrentTime(UtcTime(15));

    QCOMPARE(model.periodStart(), UtcTime(12).toSecsSinceEpoch());
    QVERIFY(qAbs(model.currentTimeFraction() - 0.25) < 0.000001);
}

void BlockClockModelTests::unchangedSecondDoesNotRepublishCurrentTime()
{
    BlockClockModel model{{}, false};
    model.updateCurrentTime(UtcTime(15));
    QSignalSpy current_time_spy{&model, &BlockClockModel::currentTimeFractionChanged};

    model.updateCurrentTime(UtcTime(15));
    QCOMPARE(current_time_spy.count(), 0);

    model.updateCurrentTime(UtcTime(15, 0, 1));
    QCOMPARE(current_time_spy.count(), 1);
}

void BlockClockModelTests::blockArrivalRefreshesCurrentTime()
{
    auto history{std::make_shared<BlockClockHistory>()};
    QDateTime current_time{UtcTime(15)};
    BlockClockModel model{history, false, [&] { return current_time; }};
    history->initialize({});
    QCoreApplication::processEvents();
    QSignalSpy current_time_spy{&model, &BlockClockModel::currentTimeFractionChanged};

    current_time = UtcTime(15, 1);
    ConnectBlock(*history, 1, UtcTime(15, 0, 30).toSecsSinceEpoch());
    QCoreApplication::processEvents();

    QCOMPARE(current_time_spy.count(), 1);
    QVERIFY(qAbs(model.currentTimeFraction() - (181.0 / 720.0)) < 0.000001);
    QCOMPARE(model.blockTimeFractions().size(), 1);
    QVERIFY(qAbs(model.blockTimeFractions().constFirst() -
                 (10830.0 / BlockClockModel::PERIOD_SECONDS)) < 0.000001);
}

void BlockClockModelTests::blockHistoryPreservesEqualTimestampsAndPeriodBounds()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    const qint64 start{model.periodStart()};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    history->initialize({
        {uint256{1}, start + 7200},
        {uint256{2}, start - 1},
        {uint256{3}, start + 3600},
        {uint256{4}, start + 7200},
        {uint256{5}, start + BlockClockModel::PERIOD_SECONDS},
    });
    QCoreApplication::processEvents();

    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0, 2.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
    QCoreApplication::processEvents();
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::initialHistoryCanArriveAfterTheModel()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    QCoreApplication::processEvents();
    QVERIFY(model.blockTimeFractions().isEmpty());
    QCOMPARE(history_spy.count(), 0);

    std::thread initializer{[history] {
        history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    }};
    initializer.join();
    // No history timer or explicit refresh is needed to publish worker results.
    QCOMPARE(history_spy.count(), 0);
    QTRY_COMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::historyInitializedBeforeModelIsPublished()
{
    auto history{std::make_shared<BlockClockHistory>()};
    std::thread initializer{[history] {
        history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    }};
    initializer.join();
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    QTRY_COMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QVERIFY(!model.timerActive());
}

void BlockClockModelTests::workerNotificationsPublishWithoutHistoryTimer()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    history->initialize({});
    QCoreApplication::processEvents();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    bool delivered_on_gui{false};
    const auto gui_thread = QThread::currentThread();
    connect(&model, &BlockClockModel::blockTimeFractionsChanged, &model, [&] {
        delivered_on_gui = QThread::currentThread() == gui_thread;
    });
    std::thread notifications{[history] {
        ConnectBlock(*history, 1, UtcTime(13).toSecsSinceEpoch());
        ConnectBlock(*history, 2, UtcTime(13).toSecsSinceEpoch());
    }};
    notifications.join();
    QCOMPARE(history_spy.count(), 0);
    const QList<qreal> expected{1.0 / 12.0, 1.0 / 12.0};
    QTRY_COMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
    QVERIFY(delivered_on_gui);
    QCOMPARE(model.findChildren<QTimer*>().size(), 1);
    QVERIFY(!model.timerActive());

    std::thread reorganization{[history] {
        DisconnectBlock(*history, 1, UtcTime(13).toSecsSinceEpoch());
        ConnectBlock(*history, 3, UtcTime(14).toSecsSinceEpoch());
    }};
    reorganization.join();
    const QList<qreal> reorganized{1.0 / 12.0, 2.0 / 12.0};
    QTRY_COMPARE(model.blockTimeFractions(), reorganized);
    QCOMPARE(history_spy.count(), 2);
}

void BlockClockModelTests::notificationDuringPublicationSchedulesFollowup()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    history->initialize({});
    QCoreApplication::processEvents();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    connect(&model, &BlockClockModel::blockTimeFractionsChanged, &model, [&] {
        if (model.blockTimeFractions().size() == 1) {
            // A change can arrive while the previous queued delivery is still
            // emitting GUI signals. It must not be lost behind its pending flag.
            ConnectBlock(*history, 2, UtcTime(14).toSecsSinceEpoch());
        }
    });
    ConnectBlock(*history, 1, UtcTime(13).toSecsSinceEpoch());
    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0};
    QTRY_COMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 2);
}

void BlockClockModelTests::idleHistoryDoesNotScheduleRepeatedUpdates()
{
    auto history{std::make_shared<BlockClockHistory>()};
    int clock_reads{0};
    BlockClockModel model{history, false, [&] { ++clock_reads; return UtcTime(15); }};
    history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    QTRY_COMPARE(model.blockTimeFractions().size(), 1);
    QCoreApplication::processEvents();
    const int reads_after_publication = clock_reads;
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    QTest::qWait(250);
    QCOMPARE(clock_reads, reads_after_publication);
    QCOMPARE(history_spy.count(), 0);
    for (const auto* timer : model.findChildren<QTimer*>()) QVERIFY(!timer->isActive());
}

void BlockClockModelTests::destroyedModelIgnoresPendingAndLateWorkerNotifications()
{
    auto history{std::make_shared<BlockClockHistory>()};
    auto model{std::make_unique<BlockClockModel>(history, false, [] { return UtcTime(15); })};
    history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    QTRY_COMPARE(model->blockTimeFractions().size(), 1);
    QSignalSpy history_spy{model.get(), &BlockClockModel::blockTimeFractionsChanged};
    QSemaphore entered;
    QSemaphore release;
    std::atomic_bool finished{false};
    std::thread notifications{[history, &entered, &release, &finished] {
        ConnectBlock(*history, 2, UtcTime(14).toSecsSinceEpoch());
        entered.release();
        release.acquire();
        for (int repeat{0}; repeat < 10'000; ++repeat) {
            ConnectBlock(*history, 3, UtcTime(15).toSecsSinceEpoch());
            DisconnectBlock(*history, 3, UtcTime(15).toSecsSinceEpoch());
        }
        finished = true;
    }};
    const auto finish = qScopeGuard([&] { release.release(); notifications.join(); });
    // Preserve the first queued delivery until the GUI owner has disappeared.
    QVERIFY(entered.tryAcquire(1, 5000));
    QCOMPARE(history_spy.count(), 0);
    release.release();
    model.reset();
    QTRY_VERIFY(finished.load());
    QCoreApplication::processEvents();
    QCOMPARE(history_spy.count(), 0);
    QVERIFY(history->retentionStart().has_value());
}

void BlockClockModelTests::cachedHistorySurvivesPeriodRollover()
{
    auto history{std::make_shared<BlockClockHistory>()};
    QDateTime current_time{UtcTime(11, 59, 59)};
    BlockClockModel model{history, false, [&] { return current_time; }};
    history->initialize({
        {uint256{1}, UtcTime(11, 30).toSecsSinceEpoch()},
        {uint256{2}, UtcTime(12, 1).toSecsSinceEpoch()},
        {uint256{3}, UtcTime(12, 1).toSecsSinceEpoch()},
    });
    QCoreApplication::processEvents();
    QCOMPARE(model.blockTimeFractions(), QList<qreal>{11.5 / 12.0});
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    // Future-dated blocks already in the cache appear at noon without another
    // block notification or any access to the chain.
    current_time = UtcTime(12);
    model.updateCurrentTime(current_time);

    QCOMPARE(model.periodStart(), UtcTime(12).toSecsSinceEpoch());
    const QList<qreal> expected{1.0 / 720.0, 1.0 / 720.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::blockNotificationsAreCoalesced()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    history->initialize({});
    QCoreApplication::processEvents();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    QueuedDeliveryCounter queued;
    model.installEventFilter(&queued);

    std::thread notifications{[history] {
        for (uint8_t id{1}; id <= 100; ++id) {
            ConnectBlock(*history, id, UtcTime(13).toSecsSinceEpoch());
        }
        // Every pair changes the cache while the GUI has not consumed the
        // preceding update; the burst must still schedule one publication.
        for (int repeat{0}; repeat < 10'000; ++repeat) {
            DisconnectBlock(*history, 100, UtcTime(13).toSecsSinceEpoch());
            ConnectBlock(*history, 100, UtcTime(13).toSecsSinceEpoch());
        }
    }};
    notifications.join();
    QCOMPARE(history_spy.count(), 0);
    QVERIFY(model.blockTimeFractions().isEmpty());

    QCoreApplication::processEvents();
    QCOMPARE(queued.deliveries, 1);
    QCOMPARE(history_spy.count(), 1);
    QCOMPARE(model.blockTimeFractions().size(), 100);
    for (const qreal fraction : model.blockTimeFractions()) {
        QVERIFY(qAbs(fraction - 1.0 / 12.0) < 0.000001);
    }
    QCoreApplication::processEvents();
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::repeatedBlockNotificationDoesNotDuplicateHistory()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    const qint64 timestamp{UtcTime(13).toSecsSinceEpoch()};
    history->initialize({{uint256{1}, timestamp}});
    QCoreApplication::processEvents();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    ConnectBlock(*history, 1, timestamp);
    QCoreApplication::processEvents();
    QCOMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QCOMPARE(history_spy.count(), 0);

    ConnectBlock(*history, 2, timestamp);
    ConnectBlock(*history, 2, timestamp);
    QCoreApplication::processEvents();
    const QList<qreal> expected{1.0 / 12.0, 1.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::disconnectRemovesOnlyTheMatchingBlock()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    history->initialize({
        {uint256{1}, UtcTime(13).toSecsSinceEpoch()},
        {uint256{2}, UtcTime(13).toSecsSinceEpoch()},
        {uint256{3}, UtcTime(14).toSecsSinceEpoch()},
    });
    QCoreApplication::processEvents();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    // A replacement outside the displayed period still removes the former
    // tip. Equal timestamps must not cause the other block to disappear.
    DisconnectBlock(*history, 2, UtcTime(13).toSecsSinceEpoch());
    ConnectBlock(*history, 4, UtcTime(11).toSecsSinceEpoch());
    QCoreApplication::processEvents();
    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);

    DisconnectBlock(*history, 1, UtcTime(13).toSecsSinceEpoch());
    ConnectBlock(*history, 5, UtcTime(15).toSecsSinceEpoch());
    QCoreApplication::processEvents();
    const QList<qreal> reorganized{2.0 / 12.0, 3.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), reorganized);
    QCOMPARE(history_spy.count(), 2);
}

void BlockClockModelTests::oldIbdNotificationsDoNotRepublishHistory()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    QCoreApplication::processEvents();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    for (uint8_t id{2}; id <= 200; ++id) {
        ConnectBlock(*history, id, UtcTime(0).toSecsSinceEpoch() - id);
        QCoreApplication::processEvents();
    }

    QCOMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QCOMPARE(history_spy.count(), 0);
}

void BlockClockModelTests::blockArrivalAtRolloverIsNotCountedTwice()
{
    auto history{std::make_shared<BlockClockHistory>()};
    QDateTime current_time{UtcTime(11, 59)};
    BlockClockModel model{history, false, [&] { return current_time; }};
    const qint64 timestamp{UtcTime(12, 1).toSecsSinceEpoch()};
    history->initialize({{uint256{1}, timestamp}});
    QCoreApplication::processEvents();
    QVERIFY(model.blockTimeFractions().isEmpty());
    QueuedDeliveryCounter queued;
    model.installEventFilter(&queued);

    current_time = UtcTime(12, 1);
    std::thread first_notification{[history, timestamp] {
        ConnectBlock(*history, 1, timestamp);
        ConnectBlock(*history, 2, timestamp);
    }};
    first_notification.join();
    // The minute update consumes the dirty snapshot while notification delivery
    // remains pending. A new block must share that event without getting lost.
    model.updateCurrentTime(current_time);
    const QList<qreal> initial{1.0 / 720.0, 1.0 / 720.0};
    QCOMPARE(model.blockTimeFractions(), initial);
    QCOMPARE(queued.deliveries, 0);
    std::thread second_notification{[history] {
        ConnectBlock(*history, 3, UtcTime(12, 2).toSecsSinceEpoch());
    }};
    second_notification.join();
    QCoreApplication::processEvents();

    QCOMPARE(model.periodStart(), UtcTime(12).toSecsSinceEpoch());
    const QList<qreal> expected{1.0 / 720.0, 1.0 / 720.0, 2.0 / 720.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(queued.deliveries, 1);
}

void BlockClockModelTests::emptyHistoryDoesNotEmitRedundantChanges()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, false, [] { return UtcTime(15); }};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    history->initialize({});
    QCoreApplication::processEvents();
    QCoreApplication::processEvents();
    QCOMPARE(history_spy.count(), 0);
}

void BlockClockModelTests::timersHaveModelOwnershipAndCanBeDisabledForTests()
{
    const auto current_time_provider{[] { return UtcTime(15); }};
    BlockClockModel stopped_model{{}, false, current_time_provider};
    QCOMPARE(stopped_model.timerActive(), false);
    const auto stopped_timers{stopped_model.findChildren<QTimer*>()};
    QCOMPARE(stopped_timers.size(), 1);
    for (QTimer* timer : stopped_timers) {
        QCOMPARE(timer->parent(), &stopped_model);
        QVERIFY(!timer->isActive());
    }

    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel running_model{history, true, current_time_provider};
    QCOMPARE(running_model.timerActive(), true);
    const auto running_timers{running_model.findChildren<QTimer*>()};
    QCOMPARE(running_timers.size(), 1);
    for (QTimer* timer : running_timers) {
        QCOMPARE(timer->parent(), &running_model);
        QVERIFY(timer->isActive());
    }
}

void BlockClockModelTests::timerAlignsToNextMinute()
{
    const QDateTime current_time{UtcTime(15, 0, 45).addMSecs(250)};
    BlockClockModel model{{}, true, [current_time] { return QDateTime{current_time}; }};

    bool found_clock_timer{false};
    for (QTimer* timer : model.findChildren<QTimer*>()) {
        if (!timer->isSingleShot()) continue;
        found_clock_timer = true;
        QCOMPARE(timer->timerType(), Qt::PreciseTimer);
        QCOMPARE(timer->interval(), BlockClockModel::CLOCK_UPDATE_INTERVAL_MS - 45250);
    }
    QVERIFY(found_clock_timer);
}

void BlockClockModelTests::minuteTimerAdvancesWithoutBlockNotifications()
{
    auto history{std::make_shared<BlockClockHistory>()};
    history->initialize({{uint256{1}, UtcTime(14).toSecsSinceEpoch()}});
    // Start just before a minute boundary to exercise the real timer without
    // waiting a minute or calling the model's update method directly.
    QDateTime current_time{UtcTime(15, 0, 59).addMSecs(950)};
    BlockClockModel model{history, true, [&] { return current_time; }};
    QCoreApplication::processEvents();
    const QList<qreal> expected_history{1.0 / 6.0};
    QCOMPARE(model.blockTimeFractions(), expected_history);
    auto* timer = model.findChild<QTimer*>();
    QVERIFY(timer);
    QSignalSpy timeout_spy{timer, &QTimer::timeout};
    QSignalSpy current_time_spy{&model, &BlockClockModel::currentTimeFractionChanged};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    current_time = UtcTime(15, 1);
    QTRY_COMPARE(timeout_spy.count(), 1);

    QCOMPARE(current_time_spy.count(), 1);
    QVERIFY(qAbs(model.currentTimeFraction() - (181.0 / 720.0)) < 0.000001);
    QCOMPARE(model.blockTimeFractions(), expected_history);
    QCOMPARE(history_spy.count(), 0);
    QVERIFY(timer->isActive());
    QCOMPARE(timer->interval(), BlockClockModel::CLOCK_UPDATE_INTERVAL_MS);
}

void BlockClockModelTests::stoppedModelDoesNotConsumeHistoryOrRestartTimers()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, true, [] { return UtcTime(15); }};
    history->initialize({{uint256{1}, UtcTime(14).toSecsSinceEpoch()}});
    QCoreApplication::processEvents();
    const auto fractions = model.blockTimeFractions();
    const auto period = model.periodStart();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    std::thread first_notification{[history] {
        ConnectBlock(*history, 2, UtcTime(16).toSecsSinceEpoch());
    }};
    first_notification.join();
    model.stop();
    std::thread late_notifications{[history] {
        for (int repeat{0}; repeat < 10'000; ++repeat) {
            ConnectBlock(*history, 3, UtcTime(17).toSecsSinceEpoch());
            DisconnectBlock(*history, 3, UtcTime(17).toSecsSinceEpoch());
        }
    }};
    late_notifications.join();
    QCoreApplication::processEvents();
    QCOMPARE(history_spy.count(), 0);
    model.updateCurrentTime(UtcTime(15).addDays(1));
    QCOMPARE(model.blockTimeFractions(), fractions);
    QCOMPARE(model.periodStart(), period);
    for (const auto* timer : model.findChildren<QTimer*>()) QVERIFY(!timer->isActive());
    // Stopping the presentation does not own backend unregistration. The init
    // worker retires the independent subscription before appShutdown instead.
    QVERIFY(history->retentionStart().has_value());
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(BlockClockModelTests)
#else
QTEST_MAIN(BlockClockModelTests)
#endif
#include "test_blockclockmodel.moc"
