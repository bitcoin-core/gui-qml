// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/nodemodel.h>

#include <interfaces/node.h>
#include <validation.h>

#include <cassert>
#include <chrono>

#include <QTimerEvent>

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    ConnectToBlockTipSignal();
}

void NodeModel::setBlockTipHeight(int new_height)
{
    if (new_height != m_block_tip_height) {
        m_block_tip_height = new_height;
        Q_EMIT blockTipHeightChanged();
    }
}

void NodeModel::setVerificationProgress(double new_progress)
{
    if (new_progress != m_verification_progress) {
        m_verification_progress = new_progress;
        Q_EMIT verificationProgressChanged();
    }
}

void NodeModel::startNodeInitializionThread()
{
    Q_EMIT requestedInitialize();
}

void NodeModel::initializeResult([[maybe_unused]] bool success, interfaces::BlockAndHeaderTipInfo tip_info)
{
    // TODO: Handle the `success` parameter,
    setBlockTipHeight(tip_info.block_height);
    setVerificationProgress(tip_info.verification_progress);
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
            setBlockTipHeight(tip.block_height);
            setVerificationProgress(verification_progress);
        });
}
