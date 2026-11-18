// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/mocks/receiverequesttest.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/activitydetailmodel.h>
#include <qml/models/activityexportoperation.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/addresslistmodel.h>
#include <qml/models/coinslistmodel.h>
#include <qml/models/options_model.h>
#include <qml/models/texteditsessionmodel.h>
#include <qt/bitcoinunits.h>
#include <core_io.h>

#include <QDate>
#include <QFile>
#include <QPointer>
#include <QQuickItem>
#include <QTemporaryDir>
#include <QScopeGuard>

#include <initializer_list>

namespace {
bool SelectActivityTypes(BitcoinQmlApplication& app, std::initializer_list<const char*> types)
{
    if (!qmltest::Click(app, "activityTypeFilterButton")) return false;
    if (!qmltest::Click(app, "activityTypeAll")) return false;
    for (const auto* type : types) {
        if (!qmltest::Click(app, type)) return false;
    }
    auto* popup = qmltest::Find(app, "activityTypeFilterPopup");
    return popup && QMetaObject::invokeMethod(popup, "close") &&
           QTest::qWaitFor([&] { return !popup->property("visible").toBool(); });
}

bool ClearActivityFilters(BitcoinQmlApplication& app)
{
    if (!qmltest::Click(app, "activityActiveFiltersButton")) return false;
    if (!qmltest::Click(app, "activityClearFiltersAction")) return false;
    return QTest::qWaitFor([&] { return qmltest::Find(app, "activityPage")->property("activeFilterCount").toInt() == 0; });
}

bool ExportActivityCsv(BitcoinQmlApplication& app, const QString& path, QString& csv)
{
    auto* path_field = qmltest::Find(app, "activityExportPathField");
    if (!path_field || !path_field->setProperty("text", path)) return false;
    if (!qmltest::Click(app, "activityMoreButton")) return false;
    if (!qmltest::Click(app, "activityExportButton")) return false;

    auto* result_title = qmltest::WaitForVisible(app, "activityExportResultTitle");
    if (!result_title || result_title->property("text").toString() != "Export complete") return false;
    auto* result_description = qmltest::Find(app, "activityExportResultDescription");
    if (!result_description || result_description->property("text").toString() != "Your Activity CSV has been saved.") return false;

    QFile file{path};
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return false;
    csv = QString::fromUtf8(file.readAll());
    return qmltest::Click(app, "activityExportResultCloseButton");
}

QString ActivityCsvHeader(const QString& unit)
{
    return QStringLiteral("\"Confirmed\",\"Date\",\"Type\",\"Label\",\"Address\","
                          "\"Amount (%1)\",\"ID\",\"Record\",\"Action ID\",\"Status\","
                          "\"Action amount (%1)\"\n").arg(unit);
}
} // namespace

class ActivityIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;
    WalletQmlModel* m_wallet{nullptr};
    ActivityFilterProxyModel* m_filter{nullptr};
    QString m_request_address;
    QString m_request_id;
    QString m_reward_txid;

    QString mineBlockToAddress(const QString& address)
    {
        UniValue params{UniValue::VARR};
        params.push_back(1);
        params.push_back(address.toStdString());
        const auto blocks = qmltest::Rpc(m_app, "generatetoaddress", params);
        UniValue block_params{UniValue::VARR};
        block_params.push_back(blocks[0].get_str());
        return QString::fromStdString(qmltest::Rpc(m_app, "getblock", block_params).find_value("tx")[0].get_str());
    }

    bool createPendingRequestAndMiningReward()
    {
        const auto saved = SaveTypedRequest(*m_wallet, [](PaymentRequestEditorModel& request) {
            request.amount()->setSatoshi(10'000);
            request.publicLabel()->edit("Alice");
            request.message()->edit("pizza");
            request.privateNote()->edit("Lunch payment");
        });
        if (!saved) return false;
        m_request_address = saved->address;
        m_request_id = saved->id;

        // A reward to another address leaves the payment request pending.
        UniValue params{UniValue::VARR};
        params.push_back("Mining reward");
        const auto reward_address = QString::fromStdString(qmltest::Rpc(m_app, "getnewaddress", params, m_wallet->name()).get_str());
        m_reward_txid = mineBlockToAddress(reward_address);
        return !m_request_id.isEmpty() && !m_request_address.isEmpty() && reward_address != m_request_address &&
               QTest::qWaitFor([&] { return m_filter->rowCount() == 2; }, 30'000);
    }

    // A payer the GUI never selects, so its payments reach the selected
    // wallet as ordinary incoming transactions with exact amounts.
    QString fundPayer()
    {
        qmltest::Rpc(m_app, "createwallet", qmltest::Json("[\"payer\"]"));
        const auto address = qmltest::NewAddress(m_app, "payer");
        qmltest::Rpc(m_app, "generatetoaddress", qmltest::Json("[101,\"" + address.toStdString() + "\"]"));
        return address;
    }
    void pay(const std::string& btc) { qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + qmltest::NewAddress(m_app, m_wallet->name()).toStdString() + "\",\"amount\":" + btc + ",\"fee_rate\":1}"), "payer"); }
    bool saveRequest(CAmount amount)
    {
        return SaveTypedRequest(*m_wallet, [amount](PaymentRequestEditorModel& request) { request.amount()->setSatoshi(amount); }).has_value();
    }
    // Scrolls a real list to a row away from the top, settling its layout.
    static bool scrollTo(QQuickItem* list, int row)
    {
        if (!list || !QMetaObject::invokeMethod(list, "positionViewAtIndex", Q_ARG(int, row), Q_ARG(int, 0))) return false;
        return qmltest::WaitForPolish(list) && list->property("contentY").toReal() > 0;
    }
    // The model row shown at the top of the viewport.
    static int topRow(QQuickItem* list)
    {
        int row{-1};
        if (!list || !qmltest::WaitForPolish(list)) return row;
        QMetaObject::invokeMethod(list, "indexAt", Q_RETURN_ARG(int, row), Q_ARG(qreal, list->width() / 2),
                                  Q_ARG(qreal, list->property("contentY").toReal() + 1));
        return row;
    }
    QString addressLabel(const QString& address)
    {
        const auto labels = qmltest::Rpc(m_app, "getaddressinfo", qmltest::Json("[\"" + address.toStdString() + "\"]"), m_wallet->name()).find_value("labels");
        return labels.empty() ? QString{} : QString::fromStdString(labels[0].get_str());
    }
    static int addressRow(AddressListModel& model, const QString& address)
    {
        for (int row{0}; row < model.rowCount(); ++row) if (model.addressAt(row) == address) return row;
        return -1;
    }
    static int coinRow(CoinsListModel& model, const QString& id)
    {
        for (int row{0}; row < model.rowCount(); ++row) if (model.index(row).data(CoinsListModel::CoinIdRole).toString() == id) return row;
        return -1;
    }
    static bool dragHandle(QQuickItem* handle, int dx)
    {
        auto* window = handle ? handle->window() : nullptr; if (!window) return false;
        const QPoint start = handle->mapToScene(handle->boundingRect().center()).toPoint();
        QTest::mousePress(window, Qt::LeftButton, {}, start);
        for (int step{1}; step <= 8; ++step) QTest::mouseMove(window, start + QPoint{dx * step / 8, 0});
        QTest::mouseRelease(window, Qt::LeftButton, {}, start + QPoint{dx, 0});
        return true;
    }

