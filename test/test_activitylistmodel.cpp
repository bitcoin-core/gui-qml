// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QSignalSpy>

#include <test/mocks/mockwallet.h>

#include <qml/models/activitylistmodel.h>
#include <qml/models/receiverequesthistorymodel.h>
#include <qml/models/walletqmlmodel.h>

#include <addresstype.h>
#include <chainparams.h>
#include <key_io.h>
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

interfaces::WalletTx ReceiveTxTo(const CTxDestination& destination, uint8_t txid_seed, CAmount amount, int64_t time)
{
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{txid_seed}), 0});
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

interfaces::WalletTx ReceiveTx(uint8_t seed, CAmount amount, int64_t time)
{
    return ReceiveTxTo(TestDestination(seed), seed, amount, time);
}

interfaces::WalletTx TwoOutputReceiveTx(uint8_t seed, CAmount amount, int64_t time)
{
    const CTxDestination first{TestDestination(seed)};
    const CTxDestination second{TestDestination(static_cast<uint8_t>(seed + 1))};

    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{seed}), 0});
    mtx.vout.emplace_back(amount, GetScriptForDestination(first));
    mtx.vout.emplace_back(amount, GetScriptForDestination(second));

    interfaces::WalletTx wtx;
    wtx.tx = MakeTransactionRef(std::move(mtx));
    wtx.txin_is_mine = {false};
    wtx.txout_is_mine = {true, true};
    wtx.txout_is_change = {false, false};
    wtx.txout_address = {first, second};
    wtx.txout_address_is_mine = {true, true};
    wtx.credit = 2 * amount;
    wtx.debit = 0;
    wtx.change = 0;
    wtx.time = time;
    wtx.is_coinbase = false;
    return wtx;
}

interfaces::WalletTx SelfPaymentTo(const CTxDestination& destination, uint8_t txid_seed, CAmount amount, int64_t time)
{
    CMutableTransaction mtx;
    mtx.version = 2;
    mtx.vin.emplace_back(COutPoint{Txid::FromUint256(uint256{txid_seed}), 0});
    mtx.vout.emplace_back(amount, GetScriptForDestination(destination));

    interfaces::WalletTx wtx;
    wtx.tx = MakeTransactionRef(std::move(mtx));
    wtx.txin_is_mine = {true};
    wtx.txout_is_mine = {true};
    wtx.txout_is_change = {false};
    wtx.txout_address = {destination};
    wtx.txout_address_is_mine = {true};
    wtx.credit = amount;
    wtx.debit = amount;
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
    bool m_fail_status_reads{false};
    std::map<CTxDestination, std::string> m_labels;
    std::map<std::string, std::string> m_stored_requests;
    std::vector<interfaces::Wallet::TransactionChangedFn> m_transaction_changed;

    std::set<interfaces::WalletTx> getWalletTxs() override { return m_txs; }
    std::vector<std::string> getAddressReceiveRequests() override
    {
        std::vector<std::string> blobs;
        for (const auto& [id, blob] : m_stored_requests) blobs.push_back(blob);
        return blobs;
    }
    bool getAddress(const CTxDestination& dest, std::string* name, wallet::AddressPurpose*) override
    {
        const auto it = m_labels.find(dest);
        if (it == m_labels.end()) return false;
        if (name) *name = it->second;
        return true;
    }
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
        if (m_fail_status_reads) return false;
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
        setDepth(wtx, depth);
    }

    void setDepth(const interfaces::WalletTx& wtx, int depth)
    {
        interfaces::WalletTxStatus status{};
        status.depth_in_main_chain = depth;
        status.is_in_main_chain = depth > 0;
        m_statuses[wtx.tx->GetHash()] = status;
    }

    void addStoredRequest(int64_t id, const CTxDestination& destination, const std::string& label,
                          CAmount amount, qint64 timestamp)
    {
        QmlRecentRequestEntry entry;
        entry.id = id;
        entry.date = QDateTime::fromSecsSinceEpoch(timestamp);
        entry.recipient.address = EncodeDestination(destination);
        entry.recipient.label = label;
        entry.recipient.amount = amount;
        m_stored_requests[std::to_string(id)] = ReceiveRequestHistoryModel::SerializeEntry(entry);
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

QStringList PendingRequestIds(const ActivityListModel& model)
{
    QStringList ids;
    for (int row = 0; row < model.rowCount(); ++row) {
        if (model.data(model.index(row, 0), ActivityListModel::IsPendingRequestRole).toBool()) {
            ids.append(model.data(model.index(row, 0), ActivityListModel::RequestIdRole).toString());
        }
    }
    return ids;
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
    void dataIsAPureRead();
    void changeRefreshesEveryRowOfTheTransaction();
    void statusRefreshKeepsCachedStatusOnFailedRead();
    void refreshLabelsFollowsAddressBook();
    void addressReuseFulfillsRequestsOldestFirst();
    void selfPaymentFulfillsOnlyWithItsCreditPart();
    void sameSecondRequestsFulfillOldestFirst();
    void paymentPredatingTheRequestDoesNotFulfillIt();
    void statusAndTypeRolesAreInts();
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

void ActivityListModelTests::dataIsAPureRead()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx wtx{ReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(wtx, /*depth=*/0);

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);

    const QModelIndex row{model->index(0, 0)};
    QCOMPARE(model->data(row, ActivityListModel::StatusRole).toInt(), int{Transaction::Unconfirmed});

    // The wallet's view of the transaction advances, but reading the model
    // must not pick that up: status only moves on an explicit refresh.
    wallet_ptr->setDepth(wtx, 6);
    QCOMPARE(model->data(row, ActivityListModel::StatusRole).toInt(), int{Transaction::Unconfirmed});
    QCOMPARE(model->data(row, ActivityListModel::DepthRole).toInt(), 0);

    QSignalSpy changed_spy{model, &QAbstractItemModel::dataChanged};
    QSignalSpy reset_spy{model, &QAbstractItemModel::modelAboutToBeReset};
    model->refreshStatuses();
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(row, ActivityListModel::StatusRole).toInt(), int{Transaction::Confirmed});
    QCOMPARE(model->data(row, ActivityListModel::DepthRole).toInt(), 6);
    QCOMPARE(changed_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);
}

