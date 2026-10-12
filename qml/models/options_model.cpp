// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/options_model.h>

#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <interfaces/node.h>
#include <mapport.h>
#include <qml/bitcoinunits.h>
#include <net.h>
#include <qml/core_settings.h>
#include <node/chainstatemanager_args.h>
#include <qml/datadir.h>
#include <qml/guiconstants.h>
#include <qml/legacy_settings_migration.h>
#include <univalue.h>

#include <cassert>

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

namespace {
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

constexpr const char* MONEY_FONT_EMBEDDED{"embedded"};
constexpr const char* MONEY_FONT_BEST_SYSTEM{"best_system"};

int NormalizeDisplayUnit(int display_unit)
{
    return display_unit >= 0 && display_unit <= 3 ? display_unit : 0;
}

bool IsThirdPartyTransactionUrlSchemeAllowed(const QUrl& url)
{
    const QString scheme = url.scheme().toLower();
    return scheme == QStringLiteral("http") || scheme == QStringLiteral("https");
}

QVariantMap CoreSettingStatusesForNames(const QVariantMap& statuses, const QStringList& names)
{
    QVariantMap subset;
    for (const QString& name : names) {
        const auto it = statuses.constFind(name);
        if (it != statuses.constEnd()) {
            subset.insert(name, *it);
        }
    }
    return subset;
}
} // namespace

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node, ArgsManager& args)
    : m_node{node}
    , m_args{args}
    , m_core_settings{QmlCoreSettings::LoadDisplayValues(node, args)}
{
    m_core_setting_statuses = QmlCoreSettings::BuildCoreSettingStatuses(m_args, QmlCoreSettings::CoreSettingNames());
    m_core_settings.setStatuses(CoreSettingStatusesForNames(m_core_setting_statuses, QmlCoreSettings::OnboardingCoreSettingNames()));

    m_dbcache_size_mib = SettingTo<int64_t>(QmlCoreSettings::DisplaySettingValue(m_node, m_args, QStringLiteral("dbcache")), DEFAULT_DB_CACHE >> 20);

    m_max_mempool_size_mb = SettingTo<int64_t>(QmlCoreSettings::DisplaySettingValue(m_node, m_args, QStringLiteral("maxmempool")), DEFAULT_MAX_MEMPOOL_SIZE_MB);

    m_script_threads = SettingTo<int64_t>(QmlCoreSettings::DisplaySettingValue(m_node, m_args, QStringLiteral("par")), DEFAULT_SCRIPTCHECK_THREADS);

    m_external_signer_path = QString::fromStdString(SettingToString(QmlCoreSettings::DisplaySettingValue(m_node, m_args, QStringLiteral("signer")), ""));

    resetDirtySnapshots();

    const QString gui_data_dir = QmlDataDir::ReadGuiDataDir();
    const QString active_data_dir = QString::fromStdString(m_args.GetDataDirBase().utf8string());
    if (!active_data_dir.isEmpty() &&
        (QmlDataDir::HasExplicitDataDirArg(m_args) || QmlDataDir::IsDefaultDataDir(gui_data_dir))) {
        m_dataDir = active_data_dir;
    } else {
        m_dataDir = gui_data_dir;
    }
    if (!QmlDataDir::IsDefaultDataDir(m_dataDir)) {
        m_custom_datadir_string = m_dataDir;
    }
    QSettings settings;
    m_language = QString::fromStdString(SettingToString(QmlCoreSettings::DisplaySettingValue(m_node, m_args, QStringLiteral("lang")), ""));
    if (m_language.isEmpty() && !QmlCoreSettings::IsCommandLineOverridden(m_args, QStringLiteral("lang"))) {
        m_language = settings.value(SettingsKeys::LANGUAGE, "").toString();
    }
    const QString command_line_language = QString::fromStdString(m_args.GetArg("-lang", ""));
    if (!command_line_language.isEmpty()) {
        m_language = command_line_language;
    }
    const int display_unit_fallback{QmlLegacySettings::ReadLegacyGuiDisplayUnit(QString::fromStdString(m_args.GetChainTypeString()), 0)};
    m_display_unit = NormalizeDisplayUnit(settings.value(SettingsKeys::DISPLAY_UNIT, display_unit_fallback).toInt());
    m_third_party_transaction_urls = settings.value(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, "").toString();
    m_money_font_choice = settings.value(SettingsKeys::MONEY_FONT_CHOICE, MONEY_FONT_EMBEDDED).toString();
    if (m_money_font_choice != MONEY_FONT_EMBEDDED && m_money_font_choice != MONEY_FONT_BEST_SYSTEM) {
        m_money_font_choice = MONEY_FONT_EMBEDDED;
    }

    buildAvailableLanguages();

    m_core_settings.setBeforeChangeHandler([this](CoreSettingsModel::ChangeOrigin) {
        m_core_change_dirty_snapshot = dirtySnapshot();
    });
    m_core_settings.setAfterChangeHandler([this](const QmlCoreSettings::Change& change, CoreSettingsModel::ChangeOrigin) {
        applyRuntimeCoreChange(change, m_core_change_dirty_snapshot);
    });
}

