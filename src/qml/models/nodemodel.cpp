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
#include <QString>

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    ConnectToBlockTipSignal();
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

        bool new_connected = (m_num_outbound_peers > 0);
        if (new_connected != m_connected) {
            setConnected(new_connected);
        }

        Q_EMIT numOutboundPeersChanged();
    }
}

void NodeModel::setConnected(bool new_connected)
{
    if (new_connected != m_connected) {
        m_connected = new_connected;
        Q_EMIT connectedChanged();
    }
}

void NodeModel::setEstimatingSyncTime(bool new_estimating)
{
    if (m_estimating_sync_time != new_estimating) {
        m_estimating_sync_time = new_estimating;
        Q_EMIT estimatingSyncTimeChanged();
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
            setFormattedRemainingSyncTime(m_remaining_sync_time);

            Q_EMIT remainingSyncTimeChanged();
        }
        static const int MAX_SAMPLES = 5000;
        if (m_block_process_time.count() > MAX_SAMPLES) {
            m_block_process_time.remove(1, m_block_process_time.count() - 1);
        }
    } else {
        m_remaining_sync_time = 0;
        setFormattedRemainingSyncTime(m_remaining_sync_time);
        
        Q_EMIT remainingSyncTimeChanged();
    }
}

void NodeModel::setFormattedRemainingSyncTime(int new_time)
{
    int minutes = new_time / 60000;
    int seconds = (new_time % 60000) / 1000;

    int weeks = minutes / 10080;
    minutes %= 10080;

    int days = minutes / 1440;
    minutes %= 1440;

    int hours = minutes / 60;
    minutes %= 60;

    if (weeks > 0) {
        m_formatted_remaining_sync_time = QObject::tr("~%1 %2 left")
            .arg(weeks)
            .arg(weeks == 1 ? QObject::tr("week") : QObject::tr("weeks"));
        setEstimatingSyncTime(false);
    } else if (days > 0) {
        m_formatted_remaining_sync_time = QObject::tr("~%1 %2 left")
            .arg(days)
            .arg(days == 1 ? QObject::tr("day") : QObject::tr("days"));
        setEstimatingSyncTime(false);
    } else if (hours >= 5) {
        m_formatted_remaining_sync_time = QObject::tr("~%1 %2 left")
            .arg(hours)
            .arg(hours == 1 ? QObject::tr("hour") : QObject::tr("hours"));
        setEstimatingSyncTime(false);
    } else if (hours > 0) {
        m_formatted_remaining_sync_time = QObject::tr("~%1h %2m left")
            .arg(hours)
            .arg(minutes);
        setEstimatingSyncTime(false);
    } else if (minutes >= 5) {
        m_formatted_remaining_sync_time = QObject::tr("~%1 %2 left")
            .arg(minutes)
            .arg(minutes == 1 ? QObject::tr("minute") : QObject::tr("minutes"));
        setEstimatingSyncTime(false);
    } else if (minutes > 0) {
        m_formatted_remaining_sync_time = QObject::tr("~%1m %2s left")
            .arg(minutes)
            .arg(seconds);
        setEstimatingSyncTime(false);
    } else if (seconds > 0) {
        m_formatted_remaining_sync_time = QObject::tr("~%1 %2 left")
            .arg(seconds)
            .arg(seconds == 1 ? QObject::tr("second") : QObject::tr("seconds"));
        setEstimatingSyncTime(false);
    } else {
        if (m_synced) {
            setEstimatingSyncTime(false);
        } else {
            m_formatted_remaining_sync_time = QObject::tr("Estimating");
            setEstimatingSyncTime(true);
        }
    }
}

void NodeModel::setVerificationProgress(double new_progress)
{
    if (new_progress != m_verification_progress) {
        setRemainingSyncTime(new_progress);
        setFormattedVerificationProgress(new_progress);

        if (new_progress >= 0.999) {
            setSynced(true);
        }

        m_verification_progress = new_progress;
        Q_EMIT verificationProgressChanged();
    }
}

void NodeModel::setFormattedVerificationProgress(double new_progress)
{
    double percentage = new_progress * 100.00;

    if (percentage >= 99.99) {
        m_formatted_verification_progress = QStringLiteral("100%"); 
    } else if (percentage == 0.0) {
        m_formatted_verification_progress = QStringLiteral("0%");
    } else {
        int decimal_places = (percentage >= 10.0) ? 1 : 2;
        m_formatted_verification_progress = QString::number(percentage, 'f', decimal_places) + '%';
    }
}

void NodeModel::setSynced(bool new_synced)
{
    if (m_synced != new_synced) {
        m_synced = new_synced;
        Q_EMIT syncedChanged();
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

void NodeModel::setErrorState(bool faulted)
{
    if (m_faulted != faulted) {
        m_faulted = faulted;
        Q_EMIT errorStateChanged(faulted);
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

void NodeModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info)
{
    if (!success) {
        setErrorState(true);
    }
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

                Q_EMIT setTimeRatioList(tip.block_time);
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
