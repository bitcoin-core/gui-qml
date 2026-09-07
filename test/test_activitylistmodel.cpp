// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <primitives/transaction.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/transaction.h>
#include <qml/models/walletqmlmodel.h>
#include <script/script.h>
#include <test/mocks/mockwallet.h>
#include <test/qt_test_registry.h>
#include <uint256.h>

#include <QAbstractItemModel>
#include <QCoreApplication>
#include <QModelIndex>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <map>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

namespace {
constexpr CAmount COIN_VALUE{100'000'000};
constexpr auto ASYNC_TIMEOUT_MS{5'000};

CTxDestination Destination(unsigned char value)
{
    uint160 id;
    id.begin()[0] = value;
    return PKHash{id};
}

//! `seed` makes the txid unique.
interfaces::WalletTx MakeIncomingWalletTx(unsigned int seed)
{
    uint256 prevout_hash;
    prevout_hash.begin()[0] = static_cast<unsigned char>(seed & 0xff);
    prevout_hash.begin()[1] = static_cast<unsigned char>((seed >> 8) & 0xff);

    CMutableTransaction mtx;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(prevout_hash), 0});
    mtx.vout.emplace_back(5 * COIN_VALUE, CScript{});

    interfaces::WalletTx wtx;
    wtx.tx = MakeTransactionRef(std::move(mtx));
    wtx.txin_is_mine = {false};
    wtx.txout_is_mine = {true};
    wtx.txout_is_change = {false};
    wtx.txout_address = {Destination(static_cast<unsigned char>(seed + 1))};
    wtx.txout_address_is_mine = {true};
    wtx.credit = 5 * COIN_VALUE;
    wtx.debit = 0;
    wtx.change = 0;
    wtx.time = 1'700'000'000 + seed;
    wtx.is_coinbase = false;
    return wtx;
}

int ReadAllRows(const QAbstractItemModel& model)
{
    const int rows = model.rowCount();
    for (int row = 0; row < rows; ++row) {
        const QModelIndex index = model.index(row, 0);
        model.data(index, ActivityListModel::AmountRole);
        model.data(index, ActivityListModel::LabelRole);
        model.data(index, ActivityListModel::StatusRole);
        model.data(index, ActivityListModel::TxIdRole);
    }
    return rows;
}

class NotifyingWallet
{
public:
    explicit NotifyingWallet(int transaction_count)
    {
        auto wallet = std::make_unique<MockWallet>();

        for (int i = 0; i < transaction_count; ++i) {
            const interfaces::WalletTx wtx = MakeIncomingWalletTx(static_cast<unsigned int>(i));
            const Txid txid = wtx.tx->GetHash();
            m_txids.push_back(txid);
            m_txs.emplace(txid, wtx);
        }

        wallet->handle_transaction_changed_fn = [this](interfaces::Wallet::TransactionChangedFn fn) {
            m_callbacks.push_back(std::move(fn));
            return interfaces::MakeCleanupHandler([] {});
        };
        wallet->get_wallet_tx_fn = [this](const Txid& txid) {
            auto it = m_txs.find(txid);
            return it == m_txs.end() ? interfaces::WalletTx{} : it->second;
        };
        wallet->try_get_tx_status_fn = [this](const Txid& txid, interfaces::WalletTxStatus& tx_status, int& num_blocks, int64_t& block_time) {
            if (!m_txs.count(txid)) return false;
            if (m_status_thread_guard != nullptr && QThread::currentThread() == m_status_thread_guard) return false;
            tx_status.depth_in_main_chain = 1;
            tx_status.is_in_main_chain = true;
            num_blocks = 100;
            block_time = 1'700'000'000;
            return true;
        };

        m_model = std::make_unique<WalletQmlModel>(std::move(wallet));
    }

    WalletQmlModel& model() { return *m_model; }
    ActivityListModel& activity() { return *m_model->activityListModel(); }
    const std::vector<Txid>& txids() const { return m_txids; }

    void OnlyServeStatusOffThread(QThread* model_thread)
    {
        m_status_thread_guard = model_thread;
    }

