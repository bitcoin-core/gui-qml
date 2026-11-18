// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/profileinspectionprocess.h>

#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include <algorithm>
#include <stdexcept>
#include <utility>
#include <vector>

namespace {
using namespace profileinspection;
using namespace std::chrono_literals;

Request OriginalRequest()
{
    return {QDir::rootPath() + QStringLiteral("profile with spaces, \u00fc"), ChainType::REGTEST, {}};
}
QByteArray Reply(const Request& request, bool present)
{
    return QJsonDocument{QJsonObject{{QStringLiteral("protocol"), 1}, {QStringLiteral("request_tag"), Tag(request)},
        {QStringLiteral("ok"), true}, {QStringLiteral("has_wallet_data"), present}}}.toJson(QJsonDocument::Compact);
}
QByteArray ErrorReply(const Request& request)
{
    return QJsonDocument{QJsonObject{{QStringLiteral("protocol"), 1}, {QStringLiteral("request_tag"), Tag(request)},
        {QStringLiteral("ok"), false}, {QStringLiteral("error"), QStringLiteral("original directory failure")}}}.toJson(QJsonDocument::Compact);
}

// One public process-port instance represents the original child. Each effect
// records that identity; the existing driver owns all decisions and budgets.
struct ManualProcess final : process::Process {
    const int identity{47};
    process::Status state{process::Phase::Exited, true, 0, {}};
    QByteArray output, errors;
    QString executable;
    QStringList arguments;
    std::chrono::milliseconds now{0};
    int starts{0}, kills{0}, reaps{0}, reads{0}, waits{0}, throw_read{0};
    bool valid_reads{true}, valid_waits{true};
    std::vector<QString> effects;
    std::function<void()> on_start, on_reap;
    std::function<void(std::chrono::milliseconds)> on_wait;

    void record(const QString& action) { effects.push_back(QString::number(identity) + QLatin1Char(':') + action); }
    void start(const QString& program, const QStringList& args) override
    {
        ++starts; record(QStringLiteral("start")); executable = program; arguments = args;
        if (on_start) on_start();
    }
    process::Status status() const override { return state; }
    QByteArray read(process::Channel channel, qsizetype max_bytes) override
    {
        ++reads;
        if (reads == throw_read) throw std::runtime_error("manual transport failure");
        valid_reads &= max_bytes >= 0 && max_bytes <= 4097;
        auto& bytes = channel == process::Channel::Output ? output : errors;
        const auto count = std::min(max_bytes, bytes.size());
        auto result = bytes.first(count); bytes.remove(0, count); return result;
    }
    void wait(std::chrono::milliseconds duration) override
    {
        ++waits; record(QStringLiteral("wait"));
        valid_waits &= duration > 0ms && duration <= 10ms;
        now += duration;
        if (on_wait) on_wait(duration);
    }
    void kill() noexcept override { ++kills; record(QStringLiteral("kill")); }
    process::Status reap() noexcept override
    {
        ++reaps; record(QStringLiteral("reap"));
        if (on_reap) on_reap();
        if (state.phase == process::Phase::Starting || state.phase == process::Phase::Running)
            state = {process::Phase::Exited, false, {}, {}};
        return state;
    }
    Result run(const Request& request, const Cancellation& cancelled = {}, Limits limits = {})
    {
        return process::Run(request, cancelled, QStringLiteral("original helper path"), limits, *this, [&] { return now; });
    }
};
} // namespace

class ProfileInspectionProcessTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void cancellationBeforeSpawnHasNoEffect();
    void matchedReplyPreservesClassification_data();
    void matchedReplyPreservesClassification();
    void failedLaunchDoesNotInventExit();
    void unsuccessfulExitCannotClassify_data();
    void unsuccessfulExitCannotClassify();
    void invalidTransportCannotClassify_data();
    void invalidTransportCannotClassify();
    void fragmentedOutputBudgetKillsOriginalChild();
    void deadlineIncludesStartupAndPhysicalReap_data();
    void deadlineIncludesStartupAndPhysicalReap();
    void runningCancellationKeepsOriginalChild();
    void finalBytesStillUseOriginalBudget_data();
    void finalBytesStillUseOriginalBudget();
    void terminalReapStillUsesTotalDeadline();
    void cancellationAtFinalSpawnCheckHasNoEffect();
    void transportExceptionCannotAbandonChild_data();
    void transportExceptionCannotAbandonChild();
    void invalidLimitsNeverSpawn_data();
    void invalidLimitsNeverSpawn();
    void nativeGuiCallIsRejected();
};

