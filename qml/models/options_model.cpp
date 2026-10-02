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
#include <QEventLoop>
#include <QDir>
#include <QFileInfo>
#include <QFontDatabase>
#include <QLocale>
#include <QRegularExpression>
#include <QSettings>
#include <QSet>
#include <QStringList>
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

struct OptionsQmlModel::Snapshot {
    QmlCoreSettings::Values core;
    QmlCoreSettings::Values effective_core;
    QVariantMap statuses;
    int dbcache;
    int mempool;
    int effective_mempool;
    int threads;
    QString signer;
    QString data_dir;
    QString default_data_dir;
    QString custom_data_dir;
    QString language;
    int display_unit;
    QString transaction_urls;
    QString money_font;
};

OptionsQmlModel::Snapshot OptionsQmlModel::readSnapshot(interfaces::Node& node, ArgsManager& args)
{
    Snapshot result;
    result.core = QmlCoreSettings::LoadDisplayValues(node, args);
    result.effective_core = result.core;
    result.effective_mempool = SettingTo<int64_t>(QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("maxmempool")), DEFAULT_MAX_MEMPOOL_SIZE_MB);
    result.statuses = QmlCoreSettings::BuildCoreSettingStatuses(args, QmlCoreSettings::CoreSettingNames());
    // Startup interactions remain effective until restart. Once the user saves
    // an override, show that saved request while retaining the source/status
    // explaining the running node's startup adjustment.
    const auto saved_override = [&](const QString& name) {
        const auto status = result.statuses.value(name).toMap();
        return status.value(QStringLiteral("startupAdjusted")).toBool()
            && status.value(QStringLiteral("hasRwSetting")).toBool();
    };
    const auto display_value = [&](const QString& name) {
        return saved_override(name) ? node.getPersistentSetting(name.toStdString())
                                    : QmlCoreSettings::DisplaySettingValue(node, args, name);
    };
    if (saved_override(QStringLiteral("listen"))) result.core.listen = SettingToBool(display_value(QStringLiteral("listen"))).value_or(result.core.listen);
    if (saved_override(QStringLiteral("natpmp"))) result.core.natpmp = SettingToBool(display_value(QStringLiteral("natpmp"))).value_or(result.core.natpmp);
    result.dbcache = SettingTo<int64_t>(display_value(QStringLiteral("dbcache")), DEFAULT_DB_CACHE >> 20);
    result.mempool = SettingTo<int64_t>(display_value(QStringLiteral("maxmempool")), DEFAULT_MAX_MEMPOOL_SIZE_MB);
    result.threads = SettingTo<int64_t>(display_value(QStringLiteral("par")), DEFAULT_SCRIPTCHECK_THREADS);
    result.signer = QString::fromStdString(SettingToString(QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("signer")), ""));
    const QString gui_data_dir = QmlDataDir::ReadGuiDataDir();
    const QString active_data_dir = QString::fromStdString(args.GetDataDirBase().utf8string());
    result.default_data_dir = QmlDataDir::DefaultDataDirString();
    result.data_dir = !active_data_dir.isEmpty() &&
        (QmlDataDir::HasExplicitDataDirArg(args) || QmlDataDir::IsDefaultDataDir(gui_data_dir))
        ? active_data_dir : gui_data_dir;
    if (!QmlDataDir::IsDefaultDataDir(result.data_dir)) result.custom_data_dir = result.data_dir;
    QSettings settings;
    result.language = QString::fromStdString(SettingToString(QmlCoreSettings::DisplaySettingValue(node, args, QStringLiteral("lang")), ""));
    if (result.language.isEmpty() && !QmlCoreSettings::IsCommandLineOverridden(args, QStringLiteral("lang"))) {
        result.language = settings.value(SettingsKeys::LANGUAGE, "").toString();
    }
    const QString command_line_language = QString::fromStdString(args.GetArg("-lang", ""));
    if (!command_line_language.isEmpty()) result.language = command_line_language;
    const int fallback = QmlLegacySettings::ReadLegacyGuiDisplayUnit(QString::fromStdString(args.GetChainTypeString()), 0);
    result.display_unit = NormalizeDisplayUnit(settings.value(SettingsKeys::DISPLAY_UNIT, fallback).toInt());
    result.transaction_urls = settings.value(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, "").toString();
    result.money_font = settings.value(SettingsKeys::MONEY_FONT_CHOICE, MONEY_FONT_EMBEDDED).toString();
    if (result.money_font != MONEY_FONT_EMBEDDED && result.money_font != MONEY_FONT_BEST_SYSTEM) result.money_font = MONEY_FONT_EMBEDDED;
    return result;
}

