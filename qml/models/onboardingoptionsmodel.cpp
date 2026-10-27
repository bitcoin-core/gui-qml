// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/onboardingoptionsmodel.h>

#include <qml/backendexecutor.h>
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
#include <utility>

OnboardingOptionsModel::OnboardingOptionsModel(std::vector<std::string> argv, bool can_listen_ipc, QObject* parent)
    : QObject{parent}
    , m_executor{std::make_unique<BackendExecutor>()}
    , m_argv{std::move(argv)}
    , m_can_listen_ipc{can_listen_ipc}
    , m_data_dir{QmlDataDir::DefaultDataDirString()}
{
    m_executor->setObjectName(QStringLiteral("onboarding"));
    m_core_settings.setAfterChangeHandler([this](const QmlCoreSettings::Change& change, CoreSettingsModel::ChangeOrigin origin) {
        QmlCoreSettings::EmitCoreSettingSignals(*this, change);
        if (origin == CoreSettingsModel::ChangeOrigin::User &&
            (QmlCoreSettings::PruneChanged(change) || QmlCoreSettings::PruneSizeGBChanged(change))) {
            m_prune_auto_recommended = false;
            emitStorageStatusChanged();
        }
    });

    m_preview_in_flight = true;
    const auto request_id = ++m_preview_request_id;
    m_executor->submit(this, [argv = m_argv, can_listen_ipc = m_can_listen_ipc] {
        return QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, can_listen_ipc);
    }, [this, request_id](const QmlOnboardingSettings::OnboardingStartupStatus& status) {
        m_preview_in_flight = false;
        if (request_id != m_preview_request_id) {
            startPreview();
            return;
        }
        if (!status.ok) {
            setPreviewError(status.error);
            setPreviewPending(false);
            return;
        }
        m_data_dir = status.active_data_dir;
        m_data_dir_source = status.data_dir_source;
        m_custom_datadir_string = QmlDataDir::IsDefaultDataDir(m_data_dir) ? QString{} : m_data_dir;
        Q_EMIT dataDirChanged(m_data_dir);
        Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
        startPreview();
    }, [this](std::exception_ptr) {
        m_preview_in_flight = false;
        setPreviewError(tr("The startup settings could not be read."));
        setPreviewPending(false);
    });
}

void OnboardingOptionsModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    ++m_preview_request_id;
    ++m_storage_request_id;
    connect(m_executor.get(), &BackendExecutor::drained, this, &OnboardingOptionsModel::shutdownFinished);
    m_executor->shutdown();
    Q_EMIT canFinishChanged();
}

void OnboardingOptionsModel::setPreviewPending(bool pending)
{
    if (m_preview_pending == pending) return;
    m_preview_pending = pending;
    Q_EMIT validationPendingChanged();
    Q_EMIT canFinishChanged();
}

OnboardingOptionsModel::~OnboardingOptionsModel() = default;

void OnboardingOptionsModel::prepareNode(ArgsManager& args, std::function<bool()> prepare_node)
{
    if (m_preparing) return;
    m_preparing = true;
    m_stopping = true;
    ++m_preview_request_id;
    ++m_storage_request_id;
    Q_EMIT canFinishChanged();
    m_executor->submit(this, [&args, request = applyRequest(), prepare_node = std::move(prepare_node)] {
        QString error;
        if (!QmlOnboardingSettings::ApplyToArgs(args, request.data_dir, request.touched_settings, request.values, &error)) {
            if (error.isEmpty()) error = tr("Onboarding settings could not be applied.");
            return std::pair{false, error};
        }
        return std::pair{prepare_node(), QString{}};
    }, [this](const std::pair<bool, QString>& result) {
        finishNodePreparation(result.first, result.second);
    }, [this](std::exception_ptr failure) {
        try {
            std::rethrow_exception(failure);
        } catch (const std::exception& e) {
            finishNodePreparation(false, QString::fromUtf8(e.what()));
        } catch (...) {
            finishNodePreparation(false, tr("Unknown exception preparing the node."));
        }
    });
}

void OnboardingOptionsModel::finishNodePreparation(bool success, const QString& error)
{
    connect(m_executor.get(), &BackendExecutor::drained, this, [this, success, error] {
        Q_EMIT nodePrepared(success, error);
    });
    m_executor->shutdown();
}

QString OnboardingOptionsModel::getDefaultDataDirString() const
{
    return QmlDataDir::DefaultDataDirString();
}

QUrl OnboardingOptionsModel::getDefaultDataDirectory() const
{
    return QUrl::fromLocalFile(getDefaultDataDirString());
}

