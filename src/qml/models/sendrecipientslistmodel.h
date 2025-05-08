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
    Q_PROPERTY(int recipientIndex READ recipientIndex NOTIFY recipientIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(SendRecipient* currentRecipient READ currentRecipient NOTIFY currentRecipientChanged)

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

    Q_INVOKABLE void add();
    Q_INVOKABLE void next();
    Q_INVOKABLE void prev();
    Q_INVOKABLE void clearAll();

    int currentIndex() const { return m_current; }
    void setCurrentIndex(int row);
    SendRecipient* currentRecipient() const;
    int count() const { return m_recipients.size(); }

Q_SIGNALS:
    void currentIndexChanged();
    void currentRecipientChanged();
    void countChanged();

private:
    QList<SendRecipient*> m_recipients;
    int m_current{0};
};

#endif // BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H