    void Notify(const Txid& txid) const
    {
        for (const auto& callback : m_callbacks) {
            callback(txid, CT_NEW);
        }
    }

private:
    QThread* m_status_thread_guard{nullptr};
    std::vector<interfaces::Wallet::TransactionChangedFn> m_callbacks;
    std::map<Txid, interfaces::WalletTx> m_txs;
    std::vector<Txid> m_txids;
    std::unique_ptr<WalletQmlModel> m_model;
};
} // namespace

class ActivityListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void walletNotificationFromAnotherThreadIsAppliedOnTheModelThread();
    void walletStatusIsReadOnTheNotifyingThread();
    void concurrentWalletNotificationsAndProxyReadsStayConsistent();
};

void ActivityListModelTests::initTestCase()
{
    SelectParams(ChainType::REGTEST);
}

//! Wallet notifications arrive on validation and network threads, but the model is
//! read on the GUI thread, so the work must be marshalled onto the model thread.
void ActivityListModelTests::walletNotificationFromAnotherThreadIsAppliedOnTheModelThread()
{
    NotifyingWallet wallet{/*transaction_count=*/1};
    ActivityListModel& activity = wallet.activity();
    QCOMPARE(activity.rowCount(), 0);

    std::atomic<QThread*> insert_thread{nullptr};
    QObject::connect(&activity, &QAbstractItemModel::rowsInserted, &activity,
                     [&insert_thread](const QModelIndex&, int, int) {
                         insert_thread = QThread::currentThread();
                     },
                     Qt::DirectConnection);

    QThread* const model_thread = activity.thread();
    std::atomic<QThread*> notifying_thread{nullptr};
    std::thread worker([&] {
        notifying_thread = QThread::currentThread();
        wallet.Notify(wallet.txids().front());
    });
    worker.join();

    QVERIFY(notifying_thread.load() != model_thread);

    // Nothing has run on the model thread yet.
    QCOMPARE(activity.rowCount(), 0);
    QCOMPARE(insert_thread.load(), nullptr);

    QTRY_COMPARE_WITH_TIMEOUT(activity.rowCount(), 1, ASYNC_TIMEOUT_MS);
    QCOMPARE(insert_thread.load(), model_thread);
    QCOMPARE(activity.data(activity.index(0), ActivityListModel::TxIdRole).toString(),
             QString::fromStdString(wallet.txids().front().ToUint256().GetHex()));
}

void ActivityListModelTests::walletStatusIsReadOnTheNotifyingThread()
{
    NotifyingWallet wallet{/*transaction_count=*/1};
    ActivityListModel& activity = wallet.activity();
    wallet.OnlyServeStatusOffThread(activity.thread());

    std::thread worker([&] { wallet.Notify(wallet.txids().front()); });
    worker.join();

    QTRY_COMPARE_WITH_TIMEOUT(activity.rowCount(), 1, ASYNC_TIMEOUT_MS);
}

//! Direct mutation from the notifying thread trips the model-thread assertions.
void ActivityListModelTests::concurrentWalletNotificationsAndProxyReadsStayConsistent()
{
    constexpr int TRANSACTION_COUNT{64};
    NotifyingWallet wallet{TRANSACTION_COUNT};
    ActivityListModel& activity = wallet.activity();

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&activity);
    QCOMPARE(proxy.rowCount(), 0);

    std::thread worker([&] {
        for (const Txid& txid : wallet.txids()) {
            wallet.Notify(txid);
            // Runs the update path alongside the insert path.
            wallet.Notify(txid);
        }
    });

    QTRY_COMPARE_WITH_TIMEOUT(ReadAllRows(proxy), TRANSACTION_COUNT, ASYNC_TIMEOUT_MS);
    worker.join();

    QTRY_COMPARE_WITH_TIMEOUT(ReadAllRows(proxy), TRANSACTION_COUNT, ASYNC_TIMEOUT_MS);
    QCOMPARE(activity.rowCount(), TRANSACTION_COUNT);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(ActivityListModelTests)
#else
QTEST_MAIN(ActivityListModelTests)
#endif
#include "test_activitylistmodel.moc"