QString OnboardingOptionsModel::getCustomDataDirString() const
{
#ifdef __ANDROID__
    QString android_path = m_custom_datadir_string;
    return android_path.replace("content://com.android.externalstorage.documents/tree/primary%3A", "/storage/self/primary/");
#else
    return m_custom_datadir_string;
#endif
}

QString OnboardingOptionsModel::validateCustomDataDir(const QString& path) const
{
    return QmlDataDir::NormalizeLocalPath(path) == m_data_dir ? m_preview_error : tr("The directory has not been checked.");
}

bool OnboardingOptionsModel::selectCustomDataDir(const QString& path)
{
    const QString local_path = QmlDataDir::NormalizeLocalPath(path);
    if (m_stopping || local_path.isEmpty()) return false;
    if (local_path != m_custom_datadir_string) {
        m_custom_datadir_string = local_path;
        Q_EMIT customDataDirStringChanged(local_path);
    }
    m_data_dir_source = QmlOnboardingSettings::DataDirSource::UserSelection;
    setDataDir(local_path);
    return true;
}

void OnboardingOptionsModel::useDefaultDataDir()
{
    if (!m_custom_datadir_string.isEmpty()) {
        m_custom_datadir_string.clear();
        Q_EMIT customDataDirStringChanged({});
    }
    const QString default_data_dir = getDefaultDataDirString();
    m_data_dir_source = QmlOnboardingSettings::DataDirSource::UserSelection;
    setDataDir(default_data_dir);
}

void OnboardingOptionsModel::setDataDir(const QString& path)
{
    const QString normalized = QmlDataDir::NormalizeLocalPath(path);
    const QString effective = normalized.isEmpty() ? getDefaultDataDirString() : normalized;
    if (effective != m_data_dir) {
        m_data_dir = effective;
        Q_EMIT dataDirChanged(m_data_dir);
    }
    // An explicit selection also retries a directory whose contents or permissions changed.
    refreshPreview();
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
    commitProxyLocation(address);
}

void OnboardingOptionsModel::setTorEnabled(bool enabled)
{
    m_core_settings.changeTorEnabled(enabled);
}

void OnboardingOptionsModel::setTorAddress(const QString& address)
{
    commitTorLocation(address);
}

QString OnboardingOptionsModel::validateProxyLocation(const QString& location) const
{
    return m_core_settings.validateProxyLocation(location);
}

bool OnboardingOptionsModel::commitProxyLocation(const QString& location)
{
    const QmlCoreSettings::Change change = m_core_settings.changeProxyLocation(location);
    return change.accepted;
}

bool OnboardingOptionsModel::commitTorLocation(const QString& location)
{
    const QmlCoreSettings::Change change = m_core_settings.changeTorLocation(location);
    return change.accepted;
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
    ++m_storage_request_id;
    m_storage_check_path = m_data_dir;
    m_storage_check_pending = true;
    emitStorageStatusChanged();
    if (!m_storage_check_in_flight) {
        startStorageCheck(m_storage_request_id, m_storage_check_path);
    }
}

void OnboardingOptionsModel::startStorageCheck(uint64_t request_id, const QString& path)
{
    m_storage_check_in_flight = true;
    m_executor->submit(this, [path] { return QmlDataDir::CheckStorageSpace(path); },
        [this, request_id](const QmlDataDir::StorageSpaceResult& result) { applyStorageCheckResult(request_id, result); },
        [this, request_id](std::exception_ptr) {
            QmlDataDir::StorageSpaceResult result;
            result.message = tr("Available storage could not be checked.");
            applyStorageCheckResult(request_id, result);
        });
}

void OnboardingOptionsModel::applyStorageCheckResult(uint64_t request_id, const QmlDataDir::StorageSpaceResult& result)
{
    m_storage_check_in_flight = false;
    if (request_id != m_storage_request_id) {
        if (!m_preview_pending && !m_stopping) startStorageCheck(m_storage_request_id, m_storage_check_path);
        return;
    }

    m_storage_check_pending = false;
    m_storage_result_valid = result.ok;
    m_storage_available_bytes = result.ok ? result.available_bytes : 0;
    m_storage_path_message = result.message;
    m_storage_error_text = result.ok ? QString{} : result.message;
    applyAutomaticPruneRecommendation();
    emitStorageStatusChanged();
}

