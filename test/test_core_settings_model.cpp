// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/core_settings_model.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <test/mocks/drainexecutors.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QThread>
#include <atomic>
#include <algorithm>
#include <deque>
#include <stdexcept>
namespace {
bool Publish(std::deque<std::function<void()>>& gui){if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;}
bool Settle(ManualRoutedExecution& transport,std::deque<std::function<void()>>& gui){
    for(int turn=0;turn<512;++turn){if(!transport.queuedIdle()){if(!transport.stepNext())return false;continue;}if(Publish(gui))continue;return true;}return false;
}
struct SettingsWriteProbe {
    QmlCoreSettings::Values persisted;
    int calls{0}, writes{0};
    bool throw_work{false};
    QString write_error{};

    void install(CoreSettingsModel& model)
    {
        model.setEffectPort([this](const auto& change, auto, std::shared_ptr<execution::Scheduler> action, auto done, auto failed) {
            ++calls;
            if (!change.accepted || (!QmlCoreSettings::ValuesChanged(change) && !change.force_write)) {
                done(CoreSettingsModel::EffectResult{});
                return;
            }
            const auto admitted = execution::Submit(*action, [this, change] {
                if (throw_work) throw std::runtime_error("native settings write failed");
                if (write_error.isEmpty()) {
                    ++writes;
                    persisted = change.after;
                }
                return CoreSettingsModel::EffectResult{write_error, persisted, change.after_statuses, {}};
            }, done, failed);
            if (!admitted) failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
        }, {}, {});
    }
};
struct SettingsHeldEffect {
    QmlCoreSettings::Change change;
    std::shared_ptr<execution::Scheduler> action;
    std::function<void(CoreSettingsModel::EffectResult)> done;
    std::function<void(std::exception_ptr)> failed;
};
}
class CoreSettingsModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void manualDecisionOutcomeAndPresentation();
    void followupPreviewPreservesOriginalOutcomeTicket();
    void acceptedEffectContinuesAfterGlobalClose();
    void oldPreviewCannotReplaceNewUserIntent();
    void queuedDecisionsOwnEffectsBeforeOutcomeDelivery();
    void queuedDuplicateRetriesFailedOriginalWithoutLosingOtherKeys();
    void heldTypingCoalesces();
    void ownedWorkRetriesOutcomeAndDrainWithoutReplayingEffect();
    void ownedWorkDrainDropsNativeCaptureWhileClosedFacadeLives();
    void proxyDraftKeepsOriginalLiteralAndAtomicPair();
    void realThreadDecisionAndEffectStayOffGui();
    void manualTypedEntryValues_data();
    void manualTypedEntryValues();
    void manualEntryEnabledGroups_data();
    void manualEntryEnabledGroups();
    void manualTouchedPreviewAndContextOwnership();
    void manualStatusIdentityAndEntryAvailability();
    void manualRefusedSettingsKeepPersistedValues_data();
    void manualRefusedSettingsKeepPersistedValues();
    void manualOlderEffectOwnsItsFailureAndContext();
    void manualReentrantPublicationStopsAtItsOwner_data();
    void manualReentrantPublicationStopsAtItsOwner();
    void manualProxyDraftValidationBounds_data();
    void manualProxyDraftValidationBounds();
    void manualProxyDraftObservationFailure_data();
    void manualProxyDraftObservationFailure();
    void manualIndependentDraftsRespectLockedGroups_data();
    void manualIndependentDraftsRespectLockedGroups();
    void manualUnrecognizedEntryCannotAdmitCommands();
    void manualServiceConfigurationOwnership_data();
    void manualServiceConfigurationOwnership();
    void manualEffectReadbackAndOneOutcome_data();
    void manualEffectReadbackAndOneOutcome();
    void manualFailureNotificationsStopAtRetiredOwner_data();
    void manualFailureNotificationsStopAtRetiredOwner();
    void manualDesiredPublicationRetiresWithItsOwner_data();
    void manualDesiredPublicationRetiresWithItsOwner();
    void manualOlderDesiredFrameCannotReplaceNewerIntent();
    void manualDraftObserverLossAtHeldOutcome_data();
    void manualDraftObserverLossAtHeldOutcome();
    void manualQueuedResetDoesNotDuplicateANewerOpening_data();
    void manualQueuedResetDoesNotDuplicateANewerOpening();
    void manualQueuedValidationDoesNotDuplicateNewerInput();
    void realDefaultPortsInitializeAtTheFirstPublicCommand_data();
    void realDefaultPortsInitializeAtTheFirstPublicCommand();
};
void CoreSettingsModelTests::manualDecisionOutcomeAndPresentation()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.listen=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(model.pending());
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(model.pending());QVERIFY(Publish(gui));QVERIFY(!model.pending());
    bool before{false},after{false};
    model.setBeforeChangeHandler([&](auto origin){before=origin==CoreSettingsModel::ChangeOrigin::User&&!model.values().listen;});
    model.setAfterChangeHandler([&](const auto& change,auto origin){after=origin==CoreSettingsModel::ChangeOrigin::User&&change.accepted&&model.values().listen;});
    const auto ticket=model.changeListen(true);QVERIFY(ticket);QVERIFY(!model.values().listen);
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(!model.values().listen);QVERIFY(!before);
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(!model.values().listen);QVERIFY(!before);QVERIFY(!after);
    QVERIFY(Publish(gui));QVERIFY(before);QVERIFY(after);QVERIFY(model.values().listen);QVERIFY(model.isTouched("listen"));
    QCOMPARE(model.operationId(),ticket);QCOMPARE(model.operationState(),CoreSettingsModel::Applied);
    model.close();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::followupPreviewPreservesOriginalOutcomeTicket()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.listen=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});
    QVERIFY(Settle(transport,gui));
    QSignalSpy outcomes{&model,&CoreSettingsModel::operationFinished};
    model.setAfterChangeHandler([&](const auto&,auto origin){if(origin==CoreSettingsModel::ChangeOrigin::User)model.refresh();});
    const auto original=model.changeListen(true);QVERIFY(original);
    QVERIFY(Settle(transport,gui));QVERIFY(model.values().listen);QVERIFY(!model.pending());
    QVERIFY(model.operationId()>original);
    int original_outcomes{0};
    for(const auto& row:outcomes)if(row[0].toULongLong()==original){++original_outcomes;QCOMPARE(row[1].value<CoreSettingsModel::OperationState>(),CoreSettingsModel::Applied);}
    QCOMPARE(original_outcomes,1);
    model.close();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::acceptedEffectContinuesAfterGlobalClose()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.natpmp=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    int effects{0};bool actual{false};
    model.setEffectPort([&](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        const auto admitted=execution::Submit(*action,[&,change]{++effects;actual=change.after.natpmp;
            return CoreSettingsModel::EffectResult{{},change.after,change.after_statuses,{}};},done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    QVERIFY(model.changeNatpmp(true));
    // The decision ran, but its Domain outcome has not admitted the Core effect yet.
    QVERIFY(transport.step(execution::Lane::Domain));QCOMPARE(effects,0);
    transport.scheduler->closeOrdinaryAdmission();model.close();
    QSignalSpy notices{&model,&CoreSettingsModel::operationChanged};
    QVERIFY(transport.step(execution::Lane::Domain));QCOMPARE(effects,0);
    QVERIFY(transport.step(execution::Lane::Core));QCOMPARE(effects,1);QVERIFY(actual);
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(Settle(transport,gui));
    QCOMPARE(notices.size(),0);QCOMPARE(effects,1);QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::oldPreviewCannotReplaceNewUserIntent()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.listen=false;initial.server=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    const auto original=model.publishedUserRevision();QVERIFY(model.changeListen(true));
    QmlCoreSettings::Values old_preview=initial;old_preview.server=true;bool adopted{true};
    QVERIFY(model.applyPreviewValuesPreservingTouched(old_preview,{},[&](bool value){adopted=value;},true,original));
    QVERIFY(Settle(transport,gui));QVERIFY(!adopted);QVERIFY(model.values().listen);QVERIFY(!model.values().server);QVERIFY(model.isTouched("listen"));
    QCOMPARE(model.operationState(),CoreSettingsModel::Stale);
    model.close();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::queuedDecisionsOwnEffectsBeforeOutcomeDelivery()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;QmlCoreSettings::Values initial;initial.listen=false;initial.natpmp=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    auto actual=std::make_shared<QmlCoreSettings::Values>(initial);auto effects=std::make_shared<int>(0);
    model.setEffectPort([actual,effects](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        if(!change.accepted||(!QmlCoreSettings::ValuesChanged(change)&&!change.force_write)){done(CoreSettingsModel::EffectResult{});return;}
        const auto admitted=execution::Submit(*action,[change,actual,effects]{++*effects;
            if(change.setting_name=="listen")actual->listen=change.after.listen;
            if(change.setting_name=="natpmp")actual->natpmp=change.after.natpmp;
            return CoreSettingsModel::EffectResult{{},*actual,change.after_statuses,{}};},done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    QVERIFY(model.changeListen(true));QVERIFY(model.changeNatpmp(true));bool preview_applied{true};
    QVERIFY(model.applyPreviewValuesPreservingTouched(initial,{},[&](bool applied){preview_applied=applied;},true,model.userRevision()));
    // Execute both decisions and the preview before delivering ANY outcome.
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(transport.step(execution::Lane::Domain));
    QCOMPARE(*effects,0);QVERIFY(model.changeListen(false));
    for(int turn=0;turn<4;++turn)QVERIFY(transport.step(execution::Lane::Domain));
    QCOMPARE(*effects,0);QVERIFY(Settle(transport,gui));QVERIFY(!preview_applied);
    QCOMPARE(*effects,3);QVERIFY(!actual->listen);QVERIFY(actual->natpmp);QVERIFY(!model.values().listen);QVERIFY(model.values().natpmp);
    model.close();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::queuedDuplicateRetriesFailedOriginalWithoutLosingOtherKeys()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial;initial.listen=false;initial.natpmp=false;
    CoreSettingsModel model{initial};model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    auto actual=std::make_shared<QmlCoreSettings::Values>(initial);auto effects=std::make_shared<int>(0);
    model.setEffectPort([actual,effects](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        if(!change.accepted||(!QmlCoreSettings::ValuesChanged(change)&&!change.force_write)){done(CoreSettingsModel::EffectResult{});return;}
        const auto admitted=execution::Submit(*action,[change,actual,effects]{
            ++*effects;const bool rejected=*effects==1;
            if(!rejected&&change.setting_name=="listen")actual->listen=change.after.listen;
            if(!rejected&&change.setting_name=="natpmp")actual->natpmp=change.after.natpmp;
            return CoreSettingsModel::EffectResult{rejected?QStringLiteral("original write failed"):QString{},*actual,change.after_statuses,{}};
        },done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    QVERIFY(model.changeListen(true));QVERIFY(model.changeNatpmp(true));const auto retry=model.changeListen(true);QVERIFY(retry);
    // All decisions precede all Core writes: the equal duplicate remains an
    // independently owned write while the original same-key write is pending.
    for(int decision=0;decision<3;++decision)QVERIFY(transport.step(execution::Lane::Domain));
    QCOMPARE(*effects,0);QVERIFY(Settle(transport,gui));
    QCOMPARE(*effects,3);QVERIFY(actual->listen);QVERIFY(actual->natpmp);
    QVERIFY(model.values().listen);QVERIFY(model.values().natpmp);
    QCOMPARE(model.operationId(),retry);QCOMPARE(model.operationState(),CoreSettingsModel::Applied);
    model.close();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::heldTypingCoalesces()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;CoreSettingsModel model;
    model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    std::unique_ptr<CoreProxyDraft> draft{model.createProxyDraft()};QVERIFY(Settle(transport,gui));
    draft->setProxyEnabled(true);transport.drain();QCOMPARE(gui.size(),size_t{1});
    for(int edit=0;edit<10'000;++edit)draft->setProxyAddress(QStringLiteral("192.0.2.%1:9050").arg(edit%200+1));
    QCOMPARE(gui.size(),size_t{1});QVERIFY(transport.queuedIdle());QVERIFY(draft->pending());QVERIFY(!draft->canApply());
    draft->setProxyAddress("127.0.0.1:9050");QVERIFY(Publish(gui));QVERIFY(draft->pending());
    // A stale completion queues one latest observation; it cannot resubmit on
    // the synchronous failure/completion stack, and the manual port owns it.
    // Releasing the old outcome may queue Native cleanup. It must not start
    // the successor decision before the independent GUI follow-up turn.
    QVERIFY((transport.queuedCountIf([](const auto& turn){
        return (turn.lane==execution::Lane::Core||turn.lane==execution::Lane::Domain)&&turn.phase==execution::Phase::Queued;
    }) == 0));QCOMPARE(gui.size(),size_t{1});
    QVERIFY(Settle(transport,gui));QVERIFY(draft->canApply());QVERIFY(draft->proxyError().isEmpty());
    model.close();transport.drain();QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::ownedWorkRetriesOutcomeAndDrainWithoutReplayingEffect()
{
    ManualRoutedExecution transport;int effects{0},publications{0};
    execution::OwnedWork work{{transport.scheduler,[](auto){throw std::runtime_error("primary post rejected");}}};
    QVERIFY(work.submit([&]{return ++effects;},[&](int result){QCOMPARE(result,1);++publications;},[](auto){QFAIL("Unexpected effect failure");}));
    transport.drain();QCOMPARE(effects,1);QCOMPARE(publications,0);QTRY_COMPARE(publications,1);
    bool drained{false};work.close([&]{drained=true;});QTRY_VERIFY(drained);
    transport.drain();QCOMPARE(effects,1);QVERIFY(transport.quiescent());

    int posted{0};std::deque<std::function<void()>> rejected;
    execution::OwnedWork duplicate{{transport.scheduler,[&](auto callback){++posted;rejected.push_back(std::move(callback));throw std::runtime_error("enqueued before rejection");}}};
    QVERIFY(duplicate.submit([&]{return ++effects;},[&](int){++publications;},[](auto){QFAIL("Unexpected effect failure");}));
    transport.drain();QTRY_COMPARE(publications,2);while(Publish(rejected)){}QCOMPARE(publications,2);QCOMPARE(effects,2);
    duplicate.close();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::ownedWorkDrainDropsNativeCaptureWhileClosedFacadeLives()
{
    int effects{0},published{0},drained{0},destroyed{0};
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    execution::OwnedWork work{{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}}};
    struct Native {int& destroyed;~Native(){++destroyed;}};
    auto value=execution::RetainSnapshot<Native>(transport.scheduler,[&]{return std::unique_ptr<Native>{new Native{destroyed}};});
    QVERIFY(value);std::weak_ptr<Native> original{value};
    QVERIFY(work.submit([&]{++effects;return 42;},[&](int){++published;},[](std::exception_ptr){QFAIL("Accepted effect failed");}));
    work.close([value,&drained]{QVERIFY(value);++drained;});value.reset();
    transport.drain();QCOMPARE(effects,1);QCOMPARE(published,0);QCOMPARE(drained,0);
    QVERIFY(!original.expired());QCOMPARE(destroyed,0);
    while(Publish(gui)){}
    QCOMPARE(drained,1);QCOMPARE(published,0);QVERIFY(original.expired());
    // Release of the last GUI alias only activates reserved Native cleanup.
    QCOMPARE(destroyed,0);transport.drain();QCOMPARE(destroyed,1);
    work.close();transport.drain();while(Publish(gui)){}
    QCOMPARE(drained,1);QCOMPARE(effects,1);QCOMPARE(transport.scheduler->pending(),size_t{0});
    // work is intentionally still alive: no consumed callback capture may
    // require destroying its closed facade to finish the Native ledger.
}

void CoreSettingsModelTests::proxyDraftKeepsOriginalLiteralAndAtomicPair()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    CoreSettingsModel model;model.configureServices({transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}});QVERIFY(Settle(transport,gui));
    std::unique_ptr<CoreProxyDraft> draft{model.createProxyDraft()};QVERIFY(draft);QVERIFY(Settle(transport,gui));
    draft->setProxyEnabled(true);draft->setProxyAddress("not a proxy");transport.drain();
    draft->setProxyAddress("127.0.0.1:9050");while(Publish(gui)){}QVERIFY(draft->pending());QVERIFY(!draft->canApply());
    QVERIFY(Settle(transport,gui));QVERIFY(draft->proxyError().isEmpty());QVERIFY(draft->canApply());QVERIFY(!model.values().proxy_enabled);
    draft->setTorEnabled(true);draft->setTorAddress("");QVERIFY(Settle(transport,gui));QVERIFY(!draft->torError().isEmpty());
    QCOMPARE(draft->requestApply(),quint64{0});QVERIFY(!model.values().proxy_enabled);QVERIFY(!model.values().tor_enabled);
    draft->setTorAddress("[::1]:9050");QVERIFY(Settle(transport,gui));
    const auto ticket=draft->requestApply();QVERIFY(ticket);draft->setProxyAddress("192.0.2.9:9050");
    QVERIFY(Settle(transport,gui));QCOMPARE(draft->operationId(),ticket);QCOMPARE(draft->operationState(),CoreSettingsModel::Applied);
    QVERIFY(model.values().proxy_enabled);QVERIFY(model.values().tor_enabled);
    QCOMPARE(model.values().proxy_address,QStringLiteral("127.0.0.1:9050"));QCOMPARE(model.values().tor_address,QStringLiteral("[::1]:9050"));
    QCOMPARE(draft->proxyAddress(),QStringLiteral("192.0.2.9:9050"));
    model.close();transport.drain();QVERIFY(transport.quiescent());
}
void CoreSettingsModelTests::realThreadDecisionAndEffectStayOffGui()
{
    const auto drain_native=DrainExecutorsBeforeNativeOwnerDestruction();
    BackendExecutor executor{execution::Options{}};CoreSettingsModel model;
    model.configureServices({executor.scheduler(),presentation::QueuedPost()});QTRY_VERIFY(!model.pending());
    const auto* gui=QThread::currentThread();std::atomic<bool> decision_native{false},effect_native{false};
    model.setEffectPort([&](const auto& change,auto,std::shared_ptr<execution::Scheduler> action,auto done,auto failed){
        decision_native=QThread::currentThread()!=gui;
        const auto admitted=execution::Submit(*action,[&,change]{effect_native=QThread::currentThread()!=gui;
            return CoreSettingsModel::EffectResult{{},change.after,change.after_statuses,{}};},done,failed);
        if(!admitted)failed(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    },{},{});
    QVERIFY(model.changeServer(true));QTRY_VERIFY(!model.pending());QVERIFY(model.values().server);QVERIFY(decision_native.load());QVERIFY(effect_native.load());
    model.close();executor.shutdown();
}
void CoreSettingsModelTests::manualTypedEntryValues_data()
{
    QTest::addColumn<QString>("key");
    QTest::addColumn<QVariant>("input");
    QTest::addColumn<QVariant>("expected");
    QTest::addColumn<bool>("typed");
    QTest::addColumn<bool>("accepted");
    QTest::addColumn<bool>("locked");
    const auto row = [](const char* name, const char* key, QVariant input, QVariant expected,
                        bool typed = true, bool accepted = true, bool locked = false) {
        QTest::newRow(name) << QString::fromLatin1(key) << input << expected << typed << accepted << locked;
    };
    row("listen-bool", "listen", true, true);
    row("natpmp-bool", "natpmp", true, true);
    row("server-bool", "server", true, true);
    row("listen-unchanged", "listen", false, false);
    row("natpmp-unchanged", "natpmp", false, false);
    row("server-unchanged", "server", false, false);
    row("prune-int", "prune", 7, 7);
    row("prune-double", "prune", 7.0, 7);
    row("prune-uint", "prune", QVariant::fromValue(uint{7}), 7);
    row("prune-longlong", "prune", QVariant::fromValue(qlonglong{7}), 7);
    row("prune-ulonglong", "prune", QVariant::fromValue(qulonglong{7}), 7);
    row("prune-unchanged", "prune", 2, 2);
    row("prune-zero-rejected", "prune", 0, 2, true, false);
    row("proxy-trimmed", "proxy", QStringLiteral(" 127.0.0.1:9050 "), QStringLiteral("127.0.0.1:9050"));
    row("onion-trimmed", "onion", QStringLiteral(" [::1]:9150 "), QStringLiteral("[::1]:9150"));
    row("proxy-invalid", "proxy", QStringLiteral("ipv4=127.0.0.1:9050"), QString{}, true, false);
    row("onion-empty", "onion", QString{}, QString{}, true, false);
    row("listen-string-ignored", "listen", QStringLiteral("true"), false, false);
    row("natpmp-container-ignored", "natpmp", QVariantList{true}, false, false);
    row("server-int-ignored", "server", 1, false, false);
    row("prune-string-ignored", "prune", QStringLiteral("7"), 2, false);
    row("proxy-int-ignored", "proxy", 9050, QString{}, false);
    row("onion-bool-ignored", "onion", false, QString{}, false);
    row("listen-locked", "listen", true, false, true, false, true);
    row("natpmp-locked", "natpmp", true, false, true, false, true);
    row("server-locked", "server", true, false, true, false, true);
    row("prune-locked", "prune", 7, 2, true, false, true);
    row("proxy-locked", "proxy", QStringLiteral("127.0.0.1:9050"), QString{}, true, false, true);
    row("onion-locked", "onion", QStringLiteral("[::1]:9150"), QString{}, true, false, true);
}

void CoreSettingsModelTests::manualTypedEntryValues()
{
    QFETCH(QString, key); QFETCH(QVariant, input); QFETCH(QVariant, expected);
    QFETCH(bool, typed); QFETCH(bool, accepted); QFETCH(bool, locked);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.listen = false;
    const QVariantMap status{{QStringLiteral("canEdit"), !locked},
        {QStringLiteral("infoText"), QStringLiteral("Original settings source")},
        {QStringLiteral("createsGuiOverride"), !locked}};
    CoreSettingsModel model{initial, QmlCoreSettings::CaptureStatuses({{key, status}})};
    SettingsWriteProbe effect{initial}; effect.install(model);
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }});
    QVERIFY(Settle(transport, gui));
    auto* entry = qobject_cast<CoreSettingEntryModel*>(model.entry(key)); QVERIFY(entry);
    QCOMPARE(model.entry(key), static_cast<QObject*>(entry)); QVERIFY(!model.entry(QStringLiteral("unsupported")));
    QCOMPARE(entry->key(), key); QCOMPARE(entry->status(), status);
    QCOMPARE(entry->canEdit(), !locked); QCOMPARE(entry->createsGuiOverride(), !locked);
    QCOMPARE(entry->infoText(), QStringLiteral("Original settings source"));
    QSignalSpy outcomes{&model, &CoreSettingsModel::operationFinished};
    const auto before = model.operationId(), revision = model.userRevision();
    const auto old_value = entry->value();
    entry->setValue(input);
    QCOMPARE(entry->value(), old_value);
    QVERIFY(QmlCoreSettings::ValuesEqual(effect.persisted, initial)); QCOMPARE(effect.writes, 0);
    QCOMPARE(model.operationId(), before + (typed ? 1 : 0));
    QCOMPARE(model.userRevision(), revision + (typed ? 1 : 0));
    QVERIFY(Settle(transport, gui));
    QCOMPARE(entry->value(), expected); QCOMPARE(effect.calls, typed ? 1 : 0);
    // An accepted typed user command owns explicit override intent even when
    // its scalar equals the current/default value (R33 scopes073/176/177).
    QCOMPARE(effect.writes, typed && accepted ? 1 : 0);
    QCOMPARE(outcomes.size(), typed ? 1 : 0);
    if (typed) {
        QCOMPARE(outcomes[0][0].toULongLong(), model.operationId());
        QCOMPARE(outcomes[0][1].value<CoreSettingsModel::OperationState>(), accepted ? CoreSettingsModel::Applied : CoreSettingsModel::Rejected);
    }
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), effect.persisted));
    QCOMPARE(entry->status(), status); QVERIFY(model.error().isEmpty());
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualEntryEnabledGroups_data()
{
    QTest::addColumn<QString>("key");
    for (const char* key : {"listen", "natpmp", "server", "prune", "proxy", "onion"}) QTest::newRow(key) << QString::fromLatin1(key);
}

void CoreSettingsModelTests::manualEntryEnabledGroups()
{
    QFETCH(QString, key);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.listen = false; initial.prune = false;
    CoreSettingsModel model{initial}; SettingsWriteProbe effect{initial}; effect.install(model);
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }});
    QVERIFY(Settle(transport, gui));
    auto* entry = qobject_cast<CoreSettingEntryModel*>(model.entry(key)); QVERIFY(entry); QVERIFY(!entry->enabled());
    QSignalSpy enabled{entry, &CoreSettingEntryModel::enabledChanged};
    entry->setEnabled(true); QCOMPARE(effect.writes, 0); QVERIFY(!entry->enabled());
    QVERIFY(Settle(transport, gui)); QVERIFY(entry->enabled()); QCOMPARE(effect.writes, 1); QCOMPARE(enabled.size(), 1);
    entry->setEnabled(true); QVERIFY(Settle(transport, gui));
    QCOMPARE(effect.calls, 2); QCOMPARE(effect.writes, 2); QCOMPARE(enabled.size(), 1);
    if (key == QStringLiteral("proxy") || key == QStringLiteral("onion")) {
        QCOMPARE(entry->defaultAddress(), QStringLiteral("127.0.0.1:9050"));
        QCOMPARE(entry->address(), QStringLiteral("127.0.0.1:9050"));
        const auto address = key == QStringLiteral("proxy") ? QStringLiteral("192.0.2.9:9050") : QStringLiteral("[::1]:9150");
        entry->setAddress(QStringLiteral(" ") + address + QStringLiteral(" "));
        QVERIFY(Settle(transport, gui)); QCOMPARE(entry->address(), address); QCOMPARE(entry->value().toString(), address);
        QCOMPARE(effect.calls, 3); QCOMPARE(effect.writes, 3);
    } else {
        QVERIFY(entry->address().isEmpty()); QCOMPARE(entry->requestAddress(QStringLiteral("127.0.0.1:9050")), quint64{0});
        QCOMPARE(effect.calls, 2); QCOMPARE(effect.writes, 2);
    }
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), effect.persisted));
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualTouchedPreviewAndContextOwnership()
{
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.listen = false;
    CoreSettingsModel model{initial}; SettingsWriteProbe effect{initial}; effect.install(model);
    QVERIFY(model.defaultProxyAddress().isEmpty()); QVERIFY(!model.publishedChange().accepted); QVERIFY(model.touchedSettings().isEmpty());
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }});
    QVERIFY(Settle(transport, gui)); QVERIFY(model.publishedChange().accepted); QVERIFY(!model.publishedValuesChanged());
    model.markTouched(QStringLiteral("listen")); QVERIFY(Settle(transport, gui)); QVERIFY(model.isTouched(QStringLiteral("listen")));
    model.setTouchedSettings({QStringLiteral("listen"), QStringLiteral("proxy")}); QVERIFY(Settle(transport, gui));
    model.clearTouched(QStringLiteral("listen")); QVERIFY(Settle(transport, gui));
    QCOMPARE(model.touchedSettings(), QSet<QString>{QStringLiteral("proxy")});
    model.clearTouchedSettings(); QVERIFY(Settle(transport, gui)); QVERIFY(model.touchedSettings().isEmpty());
    model.setTouchedSettings({QStringLiteral("listen"), QStringLiteral("prune"), QStringLiteral("proxy")}); QVERIFY(Settle(transport, gui));
    auto incoming = initial; incoming.listen = true; incoming.natpmp = true; incoming.server = true;
    incoming.prune = false; incoming.prune_size_gb = 7; incoming.proxy_enabled = true; incoming.proxy_address = QStringLiteral("192.0.2.1:9050");
    incoming.tor_enabled = true; incoming.tor_address = QStringLiteral("[::1]:9150");
    const auto statuses = QmlCoreSettings::CaptureStatuses({{QStringLiteral("listen"), QVariantMap{{QStringLiteral("canEdit"), false}}}});
    int completions{0}; bool adopted{false};
    const auto ticket = model.applyPreviewValuesPreservingTouched(incoming, statuses, [&](bool value) { ++completions; adopted = value; });
    QVERIFY(ticket); QVERIFY(Settle(transport, gui)); QCOMPARE(completions, 1); QVERIFY(adopted);
    auto expected = incoming; expected.listen = initial.listen; expected.prune = initial.prune; expected.prune_size_gb = initial.prune_size_gb;
    expected.proxy_enabled = initial.proxy_enabled; expected.proxy_address = initial.proxy_address;
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), expected)); QVERIFY(!model.canEdit(QStringLiteral("listen")));
    QVERIFY(QmlCoreSettings::ValuesEqual(model.publishedChange().before, initial));
    QVERIFY(QmlCoreSettings::ValuesEqual(model.publishedChange().after, expected)); QVERIFY(model.publishedValuesChanged());
    QVERIFY(model.applyPreviewValuesPreservingTouched(incoming, {}, [&](bool value) { ++completions; adopted = value; }, true));
    QVERIFY(Settle(transport, gui)); QCOMPARE(completions, 2); QVERIFY(adopted); QVERIFY(model.touchedSettings().isEmpty());
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), incoming)); QVERIFY(model.canEdit(QStringLiteral("listen")));
    const auto context = model.previewContext();
    QVERIFY(model.applyPreviewValuesPreservingTouched(initial, {}, [&](bool value) { ++completions; adopted = value; }, false, {}, context));
    model.invalidatePreviewContext(); QVERIFY(Settle(transport, gui));
    QCOMPARE(completions, 3); QVERIFY(!adopted); QCOMPARE(model.operationState(), CoreSettingsModel::Stale);
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), incoming)); QCOMPARE(model.userRevision(), quint64{0});
    QCOMPARE(effect.calls, 0); QCOMPARE(effect.writes, 0); QVERIFY(QmlCoreSettings::ValuesEqual(effect.persisted, initial));
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualStatusIdentityAndEntryAvailability()
{
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui; CoreSettingsModel model;
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    auto* entry = qobject_cast<CoreSettingEntryModel*>(model.entry(QStringLiteral("proxy"))); QVERIFY(entry);
    QSignalSpy statuses{&model, &CoreSettingsModel::statusesChanged}; QSignalSpy entry_status{entry, &CoreSettingEntryModel::statusChanged};
    const QVariantMap value{{QStringLiteral("canEdit"), false}, {QStringLiteral("infoText"), QStringLiteral("Locked by original configuration")},
        {QStringLiteral("createsGuiOverride"), false}};
    const auto projection = QmlCoreSettings::CaptureStatuses({{QStringLiteral("proxy"), value}});
    model.setStatuses(projection); QVERIFY(Settle(transport, gui)); QCOMPARE(statuses.size(), 1); QCOMPARE(entry_status.size(), 1);
    QVERIFY(!entry->canEdit()); QCOMPARE(entry->infoText(), QStringLiteral("Locked by original configuration")); QVERIFY(!entry->createsGuiOverride());
    const auto ticket = model.operationId(); model.setStatuses(projection); model.setStatuses({});
    QCOMPARE(model.operationId(), ticket); QVERIFY(Settle(transport, gui)); QCOMPARE(statuses.size(), 1);
    model.setStatuses(QmlCoreSettings::CaptureStatuses(projection.values)); QVERIFY(Settle(transport, gui));
    QCOMPARE(model.operationId(), ticket + 1); QCOMPARE(statuses.size(), 1);
    model.setStatuses(QmlCoreSettings::CaptureStatuses({})); QVERIFY(Settle(transport, gui));
    QVERIFY(entry->canEdit()); QVERIFY(entry->infoText().isEmpty()); QCOMPARE(statuses.size(), 2);
    model.setAdmissionReady(false); model.setAdmissionReady(false); QVERIFY(!entry->canEdit()); QCOMPARE(statuses.size(), 3);
    const auto revision = model.userRevision(); QCOMPARE(model.changeProxyEnabled(true), quint64{0}); QCOMPARE(model.userRevision(), revision);
    model.setAdmissionReady(true); QVERIFY(entry->canEdit()); QCOMPARE(statuses.size(), 4);
    model.close(); model.setAdmissionReady(false); QCOMPARE(statuses.size(), 4); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualRefusedSettingsKeepPersistedValues_data()
{
    QTest::addColumn<int>("refusal");
    const char* names[]{"closed", "not-ready", "", "domain-transport", "core-transport", "native-exception", "authoritative-error"};
    for (int row : {0, 1, 3, 4, 5, 6}) QTest::newRow(names[row]) << row;
}

void CoreSettingsModelTests::manualRefusedSettingsKeepPersistedValues()
{
    QFETCH(int, refusal);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.server = false;
    const QVariantMap status{{QStringLiteral("infoText"), QStringLiteral("Original server source")}};
    CoreSettingsModel model{initial, QmlCoreSettings::CaptureStatuses({{QStringLiteral("server"), status}})};
    SettingsWriteProbe effect{initial}; effect.install(model);
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    auto* entry = qobject_cast<CoreSettingEntryModel*>(model.entry(QStringLiteral("server"))); QVERIFY(entry);
    if (refusal == 0) model.close();
    if (refusal == 1) model.setAdmissionReady(false);
    if (refusal == 3) transport.rejectLane(execution::Lane::Domain);
    if (refusal == 4) transport.rejectLane(execution::Lane::Core);
    if (refusal == 5) effect.throw_work = true;
    if (refusal == 6) effect.write_error = QStringLiteral("The original settings file was not writable.");
    QSignalSpy outcomes{&model, &CoreSettingsModel::operationFinished};
    const auto ticket = model.changeServer(true);
    QCOMPARE(ticket != 0, refusal >= 4 && refusal <= 6);
    QVERIFY(Settle(transport, gui)); QCOMPARE(effect.writes, 0);
    QVERIFY(QmlCoreSettings::ValuesEqual(effect.persisted, initial)); QVERIFY(!model.pending());
    QCOMPARE(effect.calls, refusal >= 4 && refusal <= 6 ? 1 : 0);
    if (refusal == 0) { QCOMPARE(model.operationState(), CoreSettingsModel::Closed); QCOMPARE(outcomes.size(), 0); }
    else if (refusal == 1) { QVERIFY(model.error().isEmpty()); QCOMPARE(outcomes.size(), 0); }
    else {
        QCOMPARE(model.operationState(), CoreSettingsModel::Failed); QVERIFY(!model.error().isEmpty()); QVERIFY(!entry->canEdit());
        QCOMPARE(entry->infoText(), model.error()); QCOMPARE(outcomes.size(), refusal >= 3 && refusal <= 6 ? 1 : 0);
        if (refusal == 6) { QCOMPARE(model.error(), effect.write_error); QVERIFY(!model.values().server); }
    }
    if (refusal >= 3 && refusal <= 6) {
        effect.throw_work = false; effect.write_error.clear(); int completed{0};
        QVERIFY(model.applyPreviewValuesPreservingTouched(effect.persisted, QmlCoreSettings::CaptureStatuses({{QStringLiteral("server"), status}}),
            [&](bool accepted) { QVERIFY(accepted); ++completed; }, true, model.userRevision()));
        QVERIFY(Settle(transport, gui)); QCOMPARE(completed, 1); QVERIFY(model.error().isEmpty()); QVERIFY(entry->canEdit());
        QCOMPARE(entry->infoText(), QStringLiteral("Original server source")); QVERIFY(!model.values().server);
        const int calls = effect.calls; QVERIFY(model.changeServer(true)); QVERIFY(Settle(transport, gui));
        QCOMPARE(effect.calls, calls + 1); QCOMPARE(effect.writes, 1); QVERIFY(effect.persisted.server); QVERIFY(model.values().server);
        QCOMPARE(model.operationState(), CoreSettingsModel::Applied);
    }
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualOlderEffectOwnsItsFailureAndContext()
{
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.listen = false; initial.server = false;
    CoreSettingsModel model{initial}; model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    struct Held { QmlCoreSettings::Change change; std::shared_ptr<execution::Scheduler> action; std::function<void(CoreSettingsModel::EffectResult)> done; std::function<void(std::exception_ptr)> failed; };
    std::vector<Held> held; int captures{0}, writes{0}; std::vector<int> published; QmlCoreSettings::Values persisted = initial;
    model.setEffectPort([&](const auto& change, auto, auto action, auto done, auto failed) {
        held.push_back({change, std::move(action), std::move(done), std::move(failed)});
    }, [&] { return CoreSettingsModel::EffectContext{std::make_shared<const int>(++captures)}; },
    [&](auto context, const auto&) { published.push_back(*std::static_pointer_cast<const int>(context)); });
    QSignalSpy outcomes{&model, &CoreSettingsModel::operationFinished};
    const auto original = model.changeListen(true), latest = model.changeServer(true); QVERIFY(original); QVERIFY(latest > original);
    QVERIFY(Settle(transport, gui)); QCOMPARE(held.size(), size_t{2}); QCOMPARE(captures, 2); QCOMPARE(writes, 0); QVERIFY(model.pending());
    QVERIFY(model.values().listen); QVERIFY(model.values().server); QVERIFY(!persisted.listen); QVERIFY(!persisted.server);
    auto newer = std::move(held[1]);
    QVERIFY(execution::Submit(*newer.action, [&] { ++writes; persisted.server = true; return CoreSettingsModel::EffectResult{{}, persisted, {}, {}}; }, std::move(newer.done), std::move(newer.failed)));
    QVERIFY(Settle(transport, gui)); QCOMPARE(writes, 1); QVERIFY(model.pending()); QCOMPARE(published, std::vector<int>{2});
    newer.action.reset();
    QCOMPARE(model.operationId(), latest); QCOMPARE(model.operationState(), CoreSettingsModel::Applied);
    auto older = std::move(held[0]);
    QVERIFY(execution::Submit(*older.action, [&] { return CoreSettingsModel::EffectResult{QStringLiteral("Original listen write failed."), persisted, {}, {}}; }, std::move(older.done), std::move(older.failed)));
    QVERIFY(Settle(transport, gui)); QVERIFY(!model.pending()); QCOMPARE(writes, 1); QVERIFY(!persisted.listen); QVERIFY(persisted.server);
    older.action.reset();
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), persisted)); QVERIFY(model.error().isEmpty());
    QCOMPARE(model.operationId(), latest); QCOMPARE(model.operationState(), CoreSettingsModel::Applied);
    QCOMPARE(published, (std::vector<int>{2, 1})); QCOMPARE(outcomes.size(), 2);
    QCOMPARE(outcomes[0][0].toULongLong(), latest); QCOMPARE(outcomes[0][1].value<CoreSettingsModel::OperationState>(), CoreSettingsModel::Applied);
    QCOMPARE(outcomes[1][0].toULongLong(), original); QCOMPARE(outcomes[1][1].value<CoreSettingsModel::OperationState>(), CoreSettingsModel::Failed);
    held.clear(); model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualReentrantPublicationStopsAtItsOwner_data()
{
    QTest::addColumn<int>("stage"); QTest::addColumn<bool>("destroy");
    const char* stages[]{"before", "statuses", "entry-status", "value", "enabled", "address", "after", "finished"};
    for (int stage = 0; stage < 8; ++stage) for (bool destroy : {false, true}) {
        const QString tag = QString::fromLatin1(stages[stage]) + (destroy ? QStringLiteral("-destroy") : QStringLiteral("-close"));
        QTest::newRow(qPrintable(tag)) << stage << destroy;
    }
}

