// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/test/mocks/mocknode.h>
#include <qml/test/qt_test_registry.h>

#include <qml/models/networktraffictower.h>

#include <atomic>

#include <QSignalSpy>
#include <QThread>

namespace {
constexpr int TEST_SAMPLE_INTERVAL_MS{10};
constexpr int ASYNC_TIMEOUT_MS{1'000};
} // namespace

class NetworkTrafficTowerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void samplesOffGuiThreadWithoutPublishingWhileInactive();
    void activeControlsPublishingWithoutDiscardingBackgroundHistory();
    void filterWindowChangesPreserveTotalsAndHistory();
    void stopsSamplingWhenDestroyedWhileActive();
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
        QSignalSpy received_list_spy{&tower, &NetworkTrafficTower::receivedRateListChanged};
        QSignalSpy sent_list_spy{&tower, &NetworkTrafficTower::sentRateListChanged};

        QTRY_VERIFY_WITH_TIMEOUT(received_calls.load() >= 3, ASYNC_TIMEOUT_MS);
        QTRY_VERIFY_WITH_TIMEOUT(sent_calls.load() >= 3, ASYNC_TIMEOUT_MS);
        QVERIFY(!sampled_on_gui_thread.load());
        QVERIFY(!tower.active());
        QVERIFY(tower.receivedRateList().isEmpty());
        QVERIFY(tower.sentRateList().isEmpty());
        QCOMPARE(received_list_spy.count(), 0);
        QCOMPARE(sent_list_spy.count(), 0);
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
    QSignalSpy received_list_spy{&tower, &NetworkTrafficTower::receivedRateListChanged};
    QSignalSpy sent_list_spy{&tower, &NetworkTrafficTower::sentRateListChanged};

    tower.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesReceived(), quint64{1'000}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesSent(), quint64{2'000}, ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!tower.receivedRateList().isEmpty(), ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!tower.sentRateList().isEmpty(), ASYNC_TIMEOUT_MS);

    tower.setActive(false);
    const quint64 published_total{tower.totalBytesReceived()};
    const qsizetype published_history_size{tower.receivedRateList().size()};
    const qsizetype received_signals_before_hidden_samples{received_list_spy.count()};
    const qsizetype sent_signals_before_hidden_samples{sent_list_spy.count()};
    const int calls_before_hidden_samples{received_calls.load()};

    total_received = 1'300;
    total_sent = 2'600;
    QTRY_VERIFY_WITH_TIMEOUT(received_calls.load() >= calls_before_hidden_samples + 3, ASYNC_TIMEOUT_MS);
    QCoreApplication::processEvents();

    QCOMPARE(tower.totalBytesReceived(), published_total);
    QCOMPARE(tower.receivedRateList().size(), published_history_size);
    QCOMPARE(received_list_spy.count(), received_signals_before_hidden_samples);
    QCOMPARE(sent_list_spy.count(), sent_signals_before_hidden_samples);

    tower.setActive(true);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesReceived(), quint64{1'300}, ASYNC_TIMEOUT_MS);
    QTRY_COMPARE_WITH_TIMEOUT(tower.totalBytesSent(), quint64{2'600}, ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(tower.receivedRateList().size() > published_history_size, ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(received_list_spy.count() > received_signals_before_hidden_samples, ASYNC_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(sent_list_spy.count() > sent_signals_before_hidden_samples, ASYNC_TIMEOUT_MS);
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
    QTRY_VERIFY_WITH_TIMEOUT(tower.receivedRateList().size() > 20, ASYNC_TIMEOUT_MS);

    tower.updateFilterWindowSize(1);
    QTRY_VERIFY_WITH_TIMEOUT(tower.receivedRateList().size() <= 10, ASYNC_TIMEOUT_MS);
    const qsizetype short_history_size{tower.receivedRateList().size()};
    QVERIFY(short_history_size > 0);
    QCOMPARE(tower.totalBytesReceived(), quint64{1'000});
    QCOMPARE(tower.totalBytesSent(), quint64{2'000});

    tower.updateFilterWindowSize(2);
    QTRY_VERIFY_WITH_TIMEOUT(tower.receivedRateList().size() > short_history_size, ASYNC_TIMEOUT_MS);
    QVERIFY(tower.receivedRateList().size() <= 20);
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

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NetworkTrafficTowerTests)
#else
QTEST_MAIN(NetworkTrafficTowerTests)
#endif
#include "test_networktraffictower.moc"
