// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_wallet.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qt/bitcoinunits.h>

#include <QDate>
#include <QFile>
#include <QTemporaryDir>

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
        auto* request = m_wallet->currentPaymentRequest();
        request->amount()->setSatoshi(10'000);
        request->setLabel("Alice");
        request->setMessage("pizza");
        request->setNoteSelf("Lunch payment");
        if (!m_wallet->commitPaymentRequest()) return false;
        m_request_address = request->address();
        m_request_id = request->id();

        // A reward to another address leaves the payment request pending.
        UniValue params{UniValue::VARR};
        params.push_back("Mining reward");
        const auto reward_address = QString::fromStdString(qmltest::Rpc(m_app, "getnewaddress", params, m_wallet->name()).get_str());
        m_reward_txid = mineBlockToAddress(reward_address);
        return !m_request_id.isEmpty() && !m_request_address.isEmpty() && reward_address != m_request_address &&
               QTest::qWaitFor([&] { return m_filter->rowCount() == 2; }, 30'000);
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
        QTRY_VERIFY(!m_wallet->transactionActivityModel()->loading());
        QCOMPARE(m_filter->rowCount(), 0);
        QVERIFY(qmltest::WaitForVisible(m_app, "activityEmptyState"));
        const auto title = qmltest::Find(m_app, "activityEmptyStateTitle")->property("text").toString();
        const auto description = qmltest::Find(m_app, "activityEmptyStateDescription")->property("text").toString();
        QVERIFY(title == "No activity yet" || title == QString::fromUtf8("Syncing wallet activity…"));
        QCOMPARE(description, title == "No activity yet" ? QStringLiteral("Your transactions and payment requests will appear here.")
                                                       : QStringLiteral("Transactions may appear as your wallet catches up."));
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
        auto* row = qmltest::WaitForVisible(m_app, qPrintable("activityItem_" + m_reward_txid));
        QVERIFY(row);
        QTRY_COMPARE(row->property("amount").toString(), m_filter->index(0, 0).data(TransactionActivityModel::AmountRole).toString());
        const auto amount = row->property("amount").toString();
        QVERIFY(!amount.isEmpty());
        QVERIFY(qmltest::Click(qmltest::Find(row, "activityRowOpenButton")));
        auto* details = qmltest::WaitForVisible(m_app, "activityDetailsPage");
        QVERIFY(details);
        QTRY_COMPARE(details->property("amount").toString(), amount);
    }

    void clearingFiltersPreservesSearchAndRestoresRows()
    {
        QVERIFY(createPendingRequestAndMiningReward());
        QVERIFY(SelectActivityTypes(m_app, {"activityTypePaymentRequest"}));
        QVERIFY(qmltest::Click(m_app, "activityDateFilterButton"));
        QVERIFY(qmltest::Click(m_app, "activityDateToday"));
        QVERIFY(qmltest::SetText(m_app, "activitySearchField", "not-present"));
        QTRY_COMPARE(m_filter->rowCount(), 0);
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
        QTRY_COMPARE(m_filter->rowCount(), 1);
        QCOMPARE(m_filter->index(0, 0).data(TransactionActivityModel::TxidRole).toString(), fulfilling_txid);

        // The request filter finds its transaction instead of a second pending row.
        QVERIFY(SelectActivityTypes(m_app, {"activityTypePaymentRequest"}));
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
