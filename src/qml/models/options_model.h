// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_OPTIONS_MODEL_H
#define BITCOIN_QML_MODELS_OPTIONS_MODEL_H

#include <common/args.h>
#include <txdb.h>
#include <common/settings.h>
#include <node/caches.h>
#include <kernel/caches.h>
#include <kernel/mempool_options.h>
#include <common/system.h>
#include <policy/policy.h>
#include <validation.h>

#include <qml/core_settings.h>
#include <qml/models/core_settings_model.h>
#include <qml/models/settings_keys.h>

#include <QObject>
#include <QFont>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>
#include <QUrl>

namespace interfaces {
class Node;
}

/** Model for Bitcoin client options. */
class OptionsQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int dbcacheSizeMiB READ dbcacheSizeMiB WRITE setDbcacheSizeMiB NOTIFY dbcacheSizeMiBChanged)
    Q_PROPERTY(bool listen READ listen WRITE setListen NOTIFY listenChanged)
    Q_PROPERTY(int maxMempoolSizeMB READ maxMempoolSizeMB WRITE setMaxMempoolSizeMB NOTIFY maxMempoolSizeMBChanged)
    Q_PROPERTY(int maxMaxMempoolSizeMB READ maxMaxMempoolSizeMB CONSTANT)
    Q_PROPERTY(int minMaxMempoolSizeMB READ minMaxMempoolSizeMB CONSTANT)
    Q_PROPERTY(int maxDbcacheSizeMiB READ maxDbcacheSizeMiB CONSTANT)
    Q_PROPERTY(int minDbcacheSizeMiB READ minDbcacheSizeMiB CONSTANT)
    Q_PROPERTY(int maxScriptThreads READ maxScriptThreads CONSTANT)
    Q_PROPERTY(int minScriptThreads READ minScriptThreads CONSTANT)
    Q_PROPERTY(bool natpmp READ natpmp WRITE setNatpmp NOTIFY natpmpChanged)
    Q_PROPERTY(bool prune READ prune WRITE setPrune NOTIFY pruneChanged)
    Q_PROPERTY(int pruneSizeGB READ pruneSizeGB WRITE setPruneSizeGB NOTIFY pruneSizeGBChanged)
    Q_PROPERTY(int scriptThreads READ scriptThreads WRITE setScriptThreads NOTIFY scriptThreadsChanged)
    Q_PROPERTY(bool server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(QString dataDir READ dataDir WRITE setDataDir NOTIFY dataDirChanged)
    Q_PROPERTY(QString getDefaultDataDirString READ getDefaultDataDirString CONSTANT)
    Q_PROPERTY(QUrl getDefaultDataDirectory READ getDefaultDataDirectory CONSTANT)
    Q_PROPERTY(bool proxyEnabled READ proxyEnabled WRITE setProxyEnabled NOTIFY proxyEnabledChanged)
    Q_PROPERTY(QString proxyAddress READ proxyAddress WRITE setProxyAddress NOTIFY proxyAddressChanged)
    Q_PROPERTY(bool torEnabled READ torEnabled WRITE setTorEnabled NOTIFY torEnabledChanged)
    Q_PROPERTY(QString torAddress READ torAddress WRITE setTorAddress NOTIFY torAddressChanged)
    Q_PROPERTY(bool proxySettingsDirty READ proxySettingsDirty NOTIFY proxySettingsDirtyChanged)
    Q_PROPERTY(bool connectionSettingsDirty READ connectionSettingsDirty NOTIFY connectionSettingsDirtyChanged)
    Q_PROPERTY(bool storageSettingsDirty READ storageSettingsDirty NOTIFY storageSettingsDirtyChanged)
    Q_PROPERTY(bool developerSettingsDirty READ developerSettingsDirty NOTIFY developerSettingsDirtyChanged)
    Q_PROPERTY(bool mempoolSettingsDirty READ mempoolSettingsDirty NOTIFY mempoolSettingsDirtyChanged)
    Q_PROPERTY(bool restartRequired READ restartRequired NOTIFY restartRequiredChanged)
    Q_PROPERTY(QObject* coreSettings READ coreSettings CONSTANT)
    Q_PROPERTY(QVariantMap coreSettingStatuses READ coreSettingStatuses NOTIFY coreSettingStatusesChanged)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(QString displayUnitLabel READ displayUnitLabel NOTIFY displayUnitChanged)

public:
    explicit OptionsQmlModel(interfaces::Node& node, ArgsManager& args = gArgs);

    int dbcacheSizeMiB() const { return m_dbcache_size_mib; }
    void setDbcacheSizeMiB(int new_dbcache_size_mib);
    bool listen() const { return m_core_settings.values().listen; }
    void setListen(bool new_listen);
    int maxMempoolSizeMB() const { return m_max_mempool_size_mb; }
    void setMaxMempoolSizeMB(int new_max_mempool_size_mb);
    int maxMaxMempoolSizeMB() const { return m_max_max_mempool_size_mb; }
    int minMaxMempoolSizeMB() const { return m_min_max_mempool_size_mb; }
    int maxDbcacheSizeMiB() const { return m_max_dbcache_size_mib; }
    int minDbcacheSizeMiB() const { return m_min_dbcache_size_mib; }
    int maxScriptThreads() const { return m_max_script_threads; }
    int minScriptThreads() const { return m_min_script_threads; }
    bool natpmp() const { return m_core_settings.values().natpmp; }
    void setNatpmp(bool new_natpmp);
    bool prune() const { return m_core_settings.values().prune; }
    void setPrune(bool new_prune);
    int pruneSizeGB() const { return m_core_settings.values().prune_size_gb; }
    void setPruneSizeGB(int new_prune_size);
    int scriptThreads() const { return m_script_threads; }
    void setScriptThreads(int new_script_threads);
    bool server() const { return m_core_settings.values().server; }
    void setServer(bool new_server);
    QString dataDir() const { return m_dataDir; }
    void setDataDir(QString new_data_dir);
    QString getDefaultDataDirString();
    QUrl getDefaultDataDirectory();
    Q_INVOKABLE bool setCustomDataDirArgs(QString path);
    Q_INVOKABLE QString getCustomDataDirString();
    Q_INVOKABLE QString validateCustomDataDir(const QString& path) const;
    Q_INVOKABLE bool selectCustomDataDir(const QString& path);
    Q_INVOKABLE void useDefaultDataDir();
    bool proxyEnabled() const { return m_core_settings.values().proxy_enabled; }
    void setProxyEnabled(bool enabled);
    QString proxyAddress() const { return m_core_settings.values().proxy_address; }
    void setProxyAddress(const QString& address);
    bool torEnabled() const { return m_core_settings.values().tor_enabled; }
    void setTorEnabled(bool enabled);
    QString torAddress() const { return m_core_settings.values().tor_address; }
    void setTorAddress(const QString& address);
    bool proxySettingsDirty() const {
        const QmlCoreSettings::Values& values = m_core_settings.values();
        if (values.proxy_enabled != m_initial_core_values.proxy_enabled) return true;
        if (values.proxy_enabled && values.proxy_address != m_initial_core_values.proxy_address) return true;
        if (values.tor_enabled != m_initial_core_values.tor_enabled) return true;
        if (values.tor_enabled && values.tor_address != m_initial_core_values.tor_address) return true;
        return false;
    }
    bool connectionSettingsDirty() const;
    bool storageSettingsDirty() const;
    bool developerSettingsDirty() const;
    bool mempoolSettingsDirty() const {
        return m_max_mempool_size_mb != m_initial_max_mempool_size_mb;
    }
    bool restartRequired() const;
    QObject* coreSettings() { return &m_core_settings; }
    QVariantMap coreSettingStatuses() const;
    Q_INVOKABLE QVariantMap coreSettingStatus(const QString& name) const;
    Q_INVOKABLE QString validateProxyLocation(const QString& location) const;
    Q_INVOKABLE bool commitProxyLocation(const QString& location);
    Q_INVOKABLE bool commitTorLocation(const QString& location);
    Q_INVOKABLE QString defaultProxyAddress() const;
    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int new_display_unit);
    QString displayUnitLabel() const;
    Q_INVOKABLE QString displayUnitLabelForAmount(qint64 satoshi) const;

