// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/desktoptrayiconcontroller.h>

#include <QSignalSpy>
#include <QWindow>
#include <QtTest/QtTest>

class DesktopTrayIconControllerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initiallyNotVisible();
    void setVisibleFalseWhenAlreadyHidden_noSignal();
    void setVisibleTrueWithoutSystemTray_staysHidden();
    void setToolTipIsReadable();
    void showRestoresGeometryCapturedAtHide();
};

void DesktopTrayIconControllerTests::initiallyNotVisible()
{
    DesktopTrayIconController controller;
    QVERIFY(!controller.visible());
}

void DesktopTrayIconControllerTests::setVisibleFalseWhenAlreadyHidden_noSignal()
{
    DesktopTrayIconController controller;
    QSignalSpy spy(&controller, &DesktopTrayIconController::visibleChanged);
    controller.setVisible(false);
    QCOMPARE(spy.count(), 0);
}

void DesktopTrayIconControllerTests::setVisibleTrueWithoutSystemTray_staysHidden()
{
    DesktopTrayIconController controller;
    if (controller.supported()) QSKIP("Requires a platform without a system tray");
    QSignalSpy spy(&controller, &DesktopTrayIconController::visibleChanged);
    controller.setVisible(true);
    QVERIFY(!controller.visible());
    QCOMPARE(spy.count(), 0);
}

// The system-tray tooltip (e.g. "Bitcoin Core client [regtest]") is set on the
// QSystemTrayIcon and only shown by the OS on hover, which several Linux tray
// hosts (GNOME/SNI) never render. Verify the value is stored and readable so it
// can be asserted without depending on hover support.
void DesktopTrayIconControllerTests::setToolTipIsReadable()
{
    DesktopTrayIconController controller;
    const QString tip{QStringLiteral("Bitcoin Core client [regtest]")};
    controller.setToolTip(tip);
    QCOMPARE(controller.toolTip(), tip);
}

// Hiding to tray and showing again must preserve the window's size and position.
// The window manager does not reliably keep geometry across an unmap/map cycle,
// so the controller captures it on hide and re-applies it on show.
void DesktopTrayIconControllerTests::showRestoresGeometryCapturedAtHide()
{
    DesktopTrayIconController controller;
    QWindow window;
    window.setGeometry(120, 130, 480, 360);
    window.show();
    controller.setMainWindow(&window);

    const QRect captured = window.geometry();
    controller.hideMainWindow();

    // Simulate the window manager dropping size/position across the hide.
    window.setGeometry(0, 0, 200, 200);

    controller.showMainWindow();
    QTRY_COMPARE(window.geometry(), captured);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(DesktopTrayIconControllerTests)
#else
QTEST_MAIN(DesktopTrayIconControllerTests)
#endif
#include "test_desktoptrayiconcontroller.moc"
