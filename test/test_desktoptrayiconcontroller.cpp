// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/desktoptrayiconcontroller.h>

#include <QAction>
#include <QMenu>
#include <QPointer>
#include <QSignalSpy>
#include <QWindow>
#include <QtTest/QtTest>

#include <memory>

#if defined(Q_OS_MACOS)
#include <qml/models/macdockiconhandler.h>
#include <qt/macdockiconhandler.h>
#endif

class DesktopTrayIconControllerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initiallyNotVisible();
    void destroysMenuWithController();
    void setVisibleFalseWhenAlreadyHidden_noSignal();
    void showWithoutSystemTrayStaysHidden();
    void setToolTipIsReadable();
    void showRestoresGeometryCapturedAtHide();
#if defined(Q_OS_MACOS)
    void dockHandoffCoexistsWithCoreAndKeepsOnlyLiveReceivers();
#endif
};

void DesktopTrayIconControllerTests::initiallyNotVisible()
{
    DesktopTrayIconController controller;
    QVERIFY(!controller.visible());
}

void DesktopTrayIconControllerTests::destroysMenuWithController()
{
    QPointer<QMenu> menu;
    QPointer<QAction> action;
    {
        DesktopTrayIconController controller;
        auto* tray = controller.findChild<QSystemTrayIcon*>();
        QVERIFY(tray);
        menu = tray->contextMenu();
        QVERIFY(menu);
        QVERIFY(!menu->actions().isEmpty());
        action = menu->actions().constFirst();
    }
    QVERIFY(menu.isNull());
    QVERIFY(action.isNull());
}

void DesktopTrayIconControllerTests::setVisibleFalseWhenAlreadyHidden_noSignal()
{
    DesktopTrayIconController controller;
    QSignalSpy spy(&controller, &DesktopTrayIconController::visibleChanged);
    controller.setVisible(false);
    QCOMPARE(spy.count(), 0);
}

void DesktopTrayIconControllerTests::showWithoutSystemTrayStaysHidden()
{
    DesktopTrayIconController controller;
    if (controller.supported()) QSKIP("Requires a platform without a system tray");
    QSignalSpy visible_changed{&controller, &DesktopTrayIconController::visibleChanged};

    controller.setVisible(true);
    QVERIFY(!controller.visible());
    QCOMPARE(visible_changed.count(), 0);

    controller.setVisible(false);
    QVERIFY(!controller.visible());
    QCOMPARE(visible_changed.count(), 0);
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

#if defined(Q_OS_MACOS)
void DesktopTrayIconControllerTests::dockHandoffCoexistsWithCoreAndKeepsOnlyLiveReceivers()
{
    // Force both real meta-objects into this native executable. The app class
    // must not collide with Core's Qt implementation at link or at runtime.
    QCOMPARE(QString::fromLatin1(::MacDockIconHandler::staticMetaObject.className()), QStringLiteral("MacDockIconHandler"));
    auto* handler = appdesktop::MacDockIconHandler::instance();
    QCOMPARE(QString::fromLatin1(handler->metaObject()->className()), QStringLiteral("appdesktop::MacDockIconHandler"));
    auto first = std::make_unique<DesktopTrayIconController>();
    DesktopTrayIconController second;
    QSignalSpy first_show{first.get(), &DesktopTrayIconController::showRequested};
    QSignalSpy second_show{&second, &DesktopTrayIconController::showRequested};
    QVERIFY(QMetaObject::invokeMethod(handler, "dockIconClicked", Qt::DirectConnection));
    QCOMPARE(first_show.size(), 1);
    QCOMPARE(second_show.size(), 1);
    first.reset();
    QVERIFY(QMetaObject::invokeMethod(handler, "dockIconClicked", Qt::DirectConnection));
    QCOMPARE(first_show.size(), 1);
    QCOMPARE(second_show.size(), 2);
}
#endif

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(DesktopTrayIconControllerTests)
#else
QTEST_MAIN(DesktopTrayIconControllerTests)
#endif
#include <test_desktoptrayiconcontroller.moc>
