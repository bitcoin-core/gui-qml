// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/desktopwindowbehaviormodel.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <stdexcept>
#include <QScopeGuard>

namespace {
struct ManualGuiDispatcher {
    std::deque<std::function<void()>> pending;
    void post(std::function<void()> work) { pending.push_back(std::move(work)); }
    void drain() { while (!pending.empty()) { auto work = std::move(pending.front()); pending.pop_front(); work(); } }
};
}

class DesktopWindowBehaviorModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void init();
    void cleanup();

    // Platform guard
    void nonDesktopPlatform_allSettingsFalse();

    // Defaults on desktop
    void desktopPlatform_showTrayIconDefaultsTrue();
    void desktopPlatform_minimizeToTrayDefaultsFalse();
    void desktopPlatform_minimizeOnCloseDefaultsFalse();

    // Cascade logic
    void setShowTrayIconFalse_cascadesToMinimizeToTrayFalse();
    void setShowTrayIconFalse_doesNotCascadeMinimizeOnClose();
    void setMinimizeToTray_noopWhenTrayIconDisabled();
    void setMinimizeOnClose_worksWithoutTrayIcon();

    // Composite helpers
    void shouldHideToTrayOnMinimize_allConditionsRequired();
    void shouldMinimizeWindowOnClose_requiresDesktopAndMinimizeOnClose();

    // QSettings persistence
    void loadSettings_clampsMinimizeToTrayWhenTrayHidden();
    void settings_persistAcrossInstances();
    void manualRetryQueriesWithoutReplayingAWrite_data();
    void manualRetryQueriesWithoutReplayingAWrite();
    void externalWriterRefreshesExistingModel();
    void manualRefreshWaitsForWriteAndCoalesces();
    void heldNativeWorkKeepsOriginalPreferenceNamespace();
    void nativeWritePreservesUnrelatedExternalPreference();
    void manualPreferencesOwnOutcomesAfterClose();
    void manualPreferencesRejectAndRetryLoad();
    void manualPreferencesReportWriteFailure();
    void manualLostPublicationRecoversActualLoadAndWriteWithoutReplay();

private:
    // Use a unique org/app name so tests don't pollute the user's real settings.
    void setTestAppName();
    void clearTestSettings();
};

void DesktopWindowBehaviorModelTests::setTestAppName()
{
    QCoreApplication::setOrganizationName("BitcoinCoreAppTest");
    QCoreApplication::setApplicationName("DesktopWindowBehaviorModelTests");
}

void DesktopWindowBehaviorModelTests::clearTestSettings()
{
    QSettings settings;
    settings.remove("fHideTrayIcon");
    settings.remove("fMinimizeToTray");
    settings.remove("fMinimizeOnClose");
    settings.sync();
}

void DesktopWindowBehaviorModelTests::init()
{
    setTestAppName();
    clearTestSettings();
}

void DesktopWindowBehaviorModelTests::cleanup()
{
    bool drained{false};
    BackendExecutor::shutdownAll(this, [&] { drained = true; });
    QTRY_VERIFY(drained);
    clearTestSettings();
}

void DesktopWindowBehaviorModelTests::nonDesktopPlatform_allSettingsFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/false);
    QTRY_VERIFY(model.settingsReady());
    QCOMPARE(model.desktopPlatform(), false);
    QCOMPARE(model.showTrayIcon(), false);
    QCOMPARE(model.minimizeToTray(), false);
    QCOMPARE(model.minimizeOnClose(), false);
}

void DesktopWindowBehaviorModelTests::desktopPlatform_showTrayIconDefaultsTrue()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());
    QCOMPARE(model.desktopPlatform(), true);
    QCOMPARE(model.showTrayIcon(), true);
}

void DesktopWindowBehaviorModelTests::desktopPlatform_minimizeToTrayDefaultsFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());
    QCOMPARE(model.minimizeToTray(), false);
}

void DesktopWindowBehaviorModelTests::desktopPlatform_minimizeOnCloseDefaultsFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());
    QCOMPARE(model.minimizeOnClose(), false);
}

