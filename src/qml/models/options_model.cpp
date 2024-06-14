// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <interfaces/node.h>
#include <mapport.h>
#ifdef __ANDROID__
#include <qml/androidcustomdatadir.h>
#endif
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <qt/optionsmodel.h>
#include <txdb.h>
#include <univalue.h>
#include <util/fs.h>
#include <util/fs_helpers.h>
#include <validation.h>

#include <cassert>

#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QFile>
#include <QTextStream>
#include <QStandardPaths>

OptionsQmlModel::OptionsQmlModel(interfaces::Node* node, bool is_onboarded)
    : m_node{node}
    , m_onboarded{is_onboarded}
{
    gArgs.LockSettings([&](common::Settings& cs) {
        // Clear existing settings to ensure we're only storing new command line arguments
        m_cli_settings.command_line_options.clear();

        // Copy only the command line arguments from the provided settings
        m_cli_settings.command_line_options = cs.command_line_options;
    });

    if (!is_onboarded) {
        // added this to lock settings to default values
        if (!gArgs.IsArgSet("-resetguisettings")) {
            gArgs.LockSettings([&](common::Settings& s) { m_previous_settings = s; });
        }
        m_dbcache_size_mib = nDefaultDbCache;
        m_listen = DEFAULT_LISTEN;
        m_natpmp = DEFAULT_NATPMP;
        int64_t prune_value = 0;
        m_prune = (prune_value > 1);
        m_prune_size_gb = m_prune ? PruneMiBtoGB(prune_value) : DEFAULT_PRUNE_TARGET_GB;
        m_script_threads = DEFAULT_SCRIPTCHECK_THREADS;
        m_server = false;
        m_upnp = DEFAULT_UPNP;
    }

#ifdef __ANDROID__
    if (!getCustomDataDirString().isEmpty() && (m_dataDir != getDefaultDataDirString())) {
            m_dataDir = getCustomDataDirString();
        } else {
            m_dataDir = getDefaultDataDirString();
        }
#else
    QSettings settings;
    m_dataDir = settings.value("strDataDir", m_dataDir).toString();
#endif // __ANDROID__
}

void OptionsQmlModel::requestShutdown()
{
    Q_EMIT requestedShutdown();
}

void OptionsQmlModel::setNode(interfaces::Node* node, bool is_onboarded) {
    if (node != nullptr) {
        m_node = node;
        if (is_onboarded) {
            m_dbcache_size_mib = SettingToInt(m_node->getPersistentSetting("dbcache"), nDefaultDbCache);

            m_listen = SettingToBool(m_node->getPersistentSetting("listen"), DEFAULT_LISTEN);

            m_natpmp = SettingToBool(m_node->getPersistentSetting("natpmp"), DEFAULT_NATPMP);

            int64_t prune_value{SettingToInt(m_node->getPersistentSetting("prune"), 0)};
            m_prune = (prune_value > 1);
            m_prune_size_gb = m_prune ? PruneMiBtoGB(prune_value) : DEFAULT_PRUNE_TARGET_GB;

            m_script_threads = SettingToInt(m_node->getPersistentSetting("par"), DEFAULT_SCRIPTCHECK_THREADS);

            m_server = SettingToBool(m_node->getPersistentSetting("server"), false);

            m_upnp = SettingToBool(m_node->getPersistentSetting("upnp"), DEFAULT_UPNP);
#ifdef __ANDROID__
            m_dataDir = AndroidCustomDataDir().readCustomDataDir().isEmpty() ? getDefaultDataDirString()
                        : AndroidCustomDataDir().readCustomDataDir();
#else
            if ((gArgs.IsArgSet("-datadir") && !gArgs.GetPathArg("-datadir").empty())) {
                m_dataDir = QString::fromStdString(gArgs.GetPathArg("-datadir").generic_string());
            }
            else {
                m_custom_datadir_string = m_settings.value("strDataDir", m_custom_datadir_string).toString();

                m_dataDir = fs::exists(GUIUtil::QStringToPath(m_custom_datadir_string)) ? m_custom_datadir_string
                            : QString::fromStdString(SettingToString(m_node->getPersistentSetting("datadir"), ""));

                if (m_dataDir.isEmpty()) {
                    m_dataDir = getDefaultDataDirString();
                }
            }
#endif // __ANDROID__
        }
        return;
    }
}

void OptionsQmlModel::setDbcacheSizeMiB(int new_dbcache_size_mib)
{
    if (new_dbcache_size_mib != m_dbcache_size_mib) {
        m_dbcache_size_mib = new_dbcache_size_mib;
        if (m_onboarded) {
            m_node->updateRwSetting("dbcache", new_dbcache_size_mib);
        }
        Q_EMIT dbcacheSizeMiBChanged(new_dbcache_size_mib);
    }
}

void OptionsQmlModel::setListen(bool new_listen)
{
    if (new_listen != m_listen) {
        m_listen = new_listen;
        if (m_onboarded) {
            m_node->updateRwSetting("listen", new_listen);
        }
        Q_EMIT listenChanged(new_listen);
    }
}

