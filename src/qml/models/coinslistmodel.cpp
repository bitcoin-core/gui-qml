// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/coinslistmodel.h>

#include <consensus/amount.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <primitives/transaction.h>
#include <qml/models/walletqmlmodel.h>
#include <qt/bitcoinunits.h>
#include <vector>

CoinsListModel::CoinsListModel(WalletQmlModel* parent)
    : QAbstractListModel(parent), m_wallet_model(parent), m_sort_by("amount"), m_total_amount(0)
{
    update();
}

CoinsListModel::~CoinsListModel() = default;

int CoinsListModel::rowCount(const QModelIndex& parent) const
{
    return m_coins.size();
}

QVariant CoinsListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_coins.size()))
        return QVariant();

    const auto& [destination, outpoint, coin] = m_coins.at(index.row());
    switch (role) {
    case AddressRole:
        return QString::fromStdString(EncodeDestination(destination));
    case AmountRole:
        return BitcoinUnits::format(BitcoinUnits::Unit::BTC, coin.txout.nValue);
    case LabelRole:
        return QString::fromStdString("");
    case LockedRole:
        return m_wallet_model->isLockedCoin(outpoint);
    case SelectedRole:
        return m_wallet_model->isSelectedCoin(outpoint);
    default:
        return QVariant();
    }
}

QHash<int, QByteArray> CoinsListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[AmountRole] = "amount";
    roles[AddressRole] = "address";
    roles[DateTimeRole] = "date";
    roles[LabelRole] = "label";
    roles[LockedRole] = "locked";
    roles[SelectedRole] = "selected";
    return roles;
}

void CoinsListModel::update()
{
    if (m_wallet_model == nullptr) {
        return;
    }
    auto coins_map = m_wallet_model->listCoins();
    beginResetModel();
    m_coins.clear();
    for (const auto& [destination, vec] : coins_map) {
        for (const auto& [outpoint, tx_out] : vec) {
            auto tuple = std::make_tuple(destination, outpoint, tx_out);
            m_coins.push_back(tuple);
        }
    }
    endResetModel();
    Q_EMIT coinCountChanged();
}

void CoinsListModel::setSortBy(const QString& roleName)
{
    if (m_sort_by != roleName) {
        m_sort_by = roleName;
        // sort(RoleNameToIndex(roleName));
        Q_EMIT sortByChanged(roleName);
    }
}

void CoinsListModel::toggleCoinSelection(const int index)
{
    if (index < 0 || index >= static_cast<int>(m_coins.size())) {
        return;
    }
    const auto& [destination, outpoint, coin] = m_coins.at(index);

    if (m_wallet_model->isSelectedCoin(outpoint)) {
        m_wallet_model->unselectCoin(outpoint);
        m_total_amount -= coin.txout.nValue;
    } else {
        m_wallet_model->selectCoin(outpoint);
        m_total_amount += coin.txout.nValue;
    }
    Q_EMIT selectedCoinsCountChanged();
}

unsigned int CoinsListModel::lockedCoinsCount() const
{
    std::vector<COutPoint> lockedCoins;
    m_wallet_model->listLockedCoins(lockedCoins);
    return lockedCoins.size();
}

unsigned int CoinsListModel::selectedCoinsCount() const
{
    return m_wallet_model->listSelectedCoins().size();
}

QString CoinsListModel::totalSelected() const
{
    return BitcoinUnits::format(BitcoinUnits::Unit::BTC, m_total_amount);
}

QString CoinsListModel::changeAmount() const
{
    CAmount change = m_total_amount - m_wallet_model->sendRecipientList()->totalAmountSatoshi();
    change = std::abs(change);
    return BitcoinUnits::format(BitcoinUnits::Unit::BTC, change);
}

bool CoinsListModel::overRequiredAmount() const
{
    return m_total_amount > m_wallet_model->sendRecipientList()->totalAmountSatoshi();
}

int CoinsListModel::coinCount() const
{
    return m_coins.size();
}
