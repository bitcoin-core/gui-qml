// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/syncprogresstracker.h>

#include <test/qt_test_registry.h>

#include <QtTest/QtTest>

class SyncProgressTrackerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void publishesAtBoundedCadence();
    void boundsHistoryWithoutDiscardingTheWindow();
    void publishesDespiteFrequentProgressDips();
    void nonpositiveWindowProgressDoesNotPublish();
    void resetsForClockRegression();
};

void SyncProgressTrackerTests::publishesAtBoundedCadence()
{
    SyncProgressTracker tracker;
    QVERIFY(!tracker.addSample(0, 0.1));
    // Even thousands of incoming blocks inside one second cannot republish.
    for (int sample = 1; sample < 1000; ++sample) {
        QVERIFY(!tracker.addSample(sample, 0.1 + sample * 0.0001));
    }
    const auto first = tracker.addSample(SyncProgressTracker::PUBLISH_INTERVAL_MILLISECONDS, 0.2);
    QVERIFY(first.has_value());
    QVERIFY(qAbs(*first - 8000) <= 1);
    QVERIFY(!tracker.addSample(1500, 0.25));
    const auto second = tracker.addSample(2000, 0.3);
    QVERIFY(second.has_value());
    QVERIFY(qAbs(*second - 7000) <= 1);

    // Sparse notifications still publish once enough wall time has elapsed.
    SyncProgressTracker sparse;
    QVERIFY(!sparse.addSample(0, 0.1));
    const auto low_frequency = sparse.addSample(5000, 0.2);
    QVERIFY(low_frequency.has_value());
    QVERIFY(qAbs(*low_frequency - 40000) <= 1);
}

void SyncProgressTrackerTests::boundsHistoryWithoutDiscardingTheWindow()
{
    SyncProgressTracker tracker;
    for (std::size_t i{0}; i < SyncProgressTracker::MAX_SAMPLES + 500; ++i) {
        tracker.addSample(static_cast<int64_t>(i), static_cast<double>(i) / 10'000.0);
    }
    QCOMPARE(tracker.sampleCount(), SyncProgressTracker::MAX_SAMPLES);
    const auto estimate = tracker.addSample(SyncProgressTracker::WINDOW_MILLISECONDS + 6000, 0.8);
    QVERIFY(estimate.has_value());
    QVERIFY(tracker.sampleCount() <= 2);
}

void SyncProgressTrackerTests::publishesDespiteFrequentProgressDips()
{
    SyncProgressTracker tracker;
    for (int i = 0; i <= 200; ++i) {
        const double progress{0.1 + i * 0.0001 - (i % 10 == 9 ? 0.0005 : 0.0)};
        const auto estimate = tracker.addSample(i * 10, progress);
        if (i > 0 && i % 100 == 0) {
            QVERIFY(estimate.has_value());
            const auto expected = static_cast<int64_t>((1.0 - progress) / (progress - 0.1) * (i * 10));
            QVERIFY(qAbs(*estimate - expected) <= 1);
        } else {
            QVERIFY(!estimate.has_value());
        }
    }
}

void SyncProgressTrackerTests::nonpositiveWindowProgressDoesNotPublish()
{
    SyncProgressTracker tracker;
    QVERIFY(!tracker.addSample(0, 0.5));
    QVERIFY(!tracker.addSample(1000, 0.4));
    QVERIFY(!tracker.addSample(1999, 0.6));
    const auto recovered = tracker.addSample(2000, 0.6);
    QVERIFY(recovered.has_value());
    QVERIFY(qAbs(*recovered - 8000) <= 1);
}

void SyncProgressTrackerTests::resetsForClockRegression()
{
    SyncProgressTracker tracker;
    tracker.addSample(100, 0.2);
    tracker.addSample(200, 0.3);
    QCOMPARE(tracker.sampleCount(), std::size_t{2});

    tracker.addSample(300, 0.1);
    QCOMPARE(tracker.sampleCount(), std::size_t{3});

    tracker.addSample(250, 0.2);
    QCOMPARE(tracker.sampleCount(), std::size_t{1});
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(SyncProgressTrackerTests)
#else
QTEST_MAIN(SyncProgressTrackerTests)
#endif
#include "test_syncprogresstracker.moc"
