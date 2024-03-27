// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/onboardingmodel.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <qt/guiconstants.h>
#include <qt/guiutil.h>
#include <util/fs.h>
#include <util/fs_helpers.h>

#include <QDebug>
#include <QDir>
#include <QSettings>

OnboardingModel::OnboardingModel()
{
    // This is to reset the data directory to the default
    gArgs.LockSettings([&](common::Settings& s) { m_previous_settings = s; });

    // Initiate prune settings
    m_prune = false;
}

void OnboardingModel::setDbcacheSizeMiB(int new_dbcache_size_mib)
{
    if (new_dbcache_size_mib != m_dbcache_size_mib) {
        m_dbcache_size_mib = new_dbcache_size_mib;
        // // qDebug () << "Dbcache Size Set: " << new_dbcache_size_mib;
        Q_EMIT dbcacheSizeMiBChanged(new_dbcache_size_mib);

    }
}

void OnboardingModel::setListen(bool new_listen)
{
    if (new_listen != m_listen) {
        m_listen = new_listen;
        // qDebug () << "Listen Set: " << new_listen;
        Q_EMIT listenChanged(new_listen);
    }
}

void OnboardingModel::setNatpmp(bool new_natpmp)
{
    if (new_natpmp != m_natpmp) {
        m_natpmp = new_natpmp;
        // qDebug () << "Natpmp Set: " << new_natpmp;
        Q_EMIT natpmpChanged(new_natpmp);
    }
}

void OnboardingModel::setPrune(bool new_prune)
{
    if (new_prune != m_prune) {
        m_prune = new_prune;
        // qDebug () << "Prune Set: " << new_prune;
        Q_EMIT pruneChanged(new_prune);
    }
}

void OnboardingModel::setPruneSizeGB(int new_prune_size_gb)
{
    if (new_prune_size_gb != m_prune_size_gb) {
        m_prune_size_gb = new_prune_size_gb;
        // qDebug () << "Prune Size Set: " << new_prune_size_gb;
        Q_EMIT pruneSizeGBChanged(new_prune_size_gb);
    }
}

void OnboardingModel::setScriptThreads(int new_script_threads)
{
    if (new_script_threads != m_script_threads) {
        m_script_threads = new_script_threads;
        // qDebug () << "Script Threads Set: " << new_script_threads;
        Q_EMIT scriptThreadsChanged(new_script_threads);
    }
}

void OnboardingModel::setServer(bool new_server)
{
    if (new_server != m_server) {
        m_server = new_server;
        // qDebug () << "Server Set: " << new_server;
        Q_EMIT serverChanged(new_server);
    }
}

void OnboardingModel::setUpnp(bool new_upnp)
{
    if (new_upnp != m_upnp) {
        m_upnp = new_upnp;
        // qDebug () << "Upnp Set: " << new_upnp;
        Q_EMIT upnpChanged(new_upnp);
    }
}

void OnboardingModel::requestShutdown()
{
    Q_EMIT requestedShutdown();
}

QString PathToQString(const fs::path &path)
{
    return QString::fromStdString(path.u8string());
}

QString OnboardingModel::getDefaultDataDirString()
{
    return PathToQString(GetDefaultDataDir());
}


QUrl OnboardingModel::getDefaultDataDirectory()
{
    QString path = getDefaultDataDirString();
    return QUrl::fromLocalFile(path);
}

void OnboardingModel::defaultReset()
{
    QSettings settings;
    // Save the strDataDir setting
    QString dataDir = GUIUtil::getDefaultDataDirectory();

    // Remove all entries from our QSettings object
    settings.clear();

    // Set strDataDir
    settings.setValue("strDataDir", dataDir);

    // Set that this was reset
    settings.setValue("fReset", true);

    // qDebug() << "Resetting data directory: " << dataDir;

    gArgs.LockSettings([&](common::Settings& s) { s = m_previous_settings; });
    gArgs.ClearPathCache();
    // for debug purposes checking the cached datadir
    // const fs::path& cached_dataDir = gArgs.GetDataDirNet();
    // qDebug() << "Cached Data directory: " << QString::fromStdString(cached_dataDir);
}

void OnboardingModel::setCustomDataDirArgs(QString path)
{
    if (!path.isEmpty()) {
        #ifdef __ANDROID__
            QString uri = path;
            QSettings settings;
            QString originalPrefix = "content://com.android.externalstorage.documents/tree/primary%3A";
            QString newPrefix = "/storage/self/primary/";
            QString path = uri.replace(originalPrefix, newPrefix);
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
        settings.setValue("strDataDir", path);
        if(path != GUIUtil::getDefaultDataDirectory()) {
        gArgs.SoftSetArg("-datadir", fs::PathToString(GUIUtil::QStringToPath(path)));
        }
        gArgs.ClearPathCache();
        Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
    }
}
