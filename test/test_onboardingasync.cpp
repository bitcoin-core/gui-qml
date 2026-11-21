// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <chainparams.h>
#include <common/args.h>
#include <init.h>
#include <qml/backendexecutor.h>
#include <test/mocks/drainexecutors.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <qml/guiargs.h>
#include <qml/models/onboardingoptionsmodel.h>
#include <qml/onboarding_settings.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QPointer>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>

#ifdef Q_OS_UNIX
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace {
bool WriteFile(const QString& path, const QByteArray& contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(contents) == contents.size();
}

std::vector<std::string> Arguments(const QString& path)
{
    return {"bitcoin-qt", "-regtest", "-datadir=" + path.toStdString()};
}
} // namespace

class OnboardingAsyncTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void directorySelectionHasOneValidatedCommand()
    {
        const auto& meta = OnboardingOptionsModel::staticMetaObject;
        QCOMPARE(meta.indexOfMethod("setCustomDataDirArgs(QString)"), -1);
        QCOMPARE(meta.indexOfMethod("setCustomDataDirString(QString)"), -1);
        QVERIFY(meta.indexOfMethod("selectCustomDataDir(QString)") >= 0);
        QVERIFY(meta.indexOfMethod("useDefaultDataDir()") >= 0);
    }
    void manualPreparationOwnershipAndSignalOrder_data();
    void manualPreparationOwnershipAndSignalOrder();
    void manualSelectionAndDefaultGettersAreOwned();
    void prepareAfterShutdownReportsFailure_data();
    void prepareAfterShutdownReportsFailure();
    void shutdownDuringPreparationPreservesCompletion_data();
    void shutdownDuringPreparationPreservesCompletion();
    void shutdownObserverCanDestroyPreparedModel();
    void previewReadsOwnNetworkWithoutChangingGlobalParams();
    void startupStatusReadsOwnNetworkWithoutChangingGlobalParams();
    void reselectCustomDataDirAfterRepair_data();
    void reselectCustomDataDirAfterRepair();
    void reselectCustomDataDirReloadsConfig();
    void reselectDefaultDataDirRefreshesPreview();
    void blockedConfigReadKeepsGuiResponsiveAndRejectsStaleSelection();
};

