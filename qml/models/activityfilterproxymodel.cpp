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

CAmount ActivityFilterProxyModel::minAmount() const
{
    return m_min_amount;
}

void ActivityFilterProxyModel::setMinAmount(CAmount min_amount)
{
    // Anything below zero clears the filter; amounts are always non-negative.
    const CAmount normalized = min_amount < 0 ? -1 : min_amount;
    if (m_min_amount == normalized) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_min_amount = normalized;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    Q_EMIT minAmountChanged();
    Q_EMIT countChanged();
}

QDate ActivityFilterProxyModel::rangeStart() const
{
    return m_range_start;
}

void ActivityFilterProxyModel::setRangeStart(const QDate& range_start)
{
    if (m_range_start == range_start) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_range_start = range_start;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    Q_EMIT rangeChanged();
    Q_EMIT countChanged();
}

QDate ActivityFilterProxyModel::rangeEnd() const
{
    return m_range_end;
}

void ActivityFilterProxyModel::setRangeEnd(const QDate& range_end)
{
    if (m_range_end == range_end) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_range_end = range_end;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    Q_EMIT rangeChanged();
    Q_EMIT countChanged();
}

void ActivityFilterProxyModel::setCustomRange(const QString& start_iso, const QString& end_iso)
{
    setRangeStart(QDate::fromString(start_iso, Qt::ISODate));
    setRangeEnd(QDate::fromString(end_iso, Qt::ISODate));
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

    // A payment request whose address already has a real transaction is only
    // surfaced under the Payment request filter; everywhere else it is hidden so
    // it does not duplicate that address's real transaction row.
    if (source_index.data(ActivityListModel::IsUsedAddressRequestRole).toBool()
        && m_type_filter != PaymentRequest) {
        return false;
    }

    if (!dateMatches(source_index.data(ActivityListModel::TimestampRole).toLongLong())) {
        return false;
    }

    const TypeFilter row_type = filterTypeForIndex(source_index);
    if (m_type_filter != TypeAll && row_type != m_type_filter) {
        return false;
    }

    if (m_min_amount >= 0) {
        const CAmount net_amount = source_index.data(ActivityListModel::NetAmountSatRole).toLongLong();
        // Compare absolute value so a send and a receive of the same size both
        // pass a threshold, matching the Qt Widgets amount filter.
        if (qAbs(net_amount) < m_min_amount) {
            return false;
        }
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
        return Sent;
    case Transaction::Other:
    default:
        return Other;
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
    case LastMonth:
        end_date = QDate(current_date.year(), current_date.month(), 1);
        start_date = end_date.addMonths(-1);
        break;
    case ThisYear:
        start_date = QDate(current_date.year(), 1, 1);
        end_date = start_date.addYears(1);
        break;
    case CustomRange: {
        const QDateTime row_time = QDateTime::fromSecsSinceEpoch(timestamp);
        if (m_range_start.isValid() && row_time < QDateTime(m_range_start, QTime(0, 0))) {
            return false;
        }
        // The end date is inclusive, so accept the whole of that day.
        if (m_range_end.isValid() && row_time >= QDateTime(m_range_end.addDays(1), QTime(0, 0))) {
            return false;
        }
        return true;
    }
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
            proxy_index.data(ActivityListModel::LabelRole).toString(),
            proxy_index.data(ActivityListModel::AddressRole).toString(),
            QmlBitcoinUnits::format(ExportDisplayUnit(m_display_unit), amount, false, QmlBitcoinUnits::SeparatorStyle::NEVER),
            pending_request ? QString{} : proxy_index.data(ActivityListModel::TxIdRole).toString(),
        });
    }

    file.close();
    return file.error() == QFile::NoError;
}
