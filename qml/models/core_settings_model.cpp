// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/core_settings_model.h>

#include <univalue.h>
#include <qml/backendexecutor.h>
#include <qml/guiconstants.h>
#include <common/settings.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <QQmlEngine>
#include <QPointer>
#include <map>
#include <array>
#include <limits>
#include <stdexcept>

#include <utility>

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
    // This presentation port accepts only the typed scalar carried by its role.
    // Do not coerce arbitrary strings/containers on the GUI thread.
    const auto type=value.metaType().id();
    if (m_key == QStringLiteral("listen") && type==QMetaType::Bool) {
        m_model.changeListen(value.toBool());
    } else if (m_key == QStringLiteral("natpmp") && type==QMetaType::Bool) {
        m_model.changeNatpmp(value.toBool());
    } else if (m_key == QStringLiteral("server") && type==QMetaType::Bool) {
        m_model.changeServer(value.toBool());
    } else if (m_key == QStringLiteral("prune") && (type==QMetaType::Int||type==QMetaType::Double||type==QMetaType::UInt||type==QMetaType::LongLong||type==QMetaType::ULongLong)) {
        m_model.changePruneSizeGB(value.toInt());
    } else if (m_key == QStringLiteral("proxy") && type==QMetaType::QString) {
        m_model.changeProxyLocation(value.toString());
    } else if (m_key == QStringLiteral("onion") && type==QMetaType::QString) {
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
    requestAddress(address);
}

QVariantMap CoreSettingEntryModel::status() const
{
    return m_model.status(m_key);
}

bool CoreSettingEntryModel::canEdit() const
{
    return m_model.canEdit(m_key);
}

QString CoreSettingEntryModel::infoText() const
{
    return m_model.error().isEmpty()?status().value(QStringLiteral("infoText")).toString():m_model.error();
}

bool CoreSettingEntryModel::createsGuiOverride() const
{
    return status().value(QStringLiteral("createsGuiOverride")).toBool();
}

quint64 CoreSettingEntryModel::requestAddress(const QString& address)
{
    if (m_key == QStringLiteral("proxy")) return m_model.changeProxyLocation(address);
    if (m_key == QStringLiteral("onion")) return m_model.changeTorLocation(address);
    return false;
}

QString CoreSettingEntryModel::defaultAddress() const
{
    return m_model.defaultProxyAddress();
}

