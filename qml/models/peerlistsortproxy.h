// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERLISTSORTPROXY_H
#define BITCOIN_QML_MODELS_PEERLISTSORTPROXY_H

#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QPointer>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QVariant>

class PeerDetailsModel;

class PeerListSortProxy : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)
    Q_PROPERTY(bool sortAscending READ sortAscending WRITE setSortAscending NOTIFY sortAscendingChanged)
    Q_PROPERTY(QString searchText READ searchText WRITE setSearchText NOTIFY searchTextChanged)
    Q_PROPERTY(QStringList directionFilters READ directionFilters WRITE setDirectionFilters NOTIFY directionFiltersChanged)
    Q_PROPERTY(QStringList connectionTypeFilters READ connectionTypeFilters WRITE setConnectionTypeFilters NOTIFY connectionTypeFiltersChanged)
    Q_PROPERTY(QStringList networkFilters READ networkFilters WRITE setNetworkFilters NOTIFY networkFiltersChanged)
    Q_PROPERTY(QStringList transportFilters READ transportFilters WRITE setTransportFilters NOTIFY transportFiltersChanged)
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
    explicit PeerListSortProxy(QObject* parent);
    ~PeerListSortProxy() = default;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString sortBy() const;
    bool sortAscending() const { return m_sort_ascending; }
    QString searchText() const { return m_search_text; }
    QStringList directionFilters() const { return m_direction_filters; }
    QStringList connectionTypeFilters() const { return m_connection_type_filters; }
    QStringList networkFilters() const { return m_network_filters; }
    QStringList transportFilters() const { return m_transport_filters; }

    Q_INVOKABLE PeerDetailsModel* peerDetailsAt(int row) const;
    Q_INVOKABLE int indexOfNodeId(qint64 node_id) const;

public Q_SLOTS:
    void setSortBy(const QString & roleName);
    void setSortAscending(bool ascending);
    void setSearchText(const QString& search_text);
    void setDirectionFilters(const QStringList& filters);
    void setConnectionTypeFilters(const QStringList& filters);
    void setNetworkFilters(const QStringList& filters);
    void setTransportFilters(const QStringList& filters);

Q_SIGNALS:
    void sortByChanged(const QString & roleName);
    void sortAscendingChanged(bool ascending);
    void searchTextChanged(const QString& search_text);
    void directionFiltersChanged(const QStringList& filters);
    void connectionTypeFiltersChanged(const QStringList& filters);
    void networkFiltersChanged(const QStringList& filters);
    void transportFiltersChanged(const QStringList& filters);
    void countChanged();

private:
    bool lessThan(const QModelIndex& left_index, const QModelIndex& right_index) const override;
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override;
    int RoleNameToRole(const QString & name) const;
    int m_sort_role{0};
    QString m_sort_by;
    bool m_sort_ascending{true};
    QString m_search_text;
    QStringList m_direction_filters;
    QStringList m_connection_type_filters;
    QStringList m_network_filters;
    QStringList m_transport_filters;
    mutable QHash<qint64, QPointer<PeerDetailsModel>> m_detail_models;
};

#endif // BITCOIN_QML_MODELS_PEERLISTSORTPROXY_H
