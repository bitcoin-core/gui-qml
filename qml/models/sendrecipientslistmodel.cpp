// Copyright (c) 2025-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodel.h>

#include <key_io.h>
#include <qml/models/sendrecipient.h>

#include <set>

SendRecipientsListModel::SendRecipientsListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_wallet = qobject_cast<WalletQmlModel*>(parent);
    auto* recipient = new SendRecipient(m_wallet, this);
    connectRecipientSignals(recipient);
    connect(recipient->amount(), &BitcoinAmount::amountChanged,
            this, &SendRecipientsListModel::updateTotalAmount);
    m_recipients.append(recipient);
}

int SendRecipientsListModel::rowCount(const QModelIndex&) const
{
    return m_recipients.size();
}

QVariant SendRecipientsListModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() >= m_recipients.size())
        return {};

    const auto& r = m_recipients[index.row()];
    switch (role) {
    case AddressRole: return r->address()->ellipsesAddress();
    case LabelRole: return r->label();
    case AmountRole: return r->amount()->toDisplay();
    case MessageRole: return r->message();
    case FormattedAddressRole: return r->address()->formattedAddress();
    case AmountUnitLabelRole: return r->amount()->unitLabel();
    case IsDataOutputRole: return r->isDataOutput();
    case DataHexRole: return r->dataHex();
    default: return {};
    }
    return {};
}

QHash<int, QByteArray> SendRecipientsListModel::roleNames() const
{
    return {
        {AddressRole, "address"},
        {LabelRole, "label"},
        {AmountRole, "amount"},
        {MessageRole, "message"},
        {FormattedAddressRole, "formattedAddress"},
        {AmountUnitLabelRole, "amountUnitLabel"},
        {IsDataOutputRole, "isDataOutput"},
        {DataHexRole, "dataHex"},
    };
}

void SendRecipientsListModel::add()
{
    const int row = m_recipients.size();
    beginInsertRows(QModelIndex(), row, row);
    auto* recipient = new SendRecipient(m_wallet, this);
    connectRecipientSignals(recipient);
    connect(recipient->amount(), &BitcoinAmount::amountChanged,
            this, &SendRecipientsListModel::updateTotalAmount);
    if (m_recipients.size() > 0) {
        recipient->amount()->setUnit(m_recipients[m_current]->amount()->unit());
    }
    m_recipients.append(recipient);

    endInsertRows();
    Q_EMIT countChanged();
    Q_EMIT validationChanged();
    setCurrentIndex(row);
}

void SendRecipientsListModel::setCurrentIndex(int row)
{
    if (row < 0 || row >= m_recipients.size())
        return;

    if (row == m_current)
        return;

    m_current = row;

    Q_EMIT currentIndexChanged();
    Q_EMIT currentRecipientChanged();
}

void SendRecipientsListModel::next()
{
    setCurrentIndex(m_current + 1);
}

void SendRecipientsListModel::prev()
{
    setCurrentIndex(m_current - 1);
}

void SendRecipientsListModel::remove()
{
    if (m_recipients.size() == 1) {
        return;
    }
    beginRemoveRows(QModelIndex(), m_current, m_current);
    if (m_current > 0) {
        int index_to_remove = m_current;
        setCurrentIndex(m_current - 1);
        delete m_recipients.takeAt(index_to_remove);
    } else {
        auto removed_recipient = m_recipients.takeAt(m_current);
        Q_EMIT currentRecipientChanged();
        delete removed_recipient;
    }
    endRemoveRows();
    updateTotalAmount();
    Q_EMIT countChanged();
    Q_EMIT validationChanged();
}

SendRecipient* SendRecipientsListModel::currentRecipient() const
{
    if (m_current < 0 || m_current >= m_recipients.size())
        return nullptr;

    return m_recipients[m_current];
}

