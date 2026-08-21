// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activityfilterproxymodel.h>

#include <qml/bitcoinunits.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/transaction.h>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QStringList>
#include <QTextStream>
#include <QTime>
#include <QUrl>

namespace {
void WriteCsvValue(QTextStream& stream, QString value)
{
    value.replace('"', "\"\"");
    stream << '"' << value << '"';
}

// Sender- and address-book-controlled text cells must not execute as
// formulas when the exported file is opened in a spreadsheet: neutralize
// a leading =, +, -, @, tab, CR or LF with a single-quote prefix. Only
// text columns go through this, so numeric cells keep their minus sign.
QString GuardedCsvText(QString value)
{
    static const QString dangerous_leads{QStringLiteral("=+-@\t\r\n")};
    if (!value.isEmpty() && dangerous_leads.contains(value.at(0))) {
        value.prepend(QLatin1Char('\''));
    }
    return value;
}

void WriteCsvRow(QTextStream& stream, const QStringList& values)
{
    for (int i = 0; i < values.size(); ++i) {
        if (i > 0) stream << ',';
        WriteCsvValue(stream, values.at(i));
    }
    stream << '\n';
}

QmlBitcoinUnits::Unit ExportDisplayUnit(int display_unit)
{
    return QmlBitcoinUnits::fromDisplayUnit(display_unit);
}

QString ExportDisplayUnitLabel(int display_unit)
{
    return QmlBitcoinUnits::label(ExportDisplayUnit(display_unit));
}
} // namespace

ActivityFilterProxyModel::ActivityFilterProxyModel(QObject* parent)
    : QSortFilterProxyModel(parent)
{
    setDynamicSortFilter(true);
    setSortCaseSensitivity(Qt::CaseInsensitive);
    sort(0, Qt::DescendingOrder);

    connect(this, &QAbstractItemModel::rowsInserted, this, &ActivityFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ActivityFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ActivityFilterProxyModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ActivityFilterProxyModel::countChanged);
}

QHash<int, QByteArray> ActivityFilterProxyModel::roleNames() const
{
    return sourceModel() ? sourceModel()->roleNames() : QHash<int, QByteArray>{};
}

void ActivityFilterProxyModel::setSourceModel(QAbstractItemModel* source_model)
{
    if (sourceModel() == source_model) return;

    QSortFilterProxyModel::setSourceModel(source_model);
    sort(0, Qt::DescendingOrder);
    Q_EMIT countChanged();
}

QString ActivityFilterProxyModel::searchText() const
{
    return m_search_text;
}

void ActivityFilterProxyModel::setSearchText(const QString& search_text)
{
    if (m_search_text == search_text) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif

    m_search_text = search_text;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    Q_EMIT searchTextChanged();
    Q_EMIT countChanged();
}

ActivityFilterProxyModel::DateFilter ActivityFilterProxyModel::dateFilter() const
{
    return m_date_filter;
}

void ActivityFilterProxyModel::setDateFilter(DateFilter date_filter)
{
    if (m_date_filter == date_filter) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif

    m_date_filter = date_filter;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    Q_EMIT dateFilterChanged();
    Q_EMIT countChanged();
}

ActivityFilterProxyModel::TypeFilter ActivityFilterProxyModel::typeFilter() const
{
    return m_type_filter;
}

void ActivityFilterProxyModel::setTypeFilter(TypeFilter type_filter)
{
    if (m_type_filter == type_filter) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif

    m_type_filter = type_filter;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    Q_EMIT typeFilterChanged();
    Q_EMIT countChanged();
}

int ActivityFilterProxyModel::displayUnit() const
{
    return m_display_unit;
}

void ActivityFilterProxyModel::setDisplayUnit(int display_unit)
{
    if (m_display_unit == display_unit) return;

    m_display_unit = display_unit;
    Q_EMIT displayUnitChanged();
}

int ActivityFilterProxyModel::count() const
{
    return rowCount();
}

bool ActivityFilterProxyModel::filterAcceptsRow(int source_row, const QModelIndex& source_parent) const
{
    if (!sourceModel()) return false;

    const QModelIndex source_index = sourceModel()->index(source_row, 0, source_parent);
    if (!source_index.isValid()) return false;

    if (!dateMatches(source_index.data(ActivityListModel::TimestampRole).toLongLong())) {
        return false;
    }

    const TypeFilter row_type = filterTypeForIndex(source_index);
    if (m_type_filter != TypeAll && row_type != m_type_filter) {
        return false;
    }

    const QString search = m_search_text.trimmed();
    if (!search.isEmpty()) {
        const QString address = source_index.data(ActivityListModel::AddressRole).toString();
        const QString label = source_index.data(ActivityListModel::LabelRole).toString();
        const QString txid = source_index.data(ActivityListModel::TxIdRole).toString();
        if (!address.contains(search, Qt::CaseInsensitive) &&
            !label.contains(search, Qt::CaseInsensitive) &&
            !txid.contains(search, Qt::CaseInsensitive)) {
            return false;
        }
    }

    return true;
}

