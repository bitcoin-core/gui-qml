// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <common/args.h>
#include <common/settings.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <outputtype.h>
#include <qml/walletqmlcontroller.h>
#include <qml/walletidentity.h>
#include <qml/walletsettings.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <qml/models/walletselectionmodel.h>
#include <qml/models/walletoperationpresentationmodel.h>
#include <scheduler.h>
#include <script/descriptor.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <deque>
#include <util/string.h>
#include <util/translation.h>
#include <wallet/imports.h>
#include <wallet/types.h>
#include <wallet/walletutil.h>

#include <QEventLoop>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSettings>
#include <QTemporaryDir>
#include <QUrl>
#include <algorithm>
#include <array>
#include <set>
#include <map>
#include <vector>
#include <atomic>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <utility>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

namespace {

class DrainingWalletController : public WalletQmlController
{
public:
    explicit DrainingWalletController(interfaces::Node& node) : DrainingWalletController(node, execution::Options{}) {}
    DrainingWalletController(interfaces::Node& node, execution::Options limits) : WalletQmlController(node, limits)
    {
        connect(this, &WalletQmlController::walletsDrained, this, [this] { m_drained = true; });
    }
    ~DrainingWalletController()
    {
        disconnect(this, nullptr, nullptr, nullptr);
        QSignalSpy drained{this, &WalletQmlController::walletsDrained};
        beginShutdown();
        if (!m_drained && drained.empty()) QVERIFY(drained.wait(10000));
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
private:
    bool m_drained{false};
};

bool AwaitWalletOperation(WalletQmlController& controller)
{
    return QTest::qWaitFor([&] { return controller.walletOperationState() != WalletQmlController::WalletOperationState::WalletOperationPending &&
        !controller.walletLoadInProgress() && !controller.walletMigrationInProgress() && !controller.walletImportErrorPending(); }, 10000);
}
bool PumpRetirementTurns(std::deque<std::function<void()>>& turns, const std::function<bool()>& complete)
{
    return QTest::qWaitFor([&] {
        if (!turns.empty()) {
            auto next = std::exchange(turns.front(), {});
            turns.pop_front();
            next();
        }
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        return complete();
    }, 10000);
}
void DrainControlledController(std::unique_ptr<WalletQmlController>& controller,
                               std::deque<std::function<void()>>& turns,
                               const std::weak_ptr<execution::Scheduler>& original)
{
    if (controller) controller->beginShutdown();
    controller.reset();
    QObject receiver;
    bool native_done{false};
    BackendExecutor::shutdownAll(&receiver, [&] { native_done = true; });
    QVERIFY(PumpRetirementTurns(turns, [&] { return native_done && turns.empty() && original.expired(); }));
}
bool CheckedXpub(WalletQmlController& controller, const QString& key)
{
    std::unique_ptr<WalletProvisioningCheck> check{controller.createProvisioningCheck()};
    if (!check) { QTest::qFail("Wallet check could not be created", __FILE__, __LINE__); return false; }
    check->setXpub(key);
    if (!QTest::qWaitFor([&] { return !check->pending(); }, 5000) || !check->ready()) {
        QTest::qFail("Extended public key check did not complete", __FILE__, __LINE__); return false;
    }
    return check->xpubValid();
}
QString CheckedWalletName(WalletQmlController& controller, const QString& name)
{
    std::unique_ptr<WalletProvisioningCheck> check{controller.createProvisioningCheck()};
    if (!check) { QTest::qFail("Wallet check could not be created", __FILE__, __LINE__); return QStringLiteral("Check unavailable"); }
    check->setName(name);
    if (!QTest::qWaitFor([&] { return !check->pending(); }, 5000) || !check->ready()) {
        QTest::qFail("Wallet name check did not complete", __FILE__, __LINE__); return QStringLiteral("Check did not complete");
    }
    return check->nameError();
}
bool AwaitWalletLocation(WalletQmlController& controller, WalletQmlModel* wallet)
{
    if (!controller.openWalletLocation(wallet)) return false;
    return QTest::qWaitFor([&] { return !controller.walletLocationPending(); }, 5000) && controller.walletLocationOpenError().isEmpty();
}

constexpr auto NOT_INITIALIZED_ERROR{"Wallets are still loading. Try again in a moment."};

std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

class FakeExternalSigner : public interfaces::ExternalSigner
{
public:
    explicit FakeExternalSigner(std::string name) : m_name(std::move(name)) {}
    std::string getName() override { return m_name; }

private:
    std::string m_name;
};

std::vector<std::unique_ptr<interfaces::ExternalSigner>> MakeSigners(std::initializer_list<const char*> names)
{
    std::vector<std::unique_ptr<interfaces::ExternalSigner>> signers;
    signers.reserve(names.size());
    for (const char* name : names) {
        signers.emplace_back(std::make_unique<FakeExternalSigner>(name));
    }
    return signers;
}

class FakeWalletLoader : public interfaces::WalletLoader
{
public:
    int create_wallet_calls{0};
    int migrate_wallet_calls{0};
    int is_encrypted_calls{0};
    int handle_load_wallet_calls{0};
    int get_wallets_calls{0};
    int list_wallet_dir_calls{0};
    int load_wallet_calls{0};
    std::atomic<int> get_wallet_dir_calls{0};
    std::string wallet_dir;

    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>(const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&)>
        create_wallet_fn = [](const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&) {
            return util::Error{Untranslated("Unexpected createWallet call")};
        };
    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>(const std::string&, std::vector<bilingual_str>&)>
        load_wallet_fn = [](const std::string&, std::vector<bilingual_str>&) {
            return util::Error{Untranslated("Unexpected loadWallet call")};
        };
    std::function<util::Result<interfaces::WalletMigrationResult>(const std::string&, const SecureString&, bool)>
        migrate_wallet_fn = [](const std::string&, const SecureString&, bool) {
            return util::Error{Untranslated("Unexpected migrateWallet call")};
        };
    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>()>
        restore_wallet_fn = [] { return util::Error{Untranslated("Unexpected restoreWallet call")}; };
    std::function<util::Result<std::unique_ptr<interfaces::Wallet>>(const fs::path&, const std::string&, std::vector<bilingual_str>&, bool)> restore_detailed_fn;
    int restore_wallet_calls{0};
    std::function<bool(const std::string&)> is_encrypted_fn = [](const std::string&) {
        return false;
    };
    std::function<std::vector<std::unique_ptr<interfaces::Wallet>>()> get_wallets_fn = [] {
        return std::vector<std::unique_ptr<interfaces::Wallet>>{};
    };
    std::vector<std::pair<std::string, std::string>> wallet_dir_entries;
    LoadWalletFn captured_load_wallet_fn;
    std::function<std::unique_ptr<interfaces::Handler>()> handler_fn{MakeNoopHandler};

    void registerRpcs() override {}
    bool verify() override { return true; }
    bool load() override { return true; }
    void start(CScheduler&) override {}
    void stop() override {}
    void setMockTime(int64_t) override {}
    void schedulerMockForward(std::chrono::seconds) override {}
    util::Result<std::unique_ptr<interfaces::Wallet>> createWallet(
        const std::string& name,
        const SecureString& passphrase,
        uint64_t wallet_creation_flags,
        std::vector<bilingual_str>& warnings) override
    {
        ++create_wallet_calls;
        return create_wallet_fn(name, passphrase, wallet_creation_flags, warnings);
    }
    util::Result<std::unique_ptr<interfaces::Wallet>> loadWallet(const std::string& name, std::vector<bilingual_str>& warnings) override
    {
        ++load_wallet_calls;
        return load_wallet_fn(name, warnings);
    }
    std::atomic<QThread*> directory_path_thread{nullptr};
    std::string getWalletDir() override { ++get_wallet_dir_calls; directory_path_thread.store(QThread::currentThread()); return wallet_dir; }
    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(const fs::path& source, const std::string& target, std::vector<bilingual_str>& warnings, bool load) override
    {
        ++restore_wallet_calls;
        if (restore_detailed_fn) return restore_detailed_fn(source, target, warnings, load);
        return restore_wallet_fn();
    }
    util::Result<interfaces::WalletMigrationResult> migrateWallet(const std::string& name, const SecureString& passphrase, bool load_wallet) override
    {
        ++migrate_wallet_calls;
        return migrate_wallet_fn(name, passphrase, load_wallet);
    }
    bool isEncrypted(const std::string& name) override
    {
        ++is_encrypted_calls;
        return is_encrypted_fn(name);
    }
    std::atomic<QThread*> directory_thread{nullptr};
    std::vector<std::pair<std::string, std::string>> listWalletDir() override
    {
        directory_thread.store(QThread::currentThread());
        ++list_wallet_dir_calls;
        return wallet_dir_entries;
    }
    std::vector<std::unique_ptr<interfaces::Wallet>> getWallets() override
    {
        ++get_wallets_calls;
        return get_wallets_fn();
    }
    std::unique_ptr<interfaces::Handler> handleLoadWallet(LoadWalletFn fn) override
    {
        ++handle_load_wallet_calls;
        captured_load_wallet_fn = std::move(fn);
        return handler_fn();
    }
};

class FakeWallet : public interfaces::Wallet
{
public:
    struct State {
        struct Data {
            std::atomic<int> remove_calls{0};
            std::atomic<CAmount> balance{0};
            std::atomic<int> name_reads{0};
            std::atomic<int> imports{0}, private_accesses{0};
            std::function<std::vector<wallet::ImportResult>(std::vector<wallet::ImportDescriptorRequest>&)> import_fn;
            std::mutex mutex;
            std::map<uint64_t, interfaces::Wallet::UnloadFn> unload_handlers;
            uint64_t next_unload_handler{0};
            uint64_t addUnload(interfaces::Wallet::UnloadFn fn) { std::lock_guard lock(mutex); const auto id = ++next_unload_handler; unload_handlers.emplace(id, std::move(fn)); return id; }
            void removeUnload(uint64_t id) { std::lock_guard lock(mutex); unload_handlers.erase(id); }
            bool hasUnload() { std::lock_guard lock(mutex); return !unload_handlers.empty(); }
            void notifyUnload() { std::map<uint64_t, interfaces::Wallet::UnloadFn> handlers; { std::lock_guard lock(mutex); handlers = unload_handlers; } for (const auto& [id, fn] : handlers) { (void)id; fn(); } }
        };
        // The Native wallet and its cleanup handler can outlive the stack
        // observation facade during intentional reentrant controller deletion.
        // Keep the original fixture data alive through their actual teardown.
        const std::shared_ptr<Data> retained{std::make_shared<Data>()};
        std::atomic<int>& remove_calls{retained->remove_calls};
        std::atomic<CAmount>& balance{retained->balance};
        std::atomic<int>& name_reads{retained->name_reads};
        std::atomic<int>& imports{retained->imports};
        std::atomic<int>& private_accesses{retained->private_accesses};
        std::mutex& mutex{retained->mutex};
        std::function<std::vector<wallet::ImportResult>(std::vector<wallet::ImportDescriptorRequest>&)>& import_fn{retained->import_fn};
        State() = default;
        State(const State&) = delete;
        State& operator=(const State&) = delete;
        bool hasUnload() { return retained->hasUnload(); }
        void notifyUnload() { retained->notifyUnload(); }
    };

    explicit FakeWallet(std::string wallet_name, State* state)
        : m_wallet_name(std::move(wallet_name)), m_state(state ? state->retained : nullptr) {}

    bool private_keys_disabled{false};
    bool external_signer{false};

    bool encryptWallet(const SecureString&) override { return true; }
    bool isCrypted() override { return false; }
    bool lock() override { return true; }
    util::Expected<void, wallet::WalletError> unlock(const SecureString&) override { return {}; }
    bool isLocked() override { return false; }
    util::Expected<void, wallet::WalletError> changeWalletPassphrase(const SecureString&, const SecureString&) override { return {}; }
    void abortRescan() override {}
    bool backupWallet(const std::string&) override { return true; }
    std::string getWalletName() override { if (m_state) ++m_state->name_reads; return m_wallet_name; }
    util::Result<CTxDestination> getNewDestination(const OutputType, const std::string&) override
    {
        return CTxDestination{CNoDestination{}};
    }
    bool getPubKey(const CScript&, const CKeyID&, CPubKey&) override { return false; }
    util::Expected<CExtPubKey, wallet::WalletError> addHDKey(const std::optional<CExtKey>& key) override { return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::GenericError, Untranslated("not implemented")}}; }
    SigningResult signMessage(const std::string&, const PKHash&, std::string&) override
    {
        return SigningResult::PRIVATE_KEY_NOT_AVAILABLE;
    }
    bool isSpendable(const CTxDestination&) override { return false; }
    bool setAddressBook(const CTxDestination&, const std::string&, const std::optional<wallet::AddressPurpose>&) override { return true; }
    bool delAddressBook(const CTxDestination&) override { return true; }
    bool getAddress(const CTxDestination&, std::string*, wallet::AddressPurpose*) override { return false; }
    std::vector<interfaces::WalletAddress> getAddresses() override { return {}; }
    std::vector<std::string> getAddressReceiveRequests() override { return {}; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string&, const std::string&) override { return true; }
    util::Result<void> displayAddress(const CTxDestination&) override { return {}; }
    bool lockCoin(const COutPoint&, const bool) override { return true; }
    bool unlockCoin(const COutPoint&) override { return true; }
    bool isLockedCoin(const COutPoint&) override { return false; }
    void listLockedCoins(std::vector<COutPoint>& outputs) override { outputs.clear(); }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>&,
                                                    const wallet::CCoinControl&,
                                                    bool,
                                                    std::optional<unsigned int>) override
    {
        return util::Error{Untranslated("Unexpected createTransaction call")};
    }
    void commitTransaction(CTransactionRef, const std::vector<std::string>&) override {}
    bool transactionCanBeAbandoned(const Txid&) override { return false; }
    bool abandonTransaction(const Txid&) override { return false; }
    bool transactionCanBeBumped(const Txid&) override { return false; }
    bool createBumpTransaction(const Txid&, const wallet::CCoinControl&, std::vector<bilingual_str>&, CAmount&, CAmount&, CMutableTransaction&) override
    {
        return false;
    }
    bool signBumpTransaction(CMutableTransaction&) override { return false; }
    bool commitBumpTransaction(const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid&) override { return false; }
    CTransactionRef getTx(const Txid&) override { return {}; }
    interfaces::WalletTx getWalletTx(const Txid&) override { return {}; }
    std::set<interfaces::WalletTx> getWalletTxs() override { return {}; }
    bool tryGetTxStatus(const Txid&, interfaces::WalletTxStatus&, int&, int64_t&) override { return false; }
    interfaces::WalletTx getWalletTxDetails(const Txid&, interfaces::WalletTxStatus&, std::vector<std::string>&, std::vector<std::string>&, bool&, int&) override { return {}; }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions&, size_t*, PartiallySignedTransaction&, bool&) override
    {
        return std::nullopt;
    }
    wallet::CWallet* wallet() override {
        if (m_state) ++m_state->private_accesses;
        throw std::runtime_error{"Private wallet access forbidden"};
    }
    std::vector<wallet::ImportResult> importDescriptors(std::vector<wallet::ImportDescriptorRequest>& requests) override {
        if (m_state) {
            ++m_state->imports;
            if (m_state->import_fn) return m_state->import_fn(requests);
        }
        return {wallet::ImportResult{wallet::WalletErrorCode::GenericError, "not implemented", /*warnings=*/{}, /*general_error=*/true}};
    }
    interfaces::WalletBalances getBalances() override { return {}; }
    bool tryGetBalances(interfaces::WalletBalances& balances, uint256& tip) override
    {
        balances.balance = m_state ? m_state->balance.load() : 0;
        tip = uint256{};
        return true;
    }
    CAmount getBalance() override { return m_state ? m_state->balance.load() : 0; }
    CAmount getAvailableBalance(const wallet::CCoinControl&) override { return 0; }
    bool txinIsMine(const CTxIn&) override { return false; }
    bool txoutIsMine(const CTxOut&) override { return false; }
    CAmount getDebit(const CTxIn&) override { return 0; }
    CAmount getCredit(const CTxOut&) override { return 0; }
    CoinsList listCoins() override { return {}; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>&) override { return {}; }
    CAmount getRequiredFee(unsigned int) override { return 0; }
    CAmount getMinimumFee(unsigned int, const wallet::CCoinControl&, std::optional<int>*, FeeReason*) override { return 0; }
    unsigned int getConfirmTarget() override { return 6; }
    bool hdEnabled() override { return true; }
    bool canGetAddresses() override { return true; }
    bool privateKeysDisabled() override { return private_keys_disabled; }
    bool taprootEnabled() override { return true; }
    bool hasExternalSigner() override { return external_signer; }
    OutputType getDefaultAddressType() override { return OutputType::BECH32; }
    CAmount getMaxTxFee() override { return COIN; }
    void remove() override
    {
        if (m_state) {
            ++m_state->remove_calls;
        }
    }
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn fn) override
    {
        if (m_state) {
            const auto id = m_state->addUnload(std::move(fn));
            return interfaces::MakeCleanupHandler([state = m_state, id] { state->removeUnload(id); });
        }
        return MakeNoopHandler();
    }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleAddressBookChanged(AddressBookChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn) override { return MakeNoopHandler(); }
    util::Result<std::string> exportWatchOnlyWallet(const fs::path&) override { return util::Error{Untranslated("not implemented")}; }

private:
    std::string m_wallet_name;
    std::shared_ptr<State::Data> m_state;
};

void ConfigureSignerRpc(MockNode& node)
{
    node.is_setting_ignored_fn = [](const std::string&) { return false; };
    node.execute_rpc_fn = [&node](const std::string& method, const UniValue& params, const std::string& uri) {
        if (method != "enumeratesigners" || !params.empty() || !uri.empty()) throw std::runtime_error("Unexpected signer RPC");
        // Adapt the existing device fixture to the actual public RPC boundary.
        // This does not call the production name-only Node signer API.
        const auto devices = node.list_external_signers_fn ? node.list_external_signers_fn() : MakeSigners({});
        UniValue rows{UniValue::VARR}; int index{0};
        for (const auto& device : devices) { UniValue row{UniValue::VOBJ}; row.pushKV("fingerprint", QStringLiteral("%1").arg(++index, 8, 16, QLatin1Char('0')).toStdString()); row.pushKV("name", device->getName()); rows.push_back(std::move(row)); }
        UniValue result{UniValue::VOBJ}; result.pushKV("signers", std::move(rows)); return result;
    };
}

void ConfigureExpectedControllerInitialization(MockNode& node, FakeWalletLoader& loader)
{
    ConfigureSignerRpc(node);
    node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{}; };
    node.force_setting_fn = [](const std::string&, const common::SettingsValue&) {};
    node.list_external_signers_fn = [] { return std::vector<std::unique_ptr<interfaces::ExternalSigner>>{}; };
    node.ExpectAtLeast(node.calls.walletLoader, 2);
    node.ExpectAtLeast(node.calls.getPersistentSetting, 1);
    node.get_dust_relay_fee_fn = [] { return CFeeRate{3000}; };
    node.ExpectExactly(node.calls.forceSetting, 0);
    node.ExpectExactly(node.calls.listExternalSigners, 0);
}

class ControllerBoundaryScheduler final : public execution::Scheduler {
public:
    explicit ControllerBoundaryScheduler(std::shared_ptr<execution::Scheduler> target) : m_target(std::move(target)) {}
    std::function<void()> after_reserve, after_borrow;
    std::shared_ptr<execution::Scheduler> openActionScope() override { return m_target->openActionScope(); }
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return m_target->submit(std::move(task), spec); }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
        const auto result = m_target->reserveCleanup(std::move(task), spec);
        if (after_reserve) std::exchange(after_reserve, {})();
        return result;
    }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override {
        auto result = m_target->borrow(key);
        if (after_borrow) std::exchange(after_borrow, {})();
        return result;
    }
    bool activateCleanup(execution::TaskId id) override { return m_target->activateCleanup(id); }
    execution::ResourceKey allocateKey() override { return m_target->allocateKey(); }
    size_t pending() const override { return m_target->pending(); }
    void closeOrdinaryAdmission() override { m_target->closeOrdinaryAdmission(); }
    bool cancelQueued(execution::TaskId id) override { return m_target->cancelQueued(id); }
private:
    std::shared_ptr<execution::Scheduler> m_target;
};

struct ManualControllerGraph {
    ManualRoutedExecution native;
    std::deque<std::function<void()>> posts;
    std::function<void()> native_drained;
    std::unique_ptr<WalletQmlController> controller;
    std::atomic<int> lane{-1};
    bool drained{false};
    int physical_completions{0};

    explicit ManualControllerGraph(interfaces::Node& node, execution::Options limits = execution::Options{},
        std::function<std::shared_ptr<execution::Scheduler>(std::shared_ptr<execution::Scheduler>)> decorate = {})
        : native(limits)
    {
        WalletQmlController::Services services;
        services.scheduler = decorate ? decorate(native.scheduler) : native.scheduler;
        services.post = [this](auto callback) { posts.push_back(std::move(callback)); };
        services.shutdown = [this](auto callback) {
            native.scheduler->closeAdmission();
            native_drained = std::move(callback);
        };
        controller = std::make_unique<WalletQmlController>(node, std::move(services));
        QObject::connect(controller.get(), &WalletQmlController::walletsDrained, controller.get(), [this] { drained = true; });
    }
    bool step(execution::Lane value)
    {
        lane = static_cast<int>(value);
        const bool ran = native.step(value);
        lane = -1;
        return ran;
    }
    void runNative()
    {
        while (!native.queuedIdle()) {
            if (!step(native.nextLane().value())) break;
        }
    }
    void pump()
    {
        runNative();
        if (!posts.empty()) { auto next = std::move(posts.front()); posts.pop_front(); next(); }
        QCoreApplication::processEvents();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (native_drained && native.scheduler->pending() == 0 && native.queuedIdle() && native.activeIdle()) {
            // An actual physical ledger drain, never an idle-cleanup heuristic.
            ++physical_completions;
            posts.push_back(std::exchange(native_drained, {}));
        }
    }
    bool until(std::function<bool()> condition)
    {
        return QTest::qWaitFor([&] { pump(); return condition() && native.queuedIdle() && posts.empty(); }, 5000);
    }
    void shutdown()
    {
        if (!controller) return;
        controller->beginShutdown();
        const bool complete = until([&] { return drained; });
        if (!complete) QTest::qFail("Original controller graph did not physically drain", __FILE__, __LINE__);
        if (complete) { QCOMPARE(native.scheduler->pending(), size_t{0}); QCOMPARE(physical_completions, 1); }
    }
    ~ManualControllerGraph()
    {
        shutdown();
        controller.reset();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    }
};

class CreationPublicationGate final : public walletpublication::WalletCatalogPublisher
{
public:
    explicit CreationPublicationGate(std::shared_ptr<walletpublication::WalletCatalogPublisher> publisher) : delegate(std::move(publisher)) {}
    std::shared_ptr<walletpublication::Ticket> reserveCreation(const QString& id) override { return delegate->reserveCreation(id); }
    walletpublication::Result publishCreation(const std::shared_ptr<walletpublication::Ticket>& ticket, std::shared_ptr<interfaces::Wallet> wallet, const QString& id, walletopening::NativeEvents observation) override {
        ++publications;
        if (std::exchange(refuse_next, false)) return {{}, walletpublication::Availability::Unavailable, QStringLiteral("Controlled publication refusal")};
        return delegate->publishCreation(ticket, std::move(wallet), id, std::move(observation));
    }
    walletpublication::Result publishWithLifetime(const std::shared_ptr<walletpublication::Ticket>& ticket, std::shared_ptr<interfaces::Wallet> wallet, const QString& id, walletopening::NativeEvents observation, std::shared_ptr<walletpublication::Release> cell) override {
        ++publications;
        if (std::exchange(refuse_next, false)) return {{}, walletpublication::Availability::Unavailable, QStringLiteral("Controlled publication refusal")};
        return delegate->publishWithLifetime(ticket, std::move(wallet), id, std::move(observation), std::move(cell));
    }
    void finishCreation(const std::shared_ptr<walletpublication::Ticket>& ticket) override { delegate->finishCreation(ticket); }
    bool containsExact(WalletQmlModel* wallet) const override { return delegate->containsExact(wallet); }
    std::shared_ptr<walletpublication::WalletCatalogPublisher> delegate;
    int publications{0};
    bool refuse_next{false};
};

bool DriveCreationToCore(ManualControllerGraph& graph, WalletOperationModel& operation)
{
    // Consume validation and reservation turns, stopping before the actual
    // Core mutation. A GUI phase announcement alone is not an effect oracle.
    for (int turn = 0; turn < 256; ++turn) {
        if (operation.phase() == WalletOperationModel::Creating && graph.native.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Core; }) == 1) return true;
        if (!graph.posts.empty()) { auto post = std::move(graph.posts.front()); graph.posts.pop_front(); post(); }
        else if (graph.native.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Domain; })) graph.step(execution::Lane::Domain);
        else if (const auto lane = graph.native.nextLane()) graph.step(*lane);
        else break;
    }
    return false;
}

bool WriteDatabaseHint(const QString& path, bool berkeley = false)
{
    QFile file{path}; if (!file.open(QIODevice::WriteOnly)) return false;
    QByteArray value(berkeley ? 4096 : 512, '\0');
    if (berkeley) value.replace(12, 4, QByteArray::fromHex("00053162")); else value.replace(0, 16, QByteArray{"SQLite format 3\0", 16});
    return file.write(value) == value.size();
}
QByteArray ReadTestFile(const QString& path)
{
    QFile file{path}; if (!file.open(QIODevice::ReadOnly)) return {}; return file.readAll();
}

const QString VALID_XPUB{
    "xpub661MyMwAqRbcFtXgS5sYJABqqG9YLmC4Q1Rdap9gSE8NqtwybGhePY2gZ29ESFjqJoCu1Rupje8YtGqsefD265TMg7usUDFdp6W1EGMcet8"};
} // namespace

class WalletQmlControllerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void operationPresentationQueueOwnsOriginalPromptTeardown();
    void operationReservationSurvivesOriginalObserverLoss_data();
    void operationReservationSurvivesOriginalObserverLoss();
    void migrationCredentialRetryKeepsTheOriginalCopy_data();
    void migrationCredentialRetryKeepsTheOriginalCopy();
    void restorationCapturesExactSourceAndTarget_data();
    void restorationCapturesExactSourceAndTarget();
    void restorationFailureReasonIsContextual_data();
    void restorationFailureReasonIsContextual();
    void copiedPrunedWalletReconcilesWithoutRecopy_data();
    void copiedPrunedWalletReconcilesWithoutRecopy();
    void migrationPromptRejectsStaleOriginalAnswers();
    void lateSettingsErrorKeepsTheOriginalRestoredWallet_data();
    void lateSettingsErrorKeepsTheOriginalRestoredWallet();
    void migrationKeepsAuxiliaryRetirementIndependent();
    void creationUnloadBeforePublicationNeverOffersAPhantomFacade_data();
    void creationUnloadBeforePublicationNeverOffersAPhantomFacade();
    void creationEditorOwnsTheOriginalSubmission_data();
    void creationEditorOwnsTheOriginalSubmission();
    void canonicalCatalogIdentityPreservesTheExactNativeRpcNamespace();
    void creationValidationUsesTheFinalIdentity_data();
    void creationValidationUsesTheFinalIdentity();
    void creationSetupAndPublicationKeepTheActualArtifact_data();
    void creationSetupAndPublicationKeepTheActualArtifact();
    void initTestCase();
    void fakeWalletCleanupRetainsOriginalFixtureState();
    void importErrorProjectionUsesOriginalRevisionAndVirtualFullText();
    void importErrorCaptureOverflowPreservesActualOutcomeAndRetries();
    void migrationErrorReceiptPreparesBoundedMessageAndFrozenFullDetails();
    void importErrorProjectionRejectionAndUnknownOversizedSourceStayExplicit();
    void createErrorProjectionRetainsNativeDocumentAndFencesSupersededPublication();
    void manualShutdownFencesInventoryBeforeBoundedRetirement();
    void manualShutdownSurvivesOwnerDestructionDuringFirstBatch();
    void manualReviewRetirementGatesFinalNativeDrain();
    void failureDuringReadyPublicationCannotResumeNotifications();
    void earlierNotificationKeepsItsOriginWhenCreationStartsDuringMetadata();
    void cleanupFailureKeepsItsExplicitTerminalError();
    void successfulEmptyMigrationDoesNotFabricateAWallet();
    void changedWakeupWaitsForAnEarlierShadowDisposal();
    void failureConsumedBeforeAnEarlierReadyMessageCannotResurrectInitialization();
    void catalogCommandsBeforeInitializationAcquireNoWalletResources();
    void initializationIsSingleShotAcrossPendingSuccessFailureAndShutdown_data();
    void initializationIsSingleShotAcrossPendingSuccessFailureAndShutdown();
    void runtimeDisabledWalletDoesNotAcquireItsLoader();
    void successfulImportPublishesTheOwnedReturnedWallet();
    void successfulMigrationPublishesTheOwnedReturnedWallet();
    void notificationsDuringBatchedStartupArePresentedAfterInitialization();
    void failurePublicationSurvivesReentrantRetirement_data();
    void failurePublicationSurvivesReentrantRetirement();
    void notificationDuringNestedPresentationKeepsItsContinuation();
    void subscriptionFailureDisablesCatalogCommands();
    void startupSnapshotMergesNotificationHandlesWithoutDuplicateModels();
    void lifecyclePublicationSurvivesReentrantRetirement_data();
    void lifecyclePublicationSurvivesReentrantRetirement();
    void signerDiscoveryUsesDefaultNativeWorker();
    void closeTargetPublicationCanRetireOriginalFacade();
    void shutdownDrainsWalletQueueAndPlaceholder();
    void reentrantShutdownRetiresTheNewWalletModel();
    void shutdownRetiresQueuedLoadNotificationOffGui();
    void shutdownReleasesNotificationsBeforeMigrationRollback();
    void shutdownWaitsForNotificationRetirement();
    void walletCommandCompletionDuringShutdown_data();
    void walletCommandCompletionDuringShutdown();
    void injectedControllerServicesRejectPartialGraphBeforeAdmission();
    void manualMatchingLoadNotificationAndResult_data();
    void manualMatchingLoadNotificationAndResult();
    void manualExternalLoadAndAlreadyLoadedResult_data();
    void manualExternalLoadAndAlreadyLoadedResult();
    void manualFailedOpenRequiresCapturedOriginalOpening_data();
    void manualFailedOpenRequiresCapturedOriginalOpening();
    void manualFailedOpenAfterObservedUnloadBindsOnlyFreshOpening_data();
    void manualFailedOpenAfterObservedUnloadBindsOnlyFreshOpening();
    void manualOldOpeningPromptAndOutcomeCannotResolveReopenedName();
    void manualUnknownBindingNotificationCannotResurrectOrResolveReopenedName_data();
    void manualUnknownBindingNotificationCannotResurrectOrResolveReopenedName();
    void manualUnloadBeforeFirstGuiAdoptionKeepsViewAndControllerAlive_data();
    void manualUnloadBeforeFirstGuiAdoptionKeepsViewAndControllerAlive();
    void manualObservedUnloadAllowsFreshSameNameLoad_data();
    void manualObservedUnloadAllowsFreshSameNameLoad();
    void manualInsertionCallbacksPreserveOriginalAdoption_data();
    void manualInsertionCallbacksPreserveOriginalAdoption();
    void manualControllerAdmissionSurvivesReentrantOwnerLoss_data();
    void manualControllerAdmissionSurvivesReentrantOwnerLoss();
    void manualPendingSelectionKeepsNoOwnerReferenceAfterNotification_data();
    void manualPendingSelectionKeepsNoOwnerReferenceAfterNotification();
    void manualInjectedShutdownOwnsAcceptedOutcomeAfterObserverLoss_data();
    void manualInjectedShutdownOwnsAcceptedOutcomeAfterObserverLoss();
    void acceptedCreationDoesNotRetargetLaterSelection();
    void openWalletRowsPreserveIndependentViews();
    void manualIndependentSelectionsKeepExactRegistryMembership();
    void manualPendingSelectionCannotRetargetAnotherWindow_data();
    void manualPendingSelectionCannotRetargetAnotherWindow();
    void manualRegistryRemovalPreservesReentrantSelection();
    void manualResultRetirementDoesNotResolveByName();
    void manualRetainedResultBackendCleanupFencesReopen_data();
    void manualRetainedResultBackendCleanupFencesReopen();
    void manualPublicationOwnsResultBeforeReopeningCoreAdmission_data();
    void manualPublicationOwnsResultBeforeReopeningCoreAdmission();
    void manualTerminalPublicationRejectsReentrantCoreUntilProgressReleased_data();
    void manualTerminalPublicationRejectsReentrantCoreUntilProgressReleased();
    void manualDefaultWalletLoadAndLocation_data();
    void manualDefaultWalletLoadAndLocation();
    void openWalletTreeRemovalPreservesRowsAndPersistentIdentities();
    void provisioningChecksOwnManualPhases_data();
    void provisioningChecksOwnManualPhases();
    void validateXpubAcceptsValidKey();
    void validateXpubRejectsGarbage();
    void validateXpubRejectsEmpty();
    void validateXpubTrimsWhitespace();
    void createWatchOnlyInvalidXpubSetsError();
    void createWatchOnlyCleansUpWhenDescriptorImportFails();
    void createWatchOnlyUsesPublicDescriptorImport_data();
    void createWatchOnlyUsesPublicDescriptorImport();
    void externalSignerCreationRequiresConfiguredPath();
    void externalSignerCreationRequiresExactlyOneSigner();
    void externalSignerSuggestionUsesSignerName();
    void manualExternalSignerCreationRejectsUnavailableSnapshot_data();
    void manualExternalSignerCreationRejectsUnavailableSnapshot();
    void manualExternalSignerCreationOwnsOriginalOutcome_data();
    void manualExternalSignerCreationOwnsOriginalOutcome();
    void manualManagedWalletPathsResolveThroughPublicLoader_data();
    void manualManagedWalletPathsResolveThroughPublicLoader();
    void signerDiscoveryKeepsNewerIntentAcrossHeldPublication();
    void signerDiscoveryRejectsPublicationAfterQuietClose();
    void initializedControllerSignalsMigrationForLegacyWallet();
    void createWalletBeforeInitializationReturnsFalseAndSetsError();
    void importWalletBeforeInitializationSetsLoadError();
    void migrateWalletBeforeInitializationSetsMigrationError();
    void selectWalletBeforeInitializationSetsLoadError();
    void initializedControllerPropagatesCreateErrors();
    void initializedControllerForwardsMigrationPassphrase_data();
    void initializedControllerForwardsMigrationPassphrase();
    void capturedSecretSurvivesQueueDelayAndQuietClose_data();
    void capturedSecretSurvivesQueueDelayAndQuietClose();
    void capturedSecretTransportRefusalDoesNotCallNativeLoader_data();
    void capturedSecretTransportRefusalDoesNotCallNativeLoader();
    void initializedControllerForwardsUtf8CreatePassphrase_data();
    void initializedControllerForwardsUtf8CreatePassphrase();
    void initializedControllerEmitsWalletCreateSucceeded();
    void initializedControllerDoesNotCompleteCreateForUnrelatedLoadNotification();
    void initializedControllerDoesNotCompleteLoadForUnrelatedLoadNotification();
    void initializedControllerReportsCreateWarnings();
    void initializedControllerIgnoresDuplicateCreateWhileInProgress();
    void watchOnlyCreateBeforeInitializationSetsLoadError();
    void watchOnlyCreateWhileWalletLoadInProgressIsIgnored();
    void watchOnlyCreateFailureAfterCreateDoesNotPublishWallet();
    void initializedControllerRequestsPassphraseBeforeEncryptedMigration();
    void initializedControllerMigratesUnencryptedWalletWithoutPassphrase();
    void initializedControllerClosesSelectedWalletAndSelectsRemainingLoadedWallet();
    void initializedControllerClosesNonSelectedWalletWithoutChangingSelection();
    void reopeningRetiringWalletReportsLoadErrorAndSucceedsAfterDrain();
    void initializedControllerEmitsWalletLoadStateChanged();
    void initializedControllerHandlesExternalWalletUnload();
    void initializedControllerUnloadWalletsClearsSelectionAndOpenWallets();
    void initializedControllerEmitsLoadingThenLoadErrorOnFailedLoad();
    void publishOpenWalletsInfoEmitsWalletInfoChangedForEachOpenWallet();
    void walletNameAvailabilityErrorRejectsEmptyAndWhitespace();
    void walletNameAvailabilityErrorRejectsExistingNameWithSurroundingWhitespace();
    void walletNameAvailabilityErrorReturnsEmptyForAvailableName();
    void walletDisplayNameWriteOwnsOutcome_data();
    void walletDisplayNameWriteOwnsOutcome();
    void walletDisplayNameWriteReportsTransportRefusal();
    void openWalletLocationOpensWalletDirectory();
    void openWalletLocationReportsOpenFailure();
    void openWalletLocationReportsMissingWalletPath();
    void openWalletLocationReportsMissingWallet();
};

