// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockclockhistory.h>
#include <qml/models/blockclockmodel.h>
#include <qml/backendexecutor.h>
#include <qml/retainedsnapshot.h>
#include <test/mocks/manualexecutionenvironment.h>

#include <kernel/chain.h>
#include <kernel/types.h>
#include <primitives/block.h>
#include <test/qt_test_registry.h>
#include <uint256.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include <QtTest/QtTest>
#include <QEvent>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QTimeZone>

namespace {
bool AwaitHistory(BlockClockModel& model)
{
    QCoreApplication::processEvents();
    return QTest::qWaitFor([&] { return !model.historyPending(); }, 10000) && model.historyError().isEmpty();
}
bool AwaitHistory(const std::unique_ptr<BlockClockModel>& model)
{
    if (model) return AwaitHistory(*model);
    QCoreApplication::processEvents(); return true;
}

QDateTime UtcTime(int hour, int minute = 0, int second = 0)
{
    return QDateTime{QDate{2026, 7, 17}, QTime{hour, minute, second}, QTimeZone::utc()};
}

void ConnectBlock(BlockClockHistory& history, uint8_t id, qint64 timestamp)
{
    CBlock block;
    block.nTime = static_cast<uint32_t>(timestamp);
    const uint256 hash{id};
    interfaces::BlockInfo info{hash};
    info.data = &block;
    history.blockConnected(kernel::ChainstateRole{}, info);
}

void DisconnectBlock(BlockClockHistory& history, uint8_t id, qint64 timestamp)
{
    CBlock block;
    block.nTime = static_cast<uint32_t>(timestamp);
    const uint256 hash{id};
    interfaces::BlockInfo info{hash};
    info.data = &block;
    history.blockDisconnected(info);
}
} // namespace

class BlockClockModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void cleanupTestCase();
    void manualHistoryFencesHeldOldPeriodAndCoalescesNativeBursts();
    void manualHistoryQuietCloseReleasesAcceptedProjection();
    void retentionRequestsDoNotWaitForNativeHistoryMutex();
    void replacementObserverSurvivesOriginalNativeRetirement();
    void throwingPresentationPortCannotEscapeCoreOrStickWakeup();
    void currentTimeUsesNamedTwelveHourFraction();
    void unchangedSecondDoesNotRepublishCurrentTime();
    void blockArrivalRefreshesCurrentTime();
    void blockHistoryPreservesEqualTimestampsAndPeriodBounds();
    void initialHistoryCanArriveAfterTheModel();
    void historyInitializedBeforeModelIsPublished();
    void workerNotificationsPublishWithoutHistoryTimer();
    void notificationDuringPublicationSchedulesFollowup();
    void idleHistoryDoesNotScheduleRepeatedUpdates();
    void destroyedModelIgnoresPendingAndLateWorkerNotifications();
    void cachedHistorySurvivesPeriodRollover();
    void blockNotificationsAreCoalesced();
    void repeatedBlockNotificationDoesNotDuplicateHistory();
    void disconnectRemovesOnlyTheMatchingBlock();
    void oldIbdNotificationsDoNotRepublishHistory();
    void blockArrivalAtRolloverIsNotCountedTwice();
    void emptyHistoryDoesNotEmitRedundantChanges();
    void timersHaveModelOwnershipAndCanBeDisabledForTests();
    void timerAlignsToNextMinute();
    void minuteTimerAdvancesWithoutBlockNotifications();
    void stoppedModelDoesNotConsumeHistoryOrRestartTimers();
private:
    std::unique_ptr<BackendExecutor> m_executor;
};

void BlockClockModelTests::initTestCase()
{
    m_executor = std::make_unique<BackendExecutor>(execution::Options{});
}
void BlockClockModelTests::cleanupTestCase()
{
    bool drained{false}; m_executor->whenDrained(this, [&] { drained = true; }); m_executor->shutdown();
    QTRY_VERIFY(drained); m_executor.reset();
}