public:
    explicit ActivityIntegrationTests(BitcoinQmlApplication& app) : m_app{app} {}

private Q_SLOTS:
    void init()
    {
        m_wallet = qmltest::CreateWallet(m_app);
        QVERIFY(m_wallet);
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));
        m_filter = qobject_cast<ActivityFilterProxyModel*>(qmltest::Find(m_app, "activityFilterProxyModel"));
        QVERIFY(m_filter);
    }

    void emptyWalletShowsSourceEmptyState()
    {
        auto* source = m_wallet->transactionActivityModel();
        // Source publication and filtering own separate asynchronous outcomes.
        // An idle pre-first-read view is not an observed empty source.
        QTRY_VERIFY(source->preparedSnapshot() && !source->loading() && !m_filter->busy());
        QVERIFY2(source->loadError().isEmpty(), qPrintable(source->loadError()));
        QVERIFY2(m_filter->error().isEmpty(), qPrintable(m_filter->error()));
        QCOMPARE(source->count(), 0);
        QCOMPARE(m_filter->rowCount(), 0);
        QVERIFY(qmltest::WaitForVisible(m_app, "activityEmptyState"));
        const auto title = qmltest::Find(m_app, "activityEmptyStateTitle")->property("text").toString();
        const auto description = qmltest::Find(m_app, "activityEmptyStateDescription")->property("text").toString();
        QVERIFY2(title == "No activity yet" || title == QString::fromUtf8("Syncing wallet activity…"),
            qPrintable(QStringLiteral("Actual empty-state title: %1; sourceLoading=%2 proxyBusy=%3 sourceError=%4 proxyError=%5")
                .arg(title).arg(source->loading()).arg(m_filter->busy()).arg(source->loadError(), m_filter->error())));
        QCOMPARE(description, title == "No activity yet" ? QStringLiteral("Your transactions and payment requests will appear here.")
                                                       : QStringLiteral("Transactions may appear as your wallet catches up."));
    }

    void activityReaderTracksCoinbaseMaturityAndReorgThroughPublicCoreRpc()
    {
        UniValue address_params{UniValue::VARR};
        address_params.push_back("Activity status integration");
        const QString address = QString::fromStdString(
            qmltest::Rpc(m_app, "getnewaddress", address_params, m_wallet->name()).get_str());

        UniValue mine_one{UniValue::VARR};
        mine_one.push_back(1);
        mine_one.push_back(address.toStdString());
        const auto first_block = qmltest::Rpc(m_app, "generatetoaddress", mine_one)[0].get_str();
        UniValue block_params{UniValue::VARR};
        block_params.push_back(first_block);
        const auto coinbase = QString::fromStdString(
            qmltest::Rpc(m_app, "getblock", block_params).find_value("tx")[0].get_str());

        const auto activity = m_wallet->transactionActivityModel();
        const auto row_for_coinbase = [&] {
            const int row = activity->activityRowForId(QStringLiteral("tx:") + coinbase);
            return row < 0 ? QModelIndex{} : activity->index(row, 0);
        };
        QTRY_VERIFY_WITH_TIMEOUT(row_for_coinbase().isValid(), 30'000);
        QTRY_COMPARE_WITH_TIMEOUT(row_for_coinbase().data(TransactionActivityModel::StatusRole).toInt(),
                                  int(Transaction::Immature), 30'000);
        QVERIFY(row_for_coinbase().data(TransactionActivityModel::BlocksToMaturityRole).toInt() > 0);

        UniValue mine_maturity{UniValue::VARR};
        mine_maturity.push_back(100);
        mine_maturity.push_back(address.toStdString());
        qmltest::Rpc(m_app, "generatetoaddress", mine_maturity);
        QTRY_COMPARE_WITH_TIMEOUT(row_for_coinbase().data(TransactionActivityModel::StatusRole).toInt(),
                                  int(Transaction::Confirmed), 30'000);
        QCOMPARE(row_for_coinbase().data(TransactionActivityModel::BlocksToMaturityRole).toInt(), 0);

        UniValue invalidate_params{UniValue::VARR};
        invalidate_params.push_back(first_block);
        qmltest::Rpc(m_app, "invalidateblock", invalidate_params);
        QTRY_COMPARE_WITH_TIMEOUT(row_for_coinbase().data(TransactionActivityModel::StatusRole).toInt(),
                                  int(Transaction::NotAccepted), 30'000);
        QVERIFY(row_for_coinbase().data(TransactionActivityModel::IsInactiveRole).toBool());

        UniValue reconsider_params{UniValue::VARR};
        reconsider_params.push_back(first_block);
        qmltest::Rpc(m_app, "reconsiderblock", reconsider_params);
        QTRY_COMPARE_WITH_TIMEOUT(row_for_coinbase().data(TransactionActivityModel::StatusRole).toInt(),
                                  int(Transaction::Confirmed), 30'000);
        QVERIFY(!row_for_coinbase().data(TransactionActivityModel::IsInactiveRole).toBool());
    }

    // #1072: a fee-bumped self-payment whose original is mined, then
    // disconnected, leaves the original as the only version in Core's mempool
    // while it still stores its replacement marker. Mempool membership, not
    // that marker, decides what is pending: the original is the one pending
    // payment and keeps the saved request's received amount; the evicted
    // replacement (depth zero, outside the mempool, like an expired payment)
    // is inactive and adds nothing to the pending total.
    void reorgReturnedOriginalIsTheOnlyPendingVersionNotItsEvictedReplacement()
    {
        QVERIFY(qmltest::FundWallet(m_app, *m_wallet)); const auto name = m_wallet->name();
        const auto saved = SaveTypedRequest(*m_wallet, [](PaymentRequestEditorModel& request) { request.amount()->setSatoshi(2'000'000); });
        QVERIFY(saved); const auto id = saved->id, address = saved->address;
        const auto detail = OpenTypedRequest(*m_wallet, id); QVERIFY(detail);
        const auto original = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address.toStdString() + "\",\"amount\":0.01,\"fee_rate\":1}"), name).get_str();
        const auto replacement = qmltest::Rpc(m_app, "bumpfee", qmltest::Json("[\"" + original + "\",{\"fee_rate\":20}]"), name).find_value("txid").get_str();
        const auto hex = qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + original + "\"]"), name).find_value("hex").get_str();
        const auto block = qmltest::Rpc(m_app, "generateblock", qmltest::Json("[\"" + qmltest::NewAddress(m_app, name).toStdString() + "\",[\"" + hex + "\"]]")).find_value("hash").get_str();
        UniValue block_params{UniValue::VARR}; block_params.push_back(block);
        const auto reconsider = qScopeGuard([&] { qmltest::Rpc(m_app, "reconsiderblock", block_params); });
        qmltest::Rpc(m_app, "invalidateblock", block_params); qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        // Core's own view: only the original is back in the mempool, and it
        // still carries the historical replacement marker.
        const auto mempool = qmltest::Rpc(m_app, "getrawmempool").write();
        QVERIFY(mempool.find(original) != std::string::npos); QVERIFY(mempool.find(replacement) == std::string::npos);
        QCOMPARE(qmltest::Rpc(m_app, "gettransaction", qmltest::Json("[\"" + original + "\"]"), name).find_value("replaced_by_txid").get_str(), replacement);
        const auto activity = m_wallet->transactionActivityModel();
        const auto row = [&](const std::string& txid, int role) {
            const int index = activity->activityRowForId(QStringLiteral("tx:") + QString::fromStdString(txid));
            return index < 0 ? QVariant{} : activity->index(index, 0).data(role);
        };
        QTRY_VERIFY_WITH_TIMEOUT(row(original, TransactionActivityModel::StatusKnownRole).toBool() && row(original, TransactionActivityModel::DepthRole).toInt() == 0 &&
                                 row(replacement, TransactionActivityModel::StatusKnownRole).toBool() && row(replacement, TransactionActivityModel::DepthRole).toInt() == 0, 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(row(original, TransactionActivityModel::IsPendingRole).toBool(), 30'000);
        QVERIFY(!row(original, TransactionActivityModel::IsInactiveRole).toBool());
        QVERIFY(row(replacement, TransactionActivityModel::IsInactiveRole).toBool());
        QVERIFY(!row(replacement, TransactionActivityModel::IsPendingRole).toBool());
        QTRY_VERIFY(!m_filter->busy());
        QTRY_COMPARE(m_filter->pendingBalanceSat(), CAmount{row(original, TransactionActivityModel::NetAmountSatRole).toLongLong()});
        // The saved request counts the active original's payment.
        QTRY_COMPARE_WITH_TIMEOUT(detail->receipts()->receivedAmountSatoshi(), qint64{1'000'000}, 30'000);
    }

    // Valera (#1072): another wallet bumps the payment this wallet receives.
    // Core notifies only the replacement; the original leaves the mempool
    // without a notification. Pending counts the payment once.
    void incomingReplacementIsTheOnlyPendingPayment()
    {
        const auto receiver = m_wallet->name();
        UniValue create{UniValue::VARR}; create.push_back("rbf-sender");
        qmltest::Rpc(m_app, "createwallet", create);
        UniValue mine{UniValue::VARR}; mine.push_back(101); mine.push_back(qmltest::NewAddress(m_app, "rbf-sender").toStdString());
        qmltest::Rpc(m_app, "generatetoaddress", mine);
        const auto address = qmltest::NewAddress(m_app, receiver).toStdString();
        const auto original = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address + "\",\"amount\":0.01,\"fee_rate\":1}"), "rbf-sender").get_str();
        const auto activity = m_wallet->transactionActivityModel();
        const auto row = [&](const std::string& txid, int role) {
            const int index = activity->activityRowForId(QStringLiteral("tx:") + QString::fromStdString(txid));
            return index < 0 ? QVariant{} : activity->index(index, 0).data(role);
        };
        QTRY_VERIFY_WITH_TIMEOUT(row(original, TransactionActivityModel::IsPendingRole).toBool(), 30'000);
        QTRY_COMPARE(m_filter->pendingBalanceSat(), CAmount{1'000'000});
        const auto replacement = qmltest::Rpc(m_app, "bumpfee", qmltest::Json("[\"" + original + "\",{\"fee_rate\":20}]"), "rbf-sender").find_value("txid").get_str();
        qmltest::Rpc(m_app, "syncwithvalidationinterfacequeue");
        QTRY_VERIFY_WITH_TIMEOUT(row(replacement, TransactionActivityModel::IsPendingRole).toBool(), 30'000);
        QTRY_VERIFY(!row(original, TransactionActivityModel::IsPendingRole).toBool());
        QTRY_VERIFY(!m_filter->busy());
        QCOMPARE(m_filter->pendingBalanceSat(), CAmount{1'000'000});
    }

    // Valera (#1072): a pending payment that expires from the mempool leaves
    // Pending without a new block. Expiry sends no wallet notification.
    void expiredPaymentLeavesPendingWithoutABlock()
    {
        const auto receiver = m_wallet->name();
        UniValue create{UniValue::VARR}; create.push_back("expiry-sender");
        qmltest::Rpc(m_app, "createwallet", create);
        const auto sender = qmltest::NewAddress(m_app, "expiry-sender").toStdString();
        UniValue mine{UniValue::VARR}; mine.push_back(101); mine.push_back(sender);
        qmltest::Rpc(m_app, "generatetoaddress", mine);
        const auto address = qmltest::NewAddress(m_app, receiver).toStdString();
        const auto payment = qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + address + "\",\"amount\":0.01,\"fee_rate\":1}"), "expiry-sender").get_str();
        const auto activity = m_wallet->transactionActivityModel();
        const auto pending = [&] {
            const int index = activity->activityRowForId(QStringLiteral("tx:") + QString::fromStdString(payment));
            return index >= 0 && activity->index(index, 0).data(TransactionActivityModel::IsPendingRole).toBool();
        };
        QTRY_VERIFY_WITH_TIMEOUT(pending(), 30'000);
        QTRY_COMPARE(m_filter->pendingBalanceSat(), CAmount{1'000'000});
        // Past the default 336-hour expiry, the next accepted transaction
        // trims the mempool. No block is mined.
        const auto now = qmltest::Rpc(m_app, "getblockchaininfo").find_value("mediantime").getInt<int64_t>();
        UniValue later{UniValue::VARR}; later.push_back(now + 15 * 24 * 60 * 60);
        qmltest::Rpc(m_app, "setmocktime", later);
        const auto reset = qScopeGuard([&] { UniValue zero{UniValue::VARR}; zero.push_back(0); qmltest::Rpc(m_app, "setmocktime", zero); });
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + sender + "\",\"amount\":1,\"fee_rate\":2}"), "expiry-sender");
        QVERIFY(qmltest::Rpc(m_app, "getrawmempool").write().find(payment) == std::string::npos);
        QTRY_VERIFY_WITH_TIMEOUT(!pending(), 10'000);
        QTRY_VERIFY(!m_filter->busy());
        QCOMPARE(m_filter->pendingBalanceSat(), CAmount{0});
    }

    void searchAndTypeFiltersExportOnlyMatchingLiveRows()
    {
        QVERIFY(createPendingRequestAndMiningReward());

        // Search isolates the pending request, then clearing it restores both rows.
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", m_request_address));
        QTRY_COMPARE(m_filter->rowCount(), 1);
        QCOMPARE(m_filter->index(0, 0).data(TransactionActivityModel::RequestIdRole).toString(), m_request_id);
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", ""));
        QTRY_COMPARE(m_filter->rowCount(), 2);

        // Export only today's mining reward.
        QVERIFY(qmltest::Click(m_app, "activityDateFilterButton"));
        QVERIFY(qmltest::Click(m_app, "activityDateToday"));
        QCOMPARE(m_filter->dateFilter(), ActivityFilterProxyModel::Today);
        QVERIFY(SelectActivityTypes(m_app, {"activityTypeMined"}));
        QTRY_COMPARE(m_filter->rowCount(), 1);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString csv;
        QVERIFY(ExportActivityCsv(m_app, directory.filePath("mined.csv"), csv));
        QVERIFY(csv.startsWith(ActivityCsvHeader("BTC")));
        QVERIFY(csv.contains("\"Mined\""));
        QVERIFY(csv.contains("\"Immature\""));
        QVERIFY(csv.contains("\"50.00000000\""));
        QVERIFY(csv.contains(m_reward_txid));
        QVERIFY(!csv.contains("\"Payment request\""));

        // Type selections combine, and label search still finds the request.
        QVERIFY(SelectActivityTypes(m_app, {"activityTypeMined", "activityTypePaymentRequest"}));
        QTRY_COMPARE(m_filter->rowCount(), 2);
        QVERIFY(qmltest::Find(m_app, "activityTypeMined")->property("selected").toBool());
        QVERIFY(qmltest::Find(m_app, "activityTypePaymentRequest")->property("selected").toBool());
        QVERIFY(SelectActivityTypes(m_app, {"activityTypePaymentRequest"}));
        QTRY_COMPARE(m_filter->rowCount(), 1);
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", "Alice"));
        QTRY_COMPARE(m_filter->rowCount(), 1);
        QCOMPARE(m_filter->index(0, 0).data(TransactionActivityModel::RequestIdRole).toString(), m_request_id);
    }

    void sharedSendReviewAddressFormattingFindsTheOriginalActivityAndCsv()
    {
        QVERIFY(createPendingRequestAndMiningReward());
        const auto displayed = BitcoinAddress::formattedAddress(m_request_address); QVERIFY(displayed.contains(' '));
        // Send review uses this exact shared formatter through its typed
        // recipient entry. Core independently proves the canonical address.
        UniValue params{UniValue::VARR}; params.push_back(m_request_address.toStdString());
        QVERIFY(qmltest::Rpc(m_app, "getaddressinfo", params, m_wallet->name()).find_value("ismine").get_bool());
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", displayed));
        QTRY_VERIFY(!m_filter->busy()); QTRY_COMPARE(m_filter->rowCount(), 1);
        QCOMPARE(m_filter->index(0).data(TransactionActivityModel::RequestIdRole).toString(), m_request_id);
        QTemporaryDir directory; QVERIFY(directory.isValid()); QString csv;
        QVERIFY(ExportActivityCsv(m_app, directory.filePath("grouped-address.csv"), csv));
        QVERIFY(csv.contains('"' + m_request_address + '"')); QVERIFY(!csv.contains(m_reward_txid));
    }

    void csvConsentKeepsItsOriginalSnapshotUnitAndTargetAcrossLiveFilterChanges()
    {
        QVERIFY(createPendingRequestAndMiningReward()); QTRY_VERIFY(!m_filter->busy());
        const int original_unit = m_filter->displayUnit(); m_filter->setDisplayUnit(0); QTRY_VERIFY(!m_filter->busy());
        const auto restore = qScopeGuard([&] { m_filter->setDisplayUnit(original_unit); });
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("captured.csv");
        { QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write("original destination"), qint64{20}); }
        auto* field = qmltest::Find(m_app, "activityExportPathField"); QVERIFY(field); QVERIFY(field->setProperty("text", path));
        QVERIFY(qmltest::Click(m_app, "activityMoreButton")); QVERIFY(qmltest::Click(m_app, "activityExportButton"));
        QVERIFY(qmltest::WaitForVisible(m_app, "activityCsvApproveDestination"));
        auto* original = m_filter->exportOperation(); QVERIFY(original); QVERIFY(original->confirmationRequired());
        const auto revision = original->snapshotRevision(); const auto operation = original->operationId();
        m_filter->setSearchText(m_request_address); m_filter->setDisplayUnit(3); QTRY_VERIFY(!m_filter->busy()); QTRY_COMPARE(m_filter->rowCount(), 1);
        QCOMPARE(m_filter->exportOperation(), original); QCOMPARE(original->snapshotRevision(), revision); QCOMPARE(original->displayUnit(), 0);
        QVERIFY(qmltest::Click(m_app, "activityCsvApproveDestination")); QTRY_VERIFY(original->resultPresent()); QVERIFY(original->saved()); QVERIFY(original->outcomeKnown());
        QCOMPARE(original->operationId(), operation); QCOMPARE(original->finalPath(), path);
        { QFile file{path}; QVERIFY(file.open(QIODevice::ReadOnly)); const auto csv = QString::fromUtf8(file.readAll()); QVERIFY(csv.startsWith(ActivityCsvHeader("BTC"))); QVERIFY(csv.contains(m_reward_txid)); QVERIFY(csv.contains(m_request_address)); }
        QVERIFY(qmltest::Click(m_app, "activityExportResultCloseButton")); QString successor;
        QVERIFY(ExportActivityCsv(m_app, directory.filePath("successor.csv"), successor)); QVERIFY(successor.startsWith(ActivityCsvHeader("sat"))); QVERIFY(!successor.contains(m_reward_txid));
    }

    void requestOnlyAmountControlsReachTheSameRowsTheyFilterWithoutAddingPendingBalance()
    {
        QVERIFY(SaveTypedRequest(*m_wallet, [](PaymentRequestEditorModel& request) { request.amount()->setSatoshi(9000); }));
        QTRY_COMPARE(m_filter->rowCount(), 1); QTRY_VERIFY(!m_filter->busy());
        QCOMPARE(m_filter->requestCount(), 1); QCOMPARE(m_filter->pendingBalanceSat(), CAmount{0}); QCOMPARE(m_filter->availableMaxAmount(), CAmount{9000});
        QVERIFY(qmltest::Click(m_app, "activityAmountFilterButton")); auto* slider = qmltest::Find(m_app, "activityAmountRangeSlider"); QVERIFY(slider);
        QCOMPARE(slider->property("maxValue").toLongLong(), qint64{9000}); QVERIFY(slider->property("enabled").toBool());
        QVERIFY(m_filter->setAmountRange(0, 4500)); QTRY_VERIFY(!m_filter->busy()); QCOMPARE(m_filter->rowCount(), 0); QCOMPARE(m_filter->availableMaxAmount(), CAmount{9000}); QCOMPARE(m_filter->pendingBalanceSat(), CAmount{0});
        QCOMPARE(slider->property("upperValue").toLongLong(), qint64{4500});
    }
    // #1095: in mixed history, request amounts above every transaction stay
    // inside the slider range while the Payment request type filter is on.
    void requestAmountsAboveTransactionsStayReachableUnderThePaymentRequestFilter()
    {
        QVERIFY(saveRequest(200'000)); QVERIFY(saveRequest(500'000));
        const auto payer = fundPayer(); QCOMPARE(qmltest::SelectOpenWalletForSetup(m_app, m_wallet->name()), m_wallet);
        pay("0.001"); mineBlockToAddress(payer);
        QTRY_VERIFY_WITH_TIMEOUT(!m_filter->busy() && m_filter->transactionCount() == 1 && m_filter->requestCount() == 2, 30'000);
        QCOMPARE(m_filter->availableMaxAmount(), CAmount{500'000});
        QVERIFY(SelectActivityTypes(m_app, {"activityTypePaymentRequest"}));
        QTRY_VERIFY(!m_filter->busy() && m_filter->rowCount() == 2 && m_filter->requestCount() == 2);
        QVERIFY(qmltest::Click(m_app, "activityAmountFilterButton")); auto* slider = qmltest::Find(m_app, "activityAmountRangeSlider"); QVERIFY(slider);
        QVERIFY(slider->property("enabled").toBool()); QCOMPARE(slider->property("maxValue").toLongLong(), qint64{500'000});
        QCOMPARE(qRound64(slider->property("upperValue").toDouble()), qint64{500'000});
        QVERIFY(slider->setProperty("lowerValue", 300'000)); QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "activityPage"), "applyAmountRange"));
        QCOMPARE(m_filter->minAmount(), CAmount{300'000}); QTRY_VERIFY(!m_filter->busy() && m_filter->rowCount() == 1);
        QCOMPARE(m_filter->index(0, 0).data(TransactionActivityModel::NetAmountSatRole).toLongLong(), qint64{500'000});
        QVERIFY(m_filter->index(0, 0).data(TransactionActivityModel::IsPendingRequestRole).toBool());
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "activityAmountFilterPopup"), "close"));
    }
    // #1095: a larger incoming coin raises the View coins range without moving
    // the dragged maximum away from the filter it applies.
    void coinAmountRangeKeepsTheDraggedMaximumWhenALargerCoinArrives()
    {
        const auto payer = fundPayer(); QCOMPARE(qmltest::SelectOpenWalletForSetup(m_app, m_wallet->name()), m_wallet);
        pay("0.08"); pay("0.09"); mineBlockToAddress(payer);
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::Click(m_app, "walletCoinsRow")); QVERIFY(qmltest::WaitForVisible(m_app, "walletCoinsPage"));
        auto* coins = m_wallet->coinsListModel(); QVERIFY(coins);
        QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coins->coinCount() == 2 && coins->availableMaxAmount() == 9'000'000, 30'000);
        QVERIFY(qmltest::Click(m_app, "coinBrowserAmountFilterButton"));
        auto* popup = qmltest::Find(m_app, "coinBrowserAmountFilterPopup"); QVERIFY(popup); QTRY_VERIFY(popup->property("opened").toBool());
        auto* slider = qobject_cast<QQuickItem*>(qmltest::Find(m_app, "coinBrowserAmountRangeSlider")); QVERIFY(slider); QVERIFY(qmltest::WaitForPolish(slider));
        QTRY_VERIFY(qmltest::TransitionsFinished(popup));
        auto* upper = slider->property("second").value<QObject*>()->property("handle").value<QQuickItem*>(); QVERIFY(upper);
        QVERIFY(dragHandle(upper, -int(slider->width() / 2)));
        QTRY_VERIFY(coins->maxAmount() > 8'000'000 && coins->maxAmount() < 9'000'000);
        const auto selected = coins->maxAmount(); QTRY_COMPARE(coins->rowCount(), 1);
        QCOMPARE(qRound64(slider->property("upperValue").toDouble()), selected);
        pay("0.2"); mineBlockToAddress(payer);
        QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coins->coinCount() == 3 && coins->availableMaxAmount() == 20'000'000, 30'000);
        QTRY_COMPARE(slider->property("maxValue").toLongLong(), qint64{20'000'000});
        QCOMPARE(coins->maxAmount(), selected); QCOMPARE(qRound64(slider->property("upperValue").toDouble()), selected);
        QCOMPARE(coins->rowCount(), 1); QVERIFY(popup->property("opened").toBool());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    }

    // #1084: a wallet notification, the user's own label save and a unit
    // toggle update Addresses in place; the list keeps the address the user
    // scrolled to at the top instead of returning to the first row.
    void addressListKeepsItsScrollPositionAcrossRefreshes()
    {
        const auto payer = fundPayer(); QCOMPARE(qmltest::SelectOpenWalletForSetup(m_app, m_wallet->name()), m_wallet);
        QStringList addresses; for (int i{0}; i < 40; ++i) addresses << qmltest::NewAddress(m_app, m_wallet->name());
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options);
        const auto restore = qScopeGuard([options] { options->setDisplayUnit(0); });
        auto* model = m_wallet->addressListModel(); QVERIFY(model);
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::Click(m_app, "walletAddressesRow"));
        auto* page = qmltest::WaitForVisible(m_app, "addressListPage"); QVERIFY(page);
        model->setShowUsed(true);
        QTRY_VERIFY_WITH_TIMEOUT(!model->refreshPending() && model->count() >= 40, 30'000);
        auto* list = qobject_cast<QQuickItem*>(qmltest::Find(page, "addressListView")); QVERIFY(list);
        QVERIFY(scrollTo(list, 20)); const auto top = model->addressAt(topRow(list)); QVERIFY(!top.isEmpty());
        QVERIFY(top != model->addressAt(0));
        const auto keeps = [&] { return topRow(list) >= 0 && model->addressAt(topRow(list)) == top && list->property("contentY").toReal() > 0; };
        // Wallet notification: a payment to another listed address.
        const auto paid = model->addressAt(model->count() - 1); QVERIFY(paid != top);
        qmltest::Rpc(m_app, "sendtoaddress", qmltest::Json("{\"address\":\"" + paid.toStdString() + "\",\"amount\":0.001,\"fee_rate\":1}"), "payer");
        mineBlockToAddress(payer);
        QTRY_VERIFY_WITH_TIMEOUT(addressRow(*model, paid) >= 0 && model->index(addressRow(*model, paid)).data(AddressListModel::HasAmountRole).toBool(), 30'000);
        QTRY_VERIFY(!model->refreshPending()); QVERIFY(keeps());
        // The user's own label save on another row.
        const auto renamed = model->addressAt(model->count() - 2); QVERIFY(renamed != top);
        // A save against a row snapshot the payment refresh just replaced is
        // refused as stale by design; the user retries, as the test does.
        QTRY_VERIFY_WITH_TIMEOUT(AwaitReceiveRequest(model->setAddressLabel(renamed, "Renamed while scrolled")), 30'000);
        QTRY_VERIFY_WITH_TIMEOUT(addressRow(*model, renamed) >= 0 && model->index(addressRow(*model, renamed)).data(AddressListModel::LabelRole).toString() == "Renamed while scrolled", 30'000);
        QTRY_VERIFY(!model->refreshPending()); QVERIFY(keeps());
        // A display unit toggle reformats the paid row's amount.
        const auto amount = model->index(addressRow(*model, paid)).data(AddressListModel::DisplayAmountRole).toString();
        options->setDisplayUnit(3);
        QTRY_VERIFY_WITH_TIMEOUT(model->index(addressRow(*model, paid)).data(AddressListModel::DisplayAmountRole).toString() != amount, 30'000);
        QTRY_VERIFY(!model->refreshPending()); QVERIFY(keeps());
    }

    // register draft-190-RC49: a details save that Core refuses, because the
    // wallet label changed underneath it, keeps the typed note as an unsaved
    // draft with an error rather than asserting it, while the row shows Core's
    // label. Create payment request stays unavailable until the draft is
    // resolved, so it cannot start a request from a label the box hides.
    void refusedAddressDetailsSaveKeepsTheDraftAndBlocksCreate()
    {
        const auto name = m_wallet->name(); const auto address = qmltest::NewAddress(m_app, name);
        qmltest::Rpc(m_app, "setlabel", qmltest::Json("[\"" + address.toStdString() + "\",\"Original note\"]"), name);
        auto* model = m_wallet->addressListModel(); QVERIFY(model);
        QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::Click(m_app, "walletAddressesRow"));
        auto* page = qmltest::WaitForVisible(m_app, "addressListPage"); QVERIFY(page);
        const auto row_item = [&]() -> QObject* {
            QList<QQuickItem*> pending{m_app.window()->contentItem()};
            while (!pending.isEmpty()) {
                auto* item = pending.takeLast();
                if (item->isVisible() && item->objectName() == "addressRow" && item->property("address").toString() == address) return item;
                pending.append(item->childItems());
            }
            return nullptr;
        };
        const auto row_label = [&] { const int row = addressRow(*model, address); return row < 0 ? QString{} : model->index(row).data(AddressListModel::LabelRole).toString(); };
        QTRY_VERIFY_WITH_TIMEOUT(row_item() && row_label() == "Original note", 30'000);
        QVERIFY(qmltest::Click(qmltest::Find(row_item(), "addressRowDetailsButton")));
        auto* popup = qmltest::WaitForVisible(m_app, "addressDetailsPopup"); QVERIFY(popup);
        auto* field = qmltest::WaitForVisible(m_app, "addressDetailsNoteField"); QVERIFY(field);
        auto* create = qmltest::WaitForVisible(m_app, "addressDetailsCreatePaymentRequestButton"); QVERIFY(create);
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Original note"));
        QVERIFY(create->property("enabled").toBool());
        QVERIFY(qmltest::SetText(m_app, "addressDetailsNoteField", "Typed note"));
        auto* editor = model->labelEditor(address); QVERIFY(editor); QTRY_VERIFY(editor->dirty() && editor->canSave());
        qmltest::Rpc(m_app, "setlabel", qmltest::Json("[\"" + address.toStdString() + "\",\"Changed in Core\"]"), name);
        editor->save();
        QTRY_VERIFY_WITH_TIMEOUT(!editor->busy() && !editor->error().message.isEmpty(), 30'000);
        QCOMPARE(addressLabel(address), QStringLiteral("Changed in Core"));
        QTRY_COMPARE_WITH_TIMEOUT(row_label(), QStringLiteral("Changed in Core"), 30'000);
        // The box keeps the draft as unsaved, with the failure, not as saved.
        QVERIFY(editor->dirty()); QCOMPARE(field->property("text").toString(), QStringLiteral("Typed note"));
        QVERIFY(!create->property("enabled").toBool());
        // Discarding shows Core's label, and only then can Create start.
        editor->discard();
        QTRY_COMPARE(field->property("text").toString(), QStringLiteral("Changed in Core"));
        QTRY_VERIFY(create->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(popup, "close"));
    }

    // #1084: View coins and Manual selection keep the coin the user scrolled
    // to across a wallet notification (a new coin), the user's own coin lock
    // and a unit toggle.
    void coinListsKeepTheirScrollPositionAcrossRefreshes_data()
    {
        QTest::addColumn<bool>("manual");
        QTest::newRow("view-coins") << false;
        QTest::newRow("manual-selection") << true;
    }
    void coinListsKeepTheirScrollPositionAcrossRefreshes()
    {
        QFETCH(bool, manual);
        const auto payer = fundPayer(); QCOMPARE(qmltest::SelectOpenWalletForSetup(m_app, m_wallet->name()), m_wallet);
        UniValue outputs{UniValue::VOBJ};
        for (int i{0}; i < 40; ++i) outputs.pushKV(qmltest::NewAddress(m_app, m_wallet->name()).toStdString(), ValueFromAmount(100'000 + i * 1'000));
        UniValue send{UniValue::VARR}; send.push_back(""); send.push_back(outputs); for (int i{0}; i < 6; ++i) send.push_back(UniValue{}); send.push_back(1);
        qmltest::Rpc(m_app, "sendmany", send, "payer"); mineBlockToAddress(payer);
        auto* options = qmltest::Model<OptionsQmlModel>(m_app, "optionsModel"); QVERIFY(options);
        const auto restore = qScopeGuard([options] { options->setDisplayUnit(0); });
        auto* coins = m_wallet->coinsListModel(); QVERIFY(coins);
        QPointer<QObject> popup;
        const auto close = qScopeGuard([&] { if (popup) QMetaObject::invokeMethod(popup, "close"); });
        QQuickItem* list{nullptr};
        if (manual) {
            QVERIFY(qmltest::Click(m_app, "sendTabButton"));
            popup = qmltest::Find(m_app, "coinSelectionPopup"); QVERIFY(popup);
            QVERIFY(QMetaObject::invokeMethod(popup, "open")); QTRY_VERIFY(popup->property("opened").toBool());
            list = qobject_cast<QQuickItem*>(qmltest::Find(qmltest::Find(m_app, "sendCoinsList"), "coinSelectionListView"));
        } else {
            QVERIFY(qmltest::SelectSettings(m_app, "wallet")); QVERIFY(qmltest::Click(m_app, "walletCoinsRow"));
            auto* page = qmltest::WaitForVisible(m_app, "walletCoinsPage"); QVERIFY(page);
            list = qobject_cast<QQuickItem*>(qmltest::Find(page, "coinSelectionListView"));
        }
        QVERIFY(list);
        QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coins->coinCount() == 40 && coins->rowCount() == 40, 30'000);
        QVERIFY(scrollTo(list, 20));
        const auto id = [&](int row) { return row < 0 ? QString{} : coins->index(row).data(CoinsListModel::CoinIdRole).toString(); };
        const auto top = id(topRow(list)); QVERIFY(!top.isEmpty()); QVERIFY(top != id(0));
        const auto keeps = [&] { return id(topRow(list)) == top && list->property("contentY").toReal() > 0; };
        // Wallet notification: a new coin arrives.
        pay("0.5"); mineBlockToAddress(payer);
        QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coins->coinCount() == 41 && coins->rowCount() == 41, 30'000);
        QVERIFY(keeps());
        // The user's own lock of another coin.
        const auto locked = id(coins->rowCount() - 1); QVERIFY(locked != top);
        QVERIFY(coins->setCoinsLocked({locked}, true)); QTRY_VERIFY(!coins->lockPending());
        QTRY_VERIFY_WITH_TIMEOUT(!coins->loading() && coinRow(*coins, locked) >= 0 && coins->index(coinRow(*coins, locked)).data(CoinsListModel::LockedRole).toBool(), 30'000);
        QVERIFY(keeps());
        // A display unit toggle.
        options->setDisplayUnit(3); QTRY_COMPARE(m_wallet->displayUnit(), 3);
        QTRY_VERIFY(!coins->loading()); QVERIFY(keeps());
    }

    void displayUnitFlowsFromSettingsToExportAndDetails()
    {
        QVERIFY(createPendingRequestAndMiningReward());
        QVERIFY(SelectActivityTypes(m_app, {"activityTypePaymentRequest"}));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", "Alice"));
        QTRY_COMPARE(m_filter->rowCount(), 1);
        QVERIFY(qmltest::Click(m_app, "activityDateFilterButton"));
        QVERIFY(qmltest::Click(m_app, "activityDateToday"));
        QTRY_VERIFY(!qmltest::Find(m_app, "activityDateFilterPopup")->property("visible").toBool());
        QVERIFY(qmltest::SelectSettings(m_app, "display"));
        QVERIFY(qmltest::Click(m_app, "displayUnitPickerButton"));
        QVERIFY(qmltest::Click(m_app, "displayUnitSAT"));
        QVERIFY(qmltest::Click(m_app, "activityTabButton"));

        // Changing the unit keeps the active search and filters.
        QTRY_COMPARE(m_filter->displayUnit(), static_cast<int>(BitcoinUnits::Unit::SAT));
        QCOMPARE(m_filter->searchText(), QStringLiteral("Alice"));
        QCOMPARE(m_filter->typeFilters(), QList<int>{ActivityFilterProxyModel::PaymentRequest});
        QCOMPARE(m_filter->dateFilter(), ActivityFilterProxyModel::Today);
        QTRY_COMPARE(m_filter->rowCount(), 1);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString csv;
        QVERIFY(ExportActivityCsv(m_app, directory.filePath("request.csv"), csv));
        QVERIFY(csv.startsWith(ActivityCsvHeader("sat")));
        QVERIFY(csv.contains("\"Payment request\""));
        QVERIFY(csv.contains("\"Lunch payment\""));
        QVERIFY(csv.contains('"' + m_request_address + '"'));
        QVERIFY(csv.contains("\"10000\""));
        QVERIFY(!csv.contains("\"Mined\""));

        // The list and transaction details must use the same display unit.
        QVERIFY(SelectActivityTypes(m_app, {}));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", m_reward_txid));
        QTRY_COMPARE(m_filter->rowCount(), 1);
        const QString row_name = QStringLiteral("activityItem_") + m_reward_txid;
        const QPointer<QObject> global_row{qmltest::WaitForVisible(m_app, qPrintable(row_name))};
        QVERIFY(global_row);
        auto* list = qmltest::Find(m_app, "activityListView"); QVERIFY(list);
        QCOMPARE(list->property("model").value<QObject*>(), static_cast<QObject*>(m_filter));
        // QObject traversal includes pooled delegates, whose visible flag and
        // old role values can survive model detachment while Settings is open.
        // Inspect the delegate currently assigned to the selected model row.
        const auto activeRow = [&]() -> QObject* {
            QQuickItem* delegate{nullptr};
            if (!QMetaObject::invokeMethod(list, "itemAtIndex", Qt::DirectConnection,
                    Q_RETURN_ARG(QQuickItem*, delegate), Q_ARG(int, 0)) || !delegate) return nullptr;
            return qmltest::Find(delegate, row_name);
        };
        QPointer<QObject> row;
        QTRY_VERIFY((row = activeRow()));
        if (global_row != row) qInfo() << "Pooled Activity lookup:" << global_row.data()
            << "parent" << (global_row ? global_row->parent() : nullptr)
            << "amount" << (global_row ? global_row->property("amount") : QVariant{})
            << "active" << row.data() << "parent" << row->parent() << "amount" << row->property("amount");
        QTRY_VERIFY((row = activeRow()) && row->property("activityId").toString() == m_filter->index(0, 0).data(TransactionActivityModel::IdRole).toString());
        QCOMPARE(row->property("txid").toString(), m_reward_txid);
        QTRY_COMPARE(([&] { row = activeRow(); return row ? row->property("amount").toString() : QString{}; })(),
                     m_filter->index(0, 0).data(TransactionActivityModel::AmountRole).toString());
        QVERIFY(row);
        QCOMPARE(row->property("activityId").toString(), m_filter->index(0, 0).data(TransactionActivityModel::IdRole).toString());
        QCOMPARE(row->property("txid").toString(), m_reward_txid);
        const auto amount = row->property("amount").toString();
        QVERIFY(!amount.isEmpty());
        QVERIFY(qmltest::Click(qmltest::Find(row, "activityRowOpenButton")));
        auto* details = qmltest::WaitForVisible(m_app, "activityDetailsPage");
        QVERIFY(details);
        auto* detail_model = qobject_cast<ActivityDetailModel*>(details->property("activityModel").value<QObject*>());
        QVERIFY(detail_model);
        // The page's ActivityDetailController owns the page's detail model.
        auto* controller = qobject_cast<ActivityDetailController*>(detail_model->parent());
        QVERIFY(controller);
        const auto owned_by_page = [&] { for (auto* object = controller->parent(); object; object = object->parent()) if (object == details) return true; return false; };
        QVERIFY(owned_by_page());
        QCOMPARE(detail_model->transactionId(), m_reward_txid);
        QTRY_VERIFY(!detail_model->pending());
        QVERIFY(detail_model->error().isEmpty());
        QCOMPARE(detail_model->selectedTransactionDetails().value("txid").toString(), m_reward_txid);
        QVERIFY(detail_model->detailOutputs()->rowCount() > 0);
        QTRY_COMPARE(details->property("amount").toString(), amount);
    }

    void clearingFiltersPreservesSearchAndRestoresRows()
    {
        QVERIFY(createPendingRequestAndMiningReward());
        QVERIFY(SelectActivityTypes(m_app, {"activityTypePaymentRequest"}));
        QVERIFY(qmltest::Click(m_app, "activityDateFilterButton"));
        QVERIFY(qmltest::Click(m_app, "activityDateToday"));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", "not-present"));
        QTRY_COMPARE(m_filter->rowCount(), 0); QTRY_VERIFY(!m_filter->busy());
        QVERIFY(qmltest::WaitForVisible(m_app, "activityEmptyState"));
        QCOMPARE(qmltest::Find(m_app, "activityEmptyStateTitle")->property("text").toString(), QStringLiteral("No activity matches your filters."));
        QCOMPARE(qmltest::Find(m_app, "activityEmptyStateDescription")->property("text").toString(), QStringLiteral("Try changing your search, date, type, or amount filters."));

        // Clearing filters keeps the independent search query and its empty result.
        QVERIFY(ClearActivityFilters(m_app));
        QCOMPARE(m_filter->searchText(), QStringLiteral("not-present"));
        QCOMPARE(m_filter->rowCount(), 0);
        QCOMPARE(m_filter->dateFilter(), ActivityFilterProxyModel::DateAll);
        QVERIFY(m_filter->typeFilters().isEmpty());
        QVERIFY(qmltest::Find(m_app, "activityTypeAll")->property("selected").toBool());

        QVERIFY(qmltest::Click(m_app, "activityClearSearchButton"));
        QTRY_COMPARE(m_filter->searchText(), QString{});
        QTRY_COMPARE(m_filter->rowCount(), 2);
    }

    void amountRangeFiltersLiveRowsAndCsv()
    {
        QVERIFY(createPendingRequestAndMiningReward());

        // The minimum excludes the 10,000 sat request but keeps the mining reward.
        QVERIFY(qmltest::Click(m_app, "activityAmountFilterButton"));
        auto* slider = qmltest::Find(m_app, "activityAmountRangeSlider");
        QVERIFY(slider);
        QVERIFY(slider->setProperty("lowerValue", 100'000));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "activityPage"), "applyAmountRange"));
        QVERIFY(QMetaObject::invokeMethod(qmltest::Find(m_app, "activityAmountFilterPopup"), "close"));
        QCOMPARE(m_filter->minAmount(), 100'000);
        QTRY_COMPARE(m_filter->rowCount(), 1);

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString csv;
        QVERIFY(ExportActivityCsv(m_app, directory.filePath("minimum.csv"), csv));
        QVERIFY(csv.contains("\"Mined\""));
        QVERIFY(!csv.contains("\"Payment request\""));

        QVERIFY(ClearActivityFilters(m_app));
        QCOMPARE(m_filter->minAmount(), -1);
        QCOMPARE(m_filter->maxAmount(), -1);
        QTRY_COMPARE(m_filter->rowCount(), 2);
    }

    void calendarRangeFiltersAndResetsLiveRows()
    {
        QVERIFY(createPendingRequestAndMiningReward());
        QVERIFY(qmltest::Click(m_app, "activityDateFilterButton"));
        QVERIFY(qmltest::Click(m_app, "activityDateCustomRange"));
        QVERIFY(qmltest::Click(m_app, "calendarPrev"));
        auto* calendar = qmltest::Find(m_app, "activityCalendar");
        QVERIFY(calendar);
        const QDate month{calendar->property("calYear").toInt(), calendar->property("calMonth").toInt() + 1, 1};
        QVERIFY(month.isValid());
        const auto today = QDate::currentDate();
        QVERIFY(month < QDate(today.year(), today.month(), 1));

        // Select a past range so neither of today's records matches it.
        const auto start = month.addDays(9);
        const auto end = month.addDays(19);
        QVERIFY(qmltest::Click(m_app, qPrintable("calendarDay_" + start.toString(Qt::ISODate))));
        QTRY_COMPARE(calendar->property("startIso").toString(), start.toString(Qt::ISODate));
        QVERIFY(qmltest::Click(m_app, qPrintable("calendarDay_" + end.toString(Qt::ISODate))));
        QTRY_COMPARE(calendar->property("endIso").toString(), end.toString(Qt::ISODate));

        auto* apply = qmltest::Find(m_app, "activityDateRangeApply");
        QVERIFY(apply);
        QVERIFY(apply->property("enabled").toBool());
        QVERIFY(QMetaObject::invokeMethod(apply, "clicked"));
        QCOMPARE(m_filter->dateFilter(), ActivityFilterProxyModel::CustomRange);
        QCOMPARE(m_filter->rangeStart(), start);
        QCOMPARE(m_filter->rangeEnd(), end);
        QTRY_COMPARE(m_filter->rowCount(), 0);

        QVERIFY(qmltest::Click(m_app, "activityDateFilterButton"));
        QVERIFY(qmltest::Click(m_app, "activityDateRangeReset"));
        QCOMPARE(m_filter->dateFilter(), ActivityFilterProxyModel::DateAll);
        QTRY_COMPARE(m_filter->rowCount(), 2);
    }

    void fulfilledRequestBecomesAnAssociatedTransactionWithoutDuplicates()
    {
        QVERIFY(createPendingRequestAndMiningReward());
        QVERIFY(SelectActivityTypes(m_app, {"activityTypePaymentRequest"}));
        QTRY_COMPARE(m_filter->requestCount(), 1);

        // Pay the request and find the new transaction under the mining filter.
        const auto fulfilling_txid = mineBlockToAddress(m_request_address);
        QVERIFY(SelectActivityTypes(m_app, {"activityTypeMined"}));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", m_request_address));
        QTRY_VERIFY(!m_filter->busy() && m_filter->searchText() == m_request_address);
        QTRY_COMPARE(m_filter->rowCount(), 1);
        // The old mining row can already make rowCount equal one while the
        // new source/search observation is pending. Await this exact payment.
        QTRY_COMPARE(m_filter->index(0, 0).data(TransactionActivityModel::TxidRole).toString(), fulfilling_txid);

        // The request filter finds its transaction instead of a second pending row.
        QVERIFY(SelectActivityTypes(m_app, {"activityTypePaymentRequest"}));
        QTRY_VERIFY(!m_filter->busy());
        QTRY_COMPARE(m_filter->rowCount(), 1);
        QCOMPARE(m_filter->requestCount(), 0);
        QCOMPARE(m_filter->transactionCount(), 1);
        const auto row = m_filter->index(0, 0);
        QCOMPARE(row.data(TransactionActivityModel::TxidRole).toString(), fulfilling_txid);
        QVERIFY(row.data(TransactionActivityModel::HasPaymentRequestRole).toBool());

        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        QString csv;
        QVERIFY(ExportActivityCsv(m_app, directory.filePath("fulfilled.csv"), csv));
        QVERIFY(csv.contains("\"Mined\""));
        QVERIFY(csv.contains(fulfilling_txid));
        QVERIFY(!csv.contains("\"Payment request\""));
        QVERIFY(csv.contains("\"Lunch payment\""));
        QVERIFY(csv.contains('"' + m_request_address + '"'));

        QVERIFY(qmltest::SetText(m_app, "activitySearchField", ""));
        QVERIFY(SelectActivityTypes(m_app, {}));
        QTRY_COMPARE(m_filter->rowCount(), 2);
        QCOMPARE(m_filter->transactionCount(), 2);
        QCOMPARE(m_filter->requestCount(), 0);
    }
};

std::unique_ptr<QObject> CreateActivityTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<ActivityIntegrationTests>(app);
}

#include <test_activity.moc>
