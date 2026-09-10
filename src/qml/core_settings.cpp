// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/core_settings.h>

#include <common/args.h>
#include <common/system.h>
#include <interfaces/node.h>
#include <mapport.h>
#include <net.h>
#include <netbase.h>
#include <node/caches.h>
#include <node/chainstatemanager_args.h>
#include <qml/guiconstants.h>
#include <txdb.h>
#include <univalue.h>
#include <validation.h>

#include <QCoreApplication>

#include <cctype>
#include <optional>
#include <string>
#include <utility>

namespace {
constexpr const char* DEFAULT_PROXY_HOST{"127.0.0.1"};
constexpr int DEFAULT_PROXY_PORT{9050};

bool IsBoolCoreSetting(const QString& name)
{
    return name == QStringLiteral("listen") ||
           name == QStringLiteral("natpmp") ||
           name == QStringLiteral("server");
}

bool IsIntCoreSetting(const QString& name)
{
    return name == QStringLiteral("dbcache") ||
           name == QStringLiteral("par") ||
           name == QStringLiteral("maxmempool") ||
           name == QStringLiteral("prune");
}

bool IsParameterInteractionSetting(const QString& name)
{
    return name == QStringLiteral("listen") ||
           name == QStringLiteral("natpmp") ||
           name == QStringLiteral("maxmempool");
}

bool IsLegacyNumericRwSetting(const QString& name)
{
    return name == QStringLiteral("dbcache") ||
           name == QStringLiteral("par") ||
           name == QStringLiteral("prune") ||
           name == QStringLiteral("prune-prev");
}

bool IsOptionalStringCoreSetting(const QString& name)
{
    return name == QStringLiteral("proxy") ||
           name == QStringLiteral("onion") ||
           name == QStringLiteral("lang");
}

QString ProxyAddressFromSetting(const common::SettingsValue& setting)
{
    QString proxy_setting = QString::fromStdString(SettingToString(setting, ""));
    if (proxy_setting == QStringLiteral("0")) proxy_setting.clear();
    return proxy_setting;
}

bool SettingStatusCanEdit(const QVariantMap& statuses, const QString& name)
{
    return statuses.value(name).toMap().value(QStringLiteral("canEdit"), true).toBool();
}

bool IsSessionSetting(const QString& name)
{
    return name == QStringLiteral("listen") ||
           name == QStringLiteral("natpmp") ||
           name == QStringLiteral("server") ||
           name == QStringLiteral("prune") ||
           name == QStringLiteral("proxy") ||
           name == QStringLiteral("onion");
}

const QStringList& SessionSettingNames()
{
    return QmlCoreSettings::OnboardingCoreSettingNames();
}

common::SettingsValue ForcedParameterInteractionValue(ArgsManager& args, const QString& name)
{
    if (!IsParameterInteractionSetting(name)) return {};

    common::SettingsValue value;
    const std::string key = name.toStdString();
    args.LockSettings([&](common::Settings& settings) {
        if (const common::SettingsValue* forced_value = common::FindKey(settings.forced_settings, key)) {
            value = *forced_value;
        }
    });
    return value;
}

bool HasEffectiveSetting(ArgsManager& args, const QString& name)
{
    return !args.GetSetting(std::string{"-"} + name.toStdString()).isNull();
}

std::string TrimTrailingRootDot(std::string host)
{
    if (!host.empty() && host.back() == '.') host.pop_back();
    return host;
}

bool IsDottedNumericHost(const std::string& host)
{
    bool has_dot{false};
    bool has_digit{false};
    for (unsigned char c : host) {
        if (c == '.') {
            has_dot = true;
        } else if (std::isdigit(c)) {
            has_digit = true;
        } else {
            return false;
        }
    }
    return has_dot && has_digit;
}

bool IsValidHostnameSyntax(const std::string& host)
{
    if (host.empty() || host.size() > 253 || host.find('\0') != std::string::npos) return false;

    const std::string hostname = TrimTrailingRootDot(host);
    if (hostname.empty() || IsDottedNumericHost(hostname)) return false;

    size_t label_len{0};
    unsigned char previous{0};
    for (unsigned char c : hostname) {
        if (c == '.') {
            if (label_len == 0 || previous == '-') return false;
            label_len = 0;
            previous = c;
            continue;
        }
        if (!std::isalnum(c) && c != '-') return false;
        if (label_len == 0 && c == '-') return false;
        ++label_len;
        if (label_len > 63) return false;
        previous = c;
    }

    return label_len > 0 && previous != '-';
}
} // namespace

