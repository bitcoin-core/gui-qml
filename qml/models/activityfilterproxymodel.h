// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H
#define BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H

#include <consensus/amount.h>

#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QList>
#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QString>

class ActivityFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(DateFilter dateFilter READ dateFilter WRITE setDateFilter NOTIFY dateFilterChanged)
    Q_PROPERTY(TypeFilter typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(QList<int> typeFilters READ typeFilters WRITE setTypeFilters NOTIFY typeFiltersChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(qint64 minAmount READ minAmount WRITE setMinAmount NOTIFY minAmountChanged)
    Q_PROPERTY(qint64 maxAmount READ maxAmount WRITE setMaxAmount NOTIFY maxAmountChanged)
    Q_PROPERTY(qint64 availableMaxAmount READ availableMaxAmount NOTIFY availableMaxAmountChanged)
    Q_PROPERTY(QDate rangeStart READ rangeStart NOTIFY rangeChanged)
    Q_PROPERTY(QDate rangeEnd READ rangeEnd NOTIFY rangeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int transactionCount READ transactionCount NOTIFY countChanged)
    Q_PROPERTY(int requestCount READ requestCount NOTIFY countChanged)
    Q_PROPERTY(qint64 pendingBalanceSat READ pendingBalanceSat NOTIFY pendingBalanceChanged)
    Q_PROPERTY(QString pendingBalance READ pendingBalance NOTIFY pendingBalanceChanged)
    Q_PROPERTY(GroupBy groupBy READ groupBy WRITE setGroupBy NOTIFY groupByChanged)

public:
    enum DateFilter {
        DateAll,
        Today,
        ThisWeek,
        ThisMonth,
        ThisYear,
        CustomRange
    };
    Q_ENUM(DateFilter)

    enum TypeFilter {
        TypeAll,
        Received,
        Sent,
        SentToSelf,
        Mined,
        Other,
        PaymentRequest,
        Multiple,
        Consolidation,
        Split
    };
    Q_ENUM(TypeFilter)

    enum GroupBy { Month, Day };
    Q_ENUM(GroupBy)
    enum SectionRole { SectionKeyRole = Qt::UserRole + 200, SectionLabelRole, DateTimeLabelRole };

    explicit ActivityFilterProxyModel(QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    void setSourceModel(QAbstractItemModel* source_model) override;

    QString searchText() const;
    void setSearchText(const QString& search_text);

    DateFilter dateFilter() const;
    void setDateFilter(DateFilter date_filter);

    TypeFilter typeFilter() const;
    void setTypeFilter(TypeFilter type_filter);

    QList<int> typeFilters() const { return m_type_filters; }
    void setTypeFilters(const QList<int>& types);

    int displayUnit() const;
    void setDisplayUnit(int display_unit);

    CAmount minAmount() const;
    void setMinAmount(CAmount min_amount);

    CAmount maxAmount() const { return m_max_amount; }
    void setMaxAmount(CAmount max_amount);
    CAmount availableMaxAmount() const { return m_available_max_amount; }
    Q_INVOKABLE bool setAmountRange(qint64 minimum, qint64 maximum);

    QDate rangeStart() const;
    QDate rangeEnd() const;

    // Apply a custom date range from ISO yyyy-MM-dd strings, selecting the
    // CustomRange date filter with it. QML passes strings rather than assigning
    // the QDate properties directly because the JavaScript Date to QDate
    // conversion shifts the day across time zones. Returns false, changing
    // nothing, when either date is unparseable or the range is inverted; the
    // range is only ever set through here, which is why the two date properties
    // are read-only.
    Q_INVOKABLE bool applyCustomRange(const QString& start_iso, const QString& end_iso);

    int count() const;
    int transactionCount() const;
    int requestCount() const;
    CAmount pendingBalanceSat() const;
    QString pendingBalance() const;
    GroupBy groupBy() const { return m_group_by; }
    void setGroupBy(GroupBy group_by);

    Q_INVOKABLE bool exportCsv(const QString& path) const;

Q_SIGNALS:
    void searchTextChanged();
    void dateFilterChanged();
    void typeFilterChanged();
    void typeFiltersChanged();
    void displayUnitChanged();
    void minAmountChanged();
    void maxAmountChanged();
    void availableMaxAmountChanged();
    void rangeChanged();
    void countChanged();
    void pendingBalanceChanged();
    void groupByChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool lessThan(const QModelIndex& left_index, const QModelIndex& right_index) const override;

private:
    TypeFilter filterTypeForIndex(const QModelIndex& source_index) const;
    bool groupedSource() const;
    bool groupedTypeMatches(const QModelIndex& source_index, TypeFilter type) const;
    void updateAvailableMaxAmount();
    QString exportTypeLabelForIndex(const QModelIndex& proxy_index) const;
    bool dateMatches(qint64 timestamp) const;
    QString normalizedExportPath(const QString& path) const;

    QString m_search_text;
    DateFilter m_date_filter{DateAll};
    QList<int> m_type_filters;
    int m_display_unit{0};
    CAmount m_min_amount{-1};
    CAmount m_max_amount{-1};
    CAmount m_available_max_amount{0};
    QList<QMetaObject::Connection> m_source_connections;
    QDate m_range_start;
    QDate m_range_end;
    GroupBy m_group_by{Month};
};

#endif // BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H
