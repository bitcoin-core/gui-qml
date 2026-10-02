// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockclockhistory.h>

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <kernel/chain.h>
#include <kernel/types.h>
#include <primitives/block.h>

#include <cassert>
#include <utility>

std::unique_ptr<interfaces::Handler> SubscribeBlockClockHistory(
    interfaces::Node& node, interfaces::Chain& chain, std::shared_ptr<BlockClockHistory> history)
{
    const auto retention_start{history->retentionStart()};
    if (!retention_start || node.shutdownRequested()) return {};

    // The local RAII token owns registration throughout snapshot loading, so
    // failure or cancellation cannot leave a registered receiver behind.
    auto subscription{std::make_unique<BlockClockSubscription>(history, chain.handleNotifications(history))};
    const uint256 anchor{node.getBestBlockHash()};
    uint256 hash{anchor};
    int height{0};
    int64_t timestamp{0};
    int64_t max_time{0};
    BlockClockHistory::Blocks blocks;
    if (chain.findBlock(anchor, interfaces::FoundBlock{}.height(height).time(timestamp).maxTime(max_time))) {
        // Follow ancestors of one immutable hash. Separate active-height reads
        // can race a shortening reorg, and block times are not monotonic.
        while (max_time >= *retention_start) {
            if (node.shutdownRequested()) return {};
            if (timestamp >= *retention_start) blocks.emplace(hash, timestamp);
            if (--height < 0) break;
            if (!chain.findAncestorByHeight(anchor, height, interfaces::FoundBlock{}.hash(hash).time(timestamp).maxTime(max_time))) break;
        }
    }
    if (node.shutdownRequested()) return {};
    history->initialize(std::move(blocks));
    return subscription;
}

BlockClockSubscription::BlockClockSubscription(
    std::shared_ptr<BlockClockHistory> history, std::unique_ptr<interfaces::Handler> handler)
    : m_history{std::move(history)}, m_handler{std::move(handler)}
{
    assert(m_history);
    assert(m_handler);
}

BlockClockSubscription::~BlockClockSubscription()
{
    disconnect();
}

void BlockClockSubscription::disconnect()
{
    if (!m_history) return;
    m_history->stop();
    // Do not hold the cache mutex across Core's unregister operation. Core
    // retains in-flight receivers, which have no reference back to this token.
    m_handler.reset();
    m_history.reset();
}

void BlockClockHistory::stop()
{
    std::lock_guard lock{m_mutex};
    m_stopped = true;
    m_pending.clear();
    m_changed = {};
}

void BlockClockHistory::setChangedCallback(std::function<void()> callback)
{
    std::lock_guard lock{m_mutex};
    if (m_stopped) return;
    m_changed = std::move(callback);
    if (m_changed && m_initialized && m_dirty) m_changed();
}

std::optional<qint64> BlockClockHistory::retentionStart()
{
    std::lock_guard lock{m_mutex};
    if (m_stopped) return std::nullopt;
    return m_retention_start;
}

void BlockClockHistory::setRetentionStart(qint64 retention_start)
{
    std::lock_guard lock{m_mutex};
    if (m_stopped || m_retention_start == retention_start) return;
    m_retention_start = retention_start;
    std::erase_if(m_blocks, [retention_start](const auto& item) { return item.second < retention_start; });
    std::erase_if(m_pending, [retention_start](const auto& item) {
        return item.second && *item.second < retention_start;
    });
    m_dirty = true;
}

std::optional<QList<qint64>> BlockClockHistory::takeSnapshot()
{
    std::lock_guard lock{m_mutex};
    if (m_stopped || !m_initialized || !m_dirty) return std::nullopt;
    QList<qint64> timestamps;
    timestamps.reserve(m_blocks.size());
    for (const auto& [hash, timestamp] : m_blocks) timestamps.push_back(timestamp);
    m_dirty = false;
    return timestamps;
}

void BlockClockHistory::initialize(Blocks blocks)
{
    std::lock_guard lock{m_mutex};
    if (m_stopped || m_initialized) return;
    // Applying the last connect/disconnect for each hash is idempotent even if
    // its effect was already included in the snapshot. Equal timestamps still
    // represent distinct blocks, and a reorg removes only the disconnected hash.
    for (const auto& [hash, timestamp] : m_pending) {
        if (timestamp) {
            blocks.insert_or_assign(hash, *timestamp);
        } else {
            blocks.erase(hash);
        }
    }
    std::erase_if(blocks, [this](const auto& item) { return item.second < m_retention_start; });
    m_blocks = std::move(blocks);
    m_pending.clear();
    m_initialized = true;
    m_dirty = true;
    if (m_changed) m_changed();
}

void BlockClockHistory::blockConnected(const kernel::ChainstateRole& role, const interfaces::BlockInfo& block)
{
    if (role.historical) return;
    assert(block.data);
    record(block.hash, block.data->GetBlockTime(), /*connected=*/true);
}

void BlockClockHistory::blockDisconnected(const interfaces::BlockInfo& block)
{
    assert(block.data);
    record(block.hash, block.data->GetBlockTime(), /*connected=*/false);
}

void BlockClockHistory::record(const uint256& hash, int64_t timestamp, bool connected)
{
    std::lock_guard lock{m_mutex};
    if (m_stopped || timestamp < m_retention_start) return;
    if (!m_initialized) {
        m_pending.insert_or_assign(hash, connected ? std::optional{timestamp} : std::nullopt);
        return;
    }
    const bool changed{connected ? m_blocks.emplace(hash, timestamp).second : m_blocks.erase(hash) != 0};
    if (!changed) return;
    m_dirty = true;
    if (m_changed) m_changed();
}