bool OptionsQmlModel::connectionSettingsDirty() const
{
    const QmlCoreSettings::Values& values = m_core_settings.values();
    return values.listen != m_initial_core_values.listen || values.server != m_initial_core_values.server;
}

bool OptionsQmlModel::storageSettingsDirty() const
{
    const QmlCoreSettings::Values& values = m_core_settings.values();
    if (values.prune != m_initial_core_values.prune) return true;
    return values.prune && values.prune_size_gb != m_initial_core_values.prune_size_gb;
}

bool OptionsQmlModel::developerSettingsDirty() const
{
    return m_dbcache_size_mib != m_initial_dbcache_size_mib ||
           m_script_threads != m_initial_script_threads;
}

bool OptionsQmlModel::restartRequired() const
{
    return connectionSettingsDirty() ||
           storageSettingsDirty() ||
           developerSettingsDirty() ||
           mempoolSettingsDirty() ||
           proxySettingsDirty() ||
           walletSettingsDirty();
}

QVariantMap OptionsQmlModel::coreSettingStatuses() const
{
    return m_core_setting_statuses;
}

QVariantMap OptionsQmlModel::coreSettingStatus(const QString& name) const
{
    const auto it = m_core_setting_statuses.constFind(name);
    if (it != m_core_setting_statuses.constEnd()) {
        return it->toMap();
    }
    return QmlCoreSettings::CoreSettingStatus(m_args, name);
}

void OptionsQmlModel::resetDirtySnapshots()
{
    m_initial_core_values = m_core_settings.values();
    m_initial_dbcache_size_mib = m_dbcache_size_mib;
    m_initial_max_mempool_size_mb = m_max_mempool_size_mb;
    m_initial_script_threads = m_script_threads;
    m_initial_external_signer_path = m_external_signer_path;
}

OptionsQmlModel::DirtySnapshot OptionsQmlModel::dirtySnapshot() const
{
    DirtySnapshot snapshot;
    snapshot.connection = connectionSettingsDirty();
    snapshot.storage = storageSettingsDirty();
    snapshot.developer = developerSettingsDirty();
    snapshot.mempool = mempoolSettingsDirty();
    snapshot.proxy = proxySettingsDirty();
    snapshot.wallet = walletSettingsDirty();
    snapshot.restart = restartRequired();
    return snapshot;
}

void OptionsQmlModel::emitDirtySignals(const DirtySnapshot& before)
{
    if (connectionSettingsDirty() != before.connection) Q_EMIT connectionSettingsDirtyChanged();
    if (storageSettingsDirty() != before.storage) Q_EMIT storageSettingsDirtyChanged();
    if (developerSettingsDirty() != before.developer) Q_EMIT developerSettingsDirtyChanged();
    if (mempoolSettingsDirty() != before.mempool) Q_EMIT mempoolSettingsDirtyChanged();
    if (proxySettingsDirty() != before.proxy) Q_EMIT proxySettingsDirtyChanged();
    if (walletSettingsDirty() != before.wallet) Q_EMIT walletSettingsDirtyChanged();
    if (restartRequired() != before.restart) Q_EMIT restartRequiredChanged();
}

