// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>

SendRecipientsListModel::SendRecipientsListModel(QObject* parent)
    : QAbstractListModel(parent)
{
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
    case AddressRole: return r->address();
    case LabelRole: return r->label();
    case AmountRole: return r->amount();
    case MessageRole: return r->message();
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
    };
}

void SendRecipientsListModel::add()
{
    const int row = m_recipients.size();
    beginInsertRows(QModelIndex(), row, row);
    m_recipients.append(new SendRecipient(this));
    endInsertRows();
    Q_EMIT  countChanged();
    setCurrentIndex(row)
}