namespace {
}
struct CoreSettingsProjection {
    QmlCoreSettings::Values values;
    QSet<QString> touched,editable;
    QVariantMap statuses;
    QString default_address,prune_input,prune_error,custom_description;
    int64_t custom_target{-1};int custom_size_gb{0};CoreSettingsModel::StorageChoice storage_choice{CoreSettingsModel::FullStorage};
    QmlCoreSettings::Change change;
    quint64 user_revision{0};
    bool stale{false},values_changed{false};
    std::optional<CoreSettingsModel::EffectResult> effect;
    QStringList planned_keys;
    bool statuses_changed{false};
    std::array<unsigned,6> notices{};
};
struct CoreSettingsModel::State {
    QmlCoreSettings::Values initial_values;QVariantMap initial_statuses;
    std::optional<QmlCoreSettings::Session> session;
    std::map<QString,quint64> requested;std::map<QString,size_t> pending_keys;
    quint64 user_revision{0},consumed_user_revision{0};size_t effects{0};bool uncertain{false};
    QmlCoreSettings::Session& nativeSession(){if(!session)session.emplace(std::move(initial_values),std::move(initial_statuses));return *session;}
};
namespace {
std::unique_ptr<CoreSettingsProjection> Project(const QmlCoreSettings::Session& session,QmlCoreSettings::Change change,quint64 revision,bool stale=false){
    auto result=std::make_unique<CoreSettingsProjection>();result->values=session.values();result->touched=session.touchedSettings();
    result->statuses=session.statuses();result->change=std::move(change);result->values_changed=QmlCoreSettings::ValuesChanged(result->change);
    result->statuses_changed=QmlCoreSettings::StatusesChanged(result->change);
    const auto& value=result->change;
    result->notices={unsigned(QmlCoreSettings::ListenChanged(value)?3:0),unsigned(QmlCoreSettings::NatpmpChanged(value)?3:0),unsigned(QmlCoreSettings::ServerChanged(value)?3:0),
        unsigned((QmlCoreSettings::PruneChanged(value)?2:0)|(QmlCoreSettings::PruneSizeGBChanged(value)?1:0)),
        unsigned((QmlCoreSettings::ProxyEnabledChanged(value)?2:0)|(QmlCoreSettings::ProxyAddressChanged(value)?5:0)),
        unsigned((QmlCoreSettings::TorEnabledChanged(value)?2:0)|(QmlCoreSettings::TorAddressChanged(value)?5:0))};
    if(!value.accepted){size_t i{0};for(const auto& name:{QStringLiteral("listen"),QStringLiteral("natpmp"),QStringLiteral("server"),QStringLiteral("prune"),QStringLiteral("proxy"),QStringLiteral("onion")}){
        if(value.setting_name.isEmpty()||name==value.setting_name)result->notices[i]=7;
        ++i;
    }}
    const auto target=QmlCoreSettings::PruneSetting(session.values()).getInt<int64_t>();
    result->storage_choice=target==0?CoreSettingsModel::FullStorage:target==QmlCoreSettings::PruneGBToMiB(DEFAULT_PRUNE_TARGET_GB)?CoreSettingsModel::ReducedStorage:CoreSettingsModel::CustomStorage;
    result->custom_target=session.customPruneTarget();result->custom_size_gb=result->custom_target>1?QmlCoreSettings::PruneMiBToGB(result->custom_target):0;
    if(result->custom_target==1)result->custom_description=QObject::tr("Manual pruning. No automatic block storage limit is enforced.");
    else if(result->custom_target>1)result->custom_description=QObject::tr("Storing recent blocks up to %1 MiB.").arg(result->custom_target);
    result->prune_input=session.pruneTargetText();result->prune_error=session.pruneTargetError();
    result->default_address=session.defaultProxyAddress();result->user_revision=revision;result->stale=stale;
    for(const auto& name:QmlCoreSettings::OnboardingCoreSettingNames()){if(session.canEdit(name))result->editable.insert(name);}
    return result;
}
QStringList IntentKeys(const QmlCoreSettings::Change& change,const QmlCoreSettings::Session& session){
    if(!change.accepted)return {};
    if(!change.setting_name.isEmpty())return {change.setting_name};
    QStringList keys;for(const auto& name:{QStringLiteral("proxy"),QStringLiteral("onion")})if(session.canEdit(name))keys<<name;return keys;
}
void CopySettingGroup(QmlCoreSettings::Values& target,const QmlCoreSettings::Values& source,const QString& key){
    if(key==QStringLiteral("listen"))target.listen=source.listen;
    else if(key==QStringLiteral("natpmp"))target.natpmp=source.natpmp;
    else if(key==QStringLiteral("server"))target.server=source.server;
    else if(key==QStringLiteral("prune")){target.prune=source.prune;target.prune_size_gb=source.prune_size_gb;target.prune_target_mib=source.prune_target_mib;}
    else if(key==QStringLiteral("proxy")){target.proxy_enabled=source.proxy_enabled;target.proxy_address=source.proxy_address;}
    else if(key==QStringLiteral("onion")){target.tor_enabled=source.tor_enabled;target.tor_address=source.tor_address;}
}
QmlCoreSettings::Change Observe(QmlCoreSettings::Session& session){
    QmlCoreSettings::Change change;change.before=change.after=session.values();change.before_statuses=change.after_statuses=session.statuses();change.accepted=true;return change;
}
}
CoreSettingsModel::CoreSettingsModel(QObject* parent):QObject(parent){}
CoreSettingsModel::CoreSettingsModel(QmlCoreSettings::Values values,QmlCoreSettings::StatusProjection statuses,QObject* parent)
    :QObject(parent),m_initial_values(std::move(values)),m_initial_statuses(std::move(statuses.values)){}
CoreSettingsModel::~CoreSettingsModel(){closeAdmission();if(m_executor)m_executor->shutdown();}
void CoreSettingsModel::configureServices(execution::SnapshotServices services){
    if(m_work||m_stopping)return;
    initializeServices(std::move(services));
    if(!m_native)return;
    mutate(ChangeOrigin::Preview,Observe,{},{},{});
}
void CoreSettingsModel::initializeServices(execution::SnapshotServices services){
    if(m_work||m_stopping)return;
    m_services=std::move(services);
    if(!m_services.scheduler){m_executor=std::make_unique<BackendExecutor>(execution::Options{});m_services.scheduler=m_executor->scheduler();}
    m_services.post=presentation::ReliablePost(std::move(m_services.post));
    m_work=std::make_unique<execution::OwnedWork>(m_services);
    m_native=execution::RetainSnapshot<State>(m_services.scheduler,[&]{auto state=std::make_unique<State>();state->initial_values=m_initial_values;state->initial_statuses=m_initial_statuses;return state;});
    if(!m_native){m_error=tr("Settings preview is unavailable.");m_operation_state=Failed;return;}
}
void CoreSettingsModel::setAdmissionReady(bool ready){if(m_stopping||m_admission_ready==ready)return;m_admission_ready=ready;notifyStatusesChanged();}
void CoreSettingsModel::closeAdmission(){m_stopping=true;++*m_context;m_pending=0;m_operation_state=Closed;if(m_work)m_work->close();if(m_projection){m_initial_values=m_projection->values;m_initial_statuses=m_projection->statuses;m_empty_touched=m_projection->touched;}m_projection.reset();m_native.reset();}
const QmlCoreSettings::Values& CoreSettingsModel::values() const{return m_projection?m_projection->values:m_initial_values;}
const QSet<QString>& CoreSettingsModel::touchedSettings() const{return m_projection?m_projection->touched:m_empty_touched;}
QVariantMap CoreSettingsModel::statuses() const{return m_projection?m_projection->statuses:m_initial_statuses;}
QVariantMap CoreSettingsModel::status(const QString& name) const{return statuses().value(name).toMap();}
bool CoreSettingsModel::canEdit(const QString& name) const{return !m_stopping&&m_admission_ready&&m_error.isEmpty()&&m_projection&&m_projection->editable.contains(name);}
bool CoreSettingsModel::isTouched(const QString& name) const{return touchedSettings().contains(name);}

