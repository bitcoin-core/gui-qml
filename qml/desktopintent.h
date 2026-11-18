// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_DESKTOPINTENT_H
#define BITCOIN_QML_DESKTOPINTENT_H

#include <QString>
#include <compare>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace desktopintent {
enum class Kind { Text, File };
enum class Origin { Arguments, FileOpen, Forwarded };
enum class Phase { WaitingReady, Ready, Delivering, ReconcilingDelivery, Stopping, Stopped, Forwarding };
enum class Error { None, Invalid, Capacity, Closed, UnknownDelivery, TargetUnavailable };
enum class Disposition { Queued, Accepted, Dismissed, DroppedOnStop };
struct Input {
    QString producer_key;
    Kind kind{Kind::Text};
    QString value;
    Origin origin{Origin::Arguments};
};
struct Ticket {
    uint64_t delivery{0}, route{0}, target{0};
    auto operator<=>(const Ticket&) const = default;
};
struct Receipt {
    uint64_t delivery{0}, inspection{0};
    Disposition disposition{Disposition::Queued};
};
struct Admission {
    Error error{Error::None};
    std::vector<Receipt> receipts;
    explicit operator bool() const { return error == Error::None; }
};
struct ForwardingSnapshot {
    Error error{Error::None};
    std::vector<Input> inputs;
    explicit operator bool() const { return error == Error::None; }
};
struct Delivery { Ticket ticket; Input input; };
struct Snapshot {
    Phase phase{Phase::WaitingReady};
    Error error{Error::None};
    size_t pending{0};
    std::optional<Ticket> active;
    uint64_t sequence{0};
};

/** Domain-owned delivery protocol. Native adapters acknowledge only retained
 * admission receipts. Producer-key deduplication lasts for the bounded receipt
 * lifetime; it does not establish durable or cross-restart exactly-once work. */
class Queue {
public:
    static constexpr size_t MAX_PENDING{16}, MAX_RECEIPTS{64};
    static constexpr qsizetype MAX_INPUT_CHARS{16 * 1024}, MAX_KEY_CHARS{128};
    static constexpr size_t MAX_PENDING_BYTES{128 * 1024};
    Admission admit(const std::vector<Input>& inputs);
    ForwardingSnapshot prepareForwarding(const std::vector<Input>& arguments);
    void readiness(uint64_t revision, bool ready);
    void receiverCapacity(bool available);
    std::optional<Delivery> deliver(uint64_t target);
    bool accepted(Ticket ticket, uint64_t inspection);
    bool rejected(Ticket ticket);
    bool unknown(Ticket ticket);
    bool dismiss();
    void stop();
    Snapshot snapshot() const;
    std::optional<Receipt> lookup(const QString& producer_key) const;
private:
    struct Record { Input input; Receipt receipt; };
    Error admitOne(const Input& input, Receipt& receipt);
    Record* find(uint64_t delivery);
    void settle(Disposition disposition, uint64_t inspection = 0);
    void updatePhase();
    std::deque<Record> m_records;
    std::deque<uint64_t> m_pending;
    std::optional<Ticket> m_active;
    uint64_t m_delivery_sequence{0}, m_route_sequence{0}, m_readiness_revision{0};
    Phase m_phase{Phase::WaitingReady};
    Error m_error{Error::None};
    bool m_ready{false}, m_stopping{false}, m_unknown{false}, m_forwarding{false};
    bool m_receiver_capacity{true};
};
} // namespace desktopintent
#endif // BITCOIN_QML_DESKTOPINTENT_H