void OnboardingAsyncTests::manualPreparationOwnershipAndSignalOrder_data()
{
    QTest::addColumn<QString>("outcome");for(const char* name:{"success","failure","exception"})QTest::newRow(name)<<QString::fromLatin1(name);
}
void OnboardingAsyncTests::manualPreparationOwnershipAndSignalOrder()
{
    QFETCH(QString,outcome);ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QTemporaryDir directory;QVERIFY(directory.isValid());ArgsManager args;SetupServerArgs(args,false);SetupQmlGuiArgs(args);
    args.ForceSetArg("-regtest","1");args.ForceSetArg("-datadir",directory.path().toStdString());
    OnboardingOptionsModel model{Arguments(directory.path()),false,nullptr,{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}}};
    auto publish=[&]{if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;};
    auto settle=[&]{for(int turn=0;turn<100;++turn){if(!transport.queuedIdle()){transport.stepNext();continue;}if(publish())continue;return true;}return false;};
    QVERIFY(settle());QVERIFY(!model.validationPending());
    model.configureSettingsCommit(args);QVERIFY(model.canFinish());model.commitSettings();QVERIFY(settle());QVERIFY(model.setupCommitted());
    QStringList signals;connect(&model,&OnboardingOptionsModel::nodePrepared,this,[&]{signals<<"prepared";});
    connect(&model,&OnboardingOptionsModel::shutdownFinished,this,[&]{signals<<"shutdown";});
    int calls{0};model.prepareNode(args,[&]{++calls;if(outcome=="exception")throw std::runtime_error("owned preparation failure");return outcome=="success";});
    QCOMPARE(model.preparationState(),OnboardingOptionsModel::PreparationPending);QCOMPARE(calls,0);
    QVERIFY(transport.step(execution::Lane::Domain));
    model.beginShutdown();QVERIFY(signals.empty());
    QVERIFY(transport.step(execution::Lane::Core));QCOMPARE(calls,1);QVERIFY(signals.empty());
    QCOMPARE(model.preparationState(),outcome=="success"?OnboardingOptionsModel::PreparationSucceeded:
        outcome=="failure"?OnboardingOptionsModel::PreparationFailed:OnboardingOptionsModel::PreparationUncertain);
    QVERIFY(transport.step(execution::Lane::Domain));QVERIFY(signals.empty());
    QVERIFY(settle());QCOMPARE(signals,QStringList({"prepared","shutdown"}));QCOMPARE(calls,1);
    transport.drain();QVERIFY(transport.quiescent());
}
void OnboardingAsyncTests::manualSelectionAndDefaultGettersAreOwned()
{
    ManualRoutedExecution transport;std::deque<std::function<void()>> gui;
    QTemporaryDir first,second;QVERIFY(first.isValid());QVERIFY(second.isValid());
    OnboardingOptionsModel model{Arguments(first.path()),false,nullptr,{transport.scheduler,[&](auto turn){gui.push_back(std::move(turn));}}};
    auto publish=[&]{if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;};
    auto settle=[&]{for(int turn=0;turn<150;++turn){if(!transport.queuedIdle()){transport.stepNext();continue;}if(publish())continue;return true;}return false;};
    QVERIFY(model.getDefaultDataDirString().isEmpty());QVERIFY(model.getDefaultDataDirectory().isEmpty());
    QVERIFY(transport.step(execution::Lane::Core));QVERIFY(model.getDefaultDataDirString().isEmpty());
    // Settings initialization has independent Domain decisions ahead of the
    // Native startup result. Hold every GUI publication while they complete.
    transport.drain();QVERIFY(model.getDefaultDataDirString().isEmpty());
    for(int turn=0;turn<16&&model.getDefaultDataDirString().isEmpty();++turn)QVERIFY(publish());
    QVERIFY(!model.getDefaultDataDirString().isEmpty());
    QVERIFY(settle());QVERIFY(model.selectCustomDataDir(first.path()));
    QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Domain));
    QVERIFY(model.selectCustomDataDir(second.path()));QVERIFY(publish());
    QVERIFY(settle());QCOMPARE(model.dataDir(),second.path());QVERIFY(!model.validationPending());
    QVERIFY(!model.storageCheckPending());
    transport.rejectLane(execution::Lane::Core);QVERIFY(!model.selectCustomDataDir(first.path()));QVERIFY(publish());
    QVERIFY(!model.previewError().isEmpty());QVERIFY(!model.canFinish());
    model.beginShutdown();QVERIFY(settle());transport.drain();QVERIFY(transport.quiescent());
}

void OnboardingAsyncTests::prepareAfterShutdownReportsFailure_data()
{
    QTest::addColumn<bool>("wait_for_preview");
    QTest::newRow("preview-pending") << false;
    QTest::newRow("preview-finished") << true;
}

