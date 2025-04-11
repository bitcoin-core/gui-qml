// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/options_model.h>

#include <interfaces/node.h>
#include <qt/guiconstants.h>
#include <qt/optionsmodel.h>
#include <univalue.h>
#include <util/settings.h>
#include <util/system.h>

#include <cassert>

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node)
    : m_node{node}
{
    int64_t prune_value{SettingToInt(m_node.getPersistentSetting("prune"), 0)};
    m_prune = (prune_value > 1);
    m_prune_size_gb = m_prune ? PruneMiBtoGB(prune_value) : DEFAULT_PRUNE_TARGET_GB;
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

void OptionsQmlModel::setUpnp(bool new_upnp)
{
    if (new_upnp != m_upnp) {
        m_upnp = new_upnp;
        m_node.updateRwSetting("upnp", new_upnp);
        Q_EMIT upnpChanged(new_upnp);
    }
}

util::SettingsValue OptionsQmlModel::pruneSetting() const
{
    assert(!m_prune || m_prune_size_gb >= 1);
    return m_prune ? PruneGBtoMiB(m_prune_size_gb) : 0;
}
