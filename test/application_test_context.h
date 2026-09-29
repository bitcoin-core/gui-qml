// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_APPLICATION_TEST_CONTEXT_H
#define BITCOIN_QML_TEST_APPLICATION_TEST_CONTEXT_H

#include <interfaces/node.h>
#include <test/thread_audit.h>
#include <netbase.h>
#include <qml/models/nodemodel.h>

#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTest>

#include <chrono>
#include <future>
#include <functional>

struct ApplicationTestContext {
    interfaces::Node& node;
    QQmlApplicationEngine& engine;
    std::shared_ptr<qmlintegration::ThreadAudit> audit;

    // Fixture calls exercise the same checked handles as the application, but
    // execute on their own worker while the GUI continues processing events.
    template <typename Method, typename... Args>
    auto call(Method method, Args&&... args) const
    {
        auto result = std::async(std::launch::async, method, &node, std::forward<Args>(args)...);
        while (result.wait_for(std::chrono::milliseconds{0}) != std::future_status::ready) QTest::qWait(1);
        return result.get();
    }

    template <typename T> T* model(const char* name) const
    {
        return qobject_cast<T*>(engine.rootContext()->contextProperty(QString::fromLatin1(name)).value<QObject*>());
    }

    QObject* window() const { return engine.rootObjects().constFirst(); }

    // QML delegates and popup contents can live in the visual tree without
    // being QObject children of the window.
    static QObject* find(QObject* root, const QString& name)
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

    QObject* find(const char* name) const { return find(window(), QString::fromLatin1(name)); }

    static bool transitionsFinished(QObject* root)
    {
        QList<QObject*> pending{root};
        while (!pending.isEmpty()) {
            auto* object = pending.takeLast();
            if (object->property("busy").toBool()) return false;
            pending.append(object->children());
        }
        return true;
    }

    static bool click(QObject* object)
    {
        auto* item = qobject_cast<QQuickItem*>(object);
        if (!item || !item->isVisible() || !item->isEnabled() || !item->window() ||
            item->width() <= 0 || item->height() <= 0) return false;
        QTest::mouseClick(item->window(), Qt::LeftButton, Qt::NoModifier,
                          item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint());
        return true;
    }
};

#endif // BITCOIN_QML_TEST_APPLICATION_TEST_CONTEXT_H