QmlCoreSettings::Change CoreSettingsModel::publishedChange() const{return m_projection?m_projection->change:QmlCoreSettings::Change{};}
CoreSettingsModel::StorageChoice CoreSettingsModel::storageChoice()const{return m_projection?m_projection->storage_choice:FullStorage;}
bool CoreSettingsModel::showCustomStorage()const{return m_projection&&m_projection->custom_target>0;}
QString CoreSettingsModel::customStorageDescription()const{return m_projection?m_projection->custom_description:QString{};}
int64_t CoreSettingsModel::customStorageTarget()const{return m_projection?m_projection->custom_target:-1;}
int CoreSettingsModel::customStorageSizeGB()const{return m_projection?m_projection->custom_size_gb:0;}
void CoreSettingsModel::discardPruneInput(){mutate(ChangeOrigin::User,[](auto& session){return session.discardPruneInput();});}
void CoreSettingsModel::chooseStorage(StorageChoice choice){if(choice<FullStorage||choice>CustomStorage)return;mutate(ChangeOrigin::User,[choice](auto& session){return session.chooseStorage(static_cast<QmlCoreSettings::StorageChoice>(choice));});}
QString CoreSettingsModel::pruneTargetText()const{return m_projection?m_projection->prune_input:QString{};}
QString CoreSettingsModel::pruneTargetError()const{return m_projection?m_projection->prune_error:QString{};}
QString CoreSettingsModel::defaultProxyAddress() const{return m_projection?m_projection->default_address:QString{};}
void CoreSettingsModel::refresh(){mutate(ChangeOrigin::Preview,Observe);}
QObject* CoreSettingsModel::entry(const QString& key){if(!m_work)configureServices({});return ensureEntry(key);}
quint64 CoreSettingsModel::mutate(ChangeOrigin origin,Mutation mutation,Completion completion,std::optional<quint64> expected_user,std::optional<quint64> expected_context){
    if(m_stopping||(origin==ChangeOrigin::User&&!m_admission_ready))return 0;
    if(!m_work)initializeServices({});
    if(!m_native)return 0;
    const auto action=m_services.scheduler->openActionScope();
    if(!action){m_error=tr("The settings action could not be admitted.");m_operation_state=Failed;const QPointer<CoreSettingsModel> guard{this};notifyStatusesChanged();if(guard)Q_EMIT operationChanged();if(completion)completion(false);return 0;}
    const auto id=++m_next;const auto user_revision=origin==ChangeOrigin::User?++m_next_user_ordinal:m_user_revision;
    ++m_pending;m_operation_state=Pending;const bool had_error=!m_error.isEmpty();m_error.clear();const QPointer<CoreSettingsModel> observer{this};
    const auto context=origin==ChangeOrigin::User&&m_capture_effect?m_capture_effect():EffectContext{};
    const auto effect_control=std::make_shared<std::atomic<bool>>(false);const auto abort=m_abort_effect;
    const bool admitted=m_work->submitAsync<std::shared_ptr<const CoreSettingsProjection>>(
        [state=m_native,scheduler=action,mutation=std::move(mutation),fence=m_context,expected_user,expected_context,
         origin,user_revision,effect=m_effect,context,observer,post=m_services.post,abort,effect_control](auto finish,auto failed){
            const auto failure=[abort,context,failed](std::exception_ptr error){if(abort)abort(context);failed(error);};
            const auto admitted=execution::Submit(*scheduler,[state,scheduler,mutation,fence,expected_user,expected_context,origin,user_revision,plans_effect=bool(effect)]{
                const bool superseded=origin==ChangeOrigin::User&&user_revision<state->consumed_user_revision;
                if(origin==ChangeOrigin::User)state->consumed_user_revision=std::max(state->consumed_user_revision,user_revision);
                const bool stale=superseded||(expected_context&&fence->load()!=*expected_context)
                    ||(expected_user&&(state->consumed_user_revision!=*expected_user||state->effects));
                auto candidate=state->nativeSession();QmlCoreSettings::Change change;
                if(!stale){if(state->uncertain&&(origin==ChangeOrigin::User||!expected_user))throw std::runtime_error("The settings outcome requires an authoritative refresh");change=mutation(candidate);}
                const auto keys=origin==ChangeOrigin::User?IntentKeys(change,candidate):QStringList{};
                if(plans_effect&&origin==ChangeOrigin::User&&change.accepted&&!QmlCoreSettings::ValuesChanged(change)){
                    for(const auto& key:keys)if(state->pending_keys[key])change.force_write=true;
                }
                const bool writes=plans_effect&&origin==ChangeOrigin::User&&change.accepted&&(QmlCoreSettings::ValuesChanged(change)||change.force_write);
                auto projection=execution::RetainSnapshot<CoreSettingsProjection>(scheduler,[&]{return Project(candidate,std::move(change),state->consumed_user_revision,stale);});
                if(!projection)throw std::runtime_error("Settings projection capacity unavailable");
                // Committing a candidate and registering its planned effect is one
                // Domain decision. Outcome delivery and Core admission may be held.
                if(!stale){state->nativeSession()=std::move(candidate);if(origin==ChangeOrigin::User&&projection->change.accepted){state->user_revision=user_revision;for(const auto& key:keys)state->requested[key]=user_revision;}
                    if(expected_user)state->uncertain=false;
                    if(plans_effect&&origin==ChangeOrigin::User)++state->effects;
                    if(writes){projection->planned_keys=keys;for(const auto& key:keys)++state->pending_keys[key];}}
                return projection;
            },[state,scheduler,origin,user_revision,effect,context,finish,failed=failure,observer,post,abort,effect_control](std::shared_ptr<CoreSettingsProjection> projection){
                if(!effect||origin!=ChangeOrigin::User||projection->stale){if(projection->stale&&origin==ChangeOrigin::User&&abort)abort(context);finish(projection);return;}
                const auto& effect_done=effect_control;
                const auto effect_failed=[state,failed,effect_done,projection](std::exception_ptr error){if(effect_done->exchange(true))return;--state->effects;for(const auto& key:projection->planned_keys)--state->pending_keys[key];state->uncertain=true;failed(error);};
                try {effect(projection->change,context,scheduler,[state,scheduler,user_revision,projection,finish,failed,effect_done](EffectResult result){
                    if(effect_done->exchange(true))return;
                    --state->effects;for(const auto& key:projection->planned_keys)--state->pending_keys[key];
                    try {
                        auto change=projection->change;
                        if(result.values){
                            auto merged=state->nativeSession().values();
                            for(const auto& name:QmlCoreSettings::OnboardingCoreSettingNames())if(state->requested[name]<=user_revision)CopySettingGroup(merged,*result.values,name);
                            auto refreshed=QmlCoreSettings::Session{merged,result.statuses};refreshed.preservePruneInput(state->nativeSession());state->nativeSession()=std::move(refreshed);state->uncertain=false;
                            change.after=merged;change.after_statuses=result.statuses;
                            auto completed=execution::RetainSnapshot<CoreSettingsProjection>(scheduler,[&]{return Project(state->nativeSession(),change,state->consumed_user_revision);});
                            if(!completed)throw std::runtime_error("Settings effect outcome capacity unavailable");
                            completed->effect=std::move(result);finish(completed);
                        } else {auto completed=execution::RetainSnapshot<CoreSettingsProjection>(scheduler,[&]{return std::make_unique<CoreSettingsProjection>(*projection);});
                            if(!completed)throw std::runtime_error("Settings outcome capacity unavailable");
                            completed->effect=std::move(result);finish(completed);}
                    }catch(...){failed(std::current_exception());}
                },effect_failed);
                    if(!effect_done->load()) {const std::weak_ptr<const CoreSettingsProjection> desired{projection};
                        try{post([observer,desired]{if(observer)if(const auto snapshot=desired.lock())observer->publishDecision(snapshot);});}catch(...){}
                    }
                }catch(...){effect_failed(std::current_exception());}
            },failure,{execution::Lane::Domain,{},execution::WorkClass::Action});
            return admitted;
    },[observer,id,origin,completion,context](std::shared_ptr<const CoreSettingsProjection> projection){
        if(!observer||observer->m_stopping)return;
        --observer->m_pending;const bool accepted=!projection->stale&&projection->change.accepted&&(!projection->effect||projection->effect->error.isEmpty());
        const auto outcome=projection->stale?Stale:projection->effect&&!projection->effect->error.isEmpty()?Failed:accepted?Applied:Rejected;
        if(id==observer->m_next)observer->m_operation_state=outcome;
        if(!projection->stale&&projection->user_revision>=observer->m_published_user_revision){
            auto presentation=projection->change;presentation.before=observer->values();presentation.before_statuses=observer->statuses();
            presentation.after=projection->values;presentation.after_statuses=projection->statuses;
            const auto before=observer->m_before_change;if(projection->change.accepted&&before)before(origin);if(!observer||observer->m_stopping)return;
            observer->m_projection=projection;observer->m_published_user_revision=projection->user_revision;observer->m_published_values_changed=projection->values_changed;
            if(projection->effect&&id==observer->m_next){const bool availability=!observer->m_error.isEmpty()||!projection->effect->error.isEmpty();observer->m_error=projection->effect->error;
                if(availability)observer->notifyStatusesChanged();
                if(!observer||observer->m_stopping)return;
                }
            if(!observer||observer->m_stopping)return;
            observer->notifyChange(presentation);if(!observer||observer->m_stopping)return;
            const auto after=observer->m_after_change;if(projection->change.accepted&&after)after(presentation,origin);if(!observer||observer->m_stopping)return;
        }
        if(projection->effect&&observer->m_publish_effect)observer->m_publish_effect(context,*projection->effect);
        if(!observer||observer->m_stopping)return;
        if(completion)completion(accepted);
        if(observer&&!observer->m_stopping)Q_EMIT observer->operationFinished(id,outcome);
        if(observer&&!observer->m_stopping)Q_EMIT observer->operationChanged();
    },[observer,id,completion,abort=m_abort_effect,context](std::exception_ptr){
        if(abort)abort(context);
        if(!observer||observer->m_stopping)return;
        --observer->m_pending;if(id==observer->m_next)observer->m_operation_state=Failed;
        observer->m_error=tr("Settings preview could not be completed.");observer->notifyStatusesChanged();if(!observer||observer->m_stopping)return;
        for(const auto& key:{QStringLiteral("listen"),QStringLiteral("natpmp"),QStringLiteral("server"),QStringLiteral("prune"),QStringLiteral("proxy"),QStringLiteral("onion")}){
            if(auto* entry=observer->m_entries.value(key)){Q_EMIT entry->valueChanged();if(!observer||observer->m_stopping)return;
                Q_EMIT entry->enabledChanged();if(!observer||observer->m_stopping)return;Q_EMIT entry->addressChanged();if(!observer||observer->m_stopping)return;}
        }
        if(completion)completion(false);
        if(observer&&!observer->m_stopping)Q_EMIT observer->operationFinished(id,Failed);
        if(observer&&!observer->m_stopping)Q_EMIT observer->operationChanged();
    },action);
    if(!admitted&&abort)abort(context);
    if(observer&&admitted&&origin==ChangeOrigin::User)m_user_revision=std::max(m_user_revision,user_revision);
    if(observer&&!m_stopping&&had_error)notifyStatusesChanged();
    if(observer&&!m_stopping)Q_EMIT operationChanged();
    return admitted?id:0;
}
void CoreSettingsModel::markTouched(const QString& name){mutate(ChangeOrigin::Preview,[name](auto& session){auto change=Observe(session);session.markTouched(name);change.accepted=true;return change;});}
void CoreSettingsModel::clearTouched(const QString& name){mutate(ChangeOrigin::Preview,[name](auto& session){auto change=Observe(session);session.clearTouched(name);change.accepted=true;return change;});}
void CoreSettingsModel::clearTouchedSettings(){mutate(ChangeOrigin::Preview,[](auto& session){auto change=Observe(session);session.clearTouchedSettings();change.accepted=true;return change;});}
void CoreSettingsModel::setTouchedSettings(const QSet<QString>& touched){mutate(ChangeOrigin::Preview,[touched](auto& session){auto change=Observe(session);session.setTouchedSettings(touched);change.accepted=true;return change;});}
void CoreSettingsModel::setStatuses(const QmlCoreSettings::StatusProjection& statuses){
    if((statuses.identity&&statuses.identity==m_requested_status_identity)||(!statuses.identity&&statuses.values.isEmpty()))return;
    const QPointer<CoreSettingsModel> guard{this};const auto ticket=mutate(ChangeOrigin::Preview,[statuses](auto& session){auto change=Observe(session);session.setStatuses(statuses.values);change.after_statuses=session.statuses();change.accepted=true;return change;},{},{},{});if(guard&&ticket)m_requested_status_identity=statuses.identity;
}
quint64 CoreSettingsModel::applyPreviewValuesPreservingTouched(const QmlCoreSettings::Values& values,const QmlCoreSettings::StatusProjection& statuses,Completion completion,bool reset_touched,std::optional<quint64> expected_user,std::optional<quint64> expected_context){
    return mutate(ChangeOrigin::Preview,[values,statuses,reset_touched](auto& session){if(reset_touched)session.clearTouchedSettings();return session.applyPreviewValuesPreservingTouched(values,statuses.values);},std::move(completion),expected_user,expected_context);
}
quint64 CoreSettingsModel::changeListen(bool value){return mutate(ChangeOrigin::User,[value](auto& session){auto change=session.changeListen(value);if(!change.accepted&&session.canEdit(QStringLiteral("listen"))&&session.values().listen==value)change.accepted=true;return change;});}
quint64 CoreSettingsModel::changeNatpmp(bool value){return mutate(ChangeOrigin::User,[value](auto& session){auto change=session.changeNatpmp(value);if(!change.accepted&&session.canEdit(QStringLiteral("natpmp"))&&session.values().natpmp==value)change.accepted=true;return change;});}
quint64 CoreSettingsModel::changeServer(bool value){return mutate(ChangeOrigin::User,[value](auto& session){auto change=session.changeServer(value);if(!change.accepted&&session.canEdit(QStringLiteral("server"))&&session.values().server==value)change.accepted=true;return change;});}
quint64 CoreSettingsModel::changePrune(bool value){return mutate(ChangeOrigin::User,[value](auto& session){auto change=session.changePrune(value);if(!change.accepted&&session.canEdit(QStringLiteral("prune"))&&session.values().prune==value)change.accepted=true;return change;});}
quint64 CoreSettingsModel::changePruneSizeGB(int value){return mutate(ChangeOrigin::User,[value](auto& session){auto change=session.changePruneSizeGB(value);if(!change.accepted&&session.canEdit(QStringLiteral("prune"))&&session.values().prune_size_gb==value)change.accepted=true;return change;});}
quint64 CoreSettingsModel::changePruneTargetText(const QString& text){
    const auto bounded=text.left(65);
    return mutate(ChangeOrigin::User,[bounded](auto& session){return session.changePruneTargetText(bounded);},{},{},{});
}
quint64 CoreSettingsModel::changeProxyEnabled(bool value){return mutate(ChangeOrigin::User,[value](auto& session){auto change=session.changeProxyEnabled(value);if(!change.accepted&&session.canEdit(QStringLiteral("proxy"))&&session.values().proxy_enabled==value)change.accepted=true;return change;});}
quint64 CoreSettingsModel::changeTorEnabled(bool value){return mutate(ChangeOrigin::User,[value](auto& session){auto change=session.changeTorEnabled(value);if(!change.accepted&&session.canEdit(QStringLiteral("onion"))&&session.values().tor_enabled==value)change.accepted=true;return change;});}
quint64 CoreSettingsModel::changeProxyLocation(const QString& value){return mutate(ChangeOrigin::User,[value](auto& session){return session.changeProxyLocation(value);},{},{},{});}
quint64 CoreSettingsModel::changeTorLocation(const QString& value){return mutate(ChangeOrigin::User,[value](auto& session){return session.changeTorLocation(value);},{},{},{});}
quint64 CoreSettingsModel::changePruneRecommendation(bool prune,int size,bool touched,std::optional<quint64> expected_user){return mutate(ChangeOrigin::Recommendation,[prune,size,touched](auto& session){return session.changePruneRecommendation(prune,size,touched);},{},expected_user);}
CoreSettingEntryModel* CoreSettingsModel::ensureEntry(const QString& key)
{
    if(key!=QStringLiteral("listen")&&key!=QStringLiteral("natpmp")&&key!=QStringLiteral("server")&&key!=QStringLiteral("prune")&&key!=QStringLiteral("proxy")&&key!=QStringLiteral("onion"))return nullptr;
    if (CoreSettingEntryModel* existing = m_entries.value(key)) return existing;
    auto* entry = new CoreSettingEntryModel{key, *this, this};
    m_entries.insert(key, entry);
    return entry;
}

