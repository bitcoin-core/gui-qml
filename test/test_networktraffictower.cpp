// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/mocks/drainexecutors.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <qml/models/networktraffictower.h>

#include <algorithm>
#include <atomic>
#include <stdexcept>

#include <QSignalSpy>
#include <QThread>

namespace {
class ManualTrafficTimer final : public TrafficTimer {
public:
    std::function<void()> tick;
    void restart(int,std::function<void()> value) override {tick=std::move(value);}
    void stop() override {tick={};}
    void advance(){if(tick)tick();}
};
constexpr int TEST_SAMPLE_INTERVAL_MS{10};
constexpr int ASYNC_TIMEOUT_MS{1'000};
} // namespace

class NetworkTrafficTowerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void samplesOnlyAfterInitializationAndNeverAfterShutdown();
    void samplesOffGuiThreadWithoutPublishingWhileInactive();
    void activeControlsPublishingWithoutDiscardingBackgroundHistory();
    void filterWindowChangesPreserveTotalsAndHistory();
    void stopsSamplingWhenDestroyedWhileActive();
    void manualSamplesAndActivationFence();
    void manualFailureRejectionAndDrain();
};

void NetworkTrafficTowerTests::samplesOnlyAfterInitializationAndNeverAfterShutdown()
{
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    std::atomic<int> calls{0};
    node.get_total_bytes_recv_fn = [&] { ++calls; return int64_t{100}; };
    NetworkTrafficTower tower{node, TEST_SAMPLE_INTERVAL_MS};
    tower.setActive(true);
    tower.updateFilterWindowSize(1);
    QTest::qWait(TEST_SAMPLE_INTERVAL_MS * 3);
    QCOMPARE(calls.load(), 0);
    tower.startSampling();
    tower.startSampling();
    QTRY_VERIFY_WITH_TIMEOUT(calls.load() >= 3, ASYNC_TIMEOUT_MS);
    QSignalSpy drained(&tower, &NetworkTrafficTower::drained);
    tower.beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, ASYNC_TIMEOUT_MS);
    const int stopped_calls = calls.load();
    tower.startSampling();
    tower.setActive(false);
    tower.setActive(true);
    QTest::qWait(TEST_SAMPLE_INTERVAL_MS * 3);
    QCOMPARE(calls.load(), stopped_calls);
}

void NetworkTrafficTowerTests::samplesOffGuiThreadWithoutPublishingWhileInactive()
{
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
        tower.startSampling();
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

    {const auto finish_native=DrainExecutorsBeforeNativeOwnerDestruction();}
    const int calls_after_destruction{received_calls.load()};
    QTest::qWait(TEST_SAMPLE_INTERVAL_MS * 3);
    QCOMPARE(received_calls.load(), calls_after_destruction);
}

