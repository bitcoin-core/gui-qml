// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/receiverequesthistorymodel.h>

#include <qml/bitcoinunits.h>
#include <streams.h>

#include <algorithm>
#include <ios>

#include <QDebug>
#include <QUrl>

ReceiveRequestHistoryModel::ReceiveRequestHistoryModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ReceiveRequestHistoryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_entries.size());
}

QVariant ReceiveRequestHistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= static_cast<int>(m_entries.size())) {
        return {};
    }
    return dataForEntry(m_entries[index.row()], role);
}

QVariant ReceiveRequestHistoryModel::dataForEntry(const QmlRecentRequestEntry& entry, int role) const
{
    switch (role) {
    case IdRole:
        return QString::number(entry.id);
    case DateRole:
        return entry.date.toLocalTime().toString(Qt::TextDate);
    case DateIsoRole:
        return entry.date.toUTC().toString(Qt::ISODate);
    case AddressRole:
        return QString::fromStdString(entry.recipient.address);
    case AddressShortRole: {
        const QString addr = QString::fromStdString(entry.recipient.address);
        if (addr.size() <= 16) return addr;
        return QString(addr.left(8) + QStringLiteral("…") + addr.right(6));
    }
    case LabelRole:
        return QString::fromStdString(entry.recipient.label);
    case MessageRole:
        return QString::fromStdString(entry.recipient.message);
    case NoteSelfRole:
        return QString::fromStdString(entry.recipient.noteSelf);
    case AmountSatRole:
        return QVariant::fromValue<qlonglong>(entry.recipient.amount);
    case AmountDisplayRole:
        return FormatAmountBtc(entry.recipient.amount);
    case UriRole:
        return BuildBitcoinUri(QString::fromStdString(entry.recipient.address),
                               entry.recipient.amount,
                               QString::fromStdString(entry.recipient.label),
                               QString::fromStdString(entry.recipient.message));
    }
    return {};
}

QHash<int, QByteArray> ReceiveRequestHistoryModel::roleNames() const
{
    return {
        {IdRole, "requestId"},
        {DateRole, "date"},
        {DateIsoRole, "dateIso"},
        {AddressRole, "address"},
        {AddressShortRole, "addressShort"},
        {LabelRole, "label"},
        {MessageRole, "message"},
        {NoteSelfRole, "noteSelf"},
        {AmountSatRole, "amountSat"},
        {AmountDisplayRole, "amountDisplay"},
        {UriRole, "uri"},
    };
}

int ReceiveRequestHistoryModel::indexOfId(int64_t id) const
{
    for (size_t i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].id == id) return static_cast<int>(i);
    }
    return -1;
}

void ReceiveRequestHistoryModel::setEntries(std::vector<QmlRecentRequestEntry>&& entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    std::sort(m_entries.begin(), m_entries.end(),
              [](const QmlRecentRequestEntry& a, const QmlRecentRequestEntry& b) {
                  return a.date > b.date;
              });
    endResetModel();
    Q_EMIT countChanged();
}

void ReceiveRequestHistoryModel::prependOrReplace(const QmlRecentRequestEntry& entry)
{
    const int existing = indexOfId(entry.id);
    if (existing >= 0) {
        m_entries[existing] = entry;
        const QModelIndex idx = index(existing);
        Q_EMIT dataChanged(idx, idx);
        return;
    }
    beginInsertRows(QModelIndex(), 0, 0);
    m_entries.insert(m_entries.begin(), entry);
    endInsertRows();
    Q_EMIT countChanged();
}

bool ReceiveRequestHistoryModel::removeByRequestId(const QString& request_id)
{
    bool ok{false};
    const int64_t id = request_id.toLongLong(&ok);
    if (!ok) return false;
    const int row = indexOfId(id);
    if (row < 0) return false;
    beginRemoveRows(QModelIndex(), row, row);
    m_entries.erase(m_entries.begin() + row);
    endRemoveRows();
    Q_EMIT countChanged();
    return true;
}

std::optional<QmlRecentRequestEntry> ReceiveRequestHistoryModel::entryById(const QString& request_id) const
{
    bool ok{false};
    const int64_t id = request_id.toLongLong(&ok);
    if (!ok) return std::nullopt;
    const int row = indexOfId(id);
    if (row < 0) return std::nullopt;
    return m_entries[row];
}

int64_t ReceiveRequestHistoryModel::maxId() const
{
    int64_t max_id = 0;
    for (const auto& entry : m_entries) {
        if (entry.id > max_id) max_id = entry.id;
    }
    return max_id;
}

std::vector<QmlRecentRequestEntry> ReceiveRequestHistoryModel::DeserializeEntries(const std::vector<std::string>& blobs)
{
    std::vector<QmlRecentRequestEntry> out;
    out.reserve(blobs.size());
    for (const std::string& blob : blobs) {
        std::vector<uint8_t> data(blob.begin(), blob.end());
        DataStream ss{data};
        QmlRecentRequestEntry entry;
        try {
            ss >> entry;
        } catch (const std::ios_base::failure& e) {
            qWarning() << "ReceiveRequestHistoryModel: skipping malformed receive request entry:" << e.what();
            continue;
        } catch (const std::exception& e) {
            qWarning() << "ReceiveRequestHistoryModel: skipping receive request entry (error):" << e.what();
            continue;
        }
        out.push_back(std::move(entry));
    }
    return out;
}

std::string ReceiveRequestHistoryModel::SerializeEntry(const QmlRecentRequestEntry& entry)
{
    DataStream ss{};
    ss << entry;
    return ss.str();
}

QString ReceiveRequestHistoryModel::FormatAmountBtc(CAmount amount_sats)
{
    if (amount_sats <= 0) return {};
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, amount_sats, false,
                                   QmlBitcoinUnits::SeparatorStyle::NEVER);
}

QString ReceiveRequestHistoryModel::BuildBitcoinUri(const QString& address, CAmount amount_sats,
                                                    const QString& label, const QString& message)
{
    if (address.isEmpty()) return {};

    QString result = QStringLiteral("bitcoin:") + address;

    QStringList params;
    if (amount_sats > 0) {
        const QString amount_str = FormatAmountBtc(amount_sats);
        if (!amount_str.isEmpty()) {
            params << QStringLiteral("amount=") + amount_str;
        }
    }
    if (!label.isEmpty()) {
        params << QStringLiteral("label=") + QString::fromUtf8(QUrl::toPercentEncoding(label));
    }
    if (!message.isEmpty()) {
        params << QStringLiteral("message=") + QString::fromUtf8(QUrl::toPercentEncoding(message));
    }
    if (!params.isEmpty()) {
        result += QStringLiteral("?") + params.join(QLatin1Char('&'));
    }
    return result;
}
