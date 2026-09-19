// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/transaction.h>

#include <QAbstractListModel>
#include <QDateTime>
#include <QFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QUrl>

#include <ctime>

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
    bool has_payment_request{false};
    qlonglong net_amount_sat{0};
};

class TestActivityModel : public QAbstractListModel
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
        case TransactionActivityModel::AddressRole:
            return row.address;
        case TransactionActivityModel::AmountRole:
            return row.amount;
        case TransactionActivityModel::DateTimeRole:
            return row.date;
        case TransactionActivityModel::DepthRole:
            return row.depth;
        case TransactionActivityModel::LabelRole:
            return row.label;
        case TransactionActivityModel::StatusRole:
            return row.status;
        case TransactionActivityModel::TypeRole:
            return row.type;
        case TransactionActivityModel::TimestampRole:
            return row.timestamp;
        case TransactionActivityModel::TxidRole:
            return row.pending_request ? QString{} : row.txid;
        case TransactionActivityModel::IsPendingRequestRole:
            return row.pending_request;
        case TransactionActivityModel::HasPaymentRequestRole:
            return row.pending_request || row.has_payment_request;
        case TransactionActivityModel::IdRole:
            return row.txid.isEmpty() ? row.label : row.txid;
        case TransactionActivityModel::StatusKnownRole:
            return true;
        case TransactionActivityModel::IsPendingRole:
            return row.pending_request || row.status == Transaction::Unconfirmed;
        case TransactionActivityModel::ActivityTypeRole:
            switch (row.type) {
            case Transaction::RecvWithAddress: case Transaction::RecvFromOther: return int(TransactionActivityModel::Receive);
            case Transaction::SendToAddress: case Transaction::SendToOther: return int(TransactionActivityModel::Send);
            case Transaction::SendToSelf: return int(TransactionActivityModel::InternalTransfer);
            case Transaction::Generated: return int(TransactionActivityModel::Mined);
            default: return int(TransactionActivityModel::Other);
            }
        case TransactionActivityModel::ActionsRole: {
            const int direction = row.type == Transaction::RecvWithAddress || row.type == Transaction::RecvFromOther
                ? TransactionActivityModel::ReceiveAction : row.type == Transaction::SendToAddress || row.type == Transaction::SendToOther
                ? TransactionActivityModel::SendAction : TransactionActivityModel::InternalAction;
            return QVariantList{QVariantMap{{"direction", direction}}};
        }
        case TransactionActivityModel::NetAmountSatRole:
            return row.net_amount_sat;
        default:
            return {};
        }
    }

    QHash<int, QByteArray> roleNames() const override
    {
        return {
            {TransactionActivityModel::AddressRole, "address"},
            {TransactionActivityModel::AmountRole, "amount"},
            {TransactionActivityModel::DateTimeRole, "date"},
            {TransactionActivityModel::DepthRole, "depth"},
            {TransactionActivityModel::LabelRole, "label"},
            {TransactionActivityModel::StatusRole, "status"},
            {TransactionActivityModel::TypeRole, "type"},
            {TransactionActivityModel::TimestampRole, "timestamp"},
            {TransactionActivityModel::TxidRole, "txid"},
            {TransactionActivityModel::IsPendingRequestRole, "isPendingRequest"},
            {TransactionActivityModel::HasPaymentRequestRole, "hasPaymentRequest"},
            {TransactionActivityModel::NetAmountSatRole, "netAmountSat"},
        };
    }

    void setRows(QList<ActivityRow> rows)
    {
        beginResetModel();
        m_rows = std::move(rows);
        endResetModel();
    }

    void setAmount(int row, qint64 amount)
    {
        m_rows[row].net_amount_sat = amount;
        Q_EMIT dataChanged(index(row, 0), index(row, 0), {TransactionActivityModel::NetAmountSatRole});
    }

private:
    QList<ActivityRow> m_rows;
};

qint64 TimestampForLocalDate(const QDate& date)
{
    return QDateTime(date, QTime(12, 0)).toSecsSinceEpoch();
}

