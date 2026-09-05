// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/onboardingoptionsmodel.h>

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

namespace {
QmlOnboardingSettings::OnboardingStartupStatus InitialStartupStatus(const std::vector<std::string>& argv, bool can_listen_ipc)
{
    return QmlOnboardingSettings::ResolveOnboardingStartupStatus(argv, can_listen_ipc);
}
} // namespace

OnboardingOptionsModel::OnboardingOptionsModel(std::vector<std::string> argv, bool can_listen_ipc, QObject* parent)
    : QObject{parent}
    , m_argv{std::move(argv)}
    , m_can_listen_ipc{can_listen_ipc}
    , m_data_dir{QmlDataDir::DefaultDataDirString()}
{
    const QmlOnboardingSettings::OnboardingStartupStatus status{InitialStartupStatus(m_argv, m_can_listen_ipc)};
    m_data_dir = status.active_data_dir.isEmpty() ? QmlDataDir::ReadGuiDataDir() : status.active_data_dir;
    m_data_dir_source = status.data_dir_source;

    m_core_settings.setAfterChangeHandler([this](const QmlCoreSettings::Change& change, CoreSettingsModel::ChangeOrigin origin) {
        QmlCoreSettings::EmitCoreSettingSignals(*this, change);
        if (origin == CoreSettingsModel::ChangeOrigin::User &&
            (QmlCoreSettings::PruneChanged(change) || QmlCoreSettings::PruneSizeGBChanged(change))) {
            m_prune_auto_recommended = false;
            emitStorageStatusChanged();
        }
    });

    if (!QmlDataDir::IsDefaultDataDir(m_data_dir)) {
        m_custom_datadir_string = m_data_dir;
    }
    refreshPreview();
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
    return QmlDataDir::ValidateCustomDataDir(path);
}

bool OnboardingOptionsModel::selectCustomDataDir(const QString& path)
{
    const QString local_path = QmlDataDir::NormalizeLocalPath(path);
    const QString error = validateCustomDataDir(local_path);
    if (!error.isEmpty()) {
        setPreviewError(error);
        return false;
    }
    if (local_path != m_custom_datadir_string) {
        m_custom_datadir_string = local_path;
        Q_EMIT customDataDirStringChanged(local_path);
    }
    const bool source_changed = m_data_dir_source != QmlOnboardingSettings::DataDirSource::UserSelection;
    m_data_dir_source = QmlOnboardingSettings::DataDirSource::UserSelection;
    if (source_changed && local_path == m_data_dir) {
        refreshPreview();
        return true;
    }
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
    const bool source_changed = m_data_dir_source != QmlOnboardingSettings::DataDirSource::UserSelection;
    m_data_dir_source = QmlOnboardingSettings::DataDirSource::UserSelection;
    if (source_changed && default_data_dir == m_data_dir) {
        refreshPreview();
        return;
    }
    setDataDir(default_data_dir);
}

void OnboardingOptionsModel::setDataDir(const QString& path)
{
    const QString normalized = QmlDataDir::NormalizeLocalPath(path);
    const QString effective = normalized.isEmpty() ? getDefaultDataDirString() : normalized;
    if (effective == m_data_dir) return;
    m_data_dir = effective;
    Q_EMIT dataDirChanged(m_data_dir);
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
    QPointer<OnboardingOptionsModel> self{this};
    QThread* thread = QThread::create([self, request_id, path] {
        const QmlDataDir::StorageSpaceResult result = QmlDataDir::CheckStorageSpace(path);
        QMetaObject::invokeMethod(QCoreApplication::instance(), [self, request_id, result] {
            if (!self) return;
            self->applyStorageCheckResult(request_id, result);
        }, Qt::QueuedConnection);
    });
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void OnboardingOptionsModel::applyStorageCheckResult(uint64_t request_id, const QmlDataDir::StorageSpaceResult& result)
{
    m_storage_check_in_flight = false;
    if (request_id != m_storage_request_id) {
        startStorageCheck(m_storage_request_id, m_storage_check_path);
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
    const QmlOnboardingSettings::PreviewResult preview = QmlOnboardingSettings::Preview(
        m_argv,
        m_can_listen_ipc,
        QmlOnboardingSettings::DataDirSelection{m_data_dir, m_data_dir_source});
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
