// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodemodel.h>
#include <qml/models/snapshotqml.h>

#include <interfaces/node.h>
#include <net.h>
#include <node/interface_ui.h>
#include <validation.h>

#include <cassert>
#include <chrono>

#include <QDateTime>
#include <QMetaObject>
#include <QObject>
#include <QTimerEvent>
#include <QString>
#include <QUrl>
#include <QThread>
#include <QDebug>

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    ConnectToBlockTipSignal();
    ConnectToNumConnectionsChangedSignal();
    ConnectToSnapshotLoadProgressSignal();
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
    if (new_progress != m_verification_progress) {
        setRemainingSyncTime(new_progress);

        if (new_progress >= 0.00001) {
            setHeadersSynced(true);
        }

        if (new_progress >= 0.999) {
            setIsIBDCompleted(true);
        }

        m_verification_progress = new_progress;
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

bool NodeModel::validateProxyAddress(QString address_port)
{
    return m_node.validateProxyAddress(address_port.toStdString());
}

QString NodeModel::defaultProxyAddress()
{
    return QString::fromStdString(m_node.defaultProxyAddress());
}

void NodeModel::ConnectToSnapshotLoadProgressSignal()
{
    assert(!m_handler_snapshot_load_progress);

    m_handler_snapshot_load_progress = m_node.handleSnapshotLoadProgress(
        [this](double progress) {
            setSnapshotProgress(progress);
        });
}

void NodeModel::snapshotLoadThread(QString path_file) {
    m_snapshot_loading = true;
    Q_EMIT snapshotLoadingChanged();

    path_file = QUrl(path_file).toLocalFile();

    QThread* snapshot_thread = QThread::create([this, path_file]() {
        SnapshotQml loader(m_node, path_file);
        bool result = loader.processPath();
        if (!result) {
            m_snapshot_loading = false;
            Q_EMIT snapshotLoadingChanged();
        } else {
            m_snapshot_loaded = true;
            Q_EMIT snapshotLoaded(result);
            Q_EMIT snapshotLoadingChanged();
        }
    });

    connect(snapshot_thread, &QThread::finished, snapshot_thread, &QThread::deleteLater);

    snapshot_thread->start();
}

void NodeModel::setSnapshotProgress(double new_progress) {
    if (new_progress != m_snapshot_progress) {
        m_snapshot_progress = new_progress;
        Q_EMIT snapshotProgressChanged();
    }
}

void NodeModel::setHeadersSynced(bool new_synced) {
    if (new_synced != m_headers_synced) {
        m_headers_synced = new_synced;
        Q_EMIT headersSyncedChanged();
    }
}

void NodeModel::setIsIBDCompleted(bool new_completed) {
    if (new_completed != m_is_ibd_completed) {
        m_is_ibd_completed = new_completed;
        Q_EMIT isIBDCompletedChanged();
    }
}
