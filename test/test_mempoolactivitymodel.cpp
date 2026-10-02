// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/mempoolactivitymodel.h>

#include <QSemaphore>
#include <QSignalSpy>
#include <QtTest/QtTest>

#include <atomic>

class MempoolActivityModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void summaryAndIncomingHaveIndependentVisibility()
    {
        MempoolActivityModel::Snapshot sample{0, 0.1, true, 1200000};
        MempoolActivityModel model{[&] { return sample; }};
        QSignalSpy refreshes(&model, &MempoolActivityModel::statsRefreshRequested);
        model.setReady(true);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.queuedVbytes(), 1200000);
        model.setSummaryActive(true);
        QTRY_VERIFY(!model.pending());
        QVERIFY(refreshes.count() > 0);
        model.setActive(true);
        QTRY_VERIFY(!model.pending());
        model.setActive(false);
        QVERIFY(model.summaryActive());
        sample.queued_vbytes = 0; // Mining empties the queue, without negative arrivals.
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.queuedVbytes(), 0);
        model.setSummaryActive(false);
        const int before = refreshes.count();
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(refreshes.count(), before);
        model.setReady(false);
        QCOMPARE(model.queuedVbytes(), -1);
    }

    void savedMempoolLoadingIsNotIncomingTraffic()
    {
        qint64 now{100000};
        MempoolActivityModel::Snapshot snapshot{0, -1, false};
        MempoolActivityModel model([&] { return snapshot; }, 1667, [&] { return now; });
        model.setReady(true);
        QTRY_VERIFY(!model.pending());
        now += 5000;
        snapshot = {100000, 0.1, true};
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QVERIFY(model.history().isEmpty());
        QCOMPARE(model.incomingRate(), -1.0);
        now += 5000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.incomingRate(), 0.0);
    }

    void arrivalsUseElapsedTimeAndContinueWhileHidden()
    {
        qint64 now{100000};
        MempoolActivityModel::Snapshot snapshot{50000, 0.1};
        MempoolActivityModel model([&] { return snapshot; }, 1000000.0 / 600, [&] { return now; });
        QSignalSpy refreshes(&model, &MempoolActivityModel::statsRefreshRequested);
        model.refresh();
        QVERIFY(!model.pending());
        model.setReady(true);
        QTRY_VERIFY(!model.pending());
        QVERIFY(model.history().isEmpty()); // Existing mempool is not incoming traffic.
        QCOMPARE(model.minimumFee(), 0.1);
        now += 5000;
        snapshot.incoming_vbytes += 10000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.incomingRate(), 2000.0);
        QCOMPARE(model.history().size(), 1);
        QCOMPARE(refreshes.count(), 0); // Settings counts only refreshed when visible.
        model.setActive(true);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(refreshes.count(), 1);
        now += 10000;
        snapshot.incoming_vbytes += 10000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.incomingRate(), 1000.0);
        now += 5000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.incomingRate(), 0.0); // No arrivals is a real zero.
    }

    void sameTimestampDoesNotLoseArrivals()
    {
        qint64 now{100000};
        quint64 bytes{100};
        MempoolActivityModel model([&] { return MempoolActivityModel::Snapshot{bytes, 0.1}; }, 1667, [&] { return now; });
        model.setReady(true);
        QTRY_VERIFY(!model.pending());
        bytes += 1000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        now += 5000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.incomingRate(), 200.0);
    }

    void gapsResetsAndHistoryExpiry()
    {
        qint64 now{100000};
        quint64 bytes{100};
        MempoolActivityModel model([&] { return MempoolActivityModel::Snapshot{bytes, 0.1}; }, 1667, [&] { return now; });
        model.setReady(true);
        QTRY_VERIFY(!model.pending());
        now += 20000;
        bytes += 10000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.incomingRate(), -1.0);
        QCOMPARE(model.history().last().toMap().value("rate").toDouble(), -1.0);
        now += 5000;
        bytes += 5000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.incomingRate(), 1000.0);
        now += 7200001;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.history().size(), 1);
        bytes = 0;
        now += 5000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QVERIFY(model.history().isEmpty());
        QCOMPARE(model.incomingRate(), -1.0);
        now -= 60000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QVERIFY(model.history().isEmpty());
    }

    void shutdownDrainsSamplerAndDiscardsItsReply()
    {
        QSemaphore entered, release;
        std::atomic<int> calls{0};
        MempoolActivityModel model([&] {
            if (++calls == 1) { entered.release(); release.acquire(); }
            return MempoolActivityModel::Snapshot{100, 2};
        });
        model.setReady(true);
        const bool started = entered.tryAcquire(1, 1000);
        QSignalSpy drained(&model, &MempoolActivityModel::shutdownFinished);
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
        QCOMPARE(model.minimumFee(), -1.0);
        QVERIFY(model.history().isEmpty());
    }
};

QTEST_GUILESS_MAIN(MempoolActivityModelTests)
#include "test_mempoolactivitymodel.moc"
