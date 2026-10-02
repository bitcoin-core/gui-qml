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
#include <QVariantMap>
#include <QThread>

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
    Q_PROPERTY(QVariantMap summary READ summary NOTIFY summaryChanged)
    Q_PROPERTY(bool widgetActive READ widgetActive WRITE setWidgetActive NOTIFY widgetActiveChanged)

public:
    explicit PeerListModel(interfaces::Node& node, QObject* parent);
    ~PeerListModel();
    bool pending() const { return m_pending; }
    QVariantMap summary() const { return m_summary; }
    bool widgetActive() const { return m_widget_active; }
    void setWidgetActive(bool active);

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
    void stopForShutdown();

Q_SIGNALS:
    void shutdownFinished();
    void summaryChanged();
    void widgetActiveChanged();

private:
    void applySnapshot(QList<CNodeCombinedStats> peers, QVariantMap summary);
    void updateRefreshTimer();
    void setSummary(QVariantMap summary);
    QVariantMap m_summary;
    QThread m_thread;
    QObject* m_worker{nullptr};
    bool m_pending{false};
    bool m_widget_active{false};
    bool m_auto_refresh{false};
    bool m_shutting_down{false};
    QList<CNodeCombinedStats> m_peers_data{};
    interfaces::Node& m_node;
    QTimer* m_timer{nullptr};
};

#endif // BITCOIN_QML_MODELS_PEERLISTMODEL_H
