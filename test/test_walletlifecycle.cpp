// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletlifecycle.h>
#include <qml/walletopening.h>
#include <qml/walletpublication.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/mocks/mockwallet.h>
#include <test/mocks/walletsession.h>
#include <test/qt_test_registry.h>

#include <util/string.h>

#include <QtTest/QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <stdexcept>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <deque>
#include <mutex>
#include <thread>

namespace {
using execution::Lane;
using walletlife::Publication;

struct Probe {
    std::optional<Lane> lane;
    std::vector<std::pair<std::string, std::optional<Lane>>> names, destroyed;
    std::vector<std::optional<Lane>> registrations, captures, disconnects, unload_subscriptions, unload_disconnects;
    std::function<void()> during_name;
    std::vector<size_t> destroyed_before_disconnect;
};

class TrackedWallet final : public StubWallet
{
public:
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn) override {
        m_probe->unload_subscriptions.push_back(m_probe->lane);
        return interfaces::MakeCleanupHandler([probe = m_probe] { probe->unload_disconnects.push_back(probe->lane); });
    }
    TrackedWallet(std::shared_ptr<Probe> probe, std::string name) : m_probe(std::move(probe)), m_name(std::move(name)) {}
    ~TrackedWallet() override { m_probe->destroyed.emplace_back(m_name, m_probe->lane); }
    std::string getWalletName() override { m_probe->names.emplace_back(m_name, m_probe->lane); if (m_probe->during_name) std::exchange(m_probe->during_name, {})(); return m_name; }
private:
    const std::shared_ptr<Probe> m_probe;
    const std::string m_name;
};

struct Fixture {
    explicit Fixture(std::function<uint64_t()> ingress_stamp = {})
    {
        const auto capture = [this] {
            probe->captures.push_back(probe->lane);
            if (capture_failure) throw std::runtime_error("synthetic capture failure");
            if (during_capture) during_capture();
            std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
            if (invalid_snapshot) { wallets.emplace_back(); return wallets; }
            wallets.reserve(catalog.size());
            for (const auto& name : catalog) wallets.push_back(wallet(name));
            return wallets;
        };
        owner = std::make_shared<walletlife::LoadSubscription>(&executor,
            [this](walletlife::LoadSubscription::Callback fn) {
                probe->registrations.push_back(probe->lane);
                callback = std::move(fn);
                if (during_registration) during_registration();
                if (registration_missing) return std::unique_ptr<interfaces::Handler>{};
                return interfaces::MakeCleanupHandler([this] {
                    probe->disconnects.push_back(probe->lane);
                    probe->destroyed_before_disconnect.push_back(probe->destroyed.size());
                });
            }, capture, [this](walletlife::Update update) { publications.push_back(update); }, std::move(ingress_stamp));
    }
    ~Fixture()
    {
        if (owner) owner->retire();
        drain();
        executor.stopAndJoin();
    }
    std::unique_ptr<interfaces::Wallet> wallet(const std::string& name) { return std::make_unique<TrackedWallet>(probe, name); }

    struct Turn {
        Lane lane;
        uint64_t id;
        std::function<void()> work;
    };

    bool step(Lane lane)
    {
        std::function<void()> work;
        {
            std::lock_guard lock{mutex};
            auto& pending = turns(lane);
            if (pending.empty()) return false;
            auto turn = std::move(pending.front());
            pending.pop_front();
            trace.push_back("run:" + util::ToString(turn.id));
            work = std::move(turn.work);
        }
        const auto previous = std::exchange(probe->lane, lane);
        const auto restore = qScopeGuard([&] { probe->lane = previous; });
        auto& before_work = lane == Lane::Core ? before_core : before_utility;
        if (before_work) std::exchange(before_work, {})();
        work();
        return true;
    }
    void drain() { while (step(Lane::Core) || step(Lane::Utility)) {} }
    bool quiescent() { return pendingCount() == 0; }
    size_t pendingCount()
    {
        std::lock_guard lock{mutex};
        return core.size() + utility.size();
    }
    std::vector<QJsonObject> pendingHeads()
    {
        std::lock_guard lock{mutex};
        std::vector<QJsonObject> heads;
        for (const auto* turns : {&core, &utility}) {
            if (turns->empty()) continue;
            const auto& turn = turns->front();
            const int phase = turn.lane == Lane::Core ? 0 : 1;
            heads.push_back({{"kind", "turn"}, {"lane", static_cast<int>(turn.lane)},
                {"phase", phase}, {"task", QString::number(static_cast<qulonglong>(turn.id))}});
        }
        return heads;
    }
    void notify(const std::string& name)
    {
        const auto previous = std::exchange(probe->lane, Lane::Core);
        callback(wallet(name));
        probe->lane = previous;
    }
    int count(Publication kind) const
    {
        return std::count_if(publications.begin(), publications.end(), [kind](auto update) { return update.kind == kind; });
    }
    std::mutex mutex;
    std::deque<Turn> core, utility;
    uint64_t next_turn{0};
    std::vector<std::string> trace;
    bool reject_core{false};
    std::function<void()> before_core, before_utility, after_core_dispatch;

    std::deque<Turn>& turns(Lane lane) { return lane == Lane::Core ? core : utility; }

    execution::Executor executor{[this](Lane lane, execution::UtilityWorker, std::function<void()> work) {
        bool accept;
        {
            std::lock_guard lock{mutex};
            const auto id = ++next_turn;
            accept = lane != Lane::Core || !std::exchange(reject_core, false);
            trace.push_back(std::string{accept ? "post:" : "reject:"} + util::ToString(id));
            if (accept) turns(lane).push_back({lane, id, std::move(work)});
        }
        if (lane == Lane::Core && after_core_dispatch) std::exchange(after_core_dispatch, {})();
        return accept;
    }};
    std::shared_ptr<Probe> probe{std::make_shared<Probe>()};
    std::shared_ptr<walletlife::LoadSubscription> owner;
    walletlife::LoadSubscription::Callback callback;
    std::function<void()> during_registration, during_capture;
    std::vector<std::string> catalog;
    std::vector<walletlife::Update> publications;
    bool capture_failure{false}, registration_missing{false}, invalid_snapshot{false};
};

// The generator chooses only a posted worker dispatch. Each SerialQueue
// controls its own work. Replay checks the original dispatch and its lane.
struct ScheduleRun {
    QJsonArray script;
    QString error;
    int ready{0}, failed{0}, retired{0}, registrations{0}, disconnects{0};
    std::vector<int> effects, outcomes;
    bool quiescent{false}, backend_off_gui{true};
    std::vector<std::string> transport;
};