void CoreSettingsModel::notifyStatusesChanged(){
    const QPointer<CoreSettingsModel> observer{this};Q_EMIT statusesChanged();if(!observer||m_stopping)return;
    for(const auto& key:{QStringLiteral("listen"),QStringLiteral("natpmp"),QStringLiteral("server"),QStringLiteral("prune"),QStringLiteral("proxy"),QStringLiteral("onion")}){
        if(auto* entry=m_entries.value(key))Q_EMIT entry->statusChanged();
        if(!observer||m_stopping)return;
    }
}
void CoreSettingsModel::publishDecision(std::shared_ptr<const CoreSettingsProjection> projection){
    if(m_stopping||projection->stale||projection->user_revision<m_published_user_revision)return;
    auto presentation=projection->change;presentation.before=values();presentation.before_statuses=statuses();
    const QPointer<CoreSettingsModel> observer{this};const auto before=m_before_change;if(before)before(ChangeOrigin::User);if(!observer||m_stopping)return;
    m_projection=projection;m_published_user_revision=projection->user_revision;m_published_values_changed=projection->values_changed;
    notifyChange(presentation);if(!observer||m_stopping)return;const auto after=m_after_change;if(after)after(presentation,ChangeOrigin::User);
}
void CoreSettingsModel::notifyChange(const QmlCoreSettings::Change& change){
    // Six fixed presentation entries: compare the actual GUI projection with
    // the immutable desired/actual Native snapshot. No Session decision or
    // effect admission depends on these Qt notification choices.
    const QPointer<CoreSettingsModel> observer{this};
    if(QmlCoreSettings::StatusesChanged(change))notifyStatusesChanged();
    if(!observer||m_stopping)return;
    std::array<unsigned,6> notices={unsigned(QmlCoreSettings::ListenChanged(change)?3:0),unsigned(QmlCoreSettings::NatpmpChanged(change)?3:0),unsigned(QmlCoreSettings::ServerChanged(change)?3:0),
        unsigned((QmlCoreSettings::PruneChanged(change)?2:0)|(QmlCoreSettings::PruneSizeGBChanged(change)?1:0)),
        unsigned((QmlCoreSettings::ProxyEnabledChanged(change)?2:0)|(QmlCoreSettings::ProxyAddressChanged(change)?5:0)),
        unsigned((QmlCoreSettings::TorEnabledChanged(change)?2:0)|(QmlCoreSettings::TorAddressChanged(change)?5:0))};
    int row{0};for(const auto& key:{QStringLiteral("listen"),QStringLiteral("natpmp"),QStringLiteral("server"),QStringLiteral("prune"),QStringLiteral("proxy"),QStringLiteral("onion")}){
        auto* entry=m_entries.value(key);auto notice=notices[row++];if(!entry)continue;if(!change.accepted&&(change.setting_name.isEmpty()||change.setting_name==key))notice|=7;
        if(notice&1)Q_EMIT entry->valueChanged();
        if(!observer||m_stopping)return;
        if(notice&2)Q_EMIT entry->enabledChanged();
        if(!observer||m_stopping)return;
        if(notice&4)Q_EMIT entry->addressChanged();
        if(!observer||m_stopping)return;
    }
}

