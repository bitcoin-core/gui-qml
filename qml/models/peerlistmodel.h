// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERLISTMODEL_H
#define BITCOIN_QML_MODELS_PEERLISTMODEL_H

#include <net.h>
#include <net_processing.h>

#include <QAbstractListModel>
#include <QList>
#include <QModelIndex>
#include <QStringList>
#include <QVariant>

namespace interfaces {
class Node;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE

struct CNodeCombinedStats {
    CNodeStats nodeStats;
    CNodeStateStats nodeStateStats;
    bool fNodeStateStatsAvailable;
};
Q_DECLARE_METATYPE(const CNodeCombinedStats*)

class PeerListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit PeerListModel(interfaces::Node& node, QObject* parent);
    ~PeerListModel();

    Q_INVOKABLE
    void startAutoRefresh();
    Q_INVOKABLE
    void stopAutoRefresh();

    enum Role {
        StatsRole = Qt::UserRole,
        NetNodeId = Qt::UserRole + 1,
        Age,
        Address,
        Direction,
        ConnectionType,
        Network,
        Ping,
        Sent,
        Received,
        Subversion,
        Transport,
    };

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

public Q_SLOTS:
    void refresh();

private:
    QList<CNodeCombinedStats> m_peers_data{};
    interfaces::Node& m_node;
    QTimer* m_timer{nullptr};
};

#endif // BITCOIN_QML_MODELS_PEERLISTMODEL_H
