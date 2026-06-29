// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/transaction.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QFile>
#include <QTemporaryDir>
#include <QUrl>

namespace {
struct ActivityRow {
    QString address;
    QString amount;
    QString date;
    int depth{0};
    QString label;
    int status{Transaction::Confirmed};
    int type{Transaction::Other};
    qint64 timestamp{0};
    QString txid;
    bool pending_request{false};
    qlonglong net_amount_sat{0};
};

class TestActivityListModel : public QAbstractListModel
{
public:
    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return parent.isValid() ? 0 : m_rows.size();
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) return {};

        const ActivityRow& row = m_rows.at(index.row());
        switch (role) {
        case ActivityListModel::AddressRole:
            return row.address;
        case ActivityListModel::AmountRole:
            return row.amount;
        case ActivityListModel::DateTimeRole:
            return row.date;
        case ActivityListModel::DepthRole:
            return row.depth;
        case ActivityListModel::LabelRole:
            return row.label;
        case ActivityListModel::StatusRole:
            return row.status;
        case ActivityListModel::TypeRole:
            return row.type;
        case ActivityListModel::TimestampRole:
            return row.timestamp;
        case ActivityListModel::TxIdRole:
            return row.pending_request ? QString{} : row.txid;
        case ActivityListModel::IsPendingRequestRole:
            return row.pending_request;
        case ActivityListModel::NetAmountSatRole:
            return row.net_amount_sat;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {ActivityListModel::AddressRole, "address"},
            {ActivityListModel::AmountRole, "amount"},
            {ActivityListModel::DateTimeRole, "date"},
            {ActivityListModel::DepthRole, "depth"},
            {ActivityListModel::LabelRole, "label"},
            {ActivityListModel::StatusRole, "status"},
            {ActivityListModel::TypeRole, "type"},
            {ActivityListModel::TimestampRole, "timestamp"},
            {ActivityListModel::TxIdRole, "txid"},
            {ActivityListModel::IsPendingRequestRole, "isPendingRequest"},
            {ActivityListModel::NetAmountSatRole, "netAmountSat"},
        };
    }

    void setRows(QList<ActivityRow> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }

private:
    QList<ActivityRow> m_rows;
};

qint64 TimestampForLocalDate(const QDate& date)
{
    return QDateTime(date, QTime(12, 0)).toSecsSinceEpoch();
}

ActivityRow MakeRow(QString label, int type, qint64 timestamp, QString txid = {}, QString address = {})
{
    return ActivityRow{
        .address = address.isEmpty() ? QStringLiteral("bc1q%1").arg(label.toLower()) : address,
        .amount = QStringLiteral("BTC"),
        .date = QStringLiteral("date"),
        .label = std::move(label),
        .status = Transaction::Confirmed,
        .type = type,
        .timestamp = timestamp,
        .txid = txid,
        .net_amount_sat = 100'000,
    };
}

bool ContainsLabel(const ActivityFilterProxyModel& proxy, const QString& label)
{
    for (int row = 0; row < proxy.rowCount(); ++row) {
        if (proxy.index(row, 0).data(ActivityListModel::LabelRole).toString() == label) {
            return true;
        }
    }
    return false;
}
} // namespace

class ActivityFilterProxyModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void searchMatchesLabelAddressAndTxid();
    void filtersByDateBuckets();
    void filtersByCustomDateRange();
    void filtersByTypeBucketsAndKeepsPendingRequestsExclusive();
    void filtersByMinimumAmount();
    void sortsByTimestampDescending();
    void exportsCurrentFilteredRowsToCsv();
    void exportsCsvUsingDisplayUnit();
    void exportsCsvEscapesSignedRowsAndHandlesFailures();
};

