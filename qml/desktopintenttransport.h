// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_DESKTOPINTENTTRANSPORT_H
#define BITCOIN_QML_DESKTOPINTENTTRANSPORT_H

#include <qml/atomicsharedptr.h>
#include <qml/desktopintentowner.h>

#include <chrono>
#include <functional>
#include <memory>
#include <vector>

class DesktopIntentTransportTests;

namespace desktopintent::transport {
enum class StartStatus { Primary, Forwarded, Rejected, Unknown };
struct StartResult {
    StartStatus status{StartStatus::Rejected};
    QString error;
    std::vector<Receipt> receipts;
    // Original local keys for the immutable transfer batch. Known receipts
    // correspond positionally; Unknown keeps keys without inventing receipts.
    std::vector<QString> producer_keys{};
};
enum class ProbeStatus { Missing, Forwarded, Rejected, Unknown };
struct ProbeResult {
    ProbeStatus status{ProbeStatus::Rejected};
    // Exact existing transfer DTO for the startup receipt/trace. A probe never
    // returns Primary. Missing carries no admission or transfer receipts.
    StartResult transfer{};
};
struct Limits {
    using Time = std::chrono::steady_clock::time_point;
    std::function<Time()> now{std::chrono::steady_clock::now};
    std::chrono::milliseconds startup{5000}, connect{1000}, acknowledge{2500}, query{1500}, client{3000}, slice{10};
};

/** One process-lifetime native endpoint. Filesystem, local sockets, locks and
 * waits run on the transport thread's Qt event loop. Start and probe
 * completions run on that thread or the request owner's worker. Stop
 * completion runs after the transport thread has joined.
 * start() attempts once; a lost forwarding ACK authorizes only original-key
 * queries. Missing/evicted receipts remain Unknown, without replay.
 */
class Service final {
public:
    static std::shared_ptr<Service> Create(std::shared_ptr<Owner> owner, Limits limits = {});
    ~Service();
    // One existing-instance-only attempt. Missing leaves the original Owner
    // and inputs untouched and permits start() at the later final profile.
    // Other outcomes are terminal for this Service; Unknown cannot fall back.
    execution::SubmitResult probeExisting(QString profile_directory, QString network, const std::vector<Input>& inputs,
                                       std::function<void(ProbeResult)> completed);
    execution::SubmitResult start(QString profile_directory, QString network, std::vector<Input> inputs,
                               std::function<void(StartResult)> completed);
    // Synchronous admission fence only; no signals, I/O or drain work.
    void close();
    // Admission means stop was requested. retired is delivered only after
    // Native sockets/server/lock are physically gone. Destruction also stops.
    execution::SubmitResult stop(std::function<void()> retired = {});
    bool retired() const;
private:
    friend class ::DesktopIntentTransportTests;
    struct State;
    struct Flags;
    explicit Service(std::shared_ptr<State> state);
    bool post(std::function<void()> work) const;
    execution::SubmitResult begin(QString directory, QString network, std::vector<Input> inputs, bool existing_only,
                               std::function<void(StartResult)> completed, std::function<void()> missing);
    ownership::AtomicSharedPtr<State> m_state;
    const std::shared_ptr<Flags> m_flags;
};
} // namespace desktopintent::transport
#endif // BITCOIN_QML_DESKTOPINTENTTRANSPORT_H