void CoreSettingsModelTests::manualReentrantPublicationStopsAtItsOwner()
{
    QFETCH(int, stage); QFETCH(bool, destroy);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; auto model = std::make_unique<CoreSettingsModel>(initial);
    model->configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    auto* entry = qobject_cast<CoreSettingEntryModel*>(model->entry(QStringLiteral("proxy"))); QVERIFY(entry);
    QSignalSpy statuses{model.get(), &CoreSettingsModel::statusesChanged}, finished{model.get(), &CoreSettingsModel::operationFinished};
    QSignalSpy changed{model.get(), &CoreSettingsModel::operationChanged}, entry_status{entry, &CoreSettingEntryModel::statusChanged};
    QSignalSpy value{entry, &CoreSettingEntryModel::valueChanged}, enabled{entry, &CoreSettingEntryModel::enabledChanged}, address{entry, &CoreSettingEntryModel::addressChanged};
    int before_calls{0}, after_calls{0}, completions{0};
    const auto stop = [&] { if (destroy) model.reset(); else model->close(); };
    model->setBeforeChangeHandler([&](auto origin) { QVERIFY(origin == CoreSettingsModel::ChangeOrigin::Preview); ++before_calls; if (stage == 0) stop(); });
    model->setAfterChangeHandler([&](const auto&, auto) { ++after_calls; if (stage == 6) stop(); });
    connect(model.get(), &CoreSettingsModel::statusesChanged, this, [&] { if (stage == 1) stop(); });
    connect(entry, &CoreSettingEntryModel::statusChanged, this, [&] { if (stage == 2) stop(); });
    connect(entry, &CoreSettingEntryModel::valueChanged, this, [&] { if (stage == 3) stop(); });
    connect(entry, &CoreSettingEntryModel::enabledChanged, this, [&] { if (stage == 4) stop(); });
    connect(entry, &CoreSettingEntryModel::addressChanged, this, [&] { if (stage == 5) stop(); });
    connect(model.get(), &CoreSettingsModel::operationFinished, this, [&](auto, auto) { if (stage == 7) stop(); });
    auto preview = initial; preview.proxy_enabled = true; preview.proxy_address = QStringLiteral("192.0.2.4:9050");
    QVERIFY(model->applyPreviewValuesPreservingTouched(preview, QmlCoreSettings::CaptureStatuses({{QStringLiteral("proxy"), QVariantMap{{QStringLiteral("infoText"), QStringLiteral("Preview source")}}}}),
        [&](bool accepted) { QVERIFY(accepted); ++completions; }));
    QVERIFY(Settle(transport, gui)); QCOMPARE(before_calls, 1); QCOMPARE(after_calls, stage >= 6 ? 1 : 0);
    QCOMPARE(statuses.size(), stage >= 1 ? 1 : 0); QCOMPARE(entry_status.size(), stage >= 2 ? 1 : 0);
    QCOMPARE(value.size(), stage >= 3 ? 1 : 0); QCOMPARE(enabled.size(), stage >= 4 ? 1 : 0); QCOMPARE(address.size(), stage >= 5 ? 1 : 0);
    QCOMPARE(finished.size(), stage == 7 ? 1 : 0); QCOMPARE(completions, stage == 7 ? 1 : 0); QCOMPARE(changed.size(), 1);
    if (destroy) QVERIFY(!model);
    else { QVERIFY(!model->presentationOpen()); QVERIFY(!model->pending()); QCOMPARE(model->operationState(), CoreSettingsModel::Closed);
        QVERIFY(QmlCoreSettings::ValuesEqual(model->values(), stage == 0 ? initial : preview)); }
    transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualProxyDraftValidationBounds_data()
{
    QTest::addColumn<bool>("tor"); QTest::addColumn<QString>("literal"); QTest::addColumn<bool>("enabled"); QTest::addColumn<bool>("too_long");
    for (bool tor : {false, true}) {
        const auto prefix = tor ? QStringLiteral("onion-") : QStringLiteral("proxy-");
        QTest::newRow(qPrintable(prefix + QStringLiteral("valid"))) << tor << QStringLiteral("[::1]:9050") << true << false;
        QTest::newRow(qPrintable(prefix + QStringLiteral("invalid"))) << tor << QStringLiteral("ipv4=127.0.0.1:9050") << true << false;
        QTest::newRow(qPrintable(prefix + QStringLiteral("disabled-invalid"))) << tor << QStringLiteral("ipv4=127.0.0.1:9050") << false << false;
        QTest::newRow(qPrintable(prefix + QStringLiteral("limit"))) << tor << QString(32768, QChar{'x'}) << true << false;
        QTest::newRow(qPrintable(prefix + QStringLiteral("over-limit"))) << tor << QString(32769, QChar{'x'}) << true << true;
    }
}

void CoreSettingsModelTests::manualProxyDraftValidationBounds()
{
    QFETCH(bool, tor); QFETCH(QString, literal); QFETCH(bool, enabled); QFETCH(bool, too_long);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui; QmlCoreSettings::Values initial;
    initial.proxy_address = QStringLiteral("127.0.0.1:9050"); initial.tor_address = QStringLiteral("[::1]:9050");
    CoreSettingsModel model{initial}; SettingsWriteProbe effect{initial}; effect.install(model);
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    std::unique_ptr<CoreProxyDraft> draft{model.createProxyDraft()}; QVERIFY(draft); QVERIFY(Settle(transport, gui));
    QVERIFY(draft->ready()); QVERIFY(!draft->dirty());
    QSignalSpy unchanged{draft.get(), &CoreProxyDraft::changed};
    draft->setProxyEnabled(draft->proxyEnabled()); draft->setProxyAddress(draft->proxyAddress());
    draft->setTorEnabled(draft->torEnabled()); draft->setTorAddress(draft->torAddress());
    QCOMPARE(unchanged.size(), 0); QVERIFY(!draft->pending()); QVERIFY(transport.queuedIdle());
    if (tor) { draft->setTorEnabled(enabled); draft->setTorAddress(literal); }
    else { draft->setProxyEnabled(enabled); draft->setProxyAddress(literal); }
    QVERIFY(Settle(transport, gui)); QVERIFY(!draft->pending()); QVERIFY(draft->dirty());
    QCOMPARE(tor ? draft->torAddress() : draft->proxyAddress(), literal);
    const auto error = tor ? draft->torError() : draft->proxyError();
    const bool valid = literal == QStringLiteral("[::1]:9050");
    QCOMPARE(error.isEmpty(), valid); QCOMPARE(error == QStringLiteral("The proxy location is too long."), too_long);
    QCOMPARE(draft->canApply(), !enabled || valid);
    if (enabled && !valid) QCOMPARE(draft->requestApply(), quint64{0});
    else { QVERIFY(draft->requestApply()); QVERIFY(Settle(transport, gui)); QCOMPARE(draft->operationState(), CoreSettingsModel::Applied); }
    QCOMPARE(effect.writes, enabled && valid ? 1 : 0);
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), effect.persisted));
    if (!enabled) QVERIFY(QmlCoreSettings::ValuesEqual(effect.persisted, initial));
    model.close(); QVERIFY(!draft->canApply()); QCOMPARE(draft->requestApply(), quint64{0});
    transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualProxyDraftObservationFailure_data()
{
    QTest::addColumn<bool>("opening"); QTest::addColumn<bool>("stale");
    QTest::newRow("opening-current") << true << false;
    QTest::newRow("opening-superseded") << true << true;
    QTest::newRow("validation-current") << false << false;
    QTest::newRow("validation-superseded") << false << true;
}

