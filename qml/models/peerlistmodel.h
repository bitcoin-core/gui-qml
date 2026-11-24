// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PEERLISTMODEL_H
#define BITCOIN_QML_MODELS_PEERLISTMODEL_H

#include <qml/models/peerrecord.h>
#include <qml/nativelistmodel.h>
#include <util/time.h>
#include <map>
#include <set>

#include <QModelIndex>
#include <QPointer>
#include <QStringList>
#include <QVariant>

namespace interfaces {
class Node;
}

QT_BEGIN_NAMESPACE
class QTimer;
QT_END_NAMESPACE
class PeerCommandInterface;

class PeerListModel : public NativeListModel
{
    Q_OBJECT

public:
    explicit PeerListModel(interfaces::Node& node, QObject* parent, bool backend_ready = true,
                           presentation::Post post = {},
                           std::function<NodeClock::time_point()> clock = [] { return NodeClock::now(); }, Queues queues = {});
    void backendInitialized();
    ~PeerListModel();

    Q_INVOKABLE
    void startAutoRefresh();
    Q_INVOKABLE
    void stopAutoRefresh();
    void beginShutdown();
    void close() override;
    int rowForId(qint64 id) const { return indexForKey(QString::number(id)); }
    void watchDetails(qint64 id, PeerPresentationObserver* details);
    void setCommands(PeerCommandInterface* commands);
    PeerCommandInterface* commands() const;
    bool readingAvailable() const { return !stopping(); }

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
        ConnectionId,
        NodeIdText,
    };

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    Qt::ItemFlags flags(const QModelIndex& index) const override;

public Q_SLOTS:
    void refresh();
Q_SIGNALS:
    void commandsChanged();

protected:
    void rowUpdated(const QString& key, const NativeRowHandle& row) override;
    void rowRemoved(const QString& key) override;
private:
    void notifyDetails(qint64 id, bool removed, PeerPresentationObserver* after = nullptr);
    struct DetailRegistry;
    std::shared_ptr<DetailRegistry> m_details;
    QTimer* m_timer{nullptr};
    bool m_backend_ready;
    bool m_auto_refresh_requested{false};
    bool m_destroying{false};
    PeerCommandInterface* m_commands{nullptr};
    QPointer<QObject> m_commands_object;
    QMetaObject::Connection m_commands_change, m_commands_destroyed;
};
#endif // BITCOIN_QML_MODELS_PEERLISTMODEL_H
