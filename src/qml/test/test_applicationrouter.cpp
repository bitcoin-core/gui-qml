// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applicationrouter.h>

#include <QAbstractItemModelTester>
#include <QtTest/QtTest>

class ApplicationRouterTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void registrationExposesOnlyAvailableDestinations()
    {
        ApplicationRouter router;
        NavigationModel menu(router);
        QAbstractItemModelTester tester(&menu, QAbstractItemModelTester::FailureReportingMode::QtTest);
        QCOMPARE(menu.rowCount(), 0);
        QVERIFY(!router.navigate("wallet"));
        QVERIFY(router.registerDestination({"node", QUrl("qrc:/node.qml"), "Node"}));
        QVERIFY(!router.registerDestination({"node", QUrl("qrc:/other.qml"), "Duplicate"}));
        QVERIFY(router.registerDestination({"peer-details", QUrl("qrc:/peer.qml"), "Peer", false, "node"}));
        QCOMPARE(menu.rowCount(), 1);
        QVERIFY(router.navigate("node"));
        QCOMPARE(menu.data(menu.index(0), NavigationModel::SelectedRole).toBool(), true);
        router.setDestinationEnabled("node", false);
        QCOMPARE(menu.rowCount(), 0);
        QVERIFY(!router.navigate("node"));
        QVERIFY(router.currentRoute().isEmpty());
    }

    void historyAndSelectionHaveOneOwner()
    {
        ApplicationRouter router;
        NavigationModel menu(router);
        router.registerDestination({"node", QUrl("qrc:/node.qml"), "Node"});
        router.registerDestination({"peers", QUrl("qrc:/peers.qml"), "Peers"});
        router.registerDestination({"peer-details", QUrl("qrc:/peer.qml"), "Peer", false, "peers"});
        QVERIFY(router.navigate("node"));
        QVERIFY(!router.canGoBack());
        QVERIFY(router.navigate("node"));
        QVERIFY(!router.canGoBack());
        QVERIFY(router.navigate("peers"));
        const QVariantMap parameters{{"peer", 42}};
        QVERIFY(router.navigate("peer-details", parameters));
        QCOMPARE(router.currentParameters(), parameters);
        QCOMPARE(router.currentSelection(), QString("peers"));
        QCOMPARE(menu.data(menu.index(1), NavigationModel::SelectedRole).toBool(), true);
        router.back();
        QCOMPARE(router.currentRoute(), QString("peers"));
        QVERIFY(router.currentParameters().isEmpty());
        router.back();
        QCOMPARE(router.currentRoute(), QString("node"));
        QVERIFY(!router.canGoBack());
    }

    void shutdownIsTerminalAndClearsParameters()
    {
        ApplicationRouter router;
        NavigationModel menu(router);
        router.registerDestination({"node", QUrl("qrc:/node.qml"), "Node"});
        router.registerDestination({"shutdown", QUrl("qrc:/shutdown.qml"), "Shutdown", false});
        router.navigate("node", {{"private", "value"}});
        QVERIFY(!router.navigate("shutdown"));
        router.beginShutdown();
        QCOMPARE(router.currentRoute(), QString("shutdown"));
        QVERIFY(router.currentParameters().isEmpty());
        QVERIFY(!router.canGoBack());
        QVERIFY(!router.navigate("node"));
        QCOMPARE(menu.data(menu.index(0), NavigationModel::EnabledRole).toBool(), false);
        router.back();
        QCOMPARE(router.currentRoute(), QString("shutdown"));
    }

    void retranslationDoesNotReloadThePage()
    {
        ApplicationRouter router;
        router.registerDestination({"node", QUrl("qrc:/node.qml"), "Node"});
        router.navigate("node");
        QSignalSpy route_changed(&router, &ApplicationRouter::routeChanged);
        QSignalSpy label_changed(&router, &ApplicationRouter::currentChanged);
        router.retranslate();
        QCOMPARE(route_changed.count(), 0);
        QCOMPARE(label_changed.count(), 1);
        QCOMPARE(router.currentRoute(), QString("node"));
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <qml/test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(ApplicationRouterTests)
#else
QTEST_MAIN(ApplicationRouterTests)
#endif
#include "test_applicationrouter.moc"
