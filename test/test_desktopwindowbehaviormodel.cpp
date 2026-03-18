// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/models/desktopwindowbehaviormodel.h>

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
    void setShowTrayIconFalse_cascadesToMinimizeOnCloseFalse();
    void setMinimizeToTray_noopWhenTrayIconDisabled();

    // Composite helpers
    void shouldHideToTrayOnMinimize_allConditionsRequired();
    void shouldMinimizeWindowOnClose_requiresDesktopAndMinimizeOnClose();

    // QSettings persistence
    void settings_persistAcrossInstances();

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
    clearTestSettings();
}

void DesktopWindowBehaviorModelTests::nonDesktopPlatform_allSettingsFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/false);
    QCOMPARE(model.desktopPlatform(), false);
    QCOMPARE(model.showTrayIcon(), false);
    QCOMPARE(model.minimizeToTray(), false);
    QCOMPARE(model.minimizeOnClose(), false);
}

void DesktopWindowBehaviorModelTests::desktopPlatform_showTrayIconDefaultsTrue()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QCOMPARE(model.desktopPlatform(), true);
    QCOMPARE(model.showTrayIcon(), true);
}

void DesktopWindowBehaviorModelTests::desktopPlatform_minimizeToTrayDefaultsFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QCOMPARE(model.minimizeToTray(), false);
}

void DesktopWindowBehaviorModelTests::desktopPlatform_minimizeOnCloseDefaultsFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    QCOMPARE(model.minimizeOnClose(), false);
}

void DesktopWindowBehaviorModelTests::setShowTrayIconFalse_cascadesToMinimizeToTrayFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);

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

void DesktopWindowBehaviorModelTests::setShowTrayIconFalse_cascadesToMinimizeOnCloseFalse()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);

    // Enable minimize-on-close first (requires showTrayIcon=true default)
    model.setMinimizeOnClose(true);
    QCOMPARE(model.minimizeOnClose(), true);

    // Disable the tray icon — minimizeOnClose must cascade to false
    QSignalSpy spy(&model, &DesktopWindowBehaviorModel::minimizeOnCloseChanged);
    model.setShowTrayIcon(false);
    QCOMPARE(model.showTrayIcon(), false);
    QCOMPARE(model.minimizeOnClose(), false);
    QVERIFY(spy.count() >= 1);
}

void DesktopWindowBehaviorModelTests::setMinimizeToTray_noopWhenTrayIconDisabled()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
    model.setShowTrayIcon(false);
    QCOMPARE(model.showTrayIcon(), false);

    // Trying to enable minimizeToTray without a tray icon must be a no-op
    model.setMinimizeToTray(true);
    QCOMPARE(model.minimizeToTray(), false);
}

void DesktopWindowBehaviorModelTests::shouldHideToTrayOnMinimize_allConditionsRequired()
{
    DesktopWindowBehaviorModel model(/*desktop_platform=*/true);

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
        QCOMPARE(model.shouldMinimizeWindowOnClose(), false);
    }

    // Desktop, minimizeOnClose=false (default)
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QCOMPARE(model.shouldMinimizeWindowOnClose(), false);

        model.setMinimizeOnClose(true);
        QCOMPARE(model.shouldMinimizeWindowOnClose(), true);
    }
}

void DesktopWindowBehaviorModelTests::settings_persistAcrossInstances()
{
    // minimizeOnClose persists when tray icon stays enabled
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        model.setMinimizeOnClose(true);
    }
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QCOMPARE(model.minimizeOnClose(), true);
        QCOMPARE(model.showTrayIcon(), true);
    }

    // Disabling tray icon cascades minimizeOnClose=false and persists that
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QCOMPARE(model.minimizeOnClose(), true);
        model.setShowTrayIcon(false); // cascade: minimizeOnClose → false
    }
    {
        DesktopWindowBehaviorModel model(/*desktop_platform=*/true);
        QCOMPARE(model.minimizeOnClose(), false);
        QCOMPARE(model.showTrayIcon(), false);
    }
}

int RunDesktopWindowBehaviorModelTests(int argc, char* argv[])
{
    DesktopWindowBehaviorModelTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#ifndef BITCOINQML_NO_TEST_MAIN
QTEST_MAIN(DesktopWindowBehaviorModelTests)
#endif
#include "test_desktopwindowbehaviormodel.moc"