void WalletQmlControllerTests::migrationCredentialRetryKeepsTheOriginalCopy_data()
{
    QTest::addColumn<bool>("cancel"); QTest::addColumn<bool>("probe_missing");
    QTest::newRow("new-secret-original-copy") << false << false;
    QTest::newRow("cancel-keeps-partial-files") << true << false;
    QTest::newRow("missing-secret-opens-a-fresh-original-prompt") << false << true;
    QTest::newRow("missing-secret-cancel-keeps-the-original-copy") << true << true;
}
void WalletQmlControllerTests::migrationCredentialRetryKeepsTheOriginalCopy()
{
    QFETCH(bool, cancel); QFETCH(bool, probe_missing); QTemporaryDir source, wallets; QVERIFY(source.isValid()); QVERIFY(wallets.isValid()); const auto backup = source.filePath("legacy-import.bak"); QVERIFY(WriteDatabaseHint(backup, true)); const auto original = ReadTestFile(backup);
    MockNode node; FakeWalletLoader loader; loader.wallet_dir = wallets.path().toStdString(); loader.is_encrypted_fn = [probe_missing](const auto&) { return !probe_missing; }; ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    const QPointer<WalletQmlModel> selected_before{controller.selectedWallet()};
    loader.restore_detailed_fn = [&](const auto&, const auto& name, auto&, bool load) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (load) throw std::runtime_error{"Unsafe combined restore"};
        const auto target = wallets.filePath(QString::fromStdString(name)); if (!QDir{}.mkpath(target) || !QFile::copy(backup, QDir(target).filePath("wallet.dat"))) throw std::runtime_error{"Copy failed"}; loader.wallet_dir_entries.emplace_back(name, "bdb"); return std::unique_ptr<interfaces::Wallet>{};
    };
    std::vector<std::string> secrets;
    loader.migrate_wallet_fn = [&](const auto& name, const SecureString& secret, bool load) -> util::Result<interfaces::WalletMigrationResult> {
        if (load) throw std::runtime_error{"Unsafe combined migration"};
        secrets.emplace_back(secret.begin(), secret.end());
        if (secrets.size() == 1) return util::Error{Untranslated(probe_missing ? "Error: The wallet passphrase was not provided" : "Error: The wallet passphrase entered was incorrect.")};
        interfaces::WalletMigrationResult result; result.wallet = std::make_unique<FakeWallet>(name, nullptr); result.backup_path = fs::PathFromString(wallets.filePath("recovery.legacy.bak").toStdString()); return result;
    };
    auto* operation = controller.restore(QUrl::fromLocalFile(backup)); QVERIFY(operation); QVERIFY(graph.until([&] { return operation->phase() == WalletOperationModel::AwaitingConfirmation; })); QCOMPARE(loader.restore_wallet_calls, 0); QVERIFY(!controller.walletLoadInProgress());
    auto* confirmation = operation->prompt(); QVERIFY(confirmation->answer(confirmation->promptId(), WalletOperationPrompt::Confirm).accepted); QVERIFY(graph.until([&] { return operation->phase() == WalletOperationModel::AwaitingPassphrase; })); QCOMPARE(loader.restore_wallet_calls, probe_missing ? 1 : 0);
    auto* first = operation->prompt();
    if (!probe_missing) { QVERIFY(first->submitSecret(first->promptId(), "wrong original secret").accepted); QVERIFY(graph.until([&] { return operation->phase() == WalletOperationModel::AwaitingPassphrase; })); }
    auto* replacement = operation->prompt(); auto* refused_prompt = probe_missing ? confirmation : first; QVERIFY(replacement != refused_prompt); QCOMPARE(operation->error().code, probe_missing ? QStringLiteral("CredentialsRequired") : QStringLiteral("CredentialsRejected")); QCOMPARE(loader.restore_wallet_calls, 1); QCOMPARE(loader.migrate_wallet_calls, 1); QVERIFY(!controller.walletLoadInProgress()); QVERIFY(!operation->result().recovery_paths.isEmpty());
    QVERIFY(!refused_prompt->submitSecret(refused_prompt->promptId(), "stale secret").accepted); QCOMPARE(loader.migrate_wallet_calls, 1);
    if (cancel) QVERIFY(replacement->dismiss(replacement->promptId()).accepted);
    else QVERIFY(replacement->submitSecret(replacement->promptId(), "correct original secret").accepted);
    QVERIFY(graph.until([&] { return operation->terminal() && !controller.walletLoadInProgress(); })); QCOMPARE(loader.restore_wallet_calls, 1); QCOMPARE(loader.migrate_wallet_calls, cancel ? 1 : 2); QCOMPARE(ReadTestFile(backup), original); QVERIFY(QFileInfo::exists(wallets.filePath("legacy-import/wallet.dat"))); QCOMPARE(controller.selectedWallet(), selected_before.data());
    QCOMPARE(operation->phase(), cancel ? WalletOperationModel::Partial : WalletOperationModel::Succeeded); QCOMPARE(secrets.front(), probe_missing ? std::string{} : std::string{"wrong original secret"});
    if (!cancel) { QCOMPARE(secrets.back(), std::string{"correct original secret"}); QVERIFY(operation->canOpenResult()); }
}

void WalletQmlControllerTests::operationReservationSurvivesOriginalObserverLoss_data()
{
    QTest::addColumn<int>("boundary_kind");
    QTest::newRow("confirmation-publication-reserve") << 0;
    QTest::newRow("secret-capture-reserve") << 1;
    QTest::newRow("copied-wallet-reconcile-reserve") << 2;
    QTest::newRow("original-warning-owner-reserve") << 3;
}
void WalletQmlControllerTests::operationReservationSurvivesOriginalObserverLoss()
{
    QFETCH(int, boundary_kind); QTemporaryDir source, wallets; QVERIFY(source.isValid()); QVERIFY(wallets.isValid());
    MockNode node; FakeWalletLoader loader; loader.wallet_dir = wallets.path().toStdString(); loader.wallet_dir_entries = {{"original", "bdb"}}; loader.is_encrypted_fn = [](const auto&) { return true; }; ConfigureExpectedControllerInitialization(node, loader);
    std::shared_ptr<ControllerBoundaryScheduler> boundary;
    ManualControllerGraph graph{node, execution::Options{}, [&](auto scheduler) { boundary = std::make_shared<ControllerBoundaryScheduler>(std::move(scheduler)); return boundary; }};
    graph.controller->initialize(); QVERIFY(graph.until([&] { return graph.controller->initialized(); })); const QPointer<WalletQmlController> controller{graph.controller.get()};
    WalletCreationReceipt receipt;
    if (boundary_kind < 2) {
        auto* operation = controller->migrate("original"); QVERIFY(operation); QVERIFY(graph.until([&] { return operation->prompt(); }));
        if (boundary_kind == 1) { auto* prompt = operation->prompt(); QVERIFY(prompt->answer(prompt->promptId(), WalletOperationPrompt::Confirm).accepted); QVERIFY(graph.until([&] { return operation->phase() == WalletOperationModel::AwaitingPassphrase; })); }
        auto* prompt = operation->prompt(); const auto prompt_id = prompt->promptId();
        boundary->after_reserve = [&] { graph.controller.reset(); };
        receipt = boundary_kind == 1 ? prompt->submitSecret(prompt_id, "original secret") : prompt->answer(prompt_id, WalletOperationPrompt::Confirm);
        QVERIFY(!controller); QVERIFY(!receipt.accepted); QCOMPARE(loader.migrate_wallet_calls, 0);
    } else if (boundary_kind == 2) {
        const auto backup = source.filePath("pruned.bak"); QVERIFY(WriteDatabaseHint(backup));
        loader.restore_detailed_fn = [&](const auto&, const auto& name, auto&, bool) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
            const auto destination = wallets.filePath(QString::fromStdString(name)); if (!QDir{}.mkpath(destination) || !QFile::copy(backup, QDir(destination).filePath("wallet.dat"))) throw std::runtime_error{"Copy failed"}; loader.wallet_dir_entries.emplace_back(name, "sqlite"); return std::unique_ptr<interfaces::Wallet>{};
        };
        loader.load_wallet_fn = [](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> { return util::Error{Untranslated("Wallet loading failed. Prune: last wallet synchronisation goes beyond pruned data. You need to -reindex.")}; };
        auto* operation = controller->restore(QUrl::fromLocalFile(backup)); QVERIFY(operation); QVERIFY(graph.until([&] { return operation->terminal(); })); QVERIFY(operation->canReconcile());
        boundary->after_reserve = [&] { graph.controller.reset(); }; receipt = operation->reconcile(); QVERIFY(!controller); QVERIFY(!receipt.accepted); QCOMPARE(loader.restore_wallet_calls, 1); QCOMPARE(loader.load_wallet_calls, 1); QVERIFY(QFileInfo::exists(backup)); QVERIFY(QFileInfo::exists(wallets.filePath("pruned/wallet.dat")));
    } else {
        loader.load_wallet_fn = [](const auto& name, auto& warnings) -> util::Result<std::unique_ptr<interfaces::Wallet>> { warnings.push_back(Untranslated("Original Native warning")); return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, nullptr)}; };
        controller->setSelectedWallet("original", "sqlite"); QVERIFY(graph.until([&] { return controller->walletForName("original") && !controller->walletLoadInProgress(); }));
        auto services = controller->creationServices().operation; const auto id = controller->walletOperationId(); const QString name = controller->walletOperationName(); const QString warnings = controller->walletLoadWarnings(); auto retained = controller->m_command_owner; QPointer<WalletQmlModel> original{controller->walletForName("original")};
        boundary->after_reserve = [&] { graph.controller.reset(); };
        auto* notice = WalletOperationModel::OpenNotice(std::move(services), id, name, warnings, retained, original); QVERIFY(!controller); QVERIFY(notice); QCOMPARE(notice->targetWalletId(), name); QCOMPARE(notice->parent(), nullptr); delete notice;
        // Release this test's borrowed original metadata before physical drain.
        retained.reset();
    }
    QVERIFY(graph.until([&] { return graph.physical_completions == 1 && graph.native.scheduler->pending() == 0; }));
}

void WalletQmlControllerTests::operationPresentationQueueOwnsOriginalPromptTeardown()
{
    MockNode node; FakeWalletLoader loader; loader.wallet_dir_entries = {{"original", "bdb"}, {"successor", "bdb"}}; loader.is_encrypted_fn = [](const auto&) { return true; }; ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    auto* original = controller.migrate("original"); QVERIFY(original); QVERIFY(graph.until([&] { return original->prompt(); }));
    auto* successor = controller.migrate("successor"); QVERIFY(successor); QVERIFY(graph.until([&] { return successor->prompt(); }));
    WalletOperationPresentationModel presentations; QVERIFY(presentations.offer(original)); QVERIFY(presentations.offer(successor)); QCOMPARE(presentations.rowCount(), 2);
    auto* confirmation = original->prompt(); const auto confirmation_id = confirmation->promptId(); QVERIFY(confirmation->answer(confirmation_id, WalletOperationPrompt::Confirm).accepted);
    QVERIFY(graph.until([&] { return original->phase() == WalletOperationModel::AwaitingPassphrase; })); auto* password = original->prompt(); QVERIFY(password != confirmation);
    presentations.reportClosed(original->operationId(), confirmation_id); QCOMPARE(original->phase(), WalletOperationModel::AwaitingPassphrase); QCOMPARE(presentations.rowCount(), 2);
    presentations.reportClosed(original->operationId(), password->promptId()); QCOMPARE(presentations.rowCount(), 1);
    presentations.reportClosed(original->operationId(), confirmation_id); QCOMPARE(presentations.rowCount(), 1); QCOMPARE(successor->phase(), WalletOperationModel::AwaitingConfirmation);
    QVERIFY(graph.until([&] { return original->terminal(); })); QVERIFY(!password->submitSecret(password->promptId(), "stale password").accepted); QCOMPARE(loader.migrate_wallet_calls, 0);
    QCOMPARE(presentations.data(presentations.index(0), WalletOperationPresentationModel::OperationRole).value<QObject*>(), successor);
    QObject unrelated; QVERIFY(!presentations.offer(&unrelated)); QCOMPARE(presentations.rowCount(), 1); QVERIFY(!presentations.error().empty());
    QVERIFY(presentations.dismiss(successor->operationId())); QVERIFY(graph.until([&] { return successor->terminal(); })); QCOMPARE(presentations.rowCount(), 0);
}

void WalletQmlControllerTests::restorationCapturesExactSourceAndTarget_data()
{
    QTest::addColumn<QString>("filename"); QTest::addColumn<QString>("expected");
    QTest::newRow("modern-legacy-suffix") << QStringLiteral("Treasury.legacy.bak") << QStringLiteral("Treasury.legacy");
    QTest::newRow("ordinary-backup") << QStringLiteral("Treasury.archive.bak") << QStringLiteral("Treasury.archive");
    QTest::newRow("significant-space") << QStringLiteral("Savings .bak") << QStringLiteral("Savings ");
    QTest::newRow("encoded-url") << QStringLiteral("Trésor #1.bak") << QStringLiteral("Trésor #1");
    QTest::newRow("literal-percent") << QStringLiteral("Savings%20.bak") << QStringLiteral("Savings%20");
}
void WalletQmlControllerTests::restorationCapturesExactSourceAndTarget()
{
    QFETCH(QString, filename); QFETCH(QString, expected);
    QTemporaryDir source, wallets; QVERIFY(source.isValid()); QVERIFY(wallets.isValid());
    const auto path = source.filePath(filename); QVERIFY(WriteDatabaseHint(path)); const auto original_bytes = ReadTestFile(path);
    MockNode node; FakeWalletLoader loader; loader.wallet_dir = wallets.path().toStdString(); ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    const QPointer<WalletQmlModel> selected_before{controller.selectedWallet()};
    QString actual_source, actual_target; bool loaded_on_core{false}, restore_load{true};
    loader.restore_detailed_fn = [&](const fs::path& input, const std::string& target, auto& warnings, bool load) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        actual_source = QString::fromStdString(fs::PathToString(input)); actual_target = QString::fromStdString(target); restore_load = load;
        if (graph.lane.load() != int(execution::Lane::Core)) throw std::runtime_error{"Restore ran on GUI"};
        const auto destination = wallets.filePath(actual_target); if (!QDir{}.mkpath(destination) || !QFile::copy(path, QDir(destination).filePath("wallet.dat"))) throw std::runtime_error{"Controlled copy failed"};
        loader.wallet_dir_entries.emplace_back(target, "sqlite"); warnings.push_back(Untranslated(std::string(5000, 'w'))); return std::unique_ptr<interfaces::Wallet>{};
    };
    loader.load_wallet_fn = [&](const auto& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> { loaded_on_core = graph.lane.load() == int(execution::Lane::Core); return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, nullptr)}; };
    auto url = QUrl::fromLocalFile(path); auto* operation = controller.restore(url); QVERIFY(operation); QVERIFY(DriveCreationToCore(graph, *operation));
    url = QUrl::fromLocalFile(source.filePath("replacement.bak"));
    auto* refused = controller.restore(url); QVERIFY(refused); QCOMPARE(refused->phase(), WalletOperationModel::Rejected); QCOMPARE(controller.creationOperation(), operation);
    QVERIFY(controller.walletLoadInProgress()); QCOMPARE(loader.restore_wallet_calls, 0);
    QVERIFY(graph.until([&] { return operation->terminal() && !controller.walletLoadInProgress(); }));
    QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); QCOMPARE(actual_source, path); QCOMPARE(actual_target, expected); QVERIFY(!restore_load); QVERIFY(loaded_on_core);
    QCOMPARE(loader.restore_wallet_calls, 1); QCOMPARE(loader.load_wallet_calls, 1); QCOMPARE(loader.migrate_wallet_calls, 0);
    QCOMPARE(operation->targetWalletId(), expected); QCOMPARE(operation->resultWallet(), controller.walletForName(expected)); QVERIFY(operation->canOpenResult()); QCOMPARE(controller.selectedWallet(), selected_before.data());
    QCOMPARE(ReadTestFile(path), original_bytes); QCOMPARE(operation->warningText(), QString(5000, QLatin1Char('w'))); QVERIFY(operation->warningDetails()->rowCount() > 1);
    QVERIFY(controller.selectWallet(operation->resultWallet())); QCOMPARE(controller.selectedWallet(), operation->resultWallet());
}
void WalletQmlControllerTests::restorationFailureReasonIsContextual_data()
{
    QTest::addColumn<int>("reason");
    QTest::newRow("invalid-header-containing-migration") << 0;
    QTest::newRow("sqlite-duplicate-containing-migration") << 1;
    QTest::newRow("sqlite-duplicate-neutral-name") << 2;
    QTest::newRow("remote-url") << 3;
}
void WalletQmlControllerTests::restorationFailureReasonIsContextual()
{
    QFETCH(int, reason); QTemporaryDir source, wallets; QVERIFY(source.isValid()); QVERIFY(wallets.isValid());
    const auto filename = reason == 2 ? QStringLiteral("neutral.bak") : QStringLiteral("audit_migration_73.bak"); const auto path = source.filePath(filename);
    QVERIFY(WriteDatabaseHint(path)); if (reason == 0) { QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("invalid backup"), qint64{14}); }
    const auto original = ReadTestFile(path); const auto target = QFileInfo(filename).completeBaseName();
    if (reason == 1 || reason == 2) QVERIFY(QDir{}.mkpath(wallets.filePath(target)));
    MockNode node; FakeWalletLoader loader; loader.wallet_dir = wallets.path().toStdString(); ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    auto* operation = controller.restore(reason == 3 ? QUrl{QStringLiteral("https://example.invalid/backup.bak")} : QUrl::fromLocalFile(path)); QVERIFY(operation);
    QVERIFY(graph.until([&] { return operation->terminal() && !controller.walletLoadInProgress(); }));
    QCOMPARE(operation->phase(), WalletOperationModel::Failed); QCOMPARE(operation->result().effect, WalletOperationResult::NoEffect);
    QCOMPARE(operation->error().code, reason == 1 || reason == 2 ? QStringLiteral("TargetExists") : QStringLiteral("UnsupportedBackup"));
    QVERIFY(!operation->errorTitle().contains("legacy", Qt::CaseInsensitive)); QVERIFY(!operation->errorDescription().contains("migration", Qt::CaseInsensitive));
    QCOMPARE(loader.restore_wallet_calls, 0); QCOMPARE(loader.load_wallet_calls, 0); QCOMPARE(loader.migrate_wallet_calls, 0); QCOMPARE(ReadTestFile(path), original);
}
void WalletQmlControllerTests::copiedPrunedWalletReconcilesWithoutRecopy_data()
{
    QTest::addColumn<bool>("replace_copy"); QTest::newRow("same-original-copy") << false; QTest::newRow("changed-copy-rejected") << true;
}
void WalletQmlControllerTests::copiedPrunedWalletReconcilesWithoutRecopy()
{
    QFETCH(bool, replace_copy); QTemporaryDir source, wallets; QVERIFY(source.isValid()); QVERIFY(wallets.isValid());
    const auto backup = source.filePath("pruned.bak"); QVERIFY(WriteDatabaseHint(backup)); const auto original = ReadTestFile(backup);
    MockNode node; FakeWalletLoader loader; loader.wallet_dir = wallets.path().toStdString(); ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    const QPointer<WalletQmlModel> selected_before{controller.selectedWallet()};
    loader.restore_detailed_fn = [&](const auto&, const auto& name, auto&, bool load) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (load) throw std::runtime_error{"Combined restore/load is unsafe"};
        const auto destination = wallets.filePath(QString::fromStdString(name)); if (!QDir{}.mkpath(destination) || !QFile::copy(backup, QDir(destination).filePath("wallet.dat"))) throw std::runtime_error{"Copy failed"};
        loader.wallet_dir_entries.emplace_back(name, "sqlite"); return std::unique_ptr<interfaces::Wallet>{};
    };
    int loads{0}; loader.load_wallet_fn = [&](const auto& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (++loads == 1) return util::Error{Untranslated("Wallet loading failed. Prune: last wallet synchronisation goes beyond pruned data. You need to -reindex.")};
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, nullptr)};
    };
    auto* operation = controller.restore(QUrl::fromLocalFile(backup)); QVERIFY(operation); QVERIFY(graph.until([&] { return operation->terminal(); }));
    QCOMPARE(operation->phase(), WalletOperationModel::Partial); QCOMPARE(operation->error().code, QStringLiteral("PrunedDataUnavailable")); QVERIFY(operation->errorDescription().contains("pruned")); QVERIFY(operation->canReconcile());
    const auto copied = wallets.filePath("pruned/wallet.dat"); QVERIFY(QFileInfo::exists(copied)); QCOMPARE(ReadTestFile(backup), original);
    if (replace_copy) { QFile file{copied}; QVERIFY(file.open(QIODevice::Append)); QCOMPARE(file.write("changed"), qint64{7}); }
    QSignalSpy refused_progress{&controller, &WalletQmlController::walletLoadInProgressChanged};
    graph.native.rejectLane(execution::Lane::Domain); QVERIFY(!operation->reconcile().accepted);
    QVERIFY(operation->terminal()); QVERIFY(!operation->busy()); QVERIFY(!controller.walletLoadInProgress()); QVERIFY(operation->canReconcile()); QCOMPARE(refused_progress.count(), 2); QCOMPARE(loads, 1); QCOMPARE(loader.restore_wallet_calls, 1);
    const auto original_result_id = operation->result().result_id;
    QVERIFY(operation->acknowledge(original_result_id).accepted);
    QVERIFY(operation->reconcile().accepted); QVERIFY(operation->busy()); QVERIFY(!operation->terminal()); QVERIFY(!operation->acknowledge(original_result_id).accepted);
    QVERIFY(graph.until([&] { return operation->terminal() && !controller.walletLoadInProgress(); }));
    QVERIFY(operation->result().result_id != original_result_id); QVERIFY(!operation->acknowledge(original_result_id).accepted); QVERIFY(operation->acknowledge(operation->result().result_id).accepted);
    QCOMPARE(loader.restore_wallet_calls, 1); QCOMPARE(loads, replace_copy ? 1 : 2); QCOMPARE(ReadTestFile(backup), original); QVERIFY(!operation->canReconcile());
    if (replace_copy) { QCOMPARE(operation->phase(), WalletOperationModel::Partial); QCOMPARE(operation->error().code, QStringLiteral("SourceChanged")); QVERIFY(!operation->resultWallet()); }
    else { QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); QVERIFY(operation->canOpenResult()); QVERIFY(operation->warningText().contains("Prune:")); QCOMPARE(controller.selectedWallet(), selected_before.data()); }
}
void WalletQmlControllerTests::migrationPromptRejectsStaleOriginalAnswers()
{
    MockNode node; FakeWalletLoader loader; loader.wallet_dir_entries = {{"original", "bdb"}, {"successor", "bdb"}}; loader.is_encrypted_fn = [](const auto&) { return true; }; ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    auto* original = controller.migrate("original"); QVERIFY(original); QVERIFY(graph.until([&] { return original->prompt(); })); auto* confirmation = original->prompt(); const auto first = confirmation->promptId();
    QVERIFY(!confirmation->answer("wrong-id", WalletOperationPrompt::Confirm).accepted); QCOMPARE(loader.migrate_wallet_calls, 0);
    QSignalSpy refused_progress{&controller, &WalletQmlController::walletLoadInProgressChanged}; graph.native.rejectLane(execution::Lane::Domain);
    QVERIFY(!confirmation->answer(first, WalletOperationPrompt::Confirm).accepted); QVERIFY(!controller.walletLoadInProgress()); QVERIFY(!original->busy()); QCOMPARE(original->prompt(), confirmation); QCOMPARE(confirmation->promptId(), first); QCOMPARE(refused_progress.count(), 2); QCOMPARE(loader.migrate_wallet_calls, 0);
    QVERIFY(confirmation->answer(first, WalletOperationPrompt::Confirm).accepted); QVERIFY(graph.until([&] { return original->phase() == WalletOperationModel::AwaitingPassphrase; }));
    auto* password = original->prompt(); QVERIFY(password != confirmation); QVERIFY(password->promptId() != first); QVERIFY(!confirmation->answer(first, WalletOperationPrompt::Confirm).accepted);
    auto* successor = controller.migrate("successor"); QVERIFY(successor); QVERIFY(graph.until([&] { return successor->prompt(); }));
    QVERIFY(!password->submitSecret(successor->prompt()->promptId(), "must-not-retarget").accepted); QCOMPARE(loader.migrate_wallet_calls, 0);
    QString actual_name; std::string actual_secret; loader.migrate_wallet_fn = [&](const auto& name, const SecureString& secret, bool load) -> util::Result<interfaces::WalletMigrationResult> {
        actual_name = QString::fromStdString(name); actual_secret.assign(secret.begin(), secret.end()); if (load) throw std::runtime_error{"Combined migration is unsafe"}; return interfaces::WalletMigrationResult{};
    };
    QVERIFY(password->submitSecret(password->promptId(), "original secret").accepted); QVERIFY(controller.walletLoadInProgress());
    QVERIFY(graph.until([&] { return original->terminal() && !controller.walletLoadInProgress(); })); QCOMPARE(actual_name, QStringLiteral("original")); QCOMPARE(actual_secret, std::string{"original secret"}); QCOMPARE(loader.migrate_wallet_calls, 1);
    QCOMPARE(successor->phase(), WalletOperationModel::AwaitingConfirmation); QVERIFY(!password->dismiss(password->promptId()).accepted); QVERIFY(successor->prompt()->dismiss(successor->prompt()->promptId()).accepted);
    QVERIFY(graph.until([&] { return successor->terminal(); }));
}
void WalletQmlControllerTests::lateSettingsErrorKeepsTheOriginalRestoredWallet_data()
{
    QTest::addColumn<bool>("unloaded"); QTest::newRow("live-original-notification") << false; QTest::newRow("closed-original-cannot-resurrect") << true;
}
void WalletQmlControllerTests::lateSettingsErrorKeepsTheOriginalRestoredWallet()
{
    QFETCH(bool, unloaded); QTemporaryDir source, wallets; QVERIFY(source.isValid()); QVERIFY(wallets.isValid()); const auto backup = source.filePath("settings.bak"); QVERIFY(WriteDatabaseHint(backup)); const auto bytes = ReadTestFile(backup);
    MockNode node; FakeWalletLoader loader; FakeWallet::State state; loader.wallet_dir = wallets.path().toStdString(); ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    const QPointer<WalletQmlModel> selected_before{controller.selectedWallet()};
    loader.restore_detailed_fn = [&](const auto&, const auto& name, auto&, bool load) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (load) throw std::runtime_error{"Combined restore would delete published storage"};
        const auto destination = wallets.filePath(QString::fromStdString(name)); if (!QDir{}.mkpath(destination) || !QFile::copy(backup, QDir(destination).filePath("wallet.dat"))) throw std::runtime_error{"Copy failed"};
        loader.wallet_dir_entries.emplace_back(name, "sqlite"); return std::unique_ptr<interfaces::Wallet>{};
    };
    loader.load_wallet_fn = [&](const auto& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>(name, &state)); if (unloaded) state.notifyUnload(); throw std::runtime_error{"Attempt to write settings file when dynamic settings are disabled."};
    };
    auto* operation = controller.restore(QUrl::fromLocalFile(backup)); QVERIFY(operation); QVERIFY(graph.until([&] { return operation->terminal() && !controller.walletLoadInProgress(); }));
    QCOMPARE(loader.restore_wallet_calls, 1); QCOMPARE(loader.load_wallet_calls, 1); QCOMPARE(ReadTestFile(backup), bytes); QCOMPARE(ReadTestFile(wallets.filePath("settings/wallet.dat")), bytes);
    QVERIFY(operation->warningText().contains("startup preference")); QCOMPARE(operation->canOpenResult(), !unloaded); QCOMPARE(controller.selectedWallet(), selected_before.data());
    if (unloaded) { QCOMPARE(operation->phase(), WalletOperationModel::Partial); QVERIFY(!operation->canReconcile()); QVERIFY(!controller.walletForName("settings")); }
    else { QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); QCOMPARE(operation->resultWallet(), controller.walletForName("settings")); }
}
void WalletQmlControllerTests::migrationKeepsAuxiliaryRetirementIndependent()
{
    MockNode node; FakeWalletLoader loader; loader.wallet_dir_entries = {{"legacy", "bdb"}}; FakeWallet::State primary, auxiliary; ConfigureExpectedControllerInitialization(node, loader);
    std::atomic_int primary_destroyed{0};
    class PrimaryWallet final : public FakeWallet { std::atomic_int& destroyed; public: PrimaryWallet(State* state, std::atomic_int& count) : FakeWallet("legacy", state), destroyed(count) {} ~PrimaryWallet() override { ++destroyed; } };
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    const QPointer<WalletQmlModel> selected_before{controller.selectedWallet()};
    loader.migrate_wallet_fn = [&](const auto&, const auto&, bool load) -> util::Result<interfaces::WalletMigrationResult> {
        if (load) throw std::runtime_error{"Combined migration is unsafe"};
        interfaces::WalletMigrationResult result; result.wallet = std::make_unique<PrimaryWallet>(&primary, primary_destroyed); result.watchonly_wallet_name = "legacy_watchonly"; result.backup_path = fs::PathFromString("original.legacy.bak"); return result;
    };
    loader.load_wallet_fn = [&](const auto& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> { return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &auxiliary)}; };
    controller.migrateWallet("legacy", ""); auto* operation = controller.creationOperation(); QVERIFY(operation); QVERIFY(graph.until([&] { return operation->terminal(); })); QCOMPARE(operation->phase(), WalletOperationModel::Succeeded);
    auto list = operation->resultWallets(); QCOMPARE(list.count(&list), qsizetype{2}); QCOMPARE(list.at(&list, 0), operation->resultWallet()); auto* sibling = list.at(&list, 1); QVERIFY(sibling); QCOMPARE(sibling->name(), QStringLiteral("legacy_watchonly"));
    QCOMPARE(controller.selectedWallet(), selected_before.data()); QVERIFY(controller.selectWallet(operation->resultWallet()));
    const QPointer<WalletQmlModel> original_primary{operation->resultWallet()}; controller.closeWallet("legacy");
    // Native handle destruction precedes the facade's complete retirement.
    // Reopening is legal after the exact original facade has retired, while
    // its independently owned auxiliary remains open and available.
    QVERIFY(graph.until([&] { return original_primary.isNull() && !controller.walletForName("legacy") && primary_destroyed.load() == 1; })); QVERIFY(sibling->available()); QVERIFY(operation->resultWallets().count(&list) >= qsizetype{1});
    loader.load_wallet_fn = [&](const auto& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> { return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, nullptr)}; };
    controller.setSelectedWallet("legacy", "sqlite"); QVERIFY2(graph.until([&] { return controller.walletForName("legacy") && !controller.walletLoadInProgress(); }), qPrintable(controller.walletLoadError()));
    QVERIFY(operation->resultWallet() != controller.walletForName("legacy")); QVERIFY(!operation->canOpenResult()); QVERIFY(sibling->available()); QCOMPARE(loader.migrate_wallet_calls, 1);
}

void WalletQmlControllerTests::creationUnloadBeforePublicationNeverOffersAPhantomFacade_data()
{
    QTest::addColumn<bool>("notification_first"); QTest::addColumn<bool>("after_return"); QTest::addColumn<bool>("refuse_publication");
    QTest::newRow("unload-in-core-result-first") << false << false << false;
    QTest::newRow("unload-in-core-notification-first") << true << false << false;
    QTest::newRow("unload-after-return-result-first") << false << true << false;
    QTest::newRow("unload-after-return-notification-first") << true << true << false;
    QTest::newRow("refused-publication-unload-in-core") << false << false << true;
    QTest::newRow("refused-publication-unload-after-return") << false << true << true;
}
void WalletQmlControllerTests::creationUnloadBeforePublicationNeverOffersAPhantomFacade()
{
    QFETCH(bool, notification_first); QFETCH(bool, after_return); QFETCH(bool, refuse_publication);
    MockNode node; FakeWalletLoader loader; FakeWallet::State state; ConfigureExpectedControllerInitialization(node, loader);
    std::atomic_int destroyed{0}; std::atomic_bool destroyed_on_gui{false};
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    class ObservedWallet final : public FakeWallet {
        std::atomic_int& count; std::atomic_bool& gui; std::atomic<int>& lane;
    public:
        ObservedWallet(const std::string& name, State* state, std::atomic_int& value, std::atomic_bool& error, std::atomic<int>& active)
            : FakeWallet(name, state), count(value), gui(error), lane(active) {}
        ~ObservedWallet() override { if (lane.load() < 0) gui = true; ++count; }
    };
    loader.create_wallet_fn = [&](const auto& name, const auto&, uint64_t, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loader.captured_load_wallet_fn(std::make_unique<ObservedWallet>(name, &state, destroyed, destroyed_on_gui, graph.lane));
        if (!after_return) state.notifyUnload();
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<ObservedWallet>(name, &state, destroyed, destroyed_on_gui, graph.lane)};
    };
    QSignalSpy inventory{&controller, &WalletQmlController::openWalletsChanged}; QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    auto services = controller.creationServices();
    auto publisher = std::make_shared<CreationPublicationGate>(services.operation.publisher); publisher->refuse_next = refuse_publication; services.operation.publisher = publisher;
    auto* editor = new WalletCreationModel(std::move(services), &controller); const auto destroy_editor = qScopeGuard([editor] { delete editor; });
    connect(editor, &WalletCreationModel::operationCreated, &controller, &WalletQmlController::trackCreation);
    editor->name()->edit("retired-created"); editor->setEncryptRequested(false);
    auto* operation = editor->create({}, {}); QVERIFY(operation); QVERIFY(DriveCreationToCore(graph, *operation));
    // The editor also owns a Native validation preview. Step actual eligible
    // Domain/Core work, withholding GUI publication, until the loader effect
    // itself ran; a Creating projection and one queued Core turn are not proof.
    for (int turn = 0; loader.create_wallet_calls == 0 && turn < 16; ++turn) {
        if (graph.native.queuedCountIf([](const auto& task) { return task.lane == execution::Lane::Domain; })) QVERIFY(graph.step(execution::Lane::Domain));
        else QVERIFY(graph.step(execution::Lane::Core));
    }
    QCOMPARE(loader.create_wallet_calls, 1);
    if (after_return) {
        QVERIFY(execution::Submit(*controller.backgroundScheduler(), [&] { state.notifyUnload(); }, [] {}, {}, {execution::Lane::Control}));
        QVERIFY(graph.step(execution::Lane::Control));
    }
    graph.runNative();
    // Rearrange only the delivery of already-owned mailbox values.
    if (!notification_first) std::reverse(graph.posts.begin(), graph.posts.end());
    QVERIFY(graph.until([&] { return operation->terminal() && !controller.walletLoadInProgress(); }));
    QCOMPARE(operation->phase(), WalletOperationModel::Partial); QCOMPARE(operation->result().wallet_id, QStringLiteral("retired-created"));
    QVERIFY(!controller.walletForName("retired-created")); QVERIFY(!operation->resultWallet()); QVERIFY(!operation->canOpenResult()); QVERIFY(!operation->canReconcile());
    QCOMPARE(inventory.size(), 0); QCOMPARE(created.size(), 0); QCOMPARE(state.remove_calls.load(), 0); QCOMPARE(state.private_accesses.load(), 0);
    QCOMPARE(loader.create_wallet_calls, 1); QVERIFY(!operation->retry().accepted);
    graph.runNative(); QCOMPARE(destroyed.load(), 2); QVERIFY(!destroyed_on_gui.load());
    QCOMPARE(operation->phase(), WalletOperationModel::Partial); QVERIFY(!operation->canReconcile());
}

