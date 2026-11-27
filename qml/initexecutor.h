// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_INITEXECUTOR_H
#define BITCOIN_QML_INITEXECUTOR_H

#include <interfaces/node.h>
#include <qml/executor.h>
#include <qml/nodelifecycleworker.h>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <thread>
#include <QObject>

namespace interfaces { class Handler; }
/** Qt projection and native adapter for the node lifecycle.
 * Node outlives the physical worker drain; GUI never joins or calls Core. */
class QmlInitExecutor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(nodelifecycle::Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool acceptingRequests READ acceptingRequests NOTIFY acceptingRequestsChanged)
    Q_PROPERTY(bool initializationAttempted READ initializationAttempted NOTIFY initializationChanged)
    Q_PROPERTY(bool initializationSucceeded READ initializationSucceeded NOTIFY initializationChanged)
    Q_PROPERTY(bool initializationCancelled READ initializationCancelled NOTIFY initializationChanged)
public:
    enum class StartupOutcome { Ready, Failed, Cancelled, Uncertain };
    using StartupOutcomePort=std::function<void(StartupOutcome)>;
    using ShutdownOutcomePort=std::function<void(int)>;
    using SubscriptionFactory=std::function<std::unique_ptr<interfaces::Handler>()>;
    QmlInitExecutor(interfaces::Node& node, std::shared_ptr<execution::SerialQueue> priority,
        SubscriptionFactory subscribe={},
        std::shared_ptr<std::atomic_bool> diagnostic_loss={},StartupOutcomePort outcome={},
        ShutdownOutcomePort shutdown_outcome={},std::shared_ptr<std::atomic_bool> lifecycle_failed={});
    ~QmlInitExecutor() override;
    nodelifecycle::Phase phase() const { return m_phase; }
    // Constructed/Initializing may accept bounded intents for later readiness.
    // A stop/failure closes ingress immediately; it never reopens this owner.
    bool acceptingRequests() const { return !m_closed->load(); }
    std::shared_ptr<const std::atomic_bool> closedFlag() const { return m_closed; }
    const std::optional<nodelifecycle::NativeInitialization>& initializationOutcome() const { return m_initialization; }
    bool initializationAttempted() const { return m_initialization && m_initialization->attempted; }
    bool initializationSucceeded() const { return m_initialization && m_initialization->success; }
    bool initializationCancelled() const { return m_initialization && m_initialization->cancelled; }
public Q_SLOTS:
    void close();
    void initialize();
    void interrupt();
    void shutdown();
Q_SIGNALS:
    void initializeResult(bool success,interfaces::BlockAndHeaderTipInfo tip_info,bool initial_block_download,bool shutdown_requested);
    void interruptResult();
    void shutdownResult();
    void runawayException(const QString& message);
    void phaseChanged();
    void acceptingRequestsChanged();
    void initializationChanged();
private:
    void publish(nodelifecycle::Projection projection);
    void handleRunawayException(const QString& message);
    void diagnosticTransportFailed();
    void joinWorker();
    void finishShutdown();
    void publishAcceptingRequests();
    std::shared_ptr<std::atomic_bool> m_diagnostic_loss,m_lifecycle_failed;
    const std::shared_ptr<std::atomic_bool> m_closed{std::make_shared<std::atomic_bool>(false)};
    std::thread m_thread;
    std::unique_ptr<nodelifecycle::Driver> m_driver;
    nodelifecycle::Phase m_phase{nodelifecycle::Phase::Constructed};
    std::optional<nodelifecycle::NativeInitialization> m_initialization;
    // Ingress idempotence and physical-delivery receipts, never Core outcomes.
    bool m_initialize_requested{false},m_interrupt_requested{false},m_shutdown_requested{false};
    bool m_app_shutdown_complete{false},m_shutdown_result_emitted{false},m_runaway_exception{false};
    bool m_accepting_projection{true};
};
#endif // BITCOIN_QML_INITEXECUTOR_H
