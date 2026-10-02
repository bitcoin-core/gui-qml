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

#include <memory>

class BackendExecutor;

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
    Q_PROPERTY(bool ready READ ready NOTIFY refreshStateChanged)
    Q_PROPERTY(bool refreshPending READ refreshPending NOTIFY refreshStateChanged)
    Q_PROPERTY(QString refreshError READ refreshError NOTIFY refreshStateChanged)

public:
    explicit PeerListModel(interfaces::Node& node, QObject* parent, bool backend_ready = true);
    void backendInitialized();
    ~PeerListModel();

    Q_INVOKABLE
    void startAutoRefresh();
    Q_INVOKABLE
    void stopAutoRefresh();
    void beginShutdown();
    bool isDrained() const;
    bool ready() const { return m_ready; }
    bool refreshPending() const { return m_refresh_running; }
    QString refreshError() const { return m_refresh_error; }

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

Q_SIGNALS:
    void refreshStateChanged();
    void drained();

private:
    void applySnapshot(QList<CNodeCombinedStats> peers);
    void finishRefresh(quint64 generation, QList<CNodeCombinedStats> peers, const QString& error);
    QList<CNodeCombinedStats> m_peers_data{};
    interfaces::Node& m_node;
    std::shared_ptr<BackendExecutor> m_executor;
    QTimer* m_timer{nullptr};
    bool m_stopping{false};
    bool m_backend_ready;
    bool m_auto_refresh_requested{false};
    bool m_ready{false};
    bool m_refresh_running{false};
    bool m_refresh_requested{false};
    quint64 m_generation{0};
    QString m_refresh_error;
};

#endif // BITCOIN_QML_MODELS_PEERLISTMODEL_H
