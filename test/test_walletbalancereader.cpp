// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/models/walletbalancereader.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest/QtTest>

#include <atomic>
#include <stdexcept>

namespace {
using execution::Lane;
using walletbalance::Result;
using walletbalance::Status;

struct Fixture {
    ManualRoutedExecution env;
    std::shared_ptr<std::atomic_bool> closed{std::make_shared<std::atomic_bool>(false)};
    int reads{0}, wakeups{0};
    Result result{Status::Ready, walletbalance::Snapshot{42, "first observed block"}};
    std::function<void()> during_read, during_notify;
    bool throw_read{false};
    std::shared_ptr<walletbalance::Reader> reader{std::make_shared<walletbalance::Reader>(env.scheduler, [this] {
        ++reads;
        const auto captured = result;
        if (during_read) std::exchange(during_read, {})();
        if (throw_read) throw std::runtime_error("synthetic read failure");
        return captured;
    }, [this] {
        ++wakeups;
        if (during_notify) std::exchange(during_notify, {})();
    }, closed)};
    ~Fixture() { reader->close(); env.drain(); }
    void refresh() { QCOMPARE(reader->refresh(), execution::Rejection::None); }
    walletbalance::Snapshot takeReady()
    {
        auto value = reader->take();
        if (!value || value->status != Status::Ready || !value->snapshot) {
            QTest::qFail("Expected a ready owned balance snapshot", __FILE__, __LINE__);
            return {};
        }
        return std::move(*value->snapshot);
    }
};
} // namespace

class WalletBalanceReaderTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void retirementWaitsForAdmittedPacket_data()
    {
        QTest::addColumn<bool>("finish_effect");
        QTest::newRow("before-effect") << false;
        QTest::newRow("after-effect") << true;
    }
    void retirementWaitsForAdmittedPacket()
    {
        QFETCH(bool, finish_effect);
        ManualRoutedExecution env;
        const auto closed = std::make_shared<std::atomic_bool>(false);
        int reads{0}, retired{0};
        auto reader = std::make_shared<walletbalance::Reader>(env.scheduler, [&] {
            ++reads;
            return Result{Status::Ready, walletbalance::Snapshot{42, "held packet"}};
        }, [] {}, closed, [&] { ++retired; });
        QCOMPARE(reader->refresh(), execution::Rejection::None);
        if (finish_effect) QVERIFY(env.step(Lane::Core));
        reader->close();
        QCOMPARE(retired, 0);
        if (!finish_effect) QVERIFY(env.step(Lane::Core));
        QCOMPARE(reads, finish_effect ? 1 : 0);
        QCOMPARE(retired, 0);
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(retired, 1);
        reader->close();
        QCOMPARE(retired, 1);
        QVERIFY(env.quiescent());
    }
    void idleRetirementMayReenterWithoutHoldingReaderMutex()
    {
        ManualRoutedExecution env;
        int retired{0};
        std::shared_ptr<walletbalance::Reader> reader;
        reader = std::make_shared<walletbalance::Reader>(env.scheduler, [] { return Result{}; }, [] {},
            std::make_shared<std::atomic_bool>(false), [&] {
                ++retired;
                QCOMPARE(reader->refresh(), execution::Rejection::Closed);
                reader->close();
            });
        reader->close();
        QCOMPARE(retired, 1);
        reader->close();
        QCOMPARE(retired, 1);
        QVERIFY(env.quiescent());
    }
    void retirementAfterRejectedAdmissionStillRetires()
    {
        ManualRoutedExecution env;
        env.rejectLane(execution::Lane::Core);
        int retired{0}, reads{0};
        auto reader = std::make_shared<walletbalance::Reader>(env.scheduler, [&] { ++reads; return Result{}; }, [] {},
            std::make_shared<std::atomic_bool>(false), [&] { ++retired; });
        QCOMPARE(reader->refresh(), execution::Rejection::Transport);
        reader->close();
        QCOMPARE(reads, 0);
        QCOMPARE(retired, 1);
        QVERIFY(env.quiescent());
    }
    void retirementInsideReadWaitsForItsOutcomeRelease()
    {
        ManualRoutedExecution env;
        int retired{0}, wakes{0};
        std::weak_ptr<walletbalance::Reader> weak;
        auto reader = std::make_shared<walletbalance::Reader>(env.scheduler, [&] {
            weak.lock()->close();
            if (retired != 0) throw std::runtime_error("Retired before the active read returned");
            return Result{Status::Ready, walletbalance::Snapshot{42, "closed during read"}};
        }, [&] { ++wakes; }, std::make_shared<std::atomic_bool>(false), [&] { ++retired; });
        weak = reader;
        QCOMPARE(reader->refresh(), execution::Rejection::None);
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(retired, 0);
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(retired, 1);
        QCOMPARE(wakes, 0);
        QVERIFY(!reader->take());
        QVERIFY(env.quiescent());
    }
    void admittedPacketRetainsRetirementAfterObserverLoss()
    {
        ManualRoutedExecution env;
        int retired{0};
        auto reader = std::make_shared<walletbalance::Reader>(env.scheduler, [] { return Result{}; }, [] {},
            std::make_shared<std::atomic_bool>(false), [&] { ++retired; });
        const std::weak_ptr<walletbalance::Reader> weak{reader};
        QCOMPARE(reader->refresh(), execution::Rejection::None);
        reader->close();
        reader.reset();
        QCOMPARE(retired, 0);
        QVERIFY(!weak.expired());
        env.drain();
        QCOMPARE(retired, 1);
        QVERIFY(weak.expired());
        QVERIFY(env.quiescent());
    }
    void retirementOfOneReaderLeavesOtherOpeningUsable()
    {
        ManualRoutedExecution env;
        int retired_a{0}, retired_b{0}, reads_b{0};
        auto a = std::make_shared<walletbalance::Reader>(env.scheduler, [] { return Result{}; }, [] {},
            std::make_shared<std::atomic_bool>(false), [&] { ++retired_a; });
        auto b = std::make_shared<walletbalance::Reader>(env.scheduler, [&] {
            ++reads_b;
            return Result{Status::Ready, walletbalance::Snapshot{84, "other opening"}};
        }, [] {}, std::make_shared<std::atomic_bool>(false), [&] { ++retired_b; });
        QCOMPARE(a->refresh(), execution::Rejection::None);
        a->close();
        env.drain();
        QCOMPARE(retired_a, 1);
        QCOMPARE(retired_b, 0);
        QCOMPARE(b->refresh(), execution::Rejection::None);
        env.drain();
        QCOMPARE(reads_b, 1);
        const auto result = b->take();
        QVERIFY(result && result->snapshot);
        QCOMPARE(result->snapshot->trusted, int64_t{84});
        b->close();
        QCOMPARE(retired_b, 1);
        QVERIFY(env.quiescent());
    }
    void independentEffectConsumptionAndPublication()
    {
        Fixture f;
        f.refresh();
        QVERIFY(!f.reader->take());
        QCOMPARE(f.reads, 0);
        QVERIFY(f.env.step(Lane::Core));
        QCOMPARE(f.reads, 1);
        QCOMPARE(f.wakeups, 0);
        QVERIFY(!f.reader->take());
        QVERIFY(f.env.step(Lane::Domain));
        QCOMPARE(f.wakeups, 1);
        const auto value = f.reader->take();
        QVERIFY(value && value->snapshot);
        QCOMPARE(value->status, Status::Ready);
        QCOMPARE(value->snapshot->trusted, int64_t{42});
        QCOMPARE(value->snapshot->observed_block, std::string{"first observed block"});
        QVERIFY(f.env.quiescent());
        QVERIFY(!f.reader->take());
    }
    void noticesBeforeEffectCoalesce()
    {
        Fixture f;
        for (int i = 0; i < 1000; ++i) f.refresh();
        QCOMPARE(f.env.scheduler->pending(), size_t{1});
        f.env.drain();
        QCOMPARE(f.reads, 1);
        QCOMPARE(f.wakeups, 1);
        QCOMPARE(f.takeReady().trusted, int64_t{42});
        QVERIFY(f.env.quiescent());
    }
    void newerNoticePreservesProgress_data()
    {
        QTest::addColumn<bool>("failed");
        QTest::newRow("old-success") << false;
        QTest::newRow("old-error") << true;
    }
    void newerNoticePreservesProgress()
    {
        QFETCH(bool, failed);
        Fixture f;
        f.throw_read = failed;
        f.refresh();
        QVERIFY(f.env.step(Lane::Core));
        f.throw_read = false;
        f.result.snapshot = walletbalance::Snapshot{99, "successor block"};
        for (int i = 0; i < 100; ++i) f.refresh();
        QVERIFY(f.env.step(Lane::Domain));
        QCOMPARE(f.wakeups, 1);
        const auto earlier = f.reader->take();
        QVERIFY(earlier);
        QCOMPARE(earlier->status, failed ? Status::Failed : Status::Ready);
        if (!failed) {
            QVERIFY(earlier->snapshot);
            QCOMPARE(earlier->snapshot->trusted, int64_t{42});
        }
        f.env.drain();
        QCOMPARE(f.reads, 2);
        const auto value = f.reader->take();
        QVERIFY(value && value->snapshot);
        QCOMPARE(value->snapshot->trusted, int64_t{99});
        QCOMPARE(value->snapshot->observed_block, std::string{"successor block"});
        QVERIFY(f.env.quiescent());
    }
    void noticeDuringReadPreservesSuccessorInterest()
    {
        Fixture f;
        f.during_read = [&] {
            f.result.snapshot->trusted = 100;
            f.refresh();
        };
        f.refresh();
        f.env.drain();
        QCOMPARE(f.reads, 2);
        QCOMPARE(f.wakeups, 1);
        QCOMPARE(f.takeReady().trusted, int64_t{100});
    }
    void delayedGuiWakeupUsesLatestValue()
    {
        Fixture f;
        for (int i = 1; i <= 100; ++i) {
            f.result.snapshot->trusted = i;
            f.refresh();
            f.env.drain();
        }
        QCOMPARE(f.wakeups, 1);
        QCOMPARE(f.takeReady().trusted, int64_t{100});
        f.refresh();
        f.env.drain();
        QCOMPARE(f.wakeups, 2);
    }
    void newRefreshRetainsLastObservationUntilSuccessor()
    {
        Fixture f;
        f.refresh();
        f.env.drain();
        f.refresh();
        QCOMPARE(f.takeReady().trusted, int64_t{42});
        f.env.drain();
        QCOMPARE(f.takeReady().trusted, int64_t{42});
        QCOMPARE(f.wakeups, 2);
    }
    void busyUnavailableAndFailureStayDistinct_data()
    {
        QTest::addColumn<int>("status");
        QTest::addColumn<bool>("throws");
        QTest::newRow("busy") << int(Status::Busy) << false;
        QTest::newRow("missing-interface") << int(Status::Unavailable) << false;
        QTest::newRow("exception") << int(Status::Failed) << true;
    }
    void continuousRefreshDoesNotRequireAQuietWindow()
    {
        Fixture f;
        f.refresh();
        for (int i = 1; i <= 10; ++i) {
            f.result.snapshot->trusted = i;
            QVERIFY(f.env.step(Lane::Core));
            // A timer/notice arrives during every read, without a quiet turn.
            f.refresh();
            QVERIFY(f.env.step(Lane::Domain));
            QCOMPARE(f.takeReady().trusted, int64_t{i});
        }
        QCOMPARE(f.reads, 10);
        f.reader->close();
        f.env.drain();
        QVERIFY(f.env.quiescent());
    }
    void delayedGuiReceivesLastGoodValueAlongsideNewFailure()
    {
        Fixture f;
        f.refresh();
        f.env.drain();
        f.throw_read = true;
        f.refresh();
        f.env.drain();
        const auto value = f.reader->take();
        QVERIFY(value && value->snapshot);
        QCOMPARE(value->status, Status::Failed);
        QCOMPARE(value->snapshot->trusted, int64_t{42});
        QCOMPARE(f.wakeups, 1);
    }
    void busyUnavailableAndFailureStayDistinct()
    {
        QFETCH(int, status);
        QFETCH(bool, throws);
        Fixture f;
        f.result = {Status(status), {}};
        f.throw_read = throws;
        f.refresh();
        f.env.drain();
        const auto value = f.reader->take();
        QVERIFY(value);
        QCOMPARE(int(value->status), status);
        QVERIFY(!value->snapshot);
        QCOMPARE(f.reads, 1); // No busy retry loop on a held Core lock.
        QVERIFY(f.env.quiescent());
        f.throw_read = false;
        f.result = {Status::Ready, walletbalance::Snapshot{101, "later"}};
        f.refresh();
        f.env.drain();
        QCOMPARE(f.takeReady().trusted, int64_t{101});
    }
    void closeAtEachBoundary_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::newRow("before-effect") << 0;
        QTest::newRow("before-consumption") << 1;
        QTest::newRow("before-gui-take") << 2;
    }
    void closeAtEachBoundary()
    {
        QFETCH(int, boundary);
        Fixture f;
        f.refresh();
        if (boundary >= 1) QVERIFY(f.env.step(Lane::Core));
        if (boundary >= 2) QVERIFY(f.env.step(Lane::Domain));
        f.reader->close();
        QCOMPARE(f.reader->refresh(), execution::Rejection::Closed);
        f.env.drain();
        QVERIFY(!f.reader->take());
        QCOMPARE(f.reads, boundary == 0 ? 0 : 1);
        QVERIFY(f.env.quiescent());
    }
    void unloadIngressFencesBeforeGuiProcessesIt()
    {
        Fixture f;
        f.refresh();
        f.closed->store(true); // Same flag set by the production unload callback.
        f.env.drain();
        QCOMPARE(f.reads, 0);
        QCOMPARE(f.wakeups, 0);
        QVERIFY(!f.reader->take());
    }
    void rejectionRetainsInterestWithoutSpinning()
    {
        Fixture f;
        f.env.rejectLane(Lane::Core);
        QCOMPARE(f.reader->refresh(), execution::Rejection::Transport);
        const auto rejected = f.reader->take();
        QVERIFY(rejected);
        QCOMPARE(rejected->status, Status::Rejected);
        f.env.drain();
        QCOMPARE(f.reads, 0);
        QVERIFY(f.env.quiescent());
        f.refresh();
        f.env.drain();
        QCOMPARE(f.takeReady().trusted, int64_t{42});
    }
    void reentrantWakeupDoesNotHoldMailboxLock()
    {
        Fixture f;
        f.during_notify = [&] {
            QCOMPARE(f.takeReady().trusted, int64_t{42});
            f.result.snapshot->trusted = 77;
            f.refresh();
        };
        f.refresh();
        f.env.drain();
        QCOMPARE(f.takeReady().trusted, int64_t{77});
        QCOMPARE(f.reads, 2);
    }
    void failedPublicationTransportAllowsLaterRetry()
    {
        Fixture f;
        f.during_notify = [] { throw std::runtime_error("synthetic GUI transport failure"); };
        f.refresh();
        f.env.drain();
        QCOMPARE(f.wakeups, 1);
        f.result.snapshot->trusted = 78;
        f.refresh();
        f.env.drain();
        QCOMPARE(f.wakeups, 2);
        QCOMPARE(f.takeReady().trusted, int64_t{78});
        QVERIFY(f.env.quiescent());
    }
    void transportFailureAfterAcceptedQueueingRetainsRetry()
    {
        Fixture f;
        QVERIFY(execution::Submit(*f.env.scheduler, [] {}, [] {}));
        f.refresh(); // Core is occupied; the reader is accepted but not sent yet.
        f.env.rejectLane(Lane::Core);
        f.env.drain();
        const auto value = f.reader->take();
        QVERIFY(value);
        QCOMPARE(value->status, Status::Rejected);
        QCOMPARE(f.reads, 0);
        QVERIFY(f.env.quiescent());
        f.refresh();
        f.env.drain();
        QCOMPARE(f.takeReady().trusted, int64_t{42});
    }
    void lostObserverStillConsumesTheOutcome()
    {
        Fixture f;
        f.refresh();
        f.reader.reset();
        // Accepted work owns the reader until Domain consumes it.
        f.env.drain();
        QCOMPARE(f.reads, 1);
        QVERIFY(f.env.quiescent());
        // Restore only for Fixture teardown; this reader is a new opening.
        f.reader = std::make_shared<walletbalance::Reader>(f.env.scheduler, [] { return Result{}; }, [] {}, f.closed);
    }
    void realWorkersConsumeOffGuiAndPreservePendingRefresh()
    {
        struct State {
            QSemaphore entered, release;
            std::atomic<int> reads{0}, notices{0};
            std::atomic<QThread*> effect_thread{nullptr}, domain_thread{nullptr};
        };
        const auto state = std::make_shared<State>();
        const auto release_on_exit = qScopeGuard([&] { state->release.release(); });
        BackendExecutor executor{execution::Options{}};
        const auto closed = std::make_shared<std::atomic_bool>(false);
        auto reader = std::make_shared<walletbalance::Reader>(executor.scheduler(), [state] {
            const int read = ++state->reads;
            state->effect_thread = QThread::currentThread();
            if (read == 1) {
                state->entered.release();
                state->release.acquire();
            }
            return Result{Status::Ready, walletbalance::Snapshot{read, "worker observation"}};
        }, [state] {
            state->domain_thread = QThread::currentThread();
            ++state->notices;
        }, closed);
        QCOMPARE(reader->refresh(), execution::Rejection::None);
        QTRY_VERIFY(state->entered.available() > 0);
        for (int i = 0; i < 1000; ++i) QCOMPARE(reader->refresh(), execution::Rejection::None);
        QCOMPARE(state->reads.load(), 1);
        QCOMPARE(state->notices.load(), 0);
        bool heartbeat{false};
        QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat);
        state->release.release();
        QTRY_COMPARE(executor.scheduler()->pending(), size_t{0});
        QCOMPARE(state->notices.load(), 1);
        const auto value = reader->take();
        QVERIFY(value && value->snapshot);
        QCOMPARE(value->snapshot->trusted, int64_t{2});
        QCOMPARE(state->reads.load(), 2);
        QVERIFY(state->effect_thread.load() != qApp->thread());
        QVERIFY(state->domain_thread.load() != qApp->thread());
        QVERIFY(state->effect_thread.load() != state->domain_thread.load());
        reader->close();
        executor.shutdown();
        QTRY_VERIFY(executor.isDrained());
    }

};

BITCOINQML_REGISTER_QT_TEST(WalletBalanceReaderTests)
#include <test/test_walletbalancereader.moc>
