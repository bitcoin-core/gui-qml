// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_UTIL_H
#define BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_UTIL_H

#include <interfaces/node.h>
#include <netbase.h>
#include <qml/bitcoinqmlapplication.h>
#include <qml/models/nodemodel.h>
#include <univalue.h>

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QMetaEnum>
#include <QInputMethodEvent>
#include <QSet>
#include <QPointer>
#include <QSignalSpy>
#include <QTest>
#include <QtQuickTest/quicktest.h>

#include <chrono>
#include <future>
#include <memory>
#include <string>
#include <utility>

namespace qmltest {
template <typename Method, typename... Args>
auto Call(BitcoinQmlApplication& app, Method method, Args&&... args)
{
    auto result = std::async(std::launch::async, method, &app.node(), std::forward<Args>(args)...);
    while (result.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) QTest::qWait(1);
    return result.get();
}

inline bool RpcCommandRunning(BitcoinQmlApplication& app, const std::string& command)
{
    const auto info = Call(app, &interfaces::Node::executeRpc, "getrpcinfo", UniValue{UniValue::VARR}, "");
    for (const auto& active : info.find_value("active_commands").getValues()) {
        if (active.find_value("method").get_str() == command) return true;
    }
    return false;
}

template <typename T> T* Model(BitcoinQmlApplication& app, const char* name)
{
    return qobject_cast<T*>(app.engine().rootContext()->contextProperty(QString::fromLatin1(name)).value<QObject*>());
}

// QML delegates and popup contents can live in the visual tree without
// being QObject children of the window.
inline QObject* Find(QObject* root, const QString& name)
{
    QList<QObject*> matches;
    QList<QObject*> pending{root};
    QSet<QObject*> visited;
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (visited.contains(object)) continue;
        visited.insert(object);
        if (object->objectName() == name) matches.append(object);
        pending.append(object->children());
        if (auto* window = qobject_cast<QQuickWindow*>(object)) pending.append(window->contentItem());
        if (auto* item = qobject_cast<QQuickItem*>(object)) {
            for (auto* child : item->childItems()) pending.append(child);
        }
    }
    if (matches.isEmpty()) return nullptr;
    // Stack views retain hidden pages with duplicate names.
    for (auto* object : matches) {
        if (auto* item = qobject_cast<QQuickItem*>(object); item && item->isVisible()) return object;
    }
    for (auto* object : matches) {
        if (object->property("visible").toBool()) return object;
    }
    for (auto* object : matches) {
        for (auto* parent = object->parent(); parent; parent = parent->parent()) {
            if (auto* item = qobject_cast<QQuickItem*>(parent)) {
                if (item->isVisible()) return object;
                break;
            }
        }
    }
    return matches.first();
}

inline QObject* Find(BitcoinQmlApplication& app, const char* name)
{
    const auto object_name = QString::fromLatin1(name);
    // Closed popups and nonvisual models also share names across wallets.
    // Resolve these inside the selected wallet's retained view first.
    auto* host = Find(app.window(), QStringLiteral("walletViewsHost"));
    for (auto* parent = host; parent; parent = parent->parent()) {
        if (parent->metaObject()->indexOfProperty("activeView") < 0) continue;
        if (auto* view = parent->property("activeView").value<QObject*>()) {
            if (auto* object = Find(view, object_name)) return object;
        }
        break;
    }
    return Find(app.window(), object_name);
}

// Check visual and QObject children, including popup overlays and retained pages.
// Return locations only, never the secret itself, in failure diagnostics.
inline QStringList SecretTextLocations(QObject* root, const QString& secret)
{
    QStringList leaks;
    QList<QObject*> pending{root};
    QSet<QObject*> visited;
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (!object || visited.contains(object)) continue;
        visited.insert(object);
        for (const char* property : {"text", "errorText", "message"}) {
            if (!secret.isEmpty() && object->property(property).toString().contains(secret)) {
                leaks.append(QStringLiteral("%1.%2").arg(object->objectName(), QString::fromLatin1(property)));
            }
        }
        pending.append(object->children());
        if (auto* window = qobject_cast<QQuickWindow*>(object)) pending.append(window->contentItem());
        if (auto* item = qobject_cast<QQuickItem*>(object)) {
            for (auto* child : item->childItems()) pending.append(child);
        }
    }
    return leaks;
}

inline bool TransitionsFinished(QObject* root)
{
    QList<QObject*> pending{root};
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (object->property("busy").toBool()) return false;
        if (object->inherits("QQuickPopup") && object->property("visible").toBool() && !object->property("opened").toBool()) return false;
        pending.append(object->children());
    }
    return true;
}

inline bool WaitForPolish(QQuickItem* item)
{
    if (!item || !item->window()) return false;
#if QT_VERSION >= QT_VERSION_CHECK(6, 4, 0)
    return QQuickTest::qWaitForPolish(item->window());
#else
    for (QPointer<QQuickItem> current{item}; current; current = current->parentItem()) {
        if (!QQuickTest::qWaitForItemPolished(current)) return false;
    }
    return true;
#endif
}

