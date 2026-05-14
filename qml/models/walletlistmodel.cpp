// Copyright (c) 2024-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletlistmodel.h>

#include <interfaces/node.h>

#include <QHash>

#include <algorithm>

WalletListModel::WalletListModel(interfaces::Node& node, QObject *parent)
: QAbstractListModel(parent)
, m_node(node)
{
}

void WalletListModel::listWalletDir()
{
    QList<Item> updated_items;
    for (const auto& [path, format] : m_node.walletLoader().listWalletDir()) {
        updated_items.append({
            QString::fromStdString(path),
            QString::fromStdString(format),
        });
    }

    sortItems(updated_items);
    applyUpdatedItems(std::move(updated_items));
}

void WalletListModel::setOpenWalletNames(const QStringList& wallet_names)
{
    const QSet<QString> updated_names{wallet_names.begin(), wallet_names.end()};
    if (m_open_wallet_names == updated_names) {
        return;
    }

    m_open_wallet_names = updated_names;

    QList<Item> updated_items{m_items};
    sortItems(updated_items);
    if (applyUpdatedItems(std::move(updated_items))) {
        return;
    }

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

bool WalletListModel::itemLess(const Item& a, const Item& b) const
{
    const bool a_open{m_open_wallet_names.contains(a.name)};
    const bool b_open{m_open_wallet_names.contains(b.name)};
    if (a_open != b_open) return a_open;

    const int name_compare = QString::compare(a.name, b.name, Qt::CaseInsensitive);
    if (name_compare != 0) return name_compare < 0;

    const int case_compare = QString::compare(a.name, b.name, Qt::CaseSensitive);
    if (case_compare != 0) return case_compare < 0;

    const int format_compare = QString::compare(a.format, b.format, Qt::CaseInsensitive);
    if (format_compare != 0) return format_compare < 0;

    return QString::compare(a.format, b.format, Qt::CaseSensitive) < 0;
}

void WalletListModel::sortItems(QList<Item>& items) const
{
    std::stable_sort(items.begin(), items.end(), [this](const Item& a, const Item& b) {
        return itemLess(a, b);
    });
}

bool WalletListModel::applyUpdatedItems(QList<Item>&& updated_items)
{
    bool unchanged{m_items.size() == updated_items.size()};
    for (qsizetype i = 0; unchanged && i < m_items.size(); ++i) {
        unchanged = m_items[i].name == updated_items[i].name && m_items[i].format == updated_items[i].format;
    }
    if (unchanged) {
        return false;
    }

    beginResetModel();
    m_items = std::move(updated_items);
    endResetModel();
    return true;
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
