// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/onboardingflow.h>
#include <qml/startuppreparation.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/guiargs.h>
#include <common/args.h>
#include <init.h>
#include <QTemporaryDir>
#include <QSignalSpy>
#include <qml/executor.h>
#include <QThread>
#include <QScopeGuard>
#include <test/mocks/manualstartupexecution.h>
#include <QtTest/QtTest>

#include <atomic>
#include <deque>
#include <memory>
#include <utility>
#include <stdexcept>

namespace {
QmlOnboardingSettings::ApplyRequest Request()
{
    QmlOnboardingSettings::ApplyRequest request;
    request.data_dir.path=QStringLiteral("/original/profile");
    request.touched_settings={QStringLiteral("listen")};
    request.values.listen=false;
    return request;
}
QmlOnboardingSettings::ProfileResult Saved()
{
    QmlOnboardingSettings::ProfileResult result;
    result.completed=true;result.replacement_written=true;result.directory_saved=true;result.cleanup_completed=true;
    return result;
}
void Ready(onboarding::Flow& flow,uint64_t revision=1)
{
    flow.edit(revision);
    flow.checked({revision,true,true,true,{}});
}
// Independent queues let tests advance the producer while the consumer and its
// GUI publication are held.
struct AsyncStartup {
    ManualStartupExecution execution;
    std::shared_ptr<startup::preparation::Coordinator> owner;
    std::deque<startup::preparation::CompleteTransport> pending;
    startup::preparation::CompleteResult retained_completion;
    bool retired{false}, duplicate_completion{false}, retain_completion{false};
    bool saved_complete{false}, saved_written{false}, saved_directory{false}, saved_cleanup{false};
    int starts{0}, reads{0}, transports{0}, preparations{0}, closes{0}, retire_effects{0}, terminal_receipts{0};
    uint64_t delivered_inspection{0};
    startup::preparation::Profile profile{QStringLiteral("/original/profile"),QStringLiteral("regtest")};
    QString observed_directory;
    AsyncStartup() {
        startup::preparation::Ports ports;
        ports.read_profile=[this]{++reads;return profile;};
        ports.start_transport=[this](auto identity,auto done){
            ++transports;observed_directory=identity.directory;
            if(!(identity==profile))return execution::SubmitResult{{},execution::Rejection::Invalid};
            const bool accepted = execution.core.post([this, done = std::move(done)] { pending.push_back(done); });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        ports.prepare_native=[this](auto identity){++preparations;return identity==profile
            ?startup::PreparationOutcome::Ready:startup::PreparationOutcome::ConfigurationFailed;};
        ports.retire_transport = [this](auto done) {
            const bool accepted = execution.core.post([this, done = std::move(done)] {
                ++retire_effects;
                retired = true;
                done();
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        ports.transport_retired=[this]{return retired;};
        owner=std::make_shared<startup::preparation::Coordinator>(execution.executor(), std::move(ports));
    }
    onboarding::AsyncPreparation port() {
        const auto original=owner;
        return {[this,original](startup::preparation::Context context,startup::preparation::CompleteResult done){
            ++starts;
            saved_complete=context.saved_profile&&context.saved_profile->completed;
            saved_written=context.saved_profile&&context.saved_profile->replacement_written;
            saved_directory=context.saved_profile&&context.saved_profile->directory_saved;
            saved_cleanup=context.saved_profile&&context.saved_profile->cleanup_completed;
            if(retain_completion)retained_completion=done;
            return original->start(std::move(context),[this,done=std::move(done)](auto actual){
                ++terminal_receipts;
                if(actual->transport&&!actual->transport->receipts.empty())delivered_inspection=actual->transport->receipts.front().inspection;
                done(actual);if(duplicate_completion)done(actual);
            });
        },[this,original]{++closes;original->close();}};
    }
    void deliver(desktopintent::transport::StartStatus status) {
        QVERIFY(!pending.empty());auto done=std::move(pending.front());pending.pop_front();
        desktopintent::transport::StartResult receipt{status,{},{{91,45,desktopintent::Disposition::Accepted}}};
        QVERIFY(execution.outcomes.post([done = std::move(done), receipt = std::move(receipt)]() mutable {
            done(std::move(receipt));
        }));
    }
    void release()
    {
        retained_completion = {};
        owner.reset();
        execution.core.close();
        execution.outcomes.close();
        execution.drain();
    }
};
struct AsyncConsumer {
    ManualStartupExecution execution;
    AsyncStartup startup;
    std::unique_ptr<onboarding::Coordinator> owner;
    QList<onboarding::Projection> frames,gui_frames;
    std::deque<std::function<void()>> gui;
    int writes{0},finished_frames{0},drained{0};
    bool original_write{false};
    AsyncConsumer() {
        owner=std::make_unique<onboarding::Coordinator>(execution.executor(),
            [this](auto frame){if(frame.preparation_finished)++finished_frames;frames.push_back(frame);gui.push_back([this,frame]{gui_frames.push_back(frame);});},
            [this]{++drained;});
    }
    onboarding::Ports savePort() {onboarding::Ports ports;ports.write=[this](const auto& request){++writes;original_write=request.data_dir.path==QStringLiteral("/original/profile")&&!request.values.listen;return Saved();};return ports;}
    void drain() { execution.drain(); }
    bool step(execution::Lane lane) { return execution.step(lane); }
    bool quiescent() const { return execution.quiescent(); }
    void ready() {QVERIFY(owner->check({1,true,true,true,{}},[]{return QString{};}));drain();QCOMPARE(frames.back().phase,onboarding::Phase::Ready);}
    void finish(bool separate_save=false) {
        if(separate_save){QVERIFY(owner->finish(1,Request(),savePort()));drain();QVERIFY(owner->prepareAsync(startup.port()));}
        else QVERIFY(owner->finishAndPrepareAsync(1,Request(),savePort(),startup.port()));
        drain();
    }
    void settle() {for(int turn=0;turn<32;++turn){startup.execution.drain();drain();}}
    void publish() {while(!gui.empty()){auto turn=std::move(gui.front());gui.pop_front();turn();}}
    void cleanup() {
        if(owner)owner->close();
        drain();
        if(startup.owner)startup.owner->close();
        startup.execution.drain();
        while(!startup.pending.empty()){startup.deliver(desktopintent::transport::StartStatus::Unknown);settle();}
        startup.retained_completion={};settle();owner.reset();drain();
        startup.release();settle();publish();
    }
    ~AsyncConsumer(){cleanup();}
};
struct ManualOnboarding {
    QTemporaryDir directory;
    ArgsManager args;
    ManualStartupExecution execution;
    std::deque<std::function<void()>> gui;
    std::unique_ptr<OnboardingOptionsModel> model;
    ManualOnboarding() {
        SetupServerArgs(args,false);SetupQmlGuiArgs(args);
        args.ForceSetArg("-regtest","1");args.ForceSetArg("-datadir",directory.path().toStdString());
        model=std::make_unique<OnboardingOptionsModel>(std::vector<std::string>{"bitcoin-qt","-regtest","-datadir="+directory.path().toStdString()},false,nullptr,
            [this](auto turn){gui.push_back(std::move(turn));}, &execution.executor());
    }
    bool publish() {if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;}
    bool step(execution::Lane lane) {
        return execution.step(lane);
    }
    bool quiescent() const { return execution.quiescent(); }
    bool settle() {
        for (int i = 0; i < 1000; ++i) {
            if (execution.step(execution::Lane::Core) || execution.step(execution::Lane::Utility) || publish()) continue;
            return true;
        }
        return false;
    }
    ~ManualOnboarding(){model.reset();execution.drain();settle();}
};

}
class OnboardingFlowTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void positiveExactChecksRequired_data() {
        QTest::addColumn<int>("missing");
        for(int missing=0;missing<4;++missing)QTest::newRow(qPrintable(QString::number(missing)))<<missing;
    }
    void positiveExactChecksRequired() {
        QFETCH(int,missing);onboarding::Flow flow;flow.edit(2);
        QVERIFY(!flow.checked({1,true,true,true,{}}));
        QVERIFY(!flow.canFinish());
        QVERIFY(flow.checked({2,missing!=0,missing!=1,missing!=2,missing==3?QStringLiteral("invalid effective policy"):QString{}}));
        QVERIFY(!flow.canFinish());QVERIFY(!flow.finish(2,Request()));
        Ready(flow,3);QVERIFY(flow.canFinish());
        QVERIFY(!flow.finish(2,Request()));QVERIFY(flow.finish(3,Request()));
    }
    void sameAnswersStillOwnNewRevision() {
        onboarding::Flow flow;Ready(flow,1);flow.edit(2);
        QVERIFY(!flow.canFinish());QVERIFY(!flow.checked({1,true,true,true,{}}));
        QVERIFY(flow.checked({2,true,true,true,{}}));
        const auto attempt=flow.finish(2,Request());QVERIFY(attempt);QCOMPARE(attempt->revision,uint64_t{2});
        QCOMPARE(attempt->request.data_dir.path,QStringLiteral("/original/profile"));
        flow.edit(3);QCOMPARE(flow.revision(),uint64_t{2});QVERIFY(!flow.finish(2,Request()));
    }
    void knownPartialCannotRepeatWholeWrite() {
        onboarding::Flow flow;Ready(flow);const auto attempt=flow.finish(1,Request());QVERIFY(attempt);
        auto partial=Saved();partial.completed=false;partial.cleanup_completed=false;partial.error=QStringLiteral("source conflict");
        QVERIFY(flow.persisted(attempt->id,partial));QCOMPARE(flow.phase(),onboarding::Phase::Failed);
        flow.edit(2);QCOMPARE(flow.revision(),uint64_t{1});QVERIFY(!flow.finish(1,Request()));
        QVERIFY(flow.beginCompletion());QVERIFY(!flow.beginCompletion());
        QVERIFY(!flow.persisted(attempt->id+1,Saved()));QVERIFY(flow.pending());
        QVERIFY(flow.persisted(attempt->id,Saved()));QCOMPARE(flow.phase(),onboarding::Phase::PreparingNode);
    }
    void unknownOutcomeOnlyQueriesOriginalAttempt() {
        onboarding::Flow flow;Ready(flow);const auto attempt=flow.finish(1,Request());QVERIFY(attempt);
        QmlOnboardingSettings::ProfileResult unknown;unknown.uncertain=true;unknown.error=QStringLiteral("lost outcome");
        QVERIFY(flow.persisted(attempt->id,unknown));QCOMPARE(flow.phase(),onboarding::Phase::Reconciling);
        flow.edit(2);QVERIFY(!flow.finish(1,Request()));QVERIFY(!flow.beginCompletion());
        QVERIFY(flow.beginQuery());QVERIFY(!flow.beginQuery());
        flow.queryUnavailable(QStringLiteral("query rejected"));QCOMPARE(flow.phase(),onboarding::Phase::Reconciling);
        QVERIFY(!flow.pending());QCOMPARE(flow.attempt()->id,attempt->id);
        QVERIFY(flow.beginQuery());QVERIFY(flow.reconciled(attempt->id,Saved()));
        QCOMPARE(flow.phase(),onboarding::Phase::PreparingNode);QVERIFY(flow.beginPreparation());
        QVERIFY(!flow.beginPreparation());flow.prepared(true,false,{});QCOMPARE(flow.phase(),onboarding::Phase::Completed);
        QVERIFY(!flow.beginPreparation());
    }
    void closeAccountsActualEffects_data() {
        QTest::addColumn<bool>("preparing");QTest::newRow("persisting")<<false;QTest::newRow("preparing")<<true;
    }
    void closeAccountsActualEffects() {
        QFETCH(bool,preparing);onboarding::Flow flow;Ready(flow);auto attempt=flow.finish(1,Request());QVERIFY(attempt);
        if(preparing){QVERIFY(flow.persisted(attempt->id,Saved()));QVERIFY(flow.beginPreparation());}
        flow.close();QCOMPARE(flow.phase(),onboarding::Phase::Closing);QVERIFY(!flow.finish(1,Request()));
        if(preparing)flow.prepared(true,false,{});else QVERIFY(flow.persisted(attempt->id,Saved()));
        QCOMPARE(flow.phase(),onboarding::Phase::Closed);QVERIFY(flow.result().replacement_written);
    }
    void rejectedBeforeWriteCanRecheckButPreparationCannotReplay() {
        onboarding::Flow flow;Ready(flow);auto attempt=flow.finish(1,Request());QVERIFY(attempt);
        QmlOnboardingSettings::ProfileResult rejected;rejected.error=QStringLiteral("not admitted");
        QVERIFY(flow.persisted(attempt->id,rejected));Ready(flow,2);QVERIFY(flow.canFinish());
        attempt=flow.finish(2,Request());QVERIFY(attempt);QVERIFY(flow.persisted(attempt->id,Saved()));
        QVERIFY(flow.beginPreparation());flow.prepared(false,false,QStringLiteral("base initialization rejected"));
        QVERIFY(!flow.beginPreparation());QVERIFY(!flow.beginCompletion());
        flow.edit(3);QCOMPARE(flow.revision(),uint64_t{2});
    }
    void boundedIntentRejectsBeforeOwnership() {
        onboarding::Flow flow;Ready(flow);auto request=Request();request.data_dir.path=QString(32769,QLatin1Char('x'));
        QVERIFY(!flow.finish(1,request));QVERIFY(!flow.attempt());QCOMPARE(flow.phase(),onboarding::Phase::Failed);
        QVERIFY(!flow.error().isEmpty());
    }
    void manualChecksCoalesceAndRejectStalePermission() {
        QList<onboarding::Projection> frames;int validations{0};ManualStartupExecution transport;
        onboarding::Coordinator owner{transport.executor(), [&](auto frame) { frames.push_back(std::move(frame)); }};
        owner.check({1,true,true,true,{}},[&]{++validations;return QString{};});
        QVERIFY(transport.step(execution::Lane::Utility));QCOMPARE(validations,0);
        owner.check({2,true,true,true,{}},[&]{++validations;return QString{};});
        owner.check({3,true,true,true,{}},[&]{++validations;return QString{};});
        transport.drain();QCOMPARE(validations,2);QVERIFY(!frames.empty());
        QCOMPARE(frames.back().revision,uint64_t{3});QCOMPARE(frames.back().phase,onboarding::Phase::Ready);
        int writes{0};onboarding::Ports ports;ports.write=[&](const auto&){++writes;return Saved();};
        QVERIFY(owner.finish(2,Request(),ports));transport.drain();QCOMPARE(writes,0);
        QVERIFY(owner.finish(3,Request(),ports));transport.drain();QCOMPARE(writes,1);
        owner.close();transport.drain();QVERIFY(transport.quiescent());
    }
    void manualPartialCompletionNeverReplaysMutation() {
        QList<onboarding::Projection> frames;int writes{0},queries{0},completions{0};bool original_path=false,known_saved=false;ManualStartupExecution transport;
        onboarding::Coordinator owner{transport.executor(), [&](auto frame) { frames.push_back(std::move(frame)); }};
        owner.check({1,true,true,true,{}},[]{return QString{};});transport.drain();
        onboarding::Ports ports;
        ports.write=[&](const auto& request){++writes;original_path=request.data_dir.path==QStringLiteral("/original/profile");auto result=Saved();result.completed=false;result.cleanup_completed=false;return result;};
        ports.query=[&](const auto& original){++queries;return original;};
        ports.complete=[&](const auto& original){++completions;known_saved=original.replacement_written;return Saved();};
        QVERIFY(owner.finish(1,Request(),ports));transport.drain();QCOMPARE(writes,1);QCOMPARE(frames.back().phase,onboarding::Phase::Failed);
        QVERIFY(owner.recover(true));transport.drain();QCOMPARE(completions,1);QCOMPARE(writes,1);QCOMPARE(queries,0);QVERIFY(original_path);QVERIFY(known_saved);
        QCOMPARE(frames.back().phase,onboarding::Phase::PreparingNode);
        owner.close();transport.drain();QVERIFY(transport.quiescent());
    }
    void manualUnknownQueryFailureRetainsOwnedOutcome() {
        QList<onboarding::Projection> frames;int writes{0},queries{0};bool fail=true,original_unknown=false;ManualStartupExecution transport;
        onboarding::Coordinator owner{transport.executor(), [&](auto frame) { frames.push_back(std::move(frame)); }};
        owner.check({1,true,true,true,{}},[]{return QString{};});transport.drain();
        onboarding::Ports ports;
        ports.write=[&](const auto&)->QmlOnboardingSettings::ProfileResult{++writes;throw std::runtime_error("unknown original write");};
        ports.query=[&](const auto& original){++queries;if(fail)throw std::runtime_error("query unavailable");original_unknown=original.uncertain;return Saved();};
        QVERIFY(owner.finish(1,Request(),ports));transport.drain();QCOMPARE(writes,1);QCOMPARE(frames.back().phase,onboarding::Phase::Reconciling);
        const auto attempt=frames.back().attempt;QVERIFY(owner.recover(false));transport.drain();QCOMPARE(queries,1);QCOMPARE(frames.back().phase,onboarding::Phase::Reconciling);
        fail=false;QVERIFY(owner.recover(false));transport.drain();QCOMPARE(queries,2);QCOMPARE(writes,1);QVERIFY(original_unknown);QCOMPARE(frames.back().attempt,attempt);
        QCOMPARE(frames.back().phase,onboarding::Phase::PreparingNode);owner.close();transport.drain();QVERIFY(transport.quiescent());
    }
    void realWorkersKeepChecksWritesAndDecisionsOffGui() {
        std::atomic<int> phase{static_cast<int>(onboarding::Phase::Drafting)};
        std::atomic_bool worker_only{true},drained{false};std::atomic<int> writes{0};
        const auto* gui=QThread::currentThread();
        execution::Executor executor{execution::Executor::Config{}};
        onboarding::Coordinator owner{executor,
            [&](const auto& frame){if(QThread::currentThread()==gui)worker_only=false;phase=static_cast<int>(frame.phase);},[&]{drained=true;}};
        owner.check({1,true,true,true,{}},[&]{if(QThread::currentThread()==gui)worker_only=false;return QString{};});
        QTRY_COMPARE(phase.load(),static_cast<int>(onboarding::Phase::Ready));
        onboarding::Ports ports;ports.write=[&](const auto&){if(QThread::currentThread()==gui)worker_only=false;++writes;return Saved();};
        QVERIFY(owner.finish(1,Request(),ports));QTRY_COMPARE(phase.load(),static_cast<int>(onboarding::Phase::PreparingNode));
        QCOMPARE(writes.load(),1);QVERIFY(worker_only.load());owner.close();QTRY_VERIFY(drained.load());
    }

    void facadeCapturesOriginalWriteAndRejectsSuccessors() {
        ManualOnboarding f;QVERIFY(f.directory.isValid());QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.model->setListen(false);QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        int writes{0};QString written_path;bool written_listen=true;
        onboarding::Ports ports;ports.write=[&](const auto& request){++writes;written_path=request.data_dir.path;written_listen=request.values.listen;return Saved();};
        f.model->setProfilePorts(ports);QSignalSpy committed{f.model.get(),&OnboardingOptionsModel::settingsCommitted};
        f.model->commitSettings();QVERIFY(f.model->setupCommitPending());QVERIFY(!f.model->canFinish());
        f.model->commitSettings();QVERIFY(!f.model->selectCustomDataDir(QStringLiteral("/another-profile")));
        f.model->setListen(true);QCOMPARE(writes,0);
        QVERIFY(f.step(execution::Lane::Utility));QVERIFY(f.step(execution::Lane::Core));
        QCOMPARE(writes,1);QCOMPARE(written_path,f.directory.path());QVERIFY(!written_listen);QVERIFY(committed.empty());
        QVERIFY(!f.model->setupCommitted());QVERIFY(f.model->setupCommitPending());
        QVERIFY(f.settle());QCOMPARE(committed.size(),1);QVERIFY(f.model->setupCommitted());
        f.model->commitSettings();QVERIFY(f.settle());QCOMPARE(writes,1);
        f.model->beginShutdown();QVERIFY(f.settle());QVERIFY(f.quiescent());
    }
    void facadePartialAndUnknownKeepOriginalOwnership_data() {
        QTest::addColumn<bool>("unknown");QTest::newRow("known-partial")<<false;QTest::newRow("unknown-effect")<<true;
    }
    void facadePartialAndUnknownKeepOriginalOwnership() {
        QFETCH(bool,unknown);ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        int writes{0},queries{0},completions{0},preparations{0};bool query_fails=true,original_fact=false;
        onboarding::Ports ports;
        ports.write=[&](const auto&)->QmlOnboardingSettings::ProfileResult{++writes;if(unknown)throw std::runtime_error("lost completion");auto result=Saved();result.completed=false;result.cleanup_completed=false;result.error=QStringLiteral("legacy source changed");return result;};
        ports.complete=[&](const auto& original){++completions;original_fact=original.replacement_written;return Saved();};
        ports.query=[&](const auto& original){++queries;original_fact=original.uncertain;if(query_fails)throw std::runtime_error("read failed");return Saved();};
        f.model->setProfilePorts(ports);QSignalSpy committed{f.model.get(),&OnboardingOptionsModel::settingsCommitted};
        f.model->commitSettings();QVERIFY(f.settle());QCOMPARE(f.model->setupUncertain(),unknown);QVERIFY(!f.model->canFinish());
        const auto attempt=f.model->setupAttemptId();
        const auto original_path=f.model->dataDir();QVERIFY(!f.model->selectCustomDataDir(QStringLiteral("/another-profile")));
        f.model->useDefaultDataDir();QVERIFY(f.settle());QCOMPARE(f.model->dataDir(),original_path);
        f.model->commitSettings();f.model->prepareNode(f.args,[&]{++preparations;return true;});QVERIFY(f.settle());
        QCOMPARE(writes,1);QCOMPARE(preparations,0);
        if(unknown){
            f.model->querySetupOutcome();QVERIFY(f.settle());QCOMPARE(queries,1);QVERIFY(f.model->setupUncertain());
            query_fails=false;f.model->querySetupOutcome();
        }else{QVERIFY(f.model->setupPartial());f.model->completeSavedSetup();}
        QVERIFY(f.settle());QCOMPARE(writes,1);QVERIFY(original_fact);QCOMPARE(queries,unknown?2:0);QCOMPARE(completions,unknown?0:1);
        QVERIFY(f.model->setupCommitted());QCOMPARE(committed.size(),1);QCOMPARE(f.model->setupAttemptId(),attempt);
        f.model->beginShutdown();QVERIFY(f.settle());QVERIFY(f.quiescent());
    }
    void facadeDeletionRetainsNativeWriteReceipt() {
        ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());int writes{0};
        onboarding::Ports ports;ports.write=[&](const auto&){++writes;return Saved();};f.model->setProfilePorts(ports);
        const auto receipt=f.model->setupCommitReceipt();f.model->commitSettings();
        QVERIFY(f.step(execution::Lane::Utility));
        QCOMPARE(writes, 0);
        f.model.reset();
        QVERIFY(f.step(execution::Lane::Core));
        QCOMPARE(writes, 1);
        QCOMPARE(receipt->load(), int(OnboardingOptionsModel::SetupCommitted));
        QVERIFY(f.settle());
        QCOMPARE(writes, 1);
        QCOMPARE(receipt->load(), int(OnboardingOptionsModel::SetupCommitted));
        QVERIFY(f.quiescent());
    }
    void sharedProfileCloseWaitsForCoreAndCleanup()
    {
        QList<onboarding::Projection> frames;
        int writes{0}, drained{0};
        ManualStartupExecution shared;
        onboarding::Coordinator owner{shared.executor(), [&](auto frame) { frames.push_back(std::move(frame)); },
            [&] { ++drained; }};
        const auto cleanup = qScopeGuard([&] { owner.close(); shared.drain(); });
        QVERIFY(owner.check({1, true, true, true, {}}, [] { return QString{}; }));
        shared.drain();
        QCOMPARE(frames.back().phase, onboarding::Phase::Ready);
        onboarding::Ports save;
        save.write = [&](const auto& request) {
            ++writes;
            return request.data_dir.path == QStringLiteral("/original/profile") ? Saved() : QmlOnboardingSettings::ProfileResult{};
        };
        QVERIFY(owner.finish(1, Request(), save));
        owner.close();
        QVERIFY(!owner.finish(1, Request(), save));
        while (frames.back().phase != onboarding::Phase::Closing) QVERIFY(shared.step(execution::Lane::Utility));
        QCOMPARE(writes, 0);
        QCOMPARE(drained, 0);
        QVERIFY(shared.step(execution::Lane::Core));
        QCOMPARE(writes, 1);
        QCOMPARE(drained, 0);
        while (frames.back().phase != onboarding::Phase::Closed) QVERIFY(shared.step(execution::Lane::Utility));
        QVERIFY(frames.back().profile_complete);
        QCOMPARE(drained, 0);
        shared.drain();
        QCOMPARE(drained, 1);
        QCOMPARE(writes, 1);
        QVERIFY(shared.quiescent());
    }

    void facadePreparationCloseAccountsOutcome_data() {
        QTest::addColumn<QString>("outcome");QTest::addColumn<bool>("close_before_core");
        for(const auto* outcome:{"success","failure","throw"})for(bool before:{false,true})
            QTest::newRow(qPrintable(QString::fromLatin1(outcome)+(before?QStringLiteral("-close-before-core"):QStringLiteral("-close-after-core"))))<<QString::fromLatin1(outcome)<<before;
    }
    void facadePreparationCloseAccountsOutcome() {
        QFETCH(QString,outcome);QFETCH(bool,close_before_core);ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.model->configureSettingsCommit(f.args);f.model->commitSettings();QVERIFY(f.settle());QVERIFY(f.model->setupCommitted());
        QSignalSpy prepared{f.model.get(),&OnboardingOptionsModel::nodePrepared},shutdown{f.model.get(),&OnboardingOptionsModel::shutdownFinished};QStringList order;int calls{0};
        connect(f.model.get(),&OnboardingOptionsModel::nodePrepared,this,[&]{order<<QStringLiteral("prepared");});
        connect(f.model.get(),&OnboardingOptionsModel::shutdownFinished,this,[&]{order<<QStringLiteral("shutdown");});
        const auto disconnect_before_captures_die=qScopeGuard([&]{if(f.model)disconnect(f.model.get(),nullptr,this,nullptr);});
        f.model->prepareNode(f.args,[&]{++calls;if(outcome=="throw")throw std::runtime_error("original preparation failed");return outcome=="success";});
        QVERIFY(f.execution.step(execution::Lane::Utility));
        if(close_before_core){f.model->beginShutdown();while(f.execution.step(execution::Lane::Utility)){};}
        QVERIFY(f.execution.step(execution::Lane::Core));QCOMPARE(calls,1);
        if(!close_before_core)f.model->beginShutdown();
        QVERIFY(prepared.empty());QVERIFY(shutdown.empty());QVERIFY(f.settle());
        QCOMPARE(prepared.size(),1);QCOMPARE(shutdown.size(),1);QCOMPARE(order,QStringList({QStringLiteral("prepared"),QStringLiteral("shutdown")}));
        QCOMPARE(prepared.front().at(0).toBool(),outcome=="success");
        if(outcome=="throw"){QCOMPARE(f.model->preparationState(),OnboardingOptionsModel::PreparationUncertain);QCOMPARE(prepared.front().at(1).toString(),QStringLiteral("original preparation failed"));}
        QVERIFY(f.execution.quiescent());
    }
    void facadePreparationSignalWaitsForFlowCleanup() {
        ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());f.model->configureSettingsCommit(f.args);f.model->commitSettings();QVERIFY(f.settle());
        QSignalSpy prepared{f.model.get(),&OnboardingOptionsModel::nodePrepared},shutdown{f.model.get(),&OnboardingOptionsModel::shutdownFinished};
        f.model->prepareNode(f.args,[]{return true;});QVERIFY(f.execution.step(execution::Lane::Utility));QVERIFY(f.execution.step(execution::Lane::Core));
        while(f.execution.step(execution::Lane::Utility)){};
        while(f.publish()){};
        QVERIFY(prepared.empty());QVERIFY(shutdown.empty());f.model->beginShutdown();
        QVERIFY(f.settle());QCOMPARE(prepared.size(),1);QCOMPARE(shutdown.size(),1);QVERIFY(f.execution.quiescent());
    }
    void facadeRejectedChecksPermitExplicitRetry() {
        ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.execution.reject_utility = true;f.model->retrySetupChecks();QVERIFY(!f.model->canFinish());
        QVERIFY(!f.model->setupCommitError().isEmpty());QVERIFY(f.settle());QVERIFY(!f.model->canFinish());
        f.model->retrySetupChecks();QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.model->beginShutdown();QVERIFY(f.settle());QVERIFY(f.execution.quiescent());
    }
    void facadeRejectedPreparationIsKnownNotStarted_data() {
        QTest::addColumn<bool>("ordinary_closed");QTest::newRow("native-transport-rejected")<<false;QTest::newRow("root-ordinary-closed")<<true;
    }
    void facadeRejectedPreparationIsKnownNotStarted() {
        QFETCH(bool,ordinary_closed);ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        f.model->configureSettingsCommit(f.args);f.model->commitSettings();QVERIFY(f.settle());int calls{0};
        QSignalSpy prepared{f.model.get(),&OnboardingOptionsModel::nodePrepared};
        if (ordinary_closed) f.execution.reject_utility = true; else f.execution.reject_core = true;
        f.model->prepareNode(f.args,[&]{++calls;return true;});QVERIFY(f.settle());
        QCOMPARE(calls,0);QCOMPARE(prepared.size(),1);QVERIFY(!prepared.front().at(0).toBool());
        QCOMPARE(f.model->preparationState(),OnboardingOptionsModel::PreparationFailed);QVERIFY(f.model->setupCommitted());
        f.model->beginShutdown();QVERIFY(f.settle());QVERIFY(f.execution.quiescent());
    }