void WalletQmlControllerTests::creationEditorOwnsTheOriginalSubmission_data()
{
    QTest::addColumn<int>("kind"); QTest::addColumn<bool>("discard_editor");
    for (int kind : {0, 1, 2, 3}) for (bool discard : {false, true}) {
        const QByteArray tag = QByteArray::number(kind) + (discard ? "-detached-editor" : "-edited-editor");
        QTest::newRow(tag.constData()) << kind << discard;
    }
}
void WalletQmlControllerTests::creationEditorOwnsTheOriginalSubmission()
{
    QFETCH(int, kind); QFETCH(bool, discard_editor);
    FakeWallet::State standby_state, result_state; MockNode node; FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    node.get_persistent_setting_fn = [](const std::string& key) { return key == "signer" ? common::SettingsValue{QCoreApplication::applicationFilePath().toStdString()} : common::SettingsValue{}; };
    node.list_external_signers_fn = [] { return MakeSigners({"Device A"}); };
    loader.get_wallets_fn = [&] { std::vector<std::unique_ptr<interfaces::Wallet>> wallets; wallets.push_back(std::make_unique<FakeWallet>("standby", &standby_state)); return wallets; };
    ManualControllerGraph graph{node}; auto& controller = *graph.controller;
    controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized() && !controller.externalSignerPending(); }));
    auto* standby = controller.walletForName("standby"); QVERIFY(standby);
    std::string native_name, native_password; uint64_t native_flags{0}; bool native_lane{false};
    loader.create_wallet_fn = [&](const auto& name, const SecureString& password, uint64_t flags, auto& warnings) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        native_name = name; native_password.assign(password.begin(), password.end()); native_flags = flags;
        native_lane = graph.lane.load() == int(execution::Lane::Core); warnings.push_back(Untranslated("Keep this operation's backup"));
        auto wallet = std::make_unique<FakeWallet>(name, &result_state);
        wallet->private_keys_disabled = kind >= 2; wallet->external_signer = kind == 3;
        return std::unique_ptr<interfaces::Wallet>{std::move(wallet)};
    };
    std::unique_ptr<WalletCreationModel> editor{controller.newCreation()}; QVERIFY(editor);
    editor->chooseKind(kind == 2 ? WalletCreationModel::WatchOnly : kind == 3 ? WalletCreationModel::ExternalSigner : WalletCreationModel::Software);
    editor->setEncryptRequested(kind == 0); editor->name()->edit("original-wallet"); editor->extendedPublicKey()->edit(VALID_XPUB);
    result_state.import_fn = [](auto& requests) { return std::vector<wallet::ImportResult>(requests.size()); };
    const auto submitted_revision = editor->revision();
    QString password = QString::fromUtf8("original-pässwörd-₿");
    auto* operation = editor->create(password, password); QVERIFY(operation); QVERIFY(operation->acceptedForDelivery());
    QSignalSpy terminal{operation, &WalletOperationModel::finished};
    QVERIFY(DriveCreationToCore(graph, *operation));
    QCOMPARE(loader.create_wallet_calls, 0); QVERIFY(controller.walletLoadInProgress());
    password.fill(QLatin1Char('x')); editor->name()->edit("later-draft"); editor->extendedPublicKey()->edit("not-an-xpub"); editor->setEncryptRequested(false);
    QVERIFY(controller.selectWallet(standby));
    controller.clearWalletCreateStatus(); controller.clearWalletLoadStatus();
    QVERIFY(controller.walletLoadInProgress()); QCOMPARE(controller.creationOperation(), operation);
    QCOMPARE(operation->submittedName(), QStringLiteral("original-wallet")); QCOMPARE(operation->editorRevision(), submitted_revision);
    std::unique_ptr<WalletCreationModel> other{controller.newCreation()}; QVERIFY(other); other->name()->edit("duplicate");
    auto* refused = other->create({}, {}); QVERIFY(refused); QVERIFY(!refused->acceptedForDelivery()); QCOMPARE(refused->phase(), WalletOperationModel::Rejected);
    QCOMPARE(other->error().code, QStringLiteral("Busy")); QVERIFY(!other->operation());
    QCOMPARE(controller.creationOperation(), operation); controller.setSelectedWallet("another-core-load"); QCOMPARE(loader.load_wallet_calls, 0);
    if (discard_editor) { editor->discard(); editor.reset(); }
    QVERIFY(graph.until([&] { return operation->terminal() && !controller.walletLoadInProgress(); }));
    QCOMPARE(loader.create_wallet_calls, 1); QCOMPARE(native_name, std::string{"original-wallet"}); QVERIFY(native_lane);
    QCOMPARE(native_password, kind == 0 ? QString::fromUtf8("original-pässwörd-₿").toUtf8().toStdString() : std::string{});
    uint64_t expected{wallet::WALLET_FLAG_DESCRIPTORS};
    if (kind >= 2) expected |= wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS;
    if (kind == 2) expected |= wallet::WALLET_FLAG_BLANK_WALLET;
    if (kind == 3) expected |= wallet::WALLET_FLAG_EXTERNAL_SIGNER;
    QCOMPARE(native_flags, uint64_t{expected}); QCOMPARE(operation->phase(), WalletOperationModel::Succeeded); QCOMPARE(terminal.size(), 1);
    QCOMPARE(operation->result().warnings, QStringList{QStringLiteral("Keep this operation's backup")});
    QCOMPARE(operation->resultWallet(), controller.walletForName("original-wallet")); QVERIFY(operation->canOpenResult());
    QCOMPARE(controller.selectedWallet(), standby); QCOMPARE(result_state.imports.load(), kind == 2 ? 1 : 0); QCOMPARE(result_state.private_accesses.load(), 0);
    QVERIFY(!operation->retry().accepted); QCOMPARE(loader.create_wallet_calls, 1);
    QVERIFY(!operation->acknowledge("another-result").accepted); QVERIFY(operation->acknowledge(operation->result().result_id).accepted);
    QVERIFY(!operation->canAcknowledge()); QCOMPARE(operation->resultWallet(), controller.walletForName("original-wallet"));
}
void WalletQmlControllerTests::canonicalCatalogIdentityPreservesTheExactNativeRpcNamespace()
{
    MockNode node; FakeWalletLoader loader; FakeWallet::State state; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] { std::vector<std::unique_ptr<interfaces::Wallet>> wallets; wallets.push_back(std::make_unique<FakeWallet>("savings/", &state)); return wallets; };
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("savings") && controller.walletForName("savings")->capabilitiesKnown(); }));
    auto* wallet = controller.walletForName("savings"); QVERIFY(wallet);
    QCOMPARE(wallet->name(), QStringLiteral("savings")); QCOMPARE(wallet->rpcName(), QStringLiteral("savings/"));
    QVERIFY(!controller.walletForName("savings/")); QCOMPARE(controller.rowForWallet(wallet), 0);
    QCOMPARE(state.private_accesses.load(), 0);
}

void WalletQmlControllerTests::creationValidationUsesTheFinalIdentity_data()
{
    QTest::addColumn<QString>("name"); QTest::addColumn<bool>("valid");
    QTest::newRow("empty") << QString{} << false;
    QTest::newRow("trailing-separator-original-050") << QStringLiteral("savings/") << false;
    QTest::newRow("absolute-original-050") << QStringLiteral("/outside/savings") << false;
    QTest::newRow("significant-trailing-space-original-315") << QStringLiteral("Savings ") << false;
    QTest::newRow("parent-traversal") << QStringLiteral("../savings") << false;
    QTest::newRow("cleaned-different-path") << QStringLiteral("part/../savings") << false;
    QTest::newRow("loaded-conflict") << QStringLiteral("LOADED") << false;
    QTest::newRow("alias-conflict") << QStringLiteral("My alias") << false;
    QTest::newRow("nested-relative-control") << QStringLiteral("sub/nested") << true;
    QTest::newRow("ordinary-control") << QStringLiteral("Savings") << true;
}
void WalletQmlControllerTests::creationValidationUsesTheFinalIdentity()
{
    QFETCH(QString, name); QFETCH(bool, valid);
    walletcreation::Request request; request.submitted_name = name; request.encrypt = false;
    walletcreation::Facts facts; facts.existing = {{QStringLiteral("loaded"), {}}, {QStringLiteral("alias-owner"), QStringLiteral("My alias")}};
    const auto decision = walletcreation::Validate(request, facts);
    QCOMPARE(decision.error.empty(), valid);
    if (valid) QCOMPARE(decision.name, name); else QCOMPARE(decision.error.field, QStringLiteral("name"));
    QCOMPARE(walletidentity::Canonical("savings/"), QStringLiteral("savings"));
    QCOMPARE(walletidentity::Canonical("/wallets/sub/nested", "/wallets"), QStringLiteral("sub/nested"));
    QCOMPARE(walletidentity::Canonical("Savings "), QStringLiteral("Savings "));
    QVERIFY(walletidentity::Display("Savings") != walletidentity::Display("Savings "));
}
void WalletQmlControllerTests::creationSetupAndPublicationKeepTheActualArtifact_data()
{
    QTest::addColumn<int>("failure");
    QTest::newRow("watch-descriptor-failure") << 0;
    QTest::newRow("publication-refused-once") << 1;
    QTest::newRow("closed-original-during-publication") << 2;
}
void WalletQmlControllerTests::creationSetupAndPublicationKeepTheActualArtifact()
{
    QFETCH(int, failure);
    FakeWallet::State state; MockNode node; FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node}; auto& controller = *graph.controller; controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    QTemporaryDir directory; QVERIFY(directory.isValid()); loader.wallet_dir = directory.path().toStdString();
    state.import_fn = [](auto&) { return std::vector<wallet::ImportResult>{wallet::ImportResult{wallet::WalletErrorCode::GenericError, "descriptor rejected"}}; };
    loader.create_wallet_fn = [&](const auto& name, const auto&, uint64_t, auto& warnings) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        warnings.push_back(Untranslated("The created file must be preserved"));
        auto wallet = std::make_unique<FakeWallet>(name, &state); wallet->private_keys_disabled = failure == 0;
        return std::unique_ptr<interfaces::Wallet>{std::move(wallet)};
    };
    auto services = controller.creationServices();
    auto publisher = std::make_shared<CreationPublicationGate>(services.operation.publisher);
    publisher->refuse_next = failure == 1; services.operation.publisher = publisher;
    auto* editor = new WalletCreationModel(std::move(services), &controller);
    const auto destroy_editor = qScopeGuard([editor] { delete editor; });
    connect(editor, &WalletCreationModel::operationCreated, &controller, &WalletQmlController::trackCreation);
    editor->name()->edit("artifact"); editor->setEncryptRequested(false);
    if (failure == 0) { editor->chooseKind(WalletCreationModel::WatchOnly); editor->extendedPublicKey()->edit(VALID_XPUB); }
    auto* operation = editor->create({}, {}); QVERIFY(operation->acceptedForDelivery()); QVERIFY(DriveCreationToCore(graph, *operation));
    QMetaObject::Connection close;
    if (failure == 2) close = connect(&controller, &WalletQmlController::openWalletsChanged, &controller, [&] { if (controller.walletForName("artifact")) controller.closeWallet("artifact"); });
    QVERIFY(graph.until([&] { return operation->terminal() && !controller.walletLoadInProgress(); }));
    disconnect(close);
    QCOMPARE(loader.create_wallet_calls, 1); QCOMPARE(operation->phase(), WalletOperationModel::Partial);
    QCOMPARE(operation->result().wallet_id, QStringLiteral("artifact")); QVERIFY(!operation->result().recovery_paths.empty());
    QCOMPARE(operation->result().warnings, QStringList{QStringLiteral("The created file must be preserved")}); QVERIFY(!operation->error().empty());
    QVERIFY(!operation->canOpenResult()); QVERIFY(!operation->retry().accepted); QCOMPARE(state.private_accesses.load(), 0);
    if (failure == 0) { QCOMPARE(operation->result().effect, WalletOperationResult::SetupIncomplete); QCOMPARE(state.remove_calls.load(), 1); QVERIFY(!controller.walletForName("artifact")); }
    if (failure == 1) {
        QVERIFY(operation->canReconcile()); QVERIFY(operation->reconcile().accepted);
        QVERIFY(graph.until([&] { return operation->phase() == WalletOperationModel::Succeeded && !controller.walletLoadInProgress(); }));
        QCOMPARE(loader.create_wallet_calls, 1); QCOMPARE(publisher->publications, 2); QVERIFY(operation->canOpenResult()); QCOMPARE(state.remove_calls.load(), 0);
        QVERIFY(operation->error().empty()); QVERIFY(operation->errorText().isEmpty());
        QCOMPARE(operation->result().effect, WalletOperationResult::WalletCreated);
        QCOMPARE(operation->result().warnings, QStringList{QStringLiteral("The created file must be preserved")});
    }
    if (failure == 2) { QVERIFY(!controller.walletForName("artifact")); QVERIFY(!operation->canReconcile()); QCOMPARE(state.remove_calls.load(), 1); }
}

void WalletQmlControllerTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
}

void WalletQmlControllerTests::fakeWalletCleanupRetainsOriginalFixtureState()
{
    std::unique_ptr<FakeWallet> wallet;
    std::unique_ptr<interfaces::Handler> handler;
    std::weak_ptr<FakeWallet::State::Data> original;
    {
        FakeWallet::State state;
        original = state.retained;
        wallet = std::make_unique<FakeWallet>("original", &state);
        handler = wallet->handleUnload([] {});
        QVERIFY(state.hasUnload());
    }
    // End the stack facade before the actual Native cleanup, reproducing the
    // ordering from TSan without leaving a dangling stack mutex behind.
    QVERIFY(!original.expired());
    bool disconnected{false};
    std::thread native{[wallet = std::move(wallet), handler = std::move(handler), original, &disconnected]() mutable {
        auto retained = original.lock();
        handler.reset();
        disconnected = retained && !retained->hasUnload();
        retained.reset();
        wallet.reset();
    }};
    native.join();
    QVERIFY(disconnected);
    QVERIFY(original.expired());
}

void WalletQmlControllerTests::failureDuringReadyPublicationCannotResumeNotifications()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    bool interrupted{false}, failed_inside_ready{false};
    connect(&controller, &WalletQmlController::initializedChanged, &controller, [&] {
        if (interrupted || !controller.initialized()) return;
        interrupted = true;
        QEventLoop loop;
        connect(&controller, &WalletQmlController::walletLoadErrorChanged, &loop, &QEventLoop::quit);
        QTimer watchdog; watchdog.setSingleShot(true);
        connect(&watchdog, &QTimer::timeout, &loop, &QEventLoop::quit);
        loader.captured_load_wallet_fn({});
        watchdog.start(5000); loop.exec();
        failed_inside_ready = !controller.initialized() && !controller.walletLoadError().isEmpty();
    });
    controller.initialize(); QTRY_VERIFY(interrupted);
    QVERIFY(failed_inside_ready); QVERIFY(!controller.initialized());
    QCOMPARE(controller.openWallets().size(), 0); QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::earlierNotificationKeepsItsOriginWhenCreationStartsDuringMetadata()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    struct Probe {
        QSemaphore metadata_entered, metadata_release, create_entered, create_release;
        std::atomic_bool metadata_held{false};
    };
    const auto probe = std::make_shared<Probe>();
    DrainingWalletController controller{node};
    const auto unblock = qScopeGuard([&] { probe->metadata_release.release(); probe->create_release.release(); });
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    struct EarlierWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit EarlierWallet(std::shared_ptr<Probe> p) : FakeWallet("crossed", nullptr), probe(std::move(p)) {}
        std::string getWalletName() override {
            if (!probe->metadata_held.exchange(true)) { probe->metadata_entered.release(); probe->metadata_release.acquire(); }
            return FakeWallet::getWalletName();
        }
    };
    loader.captured_load_wallet_fn(std::make_unique<EarlierWallet>(probe));
    QVERIFY(probe->metadata_entered.tryAcquire(1, 5000));
    loader.create_wallet_fn = [probe](const auto&, const auto&, uint64_t, auto&) {
        probe->create_entered.release(); probe->create_release.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{util::Error{Untranslated("Name already in use")}};
    };
    controller.createWatchOnlyWallet("crossed", VALID_XPUB);
    QVERIFY(controller.walletLoadInProgress());
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
    QCOMPARE(loader.create_wallet_calls, 0);
    struct DeliveryCounter : QObject {
        int count{0};
        bool eventFilter(QObject* target, QEvent* event) override {
            if (target->objectName() == QStringLiteral("wallet-notifications") && event->type() == QEvent::MetaCall) ++count;
            return false;
        }
    } delivered;
    QCoreApplication::instance()->installEventFilter(&delivered);
    const auto detach = qScopeGuard([&] { QCoreApplication::instance()->removeEventFilter(&delivered); });
    probe->metadata_release.release();
    // Both effects use the original serialized Core lane. The GUI command was
    // admitted while metadata was held; let Native metadata finish before
    // waiting for Native creation. Keep GUI publication held during this phase
    // so its name-registry update cannot turn the intended creation into a
    // preflight-only conflict. The queued earlier notification is released below.
    QVERIFY(probe->create_entered.tryAcquire(1, 5000));
    QTRY_VERIFY(delivered.count > 0);
    // This wallet was announced before the new command began. Its captured
    // producer context must survive the later same-name GUI create attempt.
    QCOMPARE(controller.openWallets().size(), 1);
    QVERIFY(controller.isWalletOpen("crossed"));
    QVERIFY(controller.walletLoadInProgress());
    probe->create_release.release(); QTRY_VERIFY(!controller.walletLoadInProgress());
    QVERIFY(!controller.walletLoadError().isEmpty());
    QVERIFY(controller.isWalletOpen("crossed"));
    QCOMPARE(loader.create_wallet_calls, 1); QCOMPARE(loader.get_wallets_calls, 1);
}



void WalletQmlControllerTests::cleanupFailureKeepsItsExplicitTerminalError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    struct Handler : interfaces::Handler {
        void disconnect() override { throw std::runtime_error("synthetic disconnect failure"); }
    };
    loader.handler_fn = [] { return std::make_unique<Handler>(); };
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    loader.captured_load_wallet_fn({});
    QTRY_COMPARE(controller.walletLoadError(), QString{"Wallet notification cleanup failed."});
    QVERIFY(!controller.initialized()); QCOMPARE(controller.openWallets().size(), 0);
    QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::changedWakeupWaitsForAnEarlierShadowDisposal()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("existing", nullptr)); return wallets;
    };
    struct Probe { QThread* gui{QThread::currentThread()}; QSemaphore disposing, release, metadata; std::atomic_bool off_gui{false}; };
    const auto probe = std::make_shared<Probe>();
    DrainingWalletController controller{node};
    const auto unblock = qScopeGuard([probe] { probe->release.release(); });
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    struct Duplicate : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit Duplicate(std::shared_ptr<Probe> p) : FakeWallet("existing", nullptr), probe(std::move(p)) {}
        ~Duplicate() override {
            probe->off_gui = QThread::currentThread() != probe->gui;
            probe->disposing.release(); probe->release.acquire();
        }
    };
    struct Next : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit Next(std::shared_ptr<Probe> p) : FakeWallet("next", nullptr), probe(std::move(p)) {}
        std::string getWalletName() override {
            if (QThread::currentThread() != probe->gui) probe->metadata.release();
            return FakeWallet::getWalletName();
        }
    };
    struct DeliveryCounter : QObject {
        int count{0};
        bool eventFilter(QObject* target, QEvent* event) override {
            if (target->objectName() == QStringLiteral("wallet-notifications") && event->type() == QEvent::MetaCall) ++count;
            return false;
        }
    } delivered;
    QCoreApplication::instance()->installEventFilter(&delivered);
    const auto detach = qScopeGuard([&] { QCoreApplication::instance()->removeEventFilter(&delivered); });
    bool second_started{false}, metadata_ready{false};
    connect(&controller, &WalletQmlController::walletLoadStateChanged, &controller, [&](const QString& name) {
        if (name != "existing" || std::exchange(second_started, true)) return;
        loader.captured_load_wallet_fn(std::make_unique<Next>(probe));
        metadata_ready = probe->metadata.tryAcquire(1, 5000);
    });
    loader.captured_load_wallet_fn(std::make_unique<Duplicate>(probe));
    QTRY_VERIFY(probe->disposing.available() > 0);
    QTRY_VERIFY(delivered.count >= 2);
    QVERIFY(metadata_ready); QVERIFY(probe->off_gui.load());
    QVERIFY(!controller.isWalletOpen("next")); // Disposal still owns the previous handoff.
    probe->release.release();
    QTRY_VERIFY(controller.isWalletOpen("next"));
    QCOMPARE(controller.openWallets().size(), 2); QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::failureConsumedBeforeAnEarlierReadyMessageCannotResurrectInitialization()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    node.ExpectExactly(node.calls.walletLoader, 2);
    node.ExpectNoCalls(node.calls.forceSetting);
    node.ExpectNoCalls(node.calls.listExternalSigners);
    ManualControllerGraph graph{node};
    QSignalSpy initialized{graph.controller.get(), &WalletQmlController::initializedChanged};
    graph.controller->initialize();
    graph.runNative(); // Actual subscription registration/capture and Ready production, with GUI still held.
    QCOMPARE(loader.get_wallets_calls, 1);
    QVERIFY(loader.captured_load_wallet_fn);
    QVERIFY(!graph.posts.empty());
    QVERIFY(!graph.controller->initialized());
    QCOMPARE(initialized.count(), 0);
    // Preserve every earlier publication, including Ready, without guessing
    // that the first transport MetaCall is a lifecycle publication.
    auto earlier_publications = std::move(graph.posts);
    graph.posts.clear();
    const auto callback = loader.captured_load_wallet_fn;
    callback({});
    QVERIFY(graph.until([&] { return !graph.controller->walletLoadError().isEmpty(); }));
    QVERIFY(!graph.controller->initialized());
    QCOMPARE(graph.controller->openWallets().size(), 0);
    while (!earlier_publications.empty()) {
        auto earlier = std::exchange(earlier_publications.front(), {});
        earlier_publications.pop_front();
        earlier();
        QVERIFY(!graph.controller->initialized());
        QCOMPARE(graph.controller->openWallets().size(), 0);
    }
    QCOMPARE(initialized.count(), 0);
    QCOMPARE(loader.get_wallets_calls, 1);
    graph.shutdown();
    QCOMPARE(graph.native.scheduler->pending(), size_t{0});
}

void WalletQmlControllerTests::successfulEmptyMigrationDoesNotFabricateAWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; loader.wallet_dir_entries = {{"empty-legacy", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);
    std::atomic_bool load_requested{false};
    loader.migrate_wallet_fn = [&load_requested](const auto&, const auto&, bool load_wallet) {
        load_requested = load_wallet;
        // Pinned Core can remove an empty migrated wallet and return success
        // without loading a primary handle. The result is still owned.
        return util::Result<interfaces::WalletMigrationResult>{interfaces::WalletMigrationResult{}};
    };
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    QSignalSpy completed{&controller, &WalletQmlController::walletMigrationSucceeded};
    controller.migrateWallet("empty-legacy", "");
    QTRY_COMPARE(completed.count(), 1);
    QVERIFY(!load_requested.load());
    QVERIFY(controller.initialized()); QCOMPARE(controller.openWallets().size(), 0);
    QVERIFY(!controller.isWalletLoaded()); QCOMPARE(controller.selectedWallet()->name(), QString{});
    QVERIFY(!controller.walletMigrationInProgress()); QVERIFY(controller.walletMigrationError().isEmpty());
    QCOMPARE(loader.migrate_wallet_calls, 1);
}

void WalletQmlControllerTests::catalogCommandsBeforeInitializationAcquireNoWalletResources()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller{node};
    controller.closeWallet("unavailable");
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
    QVERIFY(!controller.createExternalSignerWallet("unavailable"));
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
    QCOMPARE(node.calls.walletLoader.load(), 0);
}


void WalletQmlControllerTests::initializationIsSingleShotAcrossPendingSuccessFailureAndShutdown_data()
{
    QTest::addColumn<int>("phase");
    QTest::newRow("pending") << 0;
    QTest::newRow("ready") << 1;
    QTest::newRow("failed") << 2;
    QTest::newRow("closing") << 3;
}

void WalletQmlControllerTests::initializationIsSingleShotAcrossPendingSuccessFailureAndShutdown()
{
    QFETCH(int, phase);
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
    if (phase < 2) ConfigureExpectedControllerInitialization(node, loader);
    QSemaphore entered, release;
    const auto unblock = qScopeGuard([&] { release.release(); });
    loader.get_wallets_fn = [&] {
        entered.release(); release.acquire();
        if (phase == 2) throw std::runtime_error("capture unavailable");
        return std::vector<std::unique_ptr<interfaces::Wallet>>{};
    };
    DrainingWalletController controller{node};
    if (phase == 3) {
        controller.beginShutdown(); controller.initialize();
        QCOMPARE(loader.handle_load_wallet_calls, 0); QCOMPARE(loader.get_wallets_calls, 0);
        return;
    }
    controller.initialize();
    QVERIFY(entered.tryAcquire(1, 5000));
    if (phase == 0) controller.initialize();
    release.release();
    if (phase == 2) QTRY_VERIFY(!controller.walletLoadError().isEmpty());
    else QTRY_VERIFY(controller.initialized());
    controller.initialize();
    QCOMPARE(loader.handle_load_wallet_calls, 1); QCOMPARE(loader.get_wallets_calls, 1);
    QCOMPARE(controller.initialized(), phase != 2);
}

void WalletQmlControllerTests::runtimeDisabledWalletDoesNotAcquireItsLoader()
{
    std::map<std::string, common::SettingsValue> original;
    gArgs.LockSettings([&](common::Settings& settings) { original = settings.forced_settings; });
    const auto restore = qScopeGuard([&] { gArgs.LockSettings([&](common::Settings& settings) { settings.forced_settings = original; }); });
    gArgs.ForceSetArg("-disablewallet", "1");
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller{node};
    controller.initialize();
    QVERIFY(!controller.initialized()); QVERIFY(controller.walletLoadError().isEmpty());
    QCOMPARE(node.calls.walletLoader.load(), 0);
}

void WalletQmlControllerTests::successfulImportPublishesTheOwnedReturnedWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    QTemporaryDir dir; QVERIFY(dir.isValid());
    QFile backup{dir.filePath("wallet.bak")}; QVERIFY(backup.open(QIODevice::WriteOnly)); QVERIFY(backup.write(QByteArray{"SQLite format 3\0", 16} + QByteArray(496, '\0')) == 512); backup.close();
    loader.restore_wallet_fn = [] {
        return util::Result<std::unique_ptr<interfaces::Wallet>>{std::make_unique<FakeWallet>("imported", nullptr)};
    };
    QSignalSpy imported{&controller, &WalletQmlController::walletImportSucceeded};
    controller.importWallet(backup.fileName());
    QTRY_COMPARE(imported.count(), 1);
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QCOMPARE(controller.walletOperationResultWallet(), qvariant_cast<WalletQmlModel*>(imported.front().front()));
    QVERIFY(controller.canOpenResult());
    QVERIFY(controller.selectWallet(controller.walletOperationResultWallet()));
    QCOMPARE(qvariant_cast<WalletQmlModel*>(imported.front().front()), controller.selectedWallet());
    QCOMPARE(controller.selectedWallet()->name(), QString{"imported"});
    QVERIFY(controller.isWalletOpen("imported")); QVERIFY(!controller.walletLoadInProgress());
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::successfulMigrationPublishesTheOwnedReturnedWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; loader.wallet_dir_entries = {{"legacy", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);
    std::atomic_bool load_requested{false};
    loader.migrate_wallet_fn = [&load_requested](const auto&, const auto&, bool load_wallet) {
        load_requested = load_wallet;
        interfaces::WalletMigrationResult result;
        result.wallet = std::make_unique<FakeWallet>("migrated", nullptr);
        return util::Result<interfaces::WalletMigrationResult>{std::move(result)};
    };
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    QSignalSpy migrated{&controller, &WalletQmlController::walletMigrationSucceeded};
    controller.migrateWallet("legacy", "");
    QTRY_COMPARE(migrated.count(), 1);
    QVERIFY(!load_requested.load());
    QVERIFY(controller.isWalletOpen("migrated"));
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QVERIFY(controller.canOpenResult());
    QCOMPARE(controller.walletOperationResultWallet()->name(), QString{"migrated"});
    QVERIFY(controller.selectWallet(controller.walletOperationResultWallet()));
    QCOMPARE(controller.selectedWallet()->name(), QString{"migrated"});
    QVERIFY(!controller.walletMigrationInProgress()); QVERIFY(controller.walletMigrationError().isEmpty());
    QCOMPARE(loader.migrate_wallet_calls, 1);
}

void WalletQmlControllerTests::notificationsDuringBatchedStartupArePresentedAfterInitialization()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("notified", nullptr));
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        for (int i{0}; i < 35; ++i) wallets.push_back(std::make_unique<FakeWallet>(QStringLiteral("initial-%1").arg(i).toStdString(), nullptr));
        return wallets;
    };
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    QTRY_COMPARE(controller.openWallets().size(), 36);
    QVERIFY(controller.isWalletOpen("notified"));
    for (int i{0}; i < 35; ++i) QVERIFY(controller.isWalletOpen("initial-" + QString::number(i)));
    QCOMPARE(loader.get_wallets_calls, 1); QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::failurePublicationSurvivesReentrantRetirement_data()
{
    QTest::addColumn<bool>("destroy_owner");
    QTest::newRow("close-on-readiness-invalidation") << false;
    QTest::newRow("destroy-on-readiness-invalidation") << true;
}

void WalletQmlControllerTests::failurePublicationSurvivesReentrantRetirement()
{
    QFETCH(bool, destroy_owner);
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    auto controller = std::make_unique<WalletQmlController>(node);
    const auto drain = qScopeGuard([&] {
        controller.reset(); QObject receiver; bool drained{false};
        BackendExecutor::shutdownAll(&receiver, [&] { drained = true; });
        QTRY_VERIFY(drained); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    controller->initialize(); QTRY_VERIFY(controller->initialized());
    QObject receiver; bool interrupted{false};
    connect(controller.get(), &WalletQmlController::initializedChanged, &receiver, [&] {
        if (interrupted) return;
        interrupted = true;
        QVERIFY(!controller->initialized()); QVERIFY(!controller->walletLoadError().isEmpty());
        if (destroy_owner) controller.reset(); else controller->beginShutdown();
    });
    loader.captured_load_wallet_fn({});
    QTRY_VERIFY(interrupted);
    QCOMPARE(loader.create_wallet_calls, 0);
}

void WalletQmlControllerTests::notificationDuringNestedPresentationKeepsItsContinuation()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    struct Probe { QThread* gui{QThread::currentThread()}; QSemaphore metadata; };
    auto probe = std::make_shared<Probe>();
    struct NotifiedWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        explicit NotifiedWallet(std::shared_ptr<Probe> p) : FakeWallet("second", nullptr), probe(std::move(p)) {}
        std::string getWalletName() override {
            if (QThread::currentThread() != probe->gui) probe->metadata.release();
            return FakeWallet::getWalletName();
        }
    };
    bool nested_delivery{false}, intercepted{false}, metadata_ready{false};
    connect(&controller, &WalletQmlController::openWalletsChanged, &controller, [&] {
        if (intercepted) return;
        intercepted = true;
        const auto callback = loader.captured_load_wallet_fn;
        std::thread producer{[callback, probe] { callback(std::make_unique<NotifiedWallet>(probe)); }};
        producer.join();
        metadata_ready = probe->metadata.tryAcquire(1, 5000);
        if (!metadata_ready) return;
        QEventLoop loop;
        struct DeliveryObserver : QObject {
            QEventLoop& loop;
            bool delivered{false};
            explicit DeliveryObserver(QEventLoop& l) : loop(l) {}
            bool eventFilter(QObject* receiver, QEvent* event) override {
                if (receiver->objectName() == QStringLiteral("wallet-notifications") && event->type() == QEvent::MetaCall) {
                    delivered = true; loop.quit();
                }
                return false;
            }
        } observer{loop};
        QCoreApplication::instance()->installEventFilter(&observer);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        QCoreApplication::instance()->removeEventFilter(&observer);
        nested_delivery = observer.delivered;
    });
    const auto callback = loader.captured_load_wallet_fn;
    std::thread producer{[callback] { callback(std::make_unique<FakeWallet>("first", nullptr)); }};
    producer.join();
    QTRY_VERIFY(intercepted);
    QVERIFY(metadata_ready); QVERIFY(nested_delivery);
    // No third backend callback or user action may be required to expose the
    // second wallet after its wakeup arrived during an active presentation.
    QTRY_VERIFY(controller.isWalletOpen("second"));
    QVERIFY(controller.isWalletOpen("first"));
    QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::subscriptionFailureDisablesCatalogCommands()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    struct InvalidMetadata : FakeWallet {
        InvalidMetadata() : FakeWallet("unavailable", nullptr) {}
        std::string getWalletName() override { throw std::runtime_error("synthetic metadata failure"); }
    };
    loader.captured_load_wallet_fn(std::make_unique<InvalidMetadata>());
    QTRY_VERIFY(!controller.walletLoadError().isEmpty());
    QVERIFY(!controller.initialized());
    controller.createSingleSigWallet("after-failure", "");
    QCOMPARE(loader.create_wallet_calls, 0);
    QVERIFY(!controller.walletCreateError().isEmpty());
}


void WalletQmlControllerTests::startupSnapshotMergesNotificationHandlesWithoutDuplicateModels()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    FakeWallet::State state;
    ConfigureExpectedControllerInitialization(node, loader);
    struct Probe {
        QThread* gui{QThread::currentThread()};
        std::atomic_bool subscribed_before_capture{false}, duplicate_destroyed{false}, duplicate_off_gui{false};
    };
    const auto probe = std::make_shared<Probe>();
    struct DuplicateWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        DuplicateWallet(State* state, std::shared_ptr<Probe> p) : FakeWallet("shared", state), probe(std::move(p)) {}
        ~DuplicateWallet() override {
            probe->duplicate_off_gui = QThread::currentThread() != probe->gui;
            probe->duplicate_destroyed = true;
        }
    };
    loader.get_wallets_fn = [&loader, &state, probe] {
        probe->subscribed_before_capture = bool(loader.captured_load_wallet_fn);
        if (loader.captured_load_wallet_fn) {
            loader.captured_load_wallet_fn(std::make_unique<DuplicateWallet>(&state, probe));
            loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("notification-only", nullptr));
        }
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("shared", &state));
        return wallets;
    };
    DrainingWalletController controller{node};
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QTRY_VERIFY(probe->duplicate_destroyed.load());
    QVERIFY(probe->subscribed_before_capture.load());
    QVERIFY(probe->duplicate_off_gui.load());
    QCOMPARE(controller.openWallets().size(), 2);
    QVERIFY(controller.isWalletOpen("shared"));
    QVERIFY(controller.isWalletOpen("notification-only"));
    QCOMPARE(loader.get_wallets_calls, 1);
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::lifecyclePublicationSurvivesReentrantRetirement_data()
{
    QTest::addColumn<bool>("initial");
    QTest::addColumn<bool>("destroy_owner");
    QTest::addColumn<int>("signal_index");
    for (bool initial : {false, true}) {
        for (bool destroy : {false, true}) {
            for (int signal{0}; signal < (initial ? 8 : 6); ++signal) {
                const QByteArray label = QByteArray(initial ? "initial-" : "notification-") +
                    (destroy ? "destroy-" : "close-") + QByteArray::number(signal);
                QTest::newRow(label.constData()) << initial << destroy << signal;
            }
        }
    }
}

