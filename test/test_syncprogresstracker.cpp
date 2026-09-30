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

void SyncProgressTrackerTests::publishesDespiteFrequentProgressDips()
{
    SyncProgressTracker tracker;
    for (std::size_t i{0}; i < 2 * SyncProgressTracker::PUBLISH_SAMPLE_INTERVAL; ++i) {
        // Timestamp-dependent verification estimates can dip even while blocks
        // are being connected. A dip every ten samples must not starve the ETA.
        const double progress{0.1 + static_cast<double>(i) * 0.0001 - (i % 10 == 9 ? 0.0005 : 0.0)};
        const auto estimate{tracker.addSample(static_cast<int64_t>(i * 10), progress)};
        if ((i + 1) % SyncProgressTracker::PUBLISH_SAMPLE_INTERVAL == 0) {
            QVERIFY(estimate.has_value());
            const auto expected{static_cast<int64_t>((1.0 - progress) / (progress - 0.1) * static_cast<double>(i * 10))};
            QVERIFY(qAbs(*estimate - expected) <= 1);
        } else {
            QVERIFY(!estimate.has_value());
        }
    }
}

void SyncProgressTrackerTests::nonpositiveWindowProgressDoesNotPublish()
{
    SyncProgressTracker tracker;
    for (std::size_t i{0}; i < SyncProgressTracker::PUBLISH_SAMPLE_INTERVAL; ++i) {
        QVERIFY(!tracker.addSample(static_cast<int64_t>(i * 10), 0.5 - static_cast<double>(i) * 0.0001).has_value());
    }
    // Preserve the baseline and publication cadence while progress recovers.
    std::optional<int64_t> estimate;
    for (std::size_t i{0}; i < SyncProgressTracker::PUBLISH_SAMPLE_INTERVAL; ++i) {
        estimate = tracker.addSample(static_cast<int64_t>((i + SyncProgressTracker::PUBLISH_SAMPLE_INTERVAL) * 10),
                                     0.4 + static_cast<double>(i) * 0.0002);
    }
    QVERIFY(estimate.has_value());
    QVERIFY(*estimate > 0);
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