    void closedRecoveryOwnerHasNoOutstandingDrain() {
        ManualStartupExecution transport;
        transport.executor().stopAndJoin();
        int drained{0};
        QList<onboarding::Projection> frames;
        onboarding::Coordinator owner{transport.executor(), [&](auto frame) { frames.push_back(std::move(frame)); }, [&] { ++drained; }};
        QVERIFY(!owner.finish(1, Request(), {}));
        QVERIFY(frames.empty());
        owner.close();
        transport.drain();
        QCOMPARE(drained, 1);
        QCOMPARE(frames.size(), 1);
        QCOMPARE(frames.front().phase, onboarding::Phase::Closed);
        QVERIFY(transport.quiescent());
    }
    void facadeClosedRecoveryOwnerStillFinishesShutdown() {
        ManualStartupExecution transport;
        transport.executor().stopAndJoin();
        std::deque<std::function<void()>> gui;
        QTemporaryDir directory;
        OnboardingOptionsModel model{{"bitcoin-qt", "-regtest", "-datadir=" + directory.path().toStdString()}, false, nullptr,
            [&](auto turn) { gui.push_back(std::move(turn)); }, &transport.executor()};
        QSignalSpy shutdown{&model, &OnboardingOptionsModel::shutdownFinished};
        model.beginShutdown();
        for (int i = 0; i < 100; ++i) {
            if (transport.step(execution::Lane::Core) || transport.step(execution::Lane::Utility)) continue;
            if (gui.empty()) break;
            auto turn = std::move(gui.front());
            gui.pop_front();
            turn();
        }
        QCOMPARE(shutdown.size(),1);QVERIFY(transport.quiescent());
    }