void ActivityListModelTests::changeRefreshesEveryRowOfTheTransaction()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx wtx{TwoOutputReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(wtx, /*depth=*/0);

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 2);

    // Abandoning the transaction fires one change notification for its
    // hash. Both of its rows have to pick the new status up: no row
    // re-reads the wallet on its own, so a row skipped here would show
    // the stale status until an unrelated refresh.
    interfaces::WalletTxStatus abandoned{};
    abandoned.is_abandoned = true;
    wallet_ptr->m_statuses[wtx.tx->GetHash()] = abandoned;
    wallet_ptr->notifyTransactionChanged(wtx);
    for (int row = 0; row < model->rowCount(); ++row) {
        QCOMPARE(model->data(model->index(row, 0), ActivityListModel::StatusRole).toInt(),
                 int{Transaction::Abandoned});
    }
}

void ActivityListModelTests::statusRefreshKeepsCachedStatusOnFailedRead()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx wtx{ReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(wtx, /*depth=*/6);

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);

    const QModelIndex row{model->index(0, 0)};
    QCOMPARE(model->data(row, ActivityListModel::StatusRole).toInt(), int{Transaction::Confirmed});

    // A failed status read (the wallet interface also fails on plain lock
    // contention) must not downgrade the row; the cached status stays until
    // a read succeeds again.
    wallet_ptr->m_fail_status_reads = true;
    model->refreshStatuses();
    QCOMPARE(model->data(row, ActivityListModel::StatusRole).toInt(), int{Transaction::Confirmed});
    QCOMPARE(model->data(row, ActivityListModel::DepthRole).toInt(), 6);

    wallet_ptr->m_fail_status_reads = false;
    wallet_ptr->setDepth(wtx, 0);
    model->refreshStatuses();
    QCOMPARE(model->data(row, ActivityListModel::StatusRole).toInt(), int{Transaction::Unconfirmed});
}

void ActivityListModelTests::refreshLabelsFollowsAddressBook()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx wtx{ReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(wtx);
    wallet->m_labels[TestDestination(1)] = "alice";

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);

    // The label is resolved when the row is created, not on every read.
    const QModelIndex row{model->index(0, 0)};
    QCOMPARE(model->data(row, ActivityListModel::LabelRole).toString(), QStringLiteral("alice"));

    wallet_ptr->m_labels[TestDestination(1)] = "bob";
    QCOMPARE(model->data(row, ActivityListModel::LabelRole).toString(), QStringLiteral("alice"));

    QSignalSpy changed_spy{model, &QAbstractItemModel::dataChanged};
    model->refreshLabels();
    QCOMPARE(model->data(row, ActivityListModel::LabelRole).toString(), QStringLiteral("bob"));
    QCOMPARE(changed_spy.count(), 1);
}

