// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/backendexecutor.h>
#include <qml/desktopintenttransport.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QFileInfo>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <chrono>
#include <future>

namespace {
using namespace desktopintent;
using namespace desktopintent::transport;
using namespace std::chrono_literals;
std::shared_ptr<Owner> NewOwner(std::shared_ptr<execution::Scheduler> scheduler)
{
    Services services; services.scheduler = std::move(scheduler);
    return Owner::Create(std::move(services), {});
}
Input Request(int id, Origin origin = Origin::Arguments)
{
    return {QString::number(id), Kind::Text, QStringLiteral("bitcoin:request-%1").arg(id), origin};
}
template <typename T> bool Ready(std::future<T>& future)
{
    return QTest::qWaitFor([&] { return future.wait_for(0ms) == std::future_status::ready; }, 8000);
}
bool Shutdown(BackendExecutor& executor)
{
    executor.shutdown();
    return QTest::qWaitFor([&] { return executor.isDrained(); }, 8000);
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
private Q_SLOTS:
    void admissionFenceDoesNotDrainAndRetainedFacadeReleasesScope()
    {
        ManualRoutedExecution commands;
        auto owner = NewOwner(commands.scheduler);
        auto service = Service::Create(commands.scheduler, owner); QVERIFY(service);
        const auto pending = commands.scheduler->pending();
        service->close();
        QCOMPARE(commands.scheduler->pending(), pending); QVERIFY(!service->retired());
        QVERIFY(!service->start(QStringLiteral("/unused"), QStringLiteral("regtest"), {}, {}));
        QVERIFY(owner->admit({Request(1)}, {})); commands.drain();
        auto retired = Stop(service); QVERIFY(!service->retired()); commands.drain();
        QVERIFY(Ready(retired)); QVERIFY(service->retired());
        std::optional<Receipt> receipt;
        QVERIFY(owner->lookup(QStringLiteral("1"), [&](auto value) { receipt = value; })); commands.drain();
        QVERIFY(receipt); QVERIFY(receipt->disposition == Disposition::DroppedOnStop);
        owner.reset(); commands.drain();
        QCOMPARE(commands.scheduler->pending(), size_t{0}); QVERIFY(service);
    }
    void missingProfileIsNotCreatedAndPhysicalStopUsesBackgroundDomain()
    {
        BackendExecutor executor{execution::Options{}};
        auto owner = NewOwner(executor.scheduler());
        auto service = Service::Create(executor.scheduler(), owner); QVERIFY(service);
        const auto cleanup = qScopeGuard([&] { service->stop(); owner.reset(); Shutdown(executor); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto profile = directory.filePath(QStringLiteral("not-created/profile"));
        auto promise = std::make_shared<std::promise<std::pair<StartResult, QThread*>>>(); auto result = promise->get_future();
        QVERIFY(service->start(profile, QStringLiteral("regtest"), {Request(1)}, [promise](auto value) { promise->set_value({std::move(value), QThread::currentThread()}); }));
        QVERIFY(Ready(result)); const auto started = result.get();
        QVERIFY(started.first.status == StartStatus::Primary); QCOMPARE(started.first.receipts.size(), size_t{1});
        QVERIFY(started.second != QThread::currentThread()); QVERIFY(!QFileInfo::exists(profile));
        auto retired = Stop(service); QVERIFY(Ready(retired)); QVERIFY(service->retired());
        owner.reset(); QVERIFY(Shutdown(executor)); QVERIFY(service);
    }
    void secondaryForwardsEarlyFileOpenAndArgumentsAndDoesNotStealEmptyEndpoint_data()
    {
        QTest::addColumn<bool>("arguments");
        QTest::newRow("early-open-only-empty-argv") << false;
        QTest::newRow("early-open-and-arguments") << true;
    }
    void secondaryForwardsEarlyFileOpenAndArgumentsAndDoesNotStealEmptyEndpoint()
    {
        QFETCH(bool, arguments);
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}}, empty_executor{execution::Options{}};
        auto primary_owner = NewOwner(primary_executor.scheduler());
        auto secondary_owner = NewOwner(secondary_executor.scheduler());
        auto empty_owner = NewOwner(empty_executor.scheduler());
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner);
        auto secondary = Service::Create(secondary_executor.scheduler(), secondary_owner);
        auto empty = Service::Create(empty_executor.scheduler(), empty_owner);
        const auto cleanup = qScopeGuard([&] {
            primary->stop(); secondary->stop(); empty->stop(); primary_owner.reset(); secondary_owner.reset(); empty_owner.reset();
            Shutdown(primary_executor); Shutdown(secondary_executor); Shutdown(empty_executor);
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
    }
    void reusedLocalProducerKeyFromAnotherServiceIsASeparateDelivery()
    {
        BackendExecutor primary_executor{execution::Options{}}, first_executor{execution::Options{}}, second_executor{execution::Options{}};
        auto primary_owner = NewOwner(primary_executor.scheduler());
        auto first_owner = NewOwner(first_executor.scheduler());
        auto second_owner = NewOwner(second_executor.scheduler());
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner);
        auto first = Service::Create(first_executor.scheduler(), first_owner);
        auto second = Service::Create(second_executor.scheduler(), second_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); first->stop(); second->stop(); primary_owner.reset(); first_owner.reset(); second_owner.reset(); Shutdown(primary_executor); Shutdown(first_executor); Shutdown(second_executor); });
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
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}};
        auto primary_owner = NewOwner(primary_executor.scheduler());
        auto secondary_owner = NewOwner(secondary_executor.scheduler());
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner);
        auto secondary = Service::Create(secondary_executor.scheduler(), secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); primary_owner.reset(); secondary_owner.reset(); Shutdown(primary_executor); Shutdown(secondary_executor); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        std::vector<Input> initial; for (int index{1}; index <= 15; ++index) initial.push_back(Request(index));
        auto listening = Start(primary, directory.path(), std::move(initial)); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto forwarded = Start(secondary, directory.path(), {Request(16), Request(17)}); QVERIFY(Ready(forwarded)); QVERIFY(forwarded.get().status == StartStatus::Rejected);
        auto promise = std::make_shared<std::promise<desktopintent::Admission>>(); auto admitted = promise->get_future();
        QVERIFY(primary_owner->admit({Request(18)}, [promise](auto value) { promise->set_value(std::move(value)); }));
        QVERIFY(Ready(admitted)); const auto last = admitted.get(); QVERIFY(last); QCOMPARE(last.receipts.front().delivery, uint64_t{16});
    }
    void lostAcknowledgmentQueriesOriginalReceiptWithoutReadmission()
    {
        ManualRoutedExecution original;
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}};
        auto primary_owner = NewOwner(original.scheduler);
        auto secondary_owner = NewOwner(secondary_executor.scheduler());
        auto clock = std::make_shared<std::atomic<int64_t>>(0);
        auto clock_calls = std::make_shared<std::atomic<size_t>>(0);
        Limits limits; limits.now = [clock, clock_calls] { ++*clock_calls; return Limits::Time{} + std::chrono::milliseconds{clock->load()}; };
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner);
        auto secondary = Service::Create(secondary_executor.scheduler(), secondary_owner, limits);
        auto release = std::make_shared<std::promise<void>>(); const auto gate = release->get_future().share(); bool released{false};
        const auto cleanup = qScopeGuard([&] {
            if (!released) release->set_value();
            primary->stop(); secondary->stop(); original.drain(); primary_owner.reset(); secondary_owner.reset(); original.drain();
            Shutdown(primary_executor); Shutdown(secondary_executor); original.drain();
        });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto forwarded = Start(secondary, directory.path(), {Request(1)});
        QVERIFY(QTest::qWaitFor([&] { return original.queuedCount() > 0; }, 8000));
        auto entered = std::make_shared<std::atomic_bool>(false);
        QVERIFY(execution::Submit(*primary_executor.scheduler(), [gate, entered] { *entered = true; gate.wait(); }, [] {}, {}, {execution::Lane::Core, {}}));
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
        ManualRoutedExecution original;
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}};
        auto primary_owner = NewOwner(original.scheduler);
        auto secondary_owner = NewOwner(secondary_executor.scheduler());
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner);
        auto secondary = Service::Create(secondary_executor.scheduler(), secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); original.drain(); primary_owner.reset(); secondary_owner.reset(); original.drain(); Shutdown(primary_executor); Shutdown(secondary_executor); original.drain(); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto forwarded = Start(secondary, directory.path(), {Request(1)});
        QVERIFY(QTest::qWaitFor([&] { return original.queuedCount() > 0; }, 8000));
        auto retired = Stop(primary); QVERIFY(Ready(retired)); QVERIFY(primary->retired());
        QVERIFY(Ready(forwarded)); QVERIFY(forwarded.get().status == StartStatus::Unknown);
        // The delayed foreign callback is delivered after Native QObject/socket
        // destruction. Its reply cell must have revoked that receiver already.
        original.drain(); primary_owner.reset(); original.drain();
        QVERIFY(Shutdown(primary_executor)); QVERIFY(primary);
    }
    void backwardClockDoesNotAuthorizeReplayOrLateAcknowledgment()
    {
        ManualRoutedExecution original;
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}};
        auto primary_owner = NewOwner(original.scheduler);
        auto secondary_owner = NewOwner(secondary_executor.scheduler());
        auto clock = std::make_shared<std::atomic<int64_t>>(1000);
        Limits limits; limits.now = [clock] { return Limits::Time{} + std::chrono::milliseconds{clock->load()}; };
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner);
        auto secondary = Service::Create(secondary_executor.scheduler(), secondary_owner, limits);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); original.drain(); primary_owner.reset(); secondary_owner.reset(); original.drain(); Shutdown(primary_executor); Shutdown(secondary_executor); original.drain(); });
        QTemporaryDir directory; QVERIFY(directory.isValid());
        auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto forwarded = Start(secondary, directory.path(), {Request(1)});
        QVERIFY(QTest::qWaitFor([&] { return original.queuedCount() > 0; }, 8000));
        const auto original_turns = original.queuedCount(); *clock = 0;
        QVERIFY(Ready(forwarded)); QVERIFY(forwarded.get().status == StartStatus::Unknown);
        QCOMPARE(original.queuedCount(), original_turns); // No second admit and no query after global deadline invalidation.
        original.drain();
    }
    void invalidDeadlineConfigurationDoesNotReserveResources()
    {
        ManualRoutedExecution commands;
        auto owner = NewOwner(commands.scheduler);
        Limits limits; limits.acknowledge = 31s;
        QVERIFY(!Service::Create(commands.scheduler, owner, limits));
        limits = {}; limits.now = {};
        QVERIFY(!Service::Create(commands.scheduler, owner, limits));
        owner.reset(); commands.drain(); QCOMPARE(commands.scheduler->pending(), size_t{0});
    }
    void missingProbeLeavesOriginalQueueAndLaterFinalProfileStartUntouched()
    {
        BackendExecutor executor{execution::Options{}}, rival_executor{execution::Options{}};
        auto owner = NewOwner(executor.scheduler()), rival_owner = NewOwner(rival_executor.scheduler());
        auto service = Service::Create(executor.scheduler(), owner), rival = Service::Create(rival_executor.scheduler(), rival_owner);
        const auto cleanup = qScopeGuard([&] { service->stop(); rival->stop(); owner.reset(); rival_owner.reset(); Shutdown(executor); Shutdown(rival_executor); });
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
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}};
        auto primary_owner = NewOwner(primary_executor.scheduler()), secondary_owner = NewOwner(secondary_executor.scheduler());
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner), secondary = Service::Create(secondary_executor.scheduler(), secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); primary_owner.reset(); secondary_owner.reset(); Shutdown(primary_executor); Shutdown(secondary_executor); });
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
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}};
        auto primary_owner = NewOwner(primary_executor.scheduler()), secondary_owner = NewOwner(secondary_executor.scheduler());
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner), secondary = Service::Create(secondary_executor.scheduler(), secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); primary_owner.reset(); secondary_owner.reset(); Shutdown(primary_executor); Shutdown(secondary_executor); });
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
        ManualRoutedExecution original;
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}};
        auto primary_owner = NewOwner(original.scheduler), secondary_owner = NewOwner(secondary_executor.scheduler());
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner), secondary = Service::Create(secondary_executor.scheduler(), secondary_owner);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); original.drain(); primary_owner.reset(); secondary_owner.reset(); original.drain(); Shutdown(primary_executor); Shutdown(secondary_executor); original.drain(); });
        QTemporaryDir directory; QVERIFY(directory.isValid()); auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto probed = Probe(secondary, directory.path(), {Request(1)});
        QVERIFY(QTest::qWaitFor([&] { return original.queuedCount() > 0; }, 8000));
        auto retired = Stop(primary); QVERIFY(Ready(retired));
        QVERIFY(Ready(probed)); const auto result = probed.get(); QVERIFY(result.status == ProbeStatus::Unknown); QVERIFY(result.transfer.status == StartStatus::Unknown);
        QCOMPARE(result.transfer.producer_keys, std::vector<QString>{QStringLiteral("1")}); QVERIFY(result.transfer.receipts.empty());
        QVERIFY(!secondary->start(directory.filePath(QStringLiteral("new-profile")), QStringLiteral("regtest"), {Request(1)}, {}));
        original.drain();
    }
    void stopBeforeProbeNativeTurnRetiresWithoutLookupOrPrimary()
    {
        ManualRoutedExecution commands; auto owner = NewOwner(commands.scheduler); auto service = Service::Create(commands.scheduler, owner); QVERIFY(service);
        auto probed = Probe(service, QStringLiteral("/unused-profile"), {Request(1)});
        QVERIFY(!service->start(QStringLiteral("/later-profile"), QStringLiteral("regtest"), {}, {}));
        QVERIFY(!service->probeExisting(QStringLiteral("/unused-profile"), QStringLiteral("regtest"), {}, {}));
        auto retired = Stop(service);
        commands.drain(); QVERIFY(Ready(probed)); QVERIFY(probed.get().status == ProbeStatus::Rejected); QVERIFY(Ready(retired)); QVERIFY(service->retired());
        owner.reset(); commands.drain(); QCOMPARE(commands.scheduler->pending(), size_t{0});
    }
    void expiredProbeClockCannotForwardOrGrantPrimary_data()
    {
        QTest::addColumn<bool>("rollback"); QTest::newRow("expired") << false; QTest::newRow("rollback") << true;
    }
    void expiredProbeClockCannotForwardOrGrantPrimary()
    {
        QFETCH(bool, rollback);
        BackendExecutor primary_executor{execution::Options{}}, secondary_executor{execution::Options{}};
        auto primary_owner = NewOwner(primary_executor.scheduler()), secondary_owner = NewOwner(secondary_executor.scheduler());
        Limits limits; auto calls = std::make_shared<std::atomic_size_t>(0);
        limits.now = [calls, rollback] { return Limits::Time{((*calls)++ == 0 ? 100ms : (rollback ? 0ms : 6000ms))}; };
        auto primary = Service::Create(primary_executor.scheduler(), primary_owner), secondary = Service::Create(secondary_executor.scheduler(), secondary_owner, limits);
        const auto cleanup = qScopeGuard([&] { primary->stop(); secondary->stop(); primary_owner.reset(); secondary_owner.reset(); Shutdown(primary_executor); Shutdown(secondary_executor); });
        QTemporaryDir directory; QVERIFY(directory.isValid()); auto listening = Start(primary, directory.path()); QVERIFY(Ready(listening)); QVERIFY(listening.get().status == StartStatus::Primary);
        auto probed = Probe(secondary, directory.path(), {Request(1)}); QVERIFY(Ready(probed)); QVERIFY(probed.get().status == ProbeStatus::Rejected);
        auto unadmitted = Lookup(primary_owner, QStringLiteral("1")); QVERIFY(Ready(unadmitted)); QVERIFY(!unadmitted.get());
        auto unchanged = Lookup(secondary_owner, QStringLiteral("1")); QVERIFY(Ready(unchanged)); QVERIFY(!unchanged.get());
        QVERIFY(!secondary->start(directory.filePath(QStringLiteral("successor")), QStringLiteral("regtest"), {Request(1)}, {}));
    }
};
BITCOINQML_REGISTER_QT_TEST(DesktopIntentTransportTests)
#include <test_desktopintenttransport.moc>