void CoreSettingsModelTests::manualProxyDraftObservationFailure()
{
    QFETCH(bool, opening); QFETCH(bool, stale);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui; QmlCoreSettings::Values initial;
    CoreSettingsModel model{initial}; SettingsWriteProbe effect{initial}; effect.install(model);
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    if (opening) transport.rejectLane(execution::Lane::Domain);
    std::unique_ptr<CoreProxyDraft> draft{model.createProxyDraft()}; QVERIFY(draft);
    if (opening) {
        QVERIFY(!draft->ready()); draft->setProxyEnabled(true); draft->setTorEnabled(true);
        draft->setProxyAddress(QStringLiteral("192.0.2.9:9050")); draft->setTorAddress(QStringLiteral("[::1]:9150"));
        QVERIFY(!draft->proxyEnabled()); QVERIFY(!draft->torEnabled()); QVERIFY(draft->proxyAddress().isEmpty()); QVERIFY(draft->torAddress().isEmpty());
        if (stale) draft->reset();
    } else {
        QVERIFY(Settle(transport, gui)); transport.rejectLane(execution::Lane::Domain);
        draft->setProxyEnabled(true); if (stale) draft->setProxyAddress(QStringLiteral("192.0.2.8:9050"));
    }
    QVERIFY(Settle(transport, gui)); QVERIFY(!draft->pending());
    if (stale) { QVERIFY(draft->error().isEmpty()); QVERIFY(draft->ready()); QVERIFY(draft->canApply()); }
    else {
        QCOMPARE(draft->error(), opening ? QStringLiteral("The proxy draft could not be opened.") : QStringLiteral("The proxy draft could not be checked."));
        QVERIFY(!draft->canApply()); draft->reset(); QVERIFY(Settle(transport, gui));
        QVERIFY(draft->error().isEmpty()); QVERIFY(draft->ready()); QVERIFY(draft->canApply()); QVERIFY(!draft->dirty());
    }
    QCOMPARE(effect.calls, 0); QCOMPARE(effect.writes, 0); QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), initial));
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualIndependentDraftsRespectLockedGroups_data()
{
    QTest::addColumn<bool>("lock_proxy"); QTest::newRow("proxy-locked") << true; QTest::newRow("onion-locked") << false;
}