void BlockClockModelTests::manualHistoryFencesHeldOldPeriodAndCoalescesNativeBursts()
{
    ManualExecutionEnvironment env;
    auto history = std::make_shared<BlockClockHistory>(); history->initialize({});
    QDateTime now = UtcTime(15);
    BlockClockModel model{history, {env.scheduler, [&](auto publish) { env.post(std::move(publish)); }}, false, [&] { return now; }};
    const auto settle = [&] { for (int turn = 0; turn < 32 && (!env.turnsIdle() || !env.publicationsIdle()); ++turn) { env.drain(); env.publishAll(); } };
    settle(); QVERIFY(!model.historyPending()); QVERIFY(model.historyError().isEmpty());
    QSignalSpy changes{&model, &BlockClockModel::blockTimeFractionsChanged};
    std::thread notifications{[history] {
        for (uint8_t id = 1; id <= 100; ++id) ConnectBlock(*history, id, UtcTime(13).toSecsSinceEpoch());
        for (int repeat = 0; repeat < 10000; ++repeat) {
            DisconnectBlock(*history, 100, UtcTime(13).toSecsSinceEpoch());
            ConnectBlock(*history, 100, UtcTime(13).toSecsSinceEpoch());
        }
    }};
    notifications.join();
    QCOMPARE(env.publicationCount(), size_t{1}); // Exact Native ingress coalescing, independent of QObject transport identity.
    QVERIFY(env.turnsIdle()); QCOMPARE(changes.count(), 0);
    QVERIFY(env.publishNext()); env.drain(); // Old-period output is prepared, GUI outcome held.
    QVERIFY(model.blockTimeFractions().isEmpty());
    now = UtcTime(3).addDays(1); model.updateCurrentTime(now);
    ConnectBlock(*history, 200, UtcTime(1).addDays(1).toSecsSinceEpoch());
    env.publishAll(); QCOMPARE(changes.count(), 0); // Neither old period nor known-stale generation may publish.
    settle();
    QCOMPARE(model.periodStart(), UtcTime(0).addDays(1).toSecsSinceEpoch());
    QCOMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QCOMPARE(changes.count(), 1);
    model.closeAdmission(); settle(); QVERIFY(env.quiescent());
}
void BlockClockModelTests::manualHistoryQuietCloseReleasesAcceptedProjection()
{
    ManualExecutionEnvironment env;
    auto history = std::make_shared<BlockClockHistory>();
    history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    auto model = std::make_unique<BlockClockModel>(history, execution::SnapshotServices{env.scheduler, [&](auto publish) { env.post(std::move(publish)); }}, false, [] { return UtcTime(15); });
    QSignalSpy changes{model.get(), &BlockClockModel::blockTimeFractionsChanged};
    env.drain(); QVERIFY(model->historyPending());
    model->closeAdmission(); model.reset();
    env.drain(); env.publishAll(); env.drain();
    QCOMPARE(changes.count(), 0); QVERIFY(env.quiescent());
    std::thread retire{[history = std::move(history)]() mutable { history.reset(); }}; retire.join();
}
void BlockClockModelTests::retentionRequestsDoNotWaitForNativeHistoryMutex()
{
    auto history = std::make_shared<BlockClockHistory>();
    QSemaphore entered, release;
    std::atomic_bool timed_out{false};
    history->setChangedCallback([&] { entered.release(); timed_out = !release.tryAcquire(1, 5000); });
    std::thread native{[history] { history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}}); }};
    auto finish = qScopeGuard([&] { release.release(); native.join(); });
    QVERIFY(entered.tryAcquire(1, 5000));
    ManualExecutionEnvironment env;
    BlockClockModel model{history, {env.scheduler, [&](auto publish) { env.post(std::move(publish)); }}, false, [] { return UtcTime(15); }};
    model.updateCurrentTime(UtcTime(3).addDays(1));
    model.closeAdmission(); // Neither atomic retention nor quiet destruction takes the held Native mutex.
    QVERIFY(!timed_out.load());
    release.release(); native.join(); finish.dismiss();
    env.drain(); env.publishAll(); env.drain(); QVERIFY(env.quiescent());
}

