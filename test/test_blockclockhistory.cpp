// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockclockhistory.h>

#include <test/qt_test_registry.h>

#include <interfaces/handler.h>
#include <kernel/chain.h>
#include <kernel/types.h>
#include <primitives/block.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <latch>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>

#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QSemaphore>

namespace {
void NotifyBlock(BlockClockHistory& history, uint8_t id, uint32_t timestamp, bool connected = true, bool historical = false)
{
    const uint256 hash{id};
    CBlock block;
    block.nTime = timestamp;
    interfaces::BlockInfo info{hash};
    info.data = &block;
    if (connected) {
        history.blockConnected(kernel::ChainstateRole{.historical = historical}, info);
    } else {
        history.blockDisconnected(info);
    }
}

QList<qint64> SortedSnapshot(BlockClockHistory& history)
{
    auto snapshot{history.takeSnapshot().value_or(QList<qint64>{})};
    std::sort(snapshot.begin(), snapshot.end());
    return snapshot;
}

struct HistoryHandlerState {
    int disconnects{0};
    bool published_during_disconnect{false};
};

/** Retain the receiver like Core's proxy, and deliver one last callback on disconnect. */
class HistoryHandler final : public interfaces::Handler
{
public:
    HistoryHandler(std::shared_ptr<BlockClockHistory> history, HistoryHandlerState& state)
        : m_history{std::move(history)}, m_state{state}
    {
    }

    ~HistoryHandler() override { disconnect(); }

    void disconnect() override
    {
        if (!m_history) return;
        ++m_state.disconnects;
        NotifyBlock(*m_history, 42, 1200);
        m_state.published_during_disconnect = m_history->takeSnapshot().has_value();
        m_history.reset();
    }

private:
    std::shared_ptr<BlockClockHistory> m_history;
    HistoryHandlerState& m_state;
};
} // namespace

class BlockClockHistoryTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void seedAndEventsPreserveBlockIdentity();
    void initialSnapshotReconcilesQueuedEvents();
    void snapshotCanOverlapAnyPartOfAReorganization();
    void oldIbdAndHistoricalEventsDoNotPublishSnapshots();
    void futureTimestampsSurviveRollover();
    void retentionChangeDuringInitializationFiltersSnapshot();
    void stopMakesLateEventsHarmless();
    void changedCallbackReportsInitializationAndRealChanges();
    void detachingChangedCallbackFinishesInFlightPosting();
    void snapshotsAreSafeDuringNotifications();
    void subscriptionReleaseHasNoOwnershipCycle();
    void subscriptionStopsBeforeUnregisteringOnlyOnce();
    void subscriptionUnwindsOnInitializationException();
    void callbacksOutliveSubscriptionAndGuiOwners();
    void subscriptionDisconnectIsSafeDuringNotifications();
};

void BlockClockHistoryTests::seedAndEventsPreserveBlockIdentity()
{
    BlockClockHistory history;
    history.setRetentionStart(1000);
    history.initialize({{uint256{1}, 999}, {uint256{2}, 1100}, {uint256{3}, 1100}});
    QCOMPARE(SortedSnapshot(history), QList<qint64>({1100, 1100}));
    QVERIFY(!history.takeSnapshot());

    NotifyBlock(history, 2, 1100); // Already included in the initial snapshot.
    QVERIFY(!history.takeSnapshot());
    NotifyBlock(history, 4, 1200);
    QCOMPARE(SortedSnapshot(history), QList<qint64>({1100, 1100, 1200}));

    NotifyBlock(history, 2, 1100, false);
    QCOMPARE(SortedSnapshot(history), QList<qint64>({1100, 1200}));
    NotifyBlock(history, 2, 1100, false);
    QVERIFY(!history.takeSnapshot());

    // A replacement tip outside the displayed period must not retain a
    // disconnected block's timestamp.
    NotifyBlock(history, 4, 1200, false);
    NotifyBlock(history, 5, 900);
    QCOMPARE(SortedSnapshot(history), QList<qint64>({1100}));
}

