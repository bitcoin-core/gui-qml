// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/chainsyncmodel.h>
#include <chainparams.h>
#include <validation.h>
#include <util/time.h>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <limits>

static constexpr int HEADER_HEIGHT_DELTA_SYNC{24};

ChainSyncModel::ChainSyncModel(interfaces::Node& node) : m_node{node}
{
    m_sample_clock.start();
    ConnectToBlockTipSignal();
    ConnectToHeaderTipSignal();
}

ChainSyncModel::~ChainSyncModel()
{
    stop();
}

void ChainSyncModel::stop()
{
    m_stopped = true;
    m_handler_notify_block_tip.reset();
    m_handler_notify_header_tip.reset();
}

void ChainSyncModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info)
{
    if (!success || m_stopped) return;
    setBlockTipHeight(tip_info.block_height);
    setVerificationProgress(tip_info.verification_progress);
    setBlockSyncActive(tip_info.block_height > 0 && m_node.isInitialBlockDownload());
    setHeaderSyncState(tip_info.header_height, tip_info.header_time, false);
    Q_EMIT setTimeRatioListInitial();
}

void ChainSyncModel::setRemainingSyncTime(double new_progress)
{
    const qint64 now{m_sample_clock.elapsed()};
    if (!m_block_process_time.empty() && new_progress < m_block_process_time.front().second) {
        m_block_process_time.clear();
    }
    m_block_process_time.push_front(qMakePair(now, new_progress));
    while (m_block_process_time.size() > 2 && m_block_process_time.back().first < now - 500'000) {
        m_block_process_time.removeLast();
    }
    if (m_block_process_time.size() > 5'000) m_block_process_time.removeLast();
    int remaining{0};
    if (m_block_process_time.size() >= 2) {
        const auto& sample{m_block_process_time.back()};
        const double progress_delta{new_progress - sample.second};
        if (progress_delta > 0.0 && now > sample.first) {
            const double estimate{(1.0 - new_progress) / progress_delta * (now - sample.first)};
            remaining = static_cast<int>(std::clamp(estimate, 0.0, static_cast<double>(std::numeric_limits<int>::max())));
        }
    }
    if (remaining != m_remaining_sync_time) {
        m_remaining_sync_time = remaining;
        Q_EMIT remainingSyncTimeChanged();
    }
}

void ChainSyncModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
    }
}

void ChainSyncModel::setVerificationProgress(double new_progress)
{
    if (!std::isfinite(new_progress)) return;
    new_progress = std::clamp(new_progress, 0.0, 1.0);
    if (new_progress != m_verification_progress) {
        setRemainingSyncTime(new_progress);

        m_verification_progress = new_progress;
        Q_EMIT verificationProgressChanged();
        Q_EMIT blockTipChanged();
    }
}

void ChainSyncModel::setBlockSyncActive(bool active)
{
    if (m_block_sync_active == active) {
        return;
    }

    m_block_sync_active = active;
    Q_EMIT blockSyncActiveChanged();
}

void ChainSyncModel::setHeaderSyncState(int height, int64_t block_time, bool presync)
{
    m_header_tip_height = height;
    m_header_tip_time = block_time;

    if (block_time <= 0) {
        const bool changed{m_header_sync_active || m_header_presync != presync || m_header_sync_progress != 0.0};
        m_header_sync_active = false;
        m_header_presync = presync;
        m_header_sync_progress = 0.0;
        if (changed) Q_EMIT headerSyncChanged();
        return;
    }

    const int64_t estimate_headers_left{(GetTime() - block_time) / Params().GetConsensus().nPowTargetSpacing};
    const bool active{height > 0 && estimate_headers_left > HEADER_HEIGHT_DELTA_SYNC};
    const double progress{active ? 1.0 * height / (height + estimate_headers_left) : 0.0};

    if (m_header_sync_active == active &&
        m_header_presync == presync &&
        m_header_sync_progress == progress) {
        return;
    }

    m_header_sync_active = active;
    m_header_presync = presync;
    m_header_sync_progress = progress;
    Q_EMIT headerSyncChanged();
}

void ChainSyncModel::ConnectToBlockTipSignal()
{
    assert(!m_handler_notify_block_tip);

    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(
        [this]([[maybe_unused]] SynchronizationState state, interfaces::BlockTip tip, double verification_progress) {
            QMetaObject::invokeMethod(this, [this, state, block_height = tip.block_height, block_time = tip.block_time, verification_progress] {
                if (m_stopped) return;
                setBlockTipHeight(block_height);
                setVerificationProgress(verification_progress);
                setBlockSyncActive(block_height > 0 && state != SynchronizationState::POST_INIT);

                Q_EMIT setTimeRatioList(block_time);
            }, Qt::QueuedConnection);
        });
}

void ChainSyncModel::ConnectToHeaderTipSignal()
{
    assert(!m_handler_notify_header_tip);

    m_handler_notify_header_tip = m_node.handleNotifyHeaderTip(
        [this]([[maybe_unused]] SynchronizationState state, interfaces::BlockTip tip, bool presync) {
            QMetaObject::invokeMethod(this, [this, block_height = tip.block_height, block_time = tip.block_time, presync] {
                if (m_stopped) return;
                setHeaderSyncState(block_height, block_time, presync);
            }, Qt::QueuedConnection);
        });
}
