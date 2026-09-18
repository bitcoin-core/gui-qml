// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/activityfilterproxymodel.h>

#include <qml/bitcoinunits.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/transaction.h>
#include <qml/models/transactionactivitymodel.h>

#include <algorithm>

#include <QDate>
#include <QDateTime>
#include <QFile>
#include <QLocale>
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
    connect(this, &ActivityFilterProxyModel::countChanged, this, &ActivityFilterProxyModel::pendingBalanceChanged);
    connect(this, &QAbstractItemModel::dataChanged, this, &ActivityFilterProxyModel::pendingBalanceChanged);
    connect(this, &ActivityFilterProxyModel::displayUnitChanged, this, &ActivityFilterProxyModel::pendingBalanceChanged);
}

QHash<int, QByteArray> ActivityFilterProxyModel::roleNames() const
{
    auto roles = sourceModel() ? sourceModel()->roleNames() : QHash<int, QByteArray>{};
    if (groupedSource()) {
        roles.insert(SectionKeyRole, "sectionKey");
        roles.insert(SectionLabelRole, "sectionLabel");
        roles.insert(DateTimeLabelRole, "dateTimeLabel");
    }
    return roles;
}

bool ActivityFilterProxyModel::groupedSource() const
{
    return qobject_cast<TransactionActivityModel*>(sourceModel()) != nullptr;
}

QVariant ActivityFilterProxyModel::data(const QModelIndex& index, int role) const
{
    if (groupedSource() && index.isValid() && index.model() == this &&
        (role == SectionKeyRole || role == SectionLabelRole || role == DateTimeLabelRole)) {
        const bool pending = QSortFilterProxyModel::data(index, TransactionActivityModel::IsPendingRole).toBool();
        const auto date = QDateTime::fromSecsSinceEpoch(QSortFilterProxyModel::data(index, TransactionActivityModel::TimestampRole).toLongLong());
        if (role == DateTimeLabelRole) {
            return QLocale().toString(date, m_group_by == Day && !pending ? "h:mm AP" : "MMM d, h:mm AP");
        }
        if (role == SectionKeyRole) return pending ? QStringLiteral("pending") : date.toString(m_group_by == Day ? "yyyy-MM-dd" : "yyyy-MM");
        return pending ? tr("Pending") : QLocale().toString(date.date(), m_group_by == Day ? "MMMM d, yyyy" : "MMMM yyyy");
    }
    return QSortFilterProxyModel::data(index, role);
}

void ActivityFilterProxyModel::setGroupBy(GroupBy group_by)
{
    if (m_group_by == group_by) return;
    m_group_by = group_by;
    if (rowCount()) Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {SectionKeyRole, SectionLabelRole, DateTimeLabelRole});
    Q_EMIT groupByChanged();
}

void ActivityFilterProxyModel::setSourceModel(QAbstractItemModel* source_model)
{
    if (sourceModel() == source_model) return;

    for (const auto& connection : m_source_connections) disconnect(connection);
    m_source_connections.clear();
    QSortFilterProxyModel::setSourceModel(source_model);
    if (source_model) {
        m_source_connections << connect(source_model, &QAbstractItemModel::modelReset, this, &ActivityFilterProxyModel::updateAvailableMaxAmount);
        m_source_connections << connect(source_model, &QAbstractItemModel::rowsInserted, this, &ActivityFilterProxyModel::updateAvailableMaxAmount);
        m_source_connections << connect(source_model, &QAbstractItemModel::rowsRemoved, this, &ActivityFilterProxyModel::updateAvailableMaxAmount);
        m_source_connections << connect(source_model, &QAbstractItemModel::dataChanged, this,
            [this](const QModelIndex&, const QModelIndex&, const QList<int>& roles) {
                if (roles.isEmpty() || roles.contains(TransactionActivityModel::NetAmountSatRole) || roles.contains(TransactionActivityModel::IsPendingRequestRole)) updateAvailableMaxAmount();
            });
        m_source_connections << connect(source_model, &QObject::destroyed, this, [this] {
            m_available_max_amount = 0;
            Q_EMIT availableMaxAmountChanged();
        });
    }
    updateAvailableMaxAmount();
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
    return m_type_filters.isEmpty() ? TypeAll : static_cast<TypeFilter>(m_type_filters.first());
}

void ActivityFilterProxyModel::setTypeFilter(TypeFilter type_filter)
{
    setTypeFilters(type_filter == TypeAll ? QList<int>{} : QList<int>{type_filter});
}

void ActivityFilterProxyModel::setTypeFilters(const QList<int>& types)
{
    QList<int> normalized;
    for (int type : types) {
        if (type > TypeAll && type <= Split && !normalized.contains(type)) normalized.append(type);
    }
    std::sort(normalized.begin(), normalized.end());
    if (m_type_filters == normalized) return;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_type_filters = normalized;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    Q_EMIT typeFilterChanged();
    Q_EMIT typeFiltersChanged();
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
    setAmountRange(min_amount, m_max_amount);
}