OptionsQmlModel::OptionsQmlModel(interfaces::Node& node, ArgsManager& args)
    : m_node{node}, m_args{args}
{
    buildAvailableLanguages();
    resetDirtySnapshots();
    refreshCoreSettingStatuses();
    connect(&m_executor, &BackendExecutor::drained, this, &OptionsQmlModel::shutdownFinished);
    m_core_settings.setBeforeChangeHandler([this](CoreSettingsModel::ChangeOrigin) {
        m_core_change_dirty_snapshot = dirtySnapshot();
    });
    m_core_settings.setAfterChangeHandler([this](const QmlCoreSettings::Change& change, CoreSettingsModel::ChangeOrigin) {
        if (!m_applying_snapshot) applyRuntimeCoreChange(change, m_core_change_dirty_snapshot);
    });
    runCommand([](interfaces::Node&, ArgsManager&) { return QString{}; });
}

OptionsQmlModel::~OptionsQmlModel()
{
    // Normal application shutdown enrolls this worker before interrupting Core.
    // Early startup failures and isolated model owners must preserve Node/Args
    // until the accepted work drains too, while continuing GUI event delivery.
    QEventLoop loop;
    connect(&m_executor, &BackendExecutor::drained, &loop, &QEventLoop::quit);
    beginShutdown();
    while (!m_executor.isDrained()) loop.exec();
}

void OptionsQmlModel::beginShutdown()
{
    if (m_stopping) return;
    m_stopping = true;
    refreshCoreSettingStatuses();
    m_executor.shutdown();
}

void OptionsQmlModel::setError(const QString& error)
{
    if (m_error == error) return;
    m_error = error;
    Q_EMIT settingsErrorChanged();
}

void OptionsQmlModel::setPending(bool pending)
{
    if (m_pending == pending) return;
    m_pending = pending;
    refreshCoreSettingStatuses();
    Q_EMIT settingsPendingChanged();
}

void OptionsQmlModel::applySnapshot(Snapshot snapshot)
{
    const bool initial = !m_ready;
    const DirtySnapshot before = dirtySnapshot();
    m_core_setting_statuses = std::move(snapshot.statuses);
    m_applying_snapshot = true;
    m_core_settings.clearTouchedSettings();
    const auto change = m_core_settings.applyPreviewValuesPreservingTouched(snapshot.core, {});
    m_applying_snapshot = false;
    m_dbcache_size_mib = snapshot.dbcache;
    m_max_mempool_size_mb = snapshot.mempool;
    m_script_threads = snapshot.threads;
    m_external_signer_path = std::move(snapshot.signer);
    m_dataDir = std::move(snapshot.data_dir);
    m_default_data_dir = std::move(snapshot.default_data_dir);
    m_custom_datadir_string = std::move(snapshot.custom_data_dir);
    m_language = std::move(snapshot.language);
    m_display_unit = snapshot.display_unit;
    m_third_party_transaction_urls = std::move(snapshot.transaction_urls);
    m_money_font_choice = std::move(snapshot.money_font);
    m_ready = true;
    if (initial) {
        resetDirtySnapshots();
        m_initial_core_values = snapshot.effective_core;
        m_initial_max_mempool_size_mb = snapshot.effective_mempool;
    }
    QmlCoreSettings::EmitCoreSettingSignals(*this, change);
    Q_EMIT dbcacheSizeMiBChanged(m_dbcache_size_mib);
    Q_EMIT maxMempoolSizeMBChanged(m_max_mempool_size_mb);
    Q_EMIT scriptThreadsChanged(m_script_threads);
    Q_EMIT externalSignerPathChanged(m_external_signer_path);
    Q_EMIT dataDirChanged(m_dataDir);
    Q_EMIT dataDirDefaultsChanged();
    Q_EMIT customDataDirStringChanged(m_custom_datadir_string);
    Q_EMIT languageChanged();
    Q_EMIT displayUnitChanged(m_display_unit);
    Q_EMIT thirdPartyTransactionUrlsChanged();
    Q_EMIT moneyFontChoiceChanged();
    Q_EMIT moneyFontChanged();
    refreshCoreSettingStatuses();
    emitDirtySignals(before);
    if (initial) Q_EMIT settingsReadyChanged();
}

