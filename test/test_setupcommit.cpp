// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/onboarding_settings.h>
#include <qml/guiargs.h>
#include <common/args.h>
#include <init.h>
#include <univalue.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QScopeGuard>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <deque>
#include <optional>
#include <stdexcept>
namespace {
struct Fixture {
    QTemporaryDir directory;
    ManualRoutedExecution transport;
    std::deque<std::function<void()>> gui;
    std::unique_ptr<OnboardingOptionsModel> model;
    Fixture(){model=std::make_unique<OnboardingOptionsModel>(std::vector<std::string>{"bitcoin-qt","-regtest","-datadir="+directory.path().toStdString()},false,nullptr,execution::SnapshotServices{transport.scheduler,[this](auto fn){gui.push_back(std::move(fn));}});}
    ~Fixture(){model.reset();transport.drain();while(publish()){}transport.drain();if(!transport.quiescent())QTest::qFail("Setup owner did not fully drain",__FILE__,__LINE__);}
    bool publish(){if(gui.empty())return false;auto fn=std::move(gui.front());gui.pop_front();fn();return true;}
    bool settle(){for(int i=0;i<4096;++i){if(transport.stepNext()||publish())continue;return transport.transportIdle();}return false;}
    bool ready(){if(!settle())return false;model->setPrune(true);model->setPruneSizeGB(1);return settle()&&model->canFinish();}
};
}
class SetupCommitTests:public QObject {
    Q_OBJECT
private Q_SLOTS:
    void directoryWriteFailureCannotPublishTheOnboardedMarker(){
        QTemporaryDir directory,settings;QVERIFY(directory.isValid());QVERIFY(settings.isValid());
        const auto previous_format=QSettings::defaultFormat();
        const auto previous_ini=QSettings{QSettings::IniFormat,QSettings::UserScope,QCoreApplication::organizationName(),QCoreApplication::applicationName()}.fileName();
        const auto original_path=QFileInfo{QFileInfo{previous_ini}.absolutePath()}.absolutePath();
        const auto blocked=settings.filePath("not-a-directory");QFile file{blocked};QVERIFY(file.open(QIODevice::WriteOnly));file.close();
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,blocked);
        const auto restore=qScopeGuard([&]{QSettings::setDefaultFormat(previous_format);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,original_path);});
        ArgsManager args;SetupServerArgs(args,false);SetupQmlGuiArgs(args);args.ForceSetArg("-regtest","1");
        QmlCoreSettings::Values values; values.listen = false;
        QmlOnboardingSettings::ApplyRequest request;
        request.data_dir = {directory.path(), QmlOnboardingSettings::DataDirSource::UserSelection};
        request.touched_settings = {QStringLiteral("listen")}; request.values = values;
        request.settings_namespace = UiPreferencesNamespace::Capture();
        // Production persists an incomplete profile first. A known Core write
        // stays known when the independent directory handoff fails.
        const auto receipt = QmlOnboardingSettings::PersistProfile(args, request);
        QVERIFY(!receipt.completed); QVERIFY(receipt.replacement_written);
        QVERIFY(!receipt.directory_saved); QVERIFY(!receipt.cleanup_completed);
        QVERIFY(!receipt.uncertain); QVERIFY(!receipt.error.isEmpty()); QVERIFY(receipt.recovery);
        QVERIFY(!args.GetPersistentSetting("qml_onboarded").isTrue());
        QCOMPARE(SettingToBool(args.GetPersistentSetting("listen")), std::optional<bool>{false});
        fs::path path; QVERIFY(args.GetSettingsPath(&path));
        QFile saved{QString::fromStdString(path.utf8string())}; QVERIFY(saved.open(QIODevice::ReadOnly));
        const auto replacement = saved.readAll();
        const auto data = QJsonDocument::fromJson(replacement).object();
        QVERIFY(!data.value("qml_onboarded").toBool());
        // Query the original attempt without replaying the replacement write.
        const auto queried = QmlOnboardingSettings::QueryProfile(args, receipt);
        QVERIFY(!queried.completed); QVERIFY(queried.replacement_written);
        QVERIFY(!queried.directory_saved); QVERIFY(!queried.cleanup_completed);
        QCOMPARE(queried.recovery, receipt.recovery);
        saved.close(); QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), replacement);
    }
    void knownFailureRetainsAnswersAndAllowsAnExplicitRetry()
    {
        Fixture f; QVERIFY(f.ready()); int calls{0}; QmlOnboardingSettings::ApplyRequest original;
        f.model->setSettingsCommitPort([&](const auto& request) {
            original = request; ++calls;
            return std::pair{calls > 1, calls > 1 ? QString{} : QStringLiteral("Settings disk is unavailable")};
        });
        QSignalSpy committed{f.model.get(), &OnboardingOptionsModel::settingsCommitted};
        const auto expected = f.model->applyRequest(); f.model->commitSettings();
        QVERIFY(f.model->setupCommitPending()); QVERIFY(!f.model->canFinish()); QCOMPARE(calls, 0);
        QVERIFY(f.settle()); QCOMPARE(calls, 1); QCOMPARE(committed.count(), 0);
        QVERIFY(!f.model->setupCommitted()); QVERIFY(!f.model->canFinish());
        QCOMPARE(f.model->applyRequest().values.prune_size_gb, expected.values.prune_size_gb);
        QCOMPARE(original.data_dir.path, expected.data_dir.path); QVERIFY(!f.model->setupCommitError().isEmpty());
        // A known no-write failure retains the answers. The public retry
        // command revalidates them before a distinct explicit save admission.
        f.model->retrySetupChecks(); QVERIFY(f.settle()); QVERIFY(f.model->canFinish()); QCOMPARE(calls, 1);
        QCOMPARE(f.model->applyRequest().values.prune_size_gb, expected.values.prune_size_gb);
        QCOMPARE(f.model->applyRequest().data_dir.path, expected.data_dir.path);
        f.model->commitSettings(); QVERIFY(f.settle()); QCOMPARE(calls, 2); QCOMPARE(committed.count(), 1);
        QVERIFY(f.model->setupCommitted()); QVERIFY(!f.model->canFinish());
        f.model->commitSettings(); QVERIFY(f.settle()); QCOMPARE(calls, 2);
    }
    void shutdownRetainsThePhysicalSetupOutcome_data(){QTest::addColumn<int>("outcome");QTest::newRow("success")<<0;QTest::newRow("known-failure")<<1;QTest::newRow("unknown-throw")<<2;}
    void shutdownRetainsThePhysicalSetupOutcome(){QFETCH(int,outcome);Fixture f;QVERIFY(f.ready());int calls{0};f.model->setSettingsCommitPort([&](const auto&){++calls;if(outcome==2)throw std::runtime_error("lost setup result");return std::pair{outcome==0,QStringLiteral("write failed")};});
        const auto receipt=f.model->setupCommitReceipt();QSignalSpy success{f.model.get(),&OnboardingOptionsModel::settingsCommitted},drained{f.model.get(),&OnboardingOptionsModel::shutdownFinished};f.model->commitSettings();QVERIFY(f.transport.step(execution::Lane::Domain));const auto held=f.transport.dispatchNext(execution::Lane::Core);QVERIFY(held);f.model->beginShutdown();QCOMPARE(drained.count(),0);QVERIFY(f.transport.finishWork(*held));QCOMPARE(calls,1);QCOMPARE(receipt->load(),outcome==0?OnboardingOptionsModel::SetupCommitted:outcome==1?OnboardingOptionsModel::SetupFailed:OnboardingOptionsModel::SetupUncertain);QVERIFY(f.settle());QCOMPARE(drained.count(),1);QCOMPARE(success.count(),0);f.model->commitSettings();QVERIFY(f.settle());QCOMPARE(calls,1);
    }
    void committedSetupIsNotAppliedAgainDuringNodePreparation(){Fixture f;QVERIFY(f.ready());int commits{0};f.model->setSettingsCommitPort([&](const auto&){++commits;return std::pair{true,QString{}};});f.model->commitSettings();QVERIFY(f.settle());QVERIFY(f.model->setupCommitted());ArgsManager args;SetupServerArgs(args,false);SetupQmlGuiArgs(args);args.ForceSetArg("-regtest","1");args.ForceSetArg("-datadir",f.directory.path().toStdString());int preparations{0};QSignalSpy prepared{f.model.get(),&OnboardingOptionsModel::nodePrepared};f.model->prepareNode(args,[&]{++preparations;return true;});QVERIFY(f.settle());QCOMPARE(commits,1);QCOMPARE(preparations,1);QCOMPARE(prepared.count(),1);QVERIFY(prepared.front().front().toBool());QVERIFY(!args.GetPersistentSetting("qml_onboarded").isTrue());}
    void unknownCommitCannotBeRepeated(){Fixture f;QVERIFY(f.ready());int calls{0};f.model->setSettingsCommitPort([&](const auto&)->std::pair<bool,QString>{++calls;throw 7;});f.model->commitSettings();QVERIFY(f.settle());QCOMPARE(f.model->setupCommitReceipt()->load(),OnboardingOptionsModel::SetupUncertain);QVERIFY(!f.model->setupCommitError().isEmpty());f.model->commitSettings();QVERIFY(f.settle());QCOMPARE(calls,1);}
};
BITCOINQML_REGISTER_QT_TEST(SetupCommitTests)
#include <test_setupcommit.moc>
