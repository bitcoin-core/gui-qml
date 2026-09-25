// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/feeratesmodel.h>

#include <QSemaphore>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <atomic>
#include <thread>

class FeeRatesModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void historyExcludesCurrentMinuteAndSurvivesRestart()
    {
        QTemporaryDir settings;
        const auto file = settings.filePath("fees.ini");
        qint64 now{864000};
        qint64 rate{2000};
        FeeRatesModel model([&](int) { return rate; }, nullptr, file, [&] { return now; });
        model.setReady(true);
        model.setActive(true);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.referenceRate(), -1.0); // No invented history on first use.
        now += 60;
        rate = 8000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.referenceRate(), 2.0); // A spike is compared with past fees.
        rate = 16000;
        model.refresh();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.referenceRate(), 2.0); // Repeated refreshes cannot skew history.
        model.setActive(false);
        now += 60;
        FeeRatesModel reopened([](int) { return 0; }, nullptr, file, [&] { return now; });
        reopened.setReady(true);
        reopened.setActive(true);
        QTRY_VERIFY(!reopened.pending());
        QCOMPARE(reopened.referenceRate(), 5.0); // Median of the 2 and 8 sat/vB observations.
        now += 86401;
        reopened.refresh();
        QTRY_VERIFY(!reopened.pending());
        QCOMPARE(reopened.referenceRate(), -1.0); // Old observations expire.
    }

    void historyIgnoresInvalidExpiredAndFutureSamples()
    {
        QTemporaryDir directory;
        const auto file = directory.filePath("fees.ini");
        QSettings settings(file, QSettings::IniFormat);
        settings.setValue("dashboard/feeRateHistory", QByteArray(R"({"version":1,"samples":[
            {"time":863940,"rate":3}, {"time":863940,"rate":3},
            {"time":864060,"rate":500}, {"time":1,"rate":500},
            {"time":863880,"rate":0}, {"time":"invalid","rate":9}]})"));
        settings.sync();
        FeeRatesModel model([](int) { return 0; }, nullptr, file, [] { return 864000; });
        model.setReady(true);
        model.setActive(true);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.referenceRate(), 3.0);
    }

    void estimatesAreWalletIndependentAndPreservePrecision()
    {
        QTemporaryDir settings;
        QList<int> targets;
        FeeRatesModel model([&](int target) -> qint64 {
            targets.append(target);
            if (target == 2) return 12345;
            if (target == 4) return 500;
            if (target == 6) return 0;
            return 1;
        }, nullptr, settings.filePath("fees.ini"));
        model.setActive(true);
        QVERIFY(!model.pending()); // No node access before initialization.
        model.setReady(true);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(targets, QList<int>({2, 4, 6, 144}));
        QCOMPARE(model.rates(), QVariantList({12.345, 0.5, -1.0, 0.001}));
        model.setReady(false);
        QCOMPARE(model.rates(), QVariantList({-1.0, -1.0, -1.0, -1.0}));
    }

    void inactiveWidgetsDoNotPoll()
    {
        QTemporaryDir settings;
        std::atomic<int> calls{0};
        FeeRatesModel model([&](int) { ++calls; return 2000; }, nullptr, settings.filePath("fees.ini"));
        model.setReady(true);
        model.refresh();
        QCOMPARE(calls.load(), 0);
        model.setActive(true);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(calls.load(), 4);
        model.setActive(false);
        model.refresh();
        QCOMPARE(calls.load(), 4);
        model.setActive(true);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(calls.load(), 8);
    }

    void staleResultsAreDiscardedAndRefreshesCoalesce()
    {
        QTemporaryDir settings;
        QSemaphore started;
        QSemaphore release;
        std::atomic<int> calls{0};
        FeeRatesModel model([&](int) {
            if (++calls == 1) { started.release(); release.acquire(); }
            return 7000;
        }, nullptr, settings.filePath("fees.ini"));
        model.setReady(true);
        model.setActive(true);
        const bool entered = started.tryAcquire(1, 1000);
        model.refresh();
        model.refresh();
        model.setActive(false);
        release.release(); // Always release the worker before any test assertion.
        QVERIFY(entered);
        QTRY_VERIFY(!model.pending());
        QCOMPARE(calls.load(), 4);
        QCOMPARE(model.rates(), QVariantList({-1.0, -1.0, -1.0, -1.0}));
    }

    void shutdownWaitsForEstimator()
    {
        QTemporaryDir settings;
        QSemaphore started;
        QSemaphore release;
        std::atomic<int> calls{0};
        FeeRatesModel model([&](int) {
            if (++calls == 1) { started.release(); release.acquire(); }
            return 3000;
        }, nullptr, settings.filePath("fees.ini"));
        model.setReady(true);
        model.setActive(true);
        const bool entered = started.tryAcquire(1, 1000);
        std::thread unblock([&] { release.release(); });
        model.setReady(false);
        const int drained_calls = calls.load();
        unblock.join();
        QVERIFY(entered);
        QCOMPARE(drained_calls, 4);
        QTRY_VERIFY(!model.pending());
        model.refresh();
        QCOMPARE(calls.load(), 4);
        QCOMPARE(model.rates(), QVariantList({-1.0, -1.0, -1.0, -1.0}));
    }
};

QTEST_GUILESS_MAIN(FeeRatesModelTests)
#include "test_feeratesmodel.moc"