void DesktopWindowBehaviorModelTests::setShowTrayIconFalse_cascadesToMinimizeToTrayFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());

    // Enable minimize-to-tray first
    model.setMinimizeToTray(true);
    QCOMPARE(model.minimizeToTray(), true);

    // Now disable the tray icon — minimizeToTray must cascade to false
    QSignalSpy spyMinimize(&model, &DesktopWindowBehaviorModel::minimizeToTrayChanged);
    model.setShowTrayIcon(false);
    QCOMPARE(model.showTrayIcon(), false);
    QCOMPARE(model.minimizeToTray(), false);
    QVERIFY(spyMinimize.count() >= 1);
}

void DesktopWindowBehaviorModelTests::setShowTrayIconFalse_doesNotCascadeMinimizeOnClose()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());

    model.setMinimizeOnClose(true);
    QCOMPARE(model.minimizeOnClose(), true);

    // Disabling tray icon should NOT affect minimizeOnClose — it works
    // independently (minimizes to taskbar, not tray).
    QSignalSpy spy(&model, &DesktopWindowBehaviorModel::minimizeOnCloseChanged);
    model.setShowTrayIcon(false);
    QCOMPARE(model.showTrayIcon(), false);
    QCOMPARE(model.minimizeOnClose(), true);
    QCOMPARE(spy.count(), 0);
}

void DesktopWindowBehaviorModelTests::setMinimizeToTray_noopWhenTrayIconDisabled()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());
    model.setShowTrayIcon(false);
    QCOMPARE(model.showTrayIcon(), false);

    // Trying to enable minimizeToTray without a tray icon must be a no-op
    model.setMinimizeToTray(true);
    QCOMPARE(model.minimizeToTray(), false);
}

void DesktopWindowBehaviorModelTests::setMinimizeOnClose_worksWithoutTrayIcon()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());
    model.setShowTrayIcon(false);
    QCOMPARE(model.showTrayIcon(), false);

    // minimizeOnClose works without a tray icon — window minimizes to taskbar
    model.setMinimizeOnClose(true);
    QCOMPARE(model.minimizeOnClose(), true);
    QCOMPARE(model.shouldMinimizeWindowOnClose(), true);
}

void DesktopWindowBehaviorModelTests::shouldHideToTrayOnMinimize_allConditionsRequired()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());

    // desktopPlatform=true, showTrayIcon=true (default), minimizeToTray=false → false
    QCOMPARE(model.shouldHideToTrayOnMinimize(), false);

    // Enable all conditions
    model.setMinimizeToTray(true);
    QCOMPARE(model.shouldHideToTrayOnMinimize(), true);

    // Disable tray icon → false again (cascade also disables minimizeToTray)
    model.setShowTrayIcon(false);
    QCOMPARE(model.shouldHideToTrayOnMinimize(), false);
}

void DesktopWindowBehaviorModelTests::shouldMinimizeWindowOnClose_requiresDesktopAndMinimizeOnClose()
{
    // Non-desktop: always false regardless of minimizeOnClose value
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/false);
        QTRY_VERIFY(model.settingsReady());
        QCOMPARE(model.shouldMinimizeWindowOnClose(), false);
    }

    // Desktop, minimizeOnClose=false (default)
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QTRY_VERIFY(model.settingsReady());
        QCOMPARE(model.shouldMinimizeWindowOnClose(), false);

        model.setMinimizeOnClose(true);
        QCOMPARE(model.shouldMinimizeWindowOnClose(), true);
    }
}

void DesktopWindowBehaviorModelTests::loadSettings_clampsMinimizeToTrayWhenTrayHidden()
{
    // A stale or hand-edited QSettings can hold the otherwise unreachable
    // combination tray-hidden + minimize-to-tray. Loading must clamp
    // minimizeToTray to false and rewrite the persisted value.
    QSettings settings;
    settings.setValue("fHideTrayIcon", true);
    settings.setValue("fMinimizeToTray", true);
    settings.sync();

    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QTRY_VERIFY(model.settingsReady());

    QCOMPARE(model.showTrayIcon(), false);
    QCOMPARE(model.minimizeToTray(), false);
    QCOMPARE(model.shouldHideToTrayOnMinimize(), false);

    QSettings updated_settings;
    QCOMPARE(updated_settings.value("fMinimizeToTray", true).toBool(), false);
}

