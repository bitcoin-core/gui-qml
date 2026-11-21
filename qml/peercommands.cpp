// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/peercommands.h>
#include <interfaces/node.h>
#include <netbase.h>
#include <QObject>
#include <algorithm>
#include <limits>
#include <utility>

namespace {
using namespace peercommands;
bool Matches(const PeerConnection& expected, const CNodeStats& current)
{
    return expected.node_id == current.nodeid && expected.connected == current.m_connected && expected.endpoint == current.addr &&
        (expected.session.isEmpty() || expected.session == QString::fromStdString(current.m_session_id));
}
bool Present(const Request& request, const interfaces::Node::NodesStats& stats)
{
    return std::any_of(stats.begin(), stats.end(), [&](const auto& entry) { return Matches(request.target, std::get<0>(entry)); });
}
bool ValidDuration(int64_t value) { return value == 3600 || value == 86400 || value == 604800 || value == 31536000; }
Result Reject(const Request& request, const QString& error)
{
    Result result; result.request = request; result.present = true; result.error = error; return result;
}
Result Execute(interfaces::Node& node, const Request& request)
{
    auto result = Reject(request, {});
    std::optional<CNetAddr> address;
    try {
        if (request.target.node_id < 0 && (request.kind == Kind::Disconnect || request.exact_connection)) return Reject(request, QObject::tr("The peer connection is invalid."));
        if (request.kind == Kind::Ban) {
            if (request.duration <= 0 || (request.exact_connection && !ValidDuration(request.duration)) ||
                request.target.raw_address.isEmpty() || request.target.raw_address.size() > 512)
                return Reject(request, QObject::tr("The ban duration or address is invalid."));
            address = LookupHost(request.target.raw_address.toStdString(), false);
            if (!address) return Reject(request, QObject::tr("The peer address is invalid."));
            if (!CSubNet{*address}.IsValid()) return Reject(request, QObject::tr("The peer network does not support an IP-subnet ban."));
        }
        if (request.exact_connection) {
            interfaces::Node::NodesStats stats;
            if (!node.getNodesStats(stats)) return Reject(request, QObject::tr("The original peer connection could not be checked."));
            if (!Present(request, stats)) return Reject(request, QObject::tr("The original peer connection is no longer available."));
        }
    } catch (...) { return Reject(request, QObject::tr("The original peer connection could not be checked.")); }
    result.mutation_attempted = true;
    try {
        if (request.kind == Kind::Disconnect) {
            result.disconnect_confirmed = node.disconnectById(request.target.node_id);
            result.outcome = result.disconnect_confirmed ? Outcome::Succeeded : Outcome::Rejected;
            if (!result.disconnect_confirmed) result.error = QObject::tr("The peer could not be disconnected. It may already be disconnected.");
        } else {
            result.ban_applied = node.ban(*address, request.duration);
            if (!result.ban_applied) { result.error = QObject::tr("The peer could not be banned."); return result; }
            result.disconnect_confirmed = node.disconnectByAddress(*address);
            // Preserve the existing bare-address compatibility behavior while
            // retaining separately confirmed effects for connection commands.
            result.outcome = result.disconnect_confirmed || !request.exact_connection ? Outcome::Succeeded : Outcome::PartiallyApplied;
            if (result.outcome == Outcome::PartiallyApplied) result.error = QObject::tr("The ban was applied, but disconnecting the peer was not confirmed.");
        }
    } catch (...) {
        result.outcome_known = false;
        result.outcome = result.ban_applied ? Outcome::PartiallyApplied : Outcome::Uncertain;
        result.error = result.ban_applied ? QObject::tr("The ban was applied, but the disconnect outcome is unknown.")
            : QObject::tr("The peer command outcome is unknown. Check the original peer and ban state before taking another action.");
    }
    return result;
}
Result Observe(interfaces::Node& node, Result original)
{
    try {
        interfaces::Node::NodesStats stats;
        if (!node.getNodesStats(stats)) { original.query_error = QObject::tr("The original peer state could not be refreshed."); return original; }
        original.observation_available = true; original.target_connected = Present(original.request, stats);
        if (original.request.kind == Kind::Ban) {
            const auto address = LookupHost(original.request.target.raw_address.toStdString(), false);
            banmap_t bans;
            if (!address || !node.getBanned(bans)) { original.query_error = QObject::tr("The original ban state could not be refreshed."); return original; }
            original.ban_observation_available = true;
            original.target_banned = std::any_of(bans.begin(), bans.end(), [&](const auto& entry) { return entry.first.Match(*address); });
        }
        original.query_error.clear();
        // Observation is not proof of this command's causality. Preserve the
        // original unknown/partial receipt; there is no mutation retry here.
    } catch (...) { original.query_error = QObject::tr("The original peer state could not be refreshed."); }
    return original;
}
void Notify(const std::function<void()>& callback) { if (callback) { try { callback(); } catch (...) { } } }
} // namespace

peercommands::Store::Store(interfaces::Node& node, execution::SnapshotServices services, std::function<void()> notify)
    : m_node(node), m_services(std::move(services)), m_key(m_services.scheduler ? m_services.scheduler->allocateKey() : execution::ResourceKey{}), m_notify(std::move(notify)) {}
