// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mockwallet.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/mocks/walletsession.h>
#include <QThread>
#include <QScopeGuard>
#include <QPointer>
#include <atomic>

#include <QVariantMap>

#include <addresstype.h>
#include <chainparams.h>
#include <key_io.h>
#include <outputtype.h>
#include <primitives/transaction.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/walletmodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <test/mocks/manualwalletdrain.h>
#include <test/mocks/receiverequesttest.h>
#include <uint256.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace {
CTxDestination TestDestination(uint8_t seed)
{
    std::vector<unsigned char> bytes(20, seed);
    return WitnessV0KeyHash{uint160{bytes}};
}

CTransactionRef TransactionWithOutputs(const std::vector<CTxDestination>& destinations, const std::vector<CAmount>& amounts)
{
    CMutableTransaction tx;
    tx.version = 2;
    for (size_t i{0}; i < destinations.size(); ++i) {
        tx.vout.emplace_back(amounts.at(i), GetScriptForDestination(destinations.at(i)));
    }
    return MakeTransactionRef(tx);
}

class TestAddressWallet final : public StubWallet
{
public:
    std::mutex m_mutex;
    std::atomic<int> reads{0};
    std::atomic<int> label_writes{0};
    const std::shared_ptr<std::atomic_int> history_reads{std::make_shared<std::atomic_int>(0)};
    std::atomic<int> coin_reads{0};
    std::atomic<QThread*> read_thread{nullptr};
    bool fail_read{false};
    std::string m_name{"test_wallet"};
    std::vector<interfaces::WalletAddress> m_addresses;
    std::map<CTxDestination, std::string> m_labels;
    interfaces::Wallet::CoinsList m_coins;
    std::set<interfaces::WalletTx> m_txs;
    CTxDestination m_next_destination{TestDestination(100)};

    std::string getWalletName() override { return m_name; }
    util::Result<CTxDestination> getNewDestination(const OutputType, const std::string& label) override
    {
        std::lock_guard lock{m_mutex};
        m_labels[m_next_destination] = label;
        m_addresses.emplace_back(m_next_destination, true, wallet::AddressPurpose::RECEIVE, label);
        return m_next_destination;
    }
    bool isSpendable(const CTxDestination&) override { return true; }
    bool setAddressBook(const CTxDestination& dest, const std::string& name, const std::optional<wallet::AddressPurpose>&) override
    {
        ++label_writes;
        std::lock_guard lock{m_mutex};
        m_labels[dest] = name;
        for (auto& address : m_addresses) {
            if (address.dest == dest) {
                address.name = name;
            }
        }
        return true;
    }
    bool delAddressBook(const CTxDestination&) override { return false; }
    bool getAddress(const CTxDestination& dest, std::string* name, wallet::AddressPurpose* purpose) override
    {
        std::lock_guard lock{m_mutex};
        for (const auto& address : m_addresses) {
            if (address.dest != dest) continue;
            if (name) *name = address.name;
            if (purpose) *purpose = address.purpose;
            return true;
        }
        return false;
    }
    std::vector<interfaces::WalletAddress> getAddresses() override { ++reads; read_thread.store(QThread::currentThread()); std::lock_guard lock{m_mutex}; if (fail_read) throw std::runtime_error("address read failed"); return m_addresses; }
    bool setAddressReceiveRequest(const CTxDestination&, const std::string&, const std::string&) override { return true; }
    std::set<interfaces::WalletTx> getWalletTxs() override { ++*history_reads; std::lock_guard lock{m_mutex}; return m_txs; }
    interfaces::Wallet::CoinsList listCoins() override { ++coin_reads; std::lock_guard lock{m_mutex}; return m_coins; }
    bool hdEnabled() override { return true; }
    bool canGetAddresses() override { return true; }
    bool taprootEnabled() override { return true; }
};

interfaces::WalletTx WalletTxFor(const std::vector<CTxDestination>& destinations, const std::vector<CAmount>& amounts, const std::vector<bool>& is_change)
{
    interfaces::WalletTx wallet_tx;
    wallet_tx.tx = TransactionWithOutputs(destinations, amounts);
    wallet_tx.txout_address = destinations;
    wallet_tx.txout_address_is_mine.assign(destinations.size(), true);
    wallet_tx.txout_is_change = is_change;
    return wallet_tx;
}

struct ManualAddressFixture {
    ManualStartupExecution worker;
    std::deque<std::function<void()>> publications;
    const std::shared_ptr<TestAddressWallet> wallet{std::make_shared<TestAddressWallet>()};
    const std::shared_ptr<std::atomic_bool> shutdown_gate{std::make_shared<std::atomic_bool>(false)};
    const std::shared_ptr<walletopening::Session> opening;
    std::unique_ptr<AddressListModel> model;
    bool admitted{false};
    ManualAddressFixture()
        : opening(std::make_shared<walletopening::Session>(shutdown_gate, [] {},
              walletopening::Session::Preferences{}, std::chrono::steady_clock::now, nullptr,
              walletopening::NativeEvents{}, WalletSessionQueues(worker.executor(), worker.executor())))
    {
        for (uint8_t seed{1}; seed <= 96; ++seed)
            wallet->m_addresses.emplace_back(TestDestination(seed), true, wallet::AddressPurpose::RECEIVE, "original");
        admitted = bool(opening->start(wallet));
        worker.drain();
        opening->take();
        model = std::make_unique<AddressListModel>();
        model->setShowUsed(true);
        model->configureSession(opening, [this](auto callback) { publications.push_back(std::move(callback)); });
    }
    bool publishNext()
    {
        if (publications.empty()) return false;
        auto turn = std::move(publications.front());
        publications.pop_front();
        turn();
        return true;
    }
    void publishAll() { while (publishNext()) {} }
    void settle()
    {
        for (int turn{0}; turn < 64 && (!worker.quiescent() || !publications.empty()); ++turn) {
            worker.drain();
            publishAll();
        }
    }
    bool finish()
    {
        if (model) model->close();
        model.reset();
        auto stopped = std::make_shared<bool>(false);
        opening->stop(false, [stopped] { *stopped = true; });
        settle();
        return *stopped && worker.quiescent() && publications.empty();
    }
    ~ManualAddressFixture()
    {
        if (!finish()) QTest::qFail("The original address observation graph did not physically drain", __FILE__, __LINE__);
    }
};
} // namespace

class AddressListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void labelEditorInventoryKeepsDirtyOriginalsAndDrainsBeforeGuiDestruction();
    void labelEditorRoundTripsItsSupportedUnicodeBoundary_data();
    void labelEditorRoundTripsItsSupportedUnicodeBoundary();
    void localeFormattingUsesTheWalletDisplayConvention_data();
    void localeFormattingUsesTheWalletDisplayConvention();
    void receiveAddressesHideUsedUntilEnabled();
    void labelsCanBeEdited();
    void changeAddressesComeFromUnspentChangeOutputs();
    void manualProjectionBudgetsAndPersistentIdentity();
    void manualStaleFailureAndQuietClose_data();
    void manualStaleFailureAndQuietClose();
    void labelRefreshPreservesInventoryAndOriginalInterest_data();
    void labelRefreshPreservesInventoryAndOriginalInterest();
    void labelRefreshFailureKeepsOriginalRowsAndRetry_data();
    void labelRefreshFailureKeepsOriginalRowsAndRetry();
    void labelSuccessCannotHideFailedInventoryRefresh();
    void labelReservationsKeepOriginalViewDuringReentrancy_data();
    void labelReservationsKeepOriginalViewDuringReentrancy();
    void closeInsideStructuralChangeKeepsBalancedOriginalRows_data();
    void closeInsideStructuralChangeKeepsBalancedOriginalRows();
    void backToBackRefreshesUpdateRowsInPlaceWithoutResettingTheModel();
};

void AddressListModelTests::initTestCase()
{
    SelectParams(ChainType::REGTEST);
}

void AddressListModelTests::receiveAddressesHideUsedUntilEnabled()
{
    auto wallet{std::make_unique<TestAddressWallet>()};
    const CTxDestination unused{TestDestination(1)};
    const CTxDestination used{TestDestination(2)};
    wallet->m_addresses = {
        {unused, true, wallet::AddressPurpose::RECEIVE, "unused"},
        {used, true, wallet::AddressPurpose::RECEIVE, "used"},
        {TestDestination(3), true, wallet::AddressPurpose::SEND, "send"},
    };

    wallet->m_txs.insert(WalletTxFor({used}, {COIN}, {false}));
    WalletModel wallet_model{std::move(wallet)};
    AddressListModel* model{wallet_model.addressListModel()};
    model->refresh();

    QTRY_COMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0), AddressListModel::LabelRole).toString(), QStringLiteral("unused"));
    QTRY_COMPARE(model->data(model->index(0), AddressListModel::DisplayAmountRole).toString(), QStringLiteral("0.00000000 BTC"));
    QCOMPARE(model->data(model->index(0), AddressListModel::HasAmountRole).toBool(), false);

    model->setShowUsed(true);
    QTRY_COMPARE(model->rowCount(), 2);
}

void AddressListModelTests::labelsCanBeEdited()
{
    auto wallet{std::make_unique<TestAddressWallet>()};
    auto* const original = wallet.get();
    const auto all_history_reads = original->history_reads;
    const CTxDestination dest{TestDestination(1)};
    wallet->m_addresses = {
        {dest, true, wallet::AddressPurpose::RECEIVE, "first label"},
    };
    WalletModel wallet_model{std::move(wallet)};
    const auto native_drained = WatchWalletNativeDrain(wallet_model);
    const auto cleanup = qScopeGuard([&] {
        wallet_model.beginShutdown();
        if (!QTest::qWaitFor([&] { return wallet_model.shutdownComplete() && *native_drained; }, 5000))
            QTest::qFail("The original label-edit wallet did not physically drain", __FILE__, __LINE__);
    });
    AddressListModel* model{wallet_model.addressListModel()};
    model->refresh();

    const QString address{QString::fromStdString(EncodeDestination(dest))};
    QTRY_COMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0), AddressListModel::LabelRole).toString(), QStringLiteral("first label"));
    QTRY_VERIFY(wallet_model.property("receiveRequestsLoaded").toBool() &&
                !wallet_model.receiveRequestReconciliationPending());
    QTRY_VERIFY(!wallet_model.transactionActivityModel()->loading());
    QTRY_VERIFY(!model->refreshPending());
    const auto native_ready = std::make_shared<std::atomic_bool>(false);
    QVERIFY(wallet_model.opening()->read([](interfaces::Wallet&) { return 0; },
        [native_ready](int) { native_ready->store(true); }));
    QTRY_VERIFY(native_ready->load());
    const auto history_before = all_history_reads->load();

    QSignalSpy resets{model, &QAbstractItemModel::modelReset};
    QSignalSpy refreshed{&wallet_model, &WalletModel::addressListChanged};
    QSignalSpy label_refresh{&wallet_model, &WalletModel::addressLabelsChanged};
    QSignalSpy inventory_refresh{&wallet_model, &WalletModel::addressInventoryChanged};
    const QPersistentModelIndex stable{model->index(0)};
    const std::unique_ptr<ReceiveRequestOperation> operation{model->setAddressLabel(address, QStringLiteral("updated label"))};
    QVERIFY(operation);
    QTRY_VERIFY(!operation->pending());
    QVERIFY(operation->labelSaved());
    QCOMPARE(operation->error(), ReceiveRequestOperation::Error::None);
    QTRY_COMPARE(model->data(model->index(0), AddressListModel::LabelRole).toString(), QStringLiteral("updated label"));
    QTRY_VERIFY(!model->refreshPending());
    QCOMPARE(refreshed.size(), 1);
    QCOMPARE(label_refresh.size(), 1);
    QCOMPARE(inventory_refresh.size(), 0);
    Q_EMIT operation->changed(); // A duplicate original receipt cannot refresh twice.
    QCOMPARE(refreshed.size(), 1);
    QCOMPARE(label_refresh.size(), 1);
    QCOMPARE(original->label_writes.load(), 1);
    QVERIFY(original->read_thread.load() != QThread::currentThread());
    QCOMPARE(resets.size(), 0);
    QVERIFY(stable.isValid());
    QCOMPARE(stable.data(AddressListModel::AddressRole).toString(), address);
    wallet_model.beginShutdown();
    QVERIFY(QTest::qWaitFor([&] { return wallet_model.shutdownComplete() && *native_drained; }, 5000));
    QCOMPARE(all_history_reads->load(), history_before);
}

