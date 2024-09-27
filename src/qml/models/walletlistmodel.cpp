// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletlistmodel.h>

#include <interfaces/node.h>

#include <QSet>

WalletListModel::WalletListModel(interfaces::Node& node, QObject *parent)
: QAbstractListModel(parent)
, m_node(node)
{
}

void WalletListModel::listWalletDir()
{
    QSet<QString> existing_names;
    for (int i = 0; i < rowCount(); ++i) {
        QModelIndex index = this->index(i, 0);
        QString name = data(index, NameRole).toString();
        existing_names.insert(name);
    }

    for (const std::string &name : m_node.walletLoader().listWalletDir()) {
        QString qname = QString::fromStdString(name);
        if (!existing_names.contains(qname)) {
            addItem({ qname });
        }
    }
}

int WalletListModel::rowCount(const QModelIndex &parent) const
{
    Q_UNUSED(parent);
    return m_items.size();
}

QVariant WalletListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_items.size())
        return QVariant();

    const auto &item = m_items[index.row()];
    switch (role) {
    case Qt::DisplayRole:
    case NameRole:
        return item.name;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> WalletListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    return roles;
}

void WalletListModel::addItem(const Item &item)
{
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    m_items.append(item);
    endInsertRows();
}
