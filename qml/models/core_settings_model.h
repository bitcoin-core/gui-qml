// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_CORE_SETTINGS_MODEL_H
#define BITCOIN_QML_MODELS_CORE_SETTINGS_MODEL_H

#include <qml/core_settings.h>
#include <qml/snapshotquery.h>
#include <qml/ownedwork.h>
#include <atomic>
#include <memory>
#include <optional>

#include <QObject>
#include <QPointer>
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
class BackendExecutor;
struct CoreSettingsProjection;
class CoreProxyDraft;

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

    Q_INVOKABLE quint64 requestAddress(const QString& address);
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
    Q_PROPERTY(bool pending READ pending NOTIFY operationChanged)
    Q_PROPERTY(QString error READ error NOTIFY operationChanged)
    Q_PROPERTY(StorageChoice storageChoice READ storageChoice NOTIFY operationChanged)
    Q_PROPERTY(bool showCustomStorage READ showCustomStorage NOTIFY operationChanged)
    Q_PROPERTY(QString customStorageDescription READ customStorageDescription NOTIFY operationChanged)
    Q_PROPERTY(int customStorageSizeGB READ customStorageSizeGB NOTIFY operationChanged)
    Q_PROPERTY(quint64 operationId READ operationId NOTIFY operationChanged)
    Q_PROPERTY(OperationState operationState READ operationState NOTIFY operationChanged)
public:
    enum StorageChoice {FullStorage,ReducedStorage,CustomStorage};Q_ENUM(StorageChoice)
    enum class ChangeOrigin {User,Preview,Recommendation};
    enum OperationState {Idle,Pending,Applied,Rejected,Stale,Failed,Closed};
    Q_ENUM(OperationState)
    using BeforeChangeHandler=std::function<void(ChangeOrigin)>;
    using AfterChangeHandler=std::function<void(const QmlCoreSettings::Change&,ChangeOrigin)>;
    using Completion=std::function<void(bool)>;
    struct EffectResult {QString error;std::optional<QmlCoreSettings::Values> values;QVariantMap statuses;std::shared_ptr<const void> presentation;};
    using EffectContext=std::shared_ptr<const void>;
    using EffectPort=std::function<void(const QmlCoreSettings::Change&,EffectContext,std::shared_ptr<execution::Scheduler>,std::function<void(EffectResult)>,std::function<void(std::exception_ptr)>)>;
    void setEffectPort(EffectPort effect,std::function<EffectContext()> capture,std::function<void(EffectContext,const EffectResult&)> publish,std::function<void(EffectContext)> abort={}){m_effect=std::move(effect);m_capture_effect=std::move(capture);m_publish_effect=std::move(publish);m_abort_effect=std::move(abort);}

    explicit CoreSettingsModel(QObject* parent=nullptr);
    CoreSettingsModel(QmlCoreSettings::Values values,QmlCoreSettings::StatusProjection statuses={},QObject* parent=nullptr);
    ~CoreSettingsModel() override;
    void configureServices(execution::SnapshotServices services);
    void close();
    void whenEffectsDrained(std::function<void()> done){if(m_work)m_work->close(std::move(done));else if(done)done();}
    bool presentationOpen() const {return !m_stopping;}
    void setAdmissionReady(bool ready);
    bool pending() const {return m_pending!=0;}
    QString error() const {return m_error;}
    quint64 operationId() const {return m_next;}
    OperationState operationState() const {return m_operation_state;}
    quint64 userRevision() const {return m_user_revision;}
    quint64 publishedUserRevision() const {return m_published_user_revision;}
    quint64 previewContext() const {return m_context->load();}
    void invalidatePreviewContext(){++*m_context;}
    bool publishedValuesChanged() const {return m_published_values_changed;}
    QmlCoreSettings::Change publishedChange() const;
    Q_INVOKABLE QObject* entry(const QString& key);
    Q_INVOKABLE void refresh();
    Q_INVOKABLE QVariantMap status(const QString& key) const;
    Q_INVOKABLE CoreProxyDraft* createProxyDraft();
    const QmlCoreSettings::Values& values() const;
    const QSet<QString>& touchedSettings() const;
    QVariantMap statuses() const;
    bool canEdit(const QString& name) const;
    bool isTouched(const QString& name) const;
    void markTouched(const QString& name);
    void clearTouched(const QString& name);
    void clearTouchedSettings();
    void setTouchedSettings(const QSet<QString>& touched);
    void setStatuses(const QmlCoreSettings::StatusProjection& statuses);
    QString defaultProxyAddress() const;
    QString pruneTargetText()const;
    QString pruneTargetError()const;
    StorageChoice storageChoice()const;
    bool showCustomStorage()const;
    QString customStorageDescription()const;
    int64_t customStorageTarget()const;
    int customStorageSizeGB()const;
    Q_INVOKABLE void chooseStorage(StorageChoice choice);
    Q_INVOKABLE void discardPruneInput();
    quint64 changeListen(bool value);
    quint64 changeNatpmp(bool value);
    quint64 changeServer(bool value);
    quint64 changePrune(bool value);
    quint64 changePruneSizeGB(int value);
    quint64 changePruneTargetText(const QString& text);
    quint64 changeProxyEnabled(bool value);
    quint64 changeTorEnabled(bool value);
    quint64 changeProxyLocation(const QString& value);
    quint64 changeTorLocation(const QString& value);
    quint64 changePruneRecommendation(bool prune,int size,bool touched,std::optional<quint64> expected_user={});
    quint64 applyPreviewValuesPreservingTouched(const QmlCoreSettings::Values& values,const QmlCoreSettings::StatusProjection& statuses,
        Completion completion={},bool reset_touched=false,std::optional<quint64> expected_user={},std::optional<quint64> expected_context={});
    void setBeforeChangeHandler(BeforeChangeHandler handler){m_before_change=std::move(handler);}
    void setAfterChangeHandler(AfterChangeHandler handler){m_after_change=std::move(handler);}
