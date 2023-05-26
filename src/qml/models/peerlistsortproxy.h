// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERLISTSORTPROXY_H
#define BITCOIN_QML_MODELS_PEERLISTSORTPROXY_H

#include <qt/peertablesortproxy.h>
#include <QByteArray>
#include <QHash>
#include <QModelIndex>
#include <QVariant>

class PeerListSortProxy : public PeerTableSortProxy
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
    int RoleNameToIndex(const QString & name) const;
    QString m_sort_by;
};

#endif // BITCOIN_QML_MODELS_PEERLISTSORTPROXY_H