void CoreSettingsModelTests::manualIndependentDraftsRespectLockedGroups()
{
    QFETCH(bool, lock_proxy);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui; QmlCoreSettings::Values initial;
    CoreSettingsModel model{initial}; SettingsWriteProbe effect{initial}; effect.install(model);
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    std::unique_ptr<CoreProxyDraft> first{model.createProxyDraft()}, second{model.createProxyDraft()};
    QVERIFY(Settle(transport, gui)); QVERIFY(first->ready()); QVERIFY(second->ready()); QVERIFY(!first->dirty()); QVERIFY(!second->dirty());
    first->setProxyEnabled(true); first->setProxyAddress(QStringLiteral("192.0.2.1:9050"));
    first->setTorEnabled(true); first->setTorAddress(QStringLiteral("[::1]:9150")); QVERIFY(Settle(transport, gui));
    QVERIFY(first->dirty()); QVERIFY(!second->dirty()); QVERIFY(first->requestApply()); QVERIFY(Settle(transport, gui));
    QCOMPARE(effect.writes, 1); QVERIFY(model.values().proxy_enabled); QVERIFY(model.values().tor_enabled);
    QVERIFY(!second->proxyEnabled()); QVERIFY(!second->torEnabled()); QVERIFY(second->dirty());
    second->reset(); QVERIFY(Settle(transport, gui)); QVERIFY(second->proxyEnabled()); QVERIFY(second->torEnabled()); QVERIFY(!second->dirty());
    const auto locked_key = lock_proxy ? QStringLiteral("proxy") : QStringLiteral("onion");
    model.setStatuses(QmlCoreSettings::CaptureStatuses({{locked_key, QVariantMap{{QStringLiteral("canEdit"), false}, {QStringLiteral("infoText"), QStringLiteral("Original command-line value")}}}}));
    QVERIFY(Settle(transport, gui));
    second->setProxyEnabled(false); second->setProxyAddress(QStringLiteral("192.0.2.9:9050"));
    second->setTorEnabled(false); second->setTorAddress(QStringLiteral("[::1]:9250")); QVERIFY(Settle(transport, gui));
    QVERIFY(second->canApply()); QVERIFY(second->requestApply()); QVERIFY(Settle(transport, gui)); QCOMPARE(effect.writes, 2);
    QCOMPARE(effect.persisted.proxy_enabled, lock_proxy); QCOMPARE(effect.persisted.tor_enabled, !lock_proxy);
    QCOMPARE(effect.persisted.proxy_address, lock_proxy ? QStringLiteral("192.0.2.1:9050") : QStringLiteral("192.0.2.9:9050"));
    QCOMPARE(effect.persisted.tor_address, lock_proxy ? QStringLiteral("[::1]:9250") : QStringLiteral("[::1]:9150"));
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), effect.persisted));
    QCOMPARE(model.status(locked_key).value(QStringLiteral("infoText")).toString(), QStringLiteral("Original command-line value"));
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualUnrecognizedEntryCannotAdmitCommands()
{
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    CoreSettingsModel model; model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }});
    QVERIFY(Settle(transport, gui)); SettingsWriteProbe effect{model.values()}; effect.install(model);
    CoreSettingEntryModel entry{QStringLiteral("unsupported"), model};
    const auto ticket = model.operationId(), revision = model.userRevision();
    QCOMPARE(entry.key(), QStringLiteral("unsupported")); QVERIFY(!entry.value().isValid()); QVERIFY(!entry.enabled());
    QVERIFY(entry.address().isEmpty()); QVERIFY(entry.status().isEmpty()); QVERIFY(!entry.canEdit());
    QVERIFY(entry.infoText().isEmpty()); QVERIFY(!entry.createsGuiOverride());
    QCOMPARE(entry.defaultAddress(), QStringLiteral("127.0.0.1:9050"));
    entry.setValue(true); entry.setValue(5); entry.setValue(QStringLiteral("127.0.0.1:9050"));
    entry.setEnabled(true); entry.setAddress(QStringLiteral("127.0.0.1:9050"));
    QCOMPARE(entry.requestAddress(QStringLiteral("127.0.0.1:9050")), quint64{0});
    QCOMPARE(model.operationId(), ticket); QCOMPARE(model.userRevision(), revision);
    QCOMPARE(effect.calls, 0); QVERIFY(transport.queuedIdle());
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualServiceConfigurationOwnership_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("unstarted-drain") << 0;
    QTest::newRow("closed-before-configuration") << 1;
    QTest::newRow("repeated-configuration") << 2;
}