void SendRecipientsListModel::connectRecipientSignals(SendRecipient* recipient)
{
    const auto emit_roles_changed = [this, recipient](const QList<int>& roles) {
        const int row = recipientRow(recipient);
        if (row < 0) {
            return;
        }

        const QModelIndex model_index = index(row, 0);
        Q_EMIT dataChanged(model_index, model_index, roles);
    };

    connect(recipient, &SendRecipient::labelChanged, this, [emit_roles_changed] {
        emit_roles_changed({LabelRole});
    });
    connect(recipient, &SendRecipient::messageChanged, this, [emit_roles_changed] {
        emit_roles_changed({MessageRole});
    });
    connect(recipient, &SendRecipient::isDataOutputChanged, this, [emit_roles_changed] {
        emit_roles_changed({IsDataOutputRole});
    });
    connect(recipient, &SendRecipient::dataHexChanged, this, [emit_roles_changed] {
        emit_roles_changed({DataHexRole});
    });
    connect(recipient->address(), &BitcoinAddress::formattedAddressChanged, this, [emit_roles_changed] {
        emit_roles_changed({AddressRole, FormattedAddressRole});
    });
    connect(recipient->address(), &BitcoinAddress::ellipsesAddressChanged, this, [emit_roles_changed] {
        emit_roles_changed({AddressRole, FormattedAddressRole});
    });
    connect(recipient->amount(), &BitcoinAmount::amountChanged, this, [emit_roles_changed] {
        emit_roles_changed({AmountRole});
    });
    connect(recipient->amount(), &BitcoinAmount::unitChanged, this, [emit_roles_changed] {
        emit_roles_changed({AmountRole, AmountUnitLabelRole});
    });
    connect(recipient, &SendRecipient::subtractFeeFromAmountChanged,
            this, &SendRecipientsListModel::subtractFeeFromAmountChanged);
    connect(recipient, &SendRecipient::isValidChanged, this, &SendRecipientsListModel::validationChanged);
}

int SendRecipientsListModel::recipientRow(const SendRecipient* recipient) const
{
    return m_recipients.indexOf(const_cast<SendRecipient*>(recipient));
}

void SendRecipientsListModel::updateTotalAmount()
{
    qint64 total = 0;
    for (const auto& recipient : m_recipients) {
        total += recipient->amount()->satoshi();
    }
    if (m_totalAmount == total) {
        return;
    }
    m_totalAmount = total;
    Q_EMIT totalAmountChanged();
}

QString SendRecipientsListModel::totalAmount() const
{
    return BitcoinAmount::satsToBtcString(m_totalAmount);
}

void SendRecipientsListModel::clear()
{
    beginResetModel();
    const QList<SendRecipient*> old_recipients{m_recipients};
    m_recipients.clear();
    m_current = 0;
    m_totalAmount = 0;

    auto* recipient = new SendRecipient(m_wallet, this);
    connectRecipientSignals(recipient);
    connect(recipient->amount(), &BitcoinAmount::amountChanged,
            this, &SendRecipientsListModel::updateTotalAmount);
    m_recipients.append(recipient);
    endResetModel();

    Q_EMIT countChanged();
    Q_EMIT totalAmountChanged();
    Q_EMIT currentRecipientChanged();
    Q_EMIT currentIndexChanged();
    Q_EMIT validationChanged();

    // Defer deletion so QML bindings can switch to the replacement recipient
    // without temporarily observing a null object.
    for (auto* recipient : old_recipients) {
        recipient->deleteLater();
    }

    if (m_wallet) {
        m_wallet->clearSelectedCoins();
    }
    Q_EMIT listCleared();
}

void SendRecipientsListModel::clearToFront()
{
    if (m_current != 0) {
        m_current = 0;
        Q_EMIT currentRecipientChanged();
        Q_EMIT currentIndexChanged();
    }

    bool count_changed = false;
    while (m_recipients.size() > 1) {
        delete m_recipients.at(1);
        m_recipients.removeAt(1);
        count_changed = true;
    }

    if (count_changed) {
        Q_EMIT countChanged();
        Q_EMIT validationChanged();
    }

    if (m_totalAmount != m_recipients[0]->amount()->satoshi()) {
        m_totalAmount = m_recipients[0]->amount()->satoshi();
        Q_EMIT totalAmountChanged();
    }
}

bool SendRecipientsListModel::allValid() const
{
    if (m_recipients.empty()) {
        return false;
    }

    std::set<CTxDestination> destinations;
    for (const auto* recipient : m_recipients) {
        if (recipient == nullptr || !recipient->isValid()) {
            return false;
        }

        const CTxDestination destination{DecodeDestination(recipient->address()->address().toStdString())};
        if (!destinations.insert(destination).second) {
            return false;
        }
    }

    return true;
}

QString SendRecipientsListModel::validationError() const
{
    if (m_recipients.empty()) {
        return tr("Enter at least one valid recipient to continue.");
    }

    std::set<CTxDestination> destinations;
    for (const auto* recipient : m_recipients) {
        if (recipient == nullptr || !recipient->isValid()) {
            return m_recipients.size() > 1 ? tr("Complete every recipient before continuing.") : QString{};
        }

        const CTxDestination destination{DecodeDestination(recipient->address()->address().toStdString())};
        if (!destinations.insert(destination).second) {
            return tr("Recipient addresses must be unique.");
        }
    }

    return {};
}
