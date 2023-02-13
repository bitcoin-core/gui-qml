// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/peerlistsortproxy.h>
#include <qt/peertablemodel.h>

PeerListSortProxy::PeerListSortProxy(QObject* parent)
    : PeerTableSortProxy(parent)
{
}

QHash<int, QByteArray> PeerListSortProxy::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[PeerTableModel::NetNodeId] = "nodeId";
    roles[PeerTableModel::Age] = "age";
    roles[PeerTableModel::Address] = "address";
    roles[PeerTableModel::Direction] = "direction";
    roles[PeerTableModel::ConnectionType] = "connectionType";
    roles[PeerTableModel::Network] = "network";
    roles[PeerTableModel::Ping] = "ping";
    roles[PeerTableModel::Sent] = "sent";
    roles[PeerTableModel::Received] = "received";
    roles[PeerTableModel::Subversion] = "subversion";
    return roles;
}

QVariant PeerListSortProxy::data(const QModelIndex& index, int role) const
{
    if (role == PeerTableModel::StatsRole) {
        return PeerTableSortProxy::data(index, role);
    }

    QModelIndex converted_index = PeerTableSortProxy::index(index.row(), role);
    return PeerTableSortProxy::data(converted_index, Qt::DisplayRole);
}