Q_SIGNALS:
    void statusesChanged();
    void operationChanged();
    // A follow-up preview can become the latest operation before observers
    // receive the original user's outcome. Retain that outcome's own ticket.
    void operationFinished(quint64 ticket, OperationState state);
private:
    friend class CoreSettingEntryModel;
    friend class CoreProxyDraft;
    struct State;
    using Mutation=std::function<QmlCoreSettings::Change(QmlCoreSettings::Session&)>;
    void initializeServices(execution::SnapshotServices services);
    quint64 mutate(ChangeOrigin origin,Mutation mutation,Completion completion={},std::optional<quint64> expected_user={},std::optional<quint64> expected_context={});
    CoreSettingEntryModel* ensureEntry(const QString& key);
    void notifyChange(const QmlCoreSettings::Change& change);
    void publishDecision(std::shared_ptr<const CoreSettingsProjection> projection);
    void notifyStatusesChanged();
    QmlCoreSettings::Values m_initial_values;
    QVariantMap m_initial_statuses;
    std::shared_ptr<const char> m_requested_status_identity;
    QSet<QString> m_empty_touched;
    std::shared_ptr<State> m_native;
    std::shared_ptr<const CoreSettingsProjection> m_projection;
    execution::SnapshotServices m_services;
    std::unique_ptr<BackendExecutor> m_executor;
    std::unique_ptr<execution::OwnedWork> m_work;
    std::shared_ptr<std::atomic<quint64>> m_context{std::make_shared<std::atomic<quint64>>(0)};
    QHash<QString,CoreSettingEntryModel*> m_entries;
    EffectPort m_effect;
    std::function<void(EffectContext)> m_abort_effect;
    std::function<EffectContext()> m_capture_effect;
    std::function<void(EffectContext,const EffectResult&)> m_publish_effect;
    BeforeChangeHandler m_before_change;
    AfterChangeHandler m_after_change;
    quint64 m_next{0},m_next_user_ordinal{0},m_user_revision{0},m_published_user_revision{0};
    size_t m_pending{0};
    QString m_error;
    OperationState m_operation_state{Idle};
    bool m_stopping{false},m_published_values_changed{false},m_admission_ready{true};
};

class CoreProxyDraft : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool proxyEnabled READ proxyEnabled WRITE setProxyEnabled NOTIFY changed)
    Q_PROPERTY(QString proxyAddress READ proxyAddress WRITE setProxyAddress NOTIFY changed)
    Q_PROPERTY(bool torEnabled READ torEnabled WRITE setTorEnabled NOTIFY changed)
    Q_PROPERTY(QString torAddress READ torAddress WRITE setTorAddress NOTIFY changed)
    Q_PROPERTY(QString proxyError READ proxyError NOTIFY changed)
    Q_PROPERTY(QString torError READ torError NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool dirty READ dirty NOTIFY changed)
    Q_PROPERTY(bool canApply READ canApply NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(quint64 operationId READ operationId NOTIFY changed)
    Q_PROPERTY(CoreSettingsModel::OperationState operationState READ operationState NOTIFY changed)
public:
    explicit CoreProxyDraft(CoreSettingsModel& source);
    bool proxyEnabled() const{return m_proxy_enabled;}
    QString proxyAddress() const{return m_proxy_address;}
    bool torEnabled() const{return m_tor_enabled;}
    QString torAddress() const{return m_tor_address;}
    void setProxyEnabled(bool value);
    void setProxyAddress(const QString& value);
    void setTorEnabled(bool value);
    void setTorAddress(const QString& value);
    QString proxyError() const{return m_pending?QString{}:m_proxy_error;}
    QString torError() const{return m_pending?QString{}:m_tor_error;}
    bool pending() const{return m_pending;}
    bool ready() const{return m_ready;}
    bool dirty() const{return m_dirty;}
    bool canApply() const{return m_source&&m_source->presentationOpen()&&!m_source->pending()&&m_ready&&!m_pending&&m_valid&&m_operation_state!=CoreSettingsModel::Pending;}
    QString error() const{return m_error;}
    quint64 operationId() const{return m_operation_id;}
    CoreSettingsModel::OperationState operationState() const{return m_operation_state;}
    Q_INVOKABLE void reset();
    Q_INVOKABLE quint64 requestApply();
Q_SIGNALS:
    void changed();
private:
    void evaluate();
    void startEvaluation();
    void startReset();
    QPointer<CoreSettingsModel> m_source;
    quint64 m_revision{0},m_operation_id{0};
    QString m_proxy_address,m_tor_address,m_proxy_error,m_tor_error,m_error;
    bool m_proxy_enabled{false},m_tor_enabled{false},m_pending{false},m_dirty{false},m_valid{false},m_validation_inflight{false},m_ready{false},m_reset_inflight{false},m_reset_pending{false};
    CoreSettingsModel::OperationState m_operation_state{CoreSettingsModel::Idle};
};
#endif // BITCOIN_QML_MODELS_CORE_SETTINGS_MODEL_H