void BlockClockModelTests::replacementObserverSurvivesOriginalNativeRetirement()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> publications;
    const auto publish_all = [&] { while (!publications.empty()) { auto publish = std::move(publications.front()); publications.pop_front(); publish(); } };
    const auto settle = [&] { for (int turn = 0; turn < 32 && (!env.queuedIdle() || !publications.empty()); ++turn) { env.drain(); publish_all(); } };
    const execution::SnapshotServices services{env.scheduler, [&](auto publish) { publications.push_back(std::move(publish)); }};
    auto history = std::make_shared<BlockClockHistory>(); history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    BlockClockModel original{history, services, false, [] { return UtcTime(15); }};
    settle(); QCOMPARE(original.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    original.closeAdmission(); // Original callback cleanup remains held on Cleanup.
    BlockClockModel replacement{history, services, false, [] { return UtcTime(15); }};
    while (env.step(execution::Lane::Domain)) {} // Register the new original observer before retiring the old token.
    settle(); QCOMPARE(replacement.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    ConnectBlock(*history, 2, UtcTime(14).toSecsSinceEpoch());
    QCOMPARE(publications.size(), size_t{1});
    settle(); QCOMPARE(replacement.blockTimeFractions(), (QList<qreal>{1.0 / 12.0, 2.0 / 12.0}));
    replacement.closeAdmission(); settle(); QVERIFY(env.quiescent());
}

void BlockClockModelTests::throwingPresentationPortCannotEscapeCoreOrStickWakeup()
{
    ManualExecutionEnvironment env;
    auto history = std::make_shared<BlockClockHistory>(); history->initialize({});
    std::atomic_int rejected{0};
    BlockClockModel model{history, {env.scheduler, [&](auto) { ++rejected; throw std::runtime_error("injected post rejection"); }}, false, [] { return UtcTime(15); }};
    const auto settle = [&] { for (int turn = 0; turn < 32; ++turn) { env.drain(); QCoreApplication::processEvents(); if (env.turnsIdle() && !model.historyPending()) break; } };
    settle(); QVERIFY(model.historyError().isEmpty());
    for (uint8_t id = 1; id <= 2; ++id) {
        std::atomic_bool escaped{false};
        std::thread native{[&] { try { ConnectBlock(*history, id, UtcTime(12 + id).toSecsSinceEpoch()); } catch (...) { escaped = true; } }};
        native.join(); QVERIFY(!escaped.load());
        settle(); QCOMPARE(model.blockTimeFractions().size(), qsizetype{id});
    }
    QVERIFY(rejected.load() >= 3);
    model.closeAdmission(); settle(); QVERIFY(env.quiescent());
}

void BlockClockModelTests::currentTimeUsesNamedTwelveHourFraction()
{
    BlockClockModel model{{}, execution::SnapshotServices{m_executor->scheduler(), {}}, false};
    model.updateCurrentTime(UtcTime(15));

    QCOMPARE(model.periodStart(), UtcTime(12).toSecsSinceEpoch());
    QVERIFY(qAbs(model.currentTimeFraction() - 0.25) < 0.000001);
}

void BlockClockModelTests::unchangedSecondDoesNotRepublishCurrentTime()
{
    BlockClockModel model{{}, execution::SnapshotServices{m_executor->scheduler(), {}}, false};
    model.updateCurrentTime(UtcTime(15));
    QSignalSpy current_time_spy{&model, &BlockClockModel::currentTimeFractionChanged};

    model.updateCurrentTime(UtcTime(15));
    QCOMPARE(current_time_spy.count(), 0);

    model.updateCurrentTime(UtcTime(15, 0, 1));
    QCOMPARE(current_time_spy.count(), 1);
}

void BlockClockModelTests::blockArrivalRefreshesCurrentTime()
{
    auto history{std::make_shared<BlockClockHistory>()};
    QDateTime current_time{UtcTime(15)};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [&] { return current_time; }};
    history->initialize({});
    QVERIFY(AwaitHistory(model));
    QSignalSpy current_time_spy{&model, &BlockClockModel::currentTimeFractionChanged};

    current_time = UtcTime(15, 1);
    ConnectBlock(*history, 1, UtcTime(15, 0, 30).toSecsSinceEpoch());
    QVERIFY(AwaitHistory(model));

    QCOMPARE(current_time_spy.count(), 1);
    QVERIFY(qAbs(model.currentTimeFraction() - (181.0 / 720.0)) < 0.000001);
    QCOMPARE(model.blockTimeFractions().size(), 1);
    QVERIFY(qAbs(model.blockTimeFractions().constFirst() -
                 (10830.0 / BlockClockModel::PERIOD_SECONDS)) < 0.000001);
}

