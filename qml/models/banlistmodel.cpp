// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/banlistmodel.h>

#include <interfaces/node.h>
#include <net_types.h>

#include <QDateTime>
#include <QLocale>

BanListModel::BanListModel(interfaces::Node& node, QObject* parent, bool backend_ready)
    : QAbstractListModel(parent), m_node(node), m_backend_ready(backend_ready)
{
}

void BanListModel::backendInitialized()
{
    if (m_stopping || m_backend_ready) return;
    m_backend_ready = true;
    refresh();
}

int BanListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return m_ban_list.size();
}

QVariant BanListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_ban_list.size()) return {};
    const BanListEntry& entry = m_ban_list.at(index.row());
    switch (static_cast<BanRoles>(role)) {
    case BanRoles::AddressRole:
        return QString::fromStdString(entry.subnet.ToString());
    case BanRoles::BanUntilRole: {
        QDateTime dt = QDateTime::fromSecsSinceEpoch(entry.ban_entry.nBanUntil);
        return QLocale::system().toString(dt, QStringLiteral("MMMM d, yyyy h:mm AP"));
    }
    }
    return {};
}

QHash<int, QByteArray> BanListModel::roleNames() const
{
    return {
        {static_cast<int>(BanRoles::AddressRole), "address"},
        {static_cast<int>(BanRoles::BanUntilRole), "banUntil"},
    };
}

bool BanListModel::unbanAt(int row)
{
    if (m_stopping || !m_backend_ready || row < 0 || row >= m_ban_list.size()) return false;
    return m_node.unban(m_ban_list.at(row).subnet);
}

void BanListModel::refresh()
{
    if (m_stopping || !m_backend_ready) return;
    beginResetModel();
    banmap_t banMap;
    m_node.getBanned(banMap);
    m_ban_list.clear();
    m_ban_list.reserve(static_cast<int>(banMap.size()));
    for (const auto& [subnet, entry] : banMap) {
        m_ban_list.append({subnet, entry});
    }
    endResetModel();
    Q_EMIT countChanged();
}