void DesktopWindowBehaviorModelTests::settings_persistAcrossInstances()
{
    // minimizeOnClose persists when tray icon stays enabled
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QTRY_VERIFY(model.settingsReady());
        model.setMinimizeOnClose(true);
        QTRY_VERIFY(!model.persistencePending());
    }
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QTRY_VERIFY(model.settingsReady());
        QCOMPARE(model.minimizeOnClose(), true);
        QCOMPARE(model.showTrayIcon(), true);
    }

    // Disabling tray icon does NOT cascade to minimizeOnClose
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QTRY_VERIFY(model.settingsReady());
        QCOMPARE(model.minimizeOnClose(), true);
        model.setShowTrayIcon(false);
        QTRY_VERIFY(!model.persistencePending());
    }
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QTRY_VERIFY(model.settingsReady());
        QCOMPARE(model.minimizeOnClose(), true);
        QCOMPARE(model.showTrayIcon(), false);
    }
}

void DesktopWindowBehaviorModelTests::manualPreferencesOwnOutcomesAfterClose()
{
    using Model = DesktopWindowBehaviorModel;
    ManualRoutedExecution env;
    ManualGuiDispatcher gui;
    Model::Values persisted{true, true, false};
    int reads{0}, writes{0};
    auto model = std::make_unique<Model>(true, execution::SnapshotServices{env.scheduler, [&](auto work) { gui.post(std::move(work)); }},
        Model::Store{[&] { ++reads; return persisted; }, [&](auto values) { ++writes; persisted = values; }});
    QVERIFY(!model->settingsReady()); QCOMPARE(reads, 0);
    model->setShowTrayIcon(false); // No write may overwrite the unknown profile.
    env.drain(); QCOMPARE(reads, 1); QCOMPARE(writes, 0);
    QVERIFY(!model->settingsReady());
    gui.drain(); QVERIFY(model->settingsReady()); QVERIFY(model->minimizeToTray());
    model->setShowTrayIcon(false);
    QVERIFY(!model->showTrayIcon()); QVERIFY(!model->minimizeToTray());
    QCOMPARE(writes, 0); QVERIFY(model->persistencePending());
    model->close(); model.reset();
    env.scheduler->close();
    env.drain(); QCOMPARE(writes, 1);
    QVERIFY(!persisted.show_tray); QVERIFY(!persisted.minimize_tray);
    gui.drain(); QVERIFY(env.quiescent());
}

void DesktopWindowBehaviorModelTests::manualPreferencesRejectAndRetryLoad()
{
    using Model = DesktopWindowBehaviorModel;
    ManualRoutedExecution env; ManualGuiDispatcher gui;
    env.rejectLane(execution::Lane::Core);
    int reads{0};
    Model model{true, {env.scheduler, [&](auto work) { gui.post(std::move(work)); }},
        {[&] { ++reads; return Model::Values{false, false, true}; }, [](auto) {}}};
    QCOMPARE(reads, 0); QVERIFY(!model.settingsReady());
    QVERIFY(!model.persistencePending()); QVERIFY(!model.persistenceError().isEmpty());
    model.retryPersistence(); env.drain();
    QVERIFY(!model.settingsReady()); gui.drain();
    QCOMPARE(reads, 1); QVERIFY(model.settingsReady());
    QVERIFY(!model.showTrayIcon()); QVERIFY(model.minimizeOnClose());
    QVERIFY(model.persistenceError().isEmpty());
}