void OptionsQmlModel::applyRuntimeCoreChange(const QmlCoreSettings::Change& change, const DirtySnapshot& before)
{
    if (m_stopping || !change.accepted || !QmlCoreSettings::ValuesChanged(change)) return;
    m_core_settings.writeToNode(m_node, m_args, change.setting_name);
    refreshCoreSettingStatuses();
    QmlCoreSettings::EmitCoreSettingSignals(*this, change);
    if (change.setting_name == QStringLiteral("natpmp")) {
        // NAT-PMP mirrors Qt Widgets as a live-applied option, not a
        // restart-required connection setting.
        // Disabling NAT-PMP joins the mapport thread and can briefly block.
        // Defer the live apply so the switch state and animation are not
        // held behind that work.
        QTimer::singleShot(200, this, [this, natpmp = change.after.natpmp] {
            if (!m_stopping) m_node.mapPort(natpmp);
        });
    }
    emitDirtySignals(before);
}

common::SettingsValue OptionsQmlModel::currentCoreSettingValue(const QString& name) const
{
    if (QmlCoreSettings::OnboardingCoreSettingNames().contains(name)) return m_core_settings.settingValue(name);
    if (name == QStringLiteral("dbcache")) return m_dbcache_size_mib;
    if (name == QStringLiteral("par")) return m_script_threads;
    if (name == QStringLiteral("maxmempool")) return m_max_mempool_size_mb;
    if (name == QStringLiteral("signer")) return common::SettingsValue{m_external_signer_path.toStdString()};
    if (name == QStringLiteral("lang")) return common::SettingsValue{m_language.toStdString()};
    return {};
}

bool OptionsQmlModel::canEditCoreSetting(const QString& name) const
{
    if (QmlCoreSettings::OnboardingCoreSettingNames().contains(name)) return m_core_settings.canEdit(name);
    const QVariantMap status = coreSettingStatus(name);
    return status.isEmpty() ? QmlCoreSettings::CanEditCoreSetting(m_args, name) : status.value(QStringLiteral("canEdit"), true).toBool();
}

bool OptionsQmlModel::writeCoreSettingOverride(const QString& name, const common::SettingsValue& value)
{
    if (!canEditCoreSetting(name)) return false;
    QmlCoreSettings::UpdateRwSetting(m_node, name, QmlCoreSettings::GuiOverrideValue(m_args, name, value));
    refreshCoreSettingStatuses();
    return true;
}

void OptionsQmlModel::refreshCoreSettingStatuses()
{
    const QVariantMap statuses = QmlCoreSettings::BuildCoreSettingStatuses(m_args, QmlCoreSettings::CoreSettingNames());
    if (statuses == m_core_setting_statuses) return;
    m_core_setting_statuses = statuses;
    m_core_settings.setStatuses(CoreSettingStatusesForNames(m_core_setting_statuses, QmlCoreSettings::OnboardingCoreSettingNames()));
    Q_EMIT coreSettingStatusesChanged();
}

void OptionsQmlModel::setDbcacheSizeMiB(int new_dbcache_size_mib)
{
    if (!canEditCoreSetting(QStringLiteral("dbcache"))) return;
    if (new_dbcache_size_mib != m_dbcache_size_mib) {
        const DirtySnapshot before = dirtySnapshot();
        m_dbcache_size_mib = new_dbcache_size_mib;
        writeCoreSettingOverride(QStringLiteral("dbcache"), currentCoreSettingValue(QStringLiteral("dbcache")));
        Q_EMIT dbcacheSizeMiBChanged(new_dbcache_size_mib);
        emitDirtySignals(before);
    }
}

void OptionsQmlModel::setListen(bool new_listen)
{
    m_core_settings.changeListen(new_listen);
}

void OptionsQmlModel::setMaxMempoolSizeMB(int new_max_mempool_size_mb)
{
    if (!canEditCoreSetting(QStringLiteral("maxmempool"))) return;
    if (new_max_mempool_size_mb != m_max_mempool_size_mb) {
        const DirtySnapshot before = dirtySnapshot();
        m_max_mempool_size_mb = new_max_mempool_size_mb;
        writeCoreSettingOverride(QStringLiteral("maxmempool"), currentCoreSettingValue(QStringLiteral("maxmempool")));
        Q_EMIT maxMempoolSizeMBChanged(new_max_mempool_size_mb);
        emitDirtySignals(before);
    }
}