void ActivityFilterProxyModel::setMaxAmount(CAmount max_amount)
{
    setAmountRange(m_min_amount, max_amount);
}

bool ActivityFilterProxyModel::setAmountRange(qint64 minimum, qint64 maximum)
{
    // Negative bounds are unset; an inverted range must not hide all activity.
    minimum = minimum < 0 ? -1 : minimum;
    maximum = maximum < 0 ? -1 : maximum;
    if (minimum >= 0 && maximum >= 0 && minimum > maximum) return false;
    const bool min_changed = m_min_amount != minimum;
    const bool max_changed = m_max_amount != maximum;
    if (!min_changed && !max_changed) return true;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_min_amount = minimum;
    m_max_amount = maximum;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif
    if (min_changed) Q_EMIT minAmountChanged();
    if (max_changed) Q_EMIT maxAmountChanged();
    Q_EMIT countChanged();
    return true;
}

void ActivityFilterProxyModel::updateAvailableMaxAmount()
{
    CAmount maximum{0};
    if (sourceModel()) {
        for (int row = 0; row < sourceModel()->rowCount(); ++row) {
            const auto index = sourceModel()->index(row, 0);
            if (!index.data(TransactionActivityModel::IsPendingRequestRole).toBool()) {
                maximum = std::max(maximum, CAmount(qAbs(index.data(TransactionActivityModel::NetAmountSatRole).toLongLong())));
            }
        }
    }
    if (m_available_max_amount == maximum) return;
    m_available_max_amount = maximum;
    Q_EMIT availableMaxAmountChanged();
}

QDate ActivityFilterProxyModel::rangeStart() const
{
    return m_range_start;
}

QDate ActivityFilterProxyModel::rangeEnd() const
{
    return m_range_end;
}

bool ActivityFilterProxyModel::applyCustomRange(const QString& start_iso, const QString& end_iso)
{
    const QDate start = QDate::fromString(start_iso, Qt::ISODate);
    const QDate end = QDate::fromString(end_iso, Qt::ISODate);
    // Reject a half-picked or inverted range outright: filtering on one would
    // silently show nothing, which reads as a broken filter rather than a
    // rejected input.
    if (!start.isValid() || !end.isValid() || start > end) return false;

    const bool range_changed = start != m_range_start || end != m_range_end;
    const bool filter_changed = m_date_filter != CustomRange;
    if (!range_changed && !filter_changed) return true;

#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
#endif
    m_range_start = start;
    m_range_end = end;
    m_date_filter = CustomRange;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    endFilterChange(QSortFilterProxyModel::Direction::Rows);
#else
    invalidateFilter();
#endif

    // Both dates and the date filter move together, so the view refilters once
    // instead of passing through the intermediate half-applied ranges two
    // separate setters would have published.
    if (range_changed) Q_EMIT rangeChanged();
    if (filter_changed) Q_EMIT dateFilterChanged();
    Q_EMIT countChanged();
    return true;
}

int ActivityFilterProxyModel::count() const
{
    return rowCount();
}

int ActivityFilterProxyModel::requestCount() const
{
    int requests{0};
    for (int row = 0; row < rowCount(); ++row) requests += index(row, 0).data(TransactionActivityModel::IsPendingRequestRole).toBool();
    return requests;
}

int ActivityFilterProxyModel::transactionCount() const
{
    return rowCount() - requestCount();
}

CAmount ActivityFilterProxyModel::pendingBalanceSat() const
{
    CAmount total{0};
    if (!groupedSource()) return total;
    for (int row = 0; row < rowCount(); ++row) {
        const auto item = index(row, 0);
        if (item.data(TransactionActivityModel::IsPendingRole).toBool()
            && !item.data(TransactionActivityModel::IsPendingRequestRole).toBool()) {
            total += item.data(TransactionActivityModel::NetAmountSatRole).toLongLong();
        }
    }
    return total;
}

QString ActivityFilterProxyModel::pendingBalance() const
{
    const auto amount = pendingBalanceSat();
    const auto unit = QmlBitcoinUnits::fromDisplayUnit(m_display_unit);
    return QmlBitcoinUnits::formatForDisplay(unit, amount, true) + QLatin1Char(' ')
        + QmlBitcoinUnits::displayLabel(unit, amount);
}

