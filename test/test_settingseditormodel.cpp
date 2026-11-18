// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/settingseditormodel.h>
#include <qml/models/options_model.h>
#include <qml/models/settingsnavigationcontroller.h>
#include <qml/guiargs.h>
#include <common/args.h>
#include <init.h>
#include <univalue.h>
#include <test/mocks/mocknode.h>
#include <QTemporaryDir>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QThread>
#include <deque>
#include <atomic>
#include <stdexcept>
namespace {
using namespace effectivesettings;
using execution::Lane;
Snapshot Original(){Snapshot result;result.ready=true;result.revision="initial";for(size_t i=0;i<settingsedit::FIELDS;++i){const auto key=static_cast<Key>(i);Value value=Type(key)==Kind::Boolean?Value{key==Key::Listen}:Type(key)==Kind::Text?Value{QString{}}:Value{int64_t{key==Key::Prune||key==Key::Threads?0:key==Key::Dbcache?450:300}};Field field{key,Type(key),value,value,value};if(key==Key::Natpmp||key==Key::Signer)field.applicability=Applicability::Live;result.fields.push_back(std::move(field));}return result;}
struct Fixture {
    std::deque<std::function<void()>> gui;
    ManualStartupExecution shared;
    Snapshot source{Original()};
    bool reject{false},throw_read{false},throw_write{false},runtime_failure{false},cancel_write{false};
    int reads{0},checks{0},writes{0};
    bool native_released{false};
    std::unique_ptr<SettingsEditorModel> model;
    Fixture()
    {
        SettingsEditorModel::Ports ports;
        ports.post = [this](auto fn) { gui.push_back(std::move(fn)); };
        ports.executor = &shared.executor();
        ports.retired=[this]{native_released=true;};ports.read=[this]{++reads;if(throw_read)throw std::runtime_error("read failed");return source;};ports.review=[this](const auto& save){++checks;return Check{!reject&&save.original.revision==source.revision,{}, {},reject?QStringLiteral("Rejected whole batch"):QString{}};};ports.write=[this](const auto& save){if(std::exchange(cancel_write,false))throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};++writes;if(throw_write)throw 7;Result result;result.outcome=runtime_failure?Outcome::Uncertain:Outcome::Applied;result.write_completed=result.persisted=true;result.runtime_confirmed=!runtime_failure;if(runtime_failure)result.error="runtime failed";for(const auto& edit:save.batch.edits){auto& field=source.fields.at(size_t(edit.key));field.committed=edit.value;field.next_start=edit.value;field.has_override=edit.intent==Intent::ExplicitOverride;field.provenance=Provenance::Persistent;result.persisted_values[edit.key]=field.has_override?std::make_optional(edit.value):std::nullopt;}source.revision=QString::number(writes);result.snapshot=source;return result;};model=std::make_unique<SettingsEditorModel>(std::move(ports));}
    ~Fixture()
    {
        model.reset();
        if (!settle() || !shared.quiescent()) {
            QTest::qFail("Editor destruction left active work", __FILE__, __LINE__);
        }
    }
    bool publish(){if(gui.empty())return false;auto fn=std::move(gui.front());gui.pop_front();fn();return true;}
    bool step(Lane lane)
    {
        return shared.step(lane == Lane::Domain ? Lane::Utility : lane);
    }
    bool settle()
    {
        for (int i = 0; i < 4096; ++i) {
            if (shared.step(Lane::Core) || shared.step(Lane::Utility)) continue;
            if (!publish()) return shared.quiescent();
        }
        return false;
    }
    void domain(){for(int i=0;i<1024&&step(Lane::Domain);++i){}while(publish()){};}
    SettingsEntryModel* entry(const char* key){return model->entry(QString::fromLatin1(key));}
};
}
class SettingsEditorModelTests:public QObject {
    Q_OBJECT
private Q_SLOTS:
    void ownerCloseClearsNavigationEditorBeforeDeletingItsEntries()
    {
        Fixture fixture;
        QVERIFY(fixture.settle());
        struct Source : SettingsEditorSource {
            explicit Source(SettingsEditorModel& editor) : model(editor) {}
            SettingsEditorModel* createSettingsEditor() override { return &model; }
            SettingsEditorModel& model;
        } source{*fixture.model};
        SettingsNavigationController navigation;
        navigation.setSource(&source);
        QCOMPARE(navigation.editor(), fixture.model.get());

        QSignalSpy editor_changed{&navigation, &SettingsNavigationController::editorChanged};
        const QPointer<SettingsEntryModel> entry{fixture.entry("proxy")};
        bool entry_alive_at_detach{false};
        connect(&navigation, &SettingsNavigationController::editorChanged, this, [&] {
            entry_alive_at_detach = entry && navigation.editor() == nullptr;
        });
        fixture.model->close();
        QVERIFY(fixture.settle());

        QCOMPARE(editor_changed.count(), 1);
        QVERIFY(entry_alive_at_detach);
        QVERIFY(navigation.editor() == nullptr);
    }

