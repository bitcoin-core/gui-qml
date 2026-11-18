// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/executor.h>
#include <qml/desktopintenttransport.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>

#include <QDir>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <future>

#ifndef Q_OS_WIN
#include <unistd.h>
#endif

namespace {
using namespace desktopintent;
using namespace desktopintent::transport;
using namespace std::chrono_literals;
std::shared_ptr<Owner> NewOwner(execution::Executor& executor, Owner::Notify notify = {})
{
    return Owner::Create({}, std::move(notify), std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility)));
}
Input Request(int id, Origin origin = Origin::Arguments)
{
    return {QString::number(id), Kind::Text, QStringLiteral("bitcoin:request-%1").arg(id), origin};
}
template <typename T> bool Ready(std::future<T>& future)
{
    return QTest::qWaitFor([&] { return future.wait_for(0ms) == std::future_status::ready; }, 8000);
}
auto Start(const std::shared_ptr<Service>& service, const QString& profile, std::vector<Input> inputs = {})
{
    auto promise = std::make_shared<std::promise<StartResult>>();
    auto future = promise->get_future();
    if (!service->start(profile, QStringLiteral("regtest"), std::move(inputs), [promise](auto result) { promise->set_value(std::move(result)); }))
        promise->set_value({StartStatus::Rejected, QStringLiteral("Not admitted"), {}});
    return future;
}
auto Stop(const std::shared_ptr<Service>& service)
{
    auto promise = std::make_shared<std::promise<void>>();
    auto future = promise->get_future();
    if (!service->stop([promise] { promise->set_value(); })) promise->set_value();
    return future;
}
bool Shutdown(const std::shared_ptr<Service>& service)
{
    auto retired = Stop(service);
    return Ready(retired) && service->retired();
}
auto Probe(const std::shared_ptr<Service>& service, const QString& profile, const std::vector<Input>& inputs = {})
{
    auto promise = std::make_shared<std::promise<ProbeResult>>();
    auto future = promise->get_future();
    if (!service->probeExisting(profile, QStringLiteral("regtest"), inputs, [promise](auto result) { promise->set_value(std::move(result)); }))
        promise->set_value({ProbeStatus::Rejected, {StartStatus::Rejected, QStringLiteral("Not admitted"), {}}});
    return future;
}
auto Lookup(const std::shared_ptr<Owner>& owner, const QString& key)
{
    auto promise = std::make_shared<std::promise<std::optional<Receipt>>>(); auto future = promise->get_future();
    if (!owner->lookup(key, [promise](auto value) { promise->set_value(std::move(value)); })) promise->set_value({});
    return future;
}
}

