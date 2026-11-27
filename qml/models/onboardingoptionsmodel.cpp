// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/onboardingoptionsmodel.h>

#include <qml/asyncjoin.h>
#include <qml/guipresentation.h>
#include <qml/core_settings.h>
#include <qml/datadir.h>
#include <qml/guiconstants.h>
#include <qml/onboarding_settings.h>
#include <qml/onboarding_storage.h>

#include <QCoreApplication>
#include <QMetaObject>
#include <QPointer>
#include <QThread>
#include <QUrl>
#include <QVariantMap>

#include <exception>
#include <future>
#include <stdexcept>
#include <utility>
#include <tuple>
#include <mutex>

namespace {
struct InitialOnboarding {
    QmlOnboardingSettings::OnboardingStartupStatus status;
    QString default_path;
    QString active_display;
    QUrl default_url;
    bool is_default{false};
};
}
struct OnboardingOptionsModel::FlowMailbox {
    std::mutex mutex;
    onboarding::Projection latest;
    std::atomic_bool posted{false};
    std::promise<void> drained;
};
OnboardingOptionsModel::OnboardingOptionsModel(std::vector<std::string> argv, bool ipc, QObject* parent,
                                               presentation::Post post, execution::Executor* executor)
    : QObject(parent), m_post(presentation::ReliablePost(std::move(post))), m_can_listen_ipc(ipc)
{
    if (!executor) {
        m_owned_executor = std::make_shared<execution::Executor>(execution::Executor::Config{});
        executor = m_owned_executor.get();
    }
    m_work = std::make_unique<execution::OwnedWork>(*executor, m_post);
    m_settings_namespace=UiPreferencesNamespace::Capture();
    m_backend_drained = !m_owned_executor;
    m_flow_mailbox = std::make_shared<FlowMailbox>();
    const QPointer<OnboardingOptionsModel> flow_observer{this};
    const auto flow_post = m_post;
    m_flow = std::make_unique<onboarding::Coordinator>(
        *executor,
        [mailbox=m_flow_mailbox,flow_observer,flow_post,receipt=m_preparation_state](onboarding::Projection value) {
            if(value.preparation_disposition) {
                using Disposition=startup::preparation::Disposition;
                const auto outcome=*value.preparation_disposition;
                receipt->store(outcome==Disposition::Ready ? PreparationSucceeded
                    : outcome==Disposition::Forwarded ? PreparationForwarded
                    : outcome==Disposition::Uncertain ? PreparationUncertain : PreparationFailed);
            }
            { std::lock_guard lock{mailbox->mutex}; mailbox->latest = std::move(value); }
            if (mailbox->posted.exchange(true)) return;
            flow_post([mailbox,flow_observer] {
                mailbox->posted.store(false);
                onboarding::Projection value;
                { std::lock_guard lock{mailbox->mutex}; value = mailbox->latest; }
                if (flow_observer) flow_observer->publishFlow(std::move(value));
            });
        }, [mailbox = m_flow_mailbox, flow_observer, flow_post] {
            mailbox->drained.set_value();
            flow_post([flow_observer] {
                if (!flow_observer) return;
                flow_observer->m_flow_drained = true;
                flow_observer->finishDrain();
            });
        });
    m_core_settings.setAdmissionReady(false);
    m_core_settings.configureServices(m_post, executor);
    connect(&m_core_settings,&CoreSettingsModel::operationChanged,this,[this]{if(m_stopping)return;const QPointer<OnboardingOptionsModel> guard{this};notifyCanFinish();if(guard&&!m_stopping)Q_EMIT pruneTargetInputChanged();});
    m_argv = std::make_shared<std::vector<std::string>>(std::move(argv));
    m_core_settings.setAfterChangeHandler([this](const QmlCoreSettings::Change& change,CoreSettingsModel::ChangeOrigin origin){
        const QPointer<OnboardingOptionsModel> observer{this};QmlCoreSettings::EmitCoreSettingSignals(*this,change);if(!observer||m_stopping)return;
        if(origin==CoreSettingsModel::ChangeOrigin::User&&(QmlCoreSettings::PruneChanged(change)||QmlCoreSettings::PruneSizeGBChanged(change))){m_prune_auto_recommended=false;emitStorageStatusChanged();}
    });
    if(!m_argv){m_preview_error=tr("Startup settings is unavailable.");m_preview_pending=false;return;}
    m_preview_in_flight=true;const auto request_id=++m_preview_request_id;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_work->submit([argv=m_argv,ipc,identity=m_settings_namespace]{InitialOnboarding result;result.default_path=QmlDataDir::DefaultDataDirString();result.default_url=QUrl::fromLocalFile(result.default_path);
        result.status=QmlOnboardingSettings::ResolveOnboardingStartupStatus(*argv,ipc,&identity);result.active_display=QmlDataDir::DisplayPath(result.status.active_data_dir);result.is_default=QmlDataDir::IsDefaultDataDir(result.status.active_data_dir);return result;},
        [observer,request_id](const InitialOnboarding& result){
            if(!observer||observer->m_stopping)return;
            observer->m_preview_in_flight=false;
            observer->m_default_data_dir=result.default_path;observer->m_default_data_directory=result.default_url;
            Q_EMIT observer->dataDirDefaultsChanged();if(!observer||observer->m_stopping)return;
            if(request_id!=observer->m_preview_request_id){observer->startPreview();return;}
            if(!result.status.ok){observer->setPreviewError(result.status.error);if(observer&&!observer->m_stopping)observer->setPreviewPending(false);return;}
            observer->m_data_dir=result.status.active_data_dir;observer->m_data_dir_source=result.status.data_dir_source;
            observer->m_custom_datadir_string=result.is_default?QString{}:observer->m_data_dir;observer->m_custom_datadir_display=result.is_default?QString{}:result.active_display;
            Q_EMIT observer->dataDirChanged(observer->m_data_dir);if(!observer||observer->m_stopping)return;
            Q_EMIT observer->customDataDirStringChanged(observer->m_custom_datadir_string);if(observer&&!observer->m_stopping)observer->startPreview();
        },[observer](std::exception_ptr){if(!observer||observer->m_stopping)return;observer->m_preview_in_flight=false;
            observer->setPreviewError(tr("The startup settings could not be read."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);});
}
void OnboardingOptionsModel::close()
{
    if(std::exchange(m_stopping,true))return;
    ++m_preview_request_id;++m_storage_request_id;++m_storage_projection_revision;++m_path_revision;
    m_work->close();m_argv.reset();
    if(m_flow&&!m_preparing){m_flow->close();m_flow.reset();}
    m_core_settings.close();
    if (!m_preparing) joinOwnedExecutor();
}

void OnboardingOptionsModel::joinOwnedExecutor()
{
    if (!m_owned_executor) return;
    auto join = JoinThreadAfterDrain([executor = std::move(m_owned_executor), flow = m_flow_mailbox->drained.get_future().share()] {
        flow.wait();
        executor->stopAndJoin();
    }, this, [this] {
        m_backend_drained = true;
        finishDrain();
    });
    const auto remaining = std::make_shared<std::atomic_int>(2);
    const auto done = [remaining, join = std::move(join)] {
        if (remaining->fetch_sub(1) == 1) join();
    };
    m_work->whenQueuesDrained(done);
    m_core_settings.whenEffectQueuesDrained(done);
}

void OnboardingOptionsModel::publishShutdown()
{
    if(m_shutdown_published)return;
    m_shutdown_published=true;Q_EMIT shutdownFinished();
}
void OnboardingOptionsModel::drainReaders()
{
    if (std::exchange(m_readers_draining, true)) return;
    const QPointer<OnboardingOptionsModel> observer{this};
    const auto remaining = std::make_shared<unsigned>(2);
    const auto finished = [observer, remaining] {
        if (--*remaining || !observer) return;
        observer->m_readers_drained = true;
        observer->finishDrain();
    };
    m_work->close(finished);
    m_core_settings.whenEffectsDrained(finished);
}

void OnboardingOptionsModel::finishDrain()
{
    if (!m_readers_drained || !m_flow_drained || !m_backend_drained || m_terminal_scheduled) return;
    if (!(m_preparation_terminal_pending && !m_preparation_published)
        && !(m_shutdown_requested&&!m_preparing&&!m_preparation_terminal_pending&&!m_shutdown_published)) return;
    m_terminal_scheduled=true;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_post([observer] {
        if(!observer)return;
        if(observer->m_preparation_terminal_pending&&!observer->m_preparation_published){
            observer->m_preparation_published=true;observer->m_preparation_terminal_pending=false;
            const auto outcome=observer->preparationState();
            const bool success=observer->m_preparation_success;
            const QString error=observer->m_preparation_error;
            Q_EMIT observer->preparationStateChanged();if(!observer)return;
            Q_EMIT observer->nodePreparationFinished(outcome,error);if(!observer)return;
            Q_EMIT observer->nodePrepared(success,error);if(!observer)return;
        }
        observer->m_terminal_scheduled=false;
        if(observer->m_shutdown_requested&&!observer->m_preparing&&!observer->m_preparation_terminal_pending)observer->publishShutdown();
    });
}

void OnboardingOptionsModel::beginShutdown()
{
    if(std::exchange(m_shutdown_requested,true))return;
    close();
    if (m_flow) {m_flow->close();m_flow.reset();}
    drainReaders();
    joinOwnedExecutor();
    if(m_preparing||m_preparation_terminal_pending)return;
    finishDrain();
}

void OnboardingOptionsModel::setPreviewPending(bool pending)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    if (m_preview_pending == pending) return;
    m_preview_pending = pending;
    Q_EMIT validationPendingChanged();
    if(!observer||m_stopping)return;
    notifyCanFinish();
    if(!observer||m_stopping)return;
}

