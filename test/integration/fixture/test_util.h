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
#include <QSet>
#include <QTest>

#include <chrono>
#include <future>
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
    QList<QObject*> pending{root};
    QSet<QObject*> visited;
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (visited.contains(object)) continue;
        visited.insert(object);
        if (object->objectName() == name) return object;
        pending.append(object->children());
        if (auto* window = qobject_cast<QQuickWindow*>(object)) pending.append(window->contentItem());
        if (auto* item = qobject_cast<QQuickItem*>(object)) {
            for (auto* child : item->childItems()) pending.append(child);
        }
    }
    return nullptr;
}

inline QObject* Find(BitcoinQmlApplication& app, const char* name) { return Find(app.window(), QString::fromLatin1(name)); }

inline bool TransitionsFinished(QObject* root)
{
    QList<QObject*> pending{root};
    while (!pending.isEmpty()) {
        auto* object = pending.takeLast();
        if (object->property("busy").toBool()) return false;
        pending.append(object->children());
    }
    return true;
}

inline bool Click(QObject* object)
{
    auto* item = qobject_cast<QQuickItem*>(object);
    if (!item || !item->isVisible() || !item->isEnabled() || !item->window() ||
        item->width() <= 0 || item->height() <= 0) return false;
    QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier,
                      item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
    return true;
}

inline bool OpenSettings(BitcoinQmlApplication& app)
{
    return Click(Find(app, app.window()->property("desktopWalletMode").toBool()
                              ? "desktopWalletSettingsTabButton" : "nodeSettingsButton"));
}

inline bool ReturnToNode(BitcoinQmlApplication& app)
{
    if (app.window()->property("desktopWalletMode").toBool()) return Click(Find(app, "blockClockTabButton"));
    auto* settings = Find(app, "settingsView");
    return !settings || !settings->property("visible").toBool() || Click(Find(app, "settingsDoneButton"));
}
} // namespace qmltest

#endif // BITCOIN_QML_TEST_INTEGRATION_FIXTURE_TEST_UTIL_H
