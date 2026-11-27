// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <qml/legacy_settings_migration.h>

#include <chainparamsbase.h>
#include <clientversion.h>
#include <common/args.h>
#include <common/settings.h>
#include <common/system.h>
#include <mapport.h>
#include <net.h>
#include <netbase.h>
#include <node/caches.h>
#include <node/chainstatemanager_args.h>
#include <qml/core_settings.h>
#include <qml/guiconstants.h>
#include <qml/models/settings_keys.h>
#include <txdb.h>
#include <univalue.h>
#include <validation.h>
#include <util/fs_helpers.h>

#include <QCoreApplication>
#include <QDataStream>
#include <QMetaType>
#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <cstdint>
#include <cstdio>

#ifdef WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace QmlLegacySettingsDetail {
enum class LegacyBitcoinUnit : qint8 {
    BTC = 0,
    mBTC = 1,
    uBTC = 2,
    SAT = 3,
};

QDataStream& operator<<(QDataStream& out, const LegacyBitcoinUnit& unit)
{
    return out << static_cast<qint8>(unit);
}

QDataStream& operator>>(QDataStream& in, LegacyBitcoinUnit& unit)
{
    qint8 input;
    in >> input;
    switch (input) {
    case 0: unit = LegacyBitcoinUnit::BTC; break;
    case 1: unit = LegacyBitcoinUnit::mBTC; break;
    case 2: unit = LegacyBitcoinUnit::uBTC; break;
    case 3: unit = LegacyBitcoinUnit::SAT; break;
    default: unit = LegacyBitcoinUnit::BTC; break;
    }
    return in;
}
} // namespace QmlLegacySettingsDetail

Q_DECLARE_METATYPE(QmlLegacySettingsDetail::LegacyBitcoinUnit)

namespace QmlLegacySettings {
struct DirectoryCleanup {
    QString organization, application;
    QSettings::Format format;
    std::map<QString,QVariant> values;
    bool completed{false};
};
struct PendingCleanup {
    struct Source {
        QString organization, application;
        std::map<QString, QVariant> values;
        std::set<QString> consumed;
        QSettings::Format format;
        bool completed{false};
        bool reset{false};
    };
    fs::path target;
    bool requires_target_write{false};
    std::map<std::string, common::SettingsValue> expected;
    std::vector<Source> sources;
};
} // namespace QmlLegacySettings

