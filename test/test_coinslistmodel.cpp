// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <QtTest/QtTest>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>
#include <qml/models/walletqmlmodel.h>
#include <key_io.h>
#include <addresstype.h>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <atomic>
#include <mutex>
#include <set>
#include <stdexcept>

namespace {
CTxDestination CoinDestination(uint8_t seed) { return WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, seed)}}; }
struct CoinGate {
    QSemaphore entered;
    QSemaphore release;
    void wait()
    {
        entered.release();
        // Bound failures if a regression runs the backend on the GUI thread.
        release.tryAcquire(1, 5000);
    }
};
class CoinWallet final : public StubWallet {
public:
    std::mutex mutex;
    CoinsList coins;
    std::set<COutPoint> locked;
    std::map<CTxDestination, std::string> labels;
    std::atomic_int lock_calls{0};
    std::atomic_int unlock_calls{0};
    std::atomic_int gui_calls{0};
    std::atomic_bool fail_writes{false};
    std::atomic_bool throw_read{false};
    QThread* gui_thread{QThread::currentThread()};
    std::shared_ptr<CoinGate> write_gate;
    std::shared_ptr<CoinGate> read_gate;
    void checkThread() { if (QThread::currentThread() == gui_thread) ++gui_calls; }
    CoinsList listCoins() override { checkThread(); std::lock_guard<std::mutex> lock{mutex}; return coins; }
    bool isLockedCoin(const COutPoint& p) override { std::lock_guard<std::mutex> lock{mutex}; return locked.contains(p); }
    void listLockedCoins(std::vector<COutPoint>& outputs) override {
        checkThread();
        std::shared_ptr<CoinGate> gate;
        {
            std::lock_guard<std::mutex> lock{mutex};
            outputs.assign(locked.begin(), locked.end());
            gate = std::move(read_gate);
        }
        if (gate) gate->wait();
        if (throw_read.exchange(false)) throw std::runtime_error{"snapshot failed"};
    }
    std::vector<interfaces::WalletAddress> getAddresses() override {
        std::lock_guard<std::mutex> lock{mutex};
        std::vector<interfaces::WalletAddress> result;
        result.reserve(labels.size());
        for (const auto& [address, label] : labels) result.emplace_back(address, true, wallet::AddressPurpose::RECEIVE, label);
        return result;
    }
    bool setLocked(const COutPoint& p, bool value) {
        checkThread();
        std::shared_ptr<CoinGate> gate;
        {
            std::lock_guard<std::mutex> lock{mutex};
            gate = std::move(write_gate);
        }
        if (gate) gate->wait();
        std::lock_guard<std::mutex> lock{mutex};
        if (value) locked.insert(p); else locked.erase(p);
        return !fail_writes;
    }
    bool lockCoin(const COutPoint& p, bool) override { ++lock_calls; return setLocked(p, true); }
    bool unlockCoin(const COutPoint& p) override { ++unlock_calls; return setLocked(p, false); }
    bool getAddress(const CTxDestination& d, std::string* name, wallet::AddressPurpose*) override {
        std::lock_guard<std::mutex> lock{mutex};
        if (!labels.count(d)) return false;
        if (name) *name = labels[d];
        return true;
    }
    void add(int n, CAmount amount, int64_t date, uint8_t address, std::string label) {
        std::lock_guard<std::mutex> lock{mutex};
        const auto dest = CoinDestination(address);
        labels[dest] = label;
        // Deliberately group change under a different ancestor address.
        coins[CoinDestination(99)].emplace_back(COutPoint{Txid{}, static_cast<uint32_t>(n)}, interfaces::WalletTxOut{CTxOut{amount, GetScriptForDestination(dest)}, date, 6, false});
    }
};
}
class CoinsListModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void lockRequestsStayPendingThroughReadback() {
        auto wallet = std::make_shared<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "First");
        WalletQmlModel model{wallet};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        coins->toggleCoinSelection(0);
        auto write = std::make_shared<CoinGate>();
        auto read = std::make_shared<CoinGate>();
        const auto release = qScopeGuard([&] { write->release.release(); read->release.release(); });
        {
            std::lock_guard<std::mutex> lock{wallet->mutex};
            wallet->write_gate = write;
            wallet->read_gate = read;
        }
        QSignalSpy pending{coins, &CoinsListModel::lockOperationStateChanged};
        bool applied_while_pending{false};
        connect(&model, &WalletQmlModel::walletStateChanged, this, [&] {
            if (coins->lockedSatoshi() != 1000) return;
            applied_while_pending = coins->lockOperationPending();
            QVERIFY(!coins->setCoinsLocked({id}, false));
        });
        QVERIFY(coins->setCoinsLocked({id}, true));
        QVERIFY(coins->lockOperationPending());
        QVERIFY(!coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(write->entered.available());
        bool gui_progress{false};
        QTimer::singleShot(0, this, [&] { gui_progress = true; });
        QTRY_VERIFY(gui_progress);
        QCOMPARE(wallet->gui_calls.load(), 0);
        QCOMPARE(coins->lockedSatoshi(), 0);
        write->release.release();
        QTRY_VERIFY(read->entered.available());
        QVERIFY(coins->lockOperationPending());
        QVERIFY(!model.setCoinsLocked({COutPoint{Txid{}, 0}}, false));
        QCOMPARE(coins->lockedSatoshi(), 0);
        QCOMPARE(wallet->lock_calls.load(), 1);
        QCOMPARE(wallet->unlock_calls.load(), 0);
        read->release.release();
        QTRY_VERIFY(!coins->lockOperationPending());
        QVERIFY(applied_while_pending);
        QCOMPARE(coins->lockedSatoshi(), 1000);
        QCOMPARE(coins->selectedCoinsCount(), 0);
        QCOMPARE(pending.count(), 2);
        QVERIFY(coins->lockOperationError().isEmpty());
        // Rejected attempts were discarded, so a fresh action is required.
        QVERIFY(coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(!coins->lockOperationPending());
        QCOMPARE(coins->lockedSatoshi(), 0);
        QCOMPARE(wallet->unlock_calls.load(), 1);
    }

    void staleCachedStateDoesNotSuppressRequests() {
        auto wallet = std::make_shared<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "First");
        WalletQmlModel model{wallet};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        {
            std::lock_guard<std::mutex> lock{wallet->mutex};
            wallet->locked.insert(COutPoint{Txid{}, 0});
        }
        QCOMPARE(coins->lockedSatoshi(), 0);
        QVERIFY(coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(!coins->lockOperationPending());
        QCOMPARE(wallet->unlock_calls.load(), 1);
        QVERIFY(!wallet->isLockedCoin(COutPoint{Txid{}, 0}));
        QVERIFY(coins->setCoinsLocked({id}, true));
        QTRY_VERIFY(!coins->lockOperationPending());
        {
            std::lock_guard<std::mutex> lock{wallet->mutex};
            wallet->locked.clear();
        }
        QCOMPARE(coins->lockedSatoshi(), 1000);
        QVERIFY(coins->setCoinsLocked({id}, true));
        QTRY_VERIFY(!coins->lockOperationPending());
        QCOMPARE(wallet->lock_calls.load(), 2);
        QVERIFY(wallet->isLockedCoin(COutPoint{Txid{}, 0}));
        QCOMPARE(wallet->gui_calls.load(), 0);
    }

    void earlierRefreshCannotCompleteLockRequest() {
        auto wallet = std::make_shared<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "First");
        WalletQmlModel model{wallet};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        auto read = std::make_shared<CoinGate>();
        auto write = std::make_shared<CoinGate>();
        const auto release = qScopeGuard([&] { write->release.release(); read->release.release(); });
        {
            std::lock_guard<std::mutex> lock{wallet->mutex};
            wallet->read_gate = read;
            wallet->write_gate = write;
        }
        model.requestWalletStateRefresh();
        QTRY_VERIFY(read->entered.available());
        QSignalSpy snapshots{&model, &WalletQmlModel::walletStateChanged};
        QVERIFY(coins->setCoinsLocked({id}, true));
        read->release.release();
        QTRY_VERIFY(write->entered.available());
        QTRY_COMPARE(snapshots.count(), 1);
        QVERIFY(coins->lockOperationPending());
        QCOMPARE(coins->lockedSatoshi(), 0);
        write->release.release();
        QTRY_VERIFY(!coins->lockOperationPending());
        QCOMPARE(snapshots.count(), 2);
        QCOMPARE(coins->lockedSatoshi(), 1000);
    }

    void failedWritesReconcileAndCanBeRetried() {
        auto wallet = std::make_shared<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "First");
        wallet->add(1, 2000, 1700000000, 1, "Second");
        wallet->fail_writes = true;
        WalletQmlModel model{wallet};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        QStringList ids;
        for (int i = 0; i < coins->rowCount(); ++i) ids.push_back(coins->data(coins->index(i), CoinsListModel::CoinIdRole).toString());
        QVERIFY(coins->setCoinsLocked(ids, true));
        QTRY_VERIFY(!coins->lockOperationPending());
        QCOMPARE(wallet->lock_calls.load(), 2);
        QCOMPARE(coins->lockedSatoshi(), 3000);
        QVERIFY(!coins->lockOperationError().isEmpty());
        wallet->fail_writes = false;
        QVERIFY(coins->setCoinsLocked(ids, true));
        QVERIFY(coins->lockOperationError().isEmpty());
        QTRY_VERIFY(!coins->lockOperationPending());
        QCOMPARE(wallet->lock_calls.load(), 4);
        QVERIFY(coins->lockOperationError().isEmpty());
        wallet->fail_writes = true;
        QVERIFY(coins->setCoinsLocked(ids, false));
        QTRY_VERIFY(!coins->lockOperationPending());
        QCOMPARE(coins->lockedSatoshi(), 0);
        QVERIFY(!coins->lockOperationError().isEmpty());
    }

    void failedReadbackDoesNotLeaveOperationPending() {
        auto wallet = std::make_shared<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "First");
        WalletQmlModel model{wallet};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        wallet->throw_read = true;
        QVERIFY(coins->setCoinsLocked({id}, true));
        QTRY_VERIFY(!coins->lockOperationPending());
        QVERIFY(!coins->lockOperationError().isEmpty());
        QTRY_COMPARE(coins->lockedSatoshi(), 1000);
        QVERIFY(coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(!coins->lockOperationPending());
        QVERIFY(coins->lockOperationError().isEmpty());
        QCOMPARE(coins->lockedSatoshi(), 0);
    }

    void walletClosureDiscardsPendingCompletion() {
        auto wallet = std::make_shared<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "First");
        auto gate = std::make_shared<CoinGate>();
        wallet->write_gate = gate;
        const auto release = qScopeGuard([&] { gate->release.release(); });
        auto model = std::make_unique<WalletQmlModel>(wallet);
        QTRY_VERIFY(model->walletStateReady());
        const auto executor = model->backendExecutor();
        QVERIFY(model->setCoinsLocked({COutPoint{Txid{}, 0}}, true));
        QTRY_VERIFY(gate->entered.available());
        model->beginShutdown();
        QVERIFY(!model->setCoinsLocked({COutPoint{Txid{}, 0}}, false));
        model.reset();
        QVERIFY(!executor->isDrained());
        gate->release.release();
        QTRY_VERIFY(executor->isDrained());
        QCOMPARE(wallet->lock_calls.load(), 1);
        QCOMPARE(wallet->unlock_calls.load(), 0);
        QCOMPARE(wallet->gui_calls.load(), 0);
    }

    void openingSelectionRefreshesLocksWithoutWalletNotifications() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 1000, 1700000000, 1, "First");
        WalletQmlModel model{std::move(wallet)};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        QCOMPARE(coins->lockedCoinsCount(), 0);

        { std::lock_guard<std::mutex> lock{source->mutex}; source->locked.insert(COutPoint{Txid{}, 0}); }
        coins->beginSelection();
        QTRY_COMPARE(coins->lockedCoinsCount(), 1);
        coins->cancelSelection();

        { std::lock_guard<std::mutex> lock{source->mutex}; source->locked.clear(); }
        coins->beginSelection();
        QTRY_COMPARE(coins->lockedCoinsCount(), 0);
        coins->toggleCoinSelection(0);
        QCOMPARE(coins->selectedCoinsCount(), 1);
        coins->cancelSelection();
        QCOMPARE(coins->selectedCoinsCount(), 0);
    }

    void stableIdentitySurvivesReorderAndRejectsRemovedCoin() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 1000, 1700000000, 1, "First");
        wallet->add(1, 2000, 1700000010, 2, "Second");
        WalletQmlModel model{std::move(wallet)};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        coins->setSortBy("amount");
        coins->setSortDescending(false);
        const QString coin_id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();

        coins->setSortDescending(true);
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 2000);
        QVERIFY(coins->toggleCoinSelectionById(coin_id));
        QCOMPARE(coins->totalSelectedSatoshi(), 1000);

        { std::lock_guard<std::mutex> lock{source->mutex}; source->coins.clear(); }
        model.requestWalletStateRefresh();
        QTRY_COMPARE(coins->rowCount(), 0);
        QVERIFY(!coins->toggleCoinSelectionById(coin_id));
        QCOMPARE(coins->selectedCoinsCount(), 0);
    }

    void filteringKeepsHiddenSelectionAndCancelRestoresInputs() {
        auto wallet = std::make_unique<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "Rent");
        wallet->add(1, 2000, 1700000010, 2, "Savings");
        WalletQmlModel model{std::move(wallet)};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        coins->setSortDescending(false);
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::AddressRole).toString(), QString::fromStdString(EncodeDestination(CoinDestination(1))));
        coins->toggleCoinSelection(0);
        QCOMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->beginSelection();
        coins->setSearchText("SAV");
        QCOMPARE(coins->rowCount(), 1);
        QCOMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->toggleCoinSelection(0);
        QCOMPARE(coins->totalSelectedSatoshi(), 3000);

        coins->setSearchText(QString::fromStdString(EncodeDestination(CoinDestination(1))));
        QCOMPARE(coins->rowCount(), 1);
        const QString coin_id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        coins->setSearchText(coin_id.left(12));
        QCOMPARE(coins->rowCount(), 2); // The mock outputs share the same transaction id.
        coins->cancelSelection();
        QCOMPARE(coins->totalSelectedSatoshi(), 1000);
        coins->setSearchText("SAV");
        coins->beginSelection();
        coins->toggleCoinSelection(0);
        coins->applySelection();
        coins->cancelSelection();
        QCOMPARE(coins->totalSelectedSatoshi(), 3000);
    }
    void sortingGroupingAndLocksUseStableCoinIdentity() {
        auto wallet = std::make_unique<CoinWallet>();
        auto* source = wallet.get();
        wallet->add(0, 3000, 1700000000, 1, "Zulu");
        wallet->add(1, 1000, 1700000010, 1, "Zulu");
        wallet->add(2, 2000, 1700000020, 2, "Alpha");
        WalletQmlModel model{std::move(wallet)};
        QTRY_VERIFY(model.walletStateReady());
        auto* coins = model.coinsListModel();
        coins->setGroupBy("date");
        coins->setSortBy("amount"); coins->setSortDescending(false);
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 1000);
        coins->setSortDescending(true);
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::AmountSatoshiRole).toLongLong(), 3000);
        coins->setSortDescending(false);
        coins->setSortBy("label");
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::LabelRole).toString(), QString("Alpha"));
        coins->setGroupBy("address");
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::GroupRole), coins->data(coins->index(0), CoinsListModel::AddressRole));
        QVERIFY(coins->data(coins->index(0), CoinsListModel::GroupFirstRole).toBool());
        QVERIFY(coins->data(coins->index(0), CoinsListModel::GroupLastRole).toBool());
        QVERIFY(coins->data(coins->index(1), CoinsListModel::GroupFirstRole).toBool());
        QVERIFY(!coins->data(coins->index(1), CoinsListModel::GroupLastRole).toBool());
        QVERIFY(coins->data(coins->index(2), CoinsListModel::GroupLastRole).toBool());
        coins->toggleCoinSelection(0);
        const auto id = coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString();
        QVERIFY(coins->setCoinsLocked({id}, true));
        QTRY_VERIFY(!coins->lockOperationPending());
        QCOMPARE(coins->selectedCoinsCount(), 0);
        coins->setFilter("locked"); QTRY_COMPARE(coins->rowCount(), 1);
        coins->toggleCoinSelection(0); QCOMPARE(coins->selectedCoinsCount(), 1);
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString(), id);
        QVERIFY(coins->setCoinsLocked({id}, false));
        QTRY_VERIFY(!coins->lockOperationPending());
        QTRY_COMPARE(coins->rowCount(), 0);
        coins->setFilter("spendable"); QCOMPARE(coins->rowCount(), 3);
        coins->setMinAmount(1500); QCOMPARE(coins->rowCount(), 2);
        coins->setMaxAmount(2500); QCOMPARE(coins->rowCount(), 1);
        coins->setMinAmount(-1); coins->setMaxAmount(-1); QCOMPARE(coins->rowCount(), 3);
        coins->setGroupBy("date");
        QVERIFY(!coins->data(coins->index(0), CoinsListModel::GroupRole).toString().isEmpty());
        coins->toggleCoinSelection(0);
        { std::lock_guard<std::mutex> lock{source->mutex}; source->coins.clear(); }
        model.requestWalletStateRefresh();
        QTRY_COMPARE(coins->rowCount(), 0);
        QCOMPARE(coins->selectedCoinsCount(), 0);
        QCOMPARE(coins->totalSatoshi(), 0);
    }
};
BITCOINQML_REGISTER_QT_TEST(CoinsListModelTests)
#include "test_coinslistmodel.moc"
