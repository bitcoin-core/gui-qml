// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BANLISTMODEL_H
#define BITCOIN_QML_MODELS_BANLISTMODEL_H

#include <qml/backendworker.h>

#include <net_types.h>
#include <netaddress.h>

#include <QAbstractListModel>
#include <QList>

namespace interfaces { class Node; }

struct BanListEntry
{
    CSubNet subnet;
    CBanEntry ban_entry;
};

class BanListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum class BanRoles {
        AddressRole = Qt::UserRole,
        BanUntilRole
    };

    explicit BanListModel(interfaces::Node& node, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    int count() const { return m_ban_list.size(); }

    Q_INVOKABLE bool unbanAt(int row);
    void drainBackend();
    void onNodeReady();

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void countChanged();
    void backendDrained();
    void unbanFinished(bool success);

private:
    interfaces::Node& m_node;
    BackendWorker m_backend;
    bool m_pending{false};
    bool m_again{false};
    bool m_draining{false};
    bool m_node_ready{false};
    QList<BanListEntry> m_ban_list;
};

#endif // BITCOIN_QML_MODELS_BANLISTMODEL_H