void ActivityFilterProxyModelTests::searchMatchesLabelAddressAndTxid()
{
    TestActivityListModel source;
    source.setRows({
        MakeRow("Pizza night", Transaction::RecvWithAddress, 10, "aaa", "bc1qpizza"),
        MakeRow("Coffee", Transaction::SendToAddress, 20, "txid-coffee", "bc1qcoffee"),
        MakeRow("Rent", Transaction::SendToOther, 30, "zzz", "bc1qrent"),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setSearchText("pizza");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Pizza night"});

    proxy.setSearchText("bc1qcoffee");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Coffee"});

    proxy.setSearchText("ZZZ");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Rent"});
}

void ActivityFilterProxyModelTests::filtersByDateBuckets()
{
    const QDate today = QDate::currentDate();
    const QDate start_of_week = today.addDays(-(today.dayOfWeek() - 1));
    const QDate start_of_month{today.year(), today.month(), 1};
    const QDate start_of_year{today.year(), 1, 1};
    const QDate start_of_next_week = start_of_week.addDays(7);
    const QDate start_of_last_month = start_of_month.addMonths(-1);
    const QDate start_of_next_month = start_of_month.addMonths(1);
    const QDate start_of_next_year = start_of_year.addYears(1);

    TestActivityListModel source;
    source.setRows({
        MakeRow("Today", Transaction::RecvWithAddress, TimestampForLocalDate(today)),
        MakeRow("Yesterday", Transaction::RecvWithAddress, TimestampForLocalDate(today.addDays(-1))),
        MakeRow("Tomorrow", Transaction::RecvWithAddress, TimestampForLocalDate(today.addDays(1))),
        MakeRow("Week start", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_week)),
        MakeRow("Before week", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_week.addDays(-1))),
        MakeRow("Next week", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_next_week)),
        MakeRow("Month start", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_month)),
        MakeRow("Before month", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_month.addDays(-1))),
        MakeRow("Next month", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_next_month)),
        MakeRow("Last month start", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_last_month)),
        MakeRow("Before last month", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_last_month.addDays(-1))),
        MakeRow("Year start", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_year)),
        MakeRow("Before year", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_year.addDays(-1))),
        MakeRow("Next year", Transaction::RecvWithAddress, TimestampForLocalDate(start_of_next_year)),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setDateFilter(ActivityFilterProxyModel::Today);
    QVERIFY(ContainsLabel(proxy, "Today"));
    QVERIFY(!ContainsLabel(proxy, "Yesterday"));
    QVERIFY(!ContainsLabel(proxy, "Tomorrow"));

    proxy.setDateFilter(ActivityFilterProxyModel::ThisWeek);
    QVERIFY(ContainsLabel(proxy, "Week start"));
    QVERIFY(!ContainsLabel(proxy, "Before week"));
    QVERIFY(!ContainsLabel(proxy, "Next week"));

    proxy.setDateFilter(ActivityFilterProxyModel::ThisMonth);
    QVERIFY(ContainsLabel(proxy, "Month start"));
    QVERIFY(!ContainsLabel(proxy, "Before month"));
    QVERIFY(!ContainsLabel(proxy, "Next month"));

    proxy.setDateFilter(ActivityFilterProxyModel::LastMonth);
    QVERIFY(ContainsLabel(proxy, "Last month start"));
    QVERIFY(ContainsLabel(proxy, "Before month"));
    QVERIFY(!ContainsLabel(proxy, "Before last month"));
    QVERIFY(!ContainsLabel(proxy, "Month start"));

    proxy.setDateFilter(ActivityFilterProxyModel::ThisYear);
    QVERIFY(ContainsLabel(proxy, "Year start"));
    QVERIFY(!ContainsLabel(proxy, "Before year"));
    QVERIFY(!ContainsLabel(proxy, "Next year"));
}

void ActivityFilterProxyModelTests::filtersByTypeBucketsAndKeepsPendingRequestsExclusive()
{
    const qint64 now = QDateTime::currentSecsSinceEpoch();
    ActivityRow request = MakeRow("Request", Transaction::RecvWithAddress, now);
    request.pending_request = true;

    TestActivityListModel source;
    source.setRows({
        MakeRow("Received", Transaction::RecvFromOther, now),
        MakeRow("Sent", Transaction::SendToAddress, now),
        MakeRow("Self", Transaction::SendToSelf, now),
        MakeRow("Mined", Transaction::Generated, now),
        MakeRow("Other", Transaction::Other, now),
        request,
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setTypeFilter(ActivityFilterProxyModel::Received);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Received"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Sent);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Sent"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Other);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Other"});

    proxy.setTypeFilter(ActivityFilterProxyModel::SentToSelf);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Self"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Mined);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Mined"});

    proxy.setTypeFilter(ActivityFilterProxyModel::PaymentRequest);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"Request"});
}

