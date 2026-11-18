// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BANLISTMODEL_H
#define BITCOIN_QML_MODELS_BANLISTMODEL_H

#include <net_types.h>
#include <netaddress.h>
#include <qml/nativelistmodel.h>

#include <QAbstractListModel>
#include <QList>

namespace interfaces { class Node; }

struct BanListEntry
{
    CSubNet subnet;
    CBanEntry ban_entry;
};

class BanListModel : public NativeListModel
{
    Q_OBJECT

public:
    enum class BanRoles {
        AddressRole = Qt::UserRole,
        BanUntilRole
    };

    explicit BanListModel(interfaces::Node& node, QObject* parent = nullptr, bool backend_ready = true,
                          execution::SnapshotServices services = {});
    ~BanListModel() override;
    void backendInitialized();

    QHash<int, QByteArray> roleNames() const override;

    Q_PROPERTY(int count READ count NOTIFY countChanged)
    int count() const { return rowCount(); }

    Q_INVOKABLE bool unbanAt(int row);
    void beginShutdown();
    void close() override;

public Q_SLOTS:
    void refresh();

Q_SIGNALS:
    void countChanged();
    void unbanFinished(bool success);

private:
    void publishCommand();
    std::shared_ptr<execution::OwnedCommand<bool>> m_unban_command;
    interfaces::Node& m_node;
    bool m_backend_ready;
};
#endif // BITCOIN_QML_MODELS_BANLISTMODEL_H
