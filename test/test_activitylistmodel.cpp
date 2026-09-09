// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QSignalSpy>
#include <QTimer>

#include <test/mocks/mockwallet.h>

#include <qml/models/activityfilterproxymodel.h>
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

#include <algorithm>
#include <map>
#include <memory>
#include <set>
#include <thread>
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
    int m_wallet_height{0};
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
        num_blocks = m_wallet_height;
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

    void notifyTransactionChanged(const interfaces::WalletTx& wtx, ChangeType change_type = CT_NEW)
    {
        for (const auto& fn : m_transaction_changed) {
            fn(wtx.tx->GetHash(), change_type);
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
    void proxyOrderMatchesSourceForTiedTimestamps();
    void notificationsFromNodeThreadAreQueued();
    void contendedNotificationReadIsRetried();
    void permanentlyFailingNotificationReadStopsRetrying();
    void deletedTransactionRowsAreRemoved();
    void contendedRefreshReadIsRetried();
    void relativeDatesRefreshWithoutANewBlock();
    void statusReadTakenBeforeTheWalletCaughtUpIsRetried();
    void caughtUpStatusReadDoesNotRetry();
    void tipArrivingDuringAPendingRetryIsNotLost();
    void walletAheadOfTheAnnouncedTipIsRetried();
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

void ActivityListModelTests::proxyOrderMatchesSourceForTiedTimestamps()
{
    // The displayed order is the proxy's, so for tied timestamps its live
    // inserts must follow the source model's deterministic tie-breaks (the
    // order a reload produces), whichever order the ties arrive in.
    for (const bool ascending_arrival : {true, false}) {
        auto wallet{std::make_unique<TestActivityWallet>()};
        wallet->addConfirmedTx(ReceiveTx(1, COIN, 100));
        TestActivityWallet* wallet_ptr{wallet.get()};
        WalletQmlModel wallet_model{std::move(wallet)};
        ActivityListModel* model{wallet_model.activityListModel()};

        ActivityFilterProxyModel proxy;
        proxy.setSourceModel(model);
        QCOMPARE(proxy.rowCount(), 1);

        std::vector<interfaces::WalletTx> tied{
            ReceiveTx(2, 2 * COIN, 200), ReceiveTx(3, 3 * COIN, 200), ReceiveTx(4, 4 * COIN, 200)};
        std::sort(tied.begin(), tied.end(), [](const interfaces::WalletTx& a, const interfaces::WalletTx& b) {
            return a.tx->GetHash().GetHex() < b.tx->GetHash().GetHex();
        });
        if (!ascending_arrival) std::reverse(tied.begin(), tied.end());
        for (const auto& wtx : tied) {
            wallet_ptr->addConfirmedTx(wtx);
            wallet_ptr->notifyTransactionChanged(wtx);
        }
        QCOMPARE(proxy.rowCount(), 4);

        QStringList proxy_order;
        for (int row = 0; row < proxy.rowCount(); ++row) {
            proxy_order.append(proxy.data(proxy.index(row, 0), ActivityListModel::TxIdRole).toString());
        }
        QCOMPARE(proxy_order, RowTxids(*model));
    }
}

void ActivityListModelTests::notificationsFromNodeThreadAreQueued()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 0);

    const interfaces::WalletTx tx{ReceiveTx(1, COIN, 100)};
    wallet_ptr->addConfirmedTx(tx);
    std::thread node_thread{[&] { wallet_ptr->notifyTransactionChanged(tx); }};
    node_thread.join();

    // The model must not have been mutated on the notifying thread; the
    // update is queued until this (the model's) thread processes events.
    QCOMPARE(model->rowCount(), 0);
    QTRY_COMPARE(model->rowCount(), 1);
}

void ActivityListModelTests::contendedNotificationReadIsRetried()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 0);

    // The wallet fires change notifications while it can still hold
    // cs_wallet, so the status read can fail transiently. The update must
    // be retried once the lock frees up, not dropped.
    const interfaces::WalletTx tx{ReceiveTx(1, COIN, 100)};
    wallet_ptr->addConfirmedTx(tx);
    wallet_ptr->m_fail_status_reads = true;
    wallet_ptr->notifyTransactionChanged(tx);
    QCOMPARE(model->rowCount(), 0);

    wallet_ptr->m_fail_status_reads = false;
    QTRY_COMPARE(model->rowCount(), 1);
}

void ActivityListModelTests::permanentlyFailingNotificationReadStopsRetrying()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 0);

    // A read that never succeeds (for example the wallet no longer knows
    // the transaction) must exhaust its retry budget rather than poll for
    // the lifetime of the model.
    const interfaces::WalletTx tx{ReceiveTx(1, COIN, 100)};
    wallet_ptr->addConfirmedTx(tx);
    wallet_ptr->m_fail_status_reads = true;
    wallet_ptr->notifyTransactionChanged(tx);
    QCOMPARE(model->rowCount(), 0);

    // Wait out the whole budget (8 retries, 250 ms apart), then let reads
    // succeed again: a bounded schedule has given up by now, so no stray
    // retry may pick the row up.
    QTest::qWait(3000);
    wallet_ptr->m_fail_status_reads = false;
    QTest::qWait(600);
    QCOMPARE(model->rowCount(), 0);
}

