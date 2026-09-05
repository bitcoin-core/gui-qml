// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_CORE_SETTINGS_MODEL_H
#define BITCOIN_QML_MODELS_CORE_SETTINGS_MODEL_H

#include <qml/core_settings.h>

#include <QObject>
#include <QHash>
#include <QString>
#include <QVariant>
#include <QVariantMap>

#include <functional>
#include <utility>

class ArgsManager;

namespace interfaces {
class Node;
} // namespace interfaces

class CoreSettingsModel;

class CoreSettingEntryModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString key READ key CONSTANT)
    Q_PROPERTY(QVariant value READ value WRITE setValue NOTIFY valueChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QString address READ address WRITE setAddress NOTIFY addressChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(bool canEdit READ canEdit NOTIFY statusChanged)
    Q_PROPERTY(QString infoText READ infoText NOTIFY statusChanged)
    Q_PROPERTY(bool createsGuiOverride READ createsGuiOverride NOTIFY statusChanged)

public:
    CoreSettingEntryModel(QString key, CoreSettingsModel& model, QObject* parent = nullptr);

    QString key() const { return m_key; }
    QVariant value() const;
    void setValue(const QVariant& value);
    bool enabled() const;
    void setEnabled(bool enabled);
    QString address() const;
    void setAddress(const QString& address);
    QVariantMap status() const;
    bool canEdit() const;
    QString infoText() const;
    bool createsGuiOverride() const;

    Q_INVOKABLE QString validate(const QString& value) const;
    Q_INVOKABLE bool commitAddress(const QString& address);
    Q_INVOKABLE QString defaultAddress() const;

Q_SIGNALS:
    void valueChanged();
    void enabledChanged();
    void addressChanged();
    void statusChanged();

private:
    QString m_key;
    CoreSettingsModel& m_model;
};

class CoreSettingsModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantMap statuses READ statuses NOTIFY statusesChanged)

public:
    enum class ChangeOrigin {
        User,
        Preview,
        Recommendation,
    };

    using BeforeChangeHandler = std::function<void(ChangeOrigin)>;
    using AfterChangeHandler = std::function<void(const QmlCoreSettings::Change&, ChangeOrigin)>;

    explicit CoreSettingsModel(QObject* parent = nullptr);
    CoreSettingsModel(QmlCoreSettings::Values values, QVariantMap statuses = {}, QObject* parent = nullptr);

    Q_INVOKABLE QObject* entry(const QString& key);
    Q_INVOKABLE QVariantMap status(const QString& key) const;

    const QmlCoreSettings::Values& values() const { return m_session.values(); }
    const QSet<QString>& touchedSettings() const { return m_session.touchedSettings(); }
    QVariantMap statuses() const { return m_session.statuses(); }

    bool canEdit(const QString& name) const { return m_session.canEdit(name); }
    bool isTouched(const QString& name) const { return m_session.isTouched(name); }
    void markTouched(const QString& name) { m_session.markTouched(name); }
    void clearTouched(const QString& name) { m_session.clearTouched(name); }
    void clearTouchedSettings() { m_session.clearTouchedSettings(); }
    void setTouchedSettings(const QSet<QString>& touched_settings) { m_session.setTouchedSettings(touched_settings); }
    void setStatuses(const QVariantMap& statuses);

    common::SettingsValue settingValue(const QString& name) const;
    QString defaultProxyAddress() const { return m_session.defaultProxyAddress(); }
    QString validateProxyLocation(const QString& location) const { return m_session.validateProxyLocation(location); }

    QmlCoreSettings::Change changeListen(bool listen);
    QmlCoreSettings::Change changeNatpmp(bool natpmp);
    QmlCoreSettings::Change changeServer(bool server);
    QmlCoreSettings::Change changePrune(bool prune);
    QmlCoreSettings::Change changePruneSizeGB(int prune_size_gb);
    QmlCoreSettings::Change changeProxyEnabled(bool enabled);
    QmlCoreSettings::Change changeTorEnabled(bool enabled);
    QmlCoreSettings::Change changeProxyLocation(const QString& location);
    QmlCoreSettings::Change changeTorLocation(const QString& location);
    QmlCoreSettings::Change changePruneRecommendation(bool prune, int prune_size_gb, bool mark_touched);
    QmlCoreSettings::Change applyPreviewValuesPreservingTouched(const QmlCoreSettings::Values& values, const QVariantMap& statuses);

    bool setListen(bool listen) { return QmlCoreSettings::ValuesChanged(changeListen(listen)); }
    bool setNatpmp(bool natpmp) { return QmlCoreSettings::ValuesChanged(changeNatpmp(natpmp)); }
    bool setServer(bool server) { return QmlCoreSettings::ValuesChanged(changeServer(server)); }
    bool setPrune(bool prune) { return QmlCoreSettings::ValuesChanged(changePrune(prune)); }
    bool setPruneSizeGB(int prune_size_gb) { return QmlCoreSettings::ValuesChanged(changePruneSizeGB(prune_size_gb)); }
    bool setProxyEnabled(bool enabled) { return QmlCoreSettings::ValuesChanged(changeProxyEnabled(enabled)); }
    bool setTorEnabled(bool enabled) { return QmlCoreSettings::ValuesChanged(changeTorEnabled(enabled)); }
    bool commitProxyLocation(const QString& location) { return changeProxyLocation(location).accepted; }
    bool commitTorLocation(const QString& location) { return changeTorLocation(location).accepted; }
    void setPruneRecommendation(bool prune, int prune_size_gb, bool mark_touched)
    {
        changePruneRecommendation(prune, prune_size_gb, mark_touched);
    }

    bool writeToArgs(ArgsManager& args, const QString& name) const { return m_session.writeToArgs(args, name); }
    bool writeTouchedToArgs(ArgsManager& args) const { return m_session.writeTouchedToArgs(args); }
    bool writeToNode(interfaces::Node& node, ArgsManager& args, const QString& name) const
    {
        return m_session.writeToNode(node, args, name);
    }
    bool writeTouchedToNode(interfaces::Node& node, ArgsManager& args) const
    {
        return m_session.writeTouchedToNode(node, args);
    }

    void setBeforeChangeHandler(BeforeChangeHandler handler) { m_before_change = std::move(handler); }
    void setAfterChangeHandler(AfterChangeHandler handler) { m_after_change = std::move(handler); }

Q_SIGNALS:
    void statusesChanged();

private:
    friend class CoreSettingEntryModel;

    CoreSettingEntryModel* ensureEntry(const QString& key);
    void notifyChange(const QmlCoreSettings::Change& change);
    void notifyStatusesChanged();

    template <typename Mutation>
    QmlCoreSettings::Change mutate(ChangeOrigin origin, Mutation mutation)
    {
        if (m_before_change) m_before_change(origin);
        const QmlCoreSettings::Change change = mutation(m_session);
        notifyChange(change);
        if (change.accepted && m_after_change) m_after_change(change, origin);
        return change;
    }

    QmlCoreSettings::Session m_session;
    QHash<QString, CoreSettingEntryModel*> m_entries;
    BeforeChangeHandler m_before_change;
    AfterChangeHandler m_after_change;
};

#endif // BITCOIN_QML_MODELS_CORE_SETTINGS_MODEL_H
