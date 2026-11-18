// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ONBOARDINGOPTIONSMODEL_H
#define BITCOIN_QML_MODELS_ONBOARDINGOPTIONSMODEL_H

#include <qml/core_settings.h>
#include <qml/backendexecutor.h>
#include <qml/ownedwork.h>
#include <qml/models/core_settings_model.h>
#include <qml/onboarding_settings.h>
#include <qml/onboardingflow.h>
#include <qml/onboarding_storage.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

class ArgsManager;
class BackendExecutor;

namespace QmlDataDir {
struct StorageSpaceResult;
} // namespace QmlDataDir

class OnboardingOptionsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString dataDir READ dataDir NOTIFY dataDirChanged)
    Q_PROPERTY(QString getDefaultDataDirString READ getDefaultDataDirString NOTIFY dataDirDefaultsChanged)
    Q_PROPERTY(QUrl getDefaultDataDirectory READ getDefaultDataDirectory NOTIFY dataDirDefaultsChanged)
    Q_PROPERTY(QString pruneTargetText READ pruneTargetText NOTIFY pruneTargetInputChanged)
    Q_PROPERTY(QString pruneTargetError READ pruneTargetError NOTIFY pruneTargetInputChanged)
    Q_PROPERTY(bool prune READ prune WRITE setPrune NOTIFY pruneChanged)
    Q_PROPERTY(int pruneSizeGB READ pruneSizeGB WRITE setPruneSizeGB NOTIFY pruneSizeGBChanged)
    Q_PROPERTY(bool listen READ listen WRITE setListen NOTIFY listenChanged)
    Q_PROPERTY(bool natpmp READ natpmp WRITE setNatpmp NOTIFY natpmpChanged)
    Q_PROPERTY(bool server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(bool proxyEnabled READ proxyEnabled WRITE setProxyEnabled NOTIFY proxyEnabledChanged)
    Q_PROPERTY(QString proxyAddress READ proxyAddress WRITE setProxyAddress NOTIFY proxyAddressChanged)
    Q_PROPERTY(bool torEnabled READ torEnabled WRITE setTorEnabled NOTIFY torEnabledChanged)
    Q_PROPERTY(QString torAddress READ torAddress WRITE setTorAddress NOTIFY torAddressChanged)
    Q_PROPERTY(QObject* coreSettings READ coreSettings CONSTANT)
    Q_PROPERTY(QVariantMap coreSettingStatuses READ coreSettingStatuses NOTIFY coreSettingStatusesChanged)
    Q_PROPERTY(bool validationPending READ validationPending NOTIFY validationPendingChanged)
    Q_PROPERTY(QString previewError READ previewError NOTIFY previewErrorChanged)
    Q_PROPERTY(bool canFinish READ canFinish NOTIFY canFinishChanged)
    Q_PROPERTY(bool setupCommitPending READ setupCommitPending NOTIFY setupCommitChanged)
    Q_PROPERTY(bool setupCommitted READ setupCommitted NOTIFY setupCommitChanged)
    Q_PROPERTY(QString setupCommitError READ setupCommitError NOTIFY setupCommitChanged)
    Q_PROPERTY(QString unpruneConsentToken READ unpruneConsentToken NOTIFY canFinishChanged)
    Q_PROPERTY(bool connectionSettingsDirty READ dirtyState CONSTANT)
    Q_PROPERTY(bool storageSettingsDirty READ dirtyState CONSTANT)
    Q_PROPERTY(bool proxySettingsDirty READ dirtyState CONSTANT)
    Q_PROPERTY(int assumedBlockchainSize READ assumedBlockchainSize NOTIFY assumedSizesChanged)
    Q_PROPERTY(int assumedChainstateSize READ assumedChainstateSize NOTIFY assumedSizesChanged)
    Q_PROPERTY(bool existingProfile READ existingProfile NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageCheckPending READ storageCheckPending NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageStatus READ storageStatus NOTIFY storageStatusChanged)
    Q_PROPERTY(int storageAvailableGB READ storageAvailableGB NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageAvailableText READ storageAvailableText NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storagePathMessage READ storagePathMessage NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageWarningText READ storageWarningText NOTIFY storageStatusChanged)
    Q_PROPERTY(QString storageErrorText READ storageErrorText NOTIFY storageStatusChanged)
    Q_PROPERTY(int storageMinimumRequiredGB READ storageMinimumRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int fullStorageRequiredGB READ fullStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int prunedStorageRequiredGB READ prunedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int reducedStorageRequiredGB READ reducedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(int selectedStorageRequiredGB READ selectedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForSelected READ storageEnoughForSelected NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForFull READ storageEnoughForFull NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForReduced READ storageEnoughForReduced NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForCustom READ storageEnoughForCustom NOTIFY storageStatusChanged)

public:
    explicit OnboardingOptionsModel(std::vector<std::string> argv, bool can_listen_ipc, QObject* parent = nullptr, execution::SnapshotServices services = {},
        profileinspection::Port inspect = {});
    ~OnboardingOptionsModel() override;

    enum PreparationState {PreparationIdle,PreparationPending,PreparationSucceeded,PreparationFailed,PreparationUncertain,PreparationForwarded};
    Q_ENUM(PreparationState)
    Q_PROPERTY(PreparationState preparationState READ preparationState NOTIFY preparationStateChanged)
    PreparationState preparationState() const {return PreparationState(m_preparation_state->load());}

    enum SetupCommit {SetupIdle,SetupPending,SetupCommitted,SetupFailed,SetupUncertain};Q_ENUM(SetupCommit)
    using CommitPort=std::function<std::pair<bool,QString>(const QmlOnboardingSettings::ApplyRequest&)>;
    void configureSettingsCommit(ArgsManager& args);
    void setSettingsCommitPort(CommitPort port);
    void setProfilePorts(onboarding::Ports ports) { m_profile_ports = std::move(ports); }
    Q_INVOKABLE void querySetupOutcome();
    Q_INVOKABLE void retrySetupChecks() { notifyCanFinish(); }
    Q_INVOKABLE void completeSavedSetup();
    Q_PROPERTY(bool setupPartial READ setupPartial NOTIFY setupCommitChanged)
    Q_PROPERTY(bool setupUncertain READ setupUncertain NOTIFY setupCommitChanged)
    Q_PROPERTY(QString setupAttemptId READ setupAttemptId NOTIFY setupCommitChanged)
    bool setupPartial() const { return m_flow_projection.profile_written && !m_flow_projection.profile_complete; }
    bool setupUncertain() const { return m_flow_projection.phase == onboarding::Phase::Reconciling || m_flow_projection.profile_unknown; }
    QString setupAttemptId() const { return QString::number(m_flow_projection.attempt); }
    Q_INVOKABLE void commitSettings();
    QString unpruneConsentToken()const;
    Q_INVOKABLE void confirmUnpruning(const QString& token);
    bool setupCommitPending()const{return m_flow_command_pending||m_setup_commit->load()==SetupPending;}
    bool setupCommitted()const{return m_flow_projection.profile_complete;}
    QString setupCommitError()const{return m_setup_error;}
    std::shared_ptr<const std::atomic<int>> setupCommitReceipt()const{return m_setup_commit;}
    // Keep args and the preparation callback's dependencies alive until nodePrepared.
    void prepareNode(ArgsManager& args, std::function<bool()> prepare_node);
    // Bounded commands into a stable asynchronous preparation owner. Native
    // transport must not belong to this transient onboarding executor.
    void prepareNodeAsync(ArgsManager& args, onboarding::AsyncPreparation preparation);

    void beginShutdown();
    void close();
    bool presentationOpen() const {return !m_stopping;}
    bool validationPending() const { return m_preview_pending; }
    QString dataDir() const { return m_data_dir; }
    QString getDefaultDataDirString() const;
    QUrl getDefaultDataDirectory() const;
    Q_INVOKABLE QString getCustomDataDirString() const;
    Q_INVOKABLE QString validateCustomDataDir(const QString& path) const;
    Q_INVOKABLE bool selectCustomDataDir(const QString& path);
    Q_INVOKABLE void useDefaultDataDir();

    QString pruneTargetText()const{return m_core_settings.pruneTargetText();}
    QString pruneTargetError()const{return m_core_settings.pruneTargetError();}
    Q_INVOKABLE void editPruneTarget(const QString& text){m_core_settings.changePruneTargetText(text);}
    bool prune() const { return m_core_settings.values().prune; }
    void setPrune(bool prune);
    int pruneSizeGB() const { return m_core_settings.values().prune_size_gb; }
    void setPruneSizeGB(int prune_size_gb);
    bool listen() const { return m_core_settings.values().listen; }
    void setListen(bool listen);
    bool natpmp() const { return m_core_settings.values().natpmp; }
    void setNatpmp(bool natpmp);
    bool server() const { return m_core_settings.values().server; }
    void setServer(bool server);

    bool proxyEnabled() const { return m_core_settings.values().proxy_enabled; }
    void setProxyEnabled(bool enabled);
    QString proxyAddress() const { return m_core_settings.values().proxy_address; }
    void setProxyAddress(const QString& address);
    bool torEnabled() const { return m_core_settings.values().tor_enabled; }
    void setTorEnabled(bool enabled);
    QString torAddress() const { return m_core_settings.values().tor_address; }
    void setTorAddress(const QString& address);

    QObject* coreSettings() { return &m_core_settings; }
    QVariantMap coreSettingStatuses() const { return m_core_settings.statuses(); }
    QString previewError() const { return m_preview_error; }
    bool canFinish() const { return m_flow_ready && !m_flow_command_pending && checksAvailable(); }
    bool dirtyState() const { return false; }
    int assumedBlockchainSize() const { return m_assumed_blockchain_size; }
    int assumedChainstateSize() const { return m_assumed_chainstate_size; }
    bool existingProfile() const { return m_profile.existing_profile; }
    bool storageCheckPending() const { return m_storage_check_pending||m_storage_projection_pending; }
    QString storageStatus() const;
    int storageAvailableGB() const;
    QString storageAvailableText() const;
    QString storagePathMessage() const { return m_storage_path_message; }
    QString storageWarningText() const;
    QString storageErrorText() const { return m_storage_error_text; }
    int storageMinimumRequiredGB() const;
    int fullStorageRequiredGB() const;
    int prunedStorageRequiredGB() const;
    int reducedStorageRequiredGB()const{return m_storage_info.reduced_required_gb;}
    int selectedStorageRequiredGB() const;
    bool storageEnoughForSelected() const;
    bool storageEnoughForFull() const;
    bool storageEnoughForReduced()const{return m_storage_info.enough_for_reduced;}
    bool storageEnoughForCustom()const{return m_storage_info.enough_for_custom;}

    Q_INVOKABLE QString defaultProxyAddress() const;

    QmlOnboardingSettings::ApplyRequest applyRequest() const;

Q_SIGNALS:
    void nodePrepared(bool success, const QString& error);
    void nodePreparationFinished(PreparationState outcome, const QString& error);
    void setupCommitChanged();
    void settingsCommitted();
    void preparationStateChanged();
    void dataDirDefaultsChanged();
    void customDataDirStringChanged(QString path);
    void dataDirChanged(QString path);
    void pruneTargetInputChanged();
    void pruneChanged(bool prune);
    void pruneSizeGBChanged(int prune_size_gb);
    void listenChanged(bool listen);
    void natpmpChanged(bool natpmp);
    void serverChanged(bool server);
    void proxyEnabledChanged(bool enabled);
    void proxyAddressChanged(QString address);
    void torEnabledChanged(bool enabled);
    void torAddressChanged(QString address);
    void coreSettingStatusesChanged();
    void previewErrorChanged();
    void validationPendingChanged();
    void dataDirSelectionFinished(bool success, const QString& error);
    void shutdownFinished();
    void canFinishChanged();
    void assumedSizesChanged();
    void storageStatusChanged();

private:
    bool checksAvailable() const;
    void notifyCanFinish();
    void finishDrain();
    void publishFlow(onboarding::Projection value);
    void finishNodePreparation(bool success, const QString& error);
    void setDataDir(const QString& path);
    void refreshPreview();
    void queuePreview();
    void startPreview();
    void applyPreview(const QmlOnboardingSettings::PreviewResult& preview,uint64_t request_id,quint64 context);
    void setPreviewPending(bool pending);
    void requestStorageCheck();
    void queueStorageCheck();
    void startStorageCheck(uint64_t request_id, const QString& path);
    void applyStorageCheckResult(uint64_t request_id, const QmlDataDir::StorageSpaceResult& result);
    void emitStorageStatusChanged();
    QmlCoreSettings::Values coreValues() const;
    QmlOnboardingStorage::State storageState() const;
    QmlOnboardingStorage::Info storageInfo() const {return m_storage_info;}
    void queueStorageProjection();
    void startStorageProjection();
    void publishShutdown();
    void setPreviewError(const QString& error);
    void invalidateInspection();

    struct FlowMailbox;
    std::shared_ptr<FlowMailbox> m_flow_mailbox;
    std::unique_ptr<onboarding::Coordinator> m_flow;
    onboarding::Ports m_profile_ports;
    onboarding::Projection m_flow_projection;
    quint64 m_flow_revision{0};
    bool m_flow_ready{false}, m_flow_command_pending{false}, m_flow_drained{false}, m_readers_drained{false};
    bool m_backend_drained{false}, m_preparation_ledger_drained{false}, m_preparation_success{false}, m_terminal_scheduled{false};
    QString m_preparation_error;
    UiPreferencesNamespace m_settings_namespace;
    std::shared_ptr<std::atomic<int>> m_setup_commit{std::make_shared<std::atomic<int>>(SetupIdle)};
    QString m_setup_error;
    QString m_unprune_consent;
    std::unique_ptr<BackendExecutor> m_executor;
    execution::SnapshotServices m_services;
    const profileinspection::Port m_inspect;
    profileinspection::Cancellation m_inspection_cancel{std::make_shared<std::atomic_bool>(false)};
    std::unique_ptr<execution::OwnedWork> m_work,m_preparation_work;
    QString m_default_data_dir;
    QUrl m_default_data_directory;
    bool m_preparation_started{false},m_preparation_terminal_pending{false},m_preparation_published{false},m_shutdown_published{false};
    quint64 m_path_revision{0};
    QString m_path_source;
    QmlOnboardingStorage::Info m_storage_info;
    quint64 m_storage_projection_revision{0};
    bool m_storage_projection_pending{false},m_storage_projection_inflight{false};
    std::shared_ptr<std::atomic<int>> m_preparation_state{std::make_shared<std::atomic<int>>(PreparationIdle)};
    bool m_preparing{false};
    bool m_stopping{false};
    bool m_shutdown_requested{false};
    bool m_preview_pending{true};
    bool m_path_pending{false};
    bool m_preview_in_flight{false};
    uint64_t m_preview_request_id{0};
    std::shared_ptr<const std::vector<std::string>> m_argv;
    bool m_can_listen_ipc;
    QString m_data_dir;
    QmlOnboardingSettings::DataDirSource m_data_dir_source{QmlOnboardingSettings::DataDirSource::Default};
    QString m_custom_datadir_string;
    QString m_custom_datadir_display;
    CoreSettingsModel m_core_settings;
    QString m_preview_error;
    QmlOnboardingSettings::ProfileSummary m_profile;
    int m_assumed_blockchain_size{0};
    int m_assumed_chainstate_size{0};
    uint64_t m_storage_request_id{0};
    uint64_t m_storage_available_bytes{0};
    bool m_storage_check_in_flight{false};
    bool m_storage_check_pending{false};
    bool m_storage_result_valid{false};
    bool m_prune_auto_recommended{false};
    QString m_storage_check_path;
    QString m_storage_path_message;
    QString m_storage_error_text;
};

#endif // BITCOIN_QML_MODELS_ONBOARDINGOPTIONSMODEL_H