void DesktopWindowBehaviorModelTests::manualLostPublicationRecoversActualLoadAndWriteWithoutReplay()
{
    using Model = DesktopWindowBehaviorModel;
    ManualRoutedExecution env; int reads{0}, writes{0};
    Model::Values persisted{true, false, false};
    Model model{true, {env.scheduler, [](auto) { throw std::runtime_error("post failed"); }},
        Model::Store{[&] { ++reads; return persisted; }, [&](auto value) { ++writes; persisted = value; }}};
    env.drain(); QVERIFY(!model.settingsReady()); QVERIFY(model.persistencePending()); QCOMPARE(reads, 1);
    model.retryPersistence(); QVERIFY(model.settingsReady()); QVERIFY(!model.persistencePending()); QCOMPARE(reads, 1); QCOMPARE(writes, 0);
    model.setMinimizeOnClose(true); env.drain(); QCOMPARE(writes, 1); QVERIFY(model.persistencePending());
    model.retryPersistence(); QVERIFY(!model.persistencePending()); QCOMPARE(writes, 1); QVERIFY(model.minimizeOnClose());
    model.close(); env.drain();
}

void DesktopWindowBehaviorModelTests::manualPreferencesReportWriteFailure()
{
    using Model = DesktopWindowBehaviorModel;
    ManualRoutedExecution env; ManualGuiDispatcher gui;
    int writes{0};
    Model model{true, {env.scheduler, [&](auto work) { gui.post(std::move(work)); }},
        {[] { return Model::Values{}; }, [&](auto) { ++writes; throw std::runtime_error{"disk full"}; }}};
    env.drain(); gui.drain(); QVERIFY(model.settingsReady());
    model.setMinimizeOnClose(true); env.drain();
    QVERIFY(model.persistencePending()); QVERIFY(model.persistenceError().isEmpty());
    gui.drain(); QCOMPARE(writes, 1); QVERIFY(!model.persistencePending());
    QVERIFY(!model.persistenceError().isEmpty()); QVERIFY(model.minimizeOnClose());
}



void DesktopWindowBehaviorModelTests::manualRetryQueriesWithoutReplayingAWrite_data()
{
    QTest::addColumn<bool>("saved");QTest::newRow("failure-before-write")<<false;QTest::newRow("exception-after-write")<<true;
}
void DesktopWindowBehaviorModelTests::manualRetryQueriesWithoutReplayingAWrite()
{
    QFETCH(bool,saved);using Model=DesktopWindowBehaviorModel;ManualRoutedExecution env;ManualGuiDispatcher gui;int reads{0},writes{0};Model::Values persisted;
    Model model{true,{env.scheduler,[&](auto turn){gui.post(std::move(turn));}},
        {[&]{++reads;return persisted;},[&](auto value){++writes;if(saved)persisted=value;throw std::runtime_error("write completion unavailable");}}};
    env.drain();gui.drain();model.setMinimizeOnClose(true);env.drain();gui.drain();
    QCOMPARE(writes,1);QVERIFY(!model.persistenceError().isEmpty());
    model.retryPersistence();env.drain();gui.drain();
    QCOMPARE(writes,1);QCOMPARE(reads,2);QCOMPARE(model.minimizeOnClose(),saved);QVERIFY(!model.persistencePending());
    model.close();env.drain();gui.drain();
}