    void asyncActualCoordinatorPreservesOneSaveAndTypedDisposition_data() {
        QTest::addColumn<int>("status");QTest::addColumn<bool>("separate_save");
        using S=desktopintent::transport::StartStatus;
        QTest::newRow("primary-combined")<<int(S::Primary)<<false;
        QTest::newRow("primary-already-saved")<<int(S::Primary)<<true;
        QTest::newRow("forwarded-combined")<<int(S::Forwarded)<<false;
        QTest::newRow("forwarded-already-saved")<<int(S::Forwarded)<<true;
        QTest::newRow("unknown-combined")<<int(S::Unknown)<<false;
        QTest::newRow("unknown-already-saved")<<int(S::Unknown)<<true;
    }
    void asyncActualCoordinatorPreservesOneSaveAndTypedDisposition() {
        QFETCH(int,status);QFETCH(bool,separate_save);using S=desktopintent::transport::StartStatus;using D=startup::preparation::Disposition;
        AsyncConsumer f;f.ready();f.startup.duplicate_completion=true;f.finish(separate_save);
        QCOMPARE(f.writes,1);QVERIFY(f.original_write);QCOMPARE(f.startup.starts,1);
        QVERIFY(f.startup.saved_complete&&f.startup.saved_written&&f.startup.saved_directory&&f.startup.saved_cleanup);
        QCOMPARE(f.frames.back().phase,onboarding::Phase::PreparingNode);QVERIFY(f.frames.back().pending);
        f.startup.execution.drain();QCOMPARE(f.startup.transports,1);QCOMPARE(f.startup.preparations,0);
        f.startup.deliver(static_cast<S>(status));
        while(f.startup.execution.step(execution::Lane::Utility)){}
        QCOMPARE(f.finished_frames,0);QCOMPARE(f.startup.retire_effects,0);
        f.startup.execution.drain();
        QCOMPARE(f.frames.back().phase,onboarding::Phase::PreparingNode);QCOMPARE(f.finished_frames,0);
        QVERIFY(f.step(execution::Lane::Utility));
        const auto expected=status==int(S::Primary)?D::Ready:status==int(S::Forwarded)?D::Forwarded:D::Uncertain;
        QVERIFY(f.frames.back().preparation_disposition);QCOMPARE(*f.frames.back().preparation_disposition,expected);
        QCOMPARE(f.frames.back().phase,status==int(S::Primary)?onboarding::Phase::Completed:status==int(S::Forwarded)?onboarding::Phase::Forwarded:onboarding::Phase::Reconciling);
        QCOMPARE(f.frames.back().preparation_complete,status==int(S::Primary));
        QVERIFY(f.frames.back().profile_complete&&f.frames.back().profile_written);
        QCOMPARE(f.startup.preparations,status==int(S::Primary)?1:0);QCOMPARE(f.startup.retire_effects,status==int(S::Primary)?0:1);
        QCOMPARE(f.startup.delivered_inspection,uint64_t{45});QCOMPARE(f.startup.terminal_receipts,1);QCOMPARE(f.finished_frames,1);QCOMPARE(f.writes,1);
        QVERIFY(f.gui_frames.empty());f.publish();QCOMPARE(f.gui_frames.back().phase,f.frames.back().phase);
        f.cleanup();QVERIFY(f.quiescent());QVERIFY(f.startup.execution.quiescent());
    }
    void asyncReceiverSurvivesOriginalOrdinaryCloseAndHeldGui() {
        AsyncConsumer f;f.ready();f.finish();f.startup.execution.drain();
        f.execution.reject_utility = true;
        QVERIFY(!f.owner->prepareAsync(f.startup.port()));
        f.startup.deliver(desktopintent::transport::StartStatus::Forwarded);
        f.startup.execution.drain();
        QCOMPARE(f.startup.retire_effects,1);QCOMPARE(f.finished_frames,0);QVERIFY(f.gui_frames.empty());
        QVERIFY(f.execution.step(execution::Lane::Utility));
        QCOMPARE(f.finished_frames,1);QCOMPARE(f.frames.back().phase,onboarding::Phase::Forwarded);
        QVERIFY(f.frames.back().profile_complete);QVERIFY(!f.frames.back().preparation_complete);QCOMPARE(f.writes,1);
        f.publish();QCOMPARE(f.gui_frames.back().phase,onboarding::Phase::Forwarded);
        f.cleanup();QVERIFY(f.execution.quiescent());QVERIFY(f.startup.execution.quiescent());
    }
    void asyncDuplicatePreparationPreservesTheOriginalPendingAndTerminalOutcome() {
        AsyncConsumer f;f.ready();f.finish();f.startup.execution.drain();
        QCOMPARE(f.startup.starts,1);QCOMPARE(f.startup.closes,0);QCOMPARE(f.writes,1);
        QVERIFY(f.owner->prepareAsync(f.startup.port()));f.drain();
        QCOMPARE(f.startup.closes,0);QCOMPARE(f.startup.starts,1);QVERIFY(f.frames.back().pending);
        f.startup.deliver(desktopintent::transport::StartStatus::Forwarded);f.settle();
        QCOMPARE(f.frames.back().phase,onboarding::Phase::Forwarded);QCOMPARE(f.finished_frames,1);
        QVERIFY(f.frames.back().preparation_disposition);
        QCOMPARE(*f.frames.back().preparation_disposition,startup::preparation::Disposition::Forwarded);
        const auto error=f.frames.back().error;
        QVERIFY(f.owner->prepareAsync(f.startup.port()));f.drain();
        QCOMPARE(f.startup.closes,0);QCOMPARE(f.startup.starts,1);QCOMPARE(f.writes,1);
        QCOMPARE(f.frames.back().phase,onboarding::Phase::Forwarded);
        QCOMPARE(*f.frames.back().preparation_disposition,startup::preparation::Disposition::Forwarded);
        QCOMPARE(f.frames.back().error,error);QVERIFY(f.frames.back().profile_complete);
        f.cleanup();QVERIFY(f.quiescent());QVERIFY(f.startup.execution.quiescent());
    }
    void asyncDuplicateFinishCannotReplaceTheOriginalPreparationPort() {
        AsyncConsumer f;f.ready();
        QVERIFY(f.owner->finishAndPrepareAsync(1,Request(),f.savePort(),f.startup.port()));
        while(f.step(execution::Lane::Utility)){}
        QCOMPARE(f.writes,0);QVERIFY(f.frames.back().pending);
        int replacement_starts{0},replacement_closes{0},replacement_writes{0};
        onboarding::AsyncPreparation replacement{
            [&](auto,auto){++replacement_starts;return execution::SubmitResult{{},execution::Rejection::Closed};},
            [&]{++replacement_closes;}};
        onboarding::Ports wrong_ports;wrong_ports.write=[&](const auto&){++replacement_writes;return Saved();};
        auto wrong_request=Request();wrong_request.data_dir.path=QStringLiteral("/replacement/profile");
        QVERIFY(f.owner->finishAndPrepareAsync(1,wrong_request,wrong_ports,replacement));
        f.drain();f.startup.execution.drain();
        QCOMPARE(f.writes,1);QVERIFY(f.original_write);QCOMPARE(f.startup.starts,1);
        QCOMPARE(replacement_starts,0);QCOMPARE(replacement_closes,0);QCOMPARE(replacement_writes,0);
        f.startup.deliver(desktopintent::transport::StartStatus::Forwarded);f.settle();
        QCOMPARE(f.frames.back().phase,onboarding::Phase::Forwarded);QCOMPARE(f.finished_frames,1);
        const auto error=f.frames.back().error;
        QVERIFY(f.owner->finishAndPrepareAsync(1,wrong_request,wrong_ports,replacement));f.drain();
        QCOMPARE(f.writes,1);QCOMPARE(f.startup.starts,1);QCOMPARE(replacement_starts,0);QCOMPARE(replacement_closes,0);QCOMPARE(replacement_writes,0);
        QCOMPARE(f.frames.back().phase,onboarding::Phase::Forwarded);
        QCOMPARE(*f.frames.back().preparation_disposition,startup::preparation::Disposition::Forwarded);
        QCOMPARE(f.frames.back().error,error);
        f.cleanup();QVERIFY(f.quiescent());QVERIFY(f.startup.execution.quiescent());
    }
    void asyncFacadeLossStillConsumesOriginalProducerReceipt() {
        AsyncConsumer f;f.ready();f.finish();f.startup.execution.drain();
        f.owner.reset();f.drain();f.startup.execution.drain();
        f.startup.deliver(desktopintent::transport::StartStatus::Forwarded);f.settle();
        QCOMPARE(f.writes,1);QCOMPARE(f.startup.starts,1);QCOMPARE(f.startup.preparations,0);
        QVERIFY(f.frames.back().preparation_finished);QVERIFY(f.frames.back().profile_complete);
        QVERIFY(f.frames.back().preparation_disposition);QCOMPARE(*f.frames.back().preparation_disposition,startup::preparation::Disposition::Forwarded);
        QCOMPARE(f.frames.back().phase,onboarding::Phase::Closed);QCOMPARE(f.drained,1);
        f.cleanup();QVERIFY(f.quiescent());QVERIFY(f.startup.execution.quiescent());
    }
    void asyncRetainedDuplicateCompletionMustReleaseConsumedCapabilities() {
        AsyncConsumer f;f.ready();f.startup.retain_completion=true;f.finish();f.startup.execution.drain();
        f.startup.deliver(desktopintent::transport::StartStatus::Forwarded);f.settle();
        QCOMPARE(f.frames.back().phase,onboarding::Phase::Forwarded);QCOMPARE(f.writes,1);QCOMPARE(f.finished_frames,1);
        // A foreign Domain producer may retain a duplicate of its completed
        // callback. It must not retain the original receiver/action forever.
        QVERIFY(f.startup.retained_completion);
        f.owner->close();f.owner.reset();f.drain();
        f.startup.owner.reset();f.startup.execution.outcomes.close();f.settle();
        const auto release=qScopeGuard([&]{f.startup.retained_completion={};f.settle();});
        QVERIFY(f.quiescent());QVERIFY(f.startup.execution.quiescent());
        QCOMPARE(f.drained, 1);
        const auto frames_after_close = f.frames.size();
        f.startup.retained_completion(std::make_shared<const startup::preparation::Result>());
        f.settle();
        QCOMPARE(f.frames.size(), frames_after_close);
        QCOMPARE(f.drained, 1);
    }
    void realAsyncConsumerUsesIndependentWorkersWithoutNestedNativeWaits() {
        struct Trace {
            std::atomic<int> phase{int(onboarding::Phase::Drafting)},outcome{-1},writes{0},reads{0},preparations{0},transport_callbacks{0};
            std::atomic_bool off_gui{true},saved_seen{false},profile_complete{false},flow_drained{false},endpoint_retired{false};
            std::atomic<const QThread*> write_thread{nullptr},read_thread{nullptr},prepare_thread{nullptr},flow_domain{nullptr},startup_domain{nullptr};
        };
        const auto trace=std::make_shared<Trace>();const auto* gui=QThread::currentThread();
        execution::Executor stable{execution::Executor::Config{}}, transient{execution::Executor::Config{}};
        const auto endpoint = std::make_shared<execution::SerialQueue>(stable.makeQueue(execution::Lane::Core));
        startup::preparation::Ports ports;
        ports.read_profile=[trace,gui]{if(QThread::currentThread()==gui)trace->off_gui=false;trace->read_thread=QThread::currentThread();++trace->reads;return startup::preparation::Profile{QStringLiteral("/original/profile"),QStringLiteral("regtest")};};
        ports.start_transport=[trace,gui,endpoint](auto profile,auto done){
            if(QThread::currentThread()==gui)trace->off_gui=false;
            trace->startup_domain=QThread::currentThread();
            if(profile.directory!=QStringLiteral("/original/profile"))return execution::SubmitResult{{},execution::Rejection::Invalid};
            const bool accepted = endpoint->post([trace, gui, done = std::move(done)] {
                if (QThread::currentThread() == gui) trace->off_gui = false;
                ++trace->transport_callbacks;
                done({desktopintent::transport::StartStatus::Primary, {}, {{91, 45, desktopintent::Disposition::Accepted}}});
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        ports.prepare_native=[trace,gui](auto){if(QThread::currentThread()==gui)trace->off_gui=false;trace->prepare_thread=QThread::currentThread();++trace->preparations;return startup::PreparationOutcome::Ready;};
        ports.retire_transport = [trace, endpoint](auto done) {
            const bool accepted = endpoint->post([trace, done = std::move(done)] {
                trace->endpoint_retired = true;
                done();
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        ports.transport_retired=[trace]{return trace->endpoint_retired.load();};
        auto preparation = std::make_shared<startup::preparation::Coordinator>(stable, std::move(ports));
        ports = {}; // Drop caller-held moved-from port aliases.
        auto owner=std::make_unique<onboarding::Coordinator>(transient,[trace,gui](const auto& projection){
            if(QThread::currentThread()==gui)trace->off_gui=false;
            trace->flow_domain=QThread::currentThread();trace->phase=int(projection.phase);
            trace->profile_complete=projection.profile_complete;if(projection.preparation_disposition)trace->outcome=int(*projection.preparation_disposition);
        },[trace]{trace->flow_drained=true;});
        const auto cleanup = qScopeGuard([&] {
            if (owner) owner->close();
            if (preparation) preparation->close();
            owner.reset();
            preparation.reset();
            transient.stopAndJoin();
            endpoint->close();
            stable.stopAndJoin();
        });
        QVERIFY(owner->check({1,true,true,true,{}},[]{return QString{};}));
        QTRY_COMPARE(trace->phase.load(),int(onboarding::Phase::Ready));
        onboarding::Ports save;save.write=[trace,gui](const auto& request){if(QThread::currentThread()==gui)trace->off_gui=false;trace->write_thread=QThread::currentThread();++trace->writes;return request.data_dir.path==QStringLiteral("/original/profile")?Saved():QmlOnboardingSettings::ProfileResult{};};
        onboarding::AsyncPreparation async{
            [preparation,trace](auto context,auto done){trace->saved_seen=context.saved_profile&&context.saved_profile->completed;return preparation->start(std::move(context),std::move(done));},
            [preparation]{preparation->close();}};
        QVERIFY(owner->finishAndPrepareAsync(1,Request(),std::move(save),std::move(async)));
        save = {}; async = {}; // Native drain must not depend on caller aliases.
        QTRY_COMPARE(trace->outcome.load(),int(startup::preparation::Disposition::Ready));
        QCOMPARE(trace->phase.load(),int(onboarding::Phase::Completed));QVERIFY(trace->profile_complete.load());QVERIFY(trace->saved_seen.load());
        QCOMPARE(trace->writes.load(),1);QCOMPARE(trace->reads.load(),1);QCOMPARE(trace->preparations.load(),1);QCOMPARE(trace->transport_callbacks.load(),1);QVERIFY(trace->off_gui.load());
        QVERIFY(trace->write_thread.load()!=trace->read_thread.load());QVERIFY(trace->write_thread.load()!=trace->prepare_thread.load());
        QVERIFY(trace->flow_domain.load()!=trace->startup_domain.load());
        owner->close();
        owner.reset();
        QTRY_VERIFY(trace->flow_drained.load());
        transient.stopAndJoin();
        QVERIFY(!trace->endpoint_retired.load());
        const auto preparation_drained = std::make_shared<std::atomic_bool>(false);
        preparation->whenDrained([preparation_drained] { *preparation_drained = true; });
        preparation->close();
        QTRY_VERIFY(trace->endpoint_retired.load());
        preparation.reset();
        QTRY_VERIFY(preparation_drained->load());
        endpoint->close();
        stable.stopAndJoin();
    }
    void modelAsyncForwardedIsTypedAndNeverLegacyReady() {
        ManualOnboarding f;QVERIFY(f.settle());QVERIFY(f.model->canFinish());
        int writes{0};onboarding::Ports ports;ports.write=[&](const auto&){++writes;return Saved();};f.model->setProfilePorts(ports);
        f.model->commitSettings();QVERIFY(f.settle());QVERIFY(f.model->setupCommitted());QCOMPARE(writes,1);
        AsyncStartup startup;startup.profile.directory=f.directory.path();qRegisterMetaType<OnboardingOptionsModel::PreparationState>();QSignalSpy typed{f.model.get(),&OnboardingOptionsModel::nodePreparationFinished};QSignalSpy legacy{f.model.get(),&OnboardingOptionsModel::nodePrepared};
        const auto cleanup=qScopeGuard([&]{f.model.reset();f.execution.drain();if(startup.owner)startup.owner->close();startup.execution.drain();while(!startup.pending.empty()){startup.deliver(desktopintent::transport::StartStatus::Unknown);startup.execution.drain();f.execution.drain();}startup.retained_completion={};startup.release();QVERIFY(f.settle());startup.execution.drain();});
        f.model->prepareNodeAsync(f.args,startup.port());f.execution.drain();startup.execution.drain();
        QCOMPARE(startup.starts,1);QVERIFY(startup.saved_complete);QCOMPARE(writes,1);QCOMPARE(startup.observed_directory,f.directory.path());
        startup.deliver(desktopintent::transport::StartStatus::Forwarded);
        while(startup.execution.step(execution::Lane::Utility)){}
        QCOMPARE(startup.retire_effects,0);QCOMPARE(typed.size(),0);QCOMPARE(legacy.size(),0);
        startup.execution.drain();f.execution.drain();
        QCOMPARE(f.model->preparationState(),OnboardingOptionsModel::PreparationForwarded);
        QVERIFY(f.model->setupCommitted());QCOMPARE(typed.size(),0);QCOMPARE(legacy.size(),0);
        for(int turn=0;turn<32;++turn){QVERIFY(f.settle());startup.execution.drain();}
        QCOMPARE(typed.size(),1);QCOMPARE(legacy.size(),1);
        QCOMPARE(typed.front().at(0).value<OnboardingOptionsModel::PreparationState>(),OnboardingOptionsModel::PreparationForwarded);
        QVERIFY(!legacy.front().at(0).toBool());QVERIFY(f.model->setupCommitted());
        QCOMPARE(writes,1);QCOMPARE(startup.preparations,0);QCOMPARE(startup.retire_effects,1);
    }

};
#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OnboardingFlowTests)
#else
QTEST_MAIN(OnboardingFlowTests)
#endif
#include <test_onboardingflow.moc>
