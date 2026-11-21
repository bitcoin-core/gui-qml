// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/uipreferences.h>
#include <qml/guipresentation.h>
#include <qml/windowgeometry.h>
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QScopeGuard>
#include <array>
#include <QCoreApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <cmath>
#include <stdexcept>
#include <limits>
#include <utility>
namespace {
const QStringList& Allowed(const QString& group){
    static const QStringList direction{QStringLiteral("inbound"),QStringLiteral("outbound")};
    static const QStringList connection{QStringLiteral("full-relay"),QStringLiteral("block-relay"),QStringLiteral("manual")};
    // Every network a peer can be on has a choice, so ticking networks never
    // hides a peer that matches none of them (#1113).
    static const QStringList network{QStringLiteral("ipv4"),QStringLiteral("ipv6"),QStringLiteral("onion"),QStringLiteral("i2p"),QStringLiteral("cjdns"),QStringLiteral("unroutable")};
    static const QStringList transport{QStringLiteral("v1"),QStringLiteral("v2")},none;
    if(group=="direction")return direction;
    if(group=="connectionType")return connection;
    if(group=="network")return network;
    if(group=="transport")return transport;
    return none;
}
QStringList Decode(const QString& literal,const QString& group){
    const auto& allowed=Allowed(group);QStringList result;
    // Parsing, deduplication, validation and legacy comma decoding are Native.
    for(const auto& item:literal.split(',')){if(!allowed.contains(item)||result.contains(item))continue;result<<item;if(group=="direction")break;}
    return result.size()==allowed.size()?QStringList{}:result;
}
bool ValidSort(const QString& key){return key=="nodeId"||key=="address"||key=="connectionType"||key=="subversion"||key=="sent"||key=="received";}
bool ValidScale(int value){return value==300||value==3600||value==43200||value==86400;}
std::unique_ptr<QSettings> Open(const UiPreferencesNamespace& id){
    if(!id.file.isEmpty())return std::make_unique<QSettings>(id.file,QSettings::IniFormat);
    return std::make_unique<QSettings>(id.format,id.scope,id.organization,id.application);
}
UiPreferencesValues Read(QSettings& settings){
    UiPreferencesValues value;value.dark=settings.value("dark",value.dark).toBool();
    const auto size=settings.value("blockclocksize",value.blockclocksize).toDouble();if(std::isfinite(size)&&size>0&&size<=1)value.blockclocksize=size;
    for(const auto& item:{std::pair{"windowX",&value.windowX},{"windowY",&value.windowY},{"windowWidth",&value.windowWidth},{"windowHeight",&value.windowHeight}}){const auto scalar=settings.value(item.first,0).toDouble();if(std::isfinite(scalar)&&scalar>=std::numeric_limits<int>::min()&&scalar<=std::numeric_limits<int>::max())*item.second=scalar;}
    const auto grouping=settings.value("activityGrouping",value.activityGrouping).toString();if(grouping=="month"||grouping=="day")value.activityGrouping=grouping;
    const auto density=settings.value("activityDisplayDensity",value.activityDisplayDensity).toString();if(density=="comfortable"||density=="compact")value.activityDisplayDensity=density;
    const auto scale=settings.value("trafficGraphScale",value.trafficGraphScale).toInt();if(ValidScale(scale))value.trafficGraphScale=scale;
    const auto sort=settings.value("peerListSortBy",value.peerListSortBy).toString();if(ValidSort(sort))value.peerListSortBy=sort;
    value.peerListSortAscending=settings.value("peerListSortAscending",true).toBool();
    value.peerDirectionFilters=Decode(settings.value("peerDirectionFilters").toString(),"direction");
    value.peerConnectionTypeFilters=Decode(settings.value("peerConnectionTypeFilters").toString(),"connectionType");
    value.peerNetworkFilters=Decode(settings.value("peerNetworkFilters").toString(),"network");
    value.peerTransportFilters=Decode(settings.value("peerTransportFilters").toString(),"transport");return value;
}
QVariant KeyValue(const UiPreferencesValues& value,const QString& key){
#define PREF_KEY(Name) if(key==QStringLiteral(#Name))return value.Name;
    PREF_KEY(dark) PREF_KEY(blockclocksize) PREF_KEY(windowX) PREF_KEY(windowY) PREF_KEY(windowWidth) PREF_KEY(windowHeight)
    PREF_KEY(activityGrouping) PREF_KEY(activityDisplayDensity) PREF_KEY(trafficGraphScale) PREF_KEY(peerListSortBy) PREF_KEY(peerListSortAscending)
#undef PREF_KEY
    if(key=="peerDirectionFilters")return value.peerDirectionFilters.join(',');
    if(key=="peerConnectionTypeFilters")return value.peerConnectionTypeFilters.join(',');
    if(key=="peerNetworkFilters")return value.peerNetworkFilters.join(',');
    if(key=="peerTransportFilters")return value.peerTransportFilters.join(',');
    throw std::invalid_argument("Unknown UI preference");
}
QStringList& Filters(UiPreferencesValues& values,const QString& group){
    if(group=="direction")return values.peerDirectionFilters;
    if(group=="connectionType")return values.peerConnectionTypeFilters;
    if(group=="network")return values.peerNetworkFilters;
    if(group=="transport")return values.peerTransportFilters;
    throw std::invalid_argument("Unknown peer filter group");
}
QString FilterKey(const QString& group){
    if(group=="direction")return QStringLiteral("peerDirectionFilters");
    if(group=="connectionType")return QStringLiteral("peerConnectionTypeFilters");
    if(group=="network")return QStringLiteral("peerNetworkFilters");
    if(group=="transport")return QStringLiteral("peerTransportFilters");
    return {};
}
}
UiPreferencesNamespace UiPreferencesNamespace::Capture(){
    UiPreferencesNamespace id;id.organization=QCoreApplication::organizationName();
#ifdef Q_OS_MACOS
    if(!QCoreApplication::organizationDomain().isEmpty())id.organization=QCoreApplication::organizationDomain();
#endif
    id.application=QCoreApplication::applicationName();id.format=QSettings::defaultFormat();return id;
}
struct UiPreferencesStore::Native {UiPreferencesNamespace id;UiPreferencesValues values;quint64 revision{0};};
struct UiPreferencesStore::Result {UiPreferencesValues values;preferences::WatchPaths watch;QString error;quint64 revision{0};bool loaded{false};};
UiPreferencesStore::UiPreferencesStore(execution::SnapshotServices services,UiPreferencesNamespace id,QObject* parent)
    :QObject(parent),m_services(std::move(services)){
    m_services.post=presentation::ReliablePost(std::move(m_services.post));
    if(!m_services.scheduler)throw std::invalid_argument("UI preferences require the shared application scheduler");
    m_key=m_services.scheduler->allocateKey();
    m_work=std::make_unique<execution::OwnedWork>(m_services);
    m_native=execution::RetainSnapshot<Native>(m_services.scheduler,[&]{auto state=std::make_unique<Native>();state->id=std::move(id);return state;});
    if(!m_native)m_error=tr("Preferences storage is unavailable.");
}
UiPreferencesStore::~UiPreferencesStore(){close();}
const UiPreferencesValues& UiPreferencesStore::values()const{return m_result?m_result->values:m_defaults;}
void UiPreferencesStore::start(){if(!m_closed&&!m_ready&&!m_pending){m_refresh_requested=false;load();}}
void UiPreferencesStore::retry(){if(!m_closed&&!m_pending){m_refresh_requested=false;load();}}
void UiPreferencesStore::refresh(){if(m_closed)return;m_refresh_requested=true;finishRefresh();}
// Failed admission/read waits for a fresh observation or explicit retry; only
// successful asynchronous publication resumes retained refresh demand.
void UiPreferencesStore::finishRefresh(){if(m_closed||m_pending||m_loading||!m_refresh_requested)return;m_refresh_requested=false;load();}
void UiPreferencesStore::load(){
    if(!m_native||m_loading)return;
    m_loading=true;const QPointer<UiPreferencesStore> loading_observer{this};
    const auto loading=qScopeGuard([loading_observer]{if(loading_observer)loading_observer->m_loading=false;});
    const auto action=m_services.scheduler->openActionScope();if(!action){m_error=tr("Preferences read could not be admitted.");notifyViews();Q_EMIT stateChanged();return;}
    const auto output=execution::RetainSnapshot<Result>(action,[]{return std::make_unique<Result>();});if(!output){m_error=tr("Preferences result is unavailable.");notifyViews();Q_EMIT stateChanged();return;}
    const auto ticket=++m_operation_id;++m_pending;const QPointer<UiPreferencesStore> observer{this};
    m_work->submitAsync<std::shared_ptr<const Result>>([action,state=m_native,output,key=m_key](auto done,auto failed){
        return execution::Submit(*action,[state,output]{auto settings=Open(state->id);settings->sync();if(settings->status()!=QSettings::NoError)throw std::runtime_error("Unable to read UI preferences");state->values=Read(*settings);output->watch=preferences::Paths(*settings);output->values=state->values;output->revision=++state->revision;output->loaded=true;return std::shared_ptr<const Result>{output};},done,failed,{execution::Lane::Core,key,execution::WorkClass::Action});
    },[observer,ticket](auto result){if(observer&&!observer->m_closed)observer->publish(std::move(result),ticket);},[observer,ticket](std::exception_ptr){if(!observer||observer->m_closed)return;--observer->m_pending;if(ticket==observer->m_operation_id)observer->m_error=tr("Unable to read UI preferences.");observer->notifyViews();Q_EMIT observer->stateChanged();},action);
    if(observer&&!m_closed){notifyViews();Q_EMIT stateChanged();}
}
quint64 UiPreferencesStore::write(QStringList keys,Mutation mutation){
    if(m_closed||!m_ready||!m_native)return 0;
    const auto action=m_services.scheduler->openActionScope();
    if(!action){m_error=tr("Preferences write could not be admitted.");notifyViews();Q_EMIT stateChanged();return 0;}
    const auto output=execution::RetainSnapshot<Result>(action,[]{return std::make_unique<Result>();});if(!output){m_error=tr("Preferences result is unavailable.");notifyViews();Q_EMIT stateChanged();return 0;}
    const auto receipt=std::make_shared<std::atomic<Outcome>>(Pending);const auto ticket=++m_operation_id;m_receipt=receipt;++m_pending;
    const QPointer<UiPreferencesStore> observer{this};
    const bool admitted=m_work->submitAsync<std::shared_ptr<const Result>>([state=m_native,action,output,receipt,keys=std::move(keys),mutation=std::move(mutation),key=m_key](auto done,auto failed){
        const auto rejected=[receipt,failed](std::exception_ptr error){if(receipt->load()==Pending)*receipt=Failed;failed(error);};
        return execution::Submit(*action,[state,output,receipt,keys,mutation]{
            auto settings=Open(state->id);settings->sync();if(settings->status()!=QSettings::NoError)throw std::runtime_error("Unable to read UI preferences before saving");auto candidate=Read(*settings);mutation(candidate);
            // Only original intent keys are written. Unrelated legacy settings
            // and independently selected chain namespaces remain untouched.
            for(const auto& key:keys)settings->setValue(key,KeyValue(candidate,key));
            settings->sync();
            if(settings->status()!=QSettings::NoError){*receipt=Uncertain;output->error=QObject::tr("UI preferences could not be saved; the persisted outcome is uncertain.");}
            else{*receipt=Applied;state->values=std::move(candidate);}
            output->watch=preferences::Paths(*settings);output->values=state->values;output->revision=++state->revision;output->loaded=true;return std::shared_ptr<const Result>{output};
        },done,rejected,{execution::Lane::Core,key,execution::WorkClass::Action});
    },[observer,ticket](auto result){if(observer&&!observer->m_closed)observer->publish(std::move(result),ticket);},[observer,receipt,ticket](std::exception_ptr){if(receipt->load()==Pending)*receipt=Failed;if(!observer||observer->m_closed)return;--observer->m_pending;if(ticket==observer->m_operation_id)observer->m_error=receipt->load()==Applied?tr("The preferences write completed, but its result could not be published."):tr("UI preferences write was rejected.");observer->notifyViews();Q_EMIT observer->stateChanged();},action);
    if(!admitted&&receipt->load()==Pending)*receipt=Failed;
    if(observer&&!m_closed){notifyViews();Q_EMIT stateChanged();}return admitted?ticket:0;
}
void UiPreferencesStore::publish(std::shared_ptr<const Result> result,quint64 ticket){
    --m_pending;if(result->revision>=m_revision){m_revision=result->revision;m_result=std::move(result);m_ready=m_result->loaded;m_watch.update(m_result->watch);if(ticket==m_operation_id)m_error=m_result->error;}
    const QPointer<UiPreferencesStore> guard{this};notifyViews();if(guard&&!m_closed)Q_EMIT stateChanged();if(guard&&!m_closed)finishRefresh();
}
void UiPreferencesStore::close(){if(m_closed)return;m_watch.stop();m_closed=true;m_pending=0;m_work->close();m_result.reset();m_native.reset();}
void UiPreferencesStore::whenDrained(std::function<void()> done){close();m_work->close(std::move(done));}
bool UiPreferencesStore::attach(AppPreferences* view){
    if(m_closed)return false;
    const auto id=++m_next_view;m_views.emplace(id,view);
    connect(view,&QObject::destroyed,this,[this,id]{m_views.erase(id);});return true;
}
void UiPreferencesStore::notifyViews(){if(m_closed)return;if(m_notify_scheduled){m_notify_again=true;return;}m_notify_cursor=0;m_notify_scheduled=true;const QPointer<UiPreferencesStore> guard{this};try{m_services.post([guard]{if(guard)guard->notifyTurn();});}catch(...){m_notify_scheduled=false;}}
void UiPreferencesStore::notifyTurn(){
    if(m_closed){m_notify_scheduled=false;return;}const QPointer<UiPreferencesStore> guard{this};int count{0};
    while(count++<32){const auto it=m_views.upper_bound(m_notify_cursor);if(it==m_views.end()){m_notify_scheduled=false;if(std::exchange(m_notify_again,false))notifyViews();return;}m_notify_cursor=it->first;const auto view=it->second;if(view)Q_EMIT view->changed();if(!guard||m_closed)return;}
    try{m_services.post([guard]{if(guard)guard->notifyTurn();});}catch(...){m_notify_scheduled=false;}
}
void AppPreferences::componentComplete(){if(auto* context=qmlContext(this))configure(qobject_cast<UiPreferencesStore*>(context->contextProperty(QStringLiteral("uiPreferencesStore")).value<QObject*>()));}
void AppPreferences::configure(UiPreferencesStore* store){if(m_store||!store)return;m_store=store;if(!store->attach(this))m_store=nullptr;Q_EMIT changed();}
#define PREF_SET(Type,Name,Condition) void AppPreferences::set_##Name(Type value){if(!m_store)return;m_store->write({QStringLiteral(#Name)},[value](auto& values){if(!(Condition))throw std::invalid_argument("Invalid UI preference");values.Name=value;});}
PREF_SET(bool,dark,true)
PREF_SET(double,blockclocksize,std::isfinite(value)&&value>0&&value<=1)
PREF_SET(int,trafficGraphScale,ValidScale(value))
PREF_SET(bool,peerListSortAscending,true)
#undef PREF_SET
#define PREF_TEXT(Name,Condition) void AppPreferences::set_##Name(QString value){if(!m_store)return;if(value.size()>64){m_store->rejectIntent(tr("The preference value is too long."));return;}m_store->write({QStringLiteral(#Name)},[value](auto& values){if(!(Condition))throw std::invalid_argument("Invalid UI preference");values.Name=value;});}
PREF_TEXT(activityGrouping,value=="day"||value=="month")
PREF_TEXT(activityDisplayDensity,value=="comfortable"||value=="compact")
PREF_TEXT(peerListSortBy,ValidSort(value))
#undef PREF_TEXT
bool AppPreferences::restoreWindow(QWindow* window) const
{
    if (!ready() || !window) return false;
    std::array<QRect, 32> screens;
    size_t count{0};
    // Qt screen acquisition and the final native window mutation belong on GUI.
    // The fixed-size policy is pure and independently unit tested.
    if (const auto* primary = QGuiApplication::primaryScreen()) screens[count++] = primary->availableGeometry();
    for (const auto* screen : QGuiApplication::screens()) {
        if (count == screens.size()) break;
        if (screen != QGuiApplication::primaryScreen()) screens[count++] = screen->availableGeometry();
    }
    const auto& stored = values();
    const QRectF saved{stored.windowX, stored.windowY, stored.windowWidth, stored.windowHeight};
    const auto restored = windowgeometry::Restore(saved, window->minimumSize(), std::span{screens.data(), count}, window->geometry());
    window->setGeometry(restored);
    return true;
}