void CoreSettingsModelTests::manualServiceConfigurationOwnership()
{
    QFETCH(int, mode);
    ManualRoutedExecution original, other; std::deque<std::function<void()>> gui, other_gui;
    CoreSettingsModel model; int drained{0};
    model.whenEffectsDrained({}); model.whenEffectsDrained([&] { ++drained; }); QCOMPARE(drained, 1);
    QVERIFY(model.presentationOpen()); QCOMPARE(model.operationState(), CoreSettingsModel::Idle);
    if (mode == 0) {
        QVERIFY(!model.pending()); QVERIFY(!model.publishedChange().accepted); QVERIFY(model.statuses().isEmpty());
        model.close(); QVERIFY(original.quiescent()); return;
    }
    if (mode == 1) model.close();
    model.configureServices({original.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }});
    if (mode == 1) {
        QVERIFY(!model.presentationOpen()); QCOMPARE(model.operationState(), CoreSettingsModel::Closed);
        QVERIFY(!model.createProxyDraft()); QCOMPARE(model.changeListen(false), quint64{0}); model.refresh();
        auto* entry = qobject_cast<CoreSettingEntryModel*>(model.entry(QStringLiteral("listen"))); QVERIFY(entry);
        QVERIFY(!entry->canEdit()); QCOMPARE(entry->value().toBool(), model.values().listen);
        QVERIFY(original.quiescent()); QVERIFY(gui.empty()); return;
    }
    QVERIFY(Settle(original, gui)); const auto ticket = model.operationId();
    model.configureServices({other.scheduler, [&](auto turn) { other_gui.push_back(std::move(turn)); }});
    QCOMPARE(model.operationId(), ticket); QVERIFY(other.quiescent());
    QVERIFY(model.changeServer(true)); QVERIFY(!original.queuedIdle()); QVERIFY(other.quiescent());
    QVERIFY(Settle(original, gui)); QVERIFY(model.values().server); QVERIFY(other_gui.empty());
    model.close(); model.configureServices({other.scheduler, {}});
    original.drain(); QVERIFY(original.quiescent()); QVERIFY(other.quiescent());
}

