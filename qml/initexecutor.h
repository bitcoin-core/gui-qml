// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_INITEXECUTOR_H
#define BITCOIN_QML_INITEXECUTOR_H

#include <interfaces/node.h>
#include <qml/backendexecutor.h>
#include <qml/nodelifecycleworker.h>
#include <atomic>
#include <exception>
#include <functional>
#include <memory>
#include <QObject>

namespace interfaces { class Handler; }
/** Qt projection and Native adapter for the Domain-owned lifecycle driver.
 * Node outlives the physical worker drain; GUI never joins or calls Core. */
class QmlInitExecutor : public QObject
{
    Q_OBJECT
    Q_PROPERTY(nodelifecycle::Phase phase READ phase NOTIFY phaseChanged)
    Q_PROPERTY(bool admissionOpen READ admissionOpen NOTIFY admissionChanged)
    Q_PROPERTY(bool initializationAttempted READ initializationAttempted NOTIFY initializationChanged)
    Q_PROPERTY(bool initializationSucceeded READ initializationSucceeded NOTIFY initializationChanged)
    Q_PROPERTY(bool initializationCancelled READ initializationCancelled NOTIFY initializationChanged)
public:
    enum class StartupOutcome { Ready, Failed, Cancelled, Uncertain };
    using StartupOutcomePort=std::function<void(StartupOutcome)>;
    using ShutdownOutcomePort=std::function<void(int)>;
    using SubscriptionFactory=std::function<std::unique_ptr<interfaces::Handler>()>;
    explicit QmlInitExecutor(interfaces::Node& node,SubscriptionFactory subscribe={},
        std::shared_ptr<std::atomic_bool> diagnostic_loss={},StartupOutcomePort outcome={},
        ShutdownOutcomePort shutdown_outcome={},std::shared_ptr<std::atomic_bool> lifecycle_failed={});
    ~QmlInitExecutor() override;
    std::shared_ptr<execution::Scheduler> fatalDiagnosticScheduler() const { return m_fatal_diagnostic_scheduler; }
    nodelifecycle::Phase phase() const { return m_phase; }
    // Constructed/Initializing may accept bounded intents for later readiness.
    // A stop/failure closes ingress immediately; it never reopens this owner.
    bool admissionOpen() const { return !m_admission_closed->load(); }
    std::shared_ptr<const std::atomic_bool> admissionClosedGate() const { return m_admission_closed; }
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
    void admissionChanged();
    void initializationChanged();
private:
    void publish(nodelifecycle::Projection projection);
    void handleRunawayException(const QString& message);
    void diagnosticTransportFailed(std::shared_ptr<execution::Scheduler> original_diagnostic_scope={});
    void finishShutdown();
    void publishAdmission();
    std::shared_ptr<std::atomic_bool> m_diagnostic_loss,m_lifecycle_failed;
    const std::shared_ptr<std::atomic_bool> m_admission_closed{std::make_shared<std::atomic_bool>(false)};
    BackendExecutor m_executor;
    std::unique_ptr<nodelifecycle::Driver> m_driver;
    std::shared_ptr<execution::Scheduler> m_fatal_diagnostic_scheduler;
    nodelifecycle::Phase m_phase{nodelifecycle::Phase::Constructed};
    std::optional<nodelifecycle::NativeInitialization> m_initialization;
    // Ingress idempotence and physical-delivery receipts, never Core outcomes.
    bool m_initialize_requested{false},m_interrupt_requested{false},m_shutdown_requested{false};
    bool m_app_shutdown_complete{false},m_shutdown_result_emitted{false},m_runaway_exception{false};
    bool m_admission_projection_open{true};
};
#endif // BITCOIN_QML_INITEXECUTOR_H
