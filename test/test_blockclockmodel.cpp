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
    void blockHistoryIsSortedUniqueAndPeriodBounded();
    void historyLoadsOnInitializationAndPeriodRollover();
    void emptyHistoryDoesNotEmitRedundantChanges();
    void timerHasModelOwnershipAndCanBeDisabledForTests();
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

void BlockClockModelTests::blockHistoryIsSortedUniqueAndPeriodBounded()
{
    BlockClockModel model{{}, false};
    model.updateCurrentTime(UtcTime(15));
    const qint64 start{model.periodStart()};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    model.recordBlockTime(start - 1);
    model.recordBlockTime(start + BlockClockModel::PERIOD_SECONDS);
    QCOMPARE(history_spy.count(), 0);

    model.recordBlockTime(start + 7200);
    model.recordBlockTime(start + 3600);
    model.recordBlockTime(start + 7200);

    QCOMPARE(history_spy.count(), 2);
    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0};
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
    BlockClockModel stopped_model{{}, false};
    QCOMPARE(stopped_model.timerActive(), false);
    QCOMPARE(stopped_model.findChildren<QTimer*>().size(), 1);
    QCOMPARE(stopped_model.findChildren<QTimer*>().constFirst()->parent(), &stopped_model);

    BlockClockModel running_model{{}, true};
    QCOMPARE(running_model.timerActive(), true);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(BlockClockModelTests)
#else
QTEST_MAIN(BlockClockModelTests)
#endif
#include "test_blockclockmodel.moc"
