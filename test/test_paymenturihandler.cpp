// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/paymenturihandler.h>

class PaymentUriHandlerTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void requestsAreHandedOutInArrivalOrder();
    void emptyRequestsAreIgnored();
    void notifiesOnlyWhenTheFrontChanges();
};

void PaymentUriHandlerTests::requestsAreHandedOutInArrivalOrder()
{
    PaymentUriHandler handler;
    QVERIFY(!handler.hasPendingRequest());
    QVERIFY(handler.pendingRequest().isEmpty());

    handler.queueRequests({QStringLiteral("bitcoin:first"), QStringLiteral("bitcoin:second")});
    QVERIFY(handler.hasPendingRequest());
    QCOMPARE(handler.pendingRequest(), QStringLiteral("bitcoin:first"));

    // Reading does not consume: a UI that cannot take the request yet leaves it.
    QCOMPARE(handler.pendingRequest(), QStringLiteral("bitcoin:first"));

    handler.completePendingRequest();
    QCOMPARE(handler.pendingRequest(), QStringLiteral("bitcoin:second"));

    handler.completePendingRequest();
    QVERIFY(!handler.hasPendingRequest());

    handler.completePendingRequest();
    QVERIFY(!handler.hasPendingRequest());
}

void PaymentUriHandlerTests::emptyRequestsAreIgnored()
{
    PaymentUriHandler handler;
    handler.queueRequest(QString());
    handler.queueRequest(QStringLiteral(""));
    QVERIFY(!handler.hasPendingRequest());
}

void PaymentUriHandlerTests::notifiesOnlyWhenTheFrontChanges()
{
    PaymentUriHandler handler;
    QSignalSpy spy(&handler, &PaymentUriHandler::pendingRequestChanged);

    handler.queueRequest(QStringLiteral("bitcoin:first"));
    QCOMPARE(spy.count(), 1);

    handler.queueRequest(QStringLiteral("bitcoin:second"));
    QCOMPARE(spy.count(), 1);

    handler.completePendingRequest();
    QCOMPARE(spy.count(), 2);

    handler.completePendingRequest();
    QCOMPARE(spy.count(), 3);

    handler.completePendingRequest();
    QCOMPARE(spy.count(), 3);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PaymentUriHandlerTests)
#else
QTEST_MAIN(PaymentUriHandlerTests)
#endif
#include "test_paymenturihandler.moc"
