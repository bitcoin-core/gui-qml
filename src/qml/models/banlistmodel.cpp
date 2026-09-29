// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/banlistmodel.h>

#include <interfaces/node.h>
#include <net_types.h>

#include <QDateTime>
#include <QLocale>

BanListModel::BanListModel(interfaces::Node& node, QObject* parent)
    : QAbstractListModel(parent), m_node(node)
{
    m_ban_changed_handler = node.handleBannedListChanged([this] {
        QMetaObject::invokeMethod(this, &BanListModel::refresh, Qt::QueuedConnection);
    });
}

BanListModel::~BanListModel()
{
    stop();
}

void BanListModel::stop()
{
    m_stopped = true;
    m_ban_changed_handler.reset();
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
    if (m_stopped || row < 0 || row >= m_ban_list.size()) return false;
    const bool unbanned{m_node.unban(m_ban_list.at(row).subnet)};
    if (unbanned) refresh();
    return unbanned;
}

void BanListModel::refresh()
{
    if (m_stopped) return;
    banmap_t banMap;
    if (!m_node.getBanned(banMap)) return;
    beginResetModel();
    m_ban_list.clear();
    m_ban_list.reserve(static_cast<int>(banMap.size()));
    for (const auto& [subnet, entry] : banMap) {
        m_ban_list.append({subnet, entry});
    }
    endResetModel();
    Q_EMIT countChanged();
}
