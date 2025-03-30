// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H
#define BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H

#include <qml/models/sendrecipient.h>

#include <QAbstractListModel>
#include <QList>

class SendRecipientsListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    enum Roles {
        AddressRole = Qt::UserRole + 1,
        LabelRole,
        AmountRole,
        MessageRole
    };

    explicit SendRecipientsListModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void addRecipient();

private:
    QList<SendRecipient*> m_recipients;
};

#endif // BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H