    void actualWorkersKeepCoreCallsOffGuiAndPublishTheReceiptOnGui(){
        const auto gui = QThread::currentThread();
        std::atomic<int> reads{0}, writes{0};
        std::atomic<bool> on_gui{false};
        auto source = std::make_shared<Snapshot>(Original());
        execution::Executor executor{execution::Executor::Config{}};
        SettingsEditorModel::Ports ports;
        ports.executor = &executor;
        ports.read=[&]{on_gui.store(on_gui.load()||QThread::currentThread()==gui);++reads;return *source;};
        ports.review=[&](const auto&){on_gui.store(on_gui.load()||QThread::currentThread()==gui);return Check{true,{},{},{}};};
        ports.write=[&](const auto& save){on_gui.store(on_gui.load()||QThread::currentThread()==gui);++writes;Result result;result.outcome=Outcome::Applied;result.persisted=result.write_completed=result.runtime_confirmed=true;for(const auto& edit:save.batch.edits){auto& field=source->fields[size_t(edit.key)];field.committed=field.next_start=edit.value;field.has_override=true;result.persisted_values[edit.key]=edit.value;}source->revision="saved";result.snapshot=*source;return result;};
        auto model=std::make_unique<SettingsEditorModel>(std::move(ports));bool wrong_publication{false};connect(model.get(),&SettingsEditorModel::changed,this,[&]{wrong_publication|=QThread::currentThread()!=gui;});QTRY_VERIFY(model->fresh());model->entry("server")->editBoolean(true);QTRY_VERIFY(model->canSave());model->save();QTRY_VERIFY(model->lastSavePersisted());QCOMPARE(writes.load(),1);QVERIFY(reads.load()>=1);QVERIFY(!on_gui.load());QVERIFY(!wrong_publication);bool drained{false};model->retire([&]{drained=true;});QTRY_VERIFY(drained);model.reset();executor.stopAndJoin();
    }
    void closedSchedulerStillAcknowledgesRetirement()
    {
        std::deque<std::function<void()>> gui;
        int reads{0}, released{0};
        SettingsEditorModel::Ports ports;
        ports.post = [&](auto fn) { gui.push_back(std::move(fn)); };
        ports.read = [&] { ++reads; return Original(); };
        ports.retired = [&] { ++released; };
        SettingsEditorModel model{std::move(ports)};
        QSignalSpy closed{&model, &SettingsEditorModel::closed};
        QSignalSpy drained{&model, &SettingsEditorModel::shutdownFinished};
        bool done{false};
        model.retire([&] { done = true; });
        while (!gui.empty()) {
            auto fn = std::move(gui.front()); gui.pop_front(); fn();
        }
        QCOMPARE(reads, 0);
        QCOMPARE(released, 1);
        QVERIFY(done);
        QCOMPARE(model.phase(), SettingsEditorModel::Closed);
        QVERIFY(!model.error().isEmpty());
        QCOMPARE(closed.count(), 1);
        QCOMPARE(drained.count(), 1);
    }
    void gettersDoNoNativeWork(){Fixture f;for(int i=0;i<100;++i){QVERIFY(!f.model->fresh());QVERIFY(!f.model->canSave());QCOMPARE(f.entry("prune")->text(),QString{});}QCOMPARE(f.reads,0);QCOMPARE(f.writes,0);QVERIFY(f.settle());QCOMPARE(f.reads,1);QVERIFY(f.model->fresh());}
    void olderProjectionCannotRewriteTheSuccessorLiteralBeforeTheNextKey(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("4");while(f.step(Lane::Domain)){}QVERIFY(!f.gui.empty());f.entry("maxmempool")->editText("45");QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("45"));while(f.publish()){}QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("45"));f.entry("maxmempool")->editText(f.entry("maxmempool")->text()+QStringLiteral("0"));QVERIFY(f.settle());QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("450"));f.model->save();QVERIFY(f.settle());QCOMPARE(f.writes,1);QCOMPARE(std::get<int64_t>(f.source.fields[size_t(Key::Mempool)].committed),450);}
    void olderToggleProjectionCannotRetargetANewerToggle(){Fixture f;QVERIFY(f.settle());f.entry("natpmp")->editBoolean(true);while(f.step(Lane::Domain)){}f.entry("natpmp")->editBoolean(false);QVERIFY(!f.entry("natpmp")->booleanValue());while(f.publish()){}QVERIFY(!f.entry("natpmp")->booleanValue());QVERIFY(f.settle());QVERIFY(!f.entry("natpmp")->booleanValue());QVERIFY(f.model->dirty());QCOMPARE(f.writes,0);f.model->save();QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(!std::get<bool>(f.source.fields[size_t(Key::Natpmp)].committed));}
    void rejectedSuccessorInputPreservesTheOriginalAdmittedLiteral(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("450");while(f.step(Lane::Domain)){}f.shared.reject_utility=true;QSignalSpy rejected{f.model.get(),&SettingsEditorModel::commandRejected};QSignalSpy projected{f.entry("maxmempool"),&SettingsEntryModel::changed};f.entry("maxmempool")->editText("900");QCOMPARE(rejected.count(),1);QCOMPARE(projected.count(),1);QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("450"));QVERIFY(f.settle());QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("450"));QCOMPARE(f.writes,0);}
    void typingAheadOfTheDomainKeepsEveryKeystroke(){
        // Each keystroke is one queued edit. Hold Utility while submitting
        // an entire entry to exercise admission of every keystroke.
        Fixture f;QVERIFY(f.settle());
        QSignalSpy rejected{f.model.get(),&SettingsEditorModel::commandRejected};
        const QString address{QStringLiteral("10.0.0.1:9050/socks-proxy-with-a-long-name")};
        for(qsizetype i=1;i<=address.size();++i){f.entry("proxy")->editText(address.left(i));QCOMPARE(f.entry("proxy")->text(),address.left(i));}
        QCOMPARE(rejected.count(),0);
        QVERIFY(f.settle());
        QCOMPARE(f.entry("proxy")->text(),address);QVERIFY(f.model->dirty());
    }
    void admittedInputCannotSkipTheOriginalCloseConsent(){Fixture f;QVERIFY(f.settle());f.entry("proxy")->editText("127.0.0.1:9050");QVERIFY(f.model->dirty());QVERIFY(f.model->busy());QVERIFY(!f.model->canSave());f.model->requestClose();QVERIFY(f.settle());QVERIFY(f.model->closeQuestion());QCOMPARE(f.entry("proxy")->text(),QStringLiteral("127.0.0.1:9050"));QCOMPARE(f.writes,0);QVERIFY(f.model->closePrompt());f.model->closePrompt()->answer(TextEditClosePrompt::KeepEditing);QVERIFY(f.settle());QVERIFY(f.model->dirty());QVERIFY(!f.model->closeQuestion());}
    void pendingSuccessorCannotSaveThePreviousValidInput(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("450");QVERIFY(f.settle());QVERIFY(f.model->canSave());f.entry("maxmempool")->editText("450x");QVERIFY(!f.model->canSave());QVERIFY(f.model->busy());f.model->save();QVERIFY(f.settle());QCOMPARE(f.writes,0);QCOMPARE(f.checks,0);QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("450x"));QVERIFY(!f.entry("maxmempool")->error().isEmpty());QVERIFY(!f.model->canSave());}
    void saveIsValidationThenCoreThenDomainThenPresentation(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");f.domain();QVERIFY(f.model->canSave());f.model->save();f.domain();QCOMPARE(f.model->phase(),SettingsEditorModel::Checking);QCOMPARE(f.checks,0);QCOMPARE(f.writes,0);QVERIFY(f.step(Lane::Core));QCOMPARE(f.checks,1);QCOMPARE(f.writes,0);f.domain();QCOMPARE(f.model->phase(),SettingsEditorModel::Saving);QVERIFY(f.step(Lane::Core));QCOMPARE(f.writes,1);QVERIFY(!f.model->lastSavePresent());QVERIFY(f.settle());QVERIFY(f.model->lastSavePersisted());QVERIFY(!f.model->dirty());QCOMPARE(f.entry("maxmempool")->effectiveText(),QStringLiteral("300"));QCOMPARE(f.entry("maxmempool")->nextStartText(),QStringLiteral("400"));QVERIFY(f.model->restartRequired());}
    void originalWriteFollowsReleasedReviewBudgetUnderUnrelatedPressure(){
        int unrelated{0};Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());
        f.model->save();f.domain();QVERIFY(f.step(Lane::Core));QCOMPARE(f.checks,1);
        const auto background=f.shared.core.post([&]{++unrelated;});
        QVERIFY(background);f.domain();QCOMPARE(f.model->phase(),SettingsEditorModel::Saving);
        QCOMPARE(f.writes,0);QCOMPARE(unrelated,0);QVERIFY(f.model->error().isEmpty());
        QVERIFY(f.step(Lane::Core));QCOMPARE(unrelated,1);f.domain();
        QVERIFY(f.step(Lane::Core));QCOMPARE(f.writes,1);QVERIFY(f.settle());
        QVERIFY(f.model->lastSavePersisted());QVERIFY(f.model->lastSaveKnown());
        QCOMPARE(std::get<int64_t>(f.source.fields[size_t(Key::Mempool)].committed),400);
    }
    void originalHandoffCannotConsumeASuccessorEdit(){
        Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());
        f.model->save();f.domain();QVERIFY(f.step(Lane::Core));
        f.domain();
        QVERIFY(f.shared.hasWork(Lane::Core));
        QCOMPARE(f.model->phase(),SettingsEditorModel::Saving);QCOMPARE(f.writes,0);
        f.entry("maxmempool")->editText("500");f.domain();
        QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("500"));QCOMPARE(f.writes,0);
        QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(f.model->lastSavePersisted());
        QCOMPARE(std::get<int64_t>(f.source.fields[size_t(Key::Mempool)].committed),400);
        QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("500"));QVERIFY(f.model->dirty());
    }
    void originalHandoffSurvivesParentCloseAndFacadeRetirement(){
        Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());
        f.model->save();f.domain();QVERIFY(f.step(Lane::Core));
        f.domain();
        QVERIFY(f.shared.hasWork(Lane::Core));
        QCOMPARE(f.model->phase(),SettingsEditorModel::Saving);QCOMPARE(f.writes,0);
        f.model->retire();
        f.shared.drain();QCOMPARE(f.writes,1);QVERIFY(f.native_released);QVERIFY(f.shared.quiescent());
        QVERIFY(f.model);QVERIFY(f.settle());QVERIFY(f.model->lastSavePersisted());
        QCOMPARE(f.model->phase(),SettingsEditorModel::Closed);
    }
    void refusedHandoffOwnsAKnownNoWriteOutcomeWithoutRetry(){
        Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());
        f.model->save();f.domain();QVERIFY(f.step(Lane::Core));
        f.shared.reject_core=true;QVERIFY(f.step(Lane::Domain));QVERIFY(f.settle());
        QCOMPARE(f.checks,1);QCOMPARE(f.writes,0);QVERIFY(f.model->lastSaveKnown());QVERIFY(!f.model->lastSavePersisted());
        QVERIFY(!f.model->busy());QVERIFY(f.model->dirty());QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("400"));
        f.model->reconcile();QVERIFY(f.settle());QCOMPARE(f.writes,0);QCOMPARE(f.checks,1);
    }
    void cancelledAcceptedHandoffSettlesItsOriginalNoWriteReceipt(){
        Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());
        f.model->save();f.domain();QVERIFY(f.step(Lane::Core));
        f.domain();
        QVERIFY(f.shared.hasWork(Lane::Core));
        QCOMPARE(f.model->phase(),SettingsEditorModel::Saving);QCOMPARE(f.writes,0);
        const auto held=f.shared.take(Lane::Core);QVERIFY(held);
        f.cancel_write=true; held();
        QVERIFY(f.settle());QCOMPARE(f.checks,1);QCOMPARE(f.writes,0);
        QVERIFY(f.model->lastSavePresent());QVERIFY(f.model->lastSaveKnown());QVERIFY(!f.model->lastSavePersisted());
        QVERIFY(!f.model->busy());QVERIFY(f.model->dirty());QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("400"));
        f.model->reconcile();QVERIFY(f.settle());QCOMPARE(f.writes,0);QCOMPARE(f.checks,1);
    }
    void validationFailureWritesNothingAndRetainsTheEdit(){Fixture f;QVERIFY(f.settle());f.reject=true;f.entry("listen")->editBoolean(false);QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QCOMPARE(f.checks,1);QCOMPARE(f.writes,0);QVERIFY(f.model->dirty());QVERIFY(f.model->canSave());QCOMPARE(f.model->error(),QStringLiteral("Rejected whole batch"));}
    void editHeldAgainstValidationPreventsTheOldWrite(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();f.domain();const auto held=f.shared.take(Lane::Core);QVERIFY(held);f.entry("maxmempool")->editText("500");f.domain();held();QVERIFY(f.settle());QCOMPARE(f.writes,0);QVERIFY(f.model->dirty());QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("500"));}
    void cancelledWriteIsKnownNotStartedAndCanBeRetried(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();f.domain();QVERIFY(f.step(Lane::Core));f.domain();const auto held=f.shared.take(Lane::Core);QVERIFY(held);f.cancel_write=true;f.domain();held();QVERIFY(f.settle());QCOMPARE(f.writes,0);QVERIFY(f.model->lastSaveKnown());QVERIFY(!f.model->lastSavePersisted());QVERIFY(f.model->canSave());f.model->save();QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(f.model->lastSavePersisted());}
    void rejectedWriteAdmissionRetainsTheDirtyBatch(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();f.domain();QVERIFY(f.step(Lane::Core));f.shared.reject_core = true;f.domain();QVERIFY(f.settle());QCOMPARE(f.checks,1);QCOMPARE(f.writes,0);QVERIFY(f.model->dirty());QVERIFY(f.model->lastSaveKnown());QVERIFY(!f.model->lastSavePersisted());QVERIFY(f.model->canSave());}
    void liveEditorOwnsOnlyItsBoundedLifetimeRegistrations(){Fixture f;QVERIFY(f.settle());QCOMPARE(f.reads,1);QVERIFY(f.model->fresh());QVERIFY(f.shared.quiescent());f.model.reset();QVERIFY(f.settle());QVERIFY(f.shared.quiescent());}
    void successorEditSurvivesACompletedWrite(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();f.domain();QVERIFY(f.step(Lane::Core));f.domain();const auto held=f.shared.take(Lane::Core);QVERIFY(held);f.entry("maxmempool")->editText("500");f.domain();held();QVERIFY(f.settle());QCOMPARE(f.writes,1);QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("500"));QVERIFY(f.model->dirty());QVERIFY(f.model->lastSavePersisted());}
    void refreshBurstsOwnOnlyOneNecessarySuccessor(){Fixture f;f.domain();const auto held=f.shared.take(Lane::Core);QVERIFY(held);for(int i=0;i<256;++i)f.model->refresh();f.domain();QCOMPARE(f.reads,0);held();QVERIFY(f.settle());QCOMPARE(f.reads,2);QVERIFY(f.model->fresh());}
    void failedReadRetainsVisibleStaleValues(){Fixture f;QVERIFY(f.settle());f.throw_read=true;f.model->refresh();QVERIFY(f.settle());QVERIFY(!f.model->fresh());QCOMPARE(f.entry("maxmempool")->text(),QStringLiteral("300"));QVERIFY(!f.model->error().isEmpty());QVERIFY(!f.model->canSave());}
    void ordinaryCloseRejectsNewSaveEvenWithLifetimeScope(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.shared.reject_utility=true;QSignalSpy rejected{f.model.get(),&SettingsEditorModel::commandRejected};f.model->save();QCOMPARE(rejected.count(),1);QVERIFY(f.settle());QCOMPARE(f.writes,0);QVERIFY(f.model->dirty());}
    void admittedSaveDrainsAfterParentClose(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();f.domain();QVERIFY(f.step(Lane::Core));f.domain();f.model->retire();QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(f.model->lastSavePersisted());}
    void originalOptionsOwnerWaitsForADeletedEditorsNativeWrite()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        ArgsManager args;
        SetupServerArgs(args, false);
        SetupQmlGuiArgs(args);
        args.ForceSetArg("-regtest", "1");
        args.ForceSetArg("-datadir", directory.path().toStdString());
        args.ForceSetArg("-settings", directory.filePath("settings.json").toStdString());
        args.SelectConfigNetwork("regtest");
        QVERIFY(args.WriteSettingsFile());

        MockNode node;
        bool closed{false};
        int reads_after_close{0};
        node.get_persistent_setting_fn = [&](const auto& name) {
            if (closed) ++reads_after_close;
            return args.GetPersistentSetting(name);
        };
        node.map_port_fn = [&](bool) { if (closed) ++reads_after_close; };

        std::deque<std::function<void()>> gui;
        ManualStartupExecution shared;
        const auto publish = [&] {
            while (!gui.empty()) {
                auto turn = std::move(gui.front());
                gui.pop_front();
                turn();
            }
        };
        const auto settle = [&] {
            for (int i = 0; i < 4096; ++i) {
                if (shared.step(Lane::Core) || shared.step(Lane::Utility)) continue;
                if (gui.empty()) return shared.quiescent();
                publish();
            }
            return false;
        };
        const auto utility = [&] {
            while (shared.step(Lane::Utility)) {}
            publish();
        };

        {
            OptionsQmlModel options{node, args, [&](auto fn) { gui.push_back(std::move(fn)); }, &shared.executor()};
            const auto finish = qScopeGuard([&] { options.beginShutdown(); settle(); });
            QVERIFY(settle());
            auto* editor = options.createSettingsEditor();
            QVERIFY(editor);
            QVERIFY(settle());
            QVERIFY2(editor->fresh(), qPrintable(editor->error()));
            editor->entry("natpmp")->editBoolean(true);
            QVERIFY(settle());
            editor->save();
            utility();
            QCOMPARE(editor->phase(), SettingsEditorModel::Checking);
            QVERIFY(shared.step(Lane::Core)); // Review.
            utility();
            QCOMPARE(editor->phase(), SettingsEditorModel::Saving);
            QVERIFY(args.GetPersistentSetting("natpmp").isNull());

            delete editor;
            QSignalSpy drained{&options, &OptionsQmlModel::shutdownFinished};
            connect(&options, &OptionsQmlModel::shutdownFinished, this, [&] { closed = true; });
            options.beginShutdown();
            utility();
            QCOMPARE(drained.count(), 0);
            QVERIFY(shared.step(Lane::Core)); // The accepted write survives editor deletion.
            QVERIFY(args.GetPersistentSetting("natpmp").isTrue());
            publish();
            QCOMPARE(drained.count(), 0);
            QVERIFY(settle());
            QCOMPARE(drained.count(), 1);
            QCOMPARE(node.calls.mapPort.load(), 1);
            QCOMPARE(reads_after_close, 0);
        }
        QVERIFY(settle());
        QVERIFY(shared.quiescent());
    }

    void retirementWhileReviewIsPendingDoesNotWrite()
    {
        Fixture f;
        QVERIFY(f.settle());
        f.entry("maxmempool")->editText("450");
        QVERIFY(f.settle());
        f.model->save();
        f.domain();
        QCOMPARE(f.model->phase(), SettingsEditorModel::Checking);
        f.model->retire();
        QVERIFY(f.settle());
        QCOMPARE(f.checks, 1);
        QCOMPARE(f.writes, 0);
        QVERIFY(f.native_released);
        QCOMPARE(f.model->phase(), SettingsEditorModel::Closed);
    }

    void nativeRetirementDoesNotWaitForGuiPublication(){Fixture f;QVERIFY(f.settle());QSignalSpy closed{f.model.get(),&SettingsEditorModel::closed};bool acknowledged{false};f.model->retire([&]{acknowledged=true;});f.shared.drain();QVERIFY(f.native_released);QVERIFY(f.shared.quiescent());QVERIFY(!acknowledged);QVERIFY(!f.gui.empty());QVERIFY(f.model);QVERIFY(f.settle());QVERIFY(acknowledged);QCOMPARE(f.model->phase(),SettingsEditorModel::Closed);QCOMPARE(closed.count(),1);f.model->retire();QVERIFY(f.settle());QCOMPARE(closed.count(),1);}
    void admittedWriteRetiresNativelyWithTheFacadeAndGuiDeliveryHeld(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();f.domain();QVERIFY(f.step(Lane::Core));f.domain();const auto held=f.shared.take(Lane::Core);QVERIFY(held);f.model->retire();f.shared.drain();QCOMPARE(f.writes,0);QVERIFY(!f.native_released);held();f.shared.drain();QCOMPARE(f.writes,1);QVERIFY(f.native_released);QVERIFY(f.shared.quiescent());QVERIFY(f.model);QVERIFY(f.settle());QVERIFY(f.model->lastSavePersisted());QCOMPARE(f.model->phase(),SettingsEditorModel::Closed);}
    void immediateSaveIntentRejectsADuplicateWithoutReplacingItsReceipt(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());QSignalSpy rejected{f.model.get(),&SettingsEditorModel::commandRejected};f.model->save();QVERIFY(f.model->busy());QVERIFY(!f.model->canSave());f.model->save();QCOMPARE(rejected.count(),1);QCOMPARE(rejected.front().front().toString(),QStringLiteral("save"));QVERIFY(!f.model->lastSavePresent());QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(f.model->lastSavePersisted());QVERIFY(!f.model->busy());QCOMPARE(rejected.count(),1);}
    void saveRejectedBehindAHeldReadReportsItsOwnIntent(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->refresh();f.domain();const auto held=f.shared.take(Lane::Core);QVERIFY(held);QSignalSpy rejected{f.model.get(),&SettingsEditorModel::commandRejected};f.model->save();f.domain();QCOMPARE(rejected.count(),1);QCOMPARE(f.writes,0);QVERIFY(!f.model->lastSavePresent());QVERIFY(f.model->dirty());held();QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(f.model->lastSavePersisted());}
    void retirementReleasesWorkerLifetimesWhileTheFacadeRemainsAlive(){Fixture f;QVERIFY(f.settle());QVERIFY(f.shared.quiescent());bool drained{false};f.model->retire([&]{drained=true;});QVERIFY(f.settle());QVERIFY(drained);QVERIFY(f.model);QCOMPARE(f.model->phase(),SettingsEditorModel::Closed);QVERIFY(f.shared.quiescent());bool later{false};f.model->retire([&]{later=true;});QVERIFY(!later);QVERIFY(f.settle());QVERIFY(later);}
    void retirementWaitsForPhysicalWriteOutcome(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();f.domain();QVERIFY(f.step(Lane::Core));f.domain();const auto held=f.shared.take(Lane::Core);QVERIFY(held);bool drained{false};f.model->retire([&]{drained=true;});f.domain();QVERIFY(!drained);held();QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(drained);QVERIFY(f.model->lastSavePersisted());QCOMPARE(f.model->phase(),SettingsEditorModel::Closed);}
    void disappearingProjectionDoesNotCancelTheWrite(){Fixture f;QVERIFY(f.settle());f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();f.domain();QVERIFY(f.step(Lane::Core));f.domain();const auto held=f.shared.take(Lane::Core);QVERIFY(held);f.model.reset();f.domain();QVERIFY(!f.native_released);held();QVERIFY(f.settle());QVERIFY(f.native_released);QCOMPARE(f.writes,1);QCOMPARE(std::get<int64_t>(f.source.fields[8].committed),400);}
    void unknownWriteQueryNeverRepeatsTheMutation(){Fixture f;QVERIFY(f.settle());f.throw_write=true;f.entry("maxmempool")->editText("400");QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QCOMPARE(f.model->phase(),SettingsEditorModel::Uncertain);QCOMPARE(f.writes,1);f.model->refresh();f.model->reconcile();f.model->save();QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(!f.model->lastSaveKnown());}
    void knownSavedFileIsNotErasedByRuntimeFailure(){Fixture f;QVERIFY(f.settle());f.runtime_failure=true;f.entry("natpmp")->editBoolean(true);QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QCOMPARE(f.writes,1);QVERIFY(f.model->lastSavePersisted());QVERIFY(f.model->lastSaveKnown());QVERIFY(!f.model->dirty());QVERIFY(!f.model->restartRequired());QVERIFY(!f.model->error().isEmpty());}
    void liveOnlyChangesDoNotRequestARestart(){Fixture f;QVERIFY(f.settle());f.entry("natpmp")->editBoolean(true);QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QVERIFY(!f.model->restartRequired());QCOMPARE(f.entry("natpmp")->effectiveText(),QStringLiteral("true"));}
    void restartAcknowledgementDoesNotPretendToApplySettings(){Fixture f;QVERIFY(f.settle());f.entry("listen")->editBoolean(false);QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QVERIFY(f.model->restartRequired());QVERIFY(f.model->restartNoticeVisible());f.model->acknowledgeRestart();QVERIFY(f.settle());QVERIFY(f.model->restartRequired());QVERIFY(!f.model->restartNoticeVisible());f.entry("server")->editBoolean(true);QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QVERIFY(f.model->restartNoticeVisible());}
    void revertingTheSavedNextStartRemovesRestartRequirement(){Fixture f;QVERIFY(f.settle());f.entry("listen")->editBoolean(false);QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QVERIFY(f.model->restartRequired());f.entry("listen")->editBoolean(true);QVERIFY(f.settle());f.model->save();QVERIFY(f.settle());QVERIFY(!f.model->restartRequired());}
    void invalidSaveAndCloseDoesNotCloseALaterSave_data()
    {
        QTest::addColumn<QString>("invalid");
        QTest::newRow("cleared") << QString{};
        QTest::newRow("not-a-number") << QStringLiteral("300x");
    }
    void invalidSaveAndCloseDoesNotCloseALaterSave()
    {
        QFETCH(QString, invalid);
        Fixture f; QVERIFY(f.settle());
        QSignalSpy closed{f.model.get(), &SettingsEditorModel::closed};
        f.entry("maxmempool")->editText(invalid); QVERIFY(f.settle());
        f.model->requestClose(); QVERIFY(f.settle()); QVERIFY(f.model->closePrompt());
        f.model->closePrompt()->answer(TextEditClosePrompt::Save); QVERIFY(f.settle());
        QCOMPARE(f.writes, 0); QVERIFY(!f.model->closeQuestion()); QVERIFY(f.model->dirty());
        f.entry("maxmempool")->editText(QStringLiteral("400")); QVERIFY(f.settle());
        f.model->save(); QVERIFY(f.settle());
        QCOMPARE(f.writes, 1); QVERIFY(f.model->lastSavePersisted());
        QCOMPARE(closed.count(), 0); QCOMPARE(f.model->phase(), SettingsEditorModel::Editing);
        f.entry("maxmempool")->editText(QStringLiteral("500")); QVERIFY(f.settle()); QVERIFY(f.model->canSave());
    }
    void oneClosePolicyRetainsOrDiscardsTheExactDraft(){Fixture f;QVERIFY(f.settle());f.entry("proxy")->editText("127.0.0.1:9050");QVERIFY(f.settle());f.model->requestClose();QVERIFY(f.settle());QVERIFY(f.model->closeQuestion());const QPointer<TextEditClosePrompt> old{f.model->closePrompt()};QVERIFY(old);f.entry("proxy")->editText("127.0.0.1:9150");QVERIFY(f.settle());QVERIFY(f.model->closePrompt()!=old.data());if(old)old->answer(TextEditClosePrompt::Discard);QVERIFY(f.settle());QVERIFY(f.model->dirty());f.model->closePrompt()->answer(TextEditClosePrompt::KeepEditing);QVERIFY(f.settle());QVERIFY(!f.model->closeQuestion());QVERIFY(f.model->dirty());QCOMPARE(f.writes,0);f.model->requestClose();QVERIFY(f.settle());f.model->closePrompt()->answer(TextEditClosePrompt::Discard);QVERIFY(f.settle());QCOMPARE(f.model->phase(),SettingsEditorModel::Closed);QCOMPARE(f.writes,0);}
};
BITCOINQML_REGISTER_QT_TEST(SettingsEditorModelTests)
#include <test_settingseditormodel.moc>
