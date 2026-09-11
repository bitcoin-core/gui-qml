// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/nodelifecyclemodel.h>
#include <qml/models/runtimedialogmodel.h>
#include <QTimerEvent>
#include <chrono>
using namespace std::chrono_literals;

NodeLifecycleModel::NodeLifecycleModel(interfaces::Node& node, RuntimeDialogModel* dialogs)
    : m_node{node}, m_dialogs{dialogs}
{
    startShutdownPolling();
}

NodeLifecycleModel::~NodeLifecycleModel()
{
    stopShutdownPolling();
}

QString NodeLifecycleModel::statusText() const
{
    switch (m_state) {
    case IDLE:
        return tr("Waiting to start");
    case INITIALIZING:
        return tr("Starting node");
    case RUNNING:
        return tr("Node is running");
    case FAILED:
        return m_startup_error.isEmpty() ? tr("Node initialization failed") : m_startup_error;
    case SHUTTING_DOWN:
        return tr("Shutting down");
    case STOPPED:
        return tr("Node stopped");
    }
    return {};
}

void NodeLifecycleModel::setState(State state)
{
    if (m_state == state) return;
    m_state = state;
    Q_EMIT stateChanged();
    Q_EMIT statusTextChanged();
}

void NodeLifecycleModel::setErrorState(bool faulted)
{
    if (m_faulted != faulted) {
        m_faulted = faulted;
        Q_EMIT errorStateChanged(faulted);
    }
}

void NodeLifecycleModel::setStartupError(const QString& error)
{
    if (m_startup_error != error) {
        m_startup_error = error;
        Q_EMIT startupErrorChanged();
        Q_EMIT errorMessageChanged();
        Q_EMIT statusTextChanged();
    }
}

void NodeLifecycleModel::start()
{
    if (m_initialization_requested || m_shutdown_requested) {
        return;
    }
    m_initialization_requested = true;
    setState(INITIALIZING);
    Q_EMIT requestedInitialize();
}

void NodeLifecycleModel::requestShutdown()
{
    if (m_shutdown_requested) {
        return;
    }
    m_shutdown_requested = true;
    setState(SHUTTING_DOWN);
    stopShutdownPolling();
    if (m_dialogs) m_dialogs->stop();
    m_node.startShutdown();
    Q_EMIT requestedShutdown();
}

void NodeLifecycleModel::handleRunawayException(const QString& message)
{
    setErrorState(true);
    setStartupError(message.isEmpty() ? tr("A fatal node error occurred.") : message);
    setState(FAILED);
}

void NodeLifecycleModel::shutdownResult()
{
    setState(STOPPED);
    Q_EMIT shutdownComplete();
}

void NodeLifecycleModel::startShutdownPolling()
{
    if (m_shutdown_polling_timer_id != 0) {
        return;
    }
    m_shutdown_polling_timer_id = startTimer(200ms);
}

void NodeLifecycleModel::stopShutdownPolling()
{
    if (m_shutdown_polling_timer_id == 0) {
        return;
    }
    killTimer(m_shutdown_polling_timer_id);
    m_shutdown_polling_timer_id = 0;
}

void NodeLifecycleModel::timerEvent(QTimerEvent* event)
{
    if (event->timerId() != m_shutdown_polling_timer_id) {
        return;
    }
    if (m_node.shutdownRequested()) {
        requestShutdown();
    }
}

void NodeLifecycleModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo)
{
    if (m_shutdown_requested || m_node.shutdownRequested()) {
        requestShutdown();
        return;
    }
    if (m_dialogs) m_dialogs->completeInitialization(success);
    if (!success) {
        if (m_dialogs && m_dialogs->startupFailureDialogShown()) {
            requestShutdown();
            return;
        }
        setErrorState(true);
        setStartupError(m_dialogs ? m_dialogs->startupErrorSummary() : tr("Node initialization failed."));
        setState(FAILED);
    } else {
        setState(RUNNING);
        Q_EMIT nodeInitialized();
    }
    Q_EMIT initializationFinished(success);
}