namespace QmlCoreSettings {

const QStringList& CoreSettingNames()
{
    static const QStringList names{
        QStringLiteral("listen"),
        QStringLiteral("natpmp"),
        QStringLiteral("server"),
        QStringLiteral("prune"),
        QStringLiteral("dbcache"),
        QStringLiteral("par"),
        QStringLiteral("maxmempool"),
        QStringLiteral("proxy"),
        QStringLiteral("onion"),
        QStringLiteral("lang"),
    };
    return names;
}

const QStringList& OnboardingCoreSettingNames()
{
    static const QStringList names{
        QStringLiteral("listen"),
        QStringLiteral("natpmp"),
        QStringLiteral("server"),
        QStringLiteral("prune"),
        QStringLiteral("proxy"),
        QStringLiteral("onion"),
    };
    return names;
}

int PruneMiBToGB(int64_t mib)
{
    return (mib * 1024 * 1024 + GB_BYTES - 1) / GB_BYTES;
}

int64_t PruneGBToMiB(int gb)
{
    return gb * GB_BYTES / 1024 / 1024;
}

bool PruneEnabled(const common::SettingsValue& setting)
{
    return SettingTo<int64_t>(setting, 0) > 1;
}

int PruneSizeGBFromSetting(const common::SettingsValue& setting)
{
    const int64_t value = SettingTo<int64_t>(setting, 0);
    return value > 1 ? PruneMiBToGB(value) : DEFAULT_PRUNE_TARGET_GB;
}

common::SettingsValue PruneSetting(bool enabled, int prune_size_gb)
{
    return enabled ? common::SettingsValue{PruneGBToMiB(prune_size_gb < 1 ? 1 : prune_size_gb)}
                   : common::SettingsValue{0};
}

QString DefaultProxyAddress()
{
    return QStringLiteral("%1:%2").arg(DEFAULT_PROXY_HOST).arg(DEFAULT_PROXY_PORT);
}

QString ProxyValidationError(const QString& location)
{
    const QString trimmed = location.trimmed();
    if (trimmed.isEmpty()) {
        return QObject::tr("Proxy location is required.");
    }

    const std::string value = trimmed.toStdString();
    if (value.find('\0') != std::string::npos) {
        return QObject::tr("The supplied proxy location is invalid.");
    }
    if (trimmed.contains(QStringLiteral("="))) {
        return QObject::tr("Per-network proxy values are not supported in this field.");
    }
    if (value.starts_with(ADDR_PREFIX_UNIX)) {
        if (IsUnixSocketPath(value)) return {};
        return QObject::tr("This Unix socket proxy path is not supported on this platform or is too long.");
    }

    uint16_t port{DEFAULT_PROXY_PORT};
    std::string host;
    if (!SplitHostPort(value, port, host) || host.empty()) {
        return QObject::tr("Enter a proxy location as host, host:port, [IPv6], [IPv6]:port, or unix:/path.");
    }

    const CService service{LookupNumeric(value, DEFAULT_PROXY_PORT)};
    const Proxy proxy{service, /*tor_stream_isolation=*/true};
    if (!proxy.IsValid()) {
        if (IsValidHostnameSyntax(host)) return {};
        return QObject::tr("The supplied proxy location is invalid.");
    }
    return {};
}

common::SettingsValue ProxySetting(bool enabled, const QString& address)
{
    const QString trimmed = address.trimmed();
    return enabled && !trimmed.isEmpty()
        ? common::SettingsValue{trimmed.toStdString()}
        : common::SettingsValue{false};
}

Values LoadPersistentValues(interfaces::Node& node)
{
    Values values;
    values.listen = SettingToBool(node.getPersistentSetting("listen"), DEFAULT_LISTEN);
    values.natpmp = SettingToBool(node.getPersistentSetting("natpmp"), DEFAULT_NATPMP);
    values.server = SettingToBool(node.getPersistentSetting("server"), false);

    const common::SettingsValue prune_setting = node.getPersistentSetting("prune");
    values.prune = PruneEnabled(prune_setting);
    values.prune_size_gb = values.prune
        ? PruneSizeGBFromSetting(prune_setting)
        : PruneSizeGBFromSetting(node.getPersistentSetting("prune-prev"));

    const QString proxy_setting = ProxyAddressFromSetting(node.getPersistentSetting("proxy"));
    values.proxy_enabled = !proxy_setting.isEmpty();
    values.proxy_address = values.proxy_enabled
        ? proxy_setting
        : QString::fromStdString(SettingToString(node.getPersistentSetting("proxy-prev"), ""));

    const QString onion_setting = ProxyAddressFromSetting(node.getPersistentSetting("onion"));
    values.tor_enabled = !onion_setting.isEmpty();
    values.tor_address = values.tor_enabled
        ? onion_setting
        : QString::fromStdString(SettingToString(node.getPersistentSetting("onion-prev"), ""));

    return values;
}

Values LoadEffectiveValues(ArgsManager& args)
{
    Values values;
    values.listen = SettingToBool(args.GetSetting("-listen"), DEFAULT_LISTEN);
    values.natpmp = SettingToBool(args.GetSetting("-natpmp"), DEFAULT_NATPMP);
    values.server = SettingToBool(args.GetSetting("-server"), false);

    const common::SettingsValue prune_setting = args.GetSetting("-prune");
    values.prune = PruneEnabled(prune_setting);
    values.prune_size_gb = values.prune
        ? PruneSizeGBFromSetting(prune_setting)
        : PruneSizeGBFromSetting(args.GetPersistentSetting("prune-prev"));

    const QString proxy_setting = ProxyAddressFromSetting(args.GetSetting("-proxy"));
    values.proxy_enabled = !proxy_setting.isEmpty();
    values.proxy_address = values.proxy_enabled
        ? proxy_setting
        : QString::fromStdString(SettingToString(args.GetPersistentSetting("proxy-prev"), ""));
    if (values.proxy_address.isEmpty()) values.proxy_address = DefaultProxyAddress();

    const QString onion_setting = ProxyAddressFromSetting(args.GetSetting("-onion"));
    values.tor_enabled = !onion_setting.isEmpty();
    values.tor_address = values.tor_enabled
        ? onion_setting
        : QString::fromStdString(SettingToString(args.GetPersistentSetting("onion-prev"), ""));
    if (values.tor_address.isEmpty()) values.tor_address = DefaultProxyAddress();

    return values;
}

bool IsCommandLineOverridden(ArgsManager& args, const QString& name)
{
    const std::string key = name.toStdString();
    bool overridden{false};
    args.LockSettings([&](common::Settings& settings) {
        if (auto* options = common::FindKey(settings.command_line_options, key)) {
            overridden = !options->empty();
        }
    });
    return overridden;
}

Values LoadDisplayValues(interfaces::Node& node, ArgsManager& args)
{
    Values values = LoadPersistentValues(node);
    const Values effective_values = LoadEffectiveValues(args);

    if (HasEffectiveSetting(args, QStringLiteral("listen"))) {
        values.listen = effective_values.listen;
    }
    if (HasEffectiveSetting(args, QStringLiteral("natpmp"))) {
        values.natpmp = effective_values.natpmp;
    }
    if (HasEffectiveSetting(args, QStringLiteral("server"))) {
        values.server = effective_values.server;
    }
    if (HasEffectiveSetting(args, QStringLiteral("prune"))) {
        values.prune = effective_values.prune;
        values.prune_size_gb = effective_values.prune_size_gb;
    }
    if (HasEffectiveSetting(args, QStringLiteral("proxy"))) {
        values.proxy_enabled = effective_values.proxy_enabled;
        values.proxy_address = effective_values.proxy_address;
    }
    if (HasEffectiveSetting(args, QStringLiteral("onion"))) {
        values.tor_enabled = effective_values.tor_enabled;
        values.tor_address = effective_values.tor_address;
    }

    return values;
}

bool ValuesEqual(const Values& left, const Values& right)
{
    return left.prune == right.prune &&
           left.prune_size_gb == right.prune_size_gb &&
           left.listen == right.listen &&
           left.natpmp == right.natpmp &&
           left.server == right.server &&
           left.proxy_enabled == right.proxy_enabled &&
           left.proxy_address == right.proxy_address &&
           left.tor_enabled == right.tor_enabled &&
           left.tor_address == right.tor_address;
}

bool ValuesChanged(const Change& change)
{
    return !ValuesEqual(change.before, change.after);
}

bool StatusesChanged(const Change& change)
{
    return change.before_statuses != change.after_statuses;
}

bool ListenChanged(const Change& change)
{
    return change.before.listen != change.after.listen;
}

bool NatpmpChanged(const Change& change)
{
    return change.before.natpmp != change.after.natpmp;
}

bool ServerChanged(const Change& change)
{
    return change.before.server != change.after.server;
}

bool PruneChanged(const Change& change)
{
    return change.before.prune != change.after.prune;
}

bool PruneSizeGBChanged(const Change& change)
{
    return change.before.prune_size_gb != change.after.prune_size_gb;
}

bool ProxyEnabledChanged(const Change& change)
{
    return change.before.proxy_enabled != change.after.proxy_enabled;
}

bool ProxyAddressChanged(const Change& change)
{
    return change.before.proxy_address != change.after.proxy_address;
}

bool TorEnabledChanged(const Change& change)
{
    return change.before.tor_enabled != change.after.tor_enabled;
}

bool TorAddressChanged(const Change& change)
{
    return change.before.tor_address != change.after.tor_address;
}

common::SettingsValue DefaultCoreSettingValue(const QString& name)
{
    if (name == QStringLiteral("listen")) return DEFAULT_LISTEN;
    if (name == QStringLiteral("natpmp")) return DEFAULT_NATPMP;
    if (name == QStringLiteral("server")) return false;
    if (name == QStringLiteral("prune")) return 0;
    if (name == QStringLiteral("dbcache")) return node::GetDefaultDBCache() >> 20;
    if (name == QStringLiteral("par")) return DEFAULT_SCRIPTCHECK_THREADS;
    if (name == QStringLiteral("maxmempool")) return DEFAULT_MAX_MEMPOOL_SIZE_MB;
    return {};
}

common::SettingsValue DisplaySettingValue(interfaces::Node& node, ArgsManager& args, const QString& name)
{
    const common::SettingsValue effective_value = args.GetSetting(std::string{"-"} + name.toStdString());
    if (!effective_value.isNull()) return effective_value;
    return node.getPersistentSetting(name.toStdString());
}

common::SettingsValue ConfigCoreSettingValue(ArgsManager& args, const QString& name)
{
    common::SettingsValue value;
    const std::string key = name.toStdString();
    const std::string chain = args.GetChainTypeString();
    args.LockSettings([&](common::Settings& settings) {
        common::Settings config_only;
        config_only.ro_config = settings.ro_config;
        value = common::GetSetting(config_only, chain, key,
            /*ignore_default_section_config=*/false,
            /*ignore_nonpersistent=*/false,
            /*get_chain_type=*/false);
    });
    return value;
}

bool CoreSettingValuesEqual(const QString& name, const common::SettingsValue& left, const common::SettingsValue& right)
{
    if (left.isNull() && right.isNull()) return true;
    if (IsBoolCoreSetting(name)) {
        const std::optional<bool> left_bool = SettingToBool(left);
        const std::optional<bool> right_bool = SettingToBool(right);
        return left_bool && right_bool && *left_bool == *right_bool;
    }
    if (IsIntCoreSetting(name)) {
        const std::optional<int64_t> left_int = SettingTo<int64_t>(left);
        const std::optional<int64_t> right_int = SettingTo<int64_t>(right);
        return left_int && right_int && *left_int == *right_int;
    }
    if (IsOptionalStringCoreSetting(name)) {
        const std::optional<std::string> left_string = SettingToString(left);
        const std::optional<std::string> right_string = SettingToString(right);
        const bool left_empty = !left_string || left_string->empty() || *left_string == "0";
        const bool right_empty = !right_string || right_string->empty() || *right_string == "0";
        if (left_empty && right_empty) return true;
        return left_string && right_string && *left_string == *right_string;
    }
    return left.write() == right.write();
}

QVariantMap CoreSettingStatus(ArgsManager& args, const QString& name)
{
    const std::string key = name.toStdString();
    const bool command_line_overridden{IsCommandLineOverridden(args, name)};
    bool has_rw_setting{false};
    bool has_config_setting{false};
    common::SettingsValue config_value;
    const std::string chain = args.GetChainTypeString();

    args.LockSettings([&](common::Settings& settings) {
        if (const common::SettingsValue* value = common::FindKey(settings.rw_settings, key)) {
            has_rw_setting = !value->isNull();
        }
        common::Settings config_only;
        config_only.ro_config = settings.ro_config;
        config_value = common::GetSetting(config_only, chain, key,
            /*ignore_default_section_config=*/false,
            /*ignore_nonpersistent=*/false,
            /*get_chain_type=*/false);
        has_config_setting = !config_value.isNull();
    });

    const bool startup_adjusted{!command_line_overridden && !ForcedParameterInteractionValue(args, name).isNull()};
    QString source{QStringLiteral("default")};
    if (command_line_overridden) {
        source = QStringLiteral("command_line");
    } else if (startup_adjusted) {
        source = QStringLiteral("startup");
    } else if (has_rw_setting) {
        source = QStringLiteral("settings_json");
    } else if (has_config_setting) {
        source = QStringLiteral("bitcoin_conf");
    }

    const bool can_edit = !command_line_overridden;
    const bool creates_gui_override = can_edit && !has_rw_setting && (has_config_setting || startup_adjusted);
    QString info_text;
    if (command_line_overridden) {
        info_text = QCoreApplication::translate("OptionsQmlModel", "Set by command line (-%1). Remove the command-line option to change this here.").arg(name);
    } else if (startup_adjusted) {
        info_text = has_rw_setting
            ? QCoreApplication::translate("OptionsQmlModel", "Adjusted by related startup options. The saved GUI override will apply after restart.")
            : QCoreApplication::translate("OptionsQmlModel", "Adjusted by related startup options. Changing this saves a GUI override in settings.json.");
    } else if (creates_gui_override) {
        info_text = QCoreApplication::translate("OptionsQmlModel", "Loaded from bitcoin.conf. Changing this saves a GUI override in settings.json.");
    }

    QVariantMap status;
    status.insert(QStringLiteral("source"), source);
    status.insert(QStringLiteral("canEdit"), can_edit);
    status.insert(QStringLiteral("commandLineOverridden"), command_line_overridden);
    status.insert(QStringLiteral("hasRwSetting"), has_rw_setting);
    status.insert(QStringLiteral("hasConfigSetting"), has_config_setting);
    status.insert(QStringLiteral("createsGuiOverride"), creates_gui_override);
    status.insert(QStringLiteral("startupAdjusted"), startup_adjusted);
    status.insert(QStringLiteral("infoText"), info_text);
    return status;
}

QVariantMap BuildCoreSettingStatuses(ArgsManager& args, const QStringList& names)
{
    QVariantMap statuses;
    for (const QString& name : names) {
        statuses.insert(name, CoreSettingStatus(args, name));
    }
    return statuses;
}

bool CanEditCoreSetting(ArgsManager& args, const QString& name)
{
    return CoreSettingStatus(args, name).value(QStringLiteral("canEdit")).toBool();
}

common::SettingsValue GuiOverrideValue(ArgsManager& args, const QString& name, const common::SettingsValue& value)
{
    const common::SettingsValue forced_value = ForcedParameterInteractionValue(args, name);
    if (!forced_value.isNull()) {
        return CoreSettingValuesEqual(name, value, forced_value) ? common::SettingsValue{} : value;
    }

    const common::SettingsValue config_value = ConfigCoreSettingValue(args, name);
    const common::SettingsValue default_value = DefaultCoreSettingValue(name);

    if (!config_value.isNull()) {
        if (CoreSettingValuesEqual(name, value, config_value)) {
            return {};
        }
    } else if (CoreSettingValuesEqual(name, value, default_value)) {
        return {};
    }

    return value;
}

common::SettingsValue LegacyCompatibleRwSettingValue(const QString& name, const common::SettingsValue& value)
{
    // Match Qt's settings.json downgrade workaround for numeric settings
    // older GUI releases read through string-only argument paths.
    if (value.isNum() && IsLegacyNumericRwSetting(name)) {
        return common::SettingsValue{value.getValStr()};
    }
    return value;
}

void SetRwSetting(ArgsManager& args, const QString& name, const common::SettingsValue& value)
{
    const std::string key = name.toStdString();
    const common::SettingsValue stored_value = LegacyCompatibleRwSettingValue(name, value);
    args.LockSettings([&](common::Settings& settings) {
        if (stored_value.isNull()) {
            settings.rw_settings.erase(key);
        } else {
            settings.rw_settings[key] = stored_value;
        }
    });
}

void UpdateRwSetting(interfaces::Node& node, const QString& name, const common::SettingsValue& value)
{
    node.updateRwSetting(name.toStdString(), LegacyCompatibleRwSettingValue(name, value));
}

bool WriteCoreSettingOverride(ArgsManager& args, const QString& name, const common::SettingsValue& value)
{
    if (!CanEditCoreSetting(args, name)) return false;
    SetRwSetting(args, name, GuiOverrideValue(args, name, value));
    return true;
}

bool WritePruneSetting(ArgsManager& args, bool enabled, int prune_size_gb)
{
    const QString key{QStringLiteral("prune")};
    if (!CanEditCoreSetting(args, key)) return false;

    if (enabled) {
        if (!WriteCoreSettingOverride(args, key, PruneSetting(true, prune_size_gb))) return false;
        SetRwSetting(args, QStringLiteral("prune-prev"), {});
    } else {
        SetRwSetting(args, QStringLiteral("prune-prev"), PruneSetting(true, prune_size_gb));
        if (!WriteCoreSettingOverride(args, key, PruneSetting(false, prune_size_gb))) return false;
    }
    return true;
}

bool WriteProxySetting(ArgsManager& args, const QString& key, bool enabled, const QString& address)
{
    if (!CanEditCoreSetting(args, key)) return false;

    const QString trimmed = address.trimmed();
    if (enabled && !ProxyValidationError(trimmed).isEmpty()) return false;

    const QString prev_key = key + QStringLiteral("-prev");
    if (enabled) {
        if (!WriteCoreSettingOverride(args, key, ProxySetting(true, trimmed))) return false;
        SetRwSetting(args, prev_key, {});
    } else {
        if (!trimmed.isEmpty()) {
            SetRwSetting(args, prev_key, common::SettingsValue{trimmed.toStdString()});
        }
        if (!WriteCoreSettingOverride(args, key, ProxySetting(false, trimmed))) return false;
    }
    return true;
}

Session::Session()
{
    m_values.proxy_address = DefaultProxyAddress();
    m_values.tor_address = DefaultProxyAddress();
}

Session::Session(Values values, QVariantMap statuses)
    : m_values{std::move(values)}
    , m_statuses{std::move(statuses)}
{
}

bool Session::canEdit(const QString& name) const
{
    return SettingStatusCanEdit(m_statuses, name);
}

common::SettingsValue Session::settingValue(const QString& name) const
{
    if (name == QStringLiteral("listen")) return m_values.listen;
    if (name == QStringLiteral("natpmp")) return m_values.natpmp;
    if (name == QStringLiteral("server")) return m_values.server;
    if (name == QStringLiteral("prune")) return PruneSetting(m_values.prune, m_values.prune_size_gb);
    if (name == QStringLiteral("proxy")) return ProxySetting(m_values.proxy_enabled, m_values.proxy_address);
    if (name == QStringLiteral("onion")) return ProxySetting(m_values.tor_enabled, m_values.tor_address);
    return {};
}

QString Session::defaultProxyAddress() const
{
    return DefaultProxyAddress();
}

QString Session::validateProxyLocation(const QString& location) const
{
    return ProxyValidationError(location);
}

Change Session::beginChange(const QString& setting_name) const
{
    Change change;
    change.setting_name = setting_name;
    change.before = m_values;
    change.after = m_values;
    change.before_statuses = m_statuses;
    change.after_statuses = m_statuses;
    return change;
}

Change Session::finishChange(Change change, bool accepted) const
{
    change.accepted = accepted;
    change.after = m_values;
    change.after_statuses = m_statuses;
    return change;
}

Change Session::changeListen(bool listen)
{
    Change change = beginChange(QStringLiteral("listen"));
    if (!canEdit(QStringLiteral("listen")) || listen == m_values.listen) return finishChange(std::move(change), false);
    markTouched(QStringLiteral("listen"));
    m_values.listen = listen;
    return finishChange(std::move(change), true);
}

Change Session::changeNatpmp(bool natpmp)
{
    Change change = beginChange(QStringLiteral("natpmp"));
    if (!canEdit(QStringLiteral("natpmp")) || natpmp == m_values.natpmp) return finishChange(std::move(change), false);
    markTouched(QStringLiteral("natpmp"));
    m_values.natpmp = natpmp;
    return finishChange(std::move(change), true);
}

Change Session::changeServer(bool server)
{
    Change change = beginChange(QStringLiteral("server"));
    if (!canEdit(QStringLiteral("server")) || server == m_values.server) return finishChange(std::move(change), false);
    markTouched(QStringLiteral("server"));
    m_values.server = server;
    return finishChange(std::move(change), true);
}

Change Session::changePrune(bool prune)
{
    Change change = beginChange(QStringLiteral("prune"));
    if (!canEdit(QStringLiteral("prune")) || prune == m_values.prune) return finishChange(std::move(change), false);
    markTouched(QStringLiteral("prune"));
    m_values.prune = prune;
    return finishChange(std::move(change), true);
}

Change Session::changePruneSizeGB(int prune_size_gb)
{
    Change change = beginChange(QStringLiteral("prune"));
    if (!canEdit(QStringLiteral("prune")) || prune_size_gb < 1 || prune_size_gb == m_values.prune_size_gb) return finishChange(std::move(change), false);
    markTouched(QStringLiteral("prune"));
    m_values.prune_size_gb = prune_size_gb;
    return finishChange(std::move(change), true);
}

Change Session::changeProxyEnabled(bool enabled)
{
    Change change = beginChange(QStringLiteral("proxy"));
    if (!canEdit(QStringLiteral("proxy")) || enabled == m_values.proxy_enabled) return finishChange(std::move(change), false);
    if (enabled && m_values.proxy_address.isEmpty()) {
        m_values.proxy_address = DefaultProxyAddress();
    }
    if (enabled && !ProxyValidationError(m_values.proxy_address).isEmpty()) return finishChange(std::move(change), false);
    markTouched(QStringLiteral("proxy"));
    m_values.proxy_enabled = enabled;
    return finishChange(std::move(change), true);
}

Change Session::changeTorEnabled(bool enabled)
{
    Change change = beginChange(QStringLiteral("onion"));
    if (!canEdit(QStringLiteral("onion")) || enabled == m_values.tor_enabled) return finishChange(std::move(change), false);
    if (enabled && m_values.tor_address.isEmpty()) {
        m_values.tor_address = DefaultProxyAddress();
    }
    if (enabled && !ProxyValidationError(m_values.tor_address).isEmpty()) return finishChange(std::move(change), false);
    markTouched(QStringLiteral("onion"));
    m_values.tor_enabled = enabled;
    return finishChange(std::move(change), true);
}

Change Session::changeProxyLocation(const QString& location)
{
    Change change = beginChange(QStringLiteral("proxy"));
    if (!canEdit(QStringLiteral("proxy"))) return finishChange(std::move(change), false);
    const QString trimmed = location.trimmed();
    if (!ProxyValidationError(trimmed).isEmpty()) return finishChange(std::move(change), false);
    if (trimmed == m_values.proxy_address) return finishChange(std::move(change), true);
    markTouched(QStringLiteral("proxy"));
    m_values.proxy_address = trimmed;
    return finishChange(std::move(change), true);
}

Change Session::changeTorLocation(const QString& location)
{
    Change change = beginChange(QStringLiteral("onion"));
    if (!canEdit(QStringLiteral("onion"))) return finishChange(std::move(change), false);
    const QString trimmed = location.trimmed();
    if (!ProxyValidationError(trimmed).isEmpty()) return finishChange(std::move(change), false);
    if (trimmed == m_values.tor_address) return finishChange(std::move(change), true);
    markTouched(QStringLiteral("onion"));
    m_values.tor_address = trimmed;
    return finishChange(std::move(change), true);
}

Change Session::changePruneRecommendation(bool prune, int prune_size_gb, bool mark_touched)
{
    Change change = beginChange(QStringLiteral("prune"));
    m_values.prune = prune;
    if (prune) {
        m_values.prune_size_gb = prune_size_gb;
    }
    if (mark_touched) {
        m_touched_settings.insert(QStringLiteral("prune"));
    } else {
        m_touched_settings.remove(QStringLiteral("prune"));
    }
    return finishChange(std::move(change), true);
}

Change Session::applyPreviewValuesPreservingTouched(const Values& values, const QVariantMap& statuses)
{
    Change change = beginChange({});
    m_statuses = statuses;
    if (!m_touched_settings.contains(QStringLiteral("listen"))) {
        m_values.listen = values.listen;
    }
    if (!m_touched_settings.contains(QStringLiteral("natpmp"))) {
        m_values.natpmp = values.natpmp;
    }
    if (!m_touched_settings.contains(QStringLiteral("server"))) {
        m_values.server = values.server;
    }
    if (!m_touched_settings.contains(QStringLiteral("prune"))) {
        m_values.prune = values.prune;
        m_values.prune_size_gb = values.prune_size_gb;
    }
    if (!m_touched_settings.contains(QStringLiteral("proxy"))) {
        m_values.proxy_enabled = values.proxy_enabled;
        m_values.proxy_address = values.proxy_address;
    }
    if (!m_touched_settings.contains(QStringLiteral("onion"))) {
        m_values.tor_enabled = values.tor_enabled;
        m_values.tor_address = values.tor_address;
    }
    return finishChange(std::move(change), true);
}

bool Session::setListen(bool listen)
{
    return ValuesChanged(changeListen(listen));
}

bool Session::setNatpmp(bool natpmp)
{
    return ValuesChanged(changeNatpmp(natpmp));
}

bool Session::setServer(bool server)
{
    return ValuesChanged(changeServer(server));
}

bool Session::setPrune(bool prune)
{
    return ValuesChanged(changePrune(prune));
}

bool Session::setPruneSizeGB(int prune_size_gb)
{
    return ValuesChanged(changePruneSizeGB(prune_size_gb));
}

bool Session::setProxyEnabled(bool enabled)
{
    return ValuesChanged(changeProxyEnabled(enabled));
}

bool Session::setTorEnabled(bool enabled)
{
    return ValuesChanged(changeTorEnabled(enabled));
}

bool Session::commitProxyLocation(const QString& location)
{
    return changeProxyLocation(location).accepted;
}

bool Session::commitTorLocation(const QString& location)
{
    return changeTorLocation(location).accepted;
}

void Session::setPruneRecommendation(bool prune, int prune_size_gb, bool mark_touched)
{
    changePruneRecommendation(prune, prune_size_gb, mark_touched);
}

bool Session::writeToArgs(ArgsManager& args, const QString& name) const
{
    if (!IsSessionSetting(name)) return false;
    if (name == QStringLiteral("proxy")) {
        return WriteProxySetting(args, QStringLiteral("proxy"), m_values.proxy_enabled, m_values.proxy_address);
    }
    if (name == QStringLiteral("onion")) {
        return WriteProxySetting(args, QStringLiteral("onion"), m_values.tor_enabled, m_values.tor_address);
    }
    if (name == QStringLiteral("prune")) {
        return WritePruneSetting(args, m_values.prune, m_values.prune_size_gb);
    }
    return WriteCoreSettingOverride(args, name, settingValue(name));
}

bool Session::writeTouchedToArgs(ArgsManager& args) const
{
    bool ok{true};
    for (const QString& name : SessionSettingNames()) {
        if (m_touched_settings.contains(name)) {
            ok = writeToArgs(args, name) && ok;
        }
    }
    return ok;
}

bool Session::writeToNode(interfaces::Node& node, ArgsManager& args, const QString& name) const
{
    if (!IsSessionSetting(name) || !CanEditCoreSetting(args, name)) return false;

    if (name == QStringLiteral("proxy") || name == QStringLiteral("onion")) {
        const bool enabled = name == QStringLiteral("proxy") ? m_values.proxy_enabled : m_values.tor_enabled;
        const QString address = name == QStringLiteral("proxy") ? m_values.proxy_address : m_values.tor_address;
        const QString trimmed = address.trimmed();
        if (enabled && !ProxyValidationError(trimmed).isEmpty()) return false;

        const QString prev_key{name + QStringLiteral("-prev")};
        if (enabled) {
            UpdateRwSetting(node, name, GuiOverrideValue(args, name, ProxySetting(true, trimmed)));
            UpdateRwSetting(node, prev_key, common::SettingsValue{});
        } else {
            if (!trimmed.isEmpty()) {
                UpdateRwSetting(node, prev_key, common::SettingsValue{trimmed.toStdString()});
            }
            UpdateRwSetting(node, name, GuiOverrideValue(args, name, ProxySetting(false, trimmed)));
        }
        return true;
    }

    if (name == QStringLiteral("prune")) {
        if (m_values.prune) {
            UpdateRwSetting(node, name, GuiOverrideValue(args, name, PruneSetting(true, m_values.prune_size_gb)));
            UpdateRwSetting(node, QStringLiteral("prune-prev"), common::SettingsValue{});
        } else {
            UpdateRwSetting(node, QStringLiteral("prune-prev"), PruneSetting(true, m_values.prune_size_gb));
            UpdateRwSetting(node, name, GuiOverrideValue(args, name, PruneSetting(false, m_values.prune_size_gb)));
        }
        return true;
    }

    UpdateRwSetting(node, name, GuiOverrideValue(args, name, settingValue(name)));
    return true;
}

bool Session::writeTouchedToNode(interfaces::Node& node, ArgsManager& args) const
{
    bool ok{true};
    for (const QString& name : SessionSettingNames()) {
        if (m_touched_settings.contains(name)) {
            ok = writeToNode(node, args, name) && ok;
        }
    }
    return ok;
}

} // namespace QmlCoreSettings
