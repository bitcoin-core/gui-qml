// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H
#define BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H

#include <consensus/amount.h>

#include <QByteArray>
#include <QDate>
#include <QHash>
#include <QModelIndex>
#include <QSortFilterProxyModel>
#include <QString>

class ActivityFilterProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(DateFilter dateFilter READ dateFilter WRITE setDateFilter NOTIFY dateFilterChanged)
    Q_PROPERTY(TypeFilter typeFilter READ typeFilter WRITE setTypeFilter NOTIFY typeFilterChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(qint64 minAmount READ minAmount WRITE setMinAmount NOTIFY minAmountChanged)
    Q_PROPERTY(QDate rangeStart READ rangeStart NOTIFY rangeChanged)
    Q_PROPERTY(QDate rangeEnd READ rangeEnd NOTIFY rangeChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum DateFilter {
        DateAll,
        Today,
        ThisWeek,
        ThisMonth,
        LastMonth,
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
        PaymentRequest
    };
    Q_ENUM(TypeFilter)

    explicit ActivityFilterProxyModel(QObject* parent = nullptr);

    QHash<int, QByteArray> roleNames() const override;
    void setSourceModel(QAbstractItemModel* source_model) override;

    QString searchText() const;
    void setSearchText(const QString& search_text);

    DateFilter dateFilter() const;
    void setDateFilter(DateFilter date_filter);

    TypeFilter typeFilter() const;
    void setTypeFilter(TypeFilter type_filter);

    int displayUnit() const;
    void setDisplayUnit(int display_unit);

    CAmount minAmount() const;
    void setMinAmount(CAmount min_amount);

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

    Q_INVOKABLE bool exportCsv(const QString& path) const;

Q_SIGNALS:
    void searchTextChanged();
    void dateFilterChanged();
    void typeFilterChanged();
    void displayUnitChanged();
    void minAmountChanged();
    void rangeChanged();
    void countChanged();

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    bool lessThan(const QModelIndex& left_index, const QModelIndex& right_index) const override;

private:
    TypeFilter filterTypeForIndex(const QModelIndex& source_index) const;
    QString exportTypeLabelForIndex(const QModelIndex& proxy_index) const;
    bool dateMatches(qint64 timestamp) const;
    QString normalizedExportPath(const QString& path) const;

    QString m_search_text;
    DateFilter m_date_filter{DateAll};
    TypeFilter m_type_filter{TypeAll};
    int m_display_unit{0};
    CAmount m_min_amount{-1};
    QDate m_range_start;
    QDate m_range_end;
};

#endif // BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H
