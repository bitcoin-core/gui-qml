// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/sendrecipientslistmodel.h>
#include <qobjectdefs.h>

#include <qml/models/sendrecipient.h>

SendRecipientsListModel::SendRecipientsListModel(QObject* parent)
    : QAbstractListModel(parent)
{
    m_recipients.append(new SendRecipient(this));
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
    case AmountRole: return r->amount()->toDisplay();
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
    Q_EMIT countChanged();
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
    delete m_recipients.takeAt(m_current);
    endRemoveRows();
    Q_EMIT countChanged();

    setCurrentIndex(m_current - 1);
}

SendRecipient* SendRecipientsListModel::currentRecipient() const
{
    if (m_current < 0 || m_current >= m_recipients.size())
        return nullptr;

    return m_recipients[m_current];
}
