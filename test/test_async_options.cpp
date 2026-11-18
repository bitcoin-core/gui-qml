// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <node/interface_ui.h>
#include <qml/asyncjoin.h>
#include <qml/models/options_model.h>
#include <qml/initexecutor.h>
#include <qml/shutdowncoordinator.h>
#include <qml/models/nodemodel.h>
#include <qml/models/settings_keys.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/drainexecutors.h>
#include <test/mocks/manualnodeobservations.h>
#include <test/mocks/manualstartupexecution.h>
#include <chainparams.h>
#include <init.h>
#include <util/translation.h>

#include <QDir>
#include <QFile>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTranslator>
#include <QtTest/QtTest>

#include <atomic>
#include <stdexcept>
#include <thread>

#ifndef BITCOINQML_NO_TEST_MAIN
const TranslateFn G_TRANSLATION_FUN{nullptr};
#endif

namespace {
struct ScopedSemaphoreRelease {
    QSemaphore& semaphore;
    ~ScopedSemaphoreRelease() { semaphore.release(); }
};

bool SettleManualOptions(ManualStartupExecution& transport,std::deque<std::function<void()>>& gui)
{
    for(int turn=0;turn<1024;++turn){transport.drain();
        if(!gui.empty()){auto publication=std::move(gui.front());gui.pop_front();publication();continue;}return transport.quiescent();}return false;
}

struct Fixture {
    QTemporaryDir directory;
    ArgsManager args;
    MockNode node;
    QVariantMap saved_settings;

    Fixture()
    {
        QSettings settings;
        for (const auto& key : settings.allKeys()) saved_settings.insert(key, settings.value(key));
        SetupServerArgs(args, false);
        args.ForceSetArg("-datadir", directory.path().toStdString());
        args.ForceSetArg("-settings", directory.filePath("settings.json").toStdString());
        SelectParams(ChainType::REGTEST);
        args.SelectConfigNetwork("regtest");
        node.get_persistent_setting_fn = [this](const std::string& name) { return args.GetPersistentSetting(name); };
    }
    ~Fixture()
    {
        {const auto drain=DrainExecutorsBeforeNativeOwnerDestruction();}
        QSettings settings;
        settings.clear();
        for (auto it = saved_settings.cbegin(); it != saved_settings.cend(); ++it) settings.setValue(it.key(), it.value());
        settings.sync();
    }
};
} // namespace

class AsyncOptionsTests : public QObject
{
    Q_OBJECT
    QString m_organization;
    QString m_application;
private Q_SLOTS:
    void initTestCase()
    {
        m_organization = QCoreApplication::organizationName();
        m_application = QCoreApplication::applicationName();
        QCoreApplication::setOrganizationName(QStringLiteral("BitcoinQmlTests"));
        QCoreApplication::setApplicationName(QStringLiteral("AsyncOptions"));
    }
    void cleanupTestCase()
    {
        QCoreApplication::setOrganizationName(m_organization);
        QCoreApplication::setApplicationName(m_application);
    }

    void signerSetterRejectsMissingExecutableBeforePersistence()
    {
        Fixture fixture; OptionsQmlModel model{fixture.node, fixture.args};
        QTRY_VERIFY(model.settingsReady() && !model.settingsPending());
        const auto previous = model.externalSignerPath();
        const auto forced = fixture.node.calls.forceSetting.load();
        model.setExternalSignerPath("this-signer-executable-does-not-exist-r15");
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY2(!model.settingsError().isEmpty(), "A direct model command must reject a missing executable.");
        QCOMPARE(model.externalSignerPath(), previous);
        QVERIFY(fixture.args.GetPersistentSetting("signer").isNull());
        QCOMPARE(fixture.node.calls.forceSetting.load(), forced);
    }

    void blockedInitialReadLeavesGuiAndGettersResponsive()
    {
        Fixture fixture;
        QSemaphore gate;
        std::atomic<bool> entered{false};
        std::atomic<bool> gui_call{false};
        std::atomic<int> calls{0};
        QThread* gui = QThread::currentThread();
        fixture.node.get_persistent_setting_fn = [&](const std::string& name) {
            if (QThread::currentThread() == gui) gui_call = true;
            if (++calls == 1) { entered = true; gate.acquire(); }
            return fixture.args.GetPersistentSetting(name);
        };
        OptionsQmlModel model(fixture.node, fixture.args);
        ScopedSemaphoreRelease release{gate};
        QTRY_VERIFY(entered.load());
        QVERIFY(!model.settingsReady());
        int heartbeats{0};
        QTimer timer;
        connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
        timer.start(0);
        QTRY_VERIFY(heartbeats > 2);
        for (int i = 0; i < 10; ++i) {
            model.coreSettingStatus(QStringLiteral("unknown"));
            model.coreSettingStatuses();
            model.dataDir();
            model.externalSignerPathValidationError(QStringLiteral("/missing"));
            model.validateCustomDataDir(QStringLiteral("/missing"));
        }
        QCOMPARE(calls.load(), 1);
        gate.release();
        QTRY_VERIFY(model.settingsReady());
        QVERIFY(!gui_call.load());
        const int loaded_calls = calls.load();
        model.coreSettingStatus(QStringLiteral("unknown"));
        model.coreSettingStatuses();
        QCOMPARE(calls.load(), loaded_calls);
    }