void WalletQmlControllerTests::lifecyclePublicationSurvivesReentrantRetirement()
{
    QFETCH(bool, initial);
    QFETCH(bool, destroy_owner);
    QFETCH(int, signal_index);
    FakeWallet::State state;
    std::atomic<int> signer_reads{0}, signer_policy_reads{0}, signer_gui_reads{0}, unexpected_settings{0}, gui_metadata_reads{0};
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    if (!initial) ConfigureExpectedControllerInitialization(node, loader);
    else {
        node.wallet_loader_fn = [&loader]() -> interfaces::WalletLoader& { return loader; };
        node.is_setting_ignored_fn = [&](const std::string& name) {
            if (name == "signer") ++signer_policy_reads;
            else ++unexpected_settings;
            if (QThread::currentThread() == qApp->thread()) ++signer_gui_reads;
            return false;
        };
        node.get_persistent_setting_fn = [&](const std::string& name) {
            if (name == "signer") {
                ++signer_reads;
                if (QThread::currentThread() == qApp->thread()) ++signer_gui_reads;
                return signal_index == 6 ? common::SettingsValue{std::string{"synthetic-signer"}} : common::SettingsValue{};
            }
            if (name == "qml_receive_address_types") {
                if (QThread::currentThread() == qApp->thread()) ++gui_metadata_reads;
            } else ++unexpected_settings;
            return common::SettingsValue{};
        };
        node.force_setting_fn = [](const std::string&, const common::SettingsValue&) {};
        node.list_external_signers_fn = [] { return std::vector<std::unique_ptr<interfaces::ExternalSigner>>{}; };
        node.get_dust_relay_fee_fn = [] { return CFeeRate{3000}; };
        // Registration + the single startup getter + canonical-identity path.
        // The public getWalletDir read is not another inventory recapture.
        node.ExpectExactly(node.calls.walletLoader, 3);
        const int signer_calls = 0;
        node.ExpectAtLeast(node.calls.getPersistentSetting, signer_calls);
        node.ExpectExactly(node.calls.forceSetting, signer_calls);
        node.ExpectExactly(node.calls.listExternalSigners, signer_calls);
    }
    if (initial) loader.get_wallets_fn = [&state] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("initial-wallet", &state));
        return wallets;
    };
    ManualRoutedExecution signer_environment;
    auto controller = std::make_unique<WalletQmlController>(node);
    controller->setExternalSignerServices({signer_environment.scheduler, {}});
    const auto drain = qScopeGuard([&] {
        controller.reset();
        signer_environment.drain();
        QVERIFY(signer_environment.quiescent());
        QObject receiver;
        bool drained{false};
        BackendExecutor::shutdownAll(&receiver, [&] { drained = true; });
        QTRY_VERIFY(drained);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        if (initial) {
            // R15 independently admits the effective configuration read. Its
            // public signer-policy/persisted-value pair may finish after
            // retirement; the discovery request never reaches Core work.
            QVERIFY(signer_reads.load() <= (signal_index >= 6 ? 1 : 0));
            QCOMPARE(signer_policy_reads.load(), signer_reads.load());
            QCOMPARE(signer_gui_reads.load(), 0);
            QCOMPARE(unexpected_settings.load(), 0);
            QCOMPARE(gui_metadata_reads.load(), 0);
            QCOMPARE(loader.handle_load_wallet_calls, 1);
            QCOMPARE(loader.get_wallets_calls, 1);
            QCOMPARE(loader.get_wallet_dir_calls.load(), 1);
            QVERIFY(loader.directory_path_thread.load() != qApp->thread());
        }
    });
    if (!initial) {
        controller->initialize();
        QTRY_VERIFY(controller->initialized());
        signer_environment.drain();
        QTRY_VERIFY(!controller->externalSignerPending());
    }
    controller->setNoWalletsFound(true);
    QSignalSpy retired{controller.get(), &WalletQmlController::walletsDrained};
    std::vector<bool> readiness;
    QObject receiver;
    const QPointer<WalletQmlController> observed{controller.get()};
    connect(controller.get(), &WalletQmlController::initializedChanged, &receiver, [observed, &readiness] {
        if (observed) readiness.push_back(observed->initialized());
    });
    bool interrupted{false};
    bool original_signer_observation_held{false};
    const auto interrupt = [&] {
        if (interrupted) return;
        interrupted = true;
        if (initial && signal_index >= 6) {
            original_signer_observation_held = (signer_environment.queuedCountIf([](const auto& turn) {
                // R15 requests enter a non-inline Domain planning turn before
                // acquiring Core/process work; either original stage is held.
                return (turn.lane == execution::Lane::Domain || turn.lane == execution::Lane::Core)
                    && turn.phase == execution::Phase::Queued;
            }) != 0);
        }
        if (destroy_owner) controller.reset();
        else controller->beginShutdown();
    };
    switch (signal_index) {
    case 0: connect(controller.get(), &WalletQmlController::walletLoadStateChanged, &receiver, interrupt); break;
    case 1: connect(controller.get(), &WalletQmlController::walletInfoChanged, &receiver, interrupt); break;
    case 2: connect(controller.get(), &WalletQmlController::openWalletsChanged, &receiver, interrupt); break;
    case 3: connect(controller.get(), &WalletQmlController::isWalletLoadedChanged, &receiver, interrupt); break;
    case 4: connect(controller.get(), &WalletQmlController::noWalletsFoundChanged, &receiver, interrupt); break;
    case 5: connect(controller.get(), &WalletQmlController::selectedWalletChanged, &receiver, interrupt); break;
    case 6: connect(controller.get(), &WalletQmlController::externalSignerStatusChanged, &receiver, interrupt); break;
    case 7: connect(controller.get(), &WalletQmlController::initializedChanged, &receiver, interrupt); break;
    }
    if (initial) controller->initialize();
    else {
        const auto callback = loader.captured_load_wallet_fn;
        std::thread producer{[&state, callback] { callback(std::make_unique<FakeWallet>("notified-wallet", &state)); }};
        producer.join();
    }
    if (!initial && signal_index == 5) {
        QTRY_VERIFY(controller && controller->walletForName("notified-wallet"));
        QVERIFY(!interrupted);
        controller->selectWallet(controller->walletForName("notified-wallet"));
    }
    QTRY_VERIFY(interrupted);
    if (initial && signal_index >= 6) {
        QVERIFY(original_signer_observation_held);
        if (!destroy_owner) QCOMPARE(retired.size(), 0);
        // The close fences this admitted planning turn; it still owns an
        // outcome and drain acknowledgment. Release that original transport
        // before asking for complete physical retirement, without ever
        // dispatching a signer process or a replacement observation.
        signer_environment.drain();
        QVERIFY(signer_environment.quiescent());
        QCOMPARE(signer_environment.coreSubmissions(), size_t{0});
    }
    if (!destroy_owner) {
        QTRY_COMPARE(retired.size(), 1);
        QVERIFY(!controller->initialized());
    }
    QCOMPARE(std::count(readiness.begin(), readiness.end(), true), initial && signal_index == 7 ? 1 : 0);
    QCOMPARE(loader.create_wallet_calls, 0);
    QCOMPARE(loader.get_wallets_calls, 1);
}

void WalletQmlControllerTests::closeTargetPublicationCanRetireOriginalFacade()
{
    MockNode node;
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{}; };
    node.force_setting_fn = [](const std::string&, const common::SettingsValue&) {};
    FakeWalletLoader loader;
    FakeWallet::State state;
    loader.get_wallets_fn = [&state] {
        std::vector<std::unique_ptr<interfaces::Wallet>> values;
        values.push_back(std::make_unique<FakeWallet>("original", &state));
        return values;
    };
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    QPointer<WalletQmlModel> original{controller.selectedWallet()};
    bool retired{false};
    connect(&controller, &WalletQmlController::closeTargetChanged, this, [&] {
        if (retired || !controller.closeTarget()) return;
        retired = true;
        controller.closeWallet("original");
        QVERIFY(QTest::qWaitFor([&] {
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            return original.isNull();
        }, 5000));
    });
    controller.requestCloseWallet(original);
    QVERIFY(retired); QVERIFY(original.isNull());
    QVERIFY(!controller.closeTarget());
    QVERIFY(!controller.isWalletOpen("original"));
    QCOMPARE(state.remove_calls.load(), 1);
}

void WalletQmlControllerTests::importErrorProjectionUsesOriginalRevisionAndVirtualFullText()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ManualExecutionEnvironment env;
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    controller.setImportErrorServicesForTesting({env.scheduler, [&env](auto publish) { env.post(std::move(publish)); }});
    QSignalSpy raw_changed{&controller, &WalletQmlController::walletLoadErrorChanged};
    controller.setWalletLoadError(QStringLiteral("old legacy wallet"));
    QVERIFY(controller.walletImportErrorPending());
    env.drain(); // Native prepared the old revision; its GUI delivery is held.
    const QString original = QStringLiteral("  verification failed: ") + QString(5000, QLatin1Char('x')) + QStringLiteral("  ");
    controller.setWalletLoadError(original);
    env.publishAll(); // Old completion cannot classify the newer original error.
    QVERIFY(controller.walletImportErrorPending());
    QVERIFY(!controller.walletImportErrorTitle().contains(QStringLiteral("migrated")));
    env.drain(); env.publishAll();
    QVERIFY(!controller.walletImportErrorPending());
    QCOMPARE(controller.walletLoadError(), original);
    QCOMPARE(controller.walletImportErrorTitle(), QStringLiteral("This wallet type is not supported"));
    QCOMPARE(raw_changed.count(), 1);
    auto* lines = controller.walletImportErrorHelpModel();
    QVERIFY(lines->rowCount() > 1);
    QString complete;
    for (int i = 0; i < lines->rowCount(); ++i) {
        const auto chunk = lines->data(lines->index(i, 0), Qt::UserRole).toString();
        QVERIFY(chunk.size() <= 1024); complete += chunk;
    }
    QCOMPARE(complete, original);
    controller.setWalletLoadError(QString{original.constData(), original.size()}); // Native equality suppresses duplicate raw notification.
    env.drain(); env.publishAll();
    QCOMPARE(raw_changed.count(), 1);
    bool replaced{false};
    const auto reentrant = connect(lines, &QAbstractItemModel::modelAboutToBeReset, &controller, [&] {
        if (std::exchange(replaced, true)) return;
        controller.setWalletLoadError(QStringLiteral("winner does not exist"));
    });
    controller.setWalletLoadError(QStringLiteral("losing legacy wallet"));
    env.drain(); env.publishAll();
    QVERIFY(replaced);
    QCOMPARE(controller.walletLoadError(), QStringLiteral("winner does not exist"));
    QCOMPARE(controller.walletImportErrorTitle(), QStringLiteral("We couldn't find that file"));
    QCOMPARE(raw_changed.count(), 2);
    disconnect(reentrant);
    controller.setWalletLoadError(QStringLiteral("late legacy wallet"));
    controller.closeAdmission();
    env.drain(); env.publishAll(); // Quiet fence suppresses accepted late observations.
    QCOMPARE(raw_changed.count(), 2);
}

void WalletQmlControllerTests::importErrorCaptureOverflowPreservesActualOutcomeAndRetries()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ManualExecutionEnvironment env;
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    controller.setImportErrorServicesForTesting({env.scheduler, [&env](auto publish) { env.post(std::move(publish)); }});
    for (int i = 0; i < 128; ++i) controller.setWalletLoadError(QStringLiteral("held error %1").arg(i));
    QVERIFY(controller.walletImportErrorPending());
    const QString actual_error = QStringLiteral("verification failed: ") + QString(5000, QLatin1Char('z'));
    QVERIFY(controller.submitWalletCommand([actual_error] {
        WalletQmlController::CommandResult result;
        result.error = actual_error; result.effect_succeeded = true; result.incomplete = true;
        return result;
    }, [&controller](const auto& result) { controller.setWalletLoadError(result.error); }, [](auto) {}));
    QTRY_VERIFY(controller.walletOperationState() != WalletQmlController::WalletOperationState::WalletOperationPending);
    QVERIFY(controller.walletImportErrorUnavailable());
    QVERIFY(controller.walletImportErrorRetryAvailable());
    QVERIFY(controller.walletLoadError() != actual_error); // Stale original details are hidden, actual operation receipt is unchanged.
    const auto actual = controller.m_command->result();
    QVERIFY(actual && actual->value);
    QVERIFY(actual->value->effect_succeeded);
    QVERIFY(actual->value->incomplete);
    QCOMPARE(actual->value->error, actual_error);
    env.drain(); env.publishAll(); // Native disposal releases old frame capacity.
    QVERIFY(controller.retryWalletImportErrorPresentation());
    QVERIFY(!controller.walletImportErrorUnavailable());
    env.drain(); env.publishAll();
    QVERIFY(!controller.walletImportErrorPending());
    QCOMPARE(controller.walletLoadError(), actual_error);
    QCOMPARE(controller.walletImportErrorTitle(), QStringLiteral("This wallet type is not supported"));
    QVERIFY(controller.walletImportErrorProjectionError().isEmpty());
    controller.closeAdmission(); env.drain(); env.publishAll();
}

void WalletQmlControllerTests::importErrorProjectionRejectionAndUnknownOversizedSourceStayExplicit()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ManualExecutionEnvironment env;
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    controller.setImportErrorServicesForTesting({env.scheduler, [&env](auto publish) { env.post(std::move(publish)); }});
    env.rejectNext(true);
    controller.setWalletLoadError(QStringLiteral("file does not exist"));
    env.drain(); env.publishAll();
    QVERIFY(!controller.walletImportErrorPending());
    QVERIFY(!controller.walletImportErrorProjectionError().isEmpty());
    QVERIFY(controller.walletImportErrorRetryAvailable());
    QVERIFY(controller.retryWalletImportErrorPresentation());
    env.drain(); env.publishAll();
    QCOMPARE(controller.walletImportErrorTitle(), QStringLiteral("We couldn't find that file"));
    QVERIFY(controller.walletImportErrorProjectionError().isEmpty());
    // No Native command owns this synthetic producer. It exceeds the explicit
    // retained byte budget and must never masquerade as the prior failure.
    const QString oversized(8 * 1024 * 1024 + 1, QLatin1Char('q'));
    controller.setWalletLoadError(oversized);
    QVERIFY(controller.walletImportErrorUnavailable());
    QVERIFY(!controller.walletImportErrorRetryAvailable());
    QVERIFY(!controller.retryWalletImportErrorPresentation());
    QVERIFY(controller.walletLoadError() != QStringLiteral("file does not exist"));
    QCOMPARE(controller.walletImportErrorHelpModel()->rowCount(), 0);
    controller.closeAdmission(); env.drain(); env.publishAll();
}

void WalletQmlControllerTests::createErrorProjectionRetainsNativeDocumentAndFencesSupersededPublication()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    const QString original = QStringLiteral("Actual creation failure: ") + QString(5000, QLatin1Char('c'));
    QVERIFY(controller.submitWalletCommand([original] {
        WalletQmlController::CommandResult result; result.error = original; result.incomplete = true; result.effect_succeeded = true; return result;
    }, [&controller](const auto& result) { controller.setWalletCreateError(result.error); }, [](auto) {}));
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletCreateError(), original);
    QCOMPARE(controller.walletCreateErrorDisplay(), original.left(1024));
    QVERIFY(controller.walletCreateHasError());
    auto* details = controller.walletCreateErrorHelpModel();
    QString complete;
    for (int i = 0; i < details->rowCount(); ++i) {
        const auto chunk = details->data(details->index(i, 0), Qt::UserRole).toString();
        QVERIFY(chunk.size() <= 1024); complete += chunk;
    }
    QCOMPARE(complete, original);
    QVERIFY(controller.m_command->result()->value->effect_succeeded);
    bool successor{false};
    std::atomic<int> rejected_effects{0};
    const auto reentrant = connect(&controller, &WalletQmlController::walletCreateErrorChanged, &controller, [&] {
        if (std::exchange(successor, true)) return;
        // R11's original137/284 contract forbids a new Core command from a
        // partially published terminal result. Presentation can reenter, but
        // cannot replace the owned command or its remaining error projection.
        QVERIFY(!controller.submitWalletCommand([&] {
            ++rejected_effects;
            return WalletQmlController::CommandResult{};
        }, [](const auto&) {}, [](auto) {}));
    });
    QVERIFY(controller.submitWalletCommand([] {
        WalletQmlController::CommandResult result; result.error = QStringLiteral("Superseded failure"); return result;
    }, [&controller](const auto& result) {
        controller.setWalletCreateError(result.error);
        controller.setWalletLoadError(result.error);
    }, [](auto) {}));
    QTRY_VERIFY(successor);
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(rejected_effects.load(), 0);
    QCOMPARE(controller.walletCreateError(), QStringLiteral("Superseded failure"));
    QCOMPARE(controller.walletLoadError(), QStringLiteral("Superseded failure"));
    disconnect(reentrant);
    // A fresh explicit action is allowed after the entire original result is
    // published. Clearing its scoped load status is the normal load admission
    // behavior, rather than admitting a replacement inside the old callback.
    controller.clearWalletLoadStatus();
    QVERIFY(controller.submitWalletCommand([] {
        WalletQmlController::CommandResult result; result.error = QStringLiteral("Successor failure"); return result;
    }, [&controller](const auto& result) { controller.setWalletCreateError(result.error); }, [](auto) {}));
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletCreateError(), QStringLiteral("Successor failure"));
    QVERIFY(!controller.walletLoadHasError());
    controller.closeAdmission(); QCOMPARE(details->rowCount(), 0);
}

void WalletQmlControllerTests::migrationErrorReceiptPreparesBoundedMessageAndFrozenFullDetails()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.wallet_dir_entries = {{"legacy-wallet", "bdb"}};
    const QString original = QStringLiteral("Incorrect PaSsPhRaSe: ") + QString(5000, QLatin1Char('p'));
    const auto gui = QThread::currentThread();
    std::atomic_bool native{false};
    loader.migrate_wallet_fn = [&](const auto&, const auto&, bool) -> util::Result<interfaces::WalletMigrationResult> {
        native = QThread::currentThread() != gui;
        return util::Error{Untranslated(original.toStdString())};
    };
    DrainingWalletController controller{node};
    controller.initialize(); QTRY_VERIFY(controller.initialized());
    QSignalSpy notices{&controller, &WalletQmlController::walletMigrationErrorNotice};
    controller.migrateWallet("legacy-wallet", "password");
    QVERIFY(AwaitWalletOperation(controller));
    QVERIFY(native.load()); QCOMPARE(notices.count(), 1);
    QCOMPARE(notices.front().at(0).toString(), QStringLiteral("legacy-wallet"));
    QVERIFY(!notices.front().at(2).toBool());
    QCOMPARE(notices.front().at(3).toString(), original.left(1024));
    QCOMPARE(controller.walletMigrationError(), original);
    const auto ticket = notices.front().at(1).toULongLong();
    auto* details = controller.captureMigrationErrorDetails(ticket, 1);
    QVERIFY(details); QVERIFY(details->rowCount() > 1);
    controller.setWalletMigrationError(QStringLiteral("Another bounded diagnostic"));
    QString complete;
    for (int i = 0; i < details->rowCount(); ++i) {
        const auto chunk = details->data(details->index(i, 0), Qt::UserRole).toString();
        QVERIFY(chunk.size() <= 1024); complete += chunk;
    }
    QCOMPARE(complete, original); // Full original document is frozen independently of the latest error.
    controller.closeAdmission(); QCOMPARE(details->rowCount(), 0);
}

void WalletQmlControllerTests::manualShutdownFencesInventoryBeforeBoundedRetirement()
{
    StrictMockNode node; [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [] { std::vector<std::unique_ptr<interfaces::Wallet>> result; result.reserve(33); for (int i = 0; i < 33; ++i) result.push_back(std::make_unique<FakeWallet>("batch-" + util::ToString(i), nullptr)); return result; };
    std::deque<std::function<void()>> turns;
    auto controller = std::make_unique<WalletQmlController>(node);
    controller->setRetirementPostForTesting([&turns](auto callback) { turns.push_back(std::move(callback)); });
    const std::weak_ptr<execution::Scheduler> original_scheduler = controller->backgroundScheduler();
    const auto cleanup = qScopeGuard([&] { DrainControlledController(controller, turns, original_scheduler); });
    controller->initialize(); QTRY_VERIFY(controller->initialized());
    // 33 wallets span two 32-object GUI publication batches; the second batch can
    // exceed the default five-second budget under full-suite load.
    QTRY_COMPARE_WITH_TIMEOUT(controller->openWallets().size(), 33, 15000);
    const auto wallets = controller->openWallets();
    std::vector<std::unique_ptr<WalletProvisioningCheck>> forms;forms.reserve(65);
    for (int i = 0; i < 65; ++i) forms.emplace_back(controller->createProvisioningCheck());
    for (const auto& form : forms) QVERIFY(form);
    struct Probe { QSemaphore entered, release; std::atomic<int> effects{0}, outcomes{0}; std::atomic_bool on_gui{false}; };
    const auto probe = std::make_shared<Probe>();
    const auto unblock = qScopeGuard([probe] { probe->release.release(); });
    auto scheduler = controller->backgroundScheduler();
    QVERIFY(execution::Submit(*scheduler, [probe] { probe->entered.release(); probe->release.acquire(); }, [] {}, {}, {execution::Lane::Core, {}, execution::WorkClass::Action}));
    QTRY_VERIFY(probe->entered.available() > 0);
    auto* original = qobject_cast<WalletQmlModel*>(wallets.first()); QVERIFY(original);
    QVERIFY(original->opening()->read([probe](interfaces::Wallet&) { if (QThread::currentThread() == qApp->thread()) probe->on_gui = true; ++probe->effects; return true; }, [probe](bool) { ++probe->outcomes; }, {}, execution::WorkClass::Action));
    QSignalSpy closed(controller.get(), &WalletQmlController::walletLoadStateChanged);
    QSignalSpy drained(controller.get(), &WalletQmlController::walletsDrained);
    controller->closeAdmission();
    for (auto* object : wallets) { auto* wallet = qobject_cast<WalletQmlModel*>(object); QVERIFY(!wallet->available()); QVERIFY(!wallet->opening()->startAction()); }
    for (const auto& form : forms) { QVERIFY(!form->ready()); const auto before = form->name(); form->setName("late change"); QCOMPARE(form->name(), before); }
    const auto late = loader.captured_load_wallet_fn; late(std::make_unique<FakeWallet>("late-wallet", nullptr));
    controller->beginShutdown(); QCOMPARE(closed.size(), 0); QCOMPARE(probe->effects.load(), 0); QVERIFY(drained.empty());
    int batches{0};
    while (!turns.empty()) {
        auto next = std::move(turns.front()); turns.pop_front(); const int before = closed.size(); next(); ++batches;
        QVERIFY(closed.size() - before <= 32);
    }
    QCOMPARE(batches, 4); //65 forms and33 wallets share one32-object GUI budget.
    QCOMPARE(closed.size(), 33); QVERIFY(drained.empty()); QCOMPARE(probe->effects.load(), 0);
    bool heartbeat{false}; QTimer::singleShot(0, [&] { heartbeat = true; }); QTRY_VERIFY(heartbeat);
    probe->release.release();
    QVERIFY(PumpRetirementTurns(turns, [&] { return drained.size() == 1; }));
    QCOMPARE(scheduler->pending(), size_t{0}); QCOMPARE(probe->effects.load(), 1); QCOMPARE(probe->outcomes.load(), 1); QVERIFY(!probe->on_gui);
    QCOMPARE(controller->openWallets().size(), 0); QVERIFY(!controller->walletForName("late-wallet"));
}
void WalletQmlControllerTests::manualShutdownSurvivesOwnerDestructionDuringFirstBatch()
{
    StrictMockNode node; [[maybe_unused]] auto verify_node = node.VerifyOnExit(); FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    std::atomic<int> destroyed{0}; std::atomic_bool on_gui{false};
    struct ObservedWallet : FakeWallet {
        std::atomic<int>& destroyed; std::atomic_bool& on_gui;
        ObservedWallet(std::string name, std::atomic<int>& count, std::atomic_bool& gui) : FakeWallet(std::move(name), nullptr), destroyed(count), on_gui(gui) {}
        ~ObservedWallet() override { if (QThread::currentThread() == qApp->thread()) on_gui = true; ++destroyed; }
    };
    loader.get_wallets_fn = [&] { std::vector<std::unique_ptr<interfaces::Wallet>> result; result.reserve(33); for (int i = 0; i < 33; ++i) result.push_back(std::make_unique<ObservedWallet>("original-" + util::ToString(i), destroyed, on_gui)); return result; };
    std::deque<std::function<void()>> turns; auto controller = std::make_unique<WalletQmlController>(node);
    controller->setRetirementPostForTesting([&turns](auto callback) { turns.push_back(std::move(callback)); });
    const std::weak_ptr<execution::Scheduler> original_scheduler = controller->backgroundScheduler();
    const auto cleanup = qScopeGuard([&] { DrainControlledController(controller, turns, original_scheduler); });
    controller->initialize(); QTRY_VERIFY(controller->initialized());
    // 33 wallets span two 32-object GUI publication batches; the second batch can
    // exceed the default five-second budget under full-suite load.
    QTRY_COMPARE_WITH_TIMEOUT(controller->openWallets().size(), 33, 15000);
    QObject receiver; bool interrupted{false};
    connect(controller.get(), &WalletQmlController::walletLoadStateChanged, &receiver, [&](const QString&, WalletListModel::LoadState state, const QString&) { if (!interrupted && state == WalletListModel::LoadState::Closed) { interrupted = true; controller.reset(); } });
    controller->beginShutdown(); QVERIFY(!turns.empty());
    auto first = std::exchange(turns.front(), {});
    turns.pop_front();
    first();
    first = {}; // The consumed callback must not retain the original Retirement owner.
    QVERIFY(interrupted);
    QVERIFY(!controller);
    QVERIFY(!turns.empty()); while (!turns.empty()) { auto next = std::move(turns.front()); turns.pop_front(); next(); }
    QObject native_receiver;
    bool native_done{false};
    BackendExecutor::shutdownAll(&native_receiver, [&] { native_done = true; });
    QVERIFY(PumpRetirementTurns(turns, [&] { return native_done && turns.empty() && original_scheduler.expired(); }));
    QCOMPARE(destroyed.load(), 33);
    QVERIFY(!on_gui);
}
void WalletQmlControllerTests::manualReviewRetirementGatesFinalNativeDrain()
{
    StrictMockNode node; [[maybe_unused]] auto verify_node = node.VerifyOnExit(); FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [] { std::vector<std::unique_ptr<interfaces::Wallet>> result; result.push_back(std::make_unique<FakeWallet>("reviews", nullptr)); return result; };
    std::deque<std::function<void()>> review_turns;
    auto controller = std::make_unique<WalletQmlController>(node);
    const auto cleanup = qScopeGuard([&] { if (controller) controller->beginShutdown(); const bool presentation_drained = QTest::qWaitFor([&] { while (!review_turns.empty()) { auto next = std::move(review_turns.front()); review_turns.pop_front(); next(); } QCoreApplication::processEvents(); return review_turns.empty(); }, 5000); if (!presentation_drained) QTest::qFail("Review retirement presentation did not drain", __FILE__, __LINE__); controller.reset(); QObject receiver; bool done{false}; BackendExecutor::shutdownAll(&receiver, [&] { done = true; }); QTRY_VERIFY(done); QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete); });
    controller->initialize(); QTRY_VERIFY(controller->initialized()); QTRY_COMPARE(controller->openWallets().size(), 1);
    auto* wallet = controller->walletForName("reviews"); QVERIFY(wallet);
    wallet->setReviewRetirementPostForTesting([&review_turns](auto callback) { review_turns.push_back(std::move(callback)); });
    std::vector<QPointer<WalletQmlModelTransaction>> reviews;
    for (int i = 0; i < 65; ++i) { walletreview::Snapshot value; value.amount = 1000; reviews.emplace_back(new WalletQmlModelTransaction(value, wallet->opening(), [](auto callback) { QTimer::singleShot(0, QCoreApplication::instance(), std::move(callback)); }, wallet)); }
    QSignalSpy drained(controller.get(), &WalletQmlController::walletsDrained);
    controller->beginShutdown(); QTRY_VERIFY(!review_turns.empty()); QVERIFY(drained.empty());
    int batches{0};
    while (!review_turns.empty()) { const auto before = std::count_if(reviews.begin(), reviews.end(), [](const auto& value) { return value.isNull(); }); auto next = std::move(review_turns.front()); review_turns.pop_front(); next(); ++batches; const auto after = std::count_if(reviews.begin(), reviews.end(), [](const auto& value) { return value.isNull(); }); QVERIFY(after - before <= 32); }
    QCOMPARE(batches, 3); for (const auto& review : reviews) QVERIFY(review.isNull()); QTRY_COMPARE(drained.size(), 1);
}

void WalletQmlControllerTests::shutdownDrainsWalletQueueAndPlaceholder()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QSemaphore entered, release;
    const auto empty_executor = controller.selectedWallet()->backendExecutor();
    const auto gui_thread = QThread::currentThread();
    std::atomic_bool off_gui{false};
    empty_executor->submit(&controller, [&] {
        off_gui = QThread::currentThread() != gui_thread;
        entered.release();
        release.acquire();
    }, [] {});
    struct Release { QSemaphore& semaphore; ~Release() { semaphore.release(); } } release_on_exit{release};
    QTRY_VERIFY(entered.available() > 0);
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    bool heartbeat{false};
    QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(off_gui.load());
    QVERIFY(drained.empty());
    release.release();
    QTRY_COMPARE(drained.size(), 1);
    QVERIFY(empty_executor->isDrained());
}

void WalletQmlControllerTests::reentrantShutdownRetiresTheNewWalletModel()
{
    FakeWallet::State state; StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
    struct Probe { std::atomic_bool destroyed{false}, off_gui{false}; };
    auto probe = std::make_shared<Probe>();
    struct RetiringWallet : FakeWallet {
        std::shared_ptr<Probe> probe; QThread* gui;
        RetiringWallet(State* state, std::shared_ptr<Probe> value, QThread* thread)
            : FakeWallet("reentrant-wallet", state), probe(std::move(value)), gui(thread) {}
        ~RetiringWallet() override { probe->off_gui = QThread::currentThread() != gui; probe->destroyed = true; }
    };
    const auto gui = QThread::currentThread();
    loader.create_wallet_fn = [&](const auto&, const auto&, uint64_t, auto&) {
        return util::Result<std::unique_ptr<interfaces::Wallet>>{std::make_unique<RetiringWallet>(&state, probe, gui)};
    };
    DrainingWalletController controller{node}; controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    connect(&controller, &WalletQmlController::walletLoadInProgressChanged, &controller, [&] {
        if (!controller.walletLoadInProgress()) controller.beginShutdown();
    });
    controller.createSingleSigWallet("reentrant-wallet", "");
    QTRY_COMPARE(drained.count(), 1);
    QVERIFY(probe->destroyed.load()); QVERIFY(probe->off_gui.load());
    QVERIFY(!controller.isWalletOpen("reentrant-wallet")); QVERIFY(!controller.isWalletLoaded());
}

void WalletQmlControllerTests::shutdownRetiresQueuedLoadNotificationOffGui()
{
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    struct Probe { std::atomic_bool destroyed{false}, off_gui{false}; };
    const auto probe = std::make_shared<Probe>();
    struct RetiringWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        QThread* gui;
        RetiringWallet(State* state, std::shared_ptr<Probe> result)
            : FakeWallet("late-wallet", state), probe(std::move(result)), gui(QThread::currentThread()) {}
        ~RetiringWallet() override { probe->off_gui = QThread::currentThread() != gui; probe->destroyed = true; }
    };
    QVERIFY(loader.captured_load_wallet_fn);
    loader.captured_load_wallet_fn(std::make_unique<RetiringWallet>(&state, probe));
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    QTRY_COMPARE(drained.size(), 1);
    QVERIFY(probe->destroyed.load());
    QVERIFY(probe->off_gui.load());
    QVERIFY(!controller.isWalletOpen("late-wallet"));
}

void WalletQmlControllerTests::shutdownReleasesNotificationsBeforeMigrationRollback()
{
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    struct MigrationRollbackProbe {
        QSemaphore notification_queued, rollback_can_finish;
        std::atomic_bool destroyed{false}, off_gui{false}, rollback_finished{false};
    };
    const auto probe = std::make_shared<MigrationRollbackProbe>();
    const auto gui_thread = QThread::currentThread();
    struct RollbackBlockingWallet : FakeWallet {
        std::shared_ptr<MigrationRollbackProbe> probe;
        QThread* gui_thread;
        RollbackBlockingWallet(State* state, std::shared_ptr<MigrationRollbackProbe> probe, QThread* gui_thread)
            : FakeWallet("legacy_wallet_watchonly", state), probe(std::move(probe)), gui_thread(gui_thread) {}
        ~RollbackBlockingWallet() override {
            probe->off_gui = QThread::currentThread() != gui_thread;
            probe->destroyed = true;
            probe->rollback_can_finish.release();
        }
    };
    loader.migrate_wallet_fn = [&, probe, gui_thread](const std::string&, const SecureString&, bool) -> util::Result<interfaces::WalletMigrationResult> {
        loader.captured_load_wallet_fn(std::make_unique<RollbackBlockingWallet>(&state, probe, gui_thread));
        probe->notification_queued.release();
        probe->rollback_can_finish.acquire();
        probe->rollback_finished = true;
        return util::Error{Untranslated("Later migrated wallet failed to load")};
    };
    const auto release_on_failure = qScopeGuard([probe] { probe->rollback_can_finish.release(); });
    controller.migrateWallet("legacy_wallet", "");
    const bool notification_queued_without_gui_dispatch{probe->notification_queued.tryAcquire(1, 5000)};
    QVERIFY(notification_queued_without_gui_dispatch);
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 5000);
    QVERIFY(probe->destroyed.load());
    QVERIFY(probe->off_gui.load());
    QVERIFY(probe->rollback_finished.load());
    QVERIFY(!controller.isWalletOpen("legacy_wallet_watchonly"));
}

void WalletQmlControllerTests::shutdownWaitsForNotificationRetirement()
{
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    struct Probe { QSemaphore entered, release; std::atomic_bool destroyed{false}; };
    const auto probe = std::make_shared<Probe>();
    struct NotificationWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        NotificationWallet(State* state, std::shared_ptr<Probe> probe)
            : FakeWallet("late-wallet", state), probe(std::move(probe)) {}
        ~NotificationWallet() override {
            probe->entered.release();
            probe->release.acquire();
            probe->destroyed = true;
        }
    };
    const auto release_on_failure = qScopeGuard([probe] { probe->release.release(); });
    loader.captured_load_wallet_fn(std::make_unique<NotificationWallet>(&state, probe));
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    QTRY_VERIFY(probe->entered.available() > 0);
    bool heartbeat{false};
    QTimer::singleShot(50, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(drained.empty());
    QVERIFY(!probe->destroyed.load());
    probe->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 5000);
    QVERIFY(probe->destroyed.load());
}

void WalletQmlControllerTests::walletCommandCompletionDuringShutdown_data()
{
    QTest::addColumn<QString>("operation");
    QTest::addColumn<bool>("throws");
    for (const auto* operation : {"create", "load", "import", "migrate"}) {
        for (bool throws : {false, true}) {
            const QByteArray tag = QByteArray{operation} + (throws ? "-exception" : "-success");
            QTest::newRow(tag.constData()) << QString::fromLatin1(operation) << throws;
        }
    }
}

void WalletQmlControllerTests::walletCommandCompletionDuringShutdown()
{
    QFETCH(QString, operation);
    QFETCH(bool, throws);
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    if (operation == "migrate") loader.wallet_dir_entries = {{"late-wallet", "bdb"}};
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QFile backup(directory.filePath("late-wallet.bak"));
    QVERIFY(backup.open(QIODevice::WriteOnly));
    QVERIFY(backup.write(QByteArray{"SQLite format 3\0", 16} + QByteArray(496, '\0')) == 512); backup.close();
    struct Probe {
        QSemaphore entered, release;
        std::atomic_bool destroyed{false}, off_gui{false};
    };
    auto probe = std::make_shared<Probe>();
    struct ResultWallet : FakeWallet {
        std::shared_ptr<Probe> probe;
        QThread* gui;
        ResultWallet(State* state, std::shared_ptr<Probe> probe, QThread* gui)
            : FakeWallet("late-wallet", state), probe(std::move(probe)), gui(gui) {}
        ~ResultWallet() override {
            probe->off_gui = QThread::currentThread() != gui;
            probe->destroyed = true;
        }
    };
    const auto gui_thread = QThread::currentThread();
    const auto complete = [&]() -> std::unique_ptr<interfaces::Wallet> {
        probe->entered.release();
        probe->release.acquire();
        if (throws) throw std::runtime_error{"wallet command failed"};
        return std::make_unique<ResultWallet>(&state, probe, gui_thread);
    };
    loader.create_wallet_fn = [&](const auto&, const auto&, uint64_t, auto&) { return complete(); };
    loader.load_wallet_fn = [&](const auto&, auto&) { return complete(); };
    loader.restore_wallet_fn = [&] { return complete(); };
    loader.migrate_wallet_fn = [&](const auto&, const auto&, bool) {
        interfaces::WalletMigrationResult result;
        result.wallet = complete();
        return result;
    };
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    const auto release_on_failure = qScopeGuard([probe] { probe->release.release(); });
    QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    QSignalSpy imported{&controller, &WalletQmlController::walletImportSucceeded};
    QSignalSpy migrated{&controller, &WalletQmlController::walletMigrationSucceeded};
    QSignalSpy migration_failed{&controller, &WalletQmlController::walletMigrationFailed};
    if (operation == "create") controller.createSingleSigWallet("late-wallet", "");
    else if (operation == "load") controller.setSelectedWallet("late-wallet", "sqlite");
    else if (operation == "import") controller.importWallet(backup.fileName());
    else controller.migrateWallet("late-wallet", "");
    QTRY_VERIFY_WITH_TIMEOUT(probe->entered.available() > 0, 5000);
    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    controller.beginShutdown();
    controller.beginShutdown();
    bool heartbeat{false};
    QTimer::singleShot(0, &controller, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(drained.empty());
    probe->release.release();
    QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 5000);
    QCOMPARE(probe->destroyed.load(), !throws);
    QCOMPARE(probe->off_gui.load(), !throws);
    QVERIFY(!controller.isWalletOpen("late-wallet"));
    QCOMPARE(created.size() + loaded.size() + imported.size() + migrated.size() + migration_failed.size(), 0);
    QVERIFY(controller.walletCreateError().isEmpty());
    QVERIFY(controller.walletLoadError().isEmpty());
    QVERIFY(controller.walletMigrationError().isEmpty());
}

