// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodemodel.h>

#include <interfaces/node.h>
#include <net.h>
#include <node/interface_ui.h>
#include <validation.h>

#include <cassert>
#include <chrono>

#include <QDateTime>
#include <QMetaObject>
#include <QTimerEvent>

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    ConnectToBlockTipSignal();
    ConnectToHeaderTipSignal();
    ConnectToNumConnectionsChangedSignal();
}

void NodeModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
    }
}

void NodeModel::setNumOutboundPeers(int new_num)
{
    if (new_num != m_num_outbound_peers) {
        m_num_outbound_peers = new_num;
        Q_EMIT numOutboundPeersChanged();
    }
}

void NodeModel::setInHeaderSync(bool new_in_header_sync)
{
    if (new_in_header_sync != m_in_header_sync) {
        m_in_header_sync = new_in_header_sync;
        Q_EMIT inHeaderSyncChanged();
    }
}

void NodeModel::setHeaderSyncProgress(int64_t header_height, const QDateTime& block_date)
{
    int estimated_headers_left = block_date.secsTo(QDateTime::currentDateTime()) / Params().GetConsensus().nPowTargetSpacing;
    double new_header_sync_progress = (100.0 / (header_height + estimated_headers_left) * header_height) / 10000;

    if (new_header_sync_progress != m_header_sync_progress) {
        m_header_sync_progress = new_header_sync_progress;
        setVerificationProgress(0.0);
        Q_EMIT headerSyncProgressChanged();
    }
}

void NodeModel::setInPreHeaderSync(bool new_in_pre_header_sync)
{
    if (new_in_pre_header_sync != m_in_pre_header_sync) {
        m_in_pre_header_sync = new_in_pre_header_sync;
        Q_EMIT inPreHeaderSyncChanged();
    }
}

void NodeModel::setPreHeaderSyncProgress(int64_t header_height, const QDateTime& block_date)
{
    int estimated_headers_left = block_date.secsTo(QDateTime::currentDateTime()) / Params().GetConsensus().nPowTargetSpacing;
    double new_pre_header_sync_progress = (100.0 / (header_height + estimated_headers_left) * header_height) / 10000;

    if (new_pre_header_sync_progress != m_pre_header_sync_progress) {
        m_pre_header_sync_progress = new_pre_header_sync_progress;
        setVerificationProgress(0.0);
        Q_EMIT preHeaderSyncProgressChanged();

    }
}

void NodeModel::setRemainingSyncTime(double new_progress)
{
    int currentTime = QDateTime::currentDateTime().toMSecsSinceEpoch();

    // keep a vector of samples of verification progress at height
    m_block_process_time.push_front(qMakePair(currentTime, new_progress));

    // show progress speed if we have more than one sample
    if (m_block_process_time.size() >= 2) {
        double progressDelta = 0;
        int timeDelta = 0;
        int remainingMSecs = 0;
        double remainingProgress = 1.0 - new_progress;
        for (int i = 1; i < m_block_process_time.size(); i++) {
            QPair<int, double> sample = m_block_process_time[i];

            // take first sample after 500 seconds or last available one
            if (sample.first < (currentTime - 500 * 1000) || i == m_block_process_time.size() - 1) {
                progressDelta = m_block_process_time[0].second - sample.second;
                timeDelta = m_block_process_time[0].first - sample.first;
                remainingMSecs = (progressDelta > 0) ? remainingProgress / progressDelta * timeDelta : -1;
                break;
            }
        }
        if (remainingMSecs > 0 && m_block_process_time.count() % 1000 == 0) {
            m_remaining_sync_time = remainingMSecs;

            Q_EMIT remainingSyncTimeChanged();
        }
        static const int MAX_SAMPLES = 5000;
        if (m_block_process_time.count() > MAX_SAMPLES) {
            m_block_process_time.remove(1, m_block_process_time.count() - 1);
        }
    }
}

void NodeModel::setVerificationProgress(double new_progress)
{
    double header_progress = m_header_sync_progress + m_pre_header_sync_progress;
    if (!m_in_header_sync && !m_in_pre_header_sync) {
       if (new_progress != m_verification_progress) {
            setRemainingSyncTime(new_progress);

            m_verification_progress = header_progress + (new_progress - header_progress);
            Q_EMIT verificationProgressChanged();
        }
    } else {
        m_verification_progress = header_progress;
        Q_EMIT verificationProgressChanged();
    }
}

void NodeModel::setPause(bool new_pause)
{
    if(m_pause != new_pause) {
        m_pause = new_pause;
        m_node.setNetworkActive(!new_pause);
        Q_EMIT pauseChanged(new_pause);
    }
}

void NodeModel::startNodeInitializionThread()
{
    Q_EMIT requestedInitialize();
}

void NodeModel::requestShutdown()
{
    Q_EMIT requestedShutdown();
}

void NodeModel::initializeResult([[maybe_unused]] bool success, interfaces::BlockAndHeaderTipInfo tip_info)
{
    // TODO: Handle the `success` parameter,
    setBlockTipHeight(tip_info.block_height);
    setVerificationProgress(tip_info.verification_progress);

    Q_EMIT setTimeRatioListInitial();
}

void NodeModel::startShutdownPolling()
{
    m_shutdown_polling_timer_id = startTimer(200ms);
}

void NodeModel::stopShutdownPolling()
{
    killTimer(m_shutdown_polling_timer_id);
}

void NodeModel::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event)
    if (m_node.shutdownRequested()) {
        stopShutdownPolling();
        Q_EMIT requestedShutdown();
    }
}

void NodeModel::ConnectToBlockTipSignal()
{
    assert(!m_handler_notify_block_tip);

    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(
        [this](SynchronizationState state, interfaces::BlockTip tip, double verification_progress) {
            QMetaObject::invokeMethod(this, [=] {
                setBlockTipHeight(tip.block_height);
                setVerificationProgress(verification_progress);
                setInHeaderSync(false);
                setInPreHeaderSync(false);
                Q_EMIT setTimeRatioList(tip.block_time);
            });
        });
}

void NodeModel::ConnectToHeaderTipSignal()
{
    assert(!m_handler_notify_header_tip);

    m_handler_notify_header_tip = m_node.handleNotifyHeaderTip(
        [this](SynchronizationState sync_state, interfaces::BlockTip tip, bool presync) {
            QMetaObject::invokeMethod(this, [=] {
                if (presync) {
                    setInHeaderSync(false);
                    setInPreHeaderSync(true);
                    setPreHeaderSyncProgress(tip.block_height, QDateTime::fromSecsSinceEpoch(tip.block_time));
                } else {
                    setInHeaderSync(true);
                    setInPreHeaderSync(false);
                    setHeaderSyncProgress(tip.block_height, QDateTime::fromSecsSinceEpoch(tip.block_time));
                }
            });
        });
}

void NodeModel::ConnectToNumConnectionsChangedSignal()
{
    assert(!m_handler_notify_num_peers_changed);

    m_handler_notify_num_peers_changed = m_node.handleNotifyNumConnectionsChanged(
        [this](PeersNumByType new_num_peers) {
            setNumOutboundPeers(new_num_peers.outbound_full_relay + new_num_peers.block_relay);
        });
}