bool ActivityFilterProxyModel::lessThan(const QModelIndex& left_index, const QModelIndex& right_index) const
{
    // Delegate to the source model's full ordering (newest first with
    // deterministic tie-breaks). Comparing the timestamp alone leaves
    // equal-time rows in arrival order, which diverges from the order a
    // reload produces. The proxy sorts descending, so returning "left is
    // less" when the right row sorts in front preserves that order.
    if (const auto* model = qobject_cast<const ActivityListModel*>(sourceModel())) {
        return model->rowSortsBefore(right_index.row(), left_index.row());
    }
    const qint64 left_timestamp = sourceModel()->data(left_index, ActivityListModel::TimestampRole).toLongLong();
    const qint64 right_timestamp = sourceModel()->data(right_index, ActivityListModel::TimestampRole).toLongLong();
    return left_timestamp < right_timestamp;
}

ActivityFilterProxyModel::TypeFilter ActivityFilterProxyModel::filterTypeForIndex(const QModelIndex& source_index) const
{
    if (source_index.data(ActivityListModel::IsPendingRequestRole).toBool()) {
        return PaymentRequest;
    }

    switch (source_index.data(ActivityListModel::TypeRole).toInt()) {
    case Transaction::RecvWithAddress:
    case Transaction::RecvFromOther:
        return Received;
    case Transaction::SendToSelf:
        return SentToSelf;
    case Transaction::Generated:
        return Mined;
    case Transaction::SendToAddress:
    case Transaction::SendToOther:
    case Transaction::Other:
    default:
        return Sent;
    }
}

QString ActivityFilterProxyModel::exportTypeLabelForIndex(const QModelIndex& proxy_index) const
{
    if (proxy_index.data(ActivityListModel::IsPendingRequestRole).toBool()) {
        return tr("Payment request");
    }

    switch (proxy_index.data(ActivityListModel::TypeRole).toInt()) {
    case Transaction::RecvWithAddress:
    case Transaction::RecvFromOther:
        return tr("Received");
    case Transaction::SendToAddress:
    case Transaction::SendToOther:
        return tr("Sent");
    case Transaction::SendToSelf:
        return tr("Sent to yourself");
    case Transaction::Generated:
        return tr("Mined");
    case Transaction::Other:
    default:
        return tr("Other");
    }
}

bool ActivityFilterProxyModel::dateMatches(qint64 timestamp) const
{
    if (m_date_filter == DateAll) return true;
    if (timestamp <= 0) return false;

    const QDate current_date = QDate::currentDate();
    QDate start_date;
    QDate end_date;
    switch (m_date_filter) {
    case Today:
        start_date = current_date;
        end_date = current_date.addDays(1);
        break;
    case ThisWeek:
        start_date = current_date.addDays(-(current_date.dayOfWeek() - 1));
        end_date = start_date.addDays(7);
        break;
    case ThisMonth:
        start_date = QDate(current_date.year(), current_date.month(), 1);
        end_date = start_date.addMonths(1);
        break;
    case ThisYear:
        start_date = QDate(current_date.year(), 1, 1);
        end_date = start_date.addYears(1);
        break;
    case DateAll:
        return true;
    }

    const QDateTime start_of_range{start_date, QTime(0, 0)};
    const QDateTime end_of_range{end_date, QTime(0, 0)};
    const QDateTime row_time = QDateTime::fromSecsSinceEpoch(timestamp);
    return row_time >= start_of_range && row_time < end_of_range;
}

QString ActivityFilterProxyModel::normalizedExportPath(const QString& path) const
{
    const QUrl url{path};
    if (url.isLocalFile()) {
        return url.toLocalFile();
    }
    return path;
}

bool ActivityFilterProxyModel::exportCsv(const QString& path) const
{
    const QString filename = normalizedExportPath(path);
    if (filename.isEmpty()) return false;

    QFile file(filename);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }

    QTextStream stream(&file);
    WriteCsvRow(stream, {
        tr("Confirmed"),
        tr("Date"),
        tr("Type"),
        tr("Label"),
        tr("Address"),
        tr("Amount") + QStringLiteral(" (%1)").arg(ExportDisplayUnitLabel(m_display_unit)),
        tr("ID"),
    });

    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex proxy_index = index(row, 0);
        const qint64 timestamp = proxy_index.data(ActivityListModel::TimestampRole).toLongLong();
        const auto status = static_cast<Transaction::Status>(proxy_index.data(ActivityListModel::StatusRole).toInt());
        const bool confirmed = status == Transaction::Confirming || status == Transaction::Confirmed;
        const CAmount amount = proxy_index.data(ActivityListModel::NetAmountSatRole).toLongLong();
        const bool pending_request = proxy_index.data(ActivityListModel::IsPendingRequestRole).toBool();

        WriteCsvRow(stream, {
            confirmed ? QStringLiteral("true") : QStringLiteral("false"),
            QDateTime::fromSecsSinceEpoch(timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            exportTypeLabelForIndex(proxy_index),
            GuardedCsvText(proxy_index.data(ActivityListModel::LabelRole).toString()),
            GuardedCsvText(proxy_index.data(ActivityListModel::AddressRole).toString()),
            QmlBitcoinUnits::format(ExportDisplayUnit(m_display_unit), amount, false, QmlBitcoinUnits::SeparatorStyle::NEVER),
            pending_request ? QString{} : proxy_index.data(ActivityListModel::TxIdRole).toString(),
        });
    }

    file.close();
    return file.error() == QFile::NoError;
}
