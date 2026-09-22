// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/coinslistmodel.h>
#include <qml/bitcoinamount.h>
#include <qml/models/walletqmlmodel.h>
#include <key_io.h>
#include <script/solver.h>
#include <algorithm>
#include <QLocale>

CoinsListModel::CoinsListModel(WalletQmlModel* parent)
    : QAbstractListModel(parent), m_wallet_model(parent)
{
    if (parent) {
        connect(parent, &WalletQmlModel::transactionChanged, this, &CoinsListModel::update);
        connect(parent, &WalletQmlModel::addressListChanged, this, &CoinsListModel::update);
        connect(parent, &WalletQmlModel::balanceChanged, this, &CoinsListModel::update);
    }
    update();
}

QString CoinsListModel::Coin::id() const
{
    return QString::fromStdString(outpoint.hash.GetHex()) + ":" + QString::number(outpoint.n);
}
int CoinsListModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : static_cast<int>(m_visible.size()); }
QString CoinsListModel::groupTitle(const Coin& coin) const
{
    if (m_group == "address") return coin.address;
    return QLocale().toString(coin.date, m_group == "month" ? "MMMM yyyy" : "MMM d, yyyy");
}
QVariant CoinsListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= rowCount()) return {};
    const auto& coin = m_coins[m_visible[index.row()]];
    switch (role) {
    case AddressRole: return coin.address;
    case AmountRole: return BitcoinAmount::satsToBtcString(coin.amount);
    case AmountSatoshiRole: return static_cast<qint64>(coin.amount);
    case DateTimeRole: return coin.date;
    case LabelRole: return coin.note;
    case LockedRole: return coin.locked;
    case SelectedRole: return m_wallet_model && m_wallet_model->isSelectedCoin(coin.outpoint);
    case CoinIdRole: return coin.id();
    case GroupRole: return groupTitle(coin);
    case GroupFirstRole:
        return index.row() == 0 || groupTitle(m_coins[m_visible[index.row() - 1]]) != groupTitle(coin);
    case GroupLastRole:
        return index.row() == rowCount() - 1 || groupTitle(m_coins[m_visible[index.row() + 1]]) != groupTitle(coin);
    default: return {};
    }
}
QHash<int, QByteArray> CoinsListModel::roleNames() const
{
    return {{AddressRole,"address"}, {AmountRole,"amount"}, {DateTimeRole,"date"},
            {LabelRole,"label"}, {LockedRole,"locked"}, {SelectedRole,"selected"},
            {AmountSatoshiRole,"amountSatoshi"}, {CoinIdRole,"coinId"}, {GroupRole,"groupTitle"},
            {GroupFirstRole,"groupFirst"}, {GroupLastRole,"groupLast"}};
}
void CoinsListModel::update()
{
    if (!m_wallet_model) return;
    std::vector<Coin> coins;
    for (const auto& [ancestor, outputs] : m_wallet_model->listCoins()) {
        for (const auto& [outpoint, output] : outputs) {
            if (output.is_spent) continue;
            // listCoins groups change under its ancestor. Show the actual output address.
            CTxDestination destination;
            if (!ExtractDestination(output.txout.scriptPubKey, destination)) destination = ancestor;
            const auto address = QString::fromStdString(EncodeDestination(destination));
            coins.push_back({outpoint, address, m_wallet_model->getAddressLabel(address), output.txout.nValue,
                             QDateTime::fromSecsSinceEpoch(output.time), m_wallet_model->isLockedCoin(outpoint)});
        }
    }
    // Remove spent inputs without changing surviving manual selections. Locked
    // coins remain valid when the user explicitly selects them.
    for (const auto& selected : m_wallet_model->listSelectedCoins()) {
        if (std::none_of(coins.begin(), coins.end(), [&](const auto& coin) { return coin.outpoint == selected; }))
            m_wallet_model->unselectCoin(selected);
    }
    beginResetModel();
    m_coins = std::move(coins);
    m_visible.clear();
    endResetModel();
    rebuildView();
    Q_EMIT coinCountChanged();
    Q_EMIT lockedCoinsCountChanged();
    Q_EMIT selectedCoinsCountChanged();
}
void CoinsListModel::rebuildView()
{
    beginResetModel();
    m_visible.clear();
    for (size_t i = 0; i < m_coins.size(); ++i) {
        const auto& coin = m_coins[i];
        if (!m_search.isEmpty() &&
            !coin.note.contains(m_search, Qt::CaseInsensitive) &&
            !coin.address.contains(m_search, Qt::CaseInsensitive) &&
            !coin.id().contains(m_search, Qt::CaseInsensitive)) continue;
        if (m_filter == "locked" && !coin.locked) continue;
        if (m_filter == "spendable" && coin.locked) continue;
        if (m_min_amount >= 0 && coin.amount < m_min_amount) continue;
        if (m_max_amount >= 0 && coin.amount > m_max_amount) continue;
        m_visible.push_back(i);
    }
    std::sort(m_visible.begin(), m_visible.end(), [&](size_t a, size_t b) {
        const auto& x = m_coins[a]; const auto& y = m_coins[b];
        if (m_group == "address" && x.address != y.address) return x.address < y.address;
        if (m_group == "month" || m_group == "date") {
            const auto format = m_group == "month" ? "yyyyMM" : "yyyyMMdd";
            const auto xm = x.date.toString(format), ym = y.date.toString(format);
            if (xm != ym) return m_descending ? xm > ym : xm < ym;
        }
        int cmp = 0;
        if (m_sort == "amount") cmp = x.amount < y.amount ? -1 : x.amount > y.amount ? 1 : 0;
        else if (m_sort == "label") cmp = QString::localeAwareCompare(x.note.toCaseFolded(), y.note.toCaseFolded());
        else cmp = x.date < y.date ? -1 : x.date > y.date ? 1 : 0;
        return cmp == 0 ? x.outpoint < y.outpoint : m_descending ? cmp > 0 : cmp < 0;
    });
    endResetModel();
    Q_EMIT viewChanged();
}
void CoinsListModel::setSearchText(const QString& v) { if (m_search != v) { m_search = v; rebuildView(); } }
void CoinsListModel::setSortBy(const QString& v) { if ((v == "date" || v == "amount" || v == "label") && m_sort != v) { m_sort = v; rebuildView(); } }
void CoinsListModel::setSortDescending(bool v) { if (m_descending != v) { m_descending = v; rebuildView(); } }
void CoinsListModel::setFilter(const QString& v) { if ((v == "all" || v == "spendable" || v == "locked") && m_filter != v) { m_filter = v; rebuildView(); } }
void CoinsListModel::setGroupBy(const QString& v) { if ((v == "date" || v == "month" || v == "address") && m_group != v) { m_group = v; rebuildView(); } }
void CoinsListModel::setMinAmount(qint64 v) { v = v < 0 ? -1 : v; if (m_min_amount != v) { m_min_amount = v; rebuildView(); } }
void CoinsListModel::setMaxAmount(qint64 v) { v = v < 0 ? -1 : v; if (m_max_amount != v) { m_max_amount = v; rebuildView(); } }
void CoinsListModel::toggleCoinSelection(int row)
{
    if (!m_wallet_model || row < 0 || row >= rowCount()) return;
    const auto& coin = m_coins[m_visible[row]];
    if (m_wallet_model->isSelectedCoin(coin.outpoint)) m_wallet_model->unselectCoin(coin.outpoint);
    else m_wallet_model->selectCoin(coin.outpoint);
    refreshSelection();
}
void CoinsListModel::refreshSelection()
{
    if (rowCount()) Q_EMIT dataChanged(index(0), index(rowCount() - 1), {SelectedRole});
    Q_EMIT selectedCoinsCountChanged();
}
void CoinsListModel::beginSelection()
{
    if (!m_wallet_model || m_previous_selection) return;
    update();
    m_previous_selection = m_wallet_model->listSelectedCoins();
}
void CoinsListModel::applySelection() { m_previous_selection.reset(); }
void CoinsListModel::cancelSelection()
{
    if (!m_wallet_model || !m_previous_selection) return;
    const auto previous = *m_previous_selection;
    m_previous_selection.reset();
    m_wallet_model->clearSelectedCoins();
    for (const auto& outpoint : previous) {
        if (std::any_of(m_coins.begin(), m_coins.end(), [&](const auto& c) { return c.outpoint == outpoint; }))
            m_wallet_model->selectCoin(outpoint);
    }
    refreshSelection();
}
bool CoinsListModel::setCoinsLocked(const QStringList& ids, bool locked)
{
    if (!m_wallet_model) return false;
    bool success = true;
    for (const auto& coin : m_coins) {
        if (!ids.contains(coin.id()) || coin.locked == locked) continue;
        const bool changed = locked ? m_wallet_model->lockCoin(coin.outpoint) : m_wallet_model->unlockCoin(coin.outpoint);
        success = changed && success;
        if (changed && locked) m_wallet_model->unselectCoin(coin.outpoint);
    }
    update();
    m_wallet_model->scheduleFeeEstimates();
    return success;
}
int CoinsListModel::lockedCoinsCount() const { return std::count_if(m_coins.begin(), m_coins.end(), [](const auto& c) { return c.locked; }); }
int CoinsListModel::selectedCoinsCount() const { return m_wallet_model ? static_cast<int>(m_wallet_model->listSelectedCoins().size()) : 0; }
qint64 CoinsListModel::totalSelectedSatoshi() const
{
    qint64 total = 0;
    if (m_wallet_model) for (const auto& c : m_coins) if (m_wallet_model->isSelectedCoin(c.outpoint)) total += c.amount;
    return total;
}
qint64 CoinsListModel::totalSatoshi() const { qint64 total = 0; for (const auto& c : m_coins) total += c.amount; return total; }
qint64 CoinsListModel::lockedSatoshi() const { qint64 total = 0; for (const auto& c : m_coins) if (c.locked) total += c.amount; return total; }
qint64 CoinsListModel::availableMinAmount() const
{
    if (m_coins.empty()) return 0;
    return std::min_element(m_coins.begin(), m_coins.end(), [](const auto& a, const auto& b) { return a.amount < b.amount; })->amount;
}
qint64 CoinsListModel::availableMaxAmount() const
{
    if (m_coins.empty()) return 0;
    return std::max_element(m_coins.begin(), m_coins.end(), [](const auto& a, const auto& b) { return a.amount < b.amount; })->amount;
}
QString CoinsListModel::totalSelected() const { return BitcoinAmount::satsToBtcString(totalSelectedSatoshi()); }
QString CoinsListModel::changeAmount() const { return BitcoinAmount::satsToBtcString(std::abs(totalSelectedSatoshi() - (m_wallet_model ? m_wallet_model->sendTotalSatoshi() : 0))); }
bool CoinsListModel::overRequiredAmount() const { return m_wallet_model && totalSelectedSatoshi() >= m_wallet_model->sendTotalSatoshi(); }
