// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/messagebumptest.h>
#include <test/mocks/sendtest.h>
#include <test/mocks/preparationtest.h>
#include <qml/guipresentation.h>
#include <qml/paymentimportports.h>
#include <util/string.h>
#include <addresstype.h>
#include <chainparams.h>
#include <common/messages.h>
#include <common/signmessage.h>
#include <interfaces/handler.h>
#include <interfaces/chain.h>
#include <interfaces/wallet.h>
#include <key.h>
#include <key_io.h>
#include <outputtype.h>
#include <node/context.h>
#include <node/types.h>
#include <primitives/transaction.h>
#include <psbt.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/psbtqmlmodel.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/signertransaction.h>
#include <qml/models/walletqmlmodel.h>
#include <qml/walletqmlcontroller.h>
#include <qml/walletidentity.h>
#include <qml/walletsettings.h>
#include <qml/bitcoinunits.h>
#include <qml/walletsend.h>
#include <test/mocks/receiverequesttest.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <script/signingprovider.h>
#include <script/solver.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualfeereceipts.h>
#include <test/mocks/admissionhookscheduler.h>
#include <test/mocks/manualwalletdrain.h>
#include <test/mocks/mockwallet.h>
#include <test/mocks/signertransactions.h>
#include <test/mocks/psbtfeefixture.h>
#include <txmempool.h>
#include <wallet/coincontrol.h>
#include <wallet/context.h>
#include <wallet/fees.h>
#include <wallet/sqlite.h>
#include <test/mocks/feepreviewchain.h>
#include <wallet/transaction.h>
#include <wallet/types.h>
#include <util/moneystr.h>
#include <util/strencodings.h>

#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QElapsedTimer>
#include <QLocale>
#include <QScopeGuard>
#include <QMetaProperty>
#include <QQmlEngine>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QVariantList>
#include <QVariantMap>
#include <QtTest/QtTest>
#include <test/mocks/psbtactiontest.h>
#include <test/mocks/approvaltest.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <numeric>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {
std::unique_ptr<interfaces::Handler> MakeNoopHandler()
{
    return interfaces::MakeCleanupHandler([] {});
}

constexpr auto NON_P2PKH_ADDRESS{"bcrt1qqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqqq3xueyj"};

constexpr auto FEE_ESTIMATE_TIMEOUT_MS{3'000};
const auto VALID_MAINNET_ADDRESS = QStringLiteral("1BoatSLRHtKNngkdXEeobR76b53LETtpyT");
const auto VALID_MAINNET_P2SH_ADDRESS = QStringLiteral("3J98t1WpEZ73CNmQviecrnyiWrnqRhWNLy");
const auto VALID_REGTEST_ADDRESS = QStringLiteral("bcrt1qdavt4j2sd7dlhqsavtnfxvzppw6k7qy97tmnu9");

class ChainSelectionGuard
{
public:
    explicit ChainSelectionGuard(const ChainType chain_type)
        : m_previous_chain_type{Params().GetChainType()}
    {
        SelectParams(chain_type);
    }

    ~ChainSelectionGuard()
    {
        SelectParams(m_previous_chain_type);
    }

private:
    ChainType m_previous_chain_type;
};

struct FeeEstimateCall {
    unsigned int target;
    bool sign;
    bool selected_inputs;
    std::optional<CAmount> fee_rate;
    size_t recipient_count;
};

struct SelectedCoinEstimateCall {
    unsigned int target;
    bool sign;
    bool expected_coin_selected;
    bool allow_other_inputs;
    size_t selected_count;
};

template <typename T>
class ThreadSafeCallLog
{
public:
    void Append(T call)
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        m_calls.push_back(std::move(call));
    }

    size_t Size() const
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_calls.size();
    }

    std::vector<T> Snapshot() const
    {
        std::lock_guard<std::mutex> lock{m_mutex};
        return m_calls;
    }

private:
    mutable std::mutex m_mutex;
    std::vector<T> m_calls;
};

void CompareFeeEstimateCalls(const std::vector<FeeEstimateCall>& actual, const std::vector<FeeEstimateCall>& expected)
{
    QCOMPARE(actual.size(), expected.size());
    for (size_t index{0}; index < std::min(actual.size(), expected.size()); ++index) {
        QCOMPARE(actual[index].target, expected[index].target);
        QCOMPARE(actual[index].sign, expected[index].sign);
        QCOMPARE(actual[index].selected_inputs, expected[index].selected_inputs);
        QCOMPARE(actual[index].fee_rate.has_value(), expected[index].fee_rate.has_value());
        if (actual[index].fee_rate && expected[index].fee_rate) {
            QCOMPARE(*actual[index].fee_rate, *expected[index].fee_rate);
        }
        QCOMPARE(actual[index].recipient_count, expected[index].recipient_count);
    }
}

// The opening's first observation establishes its resource identity. Permit
// that identity so metadata can become ready, then refuse other read-only Core
// observations without relying on a changing constructor-wide task ordinal.
class CoreObservationGate final : public execution::Scheduler {
public:
    explicit CoreObservationGate(std::shared_ptr<execution::Scheduler> target) : m_target(std::move(target)) {}
    std::atomic_bool reject{true};
    std::atomic_int rejected{0};
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
        if (spec.lane == execution::Lane::Core && spec.work_class == execution::WorkClass::Observation) {
            bool opening{false};
            {
                const std::lock_guard lock{m_mutex};
                if (!m_opening_key) m_opening_key = spec.key;
                opening = spec.key == *m_opening_key;
            }
            if (reject && !opening) {
                ++rejected;
                return {{}, execution::Rejection::Transport};
            }
        }
        return m_target->submit(std::move(task), spec);
    }
    std::shared_ptr<execution::Scheduler> openActionScope() override { return m_target->openActionScope(); }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return m_target->reserveCleanup(std::move(task), spec); }
    bool activateCleanup(execution::TaskId id) override { return m_target->activateCleanup(id); }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override { return m_target->borrow(key); }
    execution::ResourceKey allocateKey() override { return m_target->allocateKey(); }
    size_t pending() const override { return m_target->pending(); }
    void closeOrdinaryAdmission() override { m_target->closeOrdinaryAdmission(); }
    bool cancelQueued(execution::TaskId id) override { return m_target->cancelQueued(id); }
private:
    std::shared_ptr<execution::Scheduler> m_target;
    std::mutex m_mutex;
    std::optional<execution::ResourceKey> m_opening_key;
};

struct ManualFeeRetirement {
    std::shared_ptr<ManualExecutionEnvironment> environment;
    std::vector<std::shared_ptr<BackendExecutor>> backends;
    bool finish()
    {
        // Facades have already been destroyed, preserving the held-effect
        // observer-loss schedule. Their original Native owners still drain
        // before the surrounding wallet/chain fixture leaves scope.
        return QTest::qWaitFor([&] {
            environment->drain();
            environment->publishAll();
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
            environment->drain();
            environment->publishAll();
            for (const auto& backend : backends) {
                if (!backend->isDrained()) return false;
            }
            return environment->quiescent();
        }, 10000);
    }
    ~ManualFeeRetirement()
    {
        if (!finish()) QTest::qFail("Original manual fee wallet graph did not physically drain", __FILE__, __LINE__);
    }
};

struct DrainWalletModel {
    std::shared_ptr<std::atomic_bool> hold_publications{};
    std::shared_ptr<ManualFeeRetirement> manual{};
    void operator()(WalletQmlModel* model) const
    {
        // Explicitly held test publications cover the action schedule. Final
        // observer retirement must run while this fixture waits for real drain.
        if (hold_publications) hold_publications->store(false);
        if (manual) {
            manual->backends.push_back(model->backendExecutor());
            model->beginShutdown();
            delete model;
            return;
        }
        const auto backend = model->backendExecutor();
        const QPointer<WalletQmlModel> observer{model};
        QSignalSpy drained{model, &WalletQmlModel::shutdownFinished};
        model->beginShutdown();
        // Qt does not deliver earlier DeferredDelete events through every
        // nested signal-spy loop. Retire the actual GUI leases while waiting
        // for both the public completion and physical worker drain.
        if (!QTest::qWaitFor([&] {
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                return backend->isDrained() && (!observer || observer->shutdownComplete() || !drained.empty());
            }, 10000))
            QTest::qFail("Original wallet backend did not physically drain", __FILE__, __LINE__);
        if (observer) delete observer.data();
    }
};

template <typename Wallet>
struct WalletModelHarness {
    Wallet* wallet;
    std::unique_ptr<WalletQmlModel, DrainWalletModel> model;
};

// Native fake funding ledger shared by construction, public coin lookup and
// policy balance probes. Empty fee-only callback documents get actual inputs
// and change; explicitly flagged malformed documents are left malformed.
struct ReviewFunding {
    const CScript own{CScript{} << OP_TRUE << OP_DROP << OP_TRUE};
    std::map<COutPoint, CTxOut> outputs;
    std::map<Txid, CTransactionRef> previous;
    CAmount remaining{0};
    void remember(const CTransaction& tx, CAmount fee, CAmount available) {
        outputs.clear(); previous.clear(); remaining = 0;
        if (tx.vin.empty()) return;
        CAmount spent{fee}; for (const auto& output : tx.vout) spent += output.nValue;
        if (!MoneyRange(spent)) return;
        const auto count = static_cast<CAmount>(tx.vin.size());
        const CAmount each = spent / count;
        for (size_t i = 0; i < tx.vin.size(); ++i) outputs.emplace(tx.vin[i].prevout,
            CTxOut{each + (i == 0 ? spent % count : 0), own});
        std::map<Txid, CMutableTransaction> documents;
        for (const auto& [input, output] : outputs) {
            auto& document = documents[input.hash];
            if (document.vout.size() <= input.n) document.vout.resize(input.n + 1);
            document.vout[input.n] = output;
        }
        for (auto& [id, document] : documents) previous.emplace(id, MakeTransactionRef(std::move(document)));
        remaining = std::max<CAmount>(0, available - spent);
    }
    std::optional<CAmount> balance(const wallet::CCoinControl& control) const {
        if (!control.HasSelected()) return {};
        CAmount total{0};
        for (const auto& input : control.ListSelected()) {
            const auto found = outputs.find(input); if (found == outputs.end()) return {};
            total += found->second.nValue;
        }
        if (control.m_allow_other_inputs) {
            for (const auto& [input, output] : outputs) if (!control.IsSelected(input)) total += output.nValue;
            total += remaining;
        }
        return total;
    }
    std::vector<interfaces::WalletTxOut> coins(const std::vector<COutPoint>& inputs) const {
        std::vector<interfaces::WalletTxOut> result;
        for (const auto& input : inputs) {
            const auto found = outputs.find(input); if (found == outputs.end()) return {};
            interfaces::WalletTxOut output{}; output.txout = found->second; output.depth_in_main_chain = 6;
            result.push_back(output);
        }
        return result;
    }
    bool complete(CMutableTransaction& tx, CAmount fee, CAmount available, const wallet::CCoinControl& control,
                  std::optional<unsigned int>& change) {
        if (!tx.vin.empty()) return true;
        CAmount spent{fee}; for (const auto& output : tx.vout) spent += output.nValue;
        if (!MoneyRange(spent) || available < spent) return false;
        auto inputs = control.ListSelected();
        if (inputs.empty()) inputs.emplace_back(Txid::FromUint256(uint256{uint8_t{255}}), 0);
        for (const auto& input : inputs) tx.vin.emplace_back(input);
        if (!change && available > spent) {
            change = tx.vout.size(); tx.vout.emplace_back(available - spent, own);
        }
        return true;
    }
};

class ReviewMockWallet : public MockWallet
{
public:
    bool preserve_malformed{false};
    explicit ReviewMockWallet(wallet::CWallet* raw_wallet = nullptr) : m_raw_wallet{raw_wallet} {}
    wallet::CWallet* wallet() override { return m_raw_wallet; }
    void enableCorePolicy(std::shared_ptr<wallet::CWallet> owner = {}) {
        if (!owner) owner = std::shared_ptr<wallet::CWallet>{m_raw_wallet, [](wallet::CWallet*) {}};
        m_public_policy = interfaces::MakeWallet(m_context, owner);
    }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>& recipients,
        const wallet::CCoinControl& coin_control, bool sign, std::optional<unsigned int> change_pos) override
    {
        if (m_public_policy && (!create_transaction_fn || std::ranges::any_of(recipients,
            [](const auto& recipient) { return recipient.fSubtractFeeFromAmount; }))) {
            ++calls.createTransaction;
            return m_public_policy->createTransaction(recipients, coin_control, sign, change_pos);
        }
        auto result = MockWallet::createTransaction(recipients, coin_control, sign, change_pos);
        if (!result) return result;
        const bool missing_outputs = result->tx->vout.empty();
        const bool missing_scripts = result->tx->vout.size() == recipients.size()
            && std::all_of(result->tx->vout.begin(), result->tx->vout.end(), [](const CTxOut& output) {
                return output.scriptPubKey.empty();
            });
        const CAmount available = get_available_balance_fn ? get_available_balance_fn(coin_control) : 0;
        if (missing_outputs || missing_scripts) {
            CMutableTransaction completed{*result->tx};
            for (size_t i = 0; i < recipients.size(); ++i) {
                const CScript script{GetScriptForDestination(recipients[i].dest)};
                if (missing_outputs) completed.vout.emplace_back(recipients[i].nAmount - (recipients[i].fSubtractFeeFromAmount ? result->fee : 0), script);
                else completed.vout[i].scriptPubKey = script;
            }
            if (!funding.complete(completed, result->fee, available, coin_control, result->change_pos))
                return util::Error{Untranslated("Synthetic fixture inputs do not cover amount plus fee")};
            result->tx = MakeTransactionRef(std::move(completed));
        }
        if (!preserve_malformed && result->tx->vin.empty()) {
            CMutableTransaction funded{*result->tx};
            if (!funding.complete(funded, result->fee, available, coin_control, result->change_pos))
                return util::Error{Untranslated("Synthetic fixture inputs do not cover amount plus fee")};
            result->tx = MakeTransactionRef(std::move(funded));
        }
        funding.remember(*result->tx, result->fee, available);
        return result;
    }
    CAmount getAvailableBalance(const wallet::CCoinControl& control) override {
        ++calls.getAvailableBalance;
        if (m_public_policy) return m_public_policy->getAvailableBalance(control);
        if (const auto value = funding.balance(control)) return *value;
        return get_available_balance_fn ? get_available_balance_fn(control) : 0;
    }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>& inputs) override {
        return m_public_policy ? m_public_policy->getCoins(inputs) : funding.coins(inputs);
    }
    bool txoutIsMine(const CTxOut& output) override {
        return m_public_policy ? m_public_policy->txoutIsMine(output) : output.scriptPubKey == funding.own;
    }
    CTransactionRef getTx(const Txid& id) override {
        if (m_public_policy) return m_public_policy->getTx(id);
        const auto found = funding.previous.find(id);
        return found == funding.previous.end() ? CTransactionRef{} : found->second;
    }
private:
    ReviewFunding funding;
    wallet::CWallet* m_raw_wallet;
    wallet::WalletContext m_context;
    std::unique_ptr<interfaces::Wallet> m_public_policy;
};

WalletModelHarness<MockWallet> MakeWalletModel(interfaces::Node* node = nullptr, wallet::CWallet* raw_wallet = nullptr, FeeReadServices services = {}, bool core_policy = false, std::shared_ptr<wallet::CWallet> owner = {}, std::function<void(MockWallet&)> configure = {}, walletopening::FeePolicy policy = {}, std::shared_ptr<execution::Scheduler> observations = {})
{
    std::shared_ptr<ManualFeeRetirement> manual;
    if (auto environment = std::dynamic_pointer_cast<ManualExecutionEnvironment>(services.timer);
        environment && (services.scheduler == environment->scheduler || observations == environment->scheduler)) {
        manual = std::make_shared<ManualFeeRetirement>();
        manual->environment = std::move(environment);
    }
    auto wallet = std::make_unique<ReviewMockWallet>(raw_wallet);
    if (core_policy) wallet->enableCorePolicy(std::move(owner));
    MockWallet* const wallet_view{wallet.get()};

    wallet_view->get_wallet_txs_fn = [] { return std::set<interfaces::WalletTx>{}; };
    wallet_view->list_coins_fn = [] { return interfaces::Wallet::CoinsList{}; };
    wallet_view->get_balance_fn = [] { return 10 * COIN; };
    wallet_view->get_available_balance_fn = [](const wallet::CCoinControl&) { return 10 * COIN; };
    wallet_view->get_required_fee_fn = [](unsigned int) { return 1000; };
    wallet_view->get_default_address_type_fn = [] { return OutputType::BECH32; };
    wallet_view->handle_transaction_changed_fn = [](interfaces::Wallet::TransactionChangedFn) {
        return std::unique_ptr<interfaces::Handler>{};
    };
    wallet_view->get_new_destination_fn = [](OutputType, const std::string&) {
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };

    if (configure) configure(*wallet_view);

    return {wallet_view, std::unique_ptr<WalletQmlModel, DrainWalletModel>{new WalletQmlModel(
        WalletQmlModel::SharedWallet{std::move(wallet), std::move(observations), {}, {}, std::move(policy)}, node,
        std::move(services), psbtfee::Services{}, backup::Services{}),
        DrainWalletModel{{}, std::move(manual)}}};
}

WalletModelHarness<MockWallet> MakeConfiguredWalletModel(std::function<void(MockWallet&)> configure,
    interfaces::Node* node = nullptr, wallet::CWallet* raw_wallet = nullptr,
    FeeReadServices services = {}, bool core_policy = false, std::shared_ptr<wallet::CWallet> owner = {}, walletopening::FeePolicy policy = {}, std::shared_ptr<execution::Scheduler> observations = {})
{
    return MakeWalletModel(node, raw_wallet, std::move(services), core_policy, std::move(owner), std::move(configure), std::move(policy), std::move(observations));
}

void SetValidRecipient(WalletQmlModel& model,
                       const QString& address = VALID_MAINNET_ADDRESS,
                       const std::shared_ptr<ManualExecutionEnvironment>& environment = {})
{
    auto* recipient = model.sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(address, 0);
    recipient->amount()->setSatoshi(50'000);

    const auto pump = [&] {
        if (!environment) return;
        environment->drain();
        environment->publishAll();
    };
    QVERIFY2(QTest::qWaitFor([&] { pump(); return recipient->isValid(); }, 5000),
        "Recipient must be valid before scheduling fee estimates");
    QVERIFY2(QTest::qWaitFor([&] { pump(); return model.sendRecipientList()->allValid(); }, 5000),
        "Aggregate draft must be published before scheduling fee estimates");
}

// Settle observation prerequisites without advancing the business debounce.
// These fixtures then own both the captured fee request and its later outcome.
bool SettleManualFeeInputs(WalletQmlModel& model, const std::shared_ptr<ManualExecutionEnvironment>& environment,
                          const std::shared_ptr<ManualFeeReceipts>& receipts)
{
    for (int budget{0}; budget < 128; ++budget) {
        receipts->drain(); environment->publishAll();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        if (environment->turnsIdle() && environment->publicationsIdle() && receipts->publicationCount() == 0 &&
            model.capabilitiesKnown() && model.balanceKnown() && model.availableSendBalanceKnown() &&
            !model.coinSelectionPending() && !model.fees()->policyPending() && model.sendRecipientList()->draftSnapshot()) return true;
    }
    return false;
}

class FakePasswordWallet : public StubWallet
{
private:
    ReviewFunding funding;
    std::mutex m_transaction_mutex;
    std::function<std::set<interfaces::WalletTx>()> m_get_wallet_txs_fn;
    std::map<Txid, interfaces::WalletTxStatus> m_transaction_statuses;
    std::map<Txid, bool> m_transaction_in_mempool;
    std::map<Txid, interfaces::WalletTx> m_transaction_details;

public:
    Txid addBumpableTestTransaction()
    {
        CMutableTransaction original{*signerfixture::Review().GetUnsignedTx()};
        original.vout.front().nValue += 100;
        const auto transaction = MakeTransactionRef(std::move(original));
        const auto txid = transaction->GetHash();
        known_transactions.emplace(txid, transaction);
        return txid;
    }

    void setWalletTxsFn(std::function<std::set<interfaces::WalletTx>()> fn)
    {
        std::lock_guard lock{m_transaction_mutex};
        m_get_wallet_txs_fn = std::move(fn);
    }
    std::set<interfaces::WalletTx> getWalletTxs() override
    {
        std::function<std::set<interfaces::WalletTx>()> callback_snapshot;
        {
            std::lock_guard lock{m_transaction_mutex};
            callback_snapshot = m_get_wallet_txs_fn;
        }
        return callback_snapshot ? callback_snapshot() : std::set<interfaces::WalletTx>{};
    }
    void setTransactionDetails(const interfaces::WalletTx& tx)
    {
        std::lock_guard lock{m_transaction_mutex};
        m_transaction_details[tx.tx->GetHash()] = tx;
    }
    void setTransactionInMempool(const Txid& txid, bool in_mempool)
    {
        std::lock_guard lock{m_transaction_mutex};
        m_transaction_in_mempool[txid] = in_mempool;
    }
    void setTransactionDepth(const Txid& txid, int depth)
    {
        std::lock_guard lock{m_transaction_mutex};
        m_transaction_statuses[txid].depth_in_main_chain = depth;
    }
    void setTransactionAbandoned(const Txid& txid, bool abandoned)
    {
        std::lock_guard lock{m_transaction_mutex};
        m_transaction_statuses[txid].is_abandoned = abandoned;
    }
    TransactionChangedFn transaction_changed_fn;
    std::function<void()> transaction_read_probe;
    std::function<interfaces::WalletTx(const Txid&, interfaces::WalletTxStatus&, bool&)> transaction_details_fn;
    interfaces::WalletTx getWalletTx(const Txid& txid) override
    {
        if (transaction_read_probe) transaction_read_probe();
        std::lock_guard lock{m_transaction_mutex};
        const auto it = m_transaction_details.find(txid);
        return it == m_transaction_details.end() ? interfaces::WalletTx{} : it->second;
    }
    interfaces::WalletTx getWalletTxDetails(const Txid& txid, interfaces::WalletTxStatus& status,
                                            std::vector<std::string>&, std::vector<std::string>&, bool& in_mempool, int&) override
    {
        if (transaction_details_fn) return transaction_details_fn(txid, status, in_mempool);
        if (transaction_read_probe) transaction_read_probe();
        std::lock_guard lock{m_transaction_mutex};
        status = m_transaction_statuses[txid];
        in_mempool = m_transaction_in_mempool.contains(txid) ? m_transaction_in_mempool[txid] : true;
        const auto it = m_transaction_details.find(txid);
        return it == m_transaction_details.end() ? interfaces::WalletTx{} : it->second;
    }

    bool preserve_malformed{false};
    bool encrypted{true};
    bool locked{true};
    bool private_keys_disabled{false};
    bool external_signer{false};
    bool taproot_enabled{true};
    OutputType default_address_type{OutputType::BECH32};
    std::atomic<CAmount> balance{50'000};
    int encrypt_calls{0};
    int change_passphrase_calls{0};
    int backup_calls{0};
    std::string last_backup_path;
    std::vector<std::pair<std::string, std::string>> changed_passphrases;
    int unlock_calls{0};
    int lock_calls{0};
    int commit_calls{0};
    std::function<void()> commit_fn;
    std::function<bool(const std::string&)> write_note_fn;
    std::atomic<QThread*> note_thread{nullptr};
    std::atomic<QThread*> commit_thread{nullptr};
    std::atomic<QThread*> bump_check_thread{nullptr};
    std::atomic<QThread*> bump_prepare_thread{nullptr};
    std::atomic<QThread*> bump_sign_thread{nullptr};
    std::atomic<QThread*> bump_commit_thread{nullptr};
    std::atomic_bool commit_on_gui{false};
    std::atomic_bool note_on_gui{false};
    int create_bump_calls{0};
    int sign_bump_calls{0};
    int commit_bump_calls{0};
    int get_new_destination_calls{0};
    int set_address_receive_request_calls{0};
    std::vector<std::string> unlock_passphrases;
    std::vector<OutputType> new_destination_types;
    std::vector<std::string> new_destination_labels;
    std::vector<interfaces::WalletAddress> wallet_addresses;
    std::vector<std::string> receive_request_ids;
    std::vector<bool> create_transaction_sign_args;
    std::vector<bool> fill_psbt_sign_args;
    bool get_address_result{false};
    std::string get_address_label;
    std::string last_set_address_book_label;
    int set_address_book_calls{0};
    int sign_message_calls{0};
    std::string last_signed_message;
    bool can_bump_transaction{true};
    bool sign_bump_result{true};
    bool commit_bump_result{true};

    std::function<util::Result<CTransactionRef>(const std::vector<wallet::CRecipient>&,
                                                const wallet::CCoinControl&,
                                                bool,
                                                int&,
                                                CAmount&)>
        create_transaction_fn = [](const std::vector<wallet::CRecipient>&,
                                   const wallet::CCoinControl&,
                                   bool,
                                   int& change_pos,
                                   CAmount& fee) {
            change_pos = -1;
            fee = 250;
            return MakeTransactionRef(CMutableTransaction{});
        };
    std::function<util::Expected<void, wallet::WalletError>(const SecureString&)> unlock_fn = [this](const SecureString& passphrase) -> util::Expected<void, wallet::WalletError> {
        ++unlock_calls;
        unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        locked = false;
        return {};
    };
    std::function<util::Result<CTxDestination>(OutputType, const std::string&)> get_new_destination_fn = [this](OutputType type, const std::string& label) -> util::Result<CTxDestination> {
        ++get_new_destination_calls;
        new_destination_types.push_back(type);
        new_destination_labels.push_back(label);
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };
    std::function<std::optional<common::PSBTError>(const common::PSBTFillOptions&,
                                                   size_t*,
                                                   PartiallySignedTransaction&,
                                                   bool&)>
        fill_psbt_fn = [this](const common::PSBTFillOptions& options,
                              size_t*,
                              PartiallySignedTransaction&,
                              bool& complete) {
            fill_psbt_sign_args.push_back(options.sign);
            complete = options.sign;
            return std::nullopt;
        };
    std::map<Txid, CTransactionRef> known_transactions;
    bool tx_status_available{true};
    std::function<bool(const CTxIn&)> txin_is_mine_fn = [](const CTxIn&) {
        return false;
    };
    std::function<bool(const CTxOut&)> txout_is_mine_fn = [](const CTxOut&) {
        return false;
    };
    std::function<SigningResult(const std::string&, const PKHash&, std::string&)>
        sign_message_fn = [this](const std::string& message, const PKHash&, std::string& signature) {
            ++sign_message_calls;
            last_signed_message = message;
            signature = "fake-signature";
            return SigningResult::OK;
        };

    bool encryptWallet(const SecureString& passphrase) override
    {
        ++encrypt_calls;
        encrypted = true;
        locked = true;
        unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        return !passphrase.empty();
    }
    bool isCrypted() override { return encrypted; }
    bool lock() override
    {
        ++lock_calls;
        locked = true;
        return true;
    }
    util::Expected<void, wallet::WalletError> unlock(const SecureString& wallet_passphrase) override { return unlock_fn(wallet_passphrase); }
    bool isLocked() override { return locked; }
    util::Result<CTxDestination> getNewDestination(const OutputType type, const std::string& label) override
    {
        return get_new_destination_fn(type, label);
    }
    util::Expected<void, wallet::WalletError> changeWalletPassphrase(const SecureString& old_passphrase, const SecureString& new_passphrase) override
    {
        ++change_passphrase_calls;
        changed_passphrases.emplace_back(
            std::string(old_passphrase.begin(), old_passphrase.end()),
            std::string(new_passphrase.begin(), new_passphrase.end()));
        if (!old_passphrase.empty() && !new_passphrase.empty() && old_passphrase == SecureString{"secret"}) {
            return {};
        }
        return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("not implemented")}};
    }
    void abortRescan() override {}
    bool backupWallet(const std::string& path) override
    {
        ++backup_calls;
        last_backup_path = path;
        QFile file{QString::fromStdString(path)};
        return file.open(QIODevice::WriteOnly) && file.write("fake-wallet-backup") == 18;
    }
    std::string wallet_name{"fake-wallet"};
    std::string getWalletName() override { return wallet_name; }
    bool getPubKey(const CScript&, const CKeyID&, CPubKey&) override { return false; }
    SigningResult signMessage(const std::string& message, const PKHash& pkhash, std::string& signature) override
    {
        return sign_message_fn(message, pkhash, signature);
    }
    bool isSpendable(const CTxDestination&) override { return false; }
    bool setAddressBook(const CTxDestination&, const std::string& name, const std::optional<wallet::AddressPurpose>&) override
    {
        note_thread = QThread::currentThread();
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) note_on_gui = true;
        last_set_address_book_label = name;
        ++set_address_book_calls;
        return write_note_fn ? write_note_fn(name) : true;
    }
    bool delAddressBook(const CTxDestination&) override { return true; }
    bool getAddress(const CTxDestination&, std::string* name, wallet::AddressPurpose* purpose) override
    {
        if (purpose) *purpose = wallet::AddressPurpose::RECEIVE;
        if (name) {
            *name = get_address_label;
        }
        return get_address_result;
    }
    std::vector<interfaces::WalletAddress> getAddresses() override { return wallet_addresses; }
    std::map<std::string, std::string> saved_receive_requests;
    std::vector<std::string> getAddressReceiveRequests() override
    {
        std::vector<std::string> requests;
        requests.reserve(saved_receive_requests.size());
        for (const auto& [id, blob] : saved_receive_requests) requests.push_back(blob);
        return requests;
    }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string& id, const std::string& blob) override
    {
        ++set_address_receive_request_calls;
        receive_request_ids.push_back(id);
        if (set_address_receive_request_result) {
            if (blob.empty()) saved_receive_requests.erase(id);
            else saved_receive_requests[id] = blob;
        }
        return set_address_receive_request_result;
    }
    bool set_address_receive_request_result{true};
    util::Result<void> displayAddress(const CTxDestination&) override { return {}; }
    bool lockCoin(const COutPoint&, const bool) override { return true; }
    bool unlockCoin(const COutPoint&) override { return true; }
    bool isLockedCoin(const COutPoint&) override { return false; }
    void listLockedCoins(std::vector<COutPoint>& outputs) override { outputs.clear(); }
    util::Result<wallet::CreatedTransactionResult> createTransaction(const std::vector<wallet::CRecipient>& recipients,
                                                    const wallet::CCoinControl& coin_control,
                                                    bool sign,
                                                    std::optional<unsigned int>) override
    {
        create_transaction_sign_args.push_back(sign);
        int change_pos{-1};
        CAmount fee{0};
        auto result = create_transaction_fn(recipients, coin_control, sign, change_pos, fee);
        if (!result) {
            return util::Error{util::ErrorString(result)};
        }
        // Test callbacks often omit output scripts when they only exercise fee
        // or signing behavior. Supply the requested destinations so review can
        // verify its snapshot against a transaction-shaped result.
        CTransactionRef transaction{*result};
        const bool missing_outputs = transaction->vout.empty();
        const bool missing_scripts = transaction->vout.size() == recipients.size()
            && std::all_of(transaction->vout.begin(), transaction->vout.end(), [](const CTxOut& output) {
                return output.scriptPubKey.empty();
            });
        if (missing_outputs || missing_scripts) {
            CMutableTransaction completed{*transaction};
            for (size_t i = 0; i < recipients.size(); ++i) {
                const CScript script{GetScriptForDestination(recipients[i].dest)};
                if (missing_outputs) completed.vout.emplace_back(recipients[i].nAmount - (recipients[i].fSubtractFeeFromAmount ? fee : 0), script);
                else completed.vout[i].scriptPubKey = script;
            }
            auto change = change_pos >= 0 ? std::optional<unsigned int>{static_cast<unsigned int>(change_pos)} : std::nullopt;
            if (!funding.complete(completed, fee, balance.load(), coin_control, change))
                return util::Error{Untranslated("Synthetic fixture inputs do not cover amount plus fee")};
            change_pos = change ? static_cast<int>(*change) : -1;
            transaction = MakeTransactionRef(std::move(completed));
        }
        if (!preserve_malformed && transaction->vin.empty()) {
            CMutableTransaction funded{*transaction};
            auto change = change_pos >= 0 ? std::optional<unsigned int>{static_cast<unsigned int>(change_pos)} : std::nullopt;
            if (!funding.complete(funded, fee, balance.load(), coin_control, change))
                return util::Error{Untranslated("Synthetic fixture inputs do not cover amount plus fee")};
            change_pos = change ? static_cast<int>(*change) : -1;
            transaction = MakeTransactionRef(std::move(funded));
        }
        funding.remember(*transaction, fee, balance.load());
        return wallet::CreatedTransactionResult{
            transaction,
            fee,
            change_pos >= 0 ? std::optional<unsigned int>{static_cast<unsigned int>(change_pos)} : std::nullopt,
            FeeReason::FEE_RATE_ESTIMATOR};
    }
    void commitTransaction(CTransactionRef transaction, const std::vector<std::string>&) override
    {
        commit_thread = QThread::currentThread();
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) commit_on_gui = true;
        ++commit_calls;
        const auto callback = commit_fn;
        if (callback) callback();
        // The public commit fact and its later wallet-record observation are
        // distinct. This ordinary fake models Core recording a successful
        // commit; explicit transaction_details_fn scenarios still override
        // readback to exercise absent/unknown/failed observations.
        interfaces::WalletTx recorded{};
        recorded.tx = std::move(transaction);
        setTransactionDetails(recorded);
    }
    bool transactionCanBeBumped(const Txid&) override {
        bump_check_thread = QThread::currentThread();
        return can_bump_transaction;
    }
    bool createBumpTransaction(const Txid& txid,
                               const wallet::CCoinControl&,
                               std::vector<bilingual_str>&,
                               CAmount& old_fee,
                               CAmount& new_fee,
                               CMutableTransaction& mtx) override
    {
        bump_prepare_thread = QThread::currentThread();
        ++create_bump_calls;
        old_fee = 100;
        new_fee = 200;
        const auto original = getTx(txid);
        if (!original) return false;
        mtx = CMutableTransaction{*original};
        if (!mtx.vout.empty() && mtx.vout.front().nValue > 1) --mtx.vout.front().nValue;
        return true;
    }
    bool signBumpTransaction(CMutableTransaction&) override
    {
        bump_sign_thread = QThread::currentThread();
        ++sign_bump_calls;
        return sign_bump_result;
    }
    bool commitBumpTransaction(const Txid&, CMutableTransaction&&, std::vector<bilingual_str>&, Txid& bumped_txid) override
    {
        bump_commit_thread = QThread::currentThread();
        ++commit_bump_calls;
        bumped_txid = Txid::FromUint256(uint256::ONE);
        return commit_bump_result;
    }
    std::optional<common::PSBTError> fillPSBT(const common::PSBTFillOptions& options,
                                              size_t* n_signed,
                                              PartiallySignedTransaction& psbtx,
                                              bool& complete) override
    {
        return fill_psbt_fn(options, n_signed, psbtx, complete);
    }
    CAmount getBalance() override { return balance; }
    bool tryGetBalances(interfaces::WalletBalances& values, uint256& tip) override {
        values.balance = balance.load(); tip = uint256::ONE; return true;
    }
    CAmount getAvailableBalance(const wallet::CCoinControl& control) override {
        if (const auto value = funding.balance(control)) return *value;
        return balance;
    }
    bool txinIsMine(const CTxIn& txin) override { return txin_is_mine_fn(txin); }
    bool txoutIsMine(const CTxOut& txout) override { return txout.scriptPubKey == funding.own || txout_is_mine_fn(txout); }
    bool tryGetTxStatus(const Txid& txid, interfaces::WalletTxStatus&, int&, int64_t&) override
    {
        return tx_status_available && known_transactions.contains(txid);
    }
    CTransactionRef getTx(const Txid& txid) override
    {
        const auto it = known_transactions.find(txid);
        if (it != known_transactions.end()) return it->second;
        const auto funded = funding.previous.find(txid);
        return funded == funding.previous.end() ? CTransactionRef{} : funded->second;
    }
    CAmount getDebit(const CTxIn&) override { return 0; }
    CAmount getCredit(const CTxOut&) override { return 0; }
    CoinsList listCoins() override { return {}; }
    std::vector<interfaces::WalletTxOut> getCoins(const std::vector<COutPoint>& inputs) override {
        std::vector<interfaces::WalletTxOut> result;
        for (const auto& input : inputs) {
            const auto known = known_transactions.find(input.hash);
            if (known != known_transactions.end() && input.n < known->second->vout.size()) {
                interfaces::WalletTxOut output{}; output.txout = known->second->vout[input.n]; output.depth_in_main_chain = 6;
                result.push_back(std::move(output));
            } else {
                const auto synthetic = funding.coins({input});
                if (synthetic.empty()) return {};
                result.push_back(synthetic.front());
            }
        }
        return result;
    }
    CAmount getRequiredFee(unsigned int) override { return 0; }
    CAmount getMinimumFee(unsigned int, const wallet::CCoinControl&, std::optional<int>*, FeeReason*) override { return 0; }
    unsigned int getConfirmTarget() override { return 6; }
    bool hdEnabled() override { return true; }
    bool canGetAddresses() override { return true; }
    bool privateKeysDisabled() override { return private_keys_disabled; }
    bool taprootEnabled() override { return taproot_enabled; }
    bool hasExternalSigner() override { return external_signer; }
    OutputType getDefaultAddressType() override { return default_address_type; }
    CAmount getMaxTxFee() override { return COIN; }
    void remove() override {}
    std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleAddressBookChanged(AddressBookChangedFn) override { return MakeNoopHandler(); }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn) override
    {
        transaction_changed_fn = std::move(fn);
        return MakeNoopHandler();
    }
    CanGetAddressesChangedFn addresses_changed;
    std::unique_ptr<interfaces::Handler> handleCanGetAddressesChanged(CanGetAddressesChangedFn fn) override { addresses_changed = std::move(fn); return MakeNoopHandler(); }
};

WalletModelHarness<FakePasswordWallet> MakePasswordWalletModel(interfaces::Node* node = nullptr, std::shared_ptr<execution::Scheduler> commands = {}, std::function<void(std::function<void()>)> post = {}, std::shared_ptr<FeeReadTimer> timer = {}, std::function<void(FakePasswordWallet&)> configure = {})
{
    auto wallet = std::make_unique<FakePasswordWallet>();
    if (configure) configure(*wallet);
    FakePasswordWallet* const wallet_view{wallet.get()};
    walletopening::Session::Preferences preferences;
    if (node) {
        preferences.read = [node](const std::string& name) {
            const auto value = node->getPersistentSetting("qml_receive_address_types")[name];
            return value.isStr() ? value.get_str() : std::string{};
        };
        // The fake's explicit persistent store is the test oracle. Production
        // uses ChainPreferences and tests real write failures independently.
        preferences.write = [node](const std::string& name, const std::string& type) {
            auto values = node->getPersistentSetting("qml_receive_address_types");
            if (!values.isObject()) values = common::SettingsValue{UniValue::VOBJ};
            values.pushKV(name, type); node->updateRwSetting("qml_receive_address_types", values); return true;
        };
    }
    const auto hold_publications=std::make_shared<std::atomic_bool>(false);
    FeeReadServices services; services.timer = std::move(timer);
    if(post)services.post=[post=std::move(post),ordinary=presentation::QueuedPost(),hold_publications](auto turn){
        if(hold_publications->load())post(std::move(turn));else ordinary(std::move(turn));
    };
    std::unique_ptr<WalletQmlModel, DrainWalletModel> model{new WalletQmlModel(
        WalletQmlModel::SharedWallet{std::move(wallet), std::move(commands), {}, std::move(preferences)}, node,
        std::move(services), psbtfee::Services{}, backup::Services{}),DrainWalletModel{hold_publications}};
    // This fixture exposes a ready opening. Production construction remains
    // asynchronous; explicit timing tests inject a manual scheduler instead.
    if (!QTest::qWaitFor([&] { return model->capabilitiesKnown() && model->balanceKnown(); }, 3000)) {
        QTest::qFail("Wallet metadata was not published", __FILE__, __LINE__);
    }
    if (!QTest::qWaitFor([&] { return model->property("receiveRequestsLoaded").toBool() && !model->receiveRequestReconciliationPending(); }, 3000)) {
        QTest::qFail("Receive request history was not published", __FILE__, __LINE__);
    }
    hold_publications->store(true);
    return {wallet_view, std::move(model)};
}

WalletModelHarness<FakePasswordWallet> MakeConfiguredPasswordWalletModel(std::function<void(FakePasswordWallet&)> configure,
    interfaces::Node* node = nullptr, std::shared_ptr<execution::Scheduler> commands = {},
    std::function<void(std::function<void()>)> post = {}, std::shared_ptr<FeeReadTimer> timer = {})
{
    return MakePasswordWalletModel(node, std::move(commands), std::move(post), std::move(timer), std::move(configure));
}

template <typename Predicate>
bool AdvanceManualDomainAndGui(ManualRoutedExecution& transport, Predicate complete, size_t budget = 128)
{
    for (size_t turn = 0; turn < budget; ++turn) {
        if (complete()) return true;
        if (transport.step(execution::Lane::Domain)) continue;
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCoreApplication::processEvents(QEventLoop::AllEvents, 0);
        if (complete()) return true;
        if (transport.queuedCountIf([](const auto& queued) { return queued.lane == execution::Lane::Domain; }) == 0) return false;
    }
    return complete();
}

// Later fake state changes share the original Session's Native resource with
// its readers. Publishing metadata remains an explicit subsequent test intent.
bool ConfigurePasswordWalletOnNative(WalletQmlModel& model, std::function<void(FakePasswordWallet&)> configure)
{
    const auto done = std::make_shared<std::atomic_bool>(false);
    const auto succeeded = std::make_shared<std::atomic_bool>(false);
    const auto opening = model.opening();
    if (!opening) return false;
    const auto admission = opening->read([configure = std::move(configure)](interfaces::Wallet& wallet) {
        configure(static_cast<FakePasswordWallet&>(wallet));
        return true;
    }, [done, succeeded](bool result) {
        succeeded->store(result);
        done->store(true);
    }, [done](std::exception_ptr) { done->store(true); }, execution::WorkClass::Action);
    return admission && QTest::qWaitFor([&] { return done->load(); }, 5000) && succeeded->load();
}

// Own the original Session scheduler and hold presentation separately. Idle
// cleanup reservations are not mistaken for leaked effects; shutdown joins both
// the manual command ledger and the real retirement executor.
struct ManualPsbtModel {
    ManualExecutionEnvironment& environment;
    std::shared_ptr<FakePasswordWallet> wallet;
    std::unique_ptr<WalletQmlModel> model;
    std::shared_ptr<BackendExecutor> backend;
    ManualPsbtModel(ManualExecutionEnvironment& env, std::shared_ptr<FakePasswordWallet> original, psbtfee::Lookup lookup, interfaces::Node* node = nullptr,
                    std::function<void(std::function<void()>)> post = {}, std::shared_ptr<execution::Scheduler> commands = {})
        : environment(env), wallet(std::move(original))
    {
        if (!post) post = [&env](auto publication) { env.post(std::move(publication)); };
        FeeReadServices fees{env.scheduler, std::shared_ptr<FeeReadTimer>(&env, [](auto*) {}), {}, post};
        psbtfee::Services proofs{env.scheduler, std::move(lookup), post};
        model = std::make_unique<WalletQmlModel>(WalletQmlModel::SharedWallet{wallet, commands ? std::move(commands) : env.scheduler}, node, std::move(fees), std::move(proofs), backup::Services{});
        backend = model->backendExecutor();
        if (!QTest::qWaitFor([&] {
            settle();
            return model->capabilitiesKnown() && model->property("receiveRequestsLoaded").toBool() &&
                !model->receiveRequestReconciliationPending() && env.turnsIdle() && env.publicationsIdle();
        }, 5000)) QTest::qFail("Manual wallet opening did not settle", __FILE__, __LINE__);
    }
    void settle()
    {
        for (int round = 0; round < 16; ++round) {
            environment.drain(); environment.publishAll();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            if (environment.turnsIdle() && environment.publicationsIdle()) return;
        }
        QTest::qFail("Manual effects did not settle within the bounded phase budget", __FILE__, __LINE__);
    }
    void close()
    {
        if (model) model->beginShutdown();
        if (!QTest::qWaitFor([&] { settle(); return backend->isDrained() && environment.turnsIdle() && environment.publicationsIdle(); }, 10000))
            QTest::qFail("Manual wallet did not drain", __FILE__, __LINE__);
        model.reset(); settle();
    }
    ~ManualPsbtModel() { close(); }
};

// Include the input and output metadata consumed by the activity models,
// even when a test only asserts payment-request reconciliation.
interfaces::WalletTx MakeIncomingPayment(CMutableTransaction tx, std::vector<bool> owned_outputs)
{
    if (tx.vin.empty()) tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    interfaces::WalletTx payment{};
    payment.tx = MakeTransactionRef(std::move(tx));
    payment.txin_is_mine.resize(payment.tx->vin.size(), false);
    payment.txout_is_mine = std::move(owned_outputs);
    payment.txout_is_change.resize(payment.tx->vout.size(), false);
    payment.txout_address.resize(payment.tx->vout.size());
    payment.txout_address_is_mine.resize(payment.tx->vout.size(), false);
    for (size_t i{0}; i < payment.tx->vout.size(); ++i) {
        const auto& output = payment.tx->vout[i];
        const bool has_address = ExtractDestination(output.scriptPubKey, payment.txout_address[i]);
        if (payment.txout_is_mine.at(i)) {
            payment.credit += output.nValue;
            payment.txout_address_is_mine[i] = has_address;
        }
    }
    return payment;
}

void SetPasswordRecipient(WalletQmlModel& model, qint64 satoshis, std::function<void()> pump = {})
{
    auto* recipient = model.sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(satoshis);

    const QPointer<SendRecipient> observer{recipient};
    QVERIFY2(QTest::qWaitFor([&]{if(pump)pump();return !observer||!observer->validationPending();},5000)&&observer&&observer->isValid(),
        "Recipient must be valid before preparing a transaction");
}

PartiallySignedTransaction MakeReviewPsbt()
{
    CMutableTransaction previous_tx;
    previous_tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous_tx.vout.emplace_back(2'000, CScript{} << OP_DROP << OP_TRUE);
    const CTransactionRef previous{MakeTransactionRef(previous_tx)};

    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{previous->GetHash(), 0});
    tx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));

    PartiallySignedTransaction psbt{tx, /*version=*/2};
    psbt.inputs[0].non_witness_utxo = previous;
    return psbt;
}

PartiallySignedTransaction MakeWitnessReviewPsbt(const CScript& witness_script, CAmount output_amount = 1500)
{
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous.vout.emplace_back(2000, GetScriptForDestination(WitnessV0ScriptHash(witness_script)));
    const auto parent = MakeTransactionRef(previous);
    CMutableTransaction transaction;
    transaction.vin.emplace_back(COutPoint{parent->GetHash(), 0});
    transaction.vout.emplace_back(output_amount, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    PartiallySignedTransaction result{transaction, /*version=*/2};
    result.inputs[0].non_witness_utxo = parent;
    result.inputs[0].witness_utxo = parent->vout[0];
    result.inputs[0].witness_script = witness_script;
    return result;
}

CMutableTransaction UnsignedTx(const PartiallySignedTransaction& psbt)
{
    return *psbt.GetUnsignedTx();
}

COutPoint FirstInputPrevout(const PartiallySignedTransaction& psbt)
{
    return UnsignedTx(psbt).vin[0].prevout;
}

void CompleteReviewPsbt(PartiallySignedTransaction& psbt)
{
    psbt.inputs[0].final_script_sig = CScript{} << std::vector<unsigned char>{};
}

QString WritePsbt(const PartiallySignedTransaction& psbt, const QTemporaryDir& temp_dir, const QString& name)
{
    QString path{temp_dir.filePath(name)};
    if (!PsbtQmlModel::SavePsbtToFile(psbt, path).isEmpty()) {
        return {};
    }
    return path;
}
} // namespace

class WalletQmlModelTests : public QObject
{
    Q_OBJECT

private:
    std::unique_ptr<ECC_Context> m_ecc_context;

private Q_SLOTS:
    void loadedStateBelongsToTheControllerSchema()
    {
        const auto& wallet = WalletQmlModel::staticMetaObject;
        QCOMPARE(wallet.indexOfProperty("isWalletLoaded"), -1);
        QCOMPARE(wallet.indexOfSignal("walletIsLoadedChanged()"), -1);
        const auto& controller = WalletQmlController::staticMetaObject;
        const auto index = controller.indexOfProperty("isWalletLoaded");
        QVERIFY(index >= 0);
        const auto property = controller.property(index);
        QCOMPARE(property.metaType().id(), QMetaType::Bool);
        QVERIFY(property.isReadable()); QVERIFY(property.hasNotifySignal());
        QCOMPARE(property.notifySignal().methodSignature(), QByteArray{"isWalletLoadedChanged()"});
    }
    void init()
    {
        // These independent cases reuse one synthetic wallet identity. Keep
        // its legacy and canonical reservation stores isolated between cases;
        // intentional reopen scenarios stay within their original test body.
        QSettings settings;
        settings.remove(QStringLiteral("receiveAddressTypes/fake-wallet"));
        settings.remove(walletsettings::EntryKey(walletsettings::Area::Reservations, walletidentity::Canonical(QStringLiteral("fake-wallet"))));
        settings.sync(); QCOMPARE(settings.status(), QSettings::NoError);
    }
    void reviewRetirementSurvivesThrowingPost_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("manual-transport") << 0;
        QTest::newRow("throw-before-enqueue") << 1;
        QTest::newRow("enqueue-then-throw") << 2;
    }
    void reviewRetirementSurvivesThrowingPost()
    {
        QFETCH(int, mode);
        auto [wallet, model] = MakePasswordWalletModel(); Q_UNUSED(wallet);
        std::deque<std::function<void()>> turns;
        int destroyed{0}, posts{0}; bool shutdown_from_destructor{false};
        std::vector<QPointer<WalletQmlModelTransaction>> reviews;
        model->setReviewRetirementPostForTesting([&](auto turn) {
            ++posts;
            if (mode != 1) turns.push_back(std::move(turn));
            if (mode != 0) throw std::runtime_error{"Review retirement transport refused"};
        });
        QSignalSpy drained{model.get(), &WalletQmlModel::shutdownFinished};
        const auto settle = qScopeGuard([&] {
            model->beginShutdown();
            QVERIFY(QTest::qWaitFor([&] {
                while (!turns.empty()) { auto turn = std::move(turns.front()); turns.pop_front(); turn(); }
                QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                return destroyed == static_cast<int>(reviews.size()) && model->backendExecutor()->isDrained();
            }, 10'000));
        });
        for (int row = 0; row < 65; ++row) {
            walletreview::Snapshot snapshot;
            auto* review = new WalletQmlModelTransaction(snapshot, model->opening(), presentation::QueuedPost(), model.get());
            reviews.emplace_back(review);
            connect(review, &QObject::destroyed, this, [&] {
                ++destroyed;
                // Closing the original wallet from an in-flight view's
                // destructor cannot delete that QObject for a second time.
                if (!std::exchange(shutdown_from_destructor, true)) model->beginShutdown();
            });
            QVERIFY(!review->parent());
            QCOMPARE(QQmlEngine::objectOwnership(review), QQmlEngine::CppOwnership);
        }
        model->close(); QCOMPARE(destroyed, 0); QVERIFY(drained.empty());
        for (const int expected : {32, 64, 65}) {
            if (mode == 1) QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            else {
                QCOMPARE(turns.size(), size_t{1});
                auto turn = std::move(turns.front()); turns.pop_front(); turn();
            }
            QCOMPARE(destroyed, expected);
            if (expected < 65) QVERIFY(drained.empty());
        }
        QCOMPARE(posts, 3); QVERIFY(turns.empty()); QVERIFY(shutdown_from_destructor);
        for (const auto& review : reviews) QVERIFY(!review);
        // Held duplicate primary/fallback deliveries retain only the original
        // once token. They must not rerun a retirement batch or its outcome.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(destroyed, 65); QCOMPARE(posts, 3);
        QTRY_COMPARE_WITH_TIMEOUT(drained.size(), 1, 10'000);
        QVERIFY(model); model->beginShutdown(); QCOMPARE(drained.size(), 1);
    }

    void preparationRejectionKeepsOriginalOutcome_data()
    {
        QTest::addColumn<QString>("mode");
        QTest::newRow("delete-observer") << QStringLiteral("delete");
        QTest::newRow("quiet-close") << QStringLiteral("close");
        QTest::newRow("successor-request") << QStringLiteral("successor");
    }
    void preparationRejectionKeepsOriginalOutcome()
    {
        QFETCH(QString, mode);
        const auto clock = std::make_shared<ManualExecutionEnvironment>();
        const auto effects = std::make_shared<std::atomic_int>(0);
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([effects](FakePasswordWallet& value) {
            const auto unlock = value.unlock_fn;
            value.unlock_fn = [effects, unlock](const SecureString& password) { ++*effects; return unlock(password); };
            const auto create = value.create_transaction_fn;
            value.create_transaction_fn = [effects, create](const auto& recipients, const auto& control, bool sign, int& change, CAmount& fee) {
                ++*effects; return create(recipients, control, sign, change, fee);
            };
        }, nullptr, {}, {}, clock);
        Q_UNUSED(wallet);
        QTRY_VERIFY(!model->coinSelectionPending() && !model->sendRecipientList()->validationPending() && !model->customFeeTargetPending());
        const QPointer<WalletQmlModel> original{model.get()};
        const auto backend = original->backendExecutor();
        bool handled{false};
        quint64 successor{0};
        const auto connection = connect(model.get(), &WalletQmlModel::transactionErrorChanged, this, [&] {
            if (handled || original->transactionError().isEmpty()) return;
            handled = true;
            QCOMPARE(original->preparationState(), WalletQmlModel::PreparationState::PreparationFailed);
            if (mode == QStringLiteral("delete")) delete model.release();
            else if (mode == QStringLiteral("close")) original->close();
            else successor = original->requestPrepareTransactionWithPassphrase(QStringLiteral("secret"));
        });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
        const auto ticket = original->requestPrepareTransactionWithPassphrase(QStringLiteral("secret"));
        QVERIFY(ticket);
        QTRY_VERIFY(handled);
        QCOMPARE(effects->load(), 0);
        if (mode == QStringLiteral("successor")) {
            QVERIFY(successor > ticket);
            QTRY_COMPARE(model->transactionError(), QStringLiteral("Enter at least one valid recipient to continue."));
            QCOMPARE(model->preparationId(), successor);
        } else {
            if (mode == QStringLiteral("delete")) QVERIFY(!original);
            if (original) {
                QVERIFY(!original->observationsAllowed());
                QCOMPARE(original->preparationState(), WalletQmlModel::PreparationState::PreparationClosed);
                QCOMPARE(original->requestPrepareTransaction(), quint64{0});
            }
        }
        if (mode == QStringLiteral("delete")) QTRY_VERIFY(backend->isDrained());
    }
    void preparationStatusHandoffKeepsCapturedDraft()
    {
        const auto clock = std::make_shared<ManualExecutionEnvironment>();
        const auto amount = std::make_shared<std::atomic<CAmount>>(0);
        const auto calls = std::make_shared<std::atomic_int>(0);
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([amount, calls](FakePasswordWallet& value) {
            const auto create = value.create_transaction_fn;
            value.create_transaction_fn = [amount, calls, create](const auto& recipients, const auto& control, bool sign, int& change, CAmount& fee) {
                ++*calls;
                amount->store(std::accumulate(recipients.begin(), recipients.end(), CAmount{0},
                    [](CAmount total, const wallet::CRecipient& recipient) { return total + recipient.nAmount; }));
                return create(recipients, control, sign, change, fee);
            };
        }, nullptr, {}, {}, clock);
        Q_UNUSED(wallet);
        QTRY_VERIFY(!model->coinSelectionPending() && !model->sendRecipientList()->validationPending() && !model->customFeeTargetPending());
        QVERIFY(!AwaitPreparation(*model)); // Seed an actual previous failure status.
        SetPasswordRecipient(*model, 1'000);
        QTRY_VERIFY(!model->coinSelectionPending() && !model->sendRecipientList()->validationPending() && !model->customFeeTargetPending());
        bool edited{false};
        quint64 duplicate{1};
        const auto connection = connect(model.get(), &WalletQmlModel::transactionErrorChanged, this, [&] {
            if (edited || !model->transactionError().isEmpty()) return;
            edited = true;
            QCOMPARE(model->preparationState(), WalletQmlModel::PreparationState::PreparationPending);
            model->sendRecipientList()->currentRecipient()->amount()->setSatoshi(7'000);
            duplicate = model->requestPrepareTransactionWithPassphrase(QStringLiteral("secret"));
        });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(connection); });
        const auto original = model->requestPrepareTransactionWithPassphrase(QStringLiteral("secret"));
        QVERIFY(original);
        QTRY_VERIFY(edited);
        QCOMPARE(duplicate, quint64{0});
        QTRY_VERIFY(!model->preparationBusy());
        QCOMPARE(calls->load(), 1);
        QCOMPARE(amount->load(), CAmount{1'000});
        QCOMPARE(model->preparationState(), WalletQmlModel::PreparationState::PreparationStale);
        QVERIFY(!model->currentTransaction());
        QCOMPARE(model->sendRecipientList()->currentRecipient()->amount()->satoshi(), CAmount{7'000});
        QVERIFY(AwaitPreparation(*model, QStringLiteral("secret")));
        QVERIFY(model->preparationId() > original);
        QCOMPARE(calls->load(), 2);
        QCOMPARE(amount->load(), CAmount{7'000});
        QCOMPARE(model->currentTransaction()->amountAmount()->satoshi(), CAmount{7'000});
    }
    void editedDraftCannotSendPreviouslyReviewedTransaction()
    {
        ManualRoutedExecution transport;
        auto wallet = std::make_shared<FakePasswordWallet>();
        std::atomic_int create_calls{0};
        const auto create = wallet->create_transaction_fn;
        wallet->create_transaction_fn = [&create_calls, create](const auto& recipients, const auto& control,
                                                                 bool sign, int& change, CAmount& fee) {
            ++create_calls;
            return create(recipients, control, sign, change, fee);
        };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, transport.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain drain{transport, &model};
        const auto pump = [&] {
            transport.drain();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        };
        QVERIFY(QTest::qWaitFor([&] {
            pump();
            return model.capabilitiesKnown() && model.balanceKnown() &&
                model.property("receiveRequestsLoaded").toBool() && !model.receiveRequestReconciliationPending();
        }, 5000));

        SetPasswordRecipient(model, 1'000, pump);
        QVERIFY(AwaitPreparation(model, QStringLiteral("secret"), pump));
        QVERIFY(model.currentTransaction());
        QVERIFY(model.currentTransactionCanSend());
        QVERIFY(!model.currentTransactionNeedsRevalidation());
        const QPointer<WalletQmlModelTransaction> approved{model.currentTransaction()};
        const auto initial_create_calls = create_calls.load();
        QVERIFY(initial_create_calls > 0);

        model.setDisplayUnit(static_cast<int>(QmlBitcoinUnits::Unit::SAT));
        QVERIFY(model.currentTransaction() == approved);
        QCOMPARE(model.currentTransaction()->amountAmount()->satoshi(), CAmount{1'000});
        QVERIFY(!model.currentTransactionNeedsRevalidation());
        model.setDisplayUnit(static_cast<int>(QmlBitcoinUnits::Unit::SAT)); // Idempotent presentation update.
        QVERIFY(!model.currentTransactionNeedsRevalidation());

        auto* recipient_amount = model.sendRecipientList()->currentRecipient()->amount();
        recipient_amount->setUnit(BitcoinAmount::Unit::SAT);
        pump();
        QCOMPARE(recipient_amount->satoshi(), CAmount{1'000});
        QVERIFY(model.currentTransaction() == approved);
        QVERIFY(!model.currentTransactionNeedsRevalidation());

        SetPasswordRecipient(model, 2'000, pump);
        QVERIFY(model.currentTransaction() == approved);
        QVERIFY(model.currentTransactionNeedsRevalidation());
        const auto queued_before_rejected_send = transport.queuedCount();
        const auto rejected_send = model.requestSendTransactionWithPassphrase(QStringLiteral("secret"));
        QVERIFY(rejected_send > 0); // The user intent is recorded, but no wallet action is admitted.
        QCOMPARE(model.sendState(), WalletQmlModel::SendState::SendFailed);
        QVERIFY(model.transactionError().contains(QStringLiteral("no longer matches")));
        QVERIFY(model.currentTransaction() == approved);
        QCOMPARE(wallet->commit_calls, 0);
        QCOMPARE(create_calls.load(), initial_create_calls);
        QCOMPARE(transport.queuedCount(), queued_before_rejected_send);

        QVERIFY(AwaitPreparation(model, QStringLiteral("secret"), pump));
        QVERIFY(model.currentTransaction() != approved);
        QCOMPARE(model.currentTransaction()->amountAmount()->satoshi(), CAmount{2'000});
        QVERIFY(!model.currentTransactionNeedsRevalidation());
        QCOMPARE(create_calls.load(), initial_create_calls + 1);
    }
    void recipientIgnoresUserEditsWhileItsListImportsAPaymentRequest()
    {
        ManualRoutedExecution transport;
        auto wallet = std::make_shared<FakePasswordWallet>();
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, transport.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain drain{transport, &model};
        const auto pump = [&] { transport.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); };
        QVERIFY(QTest::qWaitFor([&] { pump(); return model.capabilitiesKnown() && model.balanceKnown() &&
            model.property("receiveRequestsLoaded").toBool() && !model.receiveRequestReconciliationPending(); }, 5000));
        SetPasswordRecipient(model, 1'000, pump);
        auto* recipient = model.sendRecipientList()->currentRecipient();
        const auto address = recipient->address()->address();
        const auto target = model.sendRecipientList()->importTarget(recipient); QVERIFY(target);
        auto source = std::make_shared<paymentimport::Inspection>(); source->kind = paymentimport::Kind::Payment;
        source->payment.address = VALID_MAINNET_ADDRESS; source->payment.amount = 2'000;
        source->payment.message = QStringLiteral("Imported");
        std::optional<paymentimport::Target> facts;
        QVERIFY(target->observe(source, [&](auto value) { facts = value; }, {})); transport.drain(); QVERIFY(facts);
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::Apply, *facts, true};
        std::optional<paymentimport::ChildReply> result;
        QVERIFY(target->dispatch({77, 1}, child, source, [&](auto value) { result = std::move(value); }, {}));
        QVERIFY(model.sendRecipientList()->importPending());
        // While the list imports, the recipient's own setters ignore user input.
        QSignalSpy intents{recipient, &SendRecipient::inputIntentSubmitted};
        recipient->setLabel(QStringLiteral("typed during import"));
        recipient->setMessage(QStringLiteral("typed during import"));
        recipient->setAddress(QStringLiteral("typedaddress"));
        QCOMPARE(recipient->label(), QString{}); QCOMPARE(recipient->message(), QString{});
        QCOMPARE(recipient->address()->address(), address); QVERIFY(intents.empty());
        transport.drain(); QVERIFY(result); QCOMPARE(result->receipt.outcome, paymentimport::Outcome::Applied);
        QVERIFY(QTest::qWaitFor([&] { pump(); return !model.sendRecipientList()->importPending() && model.sendRecipientList()->allValid(); }, 5000));
        QCOMPARE(recipient->message(), QStringLiteral("Imported"));
        // Afterwards input is accepted again.
        recipient->setLabel(QStringLiteral("after import"));
        QCOMPARE(recipient->label(), QStringLiteral("after import")); QVERIFY(!intents.empty());
        QVERIFY(target->acknowledge({77, 1}, child)); pump();
    }
    void heldAtomicImportCannotReuseTheEarlierReviewedSend()
    {
        ManualRoutedExecution transport;
        auto wallet = std::make_shared<FakePasswordWallet>();
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, transport.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain drain{transport, &model};
        const auto pump = [&] { transport.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); };
        QVERIFY(QTest::qWaitFor([&] { pump(); return model.capabilitiesKnown() && model.balanceKnown() &&
            model.property("receiveRequestsLoaded").toBool() && !model.receiveRequestReconciliationPending(); }, 5000));
        SetPasswordRecipient(model, 1'000, pump);
        QVERIFY(AwaitPreparation(model, QStringLiteral("secret"), pump));
        const QPointer<WalletQmlModelTransaction> approved{model.currentTransaction()}; QVERIFY(approved);
        QVERIFY(model.currentTransactionCanSend()); QVERIFY(!model.currentTransactionNeedsRevalidation());
        auto* recipient = model.sendRecipientList()->currentRecipient();
        const auto target = model.sendRecipientList()->importTarget(recipient); QVERIFY(target);
        auto source = std::make_shared<paymentimport::Inspection>(); source->kind = paymentimport::Kind::Payment;
        source->payment.address = VALID_MAINNET_ADDRESS; source->payment.amount = 2'000;
        source->payment.label = QStringLiteral("Public invoice"); source->payment.message = QStringLiteral("Whole batch");
        std::optional<paymentimport::Target> facts;
        QVERIFY(target->observe(source, [&](auto value) { facts = value; }, {})); transport.drain(); QVERIFY(facts);
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::Apply, *facts, true};
        std::optional<paymentimport::ChildReply> result;
        QVERIFY(target->dispatch({77, 1}, child, source, [&](auto value) { result = std::move(value); }, {}));
        QVERIFY(model.sendRecipientList()->importPending()); QVERIFY(model.currentTransactionNeedsRevalidation());
        QCOMPARE(recipient->amount()->satoshi(), CAmount{1'000}); QVERIFY(model.currentTransaction() == approved);
        const auto queued = transport.queuedCount();
        QVERIFY(model.requestSendTransactionWithPassphrase(QStringLiteral("secret")) > 0);
        QCOMPARE(model.sendState(), WalletQmlModel::SendState::SendFailed);
        QVERIFY(model.transactionError().contains(QStringLiteral("no longer matches")));
        QCOMPARE(transport.queuedCount(), queued); QCOMPARE(wallet->commit_calls, 0);
        transport.drain(); QVERIFY(result); QCOMPARE(result->receipt.outcome, paymentimport::Outcome::Applied);
        QVERIFY(QTest::qWaitFor([&] { pump(); return !model.sendRecipientList()->importPending() && model.sendRecipientList()->allValid(); }, 5000));
        QCOMPARE(recipient->amount()->satoshi(), CAmount{2'000}); QCOMPARE(recipient->message(), QStringLiteral("Whole batch"));
        QVERIFY(!model.currentTransaction() || model.currentTransactionNeedsRevalidation());
        QVERIFY(target->acknowledge({77, 1}, child)); pump();
        QVERIFY(AwaitPreparation(model, QStringLiteral("secret"), pump));
        QCOMPARE(model.currentTransaction()->amountAmount()->satoshi(), CAmount{2'000}); QCOMPARE(wallet->commit_calls, 0);
    }
    void heldPrepareCompletionCannotInstallEditedDraft_data()
    {
        QTest::addColumn<bool>("first_attempt_fails");
        QTest::newRow("stale-success") << false;
        QTest::newRow("stale-error") << true;
    }
    void heldPrepareCompletionCannotInstallEditedDraft()
    {
        QFETCH(bool, first_attempt_fails);
        ManualRoutedExecution transport;
        auto wallet = std::make_shared<FakePasswordWallet>();
        std::atomic_int create_calls{0};
        const auto create = wallet->create_transaction_fn;
        wallet->create_transaction_fn = [&create_calls, first_attempt_fails, create](const auto& recipients,
                const auto& control, bool sign, int& change, CAmount& fee) -> util::Result<CTransactionRef> {
            const auto call = sign ? ++create_calls : create_calls.load();
            if (sign && first_attempt_fails && call == 1) return util::Error{Untranslated("held stale failure")};
            return create(recipients, control, sign, change, fee);
        };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, transport.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain drain{transport, &model};
        const auto pump = [&] {
            transport.drain();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        };
        QVERIFY(QTest::qWaitFor([&] {
            pump();
            return model.capabilitiesKnown() && model.balanceKnown() &&
                model.property("receiveRequestsLoaded").toBool() && !model.receiveRequestReconciliationPending();
        }, 5000));
        SetPasswordRecipient(model, 1'000, pump);
        QVERIFY(QTest::qWaitFor([&] {
            pump();
            return !model.coinSelectionPending() && !model.customFeeTargetPending() && !model.feeEstimatePending() &&
                !model.preparationBusy() && !model.securityBusy() && !model.sendBusy();
        }, 5000));

        const auto held = model.requestPrepareTransactionWithPassphrase(QStringLiteral("secret"));
        QVERIFY(held > 0);
        QVERIFY(model.preparationBusy());
        QVERIFY(AdvanceManualDomainAndGui(transport, [&] {
            return transport.queuedCountIf([](const auto& queued) { return queued.lane == execution::Lane::Core; }) > 0;
        }));
        QVERIFY(transport.queuedCount() > 0);
        QCOMPARE(create_calls.load(), 0); // Core has not consumed the captured draft yet.
        model.sendRecipientList()->currentRecipient()->amount()->setSatoshi(2'000);
        QVERIFY(!model.currentTransaction());
        QVERIFY(transport.queuedCount() > 0); // Keep the original Core action held across the edit.
        QCOMPARE(create_calls.load(), 0);
        QCOMPARE(model.requestPrepareTransactionWithPassphrase(QStringLiteral("secret")), quint64{0});
        QCOMPARE(model.preparationId(), held); // A conflicting retry cannot overtake the outstanding action.
        QVERIFY(model.preparationBusy());
        QCOMPARE(create_calls.load(), 0);

        QVERIFY(QTest::qWaitFor([&] {
            pump();
            return !model.preparationBusy();
        }, 5000));
        QCOMPARE(create_calls.load(), 1);
        QVERIFY(model.preparationState() == WalletQmlModel::PreparationState::PreparationStale);
        QVERIFY(!model.currentTransaction());
        QVERIFY(model.transactionError().isEmpty()); // The stale success/error does not own current status.

        QVERIFY(AwaitPreparation(model, QStringLiteral("secret"), pump));
        QCOMPARE(create_calls.load(), 2);
        QVERIFY(model.currentTransaction());
        QCOMPARE(model.currentTransaction()->amountAmount()->satoshi(), CAmount{2'000});
        QVERIFY(!model.currentTransactionNeedsRevalidation());
    }
    void preparationFailureRetainsTheExactCurrentReview()
    {
        ManualRoutedExecution transport;
        auto wallet = std::make_shared<FakePasswordWallet>();
        std::atomic_int create_calls{0};
        const auto create = wallet->create_transaction_fn;
        wallet->create_transaction_fn = [&create_calls, create](const auto& recipients, const auto& control,
                                                                 bool sign, int& change, CAmount& fee) -> util::Result<CTransactionRef> {
            if (++create_calls == 2) return util::Error{Untranslated("successor preparation failed")};
            return create(recipients, control, sign, change, fee);
        };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, transport.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain drain{transport, &model};
        const auto pump = [&] {
            transport.drain();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        };
        QVERIFY(QTest::qWaitFor([&] {
            pump();
            return model.capabilitiesKnown() && model.balanceKnown() &&
                model.property("receiveRequestsLoaded").toBool() && !model.receiveRequestReconciliationPending();
        }, 5000));
        SetPasswordRecipient(model, 1'000, pump);
        QVERIFY(AwaitPreparation(model, QStringLiteral("secret"), pump));
        const QPointer<WalletQmlModelTransaction> approved{model.currentTransaction()};
        QVERIFY(approved);
        const auto original_transaction = approved->getWtx();
        QVERIFY(original_transaction);

        const auto previous_attempt = model.preparationId();
        const auto retry = model.requestPrepareTransactionWithPassphrase(QStringLiteral("secret"));
        QVERIFY(retry > previous_attempt);
        QVERIFY(QTest::qWaitFor([&] {
            pump();
            return !model.preparationBusy();
        }, 5000));
        QCOMPARE(create_calls.load(), 2);
        QVERIFY(model.preparationState() == WalletQmlModel::PreparationState::PreparationFailed);
        QVERIFY(!model.transactionError().isEmpty());
        QVERIFY(model.currentTransaction() == approved);
        QCOMPARE(model.currentTransaction()->getWtx(), original_transaction);
        QVERIFY(!model.currentTransactionNeedsRevalidation());
    }
    void preparationAdmissionKeepsOriginalEffect_data()
    {
        QTest::addColumn<bool>("action");
        QTest::addColumn<bool>("destroy");
        QTest::newRow("resource-close") << false << false;
        QTest::newRow("resource-delete") << false << true;
        QTest::newRow("accepted-action-close") << true << false;
        QTest::newRow("accepted-action-delete") << true << true;
    }
    void preparationAdmissionKeepsOriginalEffect()
    {
        QFETCH(bool, action);
        QFETCH(bool, destroy);
        struct Hook {
            QThread* gui{QThread::currentThread()};
            bool action{false};
            std::atomic_bool armed{false};
            std::atomic_int hits{0};
            std::function<void()> close;
        };
        class Gate final : public execution::Scheduler {
        public:
            Gate(std::shared_ptr<execution::Scheduler> target, std::shared_ptr<Hook> hook)
                : m_target(std::move(target)), m_hook(std::move(hook)) {}
            void boundary(bool action) {
                if (QThread::currentThread() != m_hook->gui || m_hook->action != action || !m_hook->armed.exchange(false)) return;
                ++m_hook->hits;
                m_hook->close();
            }
            execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
                if (spec.lane == execution::Lane::Core && spec.work_class == execution::WorkClass::Action) boundary(true);
                return m_target->submit(std::move(task), spec);
            }
            execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
                boundary(false);
                return m_target->reserveCleanup(std::move(task), spec);
            }
            std::shared_ptr<execution::Scheduler> openActionScope() override {
                auto scope = m_target->openActionScope();
                return scope ? std::make_shared<Gate>(std::move(scope), m_hook) : nullptr;
            }
            bool activateCleanup(execution::TaskId id) override { return m_target->activateCleanup(id); }
            execution::BorrowAdmission borrow(execution::ResourceKey key) override { return m_target->borrow(key); }
            execution::ResourceKey allocateKey() override { return m_target->allocateKey(); }
            size_t pending() const override { return m_target->pending(); }
            void closeOrdinaryAdmission() override { m_target->closeOrdinaryAdmission(); }
            bool cancelQueued(execution::TaskId id) override { return m_target->cancelQueued(id); }
        private:
            std::shared_ptr<execution::Scheduler> m_target;
            std::shared_ptr<Hook> m_hook;
        };
        ManualRoutedExecution transport;
        const auto clock = std::make_shared<ManualExecutionEnvironment>();
        const auto hook = std::make_shared<Hook>();
        hook->action = action;
        const auto gate = std::make_shared<Gate>(transport.scheduler, hook);
        const auto effects = std::make_shared<std::atomic_int>(0);
        FeeReadServices services;
        services.timer = clock;
        auto [wallet, model] = MakeConfiguredWalletModel([effects](MockWallet& value) {
            value.create_transaction_fn = [effects](const std::vector<wallet::CRecipient>&,
                                                    const wallet::CCoinControl&, bool,
                                                    int& change, CAmount& fee) -> util::Result<CTransactionRef> {
                ++*effects; change = -1; fee = 250;
                return MakeTransactionRef(CMutableTransaction{});
            };
        }, nullptr, nullptr, std::move(services), false, {}, {}, gate);
        Q_UNUSED(wallet);
        ManualWalletDrain drain{transport, model.get()};
        auto* recipient = model->sendRecipientList()->currentRecipient();
        recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
        recipient->amount()->setSatoshi(1'000);
        QVERIFY(QTest::qWaitFor([&] {
            transport.drain();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            return model->availableSendBalanceKnown() && !model->coinSelectionPending() &&
                !model->customFeeTargetPending() && model->sendRecipientList()->allValid() && transport.transportIdle();
        }, 5000));
        const QPointer<WalletQmlModel> original{model.get()};
        QSignalSpy changed{model.get(), &WalletQmlModel::preparationChanged};
        hook->close = [&] {
            if (destroy) {
                // Destroy the actual observer inside admission. Waiting for
                // its original local action here would wait on this stack.
                delete model.release();
            } else original->close();
        };
        const auto disarm = qScopeGuard([&] { hook->armed = false; hook->close = {}; });
        hook->armed = true;
        const auto ticket = original->requestPrepareTransaction();
        QVERIFY(ticket);
        changed.clear(); // Isolate the exact admission-time close notification.
        QVERIFY(AdvanceManualDomainAndGui(transport, [&] { return hook->hits.load() == 1; }));
        QCOMPARE(hook->hits.load(), 1);
        QCOMPARE(effects->load(), 0); // An admission handoff never executes Core work inline.
        if (destroy) QVERIFY(!original);
        else QVERIFY(!original->observationsAllowed());
        QCOMPARE(changed.count(), destroy ? 0 : 1); // A surviving facade publishes the close transition once.
        QVERIFY(drain.finish());
        QCOMPARE(effects->load(), action ? 1 : 0);
        if (!destroy) {
            QVERIFY(!original->currentTransaction()); // The accepted late result cannot reinstall a review after close.
            QVERIFY(original->preparationState() == WalletQmlModel::PreparationState::PreparationClosed);
        }
        QVERIFY(transport.quiescent());
    }
    void transactionErrorHandlerCanDeleteOrQuietCloseFacade_data()
    {
        QTest::addColumn<bool>("destroy");QTest::newRow("delete-facade")<<true;QTest::newRow("quiet-close-facade")<<false;
    }
    void transactionErrorHandlerCanDeleteOrQuietCloseFacade()
    {
        QFETCH(bool,destroy);auto [wallet,model]=MakePasswordWalletModel();Q_UNUSED(wallet);
        const QPointer<WalletQmlModel> original{model.get()};QSignalSpy send_notices{model.get(),&WalletQmlModel::sendOperationChanged};
        bool handled{false};QString actual_error;
        connect(model.get(),&WalletQmlModel::transactionErrorChanged,this,[&]{
            if(handled)return;
            handled=true;actual_error=original->transactionError();
            if(destroy)model.reset();else original->close();
        });
        const auto ticket=original->requestSendTransaction();QVERIFY(ticket);QVERIFY(handled);QVERIFY(actual_error.contains(QStringLiteral("Review a transaction")));
        QCOMPARE(send_notices.count(),0);if(destroy)QVERIFY(!original);else{QVERIFY(original);QVERIFY(!original->observationsAllowed());}
    }
    void oldReviewDestroyedHandlerCanDeleteOrQuietCloseFacade_data()
    {
        QTest::addColumn<bool>("destroy");QTest::newRow("delete-facade")<<true;QTest::newRow("quiet-close-facade")<<false;
    }
    void oldReviewDestroyedHandlerCanDeleteOrQuietCloseFacade()
    {
        QFETCH(bool,destroy);auto [wallet,model]=MakePasswordWalletModel();Q_UNUSED(wallet);SetPasswordRecipient(*model,1'000);QVERIFY(AwaitPreparation(*model,"secret"));
        auto* review=model->currentTransaction();QVERIFY(review);const QPointer<WalletQmlModel> original{model.get()};
        QSignalSpy notices{model.get(),&WalletQmlModel::currentTransactionChanged};bool handled{false},coherent{false};
        connect(review,&QObject::destroyed,this,[&]{
            handled=true;coherent=original&&!original->currentTransaction()&&!original->currentTransactionCanSend()&&!original->currentTransactionCanBroadcast()&&original->currentTransactionReviewMessage().isEmpty();
            if(destroy)model.reset();else original->close();
        });
        original->discardCurrentTransaction();QVERIFY(handled);QVERIFY(coherent);QCOMPARE(notices.count(),0);
        if(destroy)QVERIFY(!original);else QVERIFY(!original->observationsAllowed());
    }
    void reentrantDiscardSuccessorKeepsItsFormAndStatus()
    {
        auto [wallet,model]=MakePasswordWalletModel();Q_UNUSED(wallet);SetPasswordRecipient(*model,1'000);QVERIFY(AwaitPreparation(*model,"secret"));
        auto* review=model->currentTransaction();QVERIFY(review);quint64 successor{0};bool empty_at_boundary{false};
        connect(review,&QObject::destroyed,this,[&]{
            empty_at_boundary=!model->currentTransaction();
            successor=model->requestSendTransaction(); // Explicit new failed intent owns its status.
            model->sendRecipientList()->currentRecipient()->amount()->setSatoshi(7'000);
        });
        model->discardCurrentTransaction();QVERIFY(empty_at_boundary);QVERIFY(successor);QCOMPARE(model->sendOperationId(),successor);
        QCOMPARE(model->sendState(),WalletQmlModel::SendState::SendFailed);QVERIFY(model->transactionError().contains(QStringLiteral("Review a transaction")));
        QCOMPARE(model->sendRecipientList()->currentRecipient()->amount()->satoshi(),CAmount{7'000});
        QCOMPARE(model->sendRecipientList()->currentRecipient()->address()->address(),VALID_MAINNET_ADDRESS);QVERIFY(!model->currentTransaction());
    }
    void reentrantUnlockStatusCannotOverwriteSuccessorSend()
    {
        auto [wallet,model]=MakePasswordWalletModel();const auto signed_document=psbtfeefixture::Legacy(2,false);auto document=signed_document;
        for(auto& input:document.inputs)input.final_script_sig.clear();
        wallet->txin_is_mine_fn=[](const CTxIn&){return true;};
        wallet->fill_psbt_fn=[signed_document](const common::PSBTFillOptions& options,size_t* count,PartiallySignedTransaction& candidate,bool& complete){if(count)*count=2;complete=options.sign;if(options.sign)candidate=signed_document;return std::optional<common::PSBTError>{};};
        QTemporaryDir directory;QVERIFY(directory.isValid());QCOMPARE(AwaitPsbtImport(*model,WritePsbt(document,directory,"reentrant-status.psbt")),WalletQmlModel::PsbtImportResult::WalletCanSign);
        bool handled{false},coherent{false},successor_started{false};quint64 successor{0};QList<bool> unlock_notices;
        connect(model.get(),&WalletQmlModel::transactionNeedsUnlockChanged,this,[&]{unlock_notices<<model->transactionNeedsUnlock();});
        connect(model.get(),&WalletQmlModel::transactionErrorChanged,this,[&]{
            if(handled||model->transactionError().isEmpty())return;
            handled=true;coherent=model->transactionNeedsUnlock();
            model->discardCurrentTransaction();
            // Cancellation still owns its original Domain receipt.
            QVERIFY(!model->requestSendTransaction());
        });
        connect(model.get(),&WalletQmlModel::sendOperationChanged,this,[&]{
            if(!handled||successor_started||model->sendBusy()||!model->sendOperation()||!model->sendOperation()->result())return;
            successor_started=true;
            // Reenter the original terminal publication after cancellation;
            // the old publisher must retain this successor's scoped error.
            successor=model->requestSendTransaction();
        });
        const auto original=model->requestSendTransaction();QVERIFY(original);QTRY_VERIFY(handled);QVERIFY(coherent);QTRY_VERIFY(successor);QVERIFY(successor!=original);
        QCOMPARE(model->sendOperationId(),successor);QCOMPARE(model->sendState(),WalletQmlModel::SendState::SendFailed);
        QVERIFY(model->transactionError().contains(QStringLiteral("Review a transaction")));QVERIFY(!model->transactionNeedsUnlock());
        QCOMPARE(unlock_notices,QList<bool>{false});QCOMPARE(wallet->commit_calls,0);QVERIFY(!model->currentTransaction());
    }
    void unknownSweepPreviewKeepsLastMaximumAndNeverFallsBack()
    {
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) { original.locked = false; });
        SetPasswordRecipient(*model, 1000);
        model->useMaximum();
        QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending() && model->estimatedFeeSatoshi() >= 0, FEE_ESTIMATE_TIMEOUT_MS);
        const auto previous = model->sendRecipientList()->currentRecipient()->cAmount();
        QVERIFY(previous > 1000);
        const auto attempts = std::make_shared<std::atomic_int>(0);
        QVERIFY(ConfigurePasswordWalletOnNative(*model, [attempts](FakePasswordWallet& original) {
            original.preserve_malformed = true;
            original.create_transaction_fn = [attempts](const std::vector<wallet::CRecipient>& recipients,
                const wallet::CCoinControl&, bool, int& change, CAmount& fee) -> util::Result<CTransactionRef> {
                ++*attempts; change = -1; fee = 100;
                CMutableTransaction malformed;
                malformed.vout.emplace_back(recipients[0].nAmount - 100, GetScriptForDestination(recipients[0].dest));
                return MakeTransactionRef(std::move(malformed));
            };
        }));
        model->scheduleFeeEstimates();
        QTRY_VERIFY_WITH_TIMEOUT(attempts->load() > 0 && !model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
        QCOMPARE(attempts->load(), 1); // Unknown aborts the Native read, no fallback or later targets.
        QCOMPARE(model->estimatedFeeSatoshi(), qint64{-1});
        QCOMPARE(model->sendRecipientList()->currentRecipient()->cAmount(), previous);
        QVERIFY(!model->currentTransaction());
    }
    void malformedNativeFundingCannotSuppressSweepConfirmation()
    {
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
            original.locked = false;
            original.preserve_malformed = true;
            original.create_transaction_fn = [](const std::vector<wallet::CRecipient>& recipients,
                const wallet::CCoinControl&, bool, int& change, CAmount& fee) -> util::Result<CTransactionRef> {
                change = -1; fee = 100;
                CMutableTransaction malformed;
                malformed.vout.emplace_back(recipients[0].nAmount, GetScriptForDestination(recipients[0].dest));
                return MakeTransactionRef(std::move(malformed));
            };
        });
        SetPasswordRecipient(*model, 1000);
        QVERIFY(!AwaitPreparation(*model));
        QVERIFY(!model->currentTransaction());
        QVERIFY(model->transactionError().contains("remaining spendable balance"));
    }

    void committedSendPublishesTheObservedSubmission_data()
    {
        QTest::addColumn<int>("scenario");
        const char* names[] = {"mempool", "confirmed", "not-in-mempool", "conflicted", "abandoned", "missing", "foreign", "read-error"};
        for (int n{0}; n < 8; ++n) QTest::newRow(names[n]) << n;
    }
    void committedSendPublishesTheObservedSubmission()
    {
        QFETCH(int, scenario); auto [wallet, model] = MakePasswordWalletModel(); SetPasswordRecipient(*model, 1'000);
        model->sendRecipientList()->currentRecipient()->setLabel("retained note");
        QVERIFY(AwaitPreparation(*model, "secret")); const auto transaction = model->currentTransaction()->getWtx();
        const auto observed = std::make_shared<std::atomic<QThread*>>(nullptr);
        wallet->transaction_details_fn = [transaction, scenario, observed](const Txid& id, interfaces::WalletTxStatus& status, bool& mempool) {
            interfaces::WalletTx record{}; if (id != transaction->GetHash()) return record;
            *observed = QThread::currentThread();
            if (scenario == 7) throw std::runtime_error{"read failed after commit"};
            status = {}; status.depth_in_main_chain = scenario == 1 ? 2 : scenario == 3 ? -1 : 0;
            status.is_abandoned = scenario == 4; mempool = scenario == 0;
            if (scenario != 5) record.tx = transaction;
            if (scenario == 6) { CMutableTransaction other{*transaction}; ++other.vout[0].nValue; record.tx = MakeTransactionRef(other); }
            return record;
        };
        model->requestSendTransaction(); QTRY_COMPARE(model->sendState(), WalletQmlModel::SendState::SendCommitted);
        const WalletQmlModel::SendSubmission expected[] = {WalletQmlModel::SendSubmission::SubmissionMempool, WalletQmlModel::SendSubmission::SubmissionConfirmed,
            WalletQmlModel::SendSubmission::SubmissionNotInMempool, WalletQmlModel::SendSubmission::SubmissionConflicted, WalletQmlModel::SendSubmission::SubmissionAbandoned,
            WalletQmlModel::SendSubmission::SubmissionUnknown, WalletQmlModel::SendSubmission::SubmissionUnknown, WalletQmlModel::SendSubmission::SubmissionUnknown};
        QCOMPARE(model->sendSubmission(), expected[scenario]); QCOMPARE(model->sendResultTxid(), QString::fromStdString(transaction->GetHash().GetHex()));
        QCOMPARE(wallet->commit_calls, 1); QVERIFY(observed->load()); QVERIFY(observed->load() != QThread::currentThread());
        QTRY_VERIFY(!model->recipientLabelsPending());
        QCOMPARE(wallet->last_set_address_book_label, scenario < 5 ? std::string{"retained note"} : std::string{});
        QVERIFY(model->transactionError().isEmpty());
    }
    void fullWalletSendRequiresOneMatchingBackendConfirmation_data()
    {
        QTest::addColumn<bool>("cancel");
        QTest::newRow("confirm-original") << false;
        QTest::newRow("cancel-original") << true;
    }
    void fullWalletSendRequiresOneMatchingBackendConfirmation()
    {
        QFETCH(bool, cancel);
        auto [wallet, model] = MakePasswordWalletModel();
        SetPasswordRecipient(*model, 1'000);
        model->useMaximum();
        QTRY_VERIFY(model->sendDraftSweepsWallet());
        QVERIFY(AwaitPreparation(*model, "secret"));
        QVERIFY(model->currentTransactionSweepsWallet());
        const auto reviewed = model->currentTransaction()->txid();
        const auto reviewed_transaction = model->currentTransaction()->getWtx();
        wallet->transaction_details_fn = [reviewed_transaction](const Txid& id, interfaces::WalletTxStatus& status, bool& mempool) {
            interfaces::WalletTx record{};
            if (id == reviewed_transaction->GetHash()) {
                record.tx = reviewed_transaction;
                status = {};
                mempool = true;
            }
            return record;
        };
        const auto ticket = model->requestSendTransaction();
        QVERIFY(ticket);
        QTRY_COMPARE(model->sendState(), WalletQmlModel::SendState::SendNeedsConfirmation);
        QCOMPARE(model->sendOperationId(), ticket);
        QVERIFY(model->sendBusy());
        const auto operation = model->sendOperation();
        QVERIFY(operation);
        QCOMPARE(operation->stage(), walletsend::Stage::AwaitingSweepConsent);
        QCOMPARE(wallet->commit_calls, 0);
        QVERIFY(!operation->result());
        if (cancel) {
            QCOMPARE(model->cancelSendTransaction(ticket), ticket);
            QTRY_COMPARE(model->sendState(), WalletQmlModel::SendState::SendFailed);
            QCOMPARE(wallet->commit_calls, 0);
            QCOMPARE(operation->result()->error, walletsend::Error::Cancelled);
            QCOMPARE(model->confirmSendTransaction(ticket), quint64{0});
        } else {
            QCOMPARE(model->confirmSendTransaction(ticket), ticket);
            QCOMPARE(model->confirmSendTransaction(ticket), quint64{0});
            QTRY_COMPARE(model->sendState(), WalletQmlModel::SendState::SendCommitted);
            QCOMPARE(wallet->commit_calls, 1);
            QVERIFY(operation->result()->candidate);
            QCOMPARE(operation->result()->committed->GetHash(), operation->result()->candidate->GetHash());
            QCOMPARE(model->sendResultTxid(), QString::fromStdString(operation->result()->committed->GetHash().GetHex()));
            QCOMPARE(model->sendRecordState(), WalletQmlModel::SendRecord::SendRecordRecorded);
        }
        QVERIFY(!model->sendBusy());
    }
    void oldNoteCompletionCannotPublishANewerSendOutcome()
    {
        struct Gui {
            std::mutex mutex; std::deque<std::function<void()>> pending;
            void post(std::function<void()> work) { std::lock_guard lock{mutex}; pending.push_back(std::move(work)); }
            bool publish() { std::function<void()> work; { std::lock_guard lock{mutex}; if (pending.empty()) return false; work = std::move(pending.front()); pending.pop_front(); } work(); return true; }
            std::deque<std::function<void()>> takeAll() { std::lock_guard lock{mutex}; return std::exchange(pending, {}); }
        };
        auto gui = std::make_shared<Gui>(); BackendExecutor commands{execution::Options{}};
        auto [wallet, model] = MakePasswordWalletModel(nullptr, commands.scheduler(), [gui](auto work) { gui->post(std::move(work)); }, std::make_shared<ManualExecutionEnvironment>());
        QSemaphore entered, release;
        wallet->write_note_fn = [&](const auto&) { entered.release(); release.acquire(); return true; };
        const auto unblock = qScopeGuard([&] { release.release(); });
        SetPasswordRecipient(*model, 1'000,[gui]{while(gui->publish()) {}}); model->sendRecipientList()->currentRecipient()->setLabel("first note");
        QVERIFY(AwaitPreparation(*model, "secret", [gui] { while (gui->publish()) {} })); model->requestSendTransaction();
        QVERIFY(entered.tryAcquire(1, 5000));
        QTRY_VERIFY(([&] { while (gui->publish()) {} return model->sendState() == WalletQmlModel::SendState::SendCommitted; })());
        QVERIFY(model->recipientLabelsPending()); const auto first_note_txid = model->recipientLabelsTransactionId();
        auto first_send = model->sendOperation();
        release.release();
        QTRY_VERIFY(first_send->result() && first_send->result()->notes && first_send->result()->notes->result());
        std::atomic_bool notice_posted{false};
        QVERIFY(execution::Submit(*commands.scheduler(), [&] { notice_posted = true; }, [] {}, {}, {execution::Lane::Domain}));
        QTRY_VERIFY(notice_posted.load());
        auto old_notices = gui->takeAll();
        QVERIFY(!old_notices.empty());
        // Core work stays serialized. Hold the old GUI notice while a newer
        // original review/action completes, then replay notices independently.
        model->discardCurrentTransaction(); SetPasswordRecipient(*model, 2'000,[gui]{while(gui->publish()) {}});
        QVERIFY(AwaitPreparation(*model, "secret", [gui] { while (gui->publish()) {} })); while (gui->publish()) {}
        model->requestSendTransaction(); QCOMPARE(model->sendState(), WalletQmlModel::SendState::SendPending);
        auto second_send = model->sendOperation();
        QTRY_VERIFY(second_send->result().has_value());
        QCOMPARE(wallet->commit_calls, 2); QVERIFY(model->recipientLabelsPending());
        // The two Domain continuations may post in either order. Dispatch
        // each explicitly; both projections must retain their own identity.
        for (auto& notice : old_notices) notice();
        while (gui->publish()) {}
        QVERIFY(!model->recipientLabelsPending()); QCOMPARE(model->recipientLabelsTransactionId(), first_note_txid);
        QCOMPARE(model->sendState(), WalletQmlModel::SendState::SendCommitted);
        QCOMPARE(model->sendResultTxid(), model->currentTransaction()->txid());
        first_send.reset(); second_send.reset();
        model.reset(); while (gui->publish()) {} commands.shutdown(); QTRY_VERIFY(commands.isDrained());
    }
    void heldSendPublicationCannotResolveToASuccessorReview_data()
    {
        QTest::addColumn<bool>("error"); QTest::newRow("old-success") << false; QTest::newRow("old-error") << true;
    }
    void heldSendPublicationCannotResolveToASuccessorReview()
    {
        QFETCH(bool, error);
        struct Gui {
            std::mutex mutex; std::deque<std::function<void()>> pending;
            void post(std::function<void()> work) { std::lock_guard lock{mutex}; pending.push_back(std::move(work)); }
            bool publish() { std::function<void()> work; { std::lock_guard lock{mutex}; if (pending.empty()) return false; work = std::move(pending.front()); pending.pop_front(); } work(); return true; }
            std::deque<std::function<void()>> takeAll() { std::lock_guard lock{mutex}; return std::exchange(pending, {}); }
        };
        const auto gui = std::make_shared<Gui>(); BackendExecutor commands{execution::Options{}};
        auto [wallet, model] = MakePasswordWalletModel(nullptr, commands.scheduler(), [gui](auto work) { gui->post(std::move(work)); }, std::make_shared<ManualExecutionEnvironment>());
        SetPasswordRecipient(*model, 1'000,[gui]{while(gui->publish()) {}}); QVERIFY(AwaitPreparation(*model, "secret", [gui] { while (gui->publish()) {} }));
        if (error) wallet->commit_fn = [] { throw std::runtime_error{"unknown effect"}; };
        model->requestSendTransaction();
        auto original_send = model->sendOperation();
        QTRY_VERIFY(original_send->result().has_value());
        std::atomic_bool notice_posted{false};
        QVERIFY(execution::Submit(*commands.scheduler(), [&] { notice_posted = true; }, [] {}, {}, {execution::Lane::Domain}));
        QTRY_VERIFY(notice_posted.load());
        auto old_notices = gui->takeAll();
        QVERIFY(!old_notices.empty());
        QCOMPARE(wallet->commit_calls, 1); QCOMPARE(model->sendState(), WalletQmlModel::SendState::SendPending);
        model->discardCurrentTransaction(); SetPasswordRecipient(*model, 2'000,[gui]{while(gui->publish()) {}});
        QVERIFY(AwaitPreparation(*model, "secret", [gui] { while (gui->publish()) {} }));
        const QPointer<WalletQmlModelTransaction> successor{model->currentTransaction()}; const auto txid = successor->txid();
        for (auto& notice : old_notices) notice();
        while (gui->publish()) {}
        QCOMPARE(model->sendState(), WalletQmlModel::SendState::SendClosed);
        QCOMPARE(model->currentTransaction(), successor.data()); QCOMPARE(successor->txid(), txid);
        QVERIFY(model->currentTransactionCanSend()); QVERIFY(model->transactionError().isEmpty());
        original_send.reset();
        model.reset(); while (gui->publish()) {} commands.shutdown(); QTRY_VERIFY(commands.isDrained());
    }
    void asynchronousSendCommitsOffGuiAndPublishesItsActualOutcome()
    {
        auto [wallet, model] = MakePasswordWalletModel(); SetPasswordRecipient(*model, 1'000);
        model->sendRecipientList()->currentRecipient()->setLabel("async original note");
        QVERIFY(AwaitPreparation(*model, "secret")); const auto txid = model->currentTransaction()->txid();
        model->requestSendTransaction(); QCOMPARE(model->sendState(), WalletQmlModel::SendState::SendPending);
        QTRY_COMPARE(model->sendState(), WalletQmlModel::SendState::SendCommitted);
        QVERIFY(!wallet->commit_on_gui.load());
        QCOMPARE(model->sendResultTxid(), txid); QCOMPARE(wallet->commit_calls, 1);
        model->requestSendTransaction(); QCOMPARE(model->sendState(), WalletQmlModel::SendState::SendCommitted); QCOMPARE(wallet->commit_calls, 1);
        QVERIFY(wallet->commit_thread.load() != QThread::currentThread());
        QTRY_VERIFY(!model->recipientLabelsPending());
        QCOMPARE(wallet->last_set_address_book_label, std::string{"async original note"}); QVERIFY(!wallet->note_on_gui.load());
    }
    void asynchronousCommitCannotProjectOntoAReplacementReview()
    {
        auto [wallet, model] = MakePasswordWalletModel(); SetPasswordRecipient(*model, 1'000);
        QVERIFY(AwaitPreparation(*model, "secret"));
        QSemaphore claimed,release;
        wallet->commit_fn=[&]{claimed.release();release.acquire();};
        const auto unblock=qScopeGuard([&]{release.release();});
        QVERIFY(model->requestSendTransaction());
        const auto original=model->sendOperation();QVERIFY(original);
        // Entering the public commit method proves Native already claimed
        // consent. Explicit discard before that boundary instead cancels it.
        QTRY_VERIFY(claimed.available()>0);
        // Consume the Native claim receipt before observing its non-atomic
        // fixture state; available() alone is only a relaxed readiness query.
        QVERIFY(claimed.tryAcquire());QCOMPARE(wallet->commit_calls,1);
        model->discardCurrentTransaction();release.release();
        QTRY_VERIFY(original->result());QVERIFY(original->result()->committed);
        QTRY_COMPARE(model->sendState(), WalletQmlModel::SendState::SendClosed);
        QCOMPARE(wallet->commit_calls, 1); QVERIFY(!model->currentTransaction()); QVERIFY(model->transactionError().isEmpty());
    }
    void asynchronousCommitExceptionDoesNotClaimTransactionSuccess()
    {
        auto [wallet, model] = MakePasswordWalletModel(); SetPasswordRecipient(*model, 1'000);
        QVERIFY(AwaitPreparation(*model, "secret")); wallet->commit_fn = [] { throw std::runtime_error{"unknown commit outcome"}; };
        model->requestSendTransaction(); QTRY_COMPARE(model->sendState(), WalletQmlModel::SendState::SendUncertain);
        QVERIFY(model->sendResultTxid().isEmpty()); QVERIFY(!model->transactionError().isEmpty());
        QCOMPARE(wallet->commit_calls, 1); QVERIFY(!wallet->note_thread.load());
        model->requestSendTransaction(); QCOMPARE(model->sendState(), WalletQmlModel::SendState::SendUncertain); QCOMPARE(wallet->commit_calls, 1);
    }
    void sendingWithoutOutgoingNotesDoesNotCreateMetadataWork()
    {
        auto [wallet, model] = MakePasswordWalletModel(); SetPasswordRecipient(*model, 1'000);
        QVERIFY(AwaitPreparation(*model, "secret")); QVERIFY(AwaitCommittedSend(*model));
        QCOMPARE(wallet->commit_calls, 1); QVERIFY(!model->recipientLabelsPending());
        QVERIFY(model->recipientLabelsError().isEmpty()); QVERIFY(model->recipientLabelsTransactionId().isEmpty());
        QVERIFY(!wallet->note_thread.load());
    }
    void outgoingNotesUseTheBackgroundOwnerAndOriginalTransaction()
    {
        auto [wallet, model] = MakePasswordWalletModel(); SetPasswordRecipient(*model, 1'000);
        model->sendRecipientList()->currentRecipient()->setLabel("original outgoing note");
        QVERIFY(AwaitPreparation(*model, "secret")); const auto txid = model->currentTransaction()->txid();
        QVERIFY(AwaitCommittedSend(*model)); QCOMPARE(wallet->commit_calls, 1);
        QTRY_VERIFY(!model->recipientLabelsPending());
        QCOMPARE(model->recipientLabelsTransactionId(), txid); QVERIFY(model->recipientLabelsError().isEmpty());
        QCOMPARE(wallet->last_set_address_book_label, std::string{"original outgoing note"});
        QVERIFY(wallet->note_thread.load()); QVERIFY(wallet->note_thread.load() != QThread::currentThread());
        QVERIFY(!wallet->note_on_gui.load());
        QCOMPARE(wallet->set_address_receive_request_calls, 0);
    }
    void outgoingNoteFailureDoesNotReportTheCommittedTransactionAsUnsent()
    {
        auto [wallet, model] = MakePasswordWalletModel(); SetPasswordRecipient(*model, 1'000);
        model->sendRecipientList()->currentRecipient()->setLabel("unsaved note"); wallet->write_note_fn = [](const auto&) { return false; };
        QVERIFY(AwaitPreparation(*model, "secret")); QVERIFY(AwaitCommittedSend(*model));
        QTRY_VERIFY(!model->recipientLabelsPending());
        QCOMPARE(wallet->commit_calls, 1); QVERIFY(!model->recipientLabelsError().isEmpty());
        QVERIFY(model->transactionError().isEmpty()); QVERIFY(!model->recipientLabelsTransactionId().isEmpty());
    }
    void reentrantNewReviewCannotSupplyNotesForTheAlreadyCommittedTransaction()
    {
        auto [wallet, model] = MakePasswordWalletModel(); SetPasswordRecipient(*model, 1'000);
        model->sendRecipientList()->currentRecipient()->setLabel("approved note");
        QVERIFY(AwaitPreparation(*model, "secret")); QPointer<WalletQmlModelTransaction> original{model->currentTransaction()};
        QSemaphore claimed,release;
        wallet->commit_fn=[&]{claimed.release();release.acquire();};
        const auto unblock=qScopeGuard([&]{release.release();});
        QVERIFY(model->requestSendTransaction());const auto sender=model->sendOperation();QVERIFY(sender);
        QTRY_VERIFY(claimed.available()>0);
        // Consume the Native claim receipt before observing its non-atomic
        // fixture state; available() alone is only a relaxed readiness query.
        QVERIFY(claimed.tryAcquire());
        // GUI edits stay on GUI while the original Native commit is held.
        // A successor cannot enter the occupied mutation boundary yet.
        model->discardCurrentTransaction();SetPasswordRecipient(*model,1'000);
        model->sendRecipientList()->currentRecipient()->setLabel("successor note");
        QVERIFY(!model->requestPrepareTransaction());QVERIFY(!model->requestSendTransaction());
        release.release();QTRY_VERIFY(sender->result());QVERIFY(sender->result()->committed);
        QTRY_VERIFY(!model->sendBusy());QTRY_VERIFY(!model->recipientLabelsPending());
        QVERIFY(AwaitPreparation(*model,"secret"));
        QVERIFY(!original); QVERIFY(model->currentTransaction());
        QCOMPARE(wallet->commit_calls, 1); QCOMPARE(wallet->last_set_address_book_label, std::string{"approved note"});
        QCOMPARE(model->currentTransaction()->recipientLabels().value(VALID_MAINNET_ADDRESS), QStringLiteral("successor note"));
        QVERIFY(model->currentTransactionCanSend());
    }
    void deletingTheSendingFacadeCannotRetargetOrDiscardItsOutgoingNotes()
    {
        BackendExecutor commands{execution::Options{}};
        auto [wallet, model] = MakePasswordWalletModel(nullptr, commands.scheduler()); SetPasswordRecipient(*model, 1'000);
        model->sendRecipientList()->currentRecipient()->setLabel("retained note");
        const auto count = std::make_shared<std::atomic<int>>(0);
        wallet->write_note_fn = [count](const std::string& note) { if (note == "retained note") ++*count; return true; };
        QVERIFY(AwaitPreparation(*model, "secret")); auto* sender = model.get();
        QSemaphore destroyed;
        wallet->commit_fn = [&] {
            QMetaObject::invokeMethod(qApp, [&] { auto* retired = model.release(); delete retired; destroyed.release(); }, Qt::QueuedConnection);
            destroyed.acquire();
        };
        QVERIFY(sender->requestSendTransaction());
        auto operation = sender->sendOperation();
        QTRY_VERIFY(!model);
        QTRY_VERIFY(operation->result().has_value());
        QVERIFY(operation->result()->committed);
        QTRY_COMPARE(count->load(), 1);
        operation.reset();
        commands.shutdown(); QTRY_VERIFY(commands.isDrained());
    }

    void receiptNotificationCannotReadCoreOnGui();
    void closingDuringReceiptProjectionStopsFurtherWrites();
    void receiptNotificationUsesRealWorkerAndPublishesOriginalRequest();
    void shutdownCompletionSurvivesNestedGuiDelivery()
    {
        WalletQmlModel model{std::make_unique<MockWallet>()};
        QSignalSpy stopped{&model, &WalletQmlModel::shutdownFinished};
        bool nested{false};
        connect(&model, &WalletQmlModel::balanceObservationChanged, this, [&] {
            if (model.balanceState() != WalletQmlModel::BalanceState::Closed) return;
            nested = true;
            // A native handoff may run a nested GUI loop during the close
            // publication. Wait for the actual legacy drain producer.
            QTRY_VERIFY(model.backendExecutor()->isDrained());
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            QCOMPARE(stopped.count(), 1);
        });
        model.beginShutdown();
        QVERIFY(nested);
        QCOMPARE(stopped.count(), 1);
        model.beginShutdown();
        QCOMPARE(stopped.count(), 1);
    }
    void shutdownPublicationMayDestroyItsFacade()
    {
        ManualRoutedExecution env;
        auto model = std::make_unique<WalletQmlModel>(
            WalletQmlModel::SharedWallet{std::make_shared<MockWallet>(), env.scheduler}, nullptr,
            FeeReadServices{}, psbtfee::Services{}, backup::Services{});
        ManualWalletDrain manual_drain{env, model.get()};
        bool destroyed{false};
        connect(model.get(), &WalletQmlModel::balanceObservationChanged, this, [&] {
            if (!model || model->balanceState() != WalletQmlModel::BalanceState::Closed) return;
            model.reset();
            destroyed = true;
        });
        model->beginShutdown();
        QVERIFY(destroyed);
        QVERIFY(!model);
        env.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(manual_drain.finish());
        QVERIFY(env.quiescent());
    }
    void openingShutdownMustWaitForSharedBalanceEffect()
    {
        struct Read {
            QSemaphore entered, release;
            std::atomic_bool finished{false};
            std::atomic<QThread*> effect_thread{nullptr};
        };
        BackendExecutor shared{execution::Options{}};
        const auto read = std::make_shared<Read>();
        const auto release_on_exit = qScopeGuard([&] { read->release.release(); });
        auto wallet = std::make_shared<MockWallet>();
        wallet->try_get_balances_fn = [read](interfaces::WalletBalances& balances, uint256&) {
            read->effect_thread = QThread::currentThread();
            read->entered.release();
            read->release.acquire();
            balances.balance = 42;
            read->finished = true;
            return true;
        };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, shared.scheduler()}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        QTRY_VERIFY(read->entered.available() > 0);
        QVERIFY(read->effect_thread.load() != qApp->thread());
        QSignalSpy stopped{&model, &WalletQmlModel::shutdownFinished};
        model.beginShutdown();
        shared.shutdown();
        QVERIFY(!model.backendExecutor()->isDrained());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        const bool opening_reported_finished = !stopped.empty();
        QVERIFY(shared.scheduler()->pending() > 0);
        QVERIFY(!read->finished.load());
        bool heartbeat{false};
        QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection);
        QTRY_VERIFY(heartbeat);
        read->release.release();
        QTRY_VERIFY(shared.isDrained());
        QTRY_VERIFY(model.backendExecutor()->isDrained());
        QVERIFY(read->finished.load());
        QCOMPARE(stopped.count(), 1);
        QVERIFY2(!opening_reported_finished,
                 "Opening reported shutdownFinished while its shared balance effect was still held on Core");
    }
    void displayedBalanceNeverReadsCoreOnGui()
    {
        struct Reads { std::atomic<int> gui{0}, grouped{0}; };
        struct Wallet final : StubWallet {
            explicit Wallet(std::shared_ptr<Reads> reads) : reads(std::move(reads)) {}
            CAmount getBalance() override
            {
                if (QThread::currentThread() == qApp->thread()) ++reads->gui;
                return 42;
            }
            bool tryGetBalances(interfaces::WalletBalances& balances, uint256& tip) override
            {
                if (QThread::currentThread() == qApp->thread()) ++reads->gui;
                ++reads->grouped;
                balances.balance = 42;
                tip = uint256::ONE;
                return true;
            }
            const std::shared_ptr<Reads> reads;
        };
        const auto reads = std::make_shared<Reads>();
        WalletQmlModel model{std::make_unique<Wallet>(reads)};
        for (int i = 0; i < 100; ++i) {
            (void)model.property("balance");
            (void)model.property("balanceSatoshi");
        }
        QCOMPARE(reads->gui.load(), 0);
        QTRY_COMPARE(model.property("balanceSatoshi").toLongLong(), qint64{42});
        QVERIFY(reads->grouped.load() > 0);
    }
    void busyAndFailedBalanceRefreshRetainKnownValue()
    {
        struct ReadState { std::atomic<int> result{0}; };
        const auto read = std::make_shared<ReadState>();
        auto wallet = std::make_unique<MockWallet>();
        wallet->try_get_balances_fn = [read](interfaces::WalletBalances& balances, uint256&) {
            if (read->result == 1) return false;
            if (read->result == 2) throw std::runtime_error("synthetic grouped-read failure");
            balances.balance = read->result == 3 ? 0 : 42;
            return true;
        };
        WalletQmlModel model{std::move(wallet)};
        QVERIFY(!model.balanceKnown());
        QTRY_COMPARE(model.balanceState(), WalletQmlModel::BalanceState::Ready);
        QVERIFY(model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{42});
        read->result = 1;
        model.refreshBalance();
        QTRY_COMPARE(model.balanceState(), WalletQmlModel::BalanceState::Busy);
        QVERIFY(model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{42});
        read->result = 2;
        model.refreshBalance();
        QTRY_COMPARE(model.balanceState(), WalletQmlModel::BalanceState::Failed);
        QVERIFY(model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{42});
        read->result = 3;
        model.refreshBalance();
        QTRY_COMPARE(model.balanceState(), WalletQmlModel::BalanceState::Ready);
        QVERIFY(model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{0});
        model.beginShutdown();
        QVERIFY(!model.balanceKnown());
        QCOMPARE(model.balanceState(), WalletQmlModel::BalanceState::Closed);
    }
    void balancePublicationDoesNotStartOtherWalletReads()
    {
        const auto gui_reads = std::make_shared<std::atomic<int>>(0);
        const auto count_gui = [gui_reads] {
            if (QThread::currentThread() == qApp->thread()) ++*gui_reads;
        };
        auto wallet = std::make_unique<MockWallet>();
        wallet->get_balance_fn = [] { return 42; };
        wallet->get_wallet_txs_fn = [count_gui] { count_gui(); return std::set<interfaces::WalletTx>{}; };
        wallet->list_coins_fn = [count_gui] { count_gui(); return interfaces::Wallet::CoinsList{}; };
        WalletQmlModel model{std::move(wallet)};
        gui_reads->store(0); // Constructor reads belong to their legacy consumers.
        QSignalSpy contents{&model, &WalletQmlModel::walletContentsChanged};
        QTRY_VERIFY(model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{42});
        QCOMPARE(gui_reads->load(), 0);
        QCOMPARE(contents.count(), 0);
    }
    void balanceUsesInjectedSchedulerAndSeparateGuiPublication()
    {
        ManualRoutedExecution env;
        int balance_effects{0};
        auto wallet = std::make_shared<MockWallet>();
        wallet->get_balance_fn = [] { return 42; };
        wallet->try_get_balances_fn = [&](interfaces::WalletBalances& balances, uint256&) {
            ++balance_effects;
            balances.balance = 42;
            return true;
        };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, env.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain manual_drain{env, &model};
        QSignalSpy balance{&model, &WalletQmlModel::balanceChanged};
        QVERIFY(!model.balanceKnown());
        QCOMPARE(balance_effects, 0);
        // Live child caches reserve cleanup independently of these runnable
        // effects. Identify the original balance task by its actual wallet read,
        // without treating that retained inventory as two runnable jobs.
        std::optional<execution::TaskId> original_read;
        for (int budget = 128; budget > 0 && !original_read; --budget) {
            const auto task = env.dispatchNext(execution::Lane::Core);
            if (task) {
                const auto before = balance_effects;
                QVERIFY(env.finishWork(*task));
                if (balance_effects != before) original_read = task;
            } else {
                QVERIFY(env.step(execution::Lane::Domain));
            }
            QVERIFY(!model.balanceKnown());
            QCOMPARE(balance.count(), 0);
        }
        QVERIFY(original_read);
        QCOMPARE(balance_effects, 1);
        QVERIFY(env.scheduler->phase(*original_read) == execution::Phase::OutcomeReady);
        // Finish only Domain turns, never another wallet read or GUI callback.
        // The exact original result is still invisible until its queued GUI post.
        for (int budget = 128; budget > 0 && env.scheduler->phase(*original_read); --budget) {
            QVERIFY(env.step(execution::Lane::Domain));
            QVERIFY(!model.balanceKnown());
            QCOMPARE(balance.count(), 0);
        }
        QVERIFY(!env.scheduler->phase(*original_read));
        QCOMPARE(balance_effects, 1);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{42});
        QCOMPARE(balance.count(), 1);
        model.refreshBalance();
        QCOMPARE(balance_effects, 1); // Admission cannot execute the refresh inline.
        model.beginShutdown();
        QVERIFY(manual_drain.finish());
        QCOMPARE(balance_effects, 1); // Closing the original queued read fences its effect.
        QCOMPARE(balance.count(), 1);
        QVERIFY(env.quiescent()); // Now the full cleanup/borrower ledger must be zero.
    }
    void emptyAndRetiredBalanceReadersDoNotAdmitWork()
    {
        WalletQmlModel empty;
        empty.refreshBalance();
        QCOMPARE(empty.displayedBalanceSatoshi(), qint64{0});
        QVERIFY(!empty.balanceKnown());
        QCOMPARE(empty.balanceState(), WalletQmlModel::BalanceState::Unavailable);
        ManualRoutedExecution env;
        WalletQmlModel model{WalletQmlModel::SharedWallet{std::make_shared<MockWallet>(), env.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain manual_drain{env, &model};
        QSignalSpy balance{&model, &WalletQmlModel::balanceChanged};
        env.drain(); // Hold a completed observation's GUI wakeup across close.
        model.beginShutdown();
        model.refreshBalance();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(manual_drain.finish());
        QVERIFY(env.quiescent());
        QVERIFY(!model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{0});
        QCOMPARE(balance.count(), 0);
        struct RetainedCallbackWallet final : MockWallet {
            StatusChangedFn retained;
            std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn callback) override
            {
                retained = std::move(callback);
                return MakeNoopHandler();
            }
        };
        // A Core callback lease may retain the bridge after the facade is gone.
        const auto callback_wallet = std::make_shared<RetainedCallbackWallet>();
        auto lost = std::make_unique<WalletQmlModel>(WalletQmlModel::SharedWallet{callback_wallet, env.scheduler}, nullptr,
                                                    FeeReadServices{}, psbtfee::Services{}, backup::Services{});
        manual_drain.add(lost.get());
        QSignalSpy lost_balance{lost.get(), &WalletQmlModel::balanceChanged};
        env.drain(); // A second queued wakeup loses its QObject before delivery.
        lost.reset();
        QVERIFY(callback_wallet->retained);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(lost_balance.count(), 0);
        QVERIFY(manual_drain.finish());
        QVERIFY(env.quiescent());
    }
    void rejectedBalanceProjectionRecoversOnNewInterest()
    {
        ManualRoutedExecution env;
        const auto admission = std::make_shared<CoreObservationGate>(env.scheduler);
        int balance_reads{0};
        struct MetadataWallet final : MockWallet {
            std::atomic_int metadata_reads{0};
            bool canGetAddresses() override { ++metadata_reads; return true; }
        };
        auto wallet = std::make_shared<MetadataWallet>();
        wallet->try_get_balances_fn = [&](interfaces::WalletBalances& balances, uint256&) {
            ++balance_reads; balances.balance = 42; return true;
        };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, admission}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain manual_drain{env, &model};
        QVERIFY(QTest::qWaitFor([&] {
            env.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            return model.capabilitiesKnown() && model.balanceState() == WalletQmlModel::BalanceState::Rejected;
        }, 3000));
        QVERIFY(admission->rejected > 0);
        QCOMPARE(wallet->metadata_reads.load(), 1);
        QCOMPARE(balance_reads, 0); // The FIRST original balance read never executes.
        QVERIFY(!model.balanceKnown());
        admission->reject = false;
        model.refreshBalance();
        QCOMPARE(balance_reads, 0); // New interest is still only admission on GUI.
        QVERIFY(QTest::qWaitFor([&] {
            env.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            return model.balanceState() == WalletQmlModel::BalanceState::Ready;
        }, 3000));
        QCOMPARE(balance_reads, 1);
        QVERIFY(model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{42});
    }
    void heldGuiRetainsFirstValueAlongsideSameBusyState()
    {
        ManualRoutedExecution env;
        auto wallet = std::make_shared<MockWallet>();
        bool busy{true};
        wallet->try_get_balances_fn = [&busy](interfaces::WalletBalances& balances, uint256&) {
            if (busy) return false;
            balances.balance = 42;
            return true;
        };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, env.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain manual_drain{env, &model};
        env.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(model.balanceState(), WalletQmlModel::BalanceState::Busy);
        QVERIFY(!model.balanceKnown());
        QSignalSpy observation{&model, &WalletQmlModel::balanceObservationChanged};
        QSignalSpy balance{&model, &WalletQmlModel::balanceChanged};
        busy = false;
        model.refreshBalance();
        env.drain();
        busy = true;
        model.refreshBalance();
        env.drain();
        QVERIFY(!model.balanceKnown());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(model.balanceState(), WalletQmlModel::BalanceState::Busy);
        QVERIFY(model.balanceKnown());
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{42});
        QCOMPARE(observation.count(), 1);
        QCOMPARE(balance.count(), 1);
    }
    void balanceObservationMayRetireItsFacade_data()
    {
        QTest::addColumn<bool>("destroy");
        QTest::newRow("close") << false;
        QTest::newRow("destroy") << true;
    }
    void balanceObservationMayRetireItsFacade()
    {
        QFETCH(bool, destroy);
        ManualRoutedExecution env;
        auto wallet = std::make_shared<MockWallet>();
        wallet->get_balance_fn = [] { return 42; };
        auto model = std::make_unique<WalletQmlModel>(WalletQmlModel::SharedWallet{wallet, env.scheduler}, nullptr,
                                                     FeeReadServices{}, psbtfee::Services{}, backup::Services{});
        ManualWalletDrain manual_drain{env, model.get()};
        QSignalSpy balance{model.get(), &WalletQmlModel::balanceChanged};
        bool retired{false};
        connect(model.get(), &WalletQmlModel::balanceObservationChanged, this, [&] {
            if (std::exchange(retired, true)) return;
            if (destroy) model.reset();
            else model->beginShutdown();
        });
        env.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(retired);
        QCOMPARE(balance.count(), 0);
        QVERIFY(manual_drain.finish());
        QVERIFY(env.quiescent());
        if (model) QCOMPARE(model->balanceState(), WalletQmlModel::BalanceState::Closed);
    }
    void unloadIngressFencesAnAlreadyQueuedBalancePublication()
    {
        struct Wallet final : MockWallet {
            UnloadFn unloaded;
            std::unique_ptr<interfaces::Handler> handleUnload(UnloadFn callback) override
            {
                unloaded = std::move(callback);
                return MakeNoopHandler();
            }
        };
        ManualRoutedExecution env;
        auto wallet = std::make_shared<Wallet>();
        wallet->get_balance_fn = [] { return 42; };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, env.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain manual_drain{env, &model};
        QSignalSpy balance{&model, &WalletQmlModel::balanceChanged};
        QSignalSpy unloaded{&model, &WalletQmlModel::walletUnloaded};
        env.drain(); // The value's GUI wakeup precedes the unload wakeup.
        wallet->unloaded(); // Ingress fence is already authoritative locally.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(balance.count(), 0);
        QCOMPARE(unloaded.count(), 1);
        QVERIFY(!model.balanceKnown());
        QVERIFY(!model.available());
        env.drain(); // Consume the native unload Control/Domain retirement.
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(manual_drain.finish());
        QVERIFY(env.quiescent());
    }
    void statusNoticeRefreshesBalanceWithoutPublishingItsOldValue()
    {
        struct Wallet final : MockWallet {
            StatusChangedFn changed;
            std::unique_ptr<interfaces::Handler> handleStatusChanged(StatusChangedFn callback) override
            {
                changed = std::move(callback);
                return MakeNoopHandler();
            }
        };
        ManualRoutedExecution env;
        auto wallet = std::make_shared<Wallet>();
        int observed{42}, balance_reads{0};
        wallet->try_get_balances_fn = [&](interfaces::WalletBalances& balances, uint256&) {
            ++balance_reads; balances.balance = observed; return true;
        };
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, env.scheduler}, nullptr,
                             FeeReadServices{}, psbtfee::Services{}, backup::Services{}};
        ManualWalletDrain manual_drain{env, &model};
        QVERIFY(QTest::qWaitFor([&] {
            env.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            return model.capabilitiesKnown() && model.balanceKnown() && env.queuedIdle() && env.activeIdle();
        }, 3000));
        QVERIFY(balance_reads > 0);
        const auto initial_reads = balance_reads;
        QSignalSpy contents{&model, &WalletQmlModel::walletContentsChanged};
        QSignalSpy balance{&model, &WalletQmlModel::balanceChanged};
        observed = 84;
        wallet->changed();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(contents.count(), 0);
        // With startup turns settled, this exact Core turn is the original
        // status metadata read. Its Domain outcome precedes any balance read.
        const auto metadata = env.dispatchNext(execution::Lane::Core);
        QVERIFY(metadata);
        QVERIFY(env.finishWork(*metadata));
        QCOMPARE(balance_reads, initial_reads);
        for (int budget = 128; budget > 0 && env.scheduler->phase(*metadata); --budget) {
            QVERIFY(env.step(execution::Lane::Domain));
        }
        QVERIFY(!env.scheduler->phase(*metadata));
        QCOMPARE(contents.count(), 0);
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(contents.count(), 1);
        QCOMPARE(balance.count(), 0);
        QCOMPARE(balance_reads, initial_reads); // The GUI status handoff only admitted refresh.
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{42});
        env.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(model.displayedBalanceSatoshi(), qint64{84});
        QCOMPARE(balance.count(), 1);
        QCOMPARE(balance_reads, initial_reads + 1);
    }
    void invalidAmountDraftNeverReachesPreparation_data()
    {
        QTest::addColumn<QString>("text");
        QTest::newRow("incomplete") << "1.";
        QTest::newRow("malformed") << "1.2.3";
        QTest::newRow("excess-precision") << "0.000000001";
        QTest::newRow("out-of-range") << "21000000.00000001";
        QTest::newRow("bounded-overflow") << QString(1000, u'9');
    }
    void invalidAmountDraftNeverReachesPreparation()
    {
        QFETCH(QString, text);
        auto [wallet, model] = MakeWalletModel();
        SetValidRecipient(*model);
        auto* amount = model->sendRecipientList()->currentRecipient()->amount();
        amount->setUnit(BitcoinAmount::Unit::BTC);
        const auto before = amount->satoshi();
        [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
        wallet->ExpectNoCalls(wallet->calls.createTransaction);
        amount->editInput(text);
        QCOMPARE(amount->satoshi(), before);
        QVERIFY(!amount->inputAcceptable());
        QVERIFY(!model->sendRecipientList()->allValid());
        QVERIFY(!AwaitPreparation(*model));
        QVERIFY(!AwaitPreparation(*model, "owned fixture password"));
        QVERIFY(model->currentTransaction() == nullptr);
        QVERIFY(!model->transactionError().isEmpty());
        QCOMPARE(wallet->calls.createTransaction.load(), 0);
    }
    void emptyManualSelectionBlocksPreparationBeforeWalletConstruction()
    {
        FeeReadServices services;
        services.read = [](const FeeReadInput&) { return FeeReadResult{}; };
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services));
        SetValidRecipient(*model);
        QVERIFY(model->coinsListModel()->setManualInputSelection(true));
        QTRY_VERIFY(model->coinsListModel()->manualInputSelection() && !model->coinSelectionPending() && !model->customFeeTargetPending());
        const auto constructions = wallet->calls.createTransaction.load();

        const auto ticket = model->requestPrepareTransaction();
        QVERIFY(ticket > 0);
        QTRY_COMPARE(model->preparationState(), WalletQmlModel::PreparationState::PreparationFailed);
        QCOMPARE(model->preparationId(), ticket);
        QVERIFY(model->transactionError().contains(QStringLiteral("Select at least one coin")));
        QVERIFY(!model->currentTransaction());
        QCOMPARE(wallet->calls.createTransaction.load(), constructions);
    }
    void unavailableManualSelectionBlocksPreparationUntilReconciled()
    {
        const COutPoint selected{Txid::FromUint256(uint256::ONE), 7};
        const auto present = std::make_shared<std::atomic_bool>(true);
        FeeReadServices services;
        services.read = [](const FeeReadInput&) { return FeeReadResult{}; };
        auto [wallet, model] = MakeConfiguredWalletModel([selected, present](MockWallet& value) {
            value.list_coins_fn = [selected, present] {
                interfaces::Wallet::CoinsList coins;
                if (present->load()) {
                    const auto destination = DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
                    coins[destination].emplace_back(selected,
                        interfaces::WalletTxOut{CTxOut{10 * COIN, GetScriptForDestination(destination)}, 0, 6, false});
                }
                return coins;
            };
            value.get_available_balance_fn = [](const wallet::CCoinControl&) { return 10 * COIN; };
        }, nullptr, nullptr, std::move(services));
        SetValidRecipient(*model);
        QVERIFY(model->coinsListModel()->setManualInputSelection(true));
        QTRY_VERIFY(model->coinsListModel()->manualInputSelection());
        model->selectCoin(selected);
        QTRY_VERIFY(!model->coinSelectionPending() && model->isSelectedCoin(selected) && !model->customFeeTargetPending());
        QVERIFY(!model->coinSelectionBlocked());

        present->store(false);
        model->coinsListModel()->update();
        QTRY_VERIFY(!model->coinsListModel()->loading() && model->coinsListModel()->selectionReconciliationRequired());
        QVERIFY(model->coinSelectionBlocked());
        QVERIFY(model->selectedCoins()->contains(selected));
        const auto constructions = wallet->calls.createTransaction.load();

        const auto ticket = model->requestPrepareTransaction();
        QVERIFY(ticket > 0);
        QTRY_COMPARE(model->preparationState(), WalletQmlModel::PreparationState::PreparationFailed);
        QCOMPARE(model->preparationId(), ticket);
        QVERIFY(model->transactionError().contains(QStringLiteral("no longer available")));
        QCOMPARE(wallet->calls.createTransaction.load(), constructions);
    }
    void markedFinalInputsDoNotInventSigningAuthority()
    {
        auto document = psbtfeefixture::Legacy(2, false);
        for (auto& input : document.inputs) {
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
            input.non_witness_utxo = nullptr;
            input.final_script_sig = CScript{} << OP_0;
        }
        auto verification = document;
        QVERIFY(!FinalizePSBT(verification));
        QCOMPARE(CountPSBTUnsignedInputs(document), size_t{0}); // Marked final is not cryptographic completion.
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) { original.encrypted = false; });
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        int signing_effects{0};
        wallet->fill_psbt_fn = [&signing_effects](const common::PSBTFillOptions& options, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (options.sign) ++signing_effects;
            if (count) *count = 2;
            complete = false;
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QCOMPARE(AwaitPsbtImport(*model, WritePsbt(document, directory, "invalid-final.psbt")), WalletQmlModel::PsbtImportResult::WalletCannotSign);
        QVERIFY(!model->currentTransaction()->feeKnown());
        QVERIFY(!model->currentTransactionCanSend()); QVERIFY(!model->currentTransactionCanBroadcast());
        QVERIFY(!AwaitCommittedSend(*model)); QCOMPARE(signing_effects, 0);
        model.reset();
    }


    void importViewInterestIgnoresMutableSendDraft_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::newRow("background-validation-and-independent-draft") << 0;
        QTest::newRow("draft-change-during-flow-adoption") << 1;
        QTest::newRow("discard-before-first-review") << 2;
        QTest::newRow("replacement-import-before-old-publication") << 3;
        QTest::newRow("new-prepare-error-during-adoption") << 4;
        QTest::newRow("new-send-error-during-adoption") << 5;
        QTest::newRow("discard-during-flow-adoption") << 6;
        QTest::newRow("discard-during-current-notification") << 7;
    }
    void importViewInterestIgnoresMutableSendDraft()
    {
        QFETCH(int, boundary);
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        auto original = psbtfeefixture::Legacy(2, false);
        auto replacement = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (const auto* document : {&original, &replacement})
            for (const auto& input : document->inputs)
                actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->balance = 200'000; // Both independent draft amounts must be valid before import publication.
        wallet->fill_psbt_fn = [receipts](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            receipts->readStarted();
            if (count) *count = 0;
            complete = true;
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory;
        QString successor_error;
        quint64 successor_command{0};
        bool adoption_seen{false};
        ManualPsbtModel fixture{*env, wallet,
            [&actual](const COutPoint& point) { return std::optional<CTxOut>{actual.at(point)}; }, nullptr,
            [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto& model = fixture.model;
        // Held terminal receipts must be released before closing the original
        // Native owner, including when any assertion exits this function.
        const auto cleanup = qScopeGuard([&] {
            receipts->drain(); receipts->publishAll(); env->publishAll();
            fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll();
        });
        QVERIFY(directory.isValid());
        const auto opening = model->opening()->identity();
        QVERIFY(original.GetUnsignedTx()->GetHash() != replacement.GetUnsignedTx()->GetHash());
        QVERIFY(model->requestPrepareTransaction()); fixture.settle();
        QVERIFY(!model->transactionError().isEmpty());
        const auto original_ticket = model->requestImportPsbtFromFile(WritePsbt(original, directory, "original-interest.psbt"));
        QVERIFY(original_ticket);
        env->publishAll(); // Admission notices cannot consume a terminal result yet.
        const auto read = receipts->startRead(); QVERIFY(read);
        QVERIFY(receipts->finishOutcome(*read));
        QCOMPARE(receipts->publicationCount(), size_t{1});
        QVERIFY(model->psbtActionBusy()); QVERIFY(!model->currentTransaction());

        auto* recipient = model->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
        recipient->amount()->setSatoshi(70'000);
        recipient->setLabel(QStringLiteral("independent send draft"));
        model->refreshBalance();
        QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return model->balanceKnown() && model->sendRecipientList()->allValid() && !recipient->validationPending();
        }, 3000));
        QCOMPARE(receipts->publicationCount(), size_t{1});
        QVERIFY(!model->currentTransaction()); QVERIFY(model->psbtActionBusy());
        const auto flow_connection = connect(model.get(), &WalletQmlModel::currentTransactionFlowChanged, this, [&] {
            if (adoption_seen || !model->currentTransaction()) return;
            adoption_seen = true;
            if (boundary == 1) recipient->amount()->setSatoshi(81'000);
            if (boundary == 6) model->discardCurrentTransaction();
            if (boundary == 4 || boundary == 5) {
                env->scheduler->closeOrdinaryAdmission();
                successor_command = boundary == 4 ? model->requestPrepareTransaction() : model->requestSendTransaction();
                successor_error = model->transactionError();
            }
        });
        const auto disconnect_flow = qScopeGuard([flow_connection] { QObject::disconnect(flow_connection); });
        QSignalSpy changed{model.get(), &WalletQmlModel::currentTransactionChanged};
        QSignalSpy action_changed{model.get(), &WalletQmlModel::psbtActionChanged};
        const auto current_connection = connect(model.get(), &WalletQmlModel::currentTransactionChanged, this, [&] {
            if (boundary == 7 && model->currentTransaction()) model->discardCurrentTransaction();
        });
        const auto disconnect_current = qScopeGuard([current_connection] { QObject::disconnect(current_connection); });
        quint64 replacement_ticket{0};
        if (boundary == 2) model->discardCurrentTransaction(); // No review existed to identify this revocation.
        if (boundary == 3) {
            replacement_ticket = model->requestImportPsbtFromFile(WritePsbt(replacement, directory, "replacement-interest.psbt"));
            QVERIFY(replacement_ticket); QVERIFY(replacement_ticket != original_ticket);
            env->publishAll(); // Flush the new admission notice before its effect.
        }
        QVERIFY(receipts->publish(*read));
        if (boundary == 2) {
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtStale);
            QVERIFY(!model->currentTransaction()); QVERIFY(!adoption_seen);
        } else if (boundary == 6 || boundary == 7) {
            QVERIFY(adoption_seen); QVERIFY(!model->currentTransaction());
            QVERIFY(action_changed.isEmpty());
            QCOMPARE(changed.size(), boundary == 6 ? qsizetype{1} : qsizetype{2});
        } else {
            if (boundary == 3) {
                QCOMPARE(model->psbtActionId(), replacement_ticket);
                QVERIFY(model->psbtActionBusy()); QVERIFY(!model->currentTransaction());
                const auto next = receipts->startRead(); QVERIFY(next); QVERIFY(*next != *read);
                QVERIFY(receipts->finishOutcome(*next)); QVERIFY(receipts->publish(*next));
            }
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtImported);
            QVERIFY(adoption_seen); QVERIFY(model->currentTransaction()); QVERIFY(!changed.isEmpty());
            auto expected = boundary == 3 ? replacement : original;
            CMutableTransaction finalized;
            QVERIFY(FinalizeAndExtractPSBT(expected, finalized));
            QCOMPARE(model->currentTransaction()->getWtx()->GetWitnessHash(), MakeTransactionRef(finalized)->GetWitnessHash());
            QVERIFY(model->currentTransaction()->feeKnown());
            QCOMPARE(recipient->amount()->satoshi(), boundary == 1 ? CAmount{81'000} : CAmount{70'000});
            QCOMPARE(recipient->label(), QStringLiteral("independent send draft"));
            if (boundary == 4 || boundary == 5) {
                QVERIFY(successor_command); QVERIFY(!successor_error.isEmpty());
                QCOMPARE(model->transactionError(), successor_error);
                if (boundary == 4) QCOMPARE(model->preparationId(), successor_command);
                else QCOMPARE(model->sendOperationId(), successor_command);
            } else QVERIFY(model->transactionError().isEmpty());
        }
        QVERIFY(model->opening()->identity() == opening);
        QCOMPARE(wallet->commit_calls, 0);
        fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(env->quiescent());
    }

    void importViewSurvivesPriorReviewFeeRefinement()
    {
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        auto prior = psbtfeefixture::Legacy(2, false);
        auto replacement = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : prior.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
            input.non_witness_utxo = nullptr;
        }
        for (const auto& input : replacement.inputs)
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
        bool lookup_enabled{false};
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->fill_psbt_fn = [receipts](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            receipts->readStarted();
            if (count) *count = 0;
            complete = true;
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory;
        ManualPsbtModel fixture{*env, wallet, [&](const COutPoint& point) -> std::optional<CTxOut> {
            return lookup_enabled ? std::optional<CTxOut>{actual.at(point)} : std::nullopt;
        }, nullptr, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto& model = fixture.model;
        const auto cleanup = qScopeGuard([&] {
            receipts->drain(); receipts->publishAll(); env->publishAll();
            fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll();
        });
        QVERIFY(directory.isValid());
        QVERIFY(prior.GetUnsignedTx()->GetHash() != replacement.GetUnsignedTx()->GetHash());
        QVERIFY(model->requestImportPsbtFromFile(WritePsbt(prior, directory, "prior-unproven.psbt")));
        fixture.settle();
        QVERIFY(model->currentTransaction()); QVERIFY(!model->currentTransaction()->feeKnown());
        const QPointer<WalletQmlModelTransaction> old_review{model->currentTransaction()};
        lookup_enabled = true;
        model->setNode(nullptr); // Refine the previous immutable review before the new import publishes.
        const auto ticket = model->requestImportPsbtFromFile(WritePsbt(replacement, directory, "new-interest.psbt"));
        QVERIFY(ticket);
        receipts->drain();
        QCOMPARE(receipts->publicationCount(), size_t{1});
        QVERIFY(model->psbtActionBusy()); QCOMPARE(model->currentTransaction(), old_review.data());
        env->publishAll();
        QVERIFY(model->currentTransaction()); QVERIFY(model->currentTransaction() != old_review.data());
        QVERIFY(model->currentTransaction()->feeKnown()); QVERIFY(model->psbtActionBusy());
        receipts->publishAll();
        QCOMPARE(model->psbtActionId(), ticket);
        QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtImported);
        QVERIFY(model->currentTransaction());
        auto expected_successor = replacement;
        CMutableTransaction finalized_successor;
        QVERIFY(FinalizeAndExtractPSBT(expected_successor, finalized_successor));
        QCOMPARE(model->currentTransaction()->getWtx()->GetWitnessHash(), MakeTransactionRef(finalized_successor)->GetWitnessHash());
        fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(env->quiescent());
    }

    void psbtAdmissionKeepsOriginalEffectAndView_data()
    {
        QTest::addColumn<bool>("broadcast");
        QTest::addColumn<int>("reservation");
        QTest::addColumn<int>("action");
        for (bool broadcast : {false, true}) {
            for (int reservation{0}; reservation <= (broadcast ? 3 : 2); ++reservation) {
                for (int action : {0, 1, 2}) {
                    const auto name = QStringLiteral("%1-%2-%3").arg(broadcast ? "broadcast" : "save")
                        .arg(reservation ? QStringLiteral("cleanup-%1").arg(reservation) : QStringLiteral("accepted-Core"))
                        .arg(action == 0 ? "quiet" : action == 1 ? "delete" : "withdraw-review").toUtf8();
                    QTest::newRow(name.constData()) << broadcast << reservation << action;
                }
            }
        }
    }
    void psbtAdmissionKeepsOriginalEffectAndView()
    {
        QFETCH(bool, broadcast);
        QFETCH(int, reservation);
        QFETCH(int, action);
        MockNode node;
        ManualExecutionEnvironment environment;
        const auto gate = std::make_shared<AdmissionHookScheduler>(environment.scheduler);
        const auto broadcasts = std::make_shared<std::atomic_int>(0);
        const auto broadcast_hash = std::make_shared<Txid>();
        node.broadcast_transaction_fn = [broadcasts, broadcast_hash](CTransactionRef transaction, CAmount, CFeeRate, std::string&) {
            ++*broadcasts; *broadcast_hash = transaction->GetHash(); return node::TransactionError::OK;
        };
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true; return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory;
        QString original_path, requested_path, retargeted_path;
        ManualPsbtModel fixture{environment, wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }, &node, {}, gate};
        QVERIFY(directory.isValid());
        const auto document = psbtfeefixture::Legacy(2, false);
        const auto replacement = psbtfeefixture::Legacy(2, false);
        const auto source = WritePsbt(document, directory, "original-admission.psbt");
        const auto successor_path = WritePsbt(replacement, directory, "successor-admission.psbt");
        original_path = directory.filePath("original-save.psbt");
        requested_path = original_path;
        retargeted_path = directory.filePath("retargeted-save.psbt");
        QVERIFY(fixture.model->requestImportPsbtFromFile(source)); fixture.settle();
        QVERIFY(fixture.model->currentTransaction());
        QVERIFY(fixture.model->currentTransactionCanBroadcast());
        auto finalized = document; CMutableTransaction signed_transaction;
        QVERIFY(FinalizeAndExtractPSBT(finalized, signed_transaction));
        const QPointer<WalletQmlModel> original{fixture.model.get()};
        QSignalSpy changed{original.data(), &WalletQmlModel::currentTransactionChanged};
        const auto callback = [&] {
            requested_path = retargeted_path;
            if (action == 0) original->close();
            else if (action == 1) fixture.model.reset();
            else original->discardCurrentTransaction();
        };
        const auto trigger = reservation ? gate->armCleanup(reservation, callback)
            : gate->armSubmit(execution::Lane::Core, 1, callback);
        const auto disarm = qScopeGuard([gate] { gate->disarm(); });
        const auto ticket = broadcast ? original->requestBroadcastCurrentTransaction()
            : original->requestSaveCurrentTransactionAsPsbt(requested_path);
        QVERIFY(ticket);
        QCOMPARE(trigger->calls.load(), size_t{1});
        QVERIFY(trigger->originalId.load() != 0);
        QCOMPARE(broadcasts->load(), 0);
        QVERIFY(!QFile::exists(original_path)); QVERIFY(!QFile::exists(retargeted_path));
        if (action == 1) QVERIFY(!original);
        const auto view_notices = changed.size();
        environment.drain(); // Actual admitted Core effect and Domain receipt; GUI remains held.
        const bool accepted = reservation == 0;
        QCOMPARE(broadcasts->load(), accepted && broadcast ? 1 : 0);
        QCOMPARE(QFile::exists(original_path), accepted && !broadcast);
        QVERIFY(!QFile::exists(retargeted_path));
        if (accepted && broadcast) QCOMPARE(*broadcast_hash, signed_transaction.GetHash());
        if (accepted && !broadcast) {
            QFile file{original_path}; QVERIFY(file.open(QIODevice::ReadOnly));
            const auto bytes = file.readAll();
            QFile original_file{source}; QVERIFY(original_file.open(QIODevice::ReadOnly));
            QCOMPARE(bytes, original_file.readAll());
        }
        environment.publishAll(); fixture.settle();
        QCOMPARE(changed.size(), view_notices);
        QCOMPARE(wallet->commit_calls, 0);
        if (action == 2) {
            QVERIFY(original); QVERIFY(!original->currentTransaction());
            if (!accepted) QCOMPARE(original->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtStale);
            else QCOMPARE(original->psbtActionState(), broadcast ? WalletQmlModel::PsbtActionState::PsbtSubmitted : WalletQmlModel::PsbtActionState::PsbtSaved);
            QVERIFY(original->requestImportPsbtFromFile(successor_path)); fixture.settle();
            QVERIFY(original->currentTransaction());
            auto expected_successor = replacement;
            CMutableTransaction finalized_successor;
            QVERIFY(FinalizeAndExtractPSBT(expected_successor, finalized_successor));
            QCOMPARE(original->currentTransaction()->getWtx()->GetWitnessHash(), MakeTransactionRef(finalized_successor)->GetWitnessHash());
        }
        fixture.close();
        QVERIFY(environment.quiescent());
        QVERIFY(!environment.scheduler->phase(execution::TaskId{trigger->originalId.load()}));
    }

    void preparedPsbtSaveContinuesAfterGuiPublicationIsHeld_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::newRow("quiet-gui") << 0;
        QTest::newRow("delete-after-core-admission") << 1;
        QTest::newRow("withdraw-review-before-core-result") << 2;
    }
    void preparedPsbtSaveContinuesAfterGuiPublicationIsHeld()
    {
        QFETCH(int, boundary);
        ManualExecutionEnvironment environment;
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->balance = 2 * COIN;
        wallet->encrypted = false;
        wallet->locked = false;
        int fill_calls{0};
        wallet->fill_psbt_fn = [&](const common::PSBTFillOptions&, size_t* count,
                                   PartiallySignedTransaction&, bool& complete) {
            ++fill_calls;
            if (count) *count = 0;
            complete = false;
            return std::optional<common::PSBTError>{};
        };
        const auto gate = std::make_shared<AdmissionHookScheduler>(environment.scheduler);
        ManualPsbtModel fixture{environment, wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }, nullptr, {}, gate};
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SetValidRecipient(*fixture.model, VALID_MAINNET_ADDRESS,
                          std::shared_ptr<ManualExecutionEnvironment>{&environment, [](auto*) {}});
        QVERIFY(fixture.model->requestPrepareTransaction());
        fixture.settle();
        QVERIFY(fixture.model->currentTransaction());
        const auto reviewed_hash = fixture.model->currentTransaction()->getWtx()->GetHash();
        const auto requested_path = directory.filePath("prepared-save.psbt");

        if (boundary == 1) {
            gate->armSubmit(execution::Lane::Core, 1, [&] { fixture.model.reset(); });
        }
        const auto ticket = fixture.model->requestSaveCurrentTransactionAsPsbt(requested_path);
        QVERIFY(ticket);
        if (boundary == 1) QVERIFY(!fixture.model);
        if (boundary == 2) {
            fixture.model->discardCurrentTransaction();
            QVERIFY(!fixture.model->currentTransaction());
        }
        gate->disarm();
        QVERIFY(!QFile::exists(requested_path));

        // Drain Core and Domain effects while deliberately withholding GUI
        // publications. The Domain-owned export continuation must still admit
        // the immutable PSBT write using its original destination.
        environment.drain();
        QVERIFY(QFile::exists(requested_path));
        QCOMPARE(fill_calls, 1);
        QCOMPARE(wallet->commit_calls, 0);
        QVERIFY(!QFile::exists(directory.filePath("retargeted-save.psbt")));
        QFile saved{requested_path};
        QVERIFY(saved.open(QIODevice::ReadOnly));
        PartiallySignedTransaction exported{CMutableTransaction{}, 2};
        QCOMPARE(psbtwork::LoadFile(requested_path, exported), QString{});
        QCOMPARE(exported.GetUnsignedTx()->GetHash(), reviewed_hash);
        environment.publishAll();
        if (fixture.model) {
            QVERIFY(!fixture.model->psbtActionBusy());
            QCOMPARE(fixture.model->psbtActionId(), ticket);
            QCOMPARE(fixture.model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtSaved);
            QCOMPARE(fixture.model->psbtSavedPath(), requested_path);
        }
        fixture.close();
        QVERIFY(environment.quiescent());
    }

    void preparedPsbtSaveRequiresTypedOverwriteDecision_data()
    {
        QTest::addColumn<bool>("approve");
        QTest::newRow("approve-original-destination") << true;
        QTest::newRow("refuse-original-destination") << false;
    }
    void preparedPsbtSaveRequiresTypedOverwriteDecision()
    {
        QFETCH(bool, approve);
        ManualExecutionEnvironment environment;
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->balance = 2 * COIN;
        wallet->encrypted = false;
        wallet->locked = false;
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count,
                                  PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = false;
            return std::optional<common::PSBTError>{};
        };
        ManualPsbtModel fixture{environment, wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }};
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        SetValidRecipient(*fixture.model, VALID_MAINNET_ADDRESS,
                          std::shared_ptr<ManualExecutionEnvironment>{&environment, [](auto*) {}});
        QVERIFY(fixture.model->requestPrepareTransaction());
        fixture.settle();
        QVERIFY(fixture.model->currentTransaction());
        const auto reviewed_hash = fixture.model->currentTransaction()->getWtx()->GetHash();
        const auto requested_path = directory.filePath("consented-save.psbt");
        const QByteArray sentinel{"existing reviewed file"};
        {
            QFile existing{requested_path};
            QVERIFY(existing.open(QIODevice::WriteOnly));
            QCOMPARE(existing.write(sentinel), sentinel.size());
        }

        const auto ticket = fixture.model->requestSaveCurrentTransactionAsPsbt(requested_path);
        QVERIFY(ticket);
        environment.drain();
        QVERIFY(fixture.model->fileExportOperation());
        environment.publishAll();
        QVERIFY(fixture.model->fileExportOperation()->confirmationRequired());
        QVERIFY(fixture.model->psbtActionBusy());
        {
            QFile existing{requested_path};
            QVERIFY(existing.open(QIODevice::ReadOnly));
            QCOMPARE(existing.readAll(), sentinel);
        }

        QVERIFY(approve ? fixture.model->fileExportOperation()->approve()
                        : fixture.model->fileExportOperation()->refuse());
        environment.drain();
        environment.publishAll();
        QCOMPARE(fixture.model->psbtActionId(), ticket);
        QCOMPARE(fixture.model->psbtActionState(), approve ? WalletQmlModel::PsbtActionState::PsbtSaved
                                                           : WalletQmlModel::PsbtActionState::PsbtFailed);
        QCOMPARE(fixture.model->psbtSavedPath(), approve ? requested_path : QString{});
        QCOMPARE(wallet->commit_calls, 0);
        if (approve) {
            QFile saved{requested_path};
            QVERIFY(saved.open(QIODevice::ReadOnly));
            PartiallySignedTransaction exported{CMutableTransaction{}, 2};
            QCOMPARE(psbtwork::LoadFile(requested_path, exported), QString{});
            QCOMPARE(exported.GetUnsignedTx()->GetHash(), reviewed_hash);
        } else {
            QFile existing{requested_path};
            QVERIFY(existing.open(QIODevice::ReadOnly));
            QCOMPARE(existing.readAll(), sentinel);
        }
        fixture.close();
        QVERIFY(environment.quiescent());
    }

    void signerApprovalAdmissionKeepsOriginalEffectAndPhase_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::addColumn<int>("action");
        for (bool effect : {false, true}) for (int action : {0, 1, 2}) {
            const auto name = QStringLiteral("%1-%2").arg(effect ? "accepted-Core" : "cleanup-reservation")
                .arg(action == 0 ? "quiet" : action == 1 ? "delete" : "withdraw-review").toUtf8();
            QTest::newRow(name.constData()) << int(effect) << action;
        }
        QTest::newRow("accepted-Domain-delete") << 2 << 1;
    }
    void signerApprovalAdmissionKeepsOriginalEffectAndPhase()
    {
        QFETCH(int, boundary);
        QFETCH(int, action);
        ManualExecutionEnvironment environment;
        const auto gate = std::make_shared<AdmissionHookScheduler>(environment.scheduler);
        const auto device_calls = std::make_shared<std::atomic_int>(0);
        const auto signed_document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        auto unsigned_document = *signed_document;
        for (auto& input : unsigned_document.inputs) {
            input.final_script_sig.clear(); input.final_script_witness.SetNull(); input.partial_sigs.clear();
        }
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->external_signer = true; wallet->private_keys_disabled = true;
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        wallet->fill_psbt_fn = [device_calls, signed_document](const common::PSBTFillOptions& options, size_t* count, PartiallySignedTransaction& document, bool& complete) {
            if (count) *count = document.inputs.size();
            complete = options.sign;
            if (options.sign) { ++*device_calls; document = *signed_document; }
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory;
        ManualPsbtModel fixture{environment, wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }, nullptr, {}, gate};
        QVERIFY(directory.isValid());
        QVERIFY(fixture.model->requestImportPsbtFromFile(WritePsbt(unsigned_document, directory, "signer-original.psbt"))); fixture.settle();
        QVERIFY(fixture.model->currentTransaction()); QVERIFY(fixture.model->currentTransaction()->feeKnown());
        const auto replacement = psbtfeefixture::Legacy(2, false);
        const auto successor_path = WritePsbt(replacement, directory, "signer-successor.psbt");
        const QPointer<WalletQmlModel> original{fixture.model.get()};
        QSignalSpy phases{original.data(), &WalletQmlModel::signerApprovalChanged};
        QSignalSpy succeeded{original.data(), &WalletQmlModel::externalSignerApprovalSucceeded};
        QSignalSpy failed{original.data(), &WalletQmlModel::externalSignerApprovalFailed};
        qsizetype phase_at_withdrawal{-1};
        const auto callback = [&] {
            if (action == 0) original->close();
            else if (action == 1) fixture.model.reset();
            else {
                original->discardCurrentTransaction();
                phase_at_withdrawal = phases.size();
            }
        };
        const auto trigger = boundary ? gate->armSubmit(boundary == 2 ? execution::Lane::Domain : execution::Lane::Core, 1, callback)
                                      : gate->armCleanup(1, callback);
        const auto disarm = qScopeGuard([gate] { gate->disarm(); });
        const auto ticket = original->approveExternalSignerTransaction();
        QVERIFY(ticket);
        if (boundary) {
            for (int turn = 0; trigger->calls.load() == 0 && turn < 16; ++turn) QVERIFY(environment.step());
        }
        QCOMPARE(trigger->calls.load(), size_t{1});
        QCOMPARE(device_calls->load(), 0);
        if (action == 1) QVERIFY(!original);
        if (action == 2 && !boundary) {
            QCOMPARE(original->signerApprovalState(), WalletQmlModel::ApprovalState::ApprovalInitial);
            QCOMPARE(phases.size(), phase_at_withdrawal + 1); // Truthful original phase settlement reaches the new empty view.
        }
        environment.drain();
        QCOMPARE(device_calls->load(), boundary == 1 ? 1 : 0);
        QCOMPARE(succeeded.size(), 0); QCOMPARE(failed.size(), 0);
        fixture.settle();
        QCOMPARE(succeeded.size(), 0); QCOMPARE(failed.size(), 0);
        QCOMPARE(wallet->commit_calls, 0);
        if (action == 2) {
            QVERIFY(original); QVERIFY(!original->currentTransaction());
            QCOMPARE(original->signerApprovalState(), WalletQmlModel::ApprovalState::ApprovalInitial);
            QVERIFY(original->requestImportPsbtFromFile(successor_path)); fixture.settle();
            QVERIFY(original->currentTransaction());
            auto expected_successor = replacement;
            CMutableTransaction finalized_successor;
            QVERIFY(FinalizeAndExtractPSBT(expected_successor, finalized_successor));
            QCOMPARE(original->currentTransaction()->getWtx()->GetWitnessHash(), MakeTransactionRef(finalized_successor)->GetWitnessHash());
        }
        fixture.close();
        QVERIFY(environment.quiescent());
        QVERIFY(!environment.scheduler->phase(execution::TaskId{trigger->originalId.load()}));
    }

    void deviceReplacementCannotRetargetAHeldApproval_data()
    {
        QTest::addColumn<int>("boundary");
        QTest::addColumn<bool>("incompatible");
        for (int boundary : {0, 1, 2}) for (bool incompatible : {false, true}) {
            const auto name = QStringLiteral("%1-%2").arg(boundary)
                .arg(incompatible ? "incompatible-keys" : "compatible-keys");
            QTest::newRow(qPrintable(name)) << boundary << incompatible;
        }
    }
    void deviceReplacementCannotRetargetAHeldApproval()
    {
        QFETCH(int, boundary);
        QFETCH(bool, incompatible);
        ManualExecutionEnvironment environment;
        const auto original = psbtfeefixture::Legacy(2, false);
        auto invalid = original;
        const auto foreign = psbtfeefixture::Legacy(2, false);
        for (size_t i = 0; i < invalid.inputs.size(); ++i)
            invalid.inputs[i].final_script_sig = foreign.inputs[i].final_script_sig;
        auto unsigned_document = original;
        for (auto& input : unsigned_document.inputs) input.final_script_sig.clear();
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->external_signer = true; wallet->private_keys_disabled = true;
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        bool replacement_active{false};
        int device_calls{0};
        wallet->fill_psbt_fn = [&](const common::PSBTFillOptions& options, size_t* count, PartiallySignedTransaction& document, bool& complete) {
            if (count) *count = document.inputs.size();
            complete = options.sign;
            if (options.sign) {
                ++device_calls;
                document = replacement_active && incompatible ? invalid : original;
            }
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        ManualPsbtModel fixture{environment, wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }};
        auto& model = *fixture.model;
        QVERIFY(model.requestImportPsbtFromFile(WritePsbt(unsigned_document, directory, "held-device.psbt"))); fixture.settle();
        const QPointer<WalletQmlModelTransaction> review{model.currentTransaction()}; QVERIFY(review);
        const auto reviewed_fee = review->reviewedFee();
        QSignalSpy succeeded{&model, &WalletQmlModel::externalSignerApprovalSucceeded};
        QSignalSpy failed{&model, &WalletQmlModel::externalSignerApprovalFailed};
        QVERIFY(model.approveExternalSignerTransaction());
        QCOMPARE(model.approveExternalSignerTransaction(), quint64{0});
        if (boundary == 1) {
            for (int turn = 0; device_calls == 0 && turn < 16; ++turn) QVERIFY(environment.step());
            QCOMPARE(device_calls, 1); // Stop at the actual signer effect, before its Domain receipt or GUI publication.
        }
        if (boundary == 2) { environment.drain(); QCOMPARE(device_calls, 1); }
        QCOMPARE(model.signerApprovalState(), WalletQmlModel::ApprovalState::ApprovalWaiting);
        QCOMPARE(succeeded.size(), 0); QCOMPARE(failed.size(), 0);
        replacement_active = true;
        environment.drain();
        QCOMPARE(device_calls, 1); // The result is fixed before any GUI publication.
        QVERIFY(environment.duplicateFirstPublication());
        environment.publishAll(); fixture.settle();
        const bool accepted = boundary != 0 || !incompatible;
        QCOMPARE(model.signerApprovalState(), accepted ? WalletQmlModel::ApprovalState::ApprovalSigned : WalletQmlModel::ApprovalState::ApprovalFailed);
        QCOMPARE(succeeded.size(), accepted ? 1 : 0); QCOMPARE(failed.size(), accepted ? 0 : 1);
        QCOMPARE(model.currentTransaction(), review.data());
        QCOMPARE(review->reviewedFee(), reviewed_fee);
        QVERIFY(MatchesUnsignedTransaction(*original.GetUnsignedTx(), CMutableTransaction{*review->getWtx()}));
        QCOMPARE(wallet->commit_calls, 0); QCOMPARE(device_calls, 1);
        fixture.close(); QVERIFY(environment.quiescent());
    }
    void broadcastReceiptKeepsOriginalReview_data()
    {
        QTest::addColumn<uint>("version"); QTest::addColumn<int>("boundary");
        for (uint version : {0U, 2U}) for (int boundary = 0; boundary < 3; ++boundary)
            QTest::newRow(qPrintable(QStringLiteral("v%1-%2").arg(version).arg(boundary))) << version << boundary;
    }
    void broadcastReceiptKeepsOriginalReview()
    {
        QFETCH(uint, version); QFETCH(int, boundary);
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        auto wallet = std::make_shared<FakePasswordWallet>(); wallet->balance = 200'000;
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true; return std::optional<common::PSBTError>{};
        };
        struct Submission { CTransactionRef transaction; int calls{0}; };
        const auto submission = std::make_shared<Submission>();
        MockNode node;
        node.broadcast_transaction_fn = [receipts, submission](CTransactionRef transaction, CAmount, CFeeRate, std::string&) {
            receipts->readStarted(); ++submission->calls; submission->transaction = std::move(transaction);
            return node::TransactionError::OK;
        };
        const auto document = psbtfeefixture::Legacy(version, false);
        auto control = document; CMutableTransaction extracted;
        QVERIFY(FinalizeAndExtractPSBT(control, extracted));
        const auto signed_hash = extracted.GetHash();
        QVERIFY(signed_hash != document.GetUnsignedTx()->GetHash());
        QTemporaryDir directory;
        ManualPsbtModel fixture{*env, wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }, &node,
            [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto& model = fixture.model;
        const auto cleanup = qScopeGuard([&] {
            receipts->drain(); receipts->publishAll(); env->publishAll();
            fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll();
        });
        QVERIFY(directory.isValid());
        QVERIFY(model->requestImportPsbtFromFile(WritePsbt(document, directory, "frozen-broadcast.psbt"))); fixture.settle();
        QVERIFY(model->currentTransaction()); QVERIFY(model->currentTransactionCanBroadcast());
        const QPointer<WalletQmlModelTransaction> review{model->currentTransaction()};
        const auto ticket = model->requestBroadcastCurrentTransaction(); QVERIFY(ticket);
        auto* recipient = model->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        const auto edit = [&] { recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0); recipient->amount()->setSatoshi(7'000); };
        if (boundary == 0) edit();
        const auto read = receipts->startRead(); QVERIFY(read);
        QCOMPARE(submission->calls, 1); QVERIFY(submission->transaction);
        QCOMPARE(submission->transaction->GetHash(), signed_hash);
        QVERIFY(receipts->finishOutcome(*read)); QCOMPARE(receipts->publicationCount(), size_t{1});
        QVERIFY(model->psbtActionBusy());
        QCOMPARE(model->psbtSubmittedTxid(), QString::fromStdString(signed_hash.ToString()));
        QCOMPARE(review->getWtx()->GetHash(), signed_hash);
        if (boundary == 1) edit();
        if (boundary == 2) model->discardCurrentTransaction();
        else QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return model->sendRecipientList()->allValid() && !recipient->validationPending();
        }, 3000));
        QSignalSpy current{model.get(), &WalletQmlModel::currentTransactionChanged};
        QSignalSpy action{model.get(), &WalletQmlModel::psbtActionChanged};
        QVERIFY(receipts->publish(*read));
        QCOMPARE(model->psbtActionId(), ticket); QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtSubmitted);
        QCOMPARE(model->psbtSubmittedTxid(), QString::fromStdString(signed_hash.ToString()));
        QCOMPARE(submission->calls, 1); QCOMPARE(wallet->commit_calls, 0);
        if (boundary == 2) {
            QVERIFY(!model->currentTransaction()); QCOMPARE(current.count(), 0); QCOMPARE(action.count(), 0);
        } else {
            QCOMPARE(model->currentTransaction(), review.data()); QCOMPARE(review->getWtx()->GetHash(), signed_hash);
            QVERIFY(!model->currentTransactionCanSend()); QVERIFY(!model->currentTransactionCanBroadcast());
            QCOMPARE(recipient->amount()->satoshi(), CAmount{7'000}); QCOMPARE(current.count(), 1); QCOMPARE(action.count(), 1);
        }
        fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll(); QVERIFY(env->quiescent());
    }

    void protectedPsbtErrorCannotResumeRevokedView_data()
    {
        QTest::addColumn<bool>("child"); QTest::addColumn<bool>("quiet");
        for (bool child : {false, true}) for (bool quiet : {false, true})
            QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(child ? "child-error" : "status-error").arg(quiet ? "quiet" : "discard"))) << child << quiet;
    }
    void protectedPsbtErrorCannotResumeRevokedView()
    {
        QFETCH(bool, child); QFETCH(bool, quiet);
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        const auto fail = std::make_shared<std::atomic_bool>(false);
        const auto fills = std::make_shared<std::atomic_int>(0);
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->fill_psbt_fn = [receipts, fail, fills](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            receipts->readStarted(); ++*fills;
            if (count) *count = 0;
            complete = true;
            return fail->load() ? std::optional{common::PSBTError::MISSING_INPUTS} : std::optional<common::PSBTError>{};
        };
        const auto document = psbtfeefixture::Legacy(2, false);
        QTemporaryDir directory;
        ManualPsbtModel fixture{*env, wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }, nullptr,
            [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto& model = fixture.model;
        const auto cleanup = qScopeGuard([&] {
            receipts->drain(); receipts->publishAll(); env->publishAll();
            fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll();
        });
        QVERIFY(directory.isValid());
        const auto path = WritePsbt(document, directory, "failed-assessment.psbt");
        QVERIFY(model->requestImportPsbtFromFile(path)); fixture.settle(); QVERIFY(model->currentTransaction());
        fail->store(true);
        const auto ticket = model->requestImportPsbtFromFile(path); QVERIFY(ticket);
        const auto read = receipts->startRead(); QVERIFY(read); QVERIFY(receipts->finishOutcome(*read));
        QCOMPARE(receipts->publicationCount(), size_t{1});
        bool revoked{false};
        const auto revoke = [&] {
            if (revoked) return;
            revoked = true;
            if (quiet) model->close(); else model->discardCurrentTransaction();
        };
        const auto status_connection = connect(model.get(), &WalletQmlModel::transactionErrorChanged, this, [&] {
            if (!child && !model->transactionError().isEmpty()) revoke();
        });
        const auto child_connection = connect(model->importedPsbt(), &PsbtQmlModel::changed, this, [&] {
            if (child && !model->importedPsbt()->error().isEmpty()) revoke();
        });
        const auto disconnect = qScopeGuard([&] { QObject::disconnect(status_connection); QObject::disconnect(child_connection); });
        QSignalSpy current{model.get(), &WalletQmlModel::currentTransactionChanged};
        QSignalSpy action{model.get(), &WalletQmlModel::psbtActionChanged};
        QVERIFY(receipts->publish(*read));
        QVERIFY(revoked); QCOMPARE(fills->load(), 2); QCOMPARE(wallet->commit_calls, 0);
        QCOMPARE(current.count(), quiet ? 0 : 1); QCOMPARE(action.count(), 0);
        if (!child) QVERIFY(model->importedPsbt()->error().isEmpty());
        fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll(); QVERIFY(env->quiescent());
    }

    void saveReceiptSurvivesOriginalFeeRefinement()
    {
        struct FilePublications {
            std::mutex mutex;
            std::deque<std::function<void()>> queued;
            std::atomic_bool hold{false};
            bool publish() {
                std::function<void()> turn;
                { std::lock_guard lock{mutex}; if (queued.empty()) return false; turn=std::move(queued.front());queued.pop_front(); }
                turn();return true;
            }
            size_t count() { std::lock_guard lock{mutex};return queued.size(); }
        };
        const auto files=std::make_shared<FilePublications>();
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        auto document = psbtfeefixture::Legacy(2, false);
        const auto actual = std::make_shared<psbtfee::Outputs>();
        for (auto& input : document.inputs) {
            actual->emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out]; input.non_witness_utxo = nullptr;
        }
        const auto lookup = std::make_shared<std::atomic_bool>(false);
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true; return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory;
        ManualPsbtModel fixture{*env, wallet, [actual, lookup](const COutPoint& point) -> std::optional<CTxOut> {
            return lookup->load() ? std::optional{actual->at(point)} : std::nullopt;
        }, nullptr, [receipts,files](auto publication) {
            if(files->hold.load()){std::lock_guard lock{files->mutex};files->queued.push_back(std::move(publication));}
            else receipts->post(std::move(publication));
        }};
        auto& model = fixture.model;
        const auto cleanup = qScopeGuard([&] {
            files->hold=false;while(files->publish()){}
            receipts->drain(); receipts->publishAll(); env->publishAll();
            fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll();
        });
        QVERIFY(directory.isValid());
        const auto raw = psbtwork::SerializeRaw(document);
        QVERIFY(model->requestImportPsbtFromFile(WritePsbt(document, directory, "refine-source.psbt"))); fixture.settle();
        QVERIFY(model->currentTransaction()); QVERIFY(!model->currentTransaction()->feeKnown());
        const QPointer<WalletQmlModelTransaction> original_review{model->currentTransaction()};
        const auto path = directory.filePath("refine-export.psbt");
        files->hold=true;
        const auto ticket = model->requestSaveCurrentTransactionAsPsbt(path); QVERIFY(ticket);
        std::optional<execution::TaskId> written;
        for (int budget = 256; budget > 0 && !written; --budget) {
            auto packet = env->takeTurnFront(); QVERIFY(packet);
            const auto id = packet->id; packet->turn();
            if (QFileInfo::exists(path)) written = id;
        }
        QVERIFY(written); QVERIFY(receipts->observeReady(*written));
        QVERIFY(receipts->finishOutcome(*written));
        files->hold=false;
        QCOMPARE(model->psbtSavedPath(), path); QVERIFY(model->psbtActionBusy());
        // The shared export owner publishes Checking, Ready, Writing and
        // Succeeded from its Native transition, rather than a fee-read outcome.
        QCOMPARE(files->count(),size_t{4});
        lookup->store(true); model->setNode(nullptr);
        receipts->drain(); env->publishAll();
        QVERIFY(model->currentTransaction()); QVERIFY(model->currentTransaction() != original_review.data());
        QVERIFY(model->currentTransaction()->feeKnown()); QVERIFY(model->psbtActionBusy());
        const QPointer<WalletQmlModelTransaction> refined_review{model->currentTransaction()};
        QSignalSpy current{model.get(), &WalletQmlModel::currentTransactionChanged};
        QSignalSpy action{model.get(), &WalletQmlModel::psbtActionChanged};
        for(int phase=0;phase<3;++phase){QVERIFY(files->publish());QVERIFY(model->psbtActionBusy());}
        QCOMPARE(action.count(),3);action.clear();
        QVERIFY(files->publish());QCOMPARE(files->count(),size_t{0});
        QCOMPARE(model->psbtActionId(), ticket); QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtSaved);
        QCOMPARE(model->psbtSavedPath(), path); QCOMPARE(current.count(), 0); QCOMPARE(action.count(), 1);
        QCOMPARE(model->currentTransaction(),refined_review.data());
        QVERIFY(model->currentTransaction()->feeKnown());
        QFile saved{path}; QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), raw);
        QCOMPARE(wallet->commit_calls, 0);
        fixture.close(); receipts->drain(); receipts->publishAll(); env->publishAll(); QVERIFY(env->quiescent());
    }

    void authenticatedImportedFeeKeepsOwnedOutputsVisible()
    {
        ManualExecutionEnvironment environment;
        auto document = psbtfeefixture::Legacy(2, false, true);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out]; input.non_witness_utxo = nullptr;
        }
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        wallet->txout_is_mine_fn = [](const CTxOut& output) { return output.nValue == 200'000; };
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true; return std::optional<common::PSBTError>{};
        };
        int lookups{0};
        ManualPsbtModel fixture{environment, wallet, [&](const COutPoint& outpoint) { ++lookups; return std::optional<CTxOut>{actual.at(outpoint)}; }};
        auto& model = fixture.model;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto ticket = model->requestImportPsbtFromFile(WritePsbt(document, directory, "change.psbt"));
        QVERIFY(ticket); QVERIFY(model->psbtActionBusy()); QCOMPARE(lookups, 0); QVERIFY(!model->currentTransaction());
        environment.drain(); // Effect and Domain outcome, presentation held.
        QCOMPARE(lookups, 2); QVERIFY(model->psbtActionBusy()); QVERIFY(!model->currentTransaction());
        fixture.settle();
        QCOMPARE(model->psbtActionId(), ticket);
        QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtImported);
        QVERIFY(model->currentTransactionCanSend()); QVERIFY(model->currentTransactionCanBroadcast());
        // Public ownership is not authoritative change intent for an imported
        // document. Keep the owned self-transfer visible in the approved sum.
        QCOMPARE(model->currentTransaction()->reviewedRecipients().size(), qsizetype{2});
        QCOMPARE(model->currentTransaction()->amountAmount()->satoshi(), qint64{1'500'000});
        QCOMPARE(model->currentTransaction()->getTransactionFee(), CAmount{500'000});
        QCOMPARE(model->currentTransaction()->totalAmount()->satoshi(), qint64{2'000'000});
        const auto outputs = model->currentTransactionFlow().value("outputs").toList();
        QCOMPARE(outputs.size(), qsizetype{2});
        QVERIFY(!outputs[0].toMap().value("isChange").toBool()); QVERIFY(!outputs[1].toMap().value("isChange").toBool());
        QCOMPARE(outputs[1].toMap().value("ownership").toString(), QStringLiteral("wallet"));
        fixture.close(); QVERIFY(environment.quiescent());
    }

    void feeAdmissionNotificationCanRetireItsObserver_data()
    {
        QTest::addColumn<int>("action"); QTest::addColumn<bool>("reject");
        for (int action : {0, 1, 2}) for (bool reject : {false, true}) {
            const auto name = QString("action%1-reject%2").arg(action).arg(reject).toUtf8();
            QTest::newRow(name.constData()) << action << reject;
        }
    }
    void feeAdmissionNotificationCanRetireItsObserver()
    {
        QFETCH(int, action); QFETCH(bool, reject);
        ManualExecutionEnvironment environment;
        auto document = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out]; input.non_witness_utxo = nullptr;
        }
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true; return std::optional<common::PSBTError>{};
        };
        ManualPsbtModel fixture{environment, wallet, [actual](const COutPoint& outpoint) { return std::optional<CTxOut>{actual.at(outpoint)}; }};
        auto& model = fixture.model;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = WritePsbt(document, directory, "pending.psbt");
        const auto replacement = psbtfeefixture::Legacy(0, false);
        const auto other = WritePsbt(replacement, directory, "replacement.psbt");
        bool acted{false}, observed_pending{false}; quint64 replacement_ticket{0};
        auto* view = model.get();
        connect(view, &WalletQmlModel::psbtActionChanged, this, [&] {
            if (acted) return;
            acted = true; observed_pending = view->psbtActionBusy();
            if (action == 0) view->discardCurrentTransaction();
            if (action == 1) replacement_ticket = view->requestImportPsbtFromFile(other);
            if (action == 2) model.reset();
        });
        environment.rejectNext(reject);
        const auto ticket = view->requestImportPsbtFromFile(path);
        QVERIFY(ticket); QVERIFY(acted); QCOMPARE(observed_pending, !reject);
        fixture.settle();
        if (action == 2) QVERIFY(!model);
        else if (action == 0) { QVERIFY(!model->currentTransaction()); QVERIFY(!model->currentTransactionCanBroadcast()); }
        else {
            QVERIFY(replacement_ticket); QCOMPARE(model->psbtActionId(), replacement_ticket);
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtImported);
            QVERIFY(model->currentTransaction());
            auto expected = replacement; CMutableTransaction finalized;
            QVERIFY(FinalizeAndExtractPSBT(expected, finalized));
            QCOMPARE(model->currentTransaction()->getWtx()->GetWitnessHash(), MakeTransactionRef(finalized)->GetWitnessHash());
            QVERIFY(model->currentTransaction()->feeKnown());
        }
        fixture.close(); QVERIFY2(environment.quiescent(), environment.trace().c_str());
    }

    void authenticatedFeePublishesOneFrozenProjection_data()
    {
        QTest::addColumn<int>("action");
        QTest::newRow("frozen-value") << 0; QTest::newRow("close") << 1;
        QTest::newRow("replace") << 2; QTest::newRow("destroy") << 3;
    }
    void authenticatedFeePublishesOneFrozenProjection()
    {
        QFETCH(int, action);
        ManualExecutionEnvironment environment;
        auto document = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out]; input.non_witness_utxo = nullptr;
        }
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true; return std::optional<common::PSBTError>{};
        };
        bool enabled{false};
        ManualPsbtModel fixture{environment, wallet, [&](const COutPoint& outpoint) -> std::optional<CTxOut> { return enabled ? std::optional<CTxOut>{actual.at(outpoint)} : std::nullopt; }};
        auto& model = fixture.model;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QVERIFY(model->requestImportPsbtFromFile(WritePsbt(document, directory, "pending.psbt"))); fixture.settle();
        QCOMPARE(model->psbtImportResult(), WalletQmlModel::PsbtImportResult::WalletCannotSign);
        auto* previous = model->currentTransaction(); QVERIFY(previous); QVERIFY(!previous->feeKnown());
        previous->setReviewFeeDetails(12, "5");
        const auto frozen = previous->reviewedRecipients(); const auto labels = previous->recipientLabels();
        QSignalSpy old_fee_notifications{previous, &WalletQmlModelTransaction::feeChanged};
        model->sendRecipientList()->currentRecipient()->setLabel("edited draft");
        model->sendRecipientList()->currentRecipient()->amount()->setSatoshi(1'200'000);
        const auto replacement = psbtfeefixture::Legacy(0, false);
        const auto other = WritePsbt(replacement, directory, "replacement.psbt");
        enabled = true; model->setNode(nullptr); QVERIFY(model->transactionFeePending());
        bool notified{false}; quint64 replacement_ticket{0};
        connect(model.get(), &WalletQmlModel::currentTransactionChanged, this, [&] {
            if (notified || (model && model->transactionFeePending())) return;
            notified = true;
            if (action == 1) model->discardCurrentTransaction();
            if (action == 2) replacement_ticket = model->requestImportPsbtFromFile(other);
            if (action == 3) model.reset();
        });
        environment.drain(); QVERIFY(model->transactionFeePending());
        fixture.settle();
        QVERIFY(notified); QCOMPARE(old_fee_notifications.count(), 0);
        if (action == 3) QVERIFY(!model);
        else if (action == 1) { QVERIFY(!model->currentTransaction()); QVERIFY(!model->currentTransactionCanBroadcast()); }
        else if (action == 2) {
            QVERIFY(replacement_ticket); auto expected = replacement; CMutableTransaction finalized;
            QVERIFY(FinalizeAndExtractPSBT(expected, finalized));
            QCOMPARE(model->currentTransaction()->getWtx()->GetWitnessHash(), MakeTransactionRef(finalized)->GetWitnessHash());
        }
        else {
            auto* current = model->currentTransaction(); QVERIFY(current != previous);
            QCOMPARE(current->reviewedRecipients(), frozen); QCOMPARE(current->recipientLabels(), labels);
            auto expected = document; CMutableTransaction finalized;
            QVERIFY(FinalizeAndExtractPSBT(expected, finalized));
            QCOMPARE(current->getWtx()->GetWitnessHash(), MakeTransactionRef(finalized)->GetWitnessHash());
            QCOMPARE(current->getTransactionFee(), psbtfeefixture::ACTUAL_FEE);
            QCOMPARE(current->amountAmount()->satoshi(), qint64{1'500'000});
            QCOMPARE(current->totalAmount()->satoshi(), qint64{2'000'000});
            QCOMPARE(current->reviewTargetBlocks(), 12); QCOMPARE(current->reviewFeeRate(), QString("5"));
            QVERIFY(current->feeKnown()); QVERIFY(!model->transactionFeePending());
        }
        fixture.close(); QVERIFY2(environment.quiescent(), environment.trace().c_str());
    }

    void childPublicationCanCloseParentDuringNodeChange_data()
    {
        QTest::addColumn<bool>("destroy");
        QTest::addColumn<bool>("work_started");
        QTest::newRow("close-document") << false << false;
        QTest::newRow("destroy-observer") << true << false;
        QTest::newRow("destroy-after-child-work") << true << true;
    }
    void childPublicationCanCloseParentDuringNodeChange()
    {
        QFETCH(bool, destroy);
        QFETCH(bool, work_started);
        ManualExecutionEnvironment environment;
        auto document = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out]; input.non_witness_utxo = nullptr;
        }
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true; return std::optional<common::PSBTError>{};
        };
        int reads{0};
        MockNode replacement; // Outlives every accepted original child read/retirement.
        ManualPsbtModel fixture{environment, wallet, [&](const COutPoint&) -> std::optional<CTxOut> { ++reads; return {}; }};
        auto& model = fixture.model;
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = WritePsbt(document, directory, "parent-child.psbt");
        QVERIFY(model->requestImportPsbtFromFile(path)); fixture.settle();
        QVERIFY(model->currentTransaction()); QVERIFY(!model->currentTransaction()->feeKnown());
        auto* child = model->importedPsbt();
        QVERIFY(child->requestLoadFromFile(path)); fixture.settle(); QVERIFY(child->loaded());
        const auto before = reads;
        bool notified{false};
        bool queued_child_cancelled{false};
        const auto child_connection = connect(child, &PsbtQmlModel::changed, this, [&] {
            if (!model || notified) return;
            notified = true;
            if (destroy) {
                if (work_started) {
                    // Domain planning/retirement may precede the Native read.
                    // Hold the real read's outcome, identified by its public
                    // lookup effect, rather than an arbitrary first packet.
                    std::optional<execution::TaskId> actual_read;
                    for(int budget=0;budget<256&&!actual_read;++budget){
                        auto turn=environment.takeTurnFront();QVERIFY(turn);
                        const auto id=turn->id;turn->turn();
                        if(reads>before)actual_read=id;
                    }
                    QVERIFY(actual_read);
                    QCOMPARE(environment.scheduler->phase(*actual_read),std::make_optional(execution::Phase::OutcomeReady));
                    QCOMPARE(reads, before + 2);
                    model.reset();
                } else {
                    model.reset(); // Withdraw observation before the original queued read starts.
                    QCOMPARE(reads, before); // Session rejects it before any lookup.
                    queued_child_cancelled = true;
                }
            } else model->discardCurrentTransaction();
        });
        const auto disconnect_child = qScopeGuard([child_connection] { QObject::disconnect(child_connection); });
        model->setNode(&replacement); // Child admission publication closes its parent before a parent retry.
        QVERIFY(notified);
        fixture.settle();
        QCOMPARE(queued_child_cancelled, destroy && !work_started);
        QCOMPARE(reads, before + (queued_child_cancelled ? 0 : 2));
        // The closed document leaves its opening available for the child's
        // read. Observer destruction may cancel a not-started observation;
        // a completed original read retains its outcome without a parent retry.
        if (model) { QVERIFY(!model->currentTransaction()); QVERIFY(!model->currentTransactionCanBroadcast()); }
        else QVERIFY(destroy);
        fixture.close(); QVERIFY2(environment.quiescent(), environment.trace().c_str());
    }

    void unavailableNodeCannotAuthorizeWitnessMetadata()
    {
        auto [wallet, model] = MakePasswordWalletModel();
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true;
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QCOMPARE(AwaitPsbtImport(*model, WritePsbt(psbtfeefixture::Legacy(2, true), directory, "offline.psbt")), WalletQmlModel::PsbtImportResult::WalletCannotSign);
        QVERIFY(!model->transactionFeePending());
        QVERIFY(!model->currentTransaction()->feeKnown());
        QVERIFY(!model->currentTransactionCanSend()); QVERIFY(!model->currentTransactionCanBroadcast());
        QVERIFY(!AwaitCommittedSend(*model));
        model.reset();
    }
    void closingReviewDuringUnlockCannotCommit()
    {
        auto [wallet, model] = MakePasswordWalletModel();
        const auto signed_document = psbtfeefixture::Legacy(2, false);
        auto document = signed_document;
        for (auto& input : document.inputs) input.final_script_sig.clear();
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        wallet->fill_psbt_fn = [signed_document](const common::PSBTFillOptions& options, size_t* count, PartiallySignedTransaction& candidate, bool& complete) {
            if (count) *count = 2;
            complete = options.sign;
            if (options.sign) candidate = signed_document;
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QCOMPARE(AwaitPsbtImport(*model, WritePsbt(document, directory, "closing.psbt")), WalletQmlModel::PsbtImportResult::WalletCanSign);
        const auto closed = std::make_shared<std::atomic_bool>(false);
        const auto original_unlock = wallet->unlock_fn;
        const QPointer<WalletQmlModel> opening{model.get()};
        // Hold the real Native unlock boundary until the GUI has withdrawn
        // consent for this review. Only the queued GUI handoff touches QObject.
        const auto acknowledged=std::make_shared<QSemaphore>();
        const auto unblock=qScopeGuard([acknowledged]{acknowledged->release();});
        wallet->unlock_fn = [original_unlock, opening, closed,acknowledged](const SecureString& password) {
            auto result = original_unlock(password);
            if(result&&QMetaObject::invokeMethod(qApp,[opening,closed,acknowledged]{
                if(opening){*closed=true;opening->discardCurrentTransaction();}
                acknowledged->release();
            },Qt::QueuedConnection))acknowledged->acquire();
            return result;
        };
        const bool sent = AwaitCommittedSend(*model, "password");
        const auto commits = wallet->commit_calls;
        QVERIFY(closed->load()); QVERIFY(!model->currentTransaction());
        model.reset();
        QVERIFY(!sent); QCOMPARE(commits, 0);
    }

    void preparedFeeProofSurvivesOnlyMatchingApproval()
    {
        const auto document = signerfixture::Review();
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([document](FakePasswordWallet& original) {
            original.private_keys_disabled = true;
            original.external_signer = true;
            for (const auto& input : document.inputs) original.known_transactions.emplace(input.GetOutPoint().hash, input.non_witness_utxo);
        original.txout_is_mine_fn = [document](const CTxOut& output) {
            for (const auto& input : document.inputs) if (input.non_witness_utxo->vout[input.GetOutPoint().n] == output) return true;
            return false;
        };
        });
        const auto transaction = *document.GetUnsignedTx();
        wallet->create_transaction_fn = [transaction](const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&, bool,
                                                      int& change_pos, CAmount& fee) -> util::Result<CTransactionRef> {
            change_pos = -1; fee = 500; return MakeTransactionRef(transaction);
        };
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = false;
            return std::optional<common::PSBTError>{};
        };
        for (size_t i{0}; i < transaction.vout.size(); ++i) {
            if (i) model->sendRecipientList()->add();
            CTxDestination destination; QVERIFY(ExtractDestination(transaction.vout[i].scriptPubKey, destination));
            auto* recipient = model->sendRecipientList()->currentRecipient();
            recipient->setAddress(QString::fromStdString(EncodeDestination(destination)));
            recipient->amount()->setSatoshi(transaction.vout[i].nValue);
        }
        QVERIFY(AwaitPreparation(*model));
        QVERIFY(!model->currentTransactionFlow().isEmpty());
        model->currentTransaction()->setTransactionFee(501);
        QSignalSpy failed{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
        QSignalSpy partial{model.get(), &WalletQmlModel::externalSignerApprovalPartiallySucceeded};
        QVERIFY(AwaitSignerApprovalOutcome(*model));
        QCOMPARE(model->signerApprovalState(),WalletQmlModel::ApprovalState::ApprovalFailed);
        QCOMPARE(failed.count(), 1); QCOMPARE(partial.count(), 0);
        model->currentTransaction()->setTransactionFee(500);
        QVERIFY(AwaitSignerApprovalOutcome(*model));
        QCOMPARE(model->signerApprovalState(),WalletQmlModel::ApprovalState::ApprovalPartiallySigned);
        QCOMPARE(failed.count(), 1); QCOMPARE(partial.count(), 1);
        const auto flow = model->currentTransactionFlow();
        QVERIFY(flow.value("feeKnown").toBool()); QCOMPARE(flow.value("feeSat").toLongLong(), qint64{500});
        QCOMPARE(wallet->commit_calls, 0);
        model.reset();
    }

    void sendRejectsReturnedFeeMetadata_data()
    {
        QTest::addColumn<bool>("passphrase");
        QTest::newRow("ordinary-send") << false;
        QTest::newRow("password-send") << true;
    }
    void sendRejectsReturnedFeeMetadata()
    {
        QFETCH(bool, passphrase);
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([passphrase](FakePasswordWallet& original) { original.encrypted = passphrase; });
        const auto signed_document = psbtfeefixture::Legacy(2, false);
        auto unsigned_document = signed_document;
        for (auto& input : unsigned_document.inputs) input.final_script_sig.clear();
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        wallet->fill_psbt_fn = [signed_document](const common::PSBTFillOptions& options, size_t* count, PartiallySignedTransaction& document, bool& complete) {
            if (count) *count = 2;
            complete = options.sign;
            if (options.sign) {
                document = signed_document;
                auto& input = document.inputs[1];
                input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
                input.non_witness_utxo = nullptr;
                --input.witness_utxo.nValue;
            }
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QCOMPARE(AwaitPsbtImport(*model, WritePsbt(unsigned_document, directory, "send.psbt")), WalletQmlModel::PsbtImportResult::WalletCanSign);
        QVERIFY(model->currentTransactionCanSend());
        const bool sent = passphrase ? AwaitCommittedSend(*model, "password") : AwaitCommittedSend(*model);
        const auto error = model->transactionError(); const auto commits = wallet->commit_calls;
        const auto retained_hash = model->currentTransaction()->getWtx()->GetHash();
        const auto outcome=model->sendOperation()->result();QVERIFY(outcome);QCOMPARE(outcome->error,walletsend::Error::InvalidFee);
        QCOMPARE(outcome->candidate_txid,unsigned_document.GetUnsignedTx()->GetHash());
        QCOMPARE(outcome->candidate_wtxid,CTransaction{*unsigned_document.GetUnsignedTx()}.GetWitnessHash());
        model.reset();
        QCOMPARE(outcome->error,walletsend::Error::InvalidFee);
        QCOMPARE(outcome->candidate_txid,retained_hash);
        QVERIFY(!sent); QCOMPARE(commits, 0);
        QCOMPARE(error, QString("The transaction fee is missing or changed after review."));
        QCOMPARE(retained_hash, unsigned_document.GetUnsignedTx()->GetHash());
    }

    void authenticatedFeeDoesNotInventSigningAuthority_data()
    {
        QTest::addColumn<bool>("owned"); QTest::addColumn<int>("signable"); QTest::addColumn<bool>("complete");
        for (bool owned : {false, true}) for (int signable : {0, 1, 2}) for (bool complete : {false, true}) {
            QTest::newRow(qPrintable(QString("owned%1-signable%2-complete%3").arg(owned).arg(signable).arg(complete))) << owned << signable << complete;
        }
    }
    void authenticatedFeeDoesNotInventSigningAuthority()
    {
        QFETCH(bool, owned); QFETCH(int, signable); QFETCH(bool, complete);
        ManualExecutionEnvironment environment;
        auto document = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out]; input.non_witness_utxo = nullptr;
            if (!complete) input.final_script_sig.clear();
        }
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->txin_is_mine_fn = [owned](const CTxIn&) { return owned; };
        wallet->fill_psbt_fn = [signable, complete](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& filled) {
            if (count) *count = signable;
            filled = complete; return std::optional<common::PSBTError>{};
        };
        ManualPsbtModel fixture{environment, wallet, [actual](const COutPoint& outpoint) { return std::optional<CTxOut>{actual.at(outpoint)}; }};
        auto& model = fixture.model;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto ticket = model->requestImportPsbtFromFile(WritePsbt(document, directory, "authority.psbt")); QVERIFY(ticket);
        QVERIFY(!model->currentTransactionCanSend()); QVERIFY(!model->currentTransactionCanBroadcast()); QVERIFY(!model->currentTransaction());
        environment.drain();
        QVERIFY(model->psbtActionBusy()); QVERIFY(!model->currentTransactionCanSend()); QVERIFY(!model->currentTransactionCanBroadcast());
        fixture.settle();
        QCOMPARE(model->psbtActionId(), ticket); QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtImported);
        QCOMPARE(model->currentTransaction()->getTransactionFee(), psbtfeefixture::ACTUAL_FEE);
        QCOMPARE(model->currentTransactionCanSend(), owned && (complete || signable == 2));
        QCOMPARE(model->currentTransactionCanSignOnly(), owned && !complete && signable == 2);
        QCOMPARE(model->currentTransactionCanBroadcast(), complete);
        fixture.close(); QVERIFY(environment.quiescent());
    }

    void feeProofUsesWorkerAndKeepsGuiResponsive_data()
    {
        QTest::addColumn<bool>("close_review"); QTest::addColumn<bool>("mempool");
        for (bool mempool : {false, true}) for (bool close_review : {false, true}) {
            const auto name = QString("mempool%1-close%2").arg(mempool).arg(close_review).toUtf8();
            QTest::newRow(name.constData()) << close_review << mempool;
        }
    }
    void feeProofUsesWorkerAndKeepsGuiResponsive()
    {
        QFETCH(bool, close_review); QFETCH(bool, mempool);
        MockNode node;
        auto [wallet, model] = MakePasswordWalletModel(&node);
        auto document = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
            input.non_witness_utxo = nullptr;
        }
        QSemaphore gate;
        std::atomic<bool> entered{false}, off_gui{true};
        auto* gui_thread = QThread::currentThread();
        const auto cleanup = qScopeGuard([&] { gate.release(); model.reset(); });
        const auto read = [&, actual](const COutPoint& outpoint) {
            if (QThread::currentThread() == gui_thread) off_gui = false;
            if (!entered.exchange(true)) gate.tryAcquire(1, 10000);
            return actual.at(outpoint);
        };
        node.get_unspent_output_fn = [read, mempool](const COutPoint& outpoint) -> std::optional<Coin> {
            if (mempool) return std::nullopt;
            return Coin{read(outpoint), 1, false};
        };
        node.execute_rpc_fn = [read](const std::string&, const UniValue& params, const std::string&) {
            const COutPoint outpoint{*Txid::FromHex(params[0].get_str()), params[1].getInt<uint32_t>()};
            const auto output = read(outpoint);
            UniValue result{UniValue::VOBJ}, script{UniValue::VOBJ};
            result.pushKV("value", FormatMoney(output.nValue)); script.pushKV("hex", HexStr(output.scriptPubKey));
            result.pushKV("scriptPubKey", script);
            return result;
        };
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true;
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto ticket = model->requestImportPsbtFromFile(WritePsbt(document, directory, "worker.psbt"));
        QVERIFY(ticket);
        QTRY_VERIFY(entered.load());
        bool heartbeat{false};
        QTimer::singleShot(0, this, [&] { heartbeat = true; });
        QTRY_VERIFY(heartbeat);
        QVERIFY(off_gui.load());
        QVERIFY(model->psbtActionBusy());
        QVERIFY(!model->currentTransaction());
        QVERIFY(!model->currentTransactionCanBroadcast());
        if (close_review) model->discardCurrentTransaction();
        gate.release();
        QTRY_VERIFY(!model->psbtActionBusy());
        QCOMPARE(model->psbtActionId(), ticket);
        if (close_review) {
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtStale);
            QVERIFY(!model->currentTransaction()); QVERIFY(!model->currentTransactionCanBroadcast());
        } else {
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtImported);
            QVERIFY(model->currentTransactionCanBroadcast());
            QCOMPARE(model->currentTransaction()->getTransactionFee(), psbtfeefixture::ACTUAL_FEE);
        }
        QCOMPARE(node.calls.getUnspentOutput.load(), 2);
        QCOMPARE(node.calls.executeRpc.load(), mempool ? 2 : 0);
    }

    void feeProofPublicationOwnsCapabilities_data()
    {
        QTest::addColumn<int>("scenario");
        const std::array names{"success", "missing", "throw", "rejected", "closed-review", "replaced-review", "observer-lost", "contradictory", "shutdown", "duplicate-publication", "node-changed", "shutdown-node-update"};
        for (int i = 0; i < int(names.size()); ++i) QTest::newRow(names[i]) << i;
    }
    void feeProofPublicationOwnsCapabilities()
    {
        QFETCH(int, scenario);
        ManualExecutionEnvironment environment;
        auto document = psbtfeefixture::Legacy(2, false);
        psbtfee::Outputs actual;
        for (auto& input : document.inputs) {
            actual.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.witness_utxo = input.non_witness_utxo->vout[input.prev_out]; input.non_witness_utxo = nullptr;
        }
        if (scenario == 7) --document.inputs[1].witness_utxo.nValue;
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true; return std::optional<common::PSBTError>{};
        };
        int reads{0};
        ManualPsbtModel fixture{environment, wallet, [&](const COutPoint& outpoint) -> std::optional<CTxOut> {
            ++reads;
            if (scenario == 2) throw std::runtime_error("lookup failed");
            if (scenario == 1) return std::nullopt;
            return actual.at(outpoint);
        }};
        auto& model = fixture.model;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = WritePsbt(document, directory, "needs-proof.psbt");
        environment.rejectNext(scenario == 3);
        const auto ticket = model->requestImportPsbtFromFile(path); QVERIFY(ticket);
        QCOMPARE(reads, 0); QVERIFY(!model->currentTransaction());
        QVERIFY(!model->currentTransactionCanBroadcast()); QVERIFY(!model->currentTransactionCanSend());
        const auto blocked_send=model->requestSendTransaction();
        if(scenario==3){
            // A synchronously failed import is no longer busy. The invalid
            // review command has its own explicit failure receipt, no effect.
            QVERIFY(blocked_send);QCOMPARE(model->sendOperationId(),blocked_send);
            QCOMPARE(model->sendState(),WalletQmlModel::SendState::SendFailed);QCOMPARE(wallet->commit_calls,0);
        }else QVERIFY(!blocked_send);
        if (scenario == 3) {
            fixture.settle();
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtFailed);
            QCOMPARE(reads, 0); QVERIFY(!model->currentTransaction()); QVERIFY(!model->psbtActionError().isEmpty());
            fixture.close(); QVERIFY(environment.quiescent()); return;
        }
        QVERIFY(model->psbtActionBusy());
        QVERIFY(environment.step()); // Original Session Native file/decode/authentication effect.
        QCOMPARE(reads, scenario == 2 ? 1 : 2); QVERIFY(!model->currentTransaction());
        QVERIFY(environment.step()); // Domain owns actual result, GUI still held.
        QVERIFY(model->psbtActionBusy()); QVERIFY(!model->currentTransaction());
        QCOMPARE(environment.publicationCount(), size_t{1});
        quint64 replacement_ticket{0};
        auto replacement = psbtfeefixture::Legacy(0, false);
        if (scenario == 4) model->discardCurrentTransaction();
        if (scenario == 5) replacement_ticket = model->requestImportPsbtFromFile(WritePsbt(replacement, directory, "replacement.psbt"));
        if (scenario == 6) model.reset();
        if (scenario == 11) connect(model.get(), &WalletQmlModel::availableChanged, model.get(), [&] { model->setNode(nullptr); });
        if (scenario == 8 || scenario == 11) model->beginShutdown();
        if (scenario == 9) QVERIFY(environment.duplicateFirstPublication());
        if (scenario == 10) model->setNode(nullptr);
        fixture.settle();
        if (scenario == 6) QVERIFY(!model);
        else if (scenario == 4) {
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtStale);
            QVERIFY(!model->currentTransaction()); QVERIFY(!model->currentTransactionCanBroadcast());
        } else if (scenario == 8 || scenario == 11) {
            QVERIFY(!model->available()); QVERIFY(!model->currentTransactionCanBroadcast());
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtClosed);
        } else if (scenario == 1 || scenario == 2 || scenario == 7) {
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtImported);
            QVERIFY(model->currentTransaction()); QVERIFY(!model->currentTransaction()->feeKnown());
            QVERIFY(!model->currentTransactionCanBroadcast());
        } else {
            if (scenario == 5) { QVERIFY(replacement_ticket); QCOMPARE(model->psbtActionId(), replacement_ticket); }
            QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtImported);
            QCOMPARE(model->currentTransaction()->getTransactionFee(), psbtfeefixture::ACTUAL_FEE);
            QVERIFY(model->currentTransactionCanBroadcast());
            auto expected = scenario == 5 ? replacement : document; CMutableTransaction finalized;
            QVERIFY(FinalizeAndExtractPSBT(expected, finalized));
            QCOMPARE(model->currentTransaction()->getWtx()->GetWitnessHash(), MakeTransactionRef(finalized)->GetWitnessHash());
        }
        fixture.close(); QVERIFY2(environment.quiescent(), environment.trace().c_str());
    }

    void invalidPsbtOutputAmountsNeverReachReview_data()
    {
        QTest::addColumn<bool>("backend_mutation");
        QTest::newRow("file-value") << false;
        QTest::newRow("backend-value") << true;
    }
    void invalidPsbtOutputAmountsNeverReachReview()
    {
        QFETCH(bool, backend_mutation);
        auto [wallet, model] = MakePasswordWalletModel();
        int reads{0};
        wallet->fill_psbt_fn = [&](const common::PSBTFillOptions&, size_t*, PartiallySignedTransaction& psbt, bool& complete) {
            ++reads; psbt.outputs[0].amount = -1; complete = false; return std::optional<common::PSBTError>{};
        };
        auto psbt = MakeReviewPsbt();
        if (!backend_mutation) psbt.outputs[0].amount = -1;
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = WritePsbt(psbt, directory, "invalid-output.psbt");
        const auto result = AwaitPsbtImport(*model, path);
        const auto error = model->importedPsbt()->error();
        const bool has_review = model->currentTransaction(); const auto commits = wallet->commit_calls;
        model.reset();
        QCOMPARE(result, WalletQmlModel::PsbtImportResult::PsbtUnsupported);
        QCOMPARE(reads, backend_mutation ? 1 : 0); QVERIFY(!has_review); QCOMPARE(commits, 0);
        QCOMPARE(error, QString("The transaction fee is missing or invalid."));
    }
    void signerMissingCapabilityIsRetainedForRecreatedViews_data()
    {
        QTest::addColumn<bool>("missing_first");
        QTest::newRow("missing-then-backend-failure") << true;
        QTest::newRow("backend-failure-then-missing") << false;
    }
    void signerMissingCapabilityIsRetainedForRecreatedViews()
    {
        QFETCH(bool, missing_first);
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) { original.private_keys_disabled = true; original.external_signer = true; });
        const auto missing = std::make_shared<std::atomic_bool>(missing_first);
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        wallet->fill_psbt_fn = [wallet, missing](const common::PSBTFillOptions& options, size_t* count, PartiallySignedTransaction&, bool& complete) -> std::optional<common::PSBTError> {
            wallet->fill_psbt_sign_args.push_back(options.sign); if (count) *count = 1; complete = false;
            return options.sign ? std::optional{missing->load() ? common::PSBTError::EXTERNAL_SIGNER_NOT_FOUND : common::PSBTError::EXTERNAL_SIGNER_FAILED} : std::nullopt;
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        QCOMPARE(AwaitPsbtImport(*model, WritePsbt(MakeReviewPsbt(), directory, "signer-cause.psbt")), WalletQmlModel::PsbtImportResult::WalletCanSign);
        const auto reviewed = model->currentTransaction()->getWtx();
        QSignalSpy failed{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
        QVERIFY(AwaitSignerApprovalOutcome(*model));
        QCOMPARE(model->signerApprovalState(), WalletQmlModel::ApprovalState::ApprovalFailed);
        QCOMPARE(model->signerApprovalSignerNotFound(), missing_first); QCOMPARE(failed.size(), 1); QCOMPARE(failed[0][1].toBool(), missing_first);
        QCOMPARE(model->currentTransaction()->getWtx(), reviewed); QCOMPARE(wallet->commit_calls, 0);
        missing->store(!missing_first); QVERIFY(AwaitSignerApprovalOutcome(*model));
        QCOMPARE(model->signerApprovalSignerNotFound(), !missing_first); QCOMPARE(failed.size(), 2); QCOMPARE(failed[1][1].toBool(), !missing_first);
        QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 2);
        QCOMPARE(model->currentTransaction()->getWtx(), reviewed); QCOMPARE(wallet->commit_calls, 0);
    }

    void reviewedFeeGuardsProtectedActions_data()
    {
        QTest::addColumn<int>("action"); QTest::addColumn<bool>("unknown");
        for (int action : {0, 1, 2, 3}) for (bool unknown : {false, true}) {
            const auto tag = QString("action%1-%2").arg(action).arg(unknown ? "unknown" : "changed").toUtf8();
            QTest::newRow(tag.constData()) << action << unknown;
        }
    }
    void reviewedFeeGuardsProtectedActions()
    {
        QFETCH(int, action); QFETCH(bool, unknown);
        MockNode node;
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
            original.external_signer = true;
            original.private_keys_disabled = true;
        }, &node);
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true;
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = WritePsbt(psbtfeefixture::Legacy(2, unknown), directory, "approved.psbt");
        const auto imported = AwaitPsbtImport(*model, path);
        QVERIFY(imported != WalletQmlModel::PsbtImportResult::PsbtUnsupported);
        if (!unknown) model->currentTransaction()->setTransactionFee(psbtfeefixture::ACTUAL_FEE + 1);
        QSignalSpy failed{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
        const bool result = action == 0 ? AwaitPsbtBroadcast(*model) : action == 1 ? AwaitCommittedSend(*model) : action == 3 ? AwaitCommittedSend(*model, "password") : false;
        if (action == 2) {
            QVERIFY(AwaitSignerApprovalOutcome(*model));
            QTRY_COMPARE(model->signerApprovalState(), WalletQmlModel::ApprovalState::ApprovalFailed);
        }
        const int errors = failed.count();
        const auto error = errors ? failed[0][0].toString() : model->transactionError();
        const auto broadcasts = node.calls.broadcastTransaction.load(); const auto commits = wallet->commit_calls;
        model.reset();
        QVERIFY(!result); QCOMPARE(broadcasts, 0); QCOMPARE(commits, 0); QVERIFY(!error.isEmpty());
        if (action == 2) {
            QCOMPARE(errors, 1);
            QCOMPARE(error, unknown ? QString("The transaction fee is missing or invalid.") : QString("The transaction fee changed after review."));
        }
    }
    void signerCannotChangeFeeMetadataAfterReview_data()
    {
        QTest::addColumn<int>("mutation");
        QTest::newRow("omit-authentication") << 0;
        QTest::newRow("contradict-authentication") << 1;
        QTest::newRow("change-parent") << 2;
    }
    void signerCannotChangeFeeMetadataAfterReview()
    {
        QFETCH(int, mutation);
        auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
            original.external_signer = true;
            original.private_keys_disabled = true;
        });
        wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true;
            return std::optional<common::PSBTError>{};
        };
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto approved = psbtfeefixture::Legacy(2, false);
        const auto path = WritePsbt(approved, directory, "approved.psbt");
        QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
        wallet->fill_psbt_fn = [mutation](const common::PSBTFillOptions& options, size_t* count, PartiallySignedTransaction& psbt, bool& complete) {
            if (count) *count = options.sign ? 2 : 0;
            complete = false;
            if (options.sign) {
                auto& input = psbt.inputs[1];
                input.witness_utxo = input.non_witness_utxo->vout[input.prev_out];
                input.witness_utxo.nValue -= psbtfeefixture::ACTUAL_FEE - 1;
                if (mutation == 0) input.non_witness_utxo = nullptr;
                if (mutation == 2) {
                    auto parent = CMutableTransaction{*input.non_witness_utxo};
                    --parent.vout[input.prev_out].nValue;
                    input.non_witness_utxo = MakeTransactionRef(parent);
                }
            }
            return std::optional<common::PSBTError>{};
        };
        QSignalSpy failed{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
        QSignalSpy succeeded{model.get(), &WalletQmlModel::externalSignerApprovalSucceeded};
        QVERIFY(AwaitSignerApprovalOutcome(*model));
        const int errors = failed.count(), successes = succeeded.count();
        const auto message = errors ? failed[0][0].toString() : QString{};
        const auto retained_path = directory.filePath("retained.psbt");
        const auto save_error = AwaitPsbtSaveError(*model, retained_path);
        PartiallySignedTransaction retained{CMutableTransaction{}, /*version=*/2};
        const auto load_error = PsbtQmlModel::LoadPsbtFromFile(retained_path, retained);
        const auto before_retry = model->currentTransaction()->getWtx()->GetHash();
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true;
            return std::optional<common::PSBTError>{};
        };
        QVERIFY(AwaitSignerApprovalOutcome(*model));
        const auto retry_successes = succeeded.count(); const auto commits = wallet->commit_calls;
        model.reset();
        QCOMPARE(errors, 1); QCOMPARE(successes, 0); QCOMPARE(message, QString("The transaction fee changed after review."));
        QCOMPARE(save_error, QString{}); QCOMPARE(load_error, QString{});
        QVERIFY(retained.inputs[1].non_witness_utxo);
        QCOMPARE(retained.inputs[1].non_witness_utxo->GetHash(), approved.inputs[1].non_witness_utxo->GetHash());
        auto expected = approved; CMutableTransaction finalized;
        QVERIFY(FinalizeAndExtractPSBT(expected, finalized));
        QCOMPARE(before_retry, finalized.GetHash());
        QCOMPARE(retry_successes, 1); QCOMPARE(commits, 0);
    }
    void legacyFeeRequiresAuthenticatedPrevouts_data()
    {
        QTest::addColumn<uint>("version"); QTest::addColumn<int>("owned"); QTest::addColumn<bool>("forged");
        for (uint version : {0U, 2U}) for (int owned : {0, 1, 2}) for (bool forged : {false, true}) {
            const auto name = QString("v%1-owned%2-%3").arg(version).arg(owned).arg(forged ? "forged" : "control").toUtf8();
            QTest::newRow(name.constData()) << version << owned << forged;
        }
    }
    void legacyFeeRequiresAuthenticatedPrevouts()
    {
        QFETCH(uint, version); QFETCH(int, owned); QFETCH(bool, forged);
        MockNode node;
        node.broadcast_transaction_fn = [](CTransactionRef, CAmount, CFeeRate, std::string&) { return node::TransactionError::OK; };
        auto [wallet, model] = MakePasswordWalletModel(&node);
        wallet->fill_psbt_fn = [](const common::PSBTFillOptions&, size_t* count, PartiallySignedTransaction&, bool& complete) {
            if (count) *count = 0;
            complete = true;
            return std::optional<common::PSBTError>{};
        };
        wallet->txin_is_mine_fn = [owned](const CTxIn& input) { return input.prevout.n < unsigned(owned); };
        auto psbt = psbtfeefixture::Legacy(version, forged);
        CMutableTransaction extracted;
        QVERIFY(FinalizeAndExtractPSBT(psbt, extracted)); // Forged amount does not invalidate legacy signatures.
        const auto original_hash = forged ? psbt.GetUnsignedTx()->GetHash() : extracted.GetHash();
        const auto& previous = psbt.inputs[0].non_witness_utxo;
        QCOMPARE(previous->vout[0].nValue + previous->vout[1].nValue - extracted.vout[0].nValue, psbtfeefixture::ACTUAL_FEE);
        QTemporaryDir directory; QVERIFY(directory.isValid());
        const auto path = WritePsbt(psbt, directory, "legacy.psbt");
        QVERIFY(!path.isEmpty());
        const auto result = AwaitPsbtImport(*model, path);
        const QString import_failure = QStringLiteral("Expected the original read-only review; PSBT state=%1, error=%2")
            .arg(int(model->psbtActionState())).arg(model->psbtActionError());
        QVERIFY2(model->currentTransaction(), qPrintable(import_failure));
        const bool can_send = model->currentTransactionCanSend(), can_broadcast = model->currentTransactionCanBroadcast();
        const auto fee = model->currentTransaction()->feeAmount()->satoshi();
        const auto fee_text = model->currentTransaction()->fee();
        const auto flow = model->currentTransactionFlow();
        const bool broadcast = AwaitPsbtBroadcast(*model);
        const bool sent = forged ? AwaitCommittedSend(*model) : false;
        const auto hash_after = model->currentTransaction()->getWtx()->GetHash();
        const auto broadcasts = node.calls.broadcastTransaction.load(); const auto commits = wallet->commit_calls;
        model.reset(); // Drain before a red assertion fails.
        if (forged) {
            QVERIFY(!can_broadcast); QVERIFY(!can_send); QVERIFY(!broadcast); QVERIFY(!sent);
            QCOMPARE(hash_after, original_hash);
            QCOMPARE(broadcasts, 0); QCOMPARE(commits, 0);
            QVERIFY(!flow.value("feeKnown").toBool());
            QCOMPARE(fee_text, QString("Unknown"));
        } else {
            QCOMPARE(result, owned == 2 ? WalletQmlModel::PsbtImportResult::WalletCanSign : WalletQmlModel::PsbtImportResult::WalletCannotSign);
            QVERIFY(can_broadcast); QVERIFY(broadcast); QCOMPARE(broadcasts, 1);
            QCOMPARE(hash_after, original_hash);
            QCOMPARE(fee, psbtfeefixture::ACTUAL_FEE);
            QVERIFY(flow.value("feeKnown").toBool());
            QCOMPARE(flow.value("feeSat").toLongLong(), psbtfeefixture::ACTUAL_FEE);
        }
    }
    void signerSubstitutionIsRejected_data() {
        QTest::addColumn<int>("mutation");
        QTest::addColumn<bool>("partial");
        QTest::addColumn<bool>("direct_send");
        QTest::addColumn<bool>("draft");
        for (size_t i = 0; i < signerfixture::CASES.size(); ++i) {
            const auto name = QString::fromLatin1(signerfixture::CASES[i].first);
            QTest::newRow(qPrintable(name + "-complete")) << int(i) << false << false << false;
            QTest::newRow(qPrintable(name + "-partial")) << int(i) << true << false << false;
            QTest::newRow(qPrintable(name + "-direct-send")) << int(i) << false << true << false;
            QTest::newRow(qPrintable(name + "-send-draft-complete")) << int(i) << false << false << true;
            QTest::newRow(qPrintable(name + "-send-draft-partial")) << int(i) << true << false << true;
        }
        QTest::newRow("invalid-locktimes") << -1 << true << false << false;
        QTest::newRow("invalid-locktimes-direct-send") << -1 << true << true << false;
    }
    void signerApprovalRetainsValidPartialSignatures();
    void signerUnverifiedResultIsSeparateAndExportUsesOriginalToken();
    void localUnverifiedResultKeepsTheReviewAndExportsOriginalBytes();
    void signerSubstitutionIsRejected();
    void initTestCase();
    void coreApiFeePreviews_data();
    void coreApiFeePreviews();
    void coreFallbackFeeReachesPresetsAndReview_data();
    void coreFallbackFeeReachesPresetsAndReview();
    void cleanupTestCase();
    void feeTargetIndex_mapsStandardTargets();
    void customFeeRateUpdatesEstimatedTarget();
    void maximumKeepsAmountWhenValidAddressCannotBeEstimated() {
        const COutPoint selected{Txid::FromUint256(uint256::ONE), 1};
        auto [wallet, model] = MakeConfiguredWalletModel([selected](MockWallet& value) {
            value.list_coins_fn = [selected] {
                const auto destination = DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
                interfaces::Wallet::CoinsList coins;
                coins[destination].emplace_back(selected,
                    interfaces::WalletTxOut{CTxOut{10 * COIN, GetScriptForDestination(destination)}, 0, 6, false});
                return coins;
            };
        });
        SetValidRecipient(*model);
        QVERIFY(model->coinsListModel()->setManualInputSelection(true));
        model->selectCoin(selected);
        QTRY_VERIFY(!model->coinSelectionPending() && model->isSelectedCoin(selected));
        // With a valid address, preserve the existing amount if no sendall
        // estimate can be made. Gross prefilling is only for an empty address.
        model->useMaximum();
        QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
        QCOMPARE(model->sendRecipientList()->currentRecipient()->cAmount(), CAmount{50'000});
        QCOMPARE(model->estimatedFeeSatoshi(), -1);
        QVERIFY(model->isSelectedCoin(selected));
    }
    void previewExposesFeeInputCountAndTotalDebit() {
        auto [wallet, model] = MakeWalletModel();
        SetValidRecipient(*model);
        wallet->create_transaction_fn = [](const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&, bool, int& change_pos, CAmount& fee) -> util::Result<CTransactionRef> {
            CMutableTransaction tx;
            tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
            tx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 1});
            fee = 300;
            change_pos = -1;
            return MakeTransactionRef(tx);
        };
        model->setCustomFeeEnabled(true);
        model->setCustomFeeRate("2.5");
        model->scheduleFeeEstimates();
        QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeSatoshi(), 300, FEE_ESTIMATE_TIMEOUT_MS);
        QCOMPARE(model->estimatedInputCount(), 2);
        QCOMPARE(model->estimatedFeeRate(), QString("2.5"));
        QCOMPARE(model->sendTotalSatoshi(), 50'300);
    }
    void estimatedFeeForTarget_returnsEmptyWhenUnavailable();
    void feeReadDispatchUsesDomainOwner_data()
    {
        QTest::addColumn<bool>("reject_core");
        QTest::newRow("Core-accepted") << false;
        QTest::newRow("nested-Core-admission-rejected") << true;
    }
    void feeReadDispatchUsesDomainOwner()
    {
        QFETCH(bool, reject_core);
        ManualRoutedExecution transport;
        const auto clock = std::make_shared<ManualExecutionEnvironment>();
        std::vector<CAmount> captured;
        FeeReadServices services{transport.scheduler, clock, [&](const FeeReadInput& input) {
            if (input.recipients.size() != 1) throw std::runtime_error("unexpected recipient count");
            captured.push_back(input.recipients.front().amount);
            FeeReadResult result;
            result.estimates.insert(6, SendFeePreview{500, 2, 2500, {}, false});
            return result;
        }, [clock](auto publication) { clock->post(std::move(publication)); }};
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services));
        ManualWalletDrain drain{transport, model.get()};
        const auto retire = [&] {
            model->beginShutdown();
            return QTest::qWaitFor([&] {
                transport.drain();
                clock->publishAll();
                QCoreApplication::processEvents();
                QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
                transport.drain();
                clock->publishAll();
                return model->shutdownComplete() && model->backendExecutor()->isDrained() &&
                    transport.quiescent() && clock->publicationsIdle();
            }, 3000);
        };
        const auto cleanup = qScopeGuard([&] {
            if (!retire()) QTest::qFail("Original fee-phase graph did not physically drain", __FILE__, __LINE__);
        });
        auto* recipient = model->sendRecipientList()->currentRecipient();
        recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
        recipient->amount()->setSatoshi(50'000);
        QVERIFY(QTest::qWaitFor([&] {
            transport.drain();
            clock->publishAll();
            QCoreApplication::processEvents();
            return model->sendRecipientList()->allValid() && model->availableSendBalanceKnown() &&
                !model->coinSelectionPending() && !model->fees()->policyPending() &&
                transport.transportIdle() && clock->publicationsIdle();
        }, 5000));
        model->scheduleFeeEstimates();
        // Scheduling also requests an independent Native send-facts refresh.
        // Acknowledge that original false -> true publication before holding
        // only the fee-phase GUI receipt; advancing the debounce clock here
        // would mix the two observations again.
        QVERIFY(!model->availableSendBalanceKnown());
        const auto native_barrier = std::make_shared<std::atomic_bool>(false);
        const auto barrier = model->opening()->read([](interfaces::Wallet&) { return 0; },
            [native_barrier](int) { native_barrier->store(true); });
        QVERIFY(barrier);
        QVERIFY(QTest::qWaitFor([&] {
            transport.drain();
            clock->publishAll();
            QCoreApplication::processEvents();
            return native_barrier->load() && model->availableSendBalanceKnown() &&
                model->sendRecipientList()->allValid() && model->sendRecipientList()->draftSnapshot() &&
                !model->coinSelectionPending() && !model->fees()->policyPending() &&
                transport.transportIdle() && clock->publicationsIdle();
        }, 5000));
        QVERIFY(captured.empty());
        const auto core_before = transport.coreSubmissions();
        const auto available_before = wallet->calls.getAvailableBalance.load();
        QVERIFY(model->feeEstimatePending());
        clock->advance(std::chrono::milliseconds{249});
        QVERIFY(captured.empty());
        QCOMPARE(transport.coreSubmissions(), core_before);
        clock->advance(std::chrono::milliseconds{1});
        QVERIFY(captured.empty());
        QCOMPARE(transport.coreSubmissions(), core_before);
        QCOMPARE(transport.queuedCountIf([](const auto& turn) {
            return turn.lane == execution::Lane::Core;
        }), size_t{0});
        const auto decision = transport.dispatchNext(execution::Lane::Domain);
        QVERIFY(decision);
        QCOMPARE(transport.scheduler->phase(*decision), std::make_optional(execution::Phase::Queued));
        if (reject_core) transport.rejectLane(execution::Lane::Core);
        QVERIFY(transport.finishWork(*decision));
        QCOMPARE(transport.coreSubmissions(), core_before + 1);
        QCOMPARE(transport.scheduler->phase(*decision), std::make_optional(execution::Phase::OutcomeReady));
        QVERIFY(captured.empty());
        if (!reject_core) {
            // R17 exposes Reading as a distinct phase before Native starts.
            QCOMPARE(clock->publicationCount(), size_t{1}); clock->publishAll();
            QVERIFY(model->feeEstimatePending()); QCOMPARE(model->estimatedFeeSatoshi(), qint64{-1});
        }
        std::optional<execution::TaskId> effect;
        if (!reject_core) {
            effect = transport.dispatchNext(execution::Lane::Core);
            QVERIFY(effect);
            QVERIFY(*effect != *decision);
            QVERIFY(transport.finishWork(*effect));
            QCOMPARE(captured, std::vector<CAmount>{50'000});
            QCOMPARE(transport.scheduler->phase(*effect), std::make_optional(execution::Phase::OutcomeReady));
            QVERIFY(clock->publicationsIdle());
        } else {
            QVERIFY(!transport.dispatchNext(execution::Lane::Core));
            QVERIFY(captured.empty());
        }
        // The original decision and effect receipts are consumed on Domain;
        // typed GUI state remains pending until its separate held publication.
        while (transport.step(execution::Lane::Domain)) {}
        QVERIFY(!transport.scheduler->phase(*decision));
        if (effect) QVERIFY(!transport.scheduler->phase(*effect));
        QCOMPARE(clock->publicationCount(), size_t{1});
        QVERIFY(model->feeEstimatePending());
        QCOMPARE(model->estimatedFeeForTarget(6), QString{});
        clock->publishAll();
        QVERIFY(!model->feeEstimatePending());
        if (reject_core) QCOMPARE(model->estimatedFeeForTarget(6), QString{});
        else QCOMPARE(model->estimatedFeeSatoshi(), qint64{500});
        QCOMPARE(wallet->calls.createTransaction.load(), 0);
        QCOMPARE(wallet->calls.getAvailableBalance.load(), available_before);
        qInfo() << "FEE_DOMAIN_PHASE" << reject_core << barrier.id.value << decision->value << (effect ? effect->value : 0);
        QVERIFY(retire());
        QVERIFY(transport.quiescent());
    }
    void rejectedFeeDispatchSettlesPending() {
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        int reads{0};
        FeeReadServices services{env->scheduler, env, [&, receipts](const FeeReadInput&) {
            receipts->readStarted(); ++reads; return FeeReadResult{};
        }, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services), false, {}, {}, {}, env->scheduler);
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env);
        QVERIFY(SettleManualFeeInputs(*model, env, receipts));
        model->scheduleFeeEstimates(); receipts->drain();
        QCOMPARE(model->fees()->estimateState(), FeeSelectionModel::Scheduled);
        QVERIFY(model->feeEstimatePending());
        env->rejectNext(); env->advance(std::chrono::milliseconds{250});
        receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(!model->feeEstimatePending());
        model->scheduleFeeEstimates(); receipts->drain();
        QCOMPARE(model->fees()->estimateState(), FeeSelectionModel::Scheduled);
        QVERIFY(model->feeEstimatePending());
        env->rejectNext(); env->advance(std::chrono::milliseconds{250});
        receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(!model->feeEstimatePending());
        QCOMPARE(reads, 0); QCOMPARE(receipts->publicationCount(), size_t{0});
        QCOMPARE(wallet->calls.createTransaction.load(), 0);
        model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(model.get_deleter().manual->finish()); QVERIFY(env->quiescent());
    }

    void manualFeeClockSeparatesCheckpoints() {
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        int reads{0};
        FeeReadServices services{env->scheduler, env, [&, receipts](const FeeReadInput& input) {
            receipts->readStarted(); ++reads;
            if (input.recipients.size() != 1) throw std::runtime_error("unexpected recipient count");
            FeeReadResult result; result.estimates.insert(6, SendFeePreview{500, 2, 2500, {}, false}); return result;
        }, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services), false, {}, {}, {}, env->scheduler);
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env);
        QVERIFY(SettleManualFeeInputs(*model, env, receipts));
        model->scheduleFeeEstimates(); receipts->drain();
        QCOMPARE(model->fees()->estimateState(), FeeSelectionModel::Scheduled);
        QVERIFY(model->feeEstimatePending());
        env->advance(std::chrono::milliseconds{249}); QCOMPARE(reads, 0);
        env->advance(std::chrono::milliseconds{1}); QCOMPARE(reads, 0);
        const auto original = receipts->startRead(); QVERIFY(original); QCOMPARE(reads, 1);
        env->publishAll(); // Acknowledge Reading before holding its outcome.
        QCOMPARE(env->scheduler->phase(*original), std::make_optional(execution::Phase::OutcomeReady));
        QCOMPARE(model->estimatedFeeForTarget(6), QString{});
        QVERIFY(receipts->finishOutcome(*original));
        QVERIFY(!env->scheduler->phase(*original));
        QCOMPARE(receipts->publicationCount(), size_t{1});
        QVERIFY(model->feeEstimatePending()); QCOMPARE(model->estimatedFeeForTarget(6), QString{});
        QVERIFY(receipts->publish(*original)); QVERIFY(!receipts->publish(*original));
        QCOMPARE(model->estimatedFeeSatoshi(), 500); QVERIFY(!model->feeEstimatePending());
        QCOMPARE(wallet->calls.createTransaction.load(), 0);
        model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(model.get_deleter().manual->finish()); QVERIFY(env->quiescent());
        QCOMPARE(receipts->publicationCount(), size_t{0});
        qInfo().noquote() << "R01_TRACE" << QString::fromStdString(env->trace());
    }

    void manualFeeOldResultCannotRetargetNewInput_data() {
        QTest::addColumn<bool>("old_error");
        QTest::newRow("old-success") << false;
        QTest::newRow("old-error") << true;
    }
    void manualFeeOldResultCannotRetargetNewInput() {
        QFETCH(bool, old_error);
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        std::vector<CAmount> captured;
        FeeReadServices services{env->scheduler, env, [&, receipts](const FeeReadInput& input) {
            receipts->readStarted(); captured.push_back(input.recipients.front().amount);
            if (old_error && captured.size() == 1) throw std::runtime_error("old fee failure");
            FeeReadResult result; result.estimates.insert(6, SendFeePreview{input.recipients.front().amount == 50000 ? 500 : 600, 1, 1000, {}, false}); return result;
        }, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services), false, {}, {}, {}, env->scheduler);
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env);
        QVERIFY(SettleManualFeeInputs(*model, env, receipts));
        model->scheduleFeeEstimates(); receipts->drain();
        QCOMPARE(model->fees()->estimateState(), FeeSelectionModel::Scheduled);
        QVERIFY(model->feeEstimatePending());
        env->advance(std::chrono::milliseconds{250});
        // Explicitly admit immutable A on Domain before replacing the mailbox.
        const auto decision = env->takeTurnFront(); QVERIFY(decision); decision->turn(); env->publishAll();
        QVERIFY(captured.empty());
        model->sendRecipientList()->currentRecipient()->amount()->setSatoshi(60000);
        model->scheduleFeeEstimates(); // B changes before A's Native effect.
        const auto original = receipts->startRead(); QVERIFY(original);
        env->publishAll(); // Acknowledge Reading before retaining the original outcome.
        QCOMPARE(captured, std::vector<CAmount>{50000});
        // B already owns the desired intent. An old success may publish
        // retained stale progress; an old error must never become B's error.
        QVERIFY(receipts->finishOutcome(*original));
        QCOMPARE(receipts->publicationCount(), old_error ? size_t{0} : size_t{1});
        receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return model->sendRecipientList()->allValid() && !model->sendRecipientList()->currentRecipient()->validationPending() && model->feeEstimatePending();
        }, 3000));
        QCOMPARE(captured, std::vector<CAmount>{50000});
        QCOMPARE(model->estimatedFeeForTarget(6), QString{});
        env->advance(std::chrono::milliseconds{250});
        const auto replacement = receipts->startRead(); QVERIFY(replacement); QVERIFY(*replacement != *original);
        env->publishAll(); // Keep the Native outcome's GUI receipt distinct from Reading.
        QCOMPARE(captured, std::vector<CAmount>({50000, 60000}));
        QVERIFY(receipts->finishOutcome(*replacement)); QCOMPARE(receipts->publicationCount(), size_t{1});
        QVERIFY(receipts->publish(*replacement));
        QCOMPARE(model->estimatedFeeSatoshi(), 600); QVERIFY(!model->feeEstimatePending());
        QCOMPARE(wallet->calls.createTransaction.load(), 0);
        model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(model.get_deleter().manual->finish()); QVERIFY(env->quiescent());
        qInfo().noquote() << "R01_TRACE" << QString::fromStdString(env->trace());
    }

    void manualFeeRejectionExceptionAndObserverLoss_data() {
        QTest::addColumn<bool>("error");
        QTest::newRow("success") << false;
        QTest::newRow("exception") << true;
    }
    void manualFeeRejectionExceptionAndObserverLoss() {
        QFETCH(bool, error);
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        int reads{0};
        std::optional<feeestimate::Dependencies> captured_dependencies;
        FeeReadServices services{env->scheduler, env, [&, receipts](const FeeReadInput& input) -> FeeReadResult {
            receipts->readStarted(); ++reads; captured_dependencies = input.dependencies;
            if (error) throw std::runtime_error("synthetic fee failure");
            FeeReadResult result; result.estimates.insert(6, SendFeePreview{500, 1, 1000, {}, false}); return result;
        }, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        // Opening facts and fee work share the controlled transport. A late
        // real-thread balance/policy observation would legitimately supersede
        // this row's original fee revision and suppress its stale exception.
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services), false, {}, {}, {}, env->scheduler);
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env);
        QVERIFY(SettleManualFeeInputs(*model, env, receipts));
        model->scheduleFeeEstimates(); receipts->drain(); env->rejectNext();
        env->advance(std::chrono::milliseconds{250}); QCOMPARE(reads, 0); env->publishAll(); QVERIFY(!model->feeEstimatePending());
        model->scheduleFeeEstimates(); receipts->drain(); env->advance(std::chrono::milliseconds{250});
        const auto original = receipts->startRead(); QVERIFY(original); QCOMPARE(reads, 1); env->publishAll();
        {
            // This inspection alias owns Native cleanup until it is released.
            const auto original_draft = model->sendRecipientList()->draftSnapshot(); QVERIFY(original_draft);
            QVERIFY(captured_dependencies);
            QCOMPARE(captured_dependencies->draft, original_draft->revision);
            QCOMPARE(captured_dependencies->intent, original_draft->intent_revision);
            QCOMPARE(captured_dependencies->policy, model->fees()->policyRevision());
            QCOMPARE(captured_dependencies->requirements, model->fees()->observationRevision());
        }
        QCOMPARE(model->fees()->estimateState(), FeeSelectionModel::Reading);
        QVERIFY(receipts->finishOutcome(*original)); QCOMPARE(receipts->publicationCount(), size_t{1});
        QVERIFY(receipts->publish(*original));
        QCOMPARE(model->estimatedFeeSatoshi(), error ? -1 : 500); QVERIFY(!model->feeEstimatePending());
        model->scheduleFeeEstimates(); receipts->drain(); env->advance(std::chrono::milliseconds{250});
        const auto final_decision = env->takeTurnFront(); QVERIFY(final_decision); final_decision->turn();
        model.reset(); // The already-admitted original Core effect survives observer loss.
        receipts->drain(); receipts->publishAll(); env->publishAll(); QCOMPARE(reads, 2);
        QVERIFY(model.get_deleter().manual->finish()); QVERIFY(env->quiescent());
        QCOMPARE(receipts->publicationCount(), size_t{0});
        qInfo().noquote() << "R01_TRACE" << QString::fromStdString(env->trace());
    }

    void capturedFeeReadRejectsAnAddressInvalidatedBeforeExpiry() {
        auto clock = std::make_shared<ManualExecutionEnvironment>();
        FeeReadServices services;
        services.timer = clock; // A manual clock with the real background effect transport.
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services));
        SetValidRecipient(*model);
        model->scheduleFeeEstimates();
        model->sendRecipientList()->currentRecipient()->address()->setAddress("invalid", 0);
        clock->advance(std::chrono::milliseconds{250});
        QTRY_VERIFY(!model->feeEstimatePending());
        QCOMPARE(wallet->calls.createTransaction.load(), 0);
        QCOMPARE(model->estimatedFeeForTarget(6), QString{});
        QVERIFY(clock->quiescent());
    }
    void heldMaximumEstimateCannotOverwriteANewerFixedIntent_data()
    {
        QTest::addColumn<bool>("imported"); QTest::newRow("ordinary-equal") << false; QTest::newRow("imported-equal") << true;
    }
    void heldMaximumEstimateCannotOverwriteANewerFixedIntent()
    {
        QFETCH(bool, imported);
        auto env = std::make_shared<ManualExecutionEnvironment>(); auto receipts = std::make_shared<ManualFeeReceipts>(env);
        std::vector<quint64> tokens;
        FeeReadServices services{env->scheduler, env, [&, receipts](const FeeReadInput& input) {
            receipts->readStarted(); tokens.push_back(input.maximum_token);
            FeeReadResult result; result.estimates.insert(6, SendFeePreview{500, 1, 1000, CAmount{99000}, false}); return result;
        }, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services)); Q_UNUSED(wallet);
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env); auto* recipient = model->sendRecipientList()->currentRecipient();
        model->useMaximum();
        QVERIFY(QTest::qWaitFor([&] { receipts->drain(); env->publishAll(); QCoreApplication::processEvents(); return model->maximumRecipient() == recipient && model->sendRecipientList()->allValid() && model->availableSendBalanceKnown() && !model->coinSelectionPending() && !model->fees()->policyPending() && model->feeEstimatePending(); }, 3000));
        env->advance(std::chrono::milliseconds{250}); const auto original = receipts->startRead(); QVERIFY(original);
        env->publishAll(); // Acknowledge Reading; retain only the original outcome.
        QVERIFY(receipts->finishOutcome(*original)); const auto exact = recipient->amount()->satoshi();
        QCOMPARE(tokens, std::vector<quint64>{recipient->identity()});
        if (imported) recipient->applyParsedPaymentRequest({{"success", true}, {"address", VALID_MAINNET_ADDRESS}, {"hasAmount", true}, {"amountSats", exact}});
        else recipient->amount()->editInput(recipient->amount()->inputText());
        QVERIFY(!model->sendRecipientList()->draftSnapshot());
        QVERIFY(receipts->publish(*original)); // GUI receives the old successful estimate after the new input.
        QCOMPARE(recipient->amount()->satoshi(), exact);
        QVERIFY(QTest::qWaitFor([&] { receipts->drain(); env->publishAll(); QCoreApplication::processEvents(); return !model->maximumRecipient() && model->sendRecipientList()->allValid() && model->availableSendBalanceKnown() && !model->coinSelectionPending() && !model->fees()->policyPending() && model->feeEstimatePending(); }, 3000));
        env->advance(std::chrono::milliseconds{250}); const auto successor = receipts->startRead(); QVERIFY(successor);
        env->publishAll(); // Acknowledge Reading; retain only the successor outcome.
        QCOMPARE(tokens.size(), size_t{2}); QCOMPARE(tokens.back(), quint64{0});
        QVERIFY(receipts->finishOutcome(*successor)); QVERIFY(receipts->publish(*successor));
        QCOMPARE(recipient->amount()->satoshi(), exact);
    }
    void pendingAmountIntentCannotPrepareTheEarlierValidDraft()
    {
        auto env = std::make_shared<ManualExecutionEnvironment>(); auto receipts = std::make_shared<ManualFeeReceipts>(env);
        auto effects = std::make_shared<std::atomic_int>(0);
        auto observed_amount = std::make_shared<std::atomic<CAmount>>(0);
        FeeReadServices services; services.scheduler = env->scheduler; services.timer = env; services.post = [receipts](auto publication) { receipts->post(std::move(publication)); };
        auto [wallet, model] = MakeConfiguredWalletModel([effects, observed_amount](MockWallet& value) {
            value.create_transaction_fn = [effects, observed_amount](const auto& recipients, const auto&, bool, int&, CAmount&) -> util::Result<CTransactionRef> {
                ++*effects;
                observed_amount->store(recipients.empty() ? 0 : recipients.front().nAmount);
                return util::Error{Untranslated("unexpected preparation")};
            };
        }, nullptr, nullptr, std::move(services), false, {}, {}, env->scheduler); Q_UNUSED(wallet);
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env);
        QVERIFY(SettleManualFeeInputs(*model, env, receipts));
        const auto original = model->sendRecipientList()->draftSnapshot(); QVERIFY(original);
        auto* recipient = model->sendRecipientList()->currentRecipient(); recipient->amount()->editInput("1.");
        QVERIFY(!model->sendRecipientList()->draftSnapshot()); QVERIFY(model->requestPrepareTransaction() != 0);
        QCOMPARE(effects->load(), 0); QVERIFY(!model->currentTransaction());
        recipient->amount()->setSatoshi(2000);
        QCOMPARE(model->requestPrepareTransaction(), quint64{0}); // The first domain decision has not been delivered yet.
        QCOMPARE(effects->load(), 0);
        QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return !model->preparationBusy() && model->sendRecipientList()->allValid();
        }, 3000));
        const auto latest = model->sendRecipientList()->draftSnapshot(); QVERIFY(latest);
        QCOMPARE(latest->recipients.front().amount, CAmount{2000}); QCOMPARE(original->recipients.front().amount, CAmount{50000});
        QCOMPARE(effects->load(), 0); QVERIFY(!model->currentTransaction());
        const auto current = model->requestPrepareTransaction(); QVERIFY(current);
        QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return !model->preparationBusy();
        }, 3000));
        QCOMPARE(effects->load(), 1); QCOMPARE(observed_amount->load(), CAmount{2000});
        QVERIFY(!model->currentTransaction());
    }
    void explicitSameAmountExitsMaximumAtEveryExistingEntryPoint_data()
    {
        QTest::addColumn<int>("producer");
        QTest::newRow("ordinary-canonical-equal") << 0;
        QTest::newRow("ordinary-typed-equal") << 1;
        QTest::newRow("payment-request-import-equal") << 2;
    }
    void explicitSameAmountExitsMaximumAtEveryExistingEntryPoint()
    {
        QFETCH(int, producer); auto [wallet, model] = MakeWalletModel(); Q_UNUSED(wallet);
        auto* recipient = model->sendRecipientList()->currentRecipient();
        QTRY_VERIFY(model->capabilitiesKnown() && model->balanceKnown() && model->availableSendBalanceKnown());
        QVERIFY(recipient->address()->address().isEmpty());
        model->useMaximum(); QTRY_COMPARE(model->maximumRecipient(), recipient);
        QTRY_COMPARE(recipient->amount()->satoshi(), CAmount{10 * COIN});
        const auto exact = recipient->amount()->satoshi();
        if (producer == 0) recipient->amount()->setSatoshi(exact);
        if (producer == 1) recipient->amount()->editInput(recipient->amount()->inputText());
        if (producer == 2) recipient->applyParsedPaymentRequest({{"success", true}, {"address", VALID_MAINNET_ADDRESS}, {"hasAmount", true}, {"amountSats", exact}});
        QTRY_COMPARE(model->maximumRecipient(), nullptr);
        QTRY_VERIFY(!recipient->validationPending());
        QCOMPARE(recipient->amount()->satoshi(), exact);
        QVERIFY(!model->sendDraftSweepsWallet());
    }
    void transferringMaximumDoesNotLeaveTheFormerSweepAsFixedMoney()
    {
        auto [wallet, model] = MakeWalletModel(); Q_UNUSED(wallet);
        auto* first = model->sendRecipientList()->currentRecipient();
        QTRY_VERIFY(model->capabilitiesKnown() && model->balanceKnown() && model->availableSendBalanceKnown());
        model->useMaximum(); QTRY_COMPARE(model->maximumRecipient(), first);
        QTRY_COMPARE(first->amount()->satoshi(), CAmount{10 * COIN});
        model->sendRecipientList()->add(); auto* second = model->sendRecipientList()->currentRecipient();
        second->address()->setAddress(VALID_MAINNET_P2SH_ADDRESS, 0);
        model->useMaximum(); QTRY_COMPARE(model->maximumRecipient(), second);
        QTRY_VERIFY(!first->amount()->isSet()); QCOMPARE(first->amount()->satoshi(), CAmount{0});
        QVERIFY(!model->sendRecipientList()->allValid());
    }
    void automaticMaximumValidationKeepsOriginalIntent_data() {
        QTest::addColumn<int>("boundary");
        QTest::newRow("matching-automatic-validation") << 0;
        QTest::newRow("new-user-amount") << 1;
        QTest::newRow("other-recipient-edit") << 2;
        QTest::newRow("replacement-maximum-token") << 3;
        QTest::newRow("close-before-validation") << 4;
        QTest::newRow("reentrant-other-recipient-edit") << 5;
    }
    void automaticMaximumValidationKeepsOriginalIntent() {
        QFETCH(int, boundary);
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        std::vector<quint64> maximum_tokens; maximum_tokens.reserve(2);
        FeeReadServices services{env->scheduler, env, [&, receipts](const FeeReadInput& input) {
            receipts->readStarted(); maximum_tokens.push_back(input.maximum_token);
            FeeReadResult result; result.estimates.insert(6, SendFeePreview{500, 1, 1000, CAmount{99000}, false}); return result;
        }, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services));
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env);
        auto* original_recipient = model->sendRecipientList()->currentRecipient();
        SendRecipient* other{nullptr};
        if (boundary == 2 || boundary == 3 || boundary == 5) {
            model->sendRecipientList()->add(); other = model->sendRecipientList()->currentRecipient();
            other->address()->setAddress(VALID_MAINNET_P2SH_ADDRESS, 0); other->amount()->setSatoshi(10000);
            model->sendRecipientList()->setCurrentIndex(0);
        }
        QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return model->capabilitiesKnown() && model->balanceKnown() && model->availableSendBalanceKnown() && model->sendRecipientList()->allValid() && !model->coinSelectionPending() && !model->fees()->policyPending();
        }, 3000));
        model->useMaximum(); receipts->drain();
        QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return model->availableSendBalanceKnown() && model->sendRecipientList()->allValid() && !model->coinSelectionPending() && !model->fees()->policyPending() && model->feeEstimatePending();
        }, 3000));
        bool reentrant_edit{false};
        const auto reentrant_connection = connect(original_recipient->amount(), &BitcoinAmount::amountChanged, model.get(), [&] {
            if (boundary == 5 && !reentrant_edit && original_recipient->amount()->satoshi() == 99000) {
                reentrant_edit = true; other->amount()->setSatoshi(20000);
            }
        });
        const auto disconnect_reentrant = qScopeGuard([reentrant_connection] { QObject::disconnect(reentrant_connection); });
        env->advance(std::chrono::milliseconds{250});
        const auto original = receipts->startRead(); QVERIFY(original);
        env->publishAll(); // Acknowledge Reading before retaining the original outcome.
        QVERIFY(receipts->finishOutcome(*original)); QVERIFY(receipts->publish(*original));
        QCOMPARE(maximum_tokens, std::vector<quint64>{original_recipient->identity()});
        QCOMPARE(original_recipient->amount()->satoshi(), qint64{99000});
        QVERIFY(original_recipient->validationPending()); // Typed validation is physically held.
        if (boundary == 5) QVERIFY(reentrant_edit);
        if (boundary == 1) original_recipient->amount()->setSatoshi(88000);
        if (boundary == 2) other->amount()->setSatoshi(20000);
        if (boundary == 3) {
            model->sendRecipientList()->setCurrentIndex(1); model->useMaximum();
            // R16 makes handover explicit: the former computed remainder is
            // cleared. Supply a new Fixed amount before requiring a ready
            // multi-recipient successor; preserve the original held deadline.
            QVERIFY(QTest::qWaitFor([&] {
                receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
                return model->maximumRecipient() == other && !original_recipient->amount()->isSet();
            }, 3000));
            original_recipient->amount()->setSatoshi(10000);
        }
        if (boundary == 4) model->beginShutdown();
        if (boundary != 4) {
            QVERIFY(QTest::qWaitFor([&] {
                receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
                return model->sendRecipientList()->allValid();
            }, 3000));
        } else { receipts->drain(); env->publishAll(); }
        QCOMPARE(maximum_tokens.size(), size_t{1}); // No effect before a new manual deadline.
        env->advance(std::chrono::milliseconds{250});
        if (boundary == 0 || boundary == 4) {
            receipts->drain(); receipts->publishAll();
            QCOMPARE(maximum_tokens.size(), size_t{1});
            if (boundary == 0) { QCOMPARE(model->maximumRecipient(), original_recipient); QVERIFY(!model->feeEstimatePending()); }
        } else {
            const auto successor = receipts->startRead(); QVERIFY(successor); QVERIFY(*successor != *original);
            env->publishAll(); // Acknowledge Reading before retaining the original outcome.
            QCOMPARE(maximum_tokens.size(), size_t{2});
            QCOMPARE(maximum_tokens.back(), boundary == 1 ? quint64{0} : boundary == 2 || boundary == 5 ? original_recipient->identity() : other->identity());
            QVERIFY(receipts->finishOutcome(*successor)); QVERIFY(receipts->publish(*successor));
            QVERIFY(QTest::qWaitFor([&] {
                receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
                return model->sendRecipientList()->allValid();
            }, 3000));
            env->advance(std::chrono::milliseconds{250}); receipts->drain(); receipts->publishAll();
            QCOMPARE(maximum_tokens.size(), size_t{2}); // Exactly one legitimate successor.
        }
        model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(model.get_deleter().manual->finish()); QVERIFY(env->quiescent());
    }
    void maximumWithoutAddressUsesCapturedInputBalance_data()
    {
        QTest::addColumn<bool>("manual");
        QTest::addColumn<bool>("on_publication");
        QTest::addColumn<int>("facts_during_selection");
        QTest::newRow("automatic-control") << false << false << 0;
        QTest::newRow("already-published-manual-selection") << true << false << 0;
        QTest::newRow("maximum-inside-original-ready-selection-publication") << true << true << 0;
        QTest::newRow("facts-delivered-while-successor-selection-is-pending") << true << false << 1;
        QTest::newRow("failed-facts-delivered-while-successor-selection-is-pending") << true << false << 2;
    }
    void maximumWithoutAddressUsesCapturedInputBalance()
    {
        QFETCH(bool, manual); QFETCH(bool, on_publication); QFETCH(int, facts_during_selection);
        bool maximum_requested{false}, throw_next_facts_read{false};
        auto env = std::make_shared<ManualExecutionEnvironment>();
        const std::set<COutPoint> selected{{Txid::FromUint256(uint256::ONE), 0}, {Txid::FromUint256(uint256::ONE), 1}};
        std::vector<std::vector<COutPoint>> available_reads;
        FeeReadServices services{env->scheduler, env, {}, [env](auto publication) { env->post(std::move(publication)); }};
        auto [wallet, model] = MakeConfiguredWalletModel([&](MockWallet& value) {
            value.list_coins_fn = [selected] {
                interfaces::Wallet::CoinsList coins;
                const auto destination = DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
                for (const auto& point : selected) coins[destination].emplace_back(point,
                    interfaces::WalletTxOut{CTxOut{50 * COIN, GetScriptForDestination(destination)}, 0, 6, false});
                return coins;
            };
            value.get_available_balance_fn = [&](const wallet::CCoinControl& control) {
                available_reads.push_back(control.ListSelected());
                if (std::exchange(throw_next_facts_read, false)) throw std::runtime_error("original facts read failed");
                return control.m_allow_other_inputs || control.ListSelected().size() == selected.size() ? 100 * COIN : CAmount{0};
            };
            value.get_balance_fn = [] { return 100 * COIN; };
            value.get_minimum_fee_fn = [](const wallet::CCoinControl&) { return CAmount{1000}; };
        }, nullptr, nullptr, std::move(services), false, {}, {}, env->scheduler);
        const auto cleanup = qScopeGuard([&] {
            model.reset(); env->drain(); env->publishAll();
            model.get_deleter().manual->finish();
        });
        const auto settle = [&] {
            for (int turn{0}; turn < 128; ++turn) {
                env->drain(); env->publishAll(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
                if (env->turnsIdle() && env->publicationsIdle()) return true;
            }
            return false;
        };
        QVERIFY(settle()); QVERIFY(model->available());
        if (on_publication) connect(model->coinsListModel(), &CoinsListModel::selectionSnapshotChanged, model.get(), [&] {
            if (!model || maximum_requested || model->coinSelectionPending() ||
                !model->coinsListModel()->manualInputSelection() || !model->selectedCoins() || *model->selectedCoins() != selected) return;
            maximum_requested = true;
            model->useMaximum();
        });
        if (manual) {
            QVERIFY(model->coinsListModel()->setManualInputSelection(true));
            for (const auto& point : selected) model->selectCoin(point);
            QVERIFY(settle()); QVERIFY(!model->coinSelectionPending());
            QVERIFY(model->selectedCoins());
            QVERIFY(*model->selectedCoins() == selected);
            QCOMPARE(model->coinsListModel()->totalSelectedSatoshi(), CAmount{100 * COIN});
        }
        if (facts_during_selection) {
            // Complete an ordinary observation but hold its presentation turn.
            // A newer public selection intent then owns the pending state.
            const auto reads_before = available_reads.size();
            throw_next_facts_read = facts_during_selection == 2;
            model->scheduleFeeEstimates();
            env->drain();
            QVERIFY(available_reads.size() > reads_before);
            QVERIFY(!env->publicationsIdle());
            const auto original_point = *selected.begin();
            model->unselectCoin(original_point);
            QVERIFY(model->coinSelectionPending());
            env->publishAll();
            QVERIFY(model->coinSelectionPending()); // Its Domain turn is held.
            QVERIFY(!model->availableSendBalanceKnown());
            model->selectCoin(original_point);
            QVERIFY(settle());
            QVERIFY(!model->coinSelectionPending());
            QVERIFY(model->selectedCoins());
            QVERIFY(*model->selectedCoins() == selected);
            QCOMPARE(model->coinsListModel()->totalSelectedSatoshi(), CAmount{100 * COIN});
        }
        auto* recipient = model->sendRecipientList()->currentRecipient(); QVERIFY(recipient);
        QVERIFY(recipient->address()->address().isEmpty());
        if (!on_publication) { maximum_requested = true; model->useMaximum(); }
        QVERIFY(maximum_requested); QVERIFY(settle());
        QCOMPARE(model->maximumRecipient(), recipient);
        QVERIFY(!model->sendRecipientList()->validationPending());
        QVERIFY(model->availableSendBalanceKnown());
        qInfo() << "Maximum captured balance" << "manual" << manual << "reads" << available_reads.size()
                << "amount" << recipient->amount()->satoshi();
        QCOMPARE(recipient->amount()->satoshi(), CAmount{100 * COIN});
        if (manual) QVERIFY(std::ranges::any_of(available_reads, [&](const auto& points) {
            return std::set<COutPoint>(points.begin(), points.end()) == selected;
        }));
        QCOMPARE(wallet->calls.createTransaction.load(), 0);
        model.reset(); env->drain(); env->publishAll();
        QVERIFY(model.get_deleter().manual->finish()); QVERIFY(env->quiescent());
    }

    void manualMaximumPublication_data() {
        QTest::addColumn<int>("variant");
        QTest::newRow("standard-maximum") << 0;
        QTest::newRow("custom-maximum") << 1;
        QTest::newRow("no-preview") << 2;
        QTest::newRow("preview-without-maximum") << 3;
    }
    void manualMaximumPublication() {
        QFETCH(int, variant);
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        int reads{0};
        FeeReadServices services{env->scheduler, env, [&, receipts](const FeeReadInput& input) {
            receipts->readStarted(); ++reads;
            if (input.remainder_index != 0) throw std::runtime_error("maximum owner changed");
            FeeReadResult result;
            SendFeePreview preview{500, 1, 1000, variant < 2 ? std::make_optional<CAmount>(99000) : std::nullopt, false};
            if (variant == 1) result.custom = preview;
            else if (variant != 2) result.estimates.insert(6, preview);
            return result;
        }, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services));
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env);
        if (variant == 1) { model->setCustomFeeEnabled(true); model->setCustomFeeRate("2.5"); }
        model->useMaximum(); receipts->drain();
        connect(model->sendRecipientList()->currentRecipient()->amount(), &BitcoinAmount::amountChanged, model.get(), &WalletQmlModel::scheduleFeeEstimates);
        QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return model->capabilitiesKnown() && model->balanceKnown() && model->availableSendBalanceKnown() &&
                model->sendRecipientList()->allValid() && !model->sendRecipientList()->currentRecipient()->validationPending() && model->feeEstimatePending() && !model->coinSelectionPending() && !model->fees()->policyPending();
        }, 3000));
        env->advance(std::chrono::milliseconds{250});
        const auto original = receipts->startRead(); QVERIFY(original); QCOMPARE(reads, 1); env->publishAll();
        QVERIFY(receipts->finishOutcome(*original)); QCOMPARE(receipts->publicationCount(), size_t{1});
        QCOMPARE(model->sendRecipientList()->currentRecipient()->cAmount(), CAmount{50000});
        QVERIFY(receipts->publish(*original));
        const auto expected = variant < 2 ? CAmount{99000} : CAmount{50000};
        QCOMPARE(model->sendRecipientList()->currentRecipient()->amount()->satoshi(), expected);
        QVERIFY(!model->feeEstimatePending());
        QVERIFY(QTest::qWaitFor([&] {
            receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
            return !model->sendRecipientList()->currentRecipient()->validationPending() && model->sendRecipientList()->allValid() && model->sendRecipientList()->currentRecipient()->cAmount() == expected;
        }, 3000));
        env->advance(std::chrono::milliseconds{250}); receipts->drain(); receipts->publishAll();
        QCOMPARE(reads, 1); // Publishing the original maximum cannot create another business read.
        QCOMPARE(wallet->calls.createTransaction.load(), 0);
        model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(model.get_deleter().manual->finish()); QVERIFY(env->quiescent());
        qInfo().noquote() << "R01_TRACE" << QString::fromStdString(env->trace());
    }

    void manualQueuedPublicationKeepsItsOwner_data() {
        QTest::addColumn<int>("boundary");
        QTest::addColumn<bool>("error");
        for (bool error : {false, true}) {
            QTest::newRow(error ? "old-error-new-draft" : "old-success-new-draft") << 0 << error;
            QTest::newRow(error ? "error-after-close" : "success-after-close") << 1 << error;
            QTest::newRow(error ? "error-after-observer-loss" : "success-after-observer-loss") << 2 << error;
        }
    }
    void manualQueuedPublicationKeepsItsOwner() {
        QFETCH(int, boundary); QFETCH(bool, error);
        auto env = std::make_shared<ManualExecutionEnvironment>();
        auto receipts = std::make_shared<ManualFeeReceipts>(env);
        int reads{0}, notifications{0};
        std::optional<feeestimate::Dependencies> captured_dependencies;
        FeeReadServices services{env->scheduler, env, [&, receipts](const FeeReadInput& input) {
            receipts->readStarted(); ++reads; captured_dependencies = input.dependencies;
            if (error) throw std::runtime_error("queued old error");
            FeeReadResult result; result.estimates.insert(6, SendFeePreview{500, 1, 1000, {}, false}); return result;
        }, [receipts](auto publication) { receipts->post(std::move(publication)); }};
        auto [wallet, model] = MakeWalletModel(nullptr, nullptr, std::move(services), false, {}, {}, {}, env->scheduler);
        const auto cleanup = qScopeGuard([&] { model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll(); });
        connect(model.get(), &WalletQmlModel::estimatedFeeChanged, model.get(), [&] { ++notifications; });
        connect(model.get(), &WalletQmlModel::feeEstimatePendingChanged, model.get(), [&] { ++notifications; });
        SetValidRecipient(*model, VALID_MAINNET_ADDRESS, env);
        QVERIFY(SettleManualFeeInputs(*model, env, receipts));
        model->scheduleFeeEstimates(); receipts->drain(); env->advance(std::chrono::milliseconds{250});
        const auto original = receipts->startRead(); QVERIFY(original); QCOMPARE(reads, 1); env->publishAll();
        {
            // This inspection alias owns Native cleanup until it is released.
            const auto original_draft = model->sendRecipientList()->draftSnapshot(); QVERIFY(original_draft);
            QVERIFY(captured_dependencies);
            QCOMPARE(captured_dependencies->draft, original_draft->revision);
            QCOMPARE(captured_dependencies->intent, original_draft->intent_revision);
            QCOMPARE(captured_dependencies->policy, model->fees()->policyRevision());
            QCOMPARE(captured_dependencies->requirements, model->fees()->observationRevision());
        }
        QCOMPARE(model->fees()->estimateState(), FeeSelectionModel::Reading);
        QVERIFY(receipts->finishOutcome(*original));
        QCOMPARE(receipts->publicationCount(), size_t{1}); QVERIFY(!env->scheduler->phase(*original));
        if (boundary == 0) {
            model->sendRecipientList()->currentRecipient()->amount()->setSatoshi(60000);
            model->scheduleFeeEstimates();
        } else if (boundary == 1) model->beginShutdown();
        else model.reset();
        notifications = 0;
        QVERIFY(receipts->publish(*original)); QVERIFY(!receipts->publish(*original));
        QCOMPARE(notifications, 0); QCOMPARE(reads, 1);
        if (model) {
            QCOMPARE(model->estimatedFeeForTarget(6), QString{});
            if (boundary == 0) {
                QVERIFY(QTest::qWaitFor([&] {
                    receipts->drain(); env->publishAll(); QCoreApplication::processEvents();
                    return model->sendRecipientList()->allValid() && !model->sendRecipientList()->currentRecipient()->validationPending() && model->feeEstimatePending();
                }, 3000));
                QCOMPARE(reads, 1); // Replacement admission has not advanced its manual deadline.
            } else QVERIFY(!model->feeEstimatePending());
        }
        model.reset(); receipts->drain(); receipts->publishAll(); env->publishAll();
        QVERIFY(model.get_deleter().manual->finish()); QVERIFY(env->quiescent());
        QCOMPARE(receipts->publicationCount(), size_t{0});
        qInfo().noquote() << "R01_TRACE" << QString::fromStdString(env->trace());
    }

    void scheduleFeeEstimates_populatesFormattedEstimates();
    void scheduleFeeEstimates_fallsBackWhenNetworkFeeEstimatesUnavailable();
    void scheduleFeeEstimates_usesStaticRegtestFeeOverride();
    void customFeeRateValid_data();
    void customFeeRateValid();
    void customFeeRateRespectsRelayMinimum_data();
    void customFeeRateRespectsRelayMinimum();
    void scheduleFeeEstimates_usesCustomFeeRateWhenEnabled_data();
    void scheduleFeeEstimates_usesCustomFeeRateWhenEnabled();
    void scheduleFeeEstimates_neverUsesSubtractFeeFallback();
    void sendAmountExhaustsBalance_requiresFeeBuffer();
    void sendAmountExhaustsBalance_usesCoinControlAvailableBalance();
    void clearSelectedCoinsKeepsPendingIntent();
    void scheduleFeeEstimates_usesDummyPreviewChangeDestination();
    void prepareTransaction_usesStaticRegtestFeeOverride();
    void prepareTransaction_usesCustomFeeRateWithoutRegtestOverride_data();
    void prepareTransaction_usesCustomFeeRateWithoutRegtestOverride();
    void prepareTransaction_neverSubtractsFeeFromRecipient();
    void preparedReview_excludesChangeOutput();
    void scheduleFeeEstimates_usesSelectedCoinsInCoinControl();
    void prepareTransaction_disallowsOtherInputsWhenCoinsSelected();
    void scheduleFeeEstimates_debouncesRapidRestarts();
    void scheduleFeeEstimates_invalidatesStalePreviewState();
    void transactionChangedEmitsBalanceChanged();
    void setCurrentPaymentRequestAddressUsesAddressListLabel();
    void commitPaymentRequestUsesSelectedAddressType();
    void updateChangesUnpaidPaymentRequestAmount();
    void emptyPaymentRequestCanRemoveSavedPrivateNote_data();
    void emptyPaymentRequestCanRemoveSavedPrivateNote();
    void paymentArrivalLocksRequestAndPreservesNote();
    void paymentRequestTracksActualReceivedAmount();
    void replacementNotificationRechecksReceiverOriginal();
    void cancelledPaymentWithoutNotificationClearsReceivedAmount();
    void reconciliationBatchesActivityRebuilds();
    void reconciliationPreservesPaymentNotifiedDuringScan();
    void openingRequestRemainsResponsiveDuringLargeHistoryScan();
    void requestDetailEditsPreserveReceiveDraft();
    void setCurrentPaymentRequestAddressEditsExistingRequest();
    void usePaymentRequestAsTemplatePreservesAddressType();
    void commitPaymentRequestOnLockedWalletSignalsNeedsUnlock();
    void commitPaymentRequestWithPassphraseUnlocksRetriesAndRelocks_data();
    void commitPaymentRequestWithPassphraseUnlocksRetriesAndRelocks();
    void commitPaymentRequestWithPassphraseWrongPasswordSurfacesError();
    void availableReceiveAddressTypesHideUnavailableTaproot();
    void receiveAddressTypeDefaultPersistsPerWallet();
    void receivingAddressIsStableUntilRotatedOrPaid();
    void receivingAddressResumeOwnsOriginalIntent_data();
    void receivingAddressResumeOwnsOriginalIntent();
    void receivingAddressCreationAndRequestPersistenceAreSeparate_data();
    void receivingAddressCreationAndRequestPersistenceAreSeparate();
    void removeReceiveRequestRemovesPendingActivityRow();
    void editedReceiveRequestNameShownInActivityMetadata();
    void editedRequestSyncsAddressBookLabel();
    void requestSaveLeavesUneditedAddressBookLabelAlone();
    void amountOnlyRequestEditPreservesAddressBookLabel();
    void editedAddressBookLabelSyncsRequestNote();
    void requestSaveLeavesSiblingRequestNotesAlone();
    void labelSyncSkipsInMemoryUpdateWhenPersistFails();
    void editedAddressBookLabelUpdatesHeldRequestObjects();
    void prepareTransactionOnLockedWalletRequiresPassword();
    void prepareTransactionWithPrivateKeysDisabledDoesNotRequirePassword();
    void sendRecipientRejectsDustAmount();
    void sendRecipientUsesNodeDustRelayFee();
    void prepareTransactionRejectsDuplicateRecipientsBeforeUnlock();
    void prepareTransactionWithPassphraseForwardsUtf8Bytes_data();
    void prepareTransactionWithPassphraseForwardsUtf8Bytes();
    void prepareTransactionWithPassphraseRelocksWhenRecipientsInvalid();
    void prepareTransactionWithPassphraseRequiresCompleteMultiRecipient();
    void prepareTransactionWithPassphraseRelocksWhenCustomFeeInvalid();
    void prepareTransactionWithPassphraseReportsCreateErrorAndRelocks();
    void sendTransactionCommitsPreparedTransactionWithoutUnlockingAgain();
    void sendTransactionClearsSelectedCoins();
    void clearingRecipientsClearsSelectedCoins();
    void saveCurrentTransactionAsPsbt_savesUnsignedPreparedTransaction();
    void importPsbtFromFile_opensOwnedUnsignedPsbtWithoutSigning();
    void importPsbtFromFile_opensWatchOnlyUnsignedPsbtForReviewOnly();
    void saveCurrentTransactionAsPsbt_preservesImportedMetadata();
    void saveReviewOnlyPsbt_preservesOriginalWithoutDerivationMetadata();
    void discardCurrentTransaction_clearsReviewState();
    void sendImportedPsbtWithPassphraseSignsOnceAndRelocks();
    void externalSignerApprovalSignsImportedPsbtOnlyOnce();
    void externalSignerApprovalKeepsIncompleteSignedPsbt();
    void importPsbtFromFile_opensForeignUnsignedPsbtForReviewOnly();
    void importPsbtFromFile_broadcastsCompleteForeignMultisigPsbt();
    void saveCompleteBroadcastableImportedPsbt_preservesOriginalPsbt();
    void importPsbtFromFile_skipsZeroValueOpReturnOutputs();
    void importPsbtFromFile_opensUnsignedMultisigPsbtForReviewOnly();
    void importPsbtFromFile_blocksBroadcastWhenFeeIsInvalid();
    void importPsbtFromFile_returnsTransactionAlreadyKnownWhenTxIsInWallet_data();
    void importPsbtFromFile_returnsTransactionAlreadyKnownWhenTxIsInWallet();
    void sendTransactionWithPrivateKeysDisabledDoesNotCommit();
    void bumpTransactionOnLockedWalletRequiresPassword();
    void bumpFeePresentationTracksUnitWithoutRepreparing();
    void replacementOutcomeQueryDoesNotBlockGuiOrRepeatTheMutation();
    void bumpTransactionWithPassphraseUnlocksCommitsAndRelocks();
    void bumpTransactionWithWrongPassphraseDoesNotSign();
    void displayNameDefaultsToWalletName();
    void unnamedWalletHasADistinguishableDisplayName();
    void detailPropertiesReflectWalletCapabilities();
    void encryptWalletUpdatesSecurityState();
    void changeWalletPassphraseForwardsPasswords();
    void backupWalletForwardsPath();
    void backupAdmissionGuards();
    void backupInjectedPortsRequireOwnedWalletAdapter_data();
    void backupInjectedPortsRequireOwnedWalletAdapter();
    void backupRefusalAndRepeatedRequestsUseInjectedExecutor();
    void signVerifyMessageRejectsNonP2PKHAddress();
    void signVerifyMessageSignsWithLegacyP2PKHAddress();
    void signVerifyMessageSignsEmptyMessage();
    void signVerifyMessageWithPassphraseUnlocksSignsAndRelocks();
    void signVerifyMessageWrongPassphraseSurfacesErrorAndDoesNotRelock();
    void signVerifyMessageSurfacesSigningFailure();
    void signVerifyMessageWithoutWalletSurfacesError();
    void signVerifyMessageClearResetsState();
    void signVerifyMessageVerifiesValidSignature();
    void signVerifyMessageVerifyRejectsEmptySignature();
};

void WalletQmlModelTests::initTestCase()
{
    SelectParams(ChainType::MAIN);
    m_ecc_context = std::make_unique<ECC_Context>();
}

void WalletQmlModelTests::cleanupTestCase()
{
    m_ecc_context.reset();
}

void WalletQmlModelTests::displayNameDefaultsToWalletName()
{
    auto [wallet, model] = MakePasswordWalletModel();

    QCOMPARE(model->displayName(), QString("fake-wallet"));
    model->setDisplayName("Personal");
    QCOMPARE(model->displayName(), QString("Personal"));
}

void WalletQmlModelTests::unnamedWalletHasADistinguishableDisplayName()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.wallet_name.clear();
    });
    QCOMPARE(model->name(), QString{});
    QCOMPARE(model->displayName(), QString{"Default wallet"});
    model->setDisplayName("Personal");
    QCOMPARE(model->displayName(), QString{"Personal"});
    model->setDisplayName({});
    QCOMPARE(model->displayName(), QString{"Default wallet"});
}

void WalletQmlModelTests::detailPropertiesReflectWalletCapabilities()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.private_keys_disabled = true;
        original.external_signer = true;
    });
    QTRY_COMPARE(model->keyScheme(), QString("External signer"));
    QCOMPARE(model->privateKeysStatus(), QString("Disabled"));
    QCOMPARE(model->externalSignerStatus(), QString("Enabled"));
    QVERIFY(!model->canManagePassphrase());

    QVERIFY(ConfigurePasswordWalletOnNative(*model, [](FakePasswordWallet& original) { original.external_signer = false; }));
    model->opening()->refreshMetadata();
    QTRY_COMPARE(model->keyScheme(), QString("Watch-only"));
    QVERIFY(ConfigurePasswordWalletOnNative(*model, [](FakePasswordWallet& original) { original.private_keys_disabled = false; }));
    model->opening()->refreshMetadata();
    QTRY_COMPARE(model->keyScheme(), QString("Software wallet"));
    QCOMPARE(model->privateKeysStatus(), QString("Enabled"));
    QVERIFY(model->canManagePassphrase());
}

void WalletQmlModelTests::encryptWalletUpdatesSecurityState()
{
    auto wallet = std::make_unique<FakePasswordWallet>();
    wallet->encrypted = false;
    wallet->locked = false;
    FakePasswordWallet* raw_wallet = wallet.get();
    std::unique_ptr<WalletQmlModel, DrainWalletModel> model{new WalletQmlModel(std::move(wallet))};

    QVERIFY(model->encryptWallet("secret"));
    QTRY_COMPARE(model->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded);
    QCOMPARE(raw_wallet->encrypt_calls, 1);
    QTRY_VERIFY(model->isEncrypted());
    QTRY_VERIFY(model->isLocked());
    QVERIFY(model->settingsError().isEmpty());
}

void WalletQmlModelTests::changeWalletPassphraseForwardsPasswords()
{
    auto [wallet, model] = MakePasswordWalletModel();

    QVERIFY(model->changeWalletPassphrase("secret", "new-secret"));
    QTRY_COMPARE(model->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded);
    QCOMPARE(wallet->change_passphrase_calls, 1);
    QCOMPARE(wallet->changed_passphrases.size(), size_t{1});
    QCOMPARE(wallet->changed_passphrases.front().first, std::string("secret"));
    QCOMPARE(wallet->changed_passphrases.front().second, std::string("new-secret"));

    QVERIFY(model->changeWalletPassphrase("wrong", "new-secret"));
    QTRY_COMPARE(model->securityState(), WalletQmlModel::SecurityState::SecurityFailed);
    QCOMPARE(model->settingsError(), QString("The current wallet password was incorrect."));
    QSignalSpy cleared{model.get(), &WalletQmlModel::settingsErrorChanged};
    QVERIFY(model->changeWalletPassphrase("secret", "new-secret"));
    QVERIFY(model->settingsError().isEmpty());
    QCOMPARE(cleared.count(), 1);
    QTRY_COMPARE(model->securityState(), WalletQmlModel::SecurityState::SecuritySucceeded);
}

void WalletQmlModelTests::backupWalletForwardsPath()
{
    auto [wallet, model] = MakePasswordWalletModel();

    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString final{dir.filePath("fake-wallet.bak")};
    QVERIFY(model->backupWallet(final));
    QVERIFY(model->backupOperation());
    QTRY_COMPARE(model->backupOperation()->phase(), BackupFileOperation::Phase::Succeeded);
    QCOMPARE(wallet->backup_calls, 1);
    QVERIFY(QString::fromStdString(wallet->last_backup_path).startsWith(dir.path() + "/.bitcoin-backup-"));
    QFile saved{final}; QVERIFY(saved.open(QIODevice::ReadOnly));
    QCOMPARE(saved.readAll(), QByteArray("fake-wallet-backup"));
    QVERIFY(model->settingsError().isEmpty());
}

void WalletQmlModelTests::backupAdmissionGuards()
{
    WalletQmlModel empty;
    QVERIFY(!empty.backupWallet("/backup.bak")); QVERIFY(!empty.settingsError().isEmpty());
    auto [wallet, model] = MakePasswordWalletModel();
    QVERIFY(!model->backupWallet("  ")); QVERIFY(!model->settingsError().isEmpty());
    QVERIFY(!model->backupWallet(QString(32'769, 'x'))); QVERIFY(!model->backupOperation());
    model->beginShutdown(); QVERIFY(!model->backupWallet("/backup.bak")); QVERIFY(!model->settingsError().isEmpty());
}

void WalletQmlModelTests::backupInjectedPortsRequireOwnedWalletAdapter_data()
{
    QTest::addColumn<int>("missing");
    QTest::newRow("both-adapters") << 0;
    QTest::newRow("writer-only") << 1;
    QTest::newRow("wallet-name-only") << 2;
}

void WalletQmlModelTests::backupInjectedPortsRequireOwnedWalletAdapter()
{
    QFETCH(int, missing);
    ManualExecutionEnvironment environment; backup::Services services; services.scheduler = environment.scheduler;
    if (missing == 1) services.wallet_name = [] { return "owned"; };
    if (missing == 2) services.write = [](const auto&, bool) { return backup::WriteResult{}; };
    auto wallet = std::make_unique<FakePasswordWallet>();
    WalletQmlModel model{std::move(wallet), nullptr, FeeReadServices{}, psbtfee::Services{}, services};
    QVERIFY(!model.backupWallet("/backup.bak")); QVERIFY(!model.backupOperation());
    QVERIFY(!model.settingsError().isEmpty()); QVERIFY(environment.quiescent());
}

void WalletQmlModelTests::backupRefusalAndRepeatedRequestsUseInjectedExecutor()
{
    ManualExecutionEnvironment environment; int writes{0}, inspections{0};
    backup::Services services; services.scheduler = environment.scheduler;
    services.post = [&](auto publication) { environment.post(std::move(publication)); };
    services.wallet_name = [] { return "owned-wallet"; };
    services.inspect = [&](const auto&, const auto&) { ++inspections; return backup::Proposal{"/final.bak", "hash", true}; };
    services.write = [&](const auto&, bool) { ++writes; return backup::WriteResult{}; };
    services.coordinator = std::make_shared<backup::InstallCoordinator>();
    auto wallet = std::make_unique<FakePasswordWallet>();
    WalletQmlModel model{std::move(wallet), nullptr, FeeReadServices{}, psbtfee::Services{}, services};
    QVERIFY(model.backupWallet("/selected.v1")); auto* first = model.backupOperation();
    QCOMPARE(inspections, 0); QVERIFY(!model.backupWallet("/another.bak"));
    environment.drain(); environment.publishAll(); QVERIFY(first->confirmationRequired()); QCOMPARE(inspections, 1);
    QVERIFY(first->refuse()); environment.drain(); environment.publishAll(); QCOMPARE(writes, 0);
    QVERIFY(model.backupWallet("/another.bak")); QVERIFY(model.backupOperation() != first);
    environment.drain(); environment.publishAll(); QCOMPARE(inspections, 2);
    QVERIFY(model.backupOperation()->approve()); environment.drain(); environment.publishAll(); QCOMPARE(writes, 1);
    QCOMPARE(model.backupOperation()->phase(), BackupFileOperation::Phase::Succeeded);
    QVERIFY(model.backupWallet("/last.bak")); model.beginShutdown(); environment.drain(); environment.publishAll();
    QCOMPARE(model.backupOperation()->phase(), BackupFileOperation::Phase::Retired); QCOMPARE(writes, 1);
}

void WalletQmlModelTests::availableReceiveAddressTypesHideUnavailableTaproot()
{
    auto [wallet, model] = MakePasswordWalletModel();

    QVariantList types = model->availableReceiveAddressTypes();
    QCOMPARE(types.size(), 4);
    QCOMPARE(types.front().toMap().value("id").toString(), QString("bech32m"));

    QVERIFY(ConfigurePasswordWalletOnNative(*model, [](FakePasswordWallet& original) {
        original.taproot_enabled = false;
        if (!original.addresses_changed) throw std::logic_error{"Missing original metadata notification"};
        original.addresses_changed();
    }));
    QTRY_COMPARE(model->availableReceiveAddressTypes().size(), 3);
    types = model->availableReceiveAddressTypes();
    QCOMPARE(types.size(), 3);
    for (const QVariant& type : types) {
        QVERIFY(type.toMap().value("id").toString() != QString("bech32m"));
    }
}

void WalletQmlModelTests::receiveAddressTypeDefaultPersistsPerWallet()
{
    MockNode node;
    common::SettingsValue preferences{UniValue::VOBJ};
    preferences.pushKV("other-wallet", "legacy");
    node.SetPersistentSetting("qml_receive_address_types", preferences);
    node.update_rw_setting_fn = [&node](const auto& key, const auto& value) {
        node.SetPersistentSetting(key, value);
    };
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.default_address_type = OutputType::BECH32;
    }, &node);
    QCOMPARE(model->defaultReceiveAddressType(), QString("bech32m"));

    QVERIFY(model->setDefaultReceiveAddressType("p2sh-segwit"));
    QTRY_COMPARE(model->defaultReceiveAddressType(), QString("p2sh-segwit"));
    const auto saved = node.getPersistentSetting("qml_receive_address_types");
    QCOMPARE(saved["fake-wallet"].get_str(), std::string("p2sh-segwit"));
    QCOMPARE(saved["other-wallet"].get_str(), std::string("legacy"));
    auto [restored_wallet, restored_model] = MakePasswordWalletModel(&node);
    QCOMPARE(restored_model->defaultReceiveAddressType(), QString("p2sh-segwit"));

    QVERIFY(!model->setDefaultReceiveAddressType("invalid"));
    QCOMPARE(model->defaultReceiveAddressType(), QString("p2sh-segwit"));
    QVERIFY(model->setDefaultReceiveAddressType("bech32m"));
    QTRY_COMPARE(model->defaultReceiveAddressType(), QString("bech32m"));
    QVERIFY(ConfigurePasswordWalletOnNative(*model, [](FakePasswordWallet& original) {
        original.taproot_enabled = false;
        if (!original.addresses_changed) throw std::logic_error{"Missing original metadata notification"};
        original.addresses_changed();
    }));
    QTRY_COMPARE(model->defaultReceiveAddressType(), QString("bech32"));
    node.SetPersistentSetting("qml_receive_address_types", "malformed");
    model->opening()->refreshMetadata();
    QTRY_COMPARE(model->defaultReceiveAddressType(), QString("bech32"));
    QVERIFY(model->setDefaultReceiveAddressType("legacy"));
    QTRY_COMPARE(model->defaultReceiveAddressType(), QString("legacy"));
}

void WalletQmlModelTests::receivingAddressIsStableUntilRotatedOrPaid()
{
    QSettings settings;
    settings.remove("receiveAddressTypes/fake-wallet");
    MockNode node;
    node.update_rw_setting_fn = [&node](const auto& key, const auto& value) {
        node.SetPersistentSetting(key, value);
    };
    auto [wallet, model] = MakePasswordWalletModel(&node);
    // A generated destination is owned and present in Core's public receive
    // address book. Model that lookup and return a fresh destination per call.
    wallet->get_address_result = true;
    wallet->get_new_destination_fn = [wallet](OutputType type, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_types.push_back(type); wallet->new_destination_labels.push_back(label);
        uint160 key; key.begin()[0] = static_cast<unsigned char>(wallet->get_new_destination_calls);
        if (type == OutputType::BECH32M) {
            uint256 taproot; taproot.begin()[0] = key.begin()[0]; return CTxDestination{WitnessV1Taproot{XOnlyPubKey{std::span{taproot}}}};
        }
        if (type == OutputType::BECH32) return CTxDestination{WitnessV0KeyHash{key}};
        if (type == OutputType::P2SH_SEGWIT) return CTxDestination{ScriptHash{key}};
        return CTxDestination{PKHash{key}};
    };
    QVERIFY(AwaitReceiveRequest(model->ensureReceivingAddress()));
    QCOMPARE(wallet->new_destination_types.back(), OutputType::BECH32M);
    const auto address = model->receivingAddress()->address();
    QVERIFY(!address.isEmpty());
    QVERIFY(AwaitReceiveRequest(model->ensureReceivingAddress()));
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QVERIFY(AwaitReceiveRequest(model->ensureReceivingAddress(true)));
    QCOMPARE(wallet->get_new_destination_calls, 2);
    QVERIFY(AwaitReceiveRequest(model->ensureReceivingAddress(false, "bech32")));
    QCOMPARE(wallet->new_destination_types.back(), OutputType::BECH32);
    QTRY_COMPARE(model->defaultReceiveAddressType(), QString("bech32"));

    CMutableTransaction incoming;
    incoming.vout.emplace_back(1, GetScriptForDestination(model->receivingAddress()->destination()));
    const auto payment = MakeIncomingPayment(incoming, {true});
    wallet->setWalletTxsFn([payment] { return std::set<interfaces::WalletTx>{payment}; });
    model->currentPaymentRequest()->setLabel("Too late");
    QVERIFY(!AwaitReceiveRequest(model->commitReceivingPaymentRequest()));
    QVERIFY(model->receivingAddress()->paymentReceived());
    QCOMPARE(model->receiveRequests()->count(), 0);
    QVERIFY(AwaitReceiveRequest(model->ensureReceivingAddress()));
    QCOMPARE(wallet->get_new_destination_calls, 4);
    QCOMPARE(wallet->set_address_receive_request_calls, 0);
    settings.remove("receiveAddressTypes/fake-wallet");
}

void WalletQmlModelTests::receivingAddressResumeOwnsOriginalIntent_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("retry-then-resume") << 0;
    QTest::newRow("wrong-passphrase-then-resume") << 1;
    QTest::newRow("duplicate-resume") << 2;
    QTest::newRow("superseded-intent") << 3;
    QTest::newRow("edited-view") << 4;
    QTest::newRow("foreign-opening") << 5;
    QTest::newRow("create-question-is-not-resumable") << 6;
    QTest::newRow("closed-opening") << 7;
}

void WalletQmlModelTests::receivingAddressResumeOwnsOriginalIntent()
{
    QFETCH(int, scenario);
    QSettings settings;
    settings.remove("receiveAddressTypes/fake-wallet");
    const auto remove_reservation = qScopeGuard([&] { settings.remove("receiveAddressTypes/fake-wallet"); });
    ManualExecutionEnvironment environment;
    const auto make_locked_wallet = [] {
        auto wallet = std::make_shared<FakePasswordWallet>();
        wallet->encrypted = true;
        wallet->locked = true;
        wallet->get_new_destination_fn = [target = wallet.get()](OutputType, const std::string&) -> util::Result<CTxDestination> {
            ++target->get_new_destination_calls;
            if (target->locked) return util::Error{Untranslated("keypool empty")};
            return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
        };
        wallet->unlock_fn = [target = wallet.get()](const SecureString& passphrase) -> util::Expected<void, wallet::WalletError> {
            ++target->unlock_calls;
            target->unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
            if (passphrase != SecureString{"secret"}) {
                return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("")}};
            }
            target->locked = false;
            return {};
        };
        return wallet;
    };
    auto wallet = make_locked_wallet();
    ManualPsbtModel fixture{environment, wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }};
    auto& model = fixture.model;

    std::unique_ptr<ReceiveRequestOperation> initial{
        scenario == 6 ? model->commitPaymentRequest() : model->ensureReceivingAddress()};
    QVERIFY(initial);
    QVERIFY(initial->pending());
    QVERIFY(initial->addressIntent());
    const auto original_token = initial->addressToken();
    QVERIFY(!original_token.isEmpty());
    const auto original_opening = model->opening()->identity();
    QSignalSpy initial_changed{initial.get(), &ReceiveRequestOperation::changed};

    // Run the actual public-wallet work on the manual executor while holding
    // its GUI notification. The operation stays pending until this handoff.
    environment.drain();
    QCOMPARE(initial_changed.count(), 0);
    QVERIFY(initial->pending());
    QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    QVERIFY(!initial->pending());
    QCOMPARE(initial_changed.count(), 1);
    QCOMPARE(initial->error(), ReceiveRequestOperation::Error::NeedsUnlock);
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QCOMPARE(wallet->unlock_calls, 0);
    if (scenario == 6) {
        QVERIFY(model->currentPaymentRequest()->needsUnlock());
        QVERIFY(initial->addressIntent()->create);
        QVERIFY(!model->resumeReceivingAddress(initial.get(), QStringLiteral("secret")));
        QCOMPARE(wallet->get_new_destination_calls, 1);
        fixture.close();
        QVERIFY(environment.quiescent());
        return;
    }

    QVERIFY(!initial->addressIntent()->create);
    QCOMPARE(initial->addressIntent()->opening, original_opening);
    QCOMPARE(initial->viewRevision(), model->receivingAddress()->revision());
    QVERIFY(model->receivingAddress()->needsUnlock());

    if (scenario == 0) {
        std::unique_ptr<ReceiveRequestOperation> retry{model->retryReceivingAddress(initial.get())};
        QVERIFY(retry);
        QCOMPARE(retry->addressToken(), original_token);
        QSignalSpy retry_changed{retry.get(), &ReceiveRequestOperation::changed};
        environment.drain();
        QCOMPARE(retry_changed.count(), 0);
        QVERIFY(retry->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!retry->pending());
        QCOMPARE(retry_changed.count(), 1);
        QCOMPARE(retry->error(), ReceiveRequestOperation::Error::NeedsUnlock);
        QCOMPARE(wallet->get_new_destination_calls, 2);
        std::unique_ptr<ReceiveRequestOperation> resumed{model->resumeReceivingAddress(retry.get(), QStringLiteral("secret"))};
        QVERIFY(resumed);
        QCOMPARE(resumed->addressToken(), original_token);
        QSignalSpy resumed_changed{resumed.get(), &ReceiveRequestOperation::changed};
        environment.drain();
        QCOMPARE(resumed_changed.count(), 0);
        QVERIFY(resumed->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!resumed->pending());
        QCOMPARE(resumed_changed.count(), 1);
        QCOMPARE(resumed->error(), ReceiveRequestOperation::Error::None);
        QVERIFY(resumed->addressReady());
        QVERIFY(model->property("receivingAddressReady").toBool());
        QVERIFY(!model->receivingAddress()->needsUnlock());
        QVERIFY(!model->retryReceivingAddress(initial.get()));
        QVERIFY(model->property("receivingAddressReady").toBool());
        QCOMPARE(wallet->get_new_destination_calls, 3);
        QCOMPARE(wallet->unlock_calls, 1);
        QCOMPARE(wallet->lock_calls, 1);
        QCOMPARE(model->opening()->identity(), original_opening);
    } else if (scenario == 1) {
        std::unique_ptr<ReceiveRequestOperation> wrong{model->resumeReceivingAddress(initial.get(), QStringLiteral("wrong"))};
        QVERIFY(wrong);
        QCOMPARE(wrong->addressToken(), original_token);
        QSignalSpy wrong_changed{wrong.get(), &ReceiveRequestOperation::changed};
        environment.drain();
        QCOMPARE(wrong_changed.count(), 0);
        QVERIFY(wrong->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!wrong->pending());
        QCOMPARE(wrong_changed.count(), 1);
        QCOMPARE(wrong->error(), ReceiveRequestOperation::Error::IncorrectPassphrase);
        QVERIFY(model->receivingAddress()->needsUnlock());
        QVERIFY(!model->receivingAddress()->unlockError().isEmpty());
        QCOMPARE(wallet->get_new_destination_calls, 1);
        QCOMPARE(wallet->unlock_calls, 1);
        QCOMPARE(wallet->lock_calls, 0);

        std::unique_ptr<ReceiveRequestOperation> resumed{model->resumeReceivingAddress(wrong.get(), QStringLiteral("secret"))};
        QVERIFY(resumed);
        QCOMPARE(resumed->addressToken(), original_token);
        QSignalSpy resumed_changed{resumed.get(), &ReceiveRequestOperation::changed};
        environment.drain();
        QCOMPARE(resumed_changed.count(), 0);
        QVERIFY(resumed->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!resumed->pending());
        QCOMPARE(resumed_changed.count(), 1);
        QCOMPARE(resumed->error(), ReceiveRequestOperation::Error::None);
        QVERIFY(resumed->addressReady());
        QVERIFY(!model->receivingAddress()->needsUnlock());
        QVERIFY(model->receivingAddress()->unlockError().isEmpty());
        QCOMPARE(wallet->get_new_destination_calls, 2);
        QCOMPARE(wallet->unlock_calls, 2);
        QCOMPARE(wallet->lock_calls, 1);
    } else if (scenario == 2) {
        std::unique_ptr<ReceiveRequestOperation> first{model->resumeReceivingAddress(initial.get(), QStringLiteral("secret"))};
        std::unique_ptr<ReceiveRequestOperation> second{model->resumeReceivingAddress(initial.get(), QStringLiteral("secret"))};
        QVERIFY(first);
        QVERIFY(!second);
        QVERIFY(!model->retryReceivingAddress(initial.get()));
        QVERIFY(model->property("receivingAddressPending").toBool());
        QCOMPARE(first->addressToken(), original_token);
        QSignalSpy first_changed{first.get(), &ReceiveRequestOperation::changed};
        environment.drain();
        QCOMPARE(first_changed.count(), 0);
        QVERIFY(first->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!first->pending());
        QCOMPARE(first_changed.count(), 1);
        QCOMPARE(first->error(), ReceiveRequestOperation::Error::None);
        QVERIFY(first->addressReady());
        QVERIFY(model->property("receivingAddressReady").toBool());
        QVERIFY(!model->receivingAddress()->address().isEmpty());
        QCOMPARE(wallet->get_new_destination_calls, 2);
        QCOMPARE(wallet->unlock_calls, 1);
        QCOMPARE(wallet->lock_calls, 1);
    } else if (scenario == 3) {
        std::unique_ptr<ReceiveRequestOperation> successor{model->ensureReceivingAddress(true)};
        QVERIFY(successor);
        QVERIFY(successor->addressToken() != original_token);
        std::unique_ptr<ReceiveRequestOperation> stale{model->resumeReceivingAddress(initial.get(), QStringLiteral("secret"))};
        QVERIFY(!stale);
        QVERIFY(!model->retryReceivingAddress(initial.get()));
        QVERIFY(model->property("receivingAddressPending").toBool());
        QSignalSpy successor_changed{successor.get(), &ReceiveRequestOperation::changed};
        environment.drain();
        QCOMPARE(successor_changed.count(), 0);
        QVERIFY(successor->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(successor_changed.count(), 1);
        QCOMPARE(successor->error(), ReceiveRequestOperation::Error::NeedsUnlock);
        QCOMPARE(wallet->get_new_destination_calls, 2);
        QCOMPARE(wallet->unlock_calls, 0);
        QVERIFY(!model->property("receivingAddressReady").toBool());
        QVERIFY(model->receivingAddress()->needsUnlock());
    } else if (scenario == 4) {
        model->receivingAddress()->setNoteSelf(QStringLiteral("edited after password prompt"));
        QVERIFY(model->receivingAddress()->revision() != initial->viewRevision());
        QVERIFY(!model->resumeReceivingAddress(initial.get(), QStringLiteral("secret")));
        QVERIFY(!model->retryReceivingAddress(initial.get()));
        QCOMPARE(wallet->get_new_destination_calls, 1);
        QCOMPARE(wallet->unlock_calls, 0);
    } else if (scenario == 5) {
        auto other_wallet = make_locked_wallet();
        ManualPsbtModel other_fixture{environment, other_wallet, [](const COutPoint&) -> std::optional<CTxOut> { return {}; }};
        std::unique_ptr<ReceiveRequestOperation> foreign{other_fixture.model->ensureReceivingAddress()};
        QVERIFY(foreign);
        environment.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QCOMPARE(foreign->error(), ReceiveRequestOperation::Error::NeedsUnlock);
        QVERIFY(!model->resumeReceivingAddress(foreign.get(), QStringLiteral("secret")));
        QCOMPARE(wallet->get_new_destination_calls, 1);
        QCOMPARE(other_wallet->get_new_destination_calls, 1);
        other_fixture.close();
    } else if (scenario == 7) {
        model->close();
        QVERIFY(!model->resumeReceivingAddress(initial.get(), QStringLiteral("secret")));
        QVERIFY(!model->retryReceivingAddress(initial.get()));
        QCOMPARE(wallet->get_new_destination_calls, 1);
        QCOMPARE(wallet->unlock_calls, 0);
    }
    fixture.close();
    QVERIFY2(environment.quiescent(), environment.trace().c_str());
}

void WalletQmlModelTests::receivingAddressCreationAndRequestPersistenceAreSeparate_data()
{
    QTest::addColumn<QString>("note");
    QTest::newRow("empty") << QString{};
    QTest::newRow("private-note") << QStringLiteral("Only a private note");
}

void WalletQmlModelTests::receivingAddressCreationAndRequestPersistenceAreSeparate()
{
    QFETCH(QString, note);
    QSettings settings;
    settings.remove("receiveAddressTypes/fake-wallet");
    auto [wallet, model] = MakePasswordWalletModel();
    QVERIFY(AwaitReceiveRequest(model->ensureReceivingAddress()));
    const auto address = model->receivingAddress()->address();
    QCOMPARE(model->receiveRequests()->count(), 0);
    // Reopening the wallet restores its unused receiving address without
    // consuming another key or saving a payment request.
    auto [restored_wallet, restored_model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) { original.get_address_result = true; });
    QVERIFY(AwaitReceiveRequest(restored_model->ensureReceivingAddress()));
    QCOMPARE(restored_model->receivingAddress()->address(), address);
    QCOMPARE(restored_wallet->get_new_destination_calls, 0);
    model->currentPaymentRequest()->setNoteSelf(note);
    wallet->set_address_receive_request_result = false;
    QVERIFY(!AwaitReceiveRequest(model->commitReceivingPaymentRequest()));
    QCOMPARE(model->receivingAddress()->address(), address);
    QVERIFY(settings.contains("receiveAddressTypes/fake-wallet/address"));
    wallet->set_address_receive_request_result = true;
    QVERIFY(AwaitReceiveRequest(model->commitReceivingPaymentRequest()));
    QCOMPARE(model->currentPaymentRequest()->address(), address);
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QCOMPARE(model->receiveRequests()->count(), 1);
    QCOMPARE(model->currentPaymentRequest()->amount()->satoshi(), CAmount{0});
    QCOMPARE(model->currentPaymentRequest()->label(), QString{});
    QCOMPARE(model->currentPaymentRequest()->message(), QString{});
    QCOMPARE(model->currentPaymentRequest()->noteSelf(), note);
    QVERIFY(!AwaitReceiveRequest(model->commitReceivingPaymentRequest()));
    QVERIFY(model->receivingAddress()->address().isEmpty());
    // R14's accepted canonical store keeps an empty legacy tombstone to stop
    // an older alias from resurrecting this consumed reservation.
    QCOMPARE(settings.value("receiveAddressTypes/fake-wallet/address").toString(), QString{});
    model->currentPaymentRequest()->clear();
    QVERIFY(AwaitReceiveRequest(model->ensureReceivingAddress()));
    QCOMPARE(wallet->get_new_destination_calls, 2);
    settings.remove("receiveAddressTypes/fake-wallet");
}

// Distinct synthetic fee weights keep the balance-edge fixtures independent of preset names.
static unsigned int TestFeeWeight(unsigned int target)
{
    return target == 2 ? 1 : target == 6 ? 2 : target == 10 ? 6 : 0;
}

void WalletQmlModelTests::feeTargetIndex_mapsStandardTargets()
{
    WalletQmlModel model;

    QCOMPARE(model.feeTargetIndex(2), 0);
    QCOMPARE(model.feeTargetIndex(6), 1);
    QCOMPARE(model.feeTargetIndex(10), 2);
    QCOMPARE(model.feeTargetIndex(42), 1);
}

void WalletQmlModelTests::customFeeRateUpdatesEstimatedTarget()
{
    const auto fixed = std::make_shared<std::atomic_bool>(false);
    const auto fixed_reads = std::make_shared<std::atomic_int>(0);
    auto [wallet, model] = MakeConfiguredWalletModel([fixed, fixed_reads](MockWallet& value) {
        value.get_minimum_fee_fn = [fixed, fixed_reads](const wallet::CCoinControl& control) {
            if (fixed->load()) { ++*fixed_reads; return CAmount{1'000}; }
            switch (control.m_confirm_target.value_or(0)) {
            case 2: return CAmount{10'000};
            case 3: return CAmount{7'000};
            case 4: return CAmount{5'000};
            case 6: return CAmount{3'000};
            case 10: return CAmount{1'000};
            case 25: return CAmount{500};
            case 50: return CAmount{200};
            default: return CAmount{0};
            }
        };
    });
    Q_UNUSED(wallet);
    QTRY_VERIFY(model->availableSendBalanceKnown());

    model->setCustomFeeRate("5.1");
    QTRY_COMPARE(model->feeTargetBlocks(), 4U);
    model->setCustomFeeRate("3");
    QTRY_COMPARE(model->feeTargetBlocks(), 6U);
    model->setCustomFeeRate("0.5");
    QTRY_COMPARE(model->feeTargetBlocks(), 25U);
    model->setCustomFeeRate("0.1");
    QTRY_COMPARE(model->feeTargetBlocks(), 50U);
    model->setCustomFeeRate("12");
    QTRY_COMPARE(model->feeTargetBlocks(), 2U);

    model->setCustomFeeTarget(3);
    QTRY_COMPARE(model->feeTargetBlocks(), 3U);
    QTRY_COMPARE(model->customFeeRate(), QStringLiteral("7.000"));
    QVERIFY(!model->customFeeTargetPending());

    model->setCustomFeeTarget(50);
    QTRY_COMPARE(model->feeTargetBlocks(), 50U);
    QTRY_COMPARE(model->customFeeRate(), QStringLiteral("0.200"));
    QVERIFY(!model->customFeeTargetPending());

    // Change the fake's Native policy without replacing a callable in use.
    // Observe that policy before testing inference from its cached projection.
    fixed->store(true);
    model->scheduleFeeEstimates();
    QTRY_VERIFY(fixed_reads->load() > 0 && model->availableSendBalanceKnown());
    model->setCustomFeeRate("2");
    QTRY_COMPARE(model->feeTargetBlocks(), 50U);
}

void WalletQmlModelTests::estimatedFeeForTarget_returnsEmptyWhenUnavailable()
{
    WalletQmlModel model;

    QCOMPARE(model.estimatedFeeForTarget(2), QString());
    QCOMPARE(model.estimatedFeeForTarget(6), QString());
    QCOMPARE(model.estimatedFee(), QString());
}

void WalletQmlModelTests::scheduleFeeEstimates_populatesFormattedEstimates()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    QSemaphore release_first_call;
    std::atomic<bool> first_call_started{false};
    std::atomic<bool> first_call_blocked{false};
    ThreadSafeCallLog<FeeEstimateCall> calls;
    const auto gui_thread = QThread::currentThread();
    std::atomic<bool> fee_calls_off_gui{true};

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        if (QThread::currentThread() == gui_thread) fee_calls_off_gui = false;
        calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        });
        change_pos = -1;
        fee = TestFeeWeight(coin_control.m_confirm_target.value_or(0)) * 100;
        if (!first_call_blocked.exchange(true)) {
            first_call_started = true;
            release_first_call.acquire();
        }
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_VERIFY_WITH_TIMEOUT(first_call_started.load(), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(6), QString());

    release_first_call.release();

    QTRY_VERIFY_WITH_TIMEOUT(calls.Size() == 3, FEE_ESTIMATE_TIMEOUT_MS);
    CompareFeeEstimateCalls(calls.Snapshot(), {
                                                  {2, false, false, std::nullopt, 1},
                                                  {6, false, false, std::nullopt, 1},
                                                  {10, false, false, std::nullopt, 1},
                                              });
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000100 BTC"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000200 BTC"));
    QCOMPARE(model->estimatedFeeForTarget(10), QStringLiteral("0.00000600 BTC"));
    QCOMPARE(model->estimatedFee(), QStringLiteral("0.00000200 BTC"));
    QVERIFY(fee_calls_off_gui);
}

void WalletQmlModelTests::scheduleFeeEstimates_fallsBackWhenNetworkFeeEstimatesUnavailable()
{
    // Retain the historical selector: the synthetic 3/2/1 fallback was issue080.
    // Actual Core fallback is covered independently by coreApiFeePreviews.
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    ThreadSafeCallLog<FeeEstimateCall> fallback_calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->ExpectNoCalls(wallet->calls.getRequiredFee);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) {
            return util::Error{Untranslated("fee estimation unavailable")};
        }

        fallback_calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate->GetFeePerK(),
            recipients.size(),
        });
        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending() && !model->fees()->errorText().isEmpty(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->fees()->errorText(), QStringLiteral("fee estimation unavailable"));
    QCOMPARE(fallback_calls.Size(), size_t{0});
    QCOMPARE(model->estimatedFeeForTarget(2), QString{});
    QCOMPARE(model->estimatedFeeForTarget(6), QString{});
    QCOMPARE(model->estimatedFeeForTarget(10), QString{});
    QCOMPARE(model->estimatedFeeSatoshi(), qint64{-1});
    QVERIFY(!model->fees()->estimateFresh());
}

void WalletQmlModelTests::scheduleFeeEstimates_usesStaticRegtestFeeOverride()
{
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    ThreadSafeCallLog<FeeEstimateCall> calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->ExpectAtLeast(wallet->calls.getRequiredFee, 3);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        });
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) return util::Error{Untranslated("missing regtest fee override")};

        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeForTarget(6), QStringLiteral("0.00000250 BTC"), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000250 BTC"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000250 BTC"));
    QCOMPARE(model->estimatedFeeForTarget(10), QStringLiteral("0.00000250 BTC"));
    CompareFeeEstimateCalls(calls.Snapshot(), {
                                                  {0, false, false, CAmount{wallet::DEFAULT_TRANSACTION_MINFEE}, 1},
                                                  {0, false, false, CAmount{wallet::DEFAULT_TRANSACTION_MINFEE}, 1},
                                                  {0, false, false, CAmount{wallet::DEFAULT_TRANSACTION_MINFEE}, 1},
                                              });
}

void WalletQmlModelTests::customFeeRateValid_data()
{
    QTest::addColumn<QString>("rate");
    QTest::addColumn<bool>("valid");
    QTest::newRow("empty") << QString{} << false;
    QTest::newRow("zero") << QStringLiteral("0.000") << false;
    QTest::newRow("negative") << QStringLiteral("-0.1") << false;
    QTest::newRow("below-minimum") << QStringLiteral("0.099") << false;
    QTest::newRow("minimum") << QStringLiteral("0.1") << true;
    QTest::newRow("minimum-padded") << QStringLiteral(" 0.100 ") << true;
    QTest::newRow("fractional") << QStringLiteral("0.123") << true;
    QTest::newRow("whole") << QStringLiteral("2") << true;
    QTest::newRow("excess-precision") << QStringLiteral("0.1001") << false;
    QTest::newRow("maximum-representable") << QStringLiteral("9223372036854775.807") << true;
    QTest::newRow("scaled-whole-overflow") << QStringLiteral("9223372036854776") << false;
    QTest::newRow("scaled-sum-overflow") << QStringLiteral("9223372036854775.808") << false;
    QTest::newRow("whole-conversion-overflow") << QStringLiteral("9223372036854775808") << false;
    QTest::newRow("bounded-transport") << QString(33, QLatin1Char{'9'}) << false;
}

void WalletQmlModelTests::customFeeRateValid()
{
    QFETCH(QString, rate);
    QFETCH(bool, valid);
    auto [wallet, model] = MakeWalletModel();
    model->setCustomFeeRate(rate);
    QTRY_VERIFY(!model->fees()->policyPending());
    QCOMPARE(model->customFeeRateValid(), valid);
}

void WalletQmlModelTests::customFeeRateRespectsRelayMinimum_data()
{
    QTest::addColumn<qlonglong>("relay_minimum");
    QTest::addColumn<QString>("rate");
    QTest::addColumn<bool>("valid");
    QTest::addColumn<qlonglong>("expected_custom_per_kvb");
    QTest::newRow("below-wallet-minimum") << qlonglong{100} << QStringLiteral("0.1") << true << qlonglong{100};
    QTest::newRow("below-relay-minimum") << qlonglong{1000} << QStringLiteral("0.1") << false << qlonglong{100};
    QTest::newRow("just-below-relay-minimum") << qlonglong{1000} << QStringLiteral("0.999") << false << qlonglong{999};
    QTest::newRow("at-relay-minimum") << qlonglong{1000} << QStringLiteral("1") << true << qlonglong{1000};
    QTest::newRow("above-relay-minimum") << qlonglong{1000} << QStringLiteral("1.001") << true << qlonglong{1001};
}

void WalletQmlModelTests::customFeeRateRespectsRelayMinimum()
{
    QFETCH(qlonglong, relay_minimum);
    QFETCH(QString, rate);
    QFETCH(bool, valid);
    QFETCH(qlonglong, expected_custom_per_kvb);
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    node::NodeContext node_context;
    bilingual_str error;
    CTxMemPool::Options options;
    options.min_relay_feerate = CFeeRate{relay_minimum};
    node_context.mempool = std::make_unique<CTxMemPool>(options, error);
    auto chain = interfaces::MakeChain(node_context);
    wallet::CWallet raw_wallet{chain.get(), "relay-fee-test", nullptr};
    const auto calls = std::make_shared<ThreadSafeCallLog<FeeEstimateCall>>();
    const auto policy_thread = std::make_shared<std::atomic<QThread*>>(nullptr);
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    FeeReadServices services;
    services.timer = clock;
    auto [wallet, model] = MakeConfiguredWalletModel([calls](MockWallet& value) {
        // Automatic target quotes share this fixture's 1000 sat/kvB wallet
        // floor. Returning 0 for every target except the selected regtest rate
        // invents successive observation policies while the timer is running.
        value.get_minimum_fee_fn = [](const wallet::CCoinControl&) { return CAmount{1000}; };
        value.create_transaction_fn = [calls](const std::vector<wallet::CRecipient>& recipients,
                                              const wallet::CCoinControl& control, bool sign,
                                              int& change_pos, CAmount& fee) -> util::Result<CTransactionRef> {
            calls->Append({control.m_confirm_target.value_or(0), sign, false,
                control.m_feerate ? std::optional<CAmount>{control.m_feerate->GetFeePerK()} : std::nullopt,
                recipients.size()});
            change_pos = -1;
            fee = control.m_feerate ? control.m_feerate->GetFee(250) : 250;
            return MakeTransactionRef(CMutableTransaction{});
        };
    }, nullptr, &raw_wallet, std::move(services), false, {},
        walletopening::FeePolicy{[chain = chain.get(), policy_thread] {
            policy_thread->store(QThread::currentThread());
            return chain->relayMinFee().GetFeePerK();
        }});
    Q_UNUSED(wallet);
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);
    QTRY_VERIFY(model->capabilitiesKnown() && model->balanceKnown() &&
        model->availableSendBalanceKnown() && !model->coinSelectionPending() &&
        !model->fees()->policyPending());
    QCOMPARE(model->minimumCustomFeeRate(), static_cast<double>(relay_minimum) / 1000);
    QVERIFY(policy_thread->load());
    QVERIFY(policy_thread->load() != QThread::currentThread());

    model->setCustomFeeEnabled(true);
    model->setCustomFeeRate(rate);
    QTRY_VERIFY(!model->fees()->policyPending());
    QCOMPARE(model->customFeeRateValid(), valid);
    model->scheduleFeeEstimates();
    // Scheduling also starts a Native send-facts observation. Settle that
    // original observation and its policy before advancing the manual clock;
    // a late dependency publication must not supersede the quote under test.
    const auto native_barrier = std::make_shared<std::atomic_bool>(false);
    const auto barrier = model->opening()->read([](interfaces::Wallet&) { return 0; },
        [native_barrier](int) { native_barrier->store(true); });
    QVERIFY(barrier);
    QTRY_VERIFY(native_barrier->load() && model->availableSendBalanceKnown() &&
        !model->coinSelectionPending() && !model->fees()->policyPending());
    const auto observation_revision = model->fees()->observationRevision();
    const auto policy_revision = model->fees()->policyRevision();
    QCOMPARE(calls->Size(), size_t{0});
    clock->advance(std::chrono::milliseconds{249});
    QCOMPARE(calls->Size(), size_t{0});
    clock->advance(std::chrono::milliseconds{1});
    const bool estimate_complete = QTest::qWaitFor([&] {
        return calls->Size() == (valid ? 4U : 0U) && !model->feeEstimatePending() && !model->fees()->policyPending();
    }, FEE_ESTIMATE_TIMEOUT_MS);
    QStringList effects;
    for (const auto& call : calls->Snapshot()) {
        effects.push_back(QStringLiteral("target=%1 sign=%2 rate=%3 recipients=%4")
            .arg(call.target).arg(call.sign).arg(call.fee_rate.value_or(-1)).arg(qulonglong(call.recipient_count)));
    }
    const QString observation = QStringLiteral("effects=%1 pending=%2 policyPending=%3 observation=%4 policy=%5 rate=%6 error=%7 calls=[%8]")
        .arg(qulonglong(calls->Size())).arg(model->feeEstimatePending()).arg(model->fees()->policyPending())
        .arg(model->fees()->observationRevision()).arg(model->fees()->policyRevision())
        .arg(model->customFeeRate()).arg(model->fees()->errorText()).arg(effects.join(QStringLiteral("; ")));
    QVERIFY2(estimate_complete, qPrintable(observation));
    QCOMPARE(model->fees()->observationRevision(), observation_revision);
    QCOMPARE(model->fees()->policyRevision(), policy_revision);
    QCOMPARE(!model->estimatedFee().isEmpty(), valid);
    if (valid) {
        const auto previews = calls->Snapshot();
        QCOMPARE(previews.size(), size_t{4});
        for (size_t i{0}; i < 3; ++i) {
            QVERIFY(previews[i].fee_rate);
            QCOMPARE(*previews[i].fee_rate, CAmount{1000});
        }
        QVERIFY(previews.back().fee_rate);
        QCOMPARE(*previews.back().fee_rate, static_cast<CAmount>(expected_custom_per_kvb));
    }
    QCOMPARE(AwaitPreparation(*model), valid);
    QCOMPARE(calls->Size(), valid ? 5U : 0U);
    if (valid) {
        const auto prepared = calls->Snapshot();
        QVERIFY(prepared.back().fee_rate);
        QCOMPARE(*prepared.back().fee_rate, static_cast<CAmount>(expected_custom_per_kvb));
    }
    for (const auto& call : calls->Snapshot()) {
        if (call.fee_rate) QVERIFY(*call.fee_rate >= relay_minimum);
    }
}

void WalletQmlModelTests::scheduleFeeEstimates_usesCustomFeeRateWhenEnabled_data()
{
    QTest::addColumn<QString>("rate");
    QTest::addColumn<qlonglong>("per_kvb");
    QTest::addColumn<QString>("formatted_fee");
    QTest::newRow("whole") << QStringLiteral("2") << qlonglong{2000} << QStringLiteral("0.00000500 BTC");
    QTest::newRow("minimum") << QStringLiteral("0.1") << qlonglong{100} << QStringLiteral("0.00000025 BTC");
    QTest::newRow("fractional") << QStringLiteral("0.123") << qlonglong{123} << QStringLiteral("0.00000031 BTC");
}

void WalletQmlModelTests::scheduleFeeEstimates_usesCustomFeeRateWhenEnabled()
{
    QFETCH(QString, rate);
    QFETCH(qlonglong, per_kvb);
    QFETCH(QString, formatted_fee);
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    ThreadSafeCallLog<FeeEstimateCall> calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        });
        if (coin_control.m_feerate && !coin_control.fOverrideFeeRate) {
            return util::Error{Untranslated("custom fee must override the wallet minimum")};
        }
        change_pos = -1;
        fee = coin_control.m_feerate.has_value()
            ? coin_control.m_feerate->GetFee(250)
            : TestFeeWeight(coin_control.m_confirm_target.value_or(0)) * 100;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->setCustomFeeEnabled(true);
    model->setCustomFeeRate(rate);
    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFee(), formatted_fee, FEE_ESTIMATE_TIMEOUT_MS);
    const auto recorded_calls{calls.Snapshot()};
    QCOMPARE(recorded_calls.size(), 4U);
    for (const FeeEstimateCall& call : recorded_calls) {
        QVERIFY(!call.sign);
        QVERIFY(!call.selected_inputs);
        QCOMPARE(call.recipient_count, 1U);
        if (call.target == 0) {
            QVERIFY(call.fee_rate.has_value());
            QCOMPARE(*call.fee_rate, CAmount{per_kvb});
        } else {
            QVERIFY(!call.fee_rate.has_value());
        }
    }
    const auto called_target = [&](unsigned int target) {
        return std::ranges::any_of(recorded_calls, [&](const FeeEstimateCall& call) { return call.target == target; });
    };
    QVERIFY(called_target(0));
    QVERIFY(called_target(2));
    QVERIFY(called_target(6));
    QVERIFY(called_target(10));
}

void WalletQmlModelTests::scheduleFeeEstimates_neverUsesSubtractFeeFallback()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);
    std::atomic<bool> saw_sffo{false};
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl&, bool, int&, CAmount&) -> util::Result<CTransactionRef> {
        for (const auto& recipient : recipients) if (recipient.fSubtractFeeFromAmount) saw_sffo = true;
        return util::Error{Untranslated("amount plus fee exceeds balance")};
    };
    model->scheduleFeeEstimates();
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeSatoshi(), -1);
    QVERIFY(!saw_sffo);
    QCOMPARE(model->sendRecipientList()->currentRecipient()->cAmount(), CAmount{50'000});
}

void WalletQmlModelTests::sendAmountExhaustsBalance_requiresFeeBuffer()
{
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    auto [wallet, model] = MakeConfiguredPasswordWalletModel(
        [](FakePasswordWallet& value) { value.balance = 50'000; }, nullptr, {}, {}, clock);
    SetPasswordRecipient(*model, 50'000);
    const auto settled = [&](qint64 total) {
        return QTest::qWaitFor([&] {
            return !model->sendRecipientList()->validationPending() &&
                !model->coinSelectionPending() && model->availableSendBalanceKnown() &&
                model->sendTotalSatoshi() == total;
        }, 5000);
    };

    QVERIFY(settled(50'000));
    QCOMPARE(model->availableSendBalanceSatoshi(), qint64{50'000});
    QVERIFY(model->sendAmountExhaustsBalance());

    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);
    QSignalSpy spy{model.get(), &WalletQmlModel::sendAmountExhaustsBalanceChanged};
    recipient->amount()->setSatoshi(49'999);
    QVERIFY(settled(49'999));
    QVERIFY(!model->sendAmountExhaustsBalance());
    QVERIFY(spy.count() > 0);

    recipient->amount()->setSatoshi(50'000);
    QVERIFY(settled(50'000));
    QVERIFY(model->sendAmountExhaustsBalance());
    recipient->amount()->setSatoshi(49'000);
    QVERIFY(settled(49'000));
    QVERIFY(!model->sendAmountExhaustsBalance());
    model->sendRecipientList()->add();
    auto* second_recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(second_recipient != nullptr);
    second_recipient->address()->setAddress(VALID_MAINNET_P2SH_ADDRESS, 0);
    second_recipient->amount()->setSatoshi(1'000);
    QVERIFY(settled(50'000));
    QVERIFY(model->sendAmountExhaustsBalance());
    model->sendRecipientList()->remove();
    QVERIFY(settled(49'000));
    QVERIFY(!model->sendAmountExhaustsBalance());
    // This contract is the fixed-amount fallback before any fee deadline.
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::sendAmountExhaustsBalance_usesCoinControlAvailableBalance()
{
    struct Probe {
        std::atomic_bool created{false}, selected_valid{true}, unselected_valid{true};
        std::atomic_bool manual_empty_seen{false};
    };
    const auto probe = std::make_shared<Probe>();
    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    FeeReadServices services;
    services.timer = clock;
    auto [wallet, model] = MakeConfiguredWalletModel([probe, selected_outpoint](MockWallet& value) {
        value.list_coins_fn = [selected_outpoint] {
            const auto destination = DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
            interfaces::Wallet::CoinsList coins;
            coins[destination].emplace_back(selected_outpoint,
                interfaces::WalletTxOut{CTxOut{20'000, GetScriptForDestination(destination)}, 0, 6, false});
            return coins;
        };
        value.get_available_balance_fn = [probe, selected_outpoint](const wallet::CCoinControl& control) {
            if (control.HasSelected()) {
                if (!control.IsSelected(selected_outpoint) || control.m_allow_other_inputs) probe->selected_valid = false;
                return CAmount{20'000};
            }
            if (!control.m_allow_other_inputs) {
                probe->manual_empty_seen = true;
                return CAmount{0};
            }
            probe->unselected_valid = true;
            return CAmount{100'000};
        };
        value.create_transaction_fn = [probe](const std::vector<wallet::CRecipient>&,
                                              const wallet::CCoinControl&, bool,
                                              int& change_pos, CAmount& fee) -> util::Result<CTransactionRef> {
            probe->created = true;
            change_pos = -1;
            fee = 200;
            return MakeTransactionRef(CMutableTransaction{});
        };
    }, nullptr, nullptr, std::move(services));
    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getBalance);
    SetValidRecipient(*model);
    const auto settled = [&] {
        return QTest::qWaitFor([&] {
            return !model->coinSelectionPending() && !model->sendRecipientList()->validationPending() &&
                model->availableSendBalanceKnown();
        }, 5000);
    };
    QVERIFY(settled());
    QVERIFY(!model->sendAmountExhaustsBalance());

    QSignalSpy spy{model.get(), &WalletQmlModel::sendAmountExhaustsBalanceChanged};
    probe->unselected_valid = false;
    QVERIFY(model->coinsListModel()->setManualInputSelection(true));
    QVERIFY(settled());
    model->selectCoin(selected_outpoint);
    QVERIFY(settled());
    QVERIFY(spy.count() > 0);
    QVERIFY(model->sendAmountExhaustsBalance());
    QVERIFY(!AwaitPreparation(*model));
    QVERIFY(!probe->created.load());
    QCOMPARE(model->transactionError(), QStringLiteral("Selected inputs do not cover the amount plus fee"));

    const auto selected_notices = spy.count();
    model->unselectCoin(selected_outpoint);
    QVERIFY(settled());
    QVERIFY(spy.count() > selected_notices);
    QVERIFY(model->coinsListModel()->manualInputSelection());
    QVERIFY(model->coinsListModel()->setManualInputSelection(false));
    QVERIFY(settled());
    QVERIFY(!model->sendAmountExhaustsBalance());
    QVERIFY(AwaitPreparation(*model));
    QVERIFY(probe->created.load());
    QCOMPARE(wallet->calls.createTransaction.load(), 1);

    const auto unselected_notices = spy.count();
    QVERIFY(model->coinsListModel()->setManualInputSelection(true));
    QVERIFY(settled());
    model->selectCoin(selected_outpoint);
    QVERIFY(settled());
    QVERIFY(spy.count() > unselected_notices);
    QVERIFY(model->sendAmountExhaustsBalance());
    const auto reselected_notices = spy.count();
    model->clearSelectedCoins();
    QVERIFY(settled());
    QVERIFY(!model->coinsListModel()->manualInputSelection());
    QVERIFY(spy.count() > reselected_notices);
    QVERIFY(!model->sendAmountExhaustsBalance());

    // A no-op clear admits no new facts request or presentation change.
    const auto cleared_notices = spy.count();
    const auto available_reads = wallet->calls.getAvailableBalance.load();
    model->clearSelectedCoins();
    QCOMPARE(spy.count(), cleared_notices);
    QCOMPARE(wallet->calls.getAvailableBalance.load(), available_reads);
    QVERIFY(probe->selected_valid.load());
    QVERIFY(probe->unselected_valid.load());
    QVERIFY(probe->manual_empty_seen.load());
}

void WalletQmlModelTests::clearSelectedCoinsKeepsPendingIntent()
{
    ManualRoutedExecution execution;
    const COutPoint outpoint{Txid::FromUint256(uint256::ONE), 1};
    FeeReadServices services;
    services.timer = std::make_shared<ManualExecutionEnvironment>();
    auto [wallet, model] = MakeConfiguredWalletModel([outpoint](MockWallet& value) {
        value.list_coins_fn = [outpoint] {
            const auto destination = DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
            interfaces::Wallet::CoinsList coins;
            coins[destination].emplace_back(outpoint,
                interfaces::WalletTxOut{CTxOut{20'000, GetScriptForDestination(destination)}, 0, 6, false});
            return coins;
        };
    }, nullptr, nullptr, std::move(services), false, {}, {}, execution.scheduler);
    ManualWalletDrain drain{execution, model.get()};
    const auto settled = [&] {
        return QTest::qWaitFor([&] {
            execution.drain();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            return !model->coinSelectionPending() && !model->coinsListModel()->loading() &&
                model->availableSendBalanceKnown() && execution.transportIdle();
        }, 5000);
    };
    QVERIFY(settled());
    QVERIFY(!model->isSelectedCoin(outpoint));
    QSignalSpy changes{model.get(), &WalletQmlModel::coinSelectionChanged};
    model->selectCoin(outpoint);
    QVERIFY(model->coinSelectionPending());
    QVERIFY(!model->isSelectedCoin(outpoint)); // The original Domain turn is held.
    model->clearSelectedCoins();
    QVERIFY(model->coinSelectionPending());
    QCOMPARE(changes.count(), 2); // The explicit clear follows the held selection.
    QVERIFY(settled());
    QVERIFY(!model->isSelectedCoin(outpoint));
    QVERIFY(!model->coinsListModel()->manualInputSelection());
    const auto notices = changes.count();
    const auto reads = wallet->calls.getAvailableBalance.load();
    model->clearSelectedCoins();
    QCOMPARE(changes.count(), notices);
    QCOMPARE(wallet->calls.getAvailableBalance.load(), reads);
    QVERIFY(!model->coinSelectionPending());
    QVERIFY(drain.finish());
    QVERIFY(execution.quiescent());
}

void WalletQmlModelTests::scheduleFeeEstimates_usesDummyPreviewChangeDestination()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    std::atomic<bool> saw_missing_change{false};
    std::atomic<bool> saw_wrong_change_type{false};

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>&,
                                        const wallet::CCoinControl& coin_control,
                                        bool,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        if (std::get_if<CNoDestination>(&coin_control.destChange)) {
            saw_missing_change = true;
        }
        if (!std::get_if<WitnessV0KeyHash>(&coin_control.destChange)) {
            saw_wrong_change_type = true;
        }
        change_pos = -1;
        fee = TestFeeWeight(coin_control.m_confirm_target.value_or(0)) * 100;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(wallet->calls.createTransaction.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_missing_change.load());
    QVERIFY(!saw_wrong_change_type.load());
}

void WalletQmlModelTests::prepareTransaction_usesStaticRegtestFeeOverride()
{
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    std::atomic_bool requirement_observed{false},invalid_requirement_read{false};
    auto* const gui=QThread::currentThread();
    auto [wallet, model] = MakeConfiguredWalletModel([&](MockWallet& value){
        value.get_required_fee_fn=[&](unsigned int bytes){
            requirement_observed=true;
            if(bytes!=1000||QThread::currentThread()==gui)invalid_requirement_read=true;
            return CAmount{1000};
        };
    });
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    std::optional<FeeEstimateCall> call;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        call = FeeEstimateCall{
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        };
        change_pos = -1;

        if (!coin_control.m_feerate.has_value()) return util::Error{Untranslated("missing regtest fee override")};

        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    QVERIFY(AwaitPreparation(*model));
    QVERIFY(model->currentTransaction() != nullptr);
    QCOMPARE(model->currentTransaction()->feeAmount()->satoshi(), CAmount{250});
    QVERIFY(requirement_observed.load());QVERIFY(!invalid_requirement_read.load());
    QVERIFY(call.has_value());
    CompareFeeEstimateCalls({*call}, {
                                         {0, true, false, CAmount{wallet::DEFAULT_TRANSACTION_MINFEE}, 1},
                                     });
}

void WalletQmlModelTests::prepareTransaction_usesCustomFeeRateWithoutRegtestOverride_data()
{
    scheduleFeeEstimates_usesCustomFeeRateWhenEnabled_data();
}

void WalletQmlModelTests::prepareTransaction_usesCustomFeeRateWithoutRegtestOverride()
{
    QFETCH(QString, rate);
    QFETCH(qlonglong, per_kvb);
    ChainSelectionGuard chain_guard{ChainType::REGTEST};
    std::atomic_bool requirement_observed{false},invalid_requirement_read{false};
    auto* const gui=QThread::currentThread();
    auto [wallet, model] = MakeConfiguredWalletModel([&](MockWallet& value){
        value.get_required_fee_fn=[&](unsigned int bytes){
            requirement_observed=true;
            if(bytes!=1000||QThread::currentThread()==gui)invalid_requirement_read=true;
            return CAmount{1000};
        };
    });
    SetValidRecipient(*model, VALID_REGTEST_ADDRESS);

    std::optional<FeeEstimateCall> call;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        call = FeeEstimateCall{
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() || !coin_control.ListSelected().empty(),
            coin_control.m_feerate ? std::optional<CAmount>{coin_control.m_feerate->GetFeePerK()} : std::nullopt,
            recipients.size(),
        };
        change_pos = -1;

        if (!coin_control.m_feerate.has_value() || !coin_control.fOverrideFeeRate) {
            return util::Error{Untranslated("missing custom fee override")};
        }

        fee = coin_control.m_feerate->GetFee(250);
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->setCustomFeeEnabled(true);
    model->setCustomFeeRate(rate);
    // The explicit-rate test starts after its original asynchronous policy
    // and fee observations, rather than racing a changing initial target.
    QTRY_VERIFY_WITH_TIMEOUT(model->customFeeRateValid()&&!model->customFeeTargetPending()&&!model->feeEstimatePending(),FEE_ESTIMATE_TIMEOUT_MS);

    QVERIFY(AwaitPreparation(*model));
    QVERIFY(model->currentTransaction() != nullptr);
    QCOMPARE(model->currentTransaction()->feeAmount()->satoshi(), CFeeRate{per_kvb}.GetFee(250));
    QVERIFY(requirement_observed.load());QVERIFY(!invalid_requirement_read.load());
    QVERIFY(call.has_value());
    CompareFeeEstimateCalls({*call}, {
                                         {0, true, false, CAmount{per_kvb}, 1},
                                     });
}

void WalletQmlModelTests::prepareTransaction_neverSubtractsFeeFromRecipient()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model, VALID_MAINNET_ADDRESS);

    bool saw_subtract_fee_from_amount{false};
    bool saw_wrong_recipient_count{false};
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl&,
                                        bool,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        if (recipients.size() != 1U) {
            saw_wrong_recipient_count = true;
            return util::Error{Untranslated("unexpected recipient count")};
        }
        saw_subtract_fee_from_amount = recipients.at(0).fSubtractFeeFromAmount;
        change_pos = -1;
        fee = 200;

        CMutableTransaction tx;
        tx.vout.emplace_back(/*nValue=*/50'000, CScript{});
        return util::Result<CTransactionRef>{MakeTransactionRef(std::move(tx))};
    };

    QVERIFY(AwaitPreparation(*model));
    QVERIFY(!saw_wrong_recipient_count);
    QVERIFY(!saw_subtract_fee_from_amount);
    QVERIFY(model->currentTransaction() != nullptr);
    QCOMPARE(model->currentTransaction()->amountAmount()->satoshi(), CAmount{50'000});
    QCOMPARE(model->currentTransaction()->feeAmount()->satoshi(), CAmount{200});
    QCOMPARE(model->currentTransaction()->totalAmount()->satoshi(), CAmount{50'200});
    QCOMPARE(model->currentTransaction()->amount(), QStringLiteral("0.00050000 BTC"));
    QCOMPARE(model->currentTransaction()->fee(), QStringLiteral("0.00000200 BTC"));
    QCOMPARE(model->currentTransaction()->total(), QStringLiteral("0.00050200 BTC"));
    QCOMPARE(model->currentTransaction()->getTotalTransactionAmount(), CAmount{50'200});
}

void WalletQmlModelTests::preparedReview_excludesChangeOutput()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model, VALID_MAINNET_ADDRESS);
    model->sendRecipientList()->currentRecipient()->amount()->setSatoshi(49'800);
    std::atomic_bool native_on_gui{false};
    int preparations{0};
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>& recipients,
                                        const wallet::CCoinControl&, bool sign,
                                        int& change_pos, CAmount& fee) -> util::Result<CTransactionRef> {
        if (QThread::currentThread() == qApp->thread()) native_on_gui.store(true);
        if (sign) ++preparations;
        if (recipients.size() != 1 || recipients[0].nAmount != 49'800) return util::Error{Untranslated("unexpected prepared recipient")};
        change_pos = 1; fee = 200;
        CMutableTransaction tx;
        tx.vout.emplace_back(49'800, GetScriptForDestination(recipients[0].dest));
        tx.vout.emplace_back(1'000, CScript{}); // Native change is never part of the review's recipient amount.
        return MakeTransactionRef(std::move(tx));
    };
    QVERIFY(AwaitPreparation(*model));
    auto* transaction = model->currentTransaction(); QVERIFY(transaction);
    QCOMPARE(preparations, 1); QVERIFY(!native_on_gui);
    QCOMPARE(transaction->getWtx()->vout.size(), size_t{2});
    QCOMPARE(transaction->getWtx()->vout[1].nValue, CAmount{1'000});
    QCOMPARE(transaction->amountAmount()->satoshi(), CAmount{49'800});
    QCOMPARE(transaction->feeAmount()->satoshi(), CAmount{200});
    QCOMPARE(transaction->totalAmount()->satoshi(), CAmount{50'000});
    QCOMPARE(transaction->amount(), QStringLiteral("0.00049800 BTC"));
    QCOMPARE(transaction->fee(), QStringLiteral("0.00000200 BTC"));
    QCOMPARE(transaction->total(), QStringLiteral("0.00050000 BTC"));
    QCOMPARE(transaction->getTotalTransactionAmount(), CAmount{50'000});
    QCOMPARE(transaction->reviewedRecipients().size(), 1);
    QCOMPARE(transaction->reviewedRecipients().first().toMap().value("amount").toString(), QStringLiteral("0.00049800 BTC"));
    QCOMPARE(model->sendRecipientList()->currentRecipient()->cAmount(), CAmount{49'800});
}

void WalletQmlModelTests::scheduleFeeEstimates_usesSelectedCoinsInCoinControl()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    const COutPoint selected_outpoint{Txid{}, 5};
    ThreadSafeCallLog<SelectedCoinEstimateCall> calls;

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>&,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        const auto selected = coin_control.ListSelected();
        calls.Append({
            coin_control.m_confirm_target.value_or(0),
            sign,
            coin_control.HasSelected() && coin_control.IsSelected(selected_outpoint) && selected.size() == 1U && selected.front() == selected_outpoint,
            coin_control.m_allow_other_inputs,
            selected.size(),
        });
        change_pos = -1;
        fee = TestFeeWeight(coin_control.m_confirm_target.value_or(0)) * 200;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    QVERIFY(model->coinsListModel()->setManualInputSelection(true));
    model->selectCoin(selected_outpoint);

    QTRY_COMPARE_WITH_TIMEOUT(calls.Size(), size_t{3}, FEE_ESTIMATE_TIMEOUT_MS);
    const auto selected_calls{calls.Snapshot()};
    constexpr std::array expected_targets{2U, 6U, 10U};
    for (size_t index{0}; index < selected_calls.size(); ++index) {
        QCOMPARE(selected_calls[index].target, expected_targets[index]);
        QVERIFY(!selected_calls[index].sign);
        QVERIFY(selected_calls[index].expected_coin_selected);
        QVERIFY(!selected_calls[index].allow_other_inputs);
        QCOMPARE(selected_calls[index].selected_count, 1U);
    }
}

void WalletQmlModelTests::prepareTransaction_disallowsOtherInputsWhenCoinsSelected()
{
    ManualRoutedExecution execution;
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    struct Probe {
        ThreadSafeCallLog<bool> available_other_inputs;
        std::atomic_bool selected{false}, other_inputs{true};
    };
    const auto probe = std::make_shared<Probe>();
    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};
    FeeReadServices services;
    services.timer = clock;
    auto [wallet, model] = MakeConfiguredWalletModel([probe, selected_outpoint](MockWallet& value) {
        value.list_coins_fn = [selected_outpoint] {
            const auto destination = DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
            interfaces::Wallet::CoinsList coins;
            coins[destination].emplace_back(selected_outpoint,
                interfaces::WalletTxOut{CTxOut{10 * COIN, GetScriptForDestination(destination)}, 0, 6, false});
            return coins;
        };
        value.get_available_balance_fn = [probe](const wallet::CCoinControl& control) {
            probe->available_other_inputs.Append(control.m_allow_other_inputs);
            return 10 * COIN;
        };
        value.create_transaction_fn = [probe, selected_outpoint](const std::vector<wallet::CRecipient>&,
                                                               const wallet::CCoinControl& control, bool,
                                                               int& change_pos, CAmount& fee) -> util::Result<CTransactionRef> {
            probe->selected = control.HasSelected() && control.IsSelected(selected_outpoint);
            probe->other_inputs = control.m_allow_other_inputs;
            change_pos = -1;
            fee = 200;
            return MakeTransactionRef(CMutableTransaction{});
        };
    }, nullptr, nullptr, std::move(services), false, {}, {}, execution.scheduler);
    ManualWalletDrain drain{execution, model.get()};
    const auto pump = [&] {
        execution.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        execution.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
    };
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient);
    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(50'000);
    QVERIFY(model->coinsListModel()->setManualInputSelection(true));
    model->selectCoin(selected_outpoint);
    QVERIFY(QTest::qWaitFor([&] {
        pump();
        return !model->coinSelectionPending() && !model->sendRecipientList()->validationPending() &&
            model->availableSendBalanceKnown() && execution.transportIdle();
    }, 5000));
    QVERIFY(model->sendRecipientList()->allValid());
    pump();
    QVERIFY(model->coinsListModel()->manualInputSelection());
    const auto available = probe->available_other_inputs.Snapshot();
    QVERIFY(!available.empty());
    QVERIFY(!available.back()); // The original selection's cached balance excludes other inputs.
    const auto available_reads = wallet->calls.getAvailableBalance.load();
    {
        [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
        wallet->ExpectExactly(wallet->calls.createTransaction, 1);
        QVERIFY(AwaitPreparation(*model, {}, pump));
        QVERIFY(probe->selected.load());
        QVERIFY(!probe->other_inputs.load());
        // Preparation consumes the acknowledged balance projection. It must not
        // repeat the Native facts reads, or make a synchronous GUI balance query.
        QCOMPARE(wallet->calls.getAvailableBalance.load(), available_reads);
        QCOMPARE(wallet->calls.createTransaction.load(), 1);
    }
    QVERIFY(drain.finish());
    QVERIFY(execution.quiescent());
}

void WalletQmlModelTests::scheduleFeeEstimates_debouncesRapidRestarts()
{
    auto [wallet, model] = MakeWalletModel();
    SetValidRecipient(*model);

    [[maybe_unused]] auto verify_wallet = wallet->VerifyOnExit();
    wallet->ExpectNoCalls(wallet->calls.getNewDestination);
    std::atomic<bool> saw_sign_true{false};
    wallet->create_transaction_fn = [&](const std::vector<wallet::CRecipient>&,
                                        const wallet::CCoinControl& coin_control,
                                        bool sign,
                                        int& change_pos,
                                        CAmount& fee) -> util::Result<CTransactionRef> {
        if (sign) saw_sign_true = true;
        change_pos = -1;
        fee = TestFeeWeight(coin_control.m_confirm_target.value_or(0)) * 250;
        return util::Result<CTransactionRef>{MakeTransactionRef(CMutableTransaction{})};
    };

    model->scheduleFeeEstimates();
    model->scheduleFeeEstimates();
    model->scheduleFeeEstimates();

    QTRY_COMPARE_WITH_TIMEOUT(wallet->calls.createTransaction.load(), 3, FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(!saw_sign_true.load());
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(2), QStringLiteral("0.00000250 BTC"));
    QCOMPARE(model->estimatedFeeForTarget(6), QStringLiteral("0.00000500 BTC"));
    QCOMPARE(model->estimatedFeeForTarget(10), QStringLiteral("0.00001500 BTC"));
}

void WalletQmlModelTests::scheduleFeeEstimates_invalidatesStalePreviewState()
{
    struct Probe {
        std::atomic_bool stale_started{false}, fresh_started{false}, on_gui{false};
        std::atomic_int assessments{0};
        ThreadSafeCallLog<CAmount> captured_amounts;
        QSemaphore release_stale, release_fresh;
    };
    const auto probe = std::make_shared<Probe>();
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    FeeReadServices services;
    services.timer = clock;
    services.read = [probe](const FeeReadInput& input) {
        if (QThread::currentThread() == QCoreApplication::instance()->thread()) probe->on_gui = true;
        const auto total = std::accumulate(input.recipients.begin(), input.recipients.end(), CAmount{0},
            [](CAmount sum, const FeeReadInput::Recipient& recipient) { return sum + recipient.amount; });
        probe->captured_amounts.Append(total);
        if (total == 49'000 && !probe->stale_started.exchange(true)) probe->release_stale.acquire();
        if (total == 49'900 && !probe->fresh_started.exchange(true)) probe->release_fresh.acquire();
        FeeReadResult result;
        for (const unsigned int target : {2U, 6U, 10U}) {
            ++probe->assessments;
            result.estimates.insert(target, SendFeePreview{
                total == 49'900 ? TestFeeWeight(target) * 100 : 50, 1, 1000, {}, false});
        }
        return result;
    };
    auto [wallet, model] = MakeConfiguredWalletModel([](MockWallet& value) {
        value.get_available_balance_fn = [](const wallet::CCoinControl&) { return CAmount{50'000}; };
    }, nullptr, nullptr, std::move(services));
    const auto unblock = qScopeGuard([probe] {
        probe->release_stale.release();
        probe->release_fresh.release();
    });
    SetValidRecipient(*model);
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient);
    recipient->amount()->setSatoshi(49'000);
    QTRY_VERIFY(!model->sendRecipientList()->validationPending() && model->availableSendBalanceKnown());
    model->scheduleFeeEstimates();
    clock->advance(std::chrono::milliseconds{250});
    QTRY_VERIFY_WITH_TIMEOUT(probe->stale_started.load(), FEE_ESTIMATE_TIMEOUT_MS);
    QVERIFY(model->feeEstimatePending());
    QCOMPARE(model->estimatedFeeForTarget(6), QString{});

    recipient->amount()->setSatoshi(49'900);
    QCOMPARE(model->estimatedFeeForTarget(6), QString{});
    // Validation is a separate controlled stage before the successor preview.
    QTRY_VERIFY(!model->sendRecipientList()->validationPending());
    model->scheduleFeeEstimates();
    QVERIFY(model->feeEstimatePending());
    clock->advance(std::chrono::milliseconds{250});

    probe->release_stale.release();
    QTRY_VERIFY_WITH_TIMEOUT(probe->fresh_started.load(), FEE_ESTIMATE_TIMEOUT_MS);
    QCOMPARE(model->estimatedFeeForTarget(6), QString{});
    QVERIFY(model->feeEstimatePending());
    QVERIFY(!model->sendAmountExhaustsBalance());

    probe->release_fresh.release();
    QTRY_COMPARE_WITH_TIMEOUT(model->estimatedFeeForTarget(6), QStringLiteral("0.00000200 BTC"), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY_WITH_TIMEOUT(!model->feeEstimatePending(), FEE_ESTIMATE_TIMEOUT_MS);
    QTRY_VERIFY(model->sendAmountExhaustsBalance());
    // This timing port reports fees even when the amount plus fee is
    // unaffordable. Actual construction/policy is covered by coreApiFeePreviews.
    QCOMPARE(probe->captured_amounts.Snapshot(), std::vector<CAmount>({49'000, 49'900}));
    QCOMPARE(probe->assessments.load(), 6);
    QVERIFY(!probe->on_gui.load());
    QCOMPARE(wallet->calls.createTransaction.load(), 0);
}

void WalletQmlModelTests::transactionChangedEmitsBalanceChanged()
{
    const QLocale previous_locale;
    const auto restore_locale = qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
    QLocale::setDefault(QLocale::c());

    auto wallet = std::make_unique<MockWallet>();
    auto* wallet_ptr = wallet.get();

    std::atomic<CAmount> balance{50 * COIN};
    interfaces::Wallet::TransactionChangedFn transaction_changed;
    wallet_ptr->get_balance_fn = [&] { return balance.load(); };
    wallet_ptr->handle_transaction_changed_fn = [&](interfaces::Wallet::TransactionChangedFn fn) {
        transaction_changed = std::move(fn);
        return std::unique_ptr<interfaces::Handler>{};
    };

    WalletQmlModel model{std::move(wallet)};
    QTRY_COMPARE(model.balance(), QStringLiteral("50.00000000"));
    QSignalSpy balance_spy{&model, &WalletQmlModel::balanceChanged};
    QVERIFY(transaction_changed);

    balance = 75 * COIN;
    transaction_changed(Txid::FromUint256(uint256{}), CT_UPDATED);

    QTRY_COMPARE(model.balance(), QStringLiteral("75.00000000"));
    // Only a changed cached balance notifies presentation consumers.
    QCOMPARE(balance_spy.count(), 1);
}

void WalletQmlModelTests::commitPaymentRequestUsesSelectedAddressType()
{
    auto [wallet, model] = MakePasswordWalletModel();

    model->currentPaymentRequest()->setLabel(QStringLiteral("Public name"));
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("typed receive"));
    model->currentPaymentRequest()->setAddressType(QStringLiteral("bech32m"));

    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QCOMPARE(wallet->new_destination_types.size(), size_t{1});
    QCOMPARE(wallet->new_destination_types.front(), OutputType::BECH32M);
    QCOMPARE(wallet->new_destination_labels.size(), size_t{1});
    QCOMPARE(wallet->new_destination_labels.front(), std::string{"typed receive"});
}

void WalletQmlModelTests::updateChangesUnpaidPaymentRequestAmount()
{
    auto [wallet, model] = MakePasswordWalletModel();

    model->currentPaymentRequest()->setLabel(QStringLiteral("first"));
    model->currentPaymentRequest()->amount()->setSatoshi(1000);
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    const QString request_id{model->currentPaymentRequest()->id()};
    QVERIFY(!request_id.isEmpty());

    // Unpaid public fields can change without generating another address.
    model->currentPaymentRequest()->amount()->setSatoshi(999999);
    model->currentPaymentRequest()->setLabel(QStringLiteral("second"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());

    const auto entry = model->receiveRequests()->entryById(request_id);
    QVERIFY(entry.has_value());
    QCOMPARE(entry->recipient.amount, CAmount{999999});
    QCOMPARE(QString::fromStdString(entry->recipient.label), QStringLiteral("second"));
    QCOMPARE(model->currentPaymentRequest()->amount()->satoshi(), qint64{999999});
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(request_id, 0, "", "", "")));
    QCOMPARE(model->receiveRequests()->entryById(request_id)->recipient.amount, CAmount{0});
    QCOMPARE(model->receiveRequests()->entryById(request_id)->recipient.label, std::string{});
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(request_id, 0, "  ", "\t", "\n")));
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(request_id, 0, "", "", "Private reminder")));
    QCOMPARE(model->receiveRequests()->entryById(request_id)->recipient.noteSelf, std::string("Private reminder"));
}

void WalletQmlModelTests::emptyPaymentRequestCanRemoveSavedPrivateNote_data()
{
    QTest::addColumn<bool>("paid");
    QTest::newRow("unpaid") << false;
    QTest::newRow("paid") << true;
}

void WalletQmlModelTests::emptyPaymentRequestCanRemoveSavedPrivateNote()
{
    QFETCH(bool, paid);
    auto [wallet, model] = MakePasswordWalletModel();
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    const auto* request = model->currentPaymentRequest();
    const QString id = request->id();
    const QString address = request->address();
    const auto created = request->created();
    QVERIFY(model->loadPaymentRequestDetail(id));

    if (paid) {
        CMutableTransaction incoming;
        incoming.vout.emplace_back(1, GetScriptForDestination(request->destination()));
        const auto payment = MakeIncomingPayment(incoming, {true});
        wallet->setWalletTxsFn([payment] { return std::set<interfaces::WalletTx>{payment}; });
        wallet->setTransactionDetails(payment);
        wallet->transaction_changed_fn(payment.tx->GetHash(), CT_UPDATED);
        QTRY_VERIFY(model->detailPaymentRequest()->paymentReceived());
    }
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(id, 0, "", "", "Private note")));
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QStringLiteral("Private note"));
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(id, 0, "", "", "")));
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QString{});
    QCOMPARE(model->detailPaymentRequest()->address(), address);
    QCOMPARE(model->detailPaymentRequest()->created(), created);
    QCOMPARE(model->detailPaymentRequest()->paymentReceived(), paid);
    const auto entry = model->receiveRequests()->entryById(id);
    QVERIFY(entry.has_value());
    QCOMPARE(entry->recipient.amount, CAmount{0});
    QCOMPARE(entry->recipient.label, std::string{});
    QCOMPARE(entry->recipient.message, std::string{});
    QCOMPARE(entry->recipient.noteSelf, std::string{});
    const auto persisted = ReceiveRequestHistoryModel::DeserializeEntries({ReceiveRequestHistoryModel::SerializeEntry(*entry)});
    QCOMPARE(persisted.front().recipient.noteSelf, std::string{});
    QCOMPARE(persisted.front().payment_received, paid);
    QVERIFY(model->loadPaymentRequestDetail(id));
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QString{});
    QTRY_COMPARE(model->detailPaymentRequest()->qrPayload(), paid ? QString{} : "bitcoin:" + address);
    QCOMPARE(wallet->get_new_destination_calls, 1);
}

void WalletQmlModelTests::paymentArrivalLocksRequestAndPreservesNote()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* request = model->currentPaymentRequest();
    request->setLabel("Original");
    request->setMessage("Public message");
    request->amount()->setSatoshi(10000);
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const QString id = request->id();
    const QString address = request->address();
    QVERIFY(model->loadPaymentRequestDetail(id));
    QVERIFY(!model->detailPaymentRequest()->paymentReceived());

    // A queued payment notification blocks an edit until its lock is applied.
    CMutableTransaction incoming;
    incoming.vout.emplace_back(1, GetScriptForDestination(request->destination()));
    const auto payment = MakeIncomingPayment(incoming, {true});
    wallet->setWalletTxsFn([payment] { return std::set<interfaces::WalletTx>{payment}; });
    wallet->setTransactionDetails(payment);
    QVERIFY(wallet->transaction_changed_fn);
    wallet->transaction_changed_fn(payment.tx->GetHash(), CT_UPDATED);
    QVERIFY(!AwaitReceiveRequest(model->updatePaymentRequest(id, 20000, "Changed", "Changed", "")));
    QTRY_VERIFY(model->detailPaymentRequest()->paymentReceived());
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{1});
    QVERIFY(model->detailPaymentRequest()->qrPayload().isEmpty());
    QVERIFY(model->transactionActivityModel()->paymentRequestUri(id).isEmpty());
    auto entry = model->receiveRequests()->entryById(id);
    QVERIFY(entry->payment_received);
    QCOMPARE(entry->recipient.amount, CAmount{10000});
    QCOMPARE(entry->recipient.label, std::string{"Original"});
    QCOMPARE(entry->recipient.message, std::string{"Public message"});
    // A lock is conservative presentation. A private edit still waits for the
    // current reconciliation outcome, including newer native notifications.
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(id, 10000, "Original", "Public message", "Private receipt")));
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QStringLiteral("Private receipt"));
    QCOMPARE(model->detailPaymentRequest()->address(), address);
    wallet->get_address_result = true;
    QVERIFY(AwaitReceiveRequest(model->setAddressLabel(address, "Updated private receipt")));
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QStringLiteral("Updated private receipt"));
    QCOMPARE(model->detailPaymentRequest()->label(), QStringLiteral("Original"));
    QCOMPARE(model->detailPaymentRequest()->message(), QStringLiteral("Public message"));
    QVERIFY(model->detailPaymentRequest()->qrPayload().isEmpty());

    // Losing a transaction/reorg must never restore sharing after observation.
    wallet->setWalletTxsFn([] { return std::set<interfaces::WalletTx>{}; });
    QVERIFY(model->loadPaymentRequestDetail(id));
    QVERIFY(model->detailPaymentRequest()->paymentReceived());
    QVERIFY(model->detailPaymentRequest()->qrPayload().isEmpty());
    QVERIFY(!AwaitReceiveRequest(model->updatePaymentRequest(id, 10000, "Changed", "Public message", "")));
    const auto blobs = std::vector<std::string>{ReceiveRequestHistoryModel::SerializeEntry(*model->receiveRequests()->entryById(id))};
    QVERIFY(ReceiveRequestHistoryModel::DeserializeEntries(blobs).front().payment_received);
}

void WalletQmlModelTests::paymentRequestTracksActualReceivedAmount()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* request = model->currentPaymentRequest();
    request->amount()->setSatoshi(10000);
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const QString id = request->id();
    const auto script = GetScriptForDestination(request->destination());

    CMutableTransaction first;
    first.vout.emplace_back(100, script);
    first.vout.emplace_back(41, script);
    first.vout.emplace_back(5000, CScript{} << OP_RETURN);
    auto first_payment = MakeIncomingPayment(first, {true, true, false});
    CMutableTransaction second;
    second.vout.emplace_back(200, script);
    const auto second_payment = MakeIncomingPayment(second, {true});
    wallet->setTransactionDetails(first_payment);
    wallet->setTransactionDetails(second_payment);
    wallet->setWalletTxsFn([first_payment, second_payment] {
        return std::set<interfaces::WalletTx>{first_payment, second_payment};
    });

    model->reloadReceiveRequests();
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(model->loadPaymentRequestDetail(id));
    QCOMPARE(model->detailPaymentRequest()->amount()->satoshi(), qint64{10000});
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{341});
    QCOMPARE(request->receivedAmountSatoshi(), qint64{341});
    // Rebuilding the transaction snapshot must not count a payment twice.
    QVERIFY(model->loadPaymentRequestDetail(id));
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{341});

    // The receiving wallet has no replacement metadata on the original.
    wallet->setTransactionInMempool(first_payment.tx->GetHash(), false);
    model->reloadReceiveRequests();
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(model->loadPaymentRequestDetail(id));
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{200});

    // A sender-side replaced original is out of the mempool and excluded;
    // its historical marker alone does not decide that (#1072).
    first_payment.replaced_by_txid.emplace(second_payment.tx->GetHash());
    wallet->setTransactionDetails(first_payment);
    wallet->setWalletTxsFn([first_payment, second_payment] {
        return std::set<interfaces::WalletTx>{first_payment, second_payment};
    });
    model->reloadReceiveRequests();
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(model->loadPaymentRequestDetail(id));
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{200});
    QCOMPARE(request->receivedAmountSatoshi(), qint64{200});
    QVERIFY(model->detailPaymentRequest()->paymentReceived());
    QVERIFY(model->detailPaymentRequest()->qrPayload().isEmpty());
    wallet->setTransactionDepth(first_payment.tx->GetHash(), 1);
    model->reloadReceiveRequests();
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(model->loadPaymentRequestDetail(id));
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{341});

    wallet->setTransactionAbandoned(second_payment.tx->GetHash(), true);
    model->reloadReceiveRequests();
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(model->loadPaymentRequestDetail(id));
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{141});
    wallet->setTransactionDepth(first_payment.tx->GetHash(), -1);
    model->reloadReceiveRequests();
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(model->loadPaymentRequestDetail(id));
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{0});
    QVERIFY(model->detailPaymentRequest()->paymentReceived());
    QVERIFY(model->detailPaymentRequest()->qrPayload().isEmpty());

    model->detailPaymentRequest()->clear();
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{0});
}

void WalletQmlModelTests::receiptNotificationCannotReadCoreOnGui()
{
    ManualRoutedExecution execution;
    auto wallet = std::make_shared<FakePasswordWallet>();
    auto model = std::make_unique<WalletQmlModel>(WalletQmlModel::SharedWallet{wallet, execution.scheduler}, nullptr,
        FeeReadServices{}, psbtfee::Services{}, backup::Services{});
    const auto cleanup = qScopeGuard([&] {
        model->beginShutdown(); execution.drain();
        QTRY_VERIFY(model->backendExecutor()->isDrained());
        model.reset(); execution.drain();
    });
    execution.drain(); QCoreApplication::processEvents();
    QmlRecentRequestEntry entry;
    entry.id = 1;
    entry.recipient.address = VALID_MAINNET_ADDRESS.toStdString();
    wallet->saved_receive_requests["1"] = ReceiveRequestHistoryModel::SerializeEntry(entry);
    model->reloadReceiveRequests(); execution.drain(); QCoreApplication::processEvents();
    QVERIFY(model->loadPaymentRequestDetail(QStringLiteral("1")));
    CMutableTransaction tx;
    tx.vout.emplace_back(100, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    const auto payment = MakeIncomingPayment(tx, {true});
    wallet->setTransactionDetails(payment);
    int reads{0}; bool on_gui{false};
    wallet->transaction_read_probe = [&] { ++reads; on_gui |= QThread::currentThread() == qApp->thread(); };
    std::thread producer([&] { wallet->transaction_changed_fn(payment.tx->GetHash(), CT_NEW); });
    producer.join();
    QVERIFY(execution.step(execution::Lane::Core)); // Session metadata ingress.
    while (execution.step(execution::Lane::Domain)) {} // Reader admits its own Core task without GUI.
    QCoreApplication::processEvents();
    QCOMPARE(reads, 0);
    QVERIFY(!on_gui);
    execution.drain(); // Manually execute the admitted Core read and Domain result.
    QVERIFY(reads > 0);
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{0});
    wallet->transaction_read_probe = {};
    QCoreApplication::processEvents();
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{100});
    QVERIFY(model->detailPaymentRequest()->paymentReceived());
}

void WalletQmlModelTests::closingDuringReceiptProjectionStopsFurtherWrites()
{
    const auto configure = [](FakePasswordWallet& wallet) {
        for (int id{1}; id <= 2; ++id) {
            QmlRecentRequestEntry entry;
            entry.id = id; entry.recipient.address = VALID_MAINNET_ADDRESS.toStdString();
            wallet.saved_receive_requests[util::ToString(id)] = ReceiveRequestHistoryModel::SerializeEntry(entry);
        }
    };
    auto [wallet, model] = MakePasswordWalletModel(nullptr, {}, {}, {}, configure);
    model->reloadReceiveRequests();
    QTRY_VERIFY(model->property("receiveRequestsLoaded").toBool() && !model->receiveRequestReconciliationPending() && model->receiveRequests()->count() == 2);
    for (const auto& id : {QStringLiteral("1"), QStringLiteral("2")}) {
        const auto entry = model->receiveRequests()->entryById(id);
        QVERIFY(entry); QVERIFY(!entry->payment_received);
    }
    QVERIFY(model->loadPaymentRequestDetail(QStringLiteral("1")));
    auto* detail = model->detailPaymentRequest();
    QCOMPARE(detail->id(), QStringLiteral("1"));
    QVERIFY(!detail->paymentReceived());
    QCOMPARE(detail->receivedAmountSatoshi(), qint64{0});
    QSignalSpy detail_paid{detail, &PaymentRequest::paymentReceivedChanged};
    QSignalSpy detail_amount{detail, &PaymentRequest::receivedAmountChanged};
    int closed{0}, paid_signals_at_close{0}, amount_signals_at_close{0};
    bool paid_at_close{false};
    qint64 amount_at_close{0};
    const auto reset_connection = connect(model->receiveRequests(), &QAbstractItemModel::modelReset, model.get(), [&] {
        if (closed) return;
        const auto first = model->receiveRequests()->entryById(QStringLiteral("1"));
        const auto second = model->receiveRequests()->entryById(QStringLiteral("2"));
        if (!first || !second || !first->payment_received || !second->payment_received) return;
        // Native authority publishes both original rows atomically. Close at
        // that real snapshot boundary, before the parent's held-detail refresh.
        QCOMPARE(first->recipient.address, VALID_MAINNET_ADDRESS.toStdString());
        QCOMPARE(second->recipient.address, first->recipient.address);
        ++closed;
        paid_at_close = detail->paymentReceived();
        amount_at_close = detail->receivedAmountSatoshi();
        paid_signals_at_close = detail_paid.count();
        amount_signals_at_close = detail_amount.count();
        model->beginShutdown();
    });
    const auto disconnect_reset = qScopeGuard([reset_connection] { QObject::disconnect(reset_connection); });
    CMutableTransaction tx;
    tx.vout.emplace_back(100, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    const auto payment = MakeIncomingPayment(tx, {true});
    wallet->setTransactionDetails(payment);
    wallet->transaction_changed_fn(payment.tx->GetHash(), CT_NEW);
    QTRY_COMPARE(closed, 1);
    QTRY_VERIFY(model->shutdownComplete());
    QCOMPARE(detail->paymentReceived(), paid_at_close);
    QCOMPARE(detail->receivedAmountSatoshi(), amount_at_close);
    QCOMPARE(detail_paid.count(), paid_signals_at_close);
    QCOMPARE(detail_amount.count(), amount_signals_at_close);
    QVERIFY(!model->available());
    QVERIFY(model->backendExecutor()->isDrained());
}

void WalletQmlModelTests::receiptNotificationUsesRealWorkerAndPublishesOriginalRequest()
{
    auto [wallet, model] = MakePasswordWalletModel();
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const auto id = model->currentPaymentRequest()->id();
    QVERIFY(model->loadPaymentRequestDetail(id));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    struct Probe { std::atomic<int> worker{0}, gui{0}; };
    const auto probe = std::make_shared<Probe>();
    const auto gui = QThread::currentThread();
    wallet->transaction_read_probe = [probe, gui] {
        if (QThread::currentThread() == gui) ++probe->gui; else ++probe->worker;
    };
    CMutableTransaction raw;
    raw.vout.emplace_back(100, GetScriptForDestination(model->detailPaymentRequest()->destination()));
    const auto payment = MakeIncomingPayment(raw, {true});
    wallet->setTransactionDetails(payment);
    std::thread producer([&] { wallet->transaction_changed_fn(payment.tx->GetHash(), CT_NEW); });
    producer.join();
    // Amount observation and the persisted paid-state receipt can reach the
    // original detail in separate publications. Require both actual facts.
    QTRY_VERIFY(model->detailPaymentRequest()->receivedAmountSatoshi() == qint64{100} &&
        model->detailPaymentRequest()->paymentReceived());
    QCOMPARE(model->detailPaymentRequest()->id(), id);
    QCOMPARE(probe->gui.load(), 0);
    QVERIFY(probe->worker.load() > 0);
}

void WalletQmlModelTests::replacementNotificationRechecksReceiverOriginal()
{
    auto [wallet, model] = MakePasswordWalletModel();
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const QString id = model->currentPaymentRequest()->id();
    const auto script = GetScriptForDestination(model->currentPaymentRequest()->destination());
    CMutableTransaction original_raw;
    original_raw.vout.emplace_back(100, script);
    const auto original = MakeIncomingPayment(original_raw, {true});
    wallet->setTransactionDetails(original);
    wallet->transaction_changed_fn(original.tx->GetHash(), CT_NEW);
    QVERIFY(model->loadPaymentRequestDetail(id));
    QTRY_COMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{100});

    // The sender bumps the fee. The receiver sees the replacement but gets no
    // changed notification or replaced_by_txid marker for the original.
    wallet->setTransactionInMempool(original.tx->GetHash(), false);
    CMutableTransaction replacement_raw;
    replacement_raw.nLockTime = 1;
    replacement_raw.vout.emplace_back(100, script);
    const auto replacement = MakeIncomingPayment(replacement_raw, {true});
    wallet->setTransactionDetails(replacement);
    QSignalSpy changed{model.get(), &WalletQmlModel::transactionChanged};
    wallet->transaction_changed_fn(replacement.tx->GetHash(), CT_NEW);
    QTRY_COMPARE(changed.size(), 1);
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{100});
    QVERIFY(model->detailPaymentRequest()->paymentReceived());
}

void WalletQmlModelTests::cancelledPaymentWithoutNotificationClearsReceivedAmount()
{
    auto [wallet, model] = MakePasswordWalletModel();
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const QString id = model->currentPaymentRequest()->id();
    CMutableTransaction incoming;
    incoming.vout.emplace_back(100, GetScriptForDestination(model->currentPaymentRequest()->destination()));
    const auto payment = MakeIncomingPayment(incoming, {true});
    wallet->setTransactionDetails(payment);
    QVERIFY(model->loadPaymentRequestDetail(id));
    wallet->transaction_changed_fn(payment.tx->GetHash(), CT_NEW);
    QTRY_COMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{100});

    // A conflicting replacement pays elsewhere. The receiver gets no
    // notification for its original transaction leaving the mempool.
    wallet->setTransactionInMempool(payment.tx->GetHash(), false);
    QTRY_COMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{0});
    QVERIFY(model->detailPaymentRequest()->paymentReceived());
    QVERIFY(model->detailPaymentRequest()->qrPayload().isEmpty());
}

void WalletQmlModelTests::reconciliationBatchesActivityRebuilds()
{
    constexpr int REQUEST_COUNT{500};
    auto [wallet, model] = MakePasswordWalletModel();
    auto* activity = model->transactionActivityModel();
    QTRY_VERIFY(!activity->loading());
    QVERIFY(ConfigurePasswordWalletOnNative(*model, [](FakePasswordWallet& original) {
        for (int i = 1; i <= REQUEST_COUNT; ++i) {
            QmlRecentRequestEntry entry;
            entry.id = i;
            entry.date = QDateTime::currentDateTime();
            entry.recipient.address = VALID_MAINNET_ADDRESS.toStdString();
            original.saved_receive_requests[util::ToString(i)] = ReceiveRequestHistoryModel::SerializeEntry(entry);
        }
    }));
    CMutableTransaction incoming;
    incoming.vout.emplace_back(100, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    const auto payment = MakeIncomingPayment(incoming, {true});
    wallet->setTransactionDetails(payment);
    const auto scan_gate = std::make_shared<QSemaphore>();
    const auto scan_started = std::make_shared<std::atomic_bool>(false);
    const auto drain_before_fixture_destruction = qScopeGuard([&] {
        scan_gate->release();
        model.reset();
    });
    wallet->setWalletTxsFn([scan_started, scan_gate, payment] {
        if (!scan_started->exchange(true)) scan_gate->acquire();
        return std::set<interfaces::WalletTx>{payment};
    });
    model->reloadReceiveRequests();
    QTRY_VERIFY(scan_started->load());
    QTRY_COMPARE(activity->requestCount(), REQUEST_COUNT);
    QVERIFY(activity->paymentRequestUri(QStringLiteral("1")).isEmpty());
    QSignalSpy rebuilt{activity, &TransactionActivityModel::transactionDetailsChanged};
    scan_gate->release();
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QTRY_VERIFY(model->receiveRequests()->entryById(QStringLiteral("1"))->payment_received);
    QTRY_COMPARE(rebuilt.size(), 1);
    QCOMPARE(activity->requestCount(), REQUEST_COUNT);
    QTRY_VERIFY(activity->index(0).data(TransactionActivityModel::PaymentRequestsRole)
        .toList().first().toMap().value("paymentReceived").toBool());
    QVERIFY(activity->paymentRequestUri(QStringLiteral("1")).isEmpty());
}

void WalletQmlModelTests::reconciliationPreservesPaymentNotifiedDuringScan()
{
    BackendExecutor commands{execution::Options{}};
    const auto close_commands = qScopeGuard([&] {
        commands.shutdown();
        QTRY_VERIFY(commands.isDrained());
    });
    // Match production: shared Core/Domain commands are independent of the
    // compatibility history-scan worker held below.
    auto [wallet, model] = MakePasswordWalletModel(nullptr, commands.scheduler());
    QTRY_VERIFY(!model->transactionActivityModel()->loading());
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const QString id = model->currentPaymentRequest()->id();
    QVERIFY(model->loadPaymentRequestDetail(id));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());

    CMutableTransaction incoming;
    incoming.vout.emplace_back(100, GetScriptForDestination(model->detailPaymentRequest()->destination()));
    const auto payment = MakeIncomingPayment(incoming, {true});
    QSemaphore scan_gate;
    std::atomic<bool> scan_started{false};
    const auto drain_before_fixture_destruction = qScopeGuard([&] {
        scan_gate.release();
        model.reset();
    });
    wallet->setWalletTxsFn([&] {
        if (!scan_started.exchange(true)) {
            scan_gate.acquire();
            // Only this captured snapshot predates the notification. Later
            // reads observe the real current backend value without a timeout.
            return std::set<interfaces::WalletTx>{};
        }
        return std::set<interfaces::WalletTx>{payment};
    });
    model->reloadReceiveRequests();
    QTRY_VERIFY(scan_started.load());
    QVERIFY(model->receiveRequestReconciliationPending());
    wallet->setTransactionDetails(payment);
    QSignalSpy changed{model.get(), &WalletQmlModel::transactionChanged};
    wallet->transaction_changed_fn(payment.tx->GetHash(), CT_NEW);
    bool heartbeat{false};
    QTimer::singleShot(0, model.get(), [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QCOMPARE(changed.size(), 0);
    QVERIFY(model->receiveRequestReconciliationPending());
    QVERIFY(model->transactionActivityModel()->paymentRequestUri(id).isEmpty());

    scan_gate.release();
    QTRY_COMPARE(changed.size(), 1);
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{100});
    QVERIFY(model->detailPaymentRequest()->paymentReceived());
    QVERIFY(model->receiveRequests()->entryById(id)->payment_received);
}

void WalletQmlModelTests::openingRequestRemainsResponsiveDuringLargeHistoryScan()
{
    constexpr int HISTORY_SIZE{10'000};
    auto wallet = std::make_unique<FakePasswordWallet>();
    wallet->get_address_result = true;
    const auto destination = DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    const auto script = GetScriptForDestination(destination);
    const auto history = std::make_shared<std::set<interfaces::WalletTx>>();
    std::map<std::string, std::string> stored_requests;
    for (int i = 0; i < HISTORY_SIZE; ++i) {
        QmlRecentRequestEntry entry;
        entry.id = i + 1;
        entry.date = QDateTime::currentDateTime();
        entry.recipient.address = VALID_MAINNET_ADDRESS.toStdString();
        entry.payment_received = true;
        stored_requests[util::ToString(entry.id)] = ReceiveRequestHistoryModel::SerializeEntry(entry);

        CMutableTransaction raw;
        raw.nLockTime = i;
        raw.vout.emplace_back(1, script);
        const auto tx = MakeIncomingPayment(raw, {true});
        history->insert(tx);
        wallet->setTransactionDetails(tx);
    }
    const auto scan_gate = std::make_shared<QSemaphore>();
    const auto scan_started = std::make_shared<std::atomic_bool>(false);
    const auto scan_finished = std::make_shared<std::atomic_bool>(false);
    const auto history_reads = std::make_shared<std::atomic_int>(0);
    std::unique_ptr<WalletQmlModel, DrainWalletModel> model{new WalletQmlModel(std::move(wallet))};
    const auto drain_before_fixture_destruction = qScopeGuard([&] {
        scan_gate->release();
        model.reset();
    });
    // Initial metadata and the empty stored-request snapshot are ready before
    // introducing the large history. The tested action opens an existing
    // cached request while its independent Native history capture is held.
    QTRY_VERIFY(model->capabilitiesKnown());
    QTRY_VERIFY(model->property("receiveRequestsLoaded").toBool());
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QTRY_VERIFY(!model->transactionActivityModel()->loading());
    QVERIFY(!scan_started->load());
    QVERIFY(ConfigurePasswordWalletOnNative(*model, [stored_requests = std::move(stored_requests), history, scan_gate, scan_started, scan_finished](FakePasswordWallet& original) {
        original.saved_receive_requests = stored_requests;
        original.setWalletTxsFn([history, scan_gate, scan_started, scan_finished] {
            if (!scan_started->exchange(true)) scan_gate->acquire();
            *scan_finished = true;
            return *history;
        });
    }));
    model->reloadReceiveRequests();
    QTRY_VERIFY(scan_started->load());
    // A held history capture can precede the separate immutable request
    // publication. Open only after that original row is actually available.
    QTRY_COMPARE(model->receiveRequests()->rowCount(), HISTORY_SIZE);
    QVERIFY(model->receiveRequestReconciliationPending());
    QVERIFY(!scan_finished->load());
    QElapsedTimer timer;
    timer.start();
    QVERIFY(model->loadPaymentRequestDetail(QStringLiteral("1")));
    QVERIFY2(timer.elapsed() < 250, "Opening a saved request blocked on the wallet history scan");
    QCOMPARE(model->detailPaymentRequest()->id(), QStringLiteral("1"));
    QVERIFY(!scan_finished->load());
    scan_gate->release();
    QTRY_VERIFY_WITH_TIMEOUT(!model->receiveRequestReconciliationPending(), 20'000);
    QCOMPARE(model->detailPaymentRequest()->receivedAmountSatoshi(), qint64{HISTORY_SIZE});

    // The original reconciliation also reloads Activity. Finish that separate
    // history capture before measuring reads caused by this new edit.
    QTRY_VERIFY_WITH_TIMEOUT(!model->transactionActivityModel()->loading(), 20'000);
    QTRY_VERIFY(!model->addressListModel()->refreshPending());
    QVERIFY(ConfigurePasswordWalletOnNative(*model, [history_reads, history](FakePasswordWallet& original) {
        original.setWalletTxsFn([history_reads, history] {
            ++*history_reads;
            return *history;
        });
    }));
    // The preceding Activity work can outlast a receipt-poll interval. Admit
    // this edit only after all original observations are ready again; this
    // case measures a ready edit, not the separate Checking refusal contract.
    QTRY_VERIFY_WITH_TIMEOUT(!model->receiveRequestReconciliationPending() &&
        !model->transactionActivityModel()->loading() && !model->addressListModel()->refreshPending(), 20'000);
    timer.restart();
    std::unique_ptr<ReceiveRequestOperation> edit{model->updatePaymentRequest(QStringLiteral("1"), 0, {}, {}, QStringLiteral("Private note"))};
    QVERIFY2(timer.elapsed() < 250, "Admitting a saved-request edit blocked on wallet history");
    QVERIFY(edit);
    QVERIFY2(QTest::qWaitFor([&] { return !edit->pending(); }, 5000), "The original request edit did not reach its terminal outcome");
    QCOMPARE(edit->error(), ReceiveRequestOperation::Error::None);
    QVERIFY(edit->requestSaved());
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QStringLiteral("Private note"));
    // A successful edit publishes its label refresh before completing. Await
    // its Native, Domain and GUI work before checking the read counter; the
    // separate Action barrier can be prioritized ahead of an Observation.
    QTRY_VERIFY(!model->addressListModel()->refreshPending());
    QCOMPARE(model->addressListModel()->readError(), QString{});
    QVERIFY(ConfigurePasswordWalletOnNative(*model, [](FakePasswordWallet&) {}));
    QCOMPARE(history_reads->load(), 0);
}

void WalletQmlModelTests::requestDetailEditsPreserveReceiveDraft()
{
    auto [wallet, model] = MakePasswordWalletModel();
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    const QString id = model->currentPaymentRequest()->id();
    const QString address = model->currentPaymentRequest()->address();
    const auto created = model->currentPaymentRequest()->created();
    model->currentPaymentRequest()->clear();
    model->currentPaymentRequest()->setLabel("Unrelated draft");
    QVERIFY(model->loadPaymentRequestDetail(id));
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(id, 49000, "Coffee & cake", "Thank you", "Private")));
    QCOMPARE(model->currentPaymentRequest()->label(), QStringLiteral("Unrelated draft"));
    QVERIFY(model->currentPaymentRequest()->address().isEmpty());
    QCOMPARE(model->detailPaymentRequest()->address(), address);
    QCOMPARE(model->detailPaymentRequest()->created(), created);
    QTRY_VERIFY(model->detailPaymentRequest()->qrPayload().contains("amount=0.00049000"));
    QVERIFY(model->detailPaymentRequest()->qrPayload().contains("label=Coffee%20%26%20cake"));
    QVERIFY(!model->detailPaymentRequest()->qrPayload().contains("Private"));
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(id, 0, "", "", "")));
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QString{});
    QCOMPARE(model->currentPaymentRequest()->label(), QStringLiteral("Unrelated draft"));
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(id, 0, "", "", "Private")));
    QTRY_COMPARE(model->detailPaymentRequest()->qrPayload(), "bitcoin:" + address);
    QVERIFY(!AwaitReceiveRequest(model->updatePaymentRequest(id, -1, "", "", "")));
    QVERIFY(!AwaitReceiveRequest(model->updatePaymentRequest(id, MAX_MONEY + 1, "", "", "")));
    QCOMPARE(wallet->get_new_destination_calls, 1);
}

void WalletQmlModelTests::setCurrentPaymentRequestAddressEditsExistingRequest()
{
    auto [wallet, model] = MakePasswordWalletModel();

    model->currentPaymentRequest()->setLabel(QStringLiteral("original"));
    model->currentPaymentRequest()->amount()->setSatoshi(1000);
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    const QString request_id{model->currentPaymentRequest()->id()};
    const QString address{model->currentPaymentRequest()->address()};
    QVERIFY(!request_id.isEmpty());
    QVERIFY(!address.isEmpty());
    QCOMPARE(model->receiveRequests()->rowCount(), 1);

    // Selecting the same address again must load the saved request for
    // editing instead of starting a fresh draft.
    QVERIFY(AwaitReceiveRequest(model->setCurrentPaymentRequestAddress(address)));
    QCOMPARE(model->currentPaymentRequest()->id(), request_id);
    QCOMPARE(model->currentPaymentRequest()->label(), QStringLiteral("original"));
    QCOMPARE(model->currentPaymentRequest()->amount()->satoshi(), qint64{1000});
    QVERIFY(model->currentPaymentRequest()->isEditing());

    // Committing from that state updates in place rather than duplicating.
    model->currentPaymentRequest()->setLabel(QStringLiteral("renamed"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QCOMPARE(model->receiveRequests()->rowCount(), 1);
    const auto entry = model->receiveRequests()->entryById(request_id);
    QVERIFY(entry.has_value());
    QCOMPARE(QString::fromStdString(entry->recipient.label), QStringLiteral("renamed"));
}

void WalletQmlModelTests::setCurrentPaymentRequestAddressUsesAddressListLabel()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.wallet_addresses.emplace_back(
            DecodeDestination(VALID_MAINNET_ADDRESS.toStdString()),
            true,
            wallet::AddressPurpose::RECEIVE,
            "invoice 1024");
        original.get_address_result = true;
        original.get_address_label = "invoice 1024";
    });

    QVERIFY(AwaitReceiveRequest(model->setCurrentPaymentRequestAddress(VALID_MAINNET_ADDRESS)));
    QCOMPARE(model->currentPaymentRequest()->address(), VALID_MAINNET_ADDRESS);
    QCOMPARE(model->currentPaymentRequest()->noteSelf(), QStringLiteral("invoice 1024"));
    QVERIFY(model->currentPaymentRequest()->label().isEmpty());
}

void WalletQmlModelTests::usePaymentRequestAsTemplatePreservesAddressType()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_new_destination_fn = [wallet](OutputType type, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_types.push_back(type);
        wallet->new_destination_labels.push_back(label);
        return DecodeDestination(VALID_MAINNET_P2SH_ADDRESS.toStdString());
    };

    model->currentPaymentRequest()->setLabel(QStringLiteral("typed template"));
    model->currentPaymentRequest()->setAddressType(QStringLiteral("p2sh-segwit"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));

    QVERIFY(AwaitReceiveRequest(model->ensureReceivingAddress()));
    const int generated = wallet->get_new_destination_calls;
    model->usePaymentRequestAsTemplate(QStringLiteral("1"));

    QVERIFY(model->receivingAddress()->address().isEmpty());
    QVERIFY(model->currentPaymentRequest()->id().isEmpty());
    QCOMPARE(wallet->get_new_destination_calls, generated);
    QCOMPARE(model->receiveRequests()->count(), 1);
    QVERIFY(model->currentPaymentRequest()->address().isEmpty());
    QCOMPARE(model->currentPaymentRequest()->addressType(), QStringLiteral("p2sh-segwit"));
    QCOMPARE(model->currentPaymentRequest()->label(), QStringLiteral("typed template"));
}

void WalletQmlModelTests::commitPaymentRequestOnLockedWalletSignalsNeedsUnlock()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_new_destination_fn = [wallet](OutputType, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_labels.push_back(label);
        if (wallet->locked) {
            return util::Error{Untranslated("keypool empty")};
        }
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };

    QVERIFY(!AwaitReceiveRequest(model->commitPaymentRequest()));
    QVERIFY(model->currentPaymentRequest()->needsUnlock());
    QVERIFY(model->currentPaymentRequest()->unlockError().isEmpty());
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QCOMPARE(wallet->unlock_calls, 0);
    QVERIFY(wallet->locked);
}

void WalletQmlModelTests::commitPaymentRequestWithPassphraseUnlocksRetriesAndRelocks_data()
{
    QTest::addColumn<QString>("original_password");QTest::newRow("native-small-literal")<<QStringLiteral("secret");QTest::newRow("valid-40000-character-literal")<<QString(40'000,QLatin1Char('r'));
}
void WalletQmlModelTests::commitPaymentRequestWithPassphraseUnlocksRetriesAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_new_destination_fn = [wallet](OutputType, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_labels.push_back(label);
        if (wallet->locked) {
            return util::Error{Untranslated("keypool empty")};
        }
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };

    QVERIFY(!AwaitReceiveRequest(model->commitPaymentRequest()));
    QVERIFY(model->currentPaymentRequest()->needsUnlock());

    QFETCH(QString,original_password);const auto expected_password=original_password.toUtf8().toStdString();
    auto unlock_thread=std::make_shared<std::atomic<QThread*>>(nullptr);const auto original_unlock=wallet->unlock_fn;
    wallet->unlock_fn=[unlock_thread,original_unlock](const SecureString& password){*unlock_thread=QThread::currentThread();return original_unlock(password);};
    auto* operation=model->commitPaymentRequestWithPassphrase(original_password);original_password.fill(QLatin1Char('z'));
    QVERIFY(AwaitReceiveRequest(operation));QVERIFY(unlock_thread->load());QVERIFY(unlock_thread->load()!=QThread::currentThread());
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->unlock_passphrases.size(), size_t{1});
    QCOMPARE(wallet->unlock_passphrases.back(), expected_password);
    QCOMPARE(wallet->lock_calls, 1);
    QVERIFY(wallet->locked);
    QCOMPARE(wallet->get_new_destination_calls, 2);
    QVERIFY(!model->currentPaymentRequest()->needsUnlock());
    QVERIFY(model->currentPaymentRequest()->unlockError().isEmpty());
    QVERIFY(!model->currentPaymentRequest()->address().isEmpty());
}

void WalletQmlModelTests::commitPaymentRequestWithPassphraseWrongPasswordSurfacesError()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_new_destination_fn = [wallet](OutputType, const std::string& label) -> util::Result<CTxDestination> {
        ++wallet->get_new_destination_calls;
        wallet->new_destination_labels.push_back(label);
        if (wallet->locked) {
            return util::Error{Untranslated("keypool empty")};
        }
        return DecodeDestination(VALID_MAINNET_ADDRESS.toStdString());
    };
    wallet->unlock_fn = [wallet](const SecureString& passphrase) {
        ++wallet->unlock_calls;
        wallet->unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("")}};
    };

    QVERIFY(!AwaitReceiveRequest(model->commitPaymentRequest()));
    QVERIFY(!AwaitReceiveRequest(model->commitPaymentRequestWithPassphrase(QStringLiteral("wrong"))));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 0);
    QCOMPARE(wallet->get_new_destination_calls, 1);
    QCOMPARE(model->currentPaymentRequest()->unlockError(),
             QStringLiteral("The wallet password you entered was incorrect."));
    QVERIFY(model->currentPaymentRequest()->address().isEmpty());
}

void WalletQmlModelTests::removeReceiveRequestRemovesPendingActivityRow()
{
    auto [wallet, model] = MakePasswordWalletModel();
    model->currentPaymentRequest()->setLabel(QStringLiteral("request label"));

    QTRY_COMPARE(model->transactionActivityModel()->rowCount(), 0);
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QCOMPARE(wallet->set_address_receive_request_calls, 1);
    QCOMPARE(wallet->receive_request_ids.back(), std::string{"1"});
    QTRY_COMPARE(model->transactionActivityModel()->rowCount(), 1);

    QVERIFY(AwaitReceiveRequest(model->removeReceiveRequest(QStringLiteral("1"))));
    QCOMPARE(wallet->set_address_receive_request_calls, 2);
    QCOMPARE(wallet->receive_request_ids.back(), std::string{"1"});
    QTRY_COMPARE(model->transactionActivityModel()->rowCount(), 0);
}

void WalletQmlModelTests::editedReceiveRequestNameShownInActivityMetadata()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.get_address_result = true;
        original.get_address_label = "address book label";
    });

    // The address book reports a different label for the request address.
    // request metadata must show the request's own name. The activity row
    // label remains reserved for its private note.

    model->currentPaymentRequest()->setLabel(QStringLiteral("Old label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));

    TransactionActivityModel* activity = model->transactionActivityModel();
    QTRY_COMPARE(activity->rowCount(), 1);
    const QModelIndex row = activity->index(0);
    QVERIFY(activity->data(row, TransactionActivityModel::IsPendingRequestRole).toBool());
    QCOMPARE(activity->data(row, TransactionActivityModel::PaymentRequestsRole).toList().first().toMap().value("label").toString(), QStringLiteral("Old label"));

    // Editing the label commits an update for the same request id.
    model->currentPaymentRequest()->setLabel(QStringLiteral("New label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));

    QCOMPARE(activity->rowCount(), 1);
    QTRY_COMPARE(activity->data(activity->index(0), TransactionActivityModel::PaymentRequestsRole).toList().first().toMap().value("label").toString(),
             QStringLiteral("New label"));
}

void WalletQmlModelTests::editedRequestSyncsAddressBookLabel()
{
    auto [wallet, model] = MakePasswordWalletModel();
    // getAddress must succeed for the label sync to reach setAddressBook.
    wallet->get_address_result = true;

    // Creating the request labels its address.
    model->currentPaymentRequest()->setLabel(QStringLiteral("Public name"));
    model->currentPaymentRequest()->setMessage(QStringLiteral("Lunch split"));
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Old label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QCOMPARE(wallet->last_set_address_book_label, std::string{"Old label"});
    const int public_edit_calls = wallet->set_address_book_calls;
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(model->currentPaymentRequest()->id(), 0,
                                       "New public name", "Dinner split", "Old label")));
    QCOMPARE(wallet->set_address_book_calls, public_edit_calls);

    // Editing the label writes the new label back to the address book, so the
    // Addresses page reflects it instead of keeping the stale label.
    const int calls_before = wallet->set_address_book_calls;
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("New label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(wallet->set_address_book_calls > calls_before);
    QCOMPARE(wallet->last_set_address_book_label, std::string{"New label"});
    // Clearing the private note clears its matching address label, but leaves
    // the public fields intact and present in the payment URI.
    wallet->get_address_label = "New label";
    QVERIFY(AwaitReceiveRequest(model->updatePaymentRequest(model->currentPaymentRequest()->id(), 0,
                                       "New public name", "Dinner split", "")));
    QVERIFY(wallet->last_set_address_book_label.empty());
    QCOMPARE(model->currentPaymentRequest()->label(), QStringLiteral("New public name"));
}

void WalletQmlModelTests::requestSaveLeavesUneditedAddressBookLabelAlone()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        // Both public address-book reads describe the same pre-existing label.
        original.get_address_result = true;
        original.get_address_label = "Book label";
        original.wallet_addresses.emplace_back(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString()),
            true, wallet::AddressPurpose::RECEIVE, "Book label");
    });

    // Saving a request whose note is empty must not clear the book label.
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QCOMPARE(wallet->set_address_book_calls, 0);

    // Saving with an unchanged note (e.g. an amount-only edit) must not
    // rewrite the book label either.
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Book label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QCOMPARE(wallet->set_address_book_calls, 0);
}

void WalletQmlModelTests::amountOnlyRequestEditPreservesAddressBookLabel()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_address_result = true;

    // Creating the request labels its address.
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Request label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QCOMPARE(wallet->last_set_address_book_label, std::string{"Request label"});

    // The address is then relabeled independently on the Addresses page.
    wallet->get_address_label = "Independent label";
    const int calls_before = wallet->set_address_book_calls;

    // Editing only the amount must not re-assert the request note over the
    // independent one; the save changed no label.
    model->currentPaymentRequest()->amount()->setSatoshi(12'345);
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QCOMPARE(wallet->set_address_book_calls, calls_before);

    // Editing the request note itself still writes it back.
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Renamed request"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QVERIFY(wallet->set_address_book_calls > calls_before);
    QCOMPARE(wallet->last_set_address_book_label, std::string{"Renamed request"});
}

void WalletQmlModelTests::editedAddressBookLabelSyncsRequestNote()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.get_address_result = true;
    });

    // Create a request; its address carries the private note.
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Old label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const QString address = model->currentPaymentRequest()->address();
    QVERIFY(!address.isEmpty());

    TransactionActivityModel* activity = model->transactionActivityModel();
    QTRY_COMPARE(activity->rowCount(), 1);
    QTRY_COMPARE(activity->data(activity->index(0), TransactionActivityModel::PaymentRequestsRole).toList().first().toMap().value("noteSelf").toString(),
             QStringLiteral("Old label"));

    // Editing the label on the Addresses page must propagate to the matching
    // request, so the request record and its Activity row do not diverge from
    // the address book (the reverse of editedRequestSyncsAddressBookLabel).
    QVERIFY(AwaitReceiveRequest(model->setAddressLabel(address, QStringLiteral("New label"))));

    QTRY_COMPARE(activity->data(activity->index(0), TransactionActivityModel::PaymentRequestsRole).toList().first().toMap().value("noteSelf").toString(),
             QStringLiteral("New label"));
    const QVariantList matches = model->receiveRequests()->matchingEntriesForAddress(address);
    QCOMPARE(matches.size(), 1);
    QCOMPARE(matches.at(0).toMap().value(QStringLiteral("noteSelf")).toString(),
             QStringLiteral("New label"));
}

// Core supports multiple receive requests for one address, each with its own
// note. Saving one request writes its note to the address book but must not
// fan it out to the sibling requests; only an Addresses page edit
// (setAddressLabel) deliberately rewrites every request for the address.
void WalletQmlModelTests::requestSaveLeavesSiblingRequestNotesAlone()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_address_result = true;

    // Two requests on the same address (the fake wallet hands out one
    // destination), with their own labels.
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Request A"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    const QString request_a_id = model->currentPaymentRequest()->id();
    const QString address = model->currentPaymentRequest()->address();

    model->currentPaymentRequest()->clear();
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Request B"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    const QString request_b_id = model->currentPaymentRequest()->id();
    QVERIFY(request_a_id != request_b_id);
    QCOMPARE(model->currentPaymentRequest()->address(), address);

    // Editing request A's note updates the address book but leaves request
    // B's persisted note alone.
    QVERIFY(model->loadPaymentRequest(request_a_id));
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Edited A"));
    const int b_writes_before = static_cast<int>(
        std::count(wallet->receive_request_ids.begin(), wallet->receive_request_ids.end(),
                   request_b_id.toStdString()));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    QTRY_VERIFY(!model->receiveRequestReconciliationPending());
    QCOMPARE(wallet->last_set_address_book_label, std::string{"Edited A"});

    const int b_writes_after = static_cast<int>(
        std::count(wallet->receive_request_ids.begin(), wallet->receive_request_ids.end(),
                   request_b_id.toStdString()));
    QCOMPARE(b_writes_after, b_writes_before);
    const auto sibling = model->receiveRequests()->entryById(request_b_id);
    QVERIFY(sibling.has_value());
    QCOMPARE(QString::fromStdString(sibling->recipient.noteSelf), QStringLiteral("Request B"));
}

// A label sync write that fails to persist must not update the in-memory
// request models: they would show a label the wallet does not hold, and a
// reload would silently revert it.
void WalletQmlModelTests::labelSyncSkipsInMemoryUpdateWhenPersistFails()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_address_result = true;

    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Old label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const QString request_id = model->currentPaymentRequest()->id();
    const QString address = model->currentPaymentRequest()->address();

    wallet->set_address_receive_request_result = false;
    QVERIFY(AwaitReceiveRequest(model->setAddressLabel(address, QStringLiteral("New label"))));

    const auto entry = model->receiveRequests()->entryById(request_id);
    QVERIFY(entry.has_value());
    QCOMPARE(QString::fromStdString(entry->recipient.noteSelf), QStringLiteral("Old label"));
    TransactionActivityModel* activity = model->transactionActivityModel();
    QTRY_COMPARE(activity->rowCount(), 1);
    QCOMPARE(activity->data(activity->index(0), TransactionActivityModel::PaymentRequestsRole).toList().first().toMap().value("noteSelf").toString(),
             QStringLiteral("Old label"));
}

void WalletQmlModelTests::editedAddressBookLabelUpdatesHeldRequestObjects()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->get_address_result = true;

    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Old label"));
    QVERIFY(AwaitReceiveRequest(model->commitPaymentRequest()));
    const QString address = model->currentPaymentRequest()->address();
    const QString request_id = model->currentPaymentRequest()->id();
    QVERIFY(!address.isEmpty());
    QVERIFY(model->loadPaymentRequestDetail(request_id));
    QTRY_VERIFY(!model->detailPaymentRequest()->qrPayload().isEmpty());
    const QString public_uri = model->detailPaymentRequest()->qrPayload();

    // A held-open detail page or locked editor keeps reading these objects,
    // so an Addresses page label edit must refresh them too, not only the
    // stored request and its Activity row.
    QVERIFY(AwaitReceiveRequest(model->setAddressLabel(address, QStringLiteral("New label"))));
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QStringLiteral("New label"));
    QCOMPARE(model->currentPaymentRequest()->noteSelf(), QStringLiteral("New label"));
    QTRY_COMPARE(model->detailPaymentRequest()->qrPayload(), public_uri);
    QVERIFY(model->detailPaymentRequest()->label().isEmpty());

    // While the editor is mid-edit its unsaved draft must survive the sync;
    // the detail object still follows.
    model->currentPaymentRequest()->edit();
    model->currentPaymentRequest()->setNoteSelf(QStringLiteral("Unsaved draft"));
    QVERIFY(AwaitReceiveRequest(model->setAddressLabel(address, QStringLiteral("Renamed again"))));
    QCOMPARE(model->currentPaymentRequest()->noteSelf(), QStringLiteral("Unsaved draft"));
    QCOMPARE(model->detailPaymentRequest()->noteSelf(), QStringLiteral("Renamed again"));
}

void WalletQmlModelTests::prepareTransactionOnLockedWalletRequiresPassword()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    QVERIFY(!AwaitPreparation(*model));
    QVERIFY(model->isEncrypted());
    QVERIFY(model->isLocked());
    QVERIFY(model->transactionNeedsUnlock());
    QCOMPARE(model->transactionError(), QString("Enter your wallet password to prepare this transaction."));
    QVERIFY(wallet->create_transaction_sign_args.empty());
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
}

void WalletQmlModelTests::prepareTransactionWithPrivateKeysDisabledDoesNotRequirePassword()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) { original.private_keys_disabled = true; });
    SetPasswordRecipient(*model, 1'000);

    QVERIFY(AwaitPreparation(*model));
    QVERIFY(model->isEncrypted());
    QVERIFY(model->isLocked());
    QVERIFY(!model->transactionNeedsUnlock());
    QVERIFY(wallet->create_transaction_sign_args == std::vector<bool>{false});
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
}

void WalletQmlModelTests::sendRecipientRejectsDustAmount()
{
    auto [wallet, model] = MakePasswordWalletModel();
    QTRY_VERIFY(model->availableSendBalanceKnown());
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(1);

    QTRY_VERIFY(!recipient->validationPending());
    QVERIFY(!recipient->isValid());
    QCOMPARE(recipient->amountError(), QStringLiteral("Amount is too small to send."));
    QVERIFY(!model->sendRecipientList()->allValid());
}

void WalletQmlModelTests::sendRecipientUsesNodeDustRelayFee()
{
    MockNode node;
    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.get_dust_relay_fee_fn = [] { return CFeeRate{10'000}; };
    node.ExpectAtLeast(node.calls.getDustRelayFee, 1);
    auto [wallet, model] = MakePasswordWalletModel(&node);
    QTRY_VERIFY(model->availableSendBalanceKnown());
    QCOMPARE(model->dustRelayFee().GetFeePerK(), CAmount{10'000});
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);

    recipient->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    recipient->amount()->setSatoshi(600);

    QTRY_VERIFY(!recipient->validationPending());
    QVERIFY(!recipient->isValid());
    QCOMPARE(recipient->amountError(), QStringLiteral("Amount is too small to send."));
    QVERIFY(!model->sendRecipientList()->allValid());
}

void WalletQmlModelTests::prepareTransactionRejectsDuplicateRecipientsBeforeUnlock()
{
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    auto [wallet, model] = MakePasswordWalletModel(nullptr, {}, {}, clock);
    SetPasswordRecipient(*model, 1'000);

    model->sendRecipientList()->add();
    auto* second = model->sendRecipientList()->currentRecipient();
    QVERIFY(second != nullptr);
    second->address()->setAddress(VALID_MAINNET_ADDRESS, 0);
    second->amount()->setSatoshi(2'000);
    QTRY_VERIFY(second->isValid());
    QTRY_VERIFY(!model->sendRecipientList()->validationPending());

    QVERIFY(!model->sendRecipientList()->allValid());
    QCOMPARE(model->sendRecipientList()->validationError(), QString("Recipient addresses must be unique."));

    QVERIFY(!AwaitPreparation(*model, "secret"));
    QCOMPARE(model->transactionError(), QString("Recipient addresses must be unique."));
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::prepareTransactionWithPassphraseForwardsUtf8Bytes_data()
{
    QTest::addColumn<QString>("original_password");QTest::newRow("utf8-literal")<<QString::fromUtf8("pässwörd-₿");QTest::newRow("valid-40000-character-literal")<<QString(40'000,QLatin1Char('p'));
}
void WalletQmlModelTests::prepareTransactionWithPassphraseForwardsUtf8Bytes()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    QFETCH(QString,original_password);const std::string expected_passphrase{original_password.toUtf8().toStdString()};
    auto unlock_thread=std::make_shared<std::atomic<QThread*>>(nullptr);const auto original_unlock=wallet->unlock_fn;
    wallet->unlock_fn=[unlock_thread,original_unlock](const SecureString& password){*unlock_thread=QThread::currentThread();return original_unlock(password);};
    QTRY_VERIFY(!model->coinSelectionPending() && !model->sendRecipientList()->validationPending() &&
        !model->customFeeTargetPending());
    const auto ticket=model->requestPrepareTransactionWithPassphrase(original_password);QVERIFY(ticket);original_password.fill(QLatin1Char('z'));
    QTRY_VERIFY(!model->preparationBusy());QCOMPARE(model->preparationId(),ticket);QCOMPARE(model->preparationState(),WalletQmlModel::PreparationState::PreparationReady);
    QVERIFY(unlock_thread->load());QVERIFY(unlock_thread->load()!=QThread::currentThread());
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->unlock_passphrases.size(), size_t{1});
    QCOMPARE(wallet->unlock_passphrases.front(), expected_passphrase);
    QCOMPARE(wallet->lock_calls, 1);
}

void WalletQmlModelTests::prepareTransactionWithPassphraseRelocksWhenRecipientsInvalid()
{
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    auto [wallet, model] = MakePasswordWalletModel(nullptr, {}, {}, clock);
    auto* recipient = model->sendRecipientList()->currentRecipient();
    QVERIFY(recipient != nullptr);
    recipient->amount()->setSatoshi(1'000);
    QTRY_VERIFY(!model->sendRecipientList()->validationPending());
    QVERIFY(!recipient->isValid());

    QVERIFY(!model->sendRecipientList()->allValid());
    QVERIFY(model->sendRecipientList()->validationError().isEmpty());

    QVERIFY(!AwaitPreparation(*model, "secret"));
    QVERIFY(wallet->locked);
    QCOMPARE(model->transactionError(), QStringLiteral("Enter at least one valid recipient to continue."));
    QCOMPARE(model->preparationState(), WalletQmlModel::PreparationState::PreparationFailed);
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::prepareTransactionWithPassphraseRequiresCompleteMultiRecipient()
{
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    auto [wallet, model] = MakePasswordWalletModel(nullptr, {}, {}, clock);
    SetPasswordRecipient(*model, 1'000);

    model->sendRecipientList()->add();
    auto* second = model->sendRecipientList()->currentRecipient();
    QVERIFY(second != nullptr);
    QTRY_VERIFY(!model->sendRecipientList()->validationPending());
    QVERIFY(!second->isValid());

    QVERIFY(!model->sendRecipientList()->allValid());
    QCOMPARE(model->sendRecipientList()->validationError(), QString("Complete recipient 2 before continuing."));

    QVERIFY(!AwaitPreparation(*model, "secret"));
    QVERIFY(wallet->locked);
    QCOMPARE(model->transactionError(), QString("Complete recipient 2 before continuing."));
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::prepareTransactionWithPassphraseRelocksWhenCustomFeeInvalid()
{
    const auto clock = std::make_shared<ManualExecutionEnvironment>();
    auto [wallet, model] = MakePasswordWalletModel(nullptr, {}, {}, clock);
    SetPasswordRecipient(*model, 1'000);
    model->setCustomFeeEnabled(true);
    model->setCustomFeeRate(QStringLiteral("not-a-fee"));

    QVERIFY(!AwaitPreparation(*model, "secret"));
    QVERIFY(wallet->locked);
    // The invalid intent is rejected before any unlock or preparation effect.
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
    QCOMPARE(model->preparationState(), WalletQmlModel::PreparationState::PreparationFailed);
    QCOMPARE(model->transactionError(), QStringLiteral("Enter a valid custom fee rate."));
    QVERIFY(wallet->create_transaction_sign_args.empty());
}

void WalletQmlModelTests::prepareTransactionWithPassphraseReportsCreateErrorAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    wallet->create_transaction_fn = [](const std::vector<wallet::CRecipient>&,
                                       const wallet::CCoinControl&,
                                       bool,
                                       int&,
                                       CAmount&) -> util::Result<CTransactionRef> {
        return util::Error{Untranslated("Transaction needs a change address, but we can't generate it. Error: Keypool ran out, please call keypoolrefill first")};
    };

    QVERIFY(!AwaitPreparation(*model, "secret"));
    QVERIFY(model->isEncrypted());
    QVERIFY(model->isLocked());
    QVERIFY(!model->transactionNeedsUnlock());
    QCOMPARE(model->transactionError(), QString("Transaction needs a change address, but we can't generate it. Error: Keypool ran out, please call keypoolrefill first"));
    QVERIFY(wallet->create_transaction_sign_args == std::vector<bool>{true});
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
}

void WalletQmlModelTests::sendTransactionCommitsPreparedTransactionWithoutUnlockingAgain()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);

    QVERIFY(AwaitPreparation(*model, "secret"));
    QVERIFY(wallet->locked);
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);

    QVERIFY(AwaitCommittedSend(*model));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
    QCOMPARE(wallet->commit_calls, 1);
    QVERIFY(wallet->locked);
    QVERIFY(wallet->fill_psbt_sign_args.empty());
    QVERIFY(model->transactionError().isEmpty());
    QVERIFY(!model->transactionNeedsUnlock());
}

void WalletQmlModelTests::sendTransactionClearsSelectedCoins()
{
    auto [wallet, model] = MakePasswordWalletModel();
    SetPasswordRecipient(*model, 1'000);
    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};

    QVERIFY(model->coinsListModel()->setManualInputSelection(true));
    model->selectCoin(selected_outpoint);
    QTRY_COMPARE(model->selectedCoins()->size(), size_t{1});

    QVERIFY(AwaitPreparation(*model, "secret"));
    QVERIFY(AwaitCommittedSend(*model));
    QTRY_VERIFY(model->selectedCoins()->empty());
    QTRY_VERIFY(!model->coinsListModel()->manualInputSelection());
}

void WalletQmlModelTests::clearingRecipientsClearsSelectedCoins()
{
    auto [wallet, model] = MakePasswordWalletModel();
    const COutPoint selected_outpoint{Txid::FromUint256(uint256::ONE), 1};

    QVERIFY(model->coinsListModel()->setManualInputSelection(true));
    model->selectCoin(selected_outpoint);
    QTRY_COMPARE(model->selectedCoins()->size(), size_t{1});

    model->sendRecipientList()->clear();
    QTRY_VERIFY(model->selectedCoins()->empty());
}

void WalletQmlModelTests::saveCurrentTransactionAsPsbt_savesUnsignedPreparedTransaction()
{
    const std::vector<unsigned char> script_sig_bytes{0x51};
    const std::vector<unsigned char> witness_bytes{0x02, 0x03};
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([&](FakePasswordWallet& original) {
        original.create_transaction_fn = [script_sig_bytes, witness_bytes](const std::vector<wallet::CRecipient>& recipients,
                                                                      const wallet::CCoinControl&,
                                                                      bool,
                                                                      int& change_pos,
                                                                      CAmount& fee) -> util::Result<CTransactionRef> {
            CMutableTransaction mtx;
            mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
            mtx.vin[0].scriptSig = CScript{script_sig_bytes.begin(), script_sig_bytes.end()};
            mtx.vin[0].scriptWitness.stack.push_back(witness_bytes);
            mtx.vout.emplace_back(1'000, GetScriptForDestination(recipients.at(0).dest));
            change_pos = -1;
            fee = 100;
            return MakeTransactionRef(std::move(mtx));
        };
    });
    SetPasswordRecipient(*model, 1'000);

    QVERIFY(AwaitPreparation(*model, "secret"));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{temp_dir.filePath(QStringLiteral("prepared.psbt"))};
    QCOMPARE(AwaitPsbtSaveError(*model, path), QString());

    QFile file{path};
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray bytes{file.readAll()};
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char byte : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }

    const auto decoded{DecodeRawPSBT(std::span<const std::byte>{raw.data(), raw.size()})};
    QVERIFY(decoded.has_value());
    const auto decoded_tx{decoded->GetUnsignedTx()};
    QVERIFY(decoded_tx.has_value());
    QCOMPARE(decoded_tx->vout.size(), size_t{1});
    QCOMPARE(decoded_tx->vout[0].nValue, CAmount{1'000});
    QVERIFY(decoded_tx->vout[0].scriptPubKey == GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    QCOMPARE(decoded_tx->vin.size(), size_t{1});
    QVERIFY(decoded_tx->vin[0].scriptSig.empty());
    QVERIFY(decoded_tx->vin[0].scriptWitness.IsNull());
    QCOMPARE(decoded->inputs.size(), size_t{1});
    QVERIFY(decoded->inputs[0].final_script_sig.empty());
    QVERIFY(decoded->inputs[0].final_script_witness.IsNull());
}

void WalletQmlModelTests::importPsbtFromFile_opensOwnedUnsignedPsbtWithoutSigning()
{
    const auto psbt = MakeReviewPsbt();
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([&](FakePasswordWallet& original) {
        original.txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
            return txin.prevout == owned_outpoint;
        };
        original.fill_psbt_fn = [&original](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
            const bool sign{options.sign};
            original.fill_psbt_sign_args.push_back(sign);
            if (n_signed) {
                *n_signed = 1;
            }
            complete = sign;
            return std::nullopt;
        };
    });

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{temp_dir.filePath(QStringLiteral("unsigned.psbt"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray raw{PsbtQmlModel::SerializePsbtRaw(psbt)};
    QCOMPARE(file.write(raw), raw.size());
    file.close();

    QVERIFY(!AwaitPsbtBroadcast(*model));
    QVERIFY(!model->transactionError().isEmpty());
    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCanSign);
    QVERIFY(model->transactionError().isEmpty());
    QVERIFY(!wallet->fill_psbt_sign_args.empty());
    QVERIFY(std::none_of(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), [](bool sign) {
        return sign;
    }));
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(model->currentTransaction()->getWtx() != nullptr);
    QCOMPARE(model->currentTransaction()->getWtx()->vin.size(), size_t{1});
    QVERIFY(model->currentTransaction()->getWtx()->vin[0].scriptSig.empty());
    QVERIFY(model->currentTransaction()->getWtx()->vin[0].scriptWitness.IsNull());
    QTRY_VERIFY(!model->currentTransaction()->presentationPending());
    const auto recipients = model->currentTransaction()->reviewedRecipients();
    QCOMPARE(recipients.size(), qsizetype{1});
    QCOMPARE(recipients.first().toMap().value("address").toString(), QString{VALID_MAINNET_ADDRESS});
    QCOMPARE(recipients.first().toMap().value("amountSat").toLongLong(), qint64{1'500});
    QVERIFY(model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QVERIFY(model->currentTransactionReviewMessage().isEmpty());
}

void WalletQmlModelTests::importPsbtFromFile_opensForeignUnsignedPsbtForReviewOnly()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.fill_psbt_fn = [&original](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
            const bool sign{options.sign};
            original.fill_psbt_sign_args.push_back(sign);
            if (n_signed) {
                *n_signed = 1;
            }
            complete = false;
            return std::nullopt;
        };
    });

    const auto psbt = MakeReviewPsbt();

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{temp_dir.filePath(QStringLiteral("foreign-unsigned.psbt"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray raw{PsbtQmlModel::SerializePsbtRaw(psbt)};
    QCOMPARE(file.write(raw), raw.size());
    file.close();

    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!wallet->fill_psbt_sign_args.empty());
    QVERIFY(std::none_of(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), [](bool sign) {
        return sign;
    }));
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(model->currentTransaction()->getWtx() != nullptr);
    QTRY_VERIFY(!model->currentTransaction()->presentationPending());
    const auto recipients = model->currentTransaction()->reviewedRecipients();
    QCOMPARE(recipients.size(), qsizetype{1});
    QCOMPARE(recipients.first().toMap().value("address").toString(), QString{VALID_MAINNET_ADDRESS});
    QCOMPARE(recipients.first().toMap().value("amountSat").toLongLong(), qint64{1'500});
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(model->currentTransactionReviewMessage(), QString("This wallet does not have the keys to sign this transaction."));
    QVERIFY(!model->importedPsbt()->loaded());

    QVERIFY(!AwaitCommittedSend(*model));
    QCOMPARE(wallet->commit_calls, 0);
    QCOMPARE(model->transactionError(), QString("This wallet does not have the keys to sign this transaction."));
}

void WalletQmlModelTests::saveReviewOnlyPsbt_preservesOriginalWithoutDerivationMetadata()
{
    PartiallySignedTransaction psbt{MakeReviewPsbt()};
    psbt.unknown[{0x50}] = {0x01};
    const QByteArray original{PsbtQmlModel::SerializePsbtRaw(psbt)};
    const auto bip32_derivation_requests = std::make_shared<std::vector<bool>>();
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([bip32_derivation_requests](FakePasswordWallet& original_wallet) {
        original_wallet.fill_psbt_fn = [&original_wallet, bip32_derivation_requests](const common::PSBTFillOptions& options,
                                                               size_t* n_signed,
                                                               PartiallySignedTransaction& psbtx,
                                                               bool& complete) {
            const bool sign{options.sign};
            original_wallet.fill_psbt_sign_args.push_back(sign);
            const bool bip32derivs{options.bip32_derivs};
            bip32_derivation_requests->push_back(bip32derivs);
            if (bip32derivs) {
                psbtx.unknown[{0x53}] = {0x04};
            }
            if (n_signed) {
                *n_signed = 1;
            }
            complete = false;
            return std::nullopt;
        };
    });

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{WritePsbt(psbt, temp_dir, QStringLiteral("review-only-source.psbt"))};
    QVERIFY(!source_path.isEmpty());

    QCOMPARE(AwaitPsbtImport(*model, source_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QCOMPARE(*bip32_derivation_requests, std::vector<bool>({false}));

    const QString saved_path{temp_dir.filePath(QStringLiteral("review-only-saved.psbt"))};
    QCOMPARE(AwaitPsbtSaveError(*model, saved_path), QString{});
    QCOMPARE(*bip32_derivation_requests, std::vector<bool>({false}));

    QFile saved_file{saved_path};
    QVERIFY(saved_file.open(QIODevice::ReadOnly));
    QCOMPARE(saved_file.readAll(), original);
}

void WalletQmlModelTests::discardCurrentTransaction_clearsReviewState()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{WritePsbt(psbt, temp_dir, QStringLiteral("discard-review.psbt"))};
    QVERIFY(!path.isEmpty());
    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!AwaitCommittedSend(*model));
    QVERIFY(!model->transactionError().isEmpty());

    QSignalSpy transaction_changed{model.get(), &WalletQmlModel::currentTransactionChanged};
    model->discardCurrentTransaction();

    QCOMPARE(transaction_changed.count(), 1);
    QVERIFY(model->currentTransaction() == nullptr);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(model->currentTransactionReviewMessage().isEmpty());
    QVERIFY(model->transactionError().isEmpty());
    QCOMPARE(model->sendRecipientList()->count(), 1);
    QVERIFY(model->sendRecipientList()->currentRecipient()->address()->address().isEmpty());
    QCOMPARE(model->sendRecipientList()->currentRecipient()->amount()->satoshi(), CAmount{0});
}

void WalletQmlModelTests::importPsbtFromFile_opensWatchOnlyUnsignedPsbtForReviewOnly()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) { original.private_keys_disabled = true; });

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{WritePsbt(psbt, temp_dir, QStringLiteral("watch-only.psbt"))};
    QVERIFY(!path.isEmpty());

    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(!model->currentTransactionCanSend());
    QCOMPARE(model->currentTransactionReviewMessage(), QString("This wallet does not have the keys to sign this transaction."));
}

void WalletQmlModelTests::saveCurrentTransactionAsPsbt_preservesImportedMetadata()
{
    auto [wallet, model] = MakePasswordWalletModel();

    PartiallySignedTransaction psbt{MakeReviewPsbt()};
    psbt.unknown[{0x50}] = {0x01};
    psbt.inputs[0].unknown[{0x51}] = {0x02};
    psbt.outputs[0].unknown[{0x52}] = {0x03};
    const QByteArray original{PsbtQmlModel::SerializePsbtRaw(psbt)};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{WritePsbt(psbt, temp_dir, QStringLiteral("source.psbt"))};
    QVERIFY(!source_path.isEmpty());
    QCOMPARE(AwaitPsbtImport(*model, source_path), WalletQmlModel::PsbtImportResult::WalletCanSign);
    const auto fill_calls_after_import{wallet->fill_psbt_sign_args.size()};

    const QString saved_path{temp_dir.filePath(QStringLiteral("saved.psbt"))};
    QCOMPARE(AwaitPsbtSaveError(*model, saved_path), QString{});
    QCOMPARE(wallet->fill_psbt_sign_args.size(), fill_calls_after_import);

    QFile saved_file{saved_path};
    QVERIFY(saved_file.open(QIODevice::ReadOnly));
    const QByteArray bytes{saved_file.readAll()};
    QCOMPARE(bytes, original);
    std::vector<std::byte> raw;
    raw.reserve(bytes.size());
    for (const char byte : bytes) {
        raw.push_back(static_cast<std::byte>(static_cast<unsigned char>(byte)));
    }

    const auto saved{DecodeRawPSBT(std::span<const std::byte>{raw.data(), raw.size()})};
    QVERIFY(saved.has_value());
    QCOMPARE(saved->unknown, psbt.unknown);
    QCOMPARE(saved->inputs[0].unknown, psbt.inputs[0].unknown);
    QCOMPARE(saved->outputs[0].unknown, psbt.outputs[0].unknown);
}

void WalletQmlModelTests::sendImportedPsbtWithPassphraseSignsOnceAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction& psbt,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        if (sign) {
            CompleteReviewPsbt(psbt);
        }
        complete = sign;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{WritePsbt(psbt, temp_dir, QStringLiteral("locked.psbt"))};
    QVERIFY(!path.isEmpty());
    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCanSign);

    QVERIFY(!AwaitCommittedSend(*model));
    QVERIFY(model->transactionNeedsUnlock());
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 0);

    QVERIFY(AwaitCommittedSend(*model, QStringLiteral("secret")));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
    QVERIFY(wallet->locked);
    QCOMPARE(wallet->commit_calls, 1);
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 1);
}

void WalletQmlModelTests::signerSubstitutionIsRejected()
{
    QFETCH(int, mutation);
    QFETCH(bool, partial);
    QFETCH(bool, direct_send);
    QFETCH(bool, draft);
    const auto reviewed = signerfixture::Review();
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([reviewed](FakePasswordWallet& original) {
        original.private_keys_disabled = true;
        original.external_signer = true;
        original.balance = COIN;
        original.encrypted = false;
        original.locked = false;
        for (const auto& input : reviewed.inputs) original.known_transactions.emplace(input.GetOutPoint().hash, input.non_witness_utxo);
        original.txout_is_mine_fn = [reviewed](const CTxOut& output) {
            for (const auto& input : reviewed.inputs) if (input.non_witness_utxo->vout[input.GetOutPoint().n] == output) return true;
            return false;
        };
    });
    model->refreshBalance();
    QTRY_COMPARE(model->displayedBalanceSatoshi(), qint64{COIN});
    wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
    wallet->fill_psbt_fn = [wallet, mutation, partial](const common::PSBTFillOptions& options, size_t* n_signed,
                                                    PartiallySignedTransaction& psbt, bool& complete) {
        wallet->fill_psbt_sign_args.push_back(options.sign);
        if (n_signed) *n_signed = psbt.inputs.size();
        complete = false;
        if (options.sign) {
            if (mutation < 0) {
                psbt.inputs[0].time_locktime = 500000001;
                psbt.inputs[1].height_locktime = 42;
            } else {
                auto changed = *psbt.GetUnsignedTx();
                signerfixture::Change(changed, signerfixture::CASES.at(mutation).second);
                psbt = PartiallySignedTransaction{changed, /*version=*/2};
            }
            for (auto& input : psbt.inputs) {
                input.unknown[{0x53}] = {0x99};
                if (!partial) input.final_script_sig = CScript{} << std::vector<unsigned char>{};
            }
        }
        return std::optional<common::PSBTError>{};
    };
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const auto unsigned_review = *reviewed.GetUnsignedTx();
    const auto original = unsigned_review.GetHash();
    const auto file = WritePsbt(reviewed, dir, QStringLiteral("reviewed.psbt"));
    if (draft) {
        wallet->create_transaction_fn = [reviewed](const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&, bool,
                                                 int& change_pos, CAmount& fee) -> util::Result<CTransactionRef> {
            change_pos = -1; fee = 500;
            return MakeTransactionRef(*reviewed.GetUnsignedTx());
        };
        for (size_t i = 0; i < unsigned_review.vout.size(); ++i) {
            if (i) model->sendRecipientList()->add();
            CTxDestination destination;
            QVERIFY(ExtractDestination(unsigned_review.vout[i].scriptPubKey, destination));
            auto* recipient = model->sendRecipientList()->currentRecipient();
            recipient->setAddress(QString::fromStdString(EncodeDestination(destination)));
            recipient->amount()->setSatoshi(unsigned_review.vout[i].nValue);
        }
        QVERIFY2(AwaitPreparation(*model), qPrintable(model->transactionError()));
    } else {
        QCOMPARE(AwaitPsbtImport(*model, file), WalletQmlModel::PsbtImportResult::WalletCanSign);
    }
    QCOMPARE(model->currentTransaction()->getWtx()->GetHash(), original);
    QSignalSpy failed{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
    QSignalSpy succeeded{model.get(), &WalletQmlModel::externalSignerApprovalSucceeded};
    QSignalSpy partial_result{model.get(), &WalletQmlModel::externalSignerApprovalPartiallySucceeded};
    const bool sent = direct_send ? AwaitCommittedSend(*model) : false;
    if (!direct_send) QVERIFY(AwaitSignerApprovalOutcome(*model));
    const int failures = failed.count(), successes = succeeded.count(), partials = partial_result.count();
    const auto failure_reason = direct_send ? model->transactionError()
        : failed.empty() ? QString{} : failed[0][0].toString();
    const auto after = model->currentTransaction()->getWtx()->GetHash();
    const bool can_send = model->currentTransactionCanSend(), can_broadcast = model->currentTransactionCanBroadcast();
    const bool sent_again = partial ? AwaitCommittedSend(*model, "unused") : AwaitCommittedSend(*model);
    const int commits = wallet->commit_calls;
    const auto saved_path = dir.filePath("retained.psbt");
    const auto save_error = AwaitPsbtSaveError(*model, saved_path);
    PartiallySignedTransaction saved{CMutableTransaction{}, /*version=*/2};
    const auto load_error = PsbtQmlModel::LoadPsbtFromFile(saved_path, saved);
    const auto saved_tx = saved.GetUnsignedTx();
    model.reset(); // Drain before the red behavioral assertions, including on failure.
    QVERIFY(!sent);
    if (!direct_send) { QCOMPARE(failures, 1); QCOMPARE(successes, 0); QCOMPARE(partials, 0); }
    QCOMPARE(failure_reason, QString("External signer changed the reviewed transaction."));
    QCOMPARE(commits, 0);
    QCOMPARE(after, original);
    QVERIFY(can_send); // The untouched review remains eligible for a fresh signing attempt.
    QVERIFY(!can_broadcast);
    QVERIFY(!sent_again);
    QCOMPARE(save_error, QString{});
    QCOMPARE(load_error, QString{});
    QVERIFY(saved_tx);
    QCOMPARE(saved_tx->GetHash(), original);
}

void WalletQmlModelTests::signerUnverifiedResultIsSeparateAndExportUsesOriginalToken()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.private_keys_disabled = true;
        original.external_signer = true;
    });
    wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
    const auto fixture = psbtfeefixture::SignedWithoutScripts(false);
    auto reviewed = fixture.document;
    reviewed.inputs[0].partial_sigs.clear();
    auto unverified = fixture.document;
    unverified.inputs[0].partial_sigs.erase(std::next(unverified.inputs[0].partial_sigs.begin()));
    int calls{0};
    wallet->fill_psbt_fn = [unverified, &calls](const common::PSBTFillOptions& options, size_t*,
                                               PartiallySignedTransaction& result, bool& complete) {
        complete = false;
        if (options.sign) { ++calls; result = unverified; }
        return std::optional<common::PSBTError>{};
    };
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto original_path = WritePsbt(reviewed, directory, "original.psbt");
    QCOMPARE(AwaitPsbtImport(*model, original_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    const auto original_review = model->currentTransaction();
    QSignalSpy accepted{model.get(), &WalletQmlModel::externalSignerApprovalPartiallySucceeded};
    QSignalSpy failed{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
    QVERIFY(AwaitSignerApprovalOutcome(*model));
    QCOMPARE(model->signerApprovalState(), WalletQmlModel::ApprovalState::ApprovalUnverified);
    QCOMPARE(accepted.count(), 0);
    QCOMPARE(failed.count(), 1);
    QCOMPARE(calls, 1);
    QCOMPARE(model->currentTransaction(), original_review);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QVERIFY(!model->approveExternalSignerTransaction());
    const auto token = model->signerUnverifiedResultToken();
    QVERIFY(!token.isEmpty());
    const auto rejected_path = directory.filePath("stale.psbt");
    QVERIFY(!model->requestSaveUnverifiedSignerResult(token + "-stale", rejected_path));
    QVERIFY(!QFile::exists(rejected_path));
    const auto exported_path = directory.filePath("unverified.psbt");
    QVERIFY(model->requestSaveUnverifiedSignerResult(token, exported_path));
    QTRY_VERIFY(!model->psbtActionBusy());
    QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtSaved);
    PartiallySignedTransaction exported{CMutableTransaction{}, 2};
    QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(exported_path, exported), QString{});
    QCOMPARE(psbtwork::SerializeRaw(exported), psbtwork::SerializeRaw(unverified));
    const auto reviewed_path = directory.filePath("approved-review.psbt");
    QCOMPARE(AwaitPsbtSaveError(*model, reviewed_path), QString{});
    PartiallySignedTransaction saved_review{CMutableTransaction{}, 2};
    QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(reviewed_path, saved_review), QString{});
    QCOMPARE(psbtwork::SerializeRaw(saved_review), psbtwork::SerializeRaw(reviewed));
    QCOMPARE(AwaitPsbtImport(*model, original_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(model->signerUnverifiedResultToken().isEmpty());
    QVERIFY(!model->requestSaveUnverifiedSignerResult(token, rejected_path));
    QVERIFY(!QFile::exists(rejected_path));
    QCOMPARE(calls, 1);
    QCOMPARE(wallet->commit_calls, 0);
}

void WalletQmlModelTests::localUnverifiedResultKeepsTheReviewAndExportsOriginalBytes()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) { original.locked = false; });
    wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
    const auto fixture = psbtfeefixture::Contribution(false, false);
    int calls{0};
    wallet->fill_psbt_fn = [returned = fixture.returned, &calls](const common::PSBTFillOptions& options, size_t* count,
                                                               PartiallySignedTransaction& document, bool& complete) {
        if (count) *count = 1;
        complete = false;
        if (options.sign) { ++calls; document = returned; }
        return std::optional<common::PSBTError>{};
    };
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto original_path = WritePsbt(fixture.original, directory, "local-original.psbt");
    QCOMPARE(AwaitPsbtImport(*model, original_path), WalletQmlModel::PsbtImportResult::WalletCanSign);
    const auto original_review = model->currentTransaction();
    QVERIFY(model->requestSignOnlyTransaction());
    QTRY_VERIFY(!model->sendBusy());
    QCOMPARE(model->sendState(), WalletQmlModel::SendState::SendUnverified);
    QCOMPARE(model->currentTransaction(), original_review);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QVERIFY(!model->requestSignOnlyTransaction());
    const auto token = model->signerUnverifiedResultToken();
    QVERIFY(token.startsWith("local:"));
    const auto exported_path = directory.filePath("local-unverified.psbt");
    QVERIFY(model->requestSaveUnverifiedSignerResult(token, exported_path));
    QTRY_VERIFY(!model->psbtActionBusy());
    QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtSaved);
    PartiallySignedTransaction exported{CMutableTransaction{}, 2};
    QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(exported_path, exported), QString{});
    QCOMPARE(psbtwork::SerializeRaw(exported), psbtwork::SerializeRaw(fixture.returned));
    QCOMPARE(AwaitPsbtImport(*model, original_path), WalletQmlModel::PsbtImportResult::WalletCanSign);
    QVERIFY(model->signerUnverifiedResultToken().isEmpty());
    QVERIFY(!model->requestSaveUnverifiedSignerResult(token, directory.filePath("stale.psbt")));
    QCOMPARE(calls, 1);
    QCOMPARE(wallet->commit_calls, 0);
}

void WalletQmlModelTests::signerApprovalRetainsValidPartialSignatures()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.private_keys_disabled = true;
        original.external_signer = true;
    });
    wallet->txin_is_mine_fn = [](const CTxIn&) { return true; };
    CKey first, second;
    first.MakeNewKey(true); second.MakeNewKey(true);
    const auto witness_script = GetScriptForMultisig(2, {first.GetPubKey(), second.GetPubKey()});
    auto reviewed = MakeWitnessReviewPsbt(witness_script, 1500);
    auto partial = reviewed;
    FillableSigningProvider provider;
    QVERIFY(provider.AddCScript(witness_script));
    QVERIFY(provider.AddKey(first));
    const auto txdata = PrecomputePSBTData(partial);
    QVERIFY(txdata);
    const auto partial_signing = SignPSBTInput(provider, partial, 0, &*txdata, {.finalize = false});
    QVERIFY(!partial_signing);
    QCOMPARE(partial_signing.error(), PSBTError::INCOMPLETE);
    QCOMPARE(partial.inputs[0].partial_sigs.size(), size_t{1});
    auto cannot_finalize = partial;
    QVERIFY(!FinalizePSBT(cannot_finalize));
    wallet->fill_psbt_fn = [partial](const common::PSBTFillOptions& options, size_t* n_signed,
                                    PartiallySignedTransaction& result, bool& complete) {
        if (n_signed) *n_signed = 1;
        complete = false;
        if (options.sign) result = partial;
        return std::optional<common::PSBTError>{};
    };
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = WritePsbt(reviewed, directory, "review.psbt");
    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QSignalSpy accepted{model.get(), &WalletQmlModel::externalSignerApprovalPartiallySucceeded};
    QSignalSpy failed{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
    QVERIFY(AwaitSignerApprovalOutcome(*model));
    QCOMPARE(accepted.count(), 1);
    QCOMPARE(failed.count(), 0);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(wallet->commit_calls, 0);
    const auto saved_path = directory.filePath("partial.psbt");
    QCOMPARE(AwaitPsbtSaveError(*model, saved_path), QString{});
    PartiallySignedTransaction saved{CMutableTransaction{}, /*version=*/2};
    QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(saved_path, saved), QString{});
    QCOMPARE(saved.GetUnsignedTx()->GetHash(), reviewed.GetUnsignedTx()->GetHash());
    QVERIFY(saved.inputs[0].partial_sigs == partial.inputs[0].partial_sigs);
    QVERIFY(provider.AddKey(second));
    const auto saved_data = PrecomputePSBTData(saved);
    QVERIFY(saved_data);
    QVERIFY(SignPSBTInput(provider, saved, 0, &*saved_data, {.finalize = false}));
    QVERIFY(FinalizePSBT(saved)); // The retained real signature remains usable.
}

void WalletQmlModelTests::externalSignerApprovalSignsImportedPsbtOnlyOnce()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.private_keys_disabled = true;
        original.external_signer = true;
    });

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };
    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction& psbt,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        if (sign) {
            CompleteReviewPsbt(psbt);
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{WritePsbt(psbt, temp_dir, QStringLiteral("external.psbt"))};
    QVERIFY(!path.isEmpty());
    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCanSign);

    QSignalSpy succeeded_spy{model.get(), &WalletQmlModel::externalSignerApprovalSucceeded};
    QVERIFY(AwaitSignerApprovalOutcome(*model));
    QCOMPARE(succeeded_spy.count(), 1);
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 1);

    QVERIFY(AwaitCommittedSend(*model));
    QCOMPARE(wallet->commit_calls, 1);
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 1);
}

void WalletQmlModelTests::externalSignerApprovalKeepsIncompleteSignedPsbt()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.private_keys_disabled = true;
        original.external_signer = true;
    });

    const PartiallySignedTransaction psbt{MakeReviewPsbt()};
    const COutPoint owned_outpoint{FirstInputPrevout(psbt)};
    wallet->txin_is_mine_fn = [owned_outpoint](const CTxIn& txin) {
        return txin.prevout == owned_outpoint;
    };

    const std::vector<unsigned char> signer_key{0x53};
    const std::vector<unsigned char> signer_value{0x99};
    wallet->fill_psbt_fn = [wallet, signer_key, signer_value](const common::PSBTFillOptions& options,
                                                              size_t* n_signed,
                                                              PartiallySignedTransaction& psbt,
                                                              bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 1;
        }
        if (sign) {
            psbt.inputs[0].unknown[signer_key] = signer_value;
        }
        complete = false;
        return std::nullopt;
    };

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{WritePsbt(psbt, temp_dir, QStringLiteral("incomplete-external.psbt"))};
    QVERIFY(!path.isEmpty());
    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCanSign);

    QSignalSpy succeeded_spy{model.get(), &WalletQmlModel::externalSignerApprovalSucceeded};
    QSignalSpy partially_succeeded_spy{model.get(), &WalletQmlModel::externalSignerApprovalPartiallySucceeded};
    QSignalSpy failed_spy{model.get(), &WalletQmlModel::externalSignerApprovalFailed};
    QVERIFY(AwaitSignerApprovalOutcome(*model));

    QCOMPARE(succeeded_spy.count(), 0);
    QCOMPARE(partially_succeeded_spy.count(), 1);
    QCOMPARE(failed_spy.count(), 0);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(model->currentTransactionReviewMessage(), QString("Signed on external signer. More signatures are required."));
    QCOMPARE(std::count(wallet->fill_psbt_sign_args.begin(), wallet->fill_psbt_sign_args.end(), true), 1);

    const QString saved_path{temp_dir.filePath(QStringLiteral("saved-incomplete-external.psbt"))};
    QCOMPARE(AwaitPsbtSaveError(*model, saved_path), QString{});

    CMutableTransaction empty_tx;
    PartiallySignedTransaction saved{empty_tx, /*version=*/2};
    QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(saved_path, saved), QString{});
    QVERIFY(saved.inputs[0].unknown.contains(signer_key));
    QCOMPARE(saved.inputs[0].unknown.at(signer_key), signer_value);
}

void WalletQmlModelTests::importPsbtFromFile_broadcastsCompleteForeignMultisigPsbt()
{
    MockNode node;
    auto [wallet, model] = MakePasswordWalletModel(&node);

    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    CKey key1;
    CKey key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    const CScript witness_script{GetScriptForMultisig(2, {key1.GetPubKey(), key2.GetPubKey()})};

    auto psbt = MakeWitnessReviewPsbt(witness_script, 1'500);
    QVERIFY(PsbtQmlModel::IsMultisigPsbtInput(psbt, 0));

    FillableSigningProvider provider;
    QVERIFY(provider.AddCScript(witness_script));
    QVERIFY(provider.AddKey(key1));
    QVERIFY(provider.AddKey(key2));
    const auto txdata{PrecomputePSBTData(psbt)};
    QVERIFY(txdata.has_value());
    QVERIFY(SignPSBTInput(provider, psbt, 0, &*txdata, /*options=*/{}).has_value());
    QVERIFY(FinalizePSBT(psbt));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{temp_dir.filePath(QStringLiteral("complete-multisig.psbt"))};
    QFile file{path};
    QVERIFY(file.open(QIODevice::WriteOnly));
    const QByteArray raw{PsbtQmlModel::SerializePsbtRaw(psbt)};
    QCOMPARE(file.write(raw), raw.size());
    file.close();

    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(model->currentTransactionCanBroadcast());
    QVERIFY(model->currentTransactionReviewMessage().isEmpty());
    QCOMPARE(wallet->commit_calls, 0);

    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.broadcast_transaction_fn = [](CTransactionRef, CAmount, CFeeRate, std::string&) { return node::TransactionError::OK; };
    node.ExpectExactly(node.calls.broadcastTransaction, 1);
    QVERIFY(AwaitPsbtBroadcast(*model));
    QCOMPARE(wallet->commit_calls, 0);
    QVERIFY(!model->currentTransactionCanBroadcast());
    QVERIFY(model->transactionError().isEmpty());
}

void WalletQmlModelTests::saveCompleteBroadcastableImportedPsbt_preservesOriginalPsbt()
{
    MockNode node;
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.fill_psbt_fn = [&original](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
            const bool sign{options.sign};
            original.fill_psbt_sign_args.push_back(sign);
            if (n_signed) {
                *n_signed = 0;
            }
            complete = false;
            return std::nullopt;
        };
    }, &node);

    CKey key1;
    CKey key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    const CScript witness_script{GetScriptForMultisig(2, {key1.GetPubKey(), key2.GetPubKey()})};

    auto psbt = MakeWitnessReviewPsbt(witness_script, 1'500);

    FillableSigningProvider provider;
    QVERIFY(provider.AddCScript(witness_script));
    QVERIFY(provider.AddKey(key1));
    QVERIFY(provider.AddKey(key2));

    const auto txdata{PrecomputePSBTData(psbt)};
    QVERIFY(txdata.has_value());
    QVERIFY(SignPSBTInput(provider, psbt, 0, &*txdata, {.finalize = false}).has_value());

    QVERIFY(!psbt.inputs[0].partial_sigs.empty());
    QVERIFY(!psbt.inputs[0].witness_script.empty());
    QVERIFY(psbt.inputs[0].final_script_witness.IsNull());

    PartiallySignedTransaction finalized_copy{psbt};
    QVERIFY(FinalizePSBT(finalized_copy));
    QVERIFY(finalized_copy.inputs[0].partial_sigs.empty());
    QVERIFY(finalized_copy.inputs[0].witness_script.empty());
    QVERIFY(!finalized_copy.inputs[0].final_script_witness.IsNull());

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{WritePsbt(psbt, temp_dir, QStringLiteral("complete-unfinalized.psbt"))};
    QVERIFY(!source_path.isEmpty());

    QCOMPARE(AwaitPsbtImport(*model, source_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(model->currentTransactionCanBroadcast());

    const QString saved_path{temp_dir.filePath(QStringLiteral("saved.psbt"))};
    QCOMPARE(AwaitPsbtSaveError(*model, saved_path), QString{});

    CMutableTransaction empty_tx;
    PartiallySignedTransaction saved{empty_tx, /*version=*/2};
    QCOMPARE(PsbtQmlModel::LoadPsbtFromFile(saved_path, saved), QString{});

    QCOMPARE(saved.inputs[0].partial_sigs.size(), psbt.inputs[0].partial_sigs.size());
    QVERIFY(saved.inputs[0].witness_script == psbt.inputs[0].witness_script);
    QVERIFY(saved.inputs[0].final_script_witness.IsNull());
    QCOMPARE(PsbtQmlModel::SerializePsbtRaw(saved), PsbtQmlModel::SerializePsbtRaw(psbt));
}

void WalletQmlModelTests::importPsbtFromFile_skipsZeroValueOpReturnOutputs()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.fill_psbt_fn = [&original](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
            const bool sign{options.sign};
            original.fill_psbt_sign_args.push_back(sign);
            if (n_signed) {
                *n_signed = 0;
            }
            complete = false;
            return std::nullopt;
        };
    });

    const PartiallySignedTransaction base_psbt{MakeReviewPsbt()};
    CMutableTransaction psbt_tx{UnsignedTx(base_psbt)};
    psbt_tx.vout.emplace_back(0, CScript{} << OP_RETURN << std::vector<unsigned char>{0x01, 0x02});
    PartiallySignedTransaction psbt{psbt_tx, /*version=*/2};
    psbt.inputs[0] = base_psbt.inputs[0];
    CompleteReviewPsbt(psbt);
    const QByteArray original{PsbtQmlModel::SerializePsbtRaw(psbt)};

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString source_path{WritePsbt(psbt, temp_dir, QStringLiteral("op-return.psbt"))};
    QVERIFY(!source_path.isEmpty());

    QCOMPARE(AwaitPsbtImport(*model, source_path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(model->currentTransactionCanBroadcast());
    const auto reviewed_tx = model->currentTransaction()->getWtx();
    QCOMPARE(reviewed_tx->vout.size(), size_t{2});
    QCOMPARE(reviewed_tx->vout.back().nValue, CAmount{0});
    QVERIFY(reviewed_tx->vout.back().scriptPubKey.IsUnspendable());
    QTRY_VERIFY(!model->currentTransaction()->presentationPending());
    const auto recipients = model->currentTransaction()->reviewedRecipients();
    QCOMPARE(recipients.size(), qsizetype{1});
    QCOMPARE(recipients.first().toMap().value("address").toString(), QString{VALID_MAINNET_ADDRESS});
    QCOMPARE(recipients.first().toMap().value("amountSat").toLongLong(), qint64{1'500});

    const QString saved_path{temp_dir.filePath(QStringLiteral("op-return-saved.psbt"))};
    QCOMPARE(AwaitPsbtSaveError(*model, saved_path), QString{});

    QFile saved_file{saved_path};
    QVERIFY(saved_file.open(QIODevice::ReadOnly));
    QCOMPARE(saved_file.readAll(), original);
}

void WalletQmlModelTests::importPsbtFromFile_opensUnsignedMultisigPsbtForReviewOnly()
{
    auto [wallet, model] = MakePasswordWalletModel();

    wallet->fill_psbt_fn = [wallet](const common::PSBTFillOptions& options,
                                    size_t* n_signed,
                                    PartiallySignedTransaction&,
                                    bool& complete) {
        const bool sign{options.sign};
        wallet->fill_psbt_sign_args.push_back(sign);
        if (n_signed) {
            *n_signed = 0;
        }
        complete = false;
        return std::nullopt;
    };

    CKey key1;
    CKey key2;
    CKey key3;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    key3.MakeNewKey(true);
    const CScript witness_script{GetScriptForMultisig(2, {key1.GetPubKey(), key2.GetPubKey(), key3.GetPubKey()})};

    auto psbt = MakeWitnessReviewPsbt(witness_script, 1'500);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{WritePsbt(psbt, temp_dir, QStringLiteral("unsigned-multisig.psbt"))};
    QVERIFY(!path.isEmpty());

    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(model->currentTransaction() != nullptr);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(model->currentTransactionReviewMessage(), QString("This transaction requires 2 of 3 signatures."));
}

void WalletQmlModelTests::importPsbtFromFile_blocksBroadcastWhenFeeIsInvalid()
{
    MockNode node;
    auto [wallet, model] = MakePasswordWalletModel(&node);

    CKey key1;
    CKey key2;
    key1.MakeNewKey(true);
    key2.MakeNewKey(true);
    const CScript witness_script{GetScriptForMultisig(2, {key1.GetPubKey(), key2.GetPubKey()})};

    auto psbt = MakeWitnessReviewPsbt(witness_script, 2'500);

    FillableSigningProvider provider;
    QVERIFY(provider.AddCScript(witness_script));
    QVERIFY(provider.AddKey(key1));
    QVERIFY(provider.AddKey(key2));
    const auto txdata{PrecomputePSBTData(psbt)};
    QVERIFY(txdata.has_value());
    QVERIFY(SignPSBTInput(provider, psbt, 0, &*txdata, /*options=*/{}).has_value());
    QVERIFY(FinalizePSBT(psbt));

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{WritePsbt(psbt, temp_dir, QStringLiteral("complete-with-invalid-fee.psbt"))};
    QVERIFY(!path.isEmpty());

    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::WalletCannotSign);
    QVERIFY(!model->currentTransactionCanSend());
    QVERIFY(!model->currentTransactionCanBroadcast());
    QCOMPARE(
        model->currentTransactionReviewMessage(),
        QString("The transaction fee is missing or invalid. Add valid input information before broadcasting."));

    [[maybe_unused]] auto verify_node = node.VerifyOnExit();
    node.ExpectNoCalls(node.calls.broadcastTransaction);
    QVERIFY(!AwaitPsbtBroadcast(*model));
    QCOMPARE(model->transactionError(), QString("The transaction fee is missing or invalid."));
    QCOMPARE(model->psbtActionState(), WalletQmlModel::PsbtActionState::PsbtFailed);
    QCOMPARE(wallet->commit_calls, 0);
}

void WalletQmlModelTests::importPsbtFromFile_returnsTransactionAlreadyKnownWhenTxIsInWallet_data()
{
    QTest::addColumn<bool>("tx_status_available");
    QTest::newRow("status-available") << true;
    QTest::newRow("status-lock-busy") << false;
}

void WalletQmlModelTests::importPsbtFromFile_returnsTransactionAlreadyKnownWhenTxIsInWallet()
{
    QFETCH(bool, tx_status_available);
    auto [wallet, model] = MakePasswordWalletModel();

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    mtx.vout.emplace_back(1'500, GetScriptForDestination(DecodeDestination(VALID_MAINNET_ADDRESS.toStdString())));
    PartiallySignedTransaction psbt{mtx, /*version=*/2};
    psbt.inputs[0].witness_utxo = CTxOut{2'000, CScript{}};

    const Txid psbt_txid{UnsignedTx(psbt).GetHash()};
    wallet->known_transactions.emplace(psbt_txid, MakeTransactionRef(UnsignedTx(psbt)));
    wallet->tx_status_available = tx_status_available;
    wallet->fill_psbt_sign_args.clear();

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    QString path{WritePsbt(psbt, temp_dir, QStringLiteral("already-known.psbt"))};
    QVERIFY(!path.isEmpty());

    QCOMPARE(AwaitPsbtImport(*model, path), WalletQmlModel::PsbtImportResult::TransactionAlreadyKnown);
    QCOMPARE(model->importedPsbt()->matchedTxid(), QString::fromStdString(psbt_txid.GetHex()));

    // The review flow must be skipped entirely.
    QVERIFY(wallet->fill_psbt_sign_args.empty());
    QVERIFY(model->currentTransaction() == nullptr);
    QCOMPARE(model->sendRecipientList()->count(), 1);
    QVERIFY(model->sendRecipientList()->currentRecipient()->address()->address().isEmpty());
}

void WalletQmlModelTests::replacementOutcomeQueryDoesNotBlockGuiOrRepeatTheMutation()
{
    auto [wallet, model] = MakePasswordWalletModel();
    const auto original = std::make_shared<Txid>();
    QVERIFY(ConfigurePasswordWalletOnNative(*model, [original](FakePasswordWallet& native) {
        *original = native.addBumpableTestTransaction();
        native.commit_bump_result = false;
    }));
    auto* bump = model->bumpModel();
    const auto id = QString::fromStdString(original->GetHex());
    QVERIFY(AwaitBumpPreparation(*bump, id, 1));
    const auto prepared = bump->preparationOperation()->result(); QVERIFY(prepared && prepared->value && prepared->value->review);
    const auto expected = prepared->value->review->transaction.GetHash();
    QVERIFY(!AwaitBumpCommit(*bump, QStringLiteral("secret")));
    QCOMPARE(bump->state(), BumpTransactionModel::Uncertain);
    const auto entered = std::make_shared<std::atomic_bool>(false);
    const auto thread = std::make_shared<std::atomic<QThread*>>(nullptr);
    const auto gate = std::make_shared<QSemaphore>();
    const auto release = qScopeGuard([gate] { gate->release(); });
    QVERIFY(ConfigurePasswordWalletOnNative(*model, [expected, entered, thread, gate](FakePasswordWallet& native) {
        native.transaction_details_fn = [expected, entered, thread, gate](const Txid& txid, interfaces::WalletTxStatus& status, bool& mempool) {
            if (txid == expected) {
                thread->store(QThread::currentThread());
                entered->store(true);
                gate->acquire();
            }
            status = {}; mempool = false;
            return interfaces::WalletTx{};
        };
    }));
    QVERIFY(bump->requestRecheckFeeBumpOutcome());
    QTRY_VERIFY(entered->load());
    QVERIFY(thread->load() != QCoreApplication::instance()->thread());
    const auto pulse = std::make_shared<std::atomic_bool>(false);
    QTimer::singleShot(0, this, [pulse] { pulse->store(true); });
    QTRY_VERIFY(pulse->load());
    QVERIFY(bump->busy());
    QVERIFY(!bump->requestConfirmFeeBump());
    gate->release();
    QTRY_VERIFY(!bump->busy());
    QCOMPARE(bump->state(), BumpTransactionModel::Uncertain);
    QCOMPARE(wallet->create_bump_calls, 1);
    QCOMPARE(wallet->sign_bump_calls, 1);
    QCOMPARE(wallet->commit_bump_calls, 1);
}

void WalletQmlModelTests::bumpFeePresentationTracksUnitWithoutRepreparing()
{
    const QLocale previous_locale;
    QLocale::setDefault(QLocale::c());
    const auto restore_locale = qScopeGuard([previous_locale] { QLocale::setDefault(previous_locale); });
    auto [wallet, model] = MakePasswordWalletModel();
    const auto original = wallet->addBumpableTestTransaction();
    model->setDisplayUnit(1);
    auto* bump = model->bumpModel();
    QVERIFY(AwaitBumpPreparation(*bump, QString::fromStdString(original.GetHex()), 1));
    QCOMPARE(bump->oldFee(), QStringLiteral("0.00100 mBTC"));
    QCOMPARE(bump->newFee(), QStringLiteral("0.00200 mBTC"));
    const auto attempt = bump->attemptId();
    model->setDisplayUnit(2);
    QCOMPARE(bump->oldFee(), QStringLiteral("1.00 bits"));
    QCOMPARE(bump->newFee(), QStringLiteral("2.00 bits"));
    model->setDisplayUnit(3);
    QCOMPARE(bump->oldFee(), QStringLiteral("100 sats"));
    QCOMPARE(bump->newFee(), QStringLiteral("200 sats"));
    model->setDisplayUnit(1);
    QCOMPARE(bump->oldFee(), QStringLiteral("0.00100 mBTC"));
    QCOMPARE(model->displayUnit(), 1);
    QCOMPARE(bump->attemptId(), attempt);
    QCOMPARE(wallet->create_bump_calls, 1);
    QCOMPARE(wallet->sign_bump_calls, 0);
    QCOMPARE(wallet->commit_bump_calls, 0);
}

void WalletQmlModelTests::bumpTransactionOnLockedWalletRequiresPassword()
{
    auto [wallet, model] = MakePasswordWalletModel();
    const auto original = wallet->addBumpableTestTransaction();
    auto* bump_model = model->bumpModel();

    AwaitBumpPreparation(*bump_model, QString::fromStdString(original.GetHex()), 1);
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);

    QVERIFY(!AwaitBumpCommit(*bump_model));
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);
    QVERIFY(bump_model->needsUnlock());
    QCOMPARE(bump_model->errorText(), QString("Enter your wallet password to update this transaction."));
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->sign_bump_calls, 0);
    QCOMPARE(wallet->commit_bump_calls, 0);
}

void WalletQmlModelTests::bumpTransactionWithPassphraseUnlocksCommitsAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();
    const auto original = wallet->addBumpableTestTransaction();
    auto* bump_model = model->bumpModel();

    AwaitBumpPreparation(*bump_model, QString::fromStdString(original.GetHex()), 1);
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);

    QVERIFY(AwaitBumpCommit(*bump_model, QStringLiteral("secret")));
    QCOMPARE(bump_model->state(), BumpTransactionModel::Succeeded);
    QVERIFY(!bump_model->newTxid().isEmpty());
    QVERIFY(!bump_model->needsUnlock());
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->unlock_passphrases.size(), size_t{1});
    QCOMPARE(wallet->unlock_passphrases.front(), std::string{"secret"});
    QCOMPARE(wallet->lock_calls, 1);
    QCOMPARE(wallet->sign_bump_calls, 1);
    QCOMPARE(wallet->commit_bump_calls, 1);
    QVERIFY(wallet->locked);
    const auto* const gui_thread = QCoreApplication::instance()->thread();
    QVERIFY(wallet->bump_check_thread.load() != gui_thread);
    QVERIFY(wallet->bump_prepare_thread.load() != gui_thread);
    QVERIFY(wallet->bump_sign_thread.load() != gui_thread);
    QVERIFY(wallet->bump_commit_thread.load() != gui_thread);
}

void WalletQmlModelTests::bumpTransactionWithWrongPassphraseDoesNotSign()
{
    auto [wallet, model] = MakePasswordWalletModel();
    const auto original = wallet->addBumpableTestTransaction();
    wallet->unlock_fn = [wallet](const SecureString& passphrase) {
        ++wallet->unlock_calls;
        wallet->unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("")}};
    };
    auto* bump_model = model->bumpModel();

    AwaitBumpPreparation(*bump_model, QString::fromStdString(original.GetHex()), 1);
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);

    QVERIFY(!AwaitBumpCommit(*bump_model, QStringLiteral("wrong")));
    QCOMPARE(bump_model->state(), BumpTransactionModel::NeedsConfirmation);
    QVERIFY(bump_model->needsUnlock());
    QCOMPARE(bump_model->errorText(), QString("The wallet password you entered was incorrect."));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 0);
    QCOMPARE(wallet->sign_bump_calls, 0);
    QCOMPARE(wallet->commit_bump_calls, 0);
    QVERIFY(wallet->locked);
}

void WalletQmlModelTests::signVerifyMessageRejectsNonP2PKHAddress()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!sign_verify->isLegacyP2PKHAddress(QString::fromLatin1(NON_P2PKH_ADDRESS)));
    QVERIFY(!AwaitMessageSigning(*sign_verify, QString::fromLatin1(NON_P2PKH_ADDRESS), "message"));
    QCOMPARE(sign_verify->signingError(), QString("Enter a legacy P2PKH bitcoin address."));
    QCOMPARE(wallet->sign_message_calls, 0);
}

void WalletQmlModelTests::signVerifyMessageSignsWithLegacyP2PKHAddress()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.encrypted = false;
        original.locked = false;
    });
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(AwaitMessageAddressValidation(*sign_verify, VALID_MAINNET_ADDRESS));
    QCOMPARE(sign_verify->signingAddress(), QString{VALID_MAINNET_ADDRESS});
    QVERIFY(sign_verify->signingAddressValid());
    QVERIFY(sign_verify->isLegacyP2PKHAddress(VALID_MAINNET_ADDRESS));
    QVERIFY(AwaitMessageSigning(*sign_verify, VALID_MAINNET_ADDRESS, "hello"));
    QCOMPARE(sign_verify->signingState(), SignVerifyMessageModel::SigningSucceeded);
    QCOMPARE(wallet->sign_message_calls, 1);
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
    QCOMPARE(wallet->last_signed_message, std::string("hello"));
    QCOMPARE(sign_verify->signature(), QString("fake-signature"));
    QVERIFY(sign_verify->signingError().isEmpty());
    QVERIFY(!sign_verify->signingNeedsUnlock());
}

void WalletQmlModelTests::signVerifyMessageWithPassphraseUnlocksSignsAndRelocks()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!AwaitMessageSigning(*sign_verify, VALID_MAINNET_ADDRESS, "hello"));
    QVERIFY(sign_verify->signingNeedsUnlock());
    QCOMPARE(wallet->unlock_calls, 0);

    QVERIFY(AwaitMessageSigning(*sign_verify, VALID_MAINNET_ADDRESS, "hello", "secret"));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 1);
    QVERIFY(wallet->locked);
    QCOMPARE(sign_verify->signature(), QString("fake-signature"));
}

void WalletQmlModelTests::signVerifyMessageSurfacesSigningFailure()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.encrypted = false;
        original.locked = false;
    });
    wallet->sign_message_fn = [](const std::string&, const PKHash&, std::string&) {
        return SigningResult::PRIVATE_KEY_NOT_AVAILABLE;
    };
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!AwaitMessageSigning(*sign_verify, VALID_MAINNET_ADDRESS, "hello"));
    QVERIFY(sign_verify->signature().isEmpty());
    QCOMPARE(sign_verify->signingError(), QString("Private key not available"));
}

void WalletQmlModelTests::signVerifyMessageVerifiesValidSignature()
{
    CKey key;
    key.MakeNewKey(true);
    const QString address{QString::fromStdString(EncodeDestination(PKHash(key.GetPubKey())))};
    const QString message{"hello"};
    std::string signature;
    QVERIFY(MessageSign(key, message.toStdString(), signature));

    auto [wallet, model] = MakePasswordWalletModel();
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(AwaitMessageAddressValidation(*sign_verify, address, false));
    QCOMPARE(sign_verify->verificationAddress(), address);
    QVERIFY(sign_verify->verificationAddressValid());
    QVERIFY(sign_verify->isLegacyP2PKHAddress(address));
    QVERIFY(AwaitMessageVerification(*sign_verify, address, message, QString::fromStdString(signature)));
    QCOMPARE(sign_verify->verificationState(), SignVerifyMessageModel::VerificationValid);
    QVERIFY(!AwaitMessageVerification(*sign_verify, address, message + "!", QString::fromStdString(signature)));
    QCOMPARE(sign_verify->verificationState(), SignVerifyMessageModel::VerificationInvalid);
    QVERIFY(!AwaitMessageVerification(*sign_verify, QString::fromLatin1(NON_P2PKH_ADDRESS), message, QString::fromStdString(signature)));
    QCOMPARE(sign_verify->verificationState(), SignVerifyMessageModel::VerificationInvalid);
    QCOMPARE(wallet->sign_message_calls, 0);
    QCOMPARE(wallet->unlock_calls, 0);
    QCOMPARE(wallet->lock_calls, 0);
}

void WalletQmlModelTests::signVerifyMessageSignsEmptyMessage()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.encrypted = false;
        original.locked = false;
    });
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(AwaitMessageSigning(*sign_verify, VALID_MAINNET_ADDRESS, QString()));
    QCOMPARE(wallet->sign_message_calls, 1);
    QCOMPARE(wallet->last_signed_message, std::string{});
    QCOMPARE(sign_verify->signature(), QString("fake-signature"));
    QVERIFY(sign_verify->signingError().isEmpty());
}

void WalletQmlModelTests::signVerifyMessageWrongPassphraseSurfacesErrorAndDoesNotRelock()
{
    auto [wallet, model] = MakePasswordWalletModel();
    wallet->unlock_fn = [wallet](const SecureString& passphrase) {
        ++wallet->unlock_calls;
        wallet->unlock_passphrases.emplace_back(passphrase.begin(), passphrase.end());
        return util::Unexpected{wallet::WalletError{wallet::WalletErrorCode::PassphraseIncorrect, Untranslated("")}};
    };
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!AwaitMessageSigning(*sign_verify, VALID_MAINNET_ADDRESS, "hello", "wrong"));
    QCOMPARE(wallet->unlock_calls, 1);
    QCOMPARE(wallet->lock_calls, 0);
    QCOMPARE(wallet->sign_message_calls, 0);
    QCOMPARE(sign_verify->signingError(), QString("The wallet password you entered was incorrect."));
    QVERIFY(sign_verify->signature().isEmpty());
}

void WalletQmlModelTests::signVerifyMessageWithoutWalletSurfacesError()
{
    SignVerifyMessageModel sign_verify{nullptr};

    QCOMPARE(sign_verify.requestSignMessage(VALID_MAINNET_ADDRESS, "hello"), quint64{0});
    QCOMPARE(sign_verify.signingState(), SignVerifyMessageModel::SigningFailed);
    QVERIFY(!sign_verify.signingOperation());
    QVERIFY(!sign_verify.signingBusy());
    QCOMPARE(sign_verify.signingError(), QString("No wallet is selected."));
    QVERIFY(sign_verify.signature().isEmpty());
}

void WalletQmlModelTests::signVerifyMessageClearResetsState()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.encrypted = false;
        original.locked = false;
    });
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(AwaitMessageSigning(*sign_verify, VALID_MAINNET_ADDRESS, "hello"));
    QVERIFY(!sign_verify->signature().isEmpty());

    QVERIFY(!AwaitMessageSigning(*sign_verify, QString::fromLatin1(NON_P2PKH_ADDRESS), "hello"));
    QVERIFY(!sign_verify->signingError().isEmpty());
    QVERIFY(sign_verify->signature().isEmpty());

    sign_verify->clear();
    QVERIFY(sign_verify->signature().isEmpty());
    QVERIFY(sign_verify->signingError().isEmpty());
    QVERIFY(!sign_verify->signingNeedsUnlock());
}

void WalletQmlModelTests::signVerifyMessageVerifyRejectsEmptySignature()
{
    auto [wallet, model] = MakePasswordWalletModel();
    auto* sign_verify = model->signVerifyMessageModel();

    QVERIFY(!AwaitMessageVerification(*sign_verify, VALID_MAINNET_ADDRESS, "hello", QString()));
    QVERIFY(!AwaitMessageVerification(*sign_verify, VALID_MAINNET_ADDRESS, "hello", QStringLiteral("   ")));
}

void WalletQmlModelTests::sendTransactionWithPrivateKeysDisabledDoesNotCommit()
{
    auto [wallet, model] = MakeConfiguredPasswordWalletModel([](FakePasswordWallet& original) {
        original.private_keys_disabled = true;
    });
    SetPasswordRecipient(*model, 1'000);

    QVERIFY(AwaitPreparation(*model));
    QVERIFY(wallet->locked);
    QVERIFY(!model->currentTransactionCanSend());

    QVERIFY(!AwaitCommittedSend(*model));
    QCOMPARE(model->transactionError(), QString("This wallet cannot sign transactions."));
    QCOMPARE(wallet->commit_calls, 0);
    QVERIFY(wallet->fill_psbt_sign_args.empty());
}


// #1015: a real CWallet without estimator data and with -fallbackfee prices every preset with Core's "Fallback fee".
// That reason must reach each preset's provenance and the prepared review, with no estimator target; Custom stays Explicit.
void WalletQmlModelTests::coreFallbackFeeReachesPresetsAndReview_data()
{
    QTest::addColumn<int>("target"); QTest::addColumn<bool>("custom");
    QTest::newRow("priority") << 2 << false; QTest::newRow("standard") << 6 << false; QTest::newRow("flexible") << 10 << false;
    QTest::newRow("custom") << 6 << true;
}

void WalletQmlModelTests::coreFallbackFeeReachesPresetsAndReview()
{
    QFETCH(int, target); QFETCH(bool, custom);
    ChainSelectionGuard chain_guard{ChainType::MAIN};
    auto chain = std::make_shared<FeePreviewChain>();
    const auto wallet_owner = std::shared_ptr<wallet::CWallet>{
        new wallet::CWallet{chain.get(), "core-fallback", wallet::MakeInMemoryWalletDatabase()},
        [chain](wallet::CWallet* wallet) { delete wallet; }};
    auto& raw_wallet = *wallet_owner;
    CTxDestination own_destination; COutPoint selected;
    {
        LOCK(raw_wallet.cs_wallet);
        raw_wallet.m_keypool_size = 1; raw_wallet.SetWalletFlag(wallet::WALLET_FLAG_DESCRIPTORS); raw_wallet.SetupDescriptorScriptPubKeyMans();
        const auto generated = raw_wallet.GetNewDestination(OutputType::BECH32, ""); QVERIFY(generated); own_destination = *generated;
        raw_wallet.SetLastBlockProcessed(1, uint256{});
        raw_wallet.m_min_fee = CFeeRate{1'000}; raw_wallet.m_fallback_fee = CFeeRate{20'000}; raw_wallet.m_allow_fallback_fee = true;
        CMutableTransaction funding; funding.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
        funding.vout.emplace_back(1'000'000, GetScriptForDestination(own_destination));
        const auto tx = MakeTransactionRef(funding); QVERIFY(raw_wallet.AddToWallet(tx, wallet::TxStateInMempool{}));
        selected = COutPoint{tx->GetHash(), 0};
    }
    auto env = std::make_shared<ManualExecutionEnvironment>();
    FeeReadServices services{env->scheduler, env, {}, [env](std::function<void()> publish) { env->post(std::move(publish)); }};
    const auto configure = [owner = &raw_wallet](MockWallet& wallet) {
        wallet.get_minimum_fee_fn = [owner](const wallet::CCoinControl& control) { return wallet::GetMinimumFee(wallet::GetMinimumFeeRate(*owner, control), 1000); };
        wallet.get_required_fee_fn = [owner](unsigned int bytes) { return wallet::GetRequiredFee(*owner, bytes); };
    };
    auto [wallet, model] = MakeWalletModel(nullptr, &raw_wallet, std::move(services), true, wallet_owner, configure);
    SetValidRecipient(*model, QString::fromStdString(EncodeDestination(own_destination)), env);
    // Explicit inputs keep this concrete fee contract off unrelated mempool-policy chain calls.
    QVERIFY(model->coinsListModel()->setManualInputSelection(true)); model->selectCoin(selected);
    if (custom) { model->setCustomFeeEnabled(true); model->setCustomFeeRate("20"); }
    else model->fees()->choosePreset(target);
    model->scheduleFeeEstimates();
    const auto pump = [&] { env->drain(); env->publishAll(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); };
    QVERIFY(QTest::qWaitFor([&] {
        pump(); if (model->coinSelectionPending() || !model->isSelectedCoin(selected) || model->fees()->policyPending() || !model->sendRecipientList()->allValid()) return false;
        env->advance(std::chrono::milliseconds{250}); pump();
        return !model->feeEstimatePending() && env->turnsIdle() && env->publicationsIdle();
    }, 5000));
    QVERIFY2(model->fees()->estimateFresh(), qPrintable(model->fees()->errorText()));
    QCOMPARE(model->fees()->targetBlocks(), target);
    QCOMPARE(model->fees()->provenance(), custom ? FeeSelectionModel::Explicit : FeeSelectionModel::Fallback);
    QVERIFY(!model->fees()->hasEstimatorTarget());
    QCOMPARE(model->fees()->estimatedRate(), QStringLiteral("20.000"));
    const auto preview_fee = model->estimatedFeeSatoshi(); QVERIFY(preview_fee > 0);
    QVERIFY2(AwaitPreparation(*model, {}, pump), qPrintable(model->transactionError()));
    auto* review = model->currentTransaction(); QVERIFY(review);
    QCOMPARE(review->reviewFeeSource(), static_cast<int>(custom ? FeeSelectionModel::Explicit : FeeSelectionModel::Fallback));
    QCOMPARE(review->reviewUsesFallback(), !custom);
    QCOMPARE(review->feeAmount()->satoshi(), preview_fee);
    model.reset(); env->drain(); env->publishAll();
    QVERIFY(model.get_deleter().manual->finish());
    QVERIFY(env->quiescent());
    QVERIFY2(chain->unexpected.empty(), chain->unexpected.empty() ? "" : chain->unexpected.front().c_str());
}

void WalletQmlModelTests::coreApiFeePreviews_data()
{
    QTest::addColumn<int>("scenario");
    QTest::addColumn<bool>("available");
    // #1077: maximum-fallback-disabled-custom-recovery keeps Core's no-estimate
    // cause for an address-first maximum, then recovers with Custom 5 sat/vB.
    const std::array<std::pair<const char*, int>, 14> maximum{{
        {"maximum-known-input", 0}, {"maximum-below-minimum", 1},
        {"maximum-fallback-disabled", 2}, {"maximum-fallback-allowed", 3},
        {"maximum-missing-input", 4}, {"maximum-bad-index", 5},
        {"maximum-foreign-input", 6}, {"maximum-spent-input", 7},
        {"maximum-unconfirmed-truc", 8}, {"maximum-confirmed-truc", 9},
        {"maximum-absolute-fee-limit", 10}, {"maximum-feerate-limit", 11},
        {"maximum-custom-rate", 12}, {"maximum-fallback-disabled-custom-recovery", 13}}};
    for (const auto& [name, scenario] : maximum) QTest::newRow(name) << scenario << (scenario == 0 || scenario == 1 || scenario == 3 || scenario == 9 || scenario == 12 || scenario == 13);
    const std::array<std::pair<const char*, int>, 10> insufficient{{
        {"insufficient-valid-input", 100}, {"insufficient-zero-fallback", 101},
        {"insufficient-below-minimum", 102}, {"insufficient-fallback-disabled", 103},
        {"insufficient-fallback-allowed", 104}, {"insufficient-missing-input", 105},
        {"insufficient-bad-index", 106}, {"insufficient-foreign-input", 107},
        {"insufficient-spent-input", 108}, {"sufficient-input-no-error-estimate", 109}}};
    for (const auto& [name, scenario] : insufficient) QTest::newRow(name) << scenario << (scenario == 100 || scenario == 102 || scenario == 104);
}

void WalletQmlModelTests::coreApiFeePreviews()
{
    QFETCH(int, scenario);
    QFETCH(bool, available);
    const bool maximum{scenario < 100};
    const bool mainnet{scenario == 2 || scenario == 3 || scenario == 13 || scenario == 101 || scenario == 103 || scenario == 104};
    ChainSelectionGuard chain_guard{mainnet ? ChainType::MAIN : ChainType::REGTEST};
    auto chain = std::make_shared<FeePreviewChain>();
    const auto wallet_owner = std::shared_ptr<wallet::CWallet>{
        new wallet::CWallet{chain.get(), "core-api-preview", wallet::MakeInMemoryWalletDatabase()},
        [chain](wallet::CWallet* wallet) { delete wallet; }};
    auto& raw_wallet = *wallet_owner;
    const CAmount funding_amount{1'000'000};
    CTxDestination own_destination;
    COutPoint selected;
    {
        LOCK(raw_wallet.cs_wallet);
        raw_wallet.m_keypool_size = 1;
        raw_wallet.SetWalletFlag(wallet::WALLET_FLAG_DESCRIPTORS);
        raw_wallet.SetupDescriptorScriptPubKeyMans();
        const auto generated = raw_wallet.GetNewDestination(OutputType::BECH32, "");
        QVERIFY(generated);
        own_destination = *generated;
        raw_wallet.SetLastBlockProcessed(1, uint256{});
        raw_wallet.m_min_fee = CFeeRate{scenario == 1 || scenario == 102 ? 2'000 : 1'000};
        raw_wallet.m_fallback_fee = CFeeRate{scenario == 101 ? 0 : 1'000};
        raw_wallet.m_allow_fallback_fee = !(scenario == 2 || scenario == 13 || scenario == 103 || scenario == 101);
        if (scenario == 10) raw_wallet.m_max_tx_fee = 1;
        if (scenario == 11) raw_wallet.m_max_tx_fee_rate = CFeeRate{1};
        CMutableTransaction funding;
        funding.version = scenario == 8 || scenario == 9 ? 3 : 2;
        funding.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
        const bool foreign{scenario == 6 || scenario == 107};
        const auto destination = foreign ? DecodeDestination((mainnet ? VALID_MAINNET_ADDRESS : VALID_REGTEST_ADDRESS).toStdString()) : own_destination;
        funding.vout.emplace_back(funding_amount, GetScriptForDestination(destination));
        const auto tx = MakeTransactionRef(funding);
        QVERIFY(raw_wallet.AddToWallet(tx, scenario == 9 ? wallet::TxState{wallet::TxStateConfirmed{uint256::ONE, 1, 0}} : wallet::TxState{wallet::TxStateInMempool{}}));
        selected = COutPoint{tx->GetHash(), 0};
        if (scenario == 4 || scenario == 105) selected.hash = Txid::FromUint256(uint256{42});
        if (scenario == 5 || scenario == 106) selected.n = 1;
        if (scenario == 7 || scenario == 108) {
            CMutableTransaction spend;
            spend.vin.emplace_back(selected);
            spend.vout.emplace_back(funding_amount - 1000, GetScriptForDestination(own_destination));
            QVERIFY(raw_wallet.AddToWallet(MakeTransactionRef(spend), wallet::TxStateInMempool{}));
        }
    }
    auto env = std::make_shared<ManualExecutionEnvironment>();
    FeeReadServices services{env->scheduler, env, {}, [env](std::function<void()> publish) { env->post(std::move(publish)); }};
    // These callbacks are read by startup send facts on the real executor.
    // Freeze the exact fixture policy before constructing/admitting the facade.
    const auto configure = [scenario, maximum](MockWallet& wallet) {
        wallet.get_minimum_fee_fn = [](const wallet::CCoinControl&) { return CAmount{1'000}; };
        if (scenario == 1 || scenario == 102) wallet.get_required_fee_fn = [](unsigned int bytes) { return CFeeRate{2000}.GetFee(bytes); };
        // A minimum-fee fallback is independent app policy. These cases keep
        // that accessor unavailable to exercise the original backend failure.
        if (scenario == 101 || scenario == 103) wallet.get_required_fee_fn = [](unsigned int) { return CAmount{0}; };
        if (!maximum) wallet.create_transaction_fn = [](const std::vector<wallet::CRecipient>&, const wallet::CCoinControl&, bool, int&, CAmount&) -> util::Result<CTransactionRef> { return util::Error{Untranslated("Insufficient funds")}; };
    };
    auto [wallet, model] = MakeWalletModel(nullptr, &raw_wallet, std::move(services), true, wallet_owner, configure);
    // Paying the wallet itself prevents unrelated sweep-discovery state from
    // entering this concrete fee/API contract; selected inputs remain explicit.
    SetValidRecipient(*model, QString::fromStdString(EncodeDestination(own_destination)), env);
    QVERIFY(model->coinsListModel()->setManualInputSelection(true));
    model->selectCoin(selected);
    if (!maximum) model->sendRecipientList()->currentRecipient()->amount()->setSatoshi(scenario == 109 ? 50'000 : funding_amount);
    if (scenario == 12) { model->setCustomFeeEnabled(true); model->setCustomFeeRate("2"); }
    if (maximum) model->useMaximum();
    else model->scheduleFeeEstimates();
    // Selection, typed draft validation and Native send facts publish on their
    // original executors. Advance the manual fee deadline only as that captured
    // user intent becomes ready; do not sample an intermediate Pending snapshot.
    const auto settle = [&] { return QTest::qWaitFor([&] {
        env->drain(); env->publishAll();
        if (model->coinSelectionPending() || !model->isSelectedCoin(selected) ||
            model->sendRecipientList()->currentRecipient()->validationPending() ||
            !model->sendRecipientList()->allValid()) return false;
        env->advance(std::chrono::milliseconds{250});
        env->drain(); env->publishAll();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        return !model->feeEstimatePending() && env->turnsIdle() && env->publicationsIdle();
    }, 5000); };
    QVERIFY(settle());
    if (scenario == 13) {
        // The unavailable maximum names Core's cause and stays a maximum
        // request; the Custom rate it points to then yields a valid maximum.
        QVERIFY2(model->fees()->errorText().contains("fallback", Qt::CaseInsensitive), qPrintable(model->fees()->errorText()));
        QVERIFY(model->estimatedFeeSatoshi() < 0);
        QCOMPARE(model->maximumRecipient(), model->sendRecipientList()->currentRecipient());
        model->setCustomFeeEnabled(true); model->setCustomFeeRate("5");
        QVERIFY(settle());
        QVERIFY(model->fees()->errorText().isEmpty());
        QCOMPARE(model->maximumRecipient(), model->sendRecipientList()->currentRecipient());
    }
    const auto fee = model->estimatedFeeSatoshi();
    const auto amount = model->sendRecipientList()->currentRecipient()->cAmount();
    const auto input_count = model->estimatedInputCount();
    const bool pending = model->feeEstimatePending();
    const auto cause = model->fees()->errorText();
    const auto provenance = model->fees()->provenance();
    model.reset(); env->drain(); env->publishAll();
    QVERIFY(model.get_deleter().manual->finish());
    QVERIFY(env->quiescent());
    // The real Native actor is now drained, so this strict mock call log is no
    // longer being written concurrently with the assertion.
    QVERIFY2(chain->unexpected.empty(), chain->unexpected.empty() ? "" : chain->unexpected.front().c_str());
    QVERIFY(!pending);
    QCOMPARE(fee >= 0, available);
    if (available) {
        QVERIFY(fee > 0);
        QCOMPARE(input_count, 1);
        if (maximum) QCOMPARE(amount + fee, funding_amount);
        if (scenario == 3) QCOMPARE(provenance, FeeSelectionModel::Fallback);
        if (scenario == 12 || scenario == 13) QCOMPARE(provenance, FeeSelectionModel::Explicit);
        if (scenario == 13) QVERIFY(fee >= 100 * 5);
    } else if (scenario == 2) {
        QVERIFY2(cause.contains("fallback", Qt::CaseInsensitive), qPrintable(cause));
    } else if (scenario == 101 || scenario == 103) {
        // This fixture's actual create port explicitly reports insufficient
        // funds. The app must retain that fact rather than invent a fallback.
        QCOMPARE(cause, QStringLiteral("Insufficient funds"));
    }
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(WalletQmlModelTests)
#else
QTEST_MAIN(WalletQmlModelTests)
#endif
#include <test_walletqmlmodel.moc>