void BlockClockModelTests::blockHistoryPreservesEqualTimestampsAndPeriodBounds()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    const qint64 start{model.periodStart()};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    history->initialize({
        {uint256{1}, start + 7200},
        {uint256{2}, start - 1},
        {uint256{3}, start + 3600},
        {uint256{4}, start + 7200},
        {uint256{5}, start + BlockClockModel::PERIOD_SECONDS},
    });
    QVERIFY(AwaitHistory(model));

    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0, 2.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
    QVERIFY(AwaitHistory(model));
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::initialHistoryCanArriveAfterTheModel()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    QVERIFY(AwaitHistory(model));
    QVERIFY(model.blockTimeFractions().isEmpty());
    QCOMPARE(history_spy.count(), 0);

    std::thread initializer{[history] {
        history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    }};
    initializer.join();
    // No history timer or explicit refresh is needed to publish worker results.
    QCOMPARE(history_spy.count(), 0);
    QTRY_COMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::historyInitializedBeforeModelIsPublished()
{
    auto history{std::make_shared<BlockClockHistory>()};
    std::thread initializer{[history] {
        history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    }};
    initializer.join();
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    QTRY_COMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QVERIFY(!model.timerActive());
}

void BlockClockModelTests::workerNotificationsPublishWithoutHistoryTimer()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    history->initialize({});
    QVERIFY(AwaitHistory(model));
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    bool delivered_on_gui{false};
    const auto gui_thread = QThread::currentThread();
    connect(&model, &BlockClockModel::blockTimeFractionsChanged, &model, [&] {
        delivered_on_gui = QThread::currentThread() == gui_thread;
    });
    std::thread notifications{[history] {
        ConnectBlock(*history, 1, UtcTime(13).toSecsSinceEpoch());
        ConnectBlock(*history, 2, UtcTime(13).toSecsSinceEpoch());
    }};
    notifications.join();
    QCOMPARE(history_spy.count(), 0);
    const QList<qreal> expected{1.0 / 12.0, 1.0 / 12.0};
    QTRY_COMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
    QVERIFY(delivered_on_gui);
    QCOMPARE(model.findChildren<QTimer*>().size(), 1);
    QVERIFY(!model.timerActive());

    std::thread reorganization{[history] {
        DisconnectBlock(*history, 1, UtcTime(13).toSecsSinceEpoch());
        ConnectBlock(*history, 3, UtcTime(14).toSecsSinceEpoch());
    }};
    reorganization.join();
    const QList<qreal> reorganized{1.0 / 12.0, 2.0 / 12.0};
    QTRY_COMPARE(model.blockTimeFractions(), reorganized);
    QCOMPARE(history_spy.count(), 2);
}

