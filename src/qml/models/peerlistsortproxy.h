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
#include <QVariant>

class PeerDetailsModel;

class PeerListSortProxy : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString sortBy READ sortBy WRITE setSortBy NOTIFY sortByChanged)

public:
    explicit PeerListSortProxy(QObject* parent);
    ~PeerListSortProxy() = default;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    QString sortBy() const;

public Q_SLOTS:
    void setSortBy(const QString & roleName);

Q_SIGNALS:
    void sortByChanged(const QString & roleName);

private:
    bool lessThan(const QModelIndex& left_index, const QModelIndex& right_index) const override;
    int RoleNameToRole(const QString & name) const;
    int m_sort_role{0};
    QString m_sort_by;
    mutable QHash<qint64, QPointer<PeerDetailsModel>> m_details;
};

#endif // BITCOIN_QML_MODELS_PEERLISTSORTPROXY_H
