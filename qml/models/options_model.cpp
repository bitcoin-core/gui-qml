// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <interfaces/node.h>
#include <mapport.h>
#include <node/caches.h>
#include <node/chainstatemanager_args.h>
#include <qml/guiconstants.h>
#include <txdb.h>
#include <univalue.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <validation.h>

#include <cassert>

#include <QDebug>
#include <QDir>
#include <QSettings>

namespace {
int PruneMiBtoGB(int64_t mib)
{
    return (mib * 1024 * 1024 + GB_BYTES - 1) / GB_BYTES;
}

int64_t PruneGBtoMiB(int gb)
{
    return gb * GB_BYTES / 1024 / 1024;
}
} // namespace

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node, bool is_onboarded)
    : m_node{node}
    , m_onboarded{is_onboarded}
{
    auto SettingToInt = [](const common::SettingsValue& val, int64_t def) -> int64_t {
        if (val.isNull()) return def;
        const common::SettingsValue* v = &val;
        if (v->isArray() && !v->empty()) {
            v = &v->getValues()[0];
        }
        if (v->isNum()) return v->getInt<int64_t>();
        if (v->isStr()) {
            try { return std::stoll(v->get_str()); } catch (...) { return def; }
        }
        return def;
    };
    auto SettingToBool = [](const common::SettingsValue& val, bool def) -> bool {
        if (val.isNull()) return def;
        const common::SettingsValue* v = &val;
        if (v->isArray() && !v->empty()) {
            v = &v->getValues()[0];
        }
        if (v->isBool()) return v->get_bool();
        if (v->isNum()) return v->getInt<int64_t>() != 0;
        if (v->isStr()) {
            std::string s = v->get_str();
            return s == "1" || s == "true" || s == "yes";
        }
        return def;
    };
    m_dbcache_size_mib = SettingToInt(m_node.getPersistentSetting("dbcache"), DEFAULT_DB_CACHE >> 20);

    m_listen = SettingToBool(m_node.getPersistentSetting("listen"), DEFAULT_LISTEN);

    m_natpmp = SettingToBool(m_node.getPersistentSetting("natpmp"), DEFAULT_NATPMP);

    int64_t prune_value{SettingToInt(m_node.getPersistentSetting("prune"), 0)};
    m_prune = (prune_value > 1);
    m_prune_size_gb = m_prune ? PruneMiBtoGB(prune_value) : DEFAULT_PRUNE_TARGET_GB;

    m_script_threads = SettingToInt(m_node.getPersistentSetting("par"), DEFAULT_SCRIPTCHECK_THREADS);

    m_server = SettingToBool(m_node.getPersistentSetting("server"), false);

    m_dataDir = getDefaultDataDirString();

    QString proxy_setting = QString::fromStdString(SettingToString(m_node.getPersistentSetting("proxy"), ""));
    if (proxy_setting == "0") proxy_setting.clear();
    m_proxy_enabled = !proxy_setting.isEmpty();
    m_proxy_address = proxy_setting;

    QString onion_setting = QString::fromStdString(SettingToString(m_node.getPersistentSetting("onion"), ""));
    if (onion_setting == "0") onion_setting.clear();
    m_tor_enabled = !onion_setting.isEmpty();
    m_tor_address = onion_setting;

    m_initial_proxy_enabled = m_proxy_enabled;
    m_initial_proxy_address = m_proxy_address;
    m_initial_tor_enabled   = m_tor_enabled;
    m_initial_tor_address   = m_tor_address;
}

void OptionsQmlModel::setDbcacheSizeMiB(int new_dbcache_size_mib)
{
    if (new_dbcache_size_mib != m_dbcache_size_mib) {
        m_dbcache_size_mib = new_dbcache_size_mib;
        if (m_onboarded) {
            m_node.updateRwSetting("dbcache", new_dbcache_size_mib);
        }
        Q_EMIT dbcacheSizeMiBChanged(new_dbcache_size_mib);
    }
}

