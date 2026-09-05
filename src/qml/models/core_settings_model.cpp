// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/core_settings_model.h>

#include <univalue.h>

#include <utility>

namespace {
bool IsProxyEntry(const QString& key)
{
    return key == QStringLiteral("proxy") || key == QStringLiteral("onion");
}
} // namespace

CoreSettingEntryModel::CoreSettingEntryModel(QString key, CoreSettingsModel& model, QObject* parent)
    : QObject{parent}
    , m_key{std::move(key)}
    , m_model{model}
{
}

QVariant CoreSettingEntryModel::value() const
{
    const QmlCoreSettings::Values& values = m_model.values();
    if (m_key == QStringLiteral("listen")) return values.listen;
    if (m_key == QStringLiteral("natpmp")) return values.natpmp;
    if (m_key == QStringLiteral("server")) return values.server;
    if (m_key == QStringLiteral("prune")) return values.prune_size_gb;
    if (m_key == QStringLiteral("proxy")) return values.proxy_address;
    if (m_key == QStringLiteral("onion")) return values.tor_address;
    return {};
}

void CoreSettingEntryModel::setValue(const QVariant& value)
{
    if (m_key == QStringLiteral("listen")) {
        m_model.changeListen(value.toBool());
    } else if (m_key == QStringLiteral("natpmp")) {
        m_model.changeNatpmp(value.toBool());
    } else if (m_key == QStringLiteral("server")) {
        m_model.changeServer(value.toBool());
    } else if (m_key == QStringLiteral("prune")) {
        m_model.changePruneSizeGB(value.toInt());
    } else if (m_key == QStringLiteral("proxy")) {
        m_model.changeProxyLocation(value.toString());
    } else if (m_key == QStringLiteral("onion")) {
        m_model.changeTorLocation(value.toString());
    }
}

bool CoreSettingEntryModel::enabled() const
{
    const QmlCoreSettings::Values& values = m_model.values();
    if (m_key == QStringLiteral("listen")) return values.listen;
    if (m_key == QStringLiteral("natpmp")) return values.natpmp;
    if (m_key == QStringLiteral("server")) return values.server;
    if (m_key == QStringLiteral("prune")) return values.prune;
    if (m_key == QStringLiteral("proxy")) return values.proxy_enabled;
    if (m_key == QStringLiteral("onion")) return values.tor_enabled;
    return false;
}

void CoreSettingEntryModel::setEnabled(bool enabled)
{
    if (m_key == QStringLiteral("listen")) {
        m_model.changeListen(enabled);
    } else if (m_key == QStringLiteral("natpmp")) {
        m_model.changeNatpmp(enabled);
    } else if (m_key == QStringLiteral("server")) {
        m_model.changeServer(enabled);
    } else if (m_key == QStringLiteral("prune")) {
        m_model.changePrune(enabled);
    } else if (m_key == QStringLiteral("proxy")) {
        m_model.changeProxyEnabled(enabled);
    } else if (m_key == QStringLiteral("onion")) {
        m_model.changeTorEnabled(enabled);
    }
}

QString CoreSettingEntryModel::address() const
{
    const QmlCoreSettings::Values& values = m_model.values();
    if (m_key == QStringLiteral("proxy")) return values.proxy_address;
    if (m_key == QStringLiteral("onion")) return values.tor_address;
    return {};
}

void CoreSettingEntryModel::setAddress(const QString& address)
{
    commitAddress(address);
}

QVariantMap CoreSettingEntryModel::status() const
{
    return m_model.status(m_key);
}

bool CoreSettingEntryModel::canEdit() const
{
    return status().value(QStringLiteral("canEdit"), true).toBool();
}

QString CoreSettingEntryModel::infoText() const
{
    return status().value(QStringLiteral("infoText")).toString();
}

bool CoreSettingEntryModel::createsGuiOverride() const
{
    return status().value(QStringLiteral("createsGuiOverride")).toBool();
}

QString CoreSettingEntryModel::validate(const QString& value) const
{
    if (IsProxyEntry(m_key)) return m_model.validateProxyLocation(value);
    return {};
}

bool CoreSettingEntryModel::commitAddress(const QString& address)
{
    if (m_key == QStringLiteral("proxy")) return m_model.commitProxyLocation(address);
    if (m_key == QStringLiteral("onion")) return m_model.commitTorLocation(address);
    return false;
}

QString CoreSettingEntryModel::defaultAddress() const
{
    return m_model.defaultProxyAddress();
}

CoreSettingsModel::CoreSettingsModel(QObject* parent)
    : QObject{parent}
{
}

CoreSettingsModel::CoreSettingsModel(QmlCoreSettings::Values values, QVariantMap statuses, QObject* parent)
    : QObject{parent}
    , m_session{std::move(values), std::move(statuses)}
{
}

QObject* CoreSettingsModel::entry(const QString& key)
{
    return ensureEntry(key);
}

QVariantMap CoreSettingsModel::status(const QString& key) const
{
    return m_session.statuses().value(key).toMap();
}

common::SettingsValue CoreSettingsModel::settingValue(const QString& name) const
{
    return m_session.settingValue(name);
}

