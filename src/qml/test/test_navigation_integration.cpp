// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/applicationrouter.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/test/integration_test_registry.h>

#include <QQmlApplicationEngine>
#include <QSignalSpy>
#include <QTest>

class NavigationIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit NavigationIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}

private Q_SLOTS:
    void registeredPagesLoad()
    {
        auto& router = m_app.router();
        auto& engine = m_app.engine();
        QSignalSpy warnings(&engine, &QQmlEngine::warnings);
        const auto destinations = router.destinations();
        for (const auto& destination : destinations) {
            if (destination.id == "shutdown" || destination.id == "peer-details") continue;
            QVERIFY2(router.navigate(destination.id), qPrintable(destination.id));
            QObject* host = engine.rootObjects().constFirst()->findChild<QObject*>("applicationPageHost");
            QVERIFY(host);
            QTRY_VERIFY_WITH_TIMEOUT(host->property("item").value<QObject*>(), 5'000);
            QCOMPARE(host->property("source").toUrl(), destination.source);
            QCOMPARE(warnings.count(), 0);
        }
        QVERIFY(!router.navigate("wallet/send"));
        QVERIFY(router.navigate("node"));
    }
};

BITCOINQML_REGISTER_INTEGRATION_TEST(NavigationIntegrationTests)
#include <test_navigation_integration.moc>
