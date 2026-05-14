// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETLISTMODEL_H
#define BITCOIN_QML_MODELS_WALLETLISTMODEL_H

#include <interfaces/wallet.h>
#include <QAbstractListModel>
#include <QList>
#include <QSet>
#include <QStringList>

namespace interfaces {
class Node;
}

class WalletListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum class LoadState {
        Closed = 0,
        Open = 1,
    };
    Q_ENUM(LoadState)

    WalletListModel(interfaces::Node& node, QObject *parent = nullptr);
    ~WalletListModel() = default;

    enum Roles {
        NameRole = Qt::UserRole + 1,
        FormatRole,
        LoadStateRole,
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

public Q_SLOTS:
    void listWalletDir();
    void setOpenWalletNames(const QStringList& wallet_names);

private:
    struct Item {
        QString name;
        QString format;
    };

    bool itemLess(const Item& a, const Item& b) const;
    void sortItems(QList<Item>& items) const;
    bool applyUpdatedItems(QList<Item>&& updated_items);
    void updateLoadStateForAllRows();

    QList<Item> m_items;
    QSet<QString> m_open_wallet_names;
    interfaces::Node& m_node;
};

#endif // BITCOIN_QML_MODELS_WALLETLISTMODEL_H
