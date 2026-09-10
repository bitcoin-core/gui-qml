// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/walletlistmodel.h>

#include <interfaces/node.h>

#include <qml/bitcoinunits.h>

#include <QSettings>

#include <algorithm>

namespace {
QString WalletDisplayName(const QString& path)
{
    const QString trimmed_path = path.trimmed();
    if (trimmed_path.isEmpty()) {
        return {};
    }

    QSettings settings;
    const QString display_name = settings.value(QStringLiteral("walletDisplayNames/%1").arg(trimmed_path)).toString().trimmed();
    return display_name.isEmpty() ? trimmed_path : display_name;
}
} // namespace

WalletListModel::WalletListModel(interfaces::Node& node, QObject *parent)
: QAbstractListModel(parent)
, m_node(node)
{
}

void WalletListModel::listWalletDir()
{
    // Preserve per-row info (balance, keyScheme) that was pushed in before the
    // picker was first opened — it's not derivable from listWalletDir().
    QHash<QString, Item> previous_items;
    previous_items.reserve(m_items.size());
    for (const auto& item : m_items) {
        previous_items.insert(item.name, item);
    }

    QList<Item> updated_items;
    for (const auto& [path, format] : m_node.walletLoader().listWalletDir()) {
        const QString name = QString::fromStdString(path);
        Item item{name, QString::fromStdString(format), true, {}, 0};
        const auto previous = previous_items.constFind(name);
        if (previous != previous_items.constEnd()) {
            item.balance = previous->balance;
            item.keySchemeKind = previous->keySchemeKind;
        }
        updated_items.append(std::move(item));
    }

    for (const QString& wallet_name : m_open_wallet_names) {
        const bool has_wallet = std::any_of(updated_items.cbegin(), updated_items.cend(), [&](const Item& item) {
            return item.name == wallet_name;
        });
        if (!has_wallet) {
            Item item{wallet_name, QString(), false, {}, 0};
            const auto previous = previous_items.constFind(wallet_name);
            if (previous != previous_items.constEnd()) {
                item.balance = previous->balance;
                item.keySchemeKind = previous->keySchemeKind;
            }
            updated_items.append(std::move(item));
        }
    }

    sortItems(updated_items);
    applyUpdatedItems(std::move(updated_items));
    const bool wallet_dir_became_loaded{!m_wallet_dir_loaded};
    if (wallet_dir_became_loaded) {
        m_wallet_dir_loaded = true;
    }
    Q_EMIT walletListChanged(rowCount() > 0);
    if (wallet_dir_became_loaded) {
        Q_EMIT walletDirLoadedChanged();
    }
}

void WalletListModel::setWalletLoadState(const QString& name, LoadState state, const QString& error)
{
    if (name.isEmpty()) {
        return;
    }

    switch (state) {
    case LoadState::Loading: {
        bool changed = false;
        if (!m_load_error.first.isEmpty()) {
            m_load_error = {};
            changed = true;
        }
        if (m_loading_wallet != name) {
            m_loading_wallet = name;
            changed = true;
        }
        if (changed) {
            emitTransientStateChanged();
        }
        return;
    }
    case LoadState::LoadError: {
        bool changed = false;
        if (!m_loading_wallet.isEmpty()) {
            m_loading_wallet.clear();
            changed = true;
        }
        if (m_load_error.first != name || m_load_error.second != error) {
            m_load_error = {name, error};
            changed = true;
        }
        if (changed) {
            emitTransientStateChanged();
        }
        return;
    }
    case LoadState::Open:
    case LoadState::Closed:
        break;
    }

    const bool loaded = (state == LoadState::Open);
    const bool was_loaded = m_open_wallet_names.contains(name);
    if (loaded) {
        m_open_wallet_names.insert(name);
    } else {
        m_open_wallet_names.remove(name);
    }

    bool transient_changed = false;
    if (m_loading_wallet == name) {
        m_loading_wallet.clear();
        transient_changed = true;
    }
    if (m_load_error.first == name) {
        m_load_error = {};
        transient_changed = true;
    }

    QList<Item> updated_items{m_items};
    if (loaded && m_wallet_dir_loaded && rowForName(name) == -1) {
        updated_items.append({name, QString(), false, {}, 0});
    } else if (!loaded) {
        updated_items.erase(std::remove_if(updated_items.begin(), updated_items.end(), [&](const Item& item) {
            return item.name == name && !item.from_wallet_dir;
        }), updated_items.end());
    }

    const bool wallet_count_changed{updated_items.size() != m_items.size()};
    sortItems(updated_items);
    if (applyUpdatedItems(std::move(updated_items))) {
        if (wallet_count_changed) {
            Q_EMIT walletListChanged(rowCount() > 0);
        }
        return;
    }

    if (was_loaded != loaded) {
        updateLoadStateForAllRows();
    } else if (transient_changed) {
        emitTransientStateChanged();
    }
}

