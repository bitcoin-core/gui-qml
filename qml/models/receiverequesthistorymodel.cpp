// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/receiverequesthistorymodel.h>

#include <qml/walletrequestdata.h>
#include <streams.h>

#include <algorithm>
#include <ios>
#include <utility>

#include <QDebug>
#include <QPointer>

ReceiveRequestHistoryModel::ReceiveRequestHistoryModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int ReceiveRequestHistoryModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid()) return 0;
    return count();
}

int ReceiveRequestHistoryModel::count() const
{
    return static_cast<int>(m_snapshot ? m_snapshot->records.size() : m_entries.size());
}

std::shared_ptr<const walletrequests::Snapshot> ReceiveRequestHistoryModel::setSnapshot(std::shared_ptr<const walletrequests::Snapshot> snapshot)
{
    if (snapshot == m_snapshot) return {};
    const QPointer<ReceiveRequestHistoryModel> observer{this};
    beginResetModel();
    if (!observer) return {};
    auto old = std::exchange(m_snapshot, std::move(snapshot));
    endResetModel();
    if (observer) Q_EMIT countChanged();
    return old;
}

std::shared_ptr<const walletrequests::Snapshot> ReceiveRequestHistoryModel::releaseSnapshot()
{
    return std::exchange(m_snapshot, {});
}

const walletrequests::Record* ReceiveRequestHistoryModel::preparedEntry(const QString& request_id) const
{
    if (!m_snapshot) return nullptr;
    bool valid{false};
    const auto id = request_id.toLongLong(&valid);
    return valid ? m_snapshot->find(id) : nullptr;
}

QString ReceiveRequestHistoryModel::firstRequestIdForAddress(const QString& address) const
{
    if (m_snapshot) {
        const auto found = m_snapshot->by_address.find(address);
        if (found == m_snapshot->by_address.end() || found->second.empty()) return {};
        return m_snapshot->find(found->second.front())->display.id;
    }
    const auto found = m_ids_by_address.constFind(address);
    return found == m_ids_by_address.cend() || found->empty() ? QString{} : QString::number(found->front());
}

QVariant ReceiveRequestHistoryModel::data(const QModelIndex& index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
        return {};
    }
    return m_snapshot ? dataForRecord(*m_snapshot->records[index.row()], role) : dataForEntry(m_entries[index.row()], role);
}

QVariant ReceiveRequestHistoryModel::dataForRecord(const walletrequests::Record& record, int role) const
{
    const auto& display = record.display;
    switch (role) {
    case IdRole: return display.id;
    case DateRole: return display.date;
    case DateIsoRole: return display.date_iso;
    case AddressRole: return display.address;
    case AddressShortRole: return display.address_short;
    case LabelRole: return display.label_preview;
    case MessageRole: return display.message_preview;
    case NoteSelfRole: return display.note_preview;
    case AmountSatRole: return QVariant::fromValue<qlonglong>(record.entry.recipient.amount);
    case AmountDisplayRole: return display.amount;
    case UriRole: return display.uri;
    }
    return {};
}

QVariantMap ReceiveRequestHistoryModel::recordMap(const walletrequests::Record& record) const
{
    QVariantMap result;
    const auto names = roleNames();
    for (auto it = names.cbegin(); it != names.cend(); ++it) result.insert(QString::fromLatin1(it.value()), dataForRecord(record, it.key()));
    result.insert(QStringLiteral("paymentReceived"), record.entry.payment_received);
    return result;
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
        if (entry.payment_received) return QString{};
        return BuildBitcoinUri(QString::fromStdString(entry.recipient.address),
                               entry.recipient.amount,
                               QString::fromStdString(entry.recipient.label),
                               QString::fromStdString(entry.recipient.message));
    }
    return {};
}

QVariantMap ReceiveRequestHistoryModel::entryMap(const QmlRecentRequestEntry& entry) const
{
    return {
        {QStringLiteral("requestId"), dataForEntry(entry, IdRole)},
        {QStringLiteral("date"), dataForEntry(entry, DateRole)},
        {QStringLiteral("dateIso"), dataForEntry(entry, DateIsoRole)},
        {QStringLiteral("address"), dataForEntry(entry, AddressRole)},
        {QStringLiteral("addressShort"), dataForEntry(entry, AddressShortRole)},
        {QStringLiteral("label"), dataForEntry(entry, LabelRole)},
        {QStringLiteral("message"), dataForEntry(entry, MessageRole)},
        {QStringLiteral("noteSelf"), dataForEntry(entry, NoteSelfRole)},
        {QStringLiteral("amountSat"), dataForEntry(entry, AmountSatRole)},
        {QStringLiteral("amountDisplay"), dataForEntry(entry, AmountDisplayRole)},
        {QStringLiteral("uri"), dataForEntry(entry, UriRole)},
        {QStringLiteral("paymentReceived"), entry.payment_received},
    };
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
    return m_row_by_id.value(id, -1);
}