void OptionsQmlModel::setNatpmp(bool new_natpmp)
{
    m_core_settings.changeNatpmp(new_natpmp);
}

void OptionsQmlModel::setPrune(bool new_prune)
{
    m_core_settings.changePrune(new_prune);
}

void OptionsQmlModel::setPruneSizeGB(int new_prune_size_gb)
{
    m_core_settings.changePruneSizeGB(new_prune_size_gb);
}

void OptionsQmlModel::setScriptThreads(int new_script_threads)
{
    if (!canEditCoreSetting(QStringLiteral("par"))) return;
    if (new_script_threads != m_script_threads) {
        const DirtySnapshot before = dirtySnapshot();
        m_script_threads = new_script_threads;
        writeCoreSettingOverride(QStringLiteral("par"), currentCoreSettingValue(QStringLiteral("par")));
        Q_EMIT scriptThreadsChanged(new_script_threads);
        emitDirtySignals(before);
    }
}

void OptionsQmlModel::setServer(bool new_server)
{
    m_core_settings.changeServer(new_server);
}

void OptionsQmlModel::setProxyEnabled(bool enabled)
{
    m_core_settings.changeProxyEnabled(enabled);
}

void OptionsQmlModel::setProxyAddress(const QString& address)
{
    commitProxyLocation(address);
}

void OptionsQmlModel::setTorEnabled(bool enabled)
{
    m_core_settings.changeTorEnabled(enabled);
}

void OptionsQmlModel::setTorAddress(const QString& address)
{
    commitTorLocation(address);
}

QString OptionsQmlModel::validateProxyLocation(const QString& location) const
{
    return m_core_settings.validateProxyLocation(location);
}

bool OptionsQmlModel::commitProxyLocation(const QString& location)
{
    const QmlCoreSettings::Change change = m_core_settings.changeProxyLocation(location);
    return change.accepted;
}

bool OptionsQmlModel::commitTorLocation(const QString& location)
{
    const QmlCoreSettings::Change change = m_core_settings.changeTorLocation(location);
    return change.accepted;
}

QString OptionsQmlModel::defaultProxyAddress() const
{
    return m_core_settings.defaultProxyAddress();
}