    void shutdownDismissesCoreQuestionBeforeDrainingAcceptedSettingsWrite()
    {
        Fixture fixture;
        execution::Executor executor{execution::Executor::Config{}};
        interfaces::Node::QuestionFn question;
        fixture.node.handle_question_fn = [&](interfaces::Node::QuestionFn callback) {
            question = std::move(callback);
            return interfaces::MakeCleanupHandler([] {});
        };
        NodeModel::Services services{};
        services.observation_executor = &executor;
        NodeModel node{fixture.node, true, std::move(services)};
        OptionsQmlModel settings{fixture.node, fixture.args, {}, &executor};
        QTRY_VERIFY(settings.settingsReady() && node.notificationsReady());
        QVERIFY(question);
        const int amount = settings.maxMempoolSizeMB() + 10;
        std::atomic_bool interrupt_saw_write{}, dismissed{};
        fixture.node.start_shutdown_fn = [&] {
            interrupt_saw_write = SettingTo<int64_t>(
                fixture.args.GetPersistentSetting("maxmempool")).value_or(0) == amount;
        };
        auto priority = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Priority));
        QmlInitExecutor lifecycle{fixture.node, priority};
        ShutdownCoordinator coordinator{lifecycle};
        coordinator.addStopHook([&] { node.close(); });
        coordinator.addStopHook([&] { settings.close(); });
        coordinator.addBeforeInterruptParticipant(&settings, &OptionsQmlModel::shutdownFinished,
                                                  [&] { settings.beginShutdown(); });
        coordinator.addParticipant(&node, &NodeModel::workDrained, [&] { node.beginShutdown(); });
        QSignalSpy finished{&coordinator, &ShutdownCoordinator::finished};
        auto core = executor.makeQueue(execution::Lane::Core);
        QSemaphore returned;
        QVERIFY(core.post([&] {
            dismissed = !question(Untranslated("Continue?"), "Continue?", CClientUIInterface::BTN_OK);
            returned.release();
        }));
        const auto unblock = qScopeGuard([&] { node.close(); returned.acquire(); });
        QTRY_VERIFY(node.runtimeDialogVisible());
        settings.setMaxMempoolSizeMB(amount);
        QVERIFY(settings.settingsPending());
        QVERIFY(fixture.args.GetPersistentSetting("maxmempool").isNull());
        coordinator.requestShutdown();
        QTRY_COMPARE(finished.count(), 1);
        QVERIFY(dismissed.load());
        QVERIFY(interrupt_saw_write.load());
        QVERIFY(!node.runtimeDialogVisible());
    }

    void failedWriteRestoresCacheAndPersistentState()
    {
        Fixture fixture;
        OptionsQmlModel model(fixture.node, fixture.args);
        QTRY_VERIFY(model.settingsReady());
        const int initial = model.maxMempoolSizeMB();
        const QString blocked = fixture.directory.filePath("blocked-settings");
        QVERIFY(QDir().mkpath(blocked));
        fixture.args.ForceSetArg("-settings", blocked.toStdString());
        QSignalSpy mempool_changed(&model, &OptionsQmlModel::maxMempoolSizeMBChanged);
        QSignalSpy language_changed(&model, &OptionsQmlModel::languageChanged);
        model.setMaxMempoolSizeMB(initial + 10);
        QVERIFY(model.settingsPending());
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY(!model.settingsError().isEmpty());
        QCOMPARE(model.maxMempoolSizeMB(), initial);
        QVERIFY(fixture.args.GetPersistentSetting("maxmempool").isNull());
        QVERIFY(!model.mempoolSettingsDirty());
        QCOMPARE(mempool_changed.size(), 2); // Optimistic edit followed by rollback.
        QCOMPARE(mempool_changed.last().first().toInt(), initial);
        QVERIFY(language_changed.empty());
    }

    void observationRetainsTheOriginalFailedSettingsOperation()
    {
        ManualStartupExecution transport;
        std::deque<std::function<void()>> gui;
        Fixture fixture;
        OptionsQmlModel model{fixture.node, fixture.args,
            [&](auto turn) { gui.push_back(std::move(turn)); }, &transport.executor()};
        const auto cleanup = qScopeGuard([&] { model.close(); SettleManualOptions(transport, gui); });
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(model.settingsReady());
        model.setExternalSignerPath(QStringLiteral("this-signer-does-not-exist-original-receipt"));
        QVERIFY(model.settingsPending());
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(!model.settingsPending());
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsFailed);
        const auto original_id = model.settingsOperationId();
        const auto original_error = model.settingsError();
        QVERIFY(!original_error.isEmpty());
        QVERIFY(model.externalSignerPath().isEmpty());
        QVERIFY(fixture.args.GetPersistentSetting("signer").isNull());

        // An observation can load a fresh snapshot, but is not a successor
        // user intent and must not replace this known failed write receipt.
        QSignalSpy operations{&model, &OptionsQmlModel::settingsOperationChanged};
        const auto forced_before_refresh = fixture.node.calls.forceSetting.load();
        model.refreshDisplaySettings();
        QCOMPARE(model.settingsOperationId(), original_id);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsFailed);
        QCOMPARE(model.settingsError(), original_error);
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(!model.settingsPending());
        QCOMPARE(model.settingsOperationId(), original_id);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsFailed);
        QCOMPARE(model.settingsError(), original_error);
        QCOMPARE(operations.size(), 0);
        QCOMPARE(fixture.node.calls.forceSetting.load(), forced_before_refresh);
        QVERIFY(fixture.args.GetPersistentSetting("signer").isNull());
    }

    void observationFailureKeepsUserOutcomeAndPublishesItsOwnReadError_data()
    {
        QTest::addColumn<bool>("reject_transport");
        QTest::newRow("read-throws") << false;
        QTest::newRow("transport-rejects") << true;
    }

    void observationFailureKeepsUserOutcomeAndPublishesItsOwnReadError()
    {
        QFETCH(bool, reject_transport);
        ManualStartupExecution transport;
        std::deque<std::function<void()>> gui;
        Fixture fixture;
        const auto reads = std::make_shared<std::atomic<int>>(0);
        const auto throw_read = std::make_shared<std::atomic_bool>(!reject_transport);
        OptionsQmlModel model{fixture.node, fixture.args,
            [&](auto turn) { gui.push_back(std::move(turn)); }, &transport.executor()};
        const auto cleanup = qScopeGuard([&] { model.close(); SettleManualOptions(transport, gui); });
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(model.settingsReady());
        model.setExternalSignerPath(QStringLiteral("original-failed-signer-intent-does-not-exist"));
        QVERIFY(SettleManualOptions(transport, gui));
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsFailed);
        const auto original_id = model.settingsOperationId();
        const auto original_error = model.settingsError();
        QVERIFY(!original_error.isEmpty());
        const auto original_writes = fixture.node.calls.forceSetting.load();
        fixture.node.get_persistent_setting_fn = [&fixture, reads, throw_read](const std::string& name) {
            ++*reads;
            if (throw_read->load()) throw std::runtime_error("Observed settings read failed");
            return fixture.args.GetPersistentSetting(name);
        };
        if (reject_transport) transport.reject_core = true;
        QSignalSpy operations{&model, &OptionsQmlModel::settingsOperationChanged};
        QSignalSpy errors{&model, &OptionsQmlModel::settingsErrorChanged};
        model.refreshDisplaySettings();
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(!model.settingsPending());
        QCOMPARE(model.settingsOperationId(), original_id);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsFailed);
        QVERIFY(model.settingsError().startsWith(original_error + QLatin1Char('\n')));
        if (reject_transport) {
            QCOMPARE(reads->load(), 0);
        } else {
            QCOMPARE(reads->load(), 1);
            QVERIFY(model.settingsError().contains(QStringLiteral("Observed settings read failed")));
        }
        QCOMPARE(operations.size(), 0);
        QVERIFY(!errors.empty());
        QCOMPARE(fixture.node.calls.forceSetting.load(), original_writes);

        // A successful fresh read resolves only its own observation diagnostic.
        throw_read->store(false);
        model.refreshDisplaySettings();
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(reads->load() > 0);
        QCOMPARE(model.settingsOperationId(), original_id);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsFailed);
        QCOMPARE(model.settingsError(), original_error);
        QCOMPARE(operations.size(), 0);
        QCOMPARE(fixture.node.calls.forceSetting.load(), original_writes);
        QVERIFY(fixture.args.GetPersistentSetting("signer").isNull());
    }

    void successorIntentOwnsItsErrorWhileEarlierPublicationIsHeld_data()
    {
        QTest::addColumn<int>("earlier");
        QTest::newRow("successful-read") << 0;
        QTest::newRow("throwing-read") << 1;
        QTest::newRow("rejected-read") << 2;
        QTest::newRow("older-user-failure") << 3;
    }

    void successorIntentOwnsItsErrorWhileEarlierPublicationIsHeld()
    {
        QFETCH(int, earlier);
        ManualStartupExecution transport;
        std::deque<std::function<void()>> gui;
        Fixture fixture;
        OptionsQmlModel model{fixture.node, fixture.args,
            [&](auto turn) { gui.push_back(std::move(turn)); }, &transport.executor()};
        const auto cleanup = qScopeGuard([&] { model.close(); SettleManualOptions(transport, gui); });
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(model.settingsReady());
        model.setExternalSignerPath(QStringLiteral("original-failure-with-a-held-successor-read"));
        QVERIFY(SettleManualOptions(transport, gui));
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsFailed);
        QVERIFY(!model.settingsError().isEmpty());
        const auto original_id = model.settingsOperationId();
        if (earlier == 1) fixture.node.get_persistent_setting_fn = [](const std::string&) -> common::SettingsValue {
            throw std::runtime_error("Old observation failure must not replace its successor");
        };
        if (earlier == 2) transport.reject_core = true;
        if (earlier == 3) {
            model.setExternalSignerPath(QStringLiteral("older-user-operation-also-fails-before-save"));
        } else {
            model.refreshDisplaySettings();
        }
        if (earlier != 2) {
            QVERIFY(transport.step(execution::Lane::Core));
            QVERIFY(transport.step(execution::Lane::Utility));
        }
        QVERIFY(!gui.empty());
        QVERIFY(model.settingsPending());
        fixture.node.get_persistent_setting_fn = [&fixture](const std::string& name) { return fixture.args.GetPersistentSetting(name); };
        const auto before_successor = model.settingsOperationId();
        const auto amount = model.maxMempoolSizeMB() + 10;
        QSignalSpy operations{&model, &OptionsQmlModel::settingsOperationChanged};
        model.setMaxMempoolSizeMB(amount);
        QCOMPARE(model.settingsOperationId(), before_successor + 1);
        QVERIFY(model.settingsOperationId() > original_id);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsPending);
        QVERIFY(model.settingsError().isEmpty());
        QCOMPARE(operations.size(), 1);

        // Release the old GUI packet before the successor's result. Neither
        // its old snapshot nor its error owns the newer operation projection.
        auto old_publication = std::move(gui.front());
        gui.pop_front();
        old_publication();
        QCOMPARE(model.settingsOperationId(), before_successor + 1);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsPending);
        QVERIFY(model.settingsError().isEmpty());
        QCOMPARE(operations.size(), 1);
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(!model.settingsPending());
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsApplied);
        QVERIFY(model.settingsError().isEmpty());
        QCOMPARE(model.maxMempoolSizeMB(), amount);
        QCOMPARE(fixture.args.GetPersistentSetting("maxmempool").getInt<int>(), amount);
        QCOMPARE(operations.size(), 2);
    }

    void recoveryNotificationCannotApplyAnOldSnapshotToItsReentrantSuccessor()
    {
        ManualStartupExecution transport;
        std::deque<std::function<void()>> gui;
        Fixture fixture;
        const auto throw_read = std::make_shared<std::atomic_bool>(false);
        QString original_error;
        bool successor_started{false};
        int amount{0};
        fixture.node.get_persistent_setting_fn = [&fixture, throw_read](const std::string& name) {
            if (throw_read->load()) throw std::runtime_error("Recovery observation error");
            return fixture.args.GetPersistentSetting(name);
        };
        OptionsQmlModel model{fixture.node, fixture.args,
            [&](auto turn) { gui.push_back(std::move(turn)); }, &transport.executor()};
        const auto cleanup = qScopeGuard([&] { model.close(); SettleManualOptions(transport, gui); });
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(model.settingsReady());
        model.setExternalSignerPath(QStringLiteral("failed-user-signer-before-observation-recovery"));
        QVERIFY(SettleManualOptions(transport, gui));
        original_error = model.settingsError();
        QVERIFY(!original_error.isEmpty());
        const auto original_id = model.settingsOperationId();
        throw_read->store(true);
        model.refreshDisplaySettings();
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(model.settingsError().contains(QStringLiteral("Recovery observation error")));
        throw_read->store(false);
        amount = model.maxMempoolSizeMB() + 10;
        QSignalSpy values{&model, &OptionsQmlModel::maxMempoolSizeMBChanged};
        QSignalSpy operations{&model, &OptionsQmlModel::settingsOperationChanged};
        connect(&model, &OptionsQmlModel::settingsErrorChanged, &model, [&] {
            if (successor_started || !model.presentationOpen() || model.settingsError() != original_error) return;
            successor_started = true;
            model.setMaxMempoolSizeMB(amount);
        });
        model.refreshDisplaySettings();
        QVERIFY(SettleManualOptions(transport, gui));
        QVERIFY(successor_started);
        QCOMPARE(model.settingsOperationId(), original_id + 1);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsApplied);
        QVERIFY(!model.settingsPending());
        QVERIFY(model.settingsError().isEmpty());
        QCOMPARE(model.maxMempoolSizeMB(), amount);
        QCOMPARE(fixture.args.GetPersistentSetting("maxmempool").getInt<int>(), amount);
        QVERIFY(!values.empty());
        for (const auto& value : values) QCOMPARE(value.front().toInt(), amount);
        QCOMPARE(operations.size(), 2);
    }

    void savingSettingsOnlyNotifiesChangedValues()
    {
        Fixture fixture;
        OptionsQmlModel model(fixture.node, fixture.args);
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY(model.settingsReady());
        QSignalSpy dbcache(&model, &OptionsQmlModel::dbcacheSizeMiBChanged);
        QSignalSpy mempool(&model, &OptionsQmlModel::maxMempoolSizeMBChanged);
        QSignalSpy threads(&model, &OptionsQmlModel::scriptThreadsChanged);
        QSignalSpy signer(&model, &OptionsQmlModel::externalSignerPathChanged);
        QSignalSpy data_dir(&model, &OptionsQmlModel::dataDirChanged);
        QSignalSpy defaults(&model, &OptionsQmlModel::dataDirDefaultsChanged);
        QSignalSpy custom_data_dir(&model, &OptionsQmlModel::customDataDirStringChanged);
        QSignalSpy language(&model, &OptionsQmlModel::languageChanged);
        QSignalSpy display_unit(&model, &OptionsQmlModel::displayUnitChanged);
        QSignalSpy urls(&model, &OptionsQmlModel::thirdPartyTransactionUrlsChanged);
        QSignalSpy font_choice(&model, &OptionsQmlModel::moneyFontChoiceChanged);
        QSignalSpy font(&model, &OptionsQmlModel::moneyFontChanged);

        const int unit = (model.displayUnit() + 1) % 4;
        model.setDisplayUnit(unit);
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY2(model.settingsError().isEmpty(), qPrintable(model.settingsError()));
        QCOMPARE(model.displayUnit(), unit);
        QCOMPARE(display_unit.size(), 1); // Saving the optimistic value adds no notification.
        for (const auto* spy : {&dbcache, &mempool, &threads, &signer, &data_dir, &defaults,
                               &custom_data_dir, &language, &urls, &font_choice, &font}) {
            QVERIFY(spy->empty());
        }
    }

    void mappingDrainDoesNotBlockGuiOrAcceptMoreCommands()
    {
        Fixture fixture;
        QSemaphore gate;
        std::atomic<bool> entered{false};
        std::atomic<bool> gui_call{false};
        std::atomic<int> calls{0};
        QThread* gui = QThread::currentThread();
        fixture.node.map_port_fn = [&](bool) {
            gui_call = QThread::currentThread() == gui;
            ++calls;
            entered = true;
            gate.acquire();
        };
        OptionsQmlModel model(fixture.node, fixture.args);
        ScopedSemaphoreRelease release{gate};
        QTRY_VERIFY(model.settingsReady());
        const bool initial = model.natpmp();
        model.setNatpmp(!initial);
        QTRY_VERIFY(entered.load());
        QVERIFY(model.settingsPending());
        QTRY_COMPARE(model.natpmp(), !initial);
        QSignalSpy drained(&model, &OptionsQmlModel::shutdownFinished);
        // A focused field may emit editingFinished while the window is being
        // disabled, before draining starts. Close admission without emitting
        // signals first; the already accepted mapping must still drain.
        QSignalSpy statuses(&model, &OptionsQmlModel::coreSettingStatusesChanged);
        model.close();
        QCOMPARE(statuses.size(), 0);
        const int mempool = model.maxMempoolSizeMB();
        model.setMaxMempoolSizeMB(mempool + 1);
        QCOMPARE(model.maxMempoolSizeMB(), mempool);
        model.setNatpmp(initial);
        QCOMPARE(model.natpmp(), !initial);
        model.beginShutdown();
        model.beginShutdown();
        int heartbeats{0};
        QTimer timer;
        connect(&timer, &QTimer::timeout, [&] { ++heartbeats; });
        timer.start(0);
        QTRY_VERIFY(heartbeats > 2);
        QCOMPARE(drained.size(), 0);
        bool joined{false};
        QObject receiver;
        WaitForThreadJoins(&receiver, [&] { joined = true; });
        QCoreApplication::sendPostedEvents(&receiver, QEvent::MetaCall);
        const bool joined_while_mapping = joined;
        gate.release();
        QTRY_COMPARE(drained.size(), 1);
        QTRY_VERIFY(joined);
        QVERIFY(!joined_while_mapping);
        QCOMPARE(calls.load(), 1);
        QVERIFY(!gui_call.load());
    }

    void editorRetirementCanDestroyStandaloneOptions()
    {
        Fixture fixture;
        auto model = std::make_unique<OptionsQmlModel>(fixture.node, fixture.args);
        QTRY_VERIFY(model->settingsReady());
        const QPointer<SettingsEditorModel> editor{model->createSettingsEditor()};
        QVERIFY(editor);
        connect(editor, &SettingsEditorModel::changed, this, [&] { model.reset(); });

        model->beginShutdown();
        QVERIFY(!model);
        QVERIFY(!editor);
        bool joined{false};
        QObject receiver;
        WaitForThreadJoins(&receiver, [&] { joined = true; });
        QTRY_VERIFY(joined);
    }

    void queuedTogglesKeepLastRequestedValue()
    {
        Fixture fixture;
        QSemaphore gate;
        std::atomic<int> calls{0};
        fixture.node.map_port_fn = [&](bool) {
            if (++calls == 1) gate.acquire();
        };
        OptionsQmlModel model(fixture.node, fixture.args);
        ScopedSemaphoreRelease release{gate};
        QTRY_VERIFY(model.settingsReady());
        const bool initial = model.natpmp();
        model.setNatpmp(!initial);
        QTRY_COMPARE(calls.load(), 1);
        model.setNatpmp(initial);
        QTRY_COMPARE(model.natpmp(), initial);
        gate.release();
        QTRY_VERIFY(!model.settingsPending());
        QCOMPARE(calls.load(), 2);
        QCOMPARE(model.natpmp(), initial);
        QVERIFY(model.settingsError().isEmpty());
        QVERIFY(!model.restartRequired());
    }

    void manualReadWriteOutcomeAndQuietDrain()
    {
        ManualStartupExecution transport;std::deque<std::function<void()>> gui;
        Fixture fixture;
        OptionsQmlModel model{fixture.node,fixture.args,[&](auto turn){gui.push_back(std::move(turn));}, &transport.executor()};
        auto publish=[&]{if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;};
        QVERIFY(!model.settingsReady());QCOMPARE(fixture.node.calls.getPersistentSetting.load(),0);
        QVERIFY(transport.step(execution::Lane::Core));QVERIFY(!model.settingsReady());
        transport.drain();QVERIFY(!model.settingsReady());
        QVERIFY(SettleManualOptions(transport,gui));QVERIFY(model.settingsReady());QVERIFY(!model.settingsPending());
        const int initial=model.maxMempoolSizeMB();model.setMaxMempoolSizeMB(initial+10);
        const auto ticket=model.settingsOperationId();QVERIFY(ticket);QCOMPARE(model.settingsOperationState(),OptionsQmlModel::SettingsPending);
        QVERIFY(transport.step(execution::Lane::Core));
        QCOMPARE(model.settingsOperationState(),OptionsQmlModel::SettingsApplied);
        QCOMPARE(SettingTo<int64_t>(fixture.args.GetPersistentSetting("maxmempool")).value_or(0),int64_t{initial+10});
        QSignalSpy changes{&model,&OptionsQmlModel::settingsPendingChanged};QSignalSpy drained{&model,&OptionsQmlModel::shutdownFinished};
        model.close();model.beginShutdown();QCOMPARE(drained.size(),0);
        transport.drain();while(publish()){}
        QCOMPARE(drained.size(),1);QCOMPARE(changes.size(),0);QCOMPARE(model.settingsOperationId(),ticket);
        QCOMPARE(model.settingsOperationState(),OptionsQmlModel::SettingsApplied);
        transport.drain();QVERIFY(transport.quiescent());
    }
    void manualAdmissionFailureAndSignerRevision()
    {
        ManualStartupExecution transport;std::deque<std::function<void()>> gui;
        Fixture fixture;
        OptionsQmlModel model{fixture.node,fixture.args,[&](auto turn){gui.push_back(std::move(turn));}, &transport.executor()};
        auto publish=[&]{if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;};
        QVERIFY(SettleManualOptions(transport,gui));QVERIFY(model.settingsReady());
        transport.reject_core = true;model.setMaxMempoolSizeMB(model.maxMempoolSizeMB()+10);
        QCOMPARE(model.settingsOperationState(),OptionsQmlModel::SettingsFailed);QVERIFY(publish());
        QVERIFY(SettleManualOptions(transport,gui));QVERIFY(!model.settingsPending());QVERIFY(!model.settingsError().isEmpty());
        model.requestExternalSignerPathValidation("/missing/original");
        QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Utility));
        model.requestExternalSignerPathValidation(QCoreApplication::applicationFilePath());QVERIFY(publish());QVERIFY(model.signerPathValidationPending());
        QVERIFY(!transport.step(execution::Lane::Core)); // Follow-up cannot recurse in the old outcome callback.
        QVERIFY(publish());
        QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Utility));QVERIFY(publish());
        QVERIFY(!model.signerPathValidationPending());QVERIFY(model.signerPathError().isEmpty());
        model.requestExternalSignerPathValidation("/missing/quiet-original");
        QVERIFY(transport.step(execution::Lane::Core));QVERIFY(transport.step(execution::Lane::Utility));
        model.requestExternalSignerPathValidation(QCoreApplication::applicationFilePath());QVERIFY(publish());
        QVERIFY(!transport.step(execution::Lane::Core));
        model.close();
        QSignalSpy quiet{&model,&OptionsQmlModel::signerPathValidationChanged};
        QVERIFY(SettleManualOptions(transport,gui));QCOMPARE(quiet.size(),0);
        transport.drain();QVERIFY(transport.quiescent());
    }

    void manualTransactionLinksRetainExactTxidAndRetireOnSourceClose()
    {
        using execution::Lane;
        std::deque<std::function<void()>> gui;
        Fixture fixture;
        QSettings settings;
        settings.setValue(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS, "https://example.test/tx/%s|file://host/%s");
        settings.sync();

        ManualStartupExecution worker;
        const auto utility = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(Lane::Utility));
        const auto retirement = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(Lane::Utility));
        const auto post = [&](auto turn) { gui.push_back(std::move(turn)); };
        PresentationServices presentation{{post, [retirement](auto release) { return retirement->post(std::move(release)); }, {}, utility}};
        OptionsQmlModel model{fixture.node, fixture.args, post, &worker.executor(), &presentation};
        const auto publish = [&] {
            if (gui.empty()) return false;
            auto turn = std::move(gui.front());
            gui.pop_front();
            turn();
            return true;
        };
        for (int turn{0}; turn < 32 && (!worker.quiescent() || !gui.empty()); ++turn) {
            worker.drain();
            while (publish()) {}
        }
        QVERIFY(model.settingsReady());
        QVERIFY(worker.quiescent());
        QCOMPARE(presentation.pendingRetirements(), size_t{0});
        const auto core_submissions = worker.coreSubmissions();

        std::unique_ptr<OptionsTransactionLinks> links{model.createTransactionLinks()};
        QVERIFY(links);
        QVERIFY(utility->post([] {}));
        links->setTxid("old");
        QVERIFY(links->pending());
        QCOMPARE(links->rowCount(), 0);
        QVERIFY(worker.step(Lane::Utility));
        QVERIFY(gui.empty()); // The preceding presentation task had its own turn.
        QVERIFY(worker.step(Lane::Utility));
        QVERIFY(!gui.empty());
        links->setTxid("new");
        QVERIFY(publish());
        QVERIFY(links->pending());
        QCOMPARE(links->rowCount(), 0);
        worker.drain();
        QVERIFY(publish());
        QVERIFY(!links->pending());
        QCOMPARE(links->rowCount(), 1);
        QCOMPARE(links->data(links->index(0), OptionsTransactionLinks::UrlRole).toString(), QStringLiteral("https://example.test/tx/new"));

        links->setTxid("closed");
        QSignalSpy changed{links.get(), &OptionsTransactionLinks::resultChanged};
        presentation.close();
        model.close();
        bool drained{false};
        QObject receiver;
        presentation.whenRetired(&receiver, [&] { drained = true; });
        worker.drain();
        QTRY_VERIFY(drained);
        QCOMPARE(presentation.pendingRetirements(), size_t{0});
        QVERIFY(!gui.empty());
        while (publish()) {}
        QCOMPARE(changed.size(), 0);
        QCOMPARE(links->rowCount(), 0);
        QCOMPARE(worker.coreSubmissions(), core_submissions);
        QVERIFY(worker.quiescent());
    }


    // #1136: a links view that is already open follows the setting. The view is
    // rebuilt from the new value, so an old host cannot stay offered.
    void manualTransactionLinksFollowTheSettingWhileAViewIsOpen()
    {
        ManualStartupExecution transport;std::deque<std::function<void()>> gui;Fixture fixture;
        QSettings settings;settings.setValue(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS,"https://one.example/tx/%s");settings.sync();
        OptionsQmlModel model{fixture.node,fixture.args,[&](auto turn){gui.push_back(std::move(turn));}, &transport.executor()};
        QVERIFY(SettleManualOptions(transport,gui));QVERIFY(model.settingsReady());
        std::unique_ptr<OptionsTransactionLinks> links{model.createTransactionLinks()};QVERIFY(links);
        const auto settle=[&]{QVERIFY(SettleManualOptions(transport,gui));};
        const auto url=[&](int row){return links->data(links->index(row),OptionsTransactionLinks::UrlRole).toString();};
        links->setTxid("tx");settle();
        QCOMPARE(links->rowCount(),1);QCOMPARE(url(0),QStringLiteral("https://one.example/tx/tx"));

        model.setThirdPartyTransactionUrls("https://two.example/tx/%s");settle();
        QCOMPARE(links->rowCount(),1);QCOMPARE(url(0),QStringLiteral("https://two.example/tx/tx"));

        model.setThirdPartyTransactionUrls("");settle();
        QCOMPARE(links->rowCount(),0);

        model.setThirdPartyTransactionUrls("https://three.example/tx/%s");settle();
        QCOMPARE(links->rowCount(),1);QCOMPARE(url(0),QStringLiteral("https://three.example/tx/tx"));
        model.close();transport.drain();while(!gui.empty()){auto turn=std::move(gui.front());gui.pop_front();turn();}
        QVERIFY(transport.quiescent());
    }

    void manualTransactionLinkPresentationUsesBoundedTurns()
    {
        ManualStartupExecution transport;std::deque<std::function<void()>> gui;Fixture fixture;
        QStringList templates;for(int row=0;row<96;++row)templates<<QStringLiteral("https://example.test/%1/%s").arg(row);
        QSettings settings;settings.setValue(SettingsKeys::THIRD_PARTY_TRANSACTION_URLS,templates.join('|'));settings.sync();
        OptionsQmlModel model{fixture.node,fixture.args,[&](auto turn){gui.push_back(std::move(turn));}, &transport.executor()};
        auto publish=[&]{if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true;};
        QVERIFY(SettleManualOptions(transport,gui));QVERIFY(model.settingsReady());
        std::unique_ptr<OptionsTransactionLinks> links{model.createTransactionLinks()};QVERIFY(links);links->setTxid("exact");
        QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(publish());QCOMPARE(links->rowCount(),32);QVERIFY(links->pending());
        QVERIFY(publish());QCOMPARE(links->rowCount(),64);QVERIFY(links->pending());
        QVERIFY(publish());QCOMPARE(links->rowCount(),96);QVERIFY(!links->pending());
        QCOMPARE(links->data(links->index(95),OptionsTransactionLinks::UrlRole).toString(),QStringLiteral("https://example.test/95/exact"));
        links->setTxid("later");QVERIFY(transport.step(execution::Lane::Utility));
        QVERIFY(publish());QCOMPARE(links->rowCount(),64);QSignalSpy changed{links.get(),&QAbstractItemModel::rowsRemoved};model.close();
        while(publish()){}QCOMPARE(changed.size(),0);transport.drain();QVERIFY(transport.quiescent());
    }

    void trackedOptionsIntentSurvivesCloseBeforeNativeAdmission()
    {
        using execution::Lane;
        Fixture fixture;
        QSemaphore dispatch_entered, allow_dispatch;
        std::atomic_bool hold_core{false};
        ManualNodeObservations env{[&](Lane lane) {
            if (lane != Lane::Core || !hold_core.exchange(false)) return;
            dispatch_entered.release();
            allow_dispatch.acquire();
        }};
        NodeModel original_node{fixture.node, false, env.services()};
        OptionsQmlModel model{fixture.node, fixture.args, env.post(), &env.executor()};
        bool requested{false}, planned{false};
        int mapping_calls{0};
        std::thread planner;
        const auto retire = qScopeGuard([&] {
            allow_dispatch.release();
            if (planner.joinable()) planner.join();
            model.beginShutdown();
            original_node.beginShutdown();
            QVERIFY(DrainManualNodeTurns(env));
            QVERIFY(original_node.isDrained());
        });
        QVERIFY(DrainManualNodeTurns(env));
        QVERIFY(model.settingsReady());
        QVERIFY(!model.settingsPending());

        requested = !model.natpmp();
        fixture.node.map_port_fn = [&](bool value) { QCOMPARE(value, requested); ++mapping_calls; };
        hold_core = true;
        model.setNatpmp(requested);
        const auto ticket = model.settingsOperationId();
        QVERIFY(ticket);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsPending);
        planner = std::thread([&] { planned = env.step(Lane::Utility); });
        QVERIFY(dispatch_entered.tryAcquire(1, 5000));
        QCOMPARE(mapping_calls, 0);
        QSignalSpy drained{&model, &OptionsQmlModel::shutdownFinished};
        QSignalSpy values{&model, &OptionsQmlModel::natpmpChanged};
        original_node.close();
        model.close();
        model.beginShutdown();
        QCOMPARE(drained.size(), 0);

        allow_dispatch.release();
        planner.join();
        QVERIFY(planned);
        QCOMPARE(mapping_calls, 0);
        QVERIFY(env.step(Lane::Core));
        QCOMPARE(mapping_calls, 1);
        QCOMPARE(SettingToBool(fixture.args.GetPersistentSetting("natpmp")).value_or(!requested), requested);
        QCOMPARE(model.settingsOperationId(), ticket);
        QCOMPARE(model.settingsOperationState(), OptionsQmlModel::SettingsApplied);
        QVERIFY(DrainManualNodeTurns(env));
        QCOMPARE(drained.size(), 1);
        QCOMPARE(values.size(), 0);
        QCOMPARE(mapping_calls, 1);
    }

    void legacyLanguageMatchesStartupFallback_data()
    {
        QTest::addColumn<QString>("chain_language");
        QTest::addColumn<QVariant>("gui_language");
        QTest::addColumn<QString>("cli_language");
        QTest::addColumn<QString>("expected");
        QTest::newRow("legacy-chain") << QStringLiteral("es") << QVariant{} << QString{} << QStringLiteral("es");
        QTest::newRow("legacy-default") << QString{} << QVariant{} << QString{} << QStringLiteral("es");
        QTest::newRow("explicit-system-default") << QStringLiteral("es") << QVariant{QString{}} << QString{} << QString{};
        QTest::newRow("explicit-current-language") << QStringLiteral("es") << QVariant{QStringLiteral("fr")} << QString{} << QStringLiteral("fr");
        QTest::newRow("command-line") << QStringLiteral("es") << QVariant{} << QStringLiteral("de") << QStringLiteral("de");
    }

    void legacyLanguageMatchesStartupFallback()
    {
        QFETCH(QString, chain_language);
        QFETCH(QVariant, gui_language);
        QFETCH(QString, cli_language);
        QFETCH(QString, expected);
        Fixture fixture;
        fixture.args.ForceSetArg("-regtest", "1");
        if (!cli_language.isEmpty()) fixture.args.ForceSetArg("-lang", cli_language.toStdString());
        QSettings current;
        current.remove(SettingsKeys::LANGUAGE);
        if (gui_language.isValid()) current.setValue(SettingsKeys::LANGUAGE, gui_language);
        current.sync();
        QString legacy_organization = QStringLiteral("Bitcoin");
#ifdef Q_OS_MACOS
        if (QSettings::defaultFormat() == QSettings::NativeFormat) legacy_organization = QStringLiteral("bitcoin.org");
#endif
        QSettings legacy_chain(QSettings::defaultFormat(), QSettings::UserScope, legacy_organization, QStringLiteral("Bitcoin-Qt-regtest"));
        QSettings legacy_default(QSettings::defaultFormat(), QSettings::UserScope, legacy_organization, QStringLiteral("Bitcoin-Qt"));
        const QVariant old_chain = legacy_chain.value(SettingsKeys::LANGUAGE);
        const QVariant old_default = legacy_default.value(SettingsKeys::LANGUAGE);
        const auto restore_legacy = qScopeGuard([&] {
            if (old_chain.isValid()) legacy_chain.setValue(SettingsKeys::LANGUAGE, old_chain);
            else legacy_chain.remove(SettingsKeys::LANGUAGE);
            if (old_default.isValid()) legacy_default.setValue(SettingsKeys::LANGUAGE, old_default);
            else legacy_default.remove(SettingsKeys::LANGUAGE);
            legacy_chain.sync();
            legacy_default.sync();
        });
        if (chain_language.isEmpty()) legacy_chain.remove(SettingsKeys::LANGUAGE);
        else legacy_chain.setValue(SettingsKeys::LANGUAGE, chain_language);
        legacy_default.setValue(SettingsKeys::LANGUAGE, QStringLiteral("es"));
        legacy_chain.sync();
        legacy_default.sync();
        OptionsQmlModel model(fixture.node, fixture.args);
        QSignalSpy changed(&model, &OptionsQmlModel::languageChanged);
        QTRY_VERIFY(model.settingsReady());
        QCOMPARE(model.language(), expected);
        QCOMPARE(changed.size(), expected.isEmpty() ? 0 : 1);
        if (gui_language.isValid() || !cli_language.isEmpty()) return;

        model.setLanguage(QString{});
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY2(model.settingsError().isEmpty(), qPrintable(model.settingsError()));
        QCOMPARE(model.language(), QString{});
        QCOMPARE(changed.size(), 2);
        QVERIFY(current.contains(SettingsKeys::LANGUAGE));
        QCOMPARE(current.value(SettingsKeys::LANGUAGE).toString(), QString{});
        QCOMPARE(QString::fromStdString(fixture.args.GetArg("-lang", "")), QString{});

        ArgsManager restarted_args;
        SetupServerArgs(restarted_args, false);
        restarted_args.ForceSetArg("-datadir", fixture.directory.path().toStdString());
        restarted_args.ForceSetArg("-settings", fixture.directory.filePath("settings.json").toStdString());
        restarted_args.ForceSetArg("-regtest", "1");
        restarted_args.SelectConfigNetwork("regtest");
        std::vector<std::string> errors;
        QVERIFY(restarted_args.ReadSettingsFile(&errors));
        MockNode restarted_node;
        restarted_node.get_persistent_setting_fn = [&](const std::string& name) { return restarted_args.GetPersistentSetting(name); };
        OptionsQmlModel restarted(restarted_node, restarted_args);
        QTRY_VERIFY(restarted.settingsReady());
        QCOMPARE(restarted.language(), QString{});
    }

    void signerErrorsKeepSpanishTranslations_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("expected");
        QTest::newRow("missing") << QStringLiteral("missing") << QStringLiteral("La ruta configurada del firmante no existe.");
        QTest::newRow("directory") << QStringLiteral("directory") << QStringLiteral("La ruta configurada del firmante no es un archivo.");
        QTest::newRow("not-executable") << QStringLiteral("not-executable") << QStringLiteral("La ruta configurada del firmante no es ejecutable.");
    }

    void signerErrorsKeepSpanishTranslations()
    {
        QFETCH(QString, kind);
        QFETCH(QString, expected);
        Q_INIT_RESOURCE(qml_gui_translations);
        QTranslator translator;
        QVERIFY(translator.load(QStringLiteral(":/translations/bitcoin_qml_es.qm")));
        QVERIFY(QCoreApplication::installTranslator(&translator));
        struct RemoveTranslator {
            QTranslator& translator;
            ~RemoveTranslator() { QCoreApplication::removeTranslator(&translator); }
        } remove_translator{translator};
        Fixture fixture;
        const QString path = fixture.directory.filePath(kind);
        if (kind == QStringLiteral("directory")) {
            QVERIFY(QDir().mkpath(path));
        } else if (kind == QStringLiteral("not-executable")) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.close();
            QVERIFY(file.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        }
        OptionsQmlModel model(fixture.node, fixture.args);
        QTRY_VERIFY(model.settingsReady());
        model.requestExternalSignerPathValidation(path);
        QTRY_VERIFY(!model.signerPathValidationPending());
        QCOMPARE(model.signerPathError(), expected);
        model.setExternalSignerPath(path);
        QTRY_VERIFY(!model.settingsPending());
        QCOMPARE(model.settingsError(), expected);
        QVERIFY(model.externalSignerPath().isEmpty());
    }

    void signerValidationAndDataDirectoryReturnOwnedResults()
    {
        Fixture fixture;
        OptionsQmlModel model(fixture.node, fixture.args);
        QTRY_VERIFY(model.settingsReady());
        model.setExternalSignerPath(QStringLiteral("/definitely/missing/signer"));
        QTRY_VERIFY(!model.settingsPending());
        QVERIFY(!model.settingsError().isEmpty());
        QVERIFY(model.externalSignerPath().isEmpty());
        const QString signer_path = fixture.directory.filePath(QStringLiteral("fake-signer.exe"));
        QFile signer{signer_path};
        QVERIFY(signer.open(QIODevice::WriteOnly));
        QCOMPARE(signer.write("#!/bin/sh\nexit 0\n"), qint64{17});
        signer.close();
        QVERIFY(signer.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));
        model.requestExternalSignerPathValidation(QStringLiteral("/definitely/missing/signer"));
        model.requestExternalSignerPathValidation(signer_path);
        QTRY_VERIFY(!model.signerPathValidationPending());
        QVERIFY(model.signerPathError().isEmpty());
        QSignalSpy selected(&model, &OptionsQmlModel::dataDirSelectionFinished);
        QString completed_directory;
        connect(&model, &OptionsQmlModel::dataDirSelectionFinished, this, [&](bool success, const QString&) {
            if (success) completed_directory = model.dataDir();
        });
        const QString path = fixture.directory.filePath("new-directory");
        QVERIFY(model.selectCustomDataDir(path));
        QVERIFY(model.validationPending());
        QTRY_COMPARE(selected.size(), 1);
        QVERIFY(selected.front().front().toBool());
        // Capture the original selection result at its publication boundary.
        // A later settings refresh correctly restores the effective -datadir
        // fixture override; the saved GUI choice is for a later startup.
        QCOMPARE(completed_directory, path);
        QSettings saved;
        QCOMPARE(saved.value(SettingsKeys::DATA_DIR).toString(), path);
        QVERIFY(QDir(path).exists());
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(AsyncOptionsTests)
#else
QTEST_MAIN(AsyncOptionsTests)
#endif
#include <test_async_options.moc>