void OnboardingOptionsModel::applyAutomaticPruneRecommendation()
{
    if (!m_storage_result_valid || !m_storage_error_text.isEmpty()) return;
    if (m_profile.existing_profile) return;

    const QVariantMap prune_status = m_core_settings.statuses().value(QStringLiteral("prune")).toMap();
    if (!prune_status.value(QStringLiteral("canEdit"), true).toBool()) return;
    if (prune_status.value(QStringLiteral("source")).toString() != QStringLiteral("default")) return;

    const bool user_touched_prune = m_core_settings.isTouched(QStringLiteral("prune")) && !m_prune_auto_recommended;
    if (user_touched_prune) return;

    const bool should_prune = QmlOnboardingStorage::ShouldRecommendPrune(storageState());
    m_core_settings.changePruneRecommendation(should_prune, DEFAULT_PRUNE_TARGET_GB, should_prune);
    m_prune_auto_recommended = should_prune;
}

void OnboardingOptionsModel::emitStorageStatusChanged()
{
    Q_EMIT storageStatusChanged();
    Q_EMIT canFinishChanged();
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
    return state;
}

QmlOnboardingStorage::Info OnboardingOptionsModel::storageInfo() const
{
    return QmlOnboardingStorage::Evaluate(storageState());
}

QmlCoreSettings::Values OnboardingOptionsModel::coreValues() const
{
    return m_core_settings.values();
}

void OnboardingOptionsModel::setPreviewError(const QString& error)
{
    if (error == m_preview_error) return;
    m_preview_error = error;
    Q_EMIT previewErrorChanged();
    Q_EMIT canFinishChanged();
}

void OnboardingOptionsModel::applyPreviewValues(const QmlCoreSettings::Values& values, const QVariantMap& statuses)
{
    m_core_settings.applyPreviewValuesPreservingTouched(values, statuses);
}

void OnboardingOptionsModel::refreshPreview()
{
    if (m_stopping) return;
    ++m_preview_request_id;
    ++m_storage_request_id;
    setPreviewPending(true);
    if (!m_preview_in_flight) startPreview();
}

void OnboardingOptionsModel::startPreview()
{
    if (m_stopping) return;
    m_preview_in_flight = true;
    const auto request_id = m_preview_request_id;
    m_executor->submit(this, [argv = m_argv, ipc = m_can_listen_ipc,
        selection = QmlOnboardingSettings::DataDirSelection{m_data_dir, m_data_dir_source}] {
        return QmlOnboardingSettings::Preview(argv, ipc, selection);
    }, [this, request_id](const QmlOnboardingSettings::PreviewResult& preview) {
        m_preview_in_flight = false;
        if (request_id != m_preview_request_id) {
            startPreview();
            return;
        }
        applyPreview(preview);
        setPreviewPending(false);
        Q_EMIT dataDirSelectionFinished(preview.ok, preview.error);
    }, [this, request_id](std::exception_ptr) {
        m_preview_in_flight = false;
        if (request_id != m_preview_request_id) {
            startPreview();
            return;
        }
        const QString error = tr("The selected directory could not be checked.");
        setPreviewError(error);
        setPreviewPending(false);
        Q_EMIT dataDirSelectionFinished(false, error);
    });
}

void OnboardingOptionsModel::applyPreview(const QmlOnboardingSettings::PreviewResult& preview)
{
    if (!preview.ok) {
        setPreviewError(preview.error);
        return;
    }

    const int old_assumed_blockchain_size = m_assumed_blockchain_size;
    const int old_assumed_chainstate_size = m_assumed_chainstate_size;
    const bool old_existing_profile = m_profile.existing_profile;
    m_assumed_blockchain_size = preview.assumed_blockchain_size;
    m_assumed_chainstate_size = preview.assumed_chainstate_size;
    m_profile = preview.profile;
    if (m_profile.existing_profile && m_prune_auto_recommended) {
        m_core_settings.changePruneRecommendation(preview.values.prune, preview.values.prune_size_gb, /*mark_touched=*/false);
        m_prune_auto_recommended = false;
    }
    if (old_assumed_blockchain_size != m_assumed_blockchain_size || old_assumed_chainstate_size != m_assumed_chainstate_size) {
        Q_EMIT assumedSizesChanged();
        emitStorageStatusChanged();
    }
    if (old_existing_profile != m_profile.existing_profile) {
        emitStorageStatusChanged();
    }

    applyPreviewValues(preview.values, preview.core_setting_statuses);
    setPreviewError({});
    requestStorageCheck();
}

bool OnboardingOptionsModel::applyToArgs(ArgsManager& args, QString* error) const
{
    return QmlOnboardingSettings::ApplyToArgs(
        args,
        QmlOnboardingSettings::DataDirSelection{m_data_dir, m_data_dir_source},
        m_core_settings.touchedSettings(),
        coreValues(),
        error);
}

QmlOnboardingSettings::ApplyRequest OnboardingOptionsModel::applyRequest() const
{
    return {{m_data_dir, m_data_dir_source}, m_core_settings.touchedSettings(), coreValues()};
}