void WalletQmlControllerTests::injectedControllerServicesRejectPartialGraphBeforeAdmission()
{
    MockNode node;
    ManualRoutedExecution native;
    WalletQmlController::Services services;
    services.scheduler = native.scheduler;
    const auto rejects_partial = [&](WalletQmlController::Services partial) {
        try {
            WalletQmlController rejected{node, std::move(partial)};
        } catch (const std::invalid_argument&) {
            return true;
        } catch (...) {
            return false;
        }
        return false;
    };
    QVERIFY(rejects_partial(services));
    services.shutdown = [](auto) {};
    QVERIFY(rejects_partial(services));
    services.scheduler.reset();
    services.post = [](auto) {};
    QVERIFY(rejects_partial(services));
    QCOMPARE(node.calls.walletLoader.load(), 0);
    QCOMPARE(native.scheduler->pending(), size_t{0});
    QVERIFY(native.queuedIdle());
}

void WalletQmlControllerTests::manualMatchingLoadNotificationAndResult_data()
{
    QTest::addColumn<bool>("notification_first");
    QTest::addColumn<int>("later_selection");
    for (bool first : {false, true}) {
        for (int selection = 0; selection < 5; ++selection) {
            const QByteArray tag = QByteArray(first ? "notification-first-" : "result-first-") + QByteArray::number(selection);
            QTest::newRow(tag.constData()) << first << selection;
        }
    }
}

void WalletQmlControllerTests::manualMatchingLoadNotificationAndResult()
{
    QFETCH(bool, notification_first);
    QFETCH(int, later_selection); // none, accepted original, null, foreign, closing.
    FakeWallet::State original_state, standby_state, loaded_state, foreign_state;
    MockNode node, foreign_node;
    FakeWalletLoader loader, foreign_loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ConfigureExpectedControllerInitialization(foreign_node, foreign_loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &original_state));
        wallets.push_back(std::make_unique<FakeWallet>("standby", &standby_state));
        return wallets;
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    QVERIFY(!controller.m_wallet_command_executor); // No real-thread controller fallback.
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("original") && controller.walletForName("original")->capabilitiesKnown(); }));
    auto* original = controller.walletForName("original");
    QCOMPARE(controller.selectedWallet(), original);
    std::unique_ptr<ManualControllerGraph> foreign;
    if (later_selection == 3) {
        foreign_loader.get_wallets_fn = [&] { std::vector<std::unique_ptr<interfaces::Wallet>> wallets; wallets.push_back(std::make_unique<FakeWallet>("foreign", &foreign_state)); return wallets; };
        foreign = std::make_unique<ManualControllerGraph>(foreign_node);
        foreign->controller->initialize();
        QVERIFY(foreign->until([&] { return foreign->controller->initialized(); }));
    }
    QPointer<WalletQmlModel> closing;
    std::shared_ptr<interfaces::Wallet> held;
    if (later_selection == 4) {
        closing = controller.walletForName("standby"); QVERIFY(closing);
        auto borrow = closing->opening()->borrow(); QVERIFY(borrow);
        held = std::move(borrow);
        controller.closeWallet("standby");
        QVERIFY(graph.until([&] { return !controller.isWalletOpen("standby"); }));
        QVERIFY(closing); QVERIFY(!closing->available());
    }
    struct Handles { std::atomic<int> destroyed{0}; std::atomic_bool native{true}; };
    const auto handles = std::make_shared<Handles>();
    struct LoadedHandle final : FakeWallet {
        std::shared_ptr<Handles> handles; std::atomic<int>* lane; QThread* gui;
        LoadedHandle(State* state, std::shared_ptr<Handles> value, std::atomic<int>* execution_lane)
            : FakeWallet("target", state), handles(std::move(value)), lane(execution_lane), gui(QThread::currentThread()) {}
        ~LoadedHandle() override {
            const bool native = QThread::currentThread() != gui || lane->load() == int(execution::Lane::Core) || lane->load() == int(execution::Lane::Cleanup);
            if (!native) handles->native = false;
            ++handles->destroyed;
        }
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loader.captured_load_wallet_fn(std::make_unique<LoadedHandle>(&loaded_state, handles, &graph.lane));
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<LoadedHandle>(&loaded_state, handles, &graph.lane)};
    };
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    QSignalSpy inserted{controller.openWalletModel(), &QAbstractItemModel::rowsInserted};
    controller.setSelectedWallet("target", "sqlite");
    const auto ticket = controller.walletOperationId();
    auto command = controller.m_command; QVERIFY(command);
    const auto selection = controller.m_selection->revision();
    if (later_selection == 1) QVERIFY(controller.selectWallet(original));
    if (later_selection == 2) QVERIFY(!controller.selectWallet(nullptr));
    if (later_selection == 3) QVERIFY(!controller.selectWallet(foreign->controller->selectedWallet()));
    if (later_selection == 4) QVERIFY(!controller.selectWallet(closing));
    QCOMPARE(controller.m_selection->revision(), selection + (later_selection == 1 ? 1 : 0));
    QCOMPARE(controller.walletOperationId(), ticket);
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(graph.step(execution::Lane::Core));
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(!command->result());
    QVERIFY(graph.step(execution::Lane::Domain));
    QVERIFY(command->result()); // Original result is owned, but GUI has not consumed it.
    QCOMPARE(graph.posts.size(), size_t{1});
    auto command_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto notification_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    QVERIFY(controller.walletLoadInProgress());
    if (notification_first) notification_notice(); else command_notice();
    QCOMPARE(loaded.size(), notification_first ? 0 : 1);
    QCOMPARE(controller.walletLoadInProgress(), notification_first);
    auto* first = controller.walletForName("target"); QVERIFY(first);
    const auto identity = first->opening()->identity();
    if (notification_first) command_notice(); else notification_notice();
    QVERIFY(graph.until([&] { return first->capabilitiesKnown(); }));
    QCOMPARE(controller.walletForName("target"), first);
    QVERIFY(first->opening()->identity() == identity);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.front().front().value<WalletQmlModel*>(), first);
    QCOMPARE(controller.walletOperationId(), ticket);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationName(), QStringLiteral("target"));
    // The native result is retained, but only a later explicit action selects it.
    QCOMPARE(controller.selectedWallet(), original);
    QCOMPARE(controller.walletOperationResultWallet(), first);
    QVERIFY(controller.canOpenResult());
    QVERIFY(!controller.m_selection->pending());
    if (later_selection != 1) QVERIFY(controller.selectWallet(controller.walletOperationResultWallet()));
    QCOMPARE(controller.selectedWallet(), later_selection == 1 ? original : first);
    command_notice(); notification_notice(); // Reliable transport retries cannot replay either publication.
    QCOMPARE(loaded.size(), 1); QCOMPARE(inserted.size(), 1);
    QCOMPARE(controller.selectedWallet(), later_selection == 1 ? original : first);
    QCOMPARE(loader.load_wallet_calls, 1);
    held.reset(); command.reset();
    graph.shutdown();
    QCOMPARE(handles->destroyed.load(), 2);
    QVERIFY(handles->native.load());
    QCOMPARE(loaded_state.private_accesses.load(), 0);
}

void WalletQmlControllerTests::manualExternalLoadAndAlreadyLoadedResult_data()
{
    QTest::addColumn<bool>("notification_first");
    QTest::addColumn<bool>("select_b");
    QTest::addColumn<bool>("before_admission");
    QTest::newRow("notification-first-original-selection") << true << false << false;
    QTest::newRow("result-first-original-selection") << false << false << false;
    QTest::newRow("notification-first-later-selection-b") << true << true << false;
    QTest::newRow("result-first-later-selection-b") << false << true << false;
    QTest::newRow("pre-admit-notification-first-original-selection") << true << false << true;
    QTest::newRow("pre-admit-result-first-original-selection") << false << false << true;
    QTest::newRow("pre-admit-notification-first-later-selection-b") << true << true << true;
    QTest::newRow("pre-admit-result-first-later-selection-b") << false << true << true;
}

void WalletQmlControllerTests::manualExternalLoadAndAlreadyLoadedResult()
{
    QFETCH(bool, notification_first);
    QFETCH(bool, select_b);
    QFETCH(bool, before_admission);
    FakeWallet::State original_state, standby_state, target_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &original_state));
        wallets.push_back(std::make_unique<FakeWallet>("standby", &standby_state));
        return wallets;
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("original") && controller.walletForName("standby"); }));
    auto* original = controller.walletForName("original");
    auto* standby = controller.walletForName("standby");
    QVERIFY(original); QVERIFY(standby); QCOMPARE(controller.selectedWallet(), original);
    loader.load_wallet_fn = [&](const std::string& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (name != "target") return util::Error{Untranslated("Unexpected wallet target")};
        // Another producer loaded this exact wallet. Its notification and the
        // failed GUI loader call are independent facts, both owned below.
        if (!before_admission) loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("target", &target_state));
        return util::Error{Untranslated("Wallet already loaded")};
    };
    QSignalSpy errors{&controller, &WalletQmlController::walletLoadErrorNotice};
    QSignalSpy inserted{controller.openWalletModel(), &QAbstractItemModel::rowsInserted};
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    std::function<void()> notification_notice;
    if (before_admission) {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("target", &target_state));
        graph.runNative();
        QCOMPARE(graph.posts.size(), size_t{1});
        notification_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    }
    controller.setSelectedWallet("target", "sqlite");
    const auto operation = controller.walletOperationId();
    auto command = controller.m_command; QVERIFY(command);
    if (select_b) QVERIFY(controller.selectWallet(standby));
    QVERIFY(graph.step(execution::Lane::Core));
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(graph.step(execution::Lane::Domain));
    QVERIFY(command->result());
    QCOMPARE(graph.posts.size(), size_t{1});
    auto command_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    if (!before_admission) {
        graph.runNative();
        QCOMPARE(graph.posts.size(), size_t{1});
        notification_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    }
    WalletQmlModel* notified{nullptr};
    if (notification_first) {
        notification_notice(); notified = controller.walletForName("target"); command_notice();
    } else {
        command_notice(); notification_notice(); notified = controller.walletForName("target");
    }
    // Both GUI receipts are consumed before asserting their combined outcome.
    QVERIFY(notified);
    const auto opening = notified->opening()->identity();
    // Only GUI continuations run here; the Domain decision remains held.
    for (int budget = 0; budget < 16 && !graph.posts.empty(); ++budget) {
        auto next = std::move(graph.posts.front()); graph.posts.pop_front(); next();
    }
    QVERIFY(controller.walletLoadInProgress()); QCOMPARE(loaded.size(), 0);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
    QVERIFY(graph.step(execution::Lane::Domain));
    QVERIFY(controller.walletLoadInProgress()); QCOMPARE(loaded.size(), 0);
    QVERIFY(graph.step(execution::Lane::Domain));
    QVERIFY(controller.walletLoadInProgress()); QCOMPARE(loaded.size(), 0);
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress() && notified->capabilitiesKnown(); }));
    const auto actual = command->result();
    QVERIFY(actual && actual->value); QVERIFY(!actual->error);
    QCOMPARE(actual->value->error, QStringLiteral("Wallet already loaded"));
    QVERIFY(!actual->value->has_wallet); QVERIFY(!actual->value->walletForAdoption()); QVERIFY(!actual->value->effect_succeeded);
    QCOMPARE(controller.walletOperationId(), operation);
    QCOMPARE(controller.walletForName("target"), notified);
    QVERIFY(notified->opening()->identity() == opening);
    QCOMPARE(controller.openWalletModel()->rowCount(), 3);
    QCOMPARE(inserted.size(), 1);
    QCOMPARE(controller.selectedWallet(), select_b ? standby : original);
    QCOMPARE(loader.load_wallet_calls, 1);
    QCOMPARE(target_state.private_accesses.load(), 0);
    // Showing a load failure while the same target is independently available
    // reproduces scope049, even though the actual Core load error is retained.
    QCOMPARE(errors.size(), 0);
    QVERIFY(controller.walletLoadError().isEmpty());
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionAvailable);
    QCOMPARE(controller.walletOperationResultWallet(), notified); QVERIFY(controller.canOpenResult());
    QCOMPARE(loaded.size(), 1); QCOMPARE(loaded.front().front().value<WalletQmlModel*>(), notified);
    QCOMPARE(controller.walletLoadWarnings(), QStringLiteral("Wallet already loaded"));
    { std::lock_guard lock{controller.m_command_receipt->mutex};
      QCOMPARE(controller.m_command_receipt->state, WalletQmlController::WalletOperationState::WalletOperationFailed); }
    QCOMPARE(loader.get_wallets_calls, 1); QCOMPARE(loader.handle_load_wallet_calls, 1);
    command_notice(); notification_notice();
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress(); }));
    QCOMPARE(loaded.size(), 1); QCOMPARE(errors.size(), 0); QCOMPARE(inserted.size(), 1);
    QCOMPARE(controller.selectedWallet(), select_b ? standby : original);
}

void WalletQmlControllerTests::manualFailedOpenRequiresCapturedOriginalOpening_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("unrelated-notification") << 0;
    QTest::newRow("notification-after-native-result") << 1;
    QTest::newRow("captured-opening-closed") << 2;
    QTest::newRow("resolution-admission-rejected") << 3;
}

void WalletQmlControllerTests::manualFailedOpenRequiresCapturedOriginalOpening()
{
    QFETCH(int, scenario);
    FakeWallet::State original_state, target_state;
    MockNode node; FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &original_state));
        return wallets;
    };
    ManualControllerGraph graph{node}; auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("original"); }));
    auto* original = controller.walletForName("original");
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (scenario != 1) loader.captured_load_wallet_fn(std::make_unique<FakeWallet>(scenario == 0 ? "unrelated" : "target", &target_state));
        return util::Error{Untranslated("Wallet already loaded")};
    };
    QSignalSpy errors{&controller, &WalletQmlController::walletLoadErrorNotice};
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet("target", "sqlite");
    const auto operation = controller.walletOperationId();
    auto command = controller.m_command; QVERIFY(command);
    QVERIFY(graph.step(execution::Lane::Core)); QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto command_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    if (scenario == 1) loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("target", &target_state));
    graph.runNative(); QCOMPARE(graph.posts.size(), size_t{1});
    auto notification_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    notification_notice();
    if (scenario == 2) {
        QVERIFY(controller.walletForName("target"));
        controller.closeWallet("target");
        QVERIFY(graph.until([&] { return !controller.isWalletOpen("target"); }));
    }
    if (scenario == 3) {
        graph.runNative(); // Reject the resolution's initial admission, not an accepted handoff.
        graph.native.rejectLane(execution::Lane::Domain);
    }
    command_notice();
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress(); }));
    const auto actual = command->result(); QVERIFY(actual && actual->value);
    QCOMPARE(actual->value->error, QStringLiteral("Wallet already loaded"));
    QVERIFY(!actual->value->has_wallet); QVERIFY(!actual->value->effect_succeeded);
    QCOMPARE(controller.walletOperationId(), operation);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationFailed);
    QCOMPARE(controller.walletOperationResultWallet(), nullptr); QVERIFY(!controller.canOpenResult());
    QCOMPARE(loaded.size(), 0); QCOMPARE(errors.size(), 1);
    QCOMPARE(controller.selectedWallet(), original);
    if (scenario == 3) QVERIFY(controller.walletLoadError().contains(QStringLiteral("could not be reconciled")));
    else QCOMPARE(controller.walletLoadError(), QStringLiteral("Wallet already loaded"));
    QCOMPARE(loader.load_wallet_calls, 1); QCOMPARE(loader.get_wallets_calls, 1); QCOMPARE(loader.handle_load_wallet_calls, 1);
    command_notice(); notification_notice();
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress(); }));
    QCOMPARE(loaded.size(), 0); QCOMPARE(errors.size(), 1); QCOMPARE(controller.selectedWallet(), original);
}

void WalletQmlControllerTests::manualFailedOpenAfterObservedUnloadBindsOnlyFreshOpening_data()
{
    QTest::addColumn<bool>("unload_gui_first");
    QTest::newRow("old-unload-gui-before-fresh-notification") << true;
    QTest::newRow("fresh-notification-before-old-unload-gui") << false;
}

void WalletQmlControllerTests::manualFailedOpenAfterObservedUnloadBindsOnlyFreshOpening()
{
    QFETCH(bool, unload_gui_first);
    FakeWallet::State original_state, old_state, fresh_state;
    MockNode node; FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &original_state));
        wallets.push_back(std::make_unique<FakeWallet>("target", &old_state));
        return wallets;
    };
    loader.load_wallet_fn = [](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return util::Error{Untranslated("Wallet already loaded")};
    };
    ManualControllerGraph graph{node}; auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("target") && old_state.hasUnload(); }));
    auto* original = controller.walletForName("original"); QVERIFY(original);
    QPointer<WalletQmlModel> old{controller.walletForName("target")}; QVERIFY(old);
    const auto old_identity = old->opening()->identity();
    old_state.notifyUnload(); QVERIFY(!old->available());
    QVERIFY(QTest::qWaitFor([&] {
        graph.runNative(); QCoreApplication::processEvents(); return old && old->shutdownComplete();
    }, 5000));
    auto unload_posts = std::move(graph.posts); graph.posts.clear(); QVERIFY(!unload_posts.empty());
    // The callback still carries the old GUI context, but Native unload and
    // this fresh load both precede the NEW Open intent's admission.
    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("target", &fresh_state));
    graph.runNative(); QCOMPARE(graph.posts.size(), size_t{1});
    auto notification_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    QSignalSpy errors{&controller, &WalletQmlController::walletLoadErrorNotice};
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet("target", "sqlite");
    auto command = controller.m_command; QVERIFY(command);
    QVERIFY(graph.step(execution::Lane::Core)); QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto command_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    const auto publish_unload = [&] {
        while (!unload_posts.empty()) { auto turn = std::move(unload_posts.front()); unload_posts.pop_front(); turn(); }
    };
    if (unload_gui_first) publish_unload();
    notification_notice();
    if (!unload_gui_first) publish_unload();
    auto* fresh = controller.walletForName("target"); QVERIFY(fresh);
    QVERIFY(fresh->opening()->identity() != old_identity);
    command_notice();
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress() && fresh->capabilitiesKnown(); }));
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationResultWallet(), fresh); QVERIFY(controller.canOpenResult());
    QCOMPARE(controller.selectedWallet(), original); QCOMPARE(controller.openWalletModel()->rowCount(), 2);
    QCOMPARE(errors.size(), 0); QCOMPARE(loaded.size(), 1);
    QCOMPARE(loaded.front().front().value<WalletQmlModel*>(), fresh);
    const auto actual = command->result(); QVERIFY(actual && actual->value);
    QCOMPARE(actual->value->error, QStringLiteral("Wallet already loaded"));
    QVERIFY(!actual->value->has_wallet); QVERIFY(!actual->value->effect_succeeded);
    QCOMPARE(controller.walletLoadWarnings(), QStringLiteral("Wallet already loaded"));
    QCOMPARE(loader.load_wallet_calls, 1); QCOMPARE(loader.get_wallets_calls, 1); QCOMPARE(loader.handle_load_wallet_calls, 1);
    QCOMPARE(old_state.remove_calls.load(), 0); QCOMPARE(fresh_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::manualOldOpeningPromptAndOutcomeCannotResolveReopenedName()
{
    FakeWallet::State original_state, reopened_state, survivor_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        auto* state = loader.load_wallet_calls == 1 ? &original_state : &reopened_state;
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("reloaded", state));
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("reloaded", state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.selectedWallet()->capabilitiesKnown(); }));
    auto* survivor = controller.selectedWallet();
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet("./reloaded", "sqlite"); // Original request alias is not the canonical name.
    const auto original_ticket = controller.walletOperationId();
    auto original_command = controller.m_command;
    const auto original_receipt = controller.m_command_receipt;
    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto delayed_result = std::move(graph.posts.front());
    graph.posts.pop_front();
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto notification = std::move(graph.posts.front());
    graph.posts.pop_front();
    notification();
    QPointer<WalletQmlModel> original{controller.walletForName("reloaded")};
    QVERIFY(original);
    QVERIFY(graph.until([&] { return original->capabilitiesKnown(); }));
    const auto original_opening = original->opening();
    const auto original_identity = original_opening->identity();
    controller.requestCloseWallet(original);
    QVERIFY(graph.until([&] { return !original->closeQuestionToken().isEmpty(); }));
    const QString old_question = original->closeQuestionToken();
    auto held = original_opening->borrow();
    QVERIFY(held);
    const auto* native_original = held.get();
    std::optional<bool> original_outcome;
    std::function<void()> delayed_old_gui;
    int native_reads{0}, stale_gui_writes{0};
    QVERIFY(original_opening->read([&](interfaces::Wallet& wallet) {
        ++native_reads;
        return &wallet == native_original && wallet.getWalletName() == "reloaded";
    }, [&, original, original_identity](bool outcome) {
        original_outcome = outcome;
        delayed_old_gui = [&, original, original_identity] {
            if (original && original->available() && original->opening()->identity() == original_identity) ++stale_gui_writes;
        };
    }));
    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(!original_outcome);
    controller.closeWallet("reloaded");
    QVERIFY(original);
    QVERIFY(!original->available());
    QVERIFY(!controller.selectWallet(original));
    controller.requestCloseWallet(original);
    controller.setSelectedWallet("reloaded", "sqlite");
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(controller.walletLoadError().contains(QStringLiteral("still closing")));
    graph.runNative();
    QVERIFY(original_outcome && *original_outcome);
    QCOMPARE(native_reads, 1);
    delayed_result(); // The actual successful command cannot resurrect its closed adopted opening.
    QVERIFY(!controller.isWalletOpen("reloaded"));
    QCOMPARE(controller.selectedWallet(), survivor);
    QCOMPARE(controller.walletOperationId(), original_ticket);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionClosed);
    QVERIFY(!controller.walletOperationAdoptionError().isEmpty());
    QCOMPARE(loaded.size(), 0);
    QVERIFY(original_command->result());
    QVERIFY(original_command->result()->value);
    QCOMPARE(original_command->result()->value->name, QStringLiteral("reloaded"));
    original_command.reset();
    held.reset();
    QVERIFY(graph.until([&] { return original.isNull(); }));
    QCOMPARE(original_state.remove_calls.load(), 1);
    controller.setSelectedWallet("reloaded", "sqlite");
    QVERIFY(graph.until([&] { return controller.walletForName("reloaded") && controller.walletForName("reloaded")->capabilitiesKnown() && !controller.walletLoadInProgress(); }));
    auto* reopened = controller.walletForName("reloaded");
    QVERIFY(reopened);
    QVERIFY(reopened->opening()->identity() != original_identity);
    QCOMPARE(loader.load_wallet_calls, 2);
    QCOMPARE(loaded.size(), 1);
    QCOMPARE(controller.selectedWallet(), survivor);
    QCOMPARE(controller.walletOperationResultWallet(), reopened);
    QVERIFY(controller.canOpenResult());
    QVERIFY(controller.selectWallet(controller.walletOperationResultWallet()));
    QCOMPARE(controller.selectedWallet(), reopened);
    controller.requestCloseWallet(reopened);
    QVERIFY(graph.until([&] { return !reopened->closeQuestionToken().isEmpty(); }));
    const QString new_question = reopened->closeQuestionToken();
    reopened->answerClose(old_question, true);
    delayed_result();
    notification();
    QVERIFY(delayed_old_gui);
    delayed_old_gui();
    QVERIFY(graph.until([&] { return reopened->available(); }));
    QCOMPARE(reopened->closeQuestionToken(), new_question);
    QCOMPARE(stale_gui_writes, 0);
    QCOMPARE(reopened_state.remove_calls.load(), 0);
    QCOMPARE(controller.walletForName("reloaded"), reopened);
    QCOMPARE(controller.selectedWallet(), reopened);
    QCOMPARE(loaded.size(), 1);
    {
        std::lock_guard lock{original_receipt->mutex};
        QCOMPARE(original_receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded);
        QCOMPARE(original_receipt->name, QStringLiteral("reloaded"));
    }
    delayed_result = {};
    notification = {};
    delayed_old_gui = {};
}

void WalletQmlControllerTests::manualUnknownBindingNotificationCannotResurrectOrResolveReopenedName_data()
{
    QTest::addColumn<bool>("reopen_first");
    QTest::addColumn<bool>("many_native_events");
    QTest::newRow("late-before-reopen-native-ahead") << false << true;
    QTest::newRow("late-after-reopen-native-ahead") << true << true;
    QTest::newRow("late-before-reopen-many-origins") << false << false;
    QTest::newRow("late-after-reopen-many-origins") << true << false;
}

void WalletQmlControllerTests::manualUnknownBindingNotificationCannotResurrectOrResolveReopenedName()
{
    QFETCH(bool, reopen_first);
    QFETCH(bool, many_native_events);
    FakeWallet::State old_state, new_state, survivor_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &old_state));
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("original", &new_state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("original")->capabilitiesKnown(); }));
    QPointer<WalletQmlModel> old{controller.walletForName("original")};
    auto* survivor = controller.walletForName("survivor");
    const auto identity = old->opening()->identity();
    // Native freshness and GUI origin lifetime are separate monotonic domains.
    // Drive their real interfaces rather than forging the registry's counter.
    for (int event = 0; event < 128; ++event) {
        if (many_native_events) QVERIFY(controller.m_registry->eventClock()->next());
        else {
            QVERIFY(controller.newAdoptionOrigin());
            QVERIFY(graph.until([] { return true; }));
        }
    }
    QVERIFY(graph.until([] { return true; }));
    // Initial facades have stable tokens, but this source has never adopted
    // the name through its notification map. Hold its real queued publication.
    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("original", &old_state));
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto late = std::move(graph.posts.front());
    graph.posts.pop_front();
    controller.closeWallet("original");
    QVERIFY(graph.until([&] { return old.isNull(); }));
    QCOMPARE(old_state.remove_calls.load(), 1);
    QVERIFY(!controller.isWalletOpen("original"));
    QCOMPARE(controller.adoptionFenceCount(), size_t{1}); // Held old ingress still needs its escrow.
    if (reopen_first) {
        controller.setSelectedWallet("./original", "sqlite");
        QVERIFY(graph.until([&] { return controller.walletForName("original") && controller.walletForName("original")->capabilitiesKnown() && !controller.walletLoadInProgress(); }));
        QVERIFY(controller.walletForName("original")->opening()->identity() != identity);
    }
    QSignalSpy states{&controller, &WalletQmlController::walletLoadStateChanged};
    late();
    QVERIFY(graph.until([] { return true; }));
    QVERIFY(graph.until([&] { return controller.adoptionFenceCount() == 0; })); // Origin lifetime, not Native counter, retires escrow.
    QCOMPARE(states.size(), 0); // Old metadata cannot even republish the replacement as its outcome.
    QCOMPARE(controller.isWalletOpen("original"), reopen_first);
    if (!reopen_first) {
        QCOMPARE(controller.selectedWallet(), survivor);
        controller.setSelectedWallet("original", "sqlite");
        QVERIFY(graph.until([&] { return controller.walletForName("original") && controller.walletForName("original")->capabilitiesKnown() && !controller.walletLoadInProgress(); }));
    }
    QCOMPARE(loader.load_wallet_calls, 1);
    auto* actual = controller.walletForName("original");
    QVERIFY(actual);
    QVERIFY(actual->opening()->identity() != identity);
    QCOMPARE(new_state.remove_calls.load(), 0);
    states.clear();
    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("survivor", &survivor_state));
    QVERIFY(graph.until([&] { return !states.empty(); }));
    QCOMPARE(states.size(), 1);
    QCOMPARE(states.front().front().toString(), QStringLiteral("survivor"));
    QCOMPARE(controller.walletForName("survivor"), survivor);
    QCOMPARE(controller.selectedWallet(), survivor);
    QCOMPARE(controller.openWalletModel()->rowCount(), 2);
    QCOMPARE(survivor_state.remove_calls.load(), 0);
    late = {};
}

void WalletQmlControllerTests::manualUnloadBeforeFirstGuiAdoptionKeepsViewAndControllerAlive_data()
{
    QTest::addColumn<bool>("after_scan");
    QTest::newRow("unload-in-ingress-turn-before-native-scan") << false;
    QTest::newRow("unload-after-native-scan-before-gui-handoff") << true;
}
void WalletQmlControllerTests::manualUnloadBeforeFirstGuiAdoptionKeepsViewAndControllerAlive()
{
    QFETCH(bool, after_scan);
    FakeWallet::State survivor_state, target_state;
    MockNode node; FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state)); return wallets;
    };
    ManualControllerGraph graph{node}; auto& controller = *graph.controller;
    controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized() && controller.selectedWallet()->capabilitiesKnown(); }));
    auto* survivor = controller.selectedWallet(); QVERIFY(survivor->available());
    QObject opening_view;
    const QPointer<QObject> retained_view{&opening_view};
    WalletSelectionModel view_selection{controller.m_empty_wallet, [&](auto* wallet) { return controller.rowForWallet(wallet) >= 0; }, &opening_view};
    QVERIFY(view_selection.select(survivor));
    const auto operation = controller.walletOperationId();
    struct Probe { int destroyed{0}, unloads{0}; bool cleanup{true}, subscribed_before_unload{false}; };
    const auto probe = std::make_shared<Probe>();
    struct Handle final : FakeWallet {
        std::shared_ptr<Probe> probe; std::atomic<int>& lane;
        Handle(State* state, std::shared_ptr<Probe> value, std::atomic<int>& active) : FakeWallet("unloaded-before-view", state), probe(std::move(value)), lane(active) {}
        ~Handle() override { ++probe->destroyed; probe->cleanup &= lane.load() == int(execution::Lane::Cleanup); }
    };
    QSignalSpy inserted{controller.openWalletModel(), &QAbstractItemModel::rowsInserted};
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    const auto unload = [&] {
        probe->subscribed_before_unload = target_state.hasUnload();
        ++probe->unloads; target_state.notifyUnload();
    };
    const auto delivery = execution::Submit(*graph.native.scheduler, [&] {
        loader.captured_load_wallet_fn(std::make_unique<Handle>(&target_state, probe, graph.lane));
        if (!after_scan) unload();
    }, [] {}, {}, {execution::Lane::Core, graph.native.scheduler->allocateKey()});
    QVERIFY(delivery); graph.runNative();
    QVERIFY(!graph.posts.empty()); // Native completed; first GUI publication is still held.
    QVERIFY(!controller.walletForName("unloaded-before-view"));
    QCOMPARE(inserted.size(), 0); QCOMPARE(probe->destroyed, 0);
    if (after_scan) {
        const auto removal = execution::Submit(*graph.native.scheduler, unload, [] {}, {},
            {execution::Lane::Core, graph.native.scheduler->allocateKey()});
        QVERIFY(removal); graph.runNative();
    }
    QCOMPARE(probe->unloads, 1); QVERIFY(probe->subscribed_before_unload);
    QVERIFY(retained_view); QVERIFY(graph.controller.get() == &controller);
    QCOMPARE(controller.selectedWallet(), survivor); QCOMPARE(view_selection.selectedWallet(), survivor);
    QVERIFY(graph.until([&] { return probe->destroyed == 1; }));
    QVERIFY(probe->cleanup); QVERIFY(!target_state.hasUnload());
    QCOMPARE(inserted.size(), 0); QCOMPARE(loaded.size(), 0);
    QVERIFY(!controller.isWalletOpen("unloaded-before-view")); QVERIFY(!controller.walletForName("unloaded-before-view"));
    QCOMPARE(controller.openWalletModel()->rowCount(), 1); QCOMPARE(controller.openWallets(), QList<QObject*>({survivor}));
    QCOMPARE(controller.selectedWallet(), survivor); QCOMPARE(view_selection.selectedWallet(), survivor);
    QCOMPARE(controller.walletOperationId(), operation); QVERIFY(!controller.walletOperationResultWallet());
    QVERIFY(retained_view); QVERIFY(controller.initialized());
    QCOMPARE(target_state.remove_calls.load(), 0); QCOMPARE(survivor_state.remove_calls.load(), 0);
    QCOMPARE(loader.get_wallets_calls, 1); QCOMPARE(loader.handle_load_wallet_calls, 1);
}

void WalletQmlControllerTests::manualObservedUnloadAllowsFreshSameNameLoad_data()
{
    QTest::addColumn<bool>("unload_gui_first");
    QTest::newRow("old-unload-gui-before-fresh-load") << true;
    QTest::newRow("fresh-load-before-old-unload-gui") << false;
}

void WalletQmlControllerTests::manualObservedUnloadAllowsFreshSameNameLoad()
{
    QFETCH(bool, unload_gui_first);
    FakeWallet::State old_state, fresh_state, survivor_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("same-name", &old_state));
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("same-name", &old_state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.selectedWallet()->capabilitiesKnown(); }));
    auto* survivor = controller.selectedWallet();
    controller.setSelectedWallet("./same-name", "sqlite");
    const auto ticket = controller.walletOperationId();
    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto original_result = std::move(graph.posts.front());
    graph.posts.pop_front();
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto original_notification = std::move(graph.posts.front());
    graph.posts.pop_front();
    original_notification();
    QPointer<WalletQmlModel> old{controller.walletForName("same-name")};
    QVERIFY(old);
    QVERIFY(graph.until([&] { return old->capabilitiesKnown() && old_state.hasUnload(); }));
    const auto identity = old->opening()->identity();
    old_state.notifyUnload(); // Public Native unload observation, before any GUI delivery.
    QVERIFY(!old->available());
    // Advance model/backend retirement, but hold the Controller's actual old
    // unload post. The new Core load uses the still-original GUI context.
    QVERIFY(QTest::qWaitFor([&] {
        graph.runNative();
        QCoreApplication::processEvents();
        return old && old->shutdownComplete();
    }, 5000));
    auto old_posts = std::move(graph.posts);
    graph.posts.clear();
    QVERIFY(!old_posts.empty());
    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("same-name", &fresh_state));
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto fresh_notification = std::move(graph.posts.front());
    graph.posts.pop_front();
    const auto publish_unload = [&] {
        while (!old_posts.empty()) {
            auto post = std::move(old_posts.front());
            old_posts.pop_front();
            post();
        }
    };
    if (unload_gui_first) publish_unload();
    fresh_notification();
    if (!unload_gui_first) publish_unload();
    QVERIFY(graph.until([&] {
        auto* fresh = controller.walletForName("same-name");
        return fresh && fresh->available() && fresh->capabilitiesKnown() && fresh->opening()->identity() != identity;
    }));
    auto* fresh = controller.walletForName("same-name");
    QVERIFY(controller.selectWallet(survivor)); // Independent valid selection wins held old outcomes.
    original_result();
    original_notification();
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress(); }));
    QCOMPARE(controller.walletOperationId(), ticket);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionClosed);
    QCOMPARE(controller.walletForName("same-name"), fresh);
    QCOMPARE(controller.selectedWallet(), survivor);
    QCOMPARE(controller.openWalletModel()->rowCount(), 2);
    QCOMPARE(loader.load_wallet_calls, 1);
    QCOMPARE(old_state.remove_calls.load(), 0);
    QCOMPARE(fresh_state.remove_calls.load(), 0);
    QCOMPARE(survivor_state.remove_calls.load(), 0);
    original_result = {};
    original_notification = {};
    fresh_notification = {};
}

void WalletQmlControllerTests::manualInsertionCallbacksPreserveOriginalAdoption_data()
{
    QTest::addColumn<bool>("about_to_insert");
    QTest::addColumn<int>("intent");
    QTest::newRow("about-close-original") << true << 0;
    QTest::newRow("inserted-close-original") << false << 0;
    QTest::newRow("about-select-independent") << true << 1;
    QTest::newRow("inserted-select-independent") << false << 1;
    QTest::newRow("about-destroy-controller") << true << 2;
    QTest::newRow("inserted-destroy-controller") << false << 2;
}

