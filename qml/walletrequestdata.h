// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETREQUESTDATA_H
#define BITCOIN_QML_WALLETREQUESTDATA_H

#include <qml/models/receiverequestentry.h>
#include <qml/walletopeningstate.h>

#include <addresstype.h>

#include <QSet>
#include <QString>

#include <map>
#include <memory>
#include <optional>
#include <string_view>
#include <vector>

namespace walletrequests {
QString FormatAmountBtc(CAmount amount_sats);
QString BuildBitcoinUri(const QString& address, CAmount amount_sats, const QString& label, const QString& message);
struct PayloadInput {
    QString address, label, message;
    CAmount amount{0};
};
struct Decoded {
    std::vector<QmlRecentRequestEntry> entries;
    size_t malformed{0};
};
std::optional<QmlRecentRequestEntry> DecodeOne(std::string_view blob);
Decoded Decode(const std::vector<std::string>& blobs);
std::string Encode(const QmlRecentRequestEntry& entry);

/** Immutable prepared values. Copies share Qt text storage and never copy or
 * decode serialized request strings on a GUI consumer. */
struct PreparedRequest {
    QString id, date, date_iso, address, address_short, address_type, label, message, note, amount, uri;
    // Bounded history/detail summaries, never editable persistence values.
    QString label_preview, message_preview, note_preview;
    int64_t numeric_id{0};
    uint64_t revision{0};
    CAmount amount_sats{0};
    QDateTime created;
    CTxDestination destination;
    bool payment_received{false};
};
struct Record {
    QmlRecentRequestEntry entry;
    uint64_t revision{0};
    CTxDestination destination;
    PreparedRequest display{};
};
void PrepareRecord(Record& record);
using SharedRecord = std::shared_ptr<const Record>;
using SharedRecords = std::map<int64_t, SharedRecord>;
/** Build one immutable row on Domain. Every later snapshot shares it until a
 * logical change replaces that row; serialized strings are never recopied by
 * preparing another presentation of unchanged history. */
SharedRecord FreezeRecord(Record record);
/** Prepared on Domain. GUI lookups do not sort or rebuild an index. */
struct Snapshot {
    walletopening::Opening opening;
    uint64_t revision{0};
    std::vector<SharedRecord> records;
    std::map<int64_t, size_t> by_id;
    std::map<QString, std::vector<int64_t>> by_address;
    QSet<QString> addresses;
    std::map<QString, QString> labels;
    int64_t max_id{0};
    size_t malformed{0};
    const Record* find(int64_t id) const;
};
/** Incremental value preparation. The owner keeps row keys/labels stable while
 * this runs. Payment-only replacements preserve keys/order and are patched
 * into either the sorted candidates or the emitted row without restarting. */
class SnapshotPreparation
{
public:
    SnapshotPreparation(walletopening::Opening opening, const SharedRecords& records,
                        const std::map<QString, QString>& labels, size_t malformed);
    bool step(size_t budget);
    void replace(const SharedRecord& record);
    std::shared_ptr<const Snapshot> take(uint64_t revision);
private:
    struct Newest {
        bool operator()(const std::pair<QDateTime, int64_t>& a, const std::pair<QDateTime, int64_t>& b) const;
    };
    enum class Phase { Capture, Labels, Emit, Done };
    const SharedRecords& m_source;
    const std::map<QString, QString>& m_labels;
    SharedRecords::const_iterator m_next;
    std::map<QString, QString>::const_iterator m_next_label;
    std::map<std::pair<QDateTime, int64_t>, SharedRecord, Newest> m_sorted;
    std::map<QString, size_t> m_counts;
    std::shared_ptr<Snapshot> m_snapshot;
    Phase m_phase{Phase::Capture};
};
std::shared_ptr<const Snapshot> PrepareShared(walletopening::Opening opening, uint64_t revision,
    const SharedRecords& records, std::map<QString, QString> labels = {}, size_t malformed = 0);
std::shared_ptr<const Snapshot> Prepare(walletopening::Opening opening, uint64_t revision,
    const std::map<int64_t, Record>& records, std::map<QString, QString> labels = {}, size_t malformed = 0);
} // namespace walletrequests

#endif // BITCOIN_QML_WALLETREQUESTDATA_H