void OnboardingAsyncTests::prepareAfterShutdownReportsFailure()
{
    QFETCH(bool, wait_for_preview);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ArgsManager args;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    OnboardingOptionsModel model(Arguments(directory.path()), false);
    if (wait_for_preview) QTRY_VERIFY(!model.validationPending());
    QSignalSpy prepared(&model, &OnboardingOptionsModel::nodePrepared);
    QSignalSpy shutdown(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    model.beginShutdown();
    bool called{false};
    model.prepareNode(args, [&] { called = true; return true; });
    QVERIFY(prepared.empty());
    QTRY_COMPARE(prepared.size(), 1);
    QVERIFY(!prepared.first().at(0).toBool());
    QCOMPARE(prepared.first().at(1).toString(), QStringLiteral("The application is shutting down."));
    QTRY_COMPARE(shutdown.size(), 1);
    QVERIFY(!called);
    model.beginShutdown();
    QCoreApplication::processEvents();
    QCOMPARE(prepared.size(), 1);
    QCOMPARE(shutdown.size(), 1);
}

void OnboardingAsyncTests::shutdownDuringPreparationPreservesCompletion_data()
{
    QTest::addColumn<QString>("outcome");
    for (const auto* outcome : {"success", "failure", "exception", "unknown"}) {
        QTest::newRow(outcome) << QString::fromLatin1(outcome);
    }
}

void OnboardingAsyncTests::shutdownDuringPreparationPreservesCompletion()
{
    QFETCH(QString, outcome);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto argv = Arguments(directory.path());
    ArgsManager args;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    SetupServerArgs(args, false);
    SetupQmlGuiArgs(args);
    std::vector<const char*> raw;
    raw.reserve(argv.size());
    for (const auto& arg : argv) raw.push_back(arg.c_str());
    std::string error;
    QVERIFY2(args.ParseParameters(static_cast<int>(raw.size()), raw.data(), error), error.c_str());
    QSemaphore entered, release;
    std::atomic_int calls{0};
    OnboardingOptionsModel model(argv, false);
    const auto drain = qScopeGuard([&] {
        release.release();
        bool drained{false};
        BackendExecutor::shutdownAll(this, [&] { drained = true; });
        QVERIFY(QTest::qWaitFor([&] { return drained; }, 5000));
    });
    QSignalSpy prepared(&model, &OnboardingOptionsModel::nodePrepared);
    QSignalSpy shutdown(&model, &OnboardingOptionsModel::shutdownFinished);
    QStringList signals;
    connect(&model, &OnboardingOptionsModel::nodePrepared, this, [&] { signals.append(QStringLiteral("prepared")); });
    connect(&model, &OnboardingOptionsModel::shutdownFinished, this, [&] { signals.append(QStringLiteral("shutdown")); });
    QTRY_VERIFY(model.canFinish());
    model.prepareNode(args, [&] {
        ++calls;
        entered.release();
        release.acquire();
        if (outcome == QStringLiteral("exception")) throw std::runtime_error{"preparation failed"};
        if (outcome == QStringLiteral("unknown")) throw 42;
        return outcome == QStringLiteral("success");
    });
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    model.beginShutdown();
    model.beginShutdown();
    model.prepareNode(args, [&] { ++calls; return true; });
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(prepared.empty());
    QVERIFY(shutdown.empty());
    release.release();
    QTRY_COMPARE_WITH_TIMEOUT(prepared.size(), 1, 5000);
    QTRY_COMPARE_WITH_TIMEOUT(shutdown.size(), 1, 5000);
    QCOMPARE(calls.load(), 1);
    QCOMPARE(prepared.first().at(0).toBool(), outcome == QStringLiteral("success"));
    const QString failure = prepared.first().at(1).toString();
    if (outcome == QStringLiteral("exception")) QCOMPARE(failure, QStringLiteral("preparation failed"));
    else if (outcome == QStringLiteral("unknown")) QCOMPARE(failure, QStringLiteral("Unknown exception preparing the node."));
    else QVERIFY(failure.isEmpty());
    QCOMPARE(signals, QStringList({QStringLiteral("prepared"), QStringLiteral("shutdown")}));
    model.beginShutdown();
    QCoreApplication::processEvents();
    QCOMPARE(prepared.size(), 1);
    QCOMPARE(shutdown.size(), 1);
}

void OnboardingAsyncTests::shutdownObserverCanDestroyPreparedModel()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ArgsManager args;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    SetupServerArgs(args, false);
    SetupQmlGuiArgs(args);
    args.ForceSetArg("-regtest", "1");
    args.ForceSetArg("-datadir", directory.path().toStdString());
    QSemaphore entered, release;
    QPointer<OnboardingOptionsModel> model = new OnboardingOptionsModel(Arguments(directory.path()), false);
    const auto cleanup = qScopeGuard([&] {
        release.release();
        bool drained{false};
        BackendExecutor::shutdownAll(this, [&] { drained = true; });
        QVERIFY(QTest::qWaitFor([&] { return drained; }, 5000));
        delete model.data();
    });
    int prepared{0};
    int shutdown{0};
    int prepared_at_shutdown{-1};
    connect(model, &OnboardingOptionsModel::nodePrepared, this, [&] { ++prepared; });
    connect(model, &OnboardingOptionsModel::shutdownFinished, this, [&] {
        ++shutdown;
        prepared_at_shutdown = prepared;
        delete model.data();
    });
    QTRY_VERIFY(model->canFinish());
    model->prepareNode(args, [&] { entered.release(); release.acquire(); return true; });
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    model->beginShutdown();
    release.release();
    QTRY_VERIFY(model.isNull());
    QCOMPARE(prepared, 1);
    QCOMPARE(shutdown, 1);
    QCOMPARE(prepared_at_shutdown, 1);
}