namespace {
using LegacyBitcoinUnit = QmlLegacySettingsDetail::LegacyBitcoinUnit;

constexpr const char* QT_ORG_NAME{"Bitcoin"};
constexpr const char* QT_APP_NAME_DEFAULT{"Bitcoin-Qt"};
constexpr const char* QT_APP_NAME_TESTNET{"Bitcoin-Qt-testnet"};
constexpr const char* QT_APP_NAME_TESTNET4{"Bitcoin-Qt-testnet4"};
constexpr const char* QT_APP_NAME_SIGNET{"Bitcoin-Qt-signet"};
constexpr const char* QT_APP_NAME_REGTEST{"Bitcoin-Qt-regtest"};
constexpr const char* RESET_GUI_SETTINGS_KEY{"fReset"};

QString AppNameForChain(const QString& chain, bool legacy_qt)
{
    const QString normalized = chain.toLower();
    if (normalized == QStringLiteral("test")) return legacy_qt ? QT_APP_NAME_TESTNET : QAPP_APP_NAME_TESTNET;
    if (normalized == QStringLiteral("testnet4")) return legacy_qt ? QT_APP_NAME_TESTNET4 : QAPP_APP_NAME_TESTNET4;
    if (normalized == QStringLiteral("signet")) return legacy_qt ? QT_APP_NAME_SIGNET : QAPP_APP_NAME_SIGNET;
    if (normalized == QStringLiteral("regtest")) return legacy_qt ? QT_APP_NAME_REGTEST : QAPP_APP_NAME_REGTEST;
    return legacy_qt ? QT_APP_NAME_DEFAULT : QAPP_APP_NAME_DEFAULT;
}

void RegisterLegacyBitcoinUnitMetaType()
{
    static const int meta_type = qRegisterMetaType<LegacyBitcoinUnit>("BitcoinUnit");
    Q_UNUSED(meta_type);
}

std::unique_ptr<QSettings> OpenSettings(QString org, const QString& app, QSettings::Format format = QSettings::defaultFormat())
{
#ifdef Q_OS_MACOS
    // Default QSettings uses organizationDomain on Darwin. Match the actual
    // historical writers instead of inventing a com.<organization> domain.
    if (format == QSettings::NativeFormat) {
        if (org == QStringLiteral(QAPP_ORG_NAME)) org = QStringLiteral(QAPP_ORG_DOMAIN);
        else if (org == QStringLiteral("Bitcoin")) org = QStringLiteral("bitcoin.org");
    }
#endif
    // Any historical store may hold a serialized BitcoinUnit. QSettings parses
    // the whole store on first read, so every reader needs the type first.
    RegisterLegacyBitcoinUnitMetaType();
    return std::make_unique<QSettings>(format, QSettings::UserScope, org, app);
}

std::unique_ptr<QSettings> OpenLegacyDataDirSettings()
{
    return OpenSettings(QString::fromUtf8(QT_ORG_NAME), QString::fromUtf8(QT_APP_NAME_DEFAULT));
}

struct SettingsStore {
    bool legacy_qt{false};
    std::unique_ptr<QSettings> settings;
};

std::vector<SettingsStore> CoreSettingsStores(const QString& chain)
{
    std::vector<SettingsStore> stores;
    stores.push_back({
        /*legacy_qt=*/false,
        OpenSettings(QStringLiteral(QAPP_ORG_NAME), AppNameForChain(chain, /*legacy_qt=*/false)),
    });
    stores.push_back({
        /*legacy_qt=*/true,
        OpenSettings(QString::fromUtf8(QT_ORG_NAME), AppNameForChain(chain, /*legacy_qt=*/true)),
    });
    return stores;
}

const QStringList& LegacyCoreKeys(bool include_language)
{
    static const QStringList keys_with_language{
        QStringLiteral("nSettingsVersion"),
        QStringLiteral("nDatabaseCache"),
        QStringLiteral("nThreadsScriptVerif"),
#ifdef ENABLE_WALLET
        QStringLiteral("bSpendZeroConfChange"),
        QStringLiteral("external_signer_path"),
#endif
        QStringLiteral("fUseNatpmp"),
        QStringLiteral("fListen"),
        QStringLiteral("server"),
        QStringLiteral("nPruneSize"),
        QStringLiteral("bPrune"),
        QStringLiteral("addrProxy"),
        QStringLiteral("fUseProxy"),
        QStringLiteral("addrSeparateProxyTor"),
        QStringLiteral("fUseSeparateProxyTor"),
        QStringLiteral("language"),
    };
    static const QStringList keys_without_language{
        QStringLiteral("nSettingsVersion"),
        QStringLiteral("nDatabaseCache"),
        QStringLiteral("nThreadsScriptVerif"),
#ifdef ENABLE_WALLET
        QStringLiteral("bSpendZeroConfChange"),
        QStringLiteral("external_signer_path"),
#endif
        QStringLiteral("fUseNatpmp"),
        QStringLiteral("fListen"),
        QStringLiteral("server"),
        QStringLiteral("nPruneSize"),
        QStringLiteral("bPrune"),
        QStringLiteral("addrProxy"),
        QStringLiteral("fUseProxy"),
        QStringLiteral("addrSeparateProxyTor"),
        QStringLiteral("fUseSeparateProxyTor"),
    };
    return include_language ? keys_with_language : keys_without_language;
}

common::SettingsValue BoolValue(bool value)
{
    return common::SettingsValue{value};
}

common::SettingsValue IntValue(int64_t value)
{
    return common::SettingsValue{value};
}

common::SettingsValue StringValue(const QString& value)
{
    return common::SettingsValue{value.toStdString()};
}

common::SettingsValue MigrationDefaultValue(const QString& name)
{
    if (name == QStringLiteral("listen")) return BoolValue(DEFAULT_LISTEN);
    if (name == QStringLiteral("natpmp")) return BoolValue(DEFAULT_NATPMP);
    if (name == QStringLiteral("server")) return BoolValue(false);
    if (name == QStringLiteral("prune")) return IntValue(0);
    if (name == QStringLiteral("dbcache")) return IntValue(DEFAULT_KERNEL_CACHE / 1_MiB);
    if (name == QStringLiteral("par")) return IntValue(DEFAULT_SCRIPTCHECK_THREADS);
    if (name == QStringLiteral("signer")) return StringValue({});
    if (name == QStringLiteral("lang")) return StringValue({});
#ifdef ENABLE_WALLET
    if (name == QStringLiteral("spendzeroconfchange")) return BoolValue(true); // Historical Qt preference default, not a live wallet-policy read.
#endif
    return {};
}

bool MigrationValuesEqual(const QString& name, const common::SettingsValue& left, const common::SettingsValue& right)
{
    if (name == QStringLiteral("spendzeroconfchange")) {
        const std::optional<bool> left_bool = SettingToBool(left);
        const std::optional<bool> right_bool = SettingToBool(right);
        return left_bool && right_bool && *left_bool == *right_bool;
    }
    return QmlCoreSettings::CoreSettingValuesEqual(name, left, right);
}

bool HasPersistentSetting(ArgsManager& args, const QString& name)
{
    return !args.GetPersistentSetting(name.toStdString()).isNull();
}

bool WriteMigratedSetting(ArgsManager& args, const QString& name, const common::SettingsValue& value)
{
    if (HasPersistentSetting(args, name)) return false;
    if (MigrationValuesEqual(name, value, MigrationDefaultValue(name))) return false;
    QmlCoreSettings::SetRwSetting(args, name, value);
    return true;
}

bool MigrateSimpleSetting(ArgsManager& args, QSettings& settings, const QString& qt_name, const QString& core_name, const std::function<common::SettingsValue(const QVariant&)>& convert, std::set<QString>& consumed)
{
    if (!settings.contains(qt_name)) return false;

    bool changed{false};
    const auto desired=convert(settings.value(qt_name));
    if (WriteMigratedSetting(args, core_name, desired)) {
        changed = true;
    }

    auto actual=args.GetPersistentSetting(core_name.toStdString());
    if(actual.isNull())actual=MigrationDefaultValue(core_name);
    if(MigrationValuesEqual(core_name,desired,actual))consumed.insert(qt_name);
    return changed;
}

int ParseLegacyPruneSizeGB(const QVariant& prune_size)
{
    return std::max(1, prune_size.toInt());
}

QString ParsedProxyAddress(const QVariant& proxy)
{
    QString value = proxy.toString().trimmed();
    if (value.endsWith(QStringLiteral("%2"))) {
        return QmlCoreSettings::DefaultProxyAddress();
    }

    uint16_t port{0};
    std::string host;
    if (SplitHostPort(value.toStdString(), port, host) && port != 0) {
        if (host.find(':') != std::string::npos) {
            host = "[" + host + "]";
        }
        value = QStringLiteral("%1:%2").arg(QString::fromStdString(host)).arg(port);
        if (QmlCoreSettings::ProxyValidationError(value).isEmpty()) {
            return value;
        }
    }
    return QmlCoreSettings::DefaultProxyAddress();
}

bool MigratePruneSettings(ArgsManager& args, QSettings& settings, std::set<QString>& consumed)
{
    const bool has_size = settings.contains(QStringLiteral("nPruneSize"));
    const bool has_enabled = settings.contains(QStringLiteral("bPrune"));
    if (!has_size && !has_enabled) return false;

    bool changed{false};
    if (!HasPersistentSetting(args, QStringLiteral("prune"))) {
        const int prune_size_gb = has_size ? ParseLegacyPruneSizeGB(settings.value(QStringLiteral("nPruneSize"))) : DEFAULT_PRUNE_TARGET_GB;
        const bool enabled = has_enabled && settings.value(QStringLiteral("bPrune")).toBool();
        if (enabled) {
            if (!MigrationValuesEqual(QStringLiteral("prune"), QmlCoreSettings::PruneSetting(true, prune_size_gb), MigrationDefaultValue(QStringLiteral("prune")))) {
                QmlCoreSettings::SetRwSetting(args, QStringLiteral("prune"), QmlCoreSettings::PruneSetting(true, prune_size_gb));
                changed = true;
            }
            QmlCoreSettings::SetRwSetting(args, QStringLiteral("prune-prev"), {});
        } else if (has_size) {
            QmlCoreSettings::SetRwSetting(args, QStringLiteral("prune-prev"), QmlCoreSettings::PruneSetting(true, prune_size_gb));
            changed = true;
        }
    }

    const bool enabled=has_enabled&&settings.value(QStringLiteral("bPrune")).toBool();
    const auto size=has_size?ParseLegacyPruneSizeGB(settings.value(QStringLiteral("nPruneSize"))):DEFAULT_PRUNE_TARGET_GB;
    auto actual=args.GetPersistentSetting("prune");if(actual.isNull())actual=IntValue(0);
    const auto desired=enabled?QmlCoreSettings::PruneSetting(true,size):IntValue(0);
    const bool previous_matches=enabled||!has_size||MigrationValuesEqual(QStringLiteral("prune"),args.GetPersistentSetting("prune-prev"),QmlCoreSettings::PruneSetting(true,size));
    if(MigrationValuesEqual(QStringLiteral("prune"),desired,actual)&&previous_matches){
        if(has_enabled)consumed.insert(QStringLiteral("bPrune"));
        if(has_size)consumed.insert(QStringLiteral("nPruneSize"));
    }
    return changed;
}

bool MigrateProxySettings(ArgsManager& args, QSettings& settings, const QString& address_key, const QString& enabled_key, const QString& core_name, std::set<QString>& consumed)
{
    const bool has_address = settings.contains(address_key);
    const bool has_enabled = settings.contains(enabled_key);
    if (!has_address && !has_enabled) return false;

    bool changed{false};
    if (!HasPersistentSetting(args, core_name)) {
        const QString address = has_address ? ParsedProxyAddress(settings.value(address_key)) : QmlCoreSettings::DefaultProxyAddress();
        const bool enabled = has_enabled && settings.value(enabled_key).toBool();
        if (enabled) {
            QmlCoreSettings::SetRwSetting(args, core_name, QmlCoreSettings::ProxySetting(true, address));
            QmlCoreSettings::SetRwSetting(args, core_name + QStringLiteral("-prev"), {});
            changed = true;
        } else if (has_address) {
            QmlCoreSettings::SetRwSetting(args, core_name + QStringLiteral("-prev"), StringValue(address));
            changed = true;
        }
    }

    const bool enabled=has_enabled&&settings.value(enabled_key).toBool();
    const auto address=has_address?ParsedProxyAddress(settings.value(address_key)):QmlCoreSettings::DefaultProxyAddress();
    auto actual=args.GetPersistentSetting(core_name.toStdString());if(actual.isNull())actual=BoolValue(false);
    const auto desired=QmlCoreSettings::ProxySetting(enabled,address);
    const bool previous_matches=enabled||!has_address||args.GetPersistentSetting(QString(core_name+QStringLiteral("-prev")).toStdString()).write()==StringValue(address).write();
    if(MigrationValuesEqual(core_name,desired,actual)&&previous_matches){
        if(has_enabled)consumed.insert(enabled_key);
        if(has_address)consumed.insert(address_key);
    }
    return changed;
}

int LegacySettingsVersion(const QSettings& settings)
{
    static const QString settings_version_key{QStringLiteral("nSettingsVersion")};
    return settings.contains(settings_version_key) ? settings.value(settings_version_key).toInt() : 0;
}

bool MigrateStore(ArgsManager& args, SettingsStore& store, std::set<QString>& consumed)
{
    QSettings& settings{*store.settings};
    const bool include_language = store.legacy_qt;

    bool has_legacy_key{false};
    for (const QString& key : LegacyCoreKeys(include_language)) {
        if (settings.contains(key)) {
            has_legacy_key = true;
            break;
        }
    }
    if (!has_legacy_key) return false;

    const int settings_version{LegacySettingsVersion(settings)};

    bool changed{false};
    changed |= MigrateSimpleSetting(args, settings, QStringLiteral("nDatabaseCache"), QStringLiteral("dbcache"), [settings_version](const QVariant& value) {
        const qlonglong dbcache = value.toLongLong();
        return IntValue(settings_version < 130000 && dbcache == 100 ? DEFAULT_KERNEL_CACHE / 1_MiB : dbcache);
    }, consumed);
    changed |= MigrateSimpleSetting(args, settings, QStringLiteral("nThreadsScriptVerif"), QStringLiteral("par"), [](const QVariant& value) {
        return IntValue(value.toLongLong());
    }, consumed);
#ifdef ENABLE_WALLET
    changed |= MigrateSimpleSetting(args, settings, QStringLiteral("bSpendZeroConfChange"), QStringLiteral("spendzeroconfchange"), [](const QVariant& value) {
        return BoolValue(value.toBool());
    }, consumed);
    changed |= MigrateSimpleSetting(args, settings, QStringLiteral("external_signer_path"), QStringLiteral("signer"), [](const QVariant& value) {
        return StringValue(value.toString());
    }, consumed);
#endif
    changed |= MigrateSimpleSetting(args, settings, QStringLiteral("fUseNatpmp"), QStringLiteral("natpmp"), [](const QVariant& value) {
        return BoolValue(value.toBool());
    }, consumed);
    changed |= MigrateSimpleSetting(args, settings, QStringLiteral("fListen"), QStringLiteral("listen"), [](const QVariant& value) {
        return BoolValue(value.toBool());
    }, consumed);
    changed |= MigrateSimpleSetting(args, settings, QStringLiteral("server"), QStringLiteral("server"), [](const QVariant& value) {
        return BoolValue(value.toBool());
    }, consumed);
    changed |= MigratePruneSettings(args, settings, consumed);
    changed |= MigrateProxySettings(args, settings, QStringLiteral("addrProxy"), QStringLiteral("fUseProxy"), QStringLiteral("proxy"), consumed);
    changed |= MigrateProxySettings(args, settings, QStringLiteral("addrSeparateProxyTor"), QStringLiteral("fUseSeparateProxyTor"), QStringLiteral("onion"), consumed);
    if (include_language) {
        changed |= MigrateSimpleSetting(args, settings, QStringLiteral("language"), QStringLiteral("lang"), [](const QVariant& value) {
            return StringValue(value.toString());
        }, consumed);
    }

    return changed;
}
} // namespace

