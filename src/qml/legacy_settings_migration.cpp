// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

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

#include <QDataStream>
#include <QMetaType>
#include <QSettings>
#include <QVariant>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
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

std::unique_ptr<QSettings> OpenSettings(const QString& org, const QString& app)
{
    return std::make_unique<QSettings>(QSettings::defaultFormat(), QSettings::UserScope, org, app);
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
    if (name == QStringLiteral("dbcache")) return IntValue(node::GetDefaultDBCache() >> 20);
    if (name == QStringLiteral("par")) return IntValue(DEFAULT_SCRIPTCHECK_THREADS);
    if (name == QStringLiteral("lang")) return StringValue({});
    return {};
}

bool MigrationValuesEqual(const QString& name, const common::SettingsValue& left, const common::SettingsValue& right)
{
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

bool MigrateSimpleSetting(ArgsManager& args, QSettings& settings, QmlLegacySettings::MigrationMode mode, const QString& qt_name, const QString& core_name, const std::function<common::SettingsValue(const QVariant&)>& convert)
{
    if (!settings.contains(qt_name)) return false;

    bool changed{false};
    if (WriteMigratedSetting(args, core_name, convert(settings.value(qt_name)))) {
        changed = true;
    }

    if (mode == QmlLegacySettings::MigrationMode::Persist) {
        settings.remove(qt_name);
    }
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

bool MigratePruneSettings(ArgsManager& args, QSettings& settings, QmlLegacySettings::MigrationMode mode)
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

    if (mode == QmlLegacySettings::MigrationMode::Persist) {
        settings.remove(QStringLiteral("nPruneSize"));
        settings.remove(QStringLiteral("bPrune"));
    }
    return changed;
}

bool MigrateProxySettings(ArgsManager& args, QSettings& settings, QmlLegacySettings::MigrationMode mode, const QString& address_key, const QString& enabled_key, const QString& core_name)
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

    if (mode == QmlLegacySettings::MigrationMode::Persist) {
        settings.remove(address_key);
        settings.remove(enabled_key);
    }
    return changed;
}

int LegacySettingsVersion(const QSettings& settings)
{
    static const QString settings_version_key{QStringLiteral("nSettingsVersion")};
    return settings.contains(settings_version_key) ? settings.value(settings_version_key).toInt() : 0;
}

void UpdateLegacySettingsVersion(QSettings& settings, int settings_version, QmlLegacySettings::MigrationMode mode)
{
    if (settings_version >= CLIENT_VERSION) return;

    if (mode == QmlLegacySettings::MigrationMode::Persist) {
        settings.setValue(QStringLiteral("nSettingsVersion"), CLIENT_VERSION);
    }
}

bool MigrateStore(ArgsManager& args, SettingsStore& store, QmlLegacySettings::MigrationMode mode)
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
    UpdateLegacySettingsVersion(settings, settings_version, mode);

    bool changed{false};
    changed |= MigrateSimpleSetting(args, settings, mode, QStringLiteral("nDatabaseCache"), QStringLiteral("dbcache"), [settings_version](const QVariant& value) {
        const qlonglong dbcache = value.toLongLong();
        return IntValue(settings_version < 130000 && dbcache == 100 ? node::GetDefaultDBCache() >> 20 : dbcache);
    });
    changed |= MigrateSimpleSetting(args, settings, mode, QStringLiteral("nThreadsScriptVerif"), QStringLiteral("par"), [](const QVariant& value) {
        return IntValue(value.toLongLong());
    });
    changed |= MigrateSimpleSetting(args, settings, mode, QStringLiteral("fUseNatpmp"), QStringLiteral("natpmp"), [](const QVariant& value) {
        return BoolValue(value.toBool());
    });
    changed |= MigrateSimpleSetting(args, settings, mode, QStringLiteral("fListen"), QStringLiteral("listen"), [](const QVariant& value) {
        return BoolValue(value.toBool());
    });
    changed |= MigrateSimpleSetting(args, settings, mode, QStringLiteral("server"), QStringLiteral("server"), [](const QVariant& value) {
        return BoolValue(value.toBool());
    });
    changed |= MigratePruneSettings(args, settings, mode);
    changed |= MigrateProxySettings(args, settings, mode, QStringLiteral("addrProxy"), QStringLiteral("fUseProxy"), QStringLiteral("proxy"));
    changed |= MigrateProxySettings(args, settings, mode, QStringLiteral("addrSeparateProxyTor"), QStringLiteral("fUseSeparateProxyTor"), QStringLiteral("onion"));
    if (include_language) {
        changed |= MigrateSimpleSetting(args, settings, mode, QStringLiteral("language"), QStringLiteral("lang"), [](const QVariant& value) {
            return StringValue(value.toString());
        });
    }

    if (mode == QmlLegacySettings::MigrationMode::Persist) {
        settings.sync();
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
    RegisterLegacyBitcoinUnitMetaType();
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
    const QString chain_language = chain_settings->value(SettingsKeys::LANGUAGE).toString();
    if (!chain_language.isEmpty()) {
        return chain_language;
    }
    const std::unique_ptr<QSettings> default_settings = OpenSettings(QString::fromUtf8(QT_ORG_NAME), QString::fromUtf8(QT_APP_NAME_DEFAULT));
    return default_settings->value(SettingsKeys::LANGUAGE).toString();
}

QString ReadGuiLanguage(const QString& chain)
{
    const auto settings = OpenSettings(QStringLiteral(QAPP_ORG_NAME), AppNameForChain(chain, /*legacy_qt=*/false));
    return settings->value(SettingsKeys::LANGUAGE, ReadLegacyGuiLanguage(chain)).toString();
}

void ClearLegacyGuiSettings(const QString& chain)
{
    for (SettingsStore& store : CoreSettingsStores(chain)) {
        for (const QString& key : LegacyCoreKeys(store.legacy_qt)) {
            store.settings->remove(key);
        }
        store.settings->remove(SettingsKeys::DISPLAY_UNIT);
        store.settings->sync();
    }

    const std::unique_ptr<QSettings> legacy_default_settings = OpenLegacyDataDirSettings();
    legacy_default_settings->remove(SettingsKeys::DATA_DIR);
    legacy_default_settings->remove(QString::fromUtf8(RESET_GUI_SETTINGS_KEY));
    legacy_default_settings->sync();
}

MigrationResult MigrateCoreSettings(ArgsManager& args, MigrationMode mode)
{
    MigrationResult result;
    if (!args.GetSettingsPath()) {
        return result;
    }

    try {
        for (SettingsStore& store : CoreSettingsStores(QString::fromStdString(args.GetChainTypeString()))) {
            result.settings_changed |= MigrateStore(args, store, mode);
        }
    } catch (const std::exception& e) {
        result.error = QString::fromStdString(e.what());
    }
    return result;
}

} // namespace QmlLegacySettings