void OptionsQmlModel::setExternalSignerPath(const QString& path)
{
    if (!canEditCoreSetting(QStringLiteral("signer"))) return;
    const QString normalized_path = NormalizeCommandPath(path);
    if (normalized_path != m_external_signer_path) {
        const DirtySnapshot before = dirtySnapshot();
        m_external_signer_path = normalized_path;
        if (m_external_signer_path.isEmpty()) {
            m_node.forceSetting("signer", common::SettingsValue{});
        } else {
            m_node.forceSetting("signer", m_external_signer_path.toStdString());
        }
        writeCoreSettingOverride(QStringLiteral("signer"), currentCoreSettingValue(QStringLiteral("signer")));
        Q_EMIT externalSignerPathChanged(m_external_signer_path);
        emitDirtySignals(before);
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

QString OptionsQmlModel::getDefaultDataDirString()
{
    return QmlDataDir::DefaultDataDirString();
}


QUrl OptionsQmlModel::getDefaultDataDirectory()
{
    QString path = getDefaultDataDirString();
    return QUrl::fromLocalFile(path);
}

bool OptionsQmlModel::setCustomDataDirArgs(QString path)
{
    return selectCustomDataDir(path);
}

QString OptionsQmlModel::getCustomDataDirString()
{
#ifdef __ANDROID__
    m_custom_datadir_string = m_custom_datadir_string.replace("content://com.android.externalstorage.documents/tree/primary%3A", "/storage/self/primary/");
#endif // __ANDROID__
    return m_custom_datadir_string;
}

QString OptionsQmlModel::validateCustomDataDir(const QString& path) const
{
    return QmlDataDir::ValidateCustomDataDir(path);
}

bool OptionsQmlModel::selectCustomDataDir(const QString& path)
{
    const QString local_path = QmlDataDir::NormalizeLocalPath(path);
    if (local_path == m_custom_datadir_string && m_dataDir == local_path) {
        return true;
    }

    QString error;
    if (!QmlDataDir::PersistGuiDataDirSelection(local_path, &error)) {
        return false;
    }

    m_custom_datadir_string = local_path;
    Q_EMIT customDataDirStringChanged(local_path);
    setDataDir(local_path);
    return true;
}

void OptionsQmlModel::useDefaultDataDir()
{
    m_custom_datadir_string.clear();
    QmlDataDir::PersistDefaultDataDirSelection();
    Q_EMIT customDataDirStringChanged({});
    setDataDir(getDefaultDataDirString());
}

void OptionsQmlModel::setDataDir(QString new_data_dir)
{
    const QString normalized = QmlDataDir::NormalizeLocalPath(new_data_dir);
    const QString effective = normalized.isEmpty() ? getDefaultDataDirString() : normalized;
    if (effective == m_dataDir) return;
    m_dataDir = effective;
    Q_EMIT dataDirChanged(m_dataDir);
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
    if (!canEditCoreSetting(QStringLiteral("lang"))) return;
    if (new_language != m_language) {
        m_language = new_language;
        QSettings settings;
        settings.setValue(SettingsKeys::LANGUAGE, m_language);
        writeCoreSettingOverride(QStringLiteral("lang"), currentCoreSettingValue(QStringLiteral("lang")));
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
    new_display_unit = NormalizeDisplayUnit(new_display_unit);
    if (new_display_unit != m_display_unit) {
        m_display_unit = new_display_unit;
        QSettings settings;
        settings.setValue(SettingsKeys::DISPLAY_UNIT, m_display_unit);
        Q_EMIT displayUnitChanged(m_display_unit);
    }
}

void OptionsQmlModel::setThirdPartyTransactionUrls(const QString& urls)
{
    if (urls == m_third_party_transaction_urls) return;
    m_third_party_transaction_urls = urls;
    QSettings settings;
    settings.setValue(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, m_third_party_transaction_urls);
    Q_EMIT thirdPartyTransactionUrlsChanged();
}

QVariantList OptionsQmlModel::thirdPartyTransactionLinks(const QString& txid) const
{
    QVariantList links;
    const QStringList urls = m_third_party_transaction_urls.split(QLatin1Char('|'), Qt::SkipEmptyParts);
    for (QString url : urls) {
        url = url.trimmed();
        if (!url.contains(QStringLiteral("%s"))) continue;
        const QUrl parsed{url, QUrl::StrictMode};
        if (!IsThirdPartyTransactionUrlSchemeAllowed(parsed)) continue;
        const QString host = parsed.host();
        if (host.isEmpty()) continue;
        QVariantMap link;
        link.insert(QStringLiteral("host"), host);
        link.insert(QStringLiteral("url"), url.replace(QStringLiteral("%s"), txid));
        links.push_back(link);
    }
    return links;
}

void OptionsQmlModel::setMoneyFontChoice(const QString& choice)
{
    const QString normalized = choice == MONEY_FONT_BEST_SYSTEM ? QString{MONEY_FONT_BEST_SYSTEM} : QString{MONEY_FONT_EMBEDDED};
    if (normalized == m_money_font_choice) return;
    m_money_font_choice = normalized;
    QSettings settings;
    settings.setValue(SettingsKeys::MONEY_FONT_CHOICE, m_money_font_choice);
    Q_EMIT moneyFontChoiceChanged();
    Q_EMIT moneyFontChanged();
}

QFont OptionsQmlModel::moneyFont() const
{
    if (m_money_font_choice == MONEY_FONT_BEST_SYSTEM) {
        return QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    QFont font{QStringLiteral("Roboto Mono")};
    font.setStyleName(QStringLiteral("Regular"));
    return font;
}

QString OptionsQmlModel::displayUnitLabel() const
{
    return QmlBitcoinUnits::label(QmlBitcoinUnits::fromDisplayUnit(m_display_unit));
}

QString OptionsQmlModel::displayUnitLabelForAmount(qint64 satoshi) const
{
    return QmlBitcoinUnits::displayLabel(QmlBitcoinUnits::fromDisplayUnit(m_display_unit), satoshi);
}

void OptionsQmlModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    Q_EMIT shutdownFinished();
}