quint64 AppPreferences::requestGeometry(double x,double y,double width,double height){
    if(!m_store)return 0;
    return m_store->write({QStringLiteral("windowX"),QStringLiteral("windowY"),QStringLiteral("windowWidth"),QStringLiteral("windowHeight")},[x,y,width,height](auto& values){
        if(!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(width)||!std::isfinite(height)||width<=0||height<=0||width>std::numeric_limits<int>::max()||height>std::numeric_limits<int>::max()||x<std::numeric_limits<int>::min()||x>std::numeric_limits<int>::max()||y<std::numeric_limits<int>::min()||y>std::numeric_limits<int>::max())throw std::invalid_argument("Invalid window geometry");
        values.windowX=x;values.windowY=y;values.windowWidth=width;values.windowHeight=height;
    });
}
quint64 AppPreferences::togglePeerFilter(QString group,QString id){
    if(!m_store)return 0;
    if(group.size()>32||id.size()>32)return m_store->rejectIntent(tr("The peer filter is invalid."));
    const auto key=FilterKey(group);if(key.isEmpty())return m_store->rejectIntent(tr("The peer filter group is invalid."));
    return m_store->write({key},[group,id](auto& values){const auto& allowed=Allowed(group);if(!allowed.contains(id))throw std::invalid_argument("Invalid peer filter");auto& selected=Filters(values,group);
        const bool had=selected.contains(id);if(group=="direction"){selected=had?QStringList{}:QStringList{id};return;}
        if(had)selected.removeAll(id);else selected<<id;if(selected.size()==allowed.size())selected.clear();
    });
}
quint64 AppPreferences::clearPeerFilters(){if(!m_store)return 0;return m_store->write({QStringLiteral("peerDirectionFilters"),QStringLiteral("peerConnectionTypeFilters"),QStringLiteral("peerNetworkFilters"),QStringLiteral("peerTransportFilters")},[](auto& values){values.peerDirectionFilters.clear();values.peerConnectionTypeFilters.clear();values.peerNetworkFilters.clear();values.peerTransportFilters.clear();});}

quint64 AppPreferences::clearPeerFilter(QString group){if(!m_store)return 0;if(group.size()>32)return m_store->rejectIntent(tr("The peer filter group is invalid."));const auto key=FilterKey(group);if(key.isEmpty())return m_store->rejectIntent(tr("The peer filter group is invalid."));return m_store->write({key},[group](auto& values){Filters(values,group).clear();});}

quint64 UiPreferencesStore::rejectIntent(QString error){if(m_closed)return 0;m_error=std::move(error);notifyViews();Q_EMIT stateChanged();return 0;}