void WalletQmlControllerTests::manualInsertionCallbacksPreserveOriginalAdoption()
{
    QFETCH(bool, about_to_insert);
    QFETCH(int, intent);
    FakeWallet::State survivor_state, target_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("survivor", &survivor_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("target", &target_state)};
    };
    QObject receiver;
    ManualControllerGraph graph{node};
    auto* controller = graph.controller.get();
    controller->initialize();
    QVERIFY(graph.until([&] { return controller->initialized() && controller->selectedWallet()->capabilitiesKnown(); }));
    auto* survivor = controller->selectedWallet();
    QPointer<WalletQmlModel> target;
    int callbacks{0}, target_open{0};
    auto on_insert = [&] {
        if (callbacks++) return;
        target = controller->walletForName("target");
        QVERIFY(target);
        if (intent == 0) controller->closeWallet("target");
        if (intent == 1) QVERIFY(controller->selectWallet(survivor));
        if (intent == 2) graph.controller.reset();
    };
    if (about_to_insert) QObject::connect(controller->openWalletModel(), &QAbstractItemModel::rowsAboutToBeInserted, &receiver, on_insert);
    else QObject::connect(controller->openWalletModel(), &QAbstractItemModel::rowsInserted, &receiver, on_insert);
    QObject::connect(controller, &WalletQmlController::walletLoadStateChanged, &receiver,
        [&](const QString& name, WalletListModel::LoadState state, const QString&) {
            if (name == QStringLiteral("target") && state == WalletListModel::LoadState::Open) ++target_open;
        });
    controller->setSelectedWallet("target", "sqlite");
    const auto receipt = controller->m_command_receipt;
    QVERIFY(graph.until([&] {
        if (intent == 2) return !graph.controller && graph.physical_completions == 1;
        return !controller->walletLoadInProgress() && (intent != 0 || target.isNull());
    }));
    QCOMPARE(callbacks, 1);
    QCOMPARE(loader.load_wallet_calls, 1);
    {
        std::lock_guard lock{receipt->mutex};
        QCOMPARE(receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    }
    if (intent == 2) {
        QCOMPARE(graph.native.scheduler->pending(), size_t{0});
        QCOMPARE(target_open, 0);
        return;
    }
    QCOMPARE(controller->selectedWallet(), survivor);
    QCOMPARE(controller->openWalletModel()->rowCount(), intent == 0 ? 1 : 2);
    QCOMPARE(controller->openWalletModel()->data(controller->openWalletModel()->index(0, 0), Qt::UserRole + 1).value<WalletQmlModel*>(), survivor);
    if (intent == 0) {
        QVERIFY(!controller->isWalletOpen("target"));
        QCOMPARE(target_open, 0);
        QCOMPARE(controller->walletOperationAdoptionState(), WalletQmlController::WalletAdoptionState::AdoptionClosed);
        QCOMPARE(target_state.remove_calls.load(), 1);
    } else {
        QCOMPARE(controller->walletForName("target"), target.data());
        QCOMPARE(controller->openWalletModel()->data(controller->openWalletModel()->index(1, 0), Qt::UserRole + 1).value<WalletQmlModel*>(), target.data());
        QCOMPARE(target_open, 1);
        QCOMPARE(target_state.remove_calls.load(), 0);
    }
    QCOMPARE(survivor_state.remove_calls.load(), 0);
}



void WalletQmlControllerTests::manualControllerAdmissionSurvivesReentrantOwnerLoss_data()
{
    QTest::addColumn<bool>("after_borrow");
    QTest::addColumn<bool>("destroy");
    QTest::newRow("after-cleanup-reservation") << false << true;
    QTest::newRow("after-original-borrow") << true << true;
    QTest::newRow("quiet-after-cleanup-reservation") << false << false;
    QTest::newRow("quiet-after-original-borrow") << true << false;
}
void WalletQmlControllerTests::manualControllerAdmissionSurvivesReentrantOwnerLoss()
{
    QFETCH(bool, after_borrow);
    QFETCH(bool, destroy);
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    std::shared_ptr<ControllerBoundaryScheduler> boundary;
    ManualControllerGraph graph{node, execution::Options{}, [&](auto target) {
        boundary = std::make_shared<ControllerBoundaryScheduler>(std::move(target));
        return boundary;
    }};
    graph.controller->initialize();
    QVERIFY(graph.until([&] { return graph.controller->initialized(); }));
    const QPointer<WalletQmlController> observed{graph.controller.get()};
    const auto retire = [&] {
        if (destroy) graph.controller.reset();
        else graph.controller->beginShutdown();
    };
    if (after_borrow) boundary->after_borrow = retire;
    else boundary->after_reserve = retire;
    observed->setSelectedWallet("never-loaded", "sqlite");
    if (destroy) QVERIFY(!observed);
    else QVERIFY(observed && observed->m_shutting_down);
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(graph.until([&] { return graph.physical_completions == 1 && graph.native.scheduler->pending() == 0; }));
}

void WalletQmlControllerTests::manualPendingSelectionKeepsNoOwnerReferenceAfterNotification_data()
{
    QTest::addColumn<bool>("destroy");
    QTest::newRow("controller-destroyed") << true;
    QTest::newRow("quiet-controller-retained") << false;
}
void WalletQmlControllerTests::manualPendingSelectionKeepsNoOwnerReferenceAfterNotification()
{
    QFETCH(bool, destroy);
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node};
    graph.controller->initialize();
    QVERIFY(graph.until([&] { return graph.controller->initialized(); }));
    const QPointer<WalletQmlController> observed{graph.controller.get()};
    bool retired{false};
    connect(graph.controller->selection(), &WalletSelectionModel::pendingChanged, graph.controller.get(), [&] {
        if (retired) return;
        retired = true;
        if (destroy) graph.controller.reset();
        else graph.controller->beginShutdown();
    });
    observed->setSelectedWallet("unused-after-pending-notice", "sqlite");
    QVERIFY(retired);
    if (destroy) QVERIFY(!observed);
    else QVERIFY(observed && observed->m_shutting_down);
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(graph.until([&] { return graph.physical_completions == 1 && graph.native.scheduler->pending() == 0; }));
}

void WalletQmlControllerTests::manualInjectedShutdownOwnsAcceptedOutcomeAfterObserverLoss_data()
{
    QTest::addColumn<bool>("destroy_observer");
    QTest::newRow("quiet-controller-stays-live") << false;
    QTest::newRow("controller-destroyed") << true;
}
void WalletQmlControllerTests::manualInjectedShutdownOwnsAcceptedOutcomeAfterObserverLoss()
{
    QFETCH(bool, destroy_observer);
    MockNode node;
    FakeWalletLoader loader;
    FakeWallet::State state;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("accepted", &state)};
    };
    ManualControllerGraph graph{node};
    graph.controller->initialize();
    QVERIFY(graph.until([&] { return graph.controller->initialized(); }));
    graph.controller->setSelectedWallet("accepted", "sqlite");
    const auto receipt = graph.controller->m_command_receipt;
    const QPointer<WalletQmlController> observed{graph.controller.get()};
    QSignalSpy loaded{graph.controller.get(), &WalletQmlController::walletLoadSucceeded};
    QCOMPARE(loader.load_wallet_calls, 0);
    graph.controller->beginShutdown();
    QVERIFY(!graph.drained);
    QCOMPARE(graph.physical_completions, 0);
    if (destroy_observer) graph.controller.reset();
    QVERIFY(graph.until([&] { return graph.physical_completions == 1 && graph.native.scheduler->pending() == 0; }));
    QCOMPARE(loader.load_wallet_calls, 1);
    QCOMPARE(loaded.size(), 0);
    {
        std::lock_guard lock{receipt->mutex};
        QVERIFY(receipt->ready);
        QCOMPARE(receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded);
        QCOMPARE(receipt->name, QStringLiteral("accepted"));
    }
    QCOMPARE(bool(observed), !destroy_observer);
    if (observed) QVERIFY(!observed->isWalletOpen("accepted"));
    QCOMPARE(state.private_accesses.load(), 0);
}

void WalletQmlControllerTests::acceptedCreationDoesNotRetargetLaterSelection()
{
    FakeWallet::State original_state, created_state;
    MockNode node; FakeWalletLoader loader;
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &original_state)); return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);
    auto entered = std::make_shared<QSemaphore>(), release = std::make_shared<QSemaphore>();
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    auto* original = controller.selectedWallet(); QVERIFY(original);
    loader.create_wallet_fn = [&, entered, release](const std::string& name, const SecureString&, uint64_t, std::vector<bilingual_str>&) {
        entered->release(); release->acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{std::make_unique<FakeWallet>(name, &created_state)};
    };
    const auto unblock = qScopeGuard([release] { release->release(); });
    QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    controller.createSingleSigWallet("created", "");
    QVERIFY(entered->tryAcquire(1, 5000));
    const auto reads = loader.list_wallet_dir_calls;
    QVERIFY(controller.selectWallet(original)); // Explicitly reaffirm the later selection.
    QCOMPARE(loader.list_wallet_dir_calls, reads);
    release->release();
    QTRY_COMPARE(created.size(), 1);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    QCOMPARE(controller.walletOperationName(), QStringLiteral("created"));
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(controller.selectedWallet(), original);
    auto* actual = created[0][0].value<WalletQmlModel*>(); QVERIFY(actual); QCOMPARE(actual->name(), QStringLiteral("created"));
    QVERIFY(controller.rowForWallet(actual) >= 0); QCOMPARE(created_state.private_accesses.load(), 0);
}
void WalletQmlControllerTests::manualIndependentSelectionsKeepExactRegistryMembership()
{
    FakeWallet::State alpha_state, beta_state, reopened_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("alpha", &alpha_state));
        wallets.push_back(std::make_unique<FakeWallet>("beta", &beta_state));
        return wallets;
    };
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("alpha", &reopened_state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && controller.walletForName("alpha")->capabilitiesKnown(); }));
    QPointer<WalletQmlModel> alpha{controller.walletForName("alpha")};
    auto* beta = controller.walletForName("beta"); QVERIFY(alpha); QVERIFY(beta);
    const auto old_opening = alpha->opening()->identity();
    auto held = alpha->opening()->borrow(); QVERIFY(held);
    WalletSelectionModel second{controller.m_empty_wallet, [&](auto* wallet) { return controller.rowForWallet(wallet) >= 0; }};
    QVERIFY(second.select(alpha));
    QVERIFY(controller.selectWallet(beta));
    QCOMPARE(second.selectedWallet(), alpha.data());
    QCOMPARE(controller.selectedWallet(), beta);
    QSignalSpy independent_changes{&second, &WalletSelectionModel::selectedWalletChanged};
    controller.closeWallet("alpha");
    QCOMPARE(second.selectedWallet(), controller.m_empty_wallet);
    QCOMPARE(independent_changes.size(), 1);
    QCOMPARE(controller.selectedWallet(), beta);
    QCOMPARE(controller.rowForWallet(alpha), -1);
    QVERIFY(!second.select(alpha));
    QVERIFY(!controller.selectWallet(alpha));
    held.reset();
    QVERIFY(graph.until([&] { return alpha.isNull(); }));
    controller.setSelectedWallet("alpha", "sqlite");
    QVERIFY(graph.until([&] { return controller.walletForName("alpha") && !controller.walletLoadInProgress(); }));
    auto* reopened = controller.walletForName("alpha");
    QVERIFY(reopened->opening()->identity() != old_opening);
    QCOMPARE(controller.selectedWallet(), beta);
    QCOMPARE(second.selectedWallet(), controller.m_empty_wallet);
    QVERIFY(second.select(reopened));
    QCOMPARE(second.selectedWallet(), reopened);
    QCOMPARE(controller.selectedWallet(), beta);
    QCOMPARE(loader.load_wallet_calls, 1);
    QCOMPARE(alpha_state.remove_calls.load(), 1);
    QCOMPARE(reopened_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::manualPendingSelectionCannotRetargetAnotherWindow_data()
{
    QTest::addColumn<bool>("finish_before_newer_selection");
    QTest::newRow("finish-A-before-select-B") << true;
    QTest::newRow("select-B-before-finish-A") << false;
}
void WalletQmlControllerTests::manualPendingSelectionCannotRetargetAnotherWindow()
{
    QFETCH(bool, finish_before_newer_selection);
    FakeWallet::State alpha_state, beta_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("alpha", &alpha_state));
        wallets.push_back(std::make_unique<FakeWallet>("beta", &beta_state));
        return wallets;
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized(); }));
    auto* alpha = controller.walletForName("alpha"); auto* beta = controller.walletForName("beta");
    QVERIFY(alpha); QVERIFY(beta);
    WalletSelectionModel second{controller.m_empty_wallet, [&](auto* wallet) { return controller.rowForWallet(wallet) >= 0; }};
    QVERIFY(second.select(alpha));
    QSignalSpy selected{&second, &WalletSelectionModel::selectedWalletChanged};
    QSignalSpy pending{&second, &WalletSelectionModel::pendingChanged};
    const auto original = second.beginSelection(QString{}); // Empty default-wallet target is a real pending ID.
    QVERIFY(second.pending()); QCOMPARE(second.pendingTargetId(), QString{});
    if (finish_before_newer_selection) second.finishSelection(original);
    QVERIFY(second.select(beta));
    if (!finish_before_newer_selection) second.finishSelection(original);
    QVERIFY(!second.pending()); QCOMPARE(second.pendingTargetId(), QString{});
    QCOMPARE(second.selectedWallet(), beta);
    QCOMPARE(controller.selectedWallet(), alpha);
    QCOMPARE(selected.size(), 1); QCOMPARE(pending.size(), 2);
    const auto newer = second.beginSelection("beta");
    second.finishSelection(original);
    QVERIFY(second.pending()); QCOMPARE(second.pendingTargetId(), QStringLiteral("beta"));
    second.finishSelection(newer);
    QVERIFY(!second.pending()); QCOMPARE(second.selectedWallet(), beta);
    QCOMPARE(loader.load_wallet_calls, 0);
}

void WalletQmlControllerTests::manualRegistryRemovalPreservesReentrantSelection()
{
    FakeWallet::State alpha_state, beta_state, gamma_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("alpha", &alpha_state));
        wallets.push_back(std::make_unique<FakeWallet>("beta", &beta_state));
        wallets.push_back(std::make_unique<FakeWallet>("gamma", &gamma_state));
        return wallets;
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized(); }));
    QPointer<WalletQmlModel> alpha{controller.walletForName("alpha")};
    auto* beta = controller.walletForName("beta"); auto* gamma = controller.walletForName("gamma");
    QVERIFY(alpha); QVERIFY(beta); QVERIFY(gamma);
    bool chose_gamma{false};
    QObject selection_observer;
    QSignalSpy selected{&controller, &WalletQmlController::selectedWalletChanged};
    connect(&controller, &WalletQmlController::selectedWalletChanged, &selection_observer, [&] {
        if (controller.selectedWallet() != beta || chose_gamma) return;
        QCOMPARE(controller.rowForWallet(alpha), -1);
        QVERIFY(!controller.isWalletOpen("alpha"));
        QVERIFY(!alpha->available());
        QVERIFY(controller.m_registry->retirement()->values->retiring.contains(alpha));
        chose_gamma = true;
        QVERIFY(controller.selectWallet(gamma));
    });
    controller.closeWallet("alpha");
    QVERIFY(chose_gamma);
    QCOMPARE(selected.size(), 2);
    QCOMPARE(controller.selectedWallet(), gamma);
    QCOMPARE(controller.openWallets(), QList<QObject*>({beta, gamma}));
    QVERIFY(graph.until([&] { return alpha.isNull(); }));
    QCOMPARE(alpha_state.remove_calls.load(), 1);
    QCOMPARE(beta_state.remove_calls.load(), 0); QCOMPARE(gamma_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::manualResultRetirementDoesNotResolveByName()
{
    FakeWallet::State original_state, reopened_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.load_wallet_fn = [&](const auto&, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("same", loader.load_wallet_calls == 1 ? &original_state : &reopened_state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    auto* empty = controller.selectedWallet();
    controller.setSelectedWallet("same", "sqlite");
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress() && controller.walletForName("same"); }));
    QPointer<WalletQmlModel> result{controller.walletOperationResultWallet()}; QVERIFY(result);
    const auto identity = result->opening()->identity();
    QVERIFY(controller.canOpenResult()); QCOMPARE(controller.selectedWallet(), empty);
    auto held = result->opening()->borrow(); QVERIFY(held);
    controller.closeWallet("same");
    QCOMPARE(controller.walletOperationResultWallet(), result.data());
    QVERIFY(!controller.canOpenResult()); QVERIFY(!controller.selectWallet(result));
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
    held.reset(); QVERIFY(graph.until([&] { return result.isNull(); }));
    QVERIFY(!controller.walletOperationResultWallet()); QVERIFY(!controller.canOpenResult());
    controller.setSelectedWallet("same", "sqlite");
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress() && controller.walletForName("same"); }));
    auto* reopened = controller.walletOperationResultWallet(); QVERIFY(reopened);
    QVERIFY(reopened->opening()->identity() != identity);
    QCOMPARE(controller.selectedWallet(), empty);
    QVERIFY(controller.canOpenResult()); QVERIFY(controller.selectWallet(reopened));
    QCOMPARE(controller.selectedWallet(), reopened);
    QCOMPARE(loader.load_wallet_calls, 2); QCOMPARE(original_state.remove_calls.load(), 1);
    QCOMPARE(reopened_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::manualRetainedResultBackendCleanupFencesReopen_data()
{
    QTest::addColumn<bool>("with_warning");
    QTest::addColumn<bool>("notification_first");
    QTest::newRow("plain-result-first") << false << false;
    QTest::newRow("plain-callback-first") << false << true;
    QTest::newRow("warning-result-first") << true << false;
    QTest::newRow("warning-callback-first") << true << true;
}
void WalletQmlControllerTests::manualRetainedResultBackendCleanupFencesReopen()
{
    QFETCH(bool, with_warning);
    QFETCH(bool, notification_first);
    FakeWallet::State standby_state, original_state, reopened_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("standby", &standby_state));
        return wallets;
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized(); }));
    auto* standby = controller.selectedWallet();
    struct Probe { int destroyed{0}; bool native{true}; };
    const auto probe = std::make_shared<Probe>();
    struct CommandHandle final : FakeWallet {
        std::shared_ptr<Probe> probe; std::atomic<int>& lane;
        CommandHandle(State* state, std::shared_ptr<Probe> value, std::atomic<int>& context)
            : FakeWallet("target", state), probe(std::move(value)), lane(context) {}
        ~CommandHandle() override { ++probe->destroyed; probe->native &= lane.load() == int(execution::Lane::Cleanup); }
    };
    const QString warning = with_warning ? QString(8192, QLatin1Char{'w'}) : QString{};
    loader.load_wallet_fn = [&](const auto&, auto& warnings) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (loader.load_wallet_calls != 1) return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("target", &reopened_state)};
        if (with_warning) warnings.push_back(Untranslated(warning.toStdString()));
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("target", &original_state));
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<CommandHandle>(&original_state, probe, graph.lane)};
    };
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet("target", "sqlite");
    const auto ticket = controller.walletOperationId();
    auto held_backend = controller.m_command_backend; QVERIFY(held_backend);
    const auto metadata = controller.m_command_owner; QVERIFY(metadata && *metadata);
    const auto receipt = controller.m_command_receipt;
    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto command_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    graph.runNative();
    QCOMPARE(graph.posts.size(), size_t{1});
    auto notification_notice = std::move(graph.posts.front()); graph.posts.pop_front();
    if (notification_first) notification_notice(); else command_notice();
    QPointer<WalletQmlModel> original{controller.walletForName("target")}; QVERIFY(original);
    if (notification_first) command_notice(); else notification_notice();
    QVERIFY(graph.until([&] { return original->capabilitiesKnown(); }));
    QCOMPARE(loaded.size(), 1); QCOMPARE(controller.selectedWallet(), standby);
    QCOMPARE(controller.walletOperationResultWallet(), original.data());
    QCOMPARE(controller.walletLoadWarnings(), warning);
    QVERIFY((*metadata)->result()->value->has_wallet);
    QVERIFY(!(*metadata)->result()->value->wallet);
    QCOMPARE((*metadata)->result()->value->warnings, warning);
    QCOMPARE(probe->destroyed, 0);
    const auto identity = original->opening()->identity();
    QCOMPARE(controller.m_registry->retirement()->values->adoptions.at(original)->pending_results.size(), size_t{1});

    // The real close-prompt path preserves completed warning presentation.
    controller.requestCloseWallet(original);
    QVERIFY(graph.until([&] { return !original->closeQuestionToken().isEmpty(); }));
    original->answerClose(original->closeQuestionToken(), true);
    QVERIFY(graph.until([&] { return original->shutdownComplete(); }));
    QVERIFY(original); QVERIFY(!original->available());
    QVERIFY(!controller.isWalletOpen("target")); QVERIFY(!controller.canOpenResult());
    QCOMPARE(original_state.remove_calls.load(), 1);
    QCOMPARE(probe->destroyed, 0);
    QCOMPARE(controller.walletLoadWarnings(), warning);
    controller.setSelectedWallet("target", "sqlite");
    QCOMPARE(loader.load_wallet_calls, 1);
    QCOMPARE(controller.walletOperationId(), ticket);
    QVERIFY(controller.walletLoadError().contains(QStringLiteral("still closing")));
    QCOMPARE(controller.walletLoadWarnings(), warning);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);

    held_backend.reset(); // GUI drops only a lease, never the native adapter.
    QCOMPARE(probe->destroyed, 0);
    QVERIFY(graph.until([&] { return original.isNull(); }));
    QCOMPARE(probe->destroyed, 1); QVERIFY(probe->native);
    QCOMPARE(controller.walletLoadWarnings(), warning);
    QCOMPARE((*metadata)->result()->value->warnings, warning);
    QVERIFY((*metadata)->result()->value->has_wallet);
    QVERIFY((*metadata)->result()->value->backend.expired());
    { std::lock_guard lock{receipt->mutex}; QCOMPARE(receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded); QCOMPARE(receipt->name, QStringLiteral("target")); }
    controller.setSelectedWallet("target", "sqlite");
    QVERIFY(controller.walletOperationId() > ticket);
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(graph.until([&] { return controller.walletForName("target") && !controller.walletLoadInProgress(); }));
    QCOMPARE(loader.load_wallet_calls, 2);
    auto* reopened = controller.walletForName("target");
    QVERIFY(reopened->opening()->identity() != identity);
    QCOMPARE(controller.selectedWallet(), standby);
    QCOMPARE(controller.walletOperationResultWallet(), reopened);
    QCOMPARE((*metadata)->result()->value->warnings, warning);
    QVERIFY(controller.walletLoadWarnings().isEmpty());
    QCOMPARE(reopened_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::manualPublicationOwnsResultBeforeReopeningCoreAdmission_data()
{
    QTest::addColumn<bool>("try_create");
    QTest::newRow("load-A-reentrant-load-B") << false;
    QTest::newRow("load-A-reentrant-create-B") << true;
}
void WalletQmlControllerTests::manualPublicationOwnsResultBeforeReopeningCoreAdmission()
{
    QFETCH(bool, try_create);
    FakeWallet::State standby_state, selected_state, first_state, second_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    QTemporaryDir directory; QVERIFY(directory.isValid()); loader.wallet_dir = directory.path().toStdString();
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("standby", &standby_state));
        wallets.push_back(std::make_unique<FakeWallet>("selected-b", &selected_state));
        return wallets;
    };
    int first_effects{0}, second_effects{0};
    loader.load_wallet_fn = [&](const std::string& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (name == "target-a") { ++first_effects; return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &first_state)}; }
        ++second_effects; return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &second_state)};
    };
    loader.create_wallet_fn = [&](const std::string& name, const auto&, uint64_t, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        ++second_effects; return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &second_state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    auto* presentation_b = controller.walletForName("selected-b"); QVERIFY(presentation_b);
    controller.setSelectedWallet("target-a", "sqlite");
    const auto original_id = controller.walletOperationId();
    const auto actual_receipt = controller.m_command_receipt;
    const auto actual_metadata = controller.m_command_owner;
    QVERIFY(actual_metadata && *actual_metadata);
    QVERIFY(controller.walletLoadInProgress()); QVERIFY(!controller.walletForName("target-a"));
    QVERIFY(graph.step(execution::Lane::Core));
    QCOMPARE(first_effects, 1); QCOMPARE(second_effects, 0);
    QVERIFY(graph.step(execution::Lane::Domain));
    QVERIFY((*actual_metadata)->result() && (*actual_metadata)->result()->value->has_wallet);
    { std::lock_guard lock{actual_receipt->mutex}; QVERIFY(actual_receipt->ready); QCOMPARE(actual_receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded); }

    bool saw_end{false}, premature_idle{false}, selected_b{false}, premature_core_admission{false}, premature_terminal{false};
    int original_terminal_count{0};
    connect(&controller, &WalletQmlController::walletLoadSucceeded, this, [&](WalletQmlModel* wallet) {
        if (!wallet || wallet->name() != "target-a") return;
        ++original_terminal_count;
        premature_terminal |= controller.walletForName("target-a") != wallet || controller.walletOperationResultWallet() != wallet || !wallet->available();
    });
    connect(&controller, &WalletQmlController::walletLoadInProgressChanged, this, [&] {
        if (saw_end || controller.walletLoadInProgress() || controller.walletOperationId() != original_id) return;
        saw_end = true;
        auto* first = controller.walletForName("target-a");
        const bool coherent = first && first->available() && controller.walletOperationResultWallet() == first;
        premature_idle = !coherent;
        selected_b = controller.selectWallet(presentation_b); // Presentation intent stays legal.
        if (!coherent) {
            if (try_create) controller.createSingleSigWallet("created-b", {});
            else controller.setSelectedWallet("second-load", "sqlite");
            premature_core_admission = controller.walletOperationId() != original_id;
        }
    });
    QVERIFY(graph.until([&] { return saw_end && !controller.walletLoadInProgress(); }));
    QVERIFY(saw_end);
    QVERIFY(selected_b); QCOMPARE(controller.selectedWallet(), presentation_b);
    QCOMPARE(first_effects, 1);
    // The settled A receipt is independent of a reentrant facade observation.
    { std::lock_guard lock{actual_receipt->mutex}; QCOMPARE(actual_receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded); QCOMPARE(actual_receipt->name, QStringLiteral("target-a")); }
    QVERIFY((*actual_metadata)->result()->value->has_wallet);
    const auto evidence = QStringLiteral("idle-before-result=%1; premature-Core-admission=%2; A-effects=%3; B-effects=%4; A-terminal-count=%5; actual-ticket=%6; original-ticket=%7")
        .arg(premature_idle).arg(premature_core_admission).arg(first_effects).arg(second_effects)
        .arg(original_terminal_count).arg(controller.walletOperationId()).arg(original_id);
    QVERIFY2(!premature_idle, qPrintable(evidence));
    QVERIFY2(!premature_core_admission, "a second Core command was admitted inside the unowned A publication window");
    QCOMPARE(second_effects, 0);
    QCOMPARE(controller.walletOperationId(), original_id);
    QCOMPARE(original_terminal_count, 1);
    QVERIFY(!premature_terminal);
    auto* first = controller.walletForName("target-a"); QVERIFY(first);
    QCOMPARE(controller.walletOperationResultWallet(), first);
    QCOMPARE(controller.selectedWallet(), presentation_b);
}

void WalletQmlControllerTests::manualTerminalPublicationRejectsReentrantCoreUntilProgressReleased_data()
{
    QTest::addColumn<bool>("try_create");
    QTest::newRow("terminal-load-rejected-idle-load-admitted") << false;
    QTest::newRow("terminal-create-rejected-idle-create-admitted") << true;
}
void WalletQmlControllerTests::manualTerminalPublicationRejectsReentrantCoreUntilProgressReleased()
{
    QFETCH(bool, try_create);
    FakeWallet::State standby_state, selected_state, first_state, second_state;
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    QTemporaryDir directory; QVERIFY(directory.isValid()); loader.wallet_dir = directory.path().toStdString();
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("standby", &standby_state));
        wallets.push_back(std::make_unique<FakeWallet>("selected-b", &selected_state));
        return wallets;
    };
    int first_effects{0}, second_effects{0};
    loader.load_wallet_fn = [&](const std::string& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        if (name == "target-a") { ++first_effects; return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &first_state)}; }
        ++second_effects; return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &second_state)};
    };
    loader.create_wallet_fn = [&](const std::string& name, const auto&, uint64_t, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        ++second_effects; return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &second_state)};
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    auto* presentation_b = controller.walletForName("selected-b"); QVERIFY(presentation_b);
    controller.setSelectedWallet("target-a", "sqlite");
    const auto original_id = controller.walletOperationId();
    const auto receipt = controller.m_command_receipt;
    const auto metadata = controller.m_command_owner;
    QPointer<WalletQmlModel> original_result;
    int original_terminal_count{0}, successor_terminal_count{0}, idle_count{0}, adoption_count{0};
    bool admission_blocked{false}, idle_admitted{false}, selected_b{false};
    const auto try_successor = [&] {
        if (try_create) controller.createSingleSigWallet("created-b", {});
        else controller.setSelectedWallet("second-load", "sqlite");
    };
    connect(&controller, &WalletQmlController::openWalletsChanged, &controller, [&] {
        auto* first = controller.walletForName("target-a");
        if (!first || original_terminal_count) return;
        ++adoption_count;
        // Row publication precedes result ownership: the public operation and
        // Core admission must still be Pending at this original boundary.
        QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
        QVERIFY(controller.walletLoadInProgress());
        QCOMPARE(controller.walletOperationId(), original_id);
        try_successor();
        QCOMPARE(controller.walletOperationId(), original_id);
        QCOMPARE(second_effects, 0);
    });
    const auto completed = [&](WalletQmlModel* wallet) {
        if (!wallet || wallet->name() != "target-a") { ++successor_terminal_count; return; }
        ++original_terminal_count;
        original_result = wallet;
        QCOMPARE(controller.walletForName("target-a"), wallet);
        QCOMPARE(controller.walletOperationResultWallet(), wallet);
        QVERIFY(wallet->available()); QVERIFY(controller.canOpenResult());
        QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
        QVERIFY(controller.walletLoadInProgress());
        selected_b = controller.selectWallet(presentation_b);
        try_successor();
        admission_blocked = controller.walletOperationId() == original_id;
        QCOMPARE(second_effects, 0);
        QCOMPARE(controller.selectedWallet(), presentation_b);
    };
    connect(&controller, &WalletQmlController::walletLoadSucceeded, &controller, completed);
    connect(&controller, &WalletQmlController::walletCreateSucceeded, &controller, completed);
    connect(&controller, &WalletQmlController::walletLoadInProgressChanged, &controller, [&] {
        if (controller.walletLoadInProgress() || controller.walletOperationId() != original_id) return;
        ++idle_count;
        QCOMPARE(original_terminal_count, 1);
        QCOMPARE(controller.walletOperationResultWallet(), original_result.data());
        QCOMPARE(controller.walletForName("target-a"), original_result.data());
        QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
        QVERIFY(!controller.m_selection->pending());
        try_successor();
        idle_admitted = controller.walletOperationId() > original_id;
    });
    QVERIFY(graph.until([&] { return successor_terminal_count == 1 && !controller.walletLoadInProgress(); }));
    QCOMPARE(first_effects, 1); QCOMPARE(second_effects, 1);
    QCOMPARE(adoption_count, 1); QCOMPARE(original_terminal_count, 1); QCOMPARE(successor_terminal_count, 1); QCOMPARE(idle_count, 1);
    QVERIFY(admission_blocked); QVERIFY(idle_admitted); QVERIFY(selected_b);
    QVERIFY(original_result); QVERIFY(original_result->available());
    QCOMPARE(controller.selectedWallet(), presentation_b);
    QCOMPARE(controller.walletOperationResultWallet(), controller.walletForName(try_create ? QStringLiteral("created-b") : QStringLiteral("second-load")));
    QVERIFY(controller.walletOperationResultWallet() != original_result);
    QVERIFY(metadata && *metadata && (*metadata)->result() && (*metadata)->result()->value->has_wallet);
    { std::lock_guard lock{receipt->mutex}; QVERIFY(receipt->ready); QCOMPARE(receipt->state, WalletQmlController::WalletOperationState::WalletOperationSucceeded); QCOMPARE(receipt->name, QStringLiteral("target-a")); }
}

void WalletQmlControllerTests::manualDefaultWalletLoadAndLocation_data()
{
    QTest::addColumn<bool>("explicit_format");
    QTest::newRow("catalog-discovers-default-format") << false;
    QTest::newRow("typed-default-format") << true;
}
void WalletQmlControllerTests::manualDefaultWalletLoadAndLocation()
{
    QFETCH(bool, explicit_format);
    FakeWallet::State state;
    MockNode node;
    FakeWalletLoader loader;
    QTemporaryDir directory; QVERIFY(directory.isValid());
    loader.wallet_dir = directory.path().toStdString();
    loader.wallet_dir_entries = {{"", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
    bool effect_on_core{false}; std::string loaded_name{"not-executed"};
    loader.load_wallet_fn = [&](const std::string& name, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loaded_name = name; effect_on_core = graph.lane.load() == int(execution::Lane::Core);
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("", &state)};
    };
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet({}, explicit_format ? QStringLiteral("sqlite") : QString{});
    QVERIFY(controller.m_selection->pending()); QCOMPARE(controller.m_selection->pendingTargetId(), QString{});
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(graph.until([&] { return loaded.size() == 1 && !controller.walletLoadInProgress(); }));
    QCOMPARE(loaded_name, std::string{}); QVERIFY(effect_on_core);
    QCOMPARE(loader.load_wallet_calls, 1); QVERIFY(controller.isWalletOpen(QString{}));
    auto* result = controller.walletOperationResultWallet(); QVERIFY(result); QCOMPARE(result->name(), QString{});
    QCOMPARE(controller.selectedWallet(), controller.m_empty_wallet);
    QVERIFY(!controller.m_selection->pending()); QVERIFY(controller.canOpenResult());
    QVERIFY(controller.selectWallet(result));
    QString opened; int native_handoffs{0};
    controller.setOpenLocalPathFnForTesting([&](const QString& path) { opened = path; ++native_handoffs; return true; });
    QVERIFY(controller.openWalletLocation(result));
    QCOMPARE(native_handoffs, 0);
    QVERIFY(graph.until([&] { return !controller.walletLocationPending(); }));
    QCOMPARE(native_handoffs, 1); QCOMPARE(opened, directory.path());
    QVERIFY(controller.walletLocationOpenError().isEmpty());
    controller.closeWallet(QString{});
    QVERIFY(!controller.isWalletOpen(QString{})); QVERIFY(!controller.canOpenResult());
    QVERIFY(graph.until([&] { return state.remove_calls.load() == 1; }));
    QCOMPARE(state.private_accesses.load(), 0);
}

void WalletQmlControllerTests::openWalletRowsPreserveIndependentViews()
{
    FakeWallet::State alpha_state, beta_state;
    MockNode node; FakeWalletLoader loader;
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("alpha", &alpha_state));
        wallets.push_back(std::make_unique<FakeWallet>("beta", &beta_state)); return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    auto* rows = controller.openWalletModel(); QCOMPARE(rows->rowCount(), 2);
    QSignalSpy resets{rows, &QAbstractItemModel::modelReset};
    const QPersistentModelIndex alpha{rows->index(controller.rowForWallet(controller.walletForName("alpha")), 0)};
    auto* alpha_wallet = controller.walletForName("alpha");
    QCOMPARE(alpha.data(Qt::UserRole + 1).value<WalletQmlModel*>(), alpha_wallet);
    controller.closeWallet("beta");
    QTRY_COMPARE(rows->rowCount(), 1);
    QVERIFY(alpha.isValid()); QCOMPARE(alpha.data(Qt::UserRole + 1).value<WalletQmlModel*>(), alpha_wallet);
    QCOMPARE(controller.rowForWallet(alpha_wallet), 0); QCOMPARE(resets.size(), 0);
    QTRY_COMPARE(beta_state.remove_calls.load(), 1);
}

void WalletQmlControllerTests::openWalletTreeRemovalPreservesRowsAndPersistentIdentities()
{
    MockNode node;FakeWalletLoader loader;
    loader.get_wallets_fn=[] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;wallets.reserve(17);
        for(int index=0;index<17;++index)wallets.push_back(std::make_unique<FakeWallet>("tree-"+util::ToString(index),nullptr));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node,loader);
    DrainingWalletController controller{node};controller.initialize();QTRY_VERIFY(controller.initialized());
    auto* rows=controller.openWalletModel();QTRY_COMPARE(rows->rowCount(),17);
    std::vector<WalletQmlModel*> expected;
    std::vector<QPersistentModelIndex> persistent;expected.reserve(17);persistent.reserve(17);
    for(int row=0;row<rows->rowCount();++row){
        expected.push_back(rows->data(rows->index(row,0),Qt::UserRole+1).value<WalletQmlModel*>());
        persistent.emplace_back(rows->index(row,0));
    }
    const auto original=expected;std::set<WalletQmlModel*> removed;
    QSignalSpy resets{rows,&QAbstractItemModel::modelReset};
    for(int index:{7,3,11,0,16,8,4}){
        const auto name=QStringLiteral("tree-%1").arg(index);auto* wallet=controller.walletForName(name);QVERIFY(wallet);
        removed.insert(wallet);const auto found=std::find(expected.begin(),expected.end(),wallet);QVERIFY(found!=expected.end());expected.erase(found);
        controller.closeWallet(name);QTRY_COMPARE(rows->rowCount(),int(expected.size()));
        for(int row=0;row<rows->rowCount();++row){
            QCOMPARE(rows->data(rows->index(row,0),Qt::UserRole+1).value<WalletQmlModel*>(),expected[row]);
            QCOMPARE(controller.rowForWallet(expected[row]),row);
        }
        for(size_t row=0;row<original.size();++row){
            if(removed.contains(original[row]))QVERIFY(!persistent[row].isValid());
            else{QVERIFY(persistent[row].isValid());QCOMPARE(persistent[row].data(Qt::UserRole+1).value<WalletQmlModel*>(),original[row]);}
        }
    }
    QCOMPARE(resets.size(),0);
}

