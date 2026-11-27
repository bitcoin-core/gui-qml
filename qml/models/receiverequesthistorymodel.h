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
#include <memory>

#include <QAbstractListModel>
#include <QHash>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

namespace walletrequests { struct Snapshot; struct Record; }

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

    int count() const;

    // The production owner prepares order, indices and display roles on Domain.
    // Return the replaced reference so the caller can retire it off GUI.
    std::shared_ptr<const walletrequests::Snapshot> setSnapshot(std::shared_ptr<const walletrequests::Snapshot> snapshot);
    std::shared_ptr<const walletrequests::Snapshot> snapshot() const { return m_snapshot; }
    std::shared_ptr<const walletrequests::Snapshot> releaseSnapshot(); // silent close handoff
    const walletrequests::Record* preparedEntry(const QString& request_id) const;
    Q_INVOKABLE QString firstRequestIdForAddress(const QString& address) const;

    void setEntries(std::vector<QmlRecentRequestEntry>&& entries);
    void prependOrReplace(const QmlRecentRequestEntry& entry);
    bool removeByRequestId(const QString& request_id);
    Q_INVOKABLE QVariantList matchingEntriesForAddress(const QString& address) const;

    std::optional<QmlRecentRequestEntry> entryById(const QString& request_id) const;
    int64_t maxId() const;

    static std::vector<QmlRecentRequestEntry> DeserializeEntries(const std::vector<std::string>& blobs);
    static std::string SerializeEntry(const QmlRecentRequestEntry& entry);
    static QString BuildBitcoinUri(const QString& address, CAmount amount_sats, const QString& label, const QString& message);
    static QString FormatAmountBtc(CAmount amount_sats);

Q_SIGNALS:
    void countChanged();

private:
    void rebuildIndex();
    QHash<int64_t, int> m_row_by_id;
    QHash<QString, std::vector<int64_t>> m_ids_by_address;
    int indexOfId(int64_t id) const;
    QVariant dataForEntry(const QmlRecentRequestEntry& entry, int role) const;
    QVariantMap entryMap(const QmlRecentRequestEntry& entry) const;
    QVariant dataForRecord(const walletrequests::Record& record, int role) const;
    QVariantMap recordMap(const walletrequests::Record& record) const;
    std::shared_ptr<const walletrequests::Snapshot> m_snapshot;

    std::vector<QmlRecentRequestEntry> m_entries;
};

#endif // BITCOIN_QML_MODELS_RECEIVEREQUESTHISTORYMODEL_H