void BlockClockHistoryTests::initialSnapshotReconcilesQueuedEvents()
{
    BlockClockHistory history;
    history.setRetentionStart(1000);
    NotifyBlock(history, 1, 1100);
    NotifyBlock(history, 1, 1100, false);
    NotifyBlock(history, 2, 1200, false);
    NotifyBlock(history, 2, 1200);
    NotifyBlock(history, 3, 1300);
    NotifyBlock(history, 4, 1400, false);
    QVERIFY(!history.takeSnapshot());

    // The snapshot can already contain some notifications and still contain
    // blocks whose disconnections were delivered while it was being loaded.
    history.initialize({{uint256{1}, 1100}, {uint256{3}, 1300}, {uint256{4}, 1400}, {uint256{5}, 1500}});
    QCOMPARE(SortedSnapshot(history), QList<qint64>({1200, 1300, 1500}));
    QVERIFY(!history.takeSnapshot());
}

void BlockClockHistoryTests::snapshotCanOverlapAnyPartOfAReorganization()
{
    struct Event {
        uint8_t id;
        uint32_t timestamp;
        bool connected;
    };
    const std::array events{
        Event{2, 1200, false},
        Event{3, 1200, true},
        Event{4, 1300, true},
        Event{4, 1300, false},
        Event{3, 1200, false},
        Event{2, 1200, true},
        Event{5, 1300, true},
    };
    for (size_t snapshot_position{0}; snapshot_position <= events.size(); ++snapshot_position) {
        BlockClockHistory::Blocks snapshot{{uint256{1}, 1100}, {uint256{2}, 1200}};
        for (size_t i{0}; i < snapshot_position; ++i) {
            const auto& event{events[i]};
            if (event.connected) {
                snapshot.insert_or_assign(uint256{event.id}, event.timestamp);
            } else {
                snapshot.erase(uint256{event.id});
            }
        }
        BlockClockHistory history;
        history.setRetentionStart(1000);
        for (const auto& event : events) NotifyBlock(history, event.id, event.timestamp, event.connected);
        history.initialize(std::move(snapshot));
        QCOMPARE(SortedSnapshot(history), QList<qint64>({1100, 1200, 1300}));
    }
}