namespace QmlLegacySettings {

QString ReadLegacyGuiDataDir()
{
    const std::unique_ptr<QSettings> settings = OpenLegacyDataDirSettings();
    return settings->value(SettingsKeys::DATA_DIR).toString();
}

bool ReadLegacyGuiReset()
{
    const std::unique_ptr<QSettings> settings = OpenLegacyDataDirSettings();
    return settings->value(QString::fromUtf8(RESET_GUI_SETTINGS_KEY), false).toBool();
}

int ReadLegacyGuiDisplayUnit(const QString& chain, int fallback)
{
    const std::unique_ptr<QSettings> settings = OpenSettings(QString::fromUtf8(QT_ORG_NAME), AppNameForChain(chain, /*legacy_qt=*/true));
    if (!settings->contains(SettingsKeys::DISPLAY_UNIT)) {
        return fallback;
    }

    bool ok{false};
    const int display_unit = settings->value(SettingsKeys::DISPLAY_UNIT).toInt(&ok);
    if (!ok || display_unit < 0 || display_unit > 3) {
        return fallback;
    }
    return display_unit;
}

QString ReadLegacyGuiLanguage(const QString& chain)
{
    const std::unique_ptr<QSettings> chain_settings = OpenSettings(QString::fromUtf8(QT_ORG_NAME), AppNameForChain(chain, /*legacy_qt=*/true));
    QString chain_language = chain_settings->value(SettingsKeys::LANGUAGE).toString();
    if (!chain_language.isEmpty()) {
        return chain_language;
    }
    const std::unique_ptr<QSettings> default_settings = OpenSettings(QString::fromUtf8(QT_ORG_NAME), QString::fromUtf8(QT_APP_NAME_DEFAULT));
    return default_settings->value(SettingsKeys::LANGUAGE).toString();
}

bool ClearLegacyGuiSettings(const QString& chain, QString* error)
{
    for (SettingsStore& store : CoreSettingsStores(chain)) {
        for (const QString& key : LegacyCoreKeys(store.legacy_qt)) {
            store.settings->remove(key);
        }
        store.settings->remove(SettingsKeys::DISPLAY_UNIT);
        store.settings->sync();
        if (store.settings->status() != QSettings::NoError) {
            if (error) *error = QCoreApplication::translate("ProfileMigration", "The old settings may not have been cleared. The new settings are saved.");
            return false;
        }
    }

    const std::unique_ptr<QSettings> legacy_default_settings = OpenLegacyDataDirSettings();
    legacy_default_settings->remove(SettingsKeys::DATA_DIR);
    legacy_default_settings->remove(QString::fromUtf8(RESET_GUI_SETTINGS_KEY));
    legacy_default_settings->sync();
    if (legacy_default_settings->status() != QSettings::NoError) {
        if (error) *error = QCoreApplication::translate("ProfileMigration", "The old data directory setting may not have been cleared. The new setting is saved.");
        return false;
    }
    if (error) error->clear();
    return true;
}

std::shared_ptr<DirectoryCleanup> CaptureLegacyGuiDataDir()
{
    const auto settings = OpenLegacyDataDirSettings();
    settings->sync();
    if (settings->status() != QSettings::NoError) return {};
    auto result = std::make_shared<DirectoryCleanup>();
    result->organization=settings->organizationName();result->application=settings->applicationName();result->format=settings->format();
    for (const auto& key : {QString::fromLatin1(SettingsKeys::DATA_DIR),QString::fromUtf8(RESET_GUI_SETTINGS_KEY)})
        result->values.emplace(key,settings->contains(key)?settings->value(key):QVariant{});
    return result;
}
bool CompleteLegacyGuiDataDir(const std::shared_ptr<DirectoryCleanup>& cleanup, QString* error)
{
    if (error) error->clear();
    const auto fail=[&] {if(error)*error=QCoreApplication::translate("ProfileMigration","The selected directory was saved, but the old directory setting could not be cleared. It was kept.");return false;};
    if (!cleanup) return fail();
    if (cleanup->completed) return true;
    const auto settings=OpenSettings(cleanup->organization,cleanup->application,cleanup->format);
    settings->sync();
    if (settings->status()!=QSettings::NoError) return fail();
    for (const auto& [key,value]:cleanup->values)
        if(settings->contains(key)!=value.isValid()||(value.isValid()&&settings->value(key)!=value))return fail();
    for (const auto& [key,value]:cleanup->values) if(value.isValid())settings->remove(key);
    settings->sync();
    if(settings->status()!=QSettings::NoError)return fail();
    cleanup->completed=true;
    return true;
}
bool ClearLegacyGuiDataDir(QString* error)
{
    return CompleteLegacyGuiDataDir(CaptureLegacyGuiDataDir(),error);
}

MigrationResult MigrateCoreSettings(ArgsManager& args, MigrationMode mode)
{
    MigrationResult result;
    if (!args.GetSettingsPath()) {
        return result;
    }

    try {
        auto pending = std::make_shared<PendingCleanup>();
        if (mode == MigrationMode::Persist) args.GetSettingsPath(&pending->target);
        for (SettingsStore& store : CoreSettingsStores(QString::fromStdString(args.GetChainTypeString()))) {
            store.settings->sync();
            if (store.settings->status() != QSettings::NoError) {
                result.error = QCoreApplication::translate("ProfileMigration", "The old settings could not be read. They were kept.");
                return result;
            }
            PendingCleanup::Source source{store.settings->organizationName(),store.settings->applicationName(),{},{},store.settings->format()};
            if(mode==MigrationMode::Persist)
                for(const auto& key:LegacyCoreKeys(store.legacy_qt))
                    source.values.emplace(key,store.settings->contains(key)?store.settings->value(key):QVariant{});
            result.settings_changed |= MigrateStore(args,store,source.consumed);
            if(mode==MigrationMode::Persist&&!source.consumed.empty())pending->sources.push_back(std::move(source));
        }
        pending->requires_target_write = result.settings_changed;
        if (mode == MigrationMode::Persist && !pending->sources.empty()) {
            result.cleanup = std::move(pending);
            CaptureMigrationTarget(args, result);
        }
    } catch (const std::exception& e) {
        result.error = QString::fromStdString(e.what());
    }
    return result;
}


MigrationResult CaptureResetSettings(ArgsManager& args)
{
    MigrationResult result;
    if(!args.GetSettingsPath())return result;
    auto pending=std::make_shared<PendingCleanup>();args.GetSettingsPath(&pending->target);
    auto stores=CoreSettingsStores(QString::fromStdString(args.GetChainTypeString()));
    const auto directory_source=OpenLegacyDataDirSettings();
    const auto directory_org=directory_source->organizationName();const auto directory_app=directory_source->applicationName();
    stores.push_back({true,OpenLegacyDataDirSettings()});
    for(auto& store:stores){
        auto& settings=*store.settings;settings.sync();
        if(settings.status()!=QSettings::NoError){result.error=QCoreApplication::translate("ProfileMigration","The old reset setting could not be read. It was kept.");return result;}
        auto found=std::find_if(pending->sources.begin(),pending->sources.end(),[&](const auto& source){return source.organization==settings.organizationName()&&source.application==settings.applicationName()&&source.format==settings.format();});
        if(found==pending->sources.end()){
            pending->sources.push_back({settings.organizationName(),settings.applicationName(),{},{},settings.format(),false,true});
            found=std::prev(pending->sources.end());
        }
        QStringList keys=LegacyCoreKeys(store.legacy_qt);
        keys<<QString::fromLatin1(SettingsKeys::DISPLAY_UNIT);
        if(settings.organizationName()==directory_org&&settings.applicationName()==directory_app)
            keys<<QString::fromLatin1(SettingsKeys::DATA_DIR)<<QString::fromUtf8(RESET_GUI_SETTINGS_KEY);
        for(const auto& key:keys){
            const bool present=settings.contains(key);found->values.emplace(key,present?settings.value(key):QVariant{});
            if(present)found->consumed.insert(key);
        }
    }
    pending->requires_target_write=true;result.cleanup=std::move(pending);
    CaptureMigrationTarget(args,result);
    return result;
}

void CaptureMigrationTarget(ArgsManager& args, const MigrationResult& migration)
{
    if (migration.cleanup) args.LockSettings([&](const common::Settings& settings) {
        migration.cleanup->expected = settings.rw_settings;
    });
}

bool CommitTargetFile(const fs::path& target, QString* error)
{
#ifdef WIN32
    const HANDLE file = CreateFileW(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                   nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (error) *error = QCoreApplication::translate("ProfileMigration", "The new settings may not have been saved. The old settings were kept.");
    if (file == INVALID_HANDLE_VALUE) return false;
    const bool committed = FlushFileBuffers(file) != 0;
    CloseHandle(file);
    // Windows' replacement uses Core's atomic file rename. A directory flush
    // is not a supported separate Win32 receipt; do not manufacture one.
    if (committed && error) error->clear();
    return committed;
#else
    if (error) *error = QCoreApplication::translate("ProfileMigration", "The new settings may not have been saved. The old settings were kept.");
    const int file = open(target.c_str(), O_RDONLY);
    if (file < 0) return false;
    const bool committed = fsync(file) == 0;
    close(file);
    if (!committed) return false;
    const int directory = open(target.parent_path().c_str(), O_RDONLY);
    if (directory < 0) return false;
    const bool directory_committed = fsync(directory) == 0;
    close(directory);
    if (directory_committed && error) error->clear();
    return directory_committed;
#endif
}

bool CompleteCoreSettingsMigration(ArgsManager& args, const MigrationResult& migration, QString* error)
{
    if (error) error->clear();
    const auto fail = [&](const QString& message) {
        if (error) *error = message;
        return false;
    };
    if (!migration.error.isEmpty()) {
        if (error) *error = migration.error;
        return false;
    }
    if (!migration.cleanup) return true;
    const auto& pending = *migration.cleanup;
    fs::path target;
    if (!args.GetSettingsPath(&target) || target != pending.target)
        return fail(QCoreApplication::translate("ProfileMigration", "The new settings location changed. The old settings were kept."));
    const auto& expected = pending.expected;
    std::map<std::string, common::SettingsValue> observed;
    std::vector<std::string> errors;
    if ((pending.requires_target_write && !fs::exists(target)) || !common::ReadSettings(target, observed, errors))
        return fail(QCoreApplication::translate("ProfileMigration", "The new settings could not be checked. The old settings were kept."));
    if (expected.size() != observed.size())
        return fail(QCoreApplication::translate("ProfileMigration", "The new settings changed while being updated. The old settings were kept."));
    for (const auto& [name, value] : expected) {
        const auto found = observed.find(name);
        if (found == observed.end() || found->second.write() != value.write())
            return fail(QCoreApplication::translate("ProfileMigration", "The new settings changed while being updated. The old settings were kept."));
    }
    if (fs::exists(target) && !CommitTargetFile(target))
        return fail(QCoreApplication::translate("ProfileMigration", "The new settings could not be saved. The old settings were kept."));
    // Check every captured source before changing any of them. App-owned
    // effects serialize their writers; independent legacy applications do not
    // participate in this reservation, so this is not a cross-process CAS.
    std::vector<std::unique_ptr<QSettings>> stores;
    for (const auto& source : pending.sources) {
        if (source.completed) { stores.push_back({}); continue; }
        auto settings = OpenSettings(source.organization, source.application, source.format);
        settings->sync();
        if (settings->status() != QSettings::NoError)
            return fail(QCoreApplication::translate("ProfileMigration", "The replacement settings were saved, but the legacy source could not be checked."));
        for (const auto& [key, value] : source.values) {
            if (settings->contains(key) != value.isValid() || (value.isValid() && settings->value(key) != value))
                return fail(QCoreApplication::translate("ProfileMigration", "The old settings changed while they were being updated. Both the old and new settings were kept."));
        }
        stores.push_back(std::move(settings));
    }
    for (size_t i = 0; i < stores.size(); ++i) {
        if (!stores[i]) continue;
        auto& settings = *stores[i];
        for (const auto& key : pending.sources[i].consumed) settings.remove(key);
        bool remaining=false;
        for(const auto& [key,value]:pending.sources[i].values){(void)value;if(key!=QStringLiteral("nSettingsVersion")&&settings.contains(key))remaining=true;}
        remaining|=settings.contains(QStringLiteral("bSpendZeroConfChange"))||settings.contains(QStringLiteral("external_signer_path"));
        if(!pending.sources[i].reset&&!remaining&&LegacySettingsVersion(settings)<CLIENT_VERSION)settings.setValue(QStringLiteral("nSettingsVersion"),CLIENT_VERSION);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return fail(QCoreApplication::translate("ProfileMigration", "The new settings were saved, but the old ones may not have been cleared. Review the settings before trying again."));
        migration.cleanup->sources[i].completed = true;
    }
    return true;
}

} // namespace QmlLegacySettings
