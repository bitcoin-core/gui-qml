// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/test/qt_test_registry.h>

#include <qml/models/networkstatusmodel.h>

class NetworkStatusModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void unavailableReachabilityIsNotOffline();
    void disconnectedReachabilityIsOffline();
    void unknownReachabilityIsNotOffline();
    void limitedReachabilityIsNotOffline();
};

void NetworkStatusModelTests::unavailableReachabilityIsNotOffline()
{
    NetworkStatusModel model{/*monitor=*/false};
    QVERIFY(!model.reachabilityAvailable());
    QVERIFY(!model.networkOffline());
    QCOMPARE(model.reachability(), QStringLiteral("Unknown"));
}

void NetworkStatusModelTests::disconnectedReachabilityIsOffline()
{
    NetworkStatusModel model{/*monitor=*/false};
    QSignalSpy spy{&model, &NetworkStatusModel::reachabilityChanged};

    model.setReachability(QNetworkInformation::Reachability::Disconnected);

    QCOMPARE(spy.count(), 1);
    QVERIFY(model.reachabilityAvailable());
    QVERIFY(model.networkOffline());
    QCOMPARE(model.reachability(), QStringLiteral("Disconnected"));
}

void NetworkStatusModelTests::unknownReachabilityIsNotOffline()
{
    NetworkStatusModel model{/*monitor=*/false};

    model.setReachability(QNetworkInformation::Reachability::Unknown);
    QVERIFY(model.reachabilityAvailable());
    QVERIFY(!model.networkOffline());
    QCOMPARE(model.reachability(), QStringLiteral("Unknown"));
}

void NetworkStatusModelTests::limitedReachabilityIsNotOffline()
{
    NetworkStatusModel model{/*monitor=*/false};

    model.setReachability(QNetworkInformation::Reachability::Local);
    QVERIFY(!model.networkOffline());
    QCOMPARE(model.reachability(), QStringLiteral("Local"));

    model.setReachability(QNetworkInformation::Reachability::Site);
    QVERIFY(!model.networkOffline());
    QCOMPARE(model.reachability(), QStringLiteral("Site"));

    model.setReachability(QNetworkInformation::Reachability::Online);
    QVERIFY(!model.networkOffline());
    QCOMPARE(model.reachability(), QStringLiteral("Online"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NetworkStatusModelTests)
#else
QTEST_MAIN(NetworkStatusModelTests)
#endif
#include "test_networkstatusmodel.moc"
