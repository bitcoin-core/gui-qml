// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ONBOARDINGOPTIONSMODEL_H
#define BITCOIN_QML_MODELS_ONBOARDINGOPTIONSMODEL_H

#include <qml/core_settings.h>
#include <qml/models/core_settings_model.h>
#include <qml/onboarding_settings.h>
#include <qml/onboarding_storage.h>

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantMap>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

class ArgsManager;

namespace QmlDataDir {
struct StorageSpaceResult;
} // namespace QmlDataDir

class OnboardingOptionsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString dataDir READ dataDir NOTIFY dataDirChanged)
    Q_PROPERTY(QString getDefaultDataDirString READ getDefaultDataDirString CONSTANT)
    Q_PROPERTY(QUrl getDefaultDataDirectory READ getDefaultDataDirectory CONSTANT)
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
    Q_PROPERTY(QString previewError READ previewError NOTIFY previewErrorChanged)
    Q_PROPERTY(bool canFinish READ canFinish NOTIFY canFinishChanged)
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
    Q_PROPERTY(int selectedStorageRequiredGB READ selectedStorageRequiredGB NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForSelected READ storageEnoughForSelected NOTIFY storageStatusChanged)
    Q_PROPERTY(bool storageEnoughForFull READ storageEnoughForFull NOTIFY storageStatusChanged)

public:
    explicit OnboardingOptionsModel(std::vector<std::string> argv, bool can_listen_ipc, QObject* parent = nullptr);

    QString dataDir() const { return m_data_dir; }
    QString getDefaultDataDirString() const;
    QUrl getDefaultDataDirectory() const;
    Q_INVOKABLE QString getCustomDataDirString() const;
    Q_INVOKABLE QString validateCustomDataDir(const QString& path) const;
    Q_INVOKABLE bool selectCustomDataDir(const QString& path);
    Q_INVOKABLE void useDefaultDataDir();
    Q_INVOKABLE bool setCustomDataDirArgs(QString path) { return selectCustomDataDir(path); }

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
    bool canFinish() const { return m_preview_error.isEmpty() && !m_storage_check_pending && m_storage_error_text.isEmpty(); }
    bool dirtyState() const { return false; }
    int assumedBlockchainSize() const { return m_assumed_blockchain_size; }
    int assumedChainstateSize() const { return m_assumed_chainstate_size; }
    bool existingProfile() const { return m_profile.existing_profile; }
    bool storageCheckPending() const { return m_storage_check_pending; }
    QString storageStatus() const;
    int storageAvailableGB() const;
    QString storageAvailableText() const;
    QString storagePathMessage() const { return m_storage_path_message; }
    QString storageWarningText() const;
    QString storageErrorText() const { return m_storage_error_text; }
    int storageMinimumRequiredGB() const;
    int fullStorageRequiredGB() const;
    int prunedStorageRequiredGB() const;
    int selectedStorageRequiredGB() const;
    bool storageEnoughForSelected() const;
    bool storageEnoughForFull() const;

    Q_INVOKABLE QString validateProxyLocation(const QString& location) const;
    Q_INVOKABLE bool commitProxyLocation(const QString& location);
    Q_INVOKABLE bool commitTorLocation(const QString& location);
    Q_INVOKABLE QString defaultProxyAddress() const;

    bool applyToArgs(ArgsManager& args, QString* error = nullptr) const;

Q_SIGNALS:
    void customDataDirStringChanged(QString path);
    void dataDirChanged(QString path);
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
    void canFinishChanged();
    void assumedSizesChanged();
    void storageStatusChanged();

private:
    void setDataDir(const QString& path);
    void refreshPreview();
    void applyPreviewValues(const QmlCoreSettings::Values& values, const QVariantMap& statuses);
    void requestStorageCheck();
    void startStorageCheck(uint64_t request_id, const QString& path);
    void applyStorageCheckResult(uint64_t request_id, const QmlDataDir::StorageSpaceResult& result);
    void applyAutomaticPruneRecommendation();
    void emitStorageStatusChanged();
    QmlCoreSettings::Values coreValues() const;
    QmlOnboardingStorage::State storageState() const;
    QmlOnboardingStorage::Info storageInfo() const;
    void setPreviewError(const QString& error);

    std::vector<std::string> m_argv;
    bool m_can_listen_ipc;
    QString m_data_dir;
    QmlOnboardingSettings::DataDirSource m_data_dir_source{QmlOnboardingSettings::DataDirSource::Default};
    QString m_custom_datadir_string;
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
