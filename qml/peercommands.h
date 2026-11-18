// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_PEERCOMMANDS_H
#define BITCOIN_QML_PEERCOMMANDS_H

#include <qml/models/peerrecord.h>
#include <qml/snapshotquery.h>
#include <atomic>
#include <mutex>
#include <optional>

namespace interfaces { class Node; }
namespace peercommands {
enum class Kind { Disconnect, Ban };
enum class Outcome { Succeeded, Rejected, PartiallyApplied, Uncertain, Cancelled };
struct Request {
    QString id;
    quint64 ticket{0};
    PeerConnection target;
    Kind kind{Kind::Disconnect};
    int64_t duration{0};
    bool exact_connection{true}; // Bare-ID compatibility is not a connection token.
};
struct Result {
    Request request;
    Outcome outcome{Outcome::Rejected};
    QString error;
    bool present{false}, outcome_known{true};
    bool mutation_attempted{false}, ban_applied{false}, disconnect_confirmed{false};
    bool observation_available{false}, target_connected{false}, ban_observation_available{false}, target_banned{false};
    QString query_error;
};
/** Bounded immutable intent and retained actual outcome. Selection and QObject
 * lifetime never own or retarget an admitted mutation. */
class Operation {
public:
    explicit Operation(Request request, std::function<void()> notify = {}) : request(std::move(request)), notify(std::move(notify)) {}
    const Request request;
    std::optional<Result> result() const { std::lock_guard lock{mutex}; return actual; }
    bool queryPending() const { std::lock_guard lock{mutex}; return query_pending; }
private:
    friend class Store;
    mutable std::mutex mutex;
    bool started{false}, query_pending{false};
    std::optional<Result> actual;
    const std::function<void()> notify;
    std::shared_ptr<execution::OwnedCommand<Result>> command;
};
/** One public-Node effect writer. Inventory validation, address parsing,
 * mutations and recovery reads run on Core using the original identity. */
class Store : public std::enable_shared_from_this<Store> {
public:
    struct Snapshot {
        bool pending{false};
        quint64 revision{0}, mutation_revision{0};
        std::optional<Result> latest, mutation;
    };
    using Post = execution::SnapshotQuery<Result>::Post;
    Store(interfaces::Node& node, std::function<void()> notify, Post post_native);
    execution::SubmitResult submit(const std::shared_ptr<Operation>& operation);
    execution::SubmitResult reconcile(const std::shared_ptr<Operation>& operation);
    bool cancel(const std::shared_ptr<Operation>& operation);
    bool refuse(const std::shared_ptr<Operation>& operation, const QString& error);
    void refuseQuery(const std::shared_ptr<Operation>& operation, const QString& error);
    Snapshot snapshot() const;
    void close() { m_closed = true; }
private:
    execution::SubmitResult admit(const std::shared_ptr<Operation>& operation, bool query);
    void complete(const std::shared_ptr<Operation>& operation, bool query);
    interfaces::Node& m_node;
    const Post m_post_native;
    const std::function<void()> m_notify;
    std::atomic_bool m_closed{false};
    mutable std::mutex m_mutex;
    std::shared_ptr<Operation> m_active;
    Snapshot m_snapshot;
};
} // namespace peercommands
#endif // BITCOIN_QML_PEERCOMMANDS_H