OnboardingOptionsModel::~OnboardingOptionsModel()
{
    close();
    m_flow.reset();
    joinOwnedExecutor();
}

QString OnboardingOptionsModel::unpruneConsentToken()const
{
    return QString::number(m_core_settings.userRevision())+QLatin1Char(':')+QString::number(m_path_revision);
}
void OnboardingOptionsModel::confirmUnpruning()
{
    if(!m_stopping&&!setupCommitPending()&&!setupCommitted()&&!setupPartial()&&!setupUncertain())m_unprune_consent=unpruneConsentToken();
}
bool OnboardingOptionsModel::checksAvailable() const
{
    return !m_preparing && !setupCommitPending() && !setupCommitted() && m_setup_commit->load()!=SetupUncertain
        && !m_stopping && !m_path_pending && !m_core_settings.pending() && m_core_settings.pruneTargetError().isEmpty()
        && !m_preview_pending && m_preview_error.isEmpty() && !m_storage_check_pending && !m_storage_projection_pending
        && m_storage_result_valid && m_storage_error_text.isEmpty() && m_storage_info.enough_for_selected;
}
void OnboardingOptionsModel::notifyCanFinish()
{
    m_flow_ready = false;
    if (m_flow && !m_stopping && !m_flow_command_pending && !setupCommitted() && !setupPartial() && !setupUncertain()) {
        const auto revision = ++m_flow_revision;
        const auto request = applyRequest();
        const auto argv = m_argv;
        const bool ipc = m_can_listen_ipc;
        onboarding::Checks checks{revision, !m_path_pending && !m_preview_pending && m_preview_error.isEmpty(),
            !m_storage_check_pending && !m_storage_projection_pending && m_storage_result_valid && m_storage_info.enough_for_selected,
            !m_core_settings.pending() && m_core_settings.pruneTargetError().isEmpty(), {}};
        const bool admitted=m_flow->check(checks, [argv,ipc,request] {
            if (!argv) return QStringLiteral("The startup profile is unavailable.");
            return QmlOnboardingSettings::Preview(*argv, ipc, request.data_dir, &request).error;
        });
        if(!admitted){
            m_setup_error=tr("Setup checks could not start. Try again; nothing was saved.");
            const QPointer<OnboardingOptionsModel> observer{this};Q_EMIT setupCommitChanged();if(!observer)return;
        }
    }
    Q_EMIT canFinishChanged();
}
void OnboardingOptionsModel::publishFlow(onboarding::Projection value)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    const auto old_attempt = m_flow_projection.attempt;
    const bool old_complete = m_flow_projection.profile_complete;
    m_flow_projection = std::move(value);
    m_flow_ready = m_flow_projection.phase == onboarding::Phase::Ready && m_flow_projection.revision == m_flow_revision;
    m_flow_command_pending = m_flow_projection.pending;
    if (m_flow_projection.attempt) {
        const auto phase = m_flow_projection.phase;
        m_setup_commit->store(m_flow_projection.profile_complete ? SetupCommitted
            : phase == onboarding::Phase::Persisting ? SetupPending
            : m_flow_projection.profile_unknown ? SetupUncertain : SetupFailed);
        m_setup_error = m_flow_projection.error;
    } else if (m_setup_commit->load() == SetupPending) m_setup_commit->store(SetupFailed);
    if (!m_flow_projection.error.isEmpty()) m_setup_error = m_flow_projection.error;
    if (!m_stopping && !m_flow_projection.pending && m_flow_projection.phase == onboarding::Phase::Failed
        && !m_flow_projection.profile_written) m_core_settings.setAdmissionReady(true);
    if(!observer)return;
    Q_EMIT setupCommitChanged(); if (!observer) return;
    Q_EMIT canFinishChanged(); if (!observer) return;
    if (m_preparing && !m_flow_projection.pending && m_flow_projection.preparation_finished) {
        if(!m_flow_projection.preparation_disposition)
            m_preparation_state->store(m_flow_projection.preparation_complete ? PreparationSucceeded
                : m_flow_projection.preparation_unknown || m_flow_projection.profile_unknown ? PreparationUncertain : PreparationFailed);
        if (m_flow) { m_flow->close(); m_flow.reset(); }
        finishNodePreparation(m_flow_projection.preparation_complete, m_flow_projection.error);
        return;
    }
    if (!m_stopping && m_flow_projection.profile_complete && (!old_complete || old_attempt != m_flow_projection.attempt))
        Q_EMIT settingsCommitted();
}
void OnboardingOptionsModel::configureSettingsCommit(ArgsManager& args)
{
    m_profile_ports = {
        [&args](const auto& request) { return QmlOnboardingSettings::PersistProfile(args, request); },
        [&args](const auto& original) { return QmlOnboardingSettings::QueryProfile(args, original); },
        [&args](const auto& original) { return QmlOnboardingSettings::CompleteProfile(args, original); }};
}
void OnboardingOptionsModel::setSettingsCommitPort(CommitPort port)
{
    m_profile_ports.write = [port=std::move(port)](const auto& request) {
        const auto [success,error] = port(request);
        QmlOnboardingSettings::ProfileResult result;
        result.completed=success;result.replacement_written=success;result.directory_saved=success;result.cleanup_completed=success;result.error=error;
        return result;
    };
}
void OnboardingOptionsModel::commitSettings()
{
    if (!canFinish() || !m_flow || !m_profile_ports.write) return;
    m_flow_command_pending = true;
    m_setup_commit->store(SetupPending);
    m_setup_error.clear();
    const QPointer<OnboardingOptionsModel> observer{this};
    m_core_settings.setAdmissionReady(false);if(!observer)return;
    const auto receipt = m_setup_commit;
    auto ports = m_profile_ports;
    const auto original_write = ports.write;
    ports.write = [receipt,original_write](const auto& request) {
        try {
            auto result=original_write(request);
            receipt->store(result.completed?SetupCommitted:result.uncertain?SetupUncertain:SetupFailed);
            return result;
        } catch (...) {receipt->store(SetupUncertain);throw;}
    };
    if (!m_flow->finish(m_flow_revision, applyRequest(), std::move(ports))) {
        m_flow_command_pending=false;m_setup_commit->store(SetupFailed);
        m_setup_error=tr("Setup could not start. Your answers are kept.");
        m_core_settings.setAdmissionReady(true);
    }
    if(!observer)return;
    Q_EMIT setupCommitChanged();if(observer)Q_EMIT canFinishChanged();
}
void OnboardingOptionsModel::querySetupOutcome()
{
    if (!m_flow || !setupUncertain() || m_flow_command_pending) return;
    m_flow_command_pending=m_flow->recover(false);
    Q_EMIT setupCommitChanged();
}
void OnboardingOptionsModel::completeSavedSetup()
{
    if (!m_flow || !setupPartial() || setupUncertain() || m_flow_command_pending) return;
    m_flow_command_pending=m_flow->recover(true);
    Q_EMIT setupCommitChanged();
}
void OnboardingOptionsModel::prepareNode(ArgsManager& args,std::function<bool()> prepare_node)
{
    if(m_preparation_started)return;
    const QPointer<OnboardingOptionsModel> observer{this};
    const auto reject=[observer,post=m_post](const QString& error){
        post([observer,error]{if(observer)Q_EMIT observer->nodePrepared(false,error);});
    };
    if(m_stopping){reject(tr("The application is shutting down."));return;}
    if(setupCommitPending()||setupPartial()||setupUncertain()){
        reject(tr("Finish the earlier setup step before preparing the node. It will not be repeated."));return;
    }
    if(!m_flow||(!m_flow_projection.profile_complete&&!canFinish())){
        reject(tr("Finish or correct the staged storage input before preparing the node."));return;
    }
    m_preparation_started=true;m_preparing=true;m_preparation_state->store(PreparationPending);
    const auto receipt=m_preparation_state;
    const auto effect=[prepare_node=std::move(prepare_node),receipt](const auto&,bool){
        try{const bool success=prepare_node();receipt->store(success?PreparationSucceeded:PreparationFailed);return std::pair{success,QString{}};}
        catch(...){receipt->store(PreparationUncertain);throw;}
    };
    bool admitted{false};
    if(m_flow_projection.profile_complete)admitted=m_flow->prepare(effect);
    else{
        configureSettingsCommit(args);
        admitted=m_flow->finishAndPrepare(m_flow_revision,applyRequest(),m_profile_ports,effect);
    }
    m_stopping=true;++m_preview_request_id;++m_storage_request_id;++m_storage_projection_revision;++m_path_revision;
    m_work->close();m_argv.reset();m_core_settings.close();if(!observer)return;
    if(!admitted){
        m_preparation_state->store(PreparationFailed);
        if(m_flow){m_flow->close();m_flow.reset();}
        finishNodePreparation(false,tr("The node could not start preparing. Your saved settings are kept; restart to continue."));
    }
    if(observer)Q_EMIT preparationStateChanged();
}
void OnboardingOptionsModel::finishNodePreparation(bool success,const QString& error)
{
    if(m_preparation_terminal_pending||m_preparation_published)return;
    m_preparing=false;m_preparation_terminal_pending=true;m_preparation_success=success;m_preparation_error=error;
    if(m_flow){m_flow->close();m_flow.reset();}
    drainReaders();
    joinOwnedExecutor();
    finishDrain();
}
void OnboardingOptionsModel::prepareNodeAsync(ArgsManager& args,onboarding::AsyncPreparation preparation)
{
    if(m_preparation_started)return;
    const QPointer<OnboardingOptionsModel> observer{this};
    const auto reject=[observer,post=m_post](const QString& error){
        post([observer,error]{if(observer){
            Q_EMIT observer->nodePreparationFinished(PreparationFailed,error);if(!observer)return;
            Q_EMIT observer->nodePrepared(false,error);
        }});
    };
    if(m_stopping){reject(tr("The application is shutting down."));return;}
    if(setupCommitPending()||setupPartial()||setupUncertain()){
        reject(tr("Finish the earlier setup step before preparing the node. It will not be repeated."));return;
    }
    if(!m_flow||(!m_flow_projection.profile_complete&&!canFinish())){
        reject(tr("Finish or correct the staged storage input before preparing the node."));return;
    }
    m_preparation_started=true;m_preparing=true;m_preparation_state->store(PreparationPending);
    bool admitted{false};
    if(m_flow_projection.profile_complete)admitted=m_flow->prepareAsync(std::move(preparation));
    else{
        configureSettingsCommit(args);
        admitted=m_flow->finishAndPrepareAsync(m_flow_revision,applyRequest(),m_profile_ports,std::move(preparation));
    }
    m_stopping=true;++m_preview_request_id;++m_storage_request_id;++m_storage_projection_revision;++m_path_revision;
    m_work->close();m_argv.reset();m_core_settings.close();if(!observer)return;
    if(!admitted){
        m_preparation_state->store(PreparationFailed);
        if(m_flow){m_flow->close();m_flow.reset();}
        finishNodePreparation(false,tr("The node could not start preparing. Your saved settings are kept; restart to continue."));
    }
    if(observer)Q_EMIT preparationStateChanged();
}
QString OnboardingOptionsModel::getDefaultDataDirString() const{return m_default_data_dir;}
QUrl OnboardingOptionsModel::getDefaultDataDirectory() const{return m_default_data_directory;}

