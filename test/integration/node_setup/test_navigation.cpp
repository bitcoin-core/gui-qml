// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_util.h>

#include <QMetaEnum>
#include <QQmlComponent>
#include <QPointer>
#include <QSignalSpy>

#include <utility>

class NavigationIntegrationTests : public QObject
{
    Q_OBJECT
    BitcoinQmlApplication& m_app;

public:
    explicit NavigationIntegrationTests(BitcoinQmlApplication& app) : m_app(app) {}

private Q_SLOTS:
    void cleanup()
    {
        QVERIFY(qmltest::ReturnToNode(m_app));
        auto* clock = qmltest::Find(m_app, "blockClock");
        QVERIFY(clock);
        QVERIFY(QTest::qWaitFor([&] {
            return clock->property("visible").toBool() && qmltest::TransitionsFinished(m_app.window());
        }));
    }

    void canceledPressIsNotReportedAsAClick()
    {
        QQmlComponent component{&m_app.engine()};
        component.setData(R"(
            import QtQuick 2.15
            import QtQuick.Controls 2.15
            Button {
                width: 120; height: 40
                objectName: "cancelingButton"
                onPressed: enabled = false
            }
        )", QUrl{});
        std::unique_ptr<QObject> object{component.create()};
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* button = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(button);
        button->setParentItem(m_app.window()->contentItem());
        QSignalSpy pressed{button, SIGNAL(pressed())};
        QSignalSpy clicked{button, SIGNAL(clicked())};
        QVERIFY(!qmltest::Click(button));
        QCOMPARE(pressed.count(), 1);
        QCOMPARE(clicked.count(), 0);
    }

    void clickRescrollsAfterDeferredLayoutChanges()
    {
        QQmlComponent component{&m_app.engine()};
        component.setData(R"(
            import QtQuick 2.15
            import QtQuick.Controls 2.15
            Flickable {
                id: viewport
                width: 200; height: 150; clip: true
                contentWidth: width; contentHeight: 600
                property bool adjusted: false
                onContentYChanged: {
                    if (adjusted) return
                    adjusted = true
                    Qt.callLater(function() {
                        target.y += 150
                        contentHeight += 150
                        target.enabled = true
                    })
                }
                Button {
                    id: target
                    objectName: "deferredLayoutButton"
                    y: 500; width: 120; height: 40
                    enabled: false
                }
            }
        )", QUrl{});
        std::unique_ptr<QObject> object{component.create()};
        QVERIFY2(object, qPrintable(component.errorString()));
        auto* viewport = qobject_cast<QQuickItem*>(object.get());
        QVERIFY(viewport);
        viewport->setParentItem(m_app.window()->contentItem());
        auto* button = qmltest::Find(viewport, "deferredLayoutButton");
        QVERIFY(button);
        QSignalSpy clicked{button, SIGNAL(clicked())};
        QVERIFY(qmltest::Click(m_app, "deferredLayoutButton"));
        QCOMPARE(clicked.count(), 1);
    }

    void secretTextSweepIncludesVisualAndObjectChildren()
    {
        QObject owner;
        QQuickItem visual;
        visual.setParentItem(m_app.window()->contentItem());
        visual.setObjectName("visualSecretProbe");
        visual.setProperty("message", "prefix sensitive token suffix");
        QObject child{&owner};
        child.setObjectName("objectSecretProbe");
        child.setProperty("errorText", "sensitive token");
        visual.setParent(&owner);
        QCOMPARE(qmltest::SecretTextLocations(m_app.window(), "sensitive token"),
                 QStringList{"visualSecretProbe.message"});
        const auto owner_leaks = qmltest::SecretTextLocations(&owner, "sensitive token");
        QCOMPARE(owner_leaks.size(), 2);
        QVERIFY(owner_leaks.contains("visualSecretProbe.message"));
        QVERIFY(owner_leaks.contains("objectSecretProbe.errorText"));
        visual.setProperty("message", QString{});
        QVERIFY(qmltest::SecretTextLocations(m_app.window(), "sensitive token").isEmpty());
    }

    void settingsPagesLoadAndReturnToRetainedBlockClock()
    {
        QPointer<QObject> clock{qmltest::Find(m_app, "blockClock")};
        QVERIFY(clock);
        QSignalSpy warnings{&m_app.engine(), &QQmlEngine::warnings};
        QVERIFY(qmltest::OpenSettings(m_app));
        QTRY_VERIFY(qmltest::Find(m_app, "settingsView"));
        auto* settings = qmltest::Find(m_app, "settingsView");
        QTRY_VERIFY(settings->property("visible").toBool());
        QTRY_VERIFY(qmltest::TransitionsFinished(m_app.window()));
        auto* container = qmltest::Find(m_app, "settingsPageContainer");
        QVERIFY(container);
        auto* sidebar = qobject_cast<QQuickItem*>(qmltest::Find(m_app, "settingsSidebarList"));
        QVERIFY(sidebar);
        const auto position_mode = sidebar->metaObject()->enumerator(sidebar->metaObject()->indexOfEnumerator("PositionMode"));
        const int contain = position_mode.keyToValue("Contain");
        QVERIFY(contain >= 0);
        for (const auto& [section, page_name] : {
                 std::pair{"display", "displaySettingsPage"},
                 std::pair{"window-behavior", "windowBehaviorSettingsPage"},
                 std::pair{"storage", "storageSettingsPage"},
                 std::pair{"connection", "connectionSettingsPage"},
                 std::pair{"about", "aboutSettingsPage"}}) {
            auto* button = qobject_cast<QQuickItem*>(qmltest::Find(m_app, qPrintable(QStringLiteral("settingsSidebar_%1").arg(section))));
            QVERIFY2(button, section);
            const int index = button->parentItem()->property("index").toInt();
            QVERIFY(QMetaObject::invokeMethod(sidebar, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, contain)));
            QTRY_VERIFY(sidebar->contains(button->mapToItem(sidebar, button->boundingRect().center())));
            QVERIFY(qmltest::Click(button));
            QTRY_COMPARE(settings->property("selectedSectionId").toString(), QString::fromLatin1(section));
            QCOMPARE(container->property("currentSectionId").toString(), QString::fromLatin1(section));
            auto* page = qobject_cast<QQuickItem*>(container->property("currentItem").value<QObject*>());
            QVERIFY(page);
            QCOMPARE(page->objectName(), QString::fromLatin1(page_name));
            QTRY_VERIFY(page->isVisible());
            QTRY_VERIFY(page->width() > 0 && page->height() > 0);
            QVERIFY(page->mapRectToScene(page->boundingRect()).intersects(m_app.window()->contentItem()->boundingRect()));
            if (QString::fromLatin1(section) == "about") {
                QVERIFY(qmltest::Click(m_app, "aboutDeveloperRow"));
                QVERIFY(qmltest::WaitForVisible(m_app, "settingsDeveloper"));
                QVERIFY(qmltest::Click(m_app, "settingsDeveloperBack"));
                QTRY_VERIFY(page->isVisible());
            }
            QCOMPARE(warnings.count(), 0);
        }
        QVERIFY(qmltest::ReturnToNode(m_app));
        QTRY_VERIFY(clock && clock->property("visible").toBool());
        QTRY_VERIFY(qmltest::TransitionsFinished(m_app.window()));
        QCOMPARE(qmltest::Find(m_app, "blockClock"), clock.data());
        QCOMPARE(warnings.count(), 0);
    }
};

std::unique_ptr<QObject> CreateNavigationTests(BitcoinQmlApplication& app, WalletMode)
{
    return std::make_unique<NavigationIntegrationTests>(app);
}

#include <test_navigation.moc>
