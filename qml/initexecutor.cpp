// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/initexecutor.h>

#include <interfaces/node.h>
#include <interfaces/handler.h>
#include <util/threadnames.h>

#include <QString>

#include <atomic>
#include <utility>

struct QmlInitExecutor::WorkerState {
    SubscriptionFactory pending_subscription_factory;
    std::unique_ptr<interfaces::Handler> subscription;
    std::atomic_bool stopping{false};

    explicit WorkerState(SubscriptionFactory subscribe) : pending_subscription_factory(std::move(subscribe)) {}
    void retireSubscription()
    {
        subscription.reset();
        pending_subscription_factory = {};
    }
};

QmlInitExecutor::QmlInitExecutor(interfaces::Node& node, SubscriptionFactory subscribe)
    : QObject(), m_node(node), m_worker_state(std::make_shared<WorkerState>(std::move(subscribe)))
{
    connect(&m_init_and_shutdown_executor, &BackendExecutor::drained, this, &QmlInitExecutor::finishShutdown);
    connect(&m_interrupt_executor, &BackendExecutor::drained, this, &QmlInitExecutor::finishShutdown);
}

QmlInitExecutor::~QmlInitExecutor()
{
    m_worker_state->stopping = true;
    m_init_and_shutdown_executor.submit(this, [state = std::move(m_worker_state)] { state->retireSubscription(); }, [] {});
}

void QmlInitExecutor::handleRunawayException(std::exception_ptr error)
{
    m_runaway_exception = true;
    QString message{tr("Unknown exception")};
    try {
        std::rethrow_exception(error);
    } catch (const std::exception& e) {
        message = QString::fromUtf8(e.what());
    } catch (...) {
    }
    Q_EMIT runawayException(message);
}

void QmlInitExecutor::initialize()
{
    if (m_initialize_requested || m_shutdown_requested || m_runaway_exception) return;
    m_initialize_requested = true;
    struct Result {
        bool success;
        interfaces::BlockAndHeaderTipInfo tip;
        bool initial_block_download;
        bool shutdown_requested;
    };
    m_init_and_shutdown_executor.submit(this, [node = &m_node, state = m_worker_state] {
        util::ThreadRename("qml-init");
        Result result{};
        result.success = node->appInitMain(&result.tip);
        if (result.success && state->pending_subscription_factory && !state->stopping && !node->shutdownRequested()) {
            auto subscribe_once{std::exchange(state->pending_subscription_factory, {})};
            state->subscription = subscribe_once();
        }
        result.initial_block_download = result.success && node->isInitialBlockDownload();
        result.shutdown_requested = node->shutdownRequested();
        return result;
    }, [this](Result result) {
        if (!m_runaway_exception) {
            Q_EMIT initializeResult(result.success, result.tip, result.initial_block_download, result.shutdown_requested);
        }
    }, [this](std::exception_ptr error) { handleRunawayException(error); });
}

void QmlInitExecutor::interrupt()
{
    if (m_interrupt_requested || m_runaway_exception) return;
    m_interrupt_requested = true;
    m_worker_state->stopping = true;
    m_interrupt_executor.submit(this, [node = &m_node] {
        util::ThreadRename("qml-control");
        node->startShutdown();
    }, [this] {
        if (!m_runaway_exception) Q_EMIT interruptResult();
    }, [this](std::exception_ptr error) {
        handleRunawayException(error);
    });
}

void QmlInitExecutor::shutdown()
{
    if (m_shutdown_requested || m_runaway_exception) return;
    m_shutdown_requested = true;
    m_worker_state->stopping = true;
    m_init_and_shutdown_executor.submit(this, [node = &m_node, state = m_worker_state] {
        state->retireSubscription();
        node->appShutdown();
    }, [this] {
        m_app_shutdown_complete = true;
        m_init_and_shutdown_executor.shutdown();
        m_interrupt_executor.shutdown();
    }, [this](std::exception_ptr error) { handleRunawayException(error); });
}

void QmlInitExecutor::finishShutdown()
{
    if (m_runaway_exception || !m_app_shutdown_complete || m_shutdown_result_emitted) return;
    if (!m_init_and_shutdown_executor.isDrained() || !m_interrupt_executor.isDrained()) return;
    m_shutdown_result_emitted = true;
    Q_EMIT shutdownResult();
}
