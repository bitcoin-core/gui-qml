// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/application_test_context.h>
#include <test/integration_test_registry.h>

#include <QPointer>
#include <QSignalSpy>

class NavigationIntegrationTests : public QObject
{
    Q_OBJECT
    ApplicationTestContext& m_app;

public:
    explicit NavigationIntegrationTests(ApplicationTestContext& app) : m_app(app) {}

private Q_SLOTS:
    void cleanup()
    {
        // Restore the node page even after a failed assertion.
        if (auto* settings = m_app.find("settingsView")) {
            QMetaObject::invokeMethod(settings, "doneClicked");
        }
        QTRY_VERIFY(m_app.find("nodeRunner")->property("visible").toBool());
        QTRY_VERIFY(ApplicationTestContext::transitionsFinished(m_app.window()));
    }

    void settingsPagesLoadAndReturnToRetainedNode()
    {
        QPointer<QObject> node{m_app.find("nodeRunner")};
        QVERIFY(node);
        QSignalSpy warnings{&m_app.engine, &QQmlEngine::warnings};
        QVERIFY(ApplicationTestContext::click(m_app.find("nodeSettingsButton")));
        QTRY_VERIFY(m_app.find("settingsView"));
        auto* settings = m_app.find("settingsView");
        QTRY_VERIFY(settings->property("visible").toBool());
        QTRY_VERIFY(ApplicationTestContext::transitionsFinished(m_app.window()));
        auto* container = m_app.find("settingsPageContainer");
        QVERIFY(container);
        for (const auto* section : {"display", "window-behavior", "storage", "connection", "about"}) {
            auto* button = m_app.find(qPrintable(QStringLiteral("settingsSidebar_%1").arg(section)));
            QVERIFY2(button, section);
            QVERIFY(ApplicationTestContext::click(button));
            QTRY_COMPARE(settings->property("selectedSectionId").toString(), QString::fromLatin1(section));
            QCOMPARE(container->property("currentSectionId").toString(), QString::fromLatin1(section));
            auto* page = qobject_cast<QQuickItem*>(container->property("currentItem").value<QObject*>());
            QVERIFY(page);
            QTRY_VERIFY(page->isVisible());
            // Wait for a rendered frame, so deferred binding/layout errors are
            // included in the warning assertion.
            QSignalSpy rendered{page->window(), &QQuickWindow::afterRendering};
            page->window()->update();
            QTRY_VERIFY(!rendered.isEmpty());
            QCOMPARE(warnings.count(), 0);
        }
        QVERIFY(ApplicationTestContext::click(m_app.find("settingsDoneButton")));
        QTRY_VERIFY(node->property("visible").toBool());
        QTRY_VERIFY(ApplicationTestContext::transitionsFinished(m_app.window()));
        QCOMPARE(m_app.find("nodeRunner"), node.data());
        QCOMPARE(warnings.count(), 0);
    }
};

BITCOINQML_REGISTER_INTEGRATION_TEST(NavigationIntegrationTests)
#include <test_navigation_integration.moc>