void BlockClockModelTests::notificationDuringPublicationSchedulesFollowup()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    history->initialize({});
    QVERIFY(AwaitHistory(model));
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    connect(&model, &BlockClockModel::blockTimeFractionsChanged, &model, [&] {
        if (model.blockTimeFractions().size() == 1) {
            // A change can arrive while the previous queued delivery is still
            // emitting GUI signals. It must not be lost behind its pending flag.
            ConnectBlock(*history, 2, UtcTime(14).toSecsSinceEpoch());
        }
    });
    ConnectBlock(*history, 1, UtcTime(13).toSecsSinceEpoch());
    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0};
    QTRY_COMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 2);
}

void BlockClockModelTests::idleHistoryDoesNotScheduleRepeatedUpdates()
{
    auto history{std::make_shared<BlockClockHistory>()};
    int clock_reads{0};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [&] { ++clock_reads; return UtcTime(15); }};
    history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    QTRY_COMPARE(model.blockTimeFractions().size(), 1);
    QVERIFY(AwaitHistory(model));
    const int reads_after_publication = clock_reads;
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    QTest::qWait(250);
    QCOMPARE(clock_reads, reads_after_publication);
    QCOMPARE(history_spy.count(), 0);
    for (const auto* timer : model.findChildren<QTimer*>()) QVERIFY(!timer->isActive());
}

void BlockClockModelTests::destroyedModelIgnoresPendingAndLateWorkerNotifications()
{
    auto history{std::make_shared<BlockClockHistory>()};
    auto model{std::make_unique<BlockClockModel>(history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); })};
    history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    QTRY_COMPARE(model->blockTimeFractions().size(), 1);
    QSignalSpy history_spy{model.get(), &BlockClockModel::blockTimeFractionsChanged};
    QSemaphore entered;
    QSemaphore release;
    std::atomic_bool finished{false};
    std::thread notifications{[history, &entered, &release, &finished] {
        ConnectBlock(*history, 2, UtcTime(14).toSecsSinceEpoch());
        entered.release();
        release.acquire();
        for (int repeat{0}; repeat < 10'000; ++repeat) {
            ConnectBlock(*history, 3, UtcTime(15).toSecsSinceEpoch());
            DisconnectBlock(*history, 3, UtcTime(15).toSecsSinceEpoch());
        }
        finished = true;
    }};
    const auto finish = qScopeGuard([&] { release.release(); notifications.join(); });
    // Preserve the first queued delivery until the GUI owner has disappeared.
    QVERIFY(entered.tryAcquire(1, 5000));
    QCOMPARE(history_spy.count(), 0);
    release.release();
    model.reset();
    QTRY_VERIFY(finished.load());
    QVERIFY(AwaitHistory(model));
    QCOMPARE(history_spy.count(), 0);
    QVERIFY(history->retentionStart().has_value());
}