void OptionsQmlModel::setNatpmp(bool new_natpmp)
{
    if (new_natpmp != m_natpmp) {
        m_natpmp = new_natpmp;
        if (m_onboarded) {
            m_node->updateRwSetting("natpmp", new_natpmp);
        }
        Q_EMIT natpmpChanged(new_natpmp);
    }
}

void OptionsQmlModel::setPrune(bool new_prune)
{
    if (new_prune != m_prune) {
        m_prune = new_prune;
        if (m_onboarded) {
            m_node->updateRwSetting("prune", pruneSetting());
        }
        Q_EMIT pruneChanged(new_prune);
    }
}

void OptionsQmlModel::setPruneSizeGB(int new_prune_size_gb)
{
    if (new_prune_size_gb != m_prune_size_gb) {
        m_prune_size_gb = new_prune_size_gb;
        if (m_onboarded) {
            m_node->updateRwSetting("prune", pruneSetting());
        }
        Q_EMIT pruneSizeGBChanged(new_prune_size_gb);
    }
}

void OptionsQmlModel::setScriptThreads(int new_script_threads)
{
    if (new_script_threads != m_script_threads) {
        m_script_threads = new_script_threads;
        if (m_onboarded) {
            m_node->updateRwSetting("par", new_script_threads);
        }
        Q_EMIT scriptThreadsChanged(new_script_threads);
    }
}

void OptionsQmlModel::setServer(bool new_server)
{
    if (new_server != m_server) {
        m_server = new_server;
        if (m_onboarded) {
            m_node->updateRwSetting("server", new_server);
        }
        Q_EMIT serverChanged(new_server);
    }
}

void OptionsQmlModel::setUpnp(bool new_upnp)
{
    if (new_upnp != m_upnp) {
        m_upnp = new_upnp;
        if (m_onboarded) {
            m_node->updateRwSetting("upnp", new_upnp);
        }
        Q_EMIT upnpChanged(new_upnp);
    }
}

common::SettingsValue OptionsQmlModel::pruneSetting() const
{
    assert(!m_prune || m_prune_size_gb >= 1);
    return m_prune ? PruneGBtoMiB(m_prune_size_gb) : 0;
}

QString PathToQString(const fs::path &path)
{
    return QString::fromStdString(path.u8string());
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

void OptionsQmlModel::defaultReset()
{
    QSettings settings;
    // Save the strDataDir setting
    QString path = GUIUtil::getDefaultDataDirectory();

    setDataDir(path);

    // Remove all entries from our QSettings object
    settings.clear();

    // Set strDataDir
    settings.setValue("strDataDir", path);

    // Set that this was reset
    settings.setValue("fReset", true);

    // Clear the settings
    gArgs.LockSettings([&](common::Settings& s) { s = m_previous_settings; });

    // reinstate command line arguments
    gArgs.LockSettings([&](common::Settings& cs) { cs = m_cli_settings; });
    gArgs.SoftSetBoolArg("-printtoconsole", false);

    gArgs.ClearPathCache();
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
    QString dataDir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    QFile file(dataDir + "/filepath.txt");
    if (file.open(QIODevice::WriteOnly)) {
        QTextStream out(&file);
        out << path; // Store the QString path into filepath.txt
        file.close();
    }
#else
    QSettings settings;
    path = QUrl(path).toLocalFile();
#endif // __ANDROID__
        try{
            if (TryCreateDirectories(GUIUtil::QStringToPath(path))) {
                // qDebug() << "Created data directory: " << path;
                TryCreateDirectories(GUIUtil::QStringToPath(path) / "wallets");
            }
        } catch (const std::exception& e) {
            qDebug() << "Error creating data directory: " << e.what();
        }
#ifndef __ANDROID__
        settings.setValue("strDataDir", path);
#endif // __ANDROID__
        if(path != GUIUtil::getDefaultDataDirectory()) {
        gArgs.SoftSetArg("-datadir", fs::PathToString(GUIUtil::QStringToPath(path)));
        }
        gArgs.ClearPathCache();
        Q_EMIT customDataDirStringChanged(m_custom_datadir_string);

        m_custom_datadir_string = path;
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
    m_node->resetSettings();
    if (m_dbcache_size_mib != nDefaultDbCache) {
        m_node->updateRwSetting("dbcache", m_dbcache_size_mib);
    }
    if (m_listen) {
        m_node->updateRwSetting("listen", m_listen);
    }
    if (m_natpmp) {
        m_node->updateRwSetting("natpmp", m_natpmp);
    }
    if (m_prune) {
        m_node->updateRwSetting("prune", pruneSetting());
    }
    if (m_script_threads != DEFAULT_SCRIPTCHECK_THREADS) {
        m_node->updateRwSetting("par", m_script_threads);
    }
    if (m_server) {
        m_node->updateRwSetting("server", m_server);
    }
    if (m_upnp) {
        m_node->updateRwSetting("upnp", m_upnp);
    }
    if (m_dataDir != getDefaultDataDirString()) {
        std::string dataDirStd = m_dataDir.toStdString();
        m_node->updateRwSetting("datadir", UniValue(dataDirStd));
    }
    m_onboarded = true;
}
