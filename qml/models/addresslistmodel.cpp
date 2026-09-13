// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/addresslistmodel.h>

#include <addresstype.h>
#include <interfaces/wallet.h>
#include <key_io.h>
#include <qml/bitcoinunits.h>
#include <qml/models/bitcoinaddress.h>
#include <qml/models/walletqmlmodel.h>
#include <wallet/types.h>

#include <QVariantMap>

using wallet::AddressPurpose;

namespace {
QString CategoryName(AddressListModel::Category category)
{
    switch (category) {
    case AddressListModel::SingleUse: return QStringLiteral("single-use");
    case AddressListModel::Change: return QStringLiteral("change");
    }
    return {};
}

QString ScriptTypeName(const CTxDestination& destination)
{
    if (std::get_if<PKHash>(&destination)) return QStringLiteral("P2PKH");
    if (std::get_if<ScriptHash>(&destination)) return QStringLiteral("P2SH");
    if (std::get_if<WitnessV0KeyHash>(&destination)) return QStringLiteral("P2WPKH");
    if (std::get_if<WitnessV0ScriptHash>(&destination)) return QStringLiteral("P2WSH");
    if (std::get_if<WitnessV1Taproot>(&destination)) return QStringLiteral("P2TR");
    if (std::get_if<PayToAnchor>(&destination)) return QStringLiteral("P2A");
    if (std::get_if<WitnessUnknown>(&destination)) return QStringLiteral("Witness");
    if (std::get_if<PubKeyDestination>(&destination)) return QStringLiteral("P2PK");
    return {};
}

QString DisplayAmount(CAmount amount)
{
    QString value{QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, amount)};
    while (value.endsWith(QLatin1Char('0'))) value.chop(1);
    if (value.endsWith(QLatin1Char('.'))) value.chop(1);
    return QStringLiteral("₿ ") + value;
}
} // namespace

AddressListModel::AddressListModel(WalletQmlModel* parent)
    : QAbstractListModel(parent)
    , m_wallet_model(parent)
{
    if (m_wallet_model) {
        connect(m_wallet_model, &WalletQmlModel::addressListChanged, this, &AddressListModel::refresh);
        connect(m_wallet_model, &WalletQmlModel::balanceChanged, this, &AddressListModel::refresh);
    }
    refresh();
}

int AddressListModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_entries.size());
}

int AddressListModel::count() const
{
    return static_cast<int>(m_entries.size());
}

QVariant AddressListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }

    const AddressEntry& entry{m_entries.at(index.row())};
    switch (role) {
    case AddressRole:
        return entry.address;
    case FormattedAddressRole:
        return BitcoinAddress::formattedAddress(entry.address);
    case EllipsesAddressRole:
        return BitcoinAddress::ellipsesAddress(entry.address);
    case LabelRole:
        return entry.label;
    case CategoryRole:
        return CategoryName(entry.category);
    case UsedRole:
        return entry.used;
    case CurrentBalanceRole:
        return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, entry.current_balance);
    case DisplayAmountRole:
        return DisplayAmount(entry.current_balance);
    case HasAmountRole:
        return entry.current_balance != 0;
    case ScriptTypeRole:
        return entry.script_type;
    case CanEditLabelRole:
        return entry.can_edit_label;
    case CanCreatePaymentRequestRole:
        return entry.can_create_payment_request;
    }
    return {};
}

QHash<int, QByteArray> AddressListModel::roleNames() const
{
    QHash<int, QByteArray> roles;
    roles[AddressRole] = "address";
    roles[FormattedAddressRole] = "formattedAddress";
    roles[EllipsesAddressRole] = "ellipsesAddress";
    roles[LabelRole] = "label";
    roles[CategoryRole] = "category";
    roles[UsedRole] = "isUsed";
    roles[CurrentBalanceRole] = "currentBalance";
    roles[DisplayAmountRole] = "displayAmount";
    roles[HasAmountRole] = "hasAmount";
    roles[ScriptTypeRole] = "scriptType";
    roles[CanEditLabelRole] = "canEditLabel";
    roles[CanCreatePaymentRequestRole] = "canCreatePaymentRequest";
    return roles;
}

AddressListModel::Category AddressListModel::category() const
{
    return m_category;
}

void AddressListModel::setCategory(Category category)
{
    if (m_category == category) return;
    m_category = category;
    Q_EMIT categoryChanged();
    rebuild();
}

QVariantList AddressListModel::categoryOptions() const
{
    return {
        QVariantMap{{QStringLiteral("value"), static_cast<int>(SingleUse)}, {QStringLiteral("text"), tr("Single-use")}},
        QVariantMap{{QStringLiteral("value"), static_cast<int>(Change)}, {QStringLiteral("text"), tr("Change")}},
    };
}

bool AddressListModel::showUsed() const
{
    return m_show_used;
}

void AddressListModel::setShowUsed(bool show_used)
{
    if (m_show_used == show_used) return;
    m_show_used = show_used;
    Q_EMIT showUsedChanged();
    rebuild();
}

void AddressListModel::refresh()
{
    rebuild();
}

bool AddressListModel::setAddressLabel(const QString& address, const QString& label)
{
    if (!m_wallet_model || address.isEmpty()) return false;
    if (!m_wallet_model->setAddressLabel(address, label)) return false;
    refresh();
    return true;
}

QString AddressListModel::addressAt(int row) const
{
    if (row < 0 || row >= static_cast<int>(m_entries.size())) return {};
    return m_entries.at(row).address;
}

void AddressListModel::rebuild()
{
    beginResetModel();
    m_entries = collectEntries();
    endResetModel();
    Q_EMIT countChanged();
}

std::vector<AddressListModel::AddressEntry> AddressListModel::collectEntries() const
{
    std::vector<AddressEntry> entries;
    if (!m_wallet_model) return entries;

    const auto balances{m_wallet_model->addressBalances()};
    const auto used_addresses{m_wallet_model->usedAddresses()};

    if (m_category == Change) {
        const auto change_addresses{m_wallet_model->changeAddresses()};
        entries.reserve(change_addresses.size());
        for (const QString& address : change_addresses) {
            AddressEntry entry;
            entry.address = address;
            entry.label = m_wallet_model->getAddressLabel(address);
            entry.category = Change;
            entry.used = used_addresses.count(address) > 0;
            entry.script_type = ScriptTypeName(DecodeDestination(address.toStdString()));
            const auto balance_it{balances.find(address)};
            entry.current_balance = balance_it == balances.end() ? 0 : balance_it->second;
            entry.can_edit_label = false;
            entry.can_create_payment_request = false;
            entries.push_back(entry);
        }
        return entries;
    }

    for (const interfaces::WalletAddress& wallet_address : m_wallet_model->getAddresses()) {
        if (wallet_address.purpose != AddressPurpose::RECEIVE || !wallet_address.is_mine) {
            continue;
        }

        const QString address{QString::fromStdString(EncodeDestination(wallet_address.dest))};
        if (address.isEmpty()) continue;

        const bool used{used_addresses.count(address) > 0};
        if (used && !m_show_used) {
            continue;
        }

        AddressEntry entry;
        entry.address = address;
        entry.label = QString::fromStdString(wallet_address.name);
        entry.category = SingleUse;
        entry.used = used;
        entry.script_type = ScriptTypeName(wallet_address.dest);
        const auto balance_it{balances.find(address)};
        entry.current_balance = balance_it == balances.end() ? 0 : balance_it->second;
        entry.can_edit_label = true;
        entry.can_create_payment_request = true;
        entries.push_back(entry);
    }

    return entries;
}