void BlockClockModelTests::cachedHistorySurvivesPeriodRollover()
{
    auto history{std::make_shared<BlockClockHistory>()};
    QDateTime current_time{UtcTime(11, 59, 59)};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [&] { return current_time; }};
    history->initialize({
        {uint256{1}, UtcTime(11, 30).toSecsSinceEpoch()},
        {uint256{2}, UtcTime(12, 1).toSecsSinceEpoch()},
        {uint256{3}, UtcTime(12, 1).toSecsSinceEpoch()},
    });
    QVERIFY(AwaitHistory(model));
    QCOMPARE(model.blockTimeFractions(), QList<qreal>{11.5 / 12.0});
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    // Future-dated blocks already in the cache appear at noon without another
    // block notification or any access to the chain.
    current_time = UtcTime(12);
    model.updateCurrentTime(current_time);

    QCOMPARE(model.periodStart(), UtcTime(12).toSecsSinceEpoch());
    const QList<qreal> expected{1.0 / 720.0, 1.0 / 720.0};
    QVERIFY(AwaitHistory(model));
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::blockNotificationsAreCoalesced()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    history->initialize({});
    QVERIFY(AwaitHistory(model));
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    std::thread notifications{[history] {
        for (uint8_t id{1}; id <= 100; ++id) {
            ConnectBlock(*history, id, UtcTime(13).toSecsSinceEpoch());
        }
        // Every pair changes the cache while the GUI has not consumed the
        // preceding update; the burst must still schedule one publication.
        for (int repeat{0}; repeat < 10'000; ++repeat) {
            DisconnectBlock(*history, 100, UtcTime(13).toSecsSinceEpoch());
            ConnectBlock(*history, 100, UtcTime(13).toSecsSinceEpoch());
        }
    }};
    notifications.join();
    QCOMPARE(history_spy.count(), 0);
    QVERIFY(model.blockTimeFractions().isEmpty());

    QVERIFY(AwaitHistory(model));
    QCOMPARE(history_spy.count(), 1);
    QCOMPARE(model.blockTimeFractions().size(), 100);
    for (const qreal fraction : model.blockTimeFractions()) {
        QVERIFY(qAbs(fraction - 1.0 / 12.0) < 0.000001);
    }
    QVERIFY(AwaitHistory(model));
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::repeatedBlockNotificationDoesNotDuplicateHistory()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    const qint64 timestamp{UtcTime(13).toSecsSinceEpoch()};
    history->initialize({{uint256{1}, timestamp}});
    QVERIFY(AwaitHistory(model));
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    ConnectBlock(*history, 1, timestamp);
    QVERIFY(AwaitHistory(model));
    QCOMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QCOMPARE(history_spy.count(), 0);

    ConnectBlock(*history, 2, timestamp);
    ConnectBlock(*history, 2, timestamp);
    QVERIFY(AwaitHistory(model));
    const QList<qreal> expected{1.0 / 12.0, 1.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::disconnectRemovesOnlyTheMatchingBlock()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    history->initialize({
        {uint256{1}, UtcTime(13).toSecsSinceEpoch()},
        {uint256{2}, UtcTime(13).toSecsSinceEpoch()},
        {uint256{3}, UtcTime(14).toSecsSinceEpoch()},
    });
    QVERIFY(AwaitHistory(model));
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    // A replacement outside the displayed period still removes the former
    // tip. Equal timestamps must not cause the other block to disappear.
    DisconnectBlock(*history, 2, UtcTime(13).toSecsSinceEpoch());
    ConnectBlock(*history, 4, UtcTime(11).toSecsSinceEpoch());
    QVERIFY(AwaitHistory(model));
    const QList<qreal> expected{1.0 / 12.0, 2.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);

    DisconnectBlock(*history, 1, UtcTime(13).toSecsSinceEpoch());
    ConnectBlock(*history, 5, UtcTime(15).toSecsSinceEpoch());
    QVERIFY(AwaitHistory(model));
    const QList<qreal> reorganized{2.0 / 12.0, 3.0 / 12.0};
    QCOMPARE(model.blockTimeFractions(), reorganized);
    QCOMPARE(history_spy.count(), 2);
}

void BlockClockModelTests::oldIbdNotificationsDoNotRepublishHistory()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    history->initialize({{uint256{1}, UtcTime(13).toSecsSinceEpoch()}});
    QVERIFY(AwaitHistory(model));
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    for (uint8_t id{2}; id <= 200; ++id) {
        ConnectBlock(*history, id, UtcTime(0).toSecsSinceEpoch() - id);
        QVERIFY(AwaitHistory(model));
    }

    QCOMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QCOMPARE(history_spy.count(), 0);
}