void ProfileInspectionProcessTests::cancellationBeforeSpawnHasNoEffect()
{
    ManualProcess child;
    const auto cancelled = std::make_shared<std::atomic_bool>(true);
    const auto result = child.run(OriginalRequest(), cancelled);
    QCOMPARE(result.outcome, Outcome::Cancelled); QVERIFY(!result.started); QVERIFY(!result.exited); QVERIFY(!result.exit_status);
    QCOMPARE(child.starts, 0); QCOMPARE(child.kills, 0); QCOMPARE(child.reaps, 0); QVERIFY(child.effects.empty());
}

void ProfileInspectionProcessTests::matchedReplyPreservesClassification_data()
{
    QTest::addColumn<bool>("present");
    QTest::newRow("no-wallet-data") << false;
    QTest::newRow("wallet-data") << true;
}
void ProfileInspectionProcessTests::matchedReplyPreservesClassification()
{
    QFETCH(bool, present);
    auto request = OriginalRequest(); request.network = ChainType::SIGNET; request.signet_challenge = QString{};
    ManualProcess child;
    child.state = {process::Phase::Starting, false, {}, {}};
    child.on_wait = [&](auto) {
        if (child.waits == 1) child.state.phase = process::Phase::Running;
        else { child.output = Reply(request, present); child.state = {process::Phase::Exited, true, 0, {}}; }
    };
    const auto result = child.run(request);
    QCOMPARE(result.outcome, Outcome::Succeeded); QCOMPARE(result.has_wallet_data, present); QVERIFY(result.error.isEmpty());
    QVERIFY(result.started); QVERIFY(result.exited); QCOMPARE(result.exit_status, std::optional<int>{0});
    QCOMPARE(child.starts, 1); QCOMPARE(child.kills, 0); QCOMPARE(child.reaps, 1); QCOMPARE(child.waits, 2);
    QCOMPARE(child.executable, QStringLiteral("original helper path")); QCOMPARE(child.arguments, Arguments(request));
    QCOMPARE(child.arguments.last(), QString{}); QVERIFY(child.valid_reads); QVERIFY(child.valid_waits);
}

void ProfileInspectionProcessTests::failedLaunchDoesNotInventExit()
{
    ManualProcess child; child.state = {process::Phase::FailedToStart, false, {}, QStringLiteral("missing helper")};
    const auto result = child.run(OriginalRequest());
    QCOMPARE(result.outcome, Outcome::Failed); QVERIFY(!result.error.isEmpty()); QVERIFY(!result.has_wallet_data);
    QVERIFY(!result.started); QVERIFY(!result.exited); QVERIFY(!result.exit_status);
    QCOMPARE(child.starts, 1); QCOMPARE(child.kills, 0); QCOMPARE(child.reaps, 1);
}

void ProfileInspectionProcessTests::unsuccessfulExitCannotClassify_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("nonzero-with-success-reply") << 0;
    QTest::newRow("abnormal-with-success-reply") << 1;
    QTest::newRow("nonzero-with-original-error") << 2;
    QTest::newRow("missing-exit-code") << 3;
}
void ProfileInspectionProcessTests::unsuccessfulExitCannotClassify()
{
    QFETCH(int, mode);
    const auto request = OriginalRequest(); ManualProcess child;
    child.output = mode == 2 ? ErrorReply(request) : Reply(request, true);
    child.state = {process::Phase::Exited, mode != 1, mode == 3 ? std::nullopt : std::optional<int>{7}, {}};
    const auto result = child.run(request);
    QCOMPARE(result.outcome, Outcome::Failed); QVERIFY(!result.has_wallet_data); QVERIFY(!result.error.isEmpty());
    QVERIFY(result.started); QVERIFY(result.exited);
    if (mode == 1 || mode == 3) QVERIFY(!result.exit_status); else QCOMPARE(result.exit_status, std::optional<int>{7});
    if (mode == 2) QCOMPARE(result.error, QStringLiteral("original directory failure"));
    QCOMPARE(child.kills, 0); QCOMPARE(child.reaps, 1);
}

