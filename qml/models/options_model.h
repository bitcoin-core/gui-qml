// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_OPTIONS_MODEL_H
#define BITCOIN_QML_MODELS_OPTIONS_MODEL_H

#include <txdb.h>
#include <common/settings.h>
#include <node/caches.h>
#include <kernel/caches.h>
#include <kernel/mempool_options.h>
#include <common/system.h>
#include <policy/policy.h>
#include <validation.h>

#include <qml/models/settings_keys.h>

#include <QObject>
#include <QString>
#include <QStringList>
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
    Q_PROPERTY(QString externalSignerPath READ externalSignerPath WRITE setExternalSignerPath NOTIFY externalSignerPathChanged)
    Q_PROPERTY(bool proxySettingsDirty READ proxySettingsDirty NOTIFY proxySettingsDirtyChanged)
    Q_PROPERTY(bool walletSettingsDirty READ walletSettingsDirty NOTIFY walletSettingsDirtyChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QString languageSummary READ languageSummary NOTIFY languageChanged)
    Q_PROPERTY(QStringList availableLanguages READ availableLanguages CONSTANT)
    Q_PROPERTY(int displayUnit READ displayUnit WRITE setDisplayUnit NOTIFY displayUnitChanged)
    Q_PROPERTY(QString displayUnitLabel READ displayUnitLabel NOTIFY displayUnitChanged)

public:
    explicit OptionsQmlModel(interfaces::Node& node, bool is_onboarded);

    int dbcacheSizeMiB() const { return m_dbcache_size_mib; }
    void setDbcacheSizeMiB(int new_dbcache_size_mib);
    bool listen() const { return m_listen; }
    void setListen(bool new_listen);
    int maxMempoolSizeMB() const { return m_max_mempool_size_mb; }
    void setMaxMempoolSizeMB(int new_max_mempool_size_mb);
    int maxMaxMempoolSizeMB() const { return m_max_max_mempool_size_mb; }
    int minMaxMempoolSizeMB() const { return m_min_max_mempool_size_mb; }
    int maxDbcacheSizeMiB() const { return m_max_dbcache_size_mib; }
    int minDbcacheSizeMiB() const { return m_min_dbcache_size_mib; }
    int maxScriptThreads() const { return m_max_script_threads; }
    int minScriptThreads() const { return m_min_script_threads; }
    bool natpmp() const { return m_natpmp; }
    void setNatpmp(bool new_natpmp);
    bool prune() const { return m_prune; }
    void setPrune(bool new_prune);
    int pruneSizeGB() const { return m_prune_size_gb; }
    void setPruneSizeGB(int new_prune_size);
    int scriptThreads() const { return m_script_threads; }
    void setScriptThreads(int new_script_threads);
    bool server() const { return m_server; }
    void setServer(bool new_server);
    QString dataDir() const { return m_dataDir; }
    void setDataDir(QString new_data_dir);
    QString getDefaultDataDirString();
    QUrl getDefaultDataDirectory();
    Q_INVOKABLE bool setCustomDataDirArgs(QString path);
    Q_INVOKABLE QString getCustomDataDirString();
    Q_INVOKABLE QString externalSignerPathValidationError(const QString& path) const;
    bool proxyEnabled() const { return m_proxy_enabled; }
    void setProxyEnabled(bool enabled);
    QString proxyAddress() const { return m_proxy_address; }
    void setProxyAddress(const QString& address);
    bool torEnabled() const { return m_tor_enabled; }
    void setTorEnabled(bool enabled);
    QString torAddress() const { return m_tor_address; }
    void setTorAddress(const QString& address);
    QString externalSignerPath() const { return m_external_signer_path; }
    void setExternalSignerPath(const QString& path);
    bool proxySettingsDirty() const {
        if (!m_onboarded) return false;
        if (m_proxy_enabled != m_initial_proxy_enabled) return true;
        if (m_proxy_enabled && m_proxy_address != m_initial_proxy_address) return true;
        if (m_tor_enabled != m_initial_tor_enabled) return true;
        if (m_tor_enabled && m_tor_address != m_initial_tor_address) return true;
        return false;
    }
    bool walletSettingsDirty() const {
        if (!m_onboarded) return false;
        return m_external_signer_path != m_initial_external_signer_path;
    }
    QString language() const { return m_language; }
    void setLanguage(const QString& new_language);
    QString languageSummary() const;
    QStringList availableLanguages() const { return m_available_languages; }
    Q_INVOKABLE QString languageLabel(const QString& locale_tag) const;
    int displayUnit() const { return m_display_unit; }
    void setDisplayUnit(int new_display_unit);
    QString displayUnitLabel() const;
    Q_INVOKABLE QString displayUnitLabelForAmount(qint64 satoshi) const;

public Q_SLOTS:
    void setCustomDataDirString(const QString &new_custom_datadir_string) {
        m_custom_datadir_string = new_custom_datadir_string;
    }
    Q_INVOKABLE void onboard();

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
    void externalSignerPathChanged(QString path);
    void proxySettingsDirtyChanged();
    void walletSettingsDirtyChanged();
    void languageChanged();
    void displayUnitChanged(int new_display_unit);

private:
    interfaces::Node& m_node;
    bool m_onboarded;

    // Properties that are exposed to QML.
    int m_dbcache_size_mib;
    const int m_min_dbcache_size_mib{MIN_DB_CACHE >> 20};
    const int m_max_dbcache_size_mib{MAX_COINS_DB_CACHE >> 20};
    bool m_listen;
    int m_max_mempool_size_mb;
    const int m_min_max_mempool_size_mb{
        static_cast<int>((DEFAULT_CLUSTER_SIZE_LIMIT_KVB * 1000 * 40 + 999999) / 1000000)
    };
    const int m_max_max_mempool_size_mb{
        sizeof(void*) <= 4 ? 500 : 99999
    };
    const int m_max_script_threads{MAX_SCRIPTCHECK_THREADS};
    const int m_min_script_threads{-GetNumCores()};
    bool m_natpmp;
    bool m_prune;
    int m_prune_size_gb;
    int m_script_threads;
    bool m_server;
    QString m_custom_datadir_string;
    QString m_dataDir;
    bool m_proxy_enabled;
    QString m_proxy_address;
    bool m_tor_enabled;
    QString m_tor_address;
    QString m_external_signer_path;
    bool m_initial_proxy_enabled;
    QString m_initial_proxy_address;
    bool m_initial_tor_enabled;
    QString m_initial_tor_address;
    QString m_initial_external_signer_path;
    QString m_language;
    QStringList m_available_languages;
    int m_display_unit{0};

    common::SettingsValue pruneSetting() const;
    void buildAvailableLanguages();
};

#endif // BITCOIN_QML_MODELS_OPTIONS_MODEL_H