void ReceiveRequestHistoryModel::rebuildIndex()
{
    m_row_by_id.clear();
    m_ids_by_address.clear();
    for (size_t row = 0; row < m_entries.size(); ++row) {
        const auto& entry = m_entries[row];
        m_row_by_id.insert(entry.id, static_cast<int>(row));
        m_ids_by_address[QString::fromStdString(entry.recipient.address)].push_back(entry.id);
    }
}

void ReceiveRequestHistoryModel::setEntries(std::vector<QmlRecentRequestEntry>&& entries)
{
    beginResetModel();
    m_entries = std::move(entries);
    std::sort(m_entries.begin(), m_entries.end(),
              [](const QmlRecentRequestEntry& a, const QmlRecentRequestEntry& b) {
                  // Tie-break equal dates on the id so requests created in
                  // the same second keep a deterministic newest-first order.
                  if (a.date != b.date) return a.date > b.date;
                  return a.id > b.id;
              });
    rebuildIndex();
    endResetModel();
    Q_EMIT countChanged();
}

void ReceiveRequestHistoryModel::prependOrReplace(const QmlRecentRequestEntry& entry)
{
    const int existing = indexOfId(entry.id);
    if (existing >= 0) {
        const bool address_changed = m_entries[existing].recipient.address != entry.recipient.address;
        m_entries[existing] = entry;
        if (address_changed) rebuildIndex();
        const QModelIndex idx = index(existing);
        Q_EMIT dataChanged(idx, idx);
        return;
    }
    beginInsertRows(QModelIndex(), 0, 0);
    m_entries.insert(m_entries.begin(), entry);
    rebuildIndex();
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
    rebuildIndex();
    endRemoveRows();
    Q_EMIT countChanged();
    return true;
}

QVariantList ReceiveRequestHistoryModel::matchingEntriesForAddress(const QString& address) const
{
    QVariantList matches;
    if (address.isEmpty()) return matches;
    if (m_snapshot) {
        const auto found = m_snapshot->by_address.find(address);
        if (found != m_snapshot->by_address.end()) {
            for (const auto id : found->second) matches.append(recordMap(*m_snapshot->find(id)));
        }
        return matches;
    }
    for (const int64_t id : m_ids_by_address.value(address)) {
        matches.append(entryMap(m_entries[indexOfId(id)]));
    }
    return matches;
}

std::optional<QmlRecentRequestEntry> ReceiveRequestHistoryModel::entryById(const QString& request_id) const
{
    if (m_snapshot) {
        const auto record = preparedEntry(request_id);
        return record ? std::optional{record->entry} : std::nullopt;
    }
    bool ok{false};
    const int64_t id = request_id.toLongLong(&ok);
    if (!ok) return std::nullopt;
    const int row = indexOfId(id);
    if (row < 0) return std::nullopt;
    return m_entries[row];
}

int64_t ReceiveRequestHistoryModel::maxId() const
{
    if (m_snapshot) return m_snapshot->max_id;
    int64_t max_id = 0;
    for (const auto& entry : m_entries) {
        if (entry.id > max_id) max_id = entry.id;
    }
    return max_id;
}

// Wallet receive requests are stored in address destdata "rr##" values that
// bitcoin-qt reads as Qt Widgets RecentRequestEntry records. Keep the serialized
// prefix byte-compatible with bitcoin/src/qt/recentrequeststablemodel.h and
// bitcoin/src/qt/sendcoinsrecipient.h: version 1, Qt's unsigned timestamp, and
// no QML-only fields inside the prefix.
//
// QML persists noteSelf as an optional trailing string, followed by an optional
// payment-received flag. Released
// bitcoin-qt builds ignore trailing bytes after RecentRequestEntry, while QML
// reads the extension when present. If Qt Widgets gains noteSelf support, move
// this to an explicit shared contract instead of changing the prefix ad hoc.
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
            if (!ss.empty()) {
                ss >> entry.recipient.noteSelf;
                if (!ss.empty()) ss >> entry.payment_received;
            }
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
    if (!entry.recipient.noteSelf.empty() || entry.payment_received) {
        ss << entry.recipient.noteSelf;
        if (entry.payment_received) ss << entry.payment_received;
    }
    return ss.str();
}

QString ReceiveRequestHistoryModel::FormatAmountBtc(CAmount amount_sats)
{
    return walletrequests::FormatAmountBtc(amount_sats);
}

QString ReceiveRequestHistoryModel::BuildBitcoinUri(const QString& address, CAmount amount_sats,
                                                   const QString& label, const QString& message)
{
    return walletrequests::BuildBitcoinUri(address, amount_sats, label, message);
}