void BlockClockModelTests::blockArrivalAtRolloverIsNotCountedTwice()
{
    auto history{std::make_shared<BlockClockHistory>()};
    QDateTime current_time{UtcTime(11, 59)};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [&] { return current_time; }};
    const qint64 timestamp{UtcTime(12, 1).toSecsSinceEpoch()};
    history->initialize({{uint256{1}, timestamp}});
    QVERIFY(AwaitHistory(model));
    QVERIFY(model.blockTimeFractions().isEmpty());

    current_time = UtcTime(12, 1);
    std::thread first_notification{[history, timestamp] {
        ConnectBlock(*history, 1, timestamp);
        ConnectBlock(*history, 2, timestamp);
    }};
    first_notification.join();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    // The minute update queues Native consumption while GUI notification delivery
    // remains pending. A newer generation must join the same final publication.
    model.updateCurrentTime(current_time);
    QVERIFY(model.historyPending()); // Full snapshot consumption remains held on Native.
    QVERIFY(model.blockTimeFractions().isEmpty());
    std::thread second_notification{[history] {
        ConnectBlock(*history, 3, UtcTime(12, 2).toSecsSinceEpoch());
    }};
    second_notification.join();
    QVERIFY(AwaitHistory(model));

    QCOMPARE(model.periodStart(), UtcTime(12).toSecsSinceEpoch());
    const QList<qreal> expected{1.0 / 720.0, 1.0 / 720.0, 2.0 / 720.0};
    QCOMPARE(model.blockTimeFractions(), expected);
    QCOMPARE(history_spy.count(), 1);
}

void BlockClockModelTests::emptyHistoryDoesNotEmitRedundantChanges()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, false, [] { return UtcTime(15); }};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    history->initialize({});
    QVERIFY(AwaitHistory(model));
    QVERIFY(AwaitHistory(model));
    QCOMPARE(history_spy.count(), 0);

    // Filtering a nonempty snapshot leaves an empty list with allocated storage.
    // It must compare equal to the model's initially unallocated empty history.
    const qint64 future_timestamp{UtcTime(23).toSecsSinceEpoch() + BlockClockModel::PERIOD_SECONDS};
    ConnectBlock(*history, 1, future_timestamp);
    QVERIFY(AwaitHistory(model));
    QVERIFY(model.blockTimeFractions().isEmpty());
    QCOMPARE(history_spy.count(), 0);

    DisconnectBlock(*history, 1, future_timestamp);
    QVERIFY(AwaitHistory(model));
    QCOMPARE(history_spy.count(), 0);

    const qint64 visible_timestamp{UtcTime(13).toSecsSinceEpoch()};
    ConnectBlock(*history, 2, visible_timestamp);
    QVERIFY(AwaitHistory(model));
    QCOMPARE(model.blockTimeFractions(), QList<qreal>{1.0 / 12.0});
    QCOMPARE(history_spy.count(), 1);

    DisconnectBlock(*history, 2, visible_timestamp);
    QVERIFY(AwaitHistory(model));
    QVERIFY(model.blockTimeFractions().isEmpty());
    QCOMPARE(history_spy.count(), 2);

    ConnectBlock(*history, 3, future_timestamp);
    QVERIFY(AwaitHistory(model));
    QVERIFY(model.blockTimeFractions().isEmpty());
    QCOMPARE(history_spy.count(), 2);
}

void BlockClockModelTests::timersHaveModelOwnershipAndCanBeDisabledForTests()
{
    const auto current_time_provider{[] { return UtcTime(15); }};
    BlockClockModel stopped_model{{}, execution::SnapshotServices{m_executor->scheduler(), {}}, false, current_time_provider};
    QCOMPARE(stopped_model.timerActive(), false);
    const auto stopped_timers{stopped_model.findChildren<QTimer*>()};
    QCOMPARE(stopped_timers.size(), 1);
    for (QTimer* timer : stopped_timers) {
        QCOMPARE(timer->parent(), &stopped_model);
        QVERIFY(!timer->isActive());
    }

    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel running_model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, true, current_time_provider};
    QCOMPARE(running_model.timerActive(), true);
    const auto running_timers{running_model.findChildren<QTimer*>()};
    QCOMPARE(running_timers.size(), 1);
    for (QTimer* timer : running_timers) {
        QCOMPARE(timer->parent(), &running_model);
        QVERIFY(timer->isActive());
    }
}