void ActivityListModelTests::addressReuseFulfillsRequestsOldestFirst()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const CTxDestination destination{TestDestination(1)};
    wallet->addStoredRequest(1, destination, "first", COIN, 100);
    wallet->addStoredRequest(2, destination, "second", 2 * COIN, 200);

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};

    // Both requests on the same address get their own pending row.
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(PendingRequestIds(*model), (QStringList{"2", "1"}));

    // One payment fulfills exactly one row, the oldest request.
    const interfaces::WalletTx first_payment{ReceiveTxTo(destination, 10, COIN, 300)};
    wallet_ptr->addConfirmedTx(first_payment);
    wallet_ptr->notifyTransactionChanged(first_payment);
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(PendingRequestIds(*model), QStringList{"2"});

    // A reload reproduces the same still-pending set.
    model->reload();
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(PendingRequestIds(*model), QStringList{"2"});

    // A later payment can still fulfill the remaining request.
    const interfaces::WalletTx second_payment{ReceiveTxTo(destination, 11, 2 * COIN, 400)};
    wallet_ptr->addConfirmedTx(second_payment);
    wallet_ptr->notifyTransactionChanged(second_payment);
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(PendingRequestIds(*model), QStringList{});
}

void ActivityListModelTests::selfPaymentFulfillsOnlyWithItsCreditPart()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const CTxDestination destination{TestDestination(1)};
    wallet->addStoredRequest(1, destination, "first", COIN, 100);
    wallet->addStoredRequest(2, destination, "second", COIN, 200);

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 2);

    // A payment from this wallet to its own requested address produces a
    // debit part and a credit part for the same address. Only the credit
    // part may fulfill a request: one row is consumed, the send part gets
    // its own row, and the second request stays pending.
    const interfaces::WalletTx self_payment{SelfPaymentTo(destination, 10, COIN, 300)};
    wallet_ptr->addConfirmedTx(self_payment);
    wallet_ptr->notifyTransactionChanged(self_payment);
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(PendingRequestIds(*model), QStringList{"2"});

    // The fulfilled row carries the incoming part, not the send part.
    int fulfilled_type{-1};
    for (int row = 0; row < model->rowCount(); ++row) {
        const QModelIndex idx{model->index(row, 0)};
        if (model->data(idx, ActivityListModel::RequestIdRole).toString() == QStringLiteral("1")) {
            QCOMPARE(model->data(idx, ActivityListModel::IsPendingRequestRole).toBool(), false);
            fulfilled_type = model->data(idx, ActivityListModel::TypeRole).toInt();
        }
    }
    QCOMPARE(fulfilled_type, int{Transaction::RecvWithAddress});

    // A reload reproduces the same rows and pending set.
    model->reload();
    QCOMPARE(model->rowCount(), 3);
    QCOMPARE(PendingRequestIds(*model), QStringList{"2"});
}

void ActivityListModelTests::sameSecondRequestsFulfillOldestFirst()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const CTxDestination destination{TestDestination(1)};
    wallet->addStoredRequest(9, destination, "ninth", COIN, 100);
    wallet->addStoredRequest(10, destination, "tenth", COIN, 100);

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};

    // Ids compare numerically, newest (highest) first; lexicographic
    // comparison would place "10" below "9".
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(PendingRequestIds(*model), (QStringList{"10", "9"}));

    // The payment fulfills the oldest request (the lowest id), and a
    // reload agrees with the live session.
    const interfaces::WalletTx payment{ReceiveTxTo(destination, 11, COIN, 300)};
    wallet_ptr->addConfirmedTx(payment);
    wallet_ptr->notifyTransactionChanged(payment);
    QCOMPARE(PendingRequestIds(*model), QStringList{"10"});

    model->reload();
    QCOMPARE(PendingRequestIds(*model), QStringList{"10"});
}

void ActivityListModelTests::paymentPredatingTheRequestDoesNotFulfillIt()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const CTxDestination destination{TestDestination(1)};
    // The address received a payment before any request existed, and a
    // request was created afterwards.
    wallet->addConfirmedTx(ReceiveTxTo(destination, 10, COIN, 100));
    wallet->addStoredRequest(1, destination, "later request", COIN, 200);

    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};

    // Live, a payment can only fulfill a request that already existed when
    // it arrived, so the reload replay must not let the old payment consume
    // the newer request: the payment keeps its ordinary received row and
    // the request stays pending.
    QCOMPARE(model->rowCount(), 2);
    QCOMPARE(PendingRequestIds(*model), QStringList{"1"});
}

void ActivityListModelTests::statusAndTypeRolesAreInts()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    wallet->addConfirmedTx(ReceiveTx(1, COIN, 100));

    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);

    // QML delegates compare these roles against Transaction enum values;
    // handing out plain ints keeps that an explicit contract instead of
    // relying on QVariant enum coercion.
    const QModelIndex row{model->index(0, 0)};
    QCOMPARE(model->data(row, ActivityListModel::StatusRole).typeId(), int{QMetaType::Int});
    QCOMPARE(model->data(row, ActivityListModel::TypeRole).typeId(), int{QMetaType::Int});
    QCOMPARE(model->data(row, ActivityListModel::TypeRole).toInt(), int{Transaction::RecvWithAddress});
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ActivityListModelTests)
#else
QTEST_MAIN(ActivityListModelTests)
#endif
#include "test_activitylistmodel.moc"