bool ActivityFilterProxyModel::groupedTypeMatches(const QModelIndex& source_index, TypeFilter filter) const
{
    if (filter == TypeAll) return true;
    if (filter == PaymentRequest) return source_index.data(TransactionActivityModel::HasPaymentRequestRole).toBool();
    if (source_index.data(TransactionActivityModel::IsPendingRequestRole).toBool()) return false;

    const auto type = static_cast<TransactionActivityModel::ActivityType>(source_index.data(TransactionActivityModel::ActivityTypeRole).toInt());
    switch (filter) {
    case Multiple: return type == TransactionActivityModel::Multiple;
    case Consolidation: return type == TransactionActivityModel::Consolidation;
    case Split: return type == TransactionActivityModel::Split;
    case SentToSelf: return type == TransactionActivityModel::InternalTransfer || type == TransactionActivityModel::Consolidation || type == TransactionActivityModel::Split;
    case Mined: return type == TransactionActivityModel::Mined;
    case Other: return type == TransactionActivityModel::Other;
    case Received:
    case Sent: {
        if (type == TransactionActivityModel::Mined) return false;
        const int direction = filter == Received ? TransactionActivityModel::ReceiveAction : TransactionActivityModel::SendAction;
        for (const auto& action : source_index.data(TransactionActivityModel::ActionsRole).toList()) {
            if (action.toMap().value("direction").toInt() == direction) return true;
        }
        return false;
    }
    default: return false;
    }
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
        && !m_type_filters.contains(PaymentRequest)) {
        return false;
    }

    if (!dateMatches(source_index.data(TransactionActivityModel::TimestampRole).toLongLong())) {
        return false;
    }

    if (!m_type_filters.isEmpty()) {
        const bool matches = std::any_of(m_type_filters.cbegin(), m_type_filters.cend(), [&](int type) {
            return groupedSource() ? groupedTypeMatches(source_index, static_cast<TypeFilter>(type))
                                   : filterTypeForIndex(source_index) == type;
        });
        if (!matches) return false;
    }

    // Parent wallet impact includes the fee. Treat equal sends and receives
    // alike and retain every child when its parent passes the inclusive range.
    const auto amount = qAbs(source_index.data(TransactionActivityModel::NetAmountSatRole).toLongLong());
    if ((m_min_amount >= 0 && amount < m_min_amount) || (m_max_amount >= 0 && amount > m_max_amount)) return false;

    const QString search = m_search_text.trimmed();
    if (!search.isEmpty()) {
        const QString address = source_index.data(TransactionActivityModel::AddressRole).toString();
        const QString label = source_index.data(TransactionActivityModel::LabelRole).toString();
        const QString txid = source_index.data(TransactionActivityModel::TxidRole).toString();
        const QString children = groupedSource() ? source_index.data(TransactionActivityModel::SearchTextRole).toString() : QString{};
        if (!children.contains(search, Qt::CaseInsensitive) &&
            !address.contains(search, Qt::CaseInsensitive) &&
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
    if (groupedSource()) {
        const bool left_pending = left_index.data(TransactionActivityModel::IsPendingRole).toBool();
        const bool right_pending = right_index.data(TransactionActivityModel::IsPendingRole).toBool();
        if (left_pending != right_pending) return !left_pending;
    }
    const qint64 left_timestamp = sourceModel()->data(left_index, TransactionActivityModel::TimestampRole).toLongLong();
    const qint64 right_timestamp = sourceModel()->data(right_index, TransactionActivityModel::TimestampRole).toLongLong();
    if (groupedSource() && left_timestamp == right_timestamp) {
        return left_index.data(TransactionActivityModel::IdRole).toString() < right_index.data(TransactionActivityModel::IdRole).toString();
    }
    return left_timestamp < right_timestamp;
}

ActivityFilterProxyModel::TypeFilter ActivityFilterProxyModel::filterTypeForIndex(const QModelIndex& source_index) const
{
    if (source_index.data(TransactionActivityModel::IsPendingRequestRole).toBool()) {
        return PaymentRequest;
    }

    switch (source_index.data(TransactionActivityModel::TypeRole).toInt()) {
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
    if (proxy_index.data(TransactionActivityModel::IsPendingRequestRole).toBool()) {
        return tr("Payment request");
    }

    if (groupedSource()) {
        switch (proxy_index.data(TransactionActivityModel::ActivityTypeRole).toInt()) {
        case TransactionActivityModel::Multiple: return tr("Multiple actions");
        case TransactionActivityModel::Consolidation: return tr("Consolidation");
        case TransactionActivityModel::Split: return tr("Split");
        default: break;
        }
    }

    switch (proxy_index.data(TransactionActivityModel::TypeRole).toInt()) {
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
    case CustomRange: {
        const QDateTime row_time = QDateTime::fromSecsSinceEpoch(timestamp);
        // startOfDay() instead of midnight: when daylight saving skips
        // midnight, QDateTime{date, QTime(0, 0)} is invalid and compares
        // before every valid time, breaking both bounds.
        if (m_range_start.isValid() && row_time < m_range_start.startOfDay()) {
            return false;
        }
        // The end date is inclusive, so accept the whole of that day.
        if (m_range_end.isValid() && row_time >= m_range_end.addDays(1).startOfDay()) {
            return false;
        }
        return true;
    }
    case DateAll:
        return true;
    }

    const QDateTime start_of_range = start_date.startOfDay();
    const QDateTime end_of_range = end_date.startOfDay();
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
    const bool grouped = qobject_cast<TransactionActivityModel*>(sourceModel()) != nullptr;
    QStringList header{
        tr("Confirmed"),
        tr("Date"),
        tr("Type"),
        tr("Label"),
        tr("Address"),
        tr("Amount") + QStringLiteral(" (%1)").arg(ExportDisplayUnitLabel(m_display_unit)),
        tr("ID"),
    };
    if (grouped) header << tr("Record") << tr("Action ID") << tr("Status")
                        << tr("Action amount") + QStringLiteral(" (%1)").arg(ExportDisplayUnitLabel(m_display_unit));
    WriteCsvRow(stream, header);

    for (int row = 0; row < rowCount(); ++row) {
        const QModelIndex proxy_index = index(row, 0);
        const qint64 timestamp = proxy_index.data(TransactionActivityModel::TimestampRole).toLongLong();
        const auto status = static_cast<Transaction::Status>(proxy_index.data(TransactionActivityModel::StatusRole).toInt());
        const bool confirmed = status == Transaction::Confirming || status == Transaction::Confirmed;
        const bool replaced = grouped
            && !proxy_index.data(TransactionActivityModel::ReplacedByTxidRole).toString().isEmpty()
            && (!proxy_index.data(TransactionActivityModel::StatusKnownRole).toBool()
                || proxy_index.data(TransactionActivityModel::DepthRole).toInt() <= 0);
        const CAmount amount = replaced ? 0 : proxy_index.data(TransactionActivityModel::NetAmountSatRole).toLongLong();
        const bool pending_request = proxy_index.data(TransactionActivityModel::IsPendingRequestRole).toBool();

        QString status_label;
        if (pending_request) status_label = tr("Awaiting payment");
        else if (replaced) status_label = tr("Replaced");
        else if (grouped && !proxy_index.data(TransactionActivityModel::StatusKnownRole).toBool()) status_label = tr("Unknown");
        else {
            switch (status) {
            case Transaction::Confirmed: status_label = tr("Confirmed"); break;
            case Transaction::Confirming: status_label = tr("Confirming"); break;
            case Transaction::Unconfirmed: status_label = tr("Unconfirmed"); break;
            case Transaction::Conflicted: status_label = tr("Conflicted"); break;
            case Transaction::Abandoned: status_label = tr("Abandoned"); break;
            case Transaction::Immature: status_label = tr("Immature"); break;
            case Transaction::NotAccepted: status_label = tr("Not accepted"); break;
            }
        }

        QStringList values{
            confirmed ? QStringLiteral("true") : QStringLiteral("false"),
            QDateTime::fromSecsSinceEpoch(timestamp).toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
            exportTypeLabelForIndex(proxy_index),
            GuardedCsvText(proxy_index.data(TransactionActivityModel::LabelRole).toString()),
            GuardedCsvText(proxy_index.data(TransactionActivityModel::AddressRole).toString()),
            QmlBitcoinUnits::format(ExportDisplayUnit(m_display_unit), amount, false, QmlBitcoinUnits::SeparatorStyle::NEVER),
            pending_request ? QString{} : proxy_index.data(TransactionActivityModel::TxidRole).toString(),
        };
        if (grouped) values << (pending_request ? tr("Payment request") : tr("Transaction")) << QString{} << status_label << QString{};
        WriteCsvRow(stream, values);

        // Keep the wallet impact (including fees) only on the parent. Action
        // amounts describe individual movements and are not wallet balance deltas.
        const auto actions = proxy_index.data(TransactionActivityModel::ActionsRole).toList();
        if (!grouped || pending_request || actions.size() <= 1) continue;
        for (const auto& value : actions) {
            const auto action = value.toMap();
            const int direction = action.value("direction").toInt();
            const CAmount action_amount = action.value("amountSat").toLongLong();
            WriteCsvRow(stream, {
                values[0], values[1],
                direction == TransactionActivityModel::SendAction ? tr("Sent")
                    : direction == TransactionActivityModel::ReceiveAction ? tr("Received") : tr("Internal"),
                GuardedCsvText(action.value("label").toString()),
                GuardedCsvText(action.value("address").toString()),
                QString{}, values[6], tr("Action"), GuardedCsvText(action.value("actionId").toString()), status_label,
                QmlBitcoinUnits::format(ExportDisplayUnit(m_display_unit),
                    direction == TransactionActivityModel::SendAction ? -action_amount : action_amount,
                    false, QmlBitcoinUnits::SeparatorStyle::NEVER),
            });
        }
    }

    file.close();
    return file.error() == QFile::NoError;
}