void AddressListModelTests::changeAddressesComeFromUnspentChangeOutputs()
{
    const QLocale previous;
    QLocale::setDefault(QLocale{"en_US"});
    const auto restore_locale = qScopeGuard([&] { QLocale::setDefault(previous); });

    auto wallet{std::make_unique<TestAddressWallet>()};
    const CTxDestination receive{TestDestination(10)};
    const CTxDestination change{TestDestination(11)};
    const CTxDestination spent_change{TestDestination(12)};
    const CAmount change_amount{2 * COIN + COIN / 4};
    const interfaces::WalletTx change_tx{WalletTxFor({receive, change, spent_change}, {COIN, change_amount, 3 * COIN}, {false, true, true})};
    const Txid txid{change_tx.tx->GetHash()};
    wallet->m_addresses = {
        {receive, true, wallet::AddressPurpose::RECEIVE, "receive"},
    };
    interfaces::WalletTxOut change_out;
    change_out.txout = CTxOut{change_amount, GetScriptForDestination(change)};
    change_out.time = 0;

    wallet->m_txs.insert(change_tx);
    wallet->m_coins[receive].emplace_back(COutPoint{txid, 1}, change_out);
    WalletModel wallet_model{std::move(wallet)};
    AddressListModel* model{wallet_model.addressListModel()};
    model->setCategory(AddressListModel::Change);

    QTRY_COMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0), AddressListModel::AddressRole).toString(), QString::fromStdString(EncodeDestination(change)));
    QCOMPARE(model->data(model->index(0), AddressListModel::CategoryRole).toString(), QStringLiteral("change"));
    QTRY_COMPARE(model->data(model->index(0), AddressListModel::DisplayAmountRole).toString(), QStringLiteral("2.25000000 BTC"));
    QCOMPARE(model->data(model->index(0), AddressListModel::HasAmountRole).toBool(), true);

    QSignalSpy data_changed{model, &QAbstractItemModel::dataChanged};

    wallet_model.setDisplayUnit(1);
    QTRY_COMPARE(data_changed.count(), 1);
    QTRY_COMPARE(model->data(model->index(0), AddressListModel::CurrentBalanceRole).toString(), QStringLiteral("2250.00000"));
    QTRY_COMPARE(model->data(model->index(0), AddressListModel::DisplayAmountRole).toString(), QStringLiteral("2,250.00000 mBTC"));

    wallet_model.setDisplayUnit(2);
    QTRY_COMPARE(data_changed.count(), 2);
    QTRY_COMPARE(model->data(model->index(0), AddressListModel::DisplayAmountRole).toString(), QStringLiteral("2,250,000.00 bits"));

    wallet_model.setDisplayUnit(3);
    QTRY_COMPARE(data_changed.count(), 3);
    QTRY_COMPARE(model->data(model->index(0), AddressListModel::DisplayAmountRole).toString(), QStringLiteral("225,000,000 sats"));
}

void AddressListModelTests::labelEditorInventoryKeepsDirtyOriginalsAndDrainsBeforeGuiDestruction()
{
    auto wallet = std::make_unique<TestAddressWallet>(); auto* backend_wallet = wallet.get();
    for (uint8_t seed{1}; seed <= 96; ++seed) wallet->m_addresses.emplace_back(TestDestination(seed), true, wallet::AddressPurpose::RECEIVE, "original");
    WalletModel wallet_model{std::move(wallet)};
    const auto native_drained = WatchWalletNativeDrain(wallet_model);
    const auto cleanup = qScopeGuard([&] {
        wallet_model.beginShutdown();
        if (!QTest::qWaitFor([&] { return wallet_model.shutdownComplete() && *native_drained; }, 5000))
            QTest::qFail("Retained label projections held the original worker graph", __FILE__, __LINE__);
    });
    auto* addresses = wallet_model.addressListModel();
    QTRY_VERIFY(!addresses->refreshPending() && addresses->count() >= 96);
    QVERIFY(!addresses->editLabel({})); QVERIFY(!addresses->editLabel("foreign address"));
    QVERIFY(!addresses->editLabel(addresses->addressAt(0).toUpper()));
    std::vector<QPointer<TextEditSessionModel>> editors;
    for (int row{0}; row < 32; ++row) {
        auto* editor = addresses->editLabel(addresses->addressAt(row)); QVERIFY(editor);
        QCOMPARE(addresses->editLabel(addresses->addressAt(row)), editor);
        QTRY_COMPARE(editor->phase(), TextEditSessionModel::Editing);
        editor->text()->edit(QStringLiteral("draft %1").arg(row)); QTRY_VERIFY(editor->dirty());
        editors.emplace_back(editor);
    }
    QVERIFY(!addresses->editLabel(addresses->addressAt(32)));
    QVERIFY(editors.front()); QCOMPARE(editors.front()->text()->text(), QStringLiteral("draft 0"));
    QCOMPARE(backend_wallet->label_writes.load(), 0);
    editors.front()->requestClose(); QTRY_VERIFY(editors.front()->closePrompt());
    editors.front()->closePrompt()->answer(TextEditClosePrompt::Discard);
    QTRY_COMPARE(editors.front()->phase(), TextEditSessionModel::Closed);
    auto* next = addresses->editLabel(addresses->addressAt(32)); QVERIFY(next);
    QTRY_COMPARE(next->phase(), TextEditSessionModel::Editing);
    QVERIFY(editors[1]); QCOMPARE(editors[1]->text()->text(), QStringLiteral("draft 1")); QVERIFY(editors[1]->dirty());
    wallet_model.beginShutdown();
    QTRY_VERIFY(wallet_model.shutdownComplete() && *native_drained);
    // The wallet facade and its remaining editor QObjects still exist here.
    QVERIFY(editors[1]); QCOMPARE(editors[1]->phase(), TextEditSessionModel::Closed);
}

void AddressListModelTests::labelEditorRoundTripsItsSupportedUnicodeBoundary_data()
{
    QTest::addColumn<QString>("note");
    QTest::newRow("ascii") << QString(16384, QChar{'x'});
    QTest::newRow("three-byte-bmp") << QString(16384, QChar{0x20ac});
    QTest::newRow("surrogate-pairs") << QString::fromUcs4(U"\U0001f680").repeated(8192);
}
void AddressListModelTests::labelEditorRoundTripsItsSupportedUnicodeBoundary()
{
    QFETCH(QString, note); QCOMPARE(note.size(), 16384);
    auto wallet = std::make_unique<TestAddressWallet>(); auto* native = wallet.get();
    const auto destination = TestDestination(1);
    wallet->m_addresses.emplace_back(destination, true, wallet::AddressPurpose::RECEIVE, "original");
    WalletModel model{std::move(wallet)};
    const auto native_drained = WatchWalletNativeDrain(model);
    const auto cleanup = qScopeGuard([&] {
        model.beginShutdown();
        if (!QTest::qWaitFor([&] { return model.shutdownComplete() && *native_drained; }, 5000))
            QTest::qFail("Unicode note editor did not drain", __FILE__, __LINE__);
    });
    auto* addresses = model.addressListModel(); QTRY_COMPARE(addresses->count(), 1);
    auto* editor = addresses->editLabel(addresses->addressAt(0)); QVERIFY(editor);
    QTRY_COMPARE(editor->phase(), TextEditSessionModel::Editing);
    editor->text()->edit(note); editor->save(); QTRY_VERIFY(editor->saveResult().present);
    QVERIFY2(editor->saveResult().persisted, qPrintable(editor->saveResult().error.message));
    QTRY_VERIFY(!editor->busy() && editor->valid() && editor->error().code == TextEditError::None);
    QCOMPARE(editor->text()->text(), note); QCOMPARE(editor->committedText(), note);
    QCOMPARE(native->label_writes.load(), 1);
    { std::lock_guard lock{native->m_mutex}; QCOMPARE(QString::fromStdString(native->m_labels.at(destination)), note); }
    QSignalSpy refreshed{editor, &TextEditSessionModel::changed}; editor->refresh(); QTRY_VERIFY(!refreshed.isEmpty());
    QVERIFY(editor->valid()); QCOMPARE(editor->text()->text(), note);
}