void ActivityFilterProxyModelTests::sortsByTimestampDescending()
{
    TestActivityListModel source;
    source.setRows({
        MakeRow("Old", Transaction::RecvWithAddress, 10),
        MakeRow("New", Transaction::RecvWithAddress, 30),
        MakeRow("Middle", Transaction::RecvWithAddress, 20),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QCOMPARE(proxy.index(0, 0).data(ActivityListModel::LabelRole).toString(), QString{"New"});
    QCOMPARE(proxy.index(1, 0).data(ActivityListModel::LabelRole).toString(), QString{"Middle"});
    QCOMPARE(proxy.index(2, 0).data(ActivityListModel::LabelRole).toString(), QString{"Old"});
}

void ActivityFilterProxyModelTests::exportsCurrentFilteredRowsToCsv()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow request = MakeRow("Alice", Transaction::RecvWithAddress, timestamp, {}, "bc1qalice");
    request.pending_request = true;
    request.net_amount_sat = 10'000;
    request.status = Transaction::Unconfirmed;

    TestActivityListModel source;
    source.setRows({
        request,
        MakeRow("Bob", Transaction::SendToAddress, timestamp - 1, "txid-bob", "bc1qbob"),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setTypeFilter(ActivityFilterProxyModel::PaymentRequest);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("activity.csv");
    QVERIFY(proxy.exportCsv(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csv = QString::fromUtf8(file.readAll());
    QVERIFY(csv.startsWith("\"Confirmed\",\"Date\",\"Type\",\"Label\",\"Address\",\"Amount (BTC)\",\"ID\"\n"));
    QVERIFY(csv.contains("\"false\""));
    QVERIFY(csv.contains("\"Payment request\""));
    QVERIFY(csv.contains("\"Alice\""));
    QVERIFY(csv.contains("\"bc1qalice\""));
    QVERIFY(csv.contains("\"0.00010000\""));
    QVERIFY(!csv.contains("txid-bob"));
}

void ActivityFilterProxyModelTests::exportsCsvUsingDisplayUnit()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow row = MakeRow("Alice", Transaction::RecvWithAddress, timestamp, "txid-alice", "bc1qalice");
    row.net_amount_sat = 123'456'789;

    TestActivityListModel source;
    source.setRows({row});

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setDisplayUnit(3);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("activity.csv");
    QVERIFY(proxy.exportCsv(path));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csv = QString::fromUtf8(file.readAll());
    QVERIFY(csv.startsWith("\"Confirmed\",\"Date\",\"Type\",\"Label\",\"Address\",\"Amount (sat)\",\"ID\"\n"));
    QVERIFY(csv.contains("\"123456789\""));
    QVERIFY(!csv.contains("\"1.23456789\""));
}

void ActivityFilterProxyModelTests::exportsCsvEscapesSignedRowsAndHandlesFailures()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow sent = MakeRow("Bob \"Builder\"", Transaction::SendToAddress, timestamp, "txid-bob", "bc1q,bob");
    sent.net_amount_sat = -123'456'789;

    TestActivityListModel source;
    source.setRows({
        sent,
        MakeRow("Carol", Transaction::RecvWithAddress, timestamp - 1, "txid-carol", "bc1qcarol"),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("activity.csv");
    QVERIFY(proxy.exportCsv(QUrl::fromLocalFile(path).toString()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csv = QString::fromUtf8(file.readAll());
    QVERIFY(csv.contains("\"Sent\""));
    QVERIFY(csv.contains("\"Bob \"\"Builder\"\"\""));
    QVERIFY(csv.contains("\"bc1q,bob\""));
    QVERIFY(csv.contains("\"-1.23456789\""));
    QVERIFY(csv.contains("\"txid-bob\""));
    QVERIFY(csv.contains("\"true\""));

    QVERIFY(!proxy.exportCsv(temp_dir.filePath("missing/activity.csv")));
}

void ActivityFilterProxyModelTests::filtersByCustomDateRange()
{
    const QDate start{2025, 6, 10};
    const QDate end{2025, 6, 20};

    TestActivityListModel source;
    source.setRows({
        MakeRow("Before", Transaction::RecvWithAddress, TimestampForLocalDate(start.addDays(-1))),
        MakeRow("On start", Transaction::RecvWithAddress, TimestampForLocalDate(start)),
        MakeRow("Inside", Transaction::RecvWithAddress, TimestampForLocalDate(QDate(2025, 6, 15))),
        MakeRow("On end", Transaction::RecvWithAddress, TimestampForLocalDate(end)),
        MakeRow("After", Transaction::RecvWithAddress, TimestampForLocalDate(end.addDays(1))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setRangeStart(start);
    proxy.setRangeEnd(end);
    proxy.setDateFilter(ActivityFilterProxyModel::CustomRange);

    QCOMPARE(proxy.rowCount(), 3);
    QVERIFY(ContainsLabel(proxy, "On start")); // lower bound inclusive
    QVERIFY(ContainsLabel(proxy, "Inside"));
    QVERIFY(ContainsLabel(proxy, "On end"));   // upper bound inclusive (whole day)
    QVERIFY(!ContainsLabel(proxy, "Before"));
    QVERIFY(!ContainsLabel(proxy, "After"));
}

void ActivityFilterProxyModelTests::filtersByMinimumAmount()
{
    ActivityRow small = MakeRow("Small", Transaction::RecvWithAddress, 10);
    small.net_amount_sat = 50'000;
    ActivityRow big_receive = MakeRow("Big receive", Transaction::RecvWithAddress, 20);
    big_receive.net_amount_sat = 200'000;
    ActivityRow big_send = MakeRow("Big send", Transaction::SendToAddress, 30);
    big_send.net_amount_sat = -200'000;

    TestActivityListModel source;
    source.setRows({small, big_receive, big_send});

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setMinAmount(100'000);
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(ContainsLabel(proxy, "Big receive"));
    QVERIFY(ContainsLabel(proxy, "Big send")); // absolute value matches the threshold
    QVERIFY(!ContainsLabel(proxy, "Small"));

    proxy.setMinAmount(-1); // clearing restores every row
    QCOMPARE(proxy.rowCount(), 3);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ActivityFilterProxyModelTests)
#else
QTEST_MAIN(ActivityFilterProxyModelTests)
#endif
#include "test_activityfilterproxymodel.moc"
