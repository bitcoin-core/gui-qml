// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockclockmodel.h>

#include <test/qt_test_registry.h>

#include <QtTest/QtTest>
#include <QTimeZone>

namespace {
QDateTime UtcTime(int hour, int minute = 0, int second = 0)
{
    return QDateTime{QDate{2026, 7, 17}, QTime{hour, minute, second}, QTimeZone::utc()};
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
    void historyLoadsOnInitializationAndPeriodRollover();
    void loadedHistoryPreservesEqualTimestamps();
    void blockNotificationsRefreshAuthoritativeHistory();
    void blockArrivalAtRolloverIsNotCountedTwice();
    void emptyHistoryDoesNotEmitRedundantChanges();
    void timerHasModelOwnershipAndCanBeDisabledForTests();
    void timerAlignsToNextMinute();
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
    QDateTime current_time{UtcTime(15)};
    BlockClockModel model{{}, false, [&] { return current_time; }};
    QSignalSpy current_time_spy{&model, &BlockClockModel::currentTimeFractionChanged};

    current_time = UtcTime(15, 1);
    model.recordBlockTime(UtcTime(15, 0, 30).toSecsSinceEpoch());

    QCOMPARE(current_time_spy.count(), 1);
    QVERIFY(qAbs(model.currentTimeFraction() - (181.0 / 720.0)) < 0.000001);
    QCOMPARE(model.blockTimeFractions().size(), 1);
    QVERIFY(qAbs(model.blockTimeFractions().constFirst() -
                 (10830.0 / BlockClockModel::PERIOD_SECONDS)) < 0.000001);
}

void BlockClockModelTests::blockHistoryPreservesEqualTimestampsAndPeriodBounds()
{
    const QDateTime current_time{UtcTime(15)};
    BlockClockModel model{{}, false, [current_time] { return QDateTime{current_time}; }};
    model.updateCurrentTime(UtcTime(15));
    const qint64 start{model.periodStart()};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    model.recordBlockTime(start - 1);
    model.recordBlockTime(start + BlockClockModel::PERIOD_SECONDS);
    QCOMPARE(history_spy.count(), 0);

    model.recordBlockTime(start + 7200);
    model.recordBlockTime(start + 3600);
    model.recordBlockTime(start + 7200);

    QCOMPARE(history_spy.count(), 3);
    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0, 2.0 / 12.0};
    QCOMPARE(model.blockTimeFractions().size(), expected.size());
    for (qsizetype i{0}; i < expected.size(); ++i) {
        QVERIFY(qAbs(model.blockTimeFractions().at(i) - expected.at(i)) < 0.000001);
    }
}

void BlockClockModelTests::historyLoadsOnInitializationAndPeriodRollover()
{
    QList<qint64> requested_starts;
    BlockClockModel model{[&](qint64 start, qint64) {
        requested_starts.push_back(start);
        return QList<qint64>{start + 600, start + 1200};
    }, false};

    model.updateCurrentTime(UtcTime(11, 59, 59));
    model.initializeHistory();
    QCOMPARE(requested_starts, QList<qint64>{UtcTime(0).toSecsSinceEpoch()});
    QCOMPARE(model.blockTimeFractions().size(), 2);

    model.updateCurrentTime(UtcTime(12));
    QCOMPARE(requested_starts, QList<qint64>({UtcTime(0).toSecsSinceEpoch(), UtcTime(12).toSecsSinceEpoch()}));
    QCOMPARE(model.periodStart(), UtcTime(12).toSecsSinceEpoch());
    QCOMPARE(model.blockTimeFractions().size(), 2);
}

void BlockClockModelTests::loadedHistoryPreservesEqualTimestamps()
{
    BlockClockModel model{[](qint64 start, qint64 end) {
        return QList<qint64>{start + 7200, start - 1, start + 3600, start + 7200, end};
    }, false};
    model.updateCurrentTime(UtcTime(15));
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    model.initializeHistory();
    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0, 2.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
    model.initializeHistory();
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::blockNotificationsRefreshAuthoritativeHistory()
{
    QList<qint64> offsets{3600};
    BlockClockModel model{[&](qint64 start, qint64) {
        QList<qint64> timestamps;
        for (const qint64 offset : offsets) timestamps.push_back(start + offset);
        return timestamps;
    }, false};
    model.initializeHistory();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    offsets.push_back(3600);
    model.recordBlockTime(model.periodStart() + 3600);
    const QList<qreal> expected{1.0 / 12.0, 1.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);

    // A repeated or queued notification must not count a loaded block again.
    model.recordBlockTime(model.periodStart() + 3600);
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);

    // Replacing the active tip must also remove the old block's timestamp.
    offsets = {3600, 7200};
    model.recordBlockTime(model.periodStart() + 7200);
    const QList<qreal> reorganized{1.0 / 12.0, 2.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), reorganized);
    QCOMPARE(history_spy.count(), 2);
}

void BlockClockModelTests::blockArrivalAtRolloverIsNotCountedTwice()
{
    QDateTime current_time{UtcTime(11, 59)};
    BlockClockModel model{[](qint64 start, qint64) {
        return QList<qint64>{start + 60, start + 60};
    }, false, [&] { return current_time; }};
    model.initializeHistory();

    current_time = UtcTime(12, 1);
    model.recordBlockTime(current_time.toSecsSinceEpoch());

    QCOMPARE(model.periodStart(), UtcTime(12).toSecsSinceEpoch());
    const QList<qreal> expected{1.0 / 720.0, 1.0 / 720.0};
    QCOMPARE(model.blockTimeFractions(), expected);
}

void BlockClockModelTests::emptyHistoryDoesNotEmitRedundantChanges()
{
    BlockClockModel model{[](qint64, qint64) { return QList<qint64>{}; }, false};
    model.updateCurrentTime(UtcTime(15));
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    model.initializeHistory();
    model.initializeHistory();
    QCOMPARE(history_spy.count(), 0);
}

void BlockClockModelTests::timerHasModelOwnershipAndCanBeDisabledForTests()
{
    const QDateTime current_time{UtcTime(15)};
    const auto current_time_provider{[current_time] { return QDateTime{current_time}; }};
    BlockClockModel stopped_model{{}, false, current_time_provider};
    QCOMPARE(stopped_model.timerActive(), false);
    QCOMPARE(stopped_model.findChildren<QTimer*>().size(), 1);
    QCOMPARE(stopped_model.findChildren<QTimer*>().constFirst()->parent(), &stopped_model);

    BlockClockModel running_model{{}, true, current_time_provider};
    QCOMPARE(running_model.timerActive(), true);
}

void BlockClockModelTests::timerAlignsToNextMinute()
{
    const QDateTime current_time{UtcTime(15, 0, 45).addMSecs(250)};
    BlockClockModel model{{}, true, [current_time] { return QDateTime{current_time}; }};
    QTimer* timer{model.findChildren<QTimer*>().constFirst()};

    QCOMPARE(timer->timerType(), Qt::PreciseTimer);
    QCOMPARE(timer->interval(), BlockClockModel::CLOCK_UPDATE_INTERVAL_MS - 45250);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(BlockClockModelTests)
#else
QTEST_MAIN(BlockClockModelTests)
#endif
#include "test_blockclockmodel.moc"
