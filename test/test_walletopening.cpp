// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/backendexecutor.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/retainedvalue.h>
#include <qml/walletopening.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualwalletdrain.h>
#include <test/mocks/feepreviewchain.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QtTest/QtTest>

#include <stdexcept>
#include <algorithm>
#include <limits>
#include <array>
#include <barrier>
#include <thread>

namespace {
using execution::Lane;
using walletopening::Phase;
struct Wallet final : StubWallet {
    std::string getWalletName() override { ++reads; if (fail_read) throw std::runtime_error("synthetic metadata failure"); return name; }
    bool privateKeysDisabled() override { return no_keys; }
    bool hasExternalSigner() override { return external; }
    bool isCrypted() override { return true; }
    bool isLocked() override { return locked; }
    bool canGetAddresses() override { return addresses; }
    bool taprootEnabled() override { return taproot; }
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn fn) override { address_capability = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn fn) override { status = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn fn) override { unload = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn) override { transaction = std::move(fn); return {}; }
    std::unique_ptr<interfaces::Handler> handleAddressBookChanged(AddressBookChangedFn fn) override { address = std::move(fn); return {}; }
    std::string name{"original"};
    int reads{0};
    bool fail_read{false};
    bool no_keys{false}, external{false}, locked{true}, addresses{true}, taproot{true};
    CanGetAddressesChangedFn address_capability;
    StatusChangedFn status;
    UnloadFn unload;
    TransactionChangedFn transaction;
    AddressBookChangedFn address;
};
struct Fixture {
    ManualRoutedExecution commands, retirement;
    std::shared_ptr<Wallet> wallet{std::make_shared<Wallet>()};
    int wakes{0};
    std::shared_ptr<walletopening::Session> session{std::make_shared<walletopening::Session>(
        commands.scheduler, retirement.scheduler, std::make_shared<std::atomic_bool>(false), [this] { ++wakes; })};
    ~Fixture() { if (session) session->retire(); commands.drain(); retirement.drain(); }
    void start()
    {
        QVERIFY(session->start(wallet));
        commands.drain();
        QVERIFY(session->take()->metadata.has_value());
    }
    walletopening::Question question()
    {
        session->requestClose();
        commands.drain();
        const auto update = session->take();
        if (!update || !update->question) { QTest::qFail("Missing close question", __FILE__, __LINE__); return {}; }
        return *update->question;
    }
};
}

class WalletOpeningTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void unavailableRetainedValuePortDoesNotConstruct()
    {
        int constructed{0};
        const auto factory = [&] { ++constructed; return std::make_unique<int>(42); };
        QVERIFY(!walletopening::RetainValues<int>(std::shared_ptr<walletopening::Session>{}, factory));
        QCOMPARE(constructed, 0);
    }
    void failedRefreshCannotAcknowledgeUnreconciledTransactions_data()
    {
        QTest::addColumn<bool>("later_transaction");
        QTest::newRow("failed-notice-needs-rescan") << false;
        QTest::newRow("new-notice-during-failed-refresh") << true;
    }
    void failedRefreshCannotAcknowledgeUnreconciledTransactions()
    {
        QFETCH(bool, later_transaction);
        Fixture fixture; fixture.start();
        fixture.wallet->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        if (later_transaction) fixture.wallet->transaction(Txid{}, CT_UPDATED);
        fixture.wallet->fail_read = true; fixture.commands.drain();
        fixture.session->acknowledgeTransactions();
        QVERIFY(fixture.session->notificationsPending());
        const auto failed = fixture.session->take(); QVERIFY(failed && failed->failed);
        fixture.session->retire();
        fixture.session->retryNotifications(); fixture.commands.drain();
        QVERIFY(fixture.commands.quiescent());
    }
    void callbackRacingShutdownCannotRefreshTheClosingOpening()
    {
        struct Boundary final : execution::Scheduler {
            explicit Boundary(std::shared_ptr<execution::Scheduler> target) : target(std::move(target)) {}
            execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return target->submit(std::move(task), spec); }
            execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return target->reserveCleanup(std::move(task), spec); }
            bool activateCleanup(execution::TaskId id) override { return target->activateCleanup(id); }
            execution::BorrowAdmission borrow(execution::ResourceKey key) override {
                auto borrowed = target->borrow(key);
                if (after_borrow) std::exchange(after_borrow, {})();
                return borrowed;
            }
            execution::ResourceKey allocateKey() override { return target->allocateKey(); }
            size_t pending() const override { return target->pending(); }
            void closeOrdinaryAdmission() override { target->closeOrdinaryAdmission(); }
            bool cancelQueued(execution::TaskId id) override { return target->cancelQueued(id); }
            const std::shared_ptr<execution::Scheduler> target;
            std::function<void()> after_borrow;
        };
        ManualRoutedExecution commands, retirement;
        auto boundary = std::make_shared<Boundary>(retirement.scheduler);
        auto gate = std::make_shared<std::atomic_bool>(false);
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, boundary,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            [] { return std::chrono::steady_clock::now(); }, gate);
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        boundary->after_borrow = [&] { gate->store(true); };
        native->status(); commands.drain();
        QVERIFY(!session->available()); QCOMPARE(native->reads, 1); QVERIFY(!session->take());
        QVERIFY(commands.quiescent());
    }
    void transactionAcknowledgementWaitsForBothRefreshAndConsumer()
    {
        Fixture fixture; fixture.start();
        fixture.wallet->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        fixture.session->acknowledgeTransactions(); QVERIFY(fixture.session->notificationsPending());
        fixture.commands.drain();
        fixture.session->acknowledgeTransactions(); QVERIFY(fixture.session->notificationsPending());
        const auto update = fixture.session->take();
        QVERIFY(update && update->transactions.size() == 1); QVERIFY(!update->rescan);
        fixture.session->acknowledgeTransactions(); QVERIFY(!fixture.session->notificationsPending());
        fixture.commands.rejectLane(Lane::Domain);
        fixture.session->onTransactions([](auto, auto, bool) {});
        fixture.commands.drain();
        fixture.session->acknowledgeTransactions(); QVERIFY(fixture.session->notificationsPending());
        const auto rescan = fixture.session->take();
        QVERIFY(rescan && rescan->rescan && rescan->transactions.empty());
        fixture.session->acknowledgeTransactions(); QVERIFY(!fixture.session->notificationsPending());
    }
    void publicPreferencePortRepairsMalformedSettingsWithoutCopyingTheirPayload()
    {
        struct Settings final : FeePreviewChain {
            common::SettingsValue getRwSetting(const std::string&) override { return values; }
            bool updateRwSetting(const std::string&, const interfaces::SettingsUpdate& update) override
            {
                const auto action = update(values);
                return action && *action == interfaces::SettingsAction::WRITE;
            }
            common::SettingsValue values{std::string(65'537, 'x')};
        } settings;
        const auto port = walletopening::ChainPreferences(settings);
        QCOMPARE(port.read("original"), std::string{});
        QVERIFY(port.write("original", "legacy"));
        QVERIFY(settings.values.isObject()); QCOMPARE(settings.values.size(), size_t{1});
        QCOMPARE(port.read("original"), std::string{"legacy"});
    }
    void retirementBeforeStartRejectsEveryNativeAction()
    {
        Fixture fixture;
        fixture.session->retire();
        QVERIFY(!fixture.session->available()); QVERIFY(!fixture.session->borrow());
        QVERIFY(!fixture.session->startAction());
        QCOMPARE(fixture.session->read([](auto&) { return 1; }, [](int) {}).rejection, execution::Rejection::Closed);
        QVERIFY(fixture.commands.quiescent()); QVERIFY(fixture.retirement.quiescent());
    }
    void unavailableNativeHandleNeverExecutesRead()
    {
        Fixture fixture;
        int effects{0};
        QCOMPARE(fixture.session->read([&](auto&) { return ++effects; }, [](int) {}).rejection,
                 execution::Rejection::Closed);
        QCOMPARE(effects, 0);
        fixture.commands.rejectLane(Lane::Core);
        const auto started = fixture.session->start(fixture.wallet);
        QVERIFY(!started);
        QCOMPARE(fixture.wallet->reads, 0);
        QVERIFY(!fixture.wallet->status);
        QVERIFY(!fixture.session->available());
    }
    void rejectedCloseControlNeverGrantsConsent()
    {
        Fixture fixture; fixture.start();
        fixture.commands.rejectLane(Lane::Control); fixture.session->requestClose(); fixture.commands.drain();
        const auto failed_request = fixture.session->take();
        QVERIFY(failed_request && failed_request->failed); QVERIFY(!failed_request->question);
        QVERIFY(fixture.session->available());
        const auto question = fixture.question();
        fixture.commands.rejectLane(Lane::Control); fixture.session->answerClose(question, true); fixture.commands.drain();
        QVERIFY(fixture.session->take()->failed); QVERIFY(fixture.session->available());
        fixture.session->retire();
        fixture.session->answerClose(question, true); fixture.session->requestClose(); fixture.commands.drain();
        const auto closed = fixture.session->take();
        QVERIFY(closed); QCOMPARE(closed->phase, Phase::Closing); QVERIFY(!closed->question);
    }
    void shutdownGateCancelsQueuedObservationAndConsumesAnAbsentErrorObserver()
    {
        ManualRoutedExecution commands, retirement;
        auto gate = std::make_shared<std::atomic_bool>(false);
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            [] { return std::chrono::steady_clock::now(); }, gate);
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        int effects{0}, outcomes{0};
        QVERIFY(session->read([&](auto&) { return ++effects; }, [&](int) { ++outcomes; }));
        gate->store(true); commands.drain();
        QCOMPARE(effects, 0); QCOMPARE(outcomes, 0); QVERIFY(commands.quiescent());
        QVERIFY(!session->startAction());
    }
    void unsupportedOrUnconfirmedPreferenceNeverClaimsPersistence_data()
    {
        QTest::addColumn<bool>("addresses"); QTest::addColumn<bool>("write_ok");
        QTest::addColumn<bool>("taproot");
        QTest::newRow("no-address-capability") << false << true << true;
        QTest::newRow("write-failed") << true << false << true;
        QTest::newRow("write-succeeded-readback-differs") << true << true << true;
        QTest::newRow("unsupported-taproot") << true << true << false;
    }
    void unsupportedOrUnconfirmedPreferenceNeverClaimsPersistence()
    {
        QFETCH(bool, addresses); QFETCH(bool, write_ok); QFETCH(bool, taproot);
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>(); native->addresses = addresses; native->taproot = taproot;
        int writes{0};
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [](const auto&) { return std::string{"bech32"}; },
                [&](const auto&, const auto&) { ++writes; return write_ok; }});
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference(taproot ? "legacy" : "bech32m")); commands.drain();
        const auto result = session->take();
        QVERIFY(result && result->metadata && result->preference_saved);
        QVERIFY(!*result->preference_saved);
        QCOMPARE(result->metadata->receive_preference, std::string{"bech32"});
        QCOMPARE(result->metadata->can_get_addresses, addresses);
        // R30 separates temporary aggregate keypool/unlock availability from
        // per-type support. Unconfirmed readback never claims success; a known
        // unsupported Taproot request still must not call the write port.
        QCOMPARE(result->preference_revision, uint64_t{1}); QCOMPARE(writes, taproot ? 1 : 0);
    }
    void temporaryAddressUnavailabilityStillRequiresExactPreferenceReadback()
    {
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>(); native->addresses = false;
        std::string saved{"bech32"}; int writes{0};
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [&](const auto&) { return saved; },
                [&](const auto&, const auto& type) { ++writes; saved = type; return true; }});
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference("legacy")); commands.drain();
        const auto result = session->take(); QVERIFY(result && result->metadata && result->preference_saved);
        QVERIFY(*result->preference_saved); QCOMPARE(writes, 1); QCOMPARE(saved, std::string{"legacy"});
        QVERIFY(!result->metadata->can_get_addresses);
        QCOMPARE(result->metadata->receive_preference, std::string{"legacy"});
    }
    void failedStatusRefreshDoesNotInventTransactionWork()
    {
        Fixture fixture; fixture.start();
        fixture.wallet->fail_read = true; fixture.wallet->status(); fixture.commands.drain();
        const auto failed = fixture.session->take();
        QVERIFY(failed && failed->failed); QVERIFY(!failed->rescan);
        QVERIFY(!fixture.session->notificationsPending());
    }
    void concurrentNativeCallbacksReceiveUniqueOrderedStamps()
    {
        walletopening::NativeEventClock clock;
        constexpr size_t workers{4}, per_worker{4096};
        std::array<std::vector<uint64_t>, workers> results;
        std::barrier start{static_cast<std::ptrdiff_t>(workers)};
        std::vector<std::thread> threads;
        threads.reserve(workers);
        for (size_t worker{0}; worker < workers; ++worker) threads.emplace_back([&, worker] {
            results[worker].reserve(per_worker);
            start.arrive_and_wait();
            for (size_t i{0}; i < per_worker; ++i) results[worker].push_back(clock.next());
        });
        for (auto& thread : threads) thread.join();
        std::vector<uint64_t> ordered;
        ordered.reserve(workers * per_worker);
        for (const auto& result : results) {
            QVERIFY(std::is_sorted(result.begin(), result.end()));
            ordered.insert(ordered.end(), result.begin(), result.end());
        }
        std::sort(ordered.begin(), ordered.end());
        QCOMPARE(clock.value.load(), uint64_t{workers * per_worker});
        for (size_t i{0}; i < ordered.size(); ++i) QCOMPARE(ordered[i], uint64_t{i + 1});
    }
    void closeQuestionIdentityAndPhaseGuardsPreventConsentReuse()
    {
        auto opening = std::make_shared<const walletopening::Identity>();
        auto other = std::make_shared<const walletopening::Identity>();
        walletopening::State state{opening};
        using walletopening::Answer;
        QCOMPARE(state.answer({opening, 1, 0}, 0, true), Answer::Ignored);
        QVERIFY(state.requestClose(0)); const auto first = *state.question();
        QCOMPARE(state.answer({other, first.number, 0}, 0, true), Answer::Ignored);
        QCOMPARE(state.answer({opening, first.number + 1, 0}, 0, true), Answer::Ignored);
        QCOMPARE(state.answer(first, 1, true), Answer::Changed);
        const auto revised = *state.question();
        QVERIFY(revised.number != first.number); QCOMPARE(revised.consequences, uint64_t{1});
        QCOMPARE(state.answer(first, 1, false), Answer::Ignored);
        QCOMPARE(state.answer(revised, 1, false), Answer::Dismissed);
        QVERIFY(!state.question()); QCOMPARE(state.phase(), Phase::Open);
        QVERIFY(state.requestClose(1)); const auto accepted = *state.question();
        QCOMPARE(state.answer(accepted, 1, true), Answer::Close);
        QCOMPARE(state.phase(), Phase::Closing); QVERIFY(!state.question());
        QVERIFY(!state.requestClose(2));
        QCOMPARE(state.answer(accepted, 1, true), Answer::Ignored);
        QCOMPARE(state.answer(accepted, 1, false), Answer::Ignored);
    }
    void failingNativeCleanupStillClosesAndDrains_data()
    {
        QTest::addColumn<bool>("disconnect_fails"); QTest::addColumn<bool>("remove_fails");
        QTest::newRow("success") << false << false;
        QTest::newRow("disconnect-failed") << true << false;
        QTest::newRow("remove-failed") << false << true;
        QTest::newRow("both-failed") << true << true;
    }
    void failingNativeCleanupStillClosesAndDrains()
    {
        QFETCH(bool, disconnect_fails); QFETCH(bool, remove_fails);
        struct Native final : StubWallet {
            struct Handler final : interfaces::Handler {
                Native& owner;
                explicit Handler(Native& owner) : owner(owner) {}
                void disconnect() override {
                    ++owner.disconnects;
                    if (owner.disconnect_fails) throw std::runtime_error{"Disconnect failed"};
                }
            };
            std::string getWalletName() override { return "original"; }
            std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn) override { return std::make_unique<Handler>(*this); }
            void remove() override { ++removes; if (remove_fails) throw std::runtime_error{"Remove failed"}; }
            int disconnects{0}, removes{0}; bool disconnect_fails{false}, remove_fails{false};
        };
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Native>();
        native->disconnect_fails = disconnect_fails; native->remove_fails = remove_fails;
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        auto started = session->start(native); QVERIFY(started); started.borrower.reset();
        commands.drain(); session->take();
        session->retire(true); session->retire(false);
        QCOMPARE(native->disconnects, 0); QCOMPARE(native->removes, 0);
        retirement.drain(); commands.drain();
        const auto result = session->take();
        QVERIFY(result); QCOMPARE(result->phase, Phase::Closed);
        QCOMPARE(result->failed, disconnect_fails || remove_fails);
        QCOMPARE(native->disconnects, 1); QCOMPARE(native->removes, 1);
        QVERIFY(retirement.quiescent()); QVERIFY(commands.quiescent());
        QVERIFY(!session->borrow());
    }
    void unloadClockSaturationStillFencesTheOriginalOpening()
    {
        auto clock = std::make_shared<walletopening::NativeEventClock>();
        auto unloaded = std::make_shared<std::atomic<uint64_t>>(0);
        walletopening::NativeEvents missing_clock{{}, unloaded};
        missing_clock.observeUnload();
        QCOMPARE(unloaded->load(), uint64_t{0});
        walletopening::NativeEvents missing_target{clock, {}};
        missing_target.observeUnload();
        QCOMPARE(clock->value.load(), uint64_t{0});
        QCOMPARE(clock->next(), uint64_t{1});
        clock->value = std::numeric_limits<uint64_t>::max() - 1;
        walletopening::NativeEvents events{clock, unloaded};
        events.observeUnload();
        QCOMPARE(unloaded->load(), std::numeric_limits<uint64_t>::max());
        QCOMPARE(clock->next(), uint64_t{0});
        events.observeUnload();
        QCOMPARE(unloaded->load(), std::numeric_limits<uint64_t>::max());
        unloaded->store(0);
        events.observeUnload();
        QCOMPARE(unloaded->load(), std::numeric_limits<uint64_t>::max());
    }
    void localAdmissionCannotReopenTheSharedClosingFence()
    {
        auto gate = std::make_shared<std::atomic_bool>(false);
        walletopening::AdmissionFence first{gate}, second{gate}, local;
        QVERIFY(!first); QVERIFY(!second); QVERIFY(!local);
        first = true; local = true;
        QVERIFY(first); QVERIFY(local); QVERIFY(!second);
        gate->store(true); first = false; local = false;
        QVERIFY(first); QVERIFY(second); QVERIFY(!local);
        QCOMPARE(first.shared(), second.shared());
    }
    void closedRetirementAcquiresNoWalletOrHandlers()
    {
        ManualRoutedExecution commands, retirement;
        retirement.scheduler->close();
        auto native = std::make_shared<Wallet>();
        auto closed = std::make_shared<std::atomic_bool>(false);
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler, closed, [] {});
        const auto result = session->start(native);
        QCOMPARE(result.admission.rejection, execution::Rejection::Closed);
        QVERIFY(!result.borrower); QVERIFY(!session->available());
        QVERIFY(closed->load()); QCOMPARE(session->closedFlag(), closed);
        QCOMPARE(native.use_count(), 1L); QCOMPARE(native->reads, 0);
        QVERIFY(!native->status); QVERIFY(!native->unload);
        const auto update = session->take();
        QVERIFY(update && update->failed); QCOMPARE(update->phase, Phase::Closed);
        QVERIFY(!session->borrow()); QVERIFY(!session->startAction());
        QCOMPARE(session->start(native).admission.rejection, execution::Rejection::Invalid);
        commands.drain(); retirement.drain();
        QVERIFY(commands.quiescent()); QVERIFY(retirement.quiescent());
    }
    void addressNotificationsAndLateCopiedCallbacksKeepTheirOpening()
    {
        Fixture fixture; fixture.start();
        const auto address = fixture.wallet->address;
        const auto status = fixture.wallet->status;
        const auto capability = fixture.wallet->address_capability;
        const auto transaction = fixture.wallet->transaction;
        const auto unload = fixture.wallet->unload;
        QVERIFY(address && status && capability && transaction && unload);
        address({}, "label", false, wallet::AddressPurpose{}, CT_NEW);
        QVERIFY(!fixture.session->take());
        fixture.commands.drain();
        const auto update = fixture.session->take();
        QVERIFY(update && update->addresses && update->metadata);
        QCOMPARE(update->metadata->name, std::string{"original"});
        std::weak_ptr<walletopening::Session> old = fixture.session;
        fixture.session->retire(); fixture.commands.drain(); fixture.retirement.drain();
        fixture.session.reset(); QVERIFY(old.expired());
        const auto wakes = fixture.wakes;
        address({}, "late", false, wallet::AddressPurpose{}, CT_UPDATED);
        status(); capability(); transaction(Txid{}, CT_DELETED); unload();
        QCOMPARE(fixture.wakes, wakes);
        QVERIFY(fixture.commands.quiescent()); QVERIFY(fixture.retirement.quiescent());
    }
    void invalidPersistentMetadataCannotEscapeTheBoundedSnapshot_data()
    {
        QTest::addColumn<QString>("preference");
        QTest::addColumn<QString>("expected");
        for (const auto* value : {"legacy", "p2sh-segwit", "bech32", "bech32m"})
            QTest::newRow(value) << QString::fromLatin1(value) << QString::fromLatin1(value);
        QTest::newRow("empty") << QString{} << QString{};
        QTest::newRow("invalid") << QStringLiteral("unrecognized-type") << QString{};
        QTest::newRow("oversized-preference") << QString(32'769, QChar{'x'}) << QString{};
    }
    void invalidPersistentMetadataCannotEscapeTheBoundedSnapshot()
    {
        QFETCH(QString, preference); QFETCH(QString, expected);
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [preference](const std::string&) { return preference.toStdString(); }, {}});
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain();
        const auto update = session->take();
        QVERIFY(update && update->metadata && !update->failed);
        QCOMPARE(update->metadata->receive_preference, expected.toStdString());
    }
    void oversizedWalletMetadataFailsBeforeItCanBePublished()
    {
        Fixture fixture;
        fixture.wallet->name.assign(32'769, 'x');
        auto started = fixture.session->start(fixture.wallet);
        QVERIFY(started); started.borrower.reset();
        fixture.commands.drain(); fixture.retirement.drain();
        const auto update = fixture.session->take();
        QVERIFY(update && update->failed && !update->metadata);
        QCOMPARE(update->phase, Phase::Closed);
        QVERIFY(!fixture.session->available());
    }
    void preferenceExceptionReportsTheOriginalRevision_data()
    {
        QTest::addColumn<bool>("fail_write");
        QTest::newRow("write-throws-before-effect") << true;
        QTest::newRow("readback-throws-after-effect") << false;
    }
    void preferenceExceptionReportsTheOriginalRevision()
    {
        QFETCH(bool, fail_write);
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        bool fail_read{false}; int attempts{0}; std::string persisted;
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [&](const std::string&) { if (fail_read) throw std::runtime_error{"Readback failed"}; return persisted; },
                [&](const std::string&, const std::string& value) {
                    ++attempts;
                    if (fail_write) throw std::runtime_error{"Write failed"};
                    persisted = value; fail_read = true; return true;
                }});
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference("legacy"));
        const auto revision = session->preferenceRevision();
        QCOMPARE(attempts, 0); QVERIFY(!session->take());
        commands.drain(); const auto result = session->take();
        QVERIFY(result && result->preference_saved && !*result->preference_saved);
        QCOMPARE(result->preference_revision, revision); QCOMPARE(result->phase, Phase::Open);
        QCOMPARE(attempts, 1);
        QCOMPARE(persisted, fail_write ? std::string{} : std::string{"legacy"});
        commands.drain(); QCOMPARE(attempts, 1);
    }
    void lateTransactionConsumerReconcilesAfterDomainRejection()
    {
        Fixture fixture; fixture.start();
        fixture.wallet->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        fixture.commands.drain();
        QVERIFY(fixture.session->notificationsPending());
        const auto revision = fixture.session->transactionRevision();
        int deliveries{0}; bool rescan{false}; uint64_t delivered_revision{0};
        fixture.commands.rejectLane(Lane::Domain);
        fixture.session->onTransactions([&](uint64_t value, auto, bool full) {
            ++deliveries; delivered_revision = value; rescan = full;
        });
        QCOMPARE(deliveries, 0);
        fixture.session->acknowledgeTransactions();
        QVERIFY(fixture.session->notificationsPending());
        fixture.commands.drain();
        QCOMPARE(deliveries, 1); QVERIFY(rescan); QCOMPARE(delivered_revision, revision);
        fixture.session->take(); fixture.session->acknowledgeTransactions();
        QVERIFY(!fixture.session->notificationsPending());
        fixture.session->onTransactions({});
    }
    void transactionOverflowRetainsARescanAndNewestDuplicate()
    {
        Fixture fixture; fixture.start();
        const auto id = [](unsigned int value) {
            std::vector<unsigned char> bytes(32);
            bytes[0] = static_cast<unsigned char>(value % 256);
            bytes[1] = static_cast<unsigned char>(value / 256);
            return Txid::FromUint256(uint256{bytes});
        };
        for (unsigned int n{0}; n < 257; ++n) fixture.wallet->transaction(id(n), CT_NEW);
        fixture.wallet->transaction(id(1), CT_DELETED);
        fixture.wallet->transaction(id(257), CT_NEW);
        QCOMPARE(fixture.wallet->reads, 1);
        fixture.session->acknowledgeTransactions(); QVERIFY(fixture.session->notificationsPending());
        fixture.commands.drain();
        const auto result = fixture.session->take();
        QVERIFY(result && result->rescan); QVERIFY(result->transactions.size() <= 256);
        QCOMPARE(result->transaction_revision, uint64_t{259});
        QCOMPARE(result->transactions.at(id(1)), CT_DELETED);
        fixture.session->acknowledgeTransactions(); QVERIFY(!fixture.session->notificationsPending());
    }
    void closedOpeningDiscardsQueuedObservationButConsumesAdmittedDomainAction()
    {
        Fixture fixture; fixture.start();
        int observations{0}, actions{0};
        QVERIFY(fixture.session->postDomain([&] { ++observations; }));
        QVERIFY(fixture.session->postDomain([&] { ++actions; }, execution::WorkClass::Action));
        fixture.session->close();
        fixture.commands.drain();
        QCOMPARE(observations, 0); QCOMPARE(actions, 1);
        QCOMPARE(fixture.session->postDomain([] {}).rejection, execution::Rejection::Closed);
        QCOMPARE(fixture.session->postDomain([] {}, execution::WorkClass::Action).rejection, execution::Rejection::Closed);
    }
    void originalActionFailureAndForeignPermitsHaveDistinctOutcomes()
    {
        Fixture first, second; first.start(); second.start();
        auto action = first.session->startAction(); QVERIFY(action);
        int effects{0}, outcomes{0}, errors{0};
        QCOMPARE(second.session->planAction(action, [&] { ++effects; }).rejection, execution::Rejection::Invalid);
        QCOMPARE(second.session->perform(action, [&](auto&) { ++effects; return 1; }, [&](int) { ++outcomes; }).rejection,
                 execution::Rejection::Invalid);
        QCOMPARE(first.session->planAction({}, [] {}).rejection, execution::Rejection::Invalid);
        QCOMPARE(first.session->perform({}, [](auto&) { return 1; }, [](int) {}).rejection, execution::Rejection::Invalid);
        QVERIFY(first.session->perform(action, [&](auto&) -> int { ++effects; throw std::runtime_error{"Original write failed"}; },
            [&](int) { ++outcomes; }, [&](std::exception_ptr error) {
                QVERIFY(error); ++errors;
                try { std::rethrow_exception(error); }
                catch (const std::runtime_error& failure) { QCOMPARE(QString::fromUtf8(failure.what()), QStringLiteral("Original write failed")); }
            }));
        first.session->retire();
        first.commands.drain(); QCOMPARE(effects, 1); QCOMPARE(outcomes, 0); QCOMPARE(errors, 1);
        QVERIFY(first.session->perform(action, [](auto&) -> int { throw std::runtime_error{"Unobserved original failure"}; }, [](int) {}));
        first.commands.drain(); action.reset(); first.retirement.drain();
        QVERIFY(first.retirement.quiescent());
    }
    void originalActionScopeCompletesDependentEffectsAfterFullQueueClosure()
    {
        Fixture fixture; fixture.start();
        std::vector<QString> trace;
        auto action = fixture.session->startAction(); QVERIFY(action);
        auto session = fixture.session;
        QVERIFY(session->planAction(action, [session, action, &trace] {
            trace.emplace_back("plan-before-write");
            QVERIFY(session->perform(action, [&trace](interfaces::Wallet&) { trace.emplace_back("commit"); return true; },
                [session, action, &trace](bool committed) {
                    QVERIFY(committed); trace.emplace_back("actual-commit-outcome");
                    QVERIFY(session->planAction(action, [session, action, &trace] {
                        trace.emplace_back("plan-original-notes");
                        QVERIFY(session->perform(action, [&trace](interfaces::Wallet&) { trace.emplace_back("notes-write"); return true; },
                            [&trace](bool saved) { QVERIFY(saved); trace.emplace_back("actual-notes-outcome"); }));
                    }));
                }));
        }));
        fixture.session->close(); fixture.commands.scheduler->close();
        fixture.session->retire(); QVERIFY(!fixture.session->startAction()); action.reset();
        QVERIFY(!fixture.retirement.step(Lane::Cleanup)); // Original accepted action still pins its native wallet.
        QCOMPARE(trace.size(), size_t{0});
        fixture.commands.drain();
        QCOMPARE(trace, std::vector<QString>({"plan-before-write", "commit", "actual-commit-outcome", "plan-original-notes", "notes-write", "actual-notes-outcome"}));
        fixture.retirement.drain();
        QCOMPARE(fixture.session->take()->phase, Phase::Closed);
        QVERIFY(fixture.commands.quiescent()); QVERIFY(fixture.retirement.quiescent());
    }
    void facadeSeparatesSecurityAdmissionEffectOutcomeAndPublication()
    {
        struct Native final : StubWallet {
            std::string getWalletName() override { return "original"; }
            bool isCrypted() override { return encrypted; }
            bool isLocked() override { return encrypted; }
            bool encryptWallet(const SecureString& password) override { ++effects; encrypted = password == SecureString{"secret"}; return encrypted; }
            int effects{0}; bool encrypted{false};
        };
        ManualRoutedExecution commands;
        auto native = std::make_shared<Native>();
        WalletQmlModel model{WalletQmlModel::SharedWallet{native, commands.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        QVERIFY(model.capabilitiesKnown());
        QVERIFY(model.encryptWallet("secret"));
        QVERIFY(!model.encryptWallet("duplicate"));
        QCOMPARE(native->effects, 0);
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecurityPending);
        QVERIFY(commands.step(Lane::Core));
        QCOMPARE(native->effects, 1);
        QVERIFY(!model.isEncrypted());
        QVERIFY(commands.step(Lane::Domain));
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecurityPending);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(model.securityState(), WalletQmlModel::SecurityState::SecuritySucceeded);
        // Only Session metadata can update observed security; consume its
        // fresh Native read after the operation's historical outcome.
        commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(model.isEncrypted()); QVERIFY(model.isLocked());
        commands.drain();
    }
    void actualSecurityActionInvalidatesOlderCloseConsent()
    {
        struct Native final : StubWallet {
            std::string getWalletName() override { return "original"; }
            bool encryptWallet(const SecureString&) override { ++effects; return true; }
            int effects{0};
        };
        ManualRoutedExecution commands;
        auto native = std::make_shared<Native>();
        WalletQmlModel model{WalletQmlModel::SharedWallet{native, commands.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        model.requestClose(); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        const auto original_question = model.closeQuestionToken();
        QVERIFY(!original_question.isEmpty());
        QVERIFY(model.available());
        QVERIFY(model.encryptWallet("secret"));
        model.answerClose(original_question, true);
        commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(native->effects, 1);
        QVERIFY(model.available());
        QVERIFY(!model.closeQuestionToken().isEmpty());
        QVERIFY(model.closeQuestionToken() != original_question);
        model.answerClose(original_question, true); commands.drain();
        QVERIFY(model.available());
    }
    void retirementClosingDuringStartupDoesNotAcquireNativeOwnership()
    {
        struct Closing final : execution::Scheduler {
            explicit Closing(std::shared_ptr<execution::TaskQueue> queue) : queue(std::move(queue)) {}
            execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return queue->submit(std::move(task), spec); }
            execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override
            {
                const auto result = queue->reserveCleanup(std::move(task), spec);
                queue->close();
                return result;
            }
            bool activateCleanup(execution::TaskId id) override { return queue->activateCleanup(id); }
            execution::BorrowAdmission borrow(execution::ResourceKey key) override { return queue->borrow(key); }
            execution::ResourceKey allocateKey() override { return queue->allocateKey(); }
            size_t pending() const override { return queue->pending(); }
            void closeOrdinaryAdmission() override { queue->closeOrdinaryAdmission(); }
            bool cancelQueued(execution::TaskId id) override { return queue->cancelQueued(id); }
            const std::shared_ptr<execution::TaskQueue> queue;
        };
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, std::make_shared<Closing>(retirement.scheduler),
            std::make_shared<std::atomic_bool>(false), [] {});
        const auto started = session->start(native);
        QCOMPARE(started.admission.rejection, execution::Rejection::Closed);
        QVERIFY(!started.borrower);
        QCOMPARE(native.use_count(), 1L);
        QCOMPARE(native->reads, 0);
        retirement.drain(); commands.drain();
        QVERIFY(retirement.quiescent());
        QCOMPARE(session->take()->phase, Phase::Closed);
    }
    void startupFailureCannotRetireTheReturnedClientBeforeHandoff()
    {
        struct BrokenWallet final : StubWallet {
            std::string getWalletName() override { throw std::runtime_error("synthetic startup capture failure"); }
        };
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<BrokenWallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {});
        auto started = session->start(native);
        QVERIFY(started);
        QVERIFY(started.borrower);
        commands.drain();
        QVERIFY(!session->available());
        QVERIFY(!retirement.step(Lane::Cleanup));
        QCOMPARE(started.borrower.get(), native.get());
        started.borrower.reset();
        retirement.drain();
        QVERIFY(retirement.quiescent());
    }
    void duplicateOpeningStartDoesNotRetireItsLiveResource()
    {
        Fixture f; f.start();
        QCOMPARE(f.session->start(f.wallet).admission.rejection, execution::Rejection::Invalid);
        QVERIFY(f.session->available());
        QVERIFY(f.session->borrow());
    }
    void registrationAndCaptureWaitForCoreAndDomain()
    {
        Fixture f;
        QVERIFY(f.session->start(f.wallet));
        QCOMPARE(f.wallet->reads, 0);
        QVERIFY(!f.wallet->status);
        QVERIFY(!f.session->take());
        QVERIFY(f.commands.step(Lane::Core));
        QCOMPARE(f.wallet->reads, 1);
        QVERIFY(f.wallet->status);
        QCOMPARE(f.wakes, 0);
        QVERIFY(f.commands.step(Lane::Domain));
        QCOMPARE(f.wakes, 1);
        const auto update = f.session->take();
        QVERIFY(update && update->metadata);
        QCOMPARE(update->metadata->name, std::string{"original"});
        QVERIFY(update->metadata->encrypted);
        QVERIFY(update->metadata->locked);
    }
    void receiveCapabilitiesRefreshFromTheirActualProducer()
    {
        Fixture f; f.start();
        QVERIFY(f.wallet->address_capability);
        f.wallet->taproot = false;
        f.wallet->address_capability();
        QVERIFY(!f.session->take());
        f.commands.drain();
        const auto update = f.session->take();
        QVERIFY(update && update->metadata);
        QVERIFY(update->metadata->can_get_addresses);
        QVERIFY(!update->metadata->taproot);
    }
    void queuedPreferenceChangesKeepLatestOutcomePending()
    {
        ManualRoutedExecution commands;
        auto native = std::make_shared<Wallet>();
        std::string saved;
        int writes{0};
        walletopening::Session::Preferences preferences{
            [&](const std::string&) { return saved; },
            [&](const std::string&, const std::string& type) { saved = type; ++writes; return true; }};
        WalletQmlModel model{WalletQmlModel::SharedWallet{native, commands.scheduler, {}, std::move(preferences)}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain cleanup{commands, &model};
        for (int n{0}; n < 4; ++n) { commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); }
        QVERIFY(model.setDefaultReceiveAddressType("legacy"));
        QVERIFY(model.setDefaultReceiveAddressType("bech32"));
        QCOMPARE(writes, 0);
        const auto write = [&](int expected) -> std::optional<execution::TaskId> {
            for (int turn{0}; turn < 64 && writes < expected; ++turn) {
                if (const auto id = commands.dispatchNext(Lane::Core)) {
                    if (!commands.finishWork(*id)) return {};
                    if (writes == expected) return id;
                } else if (!commands.step(Lane::Domain)) break;
            }
            return {};
        };
        const auto consume = [&](execution::TaskId original) {
            for (int turn{0}; turn < 64; ++turn) {
                std::optional<ManualRoutedExecution::TurnInfo> front;
                commands.queuedCountIf([&](const auto& value) {
                    if (!front && value.lane == Lane::Domain) front = value;
                    return false;
                });
                if (front && front->id == original && front->phase != execution::Phase::OutcomeReady) return false;
                if (!front || !commands.step(Lane::Domain)) return false;
                if (front->id == original) return true;
            }
            return false;
        };
        const auto first = write(1); QVERIFY(first); QVERIFY(consume(*first));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(writes, 1);
        QVERIFY(model.property("receivePreferencePending").toBool());
        const auto second = write(2); QVERIFY(second);
        // Metadata publication can enqueue other Domain work. Native success
        // alone cannot settle pending: consume the exact second write outcome.
        QVERIFY(model.property("receivePreferencePending").toBool());
        QVERIFY(consume(*second));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(writes, 2);
        QVERIFY(!model.property("receivePreferencePending").toBool());
        QCOMPARE(saved, std::string{"bech32"});
        QCOMPARE(model.defaultReceiveAddressType(), QString{"bech32"});
    }
    void publicSettingsPortPreservesOtherWalletAndReportsWriteFailure_data()
    {
        QTest::addColumn<bool>("write_ok");
        QTest::newRow("persisted") << true;
        QTest::newRow("write-failed-memory-kept") << false;
    }
    void publicSettingsPortPreservesOtherWalletAndReportsWriteFailure()
    {
        QFETCH(bool, write_ok);
        struct Settings final : FeePreviewChain {
            common::SettingsValue getRwSetting(const std::string&) override { return values; }
            bool updateRwSetting(const std::string&, const interfaces::SettingsUpdate& update) override
            {
                ++writes;
                const auto action = update(values);
                return action && *action == interfaces::SettingsAction::WRITE && write_ok;
            }
            common::SettingsValue values{UniValue::VOBJ};
            int writes{0}; bool write_ok{true};
        } settings;
        settings.write_ok = write_ok;
        settings.values.pushKV("other-wallet", "bech32");
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::ChainPreferences(settings));
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QVERIFY(session->setReceivePreference("legacy"));
        QCOMPARE(settings.writes, 0);
        commands.drain();
        const auto result = session->take();
        QVERIFY(result && result->preference_saved && result->metadata);
        QCOMPARE(*result->preference_saved, write_ok);
        QCOMPARE(result->metadata->receive_preference, std::string{"legacy"});
        QCOMPARE(settings.values["other-wallet"].get_str(), std::string{"bech32"});
        commands.drain(); QCOMPARE(settings.writes, 1);
    }
    void receivePreferenceSeparatesEffectOutcomeAndCachesReadback()
    {
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        std::string saved;
        int writes{0};
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{
                [&](const std::string&) { return saved; },
                [&](const std::string& name, const std::string& type) { if (name != "original") QTest::qFail("Preference retargeted", __FILE__, __LINE__); saved = type; ++writes; return true; }});
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        QCOMPARE(session->setReceivePreference("invalid").rejection, execution::Rejection::Invalid);
        QVERIFY(session->setReceivePreference("p2sh-segwit"));
        QCOMPARE(writes, 0);
        QVERIFY(commands.step(Lane::Core));
        QCOMPARE(writes, 1); QCOMPARE(saved, std::string{"p2sh-segwit"});
        QVERIFY(!session->take());
        QVERIFY(commands.step(Lane::Domain));
        auto update = session->take();
        QVERIFY(update && update->preference_saved && *update->preference_saved && update->metadata);
        QCOMPARE(update->metadata->receive_preference, saved);
        native->taproot = false;
        QVERIFY(session->setReceivePreference("bech32m")); commands.drain();
        update = session->take();
        QVERIFY(update && update->preference_saved && !*update->preference_saved);
        QCOMPARE(writes, 1); QCOMPARE(saved, std::string{"p2sh-segwit"});
    }
    void closeQuestionDoesNotFenceNewActionsAndOldConsentCannotClose()
    {
        Fixture f; f.start();
        const auto before = f.question();
        QVERIFY(f.session->available());
        QVERIFY(f.session->admitAction());
        f.session->answerClose(before, true);
        f.commands.drain();
        QVERIFY(f.session->available());
        const auto changed = f.session->take();
        QVERIFY(changed && changed->question);
        QVERIFY(changed->question->number != before.number);
        f.session->answerClose(before, true);
        f.commands.drain();
        QVERIFY(f.session->available());
        f.session->answerClose(*changed->question, false);
        f.commands.drain();
        const auto dismissed = f.session->take();
        QVERIFY(dismissed && !dismissed->question);
        QVERIFY(f.session->available());
    }
    void currentConsentFencesAdmissionAndWaitsForBorrower()
    {
        Fixture f; f.start();
        auto held = f.session->borrow(); QVERIFY(held);
        const auto question = f.question();
        f.session->answerClose(question, true);
        f.commands.drain();
        QVERIFY(!f.session->available());
        QVERIFY(!f.session->admitAction());
        QVERIFY(!f.session->borrow());
        QVERIFY(!f.retirement.step(Lane::Cleanup));
        held.reset();
        QVERIFY(f.retirement.step(Lane::Cleanup));
        const auto before_outcome = f.session->take();
        QVERIFY(before_outcome && before_outcome->phase == Phase::Closing);
        QVERIFY(f.retirement.step(Lane::Domain));
        const auto closed = f.session->take();
        QVERIFY(closed && closed->phase == Phase::Closed && !closed->question);
        QVERIFY(f.retirement.quiescent());
    }
    void rejectedLatePreferenceCannotReopenClosingPublication()
    {
        Fixture f; f.start();
        auto held = f.session->borrow();
        f.session->answerClose(f.question(), true); f.commands.drain();
        QVERIFY(!f.session->available());
        QCOMPARE(f.session->setReceivePreference("legacy").rejection, execution::Rejection::Closed);
        const auto update = f.session->take();
        QVERIFY(update && update->phase == Phase::Closing && !update->question);
        held.reset(); f.retirement.drain();
        QCOMPARE(f.session->take()->phase, Phase::Closed);
    }
    void oldQuestionCannotCloseReopenedWalletWithSameName()
    {
        Fixture old; old.start(); const auto question = old.question();
        old.session->retire(); old.retirement.drain();
        Fixture reopened; reopened.start();
        QVERIFY(old.session->identity() != reopened.session->identity());
        reopened.question();
        reopened.session->answerClose(question, true); reopened.commands.drain();
        QVERIFY(reopened.session->available());
        QVERIFY(reopened.session->take()->question.has_value());
    }
    void externalUnloadSupersedesQuestionBeforeGuiDelivery()
    {
        Fixture f; f.start(); const auto question = f.question();
        QVERIFY(f.wallet->unload);
        f.wallet->unload();
        QVERIFY(!f.session->available());
        f.session->answerClose(question, true);
        f.commands.drain(); f.retirement.drain();
        const auto update = f.session->take();
        QVERIFY(update && update->unloaded && update->phase == Phase::Closed && !update->question);
    }
    void admittedReadRetainsOutcomeUntilDomainConsumption()
    {
        Fixture f; f.start();
        int outcomes{0};
        QVERIFY(f.session->read([](interfaces::Wallet&) { return 42; }, [&](int value) { QCOMPARE(value, 42); ++outcomes; }));
        QVERIFY(f.commands.step(Lane::Core));
        f.session->retire();
        QVERIFY(!f.retirement.step(Lane::Cleanup));
        QCOMPARE(outcomes, 0);
        QVERIFY(f.commands.step(Lane::Domain));
        QCOMPARE(outcomes, 1);
        f.retirement.drain();
        QVERIFY(f.retirement.quiescent());
    }
    void rejectedNoticeRetainsReconciliationAndUsesControlledBackoff()
    {
        ManualRoutedExecution commands, retirement;
        auto native = std::make_shared<Wallet>();
        std::chrono::steady_clock::time_point now{};
        auto session = std::make_shared<walletopening::Session>(commands.scheduler, retirement.scheduler,
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{}, [&] { return now; });
        const auto cleanup = qScopeGuard([&] { session->retire(); commands.drain(); retirement.drain(); });
        QVERIFY(session->start(native)); commands.drain(); session->take();
        commands.rejectLane(Lane::Core);
        native->transaction(Txid::FromUint256(uint256::ONE), CT_NEW);
        QCOMPARE(native->reads, 1);
        QVERIFY(session->notificationsPending());
        QVERIFY(session->take()->failed);
        for (int n{0}; n < 100; ++n) { session->retryNotifications(); native->status(); }
        QVERIFY(commands.queuedIdle());
        now += std::chrono::milliseconds{999}; session->retryNotifications();
        QVERIFY(commands.queuedIdle());
        now += std::chrono::milliseconds{1}; session->retryNotifications();
        native->fail_read = true; commands.drain();
        QVERIFY(session->take()->failed);
        const auto before = native->reads;
        now += std::chrono::milliseconds{1999}; session->retryNotifications();
        QVERIFY(commands.queuedIdle()); QCOMPARE(native->reads, before);
        native->fail_read = false;
        now += std::chrono::milliseconds{1}; session->retryNotifications(); commands.drain();
        const auto recovered = session->take();
        QVERIFY(recovered && recovered->rescan && recovered->metadata);
        QVERIFY(session->notificationsPending());
        session->acknowledgeTransactions();
        QVERIFY(!session->notificationsPending());
    }
    void pendingNotificationsCoalesceWithoutGuiOrCapturePolling()
    {
        Fixture f; f.start();
        for (int n{0}; n < 100; ++n) f.wallet->status();
        QCOMPARE(f.wallet->reads, 1);
        f.commands.drain();
        const auto reads = f.wallet->reads;
        QVERIFY(reads <= 3);
        const auto update = f.session->take();
        QVERIFY(update && update->status && update->metadata);
        f.commands.drain();
        QCOMPARE(f.wallet->reads, reads);
    }

};

#include <test_walletopening.moc>
BITCOINQML_REGISTER_QT_TEST(WalletOpeningTests)
