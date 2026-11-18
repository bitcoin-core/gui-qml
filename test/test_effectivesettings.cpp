// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/effectivesettings.h>
#include <qml/core_settings.h>
#include <common/args.h>
#include <common/settings.h>
#include <init.h>
#include <test/mocks/mocknode.h>
#include <test/qt_test_registry.h>
#include <QFile>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QtTest/QtTest>
#include <stdexcept>
#include <limits>
#include <thread>
#include <atomic>
#include <QScopeGuard>
#include <vector>
namespace {
using namespace effectivesettings;
struct Profile {
    QTemporaryDir directory;
    ArgsManager args;
    MockNode node;
    explicit Profile(const QStringList& options = {}, bool initialize_file = true) {
        if (!directory.isValid()) throw std::runtime_error("Settings fixture unavailable");
        SetupServerArgs(args);
        std::vector<std::string> values{"r33-test","-regtest","-datadir="+directory.path().toStdString(),"-settings="+directory.filePath("settings.json").toStdString()};
        for(const auto& option:options)values.push_back(option.toStdString());
        std::vector<const char*> argv;argv.reserve(values.size());for(const auto& value:values)argv.push_back(value.c_str());std::string error;
        if(!args.ParseParameters(int(argv.size()),argv.data(),error))throw std::runtime_error(error);
        args.SelectConfigNetwork("regtest");
        if(initialize_file&&!args.WriteSettingsFile())throw std::runtime_error("Initial settings fixture write failed");
    }
    void config(const std::string& key,const common::SettingsValue& value) {args.LockSettings([&](common::Settings& settings){settings.ro_config["regtest"][key]={value};});}
    QByteArray bytes() const {QFile file{directory.filePath("settings.json")};if(!file.open(QIODevice::ReadOnly))throw std::runtime_error("Settings fixture read failed");return file.readAll();}
    QJsonObject disk() const {return QJsonDocument::fromJson(bytes()).object();}
};
Batch Submit(const Snapshot& snapshot,std::initializer_list<Edit> edits) {return {snapshot.revision,std::vector<Edit>{edits},false};}
}
class EffectiveSettingsTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void reviewHasNoPersistenceOrRuntimeEffects_data(){QTest::addColumn<bool>("valid");QTest::newRow("valid")<<true;QTest::newRow("invalid-complete-policy")<<false;}
    void reviewHasNoPersistenceOrRuntimeEffects(){QFETCH(bool,valid);Profile profile;if(!valid)profile.config("limitclustersize",int64_t{250});const auto original=Read(profile.args);const auto bytes=profile.bytes();const auto result=Review(profile.args,original,Submit(original,{{Key::Natpmp,true},{Key::Mempool,int64_t{9}}}));QCOMPARE(result.valid,valid);QCOMPARE(profile.bytes(),bytes);QVERIFY(profile.args.GetPersistentSetting("natpmp").isNull());QCOMPARE(profile.node.calls.mapPort.load(),0);QCOMPARE(profile.node.calls.forceSetting.load(),0);}
    void untouchedCommandLinePolicyDoesNotInventPersistenceIntent(){Profile profile{{"-listen=0"}};const auto bytes=profile.bytes();const auto result=ReviewCurrent(profile.args);QVERIFY2(result.valid,qPrintable(result.error));QCOMPARE(profile.bytes(),bytes);QVERIFY(profile.args.GetPersistentSetting("listen").isNull());QVERIFY(!Find(result.snapshot,Key::Listen)->editable);}
    void brokenUneditedSignerDoesNotBlockNodeSettings(){Profile profile;profile.config("signer",std::string{"/missing/unavailable-signer"});const auto original=Read(profile.args);const auto result=Commit(profile.node,profile.args,original,Submit(original,{{Key::Natpmp,true}}));QCOMPARE(result.outcome,Outcome::Applied);QVERIFY(profile.disk().value("natpmp").toBool());}
    void unpruningRequiresAnExplicitRecoveryAcknowledgement(){Profile profile;QmlCoreSettings::SetRwSetting(profile.args,"prune",int64_t{550});QVERIFY(profile.args.WriteSettingsFile());const auto original=Read(profile.args);const auto bytes=profile.bytes();auto batch=Submit(original,{{Key::Prune,int64_t{0}}});const auto rejected=Commit(profile.node,profile.args,original,batch);QCOMPARE(rejected.outcome,Outcome::Rejected);QVERIFY(rejected.validation.requires_redownload);QCOMPARE(profile.bytes(),bytes);batch.accept_redownload=true;const auto result=Commit(profile.node,profile.args,original,batch);QCOMPARE(result.outcome,Outcome::Applied);QCOMPARE(profile.disk().value("prune").toString(),QStringLiteral("0"));QCOMPARE(profile.disk().value("prune-prev").toString(),QStringLiteral("550"));}
    // Valera: Core clamps -dbcache to its minimum, so -dbcache=0 starts. That
    // command-line value must not block saving an unrelated setting.
    void commandLineDbcacheBelowTheGuiMinimumDoesNotBlockOtherSettings(){Profile profile{{"-dbcache=0"}};const auto original=Read(profile.args);const auto result=Commit(profile.node,profile.args,original,Submit(original,{{Key::Natpmp,true}}));QVERIFY2(result.outcome==Outcome::Applied,qPrintable(result.error));QVERIFY(profile.disk().value("natpmp").toBool());QVERIFY(!profile.disk().contains("dbcache"));}
    void editedDbcacheBelowTheMinimumIsRejected(){Profile profile;const auto original=Read(profile.args);const auto result=Commit(profile.node,profile.args,original,Submit(original,{{Key::Dbcache,int64_t{2}}}));QCOMPARE(result.outcome,Outcome::Rejected);QVERIFY(result.validation.errors.contains(Key::Dbcache));}
    void wholeBatchIsValidatedBeforeAnyMutation() {
        Profile profile;const auto base=Read(profile.args);QVERIFY(base.ready);const auto bytes=profile.bytes();
        const auto result=Commit(profile.node,profile.args,base,Submit(base,{{Key::Natpmp,false},{Key::Proxy,QStringLiteral("999.999.999.999:9050")}}));
        QCOMPARE(result.outcome,Outcome::Rejected);QVERIFY(!result.persisted);QVERIFY(result.validation.errors.contains(Key::Proxy));
        QCOMPARE(profile.bytes(),bytes);QVERIFY(profile.args.GetPersistentSetting("natpmp").isNull());QVERIFY(profile.args.GetPersistentSetting("proxy").isNull());QCOMPARE(profile.node.calls.mapPort.load(),0);
    }
    void startupConstraintFamilies_data() {
        QTest::addColumn<int>("scenario");QTest::addColumn<bool>("valid");
        QTest::newRow("bind-listen-off")<<0<<false;QTest::newRow("whitebind-listen-off")<<1<<false;
        QTest::newRow("explicit-listenonion-listen-off")<<2<<false;QTest::newRow("onlynet-onion-explicitly-forbidden")<<3<<false;
        QTest::newRow("onlynet-onion-route-absent")<<4<<false;QTest::newRow("cluster250-mempool9")<<5<<false;
        QTest::newRow("cluster250-mempool10")<<6<<true;QTest::newRow("default-policy-mempool9")<<7<<true;
        QTest::newRow("unlimited-mempool")<<8<<true;QTest::newRow("prune-txindex")<<9<<false;
        QTest::newRow("prune-reindex-chainstate")<<10<<false;QTest::newRow("manual-pruning")<<11<<true;
        QTest::newRow("automatic-prune550")<<12<<true;QTest::newRow("automatic-prune549")<<13<<false;
        QTest::newRow("valid-onion-route")<<14<<true;QTest::newRow("no-bind-listen-off")<<15<<true;
    }
    void startupConstraintFamilies() {
        QFETCH(int,scenario);QFETCH(bool,valid);Profile profile;Edit edit{Key::Listen,false};
        if(scenario==0)profile.config("bind",std::string{"127.0.0.1"});
        if(scenario==1)profile.config("whitebind",std::string{"127.0.0.1:18444"});
        if(scenario==2)profile.config("listenonion",true);
        if(scenario==3||scenario==4||scenario==14){profile.config("onlynet",std::string{"onion"});profile.config("listenonion",false);edit={scenario==3?Key::Onion:Key::Proxy,scenario==14?QStringLiteral("127.0.0.1:9050"):QStringLiteral("0")};}
        if(scenario>=5&&scenario<=8){if(scenario<=6)profile.config("limitclustersize",int64_t{250});edit={Key::Mempool,int64_t{scenario==6?10:scenario==8?0:9}};}
        if(scenario>=9&&scenario<=13){if(scenario==9)profile.config("txindex",true);if(scenario==10)profile.config("reindex-chainstate",true);edit={Key::Prune,int64_t{scenario==11?1:scenario==13?549:550}};}
        const auto base=Read(profile.args);QVERIFY2(base.ready,qPrintable(base.error));const auto bytes=profile.bytes();
        const auto result=Commit(profile.node,profile.args,base,Submit(base,{edit}));QCOMPARE(result.outcome,valid?Outcome::Applied:Outcome::Rejected);
        QCOMPARE(result.persisted,valid);if(!valid)QCOMPARE(profile.bytes(),bytes);
    }
    // #1074: with name lookup disabled, Core cannot resolve a proxy or onion
    // hostname at the next start, so the editor must not save one. With DNS
    // on, resolution happens at startup; a failure there reopens setup.
    void unresolvableProxyHostCannotBeSavedForTheNextStart_data(){
        QTest::addColumn<int>("key");QTest::addColumn<bool>("dns");QTest::addColumn<QString>("value");QTest::addColumn<bool>("accepted");
        QTest::newRow("proxy-hostname-dns-off")<<int(Key::Proxy)<<false<<QStringLiteral("nosuchhost.invalid:9050")<<false;
        QTest::newRow("onion-hostname-dns-off")<<int(Key::Onion)<<false<<QStringLiteral("nosuchhost.invalid:9050")<<false;
        QTest::newRow("proxy-default-port-hostname-dns-off")<<int(Key::Proxy)<<false<<QStringLiteral("localhost")<<false;
        QTest::newRow("proxy-numeric-dns-off")<<int(Key::Proxy)<<false<<QStringLiteral("127.0.0.1:9050")<<true;
        QTest::newRow("onion-numeric-dns-off")<<int(Key::Onion)<<false<<QStringLiteral("[::1]:9050")<<true;
        QTest::newRow("proxy-hostname-dns-on")<<int(Key::Proxy)<<true<<QStringLiteral("nosuchhost.invalid:9050")<<true;
        QTest::newRow("onion-hostname-dns-on")<<int(Key::Onion)<<true<<QStringLiteral("nosuchhost.invalid:9050")<<true;
    }
    void unresolvableProxyHostCannotBeSavedForTheNextStart(){
        QFETCH(int,key);QFETCH(bool,dns);QFETCH(QString,value);QFETCH(bool,accepted);Profile profile;profile.config("dns",dns);const auto field=static_cast<Key>(key);const auto name=Name(field);
        const auto original=Read(profile.args);QVERIFY2(original.ready,qPrintable(original.error));const auto bytes=profile.bytes();
        const auto reviewed=Review(profile.args,original,Submit(original,{{field,value}}));QCOMPARE(reviewed.valid,accepted);QCOMPARE(profile.bytes(),bytes);
        const auto result=Commit(profile.node,profile.args,original,Submit(original,{{field,value}}));QCOMPARE(result.outcome,accepted?Outcome::Applied:Outcome::Rejected);QCOMPARE(result.persisted,accepted);
        if(!accepted){QVERIFY(result.validation.errors.contains(field));QCOMPARE(profile.bytes(),bytes);QVERIFY(profile.args.GetPersistentSetting(name.toStdString()).isNull());return;}
        QCOMPARE(profile.disk().value(name).toString(),value);QVERIFY(ReviewCurrent(profile.args).valid);
    }
    // #594: a per-network proxy from bitcoin.conf survives every editor edit
    // that would negate, broaden or rewrite it; unrelated edits keep Core's list.
    void perNetworkProxySurvivesEveryEditorPath_data(){
        QTest::addColumn<int>("scenario");QTest::addColumn<bool>("accepted");
        QTest::newRow("switch-off")<<0<<false;QTest::newRow("plain-address-broadens")<<1<<false;QTest::newRow("resave-shown-entry")<<2<<false;
        QTest::newRow("tor-proxy-edit")<<3<<true;QTest::newRow("reset-to-configuration")<<4<<true;
    }
    void perNetworkProxySurvivesEveryEditorPath(){
        QFETCH(int,scenario);QFETCH(bool,accepted);Profile profile;profile.config("proxy",std::string{"127.0.0.1:9050=onion"});
        const auto original=Read(profile.args);QVERIFY(original.ready);QVERIFY(!Find(original,Key::Proxy)->editable);const auto native=profile.args.GetArgs("-proxy");const auto bytes=profile.bytes();
        Edit edit{Key::Proxy,QStringLiteral("0")};
        if(scenario==1)edit={Key::Proxy,QStringLiteral("127.0.0.2:9051")};
        if(scenario==2)edit={Key::Proxy,QStringLiteral("127.0.0.1:9050=onion")};
        if(scenario==3)edit={Key::Onion,QStringLiteral("127.0.0.1:9150")};
        if(scenario==4)edit={Key::Proxy,QString{},Intent::RemoveOverride};
        const auto result=Commit(profile.node,profile.args,original,Submit(original,{edit}));QCOMPARE(result.outcome,accepted?Outcome::Applied:Outcome::Rejected);
        if(!accepted){QVERIFY(result.validation.errors.contains(Key::Proxy));QVERIFY(!result.write_completed);QCOMPARE(profile.bytes(),bytes);}
        QVERIFY(!profile.disk().contains("proxy"));QVERIFY(!profile.disk().contains("proxy-prev"));QCOMPARE(profile.args.GetArgs("-proxy"),native);
        const auto after=Read(profile.args);QVERIFY(after.ready);const auto* proxy=Find(after,Key::Proxy);QVERIFY(!proxy->editable);QVERIFY(proxy->information.contains(QStringLiteral("127.0.0.1:9050=onion")));
        QVERIFY(ReviewCurrent(profile.args).valid);
    }
    void capturedRevisionOwnsEveryProjectedField(){
        Profile profile;const auto set=[&](bool first){profile.args.LockSettings([&](common::Settings& values){values.ro_config["regtest"]["listen"]={first};values.ro_config["regtest"]["maxmempool"]={int64_t{first?111:222}};});};
        set(true);const auto first=Read(profile.args);set(false);const auto second=Read(profile.args);QVERIFY(first.ready&&second.ready);QVERIFY(first.revision!=second.revision);
        std::atomic_bool stop{false};std::atomic_int writes{0};std::thread writer{[&]{while(!stop.load()){set(writes.load()%2==0);++writes;std::this_thread::yield();}}};const auto cleanup=qScopeGuard([&]{stop=true;if(writer.joinable())writer.join();});QTRY_VERIFY(writes.load()>16);
        QString failure;std::thread reader{[&]{try{for(int i=0;i<256&&failure.isEmpty();++i){const auto frame=Read(profile.args);if(!frame.ready){failure=frame.error;break;}const auto* expected=frame.revision==first.revision?&first:frame.revision==second.revision?&second:nullptr;if(!expected){failure=QStringLiteral("Read returned an unknown captured version");break;}for(const auto& field:expected->fields){const auto* actual=Find(frame,field.key);if(!actual||actual->effective!=field.effective||actual->next_start!=field.next_start||actual->committed!=field.committed){failure=QStringLiteral("Captured revision %1 projected another configuration's values").arg(frame.revision);break;}}}}catch(const std::exception& error){failure=QString::fromUtf8(error.what());}catch(...){failure=QStringLiteral("Unexpected Native snapshot exception");}stop=true;}};reader.join();stop=true;writer.join();QVERIFY2(failure.isEmpty(),qPrintable(failure));
    }
    void unsupportedProxyListsStayVisibleAndCannotBeErased_data(){QTest::addColumn<bool>("multiple");QTest::newRow("one-network-route")<<false;QTest::newRow("multiple-network-routes")<<true;}
    void unsupportedProxyListsStayVisibleAndCannotBeErased(){QFETCH(bool,multiple);Profile profile;profile.args.LockSettings([&](common::Settings& values){values.ro_config["regtest"]["proxy"]={std::string{"127.0.0.1:9050=ipv4"}};if(multiple)values.ro_config["regtest"]["proxy"].emplace_back("127.0.0.1:9150=onion");});const auto original=Read(profile.args);const auto* proxy=Find(original,Key::Proxy);QVERIFY(proxy);QVERIFY(!proxy->editable);QVERIFY(proxy->information.contains(QStringLiteral("127.0.0.1:9050=ipv4")));if(multiple)QVERIFY(proxy->information.contains(QStringLiteral("127.0.0.1:9150=onion")));const auto native=profile.args.GetArgs("-proxy");const auto bytes=profile.bytes();const auto result=Commit(profile.node,profile.args,original,Submit(original,{{Key::Proxy,QString{}}}));QCOMPARE(result.outcome,Outcome::Rejected);QVERIFY(!result.write_completed);QCOMPARE(profile.bytes(),bytes);QVERIFY(!profile.disk().contains("proxy"));QCOMPARE(profile.args.GetArgs("-proxy"),native);}
    void structuredProxyConfigurationAllowsOtherSettings_data(){QTest::addColumn<bool>("multiple");QTest::newRow("per-network")<<false;QTest::newRow("ordered-list")<<true;}
    void structuredProxyConfigurationAllowsOtherSettings(){QFETCH(bool,multiple);Profile profile;profile.args.LockSettings([&](common::Settings& values){auto& proxies=values.ro_config["regtest"]["proxy"];proxies={std::string{"127.0.0.1:9050=ipv4"}};if(multiple){proxies.emplace_back("0=onion");proxies.emplace_back("127.0.0.1:9150=onion");}});const auto original=Read(profile.args);QVERIFY(original.ready);QVERIFY(!Find(original,Key::Proxy)->editable);const auto routes=profile.args.GetArgs("-proxy");const auto reviewed=ReviewCurrent(profile.args);QVERIFY2(reviewed.valid,qPrintable(reviewed.error));const auto result=Commit(profile.node,profile.args,original,Submit(original,{{Key::Natpmp,true}}));QCOMPARE(result.outcome,Outcome::Applied);QVERIFY(profile.disk().value("natpmp").toBool());QVERIFY(!profile.disk().contains("proxy"));QCOMPARE(profile.args.GetArgs("-proxy"),routes);QCOMPARE(profile.node.calls.mapPort.load(),1);}
    void onlyOnionUsesTheOrderedNetworkRoute_data(){QTest::addColumn<QStringList>("routes");QTest::addColumn<QString>("onion");QTest::addColumn<bool>("dns");QTest::addColumn<bool>("accepted");QTest::newRow("ipv4-is-not-onion")<<QStringList{"127.0.0.1:9050=ipv4"}<<QString{}<<true<<false;QTest::newRow("explicit-onion")<<QStringList{"127.0.0.1:9050=onion"}<<QString{}<<true<<true;QTest::newRow("later-onion-off")<<QStringList{"127.0.0.1:9050","0=onion"}<<QString{}<<true<<false;QTest::newRow("dedicated-onion-override")<<QStringList{"127.0.0.1:9050","0=onion"}<<QStringLiteral("127.0.0.1:9150")<<true<<true;QTest::newRow("dedicated-onion-off")<<QStringList{"127.0.0.1:9050=onion"}<<QStringLiteral("0")<<true<<false;QTest::newRow("ipv4-off-keeps-onion")<<QStringList{"127.0.0.1:9050","0=ipv4"}<<QString{}<<true<<true;QTest::newRow("unsupported-network")<<QStringList{"127.0.0.1:9050=i2p"}<<QString{}<<true<<false;QTest::newRow("hostname-needs-dns")<<QStringList{"example.invalid:9050=onion"}<<QString{}<<false<<false;}
    void onlyOnionUsesTheOrderedNetworkRoute(){QFETCH(QStringList,routes);QFETCH(QString,onion);QFETCH(bool,dns);QFETCH(bool,accepted);Profile profile;profile.config("onlynet",std::string{"onion"});profile.config("listen",false);profile.config("listenonion",false);profile.config("dns",dns);if(!onion.isEmpty())profile.config("onion",onion.toStdString());profile.args.LockSettings([&](common::Settings& values){auto& proxies=values.ro_config["regtest"]["proxy"];for(const auto& route:routes)proxies.emplace_back(route.toStdString());});const auto original=Read(profile.args);const auto before=profile.bytes();const auto result=Commit(profile.node,profile.args,original,Submit(original,{{Key::Natpmp,true}}));QCOMPARE(result.outcome,accepted?Outcome::Applied:Outcome::Rejected);QCOMPARE(result.persisted,accepted);QVERIFY(!profile.disk().contains("proxy"));if(!accepted){QCOMPARE(profile.bytes(),before);QCOMPARE(profile.node.calls.mapPort.load(),0);}}
    void nativeScalarLimitsRejectTheWholeBatchBeforeEffects_data(){QTest::addColumn<int>("key");QTest::addColumn<qint64>("value");QTest::newRow("mempool-byte-overflow")<<int(Key::Mempool)<<qint64{std::numeric_limits<int64_t>::max()/1'000'000+1};QTest::newRow("prune-byte-overflow")<<int(Key::Prune)<<qint64{static_cast<int64_t>(std::numeric_limits<uint64_t>::max()/(1024*1024))+1};QTest::newRow("thread-narrowing")<<int(Key::Threads)<<qint64{int64_t{std::numeric_limits<int>::max()}+1};}
    void nativeScalarLimitsRejectTheWholeBatchBeforeEffects(){QFETCH(int,key);QFETCH(qint64,value);Profile profile;const auto original=Read(profile.args);const auto bytes=profile.bytes();const auto result=Commit(profile.node,profile.args,original,Submit(original,{{Key::Natpmp,true},{static_cast<Key>(key),int64_t{value}}}));QCOMPARE(result.outcome,Outcome::Rejected);QVERIFY(result.validation.errors.contains(static_cast<Key>(key)));QVERIFY(!result.write_completed);QCOMPARE(profile.bytes(),bytes);QCOMPARE(profile.node.calls.mapPort.load(),0);QVERIFY(profile.args.GetPersistentSetting("natpmp").isNull());}
    void nativeScalarBoundaryChecksDoNotAllocateOrWrite_data(){QTest::addColumn<int>("key");QTest::addColumn<qint64>("value");QTest::newRow("mempool-last-representable")<<int(Key::Mempool)<<qint64{std::numeric_limits<int64_t>::max()/1'000'000};QTest::newRow("prune-last-representable")<<int(Key::Prune)<<qint64{static_cast<int64_t>(std::numeric_limits<uint64_t>::max()/(1024*1024))};QTest::newRow("thread-last-representable")<<int(Key::Threads)<<qint64{std::numeric_limits<int>::max()};}
    void nativeScalarBoundaryChecksDoNotAllocateOrWrite(){QFETCH(int,key);QFETCH(qint64,value);Profile profile;const auto original=Read(profile.args);const auto bytes=profile.bytes();const auto checked=Review(profile.args,original,Submit(original,{{static_cast<Key>(key),int64_t{value}}}));QVERIFY2(checked.valid,qPrintable(checked.error));QCOMPARE(profile.bytes(),bytes);QCOMPARE(profile.node.calls.mapPort.load(),0);QCOMPARE(profile.node.calls.forceSetting.load(),0);}
    void malformedBatchIsAnEffectFreeRejection_data() {QTest::addColumn<int>("scenario");QTest::newRow("wrong-type")<<0;QTest::newRow("duplicate-field")<<1;QTest::newRow("oversized-text")<<2;QTest::newRow("empty-batch")<<3;}
    void malformedBatchIsAnEffectFreeRejection() {
        QFETCH(int,scenario);Profile profile;const auto base=Read(profile.args);auto batch=Submit(base,{{Key::Listen,int64_t{1}}});
        if(scenario==1)batch=Submit(base,{{Key::Listen,false},{Key::Listen,true}});
        if(scenario==2)batch=Submit(base,{{Key::Signer,QString(32769,'x')}});
        if(scenario==3)batch.edits.clear();
        const auto bytes=profile.bytes();
        const auto result=Commit(profile.node,profile.args,base,batch);QCOMPARE(result.outcome,Outcome::Rejected);QVERIFY(!result.persisted);QCOMPARE(profile.bytes(),bytes);QCOMPARE(profile.node.calls.mapPort.load(),0);QCOMPARE(profile.node.calls.forceSetting.load(),0);
    }
    void commandLineValueIsReadOnly() {
        Profile profile{{"-listen=1"}};const auto base=Read(profile.args);QVERIFY(!Find(base,Key::Listen)->editable);const auto bytes=profile.bytes();
        const auto result=Commit(profile.node,profile.args,base,Submit(base,{{Key::Listen,false}}));QCOMPARE(result.outcome,Outcome::Rejected);QCOMPARE(profile.bytes(),bytes);
    }
    void explicitEqualValueStillPersistsAnOverride() {
        Profile profile;const auto base=Read(profile.args);QVERIFY(!Find(base,Key::Listen)->has_override);
        const auto result=Commit(profile.node,profile.args,base,Submit(base,{{Key::Listen,true}}));QCOMPARE(result.outcome,Outcome::Applied);QVERIFY(result.persisted);QVERIFY(result.runtime_confirmed);
        QVERIFY(profile.disk().value("listen").toBool());QVERIFY(Find(result.snapshot,Key::Listen)->has_override);
    }
    void explicitResetRestoresConfigInsteadOfWritingADefault() {
        Profile profile;profile.config("listen",false);QmlCoreSettings::SetRwSetting(profile.args,"listen",true);QVERIFY(profile.args.WriteSettingsFile());const auto base=Read(profile.args);
        const auto result=Commit(profile.node,profile.args,base,Submit(base,{{Key::Listen,false,Intent::RemoveOverride}}));QCOMPARE(result.outcome,Outcome::Applied);
        QVERIFY(!profile.disk().contains("listen"));QVERIFY(!std::get<bool>(Find(result.snapshot,Key::Listen)->next_start));
    }
    void changedBackendCannotBeOverwrittenByAnOldBatch() {
        Profile profile;const auto base=Read(profile.args);QmlCoreSettings::SetRwSetting(profile.args,"server",true);QVERIFY(profile.args.WriteSettingsFile());const auto bytes=profile.bytes();
        const auto result=Commit(profile.node,profile.args,base,Submit(base,{{Key::Natpmp,false}}));QCOMPARE(result.outcome,Outcome::Conflict);QVERIFY(!result.persisted);
        QCOMPARE(profile.bytes(),bytes);QVERIFY(profile.disk().value("server").toBool());QCOMPARE(profile.node.calls.mapPort.load(),0);
    }
    void persistenceReceiptSurvivesRuntimeFailure_data() {QTest::addColumn<bool>("standard");QTest::newRow("standard")<<true;QTest::newRow("nonstandard")<<false;}
    void persistenceReceiptSurvivesRuntimeFailure() {
        QFETCH(bool,standard);Profile profile;profile.node.map_port_fn=[standard](bool){if(standard)throw std::runtime_error("Runtime application failed");throw 7;};const auto base=Read(profile.args);const auto batch=Submit(base,{{Key::Natpmp,true}});
        const auto result=Commit(profile.node,profile.args,base,batch);QCOMPARE(result.outcome,Outcome::Uncertain);QVERIFY(result.write_completed);QVERIFY(result.persisted);QVERIFY(!result.runtime_confirmed);QVERIFY(profile.disk().value("natpmp").toBool());QCOMPARE(profile.node.calls.mapPort.load(),1);
        QVERIFY(result.persisted_values.at(Key::Natpmp));QVERIFY(std::get<bool>(*result.persisted_values.at(Key::Natpmp)));
        const auto replay=Commit(profile.node,profile.args,base,batch);QCOMPARE(replay.outcome,Outcome::Conflict);QCOMPARE(profile.node.calls.mapPort.load(),1);
    }
    void transactionAfterSaveExceptionNeverRollsBackTheSavedMap() {
        Profile profile;const auto result=QmlCoreSettings::PersistTransaction(profile.args,{"server"},[&]{QmlCoreSettings::SetRwSetting(profile.args,"server",true);return true;},[]{throw std::runtime_error("Post-save failure");});
        QVERIFY(result.persisted);QVERIFY(!result.completed);QVERIFY(profile.args.GetPersistentSetting("server").isTrue());QVERIFY(profile.disk().value("server").toBool());
    }
    void independentFileEditCannotBeOverwrittenByAnOldBatch() {
        Profile profile;const auto base=Read(profile.args);QFile file{profile.directory.filePath("settings.json")};QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::Truncate));const auto changed=QJsonDocument{QJsonObject{{"server",true}}}.toJson();QCOMPARE(file.write(changed),changed.size());file.close();
        const auto result=Commit(profile.node,profile.args,base,Submit(base,{{Key::Natpmp,false}}));QCOMPARE(result.outcome,Outcome::Conflict);QVERIFY(!result.write_completed);QCOMPARE(profile.bytes(),changed);QCOMPARE(profile.node.calls.mapPort.load(),0);
    }
    void unsavedNativeMapIsNotReportedAsCommitted() {
        Profile profile;QmlCoreSettings::SetRwSetting(profile.args,"server",true);const auto snapshot=Read(profile.args);
        QVERIFY(!snapshot.ready);QVERIFY(!snapshot.error.isEmpty());QVERIFY(profile.args.GetPersistentSetting("server").isTrue());QVERIFY(!profile.disk().contains("server"));
    }
    void actualFileWriteFailureRestoresOnlyTheAttemptedKeys() {
        Profile profile;const auto base=Read(profile.args);const auto bytes=profile.bytes();QVERIFY(QDir{profile.directory.path()}.mkdir("settings.json.tmp"));
        const auto result=Commit(profile.node,profile.args,base,Submit(base,{{Key::Server,true},{Key::Natpmp,false}}));QCOMPARE(result.outcome,Outcome::Failed);QVERIFY(!result.write_completed);QVERIFY(!result.persisted);QVERIFY(!result.error.isEmpty());
        QCOMPARE(profile.bytes(),bytes);QVERIFY(profile.args.GetPersistentSetting("server").isNull());QVERIFY(profile.args.GetPersistentSetting("natpmp").isNull());QCOMPARE(profile.node.calls.mapPort.load(),0);
    }
    void disabledPersistenceKeepsAReadableReadOnlySnapshot() {
        Profile profile{{"-nosettings"},false};const auto snapshot=Read(profile.args);QVERIFY(snapshot.ready);QVERIFY(!snapshot.persistence_available);
        for(const auto& field:snapshot.fields)QVERIFY(!field.editable);
        const auto result=Commit(profile.node,profile.args,snapshot,Submit(snapshot,{{Key::Server,true}}));QVERIFY(result.outcome!=Outcome::Applied);QVERIFY(!result.write_completed);
    }
};
BITCOINQML_REGISTER_QT_TEST(EffectiveSettingsTests)
#include <test_effectivesettings.moc>
