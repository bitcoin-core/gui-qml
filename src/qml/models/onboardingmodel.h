// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.


#ifndef BITCOIN_QML_MODELS_ONBOARDINGMODEL_H
#define BITCOIN_QML_MODELS_ONBOARDINGMODEL_H


#include <common/settings.h>
#include <common/system.h>
#include <chainparams.h>
#include <clientversion.h>
#include <interfaces/chain.h>
#include <univalue.h>
#include <validation.h>

#include <string>

#include <QObject>
#include <QString>
#include <QUrl>

class OnboardingModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString getDefaultDataDirString READ getDefaultDataDirString CONSTANT)
    Q_PROPERTY(QUrl getDefaultDataDirectory READ getDefaultDataDirectory CONSTANT)
    Q_PROPERTY(bool prune READ prune WRITE setPrune NOTIFY pruneChanged)
    Q_PROPERTY(int pruneSizeGB READ pruneSizeGB WRITE setPruneSizeGB NOTIFY pruneSizeGBChanged)
    Q_PROPERTY(int dbcacheSizeMiB READ dbcacheSizeMiB WRITE setDbcacheSizeMiB NOTIFY dbcacheSizeMiBChanged)
    Q_PROPERTY(bool listen READ listen WRITE setListen NOTIFY listenChanged)
    Q_PROPERTY(int maxDbcacheSizeMiB READ maxDbcacheSizeMiB CONSTANT)
    Q_PROPERTY(int minDbcacheSizeMiB READ minDbcacheSizeMiB CONSTANT)
    Q_PROPERTY(int maxScriptThreads READ maxScriptThreads CONSTANT)
    Q_PROPERTY(int minScriptThreads READ minScriptThreads CONSTANT)
    Q_PROPERTY(bool natpmp READ natpmp WRITE setNatpmp NOTIFY natpmpChanged)
    Q_PROPERTY(int scriptThreads READ scriptThreads WRITE setScriptThreads NOTIFY scriptThreadsChanged)
    Q_PROPERTY(bool server READ server WRITE setServer NOTIFY serverChanged)
    Q_PROPERTY(bool upnp READ upnp WRITE setUpnp NOTIFY upnpChanged)
    Q_PROPERTY(quint64 assumedBlockchainSize READ assumedBlockchainSize CONSTANT)
    Q_PROPERTY(quint64 assumedChainstateSize READ assumedChainstateSize CONSTANT)
    Q_PROPERTY(QString fullClientVersion READ fullClientVersion CONSTANT)


public:
    explicit OnboardingModel();

    int dbcacheSizeMiB() const { return m_dbcache_size_mib; }
    void setDbcacheSizeMiB(int new_dbcache_size_mib);
    bool listen() const { return m_listen; }
    void setListen(bool new_listen);
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
    bool upnp() const { return m_upnp; }
    void setUpnp(bool new_upnp);
    quint64 assumedBlockchainSize() const { return m_assumed_blockchain_size; };
    quint64 assumedChainstateSize() const { return m_assumed_chainstate_size; };
    QString fullClientVersion() const { return QString::fromStdString(FormatFullVersion()); }
    QString getDefaultDataDirString();
    QUrl getDefaultDataDirectory();
    Q_INVOKABLE void defaultReset();
    Q_INVOKABLE void setCustomDataDirArgs(QString path);
    Q_INVOKABLE void requestShutdown();

public Q_SLOTS:
    void setCustomDataDirString(const QString &new_custom_datadir_string) {
        m_custom_datadir_string = new_custom_datadir_string;
        m_signalReceived = true;
    }

Q_SIGNALS:
    void customDataDirStringChanged(QString new_custom_datadir_string);
    void onboardingFinished();
    void requestedShutdown();
    void dbcacheSizeMiBChanged(int new_dbcache_size_mib);
    void listenChanged(bool new_listen);
    void natpmpChanged(bool new_natpmp);
    void pruneChanged(bool new_prune);
    void pruneSizeGBChanged(int new_prune_size_gb);
    void scriptThreadsChanged(int new_script_threads);
    void serverChanged(bool new_server);
    void upnpChanged(bool new_upnp);

private:
    QString m_custom_datadir_string;
    bool m_signalReceived = false;
    common::Settings m_previous_settings;
    int m_dbcache_size_mib;
    const int m_min_dbcache_size_mib{nMinDbCache};
    const int m_max_dbcache_size_mib{nMaxDbCache};
    bool m_listen;
    const int m_max_script_threads{MAX_SCRIPTCHECK_THREADS};
    const int m_min_script_threads{-GetNumCores()};
    bool m_natpmp;
    bool m_prune;
    int m_prune_size_gb;
    int m_script_threads;
    bool m_server;
    bool m_upnp;
    quint64 m_assumed_blockchain_size{ Params().AssumedBlockchainSize() };
    quint64 m_assumed_chainstate_size{ Params().AssumedChainStateSize() };

    common::SettingsValue pruneSetting() const;
};

#endif // BITCOIN_QML_MODELS_ONBOARDINGMODEL_H