void OptionsQmlModel::setListen(bool new_listen)
{
    if (new_listen != m_listen) {
        m_listen = new_listen;
        if (m_onboarded) {
            m_node.updateRwSetting("listen", new_listen);
        }
        Q_EMIT listenChanged(new_listen);
    }
}

void OptionsQmlModel::setNatpmp(bool new_natpmp)
{
    if (new_natpmp != m_natpmp) {
        m_natpmp = new_natpmp;
        if (m_onboarded) {
            m_node.updateRwSetting("natpmp", new_natpmp);
        }
        Q_EMIT natpmpChanged(new_natpmp);
    }
}

void OptionsQmlModel::setPrune(bool new_prune)
{
    if (new_prune != m_prune) {
        m_prune = new_prune;
        if (m_onboarded) {
            m_node.updateRwSetting("prune", pruneSetting());
        }
        Q_EMIT pruneChanged(new_prune);
    }
}

void OptionsQmlModel::setPruneSizeGB(int new_prune_size_gb)
{
    if (new_prune_size_gb != m_prune_size_gb) {
        m_prune_size_gb = new_prune_size_gb;
        if (m_onboarded) {
            m_node.updateRwSetting("prune", pruneSetting());
        }
        Q_EMIT pruneSizeGBChanged(new_prune_size_gb);
    }
}

void OptionsQmlModel::setScriptThreads(int new_script_threads)
{
    if (new_script_threads != m_script_threads) {
        m_script_threads = new_script_threads;
        if (m_onboarded) {
            m_node.updateRwSetting("par", new_script_threads);
        }
        Q_EMIT scriptThreadsChanged(new_script_threads);
    }
}

void OptionsQmlModel::setServer(bool new_server)
{
    if (new_server != m_server) {
        m_server = new_server;
        if (m_onboarded) {
            m_node.updateRwSetting("server", new_server);
        }
        Q_EMIT serverChanged(new_server);
    }
}

void OptionsQmlModel::setProxyEnabled(bool enabled)
{
    if (enabled != m_proxy_enabled) {
        bool was_dirty = proxySettingsDirty();
        m_proxy_enabled = enabled;
        if (m_onboarded) {
            if (enabled && !m_proxy_address.isEmpty()) {
                m_node.updateRwSetting("proxy", m_proxy_address.toStdString());
            } else {
                m_node.updateRwSetting("proxy", common::SettingsValue{});
            }
        }
        if (proxySettingsDirty() != was_dirty) {
            Q_EMIT proxySettingsDirtyChanged();
        }
        Q_EMIT proxyEnabledChanged(enabled);
    }
}

void OptionsQmlModel::setProxyAddress(const QString& address)
{
    if (address != m_proxy_address) {
        bool was_dirty = proxySettingsDirty();
        m_proxy_address = address;
        if (m_onboarded && m_proxy_enabled) {
            m_node.updateRwSetting("proxy", address.toStdString());
        }
        if (proxySettingsDirty() != was_dirty) {
            Q_EMIT proxySettingsDirtyChanged();
        }
        Q_EMIT proxyAddressChanged(address);
    }
}

void OptionsQmlModel::setTorEnabled(bool enabled)
{
    if (enabled != m_tor_enabled) {
        bool was_dirty = proxySettingsDirty();
        m_tor_enabled = enabled;
        if (m_onboarded) {
            if (enabled && !m_tor_address.isEmpty()) {
                m_node.updateRwSetting("onion", m_tor_address.toStdString());
            } else {
                m_node.updateRwSetting("onion", common::SettingsValue{});
            }
        }
        if (proxySettingsDirty() != was_dirty) {
            Q_EMIT proxySettingsDirtyChanged();
        }
        Q_EMIT torEnabledChanged(enabled);
    }
}