void CoreSettingsModelTests::manualEffectReadbackAndOneOutcome_data()
{
    QTest::addColumn<int>("mode");
    QTest::newRow("acknowledged-without-readback") << 0;
    QTest::newRow("authoritative-readback-differs-from-intent") << 1;
    QTest::newRow("failed-write-with-authoritative-readback") << 2;
    QTest::newRow("duplicate-success-and-late-failure") << 3;
    QTest::newRow("duplicate-failure-and-late-success") << 4;
    QTest::newRow("effect-adapter-throws-before-dispatch") << 5;
}

void CoreSettingsModelTests::manualEffectReadbackAndOneOutcome()
{
    QFETCH(int, mode);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.listen = false; initial.server = false;
    CoreSettingsModel model{initial}; QmlCoreSettings::Values persisted = initial;
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    int calls{0}, native_attempts{0}, writes{0}, publications{0}, aborts{0};
    std::atomic_bool context_aborted{false};
    const auto context = std::make_shared<const int>(47);
    const QVariantMap readback_status{{QStringLiteral("server"), QVariantMap{{QStringLiteral("infoText"), QStringLiteral("Authoritative native readback")}}}};
    model.setEffectPort([&](const auto& change, auto captured, auto action, auto done, auto failed) {
        ++calls; QVERIFY(captured == context);
        if (mode == 5) throw std::runtime_error("effect dispatch failed");
        const auto complete = [&, done, failed](auto result) {
            done(result);
            if (mode == 3) { done(result); failed(std::make_exception_ptr(std::runtime_error("late error"))); }
        };
        const auto reject = [&, done, failed](std::exception_ptr error) {
            failed(error);
            if (mode == 4) { failed(error); done(CoreSettingsModel::EffectResult{}); }
        };
        const auto admitted = execution::Submit(*action, [&, change] {
            ++native_attempts;
            if (mode == 4) throw std::runtime_error("native settings failure");
            if (mode != 2) { ++writes; persisted = change.after; }
            if (mode == 1) { persisted.listen = false; persisted.server = true; }
            return CoreSettingsModel::EffectResult{mode == 2 ? QStringLiteral("Native write refused") : QString{},
                mode == 0 ? std::optional<QmlCoreSettings::Values>{} : std::optional<QmlCoreSettings::Values>{persisted},
                readback_status, {}};
        }, complete, reject);
        if (!admitted) reject(std::make_exception_ptr(execution::NotStarted{execution::NotStarted::Cause::Transport}));
    }, [context] { return CoreSettingsModel::EffectContext{context}; },
    [&](auto captured, const auto&) { QVERIFY(captured == context); ++publications; },
    [&](auto captured) { QVERIFY(captured == context); if (!context_aborted.exchange(true)) ++aborts; });
    QSignalSpy outcomes{&model, &CoreSettingsModel::operationFinished};
    const auto ticket = model.changeListen(true); QVERIFY(ticket); QVERIFY(!model.values().listen);
    QVERIFY(Settle(transport, gui)); QCOMPARE(calls, 1); QCOMPARE(native_attempts, mode == 5 ? 0 : 1);
    QCOMPARE(writes, mode == 2 || mode >= 4 ? 0 : 1); QCOMPARE(outcomes.size(), 1);
    QCOMPARE(outcomes[0][0].toULongLong(), ticket);
    const bool failed = mode == 2 || mode >= 4;
    QCOMPARE(outcomes[0][1].value<CoreSettingsModel::OperationState>(), failed ? CoreSettingsModel::Failed : CoreSettingsModel::Applied);
    QCOMPARE(model.operationState(), failed ? CoreSettingsModel::Failed : CoreSettingsModel::Applied);
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), persisted)); QVERIFY(!model.pending());
    QCOMPARE(publications, mode >= 4 ? 0 : 1); QCOMPARE(aborts, mode >= 4 ? 1 : 0);
    QCOMPARE(model.error().isEmpty(), !failed);
    if (mode == 1 || mode == 2 || mode == 3) QCOMPARE(model.status(QStringLiteral("server")), readback_status.value(QStringLiteral("server")).toMap());
    if (mode >= 4) {
        QVERIFY(model.applyPreviewValuesPreservingTouched(persisted, {}, {}, true, model.userRevision()));
        QVERIFY(Settle(transport, gui)); QVERIFY(model.error().isEmpty());
        QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), persisted)); QCOMPARE(calls, 1);
    }
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}



