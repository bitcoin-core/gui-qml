// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletrequestdata.h>
#include <qml/bitcoinunits.h>

#include <QUrl>

#include <streams.h>

#include <algorithm>
#include <exception>
#include <new>
#include <span>
#include <utility>

namespace walletrequests {
QString FormatAmountBtc(CAmount amount_sats)
{
    if (amount_sats <= 0) return {};
    return QmlBitcoinUnits::format(QmlBitcoinUnits::Unit::BTC, amount_sats, false,
                                   QmlBitcoinUnits::SeparatorStyle::NEVER);
}

QString BuildBitcoinUri(const QString& address, CAmount amount_sats,
                                                    const QString& label, const QString& message)
{
    if (address.isEmpty()) return {};

    QString result = QStringLiteral("bitcoin:") + address;

    QStringList params;
    if (amount_sats > 0) {
        const QString amount_str = walletrequests::FormatAmountBtc(amount_sats);
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

std::optional<QmlRecentRequestEntry> DecodeOne(std::string_view blob)
{
    try {
        SpanReader stream{std::as_bytes(std::span{blob.data(), blob.size()})};
        QmlRecentRequestEntry entry;
        stream >> entry;
        if (!stream.empty()) {
            stream >> entry.recipient.noteSelf;
            if (!stream.empty()) stream >> entry.payment_received;
        }
        return entry;
    } catch (const std::bad_alloc&) {
        throw; // Resource exhaustion is not evidence of corrupt wallet data.
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

Decoded Decode(const std::vector<std::string>& blobs)
{
    Decoded result;
    result.entries.reserve(blobs.size());
    for (const auto& blob : blobs) {
        if (auto entry = DecodeOne(blob)) result.entries.push_back(std::move(*entry));
        else ++result.malformed;
    }
    return result;
}

std::string Encode(const QmlRecentRequestEntry& entry)
{
    DataStream stream{};
    stream << entry;
    if (!entry.recipient.noteSelf.empty() || entry.payment_received) {
        stream << entry.recipient.noteSelf;
        if (entry.payment_received) stream << entry.payment_received;
    }
    return stream.str();
}

const Record* Snapshot::find(int64_t id) const
{
    const auto found = by_id.find(id);
    return found == by_id.end() ? nullptr : records.at(found->second).get();
}

void PrepareRecord(Record& record)
{
    const auto& entry = record.entry;
    PreparedRequest display;
    display.numeric_id = entry.id; display.revision = record.revision;
    display.amount_sats = entry.recipient.amount; display.created = entry.date;
    display.destination = record.destination; display.payment_received = entry.payment_received;
    display.id = QString::number(entry.id);
    display.date = entry.date.toLocalTime().toString(Qt::TextDate);
    display.date_iso = entry.date.toUTC().toString(Qt::ISODate);
    display.address = QString::fromStdString(entry.recipient.address);
    if (std::holds_alternative<PKHash>(record.destination)) display.address_type = QStringLiteral("legacy");
    else if (std::holds_alternative<ScriptHash>(record.destination)) display.address_type = QStringLiteral("p2sh-segwit");
    else if (std::holds_alternative<WitnessV0KeyHash>(record.destination) || std::holds_alternative<WitnessV0ScriptHash>(record.destination)) display.address_type = QStringLiteral("bech32");
    else if (std::holds_alternative<WitnessV1Taproot>(record.destination)) display.address_type = QStringLiteral("bech32m");
    const auto& address = display.address;
    display.address_short = address.size() <= 16 ? address : address.left(8) + QStringLiteral("…") + address.right(6);
    display.label = QString::fromStdString(entry.recipient.label);
    display.message = QString::fromStdString(entry.recipient.message);
    display.note = QString::fromStdString(entry.recipient.noteSelf);
    const auto preview = [](const QString& text) {
        return text.size() <= 512 ? text : QString{text.first(512) + QStringLiteral("…")};
    };
    display.label_preview = preview(display.label);
    display.message_preview = preview(display.message);
    display.note_preview = preview(display.note);
    display.amount = FormatAmountBtc(entry.recipient.amount);
    // History rows retain source fields only. Encode a URI for a selected
    // editor/action on Domain, rather than for every historical row.
    record.display = std::move(display);
}

SharedRecord FreezeRecord(Record record)
{
    PrepareRecord(record);
    return std::make_shared<const Record>(std::move(record));
}

bool SnapshotPreparation::Newest::operator()(const std::pair<QDateTime, int64_t>& a, const std::pair<QDateTime, int64_t>& b) const
{
    if (a.first != b.first) return a.first > b.first;
    return a.second > b.second;
}

SnapshotPreparation::SnapshotPreparation(walletopening::Opening opening, const SharedRecords& records,
                                         const std::map<QString, QString>& labels, size_t malformed)
    : m_source(records), m_labels(labels), m_next(records.begin()), m_next_label(labels.begin()),
      m_snapshot(std::make_shared<Snapshot>())
{
    m_snapshot->opening = std::move(opening);
    m_snapshot->malformed = malformed;
    // One allocation, no full-history pointer copies on vector growth.
    m_snapshot->records.reserve(records.size());
}

bool SnapshotPreparation::step(size_t budget)
{
    while (m_phase != Phase::Done) {
        if (m_phase == Phase::Capture && m_next == m_source.end()) { m_phase = Phase::Labels; continue; }
        if (m_phase == Phase::Labels && m_next_label == m_labels.end()) { m_phase = Phase::Emit; continue; }
        if (m_phase == Phase::Emit && m_sorted.empty()) { m_phase = Phase::Done; continue; }
        if (budget == 0) return false;
        --budget;
        if (m_phase == Phase::Capture) {
            const auto& [id, record] = *m_next++;
            m_snapshot->max_id = std::max(m_snapshot->max_id, id);
            m_sorted.emplace(std::pair{record->entry.date, id}, record);
            ++m_counts[record->display.address];
        } else if (m_phase == Phase::Labels) {
            m_snapshot->labels.insert(*m_next_label++);
        } else {
            auto node = m_sorted.extract(m_sorted.begin());
            auto record = std::move(node.mapped());
            const auto& address = record->display.address;
            if (const auto count = m_counts.find(address); count != m_counts.end()) {
                m_snapshot->by_address[address].reserve(count->second);
                m_counts.erase(count);
            }
            const auto row = m_snapshot->records.size();
            m_snapshot->by_id.emplace(record->entry.id, row);
            m_snapshot->by_address.at(address).push_back(record->entry.id);
            m_snapshot->addresses.insert(address);
            m_snapshot->records.push_back(std::move(record));
        }
    }
    return true;
}

void SnapshotPreparation::replace(const SharedRecord& record)
{
    if (const auto row = m_snapshot->by_id.find(record->entry.id); row != m_snapshot->by_id.end()) {
        m_snapshot->records.at(row->second) = record;
    } else if (const auto pending = m_sorted.find({record->entry.date, record->entry.id}); pending != m_sorted.end()) {
        pending->second = record;
    }
    // Uncaptured rows are read from the current source on a later step.
}

std::shared_ptr<const Snapshot> SnapshotPreparation::take(uint64_t revision)
{
    if (m_phase != Phase::Done) return {};
    m_snapshot->revision = revision;
    return std::exchange(m_snapshot, {});
}

std::shared_ptr<const Snapshot> PrepareShared(walletopening::Opening opening, uint64_t revision,
    const SharedRecords& records, std::map<QString, QString> labels, size_t malformed)
{
    SnapshotPreparation preparation{std::move(opening), records, labels, malformed};
    while (!preparation.step(128)) {}
    return preparation.take(revision);
}

std::shared_ptr<const Snapshot> Prepare(walletopening::Opening opening, uint64_t revision,
    const std::map<int64_t, Record>& records, std::map<QString, QString> labels, size_t malformed)
{
    SharedRecords shared;
    for (const auto& [id, record] : records) shared.emplace(id, FreezeRecord(record));
    return PrepareShared(std::move(opening), revision, shared, std::move(labels), malformed);
}
} // namespace walletrequests
