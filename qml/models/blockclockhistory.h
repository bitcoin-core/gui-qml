// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BLOCKCLOCKHISTORY_H
#define BITCOIN_QML_MODELS_BLOCKCLOCKHISTORY_H

#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <uint256.h>

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>

#include <QList>
#include <QtGlobal>

namespace interfaces {
class Node;
}

/**
 * Event-driven active-chain history for the block clock.
 *
 * This receiver owns no backend interface or subscription. The initialization
 * worker owns a separate BlockClockSubscription, which stops this cache before
 * unregistering. In-flight notifications may keep the stopped cache alive.
 *
 * The GUI only sets the retention boundary and takes cached snapshots. The
 * mutex protects this small cache; no backend call runs with the mutex held.
 * Future timestamps are retained so advancing the dial needs no node query.
 */
class BlockClockHistory : public interfaces::Chain::Notifications
{
public:
    using Blocks = std::map<uint256, int64_t>;

    void stop();

    /** Set the single GUI consumer's wake-up callback, or clear it to detach.
     * Called under the cache mutex: it must only queue work, never read this
     * cache or invoke the consumer directly. Clearing it waits for any current
     * callback to return, so the consumer can then be safely destroyed.
     */
    void setChangedCallback(std::function<void()> callback);

    /** Discard timestamps before this boundary, retaining current/future blocks. */
    void setRetentionStart(qint64 retention_start);

    /** Return the startup snapshot boundary, or nullopt after stop(). */
    std::optional<qint64> retentionStart();

    /** Return changed cached timestamps, or nullopt if nothing has changed. */
    std::optional<QList<qint64>> takeSnapshot();

    /** Install the initial snapshot and reconcile notifications received meanwhile. */
    void initialize(Blocks blocks);

    void blockConnected(const kernel::ChainstateRole& role, const interfaces::BlockInfo& block) override;
    void blockDisconnected(const interfaces::BlockInfo& block) override;

private:
    void record(const uint256& hash, int64_t timestamp, bool connected);

    std::mutex m_mutex;
    std::function<void()> m_changed;
    Blocks m_blocks;
    // Only the last event for each hash matters when reconciling a snapshot.
    std::map<uint256, std::optional<int64_t>> m_pending;
    qint64 m_retention_start{0};
    bool m_initialized{false};
    bool m_dirty{false};
    bool m_stopped{false};
};

/**
 * Owns a registered handler separately from its receiver, without an ownership
 * cycle. Construct, disconnect and destroy on the initialization worker while
 * Core's notification system is alive. disconnect() is idempotent and does not
 * wait for the validation queue; late callbacks only retain the stopped cache.
 */
class BlockClockSubscription final : public interfaces::Handler
{
public:
    BlockClockSubscription(std::shared_ptr<BlockClockHistory> history, std::unique_ptr<interfaces::Handler> handler);
    ~BlockClockSubscription() override;
    void disconnect() override;

private:
    std::shared_ptr<BlockClockHistory> m_history;
    std::unique_ptr<interfaces::Handler> m_handler;
};

/**
 * Register before loading the initial snapshot, on the initialization worker.
 * The returned token must be released there before Core shutdown. Exceptions
 * and shutdown during the snapshot automatically stop and unregister the local
 * subscription. Live notifications never query the node or chain interfaces.
 */
std::unique_ptr<interfaces::Handler> SubscribeBlockClockHistory(
    interfaces::Node& node, interfaces::Chain& chain, std::shared_ptr<BlockClockHistory> history);

#endif // BITCOIN_QML_MODELS_BLOCKCLOCKHISTORY_H
