// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/desktopintent.h>
#include <QDir>
#include <QUrl>

#include <algorithm>
#include <limits>
#include <utility>

namespace desktopintent {
Queue::Record* Queue::find(uint64_t delivery)
{
    const auto it = std::ranges::find_if(m_records, [delivery](const Record& record) { return record.receipt.delivery == delivery; });
    return it == m_records.end() ? nullptr : &*it;
}

Error Queue::admitOne(const Input& input, Receipt& receipt)
{
    if (m_stopping) return Error::Closed;
    if (input.producer_key.isEmpty() || input.producer_key.size() > MAX_KEY_CHARS ||
        input.value.isEmpty() || input.value.size() > MAX_INPUT_CHARS) return Error::Invalid;
    if (input.origin == Origin::FileOpen && input.kind == Kind::File) {
        const QUrl url{input.value, QUrl::StrictMode};
        const auto path = url.isLocalFile() && url.host().isEmpty() ? url.toLocalFile() : input.value;
        if (!QDir::isAbsolutePath(path)) return Error::Invalid;
    }
    for (const auto& record : m_records) {
        if (record.input.producer_key != input.producer_key) continue;
        if (record.input.kind != input.kind || record.input.value != input.value) return Error::Invalid;
        receipt = record.receipt;
        return Error::None;
    }
    size_t bytes{static_cast<size_t>(input.value.size()) * sizeof(QChar)};
    for (const auto id : m_pending) bytes += static_cast<size_t>(find(id)->input.value.size()) * sizeof(QChar);
    if (m_pending.size() >= MAX_PENDING || bytes > MAX_PENDING_BYTES ||
        m_delivery_sequence == std::numeric_limits<uint64_t>::max()) return Error::Capacity;
    if (m_records.size() == MAX_RECEIPTS) {
        const auto it = std::ranges::find_if(m_records, [](const Record& record) { return record.receipt.disposition != Disposition::Queued; });
        if (it == m_records.end()) return Error::Capacity;
        m_records.erase(it);
    }
    receipt.delivery = ++m_delivery_sequence;
    m_records.push_back({input, receipt});
    m_pending.push_back(receipt.delivery);
    return Error::None;
}

Acceptance Queue::admit(const std::vector<Input>& inputs)
{
    if (m_stopping || m_forwarding) return {Error::Closed, {}};
    if (inputs.empty() || inputs.size() > MAX_PENDING) return {Error::Invalid, {}};
    // Forwarded frames are atomic bounded admissions. A rejected suffix cannot
    // leave an accepted prefix or consume the original queue's identities.
    Queue trial{*this};
    Acceptance result;
    result.receipts.reserve(inputs.size());
    for (const auto& input : inputs) {
        Receipt receipt;
        const auto error = trial.admitOne(input, receipt);
        if (error != Error::None) { m_error = error; return {error, {}}; }
        result.receipts.push_back(receipt);
    }
    trial.m_error = Error::None;
    *this = std::move(trial);
    return result;
}

ForwardingSnapshot Queue::prepareForwarding(const std::vector<Input>& arguments)
{
    if (m_stopping || m_forwarding) return {Error::Closed, {}};
    if (m_ready || m_active) return {Error::Invalid, {}};
    Queue trial{*this};
    const auto admitted = arguments.empty() ? Acceptance{} : trial.admit(arguments);
    if (!admitted) {
        m_forwarding = true; m_error = admitted.error; updatePhase();
        return {admitted.error, {}};
    }
    ForwardingSnapshot snapshot;
    snapshot.inputs.reserve(trial.m_pending.size());
    for (const auto id : trial.m_pending) snapshot.inputs.push_back(trial.find(id)->input);
    trial.m_forwarding = true; trial.updatePhase();
    *this = std::move(trial);
    return snapshot;
}

void Queue::readiness(uint64_t revision, bool ready)
{
    if (m_stopping || m_forwarding || revision <= m_readiness_revision) return;
    m_readiness_revision = revision;
    m_ready = ready;
    updatePhase();
}

std::optional<Delivery> Queue::deliver(uint64_t target)
{
    if (m_stopping || !m_ready || !m_receiver_capacity || m_active || m_pending.empty() || target == 0 ||
        m_route_sequence == std::numeric_limits<uint64_t>::max()) return {};
    m_active = DeliveryKey{m_pending.front(), ++m_route_sequence, target};
    m_error = Error::None;
    m_unknown = false;
    updatePhase();
    return Delivery{*m_active, find(m_active->delivery)->input};
}
void Queue::receiverCapacity(bool available) { m_receiver_capacity = available; updatePhase(); }

void Queue::settle(Disposition disposition, uint64_t inspection)
{
    auto* record = find(m_pending.front());
    record->receipt.disposition = disposition;
    record->receipt.inspection = inspection;
    m_pending.pop_front();
    m_active.reset();
    m_unknown = false;
    m_error = Error::None;
    updatePhase();
}

bool Queue::accepted(DeliveryKey key, uint64_t inspection)
{
    if (!m_active || *m_active != key || inspection == 0) return false;
    settle(Disposition::Accepted, inspection);
    return true;
}

bool Queue::rejected(DeliveryKey key)
{
    if (!m_active || *m_active != key) return false;
    m_active.reset();
    m_unknown = false;
    m_ready = false;
    m_error = Error::TargetUnavailable;
    if (m_stopping) settle(Disposition::DroppedOnStop);
    else updatePhase();
    return true;
}

bool Queue::unknown(DeliveryKey key)
{
    if (!m_active || *m_active != key) return false;
    m_unknown = true;
    m_error = Error::UnknownDelivery;
    updatePhase();
    return true;
}

bool Queue::dismiss()
{
    if (m_stopping || m_active) return false;
    if (m_pending.empty()) { m_error = Error::None; return true; }
    settle(Disposition::Dismissed);
    return true;
}

void Queue::stop()
{
    m_stopping = true;
    m_ready = false;
    const size_t retained = m_active ? 1 : 0;
    while (m_pending.size() > retained) {
        find(m_pending.back())->receipt.disposition = Disposition::DroppedOnStop;
        m_pending.pop_back();
    }
    updatePhase();
}

void Queue::updatePhase()
{
    if (m_stopping) m_phase = m_active ? Phase::Stopping : Phase::Stopped;
    else if (m_forwarding) m_phase = Phase::Forwarding;
    else if (m_active) m_phase = m_unknown ? Phase::ReconcilingDelivery : Phase::Delivering;
    else m_phase = m_ready && m_receiver_capacity ? Phase::Ready : Phase::WaitingReady;
}

Snapshot Queue::snapshot() const { return {m_phase, m_error, m_pending.size(), m_active}; }
std::optional<Receipt> Queue::lookup(const QString& producer_key) const
{
    for (const auto& record : m_records) if (record.input.producer_key == producer_key) return record.receipt;
    return {};
}
} // namespace desktopintent
