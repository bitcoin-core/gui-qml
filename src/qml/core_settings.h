// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_CORE_SETTINGS_H
#define BITCOIN_QML_CORE_SETTINGS_H

#include <common/settings.h>

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cstdint>

class ArgsManager;
namespace interfaces {
class Node;
} // namespace interfaces

namespace QmlCoreSettings {

struct Values {
    bool prune{true};
    int prune_size_gb{2};
    bool listen{true};
    bool natpmp{false};
    bool server{false};
    bool proxy_enabled{false};
    QString proxy_address{};
    bool tor_enabled{false};
    QString tor_address{};
};

struct Change {
    bool accepted{false};
    QString setting_name;
    Values before;
    Values after;
    QVariantMap before_statuses;
    QVariantMap after_statuses;
};

bool ValuesEqual(const Values& left, const Values& right);
bool ValuesChanged(const Change& change);
bool StatusesChanged(const Change& change);
bool ListenChanged(const Change& change);
bool NatpmpChanged(const Change& change);
bool ServerChanged(const Change& change);
bool PruneChanged(const Change& change);
bool PruneSizeGBChanged(const Change& change);
bool ProxyEnabledChanged(const Change& change);
bool ProxyAddressChanged(const Change& change);
bool TorEnabledChanged(const Change& change);
bool TorAddressChanged(const Change& change);

template <typename Model>
void EmitCoreSettingSignals(Model& model, const Change& change)
{
    if (!change.accepted) return;
    if (StatusesChanged(change)) model.coreSettingStatusesChanged();
    if (PruneChanged(change)) model.pruneChanged(change.after.prune);
    if (PruneSizeGBChanged(change)) model.pruneSizeGBChanged(change.after.prune_size_gb);
    if (ListenChanged(change)) model.listenChanged(change.after.listen);
    if (NatpmpChanged(change)) model.natpmpChanged(change.after.natpmp);
    if (ServerChanged(change)) model.serverChanged(change.after.server);
    if (ProxyAddressChanged(change)) model.proxyAddressChanged(change.after.proxy_address);
    if (ProxyEnabledChanged(change)) model.proxyEnabledChanged(change.after.proxy_enabled);
    if (TorAddressChanged(change)) model.torAddressChanged(change.after.tor_address);
    if (TorEnabledChanged(change)) model.torEnabledChanged(change.after.tor_enabled);
}

const QStringList& CoreSettingNames();
const QStringList& OnboardingCoreSettingNames();

int PruneMiBToGB(int64_t mib);
int64_t PruneGBToMiB(int gb);
common::SettingsValue PruneSetting(bool enabled, int prune_size_gb);

QString DefaultProxyAddress();
QString ProxyValidationError(const QString& location);
common::SettingsValue ProxySetting(bool enabled, const QString& address);

Values LoadPersistentValues(interfaces::Node& node);
Values LoadEffectiveValues(ArgsManager& args);
Values LoadDisplayValues(interfaces::Node& node, ArgsManager& args);

common::SettingsValue DefaultCoreSettingValue(const QString& name);
common::SettingsValue DisplaySettingValue(interfaces::Node& node, ArgsManager& args, const QString& name);
common::SettingsValue ConfigCoreSettingValue(ArgsManager& args, const QString& name);
bool CoreSettingValuesEqual(const QString& name, const common::SettingsValue& left, const common::SettingsValue& right);

QVariantMap CoreSettingStatus(ArgsManager& args, const QString& name);
QVariantMap BuildCoreSettingStatuses(ArgsManager& args, const QStringList& names);
bool IsCommandLineOverridden(ArgsManager& args, const QString& name);
bool CanEditCoreSetting(ArgsManager& args, const QString& name);

common::SettingsValue GuiOverrideValue(ArgsManager& args, const QString& name, const common::SettingsValue& value);
void SetRwSetting(ArgsManager& args, const QString& name, const common::SettingsValue& value);
void UpdateRwSetting(interfaces::Node& node, const QString& name, const common::SettingsValue& value);
bool WriteCoreSettingOverride(ArgsManager& args, const QString& name, const common::SettingsValue& value);
bool WriteProxySetting(ArgsManager& args, const QString& key, bool enabled, const QString& address);

class Session
{
public:
    Session();
    explicit Session(Values values, QVariantMap statuses = {});

    const Values& values() const { return m_values; }
    const QSet<QString>& touchedSettings() const { return m_touched_settings; }
    QVariantMap statuses() const { return m_statuses; }

    bool canEdit(const QString& name) const;
    bool isTouched(const QString& name) const { return m_touched_settings.contains(name); }
    void markTouched(const QString& name) { m_touched_settings.insert(name); }
    void clearTouched(const QString& name) { m_touched_settings.remove(name); }
    void clearTouchedSettings() { m_touched_settings.clear(); }
    void setTouchedSettings(const QSet<QString>& touched_settings) { m_touched_settings = touched_settings; }
    void setStatuses(const QVariantMap& statuses) { m_statuses = statuses; }

    common::SettingsValue settingValue(const QString& name) const;
    QString defaultProxyAddress() const;
    QString validateProxyLocation(const QString& location) const;

    Change changeListen(bool listen);
    Change changeNatpmp(bool natpmp);
    Change changeServer(bool server);
    Change changePrune(bool prune);
    Change changePruneSizeGB(int prune_size_gb);
    Change changeProxyEnabled(bool enabled);
    Change changeTorEnabled(bool enabled);
    Change changeProxyLocation(const QString& location);
    Change changeTorLocation(const QString& location);
    Change changePruneRecommendation(bool prune, int prune_size_gb, bool mark_touched);
    Change applyPreviewValuesPreservingTouched(const Values& values, const QVariantMap& statuses);

    bool setListen(bool listen);
    bool setNatpmp(bool natpmp);
    bool setServer(bool server);
    bool setPrune(bool prune);
    bool setPruneSizeGB(int prune_size_gb);
    bool setProxyEnabled(bool enabled);
    bool setTorEnabled(bool enabled);
    bool commitProxyLocation(const QString& location);
    bool commitTorLocation(const QString& location);
    void setPruneRecommendation(bool prune, int prune_size_gb, bool mark_touched);

    bool writeToArgs(ArgsManager& args, const QString& name) const;
    bool writeTouchedToArgs(ArgsManager& args) const;
    bool writeToNode(interfaces::Node& node, ArgsManager& args, const QString& name) const;
    bool writeTouchedToNode(interfaces::Node& node, ArgsManager& args) const;

private:
    Change beginChange(const QString& setting_name) const;
    Change finishChange(Change change, bool accepted) const;

    Values m_values;
    QSet<QString> m_touched_settings;
    QVariantMap m_statuses;
};

} // namespace QmlCoreSettings

#endif // BITCOIN_QML_CORE_SETTINGS_H
