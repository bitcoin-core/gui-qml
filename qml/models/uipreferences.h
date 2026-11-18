// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_UIPREFERENCES_H
#define BITCOIN_QML_MODELS_UIPREFERENCES_H
#include <qml/ownedwork.h>
#include <qml/settingsnamespace.h>
#include <qml/preferenceswatch.h>
#include <QObject>
#include <QPointer>
#include <QQmlParserStatus>
#include <QSettings>
#include <QWindow>
#include <QStringList>
#include <atomic>
#include <map>
class AppPreferences;
struct UiPreferencesValues {
    bool dark{true};double blockclocksize{5.0/12.0};
    double windowX{0},windowY{0},windowWidth{0},windowHeight{0};
    QString activityGrouping{QStringLiteral("month")},activityDisplayDensity{QStringLiteral("comfortable")};
    int trafficGraphScale{300};QString peerListSortBy{QStringLiteral("nodeId")};bool peerListSortAscending{true};
    QStringList peerDirectionFilters,peerConnectionTypeFilters,peerNetworkFilters,peerTransportFilters;
};
class UiPreferencesStore final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool ready READ ready NOTIFY stateChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY stateChanged)
    Q_PROPERTY(QString error READ error NOTIFY stateChanged)
public:
    enum Outcome {Idle,Pending,Applied,Failed,Uncertain,Closed};Q_ENUM(Outcome)
    UiPreferencesStore(execution::SnapshotServices,UiPreferencesNamespace,QObject* parent=nullptr);
    ~UiPreferencesStore() override;
    void start();
    Q_INVOKABLE void retry();
    Q_INVOKABLE void refresh();
    bool ready()const{return m_ready;}bool pending()const{return m_pending!=0;}
    QString error()const{return m_error;}
    const UiPreferencesValues& values()const;
    void closeAdmission();
    void whenDrained(std::function<void()>);
    std::shared_ptr<const std::atomic<Outcome>> lastWriteReceipt()const{return m_receipt;}
    quint64 operationId()const{return m_operation_id;}
Q_SIGNALS:
    void stateChanged();
private:
    friend class AppPreferences;
    struct Native;struct Result;
    using Mutation=std::function<void(UiPreferencesValues&)>;
    quint64 write(QStringList keys,Mutation);
    quint64 rejectIntent(QString error);
    void finishRefresh();
    preferences::Watch m_watch{[this] { refresh(); }};
    bool m_refresh_requested{false}, m_loading{false};
    void load();void publish(std::shared_ptr<const Result>,quint64);
    bool attach(AppPreferences*);void notifyViews();void notifyTurn();
    execution::SnapshotServices m_services;
    std::unique_ptr<execution::OwnedWork> m_work;
    std::shared_ptr<Native> m_native;
    std::shared_ptr<const Result> m_result;
    UiPreferencesValues m_defaults;
    execution::ResourceKey m_key{};
    std::map<quint64,QPointer<AppPreferences>> m_views;
    std::shared_ptr<std::atomic<Outcome>> m_receipt;
    quint64 m_next_view{0},m_notify_cursor{0},m_revision{0},m_operation_id{0};
    size_t m_pending{0};bool m_ready{false},m_closed{false},m_notify_scheduled{false},m_notify_again{false};QString m_error;
};
/** Fixed-schema, per-view presentation facade; no Settings instance or executor. */
class AppPreferences : public QObject,public QQmlParserStatus
{
    Q_OBJECT
    Q_INTERFACES(QQmlParserStatus)
#define UI_PREF_PROPERTY(Type,Name) Q_PROPERTY(Type Name READ Name WRITE set_##Name NOTIFY changed)
    UI_PREF_PROPERTY(bool,dark)
    UI_PREF_PROPERTY(double,blockclocksize)
    Q_PROPERTY(double windowX READ windowX NOTIFY changed)
    Q_PROPERTY(double windowY READ windowY NOTIFY changed)
    Q_PROPERTY(double windowWidth READ windowWidth NOTIFY changed)
    Q_PROPERTY(double windowHeight READ windowHeight NOTIFY changed)
    UI_PREF_PROPERTY(QString,activityGrouping)
    UI_PREF_PROPERTY(QString,activityDisplayDensity)
    UI_PREF_PROPERTY(int,trafficGraphScale)
    UI_PREF_PROPERTY(QString,peerListSortBy)
    UI_PREF_PROPERTY(bool,peerListSortAscending)
    Q_PROPERTY(QStringList peerDirectionFilters READ peerDirectionFilters NOTIFY changed)
    Q_PROPERTY(QStringList peerConnectionTypeFilters READ peerConnectionTypeFilters NOTIFY changed)
    Q_PROPERTY(QStringList peerNetworkFilters READ peerNetworkFilters NOTIFY changed)
    Q_PROPERTY(QStringList peerTransportFilters READ peerTransportFilters NOTIFY changed)
#undef UI_PREF_PROPERTY
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
public:
    explicit AppPreferences(QObject* parent=nullptr):QObject(parent){}
    void classBegin() override{}void componentComplete()override;
    void configure(UiPreferencesStore*);
    bool ready()const{return m_store&&m_store->ready();}bool pending()const{return m_store&&m_store->pending();}
    QString error()const{return m_store?m_store->error():QStringLiteral("Preferences store is unavailable.");}
#define UI_PREF_ACCESS(Type,Name) Type Name()const{return values().Name;}void set_##Name(Type value);
    UI_PREF_ACCESS(bool,dark) UI_PREF_ACCESS(double,blockclocksize)
    UI_PREF_ACCESS(QString,activityGrouping) UI_PREF_ACCESS(QString,activityDisplayDensity)
    UI_PREF_ACCESS(int,trafficGraphScale) UI_PREF_ACCESS(QString,peerListSortBy) UI_PREF_ACCESS(bool,peerListSortAscending)
#undef UI_PREF_ACCESS
    double windowX()const{return values().windowX;}double windowY()const{return values().windowY;}
    double windowWidth()const{return values().windowWidth;}double windowHeight()const{return values().windowHeight;}
    QStringList peerDirectionFilters()const{return values().peerDirectionFilters;}
    QStringList peerConnectionTypeFilters()const{return values().peerConnectionTypeFilters;}
    QStringList peerNetworkFilters()const{return values().peerNetworkFilters;}
    QStringList peerTransportFilters()const{return values().peerTransportFilters;}
    Q_INVOKABLE bool restoreWindow(QWindow* window) const;
    Q_INVOKABLE void refresh(){if(m_store)m_store->refresh();}
    Q_INVOKABLE quint64 requestGeometry(double x,double y,double width,double height);
    Q_INVOKABLE quint64 togglePeerFilter(QString group,QString id);
    Q_INVOKABLE quint64 clearPeerFilters();
    Q_INVOKABLE quint64 clearPeerFilter(QString group);
    Q_INVOKABLE void retry(){if(m_store)m_store->retry();}
Q_SIGNALS:void changed();
private:
    friend class UiPreferencesStore;
    const UiPreferencesValues& values()const{return m_store?m_store->values():m_defaults;}
    QPointer<UiPreferencesStore> m_store;UiPreferencesValues m_defaults;
};
#endif // BITCOIN_QML_MODELS_UIPREFERENCES_H
