// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/execution.h>
#include <qml/retainedsnapshot.h>
#include <qml/retainedvalue.h>
#include <qml/snapshotquery.h>
#include <qml/models/activitysnapshot.h>
#include <qml/models/feereadstate.h>
#include <qml/walletlifecyclestate.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualfeereceipts.h>
#include <test/qt_test_registry.h>

#include <QtTest/QtTest>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QProcessEnvironment>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>

#include <atomic>
#include <cstdio>
#include <thread>

#include <deque>
#include <functional>
#include <optional>
#include <stdexcept>
#include <utility>

namespace {
QString RetainTransportProbe(const QByteArray& tag, const QByteArray& output, const QByteArray& error, QJsonObject receipt)
{
    const QString destination = qEnvironmentVariable("BITCOIN_QML_EXECUTION_PROBE_DIR");
    if (destination.isEmpty()) return {};
    const QDir root{destination};
    if (!root.mkpath(QStringLiteral("."))) return QStringLiteral("Cannot create transport-probe evidence directory: %1").arg(destination);
    QTemporaryDir directory{root.filePath(QString::fromLatin1(tag) + QStringLiteral("-XXXXXX"))};
    if (!directory.isValid()) return directory.errorString();
    directory.setAutoRemove(false);
    receipt.insert(QStringLiteral("artifact_directory"), directory.path());
    const auto save = [&](const QString& name, const QByteArray& bytes) -> QString {
        QFile file{directory.filePath(name)};
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.flush()) {
            return QStringLiteral("Cannot retain %1: %2").arg(file.fileName(), file.errorString());
        }
        return {};
    };
    for (const auto& entry : {std::pair{QStringLiteral("stdout"), output}, std::pair{QStringLiteral("stderr"), error},
                             std::pair{QStringLiteral("result.json"), QJsonDocument{receipt}.toJson()}}) {
        const QString failure = save(entry.first, entry.second);
        if (!failure.isEmpty()) return failure;
    }
    return {};
}
} // namespace

class ExecutionTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void cleanupReservedAfterWorkWaitsForItsOutcome_data()
    {
        QTest::addColumn<bool>("legacy"); QTest::addColumn<bool>("scoped");
        QTest::newRow("legacy-parent-close") << true << false;
        QTest::newRow("legacy-scope-release") << true << true;
        QTest::newRow("routed-parent-close") << false << false;
        QTest::newRow("routed-scope-release") << false << true;
    }
    void cleanupReservedAfterWorkWaitsForItsOutcome()
    {
        QFETCH(bool, legacy); QFETCH(bool, scoped);
        using namespace execution;
        struct Turn { Phase phase; TaskId id; std::function<void()> run; };
        std::deque<Turn> turns; std::vector<int> order;
        const auto post = [&](Phase phase, TaskId id, std::function<void()> turn) {
            turns.push_back({phase, id, std::move(turn)}); return true;
        };
        const auto queue = legacy
            ? std::make_shared<TaskQueue>(post, Options{})
            : std::make_shared<TaskQueue>([&](Lane, Phase phase, TaskId id, auto turn) { return post(phase, id, std::move(turn)); }, Options{}, 1);
        const auto step = [&] { auto turn = std::move(turns.front().run); turns.pop_front(); turn(); };
        const auto finish = qScopeGuard([&] { queue->closeAdmission(); while (!turns.empty()) step(); });
        std::shared_ptr<Scheduler> owner = scoped ? queue->openActionScope() : queue;
        QVERIFY(owner);
        const auto key = queue->allocateKey();
        const auto work = Submit(*owner, [&] { order.push_back(1); }, [&] { order.push_back(2); }, {}, {Lane::Core, key});
        QVERIFY(work);
        const auto cleanup = Submit(*owner, [&] { order.push_back(3); }, [&] { order.push_back(4); }, {}, {Lane::Cleanup, key}, true);
        QVERIFY(cleanup); QCOMPARE(turns.size(), size_t{1}); QVERIFY(order.empty());
        if (scoped) owner.reset(); else queue->closeAdmission();
        QCOMPARE(queue->phase(cleanup.id), std::make_optional(Phase::Reserved));
        QCOMPARE(turns.size(), size_t{1}); QCOMPARE(turns.front().id, work.id);
        step(); // Work finishes, but its Domain outcome still owns this key.
        QCOMPARE(order, std::vector<int>({1}));
        QCOMPARE(queue->phase(cleanup.id), std::make_optional(Phase::Reserved));
        QCOMPARE(turns.size(), size_t{1}); QCOMPARE(turns.front().id, work.id);
        QCOMPARE(turns.front().phase, Phase::OutcomeReady);
        step(); // Only outcome consumption makes the cleanup runnable.
        QCOMPARE(order, std::vector<int>({1, 2}));
        QCOMPARE(queue->phase(cleanup.id), std::make_optional(Phase::Queued));
        QCOMPARE(turns.size(), size_t{1}); QCOMPARE(turns.front().id, cleanup.id);
        step(); QCOMPARE(order, std::vector<int>({1, 2, 3}));
        QCOMPARE(turns.size(), size_t{1}); QCOMPARE(turns.front().id, cleanup.id);
        step(); QCOMPARE(order, std::vector<int>({1, 2, 3, 4}));
        QCOMPARE(queue->pending(), size_t{0}); QVERIFY(turns.empty());
    }
    void throwingInitialTransportRollsBackOnlyItsAdmission_data()
    {
        QTest::addColumn<bool>("legacy"); QTest::addColumn<bool>("core"); QTest::addColumn<bool>("retain");
        QTest::newRow("legacy-Control-before-handoff") << true << false << false;
        QTest::newRow("legacy-Core-before-handoff") << true << true << false;
        QTest::newRow("routed-Control-before-handoff") << false << false << false;
        QTest::newRow("routed-Core-before-handoff") << false << true << false;
        QTest::newRow("legacy-Control-retained-callback") << true << false << true;
        QTest::newRow("legacy-Core-retained-callback") << true << true << true;
        QTest::newRow("routed-Control-retained-callback") << false << false << true;
        QTest::newRow("routed-Core-retained-callback") << false << true << true;
    }
    void throwingInitialTransportRollsBackOnlyItsAdmission()
    {
        QFETCH(bool, legacy); QFETCH(bool, core); QFETCH(bool, retain);
        using namespace execution;
        bool armed{false}; TaskId refused;
        int effects{0}, outcomes{0}, failures{0}, retired{0}, cleanup_outcomes{0};
        std::deque<std::function<void()>> turns;
        const auto post = [&](Lane, Phase phase, TaskId id, std::function<void()> turn) {
            if (phase == Phase::Queued && std::exchange(armed, false)) {
                refused = id;
                if (retain) turns.push_back(std::move(turn));
                throw std::runtime_error{"initial transport rejected before returning"};
            }
            turns.push_back(std::move(turn)); return true;
        };
        const auto queue = legacy
            ? std::make_shared<TaskQueue>([&](Phase phase, TaskId id, auto turn) { return post(Lane::Core, phase, id, std::move(turn)); }, Options{})
            : std::make_shared<TaskQueue>(post, Options{}, 1);
        const auto drain = [&] { while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); } };
        const auto finish = qScopeGuard([&] { queue->closeAdmission(); drain(); });
        const auto key = queue->allocateKey();
        const auto cleanup = Submit(*queue, [&] { ++retired; }, [&] { ++cleanup_outcomes; }, {}, {Lane::Cleanup, key}, true);
        QVERIFY(cleanup); auto held = queue->borrow(key); QVERIFY(held);
        const auto pending = queue->pending();
        armed = true; bool threw{false};
        try {
            (void)Submit(*queue, [&] { ++effects; }, [&] { ++outcomes; }, [&](auto) { ++failures; }, {core ? Lane::Core : Lane::Control, key});
        } catch (const std::runtime_error&) { threw = true; }
        QVERIFY(threw); QCOMPARE(queue->pending(), pending); QVERIFY(!queue->phase(refused));
        QCOMPARE(effects, 0); QCOMPARE(outcomes, 0); QCOMPARE(failures, 0); QCOMPARE(retired, 0);
        QCOMPARE(turns.size(), retain ? size_t{1} : size_t{0});
        const auto next = Submit(*queue, [&] { ++effects; }, [&] { ++outcomes; }, {}, {core ? Lane::Core : Lane::Control, key});
        QVERIFY(next); QVERIFY(next.id.value > refused.value);
        if (retain) {
            auto stale = std::move(turns.front()); turns.pop_front(); stale();
            QCOMPARE(effects, 0); QCOMPARE(outcomes, 0);
            QCOMPARE(queue->phase(next.id), std::make_optional(Phase::Queued));
        }
        drain(); QCOMPARE(effects, 1); QCOMPARE(outcomes, 1); QCOMPARE(failures, 0);
        QCOMPARE(queue->pending(), pending); QVERIFY(queue->activateCleanup(cleanup.id));
        held.lease.reset(); drain();
        QCOMPARE(retired, 1); QCOMPARE(cleanup_outcomes, 1); QCOMPARE(queue->pending(), size_t{0});
    }
    void throwingLaterCoreTransportKeepsEveryAcceptedOutcome_data()
    {
        QTest::addColumn<bool>("retain");
        QTest::newRow("before-handoff") << false;
        QTest::newRow("retained-callback") << true;
    }
    void throwingLaterCoreTransportKeepsEveryAcceptedOutcome()
    {
        QFETCH(bool, retain);
        using namespace execution;
        struct Turn { Lane lane; std::function<void()> run; };
        std::deque<Turn> turns; TaskId fail;
        Lane active{Lane::Domain}; std::vector<int> effects, outcomes;
        int failed{0}, destroyed{0}; bool retired_on_cleanup{false};
        const auto queue = std::make_shared<TaskQueue>([&](Lane lane, Phase phase, TaskId id, std::function<void()> turn) {
            if (lane == Lane::Core && phase == Phase::Queued && id == fail) {
                fail = {};
                if (retain) turns.push_back({lane, std::move(turn)});
                throw std::runtime_error{"later Core transport threw"};
            }
            turns.push_back({lane, std::move(turn)}); return true;
        }, Options{}, 1);
        const auto drain = [&] {
            while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); active = turn.lane; turn.run(); }
        };
        const auto finish = qScopeGuard([&] { queue->closeAdmission(); drain(); });
        struct Capture {
            int& count; Lane& active; bool& native;
            Capture(int& count, Lane& active, bool& native) : count(count), active(active), native(native) {}
            ~Capture() { ++count; native = active == Lane::Cleanup; }
        };
        const auto first = Submit(*queue, [&] { effects.push_back(1); return 1; }, [&](int value) { outcomes.push_back(value); }, {}, {Lane::Core, {1, 1}});
        const auto second = Submit(*queue, [owned = std::make_unique<Capture>(destroyed, active, retired_on_cleanup), &effects] { effects.push_back(2); return 2; },
            [&](int value) { outcomes.push_back(value); }, [&](std::exception_ptr error) {
                QCOMPARE(active, Lane::Domain);
                try { std::rethrow_exception(error); }
                catch (const NotStarted& result) { QCOMPARE(result.cause, NotStarted::Cause::Transport); ++failed; }
            }, {Lane::Core, {2, 1}});
        const auto third = Submit(*queue, [&] { effects.push_back(3); return 3; }, [&](int value) { outcomes.push_back(value); }, {}, {Lane::Core, {3, 1}});
        QVERIFY(first && second && third); fail = second.id;
        QCOMPARE(queue->pending(), size_t{3}); QCOMPARE(effects.size(), size_t{0});
        queue->closeAdmission(); drain();
        QCOMPARE(effects, std::vector<int>({1, 3})); QCOMPARE(outcomes, std::vector<int>({1, 3}));
        QCOMPARE(failed, 1); QCOMPARE(destroyed, 1); QVERIFY(retired_on_cleanup);
        QCOMPARE(queue->pending(), size_t{0}); QVERIFY(turns.empty());
    }
    void retainedTransportRejectionTerminates_data()
    {
        QTest::addColumn<int>("mode"); QTest::addColumn<QString>("assertion_function"); QTest::addColumn<bool>("port_throws");
        QTest::newRow("final-idle-scope-wake") << 0 << QStringLiteral("releaseScope") << false;
        QTest::newRow("reserved-cleanup-turn") << 1 << QStringLiteral("activateCleanupLocked") << false;
        QTest::newRow("cancelled-not-started-handoff") << 2 << QStringLiteral("queueOutcome") << false;
        QTest::newRow("completed-domain-outcome") << 3 << QStringLiteral("queueOutcome") << false;
        QTest::newRow("throw-final-idle-scope-wake") << 0 << QStringLiteral("releaseScope") << true;
        QTest::newRow("throw-reserved-cleanup-turn") << 1 << QStringLiteral("activateCleanupLocked") << true;
        QTest::newRow("throw-cancelled-not-started-handoff") << 2 << QStringLiteral("queueOutcome") << true;
        QTest::newRow("throw-completed-domain-outcome") << 3 << QStringLiteral("queueOutcome") << true;
    }
    void retainedTransportRejectionTerminates()
    {
        QFETCH(int, mode); QFETCH(QString, assertion_function); QFETCH(bool, port_throws);
        const QByteArray tag{QTest::currentDataTag()};
        QByteArray marker{"execution retained transport rejected: "}; marker.append(tag); marker.append('\n');
        if (qEnvironmentVariableIsSet("BITCOIN_QML_EXECUTION_TRANSPORT_CHILD")) {
            QCOMPARE(qgetenv("BITCOIN_QML_EXECUTION_TRANSPORT_CHILD"), tag);
            using namespace execution;
            struct Turn { Lane lane; Phase phase; TaskId id; std::function<void()> run; };
            int effects{0}, outcomes{0}, failures{0}; bool armed{false}; TaskId expected;
            std::deque<Turn> turns;
            auto queue = std::make_shared<TaskQueue>([&](Lane lane, Phase phase, TaskId id, auto turn) {
                const bool selected = armed && ((mode == 0 && lane == Lane::Control && phase == Phase::OutcomeReady)
                    || (mode == 1 && lane == Lane::Cleanup && phase == Phase::Queued)
                    || (mode == 2 && lane == Lane::Cleanup && phase == Phase::OutcomeReady)
                    || (mode == 3 && lane == Lane::Domain && phase == Phase::OutcomeReady));
                if (selected) {
                    const bool retained = mode == 2
                        ? turns.size() == 1 && turns.front().lane == Lane::Core && turns.front().phase == Phase::Queued && turns.front().id == expected
                        : turns.empty();
                    if (id == expected && effects == (mode == 3 ? 1 : 0) && outcomes == 0 && failures == 0 && retained) {
                        std::fwrite(marker.constData(), 1, static_cast<size_t>(marker.size()), stderr); std::fflush(stderr);
                        if (port_throws) throw std::runtime_error{"retained transport threw"};
                        return false;
                    }
                    std::fputs("invalid retained-transport rejection prerequisites\n", stderr); std::fflush(stderr);
                }
                turns.push_back(Turn{lane, phase, id, std::move(turn)});
                return true;
            }, Options{}, 1);
            if (mode == 0) {
                auto scope = queue->openActionScope(); QVERIFY(scope);
                QCOMPARE(queue->pending(), size_t{1}); QVERIFY(turns.empty());
                queue->closeAdmission(); armed = true;
                scope.reset(); // Its original capability owns the final Control drain wake.
            } else if (mode == 1) {
                const auto cleanup = Submit(*queue, [&] { ++effects; }, [&] { ++outcomes; }, {},
                    {Lane::Cleanup, queue->allocateKey()}, true);
                QVERIFY(cleanup); expected = cleanup.id;
                QCOMPARE(queue->phase(expected), std::make_optional(Phase::Reserved)); QVERIFY(turns.empty());
                armed = true; QVERIFY(queue->activateCleanup(expected));
            } else {
                const auto admitted = Submit(*queue, [&] { ++effects; }, [&] { ++outcomes; },
                    [&](std::exception_ptr) { ++failures; });
                QVERIFY(admitted); expected = admitted.id;
                QCOMPARE(turns.size(), size_t{1}); QCOMPARE(effects, 0); QCOMPARE(outcomes, 0);
                QCOMPARE(turns.front().lane, Lane::Core); QCOMPARE(turns.front().phase, Phase::Queued);
                QCOMPARE(turns.front().id, expected);
                armed = true;
                if (mode == 2) QVERIFY(queue->cancelQueued(expected));
                else {
                    auto turn = std::move(turns.front().run); turns.pop_front();
                    turn(); // Actual Core execution reaches the owned Domain outcome dispatch.
                }
            }
            QFAIL("A rejected retained transport must terminate at its posted assertion");
        }
        const QString selector = QStringLiteral("retainedTransportRejectionTerminates:") + QString::fromLatin1(tag);
        const QString executable = QCoreApplication::applicationFilePath();
        const QStringList arguments{selector, QStringLiteral("-platform"), QStringLiteral("minimal"), QStringLiteral("-nocrashhandler")};
        auto environment = QProcessEnvironment::systemEnvironment();
        for (const auto* key : {"BITCOIN_QML_QTLOG_SINK_CASE", "BITCOIN_QML_QTLOG_CONFIGURED_CASE", "BITCOIN_QML_QTLOG_FATAL_CASE"}) environment.remove(key);
        environment.insert(QStringLiteral("BITCOIN_QML_TEST_CLASSES"), QStringLiteral("ExecutionTests"));
        environment.insert(QStringLiteral("BITCOIN_QML_EXECUTION_TRANSPORT_CHILD"), QString::fromLatin1(tag));
        QProcess process; process.setProcessEnvironment(environment); process.start(executable, arguments);
        const bool started = process.waitForStarted(10000);
        const qint64 child_pid = process.processId();
        const bool completed = started && process.waitForFinished(10000);
        if (!completed && process.state() != QProcess::NotRunning) { process.kill(); process.waitForFinished(10000); }
        const QByteArray output = process.readAllStandardOutput(), error = process.readAllStandardError();
        const QString evidence_error = RetainTransportProbe(tag, output, error, {
            {QStringLiteral("case"), QString::fromLatin1(tag)}, {QStringLiteral("executable"), executable},
            {QStringLiteral("arguments"), QJsonArray::fromStringList(arguments)}, {QStringLiteral("class_filter"), QStringLiteral("ExecutionTests")},
            {QStringLiteral("started"), started}, {QStringLiteral("completed_before_timeout"), completed}, {QStringLiteral("pid"), child_pid},
            {QStringLiteral("exit_status"), int(process.exitStatus())}, {QStringLiteral("exit_code"), process.exitCode()},
            {QStringLiteral("process_error"), int(process.error())}, {QStringLiteral("process_error_string"), process.errorString()},
            {QStringLiteral("LLVM_PROFILE_FILE"), environment.value(QStringLiteral("LLVM_PROFILE_FILE"))},
            {QStringLiteral("expected_assertion_function"), assertion_function}});
        QVERIFY2(evidence_error.isEmpty(), qPrintable(evidence_error));
        QVERIFY2(started, qPrintable(process.errorString())); QVERIFY2(completed, "Retained-transport child did not terminate before its timeout");
        QCOMPARE(process.exitStatus(), QProcess::CrashExit); QVERIFY(process.exitCode() != 0);
        QVERIFY2(!output.contains("FAIL!") && !output.contains("QFATAL"), output.constData());
        QByteArray protocol = error; protocol.replace("\r\n", "\n");
        QCOMPARE(protocol.count(marker), qsizetype{1}); QVERIFY2(protocol.startsWith(marker), error.constData());
        const QString diagnostic = QString::fromLocal8Bit(protocol.mid(marker.size()));
        // Match the exact posted assertion, with the runtime's source location
        // and signature. Extra diagnostics, including sanitizer errors, fail.
        const QRegularExpression assertion{QStringLiteral(
            "\\A(?:[^\\n]*execution\\.cpp:[0-9]+: [^\\n]*TaskQueue::%1\\([^\\n]*: Assertion [`']posted['`] failed\\.\\n"
            "|Assertion failed: \\(posted\\), function [^\\n]*%1[^\\n]*, file [^\\n]*execution\\.cpp, line [0-9]+\\.\\n"
            "|Assertion failed: posted, file [^\\n]*execution\\.cpp, line [0-9]+\\n)\\z").arg(assertion_function)};
        QVERIFY2(assertion.match(diagnostic).hasMatch(), error.constData());
    }
    void taskIdentityExhaustionPreservesExistingDrain_data()
    {
        QTest::addColumn<bool>("legacy"); QTest::addColumn<bool>("exhausted");
        QTest::newRow("routed-last-identities") << false << false;
        QTest::newRow("routed-exhausted") << false << true;
        QTest::newRow("legacy-last-identities") << true << false;
        QTest::newRow("legacy-exhausted") << true << true;
    }
    void taskIdentityExhaustionPreservesExistingDrain()
    {
        QFETCH(bool, legacy); QFETCH(bool, exhausted);
        using namespace execution;
        const auto maximum = std::numeric_limits<uint64_t>::max();
        auto limits = Options{}; limits.initial_task_sequence = exhausted ? maximum : maximum - 2;
        ManualRoutedExecution routed{limits}; std::deque<std::function<void()>> turns;
        auto queue = legacy ? std::make_shared<TaskQueue>([&](Phase, TaskId, auto turn) {
            turns.push_back(std::move(turn)); return true;
        }, limits) : routed.scheduler;
        const auto drain = [&] {
            while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
            routed.drain();
        };
        int effects{0}, outcomes{0}, retired{0}, cleanup_outcomes{0};
        const auto close = qScopeGuard([&] { queue->closeAdmission(); drain(); });
        const auto key = queue->allocateKey();
        std::optional<ResourceLease> held;
        if (!exhausted) {
            const auto cleanup = Submit(*queue, [&] { ++retired; }, [&] { ++cleanup_outcomes; }, {}, {Lane::Cleanup, key}, true);
            QVERIFY(cleanup); QCOMPARE(cleanup.id.value, maximum - 1);
            auto borrowed = queue->borrow(key); QVERIFY(borrowed);
            held.emplace(std::move(*borrowed.lease)); borrowed.lease.reset();
            const auto original = Submit(*queue, [&] { ++effects; }, [&] { ++outcomes; }, {}, {Lane::Core, key});
            QVERIFY(original); QCOMPARE(original.id.value, maximum);
        }
        const auto pending = queue->pending();
        QCOMPARE(Submit(*queue, [&] { ++effects; }, [] {}).rejection, Rejection::Closed);
        const auto refused_key = queue->allocateKey();
        QCOMPARE(Submit(*queue, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, refused_key}, true).rejection, Rejection::Closed);
        QCOMPARE(queue->borrow(refused_key).rejection, Rejection::Invalid);
        QCOMPARE(queue->pending(), pending); QCOMPARE(effects, 0); QCOMPARE(retired, 0);
        queue->closeAdmission(); drain();
        QCOMPARE(effects, exhausted ? 0 : 1); QCOMPARE(outcomes, exhausted ? 0 : 1);
        QCOMPARE(retired, 0); QCOMPARE(cleanup_outcomes, 0);
        held.reset(); drain();
        QCOMPARE(retired, exhausted ? 0 : 1); QCOMPARE(cleanup_outcomes, exhausted ? 0 : 1);
        QCOMPARE(queue->pending(), size_t{0}); QVERIFY(turns.empty()); QVERIFY(routed.quiescent());
    }
    void scopeIdentityExhaustionNeverWrapsOrReusesAdmission_data()
    {
        QTest::addColumn<bool>("legacy"); QTest::addColumn<quint64>("sequence");
        const auto maximum = std::numeric_limits<quint64>::max();
        QTest::newRow("routed-default") << false << quint64{0};
        QTest::newRow("routed-last-identity") << false << quint64{maximum - 1};
        QTest::newRow("routed-exhausted") << false << maximum;
        QTest::newRow("legacy-default") << true << quint64{0};
        QTest::newRow("legacy-last-identity") << true << quint64{maximum - 1};
        QTest::newRow("legacy-exhausted") << true << maximum;
    }
    void scopeIdentityExhaustionNeverWrapsOrReusesAdmission()
    {
        QFETCH(bool, legacy); QFETCH(quint64, sequence);
        using namespace execution;
        auto limits = Options{}; limits.initial_scope_sequence = sequence;
        ManualRoutedExecution routed{limits}; std::deque<std::function<void()>> turns;
        auto queue = legacy ? std::make_shared<TaskQueue>([&](Phase, TaskId, auto turn) {
            turns.push_back(std::move(turn)); return true;
        }, limits) : routed.scheduler;
        const auto drain = [&] {
            while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
            routed.drain();
        };
        int effects{0}, outcomes{0};
        const auto cleanup = qScopeGuard([&] { queue->closeAdmission(); drain(); });
        auto scope = queue->openActionScope();
        if (sequence == std::numeric_limits<quint64>::max()) {
            QVERIFY(!scope); QCOMPARE(queue->pending(), size_t{0});
        } else {
            QVERIFY(scope);
            QVERIFY(Submit(*scope, [&] { ++effects; }, [&] { ++outcomes; }));
            auto next = queue->openActionScope();
            QCOMPARE(bool(next), sequence == 0);
            next.reset(); queue->closeAdmission();
            drain(); QCOMPARE(effects, 1); QCOMPARE(outcomes, 1);
            scope.reset(); drain(); QCOMPARE(queue->pending(), size_t{0});
        }
        QVERIFY(turns.empty()); QVERIFY(routed.quiescent());
    }
    void scopesRetainBothOriginalDrainCapabilities()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto first = env.scheduler->openActionScope(); QVERIFY(first);
        QCOMPARE(first->pending(), size_t{1});
        auto second = env.scheduler->openActionScope(); QVERIFY(second);
        QCOMPARE(first->pending(), size_t{2}); QCOMPARE(second->pending(), size_t{2});
        env.scheduler->closeAdmission(); QVERIFY(env.queuedIdle());
        first.reset(); QCOMPARE(second->pending(), size_t{1}); QVERIFY(env.queuedIdle());
        second.reset(); QCOMPARE(env.scheduler->pending(), size_t{0});
        QCOMPARE(env.queuedCountIf([](const auto& turn) { return turn.lane == Lane::Control; }), 1);
        env.drain(); QVERIFY(env.quiescent());
    }
    void scopedCancellationCannotRetargetSiblingOrUnknownTasks()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto first = env.scheduler->openActionScope(), second = env.scheduler->openActionScope();
        QVERIFY(first && second);
        int effects{0}, cancelled{0}, completed{0};
        const auto original = Submit(*first, [&] { ++effects; }, [&] { ++completed; },
            [&](std::exception_ptr error) {
                try { std::rethrow_exception(error); }
                catch (const NotStarted& failure) { QCOMPARE(failure.cause, NotStarted::Cause::Cancelled); ++cancelled; }
            });
        const auto sibling = Submit(*second, [&] { ++effects; }, [&] { ++completed; });
        QVERIFY(original && sibling);
        QVERIFY(!first->cancelQueued(sibling.id)); QVERIFY(!first->cancelQueued({999}));
        QVERIFY(!first->activateCleanup({999})); QVERIFY(!env.scheduler->activateCleanup({999}));
        QVERIFY(!first->activateCleanup(original.id)); // An ordinary task is not a cleanup handle.
        QVERIFY(first->cancelQueued(original.id)); QVERIFY(!first->cancelQueued(original.id));
        first->closeOrdinaryAdmission();
        QCOMPARE(first->borrow(first->allocateKey()).rejection, Rejection::Closed);
        env.drain(); QCOMPARE(effects, 1); QCOMPARE(completed, 1); QCOMPARE(cancelled, 1);
        first.reset(); second.reset(); env.drain(); QVERIFY(env.quiescent());
    }
    void lastBorrowAloneCannotStartUnrequestedScopedCleanup()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        int retired{0}; const auto key = scope->allocateKey();
        const auto cleanup = Submit(*scope, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, key}, true);
        QVERIFY(cleanup); auto held = scope->borrow(key); QVERIFY(held);
        held.lease.reset(); QVERIFY(!env.step(Lane::Cleanup)); QCOMPARE(retired, 0);
        env.scheduler->closeAdmission(); QVERIFY(!env.step(Lane::Cleanup));
        scope.reset(); QVERIFY(env.step(Lane::Cleanup)); QCOMPARE(retired, 1);
        env.drain(); QVERIFY(env.quiescent());
    }
    void retiredScopeCannotActivateCleanupBeforeItsLastBorrowReturns()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int retired{0}, outcomes{0};
        const auto drain = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        const auto key = scope->allocateKey();
        const auto cleanup = Submit(*scope, [&] { ++retired; }, [&] { ++outcomes; }, {}, {Lane::Cleanup, key}, true);
        QVERIFY(cleanup); auto held = scope->borrow(key); QVERIFY(held);
        env.scheduler->closeAdmission(); scope.reset();
        QVERIFY(!env.scheduler->activateCleanup(cleanup.id));
        QCOMPARE(env.scheduler->phase(cleanup.id), std::make_optional(Phase::Reserved));
        QVERIFY(!env.step(Lane::Cleanup)); QCOMPARE(retired, 0);
        held.lease.reset();
        QCOMPARE(env.scheduler->phase(cleanup.id), std::make_optional(Phase::Queued));
        QVERIFY(env.step(Lane::Cleanup)); QCOMPARE(retired, 1); QCOMPARE(outcomes, 0);
        QVERIFY(env.step(Lane::Domain)); QCOMPARE(outcomes, 1);
        env.drain(); QVERIFY(env.quiescent());
    }
    void cancelledTransportCannotReenterQueuedCleanup_data()
    {
        QTest::addColumn<bool>("control");
        QTest::newRow("Core-transport") << false;
        QTest::newRow("Control-transport") << true;
    }
    void cancelledTransportCannotReenterQueuedCleanup()
    {
        QFETCH(bool, control);
        using namespace execution;
        ManualRoutedExecution env;
        int effects{0}, completed{0}, cancelled{0}, retired{0}, cleanup_outcomes{0};
        const auto drain = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        const auto key = env.scheduler->allocateKey();
        const auto cleanup = Submit(*env.scheduler, [&] { ++retired; }, [&] { ++cleanup_outcomes; }, {},
            {Lane::Cleanup, key}, true);
        QVERIFY(cleanup);
        const auto lane = control ? Lane::Control : Lane::Core;
        const auto original = Submit(*env.scheduler, [&] { ++effects; }, [&] { ++completed; },
            [&](std::exception_ptr error) {
                try { std::rethrow_exception(error); }
                catch (const NotStarted& result) { QCOMPARE(result.cause, NotStarted::Cause::Cancelled); ++cancelled; }
            }, {lane, key});
        QVERIFY(original); QVERIFY(env.scheduler->activateCleanup(cleanup.id));
        QVERIFY(!env.step(Lane::Cleanup));
        QVERIFY(env.scheduler->cancelQueued(original.id));
        const auto discard = env.dispatchNext(Lane::Cleanup);
        QCOMPARE(discard, std::make_optional(original.id));
        QVERIFY(env.finishWork(original.id));
        QCOMPARE(cancelled, 0); QCOMPARE(retired, 0);
        // Consume the cancellation before its already queued transport turn.
        // The work-key release queues cleanup, which that old turn must retain.
        QVERIFY(env.step(Lane::Domain)); QCOMPARE(cancelled, 1);
        QCOMPARE(env.scheduler->phase(original.id), std::make_optional(Phase::AwaitingTransport));
        QCOMPARE(env.scheduler->phase(cleanup.id), std::make_optional(Phase::Queued));
        QVERIFY(env.step(lane));
        QCOMPARE(effects, 0); QCOMPARE(completed, 0); QCOMPARE(retired, 0);
        QVERIFY(!env.scheduler->phase(original.id));
        QCOMPARE(env.scheduler->phase(cleanup.id), std::make_optional(Phase::Queued));
        QVERIFY(env.step(Lane::Cleanup)); QCOMPARE(retired, 1); QCOMPARE(cleanup_outcomes, 0);
        QVERIFY(env.step(Lane::Domain)); QCOMPARE(cleanup_outcomes, 1);
        env.drain(); QCOMPARE(cancelled, 1); QVERIFY(env.quiescent());
    }
    void unavailableRetainedValuePortsNeverConstructValues()
    {
        int constructed{0};
        const auto factory = [&] { ++constructed; return std::make_unique<int>(42); };
        QVERIFY(!execution::RetainValues<int>(std::shared_ptr<execution::Scheduler>{}, factory));
        QCOMPARE(constructed, 0);
        ManualRoutedExecution env; int retired{0};
        env.scheduler->closeAdmission();
        const auto held = activityview::HoldValues(env.scheduler, [&] { ++retired; });
        QCOMPARE(held.rejection, execution::Rejection::Closed); QVERIFY(!held);
        QVERIFY(!execution::RetainValues<int>(env.scheduler, factory));
        env.drain(); QCOMPARE(constructed, 0); QCOMPARE(retired, 0); QVERIFY(env.quiescent());
    }
    void rejectedProvisionalBorrowRetiresItsRealLease_data()
    {
        QTest::addColumn<bool>("values");
        QTest::newRow("snapshot-owner") << false; QTest::newRow("value-owner") << true;
    }
    void rejectedProvisionalBorrowRetiresItsRealLease()
    {
        QFETCH(bool, values);
        using namespace execution;
        struct BorrowFault final : Scheduler {
            std::shared_ptr<Scheduler> upstream;
            TaskId cleanup;
            int borrows{0}, activations{0};
            explicit BorrowFault(std::shared_ptr<Scheduler> source) : upstream(std::move(source)) {}
            Admission submit(std::shared_ptr<Task> task, TaskSpec spec) override { return upstream->submit(std::move(task), spec); }
            Admission reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec) override {
                const auto admitted = upstream->reserveCleanup(std::move(task), spec); cleanup = admitted.id; return admitted;
            }
            bool activateCleanup(TaskId id) override { ++activations; return upstream->activateCleanup(id); }
            BorrowAdmission borrow(ResourceKey key) override {
                auto held = upstream->borrow(key);
                if (!held) throw std::logic_error{"The test port needs a real provisional lease"};
                ++borrows; held.rejection = Rejection::Invalid; return held;
            }
            ResourceKey allocateKey() override { return upstream->allocateKey(); }
            size_t pending() const override { return upstream->pending(); }
            void closeOrdinaryAdmission() override { upstream->closeOrdinaryAdmission(); }
            bool cancelQueued(TaskId id) override { return upstream->cancelQueued(id); }
        };
        ManualRoutedExecution env; auto port = std::make_shared<BorrowFault>(env.scheduler);
        int constructed{0};
        const auto drain = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        const auto factory = [&] { ++constructed; return std::make_unique<int>(42); };
        const auto result = values ? RetainValues<int>(port, factory) : RetainSnapshot<int>(port, factory);
        QVERIFY(!result); QCOMPARE(constructed, 0); QCOMPARE(port->borrows, 1); QCOMPARE(port->activations, 1);
        QCOMPARE(env.scheduler->phase(port->cleanup), std::make_optional(Phase::Queued));
        QVERIFY(env.step(Lane::Cleanup)); QVERIFY(env.step(Lane::Domain)); QVERIFY(env.quiescent());
    }
    void staleTransportCannotResolveANewerTask()
    {
        using namespace execution;
        std::deque<std::function<void()>> turns; std::function<void()> old;
        auto queue = std::make_shared<TaskQueue>([&](Lane lane, Phase phase, TaskId, auto turn) {
            if (lane == Lane::Core && phase == Phase::Queued && !old) old = turn;
            turns.push_back(std::move(turn)); return true;
        }, Options{}, 1);
        const auto drain = [&] {
            while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
        };
        int original_effects{0}, original_outcomes{0}, next_effects{0}, next_outcomes{0};
        const auto cleanup = qScopeGuard([&] { queue->closeAdmission(); drain(); old = {}; });
        const auto original = Submit(*queue, [&] { ++original_effects; }, [&] { ++original_outcomes; });
        QVERIFY(original); drain(); QCOMPARE(original_effects, 1); QCOMPARE(original_outcomes, 1);
        QVERIFY(old); QCOMPARE(queue->pending(), size_t{0});
        const auto next = Submit(*queue, [&] { ++next_effects; }, [&] { ++next_outcomes; });
        QVERIFY(next); QVERIFY(next.id != original.id);
        old(); QCOMPARE(original_effects, 1); QCOMPARE(original_outcomes, 1); QCOMPARE(next_effects, 0);
        QCOMPARE(queue->phase(next.id), std::make_optional(Phase::Queued));
        drain(); QCOMPARE(next_effects, 1); QCOMPARE(next_outcomes, 1);
        queue->closeAdmission(); old();
        QCOMPARE(original_effects, 1); QCOMPARE(original_outcomes, 1);
        QCOMPARE(next_effects, 1); QCOMPARE(next_outcomes, 1); QCOMPARE(queue->pending(), size_t{0});
    }
    void discardedObservationFailureDoesNotEraseTheNextSuccess()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int reads{0}, notices{0}; bool fail{true};
        auto query = std::make_shared<SnapshotQuery<int>>(env.scheduler, [&] {
            ++reads; if (fail) throw std::runtime_error{"read unavailable"}; return 42;
        }, [&] { ++notices; });
        query->discardReadFailure(); QVERIFY(!query->take());
        query->request(); env.drain(); QCOMPARE(reads, 1); QCOMPARE(notices, 1);
        query->discardReadFailure(); QVERIFY(!query->take());
        fail = false; query->request(); env.drain(); QCOMPARE(reads, 2); QCOMPARE(notices, 2);
        query->discardReadFailure();
        const auto result = query->take(); QVERIFY(result); QVERIFY(!result->error); QVERIFY(result->snapshot);
        QCOMPARE(*result->snapshot, 42);
        query->close(); query.reset(); env.drain(); QVERIFY(env.quiescent());
    }
    void duplicateOwnedOutcomesKeepTheFirstFactAndPublicationStatus_data()
    {
        QTest::addColumn<bool>("throw_notice");
        QTest::newRow("published") << false; QTest::newRow("publication-failed") << true;
    }
    void duplicateOwnedOutcomesKeepTheFirstFactAndPublicationStatus()
    {
        QFETCH(bool, throw_notice);
        using namespace execution;
        ManualRoutedExecution env; int notices{0};
        auto command = std::make_shared<OwnedCommand<int>>(env.scheduler, [&] {
            ++notices; if (throw_notice) throw std::runtime_error{"publication unavailable"};
        });
        std::function<void(int)> completed; std::function<void(std::exception_ptr)> failed;
        const auto admitted = command->startWith([&](auto done, auto error) {
            completed = std::move(done); failed = std::move(error); return Admission{TaskId{42}, Rejection::None};
        });
        QVERIFY(admitted); QVERIFY(!command->result());
        completed(7); failed(std::make_exception_ptr(std::runtime_error{"late error"})); completed(9);
        const auto result = command->result(); QVERIFY(result); QVERIFY(result->value); QVERIFY(!result->error);
        QCOMPARE(*result->value, 7); QCOMPARE(result->publication_failed, throw_notice); QCOMPARE(notices, 1);
        completed = {}; failed = {}; command.reset(); env.drain(); QVERIFY(env.quiescent());
    }
    void feeReceiptKeepsConcurrentNativeFactsIndependent()
    {
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        int reads{0}, fee_observed{0}, facts_observed{0}, outcome_value{0};
        QSemaphore start, posted;
        std::thread native{[&] {
            start.acquire();
            receipts->post([&] { ++facts_observed; });
            posted.release();
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            receipts->drain(); receipts->publishAll(); env->publishAll();
        });
        const auto original = execution::Submit(*env->scheduler, [&] {
            receipts->readStarted(); ++reads; return 42;
        }, [&](int value) {
            outcome_value = value;
            start.release(); posted.acquire(); // Native posts inside the actual fee outcome window.
            receipts->post([&] { ++fee_observed; });
        });
        QVERIFY(original); QCOMPARE(reads, 0);
        QCOMPARE(receipts->startRead(), std::make_optional(original.id));
        QCOMPARE(reads, 1); QCOMPARE(fee_observed, 0); QCOMPARE(facts_observed, 0);
        QVERIFY(receipts->finishOutcome(original.id));
        start.release(); // Cleanup remains possible even if a broken outcome was not invoked.
        native.join();
        QCOMPARE(outcome_value, 42);
        QCOMPARE(receipts->publicationCount(), size_t{1});
        QCOMPARE(env->publicationCount(), size_t{1});
        QVERIFY(!env->scheduler->phase(original.id));
        env->publishAll(); QCOMPARE(facts_observed, 1); QCOMPARE(fee_observed, 0);
        QVERIFY(receipts->publish(original.id)); QVERIFY(!receipts->publish(original.id));
        QCOMPARE(fee_observed, 1); QCOMPARE(facts_observed, 1);
        QVERIFY(env->quiescent());
    }
    void manualDuplicatePublicationCopiesMutableCapture()
    {
        ManualExecutionEnvironment env;
        std::vector<int> observed;
        const auto cleanup = qScopeGuard([&] { env.stop(); env.drain(); env.publishAll(); });
        env.post([state = 0, &observed]() mutable { observed.push_back(++state); });
        QVERIFY(env.duplicateFirstPublication());
        QCOMPARE(env.publicationCount(), size_t{2});
        QVERIFY(observed.empty());
        env.publishAll();
        QCOMPARE(observed, std::vector<int>({1, 1}));
        QVERIFY(env.quiescent());
    }
    void routedNativeCleanupIngressKeepsOriginalManualOutcome()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int retired{0}, outcomes{0};
        const auto key = env.scheduler->allocateKey();
        const auto original = Submit(*env.scheduler, [&] { ++retired; }, [&] { ++outcomes; }, {},
            {Lane::Cleanup, key, WorkClass::Action}, true);
        QVERIFY(original);
        auto borrowed = env.scheduler->borrow(key);
        QVERIFY(borrowed);
        QVERIFY(env.scheduler->activateCleanup(original.id));
        QVERIFY(env.queuedIdle());
        QSemaphore start;
        std::atomic_bool finished{false};
        std::thread native{[&, lease = std::move(borrowed.lease)]() mutable {
            start.acquire();
            lease.reset(); // Actual Native final borrower dispatches Cleanup.
            finished = true;
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            env.drain();
        });
        start.release();
        QVERIFY(QTest::qWaitFor([&] {
            (void)env.traceSnapshot();
            (void)env.queuedCount();
            (void)env.activeIdle();
            (void)env.quiescent();
            return finished.load();
        }, 3000));
        native.join();
        QCOMPARE(retired, 0);
        QCOMPARE(outcomes, 0);
        QCOMPARE(env.queuedCountIf([&](const auto& turn) {
            (void)env.traceSnapshot(); // Inspection predicate is outside mutex.
            return turn.id == original.id && turn.lane == Lane::Cleanup;
        }), 1);
        const auto dispatched = env.dispatchNext(Lane::Cleanup);
        QCOMPARE(dispatched, std::make_optional(original.id));
        QVERIFY(!env.activeIdle());
        QCOMPARE(retired, 0);
        QVERIFY(env.finishWork(original.id));
        QCOMPARE(retired, 1);
        QCOMPARE(outcomes, 0);
        const auto outcome = env.deliverOutcome();
        QCOMPARE(outcome, std::make_optional(original.id));
        QVERIFY(env.stepMailbox(original.id));
        QCOMPARE(outcomes, 1);
        QVERIFY(env.quiescent());
    }
    void manualTransportSmallCallableDestructionMayReenter()
    {
        ManualExecutionEnvironment env;
        struct Probe {
            ManualExecutionEnvironment* environment;
            bool armed{false}, queued_phase{false}, busy_during_queued_destruction{false};
            int copies{0}, destroyed{0}, callbacks{0}, observed{0};
            void reenter() {
                (void)environment->publicationsIdle();
                if (queued_phase && !environment->transportIdle()) busy_during_queued_destruction = true;
                environment->post([this] { ++observed; });
            }
        } probe{&env};
        const auto cleanup = qScopeGuard([&] {
            probe.armed = false;
            env.stop(); env.drain(); env.publishAll();
        });
        struct SmallCallable {
            Probe* probe;
            explicit SmallCallable(Probe* value) : probe(value) {}
            SmallCallable(const SmallCallable& other) noexcept : probe(other.probe) {
                if (probe->armed) { ++probe->copies; probe->reenter(); }
            }
            void operator()() const { ++probe->callbacks; }
            ~SmallCallable() { if (probe->armed) { ++probe->destroyed; probe->reenter(); } }
        };
        env.restart(SmallCallable{&probe});
        probe.armed = true; // Client copy/destructor hooks arm after registration.
        env.restart([] {}); // Replacement retires the boxed callable outside lock.
        QVERIFY(probe.destroyed > 0);
        env.stop();
        probe.armed = false;
        env.restart(SmallCallable{&probe});
        probe.armed = true;
        const auto before_stop = probe.destroyed;
        env.stop();
        QVERIFY(probe.destroyed > before_stop);
        env.publishAll();
        QCOMPARE(probe.callbacks, 0);
        QVERIFY(env.quiescent());
        probe.armed = false;
        env.post(SmallCallable{&probe});
        probe.armed = true;
        probe.queued_phase = true;
        QVERIFY(env.publishNext()); // Extract/invoke/destroy queued small callable.
        QCOMPARE(probe.callbacks, 1);
        QVERIFY(probe.busy_during_queued_destruction);
        probe.queued_phase = false;
        env.publishAll();
        QVERIFY(probe.observed > 0);
        QVERIFY(env.quiescent());
        env.restart({});
        QVERIFY(env.transportIdle());
        env.advance(std::chrono::milliseconds{250});
        QCOMPARE(probe.callbacks, 1);
        QVERIFY(env.quiescent());
    }
    void manualTransportNativeIngressKeepsDeliveryControlled()
    {
        ManualExecutionEnvironment env;
        std::atomic<int> effects{0}, outcomes{0}, publications{0};
        std::atomic_bool finished{false}, rejected{false};
        QSemaphore start;
        std::thread native{[&] {
            start.acquire();
            for (int index = 0; index < 8; ++index) {
                const auto admitted = execution::Submit(*env.scheduler,
                    [&] { ++effects; }, [&] { ++outcomes; });
                if (!admitted) rejected = true;
                env.post([&] { ++publications; });
            }
            finished = true;
        }};
        const auto cleanup = qScopeGuard([&] {
            start.release();
            if (native.joinable()) native.join();
            env.drain(); env.publishAll();
        });
        start.release();
        QVERIFY(QTest::qWaitFor([&] {
            (void)env.trace();
            (void)env.queuedTurnCount();
            (void)env.publicationCount();
            (void)env.transportIdle();
            (void)env.quiescent();
            return finished.load();
        }, 3000));
        native.join();
        QVERIFY(!rejected);
        QCOMPARE(effects.load(), 0);
        QCOMPARE(outcomes.load(), 0);
        QCOMPARE(publications.load(), 0);
        QCOMPARE(env.publicationCount(), size_t{8});
        QVERIFY(!env.quiescent());
        env.drain();
        QCOMPARE(effects.load(), 8);
        QCOMPARE(outcomes.load(), 8);
        QCOMPARE(publications.load(), 0);
        QVERIFY(env.turnsIdle());
        env.publishAll();
        QCOMPARE(publications.load(), 8);
        QVERIFY(env.quiescent());
    }
    void manualTransportRetiredTimerCaptureMayReenter()
    {
        ManualExecutionEnvironment env;
        int destroyed{0}, published{0};
        const auto cleanup = qScopeGuard([&] { env.stop(); env.drain(); env.publishAll(); });
        auto capture = std::shared_ptr<int>{new int{0}, [&](int* value) {
            delete value;
            ++destroyed;
            (void)env.trace();
            env.post([&] { ++published; });
        }};
        env.restart([capture] {});
        capture.reset();
        env.stop(); // Last capture destructor must run outside transport mutex.
        QCOMPARE(destroyed, 1);
        QCOMPARE(published, 0);
        QCOMPARE(env.publicationCount(), size_t{1});
        env.publishAll();
        QCOMPARE(published, 1);
        QVERIFY(env.quiescent());
        int expired{0};
        env.restart([&] { ++expired; env.post([&] { ++published; }); });
        env.advance(std::chrono::milliseconds{249});
        QCOMPARE(expired, 0);
        env.advance(std::chrono::milliseconds{1});
        QCOMPARE(expired, 1);
        QCOMPARE(published, 1);
        env.publishAll();
        QCOMPARE(published, 2);
        QVERIFY(env.quiescent());
    }
    void coalescedRequestDuringRejectedAdmissionDoesNotRetryInline()
    {
        using namespace execution;
        ManualRoutedExecution env;
        struct AdmissionPort final : Scheduler {
            std::shared_ptr<Scheduler> upstream;
            std::function<void()> while_rejecting;
            bool rejecting{true};
            int attempts{0};
            Admission submit(std::shared_ptr<Task> task, TaskSpec spec) override {
                ++attempts;
                if (rejecting) { while_rejecting(); return {{}, Rejection::Capacity}; }
                return upstream->submit(std::move(task), spec);
            }
            Admission reserveCleanup(std::shared_ptr<Task> task, TaskSpec spec) override { return upstream->reserveCleanup(std::move(task), spec); }
            bool activateCleanup(TaskId id) override { return upstream->activateCleanup(id); }
            BorrowAdmission borrow(ResourceKey key) override { return upstream->borrow(key); }
            ResourceKey allocateKey() override { return upstream->allocateKey(); }
            size_t pending() const override { return upstream->pending(); }
            void closeOrdinaryAdmission() override { upstream->closeOrdinaryAdmission(); }
            bool cancelQueued(TaskId id) override { return upstream->cancelQueued(id); }
        };
        auto port = std::make_shared<AdmissionPort>(); port->upstream = env.scheduler;
        int reads{0}, notices{0};
        auto query = std::make_shared<SnapshotQuery<int>>(port, [&] { ++reads; return 42; }, [&] { ++notices; });
        port->while_rejecting = [query] { query->request(); };
        query->request();
        QCOMPARE(port->attempts, 1); QCOMPARE(reads, 0); QCOMPARE(notices, 1);
        const auto rejected = query->take(); QVERIFY(rejected); QVERIFY(rejected->error); QVERIFY(!rejected->snapshot);
        QVERIFY(env.quiescent());
        port->rejecting = false;
        query->request(); QCOMPARE(port->attempts, 2); QCOMPARE(reads, 0);
        env.drain(); QCOMPARE(reads, 1); QCOMPARE(notices, 2);
        const auto retried = query->take(); QVERIFY(retried); QVERIFY(!retried->error); QVERIFY(retried->snapshot);
        QCOMPARE(*retried->snapshot, 42);
        query->close(); port->while_rejecting = {}; query.reset(); env.drain(); QVERIFY(env.quiescent());
    }

    void controlSnapshotRunsWhileOrdinaryObservationIsHeld()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int ordinary_reads{0}, control_reads{0};
        std::deque<std::function<void()>> gui;
        auto ordinary = std::make_shared<SnapshotQuery<int>>(env.scheduler,
            [&] { ++ordinary_reads; return 7; }, [] {});
        std::optional<SnapshotQuery<int>::Result> published;
        std::shared_ptr<SnapshotQuery<int>> control;
        control = std::make_shared<SnapshotQuery<int>>(env.scheduler,
            [&] { ++control_reads; return 42; },
            [&] { gui.emplace_back([&] { published = control->take(); }); }, Lane::Control);
        ordinary->request(); control->request();
        QCOMPARE(ordinary_reads, 0); QCOMPARE(control_reads, 0); QVERIFY(!published);
        QVERIFY(env.step(Lane::Control)); QCOMPARE(control_reads, 1); QCOMPARE(ordinary_reads, 0);
        QVERIFY(!published); QVERIFY(gui.empty());
        QVERIFY(env.step(Lane::Domain)); QVERIFY(!published); QCOMPARE(gui.size(), size_t{1});
        auto publication = std::move(gui.front()); gui.pop_front(); publication();
        QVERIFY(published); QVERIFY(published->snapshot); QVERIFY(!published->error); QCOMPARE(*published->snapshot, 42);
        QCOMPARE(ordinary_reads, 0);
        ordinary->close(); control->close(); env.drain();
        QCOMPARE(ordinary_reads, 0); QCOMPARE(control_reads, 1); QVERIFY(env.quiescent());
    }

    void admittedActionCompletesDependentStagesAfterFullClose()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        int effects{0}; std::shared_ptr<int> observed;
        const auto first = Submit(*scope, [scope, &effects, &observed] {
            auto output = RetainSnapshot<int>(scope, [] { return std::make_unique<int>(0); });
            if (!output) throw std::runtime_error{"Original result ownership rejected"};
            const auto next = Submit(*scope, [output, &effects] { ++effects; *output = 42; return output; },
                [&observed](auto result) { observed = std::move(result); });
            if (!next) throw std::runtime_error{"Original Native stage rejected"};
        }, [] {}, {}, {Lane::Domain});
        QVERIFY(first); env.scheduler->closeAdmission();
        QVERIFY(!env.scheduler->openActionScope());
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}).rejection, Rejection::Closed);
        QVERIFY(env.step(Lane::Domain)); QCOMPARE(effects, 0);
        QVERIFY(env.step(Lane::Core)); QCOMPARE(effects, 1); QVERIFY(!observed);
        env.drain(); QVERIFY(observed); QCOMPARE(*observed, 42);
        scope.reset(); env.drain(); QVERIFY(!env.quiescent()); // Original value owns its reserved cleanup.
        observed.reset(); env.drain(); QVERIFY(env.quiescent());
    }

    void scopedCleanupCannotRunBeforeItsOriginalBorrow()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        env.scheduler->closeAdmission();
        const auto key = scope->allocateKey(); int retired{0};
        const auto cleanup = Submit(*scope, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, key}, true);
        QVERIFY(cleanup); QVERIFY(!env.step(Lane::Cleanup));
        auto held = scope->borrow(key); QVERIFY(held);
        QVERIFY(scope->activateCleanup(cleanup.id));
        scope.reset(); QVERIFY(!env.step(Lane::Cleanup));
        held.lease.reset(); QVERIFY(env.step(Lane::Cleanup)); QCOMPARE(retired, 1);
        env.drain(); QVERIFY(env.quiescent());
    }

    void actionScopesKeepOriginalResourceIdentity()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto first = env.scheduler->openActionScope(), second = env.scheduler->openActionScope();
        QVERIFY(first && second);
        QVERIFY(!first->openActionScope());
        int effects{0}; const auto original = Submit(*first, [&] { ++effects; }, [] {}, {}, {Lane::Core}); QVERIFY(original);
        QVERIFY(!second->cancelQueued(original.id));
        QCOMPARE(Submit(*first, [] {}, [] {}, {}, {Lane::Control}).rejection, Rejection::Invalid);
        const auto key = first->allocateKey();
        const auto cleanup = Submit(*first, [] {}, [] {}, {}, {Lane::Cleanup, key}, true); QVERIFY(cleanup);
        QCOMPARE(second->borrow(key).rejection, Rejection::Invalid);
        QVERIFY(!second->activateCleanup(cleanup.id));
        auto held = first->borrow(key); QVERIFY(held); QVERIFY(first->activateCleanup(cleanup.id));
        env.scheduler->closeOrdinaryAdmission();
        env.drain(); QCOMPARE(effects, 1);
        QVERIFY(Submit(*second, [&] { ++effects; }, [] {}, {}, {Lane::Core}));
        first->closeOrdinaryAdmission(); QCOMPARE(Submit(*first, [] {}, [] {}).rejection, Rejection::Closed);
        held.lease.reset(); first.reset(); second.reset(); env.drain();
        QCOMPARE(effects, 2); QVERIFY(env.quiescent());
    }

    void releasingScopeRetiresUnrequestedCleanupInBoundedBatches()
    {
        using namespace execution;
        ManualRoutedExecution env;
        auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        int retired{0};
        for (int index = 0; index < 257; ++index) {
            QVERIFY(Submit(*scope, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, scope->allocateKey()}, true));
        }
        env.scheduler->closeAdmission(); QVERIFY(env.queuedIdle());
        scope.reset(); QCOMPARE(env.queuedCount(), size_t{16}); QCOMPARE(retired, 0);
        env.drain(); QCOMPARE(retired, 257); QVERIFY(env.quiescent());
    }

    void implicitScopedRetirementRejectsLaterWorkForItsResource()
    {
        using namespace execution;
        ManualRoutedExecution env; auto scope = env.scheduler->openActionScope(); QVERIFY(scope);
        const auto key = scope->allocateKey(); int retired{0}, touched_after_retirement{0};
        QVERIFY(Submit(*scope, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, key}, true));
        scope.reset(); QCOMPARE(env.queuedCount(), size_t{1});
        QCOMPARE(Submit(*env.scheduler, [&] { ++touched_after_retirement; }, [] {}, {}, {Lane::Core, key}).rejection, Rejection::Closed);
        env.drain(); QCOMPARE(retired, 1); QCOMPARE(touched_after_retirement, 0); QVERIFY(env.quiescent());
    }

    void closingLargeIdleInventoryDispatchesBoundedTurns()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int retired{0};
        for (int index = 0; index < 257; ++index) {
            QVERIFY(Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, env.scheduler->allocateKey()}, true));
        }
        QVERIFY(env.queuedIdle());
        env.scheduler->closeAdmission();
        QCOMPARE(env.queuedCount(), size_t{16});
        QCOMPARE(retired, 0);
        env.drain(); QCOMPARE(retired, 257); QVERIFY(env.quiescent());
    }

    void invalidRoutedConfigurationIsRejected_data()
    {
        QTest::addColumn<int>("invalid");
        QTest::newRow("missing-transport") << 0;
        QTest::newRow("zero-core-slots") << 1;
        QTest::newRow("zero-action-burst") << 2;
    }
    void invalidRoutedConfigurationIsRejected()
    {
        QFETCH(int, invalid);
        using namespace execution;
        TaskQueue::RoutedDispatch dispatch = [](Lane, Phase, TaskId, auto) { return true; };
        auto limits = Options{};
        if (invalid == 0) dispatch = {};
        if (invalid == 2) limits.actions_before_observation = 0;
        bool rejected{false};
        try { TaskQueue queue{dispatch, limits, invalid == 1 ? size_t{0} : size_t{1}}; }
        catch (const std::invalid_argument&) { rejected = true; }
        QVERIFY(rejected);
    }
    void cancellingWaitingKeysAndControlOwnsEveryOutcome()
    {
        using namespace execution;
        ManualRoutedExecution env;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        int effects{0}, cancelled{0};
        const auto failure = [&](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const NotStarted& stopped) { if (stopped.cause == NotStarted::Cause::Cancelled) ++cancelled; }
        };
        const auto first = Submit(*env.scheduler, [&] { ++effects; }, [] {}, {}, {Lane::Core, {1, 1}});
        QVERIFY(first);
        const auto sibling = Submit(*env.scheduler, [&] { ++effects; }, [] {}, failure, {Lane::Core, {1, 1}});
        const auto waiting = Submit(*env.scheduler, [&] { ++effects; }, [] {}, failure, {Lane::Core, {2, 1}});
        const auto control = Submit(*env.scheduler, [&] { ++effects; }, [] {}, failure, {Lane::Control});
        QVERIFY(sibling && waiting && control);
        QVERIFY(env.scheduler->cancelQueued(sibling.id));
        QVERIFY(env.scheduler->cancelQueued(waiting.id));
        QVERIFY(env.scheduler->cancelQueued(control.id));
        QVERIFY(!env.scheduler->cancelQueued(control.id));
        env.drain();
        QCOMPARE(effects, 1); QCOMPARE(cancelled, 3);
        QVERIFY(env.quiescent());
        QVERIFY(Submit(*env.scheduler, [&] { ++effects; }, [] {}, {}, {Lane::Core, {2, 1}}));
        env.drain(); QCOMPARE(effects, 2);
    }
    void compatibilityCancellationAndReservedCleanupRemainOwned()
    {
        using namespace execution;
        std::deque<std::function<void()>> turns;
        auto queue = std::make_shared<TaskQueue>([&](Phase, TaskId, std::function<void()> turn) {
            turns.push_back(std::move(turn)); return true;
        });
        const auto drain = [&] { while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); } };
        const auto cleanup = qScopeGuard([&] { queue->closeAdmission(); drain(); });
        int effects{0}, failures{0}, retired{0};
        const auto work = Submit(*queue, [&] { ++effects; }, [] {}, [&](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const NotStarted& stopped) {
                QCOMPARE(stopped.cause, NotStarted::Cause::Cancelled);
                QCOMPARE(QString::fromLatin1(stopped.what()), QStringLiteral("Task did not start"));
                ++failures;
            }
        });
        QVERIFY(work); QVERIFY(queue->cancelQueued(work.id));
        QVERIFY(!queue->cancelQueued(work.id));
        drain(); QCOMPARE(effects, 0); QCOMPARE(failures, 1);
        const auto key = queue->allocateKey();
        const auto reserved = Submit(*queue, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, key}, true);
        QVERIFY(reserved); QVERIFY(queue->activateCleanup(reserved.id));
        drain(); QCOMPARE(retired, 1); QCOMPARE(queue->pending(), size_t{0});
    }
    void cancellingOneWaitingTaskPreservesItsSameKeySuccessor()
    {
        using namespace execution;
        ManualRoutedExecution env;
        const auto drain = qScopeGuard([&] { env.drain(); });
        std::vector<int> effects, outcomes;
        const auto first = Submit(*env.scheduler, [&] { effects.push_back(1); return 1; }, [&](int n) { outcomes.push_back(n); }, {}, {Lane::Core, {1, 1}});
        const auto second = Submit(*env.scheduler, [&] { effects.push_back(2); }, [] {}, [&](auto) { outcomes.push_back(-2); }, {Lane::Core, {1, 1}});
        const auto third = Submit(*env.scheduler, [&] { effects.push_back(3); return 3; }, [&](int n) { outcomes.push_back(n); }, {}, {Lane::Core, {1, 1}});
        QVERIFY(first && second && third); QVERIFY(env.scheduler->cancelQueued(second.id));
        env.drain();
        QCOMPARE(effects, std::vector<int>({1, 3}));
        QCOMPARE(outcomes.size(), size_t{3});
        QCOMPARE(std::count(outcomes.begin(), outcomes.end(), -2), 1);
        QCOMPARE(std::count(outcomes.begin(), outcomes.end(), 1), 1);
        QCOMPARE(std::count(outcomes.begin(), outcomes.end(), 3), 1);
        QVERIFY(env.quiescent());
    }
    void cancelledTransportLetsResourceCleanupProgress()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int effects{0}, destroyed{0}, failures{0}, retired{0};
        const auto cleanup = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        const auto key = env.scheduler->allocateKey();
        const auto reservation = Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, key}, true);
        QVERIFY(reservation);
        struct Resource {
            int& destroyed;
            explicit Resource(int& count) : destroyed(count) {}
            ~Resource() { ++destroyed; }
        };
        const auto work = Submit(*env.scheduler, [owned = std::make_unique<Resource>(destroyed), &effects] { ++effects; }, [] {},
            [&](std::exception_ptr error) {
                try { std::rethrow_exception(error); }
                catch (const NotStarted& failure) { if (failure.cause == NotStarted::Cause::Cancelled) ++failures; }
            }, {Lane::Core, key});
        QVERIFY(work); QVERIFY(env.scheduler->cancelQueued(work.id));
        QVERIFY(env.step(Lane::Cleanup));
        QCOMPARE(destroyed, 1); QCOMPARE(effects, 0);
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(failures, 1);
        QCOMPARE(env.scheduler->phase(work.id), std::make_optional(Phase::AwaitingTransport));
        QVERIFY(env.scheduler->activateCleanup(reservation.id));
        QVERIFY(env.step(Lane::Cleanup)); QVERIFY(env.step(Lane::Domain));
        QCOMPARE(retired, 1); QCOMPARE(env.scheduler->pending(), size_t{1});
        QVERIFY(!env.scheduler->cancelQueued(work.id));
        QVERIFY(env.step(Lane::Core)); // Dequeue the original now-empty turn.
        QCOMPARE(effects, 0); QVERIFY(env.quiescent());
        QVERIFY(Submit(*env.scheduler, [] {}, [] {})); env.drain();
        QVERIFY(env.quiescent());
    }
    void reservedCleanupWaitsForOutcomesAndNativeLeases()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int effects{0}, outcomes{0}, cleanup_effects{0}, cleanup_outcomes{0};
        const auto cleanup = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        const auto key = env.scheduler->allocateKey();
        const auto reserved = Submit(*env.scheduler, [&] { ++cleanup_effects; }, [&] { ++cleanup_outcomes; }, {}, {Lane::Cleanup, key}, true);
        QVERIFY(reserved);
        QCOMPARE(env.scheduler->phase(reserved.id), std::make_optional(Phase::Reserved));
        QVERIFY(env.queuedIdle());
        QVERIFY(!BorrowAdmission{});
        QCOMPARE(env.scheduler->borrow({999, 1}).rejection, Rejection::Invalid);
        auto borrowed = env.scheduler->borrow(key);
        QVERIFY(borrowed);
        std::optional<ResourceLease> moved{std::move(*borrowed.lease)};
        borrowed.lease.reset();
        QCOMPARE(env.scheduler->pending(), size_t{2});
        const auto work = Submit(*env.scheduler, [&] { ++effects; }, [&] { ++outcomes; }, {}, {Lane::Core, key});
        QVERIFY(work);
        QVERIFY(env.scheduler->activateCleanup(reserved.id));
        QVERIFY(!env.scheduler->activateCleanup(reserved.id));
        QVERIFY(!env.scheduler->cancelQueued(reserved.id));
        QCOMPARE(env.scheduler->borrow(key).rejection, Rejection::Closed);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Core, key}).rejection, Rejection::Closed);
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(effects, 1); QCOMPARE(outcomes, 0);
        moved.reset();
        QVERIFY(!env.step(Lane::Cleanup)); // Produced result is not yet consumed.
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(outcomes, 1);
        QVERIFY(!env.scheduler->cancelQueued(reserved.id));
        QVERIFY(env.step(Lane::Cleanup));
        QVERIFY(!env.scheduler->activateCleanup(reserved.id));
        QCOMPARE(cleanup_effects, 1); QCOMPARE(cleanup_outcomes, 0);
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(cleanup_outcomes, 1);
        QVERIFY(env.quiescent());
        QCOMPARE(env.scheduler->borrow(key).rejection, Rejection::Invalid);
    }
    void parentClosureActivatesCleanupWithoutAnotherAdmission()
    {
        using namespace execution;
        ManualRoutedExecution env;
        int retired{0};
        const auto cleanup = qScopeGuard([&] { env.scheduler->closeAdmission(); env.drain(); });
        const auto first = env.scheduler->allocateKey(), second = env.scheduler->allocateKey();
        QVERIFY(first != second);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Core, first}, true).rejection, Rejection::Invalid);
        const auto reserved = Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup, first}, true);
        QVERIFY(reserved);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Cleanup, first}, true).rejection, Rejection::Invalid);
        auto borrowed = env.scheduler->borrow(first);
        QVERIFY(borrowed);
        env.scheduler->closeOrdinaryAdmission();
        QCOMPARE(env.scheduler->borrow(first).rejection, Rejection::Closed);
        env.scheduler->closeAdmission();
        QCOMPARE(env.scheduler->borrow(first).rejection, Rejection::Closed);
        QVERIFY(!env.step(Lane::Cleanup));
        borrowed.lease.reset();
        env.drain();
        QCOMPARE(retired, 1);
        QVERIFY(env.quiescent());
        QVERIFY(!env.scheduler->activateCleanup(reserved.id));

        auto limits = Options{};
        limits.first_resource_owner = std::numeric_limits<uint64_t>::max();
        ManualRoutedExecution exhausted{limits};
        QCOMPARE(exhausted.scheduler->allocateKey().owner, std::numeric_limits<uint64_t>::max());
        bool rejected{false};
        try { exhausted.scheduler->allocateKey(); }
        catch (const std::overflow_error&) { rejected = true; }
        QVERIFY(rejected);
        QVERIFY(exhausted.quiescent());
    }
    void subscriptionDecisionsDoNotResurrectAfterRetirement()
    {
        using namespace walletlife;
        State state;
        QCOMPARE(state.phase(), Phase::Registering);
        QVERIFY(!state.apply(Event::Notification));
        QVERIFY(!state.apply(Event::CleanupFinished));
        QCOMPARE(state.apply(Event::Registered), std::make_optional(Publication::Ready));
        QCOMPARE(state.phase(), Phase::Active);
        QVERIFY(!state.apply(Event::Registered));
        QCOMPARE(state.apply(Event::Notification), std::make_optional(Publication::Changed));
        QCOMPARE(state.apply(Event::RegistrationFailed), std::make_optional(Publication::Failed));
        QCOMPARE(state.phase(), Phase::Failed);
        QVERIFY(!state.apply(Event::Notification));
        QVERIFY(!state.apply(Event::RegistrationFailed));
        state.apply(Event::Close);
        QCOMPARE(state.phase(), Phase::Closing);
        QVERIFY(!state.apply(Event::Registered));
        QVERIFY(!state.apply(Event::RegistrationFailed));
        QCOMPARE(state.apply(Event::CleanupFinished), std::make_optional(Publication::Retired));
        QCOMPARE(state.phase(), Phase::Closed);
        for (auto event : {Event::Registered, Event::RegistrationFailed, Event::Notification, Event::Close, Event::CleanupFinished, static_cast<Event>(255)}) {
            QVERIFY(!state.apply(event));
            QCOMPARE(state.phase(), Phase::Closed);
        }
        State closed_during_registration;
        closed_during_registration.apply(Event::Close);
        QVERIFY(!closed_during_registration.apply(Event::Registered));
        QVERIFY(!closed_during_registration.apply(Event::Close));
        QCOMPARE(closed_during_registration.apply(Event::CleanupFinished), std::make_optional(Publication::Retired));
        State failed_registration;
        QCOMPARE(failed_registration.apply(Event::RegistrationFailed), std::make_optional(Publication::Failed));
        QCOMPARE(failed_registration.phase(), Phase::Failed);
        State closed_active;
        closed_active.apply(Event::Registered); closed_active.apply(Event::Close);
        QCOMPARE(closed_active.phase(), Phase::Closing);
    }
    void observationsAndActionsBothProgress()
    {
        using namespace execution;
        ManualRoutedExecution env{Options{2}};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        std::vector<int> effects;
        const auto submit = [&](int value, WorkClass kind) {
            return Submit(*env.scheduler, [&, value] { effects.push_back(value); }, [] {}, {},
                          {Lane::Core, {1, static_cast<uint64_t>(value)}, kind});
        };
        QVERIFY(submit(1, WorkClass::Action));
        QVERIFY(submit(9, WorkClass::Observation));
        QVERIFY(submit(2, WorkClass::Action));
        QVERIFY(submit(3, WorkClass::Action));
        QVERIFY(submit(4, WorkClass::Action));
        while (env.step(Lane::Core)) {}
        QCOMPARE(effects, std::vector<int>({1, 2, 9, 3, 4}));
        env.drain();
        QVERIFY(env.quiescent());
        env.rejectLane(Lane::Core);
        QCOMPARE(submit(11, WorkClass::Observation).rejection, Rejection::Transport);
        QVERIFY(submit(12, WorkClass::Observation));
        env.drain();
        QCOMPARE(effects.back(), 12);
        QVERIFY(env.quiescent());
        QCOMPARE(submit(13, static_cast<WorkClass>(255)).rejection, Rejection::Invalid);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}, {}, {Lane::Control, {}, WorkClass::Observation}).rejection, Rejection::Invalid);
    }
    void keyedWorkIsFairAndOutcomeConsumptionIsSeparate()
    {
        using namespace execution;
        ManualRoutedExecution env;
        std::vector<int> effects, outcomes;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        const auto submit = [&](ResourceKey key, int value) {
            return Submit(*env.scheduler, [&, value] { effects.push_back(value); return value * 10; },
                          [&](int result) { outcomes.push_back(result); }, {}, {Lane::Core, key});
        };
        QVERIFY(submit({1, 1}, 1));
        QVERIFY(submit({1, 1}, 2));
        QVERIFY(submit({2, 1}, 3));
        QVERIFY(submit({3, 1}, 4));
        const auto first = env.dispatchNext(Lane::Core);
        QVERIFY(first);
        QVERIFY(effects.empty());
        QVERIFY(!env.dispatchNext(Lane::Core));
        QVERIFY(env.finishWork(*first));
        while (env.step(Lane::Core)) {}
        QCOMPARE(effects, std::vector<int>({1, 3, 4, 2}));
        QVERIFY(outcomes.empty());
        QCOMPARE(env.scheduler->pending(), size_t{4});
        const auto outcome = env.deliverOutcome();
        QVERIFY(outcome);
        QVERIFY(outcomes.empty());
        QVERIFY(env.stepMailbox(*outcome));
        QCOMPARE(outcomes, std::vector<int>({10}));
        env.drain();
        QCOMPARE(outcomes, std::vector<int>({10, 30, 40, 20}));
        QVERIFY(env.quiescent());
        QVERIFY(!env.finishWork({999}));
        QVERIFY(!env.stepMailbox({999}));
        QVERIFY(!env.deliverOutcome());
    }
    void independentKeysCanFinishOutOfOrderWhileControlRemainsAvailable()
    {
        using namespace execution;
        ManualRoutedExecution env{Options{}, 2};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        std::vector<int> effects;
        int outcomes{0}, control{0}, retired{0};
        const auto submit = [&](ResourceKey key, int value) {
            return Submit(*env.scheduler, [&, value] { effects.push_back(value); }, [&] { ++outcomes; }, {}, {Lane::Core, key});
        };
        const auto a = submit({1, 1}, 1), a2 = submit({1, 1}, 2), b = submit({2, 1}, 3);
        QVERIFY(a && a2 && b);
        QCOMPARE(env.dispatchNext(Lane::Core), std::make_optional(a.id));
        QCOMPARE(env.dispatchNext(Lane::Core), std::make_optional(b.id));
        QVERIFY(!env.dispatchNext(Lane::Core));
        env.scheduler->closeOrdinaryAdmission();
        QVERIFY(Submit(*env.scheduler, [&] { ++control; }, [] {}, {}, {Lane::Control}));
        QVERIFY(Submit(*env.scheduler, [&] { ++retired; }, [] {}, {}, {Lane::Cleanup}));
        QVERIFY(env.step(Lane::Control));
        QVERIFY(env.step(Lane::Cleanup));
        QCOMPARE(control, 1); QCOMPARE(retired, 1);
        QVERIFY(effects.empty());
        QVERIFY(env.finishWork(b.id));
        QVERIFY(!env.dispatchNext(Lane::Core)); // Same key still held by A.
        QVERIFY(env.finishWork(a.id));
        QCOMPARE(env.dispatchNext(Lane::Core), std::make_optional(a2.id));
        QVERIFY(env.finishWork(a2.id));
        QCOMPARE(effects, std::vector<int>({3, 1, 2}));
        QCOMPARE(outcomes, 0);
        env.drain();
        QCOMPARE(outcomes, 3);
        QVERIFY(env.quiescent());
    }
    void queuedCancellationAndLaterTransportFailureHaveOwnedOutcomes()
    {
        using namespace execution;
        ManualRoutedExecution env;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        int effects{0}, outcomes{0};
        std::vector<NotStarted::Cause> failures;
        const auto failed = [&](std::exception_ptr error) {
            try { std::rethrow_exception(error); }
            catch (const NotStarted& result) { failures.push_back(result.cause); }
        };
        const auto a = Submit(*env.scheduler, [&] { ++effects; }, [&] { ++outcomes; }, failed);
        const auto b = Submit(*env.scheduler, [&] { ++effects; }, [&] { ++outcomes; }, failed);
        QVERIFY(a && b);
        env.rejectLane(Lane::Core);
        QVERIFY(env.scheduler->cancelQueued(a.id));
        QVERIFY(!env.scheduler->cancelQueued(a.id));
        QVERIFY(!env.scheduler->cancelQueued({999}));
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(effects, 0);
        QCOMPARE(env.scheduler->phase(b.id), std::make_optional(Phase::OutcomeReady));
        QVERIFY(!env.scheduler->cancelQueued(b.id));
        QVERIFY(env.step(Lane::Cleanup));
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(failures, std::vector<NotStarted::Cause>({NotStarted::Cause::Cancelled}));
        QVERIFY(env.step(Lane::Cleanup));
        QVERIFY(env.step(Lane::Domain));
        QCOMPARE(failures, std::vector<NotStarted::Cause>({NotStarted::Cause::Cancelled, NotStarted::Cause::Transport}));
        QCOMPARE(outcomes, 0);
        QVERIFY(env.quiescent());
        env.rejectLane(Lane::Core);
        QCOMPARE(Submit(*env.scheduler, [] {}, [] {}).rejection, Rejection::Transport);
        QVERIFY(env.quiescent());
        bool invalid_transport{false};
        try { (void)std::make_shared<TaskQueue>(TaskQueue::RoutedDispatch{}, Options{}, 1); }
        catch (const std::invalid_argument&) { invalid_transport = true; }
        QVERIFY(invalid_transport);
    }
    void transportRejectionAndInvalidSpecsAdmitNothing()
    {
        using namespace execution;
        std::deque<std::function<void()>> turns;
        bool reject{true};
        auto queue = std::make_shared<TaskQueue>([&](Phase, TaskId, std::function<void()> turn) {
            if (std::exchange(reject, false)) return false;
            turns.push_back(std::move(turn));
            return true;
        }, Options{});
        int effects{0}, outcomes{0};
        const auto submit = [&] { return Submit(*queue, [&] { ++effects; }, [&] { ++outcomes; }, {}, {Lane::Core}); };
        QCOMPARE(submit().rejection, Rejection::Transport);
        QCOMPARE(queue->pending(), size_t{0});
        QVERIFY(submit()); QVERIFY(submit());
        while (!turns.empty()) {
            auto turn = std::move(turns.front()); turns.pop_front(); turn();
        }
        QCOMPARE(effects, 2);
        QCOMPARE(outcomes, 2);
        QCOMPARE(queue->pending(), size_t{0});
        QCOMPARE(queue->submit({}, {}).rejection, Rejection::Invalid);
        QCOMPARE(Submit(*queue, [] {}, [] {}, {}, {static_cast<Lane>(255)}).rejection, Rejection::Invalid);
    }
    void executionOutcomeAndObservationAreSeparate()
    {
        ManualExecutionEnvironment env;
        int effect{0}, domain{0}, observed{0};
        std::function<void()> publication;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        execution::Admission admitted;
        admitted = execution::Submit(*env.scheduler, [&] {
            if (env.scheduler->phase(admitted.id) == execution::Phase::Executing) effect = 7;
            return 9;
        }, [&](int result) {
            QCOMPARE(env.scheduler->phase(admitted.id), std::make_optional(execution::Phase::Delivering));
            QCOMPARE(env.scheduler->pending(), size_t{1});
            domain = result;
            publication = [&] { observed = domain; };
        });
        QVERIFY(admitted);
        QCOMPARE(effect, 0);
        QCOMPARE(env.scheduler->phase(admitted.id), std::make_optional(execution::Phase::Queued));
        QVERIFY(env.step());
        QCOMPARE(effect, 7);
        QCOMPARE(domain, 0);
        QCOMPARE(env.scheduler->phase(admitted.id), std::make_optional(execution::Phase::OutcomeReady));
        env.scheduler->closeAdmission();
        QCOMPARE(execution::Submit(*env.scheduler, [] {}, [] {}).rejection, execution::Rejection::Closed);
        QVERIFY(env.step());
        QCOMPARE(domain, 9);
        QCOMPARE(observed, 0);
        publication();
        QCOMPARE(observed, 9);
        QVERIFY(!env.scheduler->phase(admitted.id));
        QVERIFY(env.quiescent());
    }
    void rejectionExceptionAndFifo()
    {
        ManualExecutionEnvironment env;
        int effects{0}, results{0}, failures{0};
        std::vector<int> order;
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        env.rejectNext(true);
        const auto rejected = execution::Submit(*env.scheduler, [&] { ++effects; }, [&] { ++results; });
        QCOMPARE(rejected.rejection, execution::Rejection::Transport);
        QCOMPARE(env.scheduler->pending(), size_t{0});
        execution::Submit(*env.scheduler, [&]() -> int { ++effects; order.push_back(1); throw std::runtime_error("synthetic"); },
                          [&](int) { ++results; }, [&](std::exception_ptr error) {
                              QVERIFY(error);
                              ++failures;
                              order.push_back(3);
                          });
        execution::Submit(*env.scheduler, [&] { ++effects; order.push_back(2); }, [&] { ++results; order.push_back(4); });
        env.drain();
        QCOMPARE(effects, 2);
        QCOMPARE(results, 1);
        QCOMPARE(failures, 1);
        QCOMPARE(order, std::vector<int>({1, 2, 3, 4}));
        QVERIFY(env.quiescent());
        execution::Submit(*env.scheduler, [] { throw 1; }, [] {});
        env.drain();
        QVERIFY(env.quiescent());
    }
    void completionCanSubmitAnotherOwnedCommand()
    {
        ManualExecutionEnvironment env;
        int effects{0}, outcomes{0};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        execution::Submit(*env.scheduler, [&] { ++effects; }, [&] {
            ++outcomes;
            QVERIFY(execution::Submit(*env.scheduler, [&] { ++effects; }, [&] { ++outcomes; }));
        });
        env.drain();
        QCOMPARE(effects, 2);
        QCOMPARE(outcomes, 2);
        QVERIFY(env.quiescent());
    }
    void ownedResultsAreReleasedAfterAccounting()
    {
        ManualExecutionEnvironment env;
        int released{0};
        const auto cleanup = qScopeGuard([&] { env.drain(); });
        struct Value { int& count; ~Value() { ++count; } };
        execution::Submit(*env.scheduler, [&] { return std::make_unique<Value>(released); },
                          [](std::unique_ptr<Value>) {});
        QVERIFY(env.step());
        QCOMPARE(released, 0);
        QVERIFY(env.step());
        QCOMPARE(released, 1);
        QVERIFY(env.quiescent());
    }
    void feeDecisionGuardsAndTerminalClose()
    {
        FeeReadState state;
        using K = FeeReadState::Kind;
        using P = FeeReadState::Phase;
        QCOMPARE(state.phase(), P::Idle);
        QVERIFY(!state.apply({K::Complete, {1}}));
        QVERIFY(state.apply({K::Invalidate, {1}}));
        QVERIFY(!state.apply({K::Invalidate, {1}}));
        QVERIFY(!state.apply({K::Dispatch, {1}}));
        QVERIFY(state.apply({K::Dispatch, {2}}));
        QCOMPARE(state.phase(), P::Reading);
        QVERIFY(!state.apply({K::Complete, {1}}));
        QVERIFY(state.apply({K::Invalidate, {3}}));
        QCOMPARE(state.phase(), P::Idle);
        QVERIFY(!state.apply({K::Complete, {2}}));
        QVERIFY(state.apply({K::Dispatch, {4}}));
        QVERIFY(state.apply({K::Complete, {4}}));
        QVERIFY(!state.apply({K::Complete, {4}}));
        QVERIFY(!state.apply({static_cast<K>(255), {5}}));
        QVERIFY(state.apply({K::Close, {}}));
        QCOMPARE(state.phase(), P::Closed);
        for (auto kind : {K::Invalidate, K::Dispatch, K::Complete, K::Close}) QVERIFY(!state.apply({kind, {6}}));
    }
};

BITCOINQML_REGISTER_QT_TEST(ExecutionTests)
#include <test_execution.moc>