void CoreSettingsModelTests::manualFailureNotificationsStopAtRetiredOwner_data()
{
    QTest::addColumn<int>("stage"); QTest::addColumn<bool>("destroy");
    const char* stages[]{"statuses", "value", "enabled", "address", "completion", "finished"};
    for (int stage = 0; stage < 6; ++stage) for (bool destroy : {false, true}) {
        const QString tag = QString::fromLatin1(stages[stage]) + (destroy ? QStringLiteral("-destroy") : QStringLiteral("-close"));
        QTest::newRow(qPrintable(tag)) << stage << destroy;
    }
}

void CoreSettingsModelTests::manualFailureNotificationsStopAtRetiredOwner()
{
    QFETCH(int, stage); QFETCH(bool, destroy);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.server = false;
    auto model = std::make_unique<CoreSettingsModel>(initial);
    model->configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    auto* entry = qobject_cast<CoreSettingEntryModel*>(model->entry(QStringLiteral("server"))); QVERIFY(entry);
    QSignalSpy statuses{model.get(), &CoreSettingsModel::statusesChanged}, changed{model.get(), &CoreSettingsModel::operationChanged};
    QSignalSpy value{entry, &CoreSettingEntryModel::valueChanged}, enabled{entry, &CoreSettingEntryModel::enabledChanged};
    QSignalSpy address{entry, &CoreSettingEntryModel::addressChanged}, finished{model.get(), &CoreSettingsModel::operationFinished};
    const auto stop = [&] { if (destroy) model.reset(); else model->close(); };
    connect(model.get(), &CoreSettingsModel::statusesChanged, this, [&] { if (stage == 0) stop(); });
    connect(entry, &CoreSettingEntryModel::valueChanged, this, [&] { if (stage == 1) stop(); });
    connect(entry, &CoreSettingEntryModel::enabledChanged, this, [&] { if (stage == 2) stop(); });
    connect(entry, &CoreSettingEntryModel::addressChanged, this, [&] { if (stage == 3) stop(); });
    connect(model.get(), &CoreSettingsModel::operationFinished, this, [&](auto, auto) { if (stage == 5) stop(); });
    int completions{0}; auto preview = initial; preview.server = true; transport.rejectLane(execution::Lane::Domain);
    QCOMPARE(model->applyPreviewValuesPreservingTouched(preview, {}, [&](bool accepted) {
        QVERIFY(!accepted); ++completions; if (stage == 4) stop();
    }), quint64{0});
    QVERIFY(Settle(transport, gui)); QCOMPARE(statuses.size(), 1); QCOMPARE(value.size(), stage >= 1 ? 1 : 0);
    QCOMPARE(enabled.size(), stage >= 2 ? 1 : 0); QCOMPARE(address.size(), stage >= 3 ? 1 : 0);
    QCOMPARE(completions, stage >= 4 ? 1 : 0); QCOMPARE(finished.size(), stage == 5 ? 1 : 0); QCOMPARE(changed.size(), 1);
    if (destroy) QVERIFY(!model);
    else { QVERIFY(!model->presentationOpen()); QVERIFY(!model->pending()); QVERIFY(!model->values().server); }
    transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualDesiredPublicationRetiresWithItsOwner_data()
{
    QTest::addColumn<int>("stage"); QTest::addColumn<bool>("destroy");
    const char* stages[]{"queued", "before", "value", "after"};
    for (int stage = 0; stage < 4; ++stage) for (bool destroy : {false, true}) {
        const QString tag = QString::fromLatin1(stages[stage]) + (destroy ? QStringLiteral("-destroy") : QStringLiteral("-close"));
        QTest::newRow(qPrintable(tag)) << stage << destroy;
    }
}

void CoreSettingsModelTests::manualDesiredPublicationRetiresWithItsOwner()
{
    QFETCH(int, stage); QFETCH(bool, destroy);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.listen = false; auto persisted = initial;
    auto model = std::make_unique<CoreSettingsModel>(initial);
    model->configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    auto* entry = qobject_cast<CoreSettingEntryModel*>(model->entry(QStringLiteral("listen"))); QVERIFY(entry);
    std::vector<SettingsHeldEffect> held; int calls{0}, writes{0}, before{0}, after{0};
    model->setEffectPort([&](const auto& change, auto, auto action, auto done, auto failed) {
        ++calls; held.push_back({change, std::move(action), std::move(done), std::move(failed)});
    }, {}, {});
    const auto stop = [&] { if (destroy) model.reset(); else model->close(); };
    QSignalSpy value{entry, &CoreSettingEntryModel::valueChanged}, finished{model.get(), &CoreSettingsModel::operationFinished};
    model->setBeforeChangeHandler([&](auto origin) { QCOMPARE(origin, CoreSettingsModel::ChangeOrigin::User); ++before; if (stage == 1) stop(); });
    model->setAfterChangeHandler([&](const auto&, auto origin) { QCOMPARE(origin, CoreSettingsModel::ChangeOrigin::User); ++after; if (stage == 3) stop(); });
    connect(entry, &CoreSettingEntryModel::valueChanged, this, [&] { if (stage == 2) stop(); });
    QVERIFY(model->changeListen(true)); transport.drain(); QCOMPARE(calls, 1); QCOMPARE(held.size(), size_t{1}); QCOMPARE(writes, 0);
    if (stage == 0) stop();
    QVERIFY(Settle(transport, gui)); QCOMPARE(before, stage > 0 ? 1 : 0); QCOMPARE(after, stage == 3 ? 1 : 0);
    QCOMPARE(value.size(), stage >= 2 ? 1 : 0); QCOMPARE(finished.size(), 0);
    if (destroy) QVERIFY(!model);
    else { QVERIFY(!model->presentationOpen()); QCOMPARE(model->values().listen, stage >= 2); }
    auto effect = std::move(held.front()); held.clear();
    QVERIFY(execution::Submit(*effect.action, [&] { ++writes; persisted.listen = true;
        return CoreSettingsModel::EffectResult{{}, persisted, {}, {}}; }, std::move(effect.done), std::move(effect.failed)));
    effect.action.reset(); QVERIFY(Settle(transport, gui)); QCOMPARE(writes, 1); QVERIFY(persisted.listen); QCOMPARE(finished.size(), 0);
    transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualOlderDesiredFrameCannotReplaceNewerIntent()
{
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; initial.listen = false; initial.server = false; auto persisted = initial;
    CoreSettingsModel model{initial}; model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }});
    QVERIFY(Settle(transport, gui)); std::vector<SettingsHeldEffect> held; int before{0}, writes{0};
    model.setBeforeChangeHandler([&](auto) { ++before; });
    model.setEffectPort([&](const auto& change, auto, auto action, auto done, auto failed) {
        held.push_back({change, std::move(action), std::move(done), std::move(failed)});
    }, {}, {});
    QVERIFY(model.changeListen(true)); QVERIFY(model.changeServer(true)); transport.drain();
    QCOMPARE(held.size(), size_t{2}); QCOMPARE(gui.size(), size_t{2});
    auto newest = std::move(gui.back()); gui.pop_back(); newest();
    QVERIFY(model.values().listen); QVERIFY(model.values().server); QCOMPARE(before, 1);
    QVERIFY(Publish(gui)); QCOMPARE(before, 1); QVERIFY(model.values().server);
    for (auto& effect : held) {
        const auto change = effect.change;
        QVERIFY(execution::Submit(*effect.action, [&, change] {
            ++writes;
            if (change.setting_name == QStringLiteral("listen")) persisted.listen = change.after.listen;
            if (change.setting_name == QStringLiteral("server")) persisted.server = change.after.server;
            return CoreSettingsModel::EffectResult{{}, persisted, {}, {}};
        }, std::move(effect.done), std::move(effect.failed)));
        effect.action.reset();
    }
    held.clear(); QVERIFY(Settle(transport, gui)); QCOMPARE(writes, 2); QVERIFY(!model.pending());
    QVERIFY(QmlCoreSettings::ValuesEqual(model.values(), persisted)); QVERIFY(persisted.listen); QVERIFY(persisted.server);
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualDraftObserverLossAtHeldOutcome_data()
{
    QTest::addColumn<bool>("opening"); QTest::addColumn<bool>("failure"); QTest::addColumn<int>("retire");
    for (bool opening : {false, true}) for (bool failure : {false, true}) for (int retire = 0; retire < 3; ++retire) {
        const QString tag = QStringLiteral("%1-%2-%3").arg(opening ? QStringLiteral("opening") : QStringLiteral("validation"),
            failure ? QStringLiteral("failure") : QStringLiteral("success")).arg(retire);
        QTest::newRow(qPrintable(tag)) << opening << failure << retire;
    }
}

