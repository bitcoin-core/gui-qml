// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <QtTest/QtTest>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>
#include <qml/models/walletqmlmodel.h>
#include <key_io.h>
#include <addresstype.h>
#include <set>

namespace {
CTxDestination CoinDestination(uint8_t seed) { return WitnessV0KeyHash{uint160{std::vector<unsigned char>(20, seed)}}; }
class CoinWallet final : public StubWallet {
public:
    CoinsList coins;
    std::set<COutPoint> locked;
    std::map<CTxDestination, std::string> labels;
    CoinsList listCoins() override { return coins; }
    bool isLockedCoin(const COutPoint& p) override { return locked.count(p); }
    bool lockCoin(const COutPoint& p, bool) override { locked.insert(p); return true; }
    bool unlockCoin(const COutPoint& p) override { locked.erase(p); return true; }
    bool getAddress(const CTxDestination& d, std::string* name, wallet::AddressPurpose*) override {
        if (!labels.count(d)) return false;
        if (name) *name = labels[d];
        return true;
    }
    void add(int n, CAmount amount, int64_t date, uint8_t address, std::string label) {
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
    void filteringKeepsHiddenSelectionAndCancelRestoresInputs() {
        auto wallet = std::make_unique<CoinWallet>();
        wallet->add(0, 1000, 1700000000, 1, "Rent");
        wallet->add(1, 2000, 1700000010, 2, "Savings");
        WalletQmlModel model{std::move(wallet)};
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
        QCOMPARE(coins->selectedCoinsCount(), 0);
        coins->setFilter("locked"); QCOMPARE(coins->rowCount(), 1);
        coins->toggleCoinSelection(0); QCOMPARE(coins->selectedCoinsCount(), 1);
        QCOMPARE(coins->data(coins->index(0), CoinsListModel::CoinIdRole).toString(), id);
        QVERIFY(coins->setCoinsLocked({id}, false));
        QCOMPARE(coins->rowCount(), 0);
        coins->setFilter("spendable"); QCOMPARE(coins->rowCount(), 3);
        coins->setMinAmount(1500); QCOMPARE(coins->rowCount(), 2);
        coins->setMaxAmount(2500); QCOMPARE(coins->rowCount(), 1);
        coins->setMinAmount(-1); coins->setMaxAmount(-1); QCOMPARE(coins->rowCount(), 3);
        coins->setGroupBy("date");
        QVERIFY(!coins->data(coins->index(0), CoinsListModel::GroupRole).toString().isEmpty());
        coins->toggleCoinSelection(0);
        source->coins.clear(); coins->update();
        QCOMPARE(coins->selectedCoinsCount(), 0);
        QCOMPARE(coins->totalSatoshi(), 0);
    }
};
BITCOINQML_REGISTER_QT_TEST(CoinsListModelTests)
#include "test_coinslistmodel.moc"
