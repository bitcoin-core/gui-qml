// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_NETWORKCOMMAND_H
#define BITCOIN_QML_NETWORKCOMMAND_H

#include <qml/presentationservices.h>
#include <qml/snapshotquery.h>

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>

namespace networkcommand {
struct Observation { bool active{false}; uint64_t generation{0}; };
enum class Outcome { Idle, Pending, Succeeded, Superseded, Rejected, Uncertain, Closed };
enum class Error { None, Unavailable, Admission, Unknown, Mismatch, Closed };
struct Intent {
    uint64_t id{0};
    bool active{false};
    uint64_t base{0}, availability{0};
    bool observation_only{false};
};
enum class EffectStatus { Applied, Superseded, NotStarted, Uncertain };
struct Effect {
    uint64_t id{0};
    EffectStatus status{EffectStatus::NotStarted};
    bool attempted{false}, succeeded{false}, own_frontier{false};
    // own_frontier means the scalar generations are compatible with only the
    // command's own notice. They do not prove notification origin: independent
    // RPC between the Native fence and set remains outside an atomic guarantee.
    uint64_t before{0}, after{0}, current{0};
    std::optional<Observation> observation;
};
struct Projection {
    bool active{false}, known{false}, ready{false}, pending{false}, closed{false};
    uint64_t generation{0}, request_id{0}, acknowledged{0}, availability{0};
    Outcome outcome{Outcome::Idle};
    Error error{Error::None};
    // Factual receipt of the most recently accounted Native effect, including
    // an older effect whose GUI request has since been superseded or closed.
    Effect effect;
};

/** Pure serial Domain decisions. Only observations change observed activity.
 * A queued GUI successor may cross the first command's own notification, but
 * never a newer external observation. Unknown effects are never replayed.
 */
class Command final
{
public:
    void setAvailable(bool available, uint64_t revision);
    bool observe(Observation observation);
    void queryUnavailable();
    std::optional<Intent> request(Intent intent);
    std::optional<Intent> finish(Effect effect);
    void close();
    Projection projection() const;
    const std::optional<Intent>& running() const { return m_running; }
    const std::optional<Intent>& queued() const { return m_queued; }
private:
    std::optional<Intent> start(Intent intent);
    bool m_available{false}, m_closed{false};
    uint64_t m_availability{0}, m_request_id{0};
    std::optional<Observation> m_observed;
    std::optional<Intent> m_running, m_queued;
    Outcome m_outcome{Outcome::Idle};
    Error m_error{Error::None};
    Effect m_effect;
};

struct Ports {
    // All three ports run on Native. Observation must pair the actual value
    // with its generation; Node owns that generation and notification ingress.
    std::function<Observation()> observe;
    std::function<void(bool)> set_active;
    std::function<uint64_t()> generation;
};

/** Bounded asynchronous adapter. The facade owns ingress and close, Core
 * runs the effects, and Utility reduces outcomes and retires the ports.
 */
class Service final
{
public:
    struct Queues {
        std::shared_ptr<execution::SerialQueue> core, utility;
        PresentationServices::RetireValue retire;
    };
    Service(Queues queues, Ports ports,
            std::function<void()> changed, std::function<void()> drained = {});
    ~Service();
    Service(const Service&) = delete;
    Service& operator=(const Service&) = delete;
    bool request(Intent intent);
    bool observe(Observation observation);
    bool setAvailable(bool available, uint64_t revision);
    Projection projection() const;
    // Observer retirement withdraws publication and fresh ingress, while the
    // original accepted work still owns decisions, effects and outcomes.
    void retire();
    void close();
private:
    struct Publication;
    struct State;
    std::shared_ptr<Publication> m_publication;
    std::shared_ptr<State> m_owner;
    std::weak_ptr<State> m_state;
};
} // namespace networkcommand

#endif // BITCOIN_QML_NETWORKCOMMAND_H
