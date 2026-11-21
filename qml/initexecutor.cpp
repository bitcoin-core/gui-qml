// Copyright (c) 2014-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/initexecutor.h>
#include <qml/guipresentation.h>
#ifdef ENABLE_TEST_AUTOMATION
#include <common/args.h>
#endif
#include <interfaces/handler.h>
#include <node/interface_ui.h>
#include <util/exception.h>
#include <util/threadnames.h>
#include <QPointer>
#include <QScopeGuard>
#include <cstring>
#include <stdexcept>
#include <utility>

namespace {
struct NativeLifecycle {
    QmlInitExecutor::SubscriptionFactory pending_subscription_factory;
    std::unique_ptr<interfaces::Handler> subscription;
    QmlInitExecutor::StartupOutcomePort outcome;
    QmlInitExecutor::ShutdownOutcomePort shutdown_outcome;
    std::shared_ptr<std::atomic_bool> loss;
    std::atomic_bool initialization_threw{false};
    void record(QmlInitExecutor::StartupOutcome value)noexcept{try{if(outcome)outcome(value);}catch(...){if(loss)*loss=true;}}
    void recordExit(int status)noexcept{try{if(shutdown_outcome)shutdown_outcome(status);}catch(...){if(loss)*loss=true;}}
    void retireSubscription(){subscription.reset();pending_subscription_factory={};}
};
QString PrepareRunawayException(std::exception_ptr error,const std::shared_ptr<std::atomic_bool>& loss) noexcept
{
    QString message;
    try{
        try{std::rethrow_exception(error);}
        catch(const std::exception& e){
            PrintExceptionContinue(&e,"Runaway exception");
            const auto* text=e.what();const auto size=std::strlen(text);
            constexpr size_t MAX_BYTES{4*1024*1024};
            if(size>MAX_BYTES){if(loss)*loss=true;message=QString::fromUtf8(text,16384)+QStringLiteral(" ")+QObject::tr("[complete fatal diagnostic exceeds available capacity; check the node log]");}
            else message=QString::fromUtf8(text,static_cast<qsizetype>(size));
        }
        catch(...){PrintExceptionContinue(nullptr,"Runaway exception");message=QObject::tr("Unknown exception");}
    }catch(...){if(loss)*loss=true;message=QObject::tr("The fatal error diagnostic could not be prepared.");}
    return message;
}
}
QmlInitExecutor::QmlInitExecutor(interfaces::Node& node,SubscriptionFactory subscribe,std::shared_ptr<std::atomic_bool> diagnostic_loss,
    StartupOutcomePort outcome,ShutdownOutcomePort shutdown_outcome,std::shared_ptr<std::atomic_bool> lifecycle_failed)
    :m_diagnostic_loss(std::move(diagnostic_loss)),m_lifecycle_failed(lifecycle_failed?std::move(lifecycle_failed):std::make_shared<std::atomic_bool>(false)),
      m_executor(execution::Options{},nullptr,BackendExecutor::CleanupAffinity::Core)
{
    m_executor.setObjectName(QStringLiteral("node-lifecycle"));
    connect(&m_executor,&BackendExecutor::drained,this,&QmlInitExecutor::finishShutdown);
    auto native=std::make_shared<NativeLifecycle>();
    native->pending_subscription_factory=std::move(subscribe);native->outcome=std::move(outcome);
    native->shutdown_outcome=std::move(shutdown_outcome);native->loss=m_diagnostic_loss;
    nodelifecycle::Effects effects;
    effects.initialize=[node=&node,native](const std::atomic_bool& stopping){
        try{
            util::ThreadRename("qml-init");nodelifecycle::NativeInitialization result;
            if(stopping.load()){result.cancelled=true;native->record(StartupOutcome::Cancelled);return result;}
            const auto error_seen=std::make_shared<std::atomic_bool>(false);
            auto error_handler=node->handleMessageBox([error_seen](const bilingual_str&,unsigned int style){if(style&CClientUIInterface::ICON_ERROR)*error_seen=true;});
            if(!error_handler&&native->loss)*native->loss=true;
            result.attempted=true;
            result.success=node->appInitMain(&result.tip);
#ifdef ENABLE_TEST_AUTOMATION
            if(result.success&&gArgs.IsArgSet("-test-automation")&&gArgs.GetArg("-test-fatal-exception","")=="initialize")throw std::runtime_error("Test fatal exception after initialization");
#endif
            if(result.success&&native->pending_subscription_factory&&!stopping.load()&&!node->shutdownRequested()){
                auto subscribe_once=std::exchange(native->pending_subscription_factory,{});native->subscription=subscribe_once();
            }
            result.initial_block_download=result.success&&node->isInitialBlockDownload();
            result.shutdown_requested=node->shutdownRequested();
            result.cancelled=(result.shutdown_requested||stopping.load())&&!(error_seen->load()&&!result.success);
            native->record(result.cancelled?StartupOutcome::Cancelled:result.success?StartupOutcome::Ready:StartupOutcome::Failed);
            return result;
        }catch(...){native->initialization_threw=true;native->record(StartupOutcome::Uncertain);throw;}
    };
    effects.interrupt=[node=&node]{
        util::ThreadRename("qml-control");node->startShutdown();
#ifdef ENABLE_TEST_AUTOMATION
        if(gArgs.IsArgSet("-test-automation")&&gArgs.GetArg("-test-fatal-exception","")=="interrupt")throw std::runtime_error("Test fatal exception during interruption");
#endif
    };
    effects.shutdown=[node=&node,native]{
        if(native->initialization_threw.load())throw std::runtime_error("The fatally failed initialization cannot be torn down normally.");
#ifdef ENABLE_TEST_AUTOMATION
        if(gArgs.IsArgSet("-test-automation")&&gArgs.GetArg("-test-fatal-exception","")=="shutdown")throw std::runtime_error("Test fatal exception before shutdown");
#endif
        native->retireSubscription();node->appShutdown();
        const int status=node->getExitStatus();native->recordExit(status);return status;
    };
    effects.describe=[loss=m_diagnostic_loss](std::exception_ptr error){return PrepareRunawayException(error,loss);};
    const QPointer<QmlInitExecutor> observer{this};const auto post=presentation::ReliablePost(presentation::QueuedPost());
    m_driver=std::make_unique<nodelifecycle::Driver>(execution::SnapshotServices{m_executor.scheduler(),post},std::move(effects),
        [observer,post,receipt=m_lifecycle_failed,admission_closed=m_admission_closed](nodelifecycle::Projection value){
            // This process receipt survives a missing GUI observer and performs
            // no Core/settings work. Native settings receipts remain separate.
            if(value.phase==nodelifecycle::Phase::StartupFailed||value.phase==nodelifecycle::Phase::Fatal)receipt->store(true);
            switch(value.phase){
            case nodelifecycle::Phase::StartupFailed:
            case nodelifecycle::Phase::Stopping:
            case nodelifecycle::Phase::Draining:
            case nodelifecycle::Phase::Stopped:
            case nodelifecycle::Phase::Fatal:admission_closed->store(true);break;
            case nodelifecycle::Phase::Constructed:
            case nodelifecycle::Phase::Initializing:
            case nodelifecycle::Phase::Ready:break;
            }
            post([observer,value=std::move(value)]()mutable{if(observer)observer->publish(std::move(value));});
        });
}
QmlInitExecutor::~QmlInitExecutor(){m_admission_closed->store(true);m_driver.reset();m_executor.shutdown();}
void QmlInitExecutor::close(){m_admission_closed->store(true);publishAdmission();}
void QmlInitExecutor::initialize(){if(m_initialize_requested||!admissionOpen())return;m_initialize_requested=true;if(!m_driver||!m_driver->initialize())diagnosticTransportFailed();}
void QmlInitExecutor::interrupt()
{
    // Direct shutdown already owns its interruption and final receipt. A late
    // GUI control request cannot turn an already drained node into a failure.
    if(m_interrupt_requested||m_shutdown_requested||m_runaway_exception)return;
    m_interrupt_requested=true;
    const QPointer<QmlInitExecutor> observer{this};close();if(!observer)return;
    if(!m_driver||!m_driver->interrupt())diagnosticTransportFailed();
}
void QmlInitExecutor::shutdown()
{
    if(m_shutdown_requested||m_runaway_exception)return;
    m_shutdown_requested=true;
    const QPointer<QmlInitExecutor> observer{this};close();if(!observer)return;
    if(!m_driver||!m_driver->shutdown())diagnosticTransportFailed();
}
void QmlInitExecutor::publishAdmission()
{
    const bool open=admissionOpen();
    if(m_admission_projection_open==open)return;
    m_admission_projection_open=open;Q_EMIT admissionChanged();
}
void QmlInitExecutor::publish(nodelifecycle::Projection value)
{
    if(m_runaway_exception)return;
    if(value.event==nodelifecycle::Event::Shutdown){
        // Domain has authorized this terminal projection. Physical worker drain
        // is the existing infrastructure/native-handoff barrier before delivery.
        m_app_shutdown_complete=true;m_driver.reset();m_executor.shutdown();finishShutdown();return;
    }
    const QPointer<QmlInitExecutor> observer{this};
    // A queued Ready frame cannot cross an already admitted GUI stop intent.
    // Domain owns the eventual Stopping frame; this fence owns only readiness
    // admission while that frame is in transit.
    const bool fenced_ready=value.phase==nodelifecycle::Phase::Ready&&!admissionOpen();
    if(!fenced_ready&&m_phase!=value.phase){m_phase=value.phase;Q_EMIT phaseChanged();if(!observer)return;}
    publishAdmission();if(!observer)return;
    switch(value.event){
    case nodelifecycle::Event::Initialized:
        if(value.initialization){
            m_initialization=std::move(value.initialization);Q_EMIT initializationChanged();if(!observer)return;
            const auto result=*m_initialization;
            const bool stopping=!admissionOpen()||m_phase!=nodelifecycle::Phase::Ready;
            Q_EMIT initializeResult(result.success&&!stopping,result.tip,result.initial_block_download,result.shutdown_requested||stopping);
        }break;
    case nodelifecycle::Event::Interrupted:Q_EMIT interruptResult();break;
    case nodelifecycle::Event::Fatal:
        m_fatal_diagnostic_scheduler=std::move(value.diagnostic_scope);
        m_driver.reset();m_executor.shutdown();
        handleRunawayException(value.diagnostic?*value.diagnostic:tr("The node lifecycle diagnostic could not be retained."));break;
    case nodelifecycle::Event::State:
    case nodelifecycle::Event::Shutdown:break;
    }
}
void QmlInitExecutor::handleRunawayException(const QString& message)
{
    const QPointer<QmlInitExecutor> observer{this};
    const auto release=qScopeGuard([observer]{if(observer)observer->m_fatal_diagnostic_scheduler.reset();});
    if(m_runaway_exception)return;
    m_runaway_exception=true;m_admission_closed->store(true);
    if(m_phase!=nodelifecycle::Phase::Fatal){m_phase=nodelifecycle::Phase::Fatal;Q_EMIT phaseChanged();if(!observer)return;}
    publishAdmission();if(!observer)return;
    Q_EMIT runawayException(message);
}
void QmlInitExecutor::diagnosticTransportFailed(std::shared_ptr<execution::Scheduler> original_diagnostic_scope)
{
    m_lifecycle_failed->store(true);m_fatal_diagnostic_scheduler=std::move(original_diagnostic_scope);
    m_driver.reset();m_executor.shutdown();
    handleRunawayException(tr("The node lifecycle operation or its diagnostic could not be queued."));
}
void QmlInitExecutor::finishShutdown()
{
    if(m_runaway_exception||!m_app_shutdown_complete||m_shutdown_result_emitted||!m_executor.isDrained())return;
    m_shutdown_result_emitted=true;const QPointer<QmlInitExecutor> observer{this};
    m_admission_closed->store(true);
    m_phase=nodelifecycle::Phase::Stopped;Q_EMIT phaseChanged();if(!observer)return;
    publishAdmission();if(observer)Q_EMIT shutdownResult();
}