void OptionsQmlModel::setTorAddress(const QString& address)
{
    if (address != m_tor_address) {
        bool was_dirty = proxySettingsDirty();
        m_tor_address = address;
        if (m_onboarded && m_tor_enabled) {
            m_node.updateRwSetting("onion", address.toStdString());
        }
        if (proxySettingsDirty() != was_dirty) {
            Q_EMIT proxySettingsDirtyChanged();
        }
        Q_EMIT torAddressChanged(address);
    }
}

common::SettingsValue OptionsQmlModel::pruneSetting() const
{
    assert(!m_prune || m_prune_size_gb >= 1);
    return m_prune ? PruneGBtoMiB(m_prune_size_gb) : 0;
}

QString PathToQString(const fs::path &path)
{
    return QString::fromStdString(path.utf8string());
}

QString OptionsQmlModel::getDefaultDataDirString()
{
    return PathToQString(GetDefaultDataDir());
}


QUrl OptionsQmlModel::getDefaultDataDirectory()
{
    QString path = getDefaultDataDirString();
    return QUrl::fromLocalFile(path);
}

bool OptionsQmlModel::setCustomDataDirArgs(QString path)
{
    if (!path.isEmpty()) {
    // TODO: add actual custom data wiring
#ifdef __ANDROID__
    QString uri = path;
    QString originalPrefix = "content://com.android.externalstorage.documents/tree/primary%3A";
    QString newPrefix = "/storage/self/primary/";
    QString path = uri.replace(originalPrefix, newPrefix);
#else
    path = QUrl(path).toLocalFile();
#endif // __ANDROID__
        qDebug() << "PlaceHolder: Created data directory: " << path;

        m_custom_datadir_string = path;
        Q_EMIT customDataDirStringChanged(path);
        setDataDir(path);
        return true;
    }
    return false;
}

QString OptionsQmlModel::getCustomDataDirString()
{
#ifdef __ANDROID__
    m_custom_datadir_string = m_custom_datadir_string.replace("content://com.android.externalstorage.documents/tree/primary%3A", "/storage/self/primary/");
#endif // __ANDROID__
    return m_custom_datadir_string;
}

void OptionsQmlModel::setDataDir(QString new_data_dir)
{
    if (new_data_dir != m_dataDir) {
        m_dataDir = new_data_dir;
        if (!getCustomDataDirString().isEmpty() && (new_data_dir != getDefaultDataDirString())) {
            m_dataDir = getCustomDataDirString();
        } else {
            m_dataDir = getDefaultDataDirString();
        }
        Q_EMIT dataDirChanged(new_data_dir);
    }
}

void OptionsQmlModel::onboard()
{
    m_node.resetSettings();
    if (m_dbcache_size_mib != DEFAULT_DB_CACHE >> 20) {
        m_node.updateRwSetting("dbcache", m_dbcache_size_mib);
    }
    if (m_listen) {
        m_node.updateRwSetting("listen", m_listen);
    }
    if (m_natpmp) {
        m_node.updateRwSetting("natpmp", m_natpmp);
    }
    if (m_prune) {
        m_node.updateRwSetting("prune", pruneSetting());
    }
    if (m_script_threads != DEFAULT_SCRIPTCHECK_THREADS) {
        m_node.updateRwSetting("par", m_script_threads);
    }
    if (m_server) {
        m_node.updateRwSetting("server", m_server);
    }
    if (m_proxy_enabled && !m_proxy_address.isEmpty()) {
        m_node.updateRwSetting("proxy", m_proxy_address.toStdString());
    }
    if (m_tor_enabled && !m_tor_address.isEmpty()) {
        m_node.updateRwSetting("onion", m_tor_address.toStdString());
    }
    m_onboarded = true;
    m_initial_proxy_enabled = m_proxy_enabled;
    m_initial_proxy_address = m_proxy_address;
    m_initial_tor_enabled   = m_tor_enabled;
    m_initial_tor_address   = m_tor_address;
}