void AddressListModelTests::localeFormattingUsesTheWalletDisplayConvention_data()
{
    QTest::addColumn<QString>("locale"); QTest::addColumn<int>("unit"); QTest::addColumn<QString>("expected");
    QTest::newRow("english-btc") << QString{"en_US"} << 0 << QString{"11,862.62345678 BTC"};
    QTest::newRow("german-btc") << QString{"de_DE"} << 0 << QString{"11.862,62345678 BTC"};
    QTest::newRow("english-mbtc") << QString{"en_US"} << 1 << QString{"11,862,623.45678 mBTC"};
    QTest::newRow("german-mbtc") << QString{"de_DE"} << 1 << QString{"11.862.623,45678 mBTC"};
    QTest::newRow("english-bits") << QString{"en_US"} << 2 << QString{"11,862,623,456.78 bits"};
    QTest::newRow("german-bits") << QString{"de_DE"} << 2 << QString{"11.862.623.456,78 bits"};
    QTest::newRow("english-sat") << QString{"en_US"} << 3 << QString{"1,186,262,345,678 sats"};
    QTest::newRow("german-sat") << QString{"de_DE"} << 3 << QString{"1.186.262.345.678 sats"};
}
void AddressListModelTests::localeFormattingUsesTheWalletDisplayConvention()
{
    QFETCH(QString, locale); QFETCH(int, unit); QFETCH(QString, expected);
    const QLocale previous; QLocale::setDefault(QLocale{locale});
    const auto restore_locale = qScopeGuard([&] { QLocale::setDefault(previous); });
    ManualAddressFixture fixture;
    const auto destination = TestDestination(1);
    interfaces::WalletTxOut coin; coin.txout = CTxOut{1'186'262'345'678, GetScriptForDestination(destination)};
    fixture.wallet->m_coins[destination].emplace_back(COutPoint{Txid{}, 0}, coin);
    fixture.model->setDisplayUnit(unit); fixture.settle();
    const auto address = QString::fromStdString(EncodeDestination(destination));
    int found{-1};
    for (int row{0}; row < fixture.model->count(); ++row) if (fixture.model->addressAt(row) == address) { found = row; break; }
    QVERIFY(found >= 0);
    QCOMPARE(fixture.model->data(fixture.model->index(found), AddressListModel::DisplayAmountRole).toString(), expected);
    QVERIFY(fixture.model->data(fixture.model->index(found), AddressListModel::HasAmountRole).toBool());
    QVERIFY(fixture.finish());
}

void AddressListModelTests::manualProjectionBudgetsAndPersistentIdentity()
{
    ManualStartupExecution worker, gui;
    auto wallet = std::make_shared<TestAddressWallet>();
    for (uint8_t seed = 1; seed <= 96; ++seed) wallet->m_addresses.emplace_back(TestDestination(seed), true, wallet::AddressPurpose::RECEIVE, "label");
    auto opening = MakeWalletSession(worker.executor(), [] {});
    QVERIFY(opening->start(wallet)); worker.drain(); opening->take();
    AddressListModel model;
    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
    QSignalSpy resets{&model, &QAbstractItemModel::modelReset};
    model.configureSession(opening, [&](auto turn) { QVERIFY(gui.outcomes.post(std::move(turn))); });
    QCOMPARE(wallet->reads.load(), 0); QCOMPARE(model.count(), 0);
    QVERIFY(worker.step(execution::Lane::Core)); // Public wallet reads and formatting.
    QCOMPARE(wallet->reads.load(), 1); QCOMPARE(model.count(), 0);
    QVERIFY(worker.step(execution::Lane::Utility)); // Utility owns the immutable delta plan.
    QCOMPARE(model.count(), 0);
    QVERIFY(gui.step(execution::Lane::Utility)); QCOMPARE(model.count(), 32); QCOMPARE(inserted.size(), 32);
    QVERIFY(gui.step(execution::Lane::Utility)); QCOMPARE(model.count(), 64); QCOMPARE(inserted.size(), 64);
    QVERIFY(gui.step(execution::Lane::Utility)); QCOMPARE(model.count(), 96); QCOMPARE(inserted.size(), 96);
    const auto reads = wallet->reads.load();
    const QPersistentModelIndex stable{model.index(60)};
    const auto address = stable.data(AddressListModel::AddressRole).toString();
    std::set<QString> removed;
    for (int row = 0; row < 40; ++row) removed.insert(model.addressAt(row));
    std::vector<interfaces::WalletAddress> saved;
    for (const auto& entry : wallet->m_addresses) if (removed.contains(QString::fromStdString(EncodeDestination(entry.dest)))) saved.push_back(entry);
    std::erase_if(wallet->m_addresses, [&](const auto& entry) { return removed.contains(QString::fromStdString(EncodeDestination(entry.dest))); });
    for (auto& entry : wallet->m_addresses) if (QString::fromStdString(EncodeDestination(entry.dest)) == address) entry.name = "new label";
    QCOMPARE(wallet->reads.load(), reads); // Cached getters performed no reads.
    model.refresh(); worker.drain(); gui.drain();
    QCOMPARE(model.count(), 56); QVERIFY(stable.isValid()); QCOMPARE(stable.row(), 20);
    QCOMPARE(stable.data(AddressListModel::AddressRole).toString(), address);
    QCOMPARE(stable.data(AddressListModel::LabelRole).toString(), QStringLiteral("new label"));
    wallet->m_addresses.insert(wallet->m_addresses.end(), saved.begin(), saved.end());
    model.refresh(); worker.drain(); gui.drain();
    QCOMPARE(model.count(), 96); QVERIFY(stable.isValid()); QCOMPARE(stable.row(), 60);
    QCOMPARE(stable.data(AddressListModel::AddressRole).toString(), address);
    const auto complete = wallet->m_addresses;
    std::set<QString> survivors;
    for (int row = 0; row < model.count(); row += 2) survivors.insert(model.addressAt(row));
    std::erase_if(wallet->m_addresses, [&](const auto& entry) {
        return !survivors.contains(QString::fromStdString(EncodeDestination(entry.dest)));
    });
    model.refresh(); worker.drain(); gui.drain();
    QCOMPARE(model.count(), 48); QVERIFY(stable.isValid()); QCOMPARE(stable.row(), 30);
    QCOMPARE(stable.data(AddressListModel::AddressRole).toString(), address);
    model.refresh(); worker.drain(); gui.drain(); // Clone an unchanged retained version.
    QCOMPARE(model.count(), 48); QVERIFY(stable.isValid());
    wallet->m_addresses = complete;
    model.refresh(); worker.drain(); gui.drain();
    QCOMPARE(model.count(), 96); QVERIFY(stable.isValid()); QCOMPARE(stable.row(), 60);
    wallet->m_addresses.clear(); model.refresh(); worker.drain(); gui.drain();
    QCOMPARE(model.count(), 0); QVERIFY(!stable.isValid());
    QCOMPARE(resets.size(), 0);
    model.close(); opening->stop(false, {}); worker.drain(); gui.drain();
    QVERIFY(worker.quiescent()); QVERIFY(gui.quiescent());
}