void WalletListModel::setDisplayUnit(int unit)
{
    if (m_display_unit == unit) return;
    m_display_unit = unit;
    if (!m_items.isEmpty()) {
        Q_EMIT dataChanged(index(0, 0), index(rowCount() - 1, 0), {BalanceRole});
    }
}

void WalletListModel::setWalletInfo(const QString& name, qint64 balance, int keySchemeKind)
{
    if (name.isEmpty()) {
        return;
    }
    for (int row = 0; row < m_items.size(); ++row) {
        auto& item = m_items[row];
        if (item.name != name) {
            continue;
        }
        bool changed = false;
        if (item.balance != balance) {
            item.balance = balance;
            changed = true;
        }
        if (item.keySchemeKind != keySchemeKind) {
            item.keySchemeKind = keySchemeKind;
            changed = true;
        }
        if (changed) {
            const QModelIndex idx = index(row, 0);
            Q_EMIT dataChanged(idx, idx, {BalanceRole, KeySchemeKindRole});
        }
        return;
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
    case DisplayNameRole:
        return WalletDisplayName(item.name);
    case NameRole:
        return item.name;
    case FormatRole:
        return item.format;
    case LoadStateRole:
        if (m_load_error.first == item.name) {
            return static_cast<int>(LoadState::LoadError);
        }
        if (m_loading_wallet == item.name) {
            return static_cast<int>(LoadState::Loading);
        }
        return m_open_wallet_names.contains(item.name)
            ? static_cast<int>(LoadState::Open)
            : static_cast<int>(LoadState::Closed);
    case ErrorMessageRole:
        return (m_load_error.first == item.name) ? m_load_error.second : QString();
    case BalanceRole:
        return item.balance ? QmlBitcoinUnits::formatForDisplay(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), *item.balance) : QString{};
    case KeySchemeKindRole:
        return item.keySchemeKind;
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> WalletListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[NameRole] = "name";
    roles[FormatRole] = "format";
    roles[DisplayNameRole] = "displayName";
    roles[LoadStateRole] = "loadState";
    roles[ErrorMessageRole] = "errorMessage";
    roles[BalanceRole] = "balance";
    roles[KeySchemeKindRole] = "keySchemeKind";
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
    // Compare only structural fields (name/format/from_wallet_dir). Balance and
    // keyScheme updates go through setWalletInfo() and emit dataChanged in
    // place, so they don't justify a model reset.
    bool unchanged{m_items.size() == updated_items.size()};
    for (qsizetype i = 0; unchanged && i < m_items.size(); ++i) {
        unchanged = m_items[i].name == updated_items[i].name &&
            m_items[i].format == updated_items[i].format &&
            m_items[i].from_wallet_dir == updated_items[i].from_wallet_dir;
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
    Q_EMIT dataChanged(first, last, {LoadStateRole, ErrorMessageRole});
}

void WalletListModel::emitTransientStateChanged()
{
    updateLoadStateForAllRows();
}

int WalletListModel::rowForName(const QString& name) const
{
    for (int row = 0; row < m_items.size(); ++row) {
        if (m_items[row].name == name) {
            return row;
        }
    }
    return -1;
}

void WalletListModel::refreshDisplayNames()
{
    if (m_items.isEmpty()) {
        return;
    }

    const QModelIndex first = index(0, 0);
    const QModelIndex last = index(rowCount() - 1, 0);
    Q_EMIT dataChanged(first, last, {Qt::DisplayRole, DisplayNameRole});
}