inline bool ClickWithRetries(QObject* object)
{
    QPointer<QQuickItem> item = qobject_cast<QQuickItem*>(object);
    if (!item || !item->isVisible() || !item->isEnabled() || !WaitForPolish(item) || !item ||
        item->width() <= 0 || item->height() <= 0) return false;
    const auto click = [&] {
        if (!item || !item->isVisible() || !item->isEnabled() || !item->window()) return false;
        QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier,
                         item->mapToScene(item->boundingRect().center()).toPoint());
        return true;
    };
    if (item->metaObject()->indexOfSignal("clicked()") < 0) return click();
    QSignalSpy clicked{item, SIGNAL(clicked())};
    const auto pressed = item->metaObject()->indexOfSignal("pressed()") < 0 ? nullptr : std::make_unique<QSignalSpy>(item, SIGNAL(pressed()));
    const auto delivered = [&] { return (pressed && !pressed->isEmpty()) || !clicked.isEmpty(); };
    // Modal exit can consume a gesture. Keep retries visible in the report so
    // a successful test does not silently conceal swallowed input.
    const QString target = item->objectName();
    int attempts{0};
    const bool success = QTest::qWaitFor([&] {
        if (delivered()) return true;
        ++attempts;
        return click() && delivered();
    }, 10'000);
    if (attempts > 1) qWarning() << "Click retries for" << target << "attempts:" << attempts << "delivered:" << success;
    return success;
}

// Existing journeys tolerate modal-exit retries, but each retry is reported.
inline bool Click(QObject* object)
{
    return ClickWithRetries(object);
}

inline bool OpenSettings(BitcoinQmlApplication& app)
{
    return Click(Find(app, app.window()->property("desktopWalletMode").toBool()
                              ? "desktopWalletSettingsTabButton" : "nodeSettingsButton"));
}

inline QObject* WaitForVisible(BitcoinQmlApplication& app, const char* name)
{
    QObject* object{nullptr};
    const bool ready = QTest::qWaitFor([&] {
        object = Find(app, name);
        return object && object->property("visible").toBool() && TransitionsFinished(app.window());
    }, 10'000);
    if (!ready) qWarning() << "Control did not become ready:" << name << object << "visible:" << (object && object->property("visible").toBool()) << "transitions finished:" << TransitionsFinished(app.window());
    return ready ? object : nullptr;
}

inline bool Click(BitcoinQmlApplication& app, const char* name)
{
    auto* object = WaitForVisible(app, name);
    QPointer<QQuickItem> item = qobject_cast<QQuickItem*>(object);
    if (!item || !WaitForPolish(item) || !item) return false;
    for (auto* parent = item->parentItem(); parent; parent = parent->parentItem()) {
        if (!parent->inherits("QQuickFlickable")) continue;
        const auto rect = item->mapRectToItem(parent, item->boundingRect());
        const auto offset = rect.top() < 0 ? rect.top() : std::max(0.0, rect.bottom() - parent->height());
        if (offset != 0) parent->setProperty("contentY", parent->property("contentY").toDouble() + offset);
    }
    QCoreApplication::processEvents();
    const bool ready = QTest::qWaitFor([&] {
        return item && item->isEnabled() && item->width() > 0 && item->height() > 0 &&
               app.window()->contentItem()->contains(item->mapToItem(app.window()->contentItem(), item->boundingRect().center()));
    });
    if (!ready) qWarning() << "Cannot click" << name;
    return ready && item && Click(item.data());
}

inline bool SetText(BitcoinQmlApplication& app, const char* name, const QString& text)
{
    auto* item = qobject_cast<QQuickItem*>(WaitForVisible(app, name));
    if (!item) return false;
    item->forceActiveFocus();
    if (!QMetaObject::invokeMethod(item, "selectAll")) return false;
    QTest::keyClick(item->window(), Qt::Key_Backspace, Qt::NoModifier, 1);
    QInputMethodEvent edit;
    edit.setCommitString(text);
    QCoreApplication::sendEvent(item, &edit);
    return edit.isAccepted();
}

inline bool SelectSettings(BitcoinQmlApplication& app, const char* section)
{
    auto* settings = qobject_cast<QQuickItem*>(Find(app, "settingsView"));
    if ((!settings || !settings->isVisible()) && !OpenSettings(app)) return false;
    if (!WaitForVisible(app, "settingsView")) return false;
    auto* sidebar = qobject_cast<QQuickItem*>(Find(app, "settingsSidebarList"));
    auto* button = qobject_cast<QQuickItem*>(Find(app, qPrintable(QStringLiteral("settingsSidebar_%1").arg(section))));
    if (!sidebar || !button) return false;
    const auto position_mode = sidebar->metaObject()->enumerator(sidebar->metaObject()->indexOfEnumerator("PositionMode"));
    const int contain = position_mode.keyToValue("Contain");
    const int index = button->parentItem()->property("index").toInt();
    if (contain < 0 || !QMetaObject::invokeMethod(sidebar, "positionViewAtIndex", Q_ARG(int, index), Q_ARG(int, contain))) return false;
    return QTest::qWaitFor([&] { return sidebar->contains(button->mapToItem(sidebar, button->boundingRect().center())); }) && Click(button);
}

inline bool ReturnToNode(BitcoinQmlApplication& app)
{
    if (app.window()->property("desktopWalletMode").toBool()) return Click(Find(app, "blockClockTabButton"));
    auto* settings = Find(app, "settingsView");
    return !settings || !settings->property("visible").toBool() || Click(Find(app, "settingsDoneButton"));
}
} // namespace qmltest

#endif // BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_UTIL_H