peercommands::Store::Snapshot peercommands::Store::snapshot() const { std::lock_guard lock{m_mutex}; return m_snapshot; }
execution::Admission peercommands::Store::submit(const std::shared_ptr<Operation>& operation) { return admit(operation, false); }
execution::Admission peercommands::Store::reconcile(const std::shared_ptr<Operation>& operation) { return admit(operation, true); }
execution::Admission peercommands::Store::admit(const std::shared_ptr<Operation>& operation, bool query)
{
    if (!operation) return {{}, execution::Rejection::Invalid};
    {
        std::lock_guard lock{operation->mutex};
        if (query ? (!operation->actual || operation->actual->outcome_known || operation->query_pending) : operation->started)
            return {{}, execution::Rejection::Invalid};
        if (!query) operation->started = true;
    }
    const auto refuse = [&](execution::Rejection rejection) {
        std::lock_guard lock{operation->mutex};
        if (query) { operation->query_pending = false; if (operation->actual) operation->actual->query_error = QObject::tr("The original peer state could not be scheduled for refresh."); }
        else operation->actual = Reject(operation->request, QObject::tr("The peer command could not be scheduled."));
        return execution::Admission{{}, rejection};
    };
    const auto& target = operation->request.target;
    if (operation->request.id.size() > 64 || target.key.size() > 2048 || target.session.size() > 128 ||
        target.raw_address.size() > 512 || target.display_address.size() > 512) return refuse(execution::Rejection::Capacity);
    if (m_closed || !m_services.scheduler || m_key == execution::ResourceKey{}) return refuse(execution::Rejection::Closed);
    {
        std::lock_guard lock{m_mutex};
        if (m_active) return refuse(execution::Rejection::Invalid);
        if (m_snapshot.revision == std::numeric_limits<quint64>::max()) return refuse(execution::Rejection::Capacity);
        m_active = operation; m_snapshot.pending = true;
    }
    const std::weak_ptr<Operation> weak{operation};
    auto command = std::make_shared<execution::OwnedCommand<Result>>(m_services.scheduler, [self = shared_from_this(), weak, query] {
        if (auto original = weak.lock()) self->complete(original, query);
    });
    { std::lock_guard lock{operation->mutex}; operation->command = command; operation->query_pending = query; }
    const auto original = query ? operation->result() : std::optional<Result>{};
    const auto request = operation->request;
    const auto admission = command->start([node = &m_node, request, original, query] { return query ? Observe(*node, *original) : Execute(*node, request); },
        {execution::Lane::Core, m_key, query ? execution::WorkClass::Observation : execution::WorkClass::Action});
    if (!admission) {
        { std::lock_guard lock{m_mutex}; if (m_active == operation) { m_active.reset(); m_snapshot.pending = false; } }
        { std::lock_guard lock{operation->mutex}; operation->command.reset(); }
        return refuse(admission.rejection);
    }
    return admission;
}
void peercommands::Store::complete(const std::shared_ptr<Operation>& operation, bool query)
{
    std::shared_ptr<execution::OwnedCommand<Result>> command;
    { std::lock_guard lock{operation->mutex}; command = operation->command; }
    const auto completed = command ? command->result() : std::nullopt;
    if (!completed) return;
    auto result = completed->value.value_or(operation->result().value_or(Reject(operation->request, {})));
    if (!completed->value) {
        if (query) result.query_error = QObject::tr("The original peer state could not be refreshed.");
        else { result.outcome = Outcome::Uncertain; result.outcome_known = false; result.mutation_attempted = true; result.error = QObject::tr("The peer command outcome is unknown."); }
    }
    { std::lock_guard lock{operation->mutex}; operation->command.reset(); operation->actual = result; operation->query_pending = false; }
    {
        std::lock_guard lock{m_mutex};
        m_snapshot.latest = result; ++m_snapshot.revision;
        if (!query) { m_snapshot.mutation = result; ++m_snapshot.mutation_revision; }
        if (m_active == operation) { m_active.reset(); m_snapshot.pending = false; }
    }
    Notify(m_notify); Notify(operation->notify);
}
bool peercommands::Store::cancel(const std::shared_ptr<Operation>& operation)
{
    if (!operation) return false;
    std::lock_guard lock{operation->mutex};
    if (operation->started) return false;
    operation->started = true; operation->actual = Reject(operation->request, {}); operation->actual->outcome = Outcome::Cancelled;
    return true;
}
bool peercommands::Store::refuse(const std::shared_ptr<Operation>& operation, const QString& error)
{
    if (!operation) return false;
    std::lock_guard lock{operation->mutex};
    if (operation->started) return false;
    operation->started = true; operation->actual = Reject(operation->request, error); return true;
}
void peercommands::Store::refuseQuery(const std::shared_ptr<Operation>& operation, const QString& error)
{
    if (!operation) return;
    std::lock_guard lock{operation->mutex};
    if (operation->actual && !operation->query_pending) operation->actual->query_error = error;
}