void WalletQmlControllerTests::provisioningChecksOwnManualPhases_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("matched") << 0;
    QTest::newRow("newer-draft") << 1;
    QTest::newRow("rejected") << 2;
    QTest::newRow("closed") << 3;
}
void WalletQmlControllerTests::provisioningChecksOwnManualPhases()
{
    QFETCH(int, scenario);
    ManualExecutionEnvironment environment;
    MockNode node; FakeWalletLoader loader;
    node.wallet_loader_fn = [&]() -> interfaces::WalletLoader& { return loader; };
    loader.wallet_dir_entries = {{"taken", "sqlite"}};
    WalletProvisioningCheck check{node, {environment.scheduler, [&](auto turn) { environment.post(std::move(turn)); }}};
    check.setXpub(VALID_XPUB);
    environment.drain(); environment.publishAll();
    QVERIFY(check.ready()); QVERIFY(check.xpubValid());
    const auto before = loader.list_wallet_dir_calls;
    if (scenario == 2) environment.rejectNext(true);
    check.setName("taken");
    QCOMPARE(loader.list_wallet_dir_calls, before); QVERIFY(check.pending()); QVERIFY(!check.ready());
    environment.drain();
    QVERIFY(check.pending()); QVERIFY(!check.xpubValid());
    if (scenario == 1) { check.setName("different"); check.setXpub("garbage"); }
    if (scenario == 3) {
        QSignalSpy changes{&check, &WalletProvisioningCheck::changed};
        check.closeAdmission(); QCOMPARE(changes.size(), 0);
        environment.publishAll(); QCOMPARE(changes.size(), 0);
    } else {
        environment.drain(); environment.publishAll();
        if (scenario == 2) { QVERIFY(!check.ready()); QVERIFY(!check.error().isEmpty()); QVERIFY(!check.xpubValid()); }
        else if (scenario == 1) { QVERIFY(check.ready()); QVERIFY(check.nameError().isEmpty()); QVERIFY(!check.xpubValid()); QCOMPARE(check.name(), QStringLiteral("different")); }
        else { QVERIFY(check.ready()); QCOMPARE(check.nameError(), QStringLiteral("A wallet with this name already exists")); QVERIFY(check.xpubValid()); }
    }
    check.closeAdmission(); environment.drain(); environment.publishAll(); QVERIFY(environment.quiescent());
}

void WalletQmlControllerTests::validateXpubAcceptsValidKey()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(CheckedXpub(controller, VALID_XPUB));
}

void WalletQmlControllerTests::validateXpubRejectsGarbage()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(!CheckedXpub(controller, "not-an-xpub"));
}

void WalletQmlControllerTests::validateXpubRejectsEmpty()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(!CheckedXpub(controller, ""));
}

void WalletQmlControllerTests::validateXpubTrimsWhitespace()
{
    MockNode node;
    DrainingWalletController controller(node);

    QVERIFY(CheckedXpub(controller, "  " + VALID_XPUB + "  "));
}

void WalletQmlControllerTests::createWatchOnlyInvalidXpubSetsError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy error_spy(&controller, &WalletQmlController::walletLoadErrorChanged);
    controller.createWatchOnlyWallet("test_wallet", "garbage");
    QVERIFY(AwaitWalletOperation(controller));

    QCOMPARE(controller.walletLoadError(), QString{"Invalid extended public key."});
    QVERIFY(!controller.isWalletLoaded());
    QCOMPARE(error_spy.count(), 1);
}

void WalletQmlControllerTests::createWatchOnlyCleansUpWhenDescriptorImportFails()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        // Mimic the real WalletLoader: NotifyWalletLoaded fires synchronously
        // with the new wallet before createWallet returns. The controller's
        // defer-adoption intercept should stash this wallet instead of
        // publishing a model for it.
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>(name, &wallet_state));
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &wallet_state)};
    };

    QSignalSpy create_spy(&controller, &WalletQmlController::walletCreateSucceeded);
    QSignalSpy error_spy(&controller, &WalletQmlController::walletLoadErrorChanged);

    // The public descriptor import reports failure, so reject the incomplete wallet,
    // call remove() on it, and surface a load error without registering a model.
    controller.createWatchOnlyWallet("watch_wallet", VALID_XPUB);
    QVERIFY(AwaitWalletOperation(controller));

    QTRY_COMPARE_WITH_TIMEOUT(error_spy.count(), 1, 5000);
    QVERIFY(!controller.walletLoadError().isEmpty());
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationIncomplete);
    QCOMPARE(controller.walletOperationName(), QStringLiteral("watch_wallet"));
    QCOMPARE(wallet_state.remove_calls.load(), 1);
    QCOMPARE(create_spy.count(), 0);
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::createWatchOnlyUsesPublicDescriptorImport_data()
{
    QTest::addColumn<int>("failure");
    QTest::newRow("both-imported") << -1;
    QTest::newRow("external-fails") << 0;
    QTest::newRow("internal-fails") << 1;
    QTest::newRow("missing-result") << 2;
}

void WalletQmlControllerTests::createWatchOnlyUsesPublicDescriptorImport()
{
    QFETCH(int, failure);
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    std::vector<wallet::ImportDescriptorRequest> captured;
    std::atomic_bool off_gui{false};
    state.import_fn = [&](auto& requests) {
        off_gui = QThread::currentThread() != qApp->thread();
        { std::lock_guard lock{state.mutex}; captured = requests; }
        std::vector<wallet::ImportResult> results(failure == 2 ? 1 : 2);
        if (failure == 0 || failure == 1) results[failure] = wallet::ImportResult{wallet::WalletErrorCode::GenericError, "descriptor rejected"};
        return results;
    };
    loader.create_wallet_fn = [&](const std::string& name, const SecureString&, uint64_t, std::vector<bilingual_str>&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        auto result = std::make_unique<FakeWallet>(name, &state);
        result->private_keys_disabled = true;
        return std::unique_ptr<interfaces::Wallet>{std::move(result)};
    };
    QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    controller.createWatchOnlyWallet("public_watch", VALID_XPUB);
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_VERIFY(!controller.walletLoadInProgress());
    QCOMPARE(state.imports.load(), 1);
    QCOMPARE(state.private_accesses.load(), 0);
    QVERIFY(off_gui.load());
    {
        std::lock_guard lock{state.mutex};
        QCOMPARE(captured.size(), size_t{2});
        for (size_t index = 0; index < captured.size(); ++index) {
            const auto& request = captured[index];
            QVERIFY(request.active);
            QVERIFY(request.internal.has_value());
            QCOMPARE(*request.internal, index == 1);
            QVERIFY(!request.timestamp);
            QCOMPARE(request.next_index, std::optional<int64_t>{0});
            QVERIFY(request.descriptor.find(index ? "/1/*)#" : "/0/*)#") != std::string::npos);
            QCOMPARE(request.descriptor.substr(request.descriptor.size() - 8), GetDescriptorChecksum(request.descriptor));
        }
    }
    QCOMPARE(created.count(), failure < 0 ? 1 : 0);
    QCOMPARE(state.remove_calls.load(), failure < 0 ? 0 : 1);
    QCOMPARE(controller.walletLoadError().isEmpty(), failure < 0);
}

void WalletQmlControllerTests::externalSignerCreationRequiresConfiguredPath()
{
    MockNode node;
    ConfigureSignerRpc(node);
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{}; };
    node.list_external_signers_fn = [] { return MakeSigners({"Ledger Nano X"}); };
    node.ExpectExactly(node.calls.listExternalSigners, 0);

    DrainingWalletController controller(node);
    controller.refreshExternalSignerStatus();
    QTRY_VERIFY(!controller.externalSignerPending());

    QVERIFY(!controller.canCreateExternalSignerWallet());
    QCOMPARE(controller.externalSignerName(), QString{});
    QCOMPARE(controller.suggestedExternalSignerWalletName(), QString("external_signer"));
    QCOMPARE(node.calls.executeRpc.load(), 0);
}

void WalletQmlControllerTests::externalSignerCreationRequiresExactlyOneSigner()
{
    MockNode node;
    ConfigureSignerRpc(node);
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.get_persistent_setting_fn = [](const std::string& name) {
        return name == "signer" ? common::SettingsValue{QCoreApplication::applicationFilePath().toStdString()} : common::SettingsValue{};
    };
    node.list_external_signers_fn = [] { return MakeSigners({"Signer A", "Signer B"}); };
    node.ExpectExactly(node.calls.listExternalSigners, 0);

    DrainingWalletController controller(node);
    controller.refreshExternalSignerStatus();
    QTRY_VERIFY(!controller.externalSignerPending());

    QVERIFY(!controller.canCreateExternalSignerWallet());
    QVERIFY(controller.externalSignerName().isEmpty());
    QCOMPARE(controller.externalSignerError(), QString("More than one external signer was found. Connect only one device."));
}

void WalletQmlControllerTests::externalSignerSuggestionUsesSignerName()
{
    MockNode node;
    ConfigureSignerRpc(node);
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.get_persistent_setting_fn = [](const std::string& name) {
        return name == "signer" ? common::SettingsValue{QCoreApplication::applicationFilePath().toStdString()} : common::SettingsValue{};
    };
    node.list_external_signers_fn = [] { return MakeSigners({"Coldcard Mk4"}); };
    node.ExpectExactly(node.calls.listExternalSigners, 0);

    DrainingWalletController controller(node);
    controller.refreshExternalSignerStatus();
    QTRY_VERIFY(!controller.externalSignerPending());

    QVERIFY(controller.canCreateExternalSignerWallet());
    QCOMPARE(controller.externalSignerName(), QString("Coldcard Mk4"));
    QCOMPARE(controller.suggestedExternalSignerWalletName(), QString("Coldcard_Mk4"));
}

void WalletQmlControllerTests::manualExternalSignerCreationRejectsUnavailableSnapshot_data()
{
    QTest::addColumn<int>("snapshot");
    QTest::addColumn<QString>("expected_error");
    QTest::newRow("pending") << 0 << QStringLiteral("Wait for the external signer check to finish.");
    QTest::newRow("missing-path") << 1 << QStringLiteral("Set an external signer path in Wallet settings first.");
    QTest::newRow("no-device") << 2 << QStringLiteral("No external signer is currently detected.");
    QTest::newRow("multiple-devices") << 3 << QStringLiteral("More than one external signer was found. Connect only one device.");
    QTest::newRow("invalid-command-output") << 4 << QStringLiteral("invalid signer output");
    QTest::newRow("oversized-device-name") << 5 << QStringLiteral("The signer response has invalid identity or name fields.");
}

void WalletQmlControllerTests::manualExternalSignerCreationRejectsUnavailableSnapshot()
{
    QFETCH(int, snapshot);
    QFETCH(QString, expected_error);
    MockNode node;
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    node.ExpectExactly(node.calls.forceSetting, 0);
    node.ExpectExactly(node.calls.listExternalSigners, 0);
    node.get_persistent_setting_fn = [snapshot](const std::string& name) {
        return name == "signer" && snapshot != 1 ? common::SettingsValue{QCoreApplication::applicationFilePath().toStdString()} : common::SettingsValue{};
    };
    node.list_external_signers_fn = [snapshot]() -> std::vector<std::unique_ptr<interfaces::ExternalSigner>> {
        if (snapshot == 2) return {};
        if (snapshot == 3) return MakeSigners({"Device A", "Device B"});
        if (snapshot == 4) throw std::runtime_error{"invalid signer output"};
        if (snapshot == 5) {
            std::vector<std::unique_ptr<interfaces::ExternalSigner>> signers;
            signers.push_back(std::make_unique<FakeExternalSigner>(std::string(32'769, 'x')));
            return signers;
        }
        return MakeSigners({"Device A"});
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && !controller.externalSignerPending(); }));
    if (snapshot == 0) controller.refreshExternalSignerStatus();
    QVERIFY(!controller.canCreateExternalSignerWallet());
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    const auto ticket = controller.walletOperationId();
    QVERIFY(!controller.createExternalSignerWallet("device_wallet"));
    QCOMPARE(controller.walletLoadError(), expected_error);
    QCOMPARE(controller.walletOperationId(), ticket);
    QCOMPARE(loader.create_wallet_calls, 0);
    QCOMPARE(loaded.size(), 0);
    QCOMPARE(created.size(), 0);
    QVERIFY(graph.until([&] { return !controller.externalSignerPending(); }));
    QCOMPARE(loader.create_wallet_calls, 0);
    QVERIFY(!controller.isWalletOpen("device_wallet"));
}

void WalletQmlControllerTests::manualExternalSignerCreationOwnsOriginalOutcome_data()
{
    QTest::addColumn<int>("outcome");
    QTest::addColumn<bool>("change_selection");
    for (bool change_selection : {false, true}) {
        for (int outcome = 0; outcome < 5; ++outcome) {
            const QByteArray tag = QByteArray(change_selection ? "new-selection-" : "original-selection-") +
                std::array<const char*, 5>{"created", "loader-error", "loader-throws", "empty-name", "existing-name"}[outcome];
            QTest::newRow(tag.constData()) << outcome << change_selection;
        }
    }
}

void WalletQmlControllerTests::manualExternalSignerCreationOwnsOriginalOutcome()
{
    QFETCH(int, outcome);
    QFETCH(bool, change_selection);
    FakeWallet::State original_state, standby_state, created_state;
    MockNode node;
    FakeWalletLoader loader;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    loader.wallet_dir = directory.path().toStdString();
    if (outcome == 4) loader.wallet_dir_entries = {{"device_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);
    node.get_persistent_setting_fn = [](const std::string& name) {
        return name == "signer" ? common::SettingsValue{QCoreApplication::applicationFilePath().toStdString()} : common::SettingsValue{};
    };
    node.list_external_signers_fn = [] { return MakeSigners({"Device A"}); };
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.push_back(std::make_unique<FakeWallet>("original", &original_state));
        wallets.push_back(std::make_unique<FakeWallet>("standby", &standby_state));
        return wallets;
    };
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && !controller.externalSignerPending() && controller.walletForName("standby"); }));
    QVERIFY(controller.canCreateExternalSignerWallet());
    auto* original = controller.walletForName("original");
    auto* standby = controller.walletForName("standby");
    QCOMPARE(controller.selectedWallet(), original);
    std::string created_name;
    uint64_t creation_flags{0};
    bool empty_passphrase{false}, created_on_core{false};
    loader.create_wallet_fn = [&](const std::string& name, const SecureString& passphrase, uint64_t flags,
                                  std::vector<bilingual_str>& warnings) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        created_name = name;
        creation_flags = flags;
        empty_passphrase = passphrase.empty();
        created_on_core = graph.lane.load() == int(execution::Lane::Core);
        if (outcome == 1) return util::Error{Untranslated("Device refused wallet creation")};
        if (outcome == 2) throw std::runtime_error{"device outcome unknown"};
        warnings.push_back(Untranslated("Keep the device recovery backup"));
        auto wallet = std::make_unique<FakeWallet>(name, &created_state);
        wallet->private_keys_disabled = true;
        wallet->external_signer = true;
        return std::unique_ptr<interfaces::Wallet>{std::move(wallet)};
    };
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    QSignalSpy created{&controller, &WalletQmlController::walletCreateSucceeded};
    QVERIFY(controller.createExternalSignerWallet(outcome == 3 ? QStringLiteral(" \t ") : QStringLiteral("  device_wallet  ")));
    const auto ticket = controller.walletOperationId();
    QVERIFY(ticket > 0);
    QVERIFY(controller.walletLoadInProgress());
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
    QCOMPARE(loader.create_wallet_calls, 0);
    QVERIFY(!controller.createExternalSignerWallet("duplicate"));
    QCOMPARE(controller.walletOperationId(), ticket);
    if (change_selection) QVERIFY(controller.selectWallet(standby));
    graph.runNative();
    QCOMPARE(loader.create_wallet_calls, outcome < 3 ? 1 : 0);
    QCOMPARE(loaded.size(), 0);
    QCOMPARE(created.size(), 0);
    QCOMPARE(controller.selectedWallet(), change_selection ? standby : original);
    QVERIFY(controller.walletLoadInProgress());
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress() && controller.walletOperationState() != WalletQmlController::WalletOperationState::WalletOperationPending; }));
    QCOMPARE(controller.walletOperationId(), ticket);
    QCOMPARE(created.size(), 0);
    QCOMPARE(created_state.private_accesses.load(), 0);
    QCOMPARE(created_state.remove_calls.load(), 0);
    if (outcome < 3) {
        QCOMPARE(created_name, std::string{"device_wallet"});
        QCOMPARE(creation_flags, uint64_t{wallet::WALLET_FLAG_DESCRIPTORS | wallet::WALLET_FLAG_DISABLE_PRIVATE_KEYS | wallet::WALLET_FLAG_EXTERNAL_SIGNER});
        QVERIFY(empty_passphrase);
        QVERIFY(created_on_core);
    }
    if (outcome == 0) {
        auto* wallet = controller.walletForName("device_wallet");
        QVERIFY(wallet);
        QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
        QCOMPARE(controller.walletOperationName(), QStringLiteral("device_wallet"));
        QCOMPARE(loaded.size(), 1);
        QCOMPARE(qvariant_cast<WalletQmlModel*>(loaded.front().front()), wallet);
        QCOMPARE(controller.selectedWallet(), change_selection ? standby : original);
        QCOMPARE(controller.walletOperationResultWallet(), wallet);
        QVERIFY(controller.canOpenResult());
        if (!change_selection) QVERIFY(controller.selectWallet(controller.walletOperationResultWallet()));
        QCOMPARE(controller.selectedWallet(), change_selection ? standby : wallet);
        QCOMPARE(controller.walletLoadWarnings(), QStringLiteral("Keep the device recovery backup"));
        QVERIFY(controller.walletLoadError().isEmpty());
    } else {
        QCOMPARE(loaded.size(), 0);
        QCOMPARE(controller.walletOperationState(), outcome == 2 ? WalletQmlController::WalletOperationState::WalletOperationUncertain : WalletQmlController::WalletOperationState::WalletOperationFailed);
        QCOMPARE(controller.selectedWallet(), change_selection ? standby : original);
        QVERIFY(!controller.isWalletOpen("device_wallet"));
        if (outcome == 1) QCOMPARE(controller.walletLoadError(), QStringLiteral("Device refused wallet creation"));
        else if (outcome == 2) QCOMPARE(controller.walletLoadError(), QStringLiteral("Wallet creation outcome is uncertain. Check the wallet directory before retrying."));
        else if (outcome == 3) QCOMPARE(controller.walletLoadError(), QStringLiteral("Enter a wallet name."));
        else QCOMPARE(controller.walletLoadError(), QStringLiteral("A wallet with this name already exists"));
    }
}

void WalletQmlControllerTests::manualManagedWalletPathsResolveThroughPublicLoader_data()
{
    QTest::addColumn<int>("path_kind");
    QTest::addColumn<bool>("load_expected");
    QTest::addColumn<bool>("migration_expected");
    QTest::newRow("listed-name") << 0 << true << false;
    QTest::newRow("nested-directory") << 1 << true << false;
    QTest::newRow("nested-wallet-dat") << 2 << true << false;
    QTest::newRow("wallet-directory-itself") << 3 << true << false;
    QTest::newRow("top-level-wallet-dat") << 4 << true << false;
    QTest::newRow("top-level-data-file") << 5 << true << false;
    QTest::newRow("external-directory") << 6 << true << false;
    QTest::newRow("external-wallet-dat") << 7 << false << false;
    QTest::newRow("backup-file") << 8 << false << false;
    QTest::newRow("missing-path") << 9 << false << false;
    QTest::newRow("empty-path") << 10 << false << false;
    QTest::newRow("listed-legacy") << 11 << false << true;
    QTest::newRow("inferred-legacy") << 12 << false << true;
    QTest::newRow("local-file-url") << 13 << true << false;
    QTest::newRow("cleaned-listed-name") << 14 << true << false;
}

void WalletQmlControllerTests::manualManagedWalletPathsResolveThroughPublicLoader()
{
    QFETCH(int, path_kind);
    QFETCH(bool, load_expected);
    QFETCH(bool, migration_expected);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QDir root{directory.path()};
    QVERIFY(root.mkpath("wallets/subdir/nested"));
    QVERIFY(root.mkpath("wallets/subdir/legacy"));
    QVERIFY(root.mkpath("outside"));
    for (const auto* name : {"wallets/subdir/nested/wallet.dat", "wallets/wallet.dat", "wallets/oldfile.dat", "outside/wallet.dat", "wallets/backup.bak"}) {
        QFile file{root.filePath(QLatin1String{name})};
        QVERIFY(file.open(QIODevice::WriteOnly));
        QCOMPARE(file.write("wallet fixture"), qint64{14});
    }
    const QString wallet_dir = root.filePath(QStringLiteral("wallets"));
    const QString nested_dir = root.filePath(QStringLiteral("wallets/subdir/nested"));
    const QString legacy_dir = root.filePath(QStringLiteral("wallets/subdir/legacy"));
    const QString nested_file = nested_dir + QStringLiteral("/wallet.dat");
    const std::array<QString, 15> paths{
        QStringLiteral("listed"), nested_dir, nested_file, wallet_dir, wallet_dir + QStringLiteral("/wallet.dat"),
        wallet_dir + QStringLiteral("/oldfile.dat"), root.filePath(QStringLiteral("outside")), root.filePath(QStringLiteral("outside/wallet.dat")),
        wallet_dir + QStringLiteral("/backup.bak"), wallet_dir + QStringLiteral("/absent"), QString{}, QStringLiteral("legacy"), legacy_dir,
        QUrl::fromLocalFile(nested_file).toString(), QStringLiteral("subdir/../listed")};
    const std::array<QString, 15> references{
        QStringLiteral("listed"), QStringLiteral("subdir/nested"), QStringLiteral("subdir/nested"), wallet_dir, wallet_dir,
        QStringLiteral("oldfile.dat"), root.filePath(QStringLiteral("outside")), QString{}, QString{}, QString{}, QString{}, QStringLiteral("legacy"),
        QStringLiteral("subdir/legacy"), QStringLiteral("subdir/nested"), QStringLiteral("listed")};
    MockNode node;
    FakeWalletLoader loader;
    FakeWallet::State state;
    loader.wallet_dir = wallet_dir.toStdString();
    loader.wallet_dir_entries = {{"listed", "sqlite"}, {"subdir/nested", "sqlite"}, {"legacy", "bdb"}, {"subdir/legacy", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);
    ManualControllerGraph graph{node};
    auto& controller = *graph.controller;
    controller.initialize();
    QVERIFY(graph.until([&] { return controller.initialized() && !controller.externalSignerPending(); }));
    std::string loaded_reference;
    bool loaded_on_core{false};
    int restores{0};
    loader.load_wallet_fn = [&](const std::string& name, std::vector<bilingual_str>&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        loaded_reference = name;
        loaded_on_core = graph.lane.load() == int(execution::Lane::Core);
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>("resolved_wallet", &state)};
    };
    loader.restore_wallet_fn = [&]() -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        ++restores;
        return util::Error{Untranslated("Opening must not restore a backup")};
    };
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    QSignalSpy migration{&controller, &WalletQmlController::walletMigrationRequired};
    controller.setSelectedWallet(paths[path_kind]);
    QCOMPARE(loader.load_wallet_calls, 0);
    QCOMPARE(loaded.size(), 0);
    QVERIFY(controller.walletLoadInProgress());
    graph.runNative();
    QCOMPARE(loader.load_wallet_calls, load_expected ? 1 : 0);
    QCOMPARE(restores, 0);
    QCOMPARE(loaded.size(), 0);
    QCOMPARE(migration.size(), 0);
    QVERIFY(graph.until([&] { return !controller.walletLoadInProgress(); }));
    QCOMPARE(state.private_accesses.load(), 0);
    QCOMPARE(loaded.size(), load_expected ? 1 : 0);
    QCOMPARE(migration.size(), migration_expected ? 1 : 0);
    if (load_expected) {
        QCOMPARE(loaded_reference, references[path_kind].toStdString());
        QVERIFY(loaded_on_core);
        QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationSucceeded);
        QCOMPARE(controller.walletOperationName(), QStringLiteral("resolved_wallet"));
        QCOMPARE(controller.selectedWallet()->name(), QString{});
        QCOMPARE(controller.walletOperationResultWallet(), controller.walletForName("resolved_wallet"));
        QVERIFY(controller.canOpenResult());
        QVERIFY(controller.selectWallet(controller.walletOperationResultWallet()));
        QCOMPARE(controller.selectedWallet(), controller.walletForName("resolved_wallet"));
        QCOMPARE(qvariant_cast<WalletQmlModel*>(loaded.front().front()), controller.selectedWallet());
        QVERIFY(controller.walletLoadError().isEmpty());
    } else if (migration_expected) {
        QCOMPARE(migration.front().front().toString(), references[path_kind]);
        QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationNeedsMigration);
        QVERIFY(controller.walletLoadError().isEmpty());
        QVERIFY(!controller.isWalletLoaded());
    } else {
        QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationFailed);
        QCOMPARE(controller.walletLoadError(), path_kind == 10 ? QStringLiteral("Choose a wallet to open.") : QStringLiteral("The selected wallet is not available in the wallet directory."));
        QVERIFY(!controller.isWalletLoaded());
    }
}

void WalletQmlControllerTests::signerDiscoveryUsesDefaultNativeWorker()
{
    StrictMockNode node;
    ConfigureSignerRpc(node);
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    const auto gui = QThread::currentThread();
    std::atomic_bool setting_native{false}, force_native{false}, discovery_native{false};
    std::atomic<int> unexpected_settings{0};
    node.get_persistent_setting_fn = [&](const std::string& name) {
        setting_native = QThread::currentThread() != gui;
        if (name != "signer") ++unexpected_settings;
        return common::SettingsValue{QCoreApplication::applicationFilePath().toStdString()};
    };
    node.force_setting_fn = [&](const std::string&, const common::SettingsValue&) {
        force_native = QThread::currentThread() != gui;
    };
    node.list_external_signers_fn = [&] {
        discovery_native = QThread::currentThread() != gui;
        return MakeSigners({"Native signer"});
    };
    // One independent metadata read plus discovery's before/after fence.
    node.ExpectExactly(node.calls.getPersistentSetting, 3);
    node.ExpectExactly(node.calls.forceSetting, 0);
    node.ExpectExactly(node.calls.listExternalSigners, 0);
    DrainingWalletController controller{node};
    controller.refreshExternalSignerStatus();
    QTRY_VERIFY(!controller.externalSignerPending());
    QVERIFY(setting_native.load());
    QVERIFY(!force_native.load());
    QCOMPARE(node.calls.executeRpc.load(), 1);
    QVERIFY(discovery_native.load());
    QCOMPARE(unexpected_settings.load(), 0);
    QVERIFY(controller.canCreateExternalSignerWallet());
    QCOMPARE(controller.externalSignerName(), QStringLiteral("Native signer"));
    QCOMPARE(controller.suggestedExternalSignerWalletName(), QStringLiteral("Native_signer"));
}

void WalletQmlControllerTests::signerDiscoveryKeepsNewerIntentAcrossHeldPublication()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> gui;
    MockNode node;
    ConfigureSignerRpc(node);
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{QCoreApplication::applicationFilePath().toStdString()}; };
    int reads{0};
    node.list_external_signers_fn = [&] { return MakeSigners({++reads == 1 ? "First" : "Latest"}); };
    DrainingWalletController controller(node);
    controller.setExternalSignerServices({env.scheduler, [&](std::function<void()> callback) { gui.push_back(std::move(callback)); }});
    const auto publish = [&] { auto callback = std::move(gui.front()); gui.pop_front(); callback(); };
    controller.refreshExternalSignerStatus();
    QVERIFY(controller.externalSignerPending());
    QVERIFY(!controller.canCreateExternalSignerWallet());
    QCOMPARE(reads, 0);
    QVERIFY(env.step(execution::Lane::Domain));
    QCOMPARE(gui.size(), size_t{1});
    publish(); // Acknowledge the new explicit Reading phase.
    QVERIFY(controller.externalSignerPending());
    QVERIFY(env.step(execution::Lane::Core));
    QCOMPARE(reads, 1);
    QVERIFY(gui.empty());
    controller.refreshExternalSignerStatus();
    while (env.step(execution::Lane::Domain)) {}
    QCOMPARE(gui.size(), size_t{1});
    publish();
    QVERIFY(controller.externalSignerPending());
    QVERIFY(controller.externalSignerName().isEmpty());
    QVERIFY(env.step(execution::Lane::Core));
    QCOMPARE(reads, 2);
    while (env.step(execution::Lane::Domain)) {}
    QCOMPARE(gui.size(), size_t{1});
    publish();
    QVERIFY(!controller.externalSignerPending());
    QCOMPARE(controller.externalSignerName(), QStringLiteral("Latest"));
    QVERIFY(controller.canCreateExternalSignerWallet());
    env.drain();
}

void WalletQmlControllerTests::signerDiscoveryRejectsPublicationAfterQuietClose()
{
    ManualRoutedExecution env;
    std::deque<std::function<void()>> gui;
    MockNode node;
    ConfigureSignerRpc(node);
    node.get_persistent_setting_fn = [](const std::string&) { return common::SettingsValue{QCoreApplication::applicationFilePath().toStdString()}; };
    node.list_external_signers_fn = [] { return MakeSigners({"Late"}); };
    DrainingWalletController controller(node);
    controller.setExternalSignerServices({env.scheduler, [&](std::function<void()> callback) { gui.push_back(std::move(callback)); }});
    controller.refreshExternalSignerStatus();
    QVERIFY(env.step(execution::Lane::Domain));
    QVERIFY(env.step(execution::Lane::Core));
    while (env.step(execution::Lane::Domain)) {}
    QCOMPARE(gui.size(), size_t{1});
    controller.closeAdmission();
    QSignalSpy changed{&controller, &WalletQmlController::externalSignerStatusChanged};
    while (!gui.empty()) { gui.front()(); gui.pop_front(); }
    QCOMPARE(changed.count(), 0);
    QVERIFY(controller.externalSignerName().isEmpty());
    QVERIFY(!controller.canCreateExternalSignerWallet());
    env.drain();
}

void WalletQmlControllerTests::initializedControllerSignalsMigrationForLegacyWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy migration_spy(&controller, &WalletQmlController::walletMigrationRequired);

    controller.setSelectedWallet("legacy_wallet", "bdb");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(migration_spy.count(), 1);
    QCOMPARE(migration_spy.takeFirst().at(0).toString(), QString{"legacy_wallet"});
    QCOMPARE(loader.load_wallet_calls, 0);
    QVERIFY(!controller.walletLoadInProgress());
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::createWalletBeforeInitializationReturnsFalseAndSetsError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.createSingleSigWallet("test_wallet", "secret");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletCreateError(), QString{NOT_INITIALIZED_ERROR});
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::importWalletBeforeInitializationSetsLoadError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.importWallet("/tmp/test_wallet.dat");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
}

void WalletQmlControllerTests::migrateWalletBeforeInitializationSetsMigrationError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.migrateWallet("legacy_wallet", "secret");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletMigrationError(), QString{NOT_INITIALIZED_ERROR});
}

void WalletQmlControllerTests::selectWalletBeforeInitializationSetsLoadError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.setSelectedWallet("test_wallet");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
}