void TzSet()
{
#ifdef Q_OS_WIN
    _tzset();
#else
    tzset();
#endif
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
        if (proxy.index(row, 0).data(TransactionActivityModel::LabelRole).toString() == label) {
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
    void customRangeHandlesDstGapDayBoundaries();
    void rejectsInvalidCustomRange();
    void appliesCustomRangeWithOneNotification();
    void filtersByTypeBucketsAndKeepsPendingRequestsExclusive();
    void paymentRequestFilterIncludesAssociatedTransactions();
    void filtersByMinimumAmount();
    void combinesTypeSelections();
    void filtersByInclusiveAmountRange();
    void availableMaximumUsesUnfilteredTransactionsAndUpdates();
    void sortsByTimestampDescending();
    void exportsCurrentFilteredRowsToCsv();
    void exportsCsvUsingDisplayUnit();
    void exportsCsvEscapesSignedRowsAndHandlesFailures();
    void exportsCsvNeutralizesFormulaInjection();
};

void ActivityFilterProxyModelTests::searchMatchesLabelAddressAndTxid()
{
    TestActivityModel source;
    source.setRows({
        MakeRow("Pizza night", Transaction::RecvWithAddress, 10, "aaa", "bc1qpizza"),
        MakeRow("Coffee", Transaction::SendToAddress, 20, "txid-coffee", "bc1qcoffee"),
        MakeRow("Rent", Transaction::SendToOther, 30, "zzz", "bc1qrent"),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    proxy.setSearchText("pizza");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Pizza night"});

    proxy.setSearchText("bc1qcoffee");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Coffee"});

    proxy.setSearchText("ZZZ");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Rent"});
}

void ActivityFilterProxyModelTests::filtersByDateBuckets()
{
    const QDate today = QDate::currentDate();
    const QDate start_of_week = today.addDays(-(today.dayOfWeek() - 1));
    const QDate start_of_month{today.year(), today.month(), 1};
    const QDate start_of_year{today.year(), 1, 1};
    const QDate start_of_next_week = start_of_week.addDays(7);
    const QDate start_of_next_month = start_of_month.addMonths(1);
    const QDate start_of_next_year = start_of_year.addYears(1);

    TestActivityModel source;
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

    TestActivityModel source;
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
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Received"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Sent);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Sent"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Other);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Other"});

    proxy.setTypeFilter(ActivityFilterProxyModel::SentToSelf);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Self"});

    proxy.setTypeFilter(ActivityFilterProxyModel::Mined);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Mined"});

    proxy.setTypeFilter(ActivityFilterProxyModel::PaymentRequest);
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Request"});
}

void ActivityFilterProxyModelTests::paymentRequestFilterIncludesAssociatedTransactions()
{
    auto received = MakeRow("Received", Transaction::RecvWithAddress, 30, "tx-received");
    received.has_payment_request = true;
    auto pending = MakeRow("Pending", Transaction::RecvWithAddress, 20);
    pending.pending_request = true;
    TestActivityModel source;
    source.setRows({received, pending, MakeRow("Sent", Transaction::SendToAddress, 10, "tx-sent")});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QCOMPARE(proxy.rowCount(), 3);
    proxy.setTypeFilter(ActivityFilterProxyModel::PaymentRequest);
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(ContainsLabel(proxy, "Received"));
    QVERIFY(ContainsLabel(proxy, "Pending"));
    proxy.setTypeFilter(ActivityFilterProxyModel::Received);
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(ContainsLabel(proxy, "Received"));
}

void ActivityFilterProxyModelTests::sortsByTimestampDescending()
{
    TestActivityModel source;
    source.setRows({
        MakeRow("Old", Transaction::RecvWithAddress, 10),
        MakeRow("New", Transaction::RecvWithAddress, 30),
        MakeRow("Middle", Transaction::RecvWithAddress, 20),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QCOMPARE(proxy.index(0, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"New"});
    QCOMPARE(proxy.index(1, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Middle"});
    QCOMPARE(proxy.index(2, 0).data(TransactionActivityModel::LabelRole).toString(), QString{"Old"});
}

void ActivityFilterProxyModelTests::exportsCurrentFilteredRowsToCsv()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow request = MakeRow("Alice", Transaction::RecvWithAddress, timestamp, {}, "bc1qalice");
    request.pending_request = true;
    request.net_amount_sat = 10'000;
    request.status = Transaction::Unconfirmed;

    TestActivityModel source;
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
    QVERIFY(csv.startsWith("\"Confirmed\",\"Date\",\"Type\",\"Label\",\"Address\",\"Amount (BTC)\",\"ID\",\"Record\",\"Action ID\",\"Status\",\"Action amount (BTC)\"\n"));
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

    TestActivityModel source;
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
    QVERIFY(csv.startsWith("\"Confirmed\",\"Date\",\"Type\",\"Label\",\"Address\",\"Amount (sat)\",\"ID\",\"Record\",\"Action ID\",\"Status\",\"Action amount (sat)\"\n"));
    QVERIFY(csv.contains("\"123456789\""));
    QVERIFY(!csv.contains("\"1.23456789\""));
}

void ActivityFilterProxyModelTests::exportsCsvEscapesSignedRowsAndHandlesFailures()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow sent = MakeRow("Bob \"Builder\"", Transaction::SendToAddress, timestamp, "txid-bob", "bc1q,bob");
    sent.net_amount_sat = -123'456'789;

    TestActivityModel source;
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

    TestActivityModel source;
    source.setRows({
        MakeRow("Before", Transaction::RecvWithAddress, TimestampForLocalDate(start.addDays(-1))),
        MakeRow("On start", Transaction::RecvWithAddress, TimestampForLocalDate(start)),
        MakeRow("Inside", Transaction::RecvWithAddress, TimestampForLocalDate(QDate(2025, 6, 15))),
        MakeRow("On end", Transaction::RecvWithAddress, TimestampForLocalDate(end)),
        MakeRow("After", Transaction::RecvWithAddress, TimestampForLocalDate(end.addDays(1))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(proxy.applyCustomRange(start.toString(Qt::ISODate), end.toString(Qt::ISODate)));
    QCOMPARE(proxy.dateFilter(), ActivityFilterProxyModel::CustomRange);

    QCOMPARE(proxy.rowCount(), 3);
    QVERIFY(ContainsLabel(proxy, "On start")); // lower bound inclusive
    QVERIFY(ContainsLabel(proxy, "Inside"));
    QVERIFY(ContainsLabel(proxy, "On end"));   // upper bound inclusive (whole day)
    QVERIFY(!ContainsLabel(proxy, "Before"));
    QVERIFY(!ContainsLabel(proxy, "After"));
}

void ActivityFilterProxyModelTests::customRangeHandlesDstGapDayBoundaries()
{
    // America/Sao_Paulo entered daylight saving at midnight on 2018-11-04:
    // clocks jumped from 00:00 to 01:00, so that day has no midnight.
    // QDateTime{date, QTime(0, 0)} is invalid for it and compares before
    // every valid time, which silently broke both range boundaries; the
    // date's startOfDay() is the correct first instant. In a zone without
    // the gap this degenerates to an ordinary range check and still passes.
    const QByteArray previous_tz = qgetenv("TZ");
    qputenv("TZ", "America/Sao_Paulo");
    TzSet();
    const auto restore_tz = qScopeGuard([&previous_tz] {
        if (previous_tz.isEmpty()) {
            qunsetenv("TZ");
        } else {
            qputenv("TZ", previous_tz);
        }
        TzSet();
    });

    const QDate gap_day{2018, 11, 4};

    TestActivityModel source;
    source.setRows({
        MakeRow("Before", Transaction::RecvWithAddress, TimestampForLocalDate(gap_day.addDays(-1))),
        MakeRow("On gap day", Transaction::RecvWithAddress, TimestampForLocalDate(gap_day)),
        MakeRow("After", Transaction::RecvWithAddress, TimestampForLocalDate(gap_day.addDays(1))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    // A range starting on the gap day must still exclude earlier rows.
    QVERIFY(proxy.applyCustomRange(gap_day.toString(Qt::ISODate),
                                   gap_day.addDays(1).toString(Qt::ISODate)));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(!ContainsLabel(proxy, "Before"));
    QVERIFY(ContainsLabel(proxy, "On gap day"));
    QVERIFY(ContainsLabel(proxy, "After"));

    // A range whose exclusive upper bound lands on the gap day (inclusive
    // end date the day before) must not collapse to rejecting every row.
    QVERIFY(proxy.applyCustomRange(gap_day.addDays(-1).toString(Qt::ISODate),
                                   gap_day.addDays(-1).toString(Qt::ISODate)));
    QCOMPARE(proxy.rowCount(), 1);
    QVERIFY(ContainsLabel(proxy, "Before"));
}

void ActivityFilterProxyModelTests::rejectsInvalidCustomRange()
{
    TestActivityModel source;
    source.setRows({
        MakeRow("Inside", Transaction::RecvWithAddress, TimestampForLocalDate(QDate(2025, 6, 15))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    // An unparseable, empty, or inverted range leaves the model untouched: the
    // previous filter keeps applying rather than collapsing to an empty list.
    QVERIFY(!proxy.applyCustomRange("not-a-date", "2025-06-20"));
    QVERIFY(!proxy.applyCustomRange("2025-06-10", ""));
    QVERIFY(!proxy.applyCustomRange("2025-06-20", "2025-06-10"));

    QCOMPARE(proxy.dateFilter(), ActivityFilterProxyModel::DateAll);
    QVERIFY(!proxy.rangeStart().isValid());
    QVERIFY(!proxy.rangeEnd().isValid());
    QCOMPARE(proxy.rowCount(), 1);

    // A single-day range is not inverted and is accepted.
    QVERIFY(proxy.applyCustomRange("2025-06-15", "2025-06-15"));
    QCOMPARE(proxy.rowCount(), 1);
}

void ActivityFilterProxyModelTests::appliesCustomRangeWithOneNotification()
{
    TestActivityModel source;
    source.setRows({
        MakeRow("Inside", Transaction::RecvWithAddress, TimestampForLocalDate(QDate(2025, 6, 15))),
    });

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    QSignalSpy range_spy(&proxy, &ActivityFilterProxyModel::rangeChanged);
    QSignalSpy date_filter_spy(&proxy, &ActivityFilterProxyModel::dateFilterChanged);
    QSignalSpy count_spy(&proxy, &ActivityFilterProxyModel::countChanged);

    // Setting both ends and the date filter together notifies once, not once
    // per end, so no observer sees a half-applied range.
    QVERIFY(proxy.applyCustomRange("2025-06-10", "2025-06-20"));
    QCOMPARE(range_spy.count(), 1);
    QCOMPARE(date_filter_spy.count(), 1);
    QCOMPARE(count_spy.count(), 1);

    // Re-applying the same range is a no-op and stays silent.
    QVERIFY(proxy.applyCustomRange("2025-06-10", "2025-06-20"));
    QCOMPARE(range_spy.count(), 1);
    QCOMPARE(date_filter_spy.count(), 1);
    QCOMPARE(count_spy.count(), 1);

    // A rejected range notifies nothing either.
    QVERIFY(!proxy.applyCustomRange("2025-06-20", "2025-06-10"));
    QCOMPARE(range_spy.count(), 1);
    QCOMPARE(date_filter_spy.count(), 1);
    QCOMPARE(count_spy.count(), 1);

    // Moving only the range while the filter is already CustomRange notifies
    // the range but not the filter.
    QVERIFY(proxy.applyCustomRange("2025-06-11", "2025-06-21"));
    QCOMPARE(range_spy.count(), 2);
    QCOMPARE(date_filter_spy.count(), 1);
    QCOMPARE(count_spy.count(), 2);
}

void ActivityFilterProxyModelTests::filtersByMinimumAmount()
{
    ActivityRow small = MakeRow("Small", Transaction::RecvWithAddress, 10);
    small.net_amount_sat = 50'000;
    ActivityRow big_receive = MakeRow("Big receive", Transaction::RecvWithAddress, 20);
    big_receive.net_amount_sat = 200'000;
    ActivityRow big_send = MakeRow("Big send", Transaction::SendToAddress, 30);
    big_send.net_amount_sat = -200'000;

    TestActivityModel source;
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

void ActivityFilterProxyModelTests::exportsCsvNeutralizesFormulaInjection()
{
    const qint64 timestamp = TimestampForLocalDate(QDate::currentDate());
    ActivityRow hostile = MakeRow("=HYPERLINK(\"http://evil.test\",\"click\")",
                                  Transaction::RecvWithAddress, timestamp,
                                  "txid-hostile", "@bc1qhostile");
    ActivityRow negative = MakeRow("-2+3+cmd", Transaction::SendToAddress, timestamp - 1,
                                   "txid-negative", "bc1qplain");
    negative.net_amount_sat = -123'456'789;
    ActivityRow linefeed = MakeRow("\n=1+2", Transaction::RecvWithAddress, timestamp - 2,
                                   "txid-linefeed", "bc1qlinefeed");

    TestActivityModel source;
    source.setRows({hostile, negative, linefeed});

    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);

    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("activity.csv");
    QVERIFY(proxy.exportCsv(QUrl::fromLocalFile(path).toString()));

    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly | QIODevice::Text));
    const QString csv = QString::fromUtf8(file.readAll());

    // Text cells with a dangerous leading character are neutralized...
    QVERIFY(csv.contains("\"'=HYPERLINK(\"\"http://evil.test\"\",\"\"click\"\")\""));
    QVERIFY(csv.contains("\"'@bc1qhostile\""));
    QVERIFY(csv.contains("\"'-2+3+cmd\""));
    QVERIFY(csv.contains("\"'\n=1+2\""));
    QVERIFY(!csv.contains("\"=HYPERLINK"));
    QVERIFY(!csv.contains("\"\n=1+2"));

    // ...while the numeric amount column keeps its minus sign untouched.
    QVERIFY(csv.contains("\"-1.23456789\""));
    QVERIFY(csv.contains("\"bc1qplain\""));
}

void ActivityFilterProxyModelTests::combinesTypeSelections()
{
    auto request = MakeRow("Request", Transaction::Other, 40);
    request.pending_request = true;
    TestActivityModel source;
    source.setRows({MakeRow("Receive", Transaction::RecvWithAddress, 10),
                    MakeRow("Send", Transaction::SendToAddress, 20),
                    MakeRow("Mined", Transaction::Generated, 30), request});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    proxy.setTypeFilters({ActivityFilterProxyModel::Sent, ActivityFilterProxyModel::Received, ActivityFilterProxyModel::Sent});
    QCOMPARE(proxy.rowCount(), 2);
    QCOMPARE(proxy.typeFilters().size(), 2);
    QVERIFY(ContainsLabel(proxy, "Send"));
    QVERIFY(ContainsLabel(proxy, "Receive"));
    proxy.setTypeFilters({ActivityFilterProxyModel::Sent, ActivityFilterProxyModel::PaymentRequest});
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(ContainsLabel(proxy, "Request"));
    proxy.setTypeFilters({});
    QCOMPARE(proxy.typeFilter(), ActivityFilterProxyModel::TypeAll);
    QCOMPARE(proxy.rowCount(), 4); // All transactions and unpaid requests are visible.
    proxy.setTypeFilter(ActivityFilterProxyModel::Mined); // Legacy single selection still works.
    QCOMPARE(proxy.typeFilters(), QList<int>{ActivityFilterProxyModel::Mined});
    QCOMPARE(proxy.rowCount(), 1);
}

void ActivityFilterProxyModelTests::filtersByInclusiveAmountRange()
{
    auto send = MakeRow("Send", Transaction::SendToAddress, 10);
    auto receive = MakeRow("Receive", Transaction::RecvWithAddress, 20);
    auto larger = MakeRow("Larger", Transaction::RecvWithAddress, 30);
    send.net_amount_sat = -200;
    receive.net_amount_sat = 200;
    larger.net_amount_sat = 201;
    TestActivityModel source;
    source.setRows({send, receive, larger});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QVERIFY(proxy.setAmountRange(200, 200));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(ContainsLabel(proxy, "Send"));
    QVERIFY(ContainsLabel(proxy, "Receive"));
    QVERIFY(!proxy.setAmountRange(201, 200));
    QCOMPARE(proxy.minAmount(), 200);
    QCOMPARE(proxy.maxAmount(), 200);
    QVERIFY(proxy.setAmountRange(-1, 200));
    QCOMPARE(proxy.rowCount(), 2);
    QVERIFY(proxy.setAmountRange(-1, -1));
    QCOMPARE(proxy.rowCount(), 3);
    QVERIFY(proxy.setAmountRange(0, 0));
    QCOMPARE(proxy.rowCount(), 0);
}

void ActivityFilterProxyModelTests::availableMaximumUsesUnfilteredTransactionsAndUpdates()
{
    auto send = MakeRow("Send", Transaction::SendToAddress, 10);
    auto receive = MakeRow("Receive", Transaction::RecvWithAddress, 20);
    auto request = MakeRow("Request", Transaction::Other, 30);
    send.net_amount_sat = -500;
    receive.net_amount_sat = 200;
    request.pending_request = true;
    request.net_amount_sat = 9000;
    TestActivityModel source;
    source.setRows({send, receive, request});
    ActivityFilterProxyModel proxy;
    proxy.setSourceModel(&source);
    QCOMPARE(proxy.availableMaxAmount(), 500);
    proxy.setSearchText("Receive");
    QVERIFY(proxy.setAmountRange(0, 300));
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.availableMaxAmount(), 500);
    source.setAmount(0, -700);
    QCOMPARE(proxy.availableMaxAmount(), 700);
    source.setRows({receive, request});
    QCOMPARE(proxy.availableMaxAmount(), 200);
    TestActivityModel other;
    proxy.setSourceModel(&other);
    QCOMPARE(proxy.availableMaxAmount(), 0);
    source.setRows({send}); // A previous wallet must no longer affect the range.
    QCOMPARE(proxy.availableMaxAmount(), 0);
    other.setRows({send});
    QCOMPARE(proxy.availableMaxAmount(), 500);
    proxy.setSourceModel(nullptr);
    QCOMPARE(proxy.availableMaxAmount(), 0);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ActivityFilterProxyModelTests)
#else
QTEST_MAIN(ActivityFilterProxyModelTests)
#endif
#include "test_activityfilterproxymodel.moc"