QString OnboardingOptionsModel::getCustomDataDirString() const{return m_custom_datadir_display;}

QString OnboardingOptionsModel::validateCustomDataDir(const QString& path) const
{
    return path==m_path_source||path==m_data_dir?m_preview_error:tr("The directory has not been checked.");
}
bool OnboardingOptionsModel::selectCustomDataDir(const QString& path)
{
    if(m_stopping||setupCommitPending()||setupCommitted()||setupPartial()||setupUncertain())return false;
    m_path_pending = true;
    m_preview_pending = true;
    const auto revision=++m_path_revision;
    ++m_preview_request_id;++m_storage_request_id;m_path_source=path;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_core_settings.setAdmissionReady(false);if(!observer||m_stopping||revision!=m_path_revision)return false;
    m_core_settings.invalidatePreviewContext();
    const bool admitted=m_work->submit([path]{if(path.size()>32768)throw std::length_error("Data directory path is too long");const auto normalized=QmlDataDir::NormalizeLocalPath(path);return std::pair{normalized,QmlDataDir::DisplayPath(normalized)};},
        [observer,revision](const std::pair<QString,QString>& result){if(!observer||observer->m_stopping||revision!=observer->m_path_revision)return;
            observer->m_path_pending=false;
            const auto& [normalized,display]=result;observer->m_custom_datadir_display=display;
            if(normalized.isEmpty()){observer->setPreviewError(tr("Choose a data directory."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);return;}
            observer->m_data_dir_source=QmlOnboardingSettings::DataDirSource::UserSelection;
            if(observer->m_custom_datadir_string!=normalized){observer->m_custom_datadir_string=normalized;Q_EMIT observer->customDataDirStringChanged(normalized);}
            if(observer&&!observer->m_stopping&&revision==observer->m_path_revision)observer->setDataDir(normalized);
        },[observer,revision](std::exception_ptr){if(!observer||observer->m_stopping||revision!=observer->m_path_revision)return;
            observer->m_path_pending=false;
            observer->setPreviewError(tr("The selected directory could not be checked."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);});
    if(observer&&!m_stopping){Q_EMIT validationPendingChanged();if(observer&&!m_stopping)notifyCanFinish();}
    return admitted;
}
void OnboardingOptionsModel::useDefaultDataDir()
{
    if(m_stopping||setupCommitPending()||setupCommitted()||setupPartial()||setupUncertain())return;
    m_path_pending = true;
    m_preview_pending = true;
    const auto revision=++m_path_revision;
    ++m_preview_request_id;++m_storage_request_id;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_core_settings.setAdmissionReady(false);if(!observer||m_stopping||revision!=m_path_revision)return;
    m_core_settings.invalidatePreviewContext();
    m_work->submit([]{return QmlDataDir::DefaultDataDirString();},[observer,revision](const QString& path){
        if(!observer||observer->m_stopping||revision!=observer->m_path_revision)return;
        observer->m_path_pending=false;
        observer->m_path_source=path;observer->m_data_dir_source=QmlOnboardingSettings::DataDirSource::UserSelection;
        if(!observer->m_custom_datadir_string.isEmpty()){observer->m_custom_datadir_string.clear();observer->m_custom_datadir_display.clear();Q_EMIT observer->customDataDirStringChanged({});}
        if(observer&&!observer->m_stopping&&revision==observer->m_path_revision)observer->setDataDir(path);
    },[observer,revision](std::exception_ptr){if(!observer||observer->m_stopping||revision!=observer->m_path_revision)return;
        observer->m_path_pending=false;
        observer->setPreviewError(tr("The default directory could not be checked."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);});
    if(observer&&!m_stopping){Q_EMIT validationPendingChanged();if(observer&&!m_stopping)notifyCanFinish();}
}
void OnboardingOptionsModel::setDataDir(const QString& canonical)
{
    if(m_stopping)return;
    const QPointer<OnboardingOptionsModel> observer{this};
    const auto revision=m_path_revision;
    if(canonical!=m_data_dir){m_data_dir=canonical;Q_EMIT dataDirChanged(m_data_dir);}
    if(observer&&!m_stopping&&revision==m_path_revision)refreshPreview();
}

void OnboardingOptionsModel::setPrune(bool prune)
{
    m_core_settings.changePrune(prune);
}

void OnboardingOptionsModel::setPruneSizeGB(int prune_size_gb)
{
    m_core_settings.changePruneSizeGB(prune_size_gb);
}

void OnboardingOptionsModel::setListen(bool listen)
{
    m_core_settings.changeListen(listen);
}

void OnboardingOptionsModel::setNatpmp(bool natpmp)
{
    m_core_settings.changeNatpmp(natpmp);
}

void OnboardingOptionsModel::setServer(bool server)
{
    m_core_settings.changeServer(server);
}

void OnboardingOptionsModel::setProxyEnabled(bool enabled)
{
    m_core_settings.changeProxyEnabled(enabled);
}

void OnboardingOptionsModel::setProxyAddress(const QString& address)
{
    m_core_settings.changeProxyLocation(address);
}

void OnboardingOptionsModel::setTorEnabled(bool enabled)
{
    m_core_settings.changeTorEnabled(enabled);
}

void OnboardingOptionsModel::setTorAddress(const QString& address)
{
    m_core_settings.changeTorLocation(address);
}

QString OnboardingOptionsModel::defaultProxyAddress() const
{
    return m_core_settings.defaultProxyAddress();
}

QString OnboardingOptionsModel::storageStatus() const
{
    return storageInfo().status;
}

int OnboardingOptionsModel::storageAvailableGB() const
{
    return storageInfo().available_gb;
}

QString OnboardingOptionsModel::storageAvailableText() const
{
    return storageInfo().available_text;
}

QString OnboardingOptionsModel::storageWarningText() const
{
    return storageInfo().warning_text;
}

int OnboardingOptionsModel::storageMinimumRequiredGB() const
{
    return storageInfo().minimum_required_gb;
}

int OnboardingOptionsModel::fullStorageRequiredGB() const
{
    return storageInfo().full_required_gb;
}

int OnboardingOptionsModel::prunedStorageRequiredGB() const
{
    return storageInfo().pruned_required_gb;
}

int OnboardingOptionsModel::selectedStorageRequiredGB() const
{
    return storageInfo().selected_required_gb;
}

bool OnboardingOptionsModel::storageEnoughForSelected() const
{
    return storageInfo().enough_for_selected;
}

bool OnboardingOptionsModel::storageEnoughForFull() const
{
    return storageInfo().enough_for_full;
}

void OnboardingOptionsModel::requestStorageCheck()
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    ++m_storage_request_id;
    m_storage_check_path = m_data_dir;
    m_storage_check_pending = true;
    emitStorageStatusChanged();
    if(!observer||m_stopping)return;
    if (!m_storage_check_in_flight) {
        startStorageCheck(m_storage_request_id, m_storage_check_path);
    }
}

void OnboardingOptionsModel::queueStorageCheck()
{
    if (m_stopping) return;
    m_storage_check_in_flight = true; // Coalesce newer edits until the queued follow-up starts.
    const QPointer<OnboardingOptionsModel> observer{this};
    m_post([observer] {
        if (observer && !observer->m_stopping) observer->startStorageCheck(observer->m_storage_request_id, observer->m_storage_check_path);
    });
}

void OnboardingOptionsModel::startStorageCheck(uint64_t request_id, const QString& path)
{
    m_storage_check_in_flight = true;
    m_work->submit( [path] { return QmlDataDir::CheckStorageSpace(path); },
        [this, request_id](const QmlDataDir::StorageSpaceResult& result) { if(!m_stopping)applyStorageCheckResult(request_id, result); },
        [this, request_id](std::exception_ptr) {
            if(m_stopping)return;
            QmlDataDir::StorageSpaceResult result;
            result.message = tr("Available storage could not be checked.");
            applyStorageCheckResult(request_id, result);
        });
}

void OnboardingOptionsModel::applyStorageCheckResult(uint64_t request_id, const QmlDataDir::StorageSpaceResult& result)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    m_storage_check_in_flight = false;
    if (request_id != m_storage_request_id) {
        if (!m_preview_pending && !m_stopping) queueStorageCheck();
        return;
    }

    m_storage_check_pending = false;
    m_storage_result_valid = result.ok;
    m_storage_available_bytes = result.ok ? result.available_bytes : 0;
    m_storage_path_message = result.message;
    m_storage_error_text = result.ok ? QString{} : result.message;
    emitStorageStatusChanged();
    if(!observer||m_stopping)return;
}

void OnboardingOptionsModel::emitStorageStatusChanged()
{
    if(m_stopping)return;
    ++m_storage_projection_revision;m_storage_projection_pending=true;
    const QPointer<OnboardingOptionsModel> observer{this};
    if(!m_storage_projection_inflight)startStorageProjection();
    if(!observer||m_stopping)return;
    Q_EMIT storageStatusChanged();if(observer&&!m_stopping)notifyCanFinish();
}
void OnboardingOptionsModel::queueStorageProjection()
{
    if (m_stopping) return;
    m_storage_projection_inflight = true; // Coalesce newer edits until the queued follow-up starts.
    const QPointer<OnboardingOptionsModel> observer{this};
    m_post([observer] {
        if (observer && !observer->m_stopping) observer->startStorageProjection();
    });
}

void OnboardingOptionsModel::startStorageProjection()
{
    if(m_stopping)return;
    m_storage_projection_inflight=true;const auto revision=m_storage_projection_revision;const auto core_revision=m_core_settings.publishedUserRevision();
    const auto state=storageState();const auto status=m_core_settings.statuses().value(QStringLiteral("prune")).toMap();
    const bool touched=m_core_settings.isTouched(QStringLiteral("prune"))&&!m_prune_auto_recommended;
    const QPointer<OnboardingOptionsModel> observer{this};
    m_work->submit([state=QmlOnboardingStorage::State{state},status,touched]() mutable {
        const bool eligible=state.result_valid&&state.error_text.isEmpty()&&!state.existing_profile&&!touched
            &&status.value(QStringLiteral("canEdit"),true).toBool()&&status.value(QStringLiteral("source")).toString()==QStringLiteral("default");
        const bool recommend=eligible&&QmlOnboardingStorage::ShouldRecommendPrune(state);
        if(eligible){state.prune=recommend;state.prune_size_gb=DEFAULT_PRUNE_TARGET_GB;}
        return std::tuple{QmlOnboardingStorage::Evaluate(state),eligible,recommend};
    },[observer,revision,core_revision](const auto& result){
        if(!observer||observer->m_stopping)return;
        observer->m_storage_projection_inflight=false;
        if(revision!=observer->m_storage_projection_revision){observer->queueStorageProjection();return;}
        const auto& [info,eligible,recommend]=result;observer->m_storage_info=info;observer->m_storage_projection_pending=false;
        if(eligible){observer->m_prune_auto_recommended=recommend;observer->m_core_settings.changePruneRecommendation(recommend,DEFAULT_PRUNE_TARGET_GB,recommend,core_revision);}
        if(!observer||observer->m_stopping)return;
        Q_EMIT observer->storageStatusChanged();if(observer&&!observer->m_stopping)observer->notifyCanFinish();
    },[observer,revision](std::exception_ptr){
        if(!observer||observer->m_stopping)return;
        observer->m_storage_projection_inflight=false;
        if(revision!=observer->m_storage_projection_revision){observer->queueStorageProjection();return;}
        observer->m_storage_projection_pending=false;observer->m_storage_error_text=tr("Storage requirements could not be calculated.");
        observer->m_storage_info.status=QStringLiteral("error");Q_EMIT observer->storageStatusChanged();if(observer&&!observer->m_stopping)observer->notifyCanFinish();
    },execution::Lane::Domain);
}

QmlOnboardingStorage::State OnboardingOptionsModel::storageState() const
{
    QmlOnboardingStorage::State state;
    state.check_pending = m_storage_check_pending;
    state.result_valid = m_storage_result_valid;
    state.available_bytes = m_storage_available_bytes;
    state.error_text = m_storage_error_text;
    state.assumed_blockchain_size_gb = m_assumed_blockchain_size;
    state.assumed_chainstate_size_gb = m_assumed_chainstate_size;
    state.prune = m_core_settings.values().prune;
    state.prune_size_gb = m_core_settings.values().prune_size_gb;
    state.existing_profile = m_profile.existing_profile;
    state.manual_pruning=state.prune&&m_core_settings.values().prune_target_mib==1;
    state.custom_manual_pruning=m_core_settings.customStorageTarget()==1;
    state.custom_prune_size_gb=m_core_settings.customStorageSizeGB();
    return state;
}

QmlCoreSettings::Values OnboardingOptionsModel::coreValues() const
{
    return m_core_settings.values();
}

void OnboardingOptionsModel::setPreviewError(const QString& error)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    if (error == m_preview_error) return;
    m_preview_error = error;
    Q_EMIT previewErrorChanged();
    if(!observer||m_stopping)return;
    notifyCanFinish();
    if(!observer||m_stopping)return;
}

