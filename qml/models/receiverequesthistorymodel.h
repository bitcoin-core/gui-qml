// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RECEIVEREQUESTHISTORYMODEL_H
#define BITCOIN_QML_MODELS_RECEIVEREQUESTHISTORYMODEL_H

#include <qml/models/receiverequestentry.h>

#include <consensus/amount.h>

#include <optional>
#include <string>
#include <vector>

#include <QAbstractListModel>
#include <QString>

class ReceiveRequestHistoryModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    enum Role {
        IdRole = Qt::UserRole + 1,
        DateRole,
        DateIsoRole,
        AddressRole,
        AddressShortRole,
        LabelRole,
        MessageRole,
        NoteSelfRole,
        AmountSatRole,
        AmountDisplayRole,
        UriRole,
    };

    explicit ReceiveRequestHistoryModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    int count() const { return static_cast<int>(m_entries.size()); }

    void setEntries(std::vector<QmlRecentRequestEntry>&& entries);
    void prependOrReplace(const QmlRecentRequestEntry& entry);
    bool removeByRequestId(const QString& request_id);

    std::optional<QmlRecentRequestEntry> entryById(const QString& request_id) const;
    int64_t maxId() const;

    static std::vector<QmlRecentRequestEntry> DeserializeEntries(const std::vector<std::string>& blobs);
    static std::string SerializeEntry(const QmlRecentRequestEntry& entry);
    static QString BuildBitcoinUri(const QString& address, CAmount amount_sats, const QString& label, const QString& message);
    static QString FormatAmountBtc(CAmount amount_sats);

Q_SIGNALS:
    void countChanged();

private:
    int indexOfId(int64_t id) const;
    QVariant dataForEntry(const QmlRecentRequestEntry& entry, int role) const;

    std::vector<QmlRecentRequestEntry> m_entries;
};

#endif // BITCOIN_QML_MODELS_RECEIVEREQUESTHISTORYMODEL_H