void AddressListModelTests::manualStaleFailureAndQuietClose_data()
{
    QTest::addColumn<int>("scenario");
    QTest::newRow("stale-filter") << 0;
    QTest::newRow("transport-rejection") << 1;
    QTest::newRow("read-failure") << 2;
    QTest::newRow("close-during-batched-publication") << 3;
}
void AddressListModelTests::manualStaleFailureAndQuietClose()
{
    QFETCH(int, scenario);
    ManualStartupExecution worker, gui;
    auto wallet = std::make_shared<TestAddressWallet>();
    for (uint8_t seed = 1; seed <= 96; ++seed) wallet->m_addresses.emplace_back(TestDestination(seed), true, wallet::AddressPurpose::RECEIVE, "label");
    auto opening = MakeWalletSession(worker.executor(), [] {});
    QVERIFY(opening->start(wallet)); worker.drain(); opening->take();
    AddressListModel model;
    QSignalSpy inserted{&model, &QAbstractItemModel::rowsInserted};
    QSignalSpy count_changed{&model, &AddressListModel::countChanged};
    QSignalSpy errors{&model, &AddressListModel::readErrorChanged};
    model.configureSession(opening, [&](auto turn) { QVERIFY(gui.outcomes.post(std::move(turn))); });
    worker.drain();
    if (scenario == 0) {
        model.setCategory(AddressListModel::Change);
        worker.drain(); gui.drain();
        QCOMPARE(wallet->reads.load(), 2); QCOMPARE(model.count(), 0); QCOMPARE(inserted.size(), 0);
    } else if (scenario == 3) {
        QVERIFY(gui.step(execution::Lane::Utility)); QCOMPARE(model.count(), 32);
        const auto notices = inserted.size(); const auto counts = count_changed.size(); const auto failures = errors.size();
        model.close();
        QCOMPARE(inserted.size(), notices); QCOMPARE(count_changed.size(), counts); QCOMPARE(errors.size(), failures);
        worker.drain(); gui.drain();
        QCOMPARE(inserted.size(), notices); QCOMPARE(count_changed.size(), counts); QCOMPARE(errors.size(), failures);
    } else {
        gui.drain(); QCOMPARE(model.count(), 96);
        const auto reads = wallet->reads.load();
        if (scenario == 1) worker.reject_core = true;
        else wallet->fail_read = true;
        model.refresh(); worker.drain(); gui.drain();
        QCOMPARE(model.count(), 96); QVERIFY(!model.readError().isEmpty());
        QCOMPARE(wallet->reads.load(), reads + (scenario == 2 ? 1 : 0));
        wallet->fail_read = false;
        model.refresh(); worker.drain(); gui.drain();
        QCOMPARE(model.count(), 96); QVERIFY(model.readError().isEmpty());
    }
    model.close(); opening->stop(false, {}); worker.drain(); gui.drain();
    QVERIFY(worker.quiescent()); QVERIFY(gui.quiescent());
}

void AddressListModelTests::labelRefreshPreservesInventoryAndOriginalInterest_data()
{
    QTest::addColumn<int>("boundary");
    QTest::newRow("labels-preserve-cached-inventory") << 0;
    QTest::newRow("full-before-repeated-label-interest") << 1;
    QTest::newRow("labels-before-full") << 2;
    QTest::newRow("held-label-outcome-before-full") << 3;
    QTest::newRow("close-before-label-native-read") << 4;
    QTest::newRow("close-during-label-publication") << 5;
    QTest::newRow("held-label-outcome-before-category-replacement") << 6;
    QTest::newRow("held-label-outcome-before-repeated-label-interest") << 7;
}