void NetworkTrafficTowerTests::activeControlsPublishingWithoutDiscardingBackgroundHistory()
{
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
    tower.startSampling();
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
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    std::atomic<int> received_calls{0};

    node.get_total_bytes_recv_fn = [&] {
        ++received_calls;
        return int64_t{1'000};
    };
    node.get_total_bytes_sent_fn = [] {
        return int64_t{2'000};
    };

    NetworkTrafficTower tower{node, TEST_SAMPLE_INTERVAL_MS};
    tower.startSampling();
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
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
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
        tower.startSampling();
        tower.setActive(true);
        QTRY_VERIFY_WITH_TIMEOUT(received_calls.load() >= 3, ASYNC_TIMEOUT_MS);
        QVERIFY(tower.active());
    }

    {const auto finish_native=DrainExecutorsBeforeNativeOwnerDestruction();}
    const int calls_after_destruction{received_calls.load()};
    QTest::qWait(TEST_SAMPLE_INTERVAL_MS * 3);
    QCOMPARE(received_calls.load(), calls_after_destruction);
}

void NetworkTrafficTowerTests::manualSamplesAndActivationFence()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    auto timer=std::make_shared<ManualTrafficTimer>();
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    int64_t received{100},sent{200};
    node.get_total_bytes_recv_fn=[&]{return received;};node.get_total_bytes_sent_fn=[&]{return sent;};
    NetworkTrafficTower tower{node,1000,{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}},timer};
    auto publish=[&]{if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;};
    auto complete=[&]{return transport.step(execution::Lane::Core)&&transport.step(execution::Lane::Domain)&&publish();};
    tower.setActive(true);tower.startSampling();QCOMPARE(node.calls.getTotalBytesRecv.load(),0);
    QVERIFY(complete());
    QCOMPARE(node.calls.getTotalBytesRecv.load(),0); // Configuration cannot sample inline.
    QCOMPARE(gui.size(),size_t{1});
    QCOMPARE(transport.queuedCountIf([](const auto& turn) {
        return turn.phase == execution::Phase::Queued && (turn.lane == execution::Lane::Core || turn.lane == execution::Lane::Domain);
    }), 0); // Native cleanup and its ledger completion are independent.
    QVERIFY(publish()); // Explicit guarded successor admits the original sample.
    QCOMPARE(node.calls.getTotalBytesRecv.load(),0);
    QCOMPARE(tower.totalBytesReceived(), quint64{0}); QCOMPARE(tower.totalBytesSent(), quint64{0});
    QVERIFY(complete());QCOMPARE(tower.totalBytesReceived(),quint64{100}); QCOMPARE(tower.totalBytesSent(),quint64{200});
    QCOMPARE(node.calls.getTotalBytesRecv.load(),1);
    received=140;sent=260;timer->advance();QVERIFY(transport.step(execution::Lane::Core));
    QCOMPARE(tower.totalBytesReceived(),quint64{100}); QCOMPARE(tower.totalBytesSent(),quint64{200}); QVERIFY(transport.step(execution::Lane::Domain));
    tower.setActive(false);QVERIFY(publish());QCOMPARE(tower.totalBytesReceived(),quint64{100});
    QCOMPARE(node.calls.getTotalBytesRecv.load(),2);
    QCOMPARE(gui.size(),size_t{1});
    QCOMPARE(transport.queuedCountIf([](const auto& turn) {
        return turn.phase == execution::Phase::Queued && (turn.lane == execution::Lane::Core || turn.lane == execution::Lane::Domain);
    }), 0);
    QVERIFY(publish()); // Stale activation completion posts, never recursively executes, its successor.
    QCOMPARE(node.calls.getTotalBytesRecv.load(),2);
    // History sampled while inactive survives activation; an older active result cannot publish.
    QVERIFY(complete());QCOMPARE(node.calls.getTotalBytesRecv.load(),2);
    received=200;sent=300;timer->advance();QVERIFY(complete());
    QCOMPARE(node.calls.getTotalBytesRecv.load(),3);
    tower.setActive(true);QVERIFY(complete());QCOMPARE(tower.totalBytesReceived(),quint64{200});
    QCOMPARE(node.calls.getTotalBytesRecv.load(),3);
    QCOMPARE(tower.receivedRateList().size(),2);QCOMPARE(tower.receivedRateList().front(),50.0f);
    tower.updateFilterWindowSize(1);QVERIFY(complete());QCOMPARE(tower.receivedRateList().front(),60.0f);
    QCOMPARE(node.calls.getTotalBytesRecv.load(),3);
    tower.closeAdmission();QVERIFY(!timer->tick);transport.drain();QVERIFY(transport.quiescent());
}
void NetworkTrafficTowerTests::manualFailureRejectionAndDrain()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    auto timer=std::make_shared<ManualTrafficTimer>();
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    bool fail{false};node.get_total_bytes_recv_fn=[&]() -> int64_t {if(fail)throw std::runtime_error("read failed");return 100;};
    node.get_total_bytes_sent_fn=[]{return int64_t{200};};
    NetworkTrafficTower tower{node,1000,{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}},timer};
    auto publish=[&]{if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;};
    tower.startSampling();QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(publish());
    fail=true;timer->advance();tower.updateFilterWindowSize(2);
    QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(publish());
    QVERIFY(!tower.readError().isEmpty());QVERIFY(!tower.samplePending());
    // The coalesced request is an independently controlled presentation turn,
    // never a recursive submission from the failure callback.
    QVERIFY((transport.queuedCountIf([](const auto& turn){return turn.phase==execution::Phase::Queued&&(turn.lane==execution::Lane::Core||turn.lane==execution::Lane::Domain);}) == 0));
    QCOMPARE(gui.size(),size_t{1});
    fail=false;QVERIFY(publish());QVERIFY(tower.samplePending());
    QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(publish());
    QVERIFY(tower.readError().isEmpty());QVERIFY(!tower.samplePending());
    transport.rejectLane(execution::Lane::Core);timer->advance();QVERIFY(publish());QVERIFY(!tower.readError().isEmpty());
    fail=false;timer->advance();QSignalSpy drained{&tower,&NetworkTrafficTower::drained};
    QSignalSpy history{&tower,&NetworkTrafficTower::receivedRateListChanged};
    tower.closeAdmission();tower.beginShutdown();QCOMPARE(drained.size(),0);
    QVERIFY(transport.step(execution::Lane::Core));QCOMPARE(drained.size(),0);
    QVERIFY(transport.step(execution::Lane::Domain));while(publish()){}
    QCOMPARE(drained.size(),1);QCOMPARE(history.size(),0);QVERIFY(!timer->tick);
    transport.drain();QVERIFY(transport.quiescent());
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NetworkTrafficTowerTests)
#else
QTEST_MAIN(NetworkTrafficTowerTests)
#endif
#include <test_networktraffictower.moc>