void OnboardingOptionsModel::refreshPreview()
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    ++m_preview_request_id;
    ++m_storage_request_id;
    setPreviewPending(true);if(!observer||m_stopping)return;
    if (!m_preview_in_flight && !m_path_pending) startPreview();
}

void OnboardingOptionsModel::queuePreview()
{
    if (m_stopping) return;
    m_preview_in_flight = true; // Coalesce newer edits until the queued follow-up starts.
    const QPointer<OnboardingOptionsModel> observer{this};
    m_post([observer] {
        if (observer && !observer->m_stopping) {
            if (observer->m_path_pending) observer->m_preview_in_flight=false;
            else observer->startPreview();
        }
    });
}

void OnboardingOptionsModel::startPreview()
{
    if (m_stopping) return;
    if (m_path_pending) { m_preview_in_flight=false; return; }
    m_preview_in_flight = true;
    const auto request_id = m_preview_request_id;const auto context=m_core_settings.previewContext();
    m_work->submit( [argv = m_argv, ipc = m_can_listen_ipc,
        selection = QmlOnboardingSettings::DataDirSelection{m_data_dir, m_data_dir_source}] {
        return QmlOnboardingSettings::Preview(*argv, ipc, selection);
    }, [this, request_id,context](const QmlOnboardingSettings::PreviewResult& preview) {
        if(m_stopping)return;
        m_preview_in_flight = false;
        if (request_id != m_preview_request_id) {
            queuePreview();
            return;
        }
        const QPointer<OnboardingOptionsModel> observer{this};
        applyPreview(preview,request_id,context);
    }, [this, request_id](std::exception_ptr) {
            if(m_stopping)return;
        m_preview_in_flight = false;
        if (request_id != m_preview_request_id) {
            queuePreview();
            return;
        }
        const QString error = tr("The selected directory could not be checked.");
        const QPointer<OnboardingOptionsModel> observer{this};
        setPreviewError(error);if(!observer||m_stopping)return;
        setPreviewPending(false);if(observer&&!m_stopping)Q_EMIT dataDirSelectionFinished(false, error);
    });
}

