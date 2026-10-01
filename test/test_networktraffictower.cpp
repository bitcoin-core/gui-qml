// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>

#include <qml/models/networktraffictower.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <map>

#ifndef BITCOINQML_NO_TEST_MAIN
#include <util/translation.h>
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

#include <QSignalSpy>
#include <QThread>

namespace {
constexpr int TEST_SAMPLE_INTERVAL_MS{10};
constexpr int ASYNC_TIMEOUT_MS{1'000};

float MaximumRate(const QVariantList& history, const QString& direction)
{
    float maximum{0};
    for (const auto& point : history) maximum = std::max(maximum, point.toMap().value(direction).toFloat());
    return maximum;
}
} // namespace

class NetworkTrafficTowerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void samplesOffGuiThreadWithoutPublishingWhileInactive();
    void activeControlsPublishingWithoutDiscardingBackgroundHistory();
    void filterWindowChangesPreserveTotalsAndHistory();
    void stopsSamplingWhenDestroyedWhileActive();
    void timestampedHistoryPairsSamplesAndPreservesExtrema();
    void widgetAndPagePublishingAreIndependent();
};

void NetworkTrafficTowerTests::samplesOffGuiThreadWithoutPublishingWhileInactive()
{
    MockNode node;
    std::atomic<int> received_calls{0};
    std::atomic<int> sent_calls{0};
    std::atomic<bool> sampled_on_gui_thread{false};
    QThread* const gui_thread{QThread::currentThread()};

    node.get_total_bytes_recv_fn = [&] {
        ++received_calls;
        if (QThread::currentThread() == gui_thread) sampled_on_gui_thread = true;
        return int64_t{1'000};
    };
    node.get_total_bytes_sent_fn = [&] {
        ++sent_calls;
        if (QThread::currentThread() == gui_thread) sampled_on_gui_thread = true;
        return int64_t{2'000};
    };

    {
        NetworkTrafficTower tower{node, TEST_SAMPLE_INTERVAL_MS};
        QSignalSpy history_spy{&tower, &NetworkTrafficTower::historyChanged};

        QTRY_VERIFY_WITH_TIMEOUT(received_calls.load() >= 3, ASYNC_TIMEOUT_MS);
        QTRY_VERIFY_WITH_TIMEOUT(sent_calls.load() >= 3, ASYNC_TIMEOUT_MS);
        QVERIFY(!sampled_on_gui_thread.load());
        QVERIFY(!tower.active());
        QVERIFY(tower.history().isEmpty());
        QCOMPARE(history_spy.count(), 0);
    }

    const int calls_after_destruction{received_calls.load()};
    QTest::qWait(TEST_SAMPLE_INTERVAL_MS * 3);
    QCOMPARE(received_calls.load(), calls_after_destruction);
}

void NetworkTrafficTowerTests::activeControlsPublishingWithoutDiscardingBackgroundHistory()
{
    MockNode node;
    std::atomic<int64_t> total_received{1'000};
    std::atomic<int64_t> total_sent{2'000};
    std::atomic<int> received_calls{0};

    node.get_total_bytes_recv_fn = [&] {
        ++received_calls;
        return total_received.load();
    };
    node.get_total_bytes_sent_fn = [&] {
        return total_sent.load();
    };

    NetworkTrafficTower tower{node, TEST_SAMPLE_INTERVAL_MS};
    QSignalSpy history_spy{&tower, &NetworkTrafficTower::historyChanged};

    tower.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesReceived(), quint64{1'000}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesSent(), quint64{2'000}, ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!tower.history().isEmpty(), ASYNC_TIMEOUT_MS);

    tower.setActive(false);
    const quint64 published_total{tower.totalBytesReceived()};
    const qsizetype published_history_size{tower.history().size()};
    const qsizetype signals_before_hidden_samples{history_spy.count()};
    const int calls_before_hidden_samples{received_calls.load()};

    total_received = 1'300;
    total_sent = 2'600;
    QTRY_VERIFY_WITH_TIMEOUT(received_calls.load() >= calls_before_hidden_samples + 3, ASYNC_TIMEOUT_MS);
    QCoreApplication::processEvents();

    QCOMPARE(tower.totalBytesReceived(), published_total);
    QCOMPARE(tower.history().size(), published_history_size);
    QCOMPARE(history_spy.count(), signals_before_hidden_samples);

    tower.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesReceived(), quint64{1'300}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesSent(), quint64{2'600}, ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(tower.history().size() > published_history_size, ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(history_spy.count() > signals_before_hidden_samples, ASYNC_TIMEOUT_MS);
}

void NetworkTrafficTowerTests::filterWindowChangesPreserveTotalsAndHistory()
{
    MockNode node;
    std::atomic<int> received_calls{0};

    node.get_total_bytes_recv_fn = [&] {
        ++received_calls;
        return int64_t{1'000};
    };
    node.get_total_bytes_sent_fn = [] {
        return int64_t{2'000};
    };

    NetworkTrafficTower tower{node, TEST_SAMPLE_INTERVAL_MS};
    tower.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(received_calls.load() >= 25, ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(tower.history().size() > 20, ASYNC_TIMEOUT_MS);

    tower.updateFilterWindowSize(1);
    QTRY_VERIFY_WITH_TIMEOUT(tower.history().size() <= 10, ASYNC_TIMEOUT_MS);
    const qsizetype short_history_size{tower.history().size()};
    QVERIFY(short_history_size > 0);
    QCOMPARE(tower.totalBytesReceived(), quint64{1'000});
    QCOMPARE(tower.totalBytesSent(), quint64{2'000});

    tower.updateFilterWindowSize(2);
    QTRY_VERIFY_WITH_TIMEOUT(tower.history().size() > short_history_size, ASYNC_TIMEOUT_MS);
    QVERIFY(tower.history().size() <= 20);
    QCOMPARE(tower.totalBytesReceived(), quint64{1'000});
    QCOMPARE(tower.totalBytesSent(), quint64{2'000});
}

void NetworkTrafficTowerTests::stopsSamplingWhenDestroyedWhileActive()
{
    MockNode node;
    std::atomic<int> received_calls{0};

    node.get_total_bytes_recv_fn = [&] {
        ++received_calls;
        return int64_t{1'000};
    };
    node.get_total_bytes_sent_fn = [] {
        return int64_t{2'000};
    };

    {
        NetworkTrafficTower tower{node, TEST_SAMPLE_INTERVAL_MS};
        tower.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(received_calls.load() >= 3, ASYNC_TIMEOUT_MS);
        QVERIFY(tower.active());
    }

    const int calls_after_destruction{received_calls.load()};
    QTest::qWait(TEST_SAMPLE_INTERVAL_MS * 3);
    QCOMPARE(received_calls.load(), calls_after_destruction);
}

void NetworkTrafficTowerTests::timestampedHistoryPairsSamplesAndPreservesExtrema()
{
    MockNode node;
    std::atomic<int64_t> received{0}, sent{0};
    node.get_total_bytes_recv_fn = [&] { return received.fetch_add(10); };
    node.get_total_bytes_sent_fn = [&] { return sent.fetch_add(20); };
    NetworkTrafficTower tower{node, 1};
    // Independently reconstruct the moving averages from raw widget samples.
    // Keep this oracle in the test, rather than exporting unused full arrays.
    std::map<qint64, double> raw_received;
    connect(&tower, &NetworkTrafficTower::widgetHistoryChanged, &tower, [&] {
        const auto raw = tower.widgetHistory();
        const qint64 latest = raw_received.empty() ? 0 : raw_received.rbegin()->first;
        for (auto it = raw.crbegin(); it != raw.crend(); ++it) {
            const auto sample = it->toMap();
            const auto time = sample.value("time").toLongLong();
            if (time <= latest) break;
            raw_received.emplace(time, sample.value("received").toDouble());
        }
    });
    tower.updateFilterWindowSize(8640);
    tower.setActive(true);
    tower.setWidgetActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(raw_received.size() > 4000, 10000);
    const auto history = tower.history();
    QVERIFY(!history.isEmpty());
    QVERIFY(history.size() <= 2880);
    qint64 previous_time{0};
    float max_received{0}, max_sent{0};
    for (const auto& point : history) {
        const auto sample = point.toMap();
        const qint64 time{sample.value("time").toLongLong()};
        QVERIFY2(time > previous_time, qPrintable(QString("Time %1 follows %2, sample %3").arg(time).arg(previous_time).arg(sample.size())));
        previous_time = time;
        max_received = std::max(max_received, sample.value("received").toFloat());
        max_sent = std::max(max_sent, sample.value("sent").toFloat());
        QCOMPARE(sample.value("sent").toFloat(), sample.value("received").toFloat() * 2);
    }
    double sum{0}, expected_max{0};
    size_t count{0};
    for (const auto& [time, rate] : raw_received) {
        if (time > previous_time) break;
        sum += rate;
        expected_max = std::max(expected_max, sum / ++count);
    }
    QVERIFY(count > 4000);
    QVERIFY(expected_max > 0);
    QVERIFY(std::abs(max_received - expected_max) < expected_max * 0.0001);
    QCOMPARE(max_sent, max_received * 2);
    const auto widget_history = tower.widgetHistory();
    QVERIFY(!widget_history.isEmpty());
    QVERIFY(widget_history.size() <= 2880);
    QVERIFY(widget_history.back().toMap().value("time").toLongLong() -
            widget_history.front().toMap().value("time").toLongLong() <= 5 * 60 * 1000);
    QVERIFY(widget_history.front().toMap().value("time").toLongLong() > history.front().toMap().value("time").toLongLong());
    QCOMPARE(widget_history.back().toMap().value("time"), history.back().toMap().value("time"));
    tower.setActive(false);
    tower.setWidgetActive(false);
    const auto paused = tower.history();
    QTest::qWait(30);
    QCOMPARE(tower.history(), paused);
}

void NetworkTrafficTowerTests::widgetAndPagePublishingAreIndependent()
{
    MockNode node;
    std::atomic<int64_t> received{1000};
    int64_t sampled_received{0};
    node.get_total_bytes_recv_fn = [&] { sampled_received = received.load(); return sampled_received; };
    node.get_total_bytes_sent_fn = [&] { return sampled_received * 2; };
    NetworkTrafficTower tower{node, TEST_SAMPLE_INTERVAL_MS};
    QSignalSpy widget_spy{&tower, &NetworkTrafficTower::widgetHistoryChanged};
    tower.setWidgetActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(tower.widgetHistory().size() >= 5, ASYNC_TIMEOUT_MS);
    QVERIFY(tower.history().isEmpty());
    QCOMPARE(tower.totalBytesReceived(), quint64{1000});

    tower.updateFilterWindowSize(1000);
    tower.setActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(!tower.history().isEmpty(), ASYNC_TIMEOUT_MS);
    received += 100000;
    QTRY_VERIFY_WITH_TIMEOUT(MaximumRate(tower.history(), "received") > 0, ASYNC_TIMEOUT_MS);
    float maximum_widget_rate{0};
    for (const auto& point : tower.widgetHistory()) {
        const auto sample = point.toMap();
        maximum_widget_rate = std::max(maximum_widget_rate, sample.value("received").toFloat());
        QCOMPARE(sample.value("sent").toFloat(), sample.value("received").toFloat() * 2);
    }
    QVERIFY(maximum_widget_rate > MaximumRate(tower.history(), "received"));

    tower.setActive(false);
    const auto page_history = tower.history();
    const auto widget_signals = widget_spy.count();
    received += 1000;
    QTRY_VERIFY_WITH_TIMEOUT(widget_spy.count() > widget_signals, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesReceived(), quint64{102000}, ASYNC_TIMEOUT_MS);
    QCOMPARE(tower.history(), page_history);

    tower.setActive(true);
    tower.setWidgetActive(false);
    const auto widget_history = tower.widgetHistory();
    const auto page_size = tower.history().size();
    QTRY_VERIFY_WITH_TIMEOUT(tower.history().size() > page_size, ASYNC_TIMEOUT_MS);
    QCOMPARE(tower.widgetHistory(), widget_history);
    tower.setActive(false);
    QTest::qWait(30);
    const auto signals_while_hidden = widget_spy.count();
    QTest::qWait(30);
    QCOMPARE(widget_spy.count(), signals_while_hidden);
    tower.setWidgetActive(true);
    QTRY_VERIFY_WITH_TIMEOUT(widget_spy.count() > signals_while_hidden, ASYNC_TIMEOUT_MS);
    QVERIFY(tower.widgetHistory().size() > widget_history.size());
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NetworkTrafficTowerTests)
#else
QTEST_MAIN(NetworkTrafficTowerTests)
#endif
#include "test_networktraffictower.moc"