void CoreSettingsModelTests::manualDraftObserverLossAtHeldOutcome()
{
    QFETCH(bool, opening); QFETCH(bool, failure); QFETCH(int, retire);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui;
    QmlCoreSettings::Values initial; auto model = std::make_unique<CoreSettingsModel>(initial);
    model->configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    SettingsWriteProbe effect{initial}; effect.install(*model);
    if (opening && failure) transport.rejectLane(execution::Lane::Domain);
    std::unique_ptr<CoreProxyDraft> draft{model->createProxyDraft()}; QVERIFY(draft);
    if (!opening) {
        QVERIFY(Settle(transport, gui));
        if (failure) transport.rejectLane(execution::Lane::Domain);
        draft->setProxyEnabled(true);
    }
    transport.drain(); QSignalSpy changed{draft.get(), &CoreProxyDraft::changed};
    if (retire == 0) model->close();
    else if (retire == 1) model.reset();
    else draft.reset();
    if (draft) {
        draft->reset(); draft->setProxyEnabled(true); draft->setProxyAddress(QStringLiteral("192.0.2.4:9050"));
        draft->setTorEnabled(true); draft->setTorAddress(QStringLiteral("[::1]:9150"));
        QVERIFY(!draft->canApply()); QCOMPARE(draft->requestApply(), quint64{0});
    }
    QVERIFY(Settle(transport, gui)); QCOMPARE(changed.size(), 0); QCOMPARE(effect.calls, 0); QCOMPARE(effect.writes, 0);
    if (model) { QVERIFY(QmlCoreSettings::ValuesEqual(model->values(), initial)); model->close(); }
    transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualQueuedResetDoesNotDuplicateANewerOpening_data()
{
    QTest::addColumn<bool>("finish_newer_first");
    QTest::newRow("older-followup-while-new-opening-pending") << false;
    QTest::newRow("older-followup-after-new-opening-completes") << true;
}

void CoreSettingsModelTests::manualQueuedResetDoesNotDuplicateANewerOpening()
{
    QFETCH(bool, finish_newer_first);
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui; CoreSettingsModel model;
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    std::unique_ptr<CoreProxyDraft> draft{model.createProxyDraft()}; QVERIFY(draft); draft->reset(); transport.drain();
    QVERIFY(Publish(gui)); QVERIFY(!gui.empty()); QVERIFY(!draft->ready());
    draft->reset();
    const auto decisions = [&] { return transport.queuedCountIf([](const auto& turn) {
        return turn.lane == execution::Lane::Domain && turn.phase == execution::Phase::Queued;
    }); };
    QCOMPARE(decisions(), size_t{1});
    if (finish_newer_first) {
        transport.drain(); QVERIFY(gui.size() >= size_t{2});
        auto opened = std::move(gui.back()); gui.pop_back(); opened();
        transport.drain(); QVERIFY(gui.size() >= size_t{2});
        auto validated = std::move(gui.back()); gui.pop_back(); validated();
        QVERIFY(draft->ready()); QVERIFY(!draft->pending());
    }
    QVERIFY(Publish(gui)); QCOMPARE(decisions(), finish_newer_first ? size_t{0} : size_t{1});
    QVERIFY(Settle(transport, gui)); QVERIFY(draft->ready()); QVERIFY(!draft->pending()); QVERIFY(draft->canApply());
    QVERIFY(!draft->dirty()); QVERIFY(model.error().isEmpty());
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::manualQueuedValidationDoesNotDuplicateNewerInput()
{
    ManualRoutedExecution transport; std::deque<std::function<void()>> gui; CoreSettingsModel model;
    model.configureServices({transport.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }}); QVERIFY(Settle(transport, gui));
    std::unique_ptr<CoreProxyDraft> draft{model.createProxyDraft()}; QVERIFY(Settle(transport, gui));
    draft->setProxyEnabled(true); transport.drain(); draft->setProxyAddress(QStringLiteral("192.0.2.5:9050"));
    QVERIFY(draft->proxyError().isEmpty()); QVERIFY(draft->torError().isEmpty()); QVERIFY(Publish(gui));
    QVERIFY(!gui.empty());
    draft->setTorEnabled(true);
    const auto decisions = [&] { return transport.queuedCountIf([](const auto& turn) {
        return turn.lane == execution::Lane::Domain && turn.phase == execution::Phase::Queued;
    }); };
    QCOMPARE(decisions(), size_t{1}); QVERIFY(Publish(gui)); QCOMPARE(decisions(), size_t{1});
    QVERIFY(Settle(transport, gui)); QVERIFY(draft->canApply()); QVERIFY(draft->dirty());
    QCOMPARE(draft->proxyAddress(), QStringLiteral("192.0.2.5:9050")); QVERIFY(draft->torEnabled());
    QVERIFY(!model.values().proxy_enabled); QVERIFY(!model.values().tor_enabled);
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void CoreSettingsModelTests::realDefaultPortsInitializeAtTheFirstPublicCommand_data()
{
    QTest::addColumn<int>("first_command");
    QTest::newRow("entry") << 0;
    QTest::newRow("change") << 1;
    QTest::newRow("draft") << 2;
}

void CoreSettingsModelTests::realDefaultPortsInitializeAtTheFirstPublicCommand()
{
    QFETCH(int, first_command);
    const auto drain_native = DrainExecutorsBeforeNativeOwnerDestruction(); CoreSettingsModel model;
    QVERIFY(!model.pending()); QCOMPARE(model.operationState(), CoreSettingsModel::Idle);
    std::unique_ptr<CoreProxyDraft> draft;
    if (first_command == 0) QVERIFY(model.entry(QStringLiteral("listen")));
    else if (first_command == 1) QVERIFY(model.changeServer(true));
    else { draft.reset(model.createProxyDraft()); QVERIFY(draft); }
    QTRY_VERIFY(!model.pending());
    if (draft) QTRY_VERIFY(draft->ready() && !draft->pending());
    QVERIFY(model.error().isEmpty()); QCOMPARE(model.values().server, first_command == 1);
    QCOMPARE(model.defaultProxyAddress(), QStringLiteral("127.0.0.1:9050"));
    model.close(); bool drained{false}; model.whenEffectsDrained([&] { drained = true; }); QTRY_VERIFY(drained);
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(CoreSettingsModelTests)
#else
QTEST_MAIN(CoreSettingsModelTests)
#endif
#include <test_core_settings_model.moc>