void ProfileInspectionProcessTests::invalidTransportCannotClassify_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("mismatched-request") << 0;
    QTest::newRow("stderr-with-valid-reply") << 1;
    QTest::newRow("stdout-over-budget") << 2;
    QTest::newRow("stderr-over-budget") << 3;
}
void ProfileInspectionProcessTests::invalidTransportCannotClassify()
{
    QFETCH(int, mode);
    auto request = OriginalRequest(); ManualProcess child;
    if (!mode) { auto other = request; other.directory += QStringLiteral(" other"); child.output = Reply(other, true); }
    else child.output = Reply(request, true);
    if (mode == 1) child.errors = "unexpected diagnostic";
    if (mode == 2) child.output = QByteArray(4097, 'x');
    if (mode == 3) child.errors = QByteArray(4097, 'e');
    const auto result = child.run(request);
    QVERIFY(result.outcome != Outcome::Succeeded); QVERIFY(!result.has_wallet_data); QVERIFY(!result.error.isEmpty());
    QVERIFY(result.started); QVERIFY(result.exited); QCOMPARE(result.exit_status, std::optional<int>{0});
    QCOMPARE(child.reaps, 1); QVERIFY(child.valid_reads);
}

void ProfileInspectionProcessTests::fragmentedOutputBudgetKillsOriginalChild()
{
    ManualProcess child; child.state = {process::Phase::Running, false, {}, {}};
    child.on_wait = [&](auto) { child.output += QByteArray(2048, 'x'); };
    const auto result = child.run(OriginalRequest());
    QCOMPARE(result.outcome, Outcome::Failed); QVERIFY(result.error.contains(QStringLiteral("budget")));
    QVERIFY(result.started); QVERIFY(result.exited); QVERIFY(!result.has_wallet_data);
    QCOMPARE(child.starts, 1); QCOMPARE(child.waits, 3); QCOMPARE(child.kills, 1); QCOMPARE(child.reaps, 1);
    QVERIFY(child.valid_reads); QVERIFY(child.valid_waits);
}

void ProfileInspectionProcessTests::deadlineIncludesStartupAndPhysicalReap_data()
{
    QTest::addColumn<bool>("starting");
    QTest::newRow("still-starting") << true;
    QTest::newRow("running") << false;
}
void ProfileInspectionProcessTests::deadlineIncludesStartupAndPhysicalReap()
{
    QFETCH(bool, starting);
    ManualProcess child; child.state = {starting ? process::Phase::Starting : process::Phase::Running, false, {}, {}};
    Limits limits; limits.deadline = 25ms;
    bool returned{false}, owned_during_reap{false};
    child.on_reap = [&] {
        owned_during_reap = !returned && child.kills == 1 && child.starts == 1;
        // Startup can complete only after cancellation/timeout was requested.
        child.state = {process::Phase::Exited, false, {}, {}};
    };
    const auto result = child.run(OriginalRequest(), {}, limits); returned = true;
    QCOMPARE(result.outcome, Outcome::TimedOut); QVERIFY(!result.has_wallet_data); QVERIFY(result.started); QVERIFY(result.exited);
    QVERIFY(!result.exit_status); QCOMPARE(child.now, 25ms); QCOMPARE(child.waits, 3);
    QCOMPARE(child.kills, 1); QCOMPARE(child.reaps, 1); QVERIFY(owned_during_reap); QVERIFY(child.valid_waits);
    QCOMPARE(child.effects.front(), QStringLiteral("47:start")); QCOMPARE(child.effects.back(), QStringLiteral("47:reap"));
}

void ProfileInspectionProcessTests::runningCancellationKeepsOriginalChild()
{
    ManualProcess child; child.state = {process::Phase::Running, false, {}, {}};
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    child.on_wait = [&](auto) { cancelled->store(true); };
    bool returned{false};
    child.on_reap = [&] { QVERIFY(!returned); QCOMPARE(child.kills, 1); };
    const auto result = child.run(OriginalRequest(), cancelled); returned = true;
    QCOMPARE(result.outcome, Outcome::Cancelled); QVERIFY(result.started); QVERIFY(result.exited); QVERIFY(!result.exit_status);
    QCOMPARE(child.starts, 1); QCOMPARE(child.waits, 1); QCOMPARE(child.kills, 1); QCOMPARE(child.reaps, 1);
    QVERIFY(!result.has_wallet_data); QVERIFY(!result.error.isEmpty());
}