namespace {
struct ProxyDraftProjection {QString proxy_error,tor_error;bool valid{false},dirty{false};};
ProxyDraftProjection EvaluateProxyDraft(const QmlCoreSettings::Session& session,bool enabled,const QString& address,bool tor,const QString& onion){
    ProxyDraftProjection result;result.proxy_error=address.size()>32768?QObject::tr("The proxy location is too long."):session.validateProxyLocation(address);
    result.tor_error=onion.size()>32768?QObject::tr("The proxy location is too long."):session.validateProxyLocation(onion);
    result.valid=(!enabled||result.proxy_error.isEmpty())&&(!tor||result.tor_error.isEmpty());
    const auto& values=session.values();const auto display=[&](const QString& value){return value.isEmpty()?session.defaultProxyAddress():value;};
    result.dirty=(session.canEdit(QStringLiteral("proxy"))&&(enabled!=values.proxy_enabled||address!=display(values.proxy_address)))
        ||(session.canEdit(QStringLiteral("onion"))&&(tor!=values.tor_enabled||onion!=display(values.tor_address)));
    return result;
}
}
CoreProxyDraft* CoreSettingsModel::createProxyDraft(){if(m_stopping)return nullptr;if(!m_work)configureServices({});auto* draft=new CoreProxyDraft(*this);QQmlEngine::setObjectOwnership(draft,QQmlEngine::JavaScriptOwnership);return draft;}
CoreProxyDraft::CoreProxyDraft(CoreSettingsModel& source):m_source(&source){
    connect(&source,&CoreSettingsModel::operationChanged,this,[this]{if(!m_reset_pending)evaluate();});
    connect(&source,&CoreSettingsModel::statusesChanged,this,[this]{if(!m_reset_pending)evaluate();});reset();
}
void CoreProxyDraft::reset(){
    if(!m_source||!m_source->presentationOpen()||!m_source->m_native)return;
    ++m_revision;m_pending=true;m_ready=false;m_reset_pending=true;m_error.clear();const QPointer<CoreProxyDraft> observer{this};
    if(!m_reset_inflight)startReset();
    if(observer)Q_EMIT changed();
}
void CoreProxyDraft::startReset(){
    if(m_reset_inflight||!m_reset_pending)return;
    if(!m_source||!m_source->presentationOpen()||!m_source->m_native)return;
    m_reset_inflight=true;
    const auto revision=m_revision;const QPointer<CoreProxyDraft> observer{this};const QPointer<CoreSettingsModel> source=m_source;
    m_source->m_work->submit([state=m_source->m_native]{auto values=state->nativeSession().values();if(values.proxy_address.isEmpty())values.proxy_address=state->nativeSession().defaultProxyAddress();if(values.tor_address.isEmpty())values.tor_address=state->nativeSession().defaultProxyAddress();return values;},
        [observer,source,revision](const QmlCoreSettings::Values& values){if(!observer||!source||!source->presentationOpen())return;
            observer->m_reset_inflight=false;if(observer->m_revision!=revision){if(observer->m_reset_pending)source->m_services.post([observer]{if(observer)observer->startReset();});return;}
            observer->m_reset_pending=false;observer->m_ready=true;
            observer->m_proxy_enabled=values.proxy_enabled;observer->m_proxy_address=values.proxy_address;observer->m_tor_enabled=values.tor_enabled;observer->m_tor_address=values.tor_address;
            observer->evaluate();},[observer,source,revision](std::exception_ptr){if(!observer||!source||!source->presentationOpen())return;
            observer->m_reset_inflight=false;if(observer->m_revision!=revision){if(observer->m_reset_pending)source->m_services.post([observer]{if(observer)observer->startReset();});return;}
            observer->m_reset_pending=false;observer->m_pending=false;observer->m_ready=false;observer->m_error=tr("The proxy draft could not be opened.");Q_EMIT observer->changed();},
        {execution::Lane::Domain,{},execution::WorkClass::Observation});
}
void CoreProxyDraft::setProxyEnabled(bool value){if(!m_ready)return;if(m_proxy_enabled==value)return;m_proxy_enabled=value;evaluate();}
void CoreProxyDraft::setProxyAddress(const QString& value){if(!m_ready)return;if(value.size()<=32768&&m_proxy_address==value)return;m_proxy_address=value;evaluate();}
void CoreProxyDraft::setTorEnabled(bool value){if(!m_ready)return;if(m_tor_enabled==value)return;m_tor_enabled=value;evaluate();}
void CoreProxyDraft::setTorAddress(const QString& value){if(!m_ready)return;if(value.size()<=32768&&m_tor_address==value)return;m_tor_address=value;evaluate();}
void CoreProxyDraft::evaluate(){
    if(!m_source||!m_source->presentationOpen()||!m_source->m_native||m_reset_pending||!m_ready)return;
    ++m_revision;m_pending=true;m_valid=false;const QPointer<CoreProxyDraft> observer{this};
    if(!m_validation_inflight)startEvaluation();
    if(observer)Q_EMIT changed();
}
void CoreProxyDraft::startEvaluation(){
    if(m_validation_inflight)return;
    if(!m_source||!m_source->presentationOpen()||!m_source->m_native||m_reset_pending||!m_ready)return;
    m_validation_inflight=true;
    const auto revision=m_revision;const QPointer<CoreProxyDraft> observer{this};const QPointer<CoreSettingsModel> source=m_source;
    m_source->m_work->submit([state=m_source->m_native,enabled=m_proxy_enabled,address=m_proxy_address,tor=m_tor_enabled,onion=m_tor_address]{return EvaluateProxyDraft(state->nativeSession(),enabled,address,tor,onion);},
        [observer,source,revision](const ProxyDraftProjection& result){if(!observer||!source||!source->presentationOpen())return;
            observer->m_validation_inflight=false;if(observer->m_revision!=revision){source->m_services.post([observer]{if(observer)observer->startEvaluation();});return;}
            observer->m_pending=false;observer->m_proxy_error=result.proxy_error;observer->m_tor_error=result.tor_error;observer->m_valid=result.valid;observer->m_dirty=result.dirty;Q_EMIT observer->changed();},
        [observer,source,revision](std::exception_ptr){if(!observer||!source||!source->presentationOpen())return;
            observer->m_validation_inflight=false;if(observer->m_revision!=revision){source->m_services.post([observer]{if(observer)observer->startEvaluation();});return;}
            observer->m_pending=false;observer->m_valid=false;observer->m_error=tr("The proxy draft could not be checked.");Q_EMIT observer->changed();},
        {execution::Lane::Domain,{},execution::WorkClass::Observation});
}
quint64 CoreProxyDraft::requestApply(){
    if(!m_source||!m_source->presentationOpen()||!canApply())return 0;
    const bool enabled=m_proxy_enabled,tor=m_tor_enabled;const auto address=m_proxy_address,onion=m_tor_address;
    m_operation_state=CoreSettingsModel::Pending;m_error.clear();const QPointer<CoreProxyDraft> observer{this};
    const auto id=m_source->mutate(CoreSettingsModel::ChangeOrigin::User,[enabled,tor,address,onion](QmlCoreSettings::Session& session){
        auto change=Observe(session);if(!EvaluateProxyDraft(session,enabled,address,tor,onion).valid)return change;
        auto candidate=session;
        if(candidate.canEdit(QStringLiteral("proxy"))){if(candidate.validateProxyLocation(address).isEmpty())candidate.changeProxyLocation(address);candidate.changeProxyEnabled(enabled);}
        if(candidate.canEdit(QStringLiteral("onion"))){if(candidate.validateProxyLocation(onion).isEmpty())candidate.changeTorLocation(onion);candidate.changeTorEnabled(tor);}
        session=std::move(candidate);change.accepted=true;change.after=session.values();change.after_statuses=session.statuses();return change;
    },[observer](bool accepted){if(!observer)return;observer->m_operation_state=accepted?CoreSettingsModel::Applied:CoreSettingsModel::Failed;
        if(!accepted)observer->m_error=tr("The proxy settings could not be applied.");
        Q_EMIT observer->changed();},{},{});
    if(!observer)return id;
    m_operation_id=id;if(!id)m_operation_state=CoreSettingsModel::Failed;Q_EMIT changed();return id;
}