void CoreSettingsModel::setStatuses(const QVariantMap& statuses)
{
    if (m_session.statuses() == statuses) return;
    m_session.setStatuses(statuses);
    notifyStatusesChanged();
}

QmlCoreSettings::Change CoreSettingsModel::changeListen(bool listen)
{
    return mutate(ChangeOrigin::User, [listen](QmlCoreSettings::Session& session) {
        return session.changeListen(listen);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changeNatpmp(bool natpmp)
{
    return mutate(ChangeOrigin::User, [natpmp](QmlCoreSettings::Session& session) {
        return session.changeNatpmp(natpmp);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changeServer(bool server)
{
    return mutate(ChangeOrigin::User, [server](QmlCoreSettings::Session& session) {
        return session.changeServer(server);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changePrune(bool prune)
{
    return mutate(ChangeOrigin::User, [prune](QmlCoreSettings::Session& session) {
        return session.changePrune(prune);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changePruneSizeGB(int prune_size_gb)
{
    return mutate(ChangeOrigin::User, [prune_size_gb](QmlCoreSettings::Session& session) {
        return session.changePruneSizeGB(prune_size_gb);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changeProxyEnabled(bool enabled)
{
    return mutate(ChangeOrigin::User, [enabled](QmlCoreSettings::Session& session) {
        return session.changeProxyEnabled(enabled);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changeTorEnabled(bool enabled)
{
    return mutate(ChangeOrigin::User, [enabled](QmlCoreSettings::Session& session) {
        return session.changeTorEnabled(enabled);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changeProxyLocation(const QString& location)
{
    return mutate(ChangeOrigin::User, [&location](QmlCoreSettings::Session& session) {
        return session.changeProxyLocation(location);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changeTorLocation(const QString& location)
{
    return mutate(ChangeOrigin::User, [&location](QmlCoreSettings::Session& session) {
        return session.changeTorLocation(location);
    });
}

QmlCoreSettings::Change CoreSettingsModel::changePruneRecommendation(bool prune, int prune_size_gb, bool mark_touched)
{
    return mutate(ChangeOrigin::Recommendation, [prune, prune_size_gb, mark_touched](QmlCoreSettings::Session& session) {
        return session.changePruneRecommendation(prune, prune_size_gb, mark_touched);
    });
}

QmlCoreSettings::Change CoreSettingsModel::applyPreviewValuesPreservingTouched(const QmlCoreSettings::Values& values, const QVariantMap& statuses)
{
    return mutate(ChangeOrigin::Preview, [&values, &statuses](QmlCoreSettings::Session& session) {
        return session.applyPreviewValuesPreservingTouched(values, statuses);
    });
}

CoreSettingEntryModel* CoreSettingsModel::ensureEntry(const QString& key)
{
    if (CoreSettingEntryModel* existing = m_entries.value(key)) return existing;
    auto* entry = new CoreSettingEntryModel{key, *this, this};
    m_entries.insert(key, entry);
    return entry;
}

void CoreSettingsModel::notifyStatusesChanged()
{
    Q_EMIT statusesChanged();
    for (CoreSettingEntryModel* entry : std::as_const(m_entries)) {
        Q_EMIT entry->statusChanged();
    }
}

void CoreSettingsModel::notifyChange(const QmlCoreSettings::Change& change)
{
    if (!change.accepted) return;

    if (QmlCoreSettings::StatusesChanged(change)) {
        notifyStatusesChanged();
    }

    auto notify_bool_entry = [this](const QString& key) {
        CoreSettingEntryModel* entry = ensureEntry(key);
        Q_EMIT entry->valueChanged();
        Q_EMIT entry->enabledChanged();
    };

    if (QmlCoreSettings::ListenChanged(change)) notify_bool_entry(QStringLiteral("listen"));
    if (QmlCoreSettings::NatpmpChanged(change)) notify_bool_entry(QStringLiteral("natpmp"));
    if (QmlCoreSettings::ServerChanged(change)) notify_bool_entry(QStringLiteral("server"));

    if (QmlCoreSettings::PruneChanged(change)) {
        Q_EMIT ensureEntry(QStringLiteral("prune"))->enabledChanged();
    }
    if (QmlCoreSettings::PruneSizeGBChanged(change)) {
        Q_EMIT ensureEntry(QStringLiteral("prune"))->valueChanged();
    }

    if (QmlCoreSettings::ProxyEnabledChanged(change)) {
        Q_EMIT ensureEntry(QStringLiteral("proxy"))->enabledChanged();
    }
    if (QmlCoreSettings::ProxyAddressChanged(change)) {
        CoreSettingEntryModel* entry = ensureEntry(QStringLiteral("proxy"));
        Q_EMIT entry->addressChanged();
        Q_EMIT entry->valueChanged();
    }
    if (QmlCoreSettings::TorEnabledChanged(change)) {
        Q_EMIT ensureEntry(QStringLiteral("onion"))->enabledChanged();
    }
    if (QmlCoreSettings::TorAddressChanged(change)) {
        CoreSettingEntryModel* entry = ensureEntry(QStringLiteral("onion"));
        Q_EMIT entry->addressChanged();
        Q_EMIT entry->valueChanged();
    }
}
