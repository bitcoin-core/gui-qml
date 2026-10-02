// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/difficultyperiodmodel.h>

#include <QSemaphore>
#include <QtTest/QtTest>

#include <cmath>
#include <stdexcept>

class DifficultyPeriodModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void periodStatisticsAndIdleEstimate()
    {
        DifficultyPeriodModel::Snapshot sample{968933, 2016, 600, 1000000, 1000000 + 1253 * 612, 100, 104.16, 1000000};
        qint64 now = sample.tip_time;
        DifficultyPeriodModel model([&] { return sample; }, [&] { return now; });
        model.setReady(true);
        QVERIFY(!model.pending());
        model.setActive(true);
        QTRY_VERIFY(!model.pending());
        QVERIFY(model.available());
        QCOMPARE(model.blocksLeft(), 763);
        QCOMPARE(int(std::floor(model.progress() * 100)), 62);
        QCOMPARE(model.averageBlockSeconds(), 612.0);
        QVERIFY(std::abs(model.previousChange() - 4.16) < 1e-9);
        QVERIFY(std::abs(model.nextChange() - (600.0 / 612 - 1) * 100) < 1e-9);
        const double before = model.nextChange();
        now += 3600;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QVERIFY(model.nextChange() < before);
        QCOMPARE(model.averageBlockSeconds(), 612.0);
        model.setActive(false);
        model.refresh();
        QVERIFY(!model.pending());
    }

    void boundariesReorgsAndRetargetRules()
    {
        DifficultyPeriodModel::Snapshot sample{2016, 2016, 600, 1000000, 1000000, 100, 100, 1000000};
        qint64 now{1000000};
        DifficultyPeriodModel model([&] { return sample; }, [&] { return now; });
        model.setReady(true);
        model.setActive(true);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.progress(), 0.0);
        QCOMPARE(model.blocksLeft(), 2016);
        QVERIFY(std::isnan(model.nextChange()));
        QVERIFY(std::isnan(model.averageBlockSeconds()));
        sample.height = 2015; // A reorg can return to the preceding period.
        sample.start_time = 0;
        sample.tip_time = now = 2015 * 60;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.blocksLeft(), 1);
        QCOMPARE(model.nextChange(), 300.0);
        sample.tip_time = now = 2015 * 6000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.nextChange(), -75.0);
        sample.pow_limit = 100;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.nextChange(), 0.0);
        sample.min_difficulty_blocks = true;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QVERIFY(std::isnan(model.nextChange()));
        sample.no_retargeting = true;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.nextChange(), 0.0);
        QVERIFY(!model.retargeting());
    }

    void unavailableAndFailures()
    {
        DifficultyPeriodModel::Snapshot sample;
        bool fail{false};
        DifficultyPeriodModel model([&] {
            if (fail) throw std::runtime_error("header unavailable");
            return sample;
        });
        model.setActive(true);
        model.setReady(true);
        QTRY_VERIFY(!model.pending());
        QVERIFY(!model.available());
        QVERIFY(std::isnan(model.previousChange()));
        sample.height = 100;
        sample.interval = 0;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QVERIFY(!model.available());
        fail = true;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QVERIFY(!model.available());
    }

    void shutdownDrainsQueries()
    {
        QSemaphore entered, release;
        std::atomic<int> calls{0};
        DifficultyPeriodModel model([&] {
            if (++calls == 1) { entered.release(); release.acquire(); }
            return DifficultyPeriodModel::Snapshot{.height = 1253};
        });
        model.setActive(true);
        model.setReady(true);
        const bool started = entered.tryAcquire(1, 1000);
        QSignalSpy drained(&model, &DifficultyPeriodModel::shutdownFinished);
        int ticks{0};
        QTimer heartbeat;
        connect(&heartbeat, &QTimer::timeout, &model, [&] { ++ticks; });
        heartbeat.start(1);
        model.stopForShutdown();
        QTest::qWait(30);
        const int before_release = drained.count();
        release.release();
        QTRY_COMPARE(drained.count(), 1);
        QVERIFY(started);
        QCOMPARE(before_release, 0);
        QVERIFY(ticks > 0);
        QTRY_VERIFY(!model.pending());
        QVERIFY(!model.available());
    }
};

QTEST_GUILESS_MAIN(DifficultyPeriodModelTests)
#include "test_difficultyperiodmodel.moc"
