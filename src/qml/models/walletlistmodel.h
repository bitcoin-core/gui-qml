// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETLISTMODEL_H
#define BITCOIN_QML_MODELS_WALLETLISTMODEL_H

#include <interfaces/wallet.h>
#include <QAbstractListModel>
#include <QList>

namespace interfaces {
class Node;
}

class WalletListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString selectedWallet READ selectedWallet WRITE setSelectedWallet NOTIFY selectedWalletChanged)

public:
    WalletListModel(interfaces::Node& node, QObject *parent = nullptr);
    ~WalletListModel() = default;

    enum Roles {
        NameRole = Qt::UserRole + 1
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setSelectedWallet(QString wallet_name);
    QString selectedWallet() const;

public Q_SLOTS:
    void listWalletDir();

Q_SIGNALS:
    void selectedWalletChanged();

private:
    struct Item {
        QString name;
    };

    void addItem(const Item &item);

    QList<Item> m_items;
    interfaces::Node& m_node;
    QString m_selected_wallet;

};

#endif // BITCOIN_QML_MODELS_WALLETLISTMODEL_H