void DesktopWindowBehaviorModelTests::externalWriterRefreshesExistingModel()
{
    DesktopWindowBehaviorModel model{true};QTRY_VERIFY(model.settingsReady());QTRY_VERIFY(!model.persistencePending());
    QSignalSpy changes{&model,&DesktopWindowBehaviorModel::settingsStateChanged};
    {QSettings external;external.setValue("fHideTrayIcon",true);external.setValue("fMinimizeOnClose",true);external.sync();}
    QTRY_VERIFY(!model.showTrayIcon());QTRY_VERIFY(model.minimizeOnClose());QTRY_VERIFY(!model.persistencePending());QVERIFY(!changes.empty());
    {QSettings external;external.remove("fHideTrayIcon");external.remove("fMinimizeOnClose");external.sync();}
    QTRY_VERIFY(model.showTrayIcon());QTRY_VERIFY(!model.minimizeOnClose());
}
void DesktopWindowBehaviorModelTests::manualRefreshWaitsForWriteAndCoalesces()
{
    using Model=DesktopWindowBehaviorModel;ManualRoutedExecution env;ManualGuiDispatcher gui;int reads{0},writes{0};Model::Values persisted;
    Model model{true,{env.scheduler,[&](auto turn){gui.post(std::move(turn));}},
        {[&]{++reads;return persisted;},[&](auto value){++writes;persisted=value;}}};
    env.drain();gui.drain();QVERIFY(model.settingsReady());QCOMPARE(reads,1);
    model.setMinimizeOnClose(true);env.drain();QCOMPARE(writes,1);QVERIFY(model.persistencePending());
    persisted.show_tray=false;model.refreshSettings();model.refreshSettings();QCOMPARE(reads,1);
    gui.drain();env.drain();gui.drain();QCOMPARE(reads,2);QCOMPARE(writes,1);QVERIFY(!model.showTrayIcon());QVERIFY(model.minimizeOnClose());QVERIFY(!model.persistencePending());
    model.close();model.refreshSettings();env.drain();gui.drain();QCOMPARE(reads,2);
}
void DesktopWindowBehaviorModelTests::nativeWritePreservesUnrelatedExternalPreference()
{
    ManualRoutedExecution env;ManualGuiDispatcher gui;
    DesktopWindowBehaviorModel model{true,{env.scheduler,[&](auto turn){gui.post(std::move(turn));}}};
    env.drain();gui.drain();QVERIFY(!model.minimizeOnClose());
    // The external write happens after the GUI snapshot and before the app's
    // independent tray intent. This is a real QSettings stale-base schedule.
    {QSettings external;external.setValue("fMinimizeOnClose",true);external.sync();}
    model.setShowTrayIcon(false);env.drain();gui.drain();
    QSettings actual;actual.sync();QVERIFY(actual.value("fMinimizeOnClose").toBool());QVERIFY(actual.value("fHideTrayIcon").toBool());
    QVERIFY(model.minimizeOnClose());QVERIFY(!model.showTrayIcon());
    model.close();env.drain();gui.drain();
}

void DesktopWindowBehaviorModelTests::heldNativeWorkKeepsOriginalPreferenceNamespace()
{
    const auto original=QCoreApplication::applicationName();
    const auto restore=qScopeGuard([&]{QCoreApplication::setApplicationName(original);});
    QCoreApplication::setApplicationName(QStringLiteral("R35DesktopOriginal"));
    QSettings first;first.clear();first.setValue("fHideTrayIcon",true);first.sync();
    QCoreApplication::setApplicationName(QStringLiteral("R35DesktopSuccessor"));
    QSettings second;second.clear();second.setValue("fMinimizeOnClose",false);second.sync();
    const auto cleanup_stores=qScopeGuard([&]{first.clear();first.sync();second.clear();second.sync();});
    QCoreApplication::setApplicationName(QStringLiteral("R35DesktopOriginal"));
    ManualRoutedExecution transport;ManualGuiDispatcher gui;
    DesktopWindowBehaviorModel model{true,{transport.scheduler,[&](auto turn){gui.post(std::move(turn));}},{}};
    QCoreApplication::setApplicationName(QStringLiteral("R35DesktopSuccessor"));
    transport.drain();gui.drain();QVERIFY(model.settingsReady());QVERIFY(!model.showTrayIcon());
    model.setMinimizeOnClose(true);transport.drain();gui.drain();QVERIFY(model.minimizeOnClose());
    first.sync();second.sync();QVERIFY(first.value("fMinimizeOnClose").toBool());QVERIFY(!second.value("fMinimizeOnClose").toBool());
    model.close();transport.drain();gui.drain();QVERIFY(transport.quiescent());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(DesktopWindowBehaviorModelTests)
#else
QTEST_MAIN(DesktopWindowBehaviorModelTests)
#endif
#include <test_desktopwindowbehaviormodel.moc>