void OnboardingAsyncTests::previewReadsOwnNetworkWithoutChangingGlobalParams()
{
    SelectParams(ChainType::MAIN);
    const auto* live_params = &Params();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath("regtest"));
    QVERIFY(WriteFile(directory.filePath("settings.json"), R"({"listen":true})"));
    QVERIFY(WriteFile(directory.filePath("regtest/settings.json"), R"({"listen":false})"));
    const auto preview = std::async(std::launch::async, [&] {
        return QmlOnboardingSettings::Preview(Arguments(directory.path()), false, directory.path());
    }).get();
    QVERIFY2(preview.ok, qPrintable(preview.error));
    QVERIFY(!preview.values.listen);
    QVERIFY(preview.profile.has_settings_file);
    QCOMPARE(&Params(), live_params);
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
}

void OnboardingAsyncTests::startupStatusReadsOwnNetworkWithoutChangingGlobalParams()
{
    SelectParams(ChainType::MAIN);
    const auto* live_params = &Params();
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QVERIFY(QDir(directory.path()).mkpath("regtest"));
    QVERIFY(WriteFile(directory.filePath("settings.json"), R"({"qml_onboarded":false})"));
    QVERIFY(WriteFile(directory.filePath("regtest/settings.json"), R"({"qml_onboarded":true})"));
    const auto status = QmlOnboardingSettings::ResolveOnboardingStartupStatus(Arguments(directory.path()), false);
    QVERIFY2(status.ok, qPrintable(status.error));
    QVERIFY(status.qml_onboarded);
    QCOMPARE(&Params(), live_params);
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
}

void OnboardingAsyncTests::reselectCustomDataDirAfterRepair_data()
{
    QTest::addColumn<QString>("failure");
    QTest::newRow("permissions") << QStringLiteral("permissions");
    QTest::newRow("file-instead-of-directory") << QStringLiteral("file");
}

void OnboardingAsyncTests::reselectCustomDataDirAfterRepair()
{
    QFETCH(QString, failure);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString selected = directory.filePath("selected");
    const auto writable_permissions = QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner;
    const auto restore_permissions = qScopeGuard([&] {
        if (failure == "permissions") QFile::setPermissions(selected, writable_permissions);
    });
    if (failure == "file") {
        QVERIFY(WriteFile(selected, {}));
    } else {
        QVERIFY(QDir().mkpath(selected));
        if (failure == "permissions") {
#ifdef Q_OS_UNIX
            QVERIFY(QFile::setPermissions(selected, QFileDevice::ReadOwner | QFileDevice::ExeOwner));
            if (QFileInfo(selected).isWritable()) QSKIP("The current user can write to read-only directories.");
#else
            QSKIP("The permission fixture requires POSIX directory permissions.");
#endif
        }
    }

    OnboardingOptionsModel model(Arguments(directory.path()), false);
    QTRY_VERIFY(model.canFinish());
    QSignalSpy finished(&model, &OnboardingOptionsModel::dataDirSelectionFinished);
    QSignalSpy directory_changed(&model, &OnboardingOptionsModel::dataDirChanged);
    QSignalSpy custom_changed(&model, &OnboardingOptionsModel::customDataDirStringChanged);
    QVERIFY(model.selectCustomDataDir(selected));
    QVERIFY(model.validationPending());
    QVERIFY(!model.canFinish());
    QTRY_COMPARE(finished.count(), 1);
    QVERIFY(!finished.at(0).at(0).toBool());
    QVERIFY(!model.previewError().isEmpty());
    QCOMPARE(model.validateCustomDataDir(selected), model.previewError());
    QCOMPARE(model.dataDir(), selected);
    QVERIFY(!model.canFinish());

    if (failure == "permissions") {
        QVERIFY(QFile::setPermissions(selected, writable_permissions));
    } else {
        QVERIFY(QFile::remove(selected));
        QVERIFY(QDir().mkpath(selected));
    }

    // The folder picker supplies a file URL for the same normalized path.
    QVERIFY(model.selectCustomDataDir(QUrl::fromLocalFile(selected).toString()));
    QVERIFY(model.validationPending());
    QVERIFY(!model.canFinish());
    QTRY_COMPARE(finished.count(), 2);
    QVERIFY2(finished.at(1).at(0).toBool(), qPrintable(model.previewError()));
    QVERIFY(model.previewError().isEmpty());
    QVERIFY(model.validateCustomDataDir(selected).isEmpty());
    QTRY_VERIFY(model.canFinish());
    QCOMPARE(directory_changed.count(), 1);
    QCOMPARE(custom_changed.count(), 1);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
}

