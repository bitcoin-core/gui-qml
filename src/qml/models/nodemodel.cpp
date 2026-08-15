// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodemodel.h>

#include <interfaces/node.h>

#include <QMetaObject>

NodeModel::NodeModel(interfaces::Node& node)
    : m_node{node}
{
    m_handler_notify_block_tip = m_node.handleNotifyBlockTip(
        [this](SynchronizationState, interfaces::BlockTip tip, double verification_progress) {
            QMetaObject::invokeMethod(this, [this, height = tip.block_height, verification_progress] {
                if (m_state == RUNNING) setBlockTip(height, verification_progress);
            }, Qt::QueuedConnection);
        });
    m_node_poll_timer.setInterval(200);
    connect(&m_node_poll_timer, &QTimer::timeout, this, [this] {
        if (m_node.shutdownRequested()) {
            requestShutdown();
            return;
        }
    });
    m_node_poll_timer.start();
}

QString NodeModel::statusText() const
{
    switch (m_state) {
    case IDLE:
        return tr("Waiting to start");
    case INITIALIZING:
        return tr("Starting node");
    case RUNNING:
        return tr("Node is running");
    case FAILED:
        return m_error_message.isEmpty() ? tr("Node initialization failed") : m_error_message;
    case SHUTTING_DOWN:
        return tr("Shutting down");
    case STOPPED:
        return tr("Node stopped");
    }
    return {};
}

void NodeModel::setState(State state)
{
    if (m_state == state) {
        return;
    }
    m_state = state;
    Q_EMIT stateChanged();
    Q_EMIT statusTextChanged();
}

void NodeModel::setErrorMessage(const QString& message)
{
    if (m_error_message == message) {
        return;
    }
    m_error_message = message;
    Q_EMIT errorMessageChanged();
    Q_EMIT statusTextChanged();
}

void NodeModel::setBlockTip(int height, double verification_progress)
{
    if (m_block_tip_height == height && m_verification_progress == verification_progress) {
        return;
    }
    m_block_tip_height = height;
    m_verification_progress = verification_progress;
    Q_EMIT blockTipChanged();
}

void NodeModel::start()
{
    if (m_state != IDLE) {
        return;
    }
    setState(INITIALIZING);
    Q_EMIT requestedInitialize();
}

void NodeModel::requestShutdown()
{
    if (m_state == SHUTTING_DOWN || m_state == STOPPED) {
        return;
    }
    setState(SHUTTING_DOWN);
    m_node_poll_timer.stop();
    m_node.startShutdown();
    Q_EMIT requestedShutdown();
}

void NodeModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info)
{
    if (m_state == SHUTTING_DOWN || m_node.shutdownRequested()) {
        requestShutdown();
        return;
    }

    if (!success) {
        setErrorMessage(tr("Node initialization failed"));
        setState(FAILED);
        Q_EMIT initializationFinished(false);
        return;
    }

    setBlockTip(tip_info.block_height, tip_info.verification_progress);
    setState(RUNNING);
    Q_EMIT initializationFinished(true);
}

void NodeModel::shutdownResult()
{
    setState(STOPPED);
    Q_EMIT shutdownComplete();
}

void NodeModel::handleRunawayException(const QString& message)
{
    setErrorMessage(message.isEmpty() ? tr("A fatal node error occurred") : message);
    setState(FAILED);
}
