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
#include <QFileInfo>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QStringList>

namespace {
int PruneMiBtoGB(int64_t mib)
{
    return (mib * 1024 * 1024 + GB_BYTES - 1) / GB_BYTES;
}

int64_t PruneGBtoMiB(int gb)
{
    return gb * GB_BYTES / 1024 / 1024;
}

QString NormalizeCommandPath(const QString& path)
{
    const QString trimmed = path.trimmed();
    if (trimmed.isEmpty()) {
        return {};
    }

    const QUrl url(trimmed);
    return url.isLocalFile() ? url.toLocalFile() : trimmed;
}

QString FirstCommandToken(const QString& command)
{
    static const QRegularExpression TOKEN_RE(QStringLiteral(R"re(^\s*(?:"([^"]+)"|'([^']+)'|(\S+)))re"));
    const auto match = TOKEN_RE.match(command);
    if (!match.hasMatch()) {
        return {};
    }
    for (int i = 1; i <= 3; ++i) {
        const QString captured = match.captured(i);
        if (!captured.isEmpty()) {
            return captured;
        }
    }
    return {};
}

QString ExpandUserPath(const QString& path)
{
    if (path == QStringLiteral("~")) {
        return QDir::homePath();
    }
    if (path.startsWith(QStringLiteral("~/"))) {
        return QDir::homePath() + path.mid(1);
    }
    return path;
}

bool TokenLooksLikePath(const QString& token)
{
    return token.startsWith(QStringLiteral("/")) ||
           token.startsWith(QStringLiteral("./")) ||
           token.startsWith(QStringLiteral("../")) ||
           token.startsWith(QStringLiteral("~/")) ||
           token.contains(QLatin1Char('/')) ||
           token.contains(QLatin1Char('\\')) ||
           QDir::isAbsolutePath(token);
}
} // namespace

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node, bool is_onboarded)
    : m_node{node}
    , m_onboarded{is_onboarded}
{
    m_dbcache_size_mib = SettingTo<int64_t>(m_node.getPersistentSetting("dbcache"), DEFAULT_DB_CACHE >> 20);

    m_listen = SettingToBool(m_node.getPersistentSetting("listen"), DEFAULT_LISTEN);

    m_max_mempool_size_mb = SettingTo<int64_t>(m_node.getPersistentSetting("maxmempool"), DEFAULT_MAX_MEMPOOL_SIZE_MB);

    m_natpmp = SettingToBool(m_node.getPersistentSetting("natpmp"), DEFAULT_NATPMP);

    int64_t prune_value{SettingTo<int64_t>(m_node.getPersistentSetting("prune"), 0)};
    m_prune = (prune_value > 1);
    m_prune_size_gb = m_prune ? PruneMiBtoGB(prune_value) : DEFAULT_PRUNE_TARGET_GB;

    m_script_threads = SettingTo<int64_t>(m_node.getPersistentSetting("par"), DEFAULT_SCRIPTCHECK_THREADS);

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

    m_external_signer_path = QString::fromStdString(SettingToString(m_node.getPersistentSetting("signer"), ""));

    m_initial_proxy_enabled = m_proxy_enabled;
    m_initial_proxy_address = m_proxy_address;
    m_initial_tor_enabled   = m_tor_enabled;
    m_initial_tor_address   = m_tor_address;
    m_initial_external_signer_path = m_external_signer_path;

    QSettings settings;
    m_language = settings.value(SettingsKeys::LANGUAGE, "").toString();
    m_display_unit = settings.value(SettingsKeys::DISPLAY_UNIT, 0).toInt();

    buildAvailableLanguages();
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

void OptionsQmlModel::setMaxMempoolSizeMB(int new_max_mempool_size_mb)
{
    if (new_max_mempool_size_mb != m_max_mempool_size_mb) {
        m_max_mempool_size_mb = new_max_mempool_size_mb;
        if (m_onboarded) {
            m_node.updateRwSetting("maxmempool", new_max_mempool_size_mb);
        }
        Q_EMIT maxMempoolSizeMBChanged(new_max_mempool_size_mb);
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

void OptionsQmlModel::setExternalSignerPath(const QString& path)
{
    const QString normalized_path = NormalizeCommandPath(path);
    if (normalized_path != m_external_signer_path) {
        bool was_dirty = walletSettingsDirty();
        m_external_signer_path = normalized_path;
        if (m_external_signer_path.isEmpty()) {
            m_node.forceSetting("signer", common::SettingsValue{});
        } else {
            m_node.forceSetting("signer", m_external_signer_path.toStdString());
        }
        if (m_onboarded) {
            if (m_external_signer_path.isEmpty()) {
                m_node.updateRwSetting("signer", common::SettingsValue{});
            } else {
                m_node.updateRwSetting("signer", m_external_signer_path.toStdString());
            }
        }
        if (walletSettingsDirty() != was_dirty) {
            Q_EMIT walletSettingsDirtyChanged();
        }
        Q_EMIT externalSignerPathChanged(m_external_signer_path);
    }
}

QString OptionsQmlModel::externalSignerPathValidationError(const QString& path) const
{
    const QString normalized_path = NormalizeCommandPath(path);
    if (normalized_path.isEmpty()) {
        return {};
    }

    const QString token = FirstCommandToken(normalized_path);
    if (token.isEmpty() || !TokenLooksLikePath(token)) {
        return {};
    }

    const QFileInfo info(ExpandUserPath(token));
    if (!info.exists()) {
        return tr("The configured signer path does not exist.");
    }
    if (!info.isFile()) {
        return tr("The configured signer path is not a file.");
    }
    if (!info.isExecutable()) {
        return tr("The configured signer path is not executable.");
    }
    return {};
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

void OptionsQmlModel::buildAvailableLanguages()
{
    m_available_languages.clear();
    m_available_languages << "";  // empty = system default

    QDir translations_dir(":/translations");
    QStringList files = translations_dir.entryList({"bitcoin_*.qm"}, QDir::Files);
    QStringList tags;
    for (const QString& file : files) {
        // Strip "bitcoin_" prefix and ".qm" suffix to get locale tag.
        QString tag = file;
        tag.remove(0, 8);       // remove "bitcoin_"
        tag.chop(3);            // remove ".qm"
        // Skip QML-app-specific translation resources (e.g. "qml_es").
        if (tag.startsWith(QStringLiteral("qml_"))) continue;
        tags << tag;
    }
    tags.sort(Qt::CaseInsensitive);
    m_available_languages << tags;
}

void OptionsQmlModel::setLanguage(const QString& new_language)
{
    if (new_language != m_language) {
        m_language = new_language;
        QSettings settings;
        settings.setValue(SettingsKeys::LANGUAGE, m_language);
        Q_EMIT languageChanged();
    }
}

QString OptionsQmlModel::languageSummary() const
{
    return languageLabel(m_language);
}

QString OptionsQmlModel::languageLabel(const QString& locale_tag) const
{
    if (locale_tag.isEmpty()) {
        return QObject::tr("System default");
    }
    QLocale locale(locale_tag);
    QString native = locale.nativeLanguageName();
    if (native.isEmpty()) {
        return locale_tag;
    }
    // Capitalize first letter of native name.
    native[0] = native[0].toUpper();
    QString english = QLocale::languageToString(locale.language());
    // Append territory disambiguation when the tag includes a territory code.
    if (locale_tag.contains('_')) {
        QString native_territory = locale.nativeTerritoryName();
        if (!native_territory.isEmpty()) {
            native += QStringLiteral(" (%1)").arg(native_territory);
        }
        english += QStringLiteral(" (%1)").arg(QLocale::territoryToString(locale.territory()));
    }
    return QStringLiteral("%1 \u2014 %2").arg(native, english);
}

void OptionsQmlModel::setDisplayUnit(int new_display_unit)
{
    if (new_display_unit != m_display_unit) {
        m_display_unit = new_display_unit;
        QSettings settings;
        settings.setValue(SettingsKeys::DISPLAY_UNIT, m_display_unit);
        Q_EMIT displayUnitChanged(m_display_unit);
    }
}

QString OptionsQmlModel::displayUnitLabel() const
{
    return (m_display_unit == 1) ? QStringLiteral("sat") : QStringLiteral("BTC");
}

QString OptionsQmlModel::displayUnitLabelForAmount(qint64 satoshi) const
{
    if (m_display_unit != 1) return QStringLiteral("₿");
    return (qAbs(satoshi) == 1) ? QStringLiteral("sat") : QStringLiteral("sats");
}


void OptionsQmlModel::onboard()
{
    m_node.resetSettings();
    if (m_external_signer_path.isEmpty()) {
        m_node.forceSetting("signer", common::SettingsValue{});
    } else {
        m_node.forceSetting("signer", m_external_signer_path.toStdString());
    }
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
    if (!m_external_signer_path.isEmpty()) {
        m_node.updateRwSetting("signer", m_external_signer_path.toStdString());
    }
    m_onboarded = true;
    m_initial_proxy_enabled = m_proxy_enabled;
    m_initial_proxy_address = m_proxy_address;
    m_initial_tor_enabled   = m_tor_enabled;
    m_initial_tor_address   = m_tor_address;
    m_initial_external_signer_path = m_external_signer_path;
}
