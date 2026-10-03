// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_INITEXECUTOR_H
#define BITCOIN_QML_INITEXECUTOR_H

#include <interfaces/node.h>
#include <qml/backendexecutor.h>

#include <exception>
#include <functional>
#include <memory>

#include <QObject>

QT_BEGIN_NAMESPACE
class QString;
QT_END_NAMESPACE

namespace interfaces {
class Handler;
}

/** Runs app initialization and shutdown work off the GUI thread. */
class QmlInitExecutor : public QObject
{
    Q_OBJECT
public:
    using SubscriptionFactory = std::function<std::unique_ptr<interfaces::Handler>()>;

    /** Acquire an optional subscription on the init worker after successful
     * initialization. Retire it there before appShutdown or owner destruction.
     * Keep borrowed node/chain interfaces alive through shutdownResult, or the
     * final BackendExecutor::shutdownAll barrier after owner destruction.
     */
    explicit QmlInitExecutor(interfaces::Node& node, SubscriptionFactory subscribe = {});
    ~QmlInitExecutor();

public Q_SLOTS:
    void initialize();
    /** Runs interruption independently of a potentially blocked initialize job. */
    void interrupt();
    void shutdown();

Q_SIGNALS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info,
                          bool initial_block_download, bool shutdown_requested);
    void interruptResult();
    void shutdownResult();
    void runawayException(const QString& message);

private:
    void handleRunawayException(std::exception_ptr error);
    void finishShutdown();

    struct WorkerState;
    interfaces::Node& m_node;
    std::shared_ptr<WorkerState> m_worker_state;
    BackendExecutor m_backend;
    BackendExecutor m_control;
    bool m_initialize_requested{false};
    bool m_interrupt_requested{false};
    bool m_shutdown_requested{false};
    bool m_shutdown_complete{false};
    bool m_shutdown_emitted{false};
    bool m_runaway_exception{false};
};

#endif // BITCOIN_QML_INITEXECUTOR_H
