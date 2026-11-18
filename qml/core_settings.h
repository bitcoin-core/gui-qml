// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_CORE_SETTINGS_H
#define BITCOIN_QML_CORE_SETTINGS_H
#include <QPointer>

#include <common/settings.h>

#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

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
    // Exact configured target, retained while disabled. -1 is the legacy
    // constructor's GB-derived value; 1 represents manual pruning.
    int64_t prune_target_mib{-1};
};

/** Worker/test-only metadata envelope. The producer measures structural values
 * once on Native; GUI admission reads only the cached byte count. */
struct StatusProjection {QVariantMap values;std::shared_ptr<const char> identity{};};
StatusProjection CaptureStatuses(QVariantMap values);

struct Change {
    bool accepted{false};
    QString setting_name;
    Values before;
    Values after;
    QVariantMap before_statuses;
    QVariantMap after_statuses;
    bool force_write{false};
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
    const QPointer<Model> observer{&model};
    const auto open=[&] {
        if(!observer)return false;
        if constexpr(requires {observer->presentationOpen();})return observer->presentationOpen();
        return true;
    };
    if(!open())return;
    if (StatusesChanged(change)) model.coreSettingStatusesChanged();
    if(!open())return;
    if (PruneChanged(change)) model.pruneChanged(change.after.prune);
    if(!open())return;
    if (PruneSizeGBChanged(change)) model.pruneSizeGBChanged(change.after.prune_size_gb);
    if(!open())return;
    if (ListenChanged(change)) model.listenChanged(change.after.listen);
    if(!open())return;
    if (NatpmpChanged(change)) model.natpmpChanged(change.after.natpmp);
    if(!open())return;
    if (ServerChanged(change)) model.serverChanged(change.after.server);
    if(!open())return;
    if (ProxyAddressChanged(change)) model.proxyAddressChanged(change.after.proxy_address);
    if(!open())return;
    if (ProxyEnabledChanged(change)) model.proxyEnabledChanged(change.after.proxy_enabled);
    if(!open())return;
    if (TorAddressChanged(change)) model.torAddressChanged(change.after.tor_address);
    if(!open())return;
    if (TorEnabledChanged(change)) model.torEnabledChanged(change.after.tor_enabled);
    if(!open())return;
}

const QStringList& CoreSettingNames();
const QStringList& OnboardingCoreSettingNames();

int PruneMiBToGB(int64_t mib);
int64_t PruneGBToMiB(int gb);
common::SettingsValue PruneSetting(bool enabled, int prune_size_gb);
common::SettingsValue PruneSetting(const Values& values);

QString DefaultProxyAddress();
QString ProxyValidationError(const QString& location);
bool ProxyRequiresNameLookup(const QString& location);
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
common::SettingsValue LegacyCompatibleRwSettingValue(const QString& name, const common::SettingsValue& value);
void SetRwSetting(ArgsManager& args, const QString& name, const common::SettingsValue& value);
void UpdateRwSetting(interfaces::Node& node, const QString& name, const common::SettingsValue& value);
bool WriteCoreSettingOverride(ArgsManager& args, const QString& name, const common::SettingsValue& value);
bool WriteProxySetting(ArgsManager& args, const QString& key, bool enabled, const QString& address);

/** Worker-only persistence. Serialize GUI writers and restore only the named
 * keys after failure, without overwriting unrelated or subsequently changed
 * settings. mutation must only change these keys and must not call this helper.
 */
bool PersistSettings(ArgsManager& args, const QStringList& keys,
                     const std::function<bool()>& mutation, QString* error = nullptr);
struct PersistenceResult {
    bool persisted{false};
    bool completed{false};
    QString error;
};
/** Keep the app writer reservation through runtime application/readback.
 * A post-save exception retains known persistence and never rolls it back.
 * skip_unchanged is for an explicit retry of an already observed saved value. */
PersistenceResult PersistTransaction(ArgsManager& args, const QStringList& keys,
    const std::function<bool()>& mutation, const std::function<void()>& after_save = {}, bool skip_unchanged = false);

enum class StorageChoice {Full,Reduced,Custom};
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
    Change changePruneTargetText(const QString& text);
    QString pruneTargetText()const;
    int64_t customPruneTarget()const;
    Change chooseStorage(StorageChoice choice);
    Change discardPruneInput();
    QString pruneTargetError()const{return m_prune_input_error;}
    void preservePruneInput(const Session& original){m_prune_input=original.m_prune_input;m_prune_input_error=original.m_prune_input_error;m_custom_prune_target=original.m_custom_prune_target;}
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

    QString m_prune_input,m_prune_input_error;
    int64_t m_custom_prune_target{-1};
    Values m_values;
    QSet<QString> m_touched_settings;
    QVariantMap m_statuses;
};

} // namespace QmlCoreSettings

#endif // BITCOIN_QML_CORE_SETTINGS_H