void ProfileInspectionProcessTests::finalBytesStillUseOriginalBudget_data()
{
    QTest::addColumn<bool>("overflow");
    QTest::newRow("reply-visible-at-reap") << false;
    QTest::newRow("overflow-visible-at-reap") << true;
}
void ProfileInspectionProcessTests::finalBytesStillUseOriginalBudget()
{
    QFETCH(bool, overflow);
    const auto request = OriginalRequest(); ManualProcess child;
    child.on_reap = [&] { child.output = overflow ? QByteArray(4097, 'x') : Reply(request, true); };
    const auto result = child.run(request);
    QCOMPARE(result.outcome, overflow ? Outcome::Failed : Outcome::Succeeded);
    QCOMPARE(result.has_wallet_data, !overflow); QVERIFY(result.started); QVERIFY(result.exited);
    QCOMPARE(child.reaps, 1); QCOMPARE(child.kills, 0); QVERIFY(child.valid_reads);
}

void ProfileInspectionProcessTests::terminalReapStillUsesTotalDeadline()
{
    const auto request = OriginalRequest(); ManualProcess child;
    child.output = Reply(request, true);
    child.on_reap = [&] { child.now = 5000ms; };
    const auto result = child.run(request);
    QCOMPARE(result.outcome, Outcome::TimedOut); QVERIFY(!result.has_wallet_data);
    QVERIFY(result.started); QVERIFY(result.exited); QCOMPARE(result.exit_status, std::optional<int>{0});
    QCOMPARE(child.kills, 0); QCOMPARE(child.reaps, 1);
}

void ProfileInspectionProcessTests::cancellationAtFinalSpawnCheckHasNoEffect()
{
    ManualProcess child; int clock_calls{0};
    const auto cancelled = std::make_shared<std::atomic_bool>(false);
    const auto result = process::Run(OriginalRequest(), cancelled, QStringLiteral("original helper"), {}, child, [&] {
        if (++clock_calls == 2) cancelled->store(true);
        return 0ms;
    });
    QCOMPARE(result.outcome, Outcome::Cancelled); QVERIFY(!result.started); QVERIFY(!result.exited);
    QCOMPARE(child.starts, 0); QCOMPARE(child.kills, 0); QCOMPARE(child.reaps, 0);
}

void ProfileInspectionProcessTests::transportExceptionCannotAbandonChild_data()
{
    QTest::addColumn<bool>("after_reap");
    QTest::newRow("running-read-failure") << false;
    QTest::newRow("final-read-failure") << true;
}
void ProfileInspectionProcessTests::transportExceptionCannotAbandonChild()
{
    QFETCH(bool, after_reap);
    ManualProcess child;
    if (!after_reap) child.state = {process::Phase::Running, false, {}, {}};
    child.throw_read = after_reap ? 3 : 1;
    const auto result = child.run(OriginalRequest());
    QCOMPARE(result.outcome, Outcome::Failed); QVERIFY(result.started); QVERIFY(result.exited); QVERIFY(!result.has_wallet_data);
    QCOMPARE(child.starts, 1); QCOMPARE(child.reaps, 1); QCOMPARE(child.kills, after_reap ? 0 : 1);
    QVERIFY(!result.error.isEmpty());
}

void ProfileInspectionProcessTests::invalidLimitsNeverSpawn_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("negative-budget") << 0;
    QTest::newRow("oversized-budget") << 1;
    QTest::newRow("expired-before-start") << 2;
}
void ProfileInspectionProcessTests::invalidLimitsNeverSpawn()
{
    QFETCH(int, mode);
    ManualProcess child; Limits limits;
    if (!mode) limits.stdout_bytes = -1;
    if (mode == 1) limits.stderr_bytes = 4097;
    if (mode == 2) limits.deadline = 0ms;
    const auto result = child.run(OriginalRequest(), {}, limits);
    QCOMPARE(result.outcome, mode == 2 ? Outcome::TimedOut : Outcome::Rejected);
    QVERIFY(!result.started); QVERIFY(!result.exited); QVERIFY(!result.exit_status); QVERIFY(!result.error.isEmpty());
    QCOMPARE(child.starts, 0); QCOMPARE(child.kills, 0); QCOMPARE(child.reaps, 0);
}

void ProfileInspectionProcessTests::nativeGuiCallIsRejected()
{
    const auto result = profileinspection::Run(OriginalRequest(), {}, QStringLiteral("unused helper"));
    QCOMPARE(result.outcome, Outcome::Rejected); QVERIFY(!result.started); QVERIFY(!result.exited); QVERIFY(!result.exit_status);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ProfileInspectionProcessTests)
#else
QTEST_MAIN(ProfileInspectionProcessTests)
#endif
#include <test_profileinspectionprocess.moc>