void WalletQmlControllerTests::initializedControllerPropagatesCreateErrors()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QVERIFY(controller.initialized());

    bool saw_expected_passphrase{false};
    bool saw_expected_flags{false};
    loader.create_wallet_fn = [&](const std::string&,
                                  const SecureString& passphrase,
                                  uint64_t wallet_creation_flags,
                                  std::vector<bilingual_str>&) {
        saw_expected_passphrase = (passphrase == SecureString{"secret"});
        saw_expected_flags = (wallet_creation_flags == wallet::WALLET_FLAG_DESCRIPTORS);
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };

    QSignalSpy error_spy(&controller, &WalletQmlController::walletCreateErrorChanged);
    controller.createSingleSigWallet("test_wallet", "secret");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(error_spy.count(), 1);
    QVERIFY(saw_expected_passphrase);
    QVERIFY(saw_expected_flags);
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::capturedSecretSurvivesQueueDelayAndQuietClose_data()
{
    QTest::addColumn<bool>("migration");QTest::newRow("create")<<false;QTest::newRow("migrate")<<true;
}
void WalletQmlControllerTests::capturedSecretSurvivesQueueDelayAndQuietClose()
{
    QFETCH(bool, migration);
    if (!migration) {
        MockNode node; FakeWalletLoader loader; ConfigureExpectedControllerInitialization(node, loader);
        ManualControllerGraph graph{node}; auto& controller = *graph.controller;
        controller.initialize(); QVERIFY(graph.until([&] { return controller.initialized(); }));
        const std::string expected(40'000, 's'); bool exact{false}, native{false};
        loader.create_wallet_fn = [&](const auto&, const SecureString& password, uint64_t, auto&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
            exact = std::string{password.begin(), password.end()} == expected; native = graph.lane.load() == int(execution::Lane::Core);
            return util::Error{Untranslated("Captured creation reached Native")};
        };
        QString source = QString::fromStdString(expected); controller.createSingleSigWallet("captured-wallet", source);
        auto* operation = controller.creationOperation(); QVERIFY(operation); QVERIFY(DriveCreationToCore(graph, *operation));
        auto original = operation->m_owner; QVERIFY(original);
        // Creation now has an explicit validation phase. Hold the accepted
        // Core mutation, not its earlier cancellable validation read.
        source.fill(QLatin1Char('z')); controller.closeAdmission(); QCOMPARE(loader.create_wallet_calls, 0);
        QVERIFY(graph.until([&] { return walletcreation::Terminal(original->snapshot().phase); }));
        QVERIFY(exact); QVERIFY(native); QCOMPARE(loader.create_wallet_calls, 1);
        QCOMPARE(original->errorText(), QStringLiteral("Captured creation reached Native"));
        QCOMPARE(original->snapshot().phase, walletcreation::Phase::Failed);
        original.reset();
        return;
    }
    StrictMockNode node; [[maybe_unused]] auto verify_node = node.VerifyOnExit(); FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader); loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    DrainingWalletController controller{node}; controller.initialize(); QTRY_VERIFY(controller.initialized());
    struct Probe { QSemaphore entered, release; std::atomic_bool exact{false}; std::atomic<QThread*> native{nullptr}; };
    const auto probe = std::make_shared<Probe>(); const auto unblock = qScopeGuard([probe] { probe->release.release(); });
    const std::string expected(40'000, 's');
    loader.migrate_wallet_fn = [probe, expected](const auto&, const SecureString& password, bool) -> util::Result<interfaces::WalletMigrationResult> {
        probe->entered.release(); probe->release.acquire();
        probe->exact = std::string{password.begin(), password.end()} == expected; probe->native = QThread::currentThread();
        return util::Error{Untranslated("Captured migration reached Native")};
    };
    QString source = QString::fromStdString(expected); controller.migrateWallet("legacy_wallet", source);
    auto* operation = controller.creationOperation(); QVERIFY(operation);
    auto original = operation->m_owner; QVERIFY(original);
    QTRY_VERIFY(probe->entered.available() > 0);
    source.fill(QLatin1Char('z')); controller.closeAdmission(); QCOMPARE(loader.migrate_wallet_calls, 1);
    probe->release.release(); QTRY_VERIFY(walletcreation::Terminal(original->snapshot().phase));
    QVERIFY(probe->exact.load()); QVERIFY(probe->native.load()); QVERIFY(probe->native.load() != QThread::currentThread());
    QCOMPARE(loader.migrate_wallet_calls, 1); QVERIFY(original->errorText().contains(QStringLiteral("reached Native"))); original.reset();
}
void WalletQmlControllerTests::capturedSecretTransportRefusalDoesNotCallNativeLoader_data()
{
    QTest::addColumn<int>("input");QTest::newRow("raw-name")<<1;QTest::newRow("raw-migration-path")<<2;
}
void WalletQmlControllerTests::capturedSecretTransportRefusalDoesNotCallNativeLoader()
{
    QFETCH(int,input);StrictMockNode node;[[maybe_unused]] auto verify_node=node.VerifyOnExit();FakeWalletLoader loader;ConfigureExpectedControllerInitialization(node,loader);
    DrainingWalletController controller{node};controller.initialize();QTRY_VERIFY(controller.initialized());const auto directory_reads=loader.list_wallet_dir_calls;
    if(input==1)controller.createSingleSigWallet(QString(150'000,QLatin1Char('n')),"secret");
    if(input==2)controller.migrateWallet(QString(150'000,QLatin1Char('p')),"secret");
    QVERIFY(AwaitWalletOperation(controller));QCOMPARE(loader.create_wallet_calls,0);QCOMPARE(loader.migrate_wallet_calls,0);QCOMPARE(loader.list_wallet_dir_calls,directory_reads);
    QVERIFY(input==2?!controller.walletMigrationError().isEmpty():!controller.walletCreateError().isEmpty());QCOMPARE(controller.walletOperationState(),WalletQmlController::WalletOperationState::WalletOperationFailed);
}
void WalletQmlControllerTests::initializedControllerForwardsMigrationPassphrase_data()
{
    QTest::addColumn<QString>("original_password");QTest::newRow("native-small-literal")<<QStringLiteral("secret");QTest::newRow("valid-40000-character-literal")<<QString(40'000,QLatin1Char('m'));
}
void WalletQmlControllerTests::initializedControllerForwardsMigrationPassphrase()
{
    QFETCH(QString,original_password);const auto expected_password=original_password.toUtf8().toStdString();std::atomic<QThread*> worker{nullptr};
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy failed_spy(&controller, &WalletQmlController::walletMigrationFailed);

    bool saw_expected_name{false};
    bool saw_expected_passphrase{false};
    loader.migrate_wallet_fn = [&](const std::string& name, const SecureString& passphrase, bool) {
        saw_expected_name = (name == "legacy_wallet");
        saw_expected_passphrase = (std::string{passphrase.begin(),passphrase.end()} == expected_password);worker=QThread::currentThread();
        return util::Result<interfaces::WalletMigrationResult>{
            util::Error{Untranslated("Migration failed.")}};
    };

    controller.migrateWallet("legacy_wallet", original_password);
    original_password.fill(QLatin1Char('z'));
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_VERIFY(!failed_spy.empty());
    QVERIFY(worker.load());QVERIFY(worker.load()!=QThread::currentThread());
    QVERIFY(saw_expected_name);
    QVERIFY(saw_expected_passphrase);
    QCOMPARE(loader.migrate_wallet_calls, 1);
    QCOMPARE(controller.walletMigrationError(), QString{"Migration failed."});
    QVERIFY(!controller.walletMigrationInProgress());
}

void WalletQmlControllerTests::initializedControllerForwardsUtf8CreatePassphrase_data()
{
    QTest::addColumn<QString>("original_password");QTest::newRow("utf8-literal")<<QString::fromUtf8("pässwörd-₿");QTest::newRow("valid-40000-character-literal")<<QString(40'000,QLatin1Char('c'));
}
void WalletQmlControllerTests::initializedControllerForwardsUtf8CreatePassphrase()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QFETCH(QString,original_password);const std::string expected_passphrase{original_password.toUtf8().toStdString()};std::atomic<QThread*> worker{nullptr};

    bool saw_expected_passphrase{false};
    loader.create_wallet_fn = [&](const std::string&,
                                  const SecureString& passphrase,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        saw_expected_passphrase = (std::string{passphrase.begin(), passphrase.end()} == expected_passphrase);worker=QThread::currentThread();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };

    QSignalSpy error_spy(&controller, &WalletQmlController::walletCreateErrorChanged);
    controller.createSingleSigWallet("test_wallet", original_password);
    original_password.fill(QLatin1Char('z'));
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_VERIFY(!error_spy.empty());
    QVERIFY(worker.load());QVERIFY(worker.load()!=QThread::currentThread());
    QVERIFY(saw_expected_passphrase);
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
}

void WalletQmlControllerTests::initializedControllerEmitsWalletCreateSucceeded()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &wallet_state)};
    };

    QSignalSpy create_spy(&controller, &WalletQmlController::walletCreateSucceeded);
    QSignalSpy create_calls_spy(&controller, &WalletQmlController::walletLoadInProgressChanged);
    controller.createSingleSigWallet("created_wallet", "");
    QVERIFY(AwaitWalletOperation(controller));

    QTRY_COMPARE_WITH_TIMEOUT(create_spy.count(), 1, 5000);
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
    QCOMPARE(controller.walletCreateError(), QString{});
    auto* created_model = create_spy.at(0).at(0).value<WalletQmlModel*>();
    QVERIFY(created_model);
    QCOMPARE(created_model->name(), QString{"created_wallet"});
    QVERIFY(controller.openWallets().contains(created_model));
}

void WalletQmlControllerTests::initializedControllerDoesNotCompleteCreateForUnrelatedLoadNotification()
{
    FakeWallet::State created_state;
    FakeWallet::State unrelated_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    QSemaphore create_started;
    QSemaphore allow_create_return;
    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        create_started.release();
        allow_create_return.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &created_state)};
    };

    QSignalSpy create_spy(&controller, &WalletQmlController::walletCreateSucceeded);

    controller.createSingleSigWallet("created_wallet", "");
    QVERIFY(create_started.tryAcquire(1, 5000));
    QVERIFY(controller.walletLoadInProgress());

    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("unrelated_wallet", &unrelated_state));
    QCoreApplication::processEvents();

    QCOMPARE(create_spy.count(), 0);
    QVERIFY(controller.walletLoadInProgress());

    allow_create_return.release();
    QTRY_COMPARE_WITH_TIMEOUT(create_spy.count(), 1, 5000);
    QVERIFY(!controller.walletLoadInProgress());
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QVERIFY(controller.canOpenResult());
    QCOMPARE(controller.walletOperationResultWallet()->name(), QString{"created_wallet"});
    QVERIFY(controller.selectWallet(controller.walletOperationResultWallet()));
    QCOMPARE(controller.selectedWallet()->name(), QString{"created_wallet"});
}

void WalletQmlControllerTests::initializedControllerDoesNotCompleteLoadForUnrelatedLoadNotification()
{
    FakeWallet::State loaded_state;
    FakeWallet::State unrelated_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"target_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    QSemaphore load_started;
    QSemaphore allow_load_return;
    loader.load_wallet_fn = [&](const std::string& name,
                                std::vector<bilingual_str>&) {
        load_started.release();
        allow_load_return.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &loaded_state)};
    };

    QSignalSpy load_spy(&controller, &WalletQmlController::walletLoadSucceeded);

    controller.setSelectedWallet("target_wallet", "sqlite");
    QVERIFY(load_started.tryAcquire(1, 5000));
    QVERIFY(controller.walletLoadInProgress());

    loader.captured_load_wallet_fn(std::make_unique<FakeWallet>("unrelated_wallet", &unrelated_state));
    QCoreApplication::processEvents();

    QCOMPARE(load_spy.count(), 0);
    QVERIFY(controller.walletLoadInProgress());

    allow_load_return.release();
    QTRY_COMPARE_WITH_TIMEOUT(load_spy.count(), 1, 5000);
    QVERIFY(!controller.walletLoadInProgress());
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QVERIFY(controller.canOpenResult());
    QCOMPARE(controller.walletOperationResultWallet()->name(), QString{"target_wallet"});
    QVERIFY(controller.selectWallet(controller.walletOperationResultWallet()));
    QCOMPARE(controller.selectedWallet()->name(), QString{"target_wallet"});
}

void WalletQmlControllerTests::initializedControllerReportsCreateWarnings()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    loader.create_wallet_fn = [](const std::string&,
                                 const SecureString&,
                                 uint64_t,
                                 std::vector<bilingual_str>& warnings) {
        warnings.push_back(Untranslated("Disk space is low."));
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };

    QSignalSpy warnings_spy(&controller, &WalletQmlController::walletLoadWarningsChanged);
    controller.createSingleSigWallet("test_wallet", "secret");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(warnings_spy.count(), 1);
    QCOMPARE(controller.walletLoadWarnings(), QString{"Disk space is low."});
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
}

void WalletQmlControllerTests::initializedControllerIgnoresDuplicateCreateWhileInProgress()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);
    struct CreateGate { QSemaphore entered, release; };
    const auto gate = std::make_shared<CreateGate>();
    DrainingWalletController controller(node);
    const auto unblock = qScopeGuard([gate] { gate->release.release(); });
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    loader.create_wallet_fn = [gate](const std::string&, const SecureString&, uint64_t,
                                    std::vector<bilingual_str>&) {
        gate->entered.release();
        gate->release.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            util::Error{Untranslated("Wallet creation failed.")}};
    };
    QSignalSpy error_spy(&controller, &WalletQmlController::walletCreateErrorChanged);
    controller.createSingleSigWallet("test_wallet", "secret");
    const auto original = controller.walletOperationId();
    QVERIFY(QTest::qWaitFor([&] { return gate->entered.tryAcquire(1); }, 5000));
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(error_spy.count(), 0);
    controller.createSingleSigWallet("test_wallet", "secret");
    QCOMPARE(controller.walletOperationId(), original);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationPending);
    QCOMPARE(loader.create_wallet_calls, 1);
    QCOMPARE(error_spy.count(), 0);
    gate->release.release();
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(controller.walletOperationId(), original);
    QCOMPARE(controller.walletOperationState(), WalletQmlController::WalletOperationState::WalletOperationFailed);
    QCOMPARE(error_spy.count(), 1);
    QCOMPARE(controller.walletCreateError(), QString{"Wallet creation failed."});
    QCOMPARE(loader.create_wallet_calls, 1);
}

void WalletQmlControllerTests::watchOnlyCreateBeforeInitializationSetsLoadError()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    controller.createWatchOnlyWallet("watch_only", VALID_XPUB);
    QVERIFY(AwaitWalletOperation(controller));

    QCOMPARE(controller.walletLoadError(), QString{NOT_INITIALIZED_ERROR});
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::watchOnlyCreateWhileWalletLoadInProgressIsIgnored()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSemaphore create_started;
    QSemaphore allow_create_return;
    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        create_started.release();
        allow_create_return.acquire();
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &wallet_state)};
    };

    controller.createSingleSigWallet("regular_wallet", "");
    QVERIFY(create_started.tryAcquire(1, 5000));
    QVERIFY(controller.walletLoadInProgress());

    controller.createWatchOnlyWallet("watch_only", VALID_XPUB);

    QCOMPARE(loader.create_wallet_calls, 1);
    QVERIFY(controller.walletLoadInProgress());

    allow_create_return.release();
    QTRY_VERIFY_WITH_TIMEOUT(!controller.walletLoadInProgress(), 5000);
}

void WalletQmlControllerTests::watchOnlyCreateFailureAfterCreateDoesNotPublishWallet()
{
    FakeWallet::State returned_state;
    FakeWallet::State notified_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    QVERIFY(loader.captured_load_wallet_fn);

    loader.create_wallet_fn = [&](const std::string& name,
                                  const SecureString&,
                                  uint64_t,
                                  std::vector<bilingual_str>&) {
        loader.captured_load_wallet_fn(std::make_unique<FakeWallet>(name, &notified_state));
        return util::Result<std::unique_ptr<interfaces::Wallet>>{
            std::make_unique<FakeWallet>(name, &returned_state)};
    };

    QSignalSpy create_spy(&controller, &WalletQmlController::walletCreateSucceeded);
    QSignalSpy error_spy(&controller, &WalletQmlController::walletLoadErrorChanged);

    controller.createWatchOnlyWallet("watch_only", VALID_XPUB);
    QVERIFY(AwaitWalletOperation(controller));

    QTRY_COMPARE_WITH_TIMEOUT(error_spy.count(), 1, 5000);
    QCOMPARE(create_spy.count(), 0);
    QVERIFY(!controller.isWalletLoaded());
    QVERIFY(!controller.walletLoadInProgress());
    QVERIFY(!controller.walletLoadError().isEmpty());
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QCOMPARE(returned_state.remove_calls.load(), 1);
}

void WalletQmlControllerTests::initializedControllerRequestsPassphraseBeforeEncryptedMigration()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    loader.is_encrypted_fn = [](const std::string& name) {
        return name == "legacy_wallet";
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy passphrase_spy(&controller, &WalletQmlController::walletMigrationPassphraseRequired);

    controller.migrateWallet("legacy_wallet", "");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(passphrase_spy.count(), 1);
    QCOMPARE(passphrase_spy.takeFirst().at(0).toString(), QString{"legacy_wallet"});
    QCOMPARE(loader.is_encrypted_calls, 1);
    QCOMPARE(loader.migrate_wallet_calls, 0);
    QVERIFY(!controller.walletMigrationInProgress());
    QVERIFY(controller.walletMigrationError().isEmpty());
}

void WalletQmlControllerTests::initializedControllerMigratesUnencryptedWalletWithoutPassphrase()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"legacy_wallet", "bdb"}};
    loader.is_encrypted_fn = [](const std::string&) {
        return false;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy failed_spy(&controller, &WalletQmlController::walletMigrationFailed);

    bool saw_empty_passphrase{false};
    loader.migrate_wallet_fn = [&](const std::string& name, const SecureString& passphrase, bool) {
        saw_empty_passphrase = (name == "legacy_wallet" && passphrase.empty());
        return util::Result<interfaces::WalletMigrationResult>{
            util::Error{Untranslated("Migration failed.")}};
    };

    controller.migrateWallet("legacy_wallet", "");
    QVERIFY(AwaitWalletOperation(controller));
    QCOMPARE(failed_spy.count(), 1);
    QVERIFY(saw_empty_passphrase);
    QCOMPARE(loader.is_encrypted_calls, 1);
    QCOMPARE(loader.migrate_wallet_calls, 1);
    QCOMPARE(controller.walletMigrationError(), QString{"Migration failed."});
    QVERIFY(!controller.walletMigrationInProgress());
}

void WalletQmlControllerTests::initializedControllerClosesSelectedWalletAndSelectsRemainingLoadedWallet()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(controller.selectedWallet()->name(), QString{"alpha_wallet"});
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QVERIFY(controller.isWalletOpen("beta_wallet"));
    QCOMPARE(controller.openWallets().size(), 2);
    WalletQmlModel* beta_model = qobject_cast<WalletQmlModel*>(controller.openWallets().at(1));
    QVERIFY(beta_model);

    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);
    QStringList signal_order;
    QObject::connect(&controller, &WalletQmlController::selectedWalletChanged, [&]() {
        signal_order.append("selectedWalletChanged");
    });
    QObject::connect(&controller, &WalletQmlController::walletLoadStateChanged, [&]() {
        signal_order.append("walletLoadStateChanged");
    });
    // Views are keyed by the open wallets, so the next wallet must be
    // selected before the closed one leaves the list.
    QObject::connect(&controller, &WalletQmlController::openWalletsChanged, [&]() {
        signal_order.append("openWalletsChanged");
        QCOMPARE(controller.selectedWallet(), beta_model);
    });

    controller.closeWallet("alpha_wallet");

    QTRY_COMPARE(alpha_state.remove_calls.load(), 1);
    QCOMPARE(beta_state.remove_calls.load(), 0);
    QCOMPARE(selected_spy.count(), 1);
    QCOMPARE(load_state_spy.count(), 1);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), false);
    QCOMPARE(signal_order, QStringList({"selectedWalletChanged", "openWalletsChanged", "walletLoadStateChanged"}));
    QCOMPARE(controller.selectedWallet()->name(), QString{"beta_wallet"});
    QCOMPARE(controller.openWallets(), QList<QObject*>({beta_model}));
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QVERIFY(controller.isWalletOpen("beta_wallet"));
}

void WalletQmlControllerTests::initializedControllerClosesNonSelectedWalletWithoutChangingSelection()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);

    controller.closeWallet("beta_wallet");

    QCOMPARE(alpha_state.remove_calls.load(), 0);
    QTRY_COMPARE(beta_state.remove_calls.load(), 1);
    QCOMPARE(selected_spy.count(), 0);
    QCOMPARE(load_state_spy.count(), 1);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"beta_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), false);
    QCOMPARE(controller.selectedWallet()->name(), QString{"alpha_wallet"});
    QVERIFY(controller.isWalletLoaded());
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QVERIFY(!controller.isWalletOpen("beta_wallet"));
}

void WalletQmlControllerTests::reopeningRetiringWalletReportsLoadErrorAndSucceedsAfterDrain()
{
    FakeWallet::State state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    loader.get_wallets_fn = [&] {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &state));
        return wallets;
    };
    struct LoadGate { QSemaphore entered, release; };
    const auto load_gate = std::make_shared<LoadGate>();
    loader.load_wallet_fn = [&, load_gate](const std::string& name, std::vector<bilingual_str>&) -> util::Result<std::unique_ptr<interfaces::Wallet>> {
        load_gate->entered.release();
        load_gate->release.acquire();
        return std::unique_ptr<interfaces::Wallet>{std::make_unique<FakeWallet>(name, &state)};
    };
    ConfigureExpectedControllerInitialization(node, loader);
    QSemaphore entered, release;
    DrainingWalletController controller{node};
    WalletListModel list{node};
    list.listWalletDir();
    QTRY_VERIFY(!list.projectionPending());
    QVERIFY(list.readError().isEmpty());
    connect(&controller, &WalletQmlController::walletLoadStateChanged, &list, &WalletListModel::setWalletLoadState);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());
    const auto row = list.index(0);
    QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));

    const auto executor = controller.selectedWallet()->backendExecutor();
    QSignalSpy retired{controller.selectedWallet(), &WalletQmlModel::shutdownFinished};
    executor->submit(this, [&] { entered.release(); release.acquire(); }, [] {});
    const auto release_on_exit = qScopeGuard([&] { release.release(); load_gate->release.release(); });
    QTRY_VERIFY(entered.available() > 0);
    controller.closeWallet("alpha_wallet");
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Closed));

    for (int attempt = 0; attempt < 2; ++attempt) {
        controller.setSelectedWallet("alpha_wallet", "sqlite");
        QCOMPARE(loader.load_wallet_calls, 0);
        QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::LoadError));
        QCOMPARE(list.data(row, WalletListModel::ErrorMessageRole).toString(), QStringLiteral("This wallet is still closing."));
        QCOMPARE(controller.walletLoadError(), list.data(row, WalletListModel::ErrorMessageRole).toString());
    }

    release.release();
    QTRY_COMPARE(retired.size(), 1);
    QVERIFY(executor->isDrained());
    QSignalSpy loaded{&controller, &WalletQmlController::walletLoadSucceeded};
    controller.setSelectedWallet("alpha_wallet", "sqlite");
    QVERIFY(QTest::qWaitFor([&] { return load_gate->entered.tryAcquire(1); }, 5000));
    QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Loading));
    QCOMPARE(loaded.size(), 0);
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(controller.walletLoadInProgress());
    load_gate->release.release();
    QTRY_COMPARE(loaded.size(), 1);
    QCOMPARE(loader.load_wallet_calls, 1);
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QTRY_COMPARE(list.data(row, WalletListModel::LoadStateRole).toInt(), static_cast<int>(WalletListModel::LoadState::Open));
    QVERIFY(list.data(row, WalletListModel::ErrorMessageRole).toString().isEmpty());
    QVERIFY(controller.walletLoadError().isEmpty());
}

void WalletQmlControllerTests::initializedControllerEmitsWalletLoadStateChanged()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);

    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(load_state_spy.count(), 2);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), true);
    QCOMPARE(load_state_spy.at(1).at(0).toString(), QString{"beta_wallet"});
    QCOMPARE(load_state_spy.at(1).at(1).toBool(), true);
    QCOMPARE(loader.list_wallet_dir_calls, 0);
}

void WalletQmlControllerTests::initializedControllerHandlesExternalWalletUnload()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QTRY_VERIFY(alpha_state.hasUnload());
    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);
    WalletQmlModel* alpha_model = controller.selectedWallet();
    QVERIFY(alpha_model->available());
    QSignalSpy available_spy(alpha_model, &WalletQmlModel::availableChanged);

    alpha_state.notifyUnload();

    // Actions are refused from the moment the backend reports the unload,
    // before the GUI thread hears about it or drops the wallet.
    QVERIFY(!alpha_model->available());
    QVERIFY(controller.isWalletOpen("alpha_wallet"));
    QVERIFY(!alpha_model->backupWallet(QStringLiteral("/nonexistent/alpha.bak")));
    QCOMPARE(alpha_model->settingsError(), QString{"This wallet is closing."});
    QTRY_COMPARE(available_spy.count(), 1);

    QTRY_COMPARE(load_state_spy.count(), 1);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), false);
    QCOMPARE(selected_spy.count(), 1);
    QCOMPARE(controller.selectedWallet()->name(), QString{"beta_wallet"});
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QVERIFY(controller.isWalletOpen("beta_wallet"));
    QVERIFY(controller.isWalletLoaded());
    QCOMPARE(alpha_state.remove_calls.load(), 0);
    QCOMPARE(beta_state.remove_calls.load(), 0);
}

void WalletQmlControllerTests::initializedControllerUnloadWalletsClearsSelectionAndOpenWallets()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy drained{&controller, &WalletQmlController::walletsDrained};
    QSignalSpy selected_spy(&controller, &WalletQmlController::selectedWalletChanged);
    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);

    controller.unloadWallets();

    QCOMPARE(selected_spy.count(), 1);
    QTRY_COMPARE(load_state_spy.count(), 2);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toBool(), false);
    QCOMPARE(load_state_spy.at(1).at(0).toString(), QString{"beta_wallet"});
    QCOMPARE(load_state_spy.at(1).at(1).toBool(), false);
    QCOMPARE(controller.selectedWallet()->name(), QString{});
    QVERIFY(!controller.isWalletOpen("alpha_wallet"));
    QVERIFY(!controller.isWalletOpen("beta_wallet"));
    QCOMPARE(alpha_state.remove_calls.load(), 0);
    QCOMPARE(beta_state.remove_calls.load(), 0);
    QTRY_COMPARE(drained.count(), 1);
}

void WalletQmlControllerTests::initializedControllerEmitsLoadingThenLoadErrorOnFailedLoad()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QSignalSpy load_state_spy(&controller, &WalletQmlController::walletLoadStateChanged);
    QSignalSpy load_error_spy(&controller, &WalletQmlController::walletLoadErrorChanged);
    controller.setSelectedWallet("alpha_wallet", "sqlite");

    // FakeWalletLoader::loadWallet() defaults to returning an error, so
    // startWalletLoad must emit Loading synchronously and LoadError once the
    // worker completes.
    QTRY_COMPARE_WITH_TIMEOUT(load_state_spy.count(), 2, 5000);
    QCOMPARE(load_state_spy.at(0).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(0).at(1).toInt(),
             static_cast<int>(WalletListModel::LoadState::Loading));
    QCOMPARE(load_state_spy.at(0).at(2).toString(), QString{});

    QCOMPARE(load_state_spy.at(1).at(0).toString(), QString{"alpha_wallet"});
    QCOMPARE(load_state_spy.at(1).at(1).toInt(),
             static_cast<int>(WalletListModel::LoadState::LoadError));
    QVERIFY(!load_state_spy.at(1).at(2).toString().isEmpty());

    QTRY_VERIFY(load_error_spy.count() >= 1);
    QVERIFY(!controller.walletLoadInProgress());
}

void WalletQmlControllerTests::publishOpenWalletsInfoEmitsWalletInfoChangedForEachOpenWallet()
{
    FakeWallet::State alpha_state;
    FakeWallet::State beta_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    alpha_state.balance = 13900000000LL;
    beta_state.balance = 1;
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("alpha_wallet", &alpha_state));
        wallets.emplace_back(std::make_unique<FakeWallet>("beta_wallet", &beta_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    controller.setSelectedWallet("alpha_wallet", "sqlite");
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_COMPARE(controller.selectedWallet()->displayedBalanceSatoshi(), qint64{13900000000LL});
    controller.setSelectedWallet("beta_wallet", "sqlite");
    QVERIFY(AwaitWalletOperation(controller));
    QTRY_COMPARE(controller.selectedWallet()->displayedBalanceSatoshi(), qint64{1});
    QSignalSpy info_spy(&controller, &WalletQmlController::walletInfoChanged);
    const auto metadata_reads = alpha_state.name_reads.load() + beta_state.name_reads.load();
    controller.publishOpenWalletsInfo();
    QCOMPARE(alpha_state.name_reads.load() + beta_state.name_reads.load(), metadata_reads);

    QCOMPARE(info_spy.count(), 2);
    QStringList names;
    names << info_spy.at(0).at(0).toString() << info_spy.at(1).at(0).toString();
    names.sort();
    QCOMPARE(names, QStringList({"alpha_wallet", "beta_wallet"}));
    const auto verify_raw_balances = [&info_spy]() {
        QCOMPARE(info_spy.count(), 2);
        for (const auto& args : info_spy) {
            QCOMPARE(args.at(1).metaType().id(), QMetaType::LongLong);
            QCOMPARE(args.at(1).toLongLong(), args.at(0).toString() == "alpha_wallet" ? 13900000000LL : 1LL);
            QVERIFY(args.at(3).toBool());
        }
    };
    verify_raw_balances();
    controller.setSelectedWallet("alpha_wallet", "sqlite");
    QVERIFY(AwaitWalletOperation(controller));
    controller.selectedWallet()->setDisplayUnit(3);
    controller.setSelectedWallet("beta_wallet", "sqlite");
    QVERIFY(AwaitWalletOperation(controller));
    info_spy.clear();
    controller.publishOpenWalletsInfo();
    verify_raw_balances();
}

void WalletQmlControllerTests::walletNameAvailabilityErrorRejectsEmptyAndWhitespace()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(CheckedWalletName(controller, ""), QString{"Enter a wallet name."});
    QCOMPARE(CheckedWalletName(controller, "   "), QString{"Enter a wallet name."});
    QCOMPARE(CheckedWalletName(controller, "\t\n"), QString{"Enter a wallet name."});
}

void WalletQmlControllerTests::walletNameAvailabilityErrorRejectsExistingNameWithSurroundingWhitespace()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(CheckedWalletName(controller, "alpha_wallet"),
             QString{"A wallet with this name already exists"});
    QCOMPARE(CheckedWalletName(controller, "  alpha_wallet  "),
             QString{"A wallet with this name already exists"});
    QCOMPARE(CheckedWalletName(controller, "ALPHA_WALLET"),
             QString{"A wallet with this name already exists"});
}

void WalletQmlControllerTests::walletNameAvailabilityErrorReturnsEmptyForAvailableName()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    loader.wallet_dir_entries = {{"alpha_wallet", "sqlite"}};
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QCOMPARE(CheckedWalletName(controller, "brand_new_wallet"), QString{});
    QVERIFY(loader.directory_thread.load() != QThread::currentThread());
}

void WalletQmlControllerTests::walletDisplayNameWriteOwnsOutcome_data()
{
    QTest::addColumn<QString>("requested_alias");
    QTest::addColumn<QString>("expected_alias");
    QTest::newRow("trim-and-save") << QStringLiteral("  Savings  ") << QStringLiteral("Savings");
    QTest::newRow("clear-empty") << QString{} << QStringLiteral("r10-display-name-manual-wallet");
    QTest::newRow("clear-with-wallet-name")
        << QStringLiteral("r10-display-name-manual-wallet") << QStringLiteral("r10-display-name-manual-wallet");
}

void WalletQmlControllerTests::walletDisplayNameWriteOwnsOutcome()
{
    QFETCH(QString, requested_alias);
    QFETCH(QString, expected_alias);

    const QString wallet_name{QStringLiteral("r10-display-name-manual-wallet")};
    const QString key{QStringLiteral("walletDisplayNames/%1").arg(wallet_name)};
    QSettings settings;
    const auto v2_key = walletsettings::EntryKey(walletsettings::Area::Aliases, wallet_name);
    const bool had_v2 = settings.contains(v2_key);
    const auto previous_v2 = settings.value(v2_key);
    const bool had_previous_value = settings.contains(key);
    const QVariant previous_value = settings.value(key);
    const auto restore_settings = qScopeGuard([&] {
        if (had_v2) settings.setValue(v2_key, previous_v2);
        else settings.remove(v2_key);
        if (had_previous_value) settings.setValue(key, previous_value);
        else settings.remove(key);
        settings.sync();
    });
    settings.remove(v2_key);
    settings.setValue(key, QStringLiteral("Prior alias"));
    settings.sync();
    QCOMPARE(settings.status(), QSettings::NoError);

    MockNode node;
    ManualControllerGraph graph{node};
    // The controller constructor schedules its first alias observation.
    // Drain that independent work before attributing effects or signals to
    // the user's write request.
    QVERIFY(graph.until([&] { return graph.native.transportIdle(); }));
    QVERIFY(graph.native.transportIdle());
    QVERIFY(graph.native.scheduler->pending() > 0);
    auto& controller = *graph.controller;
    QSignalSpy write_changed{&controller, &WalletQmlController::walletDisplayNameWriteChanged};
    QSignalSpy names_changed{&controller, &WalletQmlController::walletDisplayNamesChanged};

    QVERIFY(controller.setWalletDisplayName(wallet_name, requested_alias));
    QVERIFY(controller.walletDisplayNameWritePending());
    QVERIFY(!controller.walletDisplayNameWriteSucceeded());
    QVERIFY(controller.walletDisplayNameWriteError().isEmpty());
    QCOMPARE(write_changed.size(), 1);
    QVERIFY(!controller.setWalletDisplayName(wallet_name, QStringLiteral("duplicate")));
    QCOMPARE(write_changed.size(), 1);

    settings.sync();
    QCOMPARE(settings.value(key).toString(), QStringLiteral("Prior alias"));

    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(controller.walletDisplayNameWritePending());
    QVERIFY(!controller.walletDisplayNameWriteSucceeded());
    QVERIFY(controller.walletDisplayNameWriteError().isEmpty());
    QCOMPARE(graph.native.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Domain; }), 1);
    QVERIFY(graph.posts.empty());
    settings.sync();
    if (expected_alias == wallet_name) {
        QVERIFY(settings.value(key).toString().isEmpty());
    } else {
        QCOMPARE(settings.value(key).toString(), expected_alias);
    }
    QCOMPARE(write_changed.size(), 1);

    // OwnedCommand commits the receipt on its Domain outcome turn. Core
    // effect completion alone must not publish a successful receipt.
    QVERIFY(graph.step(execution::Lane::Domain));
    QVERIFY(controller.walletDisplayNameWritePending());
    QVERIFY(controller.walletDisplayNameWriteSucceeded());
    QVERIFY(controller.walletDisplayNameWriteError().isEmpty());
    QCOMPARE(graph.posts.size(), size_t{1});

    auto publish_write = std::move(graph.posts.front());
    graph.posts.pop_front();
    publish_write();
    QVERIFY(!controller.walletDisplayNameWritePending());
    QVERIFY(controller.walletDisplayNameWriteSucceeded());
    QVERIFY(controller.walletDisplayNameWriteError().isEmpty());
    QCOMPARE(write_changed.size(), 2);
    QCOMPARE(names_changed.size(), 0);

    QVERIFY(graph.step(execution::Lane::Core));
    QVERIFY(graph.posts.empty());
    QCOMPARE(names_changed.size(), 0);
    QVERIFY(graph.step(execution::Lane::Domain));
    QCOMPARE(graph.posts.size(), size_t{1});
    auto publish_aliases = std::move(graph.posts.front());
    graph.posts.pop_front();
    publish_aliases();
    QCOMPARE(names_changed.size(), 1);
    QCOMPARE(controller.walletDisplayName(wallet_name), expected_alias);
    QVERIFY(graph.posts.empty());
    graph.shutdown();
    graph.controller.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(graph.native.quiescent());
}

void WalletQmlControllerTests::walletDisplayNameWriteReportsTransportRefusal()
{
    const QString wallet_name{QStringLiteral("r10-display-name-refused-wallet")};
    const QString key{QStringLiteral("walletDisplayNames/%1").arg(wallet_name)};
    QSettings settings;
    const bool had_previous_value = settings.contains(key);
    const QVariant previous_value = settings.value(key);
    const auto restore_settings = qScopeGuard([&] {
        if (had_previous_value) settings.setValue(key, previous_value);
        else settings.remove(key);
        settings.sync();
    });
    settings.setValue(key, QStringLiteral("Prior alias"));
    settings.sync();

    MockNode node;
    ManualControllerGraph graph{node};
    // Retire the constructor's initial alias read before rejecting the
    // independent write admission.
    QVERIFY(graph.until([&] { return graph.native.transportIdle(); }));
    QVERIFY(graph.native.transportIdle());
    QVERIFY(graph.native.scheduler->pending() > 0);
    graph.native.rejectLane(execution::Lane::Core);
    auto& controller = *graph.controller;
    QSignalSpy write_changed{&controller, &WalletQmlController::walletDisplayNameWriteChanged};

    QVERIFY(!controller.setWalletDisplayName(wallet_name, QStringLiteral("New alias")));
    QVERIFY(!controller.walletDisplayNameWritePending());
    QVERIFY(!controller.walletDisplayNameWriteSucceeded());
    QCOMPARE(controller.walletDisplayNameWriteError(), QStringLiteral("Wallet display name write could not be queued."));
    QCOMPARE(write_changed.size(), 1);
    QVERIFY(graph.posts.empty());
    settings.sync();
    QCOMPARE(settings.value(key).toString(), QStringLiteral("Prior alias"));
    graph.shutdown();
    graph.controller.reset();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(graph.native.quiescent());
}

void WalletQmlControllerTests::openWalletLocationOpensWalletDirectory()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    QVERIFY(QDir{wallet_dir.path()}.mkpath("created_wallet"));
    loader.wallet_dir = wallet_dir.path().toStdString();
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("created_wallet", &wallet_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QString opened_path;
    controller.setOpenLocalPathFnForTesting([&](const QString& path) {
        opened_path = path;
        return true;
    });

    QVERIFY(AwaitWalletLocation(controller, controller.selectedWallet()));
    QVERIFY(loader.directory_path_thread.load() != nullptr);
    QVERIFY(loader.directory_path_thread.load() != QThread::currentThread());
    QCOMPARE(opened_path, QFileInfo{wallet_dir.filePath("created_wallet")}.absoluteFilePath());
    QVERIFY(controller.walletLocationOpenError().isEmpty());
}

void WalletQmlControllerTests::openWalletLocationReportsOpenFailure()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    QVERIFY(QDir{wallet_dir.path()}.mkpath("created_wallet"));
    loader.wallet_dir = wallet_dir.path().toStdString();
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("created_wallet", &wallet_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    QString opened_path;
    controller.setOpenLocalPathFnForTesting([&](const QString& path) {
        opened_path = path;
        return false;
    });

    QVERIFY(!AwaitWalletLocation(controller, controller.selectedWallet()));
    QCOMPARE(opened_path, QFileInfo{wallet_dir.filePath("created_wallet")}.absoluteFilePath());
    QCOMPARE(controller.walletLocationOpenError(), QString{"Could not open wallet file location."});
}

void WalletQmlControllerTests::openWalletLocationReportsMissingWalletPath()
{
    FakeWallet::State wallet_state;
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    FakeWalletLoader loader;
    QTemporaryDir wallet_dir;
    QVERIFY(wallet_dir.isValid());
    loader.wallet_dir = wallet_dir.path().toStdString();
    loader.get_wallets_fn = [&]() {
        std::vector<std::unique_ptr<interfaces::Wallet>> wallets;
        wallets.emplace_back(std::make_unique<FakeWallet>("missing_wallet", &wallet_state));
        return wallets;
    };
    ConfigureExpectedControllerInitialization(node, loader);

    DrainingWalletController controller(node);
    controller.initialize();
    QTRY_VERIFY(controller.initialized());

    bool opened{false};
    controller.setOpenLocalPathFnForTesting([&](const QString&) {
        opened = true;
        return true;
    });

    QVERIFY(!AwaitWalletLocation(controller, controller.selectedWallet()));
    QVERIFY(!opened);
    QCOMPARE(controller.walletLocationOpenError(), QString{"Wallet file not found: %1"}.arg(QFileInfo{wallet_dir.filePath("missing_wallet")}.absoluteFilePath()));
}

void WalletQmlControllerTests::openWalletLocationReportsMissingWallet()
{
    StrictMockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    DrainingWalletController controller(node);

    QSignalSpy errors{&controller, &WalletQmlController::walletLocationOpenErrorChanged};
    node.ExpectNoCalls(node.calls.walletLoader);
    QVERIFY(!AwaitWalletLocation(controller, nullptr));
    QCOMPARE(errors.count(), 1);
    QVERIFY(!controller.walletLocationPending());
    QCOMPARE(controller.walletLocationOpenError(), QString{"No wallet file is available to view."});
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletQmlControllerTests)
#else
QTEST_MAIN(WalletQmlControllerTests)
#endif

#include <test_walletqmlcontroller.moc>