void AddressListModelTests::labelRefreshPreservesInventoryAndOriginalInterest()
{
    QFETCH(int, boundary);
    ManualAddressFixture f;
    QVERIFY(f.admitted);
    f.settle();
    QCOMPARE(f.model->count(), 96);
    QVERIFY(!f.model->refreshPending());
    const auto history_before = f.wallet->history_reads->load();
    const auto coins_before = f.wallet->coin_reads.load();
    const auto addresses_before = f.wallet->reads.load();
    const auto target = TestDestination(1);
    const auto used = TestDestination(2);
    const auto target_address = QString::fromStdString(EncodeDestination(target));
    const auto used_address = QString::fromStdString(EncodeDestination(used));
    const auto find = [&](const QString& address) {
        for (int row{0}; row < f.model->count(); ++row) {
            if (f.model->addressAt(row) == address) return f.model->index(row);
        }
        return QModelIndex{};
    };
    const QPersistentModelIndex original_target{find(target_address)};
    QVERIFY(original_target.isValid());
    QSignalSpy changes{f.model.get(), &QAbstractItemModel::dataChanged};
    QSignalSpy resets{f.model.get(), &QAbstractItemModel::modelReset};
    QSignalSpy pending{f.model.get(), &AddressListModel::refreshStateChanged};
    {
        std::lock_guard lock{f.wallet->m_mutex};
        for (auto& address : f.wallet->m_addresses) address.name = "new label";
        f.wallet->m_addresses.emplace_back(TestDestination(97), true, wallet::AddressPurpose::RECEIVE, "new arrival");
        const auto transaction = WalletTxFor({target, used}, {40'000, 1}, {true, false});
        f.wallet->m_txs.insert(transaction);
        f.wallet->m_coins[target].emplace_back(COutPoint{transaction.tx->GetHash(), 0},
            interfaces::WalletTxOut{transaction.tx->vout[0], 0, 6, false});
    }
    if (boundary == 1) {
        f.model->refresh();
        f.model->refreshLabels();
        f.model->refreshLabels();
        f.model->refreshLabels();
    } else {
        f.model->refreshLabels();
        if (boundary == 2) f.model->refresh();
    }
    QVERIFY(f.model->refreshPending());
    QCOMPARE(changes.size(), 0);
    QCOMPARE(f.wallet->history_reads->load(), history_before);
    QCOMPARE(f.wallet->coin_reads.load(), coins_before);
    if (boundary == 4) {
        const auto notices = pending.size();
        f.model->close();
        QVERIFY(!f.model->refreshPending());
        QCOMPARE(pending.size(), notices + 1); // One coherent explicit close outcome.
        f.settle();
        QCOMPARE(changes.size(), 0);
        QCOMPARE(pending.size(), notices + 1); // No late Native/Domain/GUI notification.
        QCOMPARE(f.wallet->reads.load(), addresses_before);
        QCOMPARE(f.wallet->history_reads->load(), history_before);
        QCOMPARE(f.wallet->coin_reads.load(), coins_before);
        QVERIFY(f.finish());
        return;
    }
    f.worker.drain();
    QVERIFY(f.model->refreshPending()); // Actual Native/Domain outcome held before GUI.
    QCOMPARE(changes.size(), 0);
    if (boundary == 7) {
        f.model->refreshLabels();
        const auto reads = f.wallet->reads.load();
        QVERIFY(f.publishNext()); // Superseded original receipt only.
        QCOMPARE(changes.size(), 0);
        QVERIFY(f.model->readError().isEmpty());
        QVERIFY(f.model->refreshPending());
        QCOMPARE(f.wallet->reads.load(), reads);
        QCOMPARE(f.wallet->history_reads->load(), history_before);
        QCOMPARE(f.wallet->coin_reads.load(), coins_before);
        QCOMPARE(f.publications.size(), size_t{1}); // Successor is a separate GUI turn.
        QVERIFY(f.publishNext());
        QCOMPARE(changes.size(), 0);
        QVERIFY(f.model->refreshPending());
        QCOMPARE(f.wallet->reads.load(), reads); // Admission alone cannot perform Native reads.
        f.worker.drain();
    }
    if (boundary == 3 || boundary == 6) {
        if (boundary == 6) f.model->setCategory(AddressListModel::Change);
        else f.model->refresh();
        f.publishAll(); // Only the superseded original label notice exists.
        QCOMPARE(changes.size(), 0);
        QVERIFY(f.model->refreshPending());
        QCOMPARE(f.wallet->history_reads->load(), history_before);
        QCOMPARE(f.wallet->coin_reads.load(), coins_before);
        f.worker.drain();
    }
    if (boundary == 5) {
        QVERIFY(f.publishNext());
        QCOMPARE(changes.size(), 32);
        QVERIFY(f.model->refreshPending());
        const auto notices = pending.size();
        f.model->close();
        QVERIFY(!f.model->refreshPending());
        QCOMPARE(pending.size(), notices + 1);
        f.settle();
        QCOMPARE(changes.size(), 32);
        QCOMPARE(pending.size(), notices + 1);
        QCOMPARE(f.wallet->history_reads->load(), history_before);
        QCOMPARE(f.wallet->coin_reads.load(), coins_before);
        QVERIFY(f.finish());
        return;
    }
    if (boundary == 0) {
        QVERIFY(f.publishNext());
        QCOMPARE(changes.size(), 32);
        QVERIFY(f.model->refreshPending());
        QVERIFY(f.publishNext());
        QCOMPARE(changes.size(), 64);
        QVERIFY(f.model->refreshPending());
    }
    f.settle();
    QVERIFY(!f.model->refreshPending());
    QVERIFY(f.model->readError().isEmpty());
    QCOMPARE(resets.size(), 0);
    QVERIFY(original_target.isValid());
    QCOMPARE(original_target.data(AddressListModel::AddressRole).toString(), target_address);
    QCOMPARE(original_target.data(AddressListModel::LabelRole).toString(), QStringLiteral("new label"));
    if (boundary == 0 || boundary == 7) {
        QCOMPARE(f.model->count(), 96);
        QCOMPARE(changes.size(), 96);
        QVERIFY(!find(QString::fromStdString(EncodeDestination(TestDestination(97)))).isValid());
        QCOMPARE(find(used_address).data(AddressListModel::UsedRole).toBool(), false);
        QCOMPARE(original_target.data(AddressListModel::HasAmountRole).toBool(), false);
        QCOMPARE(f.wallet->history_reads->load(), history_before);
        QCOMPARE(f.wallet->coin_reads.load(), coins_before);
        QCOMPARE(f.wallet->reads.load(), addresses_before + (boundary == 7 ? 2 : 1));
    } else {
        QCOMPARE(f.model->count(), boundary == 6 ? 1 : 97);
        QCOMPARE(original_target.data(AddressListModel::HasAmountRole).toBool(), true);
        QCOMPARE(original_target.data(AddressListModel::DisplayAmountRole).toString(), QStringLiteral("0.00040000 BTC"));
        if (boundary == 6) QCOMPARE(original_target.data(AddressListModel::CategoryRole).toString(), QStringLiteral("change"));
        else QVERIFY(find(used_address).data(AddressListModel::UsedRole).toBool());
        QCOMPARE(f.wallet->history_reads->load(), history_before + 1);
        QCOMPARE(f.wallet->coin_reads.load(), coins_before + 1);
    }
    QVERIFY(f.finish());
}

void AddressListModelTests::labelRefreshFailureKeepsOriginalRowsAndRetry_data()
{
    QTest::addColumn<int>("failure");
    QTest::newRow("authoritative-address-read-throws") << 0;
    QTest::newRow("native-submitted-rejected") << 1;
    QTest::newRow("domain-planning-submitted-rejected") << 2;
}

void AddressListModelTests::labelRefreshFailureKeepsOriginalRowsAndRetry()
{
    QFETCH(int, failure);
    ManualAddressFixture f;
    QVERIFY(f.admitted);
    f.settle();
    QCOMPARE(f.model->count(), 96);
    QVERIFY(!f.model->refreshPending());
    const QPersistentModelIndex original{f.model->index(40)};
    const auto address = original.data(AddressListModel::AddressRole).toString();
    const auto history_before = f.wallet->history_reads->load();
    const auto coins_before = f.wallet->coin_reads.load();
    const auto addresses_before = f.wallet->reads.load();
    {
        std::lock_guard lock{f.wallet->m_mutex};
        for (auto& entry : f.wallet->m_addresses) entry.name = "retried label";
        f.wallet->fail_read = failure == 0;
    }
    QSignalSpy changes{f.model.get(), &QAbstractItemModel::dataChanged};
    QSignalSpy errors{f.model.get(), &AddressListModel::readErrorChanged};
    QSignalSpy resets{f.model.get(), &QAbstractItemModel::modelReset};
    if (failure == 1) f.worker.reject_core = true;
    f.model->refreshLabels();
    QVERIFY(f.model->refreshPending());
    if (failure == 2) {
        QVERIFY(f.worker.step(execution::Lane::Core)); // Actual public getAddresses completes.
        QCOMPARE(f.wallet->reads.load(), addresses_before + 1);
        QCOMPARE(changes.size(), 0);
        f.shutdown_gate->store(true); // Refuse the subsequent Domain planning admission.
    }
    f.worker.drain();
    f.shutdown_gate->store(false);
    QVERIFY(f.model->refreshPending());
    QCOMPARE(changes.size(), 0);
    QVERIFY(f.model->readError().isEmpty()); // Failure receipt is still held.
    f.publishAll();
    QVERIFY(!f.model->refreshPending());
    QVERIFY(!f.model->readError().isEmpty());
    QCOMPARE(errors.size(), 1);
    QCOMPARE(f.model->count(), 96);
    QVERIFY(original.isValid());
    QCOMPARE(original.data(AddressListModel::AddressRole).toString(), address);
    QCOMPARE(original.data(AddressListModel::LabelRole).toString(), QStringLiteral("original"));
    QCOMPARE(f.wallet->reads.load(), addresses_before + (failure == 1 ? 0 : 1));
    QCOMPARE(f.wallet->history_reads->load(), history_before);
    QCOMPARE(f.wallet->coin_reads.load(), coins_before);
    {
        std::lock_guard lock{f.wallet->m_mutex};
        f.wallet->fail_read = false;
    }
    f.model->refreshLabels();
    QVERIFY(f.model->refreshPending());
    f.worker.drain();
    QCOMPARE(original.data(AddressListModel::LabelRole).toString(), QStringLiteral("original"));
    QVERIFY(f.model->refreshPending());
    f.settle();
    QVERIFY(!f.model->refreshPending());
    QVERIFY(f.model->readError().isEmpty());
    QCOMPARE(errors.size(), 2);
    QCOMPARE(changes.size(), 96);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(f.model->count(), 96);
    QVERIFY(original.isValid());
    QCOMPARE(original.data(AddressListModel::AddressRole).toString(), address);
    QCOMPARE(original.data(AddressListModel::LabelRole).toString(), QStringLiteral("retried label"));
    QCOMPARE(f.wallet->reads.load(), addresses_before + (failure == 1 ? 1 : 2));
    QCOMPARE(f.wallet->history_reads->load(), history_before);
    QCOMPARE(f.wallet->coin_reads.load(), coins_before);
    QVERIFY(f.finish());
}

void AddressListModelTests::labelSuccessCannotHideFailedInventoryRefresh()
{
    ManualAddressFixture f;
    QVERIFY(f.admitted);
    f.settle();
    QCOMPARE(f.model->count(), 96);
    QVERIFY(!f.model->refreshPending());
    const QPersistentModelIndex original{f.model->index(40)};
    const auto address = original.data(AddressListModel::AddressRole).toString();
    const auto history_before = f.wallet->history_reads->load();
    const auto coins_before = f.wallet->coin_reads.load();
    const auto addresses_before = f.wallet->reads.load();
    {
        std::lock_guard lock{f.wallet->m_mutex};
        f.wallet->fail_read = true;
    }
    f.model->refresh();
    f.settle();
    QVERIFY(!f.model->refreshPending());
    const auto inventory_error = f.model->readError();
    QVERIFY(!inventory_error.isEmpty());
    QCOMPARE(f.model->count(), 96);
    QCOMPARE(f.wallet->reads.load(), addresses_before + 1);
    {
        std::lock_guard lock{f.wallet->m_mutex};
        f.wallet->fail_read = false;
        for (auto& entry : f.wallet->m_addresses) entry.name = "new label";
        f.wallet->m_addresses.emplace_back(TestDestination(97), true, wallet::AddressPurpose::RECEIVE, "new arrival");
    }
    f.model->refreshLabels();
    QVERIFY(f.model->refreshPending());
    f.settle();
    QVERIFY(!f.model->refreshPending());
    QCOMPARE(f.model->readError(), inventory_error);
    QCOMPARE(f.model->count(), 96); // Labels cannot certify fresh membership.
    QVERIFY(original.isValid());
    QCOMPARE(original.data(AddressListModel::AddressRole).toString(), address);
    QCOMPARE(original.data(AddressListModel::LabelRole).toString(), QStringLiteral("new label"));
    QCOMPARE(f.wallet->reads.load(), addresses_before + 2);
    QCOMPARE(f.wallet->history_reads->load(), history_before);
    QCOMPARE(f.wallet->coin_reads.load(), coins_before);
    f.model->refresh();
    QVERIFY(f.model->refreshPending());
    f.settle();
    QVERIFY(!f.model->refreshPending());
    QVERIFY(f.model->readError().isEmpty());
    QCOMPARE(f.model->count(), 97);
    QVERIFY(original.isValid());
    QCOMPARE(original.data(AddressListModel::AddressRole).toString(), address);
    QCOMPARE(f.wallet->reads.load(), addresses_before + 3);
    QCOMPARE(f.wallet->history_reads->load(), history_before + 1);
    QCOMPARE(f.wallet->coin_reads.load(), coins_before + 1);
    QVERIFY(f.finish());
}

void AddressListModelTests::labelReservationsKeepOriginalViewDuringReentrancy_data()
{
    QTest::addColumn<bool>("read_completed");
    QTest::addColumn<int>("action");
    for (bool read_completed : {false, true}) {
        for (int action : {0, 1, 2}) {
            const char* operation = action == 0 ? "close" : action == 1 ? "delete" : "new-category";
            const QByteArray name = QByteArray(operation) + (read_completed ? "-after-read" : "-before-read");
            QTest::newRow(name.constData()) << read_completed << action;
        }
    }
}

void AddressListModelTests::labelReservationsKeepOriginalViewDuringReentrancy()
{
    QFETCH(bool, read_completed);
    QFETCH(int, action);
    ManualAddressFixture f;
    QVERIFY(f.admitted);
    f.settle();
    QCOMPARE(f.model->count(), 96);
    QVERIFY(!f.model->refreshPending());
    const auto addresses_before = f.wallet->reads.load();
    const auto history_before = f.wallet->history_reads->load();
    const auto coins_before = f.wallet->coin_reads.load();
    const auto target = TestDestination(1);
    const auto address = QString::fromStdString(EncodeDestination(target));
    {
        std::lock_guard lock{f.wallet->m_mutex};
        for (auto& entry : f.wallet->m_addresses) entry.name = "new category label";
        const auto transaction = WalletTxFor({target}, {40'000}, {true});
        f.wallet->m_txs.insert(transaction);
        f.wallet->m_coins[target].emplace_back(COutPoint{transaction.tx->GetHash(), 0},
            interfaces::WalletTxOut{transaction.tx->vout[0], 0, 6, false});
    }
    const QPointer<AddressListModel> original{f.model.get()};
    QSignalSpy changes{f.model.get(), &QAbstractItemModel::dataChanged};
    QSignalSpy errors{f.model.get(), &AddressListModel::readErrorChanged};
    f.model->refreshLabels();
    if (read_completed) QVERIFY(f.worker.step(execution::Lane::Core));
    const auto expected_reads = addresses_before + int(read_completed);
    QCOMPARE(f.wallet->reads.load(), expected_reads);
    if (action == 0) f.model->close();
    else if (action == 1) f.model.reset();
    else f.model->setCategory(AddressListModel::Change);
    QCOMPARE(changes.size(), 0);
    if (action == 2) {
        QVERIFY(original);
        QCOMPARE(f.model->category(), AddressListModel::Change);
        QVERIFY(f.model->refreshPending());
        f.publishAll();
        QCOMPARE(changes.size(), 0);
        QVERIFY(f.model->refreshPending());
        f.worker.drain();
        QVERIFY(f.model->refreshPending());
        f.settle();
        QVERIFY(!f.model->refreshPending());
        QVERIFY(f.model->readError().isEmpty());
        QCOMPARE(f.model->count(), 1);
        QCOMPARE(f.model->addressAt(0), address);
        QCOMPARE(f.model->data(f.model->index(0), AddressListModel::LabelRole).toString(), QStringLiteral("new category label"));
        QCOMPARE(f.model->data(f.model->index(0), AddressListModel::CategoryRole).toString(), QStringLiteral("change"));
        QCOMPARE(f.wallet->reads.load(), expected_reads + 1);
        QCOMPARE(f.wallet->history_reads->load(), history_before + 1);
        QCOMPARE(f.wallet->coin_reads.load(), coins_before + 1);
    } else {
        QCOMPARE(bool(original), action == 0);
        if (original) QVERIFY(!original->refreshPending());
        f.settle();
        QCOMPARE(changes.size(), 0);
        QCOMPARE(errors.size(), 0);
        QCOMPARE(f.wallet->reads.load(), expected_reads);
        QCOMPARE(f.wallet->history_reads->load(), history_before);
        QCOMPARE(f.wallet->coin_reads.load(), coins_before);
    }
    QVERIFY(f.finish());
}

void AddressListModelTests::closeInsideStructuralChangeKeepsBalancedOriginalRows_data()
{
    QTest::addColumn<bool>("insert");
    QTest::newRow("close-during-about-to-insert") << true;
    QTest::newRow("close-during-about-to-remove") << false;
}

void AddressListModelTests::closeInsideStructuralChangeKeepsBalancedOriginalRows()
{
    QFETCH(bool, insert);
    int about{0}, completed{0}, before_close{-1}, after_close{-1}, completed_rows{-1};
    ManualAddressFixture f;
    QVERIFY(f.admitted);
    f.settle();
    QCOMPARE(f.model->count(), 96);
    {
        std::lock_guard lock{f.wallet->m_mutex};
        if (insert) f.wallet->m_addresses.emplace_back(TestDestination(97), true, wallet::AddressPurpose::RECEIVE, "new arrival");
        else {
            const auto removed = f.model->addressAt(0);
            std::erase_if(f.wallet->m_addresses, [&](const auto& entry) {
                return QString::fromStdString(EncodeDestination(entry.dest)) == removed;
            });
        }
    }
    const auto about_callback = [&](const QModelIndex&, int, int) {
        ++about;
        before_close = f.model->rowCount();
        f.model->close();
        after_close = f.model->rowCount();
    };
    const auto completed_callback = [&](const QModelIndex&, int, int) {
        ++completed;
        completed_rows = f.model->rowCount();
    };
    QMetaObject::Connection begin, end;
    if (insert) {
        begin = connect(f.model.get(), &QAbstractItemModel::rowsAboutToBeInserted, this, about_callback);
        end = connect(f.model.get(), &QAbstractItemModel::rowsInserted, this, completed_callback);
    } else {
        begin = connect(f.model.get(), &QAbstractItemModel::rowsAboutToBeRemoved, this, about_callback);
        end = connect(f.model.get(), &QAbstractItemModel::rowsRemoved, this, completed_callback);
    }
    const auto disconnect_callbacks = qScopeGuard([&] { disconnect(begin); disconnect(end); });
    QSignalSpy counts{f.model.get(), &AddressListModel::countChanged};
    QSignalSpy changes{f.model.get(), &QAbstractItemModel::dataChanged};
    QSignalSpy resets{f.model.get(), &QAbstractItemModel::modelReset};
    f.model->refresh();
    f.worker.drain();
    QCOMPARE(about, 0);
    QVERIFY(f.publishNext());
    QCOMPARE(about, 1);
    QCOMPARE(completed, 1);
    QCOMPARE(before_close, 96);
    QCOMPARE(after_close, 96); // begin/end still describe the original rows.
    QCOMPARE(completed_rows, insert ? 97 : 95); // One balanced actual structural change.
    QVERIFY(!f.model->refreshPending());
    const auto count_notices = counts.size();
    f.settle();
    QCOMPARE(about, 1);
    QCOMPARE(completed, 1);
    QCOMPARE(counts.size(), count_notices);
    QCOMPARE(changes.size(), 0);
    QCOMPARE(resets.size(), 0);
    QVERIFY(f.finish());
}

void AddressListModelTests::backToBackRefreshesUpdateRowsInPlaceWithoutResettingTheModel()
{
    ManualAddressFixture f;
    QVERIFY(f.admitted);
    f.settle();
    const int initial_count = f.model->count();
    QVERIFY(initial_count > 0);
    const auto target_address = f.model->addressAt(0);
    QSignalSpy resets{f.model.get(), &QAbstractItemModel::modelReset};
    QSignalSpy changes{f.model.get(), &QAbstractItemModel::dataChanged};
    for (const auto* updated_label : {"First label update", "Second label update"}) {
        {
            std::lock_guard lock{f.wallet->m_mutex};
            for (auto& entry : f.wallet->m_addresses) {
                if (QString::fromStdString(EncodeDestination(entry.dest)) == target_address) {
                    entry.name = updated_label;
                    f.wallet->m_labels[entry.dest] = updated_label;
                }
            }
        }
        f.model->refresh();
        f.model->refresh();
        f.settle();
        QCOMPARE(f.model->count(), initial_count);
        QCOMPARE(f.model->data(f.model->index(0), AddressListModel::LabelRole).toString(), QString::fromLatin1(updated_label));
    }
    QCOMPARE(resets.size(), 0);
    QVERIFY(changes.size() >= 2);
    QVERIFY(f.finish());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(AddressListModelTests)
#else
QTEST_MAIN(AddressListModelTests)
#endif
#include <test_addresslistmodel.moc>
