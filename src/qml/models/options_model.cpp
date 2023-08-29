// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <interfaces/node.h>
#include <qt/guiconstants.h>
#include <qt/optionsmodel.h>
#include <txdb.h>
#include <univalue.h>
#include <validation.h>

#include <cassert>

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node)
    : m_node{node}
{
    m_dbcache_size_mib = SettingToInt(m_node.getPersistentSetting("dbcache"), nDefaultDbCache);

    int64_t prune_value{SettingToInt(m_node.getPersistentSetting("prune"), 0)};
    m_prune = (prune_value > 1);
    m_prune_size_gb = m_prune ? PruneMiBtoGB(prune_value) : DEFAULT_PRUNE_TARGET_GB;

    m_script_threads = SettingToInt(m_node.getPersistentSetting("par"), DEFAULT_SCRIPTCHECK_THREADS);
}

void OptionsQmlModel::setDbcacheSizeMiB(int new_dbcache_size_mib)
{
    if (new_dbcache_size_mib != m_dbcache_size_mib) {
        m_dbcache_size_mib = new_dbcache_size_mib;
        m_node.updateRwSetting("dbcache", new_dbcache_size_mib);
        Q_EMIT dbcacheSizeMiBChanged(new_dbcache_size_mib);
    }
}

void OptionsQmlModel::setListen(bool new_listen)
{
    if (new_listen != m_listen) {
        m_listen = new_listen;
        m_node.updateRwSetting("listen", new_listen);
        Q_EMIT listenChanged(new_listen);
    }
}

void OptionsQmlModel::setNatpmp(bool new_natpmp)
{
    if (new_natpmp != m_natpmp) {
        m_natpmp = new_natpmp;
        m_node.updateRwSetting("natpmp", new_natpmp);
        Q_EMIT natpmpChanged(new_natpmp);
    }
}

void OptionsQmlModel::setPrune(bool new_prune)
{
    if (new_prune != m_prune) {
        m_prune = new_prune;
        m_node.updateRwSetting("prune", pruneSetting());
        Q_EMIT pruneChanged(new_prune);
    }
}

void OptionsQmlModel::setPruneSizeGB(int new_prune_size_gb)
{
    if (new_prune_size_gb != m_prune_size_gb) {
        m_prune_size_gb = new_prune_size_gb;
        m_node.updateRwSetting("prune", pruneSetting());
        Q_EMIT pruneSizeGBChanged(new_prune_size_gb);
    }
}

void OptionsQmlModel::setScriptThreads(int new_script_threads)
{
    if (new_script_threads != m_script_threads) {
        m_script_threads = new_script_threads;
        m_node.updateRwSetting("par", new_script_threads);
        Q_EMIT scriptThreadsChanged(new_script_threads);
    }
}

void OptionsQmlModel::setServer(bool new_server)
{
    if (new_server != m_server) {
        m_server = new_server;
        m_node.updateRwSetting("server", new_server);
        Q_EMIT serverChanged(new_server);
    }
}

void OptionsQmlModel::setUpnp(bool new_upnp)
{
    if (new_upnp != m_upnp) {
        m_upnp = new_upnp;
        m_node.updateRwSetting("upnp", new_upnp);
        Q_EMIT upnpChanged(new_upnp);
    }
}

common::SettingsValue OptionsQmlModel::pruneSetting() const
{
    assert(!m_prune || m_prune_size_gb >= 1);
    return m_prune ? PruneGBtoMiB(m_prune_size_gb) : 0;
}
