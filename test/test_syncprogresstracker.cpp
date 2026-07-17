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
    void resetsForProgressOrClockRegression();
};

void SyncProgressTrackerTests::publishesAtBoundedCadence()
{
    SyncProgressTracker tracker;
    std::optional<int64_t> estimate;
    for (std::size_t i{0}; i < SyncProgressTracker::PUBLISH_SAMPLE_INTERVAL; ++i) {
        estimate = tracker.addSample(static_cast<int64_t>(i * 10), static_cast<double>(i) / 2000.0);
    }

    QVERIFY(estimate.has_value());
    QVERIFY(*estimate > 0);
}

void SyncProgressTrackerTests::boundsHistoryWithoutDiscardingTheWindow()
{
    SyncProgressTracker tracker;
    for (std::size_t i{0}; i < SyncProgressTracker::MAX_SAMPLES + 500; ++i) {
        tracker.addSample(static_cast<int64_t>(i), static_cast<double>(i) / 10'000.0);
    }

    QCOMPARE(tracker.sampleCount(), SyncProgressTracker::MAX_SAMPLES);
}

void SyncProgressTrackerTests::resetsForProgressOrClockRegression()
{
    SyncProgressTracker tracker;
    tracker.addSample(100, 0.2);
    tracker.addSample(200, 0.3);
    QCOMPARE(tracker.sampleCount(), std::size_t{2});

    tracker.addSample(300, 0.1);
    QCOMPARE(tracker.sampleCount(), std::size_t{1});

    tracker.addSample(250, 0.2);
    QCOMPARE(tracker.sampleCount(), std::size_t{1});
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(SyncProgressTrackerTests)
#else
QTEST_MAIN(SyncProgressTrackerTests)
#endif
#include "test_syncprogresstracker.moc"