void OptionsQmlModel::runCommand(std::function<QString(interfaces::Node&, ArgsManager&)> command)
{
    if (m_stopping) return;
    if (m_pending_commands++ == 0) setError({});
    const quint64 revision = ++m_command_revision;
    setPending(true);
    m_executor.submit(this, [node = &m_node, args = &m_args, command = std::move(command)] {
        const QString error = command(*node, *args);
        return std::make_pair(readSnapshot(*node, *args), error);
    }, [this, revision](auto result) {
        if (revision == m_command_revision) applySnapshot(std::move(result.first));
        if (!result.second.isEmpty()) setError(result.second);
        if (--m_pending_commands == 0) setPending(false);
    }, [this](std::exception_ptr exception) {
        try { std::rethrow_exception(exception); }
        catch (const std::exception& error) { setError(QString::fromUtf8(error.what())); }
        catch (...) { setError(tr("Unable to load or save settings.")); }
        if (--m_pending_commands == 0) setPending(false);
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
    QVariantMap statuses = m_core_setting_statuses;
    if (!m_ready || m_stopping) {
        for (const auto& name : QmlCoreSettings::CoreSettingNames()) {
            auto status = statuses.value(name).toMap();
            status.insert(QStringLiteral("canEdit"), false);
            statuses.insert(name, status);
        }
    }
    return statuses;
}

QVariantMap OptionsQmlModel::coreSettingStatus(const QString& name) const
{
    return coreSettingStatuses().value(name).toMap();
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
    if (!change.accepted || !QmlCoreSettings::ValuesChanged(change)) return;
    const QmlCoreSettings::Session session{change.after};
    runCommand([session, name = change.setting_name](interfaces::Node& node, ArgsManager& args) {
        QString error;
        const bool saved = QmlCoreSettings::PersistSettings(args, {name, name + QStringLiteral("-prev")},
            [&] { return session.writeToArgs(args, name); }, &error);
        if (saved && name == QStringLiteral("natpmp")) node.mapPort(session.values().natpmp);
        return error;
    });
    QmlCoreSettings::EmitCoreSettingSignals(*this, change);
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
    if (!m_ready || m_stopping || m_validation_pending) return false;
    return coreSettingStatus(name).value(QStringLiteral("canEdit"), false).toBool();
}

bool OptionsQmlModel::writeCoreSettingOverride(const QString& name, const common::SettingsValue& value)
{
    if (!canEditCoreSetting(name)) return false;
    runCommand([name, value](interfaces::Node& node, ArgsManager& args) {
        QString error;
        const bool saved = QmlCoreSettings::PersistSettings(args, {name},
            [&] { return QmlCoreSettings::WriteCoreSettingOverride(args, name, value); }, &error);
        if (saved && name == QStringLiteral("signer")) node.forceSetting("signer", value);
        return error;
    });
    return true;
}

void OptionsQmlModel::refreshCoreSettingStatuses()
{
    m_core_settings.setStatuses(CoreSettingStatusesForNames(coreSettingStatuses(), QmlCoreSettings::OnboardingCoreSettingNames()));
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

namespace {
QString SignerPathError(const QString& normalized_path)
{
    if (normalized_path.isEmpty()) return {};
    const QString token = FirstCommandToken(normalized_path);
    if (token.isEmpty() || !TokenLooksLikePath(token)) return {};
    const QFileInfo info(ExpandUserPath(token));
    if (!info.exists()) return QObject::tr("The configured signer path does not exist.");
    if (!info.isFile()) return QObject::tr("The configured signer path is not a file.");
    if (!info.isExecutable()) return QObject::tr("The configured signer path is not executable.");
    return {};
}

QString SaveGuiSetting(const QString& key, const QVariant& value)
{
    QSettings settings;
    const QVariant previous = settings.value(key);
    if (value.isValid()) settings.setValue(key, value);
    else settings.remove(key);
    settings.sync();
    if (settings.status() == QSettings::NoError) return {};
    if (previous.isValid()) settings.setValue(key, previous);
    else settings.remove(key);
    settings.sync();
    return QObject::tr("Unable to save application settings.");
}
} // namespace

void OptionsQmlModel::setExternalSignerPath(const QString& path)
{
    if (!canEditCoreSetting(QStringLiteral("signer"))) return;
    const QString normalized_path = NormalizeCommandPath(path);
    if (normalized_path == m_external_signer_path) return;
    const auto before = dirtySnapshot();
    m_external_signer_path = normalized_path;
    Q_EMIT externalSignerPathChanged(m_external_signer_path);
    emitDirtySignals(before);
    runCommand([normalized_path](interfaces::Node& node, ArgsManager& args) {
        QString error = SignerPathError(normalized_path);
        if (!error.isEmpty()) return error;
        const common::SettingsValue value = normalized_path.isEmpty()
            ? common::SettingsValue{} : common::SettingsValue{normalized_path.toStdString()};
        if (QmlCoreSettings::PersistSettings(args, {QStringLiteral("signer")},
                [&] { return QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("signer"), value); }, &error)) {
            node.forceSetting("signer", value);
        }
        return error;
    });
}

QString OptionsQmlModel::externalSignerPathValidationError(const QString& path) const
{
    return NormalizeCommandPath(path) == m_validated_signer_path ? m_signer_validation_error : QString{};
}

void OptionsQmlModel::requestExternalSignerPathValidation(const QString& path)
{
    if (m_stopping) return;
    m_requested_signer_path = NormalizeCommandPath(path);
    ++m_signer_validation_revision;
    m_signer_validation_pending = true;
    m_signer_validation_error.clear();
    Q_EMIT signerPathValidationChanged();
    if (!m_signer_validation_in_flight) startSignerValidation();
}

void OptionsQmlModel::startSignerValidation()
{
    const QString normalized = m_requested_signer_path;
    const quint64 revision = m_signer_validation_revision;
    m_signer_validation_in_flight = true;
    m_executor.submit(this, [normalized] { return SignerPathError(normalized); }, [this, normalized, revision](const QString& error) {
        m_signer_validation_in_flight = false;
        if (revision != m_signer_validation_revision) { startSignerValidation(); return; }
        m_validated_signer_path = normalized;
        m_signer_validation_error = error;
        m_signer_validation_pending = false;
        Q_EMIT signerPathValidationChanged();
    }, [this, revision](std::exception_ptr) {
        m_signer_validation_in_flight = false;
        if (revision != m_signer_validation_revision) { startSignerValidation(); return; }
        m_signer_validation_error = tr("Unable to validate the signer path.");
        m_signer_validation_pending = false;
        Q_EMIT signerPathValidationChanged();
    });
}

QString OptionsQmlModel::getDefaultDataDirString()
{
    return m_default_data_dir;
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
    return QmlDataDir::NormalizeLocalPath(path) == m_validated_data_dir ? m_data_dir_validation_error : QString{};
}

bool OptionsQmlModel::selectCustomDataDir(const QString& path)
{
    if (!m_ready || m_stopping || m_pending || m_validation_pending) return false;
    const QString local_path = QmlDataDir::NormalizeLocalPath(path);
    m_validation_pending = true;
    Q_EMIT validationPendingChanged();
    setError({});
    m_executor.submit(this, [local_path] {
        QString error;
        QmlDataDir::PersistGuiDataDirSelection(local_path, &error);
        return error;
    }, [this, local_path](const QString& error) {
        m_validated_data_dir = local_path;
        m_data_dir_validation_error = error;
        if (error.isEmpty()) {
            m_custom_datadir_string = local_path;
            Q_EMIT customDataDirStringChanged(local_path);
            setDataDir(local_path);
        }
        setError(error);
        m_validation_pending = false;
        Q_EMIT validationPendingChanged();
        Q_EMIT dataDirSelectionFinished(error.isEmpty(), error);
    }, [this](std::exception_ptr) {
        const QString error = tr("Unable to select the data directory.");
        setError(error);
        m_validation_pending = false;
        Q_EMIT validationPendingChanged();
        Q_EMIT dataDirSelectionFinished(false, error);
    });
    return true;
}

void OptionsQmlModel::useDefaultDataDir()
{
    if (!m_ready || m_stopping || m_pending || m_validation_pending) return;
    m_validation_pending = true;
    Q_EMIT validationPendingChanged();
    m_executor.submit(this, [] { QString error; QmlDataDir::PersistDefaultDataDirSelection(&error); return error; }, [this](const QString& error) {
        if (error.isEmpty()) {
            m_custom_datadir_string.clear();
            Q_EMIT customDataDirStringChanged({});
            setDataDir(m_default_data_dir);
        }
        setError(error);
        m_validation_pending = false;
        Q_EMIT validationPendingChanged();
        Q_EMIT dataDirSelectionFinished(error.isEmpty(), error);
    }, [this](std::exception_ptr) {
        const QString error = tr("Unable to select the data directory.");
        setError(error);
        m_validation_pending = false;
        Q_EMIT validationPendingChanged();
        Q_EMIT dataDirSelectionFinished(false, error);
    });
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

void OptionsQmlModel::persistGuiSetting(const QString& key, const QVariant& value)
{
    if (!m_ready || m_stopping || m_validation_pending) return;
    runCommand([key, value](interfaces::Node&, ArgsManager&) { return SaveGuiSetting(key, value); });
}

void OptionsQmlModel::setLanguage(const QString& new_language)
{
    if (!canEditCoreSetting(QStringLiteral("lang")) || new_language == m_language) return;
    m_language = new_language;
    Q_EMIT languageChanged();
    runCommand([new_language](interfaces::Node&, ArgsManager& args) {
        QString error;
        if (QmlCoreSettings::PersistSettings(args, {QStringLiteral("lang")}, [&] {
                return QmlCoreSettings::WriteCoreSettingOverride(args, QStringLiteral("lang"), common::SettingsValue{new_language.toStdString()});
            }, &error)) error = SaveGuiSetting(SettingsKeys::LANGUAGE, new_language);
        return error;
    });
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
    if (!m_ready || m_stopping || m_validation_pending || new_display_unit == m_display_unit) return;
    m_display_unit = new_display_unit;
    Q_EMIT displayUnitChanged(m_display_unit);
    persistGuiSetting(SettingsKeys::DISPLAY_UNIT, new_display_unit);
}

void OptionsQmlModel::setThirdPartyTransactionUrls(const QString& urls)
{
    if (!m_ready || m_stopping || m_validation_pending || urls == m_third_party_transaction_urls) return;
    m_third_party_transaction_urls = urls;
    Q_EMIT thirdPartyTransactionUrlsChanged();
    persistGuiSetting(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, urls);
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
    if (!m_ready || m_stopping || m_validation_pending || normalized == m_money_font_choice) return;
    m_money_font_choice = normalized;
    Q_EMIT moneyFontChoiceChanged();
    Q_EMIT moneyFontChanged();
    persistGuiSetting(SettingsKeys::MONEY_FONT_CHOICE, normalized);
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