void ActivityListModelTests::deletedTransactionRowsAreRemoved()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx deleted{TwoOutputReceiveTx(1, COIN, 200)};
    const interfaces::WalletTx kept{ReceiveTx(5, COIN, 100)};
    wallet->addConfirmedTx(deleted);
    wallet->addConfirmedTx(kept);

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 3);

    // Deleting the transaction leaves no wallet record behind, so a status
    // read can never succeed for it again: every one of its rows has to go,
    // and no retry may keep polling for a record that will not come back.
    wallet_ptr->m_txs.erase(deleted);
    wallet_ptr->m_statuses.erase(deleted.tx->GetHash());
    wallet_ptr->notifyTransactionChanged(deleted, CT_DELETED);
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0, 0), ActivityListModel::TimestampRole).toLongLong(), qint64{100});

    QTest::qWait(600);
    QCOMPARE(model->rowCount(), 1);
}

void ActivityListModelTests::contendedRefreshReadIsRetried()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx tx{ReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(tx, /*depth=*/1);

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);
    QCOMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 1);

    // A refresh pass that loses the wallet lock race keeps the cached
    // status, but must schedule a follow-up so the row does not stay
    // stale until the next block.
    wallet_ptr->setDepth(tx, 4);
    wallet_ptr->m_fail_status_reads = true;
    model->refreshStatuses();
    QCOMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 1);

    wallet_ptr->m_fail_status_reads = false;
    QTRY_COMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 4);
}

void ActivityListModelTests::relativeDatesRefreshWithoutANewBlock()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    wallet->addConfirmedTx(ReceiveTx(1, COIN, 100));

    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);

    // Row dates read "N minutes ago" as of the moment they are read, so the
    // model has to re-emit them as time passes; nothing else re-reads a row
    // between blocks.
    QSignalSpy changed_spy{model, &QAbstractItemModel::dataChanged};
    model->refreshDates();
    QCOMPARE(changed_spy.count(), 1);
    const auto roles{changed_spy.takeFirst().at(2).value<QList<int>>()};
    QVERIFY(roles.contains(ActivityListModel::DateTimeRole));
    QVERIFY(!roles.contains(ActivityListModel::AmountRole));

    // The refresh is wired to a timer so it happens on its own, and samples
    // several times a minute so a row does not sit on a stale age.
    const auto timers{model->findChildren<QTimer*>()};
    QVERIFY(std::any_of(timers.begin(), timers.end(), [](const QTimer* t) {
        return t->isActive() && t->interval() > 0 && t->interval() <= 30 * 1000;
    }));
}

void ActivityListModelTests::statusReadTakenBeforeTheWalletCaughtUpIsRetried()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx tx{ReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(tx, /*depth=*/1);
    wallet->m_wallet_height = 100;

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);

    // The node announces a new tip before the wallet has processed it, so the
    // read succeeds but reports the depth from the previous block. Nothing
    // re-reads a row on its own, so without a follow-up the row would show one
    // confirmation too few until the next block arrived.
    model->refreshStatuses(/*chain_height=*/101);
    QCOMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 1);

    wallet_ptr->m_wallet_height = 101;
    wallet_ptr->setDepth(tx, 2);
    QTRY_COMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 2);
}

void ActivityListModelTests::caughtUpStatusReadDoesNotRetry()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx tx{ReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(tx, /*depth=*/2);
    wallet->m_wallet_height = 101;

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};

    // A wallet level with the announced tip needs no follow-up, so a later
    // wallet-side change must not be picked up by a stray retry.
    model->refreshStatuses(/*chain_height=*/101);
    QSignalSpy changed_spy{model, &QAbstractItemModel::dataChanged};
    wallet_ptr->setDepth(tx, 9);
    QTest::qWait(600);
    QCOMPARE(changed_spy.count(), 0);
    QCOMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 2);
}

void ActivityListModelTests::tipArrivingDuringAPendingRetryIsNotLost()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx tx{ReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(tx, /*depth=*/1);
    wallet->m_wallet_height = 100;

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);

    // The wallet trails the announced tip, so a retry is scheduled chasing
    // height 101. A newer tip arrives while that retry is still pending;
    // it cannot schedule its own follow-up, so the pending retry has to
    // chase 102 rather than the height it was scheduled for.
    model->refreshStatuses(/*chain_height=*/101);
    model->refreshStatuses(/*chain_height=*/102);

    // The wallet reaches 101 first: the pending retry sees it still short
    // of 102 and must keep following up rather than settle for its
    // original target and leave every row one confirmation behind.
    wallet_ptr->m_wallet_height = 101;
    wallet_ptr->setDepth(tx, 2);
    QTRY_COMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 2);

    wallet_ptr->m_wallet_height = 102;
    wallet_ptr->setDepth(tx, 3);
    QTRY_COMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 3);
}

void ActivityListModelTests::walletAheadOfTheAnnouncedTipIsRetried()
{
    auto wallet{std::make_unique<TestActivityWallet>()};
    const interfaces::WalletTx tx{ReceiveTx(1, COIN, 100)};
    wallet->addConfirmedTx(tx, /*depth=*/2);
    wallet->m_wallet_height = 103;

    TestActivityWallet* wallet_ptr{wallet.get()};
    WalletQmlModel wallet_model{std::move(wallet)};
    ActivityListModel* model{wallet_model.activityListModel()};
    QCOMPARE(model->rowCount(), 1);

    // After a tip disconnect the node announces a tip below the wallet's
    // height. A read taken before the wallet processes the disconnect
    // reports confirmations from the abandoned chain, so any height
    // mismatch counts as out of sync, not only the wallet trailing.
    model->refreshStatuses(/*chain_height=*/102);
    QCOMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 2);

    wallet_ptr->m_wallet_height = 102;
    wallet_ptr->setDepth(tx, 1);
    QTRY_COMPARE(model->data(model->index(0, 0), ActivityListModel::DepthRole).toInt(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ActivityListModelTests)
#else
QTEST_MAIN(ActivityListModelTests)
#endif
#include "test_activitylistmodel.moc"