class DesktopIntentTransportTests : public QObject {
    Q_OBJECT
#ifndef Q_OS_WIN
    // Endpoints live in the user's runtime directory. Give the tests a short
    // private one: a long inherited path would exceed the socket path limit,
    // and the tests must not use the real user's endpoints.
    QTemporaryDir m_runtime{QStringLiteral("/tmp/qit-XXXXXX")};
    QByteArray m_previous_runtime;
    bool m_had_runtime{false};
#endif
private Q_SLOTS:
#ifndef Q_OS_WIN
    void initTestCase()
    {
        QVERIFY(m_runtime.isValid());
        QVERIFY(QFile::setPermissions(m_runtime.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        m_had_runtime = qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
        m_previous_runtime = qgetenv("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(m_runtime.path()));
    }
    void cleanupTestCase()
    {
        if (m_had_runtime) qputenv("XDG_RUNTIME_DIR", m_previous_runtime); else qunsetenv("XDG_RUNTIME_DIR");
    }
#endif
    void admissionFenceDoesNotDrainAndRetainedFacadeReleasesScope()
    {
        ManualStartupExecution commands;
        auto owner = NewOwner(commands.executor());
        auto service = Service::Create(owner); QVERIFY(service);
        QVERIFY(commands.quiescent());
        service->close();
        QVERIFY(commands.quiescent()); QVERIFY(!service->retired());
        QVERIFY(!service->start(QStringLiteral("/unused"), QStringLiteral("regtest"), {}, {}));
        QVERIFY(owner->admit({Request(1)}, {})); commands.drain();
        auto retired = Stop(service); QVERIFY(!service->retired()); commands.drain();
        QVERIFY(Ready(retired)); QVERIFY(service->retired());
        std::optional<Receipt> receipt;
        QVERIFY(owner->lookup(QStringLiteral("1"), [&](auto value) { receipt = value; })); commands.drain();
        QVERIFY(receipt); QVERIFY(receipt->disposition == Disposition::DroppedOnStop);
        owner.reset(); commands.drain();
        QVERIFY(commands.quiescent()); QVERIFY(service);
    }
    void missingProfileIsNotCreatedAndPhysicalStopUsesBackgroundDomain()
    {
        execution::Executor executor{execution::Executor::Config{}};
        auto owner = NewOwner(executor);
        auto service = Service::Create(owner); QVERIFY(service);
        const auto cleanup = qScopeGuard([&] { service->stop(); owner.reset(); Shutdown(service); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto profile = directory.filePath(QStringLiteral("not-created/profile"));
        auto promise = std::make_shared<std::promise<std::pair<StartResult, QThread*>>>(); auto result = promise->get_future();
        QVERIFY(service->start(profile, QStringLiteral("regtest"), {Request(1)}, [promise](auto value) { promise->set_value({std::move(value), QThread::currentThread()}); }));
        QVERIFY(Ready(result)); const auto started = result.get();
        QVERIFY(started.first.status == StartStatus::Primary); QCOMPARE(started.first.receipts.size(), size_t{1});
        QVERIFY(started.second != QThread::currentThread()); QVERIFY(!QFileInfo::exists(profile));
        auto retired = Stop(service); QVERIFY(Ready(retired)); QVERIFY(service->retired());
        owner.reset(); QVERIFY(Shutdown(service)); QVERIFY(service);
    }
#ifndef Q_OS_WIN
    void unsafeSharedTemporaryDirectoryDoesNotBlockDelivery()
    {
        // Another user can create /tmp/qml-intent-<uid> before this user starts.
        // The endpoint lives in the user's runtime directory, so that name has
        // no effect. A writable-by-others directory stands in for a foreign one.
        const QString shared = QStringLiteral("/tmp/qml-intent-%1").arg(getuid());
        if (QFileInfo::exists(shared)) QSKIP("A shared payment-delivery directory already exists for this user");
        QVERIFY(QDir{}.mkdir(shared));
        const auto remove_shared = qScopeGuard([&] { QDir{shared}.removeRecursively(); });
        QVERIFY(QFile::setPermissions(shared, QFileDevice::Permissions{0x7777}));
        QTemporaryDir runtime; QVERIFY(runtime.isValid());
        QVERIFY(QFile::setPermissions(runtime.path(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        const auto previous = qgetenv("XDG_RUNTIME_DIR");
        const bool had_previous = qEnvironmentVariableIsSet("XDG_RUNTIME_DIR");
        qputenv("XDG_RUNTIME_DIR", QFile::encodeName(runtime.path()));
        const auto restore = qScopeGuard([&] { if (had_previous) qputenv("XDG_RUNTIME_DIR", previous); else qunsetenv("XDG_RUNTIME_DIR"); });

        execution::Executor executor{execution::Executor::Config{}};
        auto owner = NewOwner(executor);
        auto service = Service::Create(owner); QVERIFY(service);
        const auto cleanup = qScopeGuard([&] { service->stop(); owner.reset(); Shutdown(service); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto started = Start(service, directory.path()); QVERIFY(Ready(started));
        const auto result = started.get();
        QVERIFY2(result.status == StartStatus::Primary, qPrintable(result.error));
        // XDG_RUNTIME_DIR sets the runtime location except on macOS, which
        // keeps its own per-user location; there only the endpoint is checked.
        const QDir endpoints{QDir{QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation)}.filePath(QStringLiteral("qml-intent"))};
#ifdef Q_OS_MACOS
        QVERIFY(!endpoints.entryList(QDir::System | QDir::NoDotAndDotDot).isEmpty());
#else
        QCOMPARE(endpoints.path(), QDir{runtime.path()}.filePath(QStringLiteral("qml-intent")));
        QCOMPARE(endpoints.entryList(QDir::System | QDir::NoDotAndDotDot).size(), 1);
#endif
        QVERIFY(QDir{shared}.isEmpty());
        auto retired = Stop(service); QVERIFY(Ready(retired));
    }
#endif
    void secondaryForwardsEarlyFileOpenAndArgumentsAndDoesNotStealEmptyEndpoint_data()
    {
        QTest::addColumn<bool>("arguments");
        QTest::addColumn<bool>("separate_owner");
        QTest::newRow("early-open-only-empty-argv") << false << false;
        QTest::newRow("early-open-and-arguments") << true << false;
        QTest::newRow("early-open-and-arguments-separate-owner") << true << true;
    }
    void secondaryForwardsEarlyFileOpenAndArgumentsAndDoesNotStealEmptyEndpoint()
    {
        QFETCH(bool, arguments);
        QFETCH(bool, separate_owner);
        execution::Executor primary_executor{execution::Executor::Config{}}, secondary_executor{execution::Executor::Config{}}, empty_executor{execution::Executor::Config{}};
        const auto owner_executor = separate_owner ? std::make_unique<execution::Executor>(execution::Executor::Config{}) : nullptr;
        auto primary_owner = NewOwner(owner_executor ? *owner_executor : primary_executor);
        auto secondary_owner = NewOwner(owner_executor ? *owner_executor : secondary_executor);
        auto empty_owner = NewOwner(owner_executor ? *owner_executor : empty_executor);
        auto primary = Service::Create(primary_owner);
        auto secondary = Service::Create(secondary_owner);
        auto empty = Service::Create(empty_owner);
        const auto cleanup = qScopeGuard([&] {
            primary->stop(); secondary->stop(); empty->stop(); primary_owner.reset(); secondary_owner.reset(); empty_owner.reset();
            Shutdown(primary); Shutdown(secondary); Shutdown(empty);
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto profile = directory.filePath(QStringLiteral("missing/profile"));
        auto listening = Start(primary, profile); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        QVERIFY(secondary_owner->admit({Request(1, Origin::FileOpen)}, {}));
        auto forwarded = Start(secondary, profile, arguments ? std::vector<Input>{Request(2)} : std::vector<Input>{}); QVERIFY(Ready(forwarded));
        const auto result = forwarded.get(); QVERIFY(result.status == StartStatus::Forwarded);
        QCOMPARE(result.receipts.size(), arguments ? size_t{2} : size_t{1});
        QCOMPARE(result.producer_keys, arguments ? (std::vector<QString>{QStringLiteral("1"), QStringLiteral("2")}) : std::vector<QString>{QStringLiteral("1")});
        QCOMPARE(result.receipts[0].delivery, uint64_t{1}); if (arguments) QCOMPARE(result.receipts[1].delivery, uint64_t{2});
        QVERIFY(!secondary_owner->admit({Request(3, Origin::FileOpen)}, {}));
        auto no_request = Start(empty, profile); QVERIFY(Ready(no_request)); QVERIFY(no_request.get().status == StartStatus::Rejected);
        QVERIFY(!primary->retired()); QVERIFY(!QFileInfo::exists(profile));
        auto retired = Stop(primary); QVERIFY(Ready(retired)); QVERIFY(primary->retired());
    }
    void reusedLocalProducerKeyFromAnotherServiceIsASeparateDelivery()
    {
        execution::Executor primary_executor{execution::Executor::Config{}}, first_executor{execution::Executor::Config{}}, second_executor{execution::Executor::Config{}};
        auto primary_owner = NewOwner(primary_executor);
        auto first_owner = NewOwner(first_executor);
        auto second_owner = NewOwner(second_executor);
        auto primary = Service::Create(primary_owner);
        auto first = Service::Create(first_owner);
        auto second = Service::Create(second_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); first->stop(); second->stop(); primary_owner.reset(); first_owner.reset(); second_owner.reset(); Shutdown(primary); Shutdown(first); Shutdown(second); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto original = Start(first, directory.path(), {Request(1)}); QVERIFY(Ready(original));
        auto reused = Start(second, directory.path(), {Request(1)}); QVERIFY(Ready(reused));
        const auto before = original.get(), after = reused.get();
        QVERIFY(before.status == StartStatus::Forwarded); QVERIFY(after.status == StartStatus::Forwarded);
        QCOMPARE(before.producer_keys, after.producer_keys); QCOMPARE(before.receipts.front().delivery, uint64_t{1}); QCOMPARE(after.receipts.front().delivery, uint64_t{2});
    }
    void capacityRejectsWholeRemoteBatchWithoutConsumingIdentity()
    {
        execution::Executor primary_executor{execution::Executor::Config{}}, secondary_executor{execution::Executor::Config{}};
        auto primary_owner = NewOwner(primary_executor);
        auto secondary_owner = NewOwner(secondary_executor);
        auto primary = Service::Create(primary_owner);
        auto secondary = Service::Create(secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); primary_owner.reset(); secondary_owner.reset(); Shutdown(primary); Shutdown(secondary); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        std::vector<Input> initial; for (int index{1}; index <= 15; ++index) initial.push_back(Request(index));
        auto listening = Start(primary, directory.path(), std::move(initial)); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto forwarded = Start(secondary, directory.path(), {Request(16), Request(17)}); QVERIFY(Ready(forwarded)); QVERIFY(forwarded.get().status == StartStatus::Rejected);
        auto promise = std::make_shared<std::promise<desktopintent::Acceptance>>(); auto admitted = promise->get_future();
        QVERIFY(primary_owner->admit({Request(18)}, [promise](auto value) { promise->set_value(std::move(value)); }));
        QVERIFY(Ready(admitted)); const auto last = admitted.get(); QVERIFY(last); QCOMPARE(last.receipts.front().delivery, uint64_t{16});
    }
    void lostAcknowledgmentQueriesOriginalReceiptWithoutReadmission()
    {
        ManualStartupExecution original;
        execution::Executor secondary_executor{execution::Executor::Config{}};
        auto primary_owner = NewOwner(original.executor());
        auto secondary_owner = NewOwner(secondary_executor);
        auto clock = std::make_shared<std::atomic<int64_t>>(0);
        auto clock_calls = std::make_shared<std::atomic<size_t>>(0);
        Limits limits; limits.now = [clock, clock_calls] { ++*clock_calls; return Limits::Time{} + std::chrono::milliseconds{clock->load()}; };
        auto primary = Service::Create(primary_owner);
        auto secondary = Service::Create(secondary_owner, limits);
        auto release = std::make_shared<std::promise<void>>(); const auto gate = release->get_future().share(); bool released{false};
        const auto cleanup = qScopeGuard([&] {
            if (!released) release->set_value();
            primary->stop(); secondary->stop(); original.drain(); primary_owner.reset(); secondary_owner.reset(); original.drain();
            Shutdown(primary); Shutdown(secondary); original.drain();
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto forwarded = Start(secondary, directory.path(), {Request(1)});
        QVERIFY(QTest::qWaitFor([&] { return original.hasWork(execution::Lane::Utility); }, 8000));
        auto entered = std::make_shared<std::atomic_bool>(false);
        QVERIFY(primary->post([gate, entered] { *entered = true; gate.wait(); }));
        QVERIFY(QTest::qWaitFor([&] { return entered->load(); }, 8000));
        QVERIFY(QTest::qWaitFor([&] { return clock_calls->load() >= 8; }, 8000)); // Sender has entered its ACK deadline loop.
        original.drain(); // Retain admission receipt; its ACK waits behind gate.
        QVERIFY(primary_owner->stop()); original.drain(); // Readmission now rejects; original-key lookup remains supported.
        *clock = 3000; release->set_value(); released = true;
        QVERIFY(QTest::qWaitFor([&] { original.drain(); return forwarded.wait_for(0ms) == std::future_status::ready; }, 8000));
        const auto result = forwarded.get(); QVERIFY(result.status == StartStatus::Forwarded);
        QCOMPARE(result.receipts.size(), size_t{1}); QCOMPARE(result.receipts.front().delivery, uint64_t{1});
        QVERIFY(result.receipts.front().disposition == Disposition::DroppedOnStop);
        QCOMPARE(result.producer_keys, std::vector<QString>{QStringLiteral("1")});
    }
    void lateOwnerReplyDoesNotHoldPhysicalNativeRetirement()
    {
        ManualStartupExecution original;
        execution::Executor secondary_executor{execution::Executor::Config{}};
        auto primary_owner = NewOwner(original.executor());
        auto secondary_owner = NewOwner(secondary_executor);
        auto primary = Service::Create(primary_owner);
        auto secondary = Service::Create(secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); original.drain(); primary_owner.reset(); secondary_owner.reset(); original.drain(); Shutdown(primary); Shutdown(secondary); original.drain(); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto forwarded = Start(secondary, directory.path(), {Request(1)});
        QVERIFY(QTest::qWaitFor([&] { return original.hasWork(execution::Lane::Utility); }, 8000));
        auto retired = Stop(primary); QVERIFY(Ready(retired)); QVERIFY(primary->retired());
        QVERIFY(Ready(forwarded)); QVERIFY(forwarded.get().status == StartStatus::Unknown);
        // The delayed foreign callback is delivered after Native QObject/socket
        // destruction. Its reply cell must have revoked that receiver already.
        original.drain(); primary_owner.reset(); original.drain();
        QVERIFY(Shutdown(primary)); QVERIFY(primary);
    }
    void backwardClockDoesNotAuthorizeReplayOrLateAcknowledgment()
    {
        ManualStartupExecution original;
        execution::Executor secondary_executor{execution::Executor::Config{}};
        size_t owner_turns{0};
        auto primary_owner = NewOwner(original.executor(), [&](auto) { ++owner_turns; });
        auto secondary_owner = NewOwner(secondary_executor);
        auto clock = std::make_shared<std::atomic<int64_t>>(1000);
        Limits limits; limits.now = [clock] { return Limits::Time{} + std::chrono::milliseconds{clock->load()}; };
        auto primary = Service::Create(primary_owner);
        auto secondary = Service::Create(secondary_owner, limits);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); original.drain(); primary_owner.reset(); secondary_owner.reset(); original.drain(); Shutdown(primary); Shutdown(secondary); original.drain(); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto forwarded = Start(secondary, directory.path(), {Request(1)});
        QVERIFY(QTest::qWaitFor([&] { return original.hasWork(execution::Lane::Utility); }, 8000));
        QCOMPARE(owner_turns, size_t{0});
        *clock = 0;
        QVERIFY(Ready(forwarded)); QVERIFY(forwarded.get().status == StartStatus::Unknown);
        original.drain();
        // Each admitted request or lookup publishes a new Owner snapshot.
        // The invalid clock must permit only the original admission.
        QCOMPARE(owner_turns, size_t{1});
    }
    void invalidDeadlineConfigurationDoesNotReserveResources()
    {
        ManualStartupExecution commands;
        auto owner = NewOwner(commands.executor());
        Limits limits; limits.acknowledge = 31s;
        QVERIFY(!Service::Create(owner, limits));
        limits = {}; limits.now = {};
        QVERIFY(!Service::Create(owner, limits));
        owner.reset(); commands.drain(); QVERIFY(commands.quiescent());
    }
    void missingProbeLeavesOriginalQueueAndLaterFinalProfileStartUntouched()
    {
        execution::Executor executor{execution::Executor::Config{}}, rival_executor{execution::Executor::Config{}};
        auto owner = NewOwner(executor), rival_owner = NewOwner(rival_executor);
        auto service = Service::Create(owner), rival = Service::Create(rival_owner);
        const auto cleanup = qScopeGuard([&] { service->stop(); rival->stop(); owner.reset(); rival_owner.reset(); Shutdown(service); Shutdown(rival); });
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto lookup_profile = directory.filePath(QStringLiteral("initial/missing"));
        const auto final_profile = directory.filePath(QStringLiteral("final/missing"));
        QVERIFY(owner->admit({Request(1, Origin::FileOpen)}, {})); auto before = Lookup(owner, QStringLiteral("1")); QVERIFY(Ready(before)); const auto original = before.get(); QVERIFY(original);
        const std::vector<Input> arguments{Request(2)};
        auto probed = Probe(service, lookup_profile, arguments); QVERIFY(Ready(probed)); const auto absent = probed.get();
        QVERIFY(absent.status == ProbeStatus::Missing); QVERIFY(absent.transfer.receipts.empty()); QVERIFY(absent.transfer.producer_keys.empty());
        auto early = Lookup(owner, QStringLiteral("1")); auto unadmitted = Lookup(owner, QStringLiteral("2")); QVERIFY(Ready(early)); QVERIFY(Ready(unadmitted));
        const auto retained = early.get(); QVERIFY(retained); QCOMPARE(retained->delivery, original->delivery); QVERIFY(!unadmitted.get());
        QCOMPARE(arguments.size(), size_t{1}); QCOMPARE(arguments.front().producer_key, QStringLiteral("2"));
        auto independent = Start(rival, lookup_profile); QVERIFY(Ready(independent)); QVERIFY(independent.get().status == StartStatus::Primary); // Probe bound no early listener/lock.
        auto final = Start(service, final_profile, arguments); QVERIFY(Ready(final)); const auto started = final.get();
        QVERIFY(started.status == StartStatus::Primary); QCOMPARE(started.receipts.front().delivery, uint64_t{2});
        QVERIFY(!QFileInfo::exists(lookup_profile)); QVERIFY(!QFileInfo::exists(final_profile));
    }
    void existingProbeForwardsOriginalEarlyInputsAndNeverFallsThrough_data()
    {
        QTest::addColumn<bool>("arguments"); QTest::newRow("early-open-only") << false; QTest::newRow("early-open-plus-argv") << true;
    }
    void existingProbeForwardsOriginalEarlyInputsAndNeverFallsThrough()
    {
        QFETCH(bool, arguments);
        execution::Executor primary_executor{execution::Executor::Config{}}, secondary_executor{execution::Executor::Config{}};
        auto primary_owner = NewOwner(primary_executor), secondary_owner = NewOwner(secondary_executor);
        auto primary = Service::Create(primary_owner), secondary = Service::Create(secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); primary_owner.reset(); secondary_owner.reset(); Shutdown(primary); Shutdown(secondary); });
        QTemporaryDir directory; QVERIFY(directory.isValid()); auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        QVERIFY(secondary_owner->admit({Request(1, Origin::FileOpen)}, {}));
        const auto inputs = arguments ? std::vector<Input>{Request(2)} : std::vector<Input>{};
        auto probed = Probe(secondary, directory.path(), inputs); QVERIFY(Ready(probed)); const auto result = probed.get();
        QVERIFY(result.status == ProbeStatus::Forwarded); QVERIFY(result.transfer.status == StartStatus::Forwarded);
        QCOMPARE(result.transfer.receipts.size(), arguments ? size_t{2} : size_t{1});
        QCOMPARE(result.transfer.producer_keys.front(), QStringLiteral("1"));
        if (arguments) QCOMPARE(result.transfer.producer_keys.back(), QStringLiteral("2"));
        QVERIFY(!secondary->start(directory.filePath(QStringLiteral("other-profile")), QStringLiteral("regtest"), inputs, {}));
        QVERIFY(!secondary->probeExisting(directory.path(), QStringLiteral("regtest"), inputs, {}));
        QVERIFY(!secondary_owner->admit({Request(3)}, {}));
    }
    void rejectedExistingProbeCannotAcquireAnotherPrimary_data()
    {
        QTest::addColumn<bool>("full"); QTest::newRow("no-input") << false; QTest::newRow("peer-capacity") << true;
    }
    void rejectedExistingProbeCannotAcquireAnotherPrimary()
    {
        QFETCH(bool, full);
        execution::Executor primary_executor{execution::Executor::Config{}}, secondary_executor{execution::Executor::Config{}};
        auto primary_owner = NewOwner(primary_executor), secondary_owner = NewOwner(secondary_executor);
        auto primary = Service::Create(primary_owner), secondary = Service::Create(secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); primary_owner.reset(); secondary_owner.reset(); Shutdown(primary); Shutdown(secondary); });
        QTemporaryDir directory; QVERIFY(directory.isValid()); std::vector<Input> initial;
        if (full) for (int index{1}; index <= 16; ++index) initial.push_back(Request(index));
        auto listening = Start(primary, directory.path(), initial); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        const auto inputs = full ? std::vector<Input>{Request(17)} : std::vector<Input>{};
        auto probed = Probe(secondary, directory.path(), inputs); QVERIFY(Ready(probed)); const auto result = probed.get();
        QVERIFY(result.status == ProbeStatus::Rejected); QVERIFY(result.transfer.status == StartStatus::Rejected);
        QVERIFY(!secondary->start(directory.filePath(QStringLiteral("successor")), QStringLiteral("regtest"), inputs, {}));
        QVERIFY(!primary->retired());
    }
    void unknownProbeTransferRetainsOriginalKeysAndCannotReplay()
    {
        ManualStartupExecution original;
        execution::Executor secondary_executor{execution::Executor::Config{}};
        auto primary_owner = NewOwner(original.executor()), secondary_owner = NewOwner(secondary_executor);
        auto primary = Service::Create(primary_owner), secondary = Service::Create(secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); original.drain(); primary_owner.reset(); secondary_owner.reset(); original.drain(); Shutdown(primary); Shutdown(secondary); original.drain(); });
        QTemporaryDir directory; QVERIFY(directory.isValid()); auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto probed = Probe(secondary, directory.path(), {Request(1)});
        QVERIFY(QTest::qWaitFor([&] { return original.hasWork(execution::Lane::Utility); }, 8000));
        auto retired = Stop(primary); QVERIFY(Ready(retired));
        QVERIFY(Ready(probed)); const auto result = probed.get(); QVERIFY(result.status == ProbeStatus::Unknown); QVERIFY(result.transfer.status == StartStatus::Unknown);
        QCOMPARE(result.transfer.producer_keys, std::vector<QString>{QStringLiteral("1")}); QVERIFY(result.transfer.receipts.empty());
        QVERIFY(!secondary->start(directory.filePath(QStringLiteral("new-profile")), QStringLiteral("regtest"), {Request(1)}, {}));
        original.drain();
    }
    void stopBeforeProbeNativeTurnRetiresWithoutLookupOrPrimary()
    {
        ManualStartupExecution commands; auto owner = NewOwner(commands.executor()); auto service = Service::Create(owner); QVERIFY(service);
        QSemaphore entered, release;
        auto unblock = qScopeGuard([&] { release.release(); });
        QVERIFY(service->post([&] { entered.release(); release.acquire(); }));
        QVERIFY(entered.tryAcquire(1, 8000));
        auto probed = Probe(service, QStringLiteral("/unused-profile"), {Request(1)});
        QVERIFY(!service->start(QStringLiteral("/later-profile"), QStringLiteral("regtest"), {}, {}));
        QVERIFY(!service->probeExisting(QStringLiteral("/unused-profile"), QStringLiteral("regtest"), {}, {}));
        auto retired = Stop(service);
        QVERIFY(probed.wait_for(0ms) != std::future_status::ready);
        QVERIFY(retired.wait_for(0ms) != std::future_status::ready);
        release.release();
        unblock.dismiss();
        commands.drain(); QVERIFY(Ready(probed)); QVERIFY(probed.get().status == ProbeStatus::Rejected); QVERIFY(Ready(retired)); QVERIFY(service->retired());
        owner.reset(); commands.drain(); QVERIFY(commands.quiescent());
    }
    void stopWaitsForListenerThreadLocalCleanup()
    {
        struct Cleanup {
            QSemaphore entered, release;
        };
        struct WaitAtExit {
            std::shared_ptr<Cleanup> cleanup;
            ~WaitAtExit() {
                cleanup->entered.release();
                cleanup->release.acquire();
            }
        };
        ManualStartupExecution commands;
        auto owner = NewOwner(commands.executor());
        auto service = Service::Create(owner);
        QVERIFY(service);
        auto cleanup = std::make_shared<Cleanup>();
        const auto unblock = qScopeGuard([&] { cleanup->release.release(); });
        QVERIFY(service->post([cleanup] { static thread_local WaitAtExit wait{cleanup}; })); // NOLINT(bitcoin-nontrivial-threadlocal): Tests thread exit.
        auto retired = Stop(service);
        auto also_retired = Stop(service);
        commands.drain();
        QVERIFY(QTest::qWaitFor([&] { return cleanup->entered.available() > 0; }, 8000));
        QVERIFY(!service->retired());
        QVERIFY(retired.wait_for(0ms) != std::future_status::ready);
        QVERIFY(also_retired.wait_for(0ms) != std::future_status::ready);
        cleanup->release.release();
        QVERIFY(Ready(retired));
        QVERIFY(Ready(also_retired));
        QVERIFY(service->retired());
        owner.reset();
        commands.drain();
        QVERIFY(commands.quiescent());
    }
    void expiredProbeClockCannotForwardOrGrantPrimary_data()
    {
        QTest::addColumn<bool>("rollback"); QTest::newRow("expired") << false; QTest::newRow("rollback") << true;
    }
    void expiredProbeClockCannotForwardOrGrantPrimary()
    {
        QFETCH(bool, rollback);
        execution::Executor primary_executor{execution::Executor::Config{}}, secondary_executor{execution::Executor::Config{}};
        auto primary_owner = NewOwner(primary_executor), secondary_owner = NewOwner(secondary_executor);
        Limits limits; auto calls = std::make_shared<std::atomic_size_t>(0);
        limits.now = [calls, rollback] { return Limits::Time{((*calls)++ == 0 ? 100ms : (rollback ? 0ms : 6000ms))}; };
        auto primary = Service::Create(primary_owner), secondary = Service::Create(secondary_owner, limits);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); primary_owner.reset(); secondary_owner.reset(); Shutdown(primary); Shutdown(secondary); });
        QTemporaryDir directory; QVERIFY(directory.isValid()); auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto probed = Probe(secondary, directory.path(), {Request(1)}); QVERIFY(Ready(probed)); QVERIFY(probed.get().status == ProbeStatus::Rejected);
        auto unadmitted = Lookup(primary_owner, QStringLiteral("1")); QVERIFY(Ready(unadmitted)); QVERIFY(!unadmitted.get());
        auto unchanged = Lookup(secondary_owner, QStringLiteral("1")); QVERIFY(Ready(unchanged)); QVERIFY(!unchanged.get());
        QVERIFY(!secondary->start(directory.filePath(QStringLiteral("successor")), QStringLiteral("regtest"), {Request(1)}, {}));
    }
};
BITCOINQML_REGISTER_QT_TEST(DesktopIntentTransportTests)
#include <test_desktopintenttransport.moc>