public Q_SLOTS:
    void setCustomDataDirString(const QString &new_custom_datadir_string) {
        m_custom_datadir_string = new_custom_datadir_string;
    }

Q_SIGNALS:
    void dbcacheSizeMiBChanged(int new_dbcache_size_mib);
    void listenChanged(bool new_listen);
    void maxMempoolSizeMBChanged(int new_max_mempool_size_mb);
    void natpmpChanged(bool new_natpmp);
    void pruneChanged(bool new_prune);
    void pruneSizeGBChanged(int new_prune_size_gb);
    void scriptThreadsChanged(int new_script_threads);
    void serverChanged(bool new_server);
    void customDataDirStringChanged(QString new_custom_datadir_string);
    void dataDirChanged(QString new_data_dir);
    void proxyEnabledChanged(bool enabled);
    void proxyAddressChanged(QString address);
    void torEnabledChanged(bool enabled);
    void torAddressChanged(QString address);
    void proxySettingsDirtyChanged();
    void connectionSettingsDirtyChanged();
    void storageSettingsDirtyChanged();
    void developerSettingsDirtyChanged();
    void mempoolSettingsDirtyChanged();
    void restartRequiredChanged();
    void coreSettingStatusesChanged();
    void displayUnitChanged(int new_display_unit);

private:
    struct DirtySnapshot {
        bool connection{false};
        bool storage{false};
        bool developer{false};
        bool mempool{false};
        bool proxy{false};
        bool restart{false};
    };

    interfaces::Node& m_node;
    ArgsManager& m_args;
    CoreSettingsModel m_core_settings;

    // Properties that are exposed to QML.
    int m_dbcache_size_mib;
    const int m_min_dbcache_size_mib{MIN_DBCACHE_BYTES >> 20};
    const int m_max_dbcache_size_mib{MAX_COINS_DB_CACHE >> 20};
    int m_max_mempool_size_mb;
    const int m_min_max_mempool_size_mb{
        static_cast<int>((DEFAULT_CLUSTER_SIZE_LIMIT_KVB * 1000 * 40 + 999999) / 1000000)
    };
    const int m_max_max_mempool_size_mb{
        sizeof(void*) <= 4 ? 500 : 99999
    };
    const int m_max_script_threads{MAX_SCRIPTCHECK_THREADS};
    const int m_min_script_threads{-GetNumCores()};
    int m_script_threads;
    QString m_custom_datadir_string;
    QString m_dataDir;
    QmlCoreSettings::Values m_initial_core_values;
    int m_initial_dbcache_size_mib;
    int m_initial_max_mempool_size_mb;
    int m_initial_script_threads;
    int m_display_unit{0};
    DirtySnapshot m_core_change_dirty_snapshot;
    QVariantMap m_core_setting_statuses;

    common::SettingsValue currentCoreSettingValue(const QString& name) const;
    bool canEditCoreSetting(const QString& name) const;
    bool writeCoreSettingOverride(const QString& name, const common::SettingsValue& value);
    void refreshCoreSettingStatuses();
    void resetDirtySnapshots();
    DirtySnapshot dirtySnapshot() const;
    void emitDirtySignals(const DirtySnapshot& before);
    void applyRuntimeCoreChange(const QmlCoreSettings::Change& change, const DirtySnapshot& before);
};

#endif // BITCOIN_QML_MODELS_OPTIONS_MODEL_H
