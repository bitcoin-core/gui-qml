// Copyright (c) 2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H
#define BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H

#include <qml/models/sendrecipient.h>

#include <QAbstractListModel>
#include <QList>
#include <qobjectdefs.h>

class SendRecipientsListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(SendRecipient* current READ currentRecipient NOTIFY currentRecipientChanged)
    Q_PROPERTY(QString totalAmount READ totalAmount NOTIFY totalAmountChanged)

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
    Q_INVOKABLE void remove();

    int currentIndex() const { return m_current + 1; }
    void setCurrentIndex(int row);
    SendRecipient* currentRecipient() const;
    int count() const { return m_recipients.size(); }
    QList<SendRecipient*> recipients() const { return m_recipients; }
    QString totalAmount() const;
    qint64 totalAmountSatoshi() const { return m_totalAmount; }

Q_SIGNALS:
    void currentIndexChanged();
    void currentRecipientChanged();
    void countChanged();
    void totalAmountChanged();

private:
    void updateTotalAmount();

    QList<SendRecipient*> m_recipients;
    int m_current{0};
    qint64 m_totalAmount{0};
};

#endif // BITCOIN_QML_MODELS_SENDRECIPIENTSLISTMODEL_H
