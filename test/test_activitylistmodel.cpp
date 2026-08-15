// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mockwallet.h>

#include <qml/models/activitylistmodel.h>
#include <qml/models/walletqmlmodel.h>

#include <addresstype.h>
#include <chainparams.h>
#include <consensus/amount.h>
#include <interfaces/wallet.h>
#include <primitives/transaction.h>
#include <uint256.h>

#include <map>
#include <memory>
#include <set>
#include <utility>
#include <vector>

namespace {
CTxDestination TestDestination(uint8_t seed)
{
    std::vector<unsigned char> bytes(20, seed);
    return WitnessV0KeyHash{uint160{bytes}};
}

interfaces::WalletTx ReceiveTx(uint8_t seed, CAmount amount, int64_t time)
{
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{seed}), 0});
    const CTxDestination destination{TestDestination(seed)};
    mtx.vout.emplace_back(amount, GetScriptForDestination(destination));

    interfaces::WalletTx wtx;
    wtx.tx = MakeTransactionRef(std::move(mtx));
    wtx.txin_is_mine = {false};
    wtx.txout_is_mine = {true};
    wtx.txout_is_change = {false};
    wtx.txout_address = {destination};
    wtx.txout_address_is_mine = {true};
    wtx.credit = amount;
    wtx.debit = 0;
    wtx.change = 0;
    wtx.time = time;
    wtx.is_coinbase = false;
    return wtx;
}

class TestActivityWallet final : public StubWallet
{
public:
    std::set<interfaces::WalletTx> m_txs;
    std::map<Txid, interfaces::WalletTxStatus> m_statuses;
    std::vector<interfaces::Wallet::TransactionChangedFn> m_transaction_changed;

    std::set<interfaces::WalletTx> getWalletTxs() override { return m_txs; }
    interfaces::WalletTx getWalletTx(const Txid& txid) override
    {
        for (const auto& wtx : m_txs) {
            if (wtx.tx->GetHash() == txid) return wtx;
        }
        return {};
    }
    bool tryGetTxStatus(const Txid& txid, interfaces::WalletTxStatus& tx_status,
                        int& num_blocks, int64_t& block_time) override
    {
        const auto it = m_statuses.find(txid);
        if (it == m_statuses.end()) return false;
        tx_status = it->second;
        num_blocks = 0;
        block_time = 0;
        return true;
    }
    std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn) override
    {
        m_transaction_changed.push_back(std::move(fn));
        return {};
    }

    void addConfirmedTx(const interfaces::WalletTx& wtx, int depth = 1)
    {
        m_txs.insert(wtx);
        interfaces::WalletTxStatus status{};
        status.depth_in_main_chain = depth;
        status.is_in_main_chain = depth > 0;
        m_statuses[wtx.tx->GetHash()] = status;
    }

    void notifyTransactionChanged(const interfaces::WalletTx& wtx)
    {
        for (const auto& fn : m_transaction_changed) {
            fn(wtx.tx->GetHash(), CT_NEW);
        }
    }
};

QList<qint64> RowTimestamps(const ActivityListModel& model)
{
    QList<qint64> timestamps;
    for (int row = 0; row < model.rowCount(); ++row) {
        timestamps.append(model.data(model.index(row, 0), ActivityListModel::TimestampRole).toLongLong());
    }
    return timestamps;
}

QStringList RowTxids(const ActivityListModel& model)
{
    QStringList txids;
    for (int row = 0; row < model.rowCount(); ++row) {
        txids.append(model.data(model.index(row, 0), ActivityListModel::TxIdRole).toString());
    }
    return txids;
}
} // namespace

class ActivityListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void refreshOrdersRowsNewestFirst();
    void liveInsertMatchesRefreshOrder();
};

void ActivityListModelTests::initTestCase()
{
    SelectParams(ChainType::REGTEST);
}

void ActivityListModelTests::refreshOrdersRowsNewestFirst()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    wallet->addConfirmedTx(ReceiveTx(1, COIN, 100));
    wallet->addConfirmedTx(ReceiveTx(2, 2 * COIN, 300));
    wallet->addConfirmedTx(ReceiveTx(3, 3 * COIN, 200));

    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};

    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(RowTimestamps(*model), (QList<qint64>{300, 200, 100}));
}

void ActivityListModelTests::liveInsertMatchesRefreshOrder()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    wallet->addConfirmedTx(ReceiveTx(1, COIN, 100));
    wallet->addConfirmedTx(ReceiveTx(2, 2 * COIN, 300));

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 2);

    // A transaction arriving between the two existing rows, plus one that ties
    // an existing timestamp, must land where a full refresh would place them.
    const interfaces::WalletTx between{ReceiveTx(3, 3 * COIN, 200)};
    wallet_ptr->addConfirmedTx(between);
    wallet_ptr->notifyTransactionChanged(between);
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(RowTimestamps(*model), (QList<qint64>{300, 200, 100}));

    const interfaces::WalletTx tied{ReceiveTx(4, 4 * COIN, 200)};
    wallet_ptr->addConfirmedTx(tied);
    wallet_ptr->notifyTransactionChanged(tied);
    QCOMPARE(model->rowCount(), 4);
    QCOMPARE(RowTimestamps(*model), (QList<qint64>{300, 200, 200, 100}));

    const QStringList live_order{RowTxids(*model)};
    model->reload();
    QCOMPARE(model->rowCount(), 4);
    QCOMPARE(RowTxids(*model), live_order);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ActivityListModelTests)
#else
QTEST_MAIN(ActivityListModelTests)
#endif
#include "test_activitylistmodel.moc"