void OnboardingOptionsModel::applyPreview(const QmlOnboardingSettings::PreviewResult& preview,uint64_t request_id,quint64 context)
{
    const QPointer<OnboardingOptionsModel> observer{this};
    if(m_stopping)return;
    if (!preview.ok) {
        setPreviewError(preview.error);if(!observer||m_stopping)return;setPreviewPending(false);
        if(observer&&!m_stopping)Q_EMIT dataDirSelectionFinished(false,preview.error);
        return;
    }

    const int old_assumed_blockchain_size = m_assumed_blockchain_size;
    const int old_assumed_chainstate_size = m_assumed_chainstate_size;
    const bool old_existing_profile = m_profile.existing_profile;
    m_assumed_blockchain_size = preview.assumed_blockchain_size;
    m_assumed_chainstate_size = preview.assumed_chainstate_size;
    m_profile = preview.profile;
    if (m_profile.existing_profile && m_prune_auto_recommended) {
        m_core_settings.changePruneRecommendation(preview.values.prune, preview.values.prune_size_gb, /*touched=*/false);
        if(!observer||m_stopping)return;
        m_prune_auto_recommended = false;
    }
    if (old_assumed_blockchain_size != m_assumed_blockchain_size || old_assumed_chainstate_size != m_assumed_chainstate_size) {
        Q_EMIT assumedSizesChanged();
    if(!observer||m_stopping)return;
        emitStorageStatusChanged();
    if(!observer||m_stopping)return;
    }
    if (old_existing_profile != m_profile.existing_profile) {
        emitStorageStatusChanged();
    if(!observer||m_stopping)return;
    }

    const auto revision=request_id;
    m_core_settings.applyPreviewValuesPreservingTouched(preview.values,QmlCoreSettings::StatusProjection{preview.core_setting_statuses},[observer,revision](bool accepted){
        if(!observer||observer->m_stopping||revision!=observer->m_preview_request_id)return;
        if(!accepted){observer->setPreviewError(tr("The settings preview could not be applied."));if(observer&&!observer->m_stopping)observer->setPreviewPending(false);return;}
        observer->m_core_settings.setAdmissionReady(true);if(!observer||observer->m_stopping)return;
        observer->setPreviewError({});if(!observer||observer->m_stopping)return;observer->requestStorageCheck();if(!observer||observer->m_stopping)return;
        observer->setPreviewPending(false);if(observer&&!observer->m_stopping)Q_EMIT observer->dataDirSelectionFinished(true,{});
    },false,{},context);
}

QmlOnboardingSettings::ApplyRequest OnboardingOptionsModel::applyRequest() const
{
    return {{m_data_dir, m_data_dir_source}, m_core_settings.touchedSettings(), coreValues(),m_unprune_consent==unpruneConsentToken(),m_settings_namespace};
}
