// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BANLISTMODEL_H
#define BITCOIN_QML_MODELS_BANLISTMODEL_H

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

    explicit BanListModel(interfaces::Node& node, QObject* parent = nullptr, bool backend_ready = true);
    void backendInitialized();

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    int count() const { return m_ban_list.size(); }

    Q_INVOKABLE bool unbanAt(int row);
    void beginShutdown() { m_stopping = true; }

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void countChanged();

private:
    interfaces::Node& m_node;
    QList<BanListEntry> m_ban_list;
    bool m_stopping{false};
    bool m_backend_ready;
};

#endif // BITCOIN_QML_MODELS_BANLISTMODEL_H