void OnboardingAsyncTests::reselectCustomDataDirReloadsConfig()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString config = directory.filePath("bitcoin.conf");
    QVERIFY(WriteFile(config, "listen=0\n"));
    OnboardingOptionsModel model(Arguments(directory.path()), false);
    QTRY_VERIFY(model.canFinish());
    QVERIFY(model.selectCustomDataDir(directory.path()));
    QTRY_VERIFY(model.canFinish());
    QVERIFY(!model.listen());
    model.setServer(true);

    QVERIFY(WriteFile(config, "listen=1\n"));
    QSignalSpy finished(&model, &OnboardingOptionsModel::dataDirSelectionFinished);
    QSignalSpy directory_changed(&model, &OnboardingOptionsModel::dataDirChanged);
    QVERIFY(model.selectCustomDataDir(directory.path()));
    QVERIFY(model.validationPending());
    QTRY_COMPARE(finished.count(), 1);
    QTRY_VERIFY(model.canFinish());
    QVERIFY(model.listen());
    QVERIFY(model.server()); // Refreshes preserve settings edited during onboarding.
    QCOMPARE(directory_changed.count(), 0);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
}

void OnboardingAsyncTests::reselectDefaultDataDirRefreshesPreview()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    OnboardingOptionsModel model(Arguments(directory.path()), false);
    QTRY_VERIFY(model.canFinish());
    model.useDefaultDataDir();
    QTRY_VERIFY(!model.validationPending());

    QSignalSpy finished(&model, &OnboardingOptionsModel::dataDirSelectionFinished);
    QSignalSpy directory_changed(&model, &OnboardingOptionsModel::dataDirChanged);
    model.useDefaultDataDir();
    QVERIFY(model.validationPending());
    QVERIFY(!model.canFinish());
    QTRY_COMPARE(finished.count(), 1);
    QCOMPARE(model.dataDir(), model.getDefaultDataDirString());
    QCOMPARE(directory_changed.count(), 0);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
}

void OnboardingAsyncTests::blockedConfigReadKeepsGuiResponsiveAndRejectsStaleSelection()
{
#ifndef Q_OS_UNIX
    QSKIP("The gated filesystem fixture uses a POSIX FIFO.");
#else
    SelectParams(ChainType::MAIN);
    QTemporaryDir first;
    QTemporaryDir latest;
    QVERIFY(first.isValid());
    QVERIFY(latest.isValid());
    const QByteArray pipe = QFile::encodeName(first.filePath("bitcoin.conf"));
    const QByteArray replacement = QFile::encodeName(first.filePath("bitcoin.conf.next"));
    QVERIFY(WriteFile(QString::fromLocal8Bit(replacement), "regtest=1\n"));
    QVERIFY(::mkfifo(pipe.constData(), 0600) == 0);

    QSemaphore entered;
    QSemaphore release;
    std::atomic<bool> stop{false};
    std::thread writer([&] {
        int fd{-1};
        while (!stop && fd < 0) {
            fd = ::open(pipe.constData(), O_WRONLY | O_NONBLOCK);
            if (fd < 0) QThread::msleep(1);
        }
        if (fd < 0) return;
        entered.release();
        // A finite watchdog also releases a regressed synchronous constructor.
        release.tryAcquire(1, 10000);
        ::rename(replacement.constData(), pipe.constData());
        constexpr char config[]{"regtest=1\n"};
        const auto written = ::write(fd, config, sizeof(config) - 1);
        Q_UNUSED(written);
        ::close(fd);
    });
    const auto finish_writer = qScopeGuard([&] {
        stop = true;
        release.release();
        writer.join();
    });
    OnboardingOptionsModel model(Arguments(first.path()), false);
    QVERIFY(model.validationPending());
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    bool heartbeat{false};
    QTimer::singleShot(0, &model, [&] { heartbeat = true; });
    QTRY_VERIFY(heartbeat);
    QVERIFY(!model.canFinish());
    QVERIFY(model.selectCustomDataDir(latest.path()));
    release.release();
    QTRY_VERIFY_WITH_TIMEOUT(!model.validationPending(), 10000);
    QCOMPARE(model.dataDir(), latest.path());
    QCOMPARE(Params().GetChainType(), ChainType::MAIN);
    QSignalSpy drained(&model, &OnboardingOptionsModel::shutdownFinished);
    model.beginShutdown();
    QTRY_COMPARE(drained.count(), 1);
#endif
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(OnboardingAsyncTests)
#else
QTEST_MAIN(OnboardingAsyncTests)
#endif
#include <test_onboardingasync.moc>