ScheduleRun RunLifecycleSchedule(uint32_t seed, bool inline_callback, bool capture_failure,
                                 const std::optional<QJsonArray>& replay = {})
{
    Fixture f;
    ScheduleRun result;
    f.catalog = {"DO_NOT_TRACE_schedule_wallet"};
    f.capture_failure = capture_failure;
    if (inline_callback) f.during_registration = [&] { f.notify("DO_NOT_TRACE_inline_callback"); };
    if (!f.owner->start()) { result.error = QStringLiteral("initial submitted refused"); return result; }
    auto effects = f.executor.makeQueue(Lane::Core);
    auto outcomes = f.executor.makeQueue(Lane::Utility);
    const auto finish = qScopeGuard([&] { f.owner->retire(); f.drain(); });
    for (int value : {1, 2}) {
        if (!effects.post([&, value] {
            result.effects.push_back(value);
            QVERIFY(outcomes.post([&, value] { result.outcomes.push_back(value); }));
        })) {
            result.error = QStringLiteral("independent Core submission refused"); return result;
        }
    }
    auto utility = f.executor.makeQueue(Lane::Utility);
    if (!utility.post([] {})) {
        result.error = QStringLiteral("independent Utility submission refused"); return result;
    }
    std::unique_ptr<walletlife::LoadSubscription::Handoff> handoff;
    bool closed{false}, claimed{false}, notified{false};
    // This guard releases a retained handoff BEFORE Fixture's destructor drains.
    const auto release = qScopeGuard([&] { handoff.reset(); });
    const auto candidates = [&] {
        auto choices = f.pendingHeads();
        if (!closed) {
            choices.push_back({{"kind", "close"}});
            if (f.callback && !notified) choices.push_back({{"kind", "notify"}});
            if (!claimed && f.count(Publication::Ready)) choices.push_back({{"kind", "claim"}});
        }
        if (handoff) choices.push_back({{"kind", "release"}});
        return choices;
    };
    constexpr size_t MAX_STEPS{96};
    for (size_t step{0}; step < MAX_STEPS; ++step) {
        if (closed && !handoff && f.quiescent()) break;
        auto choices = candidates();
        if (choices.empty()) { result.error = QStringLiteral("nonquiescent state has no legal turn"); break; }
        QJsonObject action;
        if (replay) {
            if (step >= static_cast<size_t>(replay->size()) || !replay->at(static_cast<qsizetype>(step)).isObject()) {
                result.error = QStringLiteral("record ends before physical quiescence"); break;
            }
            action = replay->at(static_cast<qsizetype>(step)).toObject();
            if (std::find(choices.begin(), choices.end(), action) == choices.end()) {
                result.error = QStringLiteral("recorded action is not currently legal"); break;
            }
        } else {
            // Stable arithmetic is specified here, independent of library RNGs.
            seed = static_cast<uint32_t>((uint64_t{seed} * 1664525 + 1013904223) & 0xffffffffULL);
            action = choices[(seed >> 8) % choices.size()];
            // Bound external lifetime without inventing a backend deadline.
            if (step == 32 && !closed) action = {{"kind", "close"}};
        }
        result.script.append(action);
        const auto kind = action.value("kind").toString();
        if (kind == "turn") {
            if (!f.step(static_cast<Lane>(action.value("lane").toInt()))) {
                result.error = QStringLiteral("selected legal turn did not execute"); break;
            }
        } else if (kind == "close") {
            closed = true;
            f.owner->retire();
        } else if (kind == "notify") {
            notified = true;
            f.notify("DO_NOT_TRACE_queued_callback");
        } else if (kind == "claim") {
            claimed = true;
            handoff = f.owner->takeInitial();
            if (!handoff) { result.error = QStringLiteral("ready original handoff was unavailable"); break; }
        } else if (kind == "release") {
            handoff.reset();
        }
        if (closed && handoff && !f.probe->disconnects.empty()) {
            result.error = QStringLiteral("parent disconnected before original presentation lease retired"); break;
        }
    }
    if (result.error.isEmpty() && replay && result.script.size() != replay->size())
        result.error = QStringLiteral("record contains actions after quiescence");
    result.quiescent = closed && !handoff && f.quiescent();
    if (result.error.isEmpty() && !result.quiescent) result.error = QStringLiteral("bounded schedule did not quiesce");
    result.ready = f.count(Publication::Ready); result.failed = f.count(Publication::Failed);
    result.retired = f.count(Publication::Retired);
    result.registrations = static_cast<int>(f.probe->registrations.size());
    result.disconnects = static_cast<int>(f.probe->disconnects.size());
    for (const auto& entry : f.probe->destroyed)
        result.backend_off_gui &= entry.second == Lane::Core || entry.second == Lane::Utility;
    for (const auto& lane : f.probe->disconnects) result.backend_off_gui &= lane == Lane::Utility;
    result.transport = f.trace;
    return result;
}

struct PhysicalProbe {
    QThread* gui{QThread::currentThread()};
    QSemaphore entered, release, ready, retired, initial_disposed, changed;
    std::atomic_bool registration_off_gui{false}, metadata_off_gui{true}, destruction_off_gui{true}, notification_off_gui{true};
    std::atomic_int captures{0}, disconnects{0}, destroyed{0}, gui_publications{0};
    std::atomic_bool disconnect_off_gui{false}, drained{false}, heartbeat{false};
    walletlife::LoadSubscription::Callback callback;
};

class PhysicalWallet final : public StubWallet
{
public:
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn) override { return interfaces::MakeCleanupHandler([] {}); }
    PhysicalWallet(std::shared_ptr<PhysicalProbe> probe, std::string name) : m_probe(std::move(probe)), m_name(std::move(name)) {}
    ~PhysicalWallet() override
    {
        if (QThread::currentThread() == m_probe->gui) m_probe->destruction_off_gui = false;
        ++m_probe->destroyed;
    }
    std::string getWalletName() override
    {
        if (QThread::currentThread() == m_probe->gui) m_probe->metadata_off_gui = false;
        return m_name;
    }
private:
    const std::shared_ptr<PhysicalProbe> m_probe;
    const std::string m_name;
};
} // namespace

class WalletLifecycleTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void generatedLegalSchedulesReplayOriginalLifetimeOracles_data()
    {
        QTest::addColumn<uint32_t>("seed"); QTest::addColumn<bool>("inline_callback");
        for (uint32_t seed : {1U, 2U, 3U, 5U, 8U, 13U, 21U, 34U}) {
            for (bool callback : {false, true}) {
                const QByteArray tag = QByteArray::number(seed) + (callback ? "-registration-callback" : "-snapshot");
                QTest::newRow(tag.constData()) << seed << callback;
            }
        }
    }
    void generatedLegalSchedulesReplayOriginalLifetimeOracles()
    {
        QFETCH(uint32_t, seed); QFETCH(bool, inline_callback);
        const auto first = RunLifecycleSchedule(seed, inline_callback, false);
        qInfo().noquote() << QJsonDocument{QJsonObject{{"seed", static_cast<qint64>(seed)},
            {"inline_callback", inline_callback}, {"script", first.script}}}.toJson(QJsonDocument::Compact);
        QVERIFY2(first.error.isEmpty(), qPrintable(first.error)); QVERIFY(first.quiescent);
        QCOMPARE(first.retired, 1); QCOMPARE(first.disconnects, first.registrations);
        QVERIFY(first.backend_off_gui); QCOMPARE(first.failed, 0); QVERIFY(first.ready <= 1);
        QCOMPARE(first.effects, std::vector<int>({1, 2})); QCOMPARE(first.outcomes, std::vector<int>({1, 2}));
        for (const auto& event : first.transport) QVERIFY(event.find("DO_NOT_TRACE") == std::string::npos);
        // Replay uses the saved actions, not the seed, and checks every task/phase.
        const auto recorded = QJsonDocument::fromJson(QJsonDocument{first.script}.toJson(QJsonDocument::Compact)).array();
        QCOMPARE(recorded, first.script);
        const auto again = RunLifecycleSchedule(0, inline_callback, false, recorded);
        QVERIFY2(again.error.isEmpty(), qPrintable(again.error)); QCOMPARE(again.script, first.script);
        QCOMPARE(again.transport, first.transport); QCOMPARE(again.ready, first.ready);
        QCOMPARE(again.retired, 1); QCOMPARE(again.disconnects, first.disconnects);
        QCOMPARE(again.effects, first.effects); QCOMPARE(again.outcomes, first.outcomes);
        QVERIFY(again.backend_off_gui); QVERIFY(again.quiescent);
    }
    void recordedProducerFailureReplaysTheSameIndependentOracle()
    {
        // This is the existing real capture-port exception, not a fake ledger or
        // an injected post-state. Search bounded legal schedules that reach
        // capture before retirement, recording production-queue choices.
        std::optional<ScheduleRun> failure;
        for (uint32_t seed{1}; seed <= 64; ++seed) {
            auto candidate = RunLifecycleSchedule(seed, false, true);
            if (candidate.error.isEmpty() && candidate.failed == 1) { failure = std::move(candidate); break; }
        }
        QVERIFY(failure); QVERIFY(failure->quiescent); QCOMPARE(failure->ready, 0);
        QCOMPARE(failure->retired, 1); QCOMPARE(failure->disconnects, failure->registrations);
        QVERIFY(failure->backend_off_gui);
        qInfo().noquote() << QJsonDocument{QJsonObject{{"producer", "capture-exception"},
            {"failed_oracle", "ready-count-equals-one"}, {"script", failure->script}}}.toJson(QJsonDocument::Compact);
        const auto recorded = QJsonDocument::fromJson(QJsonDocument{failure->script}.toJson(QJsonDocument::Compact)).array();
        QCOMPARE(recorded, failure->script);
        const auto replay = RunLifecycleSchedule(0, false, true, recorded);
        QVERIFY2(replay.error.isEmpty(), qPrintable(replay.error)); QCOMPARE(replay.transport, failure->transport);
        // Independent original captureFailureOwnsOneTerminalFailureAndDisconnect
        // oracle: no Ready, one Failed, one Retired, all handlers released.
        QCOMPARE(replay.ready, 0); QCOMPARE(replay.failed, 1); QCOMPARE(replay.retired, 1);
        QCOMPARE(replay.disconnects, replay.registrations); QVERIFY(replay.backend_off_gui); QVERIFY(replay.quiescent);
        // Negative control: removing the actual producer fault cannot reproduce
        // this failed readiness oracle with the identical recorded schedule.
        const auto healthy = RunLifecycleSchedule(0, false, false, failure->script);
        QVERIFY(!healthy.error.isEmpty() || healthy.failed != replay.failed || healthy.ready != replay.ready);
    }
    void replayRejectsUnrecordedOrIllegalTurns_data()
    {
        QTest::addColumn<QString>("mutation");
        for (const auto& mutation : {"unknown-task", "wrong-phase", "later-key-turn", "truncated", "extra-turn"})
            QTest::newRow(mutation) << QString::fromLatin1(mutation);
    }
    void replayRejectsUnrecordedOrIllegalTurns()
    {
        QFETCH(QString, mutation);
        const auto original = RunLifecycleSchedule(8, true, false);
        QVERIFY2(original.error.isEmpty(), qPrintable(original.error)); QVERIFY(!original.script.isEmpty());
        auto altered = original.script;
        if (mutation == "truncated") altered.removeAt(altered.size() - 1);
        else if (mutation == "extra-turn") altered.append(QJsonObject{{"kind", "close"}});
        else if (mutation == "later-key-turn") {
            auto first = std::find_if(altered.begin(), altered.end(), [](const auto& value) {
                const auto turn = value.toObject();
                return turn.value("kind") == "turn" && turn.value("lane").toInt() == static_cast<int>(Lane::Core);
            });
            QVERIFY(first != altered.end());
            auto later = std::find_if(std::next(first), altered.end(), [&](const auto& value) {
                const auto turn = value.toObject();
                return turn.value("kind") == "turn" && turn.value("lane").toInt() == static_cast<int>(Lane::Core)
                    && turn.value("task") != first->toObject().value("task");
            });
            QVERIFY(later != altered.end());
            *first = *later;
        } else {
            auto turn = std::find_if(altered.begin(), altered.end(), [](const auto& value) { return value.toObject().value("kind") == "turn"; });
            QVERIFY(turn != altered.end()); auto action = turn->toObject();
            if (mutation == "unknown-task") action.insert("task", QStringLiteral("18446744073709551615"));
            else action.insert("phase", -1);
            *turn = action;
        }
        const auto replay = RunLifecycleSchedule(0, true, false, altered);
        QVERIFY2(!replay.error.isEmpty(), "a changed or incomplete trace must never silently generate a replacement schedule");
    }
    void delayedOlderNativeCallbackCannotReplaceANewerHandle()
    {
        QSemaphore first_entered, release_first;
        std::atomic_uint64_t stamp{0};
        Fixture f{[&] {
            const auto current = ++stamp;
            if (current == 1) { first_entered.release(); release_first.acquire(); }
            return current;
        }};
        QVERIFY(f.owner->start()); f.drain();
        auto older = f.wallet("same");
        std::thread first{[&] { f.callback(std::move(older)); }};
        const auto join = qScopeGuard([&] { release_first.release(); if (first.joinable()) first.join(); });
        QVERIFY(first_entered.tryAcquire(1, 10'000));
        f.notify("same"); f.drain();
        QCOMPARE(f.probe->destroyed.size(), size_t{0});
        release_first.release(); first.join();
        f.drain();
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        QCOMPARE(handoff->entries.front().ingress_sequence, uint64_t{2});
        QCOMPARE(handoff->entries.front().name, std::string{"same"});
        handoff.reset(); f.drain(); f.owner->retire(); f.drain();
        QCOMPARE(f.probe->destroyed.size(), size_t{2});
        QCOMPARE(f.count(Publication::Retired), 1);
    }
    void unavailableIngressIdentityAndRejectedContextFailClosed_data()
    {
        QTest::addColumn<int>("failure");
        QTest::newRow("stamp-capacity") << 0;
        QTest::newRow("stamp-exception") << 1;
        QTest::newRow("context-adapter-rejected") << 2;
    }
    void unavailableIngressIdentityAndRejectedContextFailClosed()
    {
        QFETCH(int, failure);
        Fixture f{[failure]() -> uint64_t {
            if (failure == 1) throw std::runtime_error{"Synthetic native clock failure"};
            return 0;
        }};
        QVERIFY(f.owner->start()); f.drain();
        if (failure == 2) f.owner->rejectNotificationContext();
        else f.notify("must-not-be-adopted");
        // The producer has failed, while its queued Control outcome has not
        // yet retired it. A new command must not capture a fresh origin.
        QVERIFY(!f.owner->setNotificationContext("before-failure-delivery", true, 2));
        f.drain();
        QVERIFY(!f.owner->takeNotifications());
        QCOMPARE(f.count(Publication::Failed), 1); QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.publications.back().reason, failure == 1 ? walletlife::FailureReason::Backend : walletlife::FailureReason::IngressCapacity);
        QVERIFY(!f.owner->setNotificationContext("late-origin", true, 3));
        QVERIFY(f.quiescent());
    }
    void contextRetentionRacingRetirementPublishesOnlyRetirement_data()
    {
        QTest::addColumn<bool>("failure");
        QTest::newRow("retire-during-retention") << false;
        QTest::newRow("fail-during-retention") << true;
    }
    void contextRetentionRacingRetirementPublishesOnlyRetirement()
    {
        QFETCH(bool, failure);
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        QVERIFY(f.owner->setNotificationContext("previous-command", true, 8));
        // Replacing the previous context queues its destruction on Utility.
        // Retire at that handoff and verify a later capture cannot reopen it.
        f.before_utility = [&] {
            if (failure) f.owner->rejectNotificationContext();
            else f.owner->retire();
        };
        QVERIFY(f.owner->setNotificationContext("original-command", true, 9));
        QVERIFY(f.step(Lane::Utility));
        QVERIFY(!f.owner->setNotificationContext("late-command", true, 10));
        f.drain();
        QCOMPARE(f.count(Publication::Retired), 1); QCOMPARE(f.count(Publication::Changed), 0);
        QVERIFY(f.quiescent());
    }
    void throwingContextReservationReportsFailureWithoutRetainingTheOrigin()
    {
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        const auto origin = std::make_shared<int>(4);
        const auto failure = [&] {
            const auto capture_origin = [origin]() -> std::shared_ptr<void> {
                if (*origin == 4) throw std::runtime_error{"Synthetic notification context capture failure"};
                return origin;
            };
            try { f.owner->setNotificationContext("original-command", true, 4, capture_origin()); }
            catch (const std::runtime_error&) { f.owner->rejectNotificationContext(); }
        };
        failure();
        QVERIFY(!f.owner->setNotificationContext("original-command", true, 4, origin));
        f.drain();
        QCOMPARE(origin.use_count(), 1);
        QCOMPARE(f.count(Publication::Failed), 1); QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.quiescent());
    }
    void lateRefreshContinuationCannotReopenStoppingOrFailedOwner_data()
    {
        QTest::addColumn<bool>("failure");
        QTest::newRow("close-before-continuation") << false;
        QTest::newRow("failure-before-continuation") << true;
    }
    void lateRefreshContinuationCannotReopenStoppingOrFailedOwner()
    {
        QFETCH(bool, failure);
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        if (failure) f.callback({}); else f.owner->retire();
        const auto before = f.pendingCount();
        // Resume a continuation that had passed its producer-side check before
        // close/failure. The real admission entry must reject it without work.
        f.owner->requestScan();
        QCOMPARE(f.pendingCount(), before);
        f.drain();
        QCOMPARE(f.count(Publication::Ready), 1); QCOMPARE(f.count(Publication::Changed), 0);
        QCOMPARE(f.count(Publication::Failed), failure ? 1 : 0);
        QCOMPARE(f.count(Publication::Retired), 1); QCOMPARE(f.probe->captures.size(), size_t{1});
        QVERIFY(f.quiescent());
    }
    void retirementDuringRefreshAdmissionPreservesOwnedOutcomes_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("accepted-before-close") << true;
        QTest::newRow("rejected-during-close") << false;
    }
    void retirementDuringRefreshAdmissionPreservesOwnedOutcomes()
    {
        QFETCH(bool, accept);
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        if (!accept) f.reject_core = true;
        std::thread stopper;
        const auto join = qScopeGuard([&] { if (stopper.joinable()) stopper.join(); });
        f.after_core_dispatch = [&] {
            stopper = std::thread{[&] { f.owner->retire(); }};
            // Core dispatch is held while the retirement fence closes.
            QVERIFY(QTest::qWaitFor([&] { return f.owner->m_stopping.load(); }, 5000));
        };
        f.notify("closing"); stopper.join(); f.drain();
        QCOMPARE(f.count(Publication::Changed), 0); QCOMPARE(f.count(Publication::Failed), 0);
        QCOMPARE(f.count(Publication::Retired), 1); QCOMPARE(f.probe->captures.size(), size_t{1});
        QVERIFY(f.probe->names.empty()); QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Utility});
        QVERIFY(f.quiescent());
    }
    void retirementBetweenCleanupReservationAndRegistration()
    {
        Fixture f;
        f.after_core_dispatch = [&] { f.owner->retire(); };
        QVERIFY(f.owner->start());
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Closed);
        f.drain();
        QVERIFY(f.probe->registrations.empty()); QVERIFY(f.probe->captures.empty());
        QCOMPARE(f.count(Publication::Ready), 0); QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.quiescent());
    }
    void handoffAdmissionRacesRetainEveryHandle_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::newRow("close-after-parent-lease") << 0;
        QTest::newRow("close-after-disposal-reservation") << 1;
        QTest::newRow("close-after-disposal-lease") << 2;
    }
    void handoffAdmissionRacesRetainEveryHandle()
    {
        QFETCH(int, boundary);
        Fixture f; f.catalog = {"never-exposed"};
        QVERIFY(f.owner->start()); f.drain();
        std::unique_ptr<walletlife::LoadSubscription::Handoff> handoff;
        if (boundary == 0) {
            f.owner->retire();
        } else {
            handoff = f.owner->takeInitial();
            QVERIFY(handoff && handoff->entries.size() == 1);
            if (boundary == 1) f.owner->retire();
            else {
                f.before_utility = [&] { f.owner->retire(); };
                handoff.reset();
            }
        }
        QVERIFY(!f.owner->takeInitial());
        QVERIFY(f.probe->destroyed.empty());
        handoff.reset();
        f.drain();
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.count(Publication::Failed), 0);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Utility});
        QVERIFY(f.quiescent());
    }
    void callbackRetirementAfterItsLeaseDisposesBeforeCleanup()
    {
        std::function<void()> during_ingress;
        Fixture f{[&] {
            if (during_ingress) std::exchange(during_ingress, {})();
            return 1;
        }};
        QVERIFY(f.owner->start()); f.drain();
        during_ingress = [&] { f.owner->retire(); };
        f.notify("retired-in-callback");
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        f.drain(); QCOMPARE(f.count(Publication::Changed), 0);
        QCOMPARE(f.count(Publication::Retired), 1); QVERIFY(f.quiescent());
    }
    void repeatedFailureBeforeControlConsumptionCoalesces()
    {
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        f.callback({}); f.callback({}); f.notify("after-failure-fence");
        QVERIFY(!f.owner->takeInitial());
        f.drain();
        QCOMPARE(f.count(Publication::Failed), 1); QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->captures.size(), size_t{1}); QVERIFY(f.quiescent());
    }
    void initialClaimWaitsForRegistrationOutcome()
    {
        Fixture f; f.catalog = {"initial"};
        QVERIFY(f.owner->start()); QVERIFY(f.step(Lane::Core));
        QVERIFY(!f.owner->takeInitial());
        f.drain(); auto handoff = f.owner->takeInitial();
        QVERIFY(handoff); QCOMPARE(handoff->entries.front().name, std::string{"initial"});
        handoff.reset(); f.owner->retire(); f.drain(); QVERIFY(f.quiescent());
    }
    void notificationDuringMetadataOwnsANewScanAfterTheCurrentOutcome()
    {
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        f.probe->during_name = [&] { f.notify("second"); };
        f.notify("first"); f.drain();
        auto handoff = f.owner->takeNotifications(); QVERIFY(handoff);
        QCOMPARE(handoff->entries.size(), size_t{2});
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.probe->names.size(), size_t{2});
        QCOMPARE(f.count(Publication::Changed), 1);
        handoff.reset(); f.owner->retire(); f.drain(); QVERIFY(f.quiescent());
    }
    void retirementAfterDispatchBeforeScanStartsCannotPublish()
    {
        Fixture f; QVERIFY(f.owner->start()); f.drain();
        f.before_core = [&] { f.owner->retire(); };
        f.notify("retired-before-scan-body"); f.drain();
        QVERIFY(f.probe->names.empty());
        QCOMPARE(f.count(Publication::Changed), 0); QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Utility});
        QVERIFY(f.quiescent());
    }
    void missingPortsAreRejected_data()
    {
        QTest::addColumn<int>("missing");
        for (int i{0}; i < 4; ++i) QTest::newRow(QByteArray::number(i).constData()) << i;
    }
    void missingPortsAreRejected()
    {
        QFETCH(int, missing);
        ManualStartupExecution env;
        auto* executor = &env.executor();
        walletlife::LoadSubscription::Register registration = [](auto) { return interfaces::MakeCleanupHandler([] {}); };
        walletlife::LoadSubscription::Capture capture = [] { return std::vector<std::unique_ptr<interfaces::Wallet>>{}; };
        walletlife::LoadSubscription::Notify notify = [](auto) {};
        if (missing == 0) executor = nullptr;
        if (missing == 1) registration = {};
        if (missing == 2) capture = {};
        if (missing == 3) notify = {};
        bool rejected{false};
        try { walletlife::LoadSubscription owner{executor, registration, capture, notify}; }
        catch (const std::invalid_argument&) { rejected = true; }
        QVERIFY(rejected);
        QVERIFY(env.quiescent());
    }
    void repeatedStartAndLateCallbackCannotResurrectSubscription()
    {
        Fixture f;
        QVERIFY(f.owner->start());
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Invalid);
        f.drain();
        f.owner->retire(); f.drain();
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Closed);
        f.owner.reset();
        f.notify("after-owner-loss");
        QCOMPARE(f.count(Publication::Ready), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        QVERIFY(f.quiescent());
    }
    void invalidSnapshotFailsAndDisconnectsProducer()
    {
        Fixture f; f.invalid_snapshot = true;
        QVERIFY(f.owner->start()); f.drain();
        QCOMPARE(f.count(Publication::Ready), 0);
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->disconnects.size(), size_t{1});
        QVERIFY(f.quiescent());
    }
    void nullNotificationOwnsFailureWithoutRecapture()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.callback({}); f.drain();
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.publications.back().reason, walletlife::FailureReason::Backend);
        QVERIFY(f.quiescent());
    }
    void rejectedRegistrationOwnsFailure()
    {
        Fixture f; f.reject_core = true;
        QCOMPARE(f.owner->start().rejection, execution::Rejection::Transport);
        f.drain();
        QVERIFY(f.probe->registrations.empty()); QVERIFY(f.probe->captures.empty());
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.quiescent());
    }
    void rejectedRefreshHasAnOwnedTerminalOutcome()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.reject_core = true;
        f.notify("rejected-refresh"); f.drain();
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Utility});
        QVERIFY(f.quiescent());
    }
    void schedulerClosureFencesProducerBeforeCleanupExecutes()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.owner->retire();
        f.notify("after-fence"); f.drain();
        QCOMPARE(f.count(Publication::Changed), 0);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        QVERIFY(f.quiescent());
    }
    void repeatedNotificationsKeepOnePendingPublication()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        for (int i{0}; i < 256; ++i) { f.notify("same-wallet"); f.drain(); }
        QCOMPARE(f.count(Publication::Changed), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{255});
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        handoff.reset(); f.drain();
        QCOMPARE(f.count(Publication::Changed), 2);
        auto empty = f.owner->takeNotifications();
        QVERIFY(empty); QVERIFY(empty->entries.empty());
        f.notify("new-wallet"); f.drain();
        QCOMPARE(f.count(Publication::Changed), 3);
        auto next = f.owner->takeNotifications();
        QVERIFY(next); QCOMPARE(next->entries.front().name, std::string{"new-wallet"});
        next.reset(); f.drain();
    }
    void preAddNotificationMustNotClaimACompleteCatalog()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        for (int i{0}; i < 64; ++i) {
            const auto name = std::string{"committed-"} + util::ToString(i);
            f.notify(name);                 // Core NotifyWalletLoaded.
            f.catalog.push_back(name);      // Core AddWallet completes.
            f.drain();
        }
        f.notify("pending-add");           // Producer pauses after Notify returns.
        f.drain();                         // Another worker captures the catalog.
        f.catalog.emplace_back("pending-add"); // AddWallet commits with no later notification.
        bool observed{false};
        size_t claimed{0};
        for (;;) {
            auto handoff = f.owner->takeNotifications();
            if (!handoff || handoff->entries.empty()) break;
            for (const auto& entry : handoff->entries) { observed |= entry.name == "pending-add"; ++claimed; }
            handoff.reset(); f.drain();
        }
        QVERIFY2(observed, "The notification handle must preserve the pre-AddWallet discovery");
        QCOMPARE(claimed, size_t{65});
        QCOMPARE(f.count(Publication::Failed), 0);
        QCOMPARE(f.probe->captures.size(), size_t{1});
        f.owner->retire(); f.drain();
        QVERIFY(f.quiescent());
    }
    void announcedWalletsRemainModelStateWithoutRecapture()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        for (int i{0}; i < 96; ++i) {
            const auto name = std::string{"wallet-"} + util::ToString(i);
            f.notify(name);
            f.catalog.push_back(name);
            f.drain();
        }
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.count(Publication::Failed), 0);
        QCOMPARE(f.count(Publication::Changed), 1);
        QCOMPARE(f.pendingCount(), size_t{0});
        size_t presented{0};
        for (;;) {
            auto handoff = f.owner->takeNotifications();
            QVERIFY(handoff);
            if (handoff->entries.empty()) break;
            QVERIFY(handoff->entries.size() <= 16);
            presented += handoff->entries.size();
            handoff.reset(); f.drain();
        }
        QCOMPARE(presented, size_t{96});
        QCOMPARE(f.probe->captures.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.size(), size_t{96});
        QCOMPARE(f.probe->unload_subscriptions.size(), size_t{96});
        QCOMPARE(f.probe->unload_disconnects, std::vector<std::optional<Lane>>(96, Lane::Utility));
        f.owner->retire(); f.drain();
        QVERIFY(f.quiescent());
    }
    void realCallbackConcurrentWithInitialCaptureKeepsItsRefresh()
    {
        const auto probe = std::make_shared<PhysicalProbe>();
        execution::Executor executor{execution::Executor::Config{}};
        auto owner = std::make_shared<walletlife::LoadSubscription>(&executor,
            [probe](walletlife::LoadSubscription::Callback callback) {
                probe->registration_off_gui = QThread::currentThread() != probe->gui;
                probe->callback = std::move(callback);
                return interfaces::MakeCleanupHandler([probe] {
                    probe->disconnect_off_gui = QThread::currentThread() != probe->gui;
                    ++probe->disconnects;
                });
            }, [probe] {
                ++probe->captures;
                probe->entered.release(); probe->release.acquire();
                std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
                wallets.push_back(std::make_unique<PhysicalWallet>(probe, "initial"));
                return wallets;
            }, [probe](walletlife::Update update) {
                if (QThread::currentThread() == probe->gui) probe->notification_off_gui = false;
                if (update.kind == Publication::Ready) probe->ready.release();
                if (update.kind == Publication::InitialHandoff) probe->initial_disposed.release();
                if (update.kind == Publication::Changed) probe->changed.release();
            });
        QSemaphore drained;
        owner->whenRetired([&] { drained.release(); });
        const auto cleanup = qScopeGuard([&] { probe->release.release(); owner->retire(); drained.acquire(); });
        QVERIFY(owner->start());
        QVERIFY(probe->entered.tryAcquire(1, 5000));
        // Core owns the producer thread; it can enter ingress while the
        // executor's capture is held. Joining follows the completed callback.
        QSemaphore callback_finished;
        std::thread producer{[probe, &callback_finished] {
            probe->callback(std::make_unique<PhysicalWallet>(probe, "concurrent"));
            callback_finished.release();
        }};
        const bool callback_done = callback_finished.tryAcquire(1, 5000);
        if (!callback_done) probe->release.release();
        producer.join();
        QVERIFY(callback_done);
        probe->release.release();
        QVERIFY(probe->ready.tryAcquire(1, 5000));
        auto initial = owner->takeInitial();
        QVERIFY(initial); QCOMPARE(initial->entries.size(), size_t{1});
        QCOMPARE(initial->entries.front().name, std::string{"initial"});
        initial.reset();
        QVERIFY(probe->initial_disposed.tryAcquire(1, 5000));
        QVERIFY(probe->changed.tryAcquire(1, 5000));
        auto changed = owner->takeNotifications();
        QVERIFY(changed);
        QCOMPARE(changed->entries.size(), size_t{1});
        QCOMPARE(changed->entries.front().name, std::string{"concurrent"});
        changed.reset(); owner->retire();
        QTRY_COMPARE(drained.available(), 1);
        QCOMPARE(probe->captures.load(), 1);
        QCOMPARE(probe->disconnects.load(), 1);
        QCOMPARE(probe->destroyed.load(), 2);
        QVERIFY(probe->registration_off_gui && probe->metadata_off_gui && probe->disconnect_off_gui && probe->destruction_off_gui && probe->notification_off_gui);
    }
    void realRegistrationCanBeRetiredWhileBlockedWithoutBlockingGui()
    {
        const auto probe = std::make_shared<PhysicalProbe>();
        execution::Executor executor{execution::Executor::Config{}};
        auto owner = std::make_shared<walletlife::LoadSubscription>(&executor,
            [probe](walletlife::LoadSubscription::Callback callback) {
                probe->registration_off_gui = QThread::currentThread() != probe->gui;
                callback(std::make_unique<PhysicalWallet>(probe, "inline"));
                probe->entered.release(); probe->release.acquire();
                return interfaces::MakeCleanupHandler([probe] {
                    probe->disconnect_off_gui = QThread::currentThread() != probe->gui;
                    ++probe->disconnects;
                });
            }, [probe] { ++probe->captures; return std::vector<std::unique_ptr<interfaces::Wallet>>{}; },
            [probe](walletlife::Update update) {
                if (QThread::currentThread() == probe->gui) probe->notification_off_gui = false;
                if (update.kind == Publication::Retired) probe->retired.release();
            });
        QSemaphore drained;
        owner->whenRetired([&] { drained.release(); });
        const auto cleanup = qScopeGuard([&] { probe->release.release(); owner->retire(); drained.acquire(); });
        QVERIFY(owner->start());
        QVERIFY(probe->entered.tryAcquire(1, 5000));
        owner->retire();
        QTimer::singleShot(0, this, [probe] { probe->heartbeat = true; });
        QTRY_VERIFY(probe->heartbeat.load());
        QCOMPARE(drained.available(), 0); QCOMPARE(probe->retired.available(), 0);
        probe->release.release();
        QTRY_COMPARE(drained.available(), 1);
        QCOMPARE(probe->retired.available(), 1);
        QCOMPARE(probe->captures.load(), 0);
        QCOMPARE(probe->disconnects.load(), 1);
        QCOMPARE(probe->destroyed.load(), 1);
        QVERIFY(probe->registration_off_gui && probe->disconnect_off_gui && probe->destruction_off_gui && probe->notification_off_gui);
    }

    void stoppingQueuedScanReleasesTheNotificationWhileCoreIsBlocked()
    {
        execution::Executor executor{execution::Executor::Config{}};
        auto blocker = executor.makeQueue(Lane::Core);
        const auto probe = std::make_shared<PhysicalProbe>();
        std::atomic_bool metadata_read{false};
        auto owner = std::make_shared<walletlife::LoadSubscription>(&executor,
            [probe](walletlife::LoadSubscription::Callback callback) {
                probe->callback = std::move(callback);
                return interfaces::MakeCleanupHandler([probe] { ++probe->disconnects; });
            }, [] { return std::vector<std::unique_ptr<interfaces::Wallet>>{}; },
            [probe](walletlife::Update update) {
                if (update.kind == Publication::Ready) probe->ready.release();
                if (update.kind == Publication::Changed) probe->changed.release();
                if (update.kind == Publication::Retired) probe->retired.release();
            }, std::function<uint64_t()>{}, [&](interfaces::Wallet&) {
                metadata_read = true;
                return std::string{"queued"};
            });
        QSemaphore drained;
        owner->whenRetired([&] { drained.release(); });
        const auto cleanup = qScopeGuard([&] {
            probe->release.release();
            owner->retire();
            drained.acquire();
            executor.stopAndJoin();
        });
        QVERIFY(owner->start());
        QVERIFY(probe->ready.tryAcquire(1, 5000));
        QVERIFY(blocker.post([probe] {
            probe->callback(std::make_unique<PhysicalWallet>(probe, "queued"));
            probe->entered.release();
            probe->release.acquire();
        }));
        QVERIFY(probe->entered.tryAcquire(1, 5000));
        QCOMPARE(owner->settlementThrough(owner->notificationFence()), walletlife::LoadSubscription::Settlement::Pending);
        owner->retire();
        QTRY_COMPARE(drained.available(), 1);
        QCOMPARE(probe->retired.available(), 1);
        QCOMPARE(probe->disconnects.load(), 1);
        QCOMPARE(probe->destroyed.load(), 1);
        QVERIFY(probe->destruction_off_gui);
        probe->release.release();
        executor.stopAndJoin();
        QVERIFY(!metadata_read);
        QCOMPARE(probe->changed.available(), 0);
    }
    void claimedNotificationKeepsTheOriginalUnloadReceipt()
    {
        const auto clock = std::make_shared<walletopening::NativeEventClock>();
        const auto observed = std::make_shared<walletpublication::NativeObservation>(clock);
        Fixture fixture{[clock] { return clock->next(); }};
        QVERIFY(fixture.owner->start());
        fixture.drain();
        QVERIFY(fixture.owner->setNotificationContext({}, false, 0, {}, observed));
        fixture.notify("same-name");
        fixture.drain();
        auto handoff = fixture.owner->takeNotifications();
        QVERIFY(handoff && handoff->entries.size() == 1);
        const auto& source = handoff->entries.front().wallet;
        const auto original = source->receipt()->unloaded;
        const std::weak_ptr<walletpublication::ObservedLoad> carrier{source};
        std::unique_ptr<interfaces::Handler> handler;
        auto wallet = source->take(handler);
        QVERIFY(wallet && handler);
        QVERIFY(!source->take(handler));

        auto opening = MakeWalletSession(fixture.executor, [] {}, walletopening::NativeEvents{clock, original});
        QVERIFY(opening->start(wallet, handler));
        QVERIFY(!wallet && !handler);
        handoff.reset();
        fixture.drain();
        QVERIFY(carrier.expired());
        QVERIFY(fixture.probe->destroyed.empty());

        walletopening::NativeEvents{clock, original}.observeUnload();
        opening->retire();
        bool stopped{false};
        opening->stop(false, [&] { stopped = true; });
        fixture.drain();
        QVERIFY(stopped);
        QCOMPARE(fixture.probe->destroyed.size(), size_t{1});
        QVERIFY(fixture.probe->destroyed.front().second == Lane::Utility);

        fixture.notify("same-name");
        fixture.drain();
        const auto receipt = observed->find("same-name", [](interfaces::Wallet&) -> QString {
            throw std::logic_error("An already described load must use its own receipt");
        });
        QVERIFY(!receipt.wallet);
        QCOMPARE(receipt.events.unloaded.get(), original.get());
        QVERIFY(receipt.events.unloaded->load() > 0);
    }

    void initialWalletHandoffTransfersOwnershipOnlyAfterRetirementAdmission()
    {
        const auto probe = std::make_shared<PhysicalProbe>();
        execution::Executor executor{execution::Executor::Config{}};
        auto subscription = std::make_shared<walletlife::LoadSubscription>(&executor,
            [](walletlife::LoadSubscription::Callback) { return interfaces::MakeCleanupHandler([] {}); },
            [probe] {
                std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
                wallets.push_back(std::make_unique<PhysicalWallet>(probe, "initial"));
                return wallets;
            }, [probe](walletlife::Update update) {
                if (update.kind == Publication::Ready) probe->ready.release();
            });
        const auto retired = std::make_shared<std::atomic_bool>(false);
        subscription->whenRetired([retired] { *retired = true; });
        const auto cleanup = qScopeGuard([&] {
            subscription->retire();
            const bool drained = QTest::qWaitFor([&] { return retired->load(); }, 5'000);
            executor.stopAndJoin();
            QVERIFY(drained);
        });
        QVERIFY(subscription->start());
        QTRY_VERIFY(probe->ready.available() > 0);
        auto handoff = subscription->takeInitial();
        QVERIFY(handoff);
        QCOMPARE(handoff->entries.size(), size_t{1});
        auto& entry = handoff->entries.front();
        const auto* original = entry.initial_wallet.get();
        QVERIFY(original);
        QVERIFY(entry.unload_observation && entry.unload_observation->handler);

        auto rejected = MakeWalletSession(executor, [] {});
        const auto refused = std::make_shared<std::atomic_bool>(false);
        rejected->stop(false, [refused] { *refused = true; });
        QTRY_VERIFY(refused->load());
        QCOMPARE(rejected->start(entry.initial_wallet, entry.unload_observation->handler).submitted.rejection,
                 execution::Rejection::Closed);
        QCOMPARE(entry.initial_wallet.get(), original);
        QVERIFY(entry.unload_observation->handler);
        QCOMPARE(probe->destroyed.load(), 0);

        auto opening = MakeWalletSession(executor, [] {});
        const auto stopped = std::make_shared<std::atomic_bool>(false);
        const auto stop_opening = qScopeGuard([&] {
            opening->stop(false, [stopped] { *stopped = true; });
            QVERIFY(QTest::qWaitFor([&] { return stopped->load(); }, 5'000));
        });
        {
            const auto started = opening->start(entry.initial_wallet, entry.unload_observation->handler);
            QVERIFY(started);
            QCOMPARE(started.borrower.get(), original);
        }
        QVERIFY(!entry.initial_wallet);
        QVERIFY(!entry.unload_observation->handler);
        opening->stop(false, [stopped] { stopped->store(true); });
        QTRY_VERIFY(stopped->load());
        QCOMPARE(probe->destroyed.load(), 1);
        QVERIFY(probe->destruction_off_gui);

        handoff.reset();
        subscription->retire();
        QTRY_VERIFY(retired->load());
        QCOMPARE(probe->destroyed.load(), 1);
    }

    void realObserverLossAndDroppedHandoffStillDrainOwningResources()
    {
        const auto probe = std::make_shared<PhysicalProbe>();
        execution::Executor executor{execution::Executor::Config{}};
        auto observer = std::make_unique<QObject>();
        const QPointer<QObject> guard{observer.get()};
        const auto bridge = std::shared_ptr<QObject>{new QObject, [](QObject* object) {
            if (QThread::currentThread() == object->thread()) delete object;
            else object->deleteLater();
        }};
        auto owner = std::make_shared<walletlife::LoadSubscription>(&executor,
            [probe](walletlife::LoadSubscription::Callback callback) {
                probe->registration_off_gui = QThread::currentThread() != probe->gui;
                callback(std::make_unique<PhysicalWallet>(probe, "inline"));
                return interfaces::MakeCleanupHandler([probe] {
                    probe->disconnect_off_gui = QThread::currentThread() != probe->gui;
                    ++probe->disconnects;
                });
            }, [probe] {
                ++probe->captures;
                std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
                wallets.push_back(std::make_unique<PhysicalWallet>(probe, "initial"));
                return wallets;
            }, [probe, bridge, guard](walletlife::Update update) {
                if (QThread::currentThread() == probe->gui) probe->notification_off_gui = false;
                QMetaObject::invokeMethod(bridge.get(), [probe, guard, update] {
                    (void)update; // Only a value reaches the observer's GUI turn.
                    if (guard) ++probe->gui_publications;
                }, Qt::QueuedConnection);
                if (update.kind == Publication::Ready) probe->ready.release();
                if (update.kind == Publication::Retired) probe->retired.release();
            });
        QSemaphore drained;
        owner->whenRetired([&] { probe->drained = true; drained.release(); });
        const auto cleanup = qScopeGuard([&] { if (owner) owner->retire(); drained.acquire(); });
        QVERIFY(owner->start());
        // Withhold GUI dispatch until after destroying its observer.
        QVERIFY(probe->ready.tryAcquire(1, 5000));
        auto handoff = owner->takeInitial();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        observer.reset(); owner->retire();
        QVERIFY(!probe->drained.load());
        handoff.reset(); owner.reset();
        QTRY_VERIFY(probe->drained.load());
        executor.stopAndJoin();
        QCOMPARE(probe->gui_publications.load(), 0);
        QCOMPARE(probe->retired.available(), 1);
        QCOMPARE(probe->disconnects.load(), 1);
        QCOMPARE(probe->destroyed.load(), 2);
        QVERIFY(probe->registration_off_gui && probe->metadata_off_gui && probe->disconnect_off_gui && probe->destruction_off_gui && probe->notification_off_gui);
    }
    void capturedQStringContextKeepsOriginalUtf8AndRetiresNatively()
    {
        Fixture f;QVERIFY(f.owner->start());f.drain();
        QString source=QStringLiteral("  ")+QString::fromUtf8("pending-₿-")+QString(40'000,QLatin1Char('x'))+QStringLiteral("  ");const auto native=source.trimmed().toStdString();
        QVERIFY(f.owner->setNotificationContext(source,true,71));source.fill(QLatin1Char('z'));
        f.notify(native);f.notify("independent");QVERIFY(f.owner->setNotificationContext({},false,72));f.drain();
        auto handoff=f.owner->takeNotifications();QVERIFY(handoff);QCOMPARE(handoff->entries.size(),size_t{1});QCOMPARE(handoff->entries.front().name,std::string{"independent"});
        QVERIFY(!handoff->entries.front().select_wallet);QCOMPARE(handoff->entries.front().selection_revision,uint64_t{71});
        handoff.reset();f.drain();
        f.owner->retire();f.drain();QVERIFY(f.quiescent());
    }
    void whitespaceContextDoesNotHideTheUnnamedWallet()
    {
        Fixture f;QVERIFY(f.owner->start());f.drain();QVERIFY(f.owner->setNotificationContext(QStringLiteral("   "),true,83));f.notify("");f.drain();
        auto handoff=f.owner->takeNotifications();QVERIFY(handoff);QCOMPARE(handoff->entries.size(),size_t{1});QVERIFY(handoff->entries.front().name.empty());QCOMPARE(handoff->entries.front().selection_revision,uint64_t{83});
    }
    void opaqueAdoptionContextKeepsOriginalIdentityAndNativeRetirement()
    {
        Fixture f;
        QVERIFY(f.owner->start());
        f.drain();
        std::optional<Lane> destroyed;
        auto origin = std::shared_ptr<void>(new uint64_t{91}, [&](void* value) {
            destroyed = f.probe->lane;
            delete static_cast<uint64_t*>(value);
        });
        const std::weak_ptr<void> original = origin;
        QVERIFY(f.owner->setNotificationContext({}, false, 1, origin));
        f.notify("original-context");
        QVERIFY(f.owner->setNotificationContext({}, false, 2, {}));
        origin.reset();
        f.drain();
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff);
        QCOMPARE(handoff->entries.size(), size_t{1});
        QVERIFY(handoff->entries.front().adoption_origin == original.lock());
        QCOMPARE(handoff->entries.front().selection_revision, uint64_t{1});
        QVERIFY(!original.expired());
        handoff.reset();
        f.drain();
        QVERIFY(original.expired());
        QVERIFY(destroyed == Lane::Core || destroyed == Lane::Utility);
    }

    void notificationRetainsTheProducingCommandContext()
    {
        Fixture f;
        QVERIFY(f.owner->start()); f.drain();
        f.owner->setNotificationContext("unfinished", true, 41);
        f.notify("unfinished");
        f.notify("unrelated");
        f.owner->setNotificationContext({}, false, 42);
        f.drain();
        auto handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        QCOMPARE(handoff->entries.front().name, std::string{"unrelated"});
        QVERIFY(!handoff->entries.front().select_wallet);
        QCOMPARE(handoff->entries.front().selection_revision, uint64_t{41});
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().first, std::string{"unfinished"});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Core});
        handoff.reset(); f.drain();
        f.notify("after-command"); f.drain();
        handoff = f.owner->takeNotifications();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{1});
        QVERIFY(handoff->entries.front().select_wallet);
        QCOMPARE(handoff->entries.front().selection_revision, uint64_t{42});
    }
    void legalRetirementSchedules_data()
    {
        QTest::addColumn<int>("stage");
        QTest::addColumn<bool>("inline_callback");
        for (int stage{0}; stage < 8; ++stage) {
            for (const bool callback : {false, true}) {
                const QByteArray tag = QByteArray{"retire-"} + QByteArray::number(stage) + (callback ? "-inline" : "-snapshot");
                QTest::newRow(tag.constData()) << stage << callback;
            }
        }
    }
    void legalRetirementSchedules()
    {
        QFETCH(int, stage);
        QFETCH(bool, inline_callback);
        Fixture f; f.catalog = {"DO_NOT_TRACE_private_wallet"};
        const auto record_trace = qScopeGuard([&] {
            QJsonArray trace;
            for (const auto& event : f.trace) trace.append(QString::fromStdString(event));
            qInfo().noquote() << QJsonDocument{QJsonObject{{"stage", stage}, {"inline_callback", inline_callback}, {"events", trace}}}.toJson(QJsonDocument::Compact);
        });
        if (inline_callback) f.during_registration = [&] { f.callback(f.wallet("DO_NOT_TRACE_callback")); };
        QVERIFY(f.owner->start());
        if (stage >= 1) QVERIFY(f.step(Lane::Core));
        if (stage >= 2) QVERIFY(f.step(Lane::Utility));
        std::unique_ptr<walletlife::LoadSubscription::Handoff> handoff;
        if (stage >= 3) { handoff = f.owner->takeInitial(); QVERIFY(handoff); }
        if (stage >= 4) handoff.reset();
        if (stage >= 5) QVERIFY(f.step(Lane::Utility));
        if (stage >= 6) f.notify("DO_NOT_TRACE_after_initial_disposal");
        if (stage >= 7) f.drain();
        f.owner->retire();
        if (handoff) {
            // Worker turns cannot retire the handler while the original
            // handoff still holds the wallet.
            while (f.step(Lane::Utility)) {}
            QVERIFY(f.probe->disconnects.empty());
            QVERIFY(f.probe->destroyed.empty());
            handoff.reset();
        }
        f.drain();
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->disconnects.size(), f.probe->registrations.size());
        QCOMPARE(f.count(Publication::Ready), stage >= 2 ? 1 : 0);
        if (f.callback) f.notify("DO_NOT_TRACE_late");
        f.drain();
        QVERIFY(f.quiescent());
        for (const auto& event : f.trace) QVERIFY(event.find("DO_NOT_TRACE") == std::string::npos);
        for (const auto& entry : f.probe->destroyed) QVERIFY(entry.second == Lane::Core || entry.second == Lane::Utility);
    }
    void callbackDuringRegistrationRetainsBothInitialAndNotifiedWallets()
    {
        Fixture f;
        f.catalog = {"initial"};
        f.during_registration = [&] { f.callback(f.wallet("inline")); };
        QVERIFY(f.owner->start());
        QVERIFY(!f.owner->takeInitial());
        QVERIFY(f.step(Lane::Core));
        QCOMPARE(f.probe->registrations, std::vector<std::optional<Lane>>{Lane::Core});
        QCOMPARE(f.probe->captures, std::vector<std::optional<Lane>>{Lane::Core});
        QVERIFY(f.publications.empty());
        f.drain();
        QCOMPARE(f.count(Publication::Ready), 1);
        auto initial = f.owner->takeInitial();
        QVERIFY(initial); QCOMPARE(initial->entries.size(), size_t{1});
        QCOMPARE(initial->entries.front().name, std::string{"initial"});
        QVERIFY(!f.owner->takeNotifications());
        initial.reset(); f.drain();
        auto changed = f.owner->takeNotifications();
        QVERIFY(changed); QCOMPARE(changed->entries.size(), size_t{1});
        QCOMPARE(changed->entries.front().name, std::string{"inline"});
        changed.reset(); f.drain();
        f.owner->retire(); f.drain();
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->destroyed.size(), size_t{2});
        for (const auto& entry : f.probe->names) QCOMPARE(entry.second, std::optional<Lane>{Lane::Core});
        for (const auto& entry : f.probe->destroyed) QCOMPARE(entry.second, std::optional<Lane>{Lane::Utility});
        QCOMPARE(f.probe->disconnects, std::vector<std::optional<Lane>>{Lane::Utility});
        QVERIFY(f.quiescent());
    }
    void retirementBeforeRegistrationOutcomeCannotPublishReady()
    {
        Fixture f; f.catalog = {"retired"};
        QVERIFY(f.owner->start());
        QVERIFY(f.step(Lane::Core));
        f.owner->retire();
        QVERIFY(f.probe->disconnects.empty());
        QVERIFY(f.step(Lane::Utility));
        f.drain();
        QCOMPARE(f.count(Publication::Ready), 0);
        QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(!f.owner->takeInitial());
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Utility});
        QVERIFY(f.quiescent());
    }
    void executorClosePreservesHandoffAndSubscriptionRetirement()
    {
        Fixture f;
        f.catalog = {"original"};
        QVERIFY(f.owner->setNotificationContext(QStringLiteral("original"), false));
        QVERIFY(f.owner->start());
        f.drain();
        auto handoff = f.owner->takeInitial();
        QVERIFY(handoff);
        QCOMPARE(handoff->entries.size(), size_t{1});
        f.executor.stopAndJoin();
        f.owner->retire();
        handoff.reset();
        QVERIFY(f.probe->destroyed.empty());
        f.drain();
        QCOMPARE(f.probe->destroyed.size(), size_t{1});
        QCOMPARE(f.probe->destroyed.front().second, std::optional<Lane>{Lane::Utility});
        QCOMPARE(f.probe->destroyed_before_disconnect, std::vector<size_t>{1});
        QCOMPARE(f.count(Publication::Retired), 1);
    }

    void droppedGuiHandoffRetiresShadowsBeforeParentCleanup()
    {
        Fixture f; f.catalog = {"one", "two"};
        QVERIFY(f.owner->start()); f.drain();
        auto handoff = f.owner->takeInitial();
        QVERIFY(handoff); QCOMPARE(handoff->entries.size(), size_t{2});
        f.owner->retire();
        QVERIFY(!f.step(Lane::Utility));
        handoff.reset();
        QVERIFY(f.probe->destroyed.empty());
        QVERIFY(f.step(Lane::Utility));
        QCOMPARE(f.probe->destroyed.size(), size_t{2});
        QCOMPARE(f.probe->unload_disconnects, std::vector<std::optional<Lane>>(2, Lane::Utility));
        f.drain();
        QCOMPARE(f.probe->destroyed_before_disconnect, std::vector<size_t>{2});
        QCOMPARE(f.probe->disconnects.size(), size_t{1});
        QCOMPARE(f.count(Publication::InitialHandoff), 0);
        QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.quiescent());
    }
    void initialPresentationIsBoundedAndHasAnOwnedContinuation()
    {
        Fixture f;
        for (int i{0}; i < 35; ++i) f.catalog.push_back("wallet-" + util::ToString(i));
        QVERIFY(f.owner->start()); f.drain();
        size_t presented{0}; int batches{0};
        for (;;) {
            auto handoff = f.owner->takeInitial();
            QVERIFY(handoff);
            if (handoff->entries.empty()) break;
            QVERIFY(handoff->entries.size() <= 16);
            presented += handoff->entries.size(); ++batches;
            QVERIFY(!f.owner->takeInitial());
            const auto before = f.count(Publication::InitialHandoff);
            handoff.reset(); f.drain();
            QCOMPARE(f.count(Publication::InitialHandoff), before + 1);
        }
        QCOMPARE(presented, size_t{35}); QCOMPARE(batches, 3);
        QCOMPARE(f.probe->destroyed.size(), size_t{35});
    }
    void absentProducerIsAnExplicitFailureBeforeSnapshotCapture()
    {
        Fixture f; f.registration_missing = true;
        QVERIFY(f.owner->start()); f.drain();
        QCOMPARE(f.count(Publication::Ready), 0);
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QVERIFY(f.probe->captures.empty()); QVERIFY(f.probe->disconnects.empty());
        QVERIFY(f.quiescent());
    }
    void captureFailureOwnsOneTerminalFailureAndDisconnect()
    {
        Fixture f; f.capture_failure = true;
        QVERIFY(f.owner->start()); f.drain();
        QCOMPARE(f.count(Publication::Ready), 0);
        QCOMPARE(f.count(Publication::Failed), 1);
        QCOMPARE(f.count(Publication::Retired), 1);
        QCOMPARE(f.probe->disconnects.size(), size_t{1});
        f.notify("late"); f.drain();
        QCOMPARE(f.count(Publication::Changed), 0);
        QCOMPARE(f.probe->destroyed.back().second, std::optional<Lane>{Lane::Core});
        QVERIFY(f.quiescent());
    }
};

BITCOINQML_REGISTER_QT_TEST(WalletLifecycleTests)
#include <test_walletlifecycle.moc>
