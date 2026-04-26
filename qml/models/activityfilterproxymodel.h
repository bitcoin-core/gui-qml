// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H
#define BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H

#include <QByteArray>
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
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum DateFilter {
        DateAll,
        Today,
        ThisWeek,
        ThisMonth,
        ThisYear
    };
    Q_ENUM(DateFilter)

    enum TypeFilter {
        TypeAll,
        Received,
        Sent,
        SentToSelf,
        Mined,
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

    int count() const;

    Q_INVOKABLE bool exportCsv(const QString& path) const;

Q_SIGNALS:
    void searchTextChanged();
    void dateFilterChanged();
    void typeFilterChanged();
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
};

#endif // BITCOIN_QML_MODELS_ACTIVITYFILTERPROXYMODEL_H