void BlockClockHistoryTests::oldIbdAndHistoricalEventsDoNotPublishSnapshots()
{
    BlockClockHistory history;
    history.setRetentionStart(1000);
    for (int i{0}; i < 10'000; ++i) NotifyBlock(history, 1, 999);
    NotifyBlock(history, 2, 1100, true, true);
    history.initialize({});
    QCOMPARE(SortedSnapshot(history), QList<qint64>{});

    for (int i{0}; i < 10'000; ++i) NotifyBlock(history, 1, 999);
    NotifyBlock(history, 2, 1100, true, true);
    QVERIFY(!history.takeSnapshot());
}

void BlockClockHistoryTests::futureTimestampsSurviveRollover()
{
    BlockClockHistory history;
    history.setRetentionStart(1000);
    history.initialize({{uint256{1}, 1100}, {uint256{2}, 45'000}});
    QCOMPARE(SortedSnapshot(history), QList<qint64>({1100, 45'000}));
    NotifyBlock(history, 3, 45'100);
    history.setRetentionStart(44'200);
    QCOMPARE(SortedSnapshot(history), QList<qint64>({45'000, 45'100}));
    NotifyBlock(history, 2, 45'000, false);
    QCOMPARE(SortedSnapshot(history), QList<qint64>({45'100}));
}

void BlockClockHistoryTests::retentionChangeDuringInitializationFiltersSnapshot()
{
    BlockClockHistory history;
    history.setRetentionStart(1000);
    NotifyBlock(history, 1, 1100);
    NotifyBlock(history, 2, 1300);
    history.setRetentionStart(1200);
    history.initialize({{uint256{3}, 1150}, {uint256{4}, 1400}});
    QCOMPARE(SortedSnapshot(history), QList<qint64>({1300, 1400}));
}

void BlockClockHistoryTests::stopMakesLateEventsHarmless()
{
    BlockClockHistory history;
    history.setRetentionStart(1000);
    NotifyBlock(history, 1, 1100);
    history.stop();
    history.initialize({{uint256{2}, 1200}});
    NotifyBlock(history, 3, 1300);
    QVERIFY(!history.takeSnapshot());

    BlockClockHistory initialized;
    initialized.initialize({{uint256{1}, 1100}});
    initialized.stop();
    NotifyBlock(initialized, 1, 1100, false);
    NotifyBlock(initialized, 2, 1200);
    QVERIFY(!initialized.takeSnapshot());
}

void BlockClockHistoryTests::changedCallbackReportsInitializationAndRealChanges()
{
    BlockClockHistory history;
    history.setRetentionStart(1000);
    std::atomic_int notifications{0};
    history.setChangedCallback([&] { ++notifications; });
    NotifyBlock(history, 1, 1100);
    QCOMPARE(notifications.load(), 0);
    history.initialize({{uint256{1}, 1100}});
    QCOMPARE(notifications.load(), 1);
    QCOMPARE(SortedSnapshot(history), QList<qint64>{1100});

    NotifyBlock(history, 1, 1100); // Duplicate active block.
    NotifyBlock(history, 2, 999); // Outside retention.
    NotifyBlock(history, 3, 1200, true, true); // Historical chainstate.
    QCOMPARE(notifications.load(), 1);
    NotifyBlock(history, 2, 1200);
    QCOMPARE(notifications.load(), 2);
    NotifyBlock(history, 2, 1200, false);
    QCOMPARE(notifications.load(), 3);
    NotifyBlock(history, 2, 1200, false);
    QCOMPARE(notifications.load(), 3);

    history.setChangedCallback({});
    NotifyBlock(history, 3, 1300);
    QCOMPARE(notifications.load(), 3);
    // Attaching after initialization must announce an already dirty snapshot.
    history.setChangedCallback([&] { ++notifications; });
    QCOMPARE(notifications.load(), 4);
    QCOMPARE(SortedSnapshot(history), QList<qint64>({1100, 1300}));
    history.stop();
    history.setChangedCallback([&] { ++notifications; });
    NotifyBlock(history, 4, 1400);
    QCOMPARE(notifications.load(), 4);
}

void BlockClockHistoryTests::detachingChangedCallbackFinishesInFlightPosting()
{
    BlockClockHistory history;
    history.initialize({});
    history.takeSnapshot();
    QSemaphore entered;
    QSemaphore release;
    std::atomic_int posts{0};
    std::atomic_bool detach_started{false};
    std::atomic_bool detached{false};
    // Pause the equivalent of event posting without reentering the cache.
    // Detaching must not return while that callback can still touch its target.
    history.setChangedCallback([&] {
        ++posts;
        entered.release();
        release.acquire();
    });
    std::thread notifier;
    std::thread detacher;
    const auto finish = qScopeGuard([&] {
        release.release();
        if (notifier.joinable()) notifier.join();
        if (detacher.joinable()) detacher.join();
    });
    notifier = std::thread{[&] { NotifyBlock(history, 1, 1100); }};
    QTRY_VERIFY(entered.available() > 0);
    detacher = std::thread{[&] {
        detach_started = true;
        history.setChangedCallback({});
        detached = true;
    }};
    QTRY_VERIFY(detach_started.load());
    QVERIFY(!detached.load());
    release.release();
    QTRY_VERIFY(detached.load());
    NotifyBlock(history, 2, 1200);
    QCOMPARE(posts.load(), 1);
}

void BlockClockHistoryTests::snapshotsAreSafeDuringNotifications()
{
    BlockClockHistory history;
    history.setRetentionStart(1000);
    history.initialize({});
    std::atomic<bool> finished{false};
    std::thread notifications{[&] {
        for (int i{0}; i < 10'000; ++i) {
            NotifyBlock(history, 1, 1100);
            NotifyBlock(history, 2, 1200);
            NotifyBlock(history, 1, 1100, false);
            NotifyBlock(history, 2, 1200, false);
        }
        finished.store(true);
    }};
    bool valid{true};
    while (!finished.load()) {
        if (auto snapshot{history.takeSnapshot()}) {
            valid &= snapshot->size() <= 2;
            for (const qint64 timestamp : *snapshot) valid &= timestamp == 1100 || timestamp == 1200;
        }
    }
    notifications.join();
    QVERIFY(valid);
    QCOMPARE(SortedSnapshot(history), QList<qint64>{});
}

void BlockClockHistoryTests::subscriptionReleaseHasNoOwnershipCycle()
{
    auto history{std::make_shared<BlockClockHistory>()};
    history->initialize({});
    const std::weak_ptr<BlockClockHistory> weak_history{history};
    HistoryHandlerState state;
    std::unique_ptr<interfaces::Handler> subscription{std::make_unique<BlockClockSubscription>(
        history, std::make_unique<HistoryHandler>(history, state))};

    history.reset();
    QVERIFY(!weak_history.expired());
    subscription.reset();
    QCOMPARE(state.disconnects, 1);
    QVERIFY(!state.published_during_disconnect);
    QVERIFY(weak_history.expired());
}

void BlockClockHistoryTests::subscriptionStopsBeforeUnregisteringOnlyOnce()
{
    auto history{std::make_shared<BlockClockHistory>()};
    history->initialize({});
    HistoryHandlerState state;
    auto subscription{std::make_unique<BlockClockSubscription>(
        history, std::make_unique<HistoryHandler>(history, state))};

    subscription->disconnect();
    subscription->disconnect();
    QCOMPARE(state.disconnects, 1);
    // The fake handler delivers a callback while unregistering. This must see
    // the stopped cache, and must be free to acquire its mutex.
    QVERIFY(!state.published_during_disconnect);
    NotifyBlock(*history, 1, 1100);
    QVERIFY(!history->takeSnapshot());
    subscription.reset();
    QCOMPARE(state.disconnects, 1);
}

void BlockClockHistoryTests::subscriptionUnwindsOnInitializationException()
{
    auto history{std::make_shared<BlockClockHistory>()};
    history->initialize({});
    const std::weak_ptr<BlockClockHistory> weak_history{history};
    HistoryHandlerState state;
    const auto failed_initialization = [&] {
        BlockClockSubscription subscription{history, std::make_unique<HistoryHandler>(history, state)};
        history.reset();
        throw std::runtime_error{"initial snapshot failed"};
    };

    bool threw_expected{false};
    try {
        failed_initialization();
    } catch (const std::runtime_error&) {
        threw_expected = true;
    } catch (...) {
        QFAIL("Expected std::runtime_error from the failed history initialization");
    }
    QVERIFY(threw_expected);
    QCOMPARE(state.disconnects, 1);
    QVERIFY(!state.published_during_disconnect);
    QVERIFY(weak_history.expired());
}

void BlockClockHistoryTests::callbacksOutliveSubscriptionAndGuiOwners()
{
    auto history{std::make_shared<BlockClockHistory>()};
    history->initialize({});
    const std::weak_ptr<BlockClockHistory> weak_history{history};
    HistoryHandlerState state;
    auto subscription{std::make_unique<BlockClockSubscription>(
        history, std::make_unique<HistoryHandler>(history, state))};
    // Core preserves an executing receiver after its registration is removed.
    auto in_flight_receiver{history};
    history.reset();
    subscription.reset();

    QCOMPARE(state.disconnects, 1);
    QVERIFY(!weak_history.expired());
    NotifyBlock(*in_flight_receiver, 1, 1100);
    NotifyBlock(*in_flight_receiver, 1, 1100, false);
    QVERIFY(!in_flight_receiver->takeSnapshot());
    in_flight_receiver.reset();
    QVERIFY(weak_history.expired());
}

void BlockClockHistoryTests::subscriptionDisconnectIsSafeDuringNotifications()
{
    auto history{std::make_shared<BlockClockHistory>()};
    history->initialize({});
    const std::weak_ptr<BlockClockHistory> weak_history{history};
    HistoryHandlerState state;
    auto subscription{std::make_unique<BlockClockSubscription>(
        history, std::make_unique<HistoryHandler>(history, state))};
    std::latch callback_started{1};
    std::latch disconnected{1};
    bool published_after_disconnect{false};
    std::thread notifications{[receiver = history, &callback_started, &disconnected, &published_after_disconnect] {
        NotifyBlock(*receiver, 1, 1100);
        callback_started.count_down();
        for (int i{0}; i < 10'000; ++i) {
            NotifyBlock(*receiver, 1, 1100);
            NotifyBlock(*receiver, 1, 1100, false);
        }
        disconnected.wait();
        NotifyBlock(*receiver, 2, 1200);
        published_after_disconnect = receiver->takeSnapshot().has_value();
    }};

    history.reset();
    callback_started.wait();
    subscription.reset();
    disconnected.count_down();
    notifications.join();

    QCOMPARE(state.disconnects, 1);
    QVERIFY(!state.published_during_disconnect);
    QVERIFY(!published_after_disconnect);
    QVERIFY(weak_history.expired());
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(BlockClockHistoryTests)
#else
QTEST_MAIN(BlockClockHistoryTests)
#endif
#include "test_blockclockhistory.moc"
