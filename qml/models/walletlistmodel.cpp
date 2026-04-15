// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletlistmodel.h>

#include <interfaces/node.h>

#include <QHash>

WalletListModel::WalletListModel(interfaces::Node& node, QObject *parent)
: QAbstractListModel(parent)
, m_node(node)
{
}

void WalletListModel::listWalletDir()
{
    QHash<QString, int> existing_rows;
    for (int i = 0; i < rowCount(); ++i) {
        QModelIndex index = this->index(i, 0);
        QString name = data(index, NameRole).toString();
        existing_rows.insert(name, i);
    }

    for (const auto& [path, format] : m_node.walletLoader().listWalletDir()) {
        QString qname = QString::fromStdString(path);
        QString qformat = QString::fromStdString(format);
        if (existing_rows.contains(qname)) {
            const int row = existing_rows.value(qname);
            if (m_items[row].format != qformat) {
                m_items[row].format = qformat;
                Q_EMIT dataChanged(index(row, 0), index(row, 0), {FormatRole});
            }
        } else {
            addItem({ qname, qformat });
        }
    }
}

void WalletListModel::setOpenWalletNames(const QStringList& wallet_names)
{
    const QSet<QString> updated_names{wallet_names.begin(), wallet_names.end()};
    if (m_open_wallet_names == updated_names) {
        return;
    }

    m_open_wallet_names = updated_names;
    updateLoadStateForAllRows();
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
    case FormatRole:
        return item.format;
    case LoadStateRole:
        return m_open_wallet_names.contains(item.name)
            ? static_cast<int>(LoadState::Open)
            : static_cast<int>(LoadState::Closed);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> WalletListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[FormatRole] = "format";
    roles[LoadStateRole] = "loadState";
    return roles;
}

void WalletListModel::addItem(const Item &item)
{
    beginInsertRows(QModelIndex(), rowCount(), rowCount());
    m_items.append(item);
    endInsertRows();
}

void WalletListModel::updateLoadStateForAllRows()
{
    if (m_items.isEmpty()) {
        return;
    }

    const QModelIndex first = index(0, 0);
    const QModelIndex last = index(rowCount() - 1, 0);
    Q_EMIT dataChanged(first, last, {LoadStateRole});
}