void BlockClockModelTests::timerAlignsToNextMinute()
{
    const QDateTime current_time{UtcTime(15, 0, 45).addMSecs(250)};
    BlockClockModel model{{}, execution::SnapshotServices{m_executor->scheduler(), {}}, true, [current_time] { return QDateTime{current_time}; }};

    bool found_clock_timer{false};
    for (QTimer* timer : model.findChildren<QTimer*>()) {
        if (!timer->isSingleShot()) continue;
        found_clock_timer = true;
        QCOMPARE(timer->timerType(), Qt::PreciseTimer);
        QCOMPARE(timer->interval(), BlockClockModel::CLOCK_UPDATE_INTERVAL_MS - 45250);
    }
    QVERIFY(found_clock_timer);
}

void BlockClockModelTests::minuteTimerAdvancesWithoutBlockNotifications()
{
    auto history{std::make_shared<BlockClockHistory>()};
    history->initialize({{uint256{1}, UtcTime(14).toSecsSinceEpoch()}});
    // Start just before a minute boundary to exercise the real timer without
    // waiting a minute or calling the model's update method directly.
    QDateTime current_time{UtcTime(15, 0, 59).addMSecs(950)};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, true, [&] { return current_time; }};
    QVERIFY(AwaitHistory(model));
    const QList<qreal> expected_history{1.0 / 6.0};
    QCOMPARE(model.blockTimeFractions(), expected_history);
    auto* timer = model.findChild<QTimer*>();
    QVERIFY(timer);
    QSignalSpy timeout_spy{timer, &QTimer::timeout};
    QSignalSpy current_time_spy{&model, &BlockClockModel::currentTimeFractionChanged};
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};

    current_time = UtcTime(15, 1);
    QTRY_COMPARE(timeout_spy.count(), 1);

    QCOMPARE(current_time_spy.count(), 1);
    QVERIFY(qAbs(model.currentTimeFraction() - (181.0 / 720.0)) < 0.000001);
    QCOMPARE(model.blockTimeFractions(), expected_history);
    QCOMPARE(history_spy.count(), 0);
    QVERIFY(timer->isActive());
    QCOMPARE(timer->interval(), BlockClockModel::CLOCK_UPDATE_INTERVAL_MS);
}

void BlockClockModelTests::stoppedModelDoesNotConsumeHistoryOrRestartTimers()
{
    auto history{std::make_shared<BlockClockHistory>()};
    BlockClockModel model{history, execution::SnapshotServices{m_executor->scheduler(), {}}, true, [] { return UtcTime(15); }};
    history->initialize({{uint256{1}, UtcTime(14).toSecsSinceEpoch()}});
    QVERIFY(AwaitHistory(model));
    const auto fractions = model.blockTimeFractions();
    const auto period = model.periodStart();
    QSignalSpy history_spy{&model, &BlockClockModel::blockTimeFractionsChanged};
    std::thread first_notification{[history] {
        ConnectBlock(*history, 2, UtcTime(16).toSecsSinceEpoch());
    }};
    first_notification.join();
    model.stop();
    std::thread late_notifications{[history] {
        for (int repeat{0}; repeat < 10'000; ++repeat) {
            ConnectBlock(*history, 3, UtcTime(17).toSecsSinceEpoch());
            DisconnectBlock(*history, 3, UtcTime(17).toSecsSinceEpoch());
        }
    }};
    late_notifications.join();
    QVERIFY(AwaitHistory(model));
    QCOMPARE(history_spy.count(), 0);
    model.updateCurrentTime(UtcTime(15).addDays(1));
    QCOMPARE(model.blockTimeFractions(), fractions);
    QCOMPARE(model.periodStart(), period);
    for (const auto* timer : model.findChildren<QTimer*>()) QVERIFY(!timer->isActive());
    // Stopping the presentation does not own backend unregistration. The init
    // worker retires the independent subscription before appShutdown instead.
    QVERIFY(history->retentionStart().has_value());
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(BlockClockModelTests)
#else
QTEST_MAIN(BlockClockModelTests)
#endif
#include <test_blockclockmodel.moc>
