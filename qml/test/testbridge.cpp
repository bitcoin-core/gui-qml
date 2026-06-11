// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/testbridge.h>

#include <QClipboard>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMetaMethod>
#include <QMetaObject>
#include <QEventLoop>
#include <QFont>
#include <QImage>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QCloseEvent>
#include <QQmlContext>
#include <QScopedValueRollback>
#include <QTimer>
#include <QVariant>

#include <algorithm>

namespace {
QByteArray okResponse()
{
    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray clickObject(QObject* obj)
{
    if (!obj) {
        QJsonObject resp;
        resp[QStringLiteral("error")] = QStringLiteral("Cannot click null object");
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    // Preferred path: AbstractButton::click() runs the full press/release/
    // clicked pipeline synchronously and toggles `checked` when checkable.
    // Available on QQuickAbstractButton from Qt 6.8+ (Q_REVISION(6, 8)).
    const QMetaObject* meta = obj->metaObject();
    if (int idx = meta->indexOfMethod("click()"); idx >= 0) {
        if (meta->method(idx).invoke(obj, Qt::DirectConnection)) return okResponse();
    }
    if (int idx = meta->indexOfMethod("trigger()"); idx >= 0) {
        if (meta->method(idx).invoke(obj, Qt::DirectConnection)) return okResponse();
    }

    // Fallback for Qt < 6.8 (no QQuickAbstractButton::click()): invoke both
    // toggle() (if checkable) and the clicked() signal. We can't tell which
    // one a given control needs - NavigationTab + ButtonGroup react to
    // `checked` changes via toggle(), while NavButton / ContinueButton react
    // to QML-defined onClicked handlers via the clicked() signal. Firing
    // both covers both shapes without per-component special cases.
    bool invoked_any{false};
    if (obj->property("checkable").toBool()) {
        if (int idx = meta->indexOfMethod("toggle()"); idx >= 0) {
            invoked_any |= meta->method(idx).invoke(obj, Qt::DirectConnection);
        }
    }
    if (int idx = meta->indexOfSignal("clicked()"); idx >= 0) {
        invoked_any |= meta->method(idx).invoke(obj, Qt::DirectConnection);
    }
    if (invoked_any) return okResponse();

    // Last resort for items without click()/trigger()/toggle()/clicked()
    // (bare Item + MouseArea, etc.): synthesize a real mouse press + release
    // at the item's center.
    if (auto* item = qobject_cast<QQuickItem*>(obj)) {
        QQuickWindow* window = item->window();
        if (window && item->isVisible() && item->width() > 0 && item->height() > 0) {
            const QPointF center = item->mapToScene(
                QPointF(item->width() / 2.0, item->height() / 2.0));
            const QPoint pos = center.toPoint();

            QMouseEvent press(QEvent::MouseButtonPress, pos, window->mapToGlobal(pos),
                              Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
            QMouseEvent release(QEvent::MouseButtonRelease, pos, window->mapToGlobal(pos),
                                Qt::LeftButton, Qt::NoButton, Qt::NoModifier);

            QCoreApplication::sendEvent(window, &press);
            QCoreApplication::sendEvent(window, &release);
            return okResponse();
        }
    }

    QJsonObject resp;
    resp[QStringLiteral("error")] = QStringLiteral("Cannot click object");
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}
} // namespace

TestBridge::TestBridge(QQmlApplicationEngine* engine, const QString& socket_path, QObject* parent)
    : QObject(parent), m_engine(engine), m_server(new QLocalServer(this))
{
    // Remove any stale socket file from a previous run.
    QLocalServer::removeServer(socket_path);

    if (!m_server->listen(socket_path)) {
        qWarning("TestBridge: failed to listen on %s: %s",
                 qPrintable(socket_path),
                 qPrintable(m_server->errorString()));
        return;
    }

    connect(m_server, &QLocalServer::newConnection, this, &TestBridge::handleNewConnection);
    qInfo("TestBridge: listening on %s", qPrintable(socket_path));
}

TestBridge::~TestBridge()
{
    for (auto* client : m_clients) {
        client->disconnectFromServer();
        client->deleteLater();
    }
    m_server->close();
}

void TestBridge::handleNewConnection()
{
    while (QLocalSocket* client = m_server->nextPendingConnection()) {
        m_clients.push_back(client);
        m_read_buffers.insert(client, QByteArray{});
        connect(client, &QLocalSocket::readyRead, this, &TestBridge::handleClientData);
        connect(client, &QLocalSocket::disconnected, this, &TestBridge::handleClientDisconnected);
        qInfo("TestBridge: client connected");
    }
}

void TestBridge::handleClientData()
{
    if (m_processing_client_data) {
        // A nested event loop is active while executing a command. Defer any
        // re-entrant readyRead delivery until the active command completes.
        m_pending_client_data = true;
        return;
    }

   // Keep this guard on while handling commands. wait_for_page and
   // wait_for_property run a nested event loop, which, if not guarded,
   // could allow a second command to begin.
    QScopedValueRollback<bool> processing_guard(m_processing_client_data, true);

    do {
        m_pending_client_data = false;
        std::vector<QPointer<QLocalSocket>> clients_snapshot;
        clients_snapshot.reserve(m_clients.size());
        for (QLocalSocket* client : m_clients) {
            clients_snapshot.emplace_back(client);
        }
        for (const QPointer<QLocalSocket>& client : clients_snapshot) {
            if (!client) continue;
            processClientCommands(client.data());
        }
    } while (m_pending_client_data);
}

void TestBridge::processClientCommands(QLocalSocket* client)
{
    if (!client) return;

    QByteArray& read_buffer = m_read_buffers[client];
    if (client->bytesAvailable() > 0) {
        read_buffer.append(client->readAll());
    }

    // Process newline-delimited JSON commands.
    int newline_pos;
    while ((newline_pos = read_buffer.indexOf('\n')) != -1) {
        QByteArray line = read_buffer.left(newline_pos);
        read_buffer.remove(0, newline_pos + 1);

        if (line.trimmed().isEmpty()) continue;

        QByteArray response = processCommand(line);
        if (client->state() != QLocalSocket::ConnectedState) {
            return;
        }
        response.append('\n');
        client->write(response);
        client->flush();
    }
}

void TestBridge::handleClientDisconnected()
{
    auto* client = qobject_cast<QLocalSocket*>(sender());
    if (!client) return;

    auto it = std::find(m_clients.begin(), m_clients.end(), client);
    if (it != m_clients.end()) {
        m_clients.erase(it);
    }
    m_read_buffers.remove(client);
    client->deleteLater();
    qInfo("TestBridge: client disconnected");
}

QObject* TestBridge::findObjectByName(const QString& name) const
{
    // Traverse both QObject children and visual childItems so that items
    // pushed into a StackView (which are visual children before QObject
    // parenting is fully settled) are also reachable.
    QList<QObject*> matches;
    QSet<const QObject*> visited;

    struct Visitor {
        const QString& name;
        QList<QObject*>& matches;
        QSet<const QObject*>& visited;
        void visit(QObject* root) {
            if (!root || visited.contains(root)) return;
            visited.insert(root);
            if (root->objectName() == name) matches.append(root);
            for (QObject* child : root->children()) visit(child);
            if (auto* item = qobject_cast<QQuickItem*>(root)) {
                for (QQuickItem* vchild : item->childItems()) visit(vchild);
            }
        }
    } visitor{name, matches, visited};

    for (QObject* root : m_engine->rootObjects()) {
        visitor.visit(root);
    }

    if (matches.isEmpty()) return nullptr;

    // Prefer a visible QQuickItem (important when StackView keeps
    // hidden pages in the tree with duplicate objectNames).
    for (QObject* obj : matches) {
        auto* item = qobject_cast<QQuickItem*>(obj);
        if (item && item->isVisible()) return obj;
    }

    // Fall back to the first match.
    return matches.first();
}

QObject* TestBridge::findNamedObjectInSubtree(QObject* root, const QString& name) const
{
    if (!root || name.isEmpty()) return nullptr;

    QSet<const QObject*> visited;
    QList<QObject*> stack{root};
    while (!stack.isEmpty()) {
        QObject* current = stack.takeLast();
        if (!current || visited.contains(current)) continue;
        visited.insert(current);

        if (current->objectName() == name) {
            return current;
        }

        for (QObject* child : current->children()) {
            stack.append(child);
        }
        if (auto* item = qobject_cast<QQuickItem*>(current)) {
            for (QQuickItem* visual_child : item->childItems()) {
                stack.append(visual_child);
            }
        }
    }

    return nullptr;
}

QObject* TestBridge::findListItem(QObject* view_obj, int row) const
{
    if (!view_obj || row < 0) return nullptr;

    const QMetaObject* meta = view_obj->metaObject();
    int item_at_index = meta->indexOfMethod("itemAtIndex(int)");
    if (item_at_index >= 0) {
        QQuickItem* item = nullptr;
        if (meta->method(item_at_index).invoke(
                view_obj,
                Qt::DirectConnection,
                Q_RETURN_ARG(QQuickItem*, item),
                Q_ARG(int, row)) && item) {
            return item;
        }
    }

    QObject* content_item = view_obj->property("contentItem").value<QObject*>();
    auto* content_quick_item = qobject_cast<QQuickItem*>(content_item);
    if (!content_quick_item) return nullptr;

    for (QQuickItem* child : content_quick_item->childItems()) {
        if (!child) continue;
        const QVariant index = child->property("index");
        if (index.isValid() && index.toInt() == row) {
            return child;
        }
    }

    return nullptr;
}

QObject* TestBridge::resolveCurrentLeafItem(QObject* item) const
{
    QObject* current = item;
    QSet<const QObject*> seen;
    int guard = 0;

    while (current && guard < 64 && !seen.contains(current)) {
        seen.insert(current);
        ++guard;

        const QVariant depth = current->property("depth");
        const QVariant current_item = current->property("currentItem");
        if (!depth.isValid() || !current_item.isValid()) {
            return current;
        }

        QObject* next = current_item.value<QObject*>();
        if (!next) {
            return current;
        }
        current = next;
    }

    return current;
}

void TestBridge::collectNamedObjects(QObject* root, std::vector<NamedObjectEntry>& results, QSet<const QObject*>& visited, int depth) const
{
    if (!root) return;
    if (visited.contains(root)) return;
    visited.insert(root);

    if (!root->objectName().isEmpty()) {
        NamedObjectEntry named_entry;
        named_entry.object_name = root->objectName();
        named_entry.class_name = QString::fromLatin1(root->metaObject()->className());
        named_entry.depth = depth;
        results.push_back(named_entry);
    }
    for (QObject* child : root->children()) {
        collectNamedObjects(child, results, visited, depth + 1);
    }
    // Also traverse visual children for QQuickItem-based trees.
    auto* item = qobject_cast<QQuickItem*>(root);
    if (item) {
        for (QQuickItem* visual_child : item->childItems()) {
            collectNamedObjects(visual_child, results, visited, depth + 1);
        }
    }
}

QByteArray TestBridge::processCommand(const QByteArray& json_cmd)
{
    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(json_cmd, &parse_error);
    if (doc.isNull()) {
        return errorResponse(QStringLiteral("JSON parse error: %1").arg(parse_error.errorString()));
    }

    QJsonObject obj = doc.object();
    QString cmd = obj.value(QStringLiteral("cmd")).toString();

    if (cmd == QLatin1String("get_current_page")) {
        return cmdGetCurrentPage();
    } else if (cmd == QLatin1String("get_context_property")) {
        return cmdGetContextProperty(obj.value(QStringLiteral("name")).toString());
    } else if (cmd == QLatin1String("get_property")) {
        return cmdGetProperty(
            obj.value(QStringLiteral("objectName")).toString(),
            obj.value(QStringLiteral("prop")).toString());
    } else if (cmd == QLatin1String("click")) {
        return cmdClick(obj.value(QStringLiteral("objectName")).toString());
    } else if (cmd == QLatin1String("set_text")) {
        return cmdSetText(
            obj.value(QStringLiteral("objectName")).toString(),
            obj.value(QStringLiteral("text")).toString());
    } else if (cmd == QLatin1String("wait_for_page")) {
        return cmdWaitForPage(
            obj.value(QStringLiteral("page")).toString(),
            obj.value(QStringLiteral("timeout")).toInt(5000));
    } else if (cmd == QLatin1String("wait_for_property")) {
        return cmdWaitForProperty(
            obj.value(QStringLiteral("objectName")).toString(),
            obj.value(QStringLiteral("prop")).toString(),
            obj.value(QStringLiteral("timeout")).toInt(5000),
            obj.value(QStringLiteral("value")),
            obj.contains(QStringLiteral("value")),
            obj.value(QStringLiteral("contains")).toString(),
            obj.value(QStringLiteral("nonEmpty")).toBool(false));
    } else if (cmd == QLatin1String("get_text")) {
        return cmdGetText(obj.value(QStringLiteral("objectName")).toString());
    } else if (cmd == QLatin1String("click_list_item")) {
        return cmdClickListItem(
            obj.value(QStringLiteral("objectName")).toString(),
            obj.value(QStringLiteral("index")).toInt(-1),
            obj.value(QStringLiteral("childObjectName")).toString());
    } else if (cmd == QLatin1String("get_list_item_property")) {
        return cmdGetListItemProperty(
            obj.value(QStringLiteral("objectName")).toString(),
            obj.value(QStringLiteral("index")).toInt(-1),
            obj.value(QStringLiteral("prop")).toString());
    } else if (cmd == QLatin1String("save_screenshot")) {
        return cmdSaveScreenshot(obj.value(QStringLiteral("path")).toString());
    } else if (cmd == QLatin1String("show_runtime_dialog")) {
        return cmdShowRuntimeDialog(
            obj.value(QStringLiteral("message")).toString(),
            static_cast<unsigned int>(obj.value(QStringLiteral("style")).toDouble()),
            obj.value(QStringLiteral("question")).toBool(false));
    } else if (cmd == QLatin1String("answer_runtime_dialog")) {
        return cmdAnswerRuntimeDialog(static_cast<unsigned int>(obj.value(QStringLiteral("button")).toDouble()));
    } else if (cmd == QLatin1String("list_objects")) {
        return cmdListObjects();
    } else if (cmd == QLatin1String("close_window")) {
        return cmdCloseWindow();
    } else if (cmd == QLatin1String("set_clipboard_text")) {
        return cmdSetClipboardText(obj.value(QStringLiteral("text")).toString());
    }

    return errorResponse(QStringLiteral("Unknown command: %1").arg(cmd));
}

QByteArray TestBridge::cmdGetCurrentPage()
{
    auto pageResponse = [](QObject* page_obj) {
        QString name = page_obj->objectName();
        if (name.isEmpty()) {
            name = QString::fromLatin1(page_obj->metaObject()->className());
        }
        QJsonObject resp;
        resp[QStringLiteral("page")] = name;
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    };

    // Preferred path: resolve from the named main PageStack in main.qml.
    for (QObject* root : m_engine->rootObjects()) {
        QObject* main_stack = root->findChild<QObject*>(QStringLiteral("mainPageStack"));
        if (!main_stack) continue;

        QObject* page_obj = resolveCurrentLeafItem(main_stack->property("currentItem").value<QObject*>());
        if (page_obj) {
            return pageResponse(page_obj);
        }
    }

    // Compatibility fallback for roots that directly expose currentItem.
    for (QObject* root : m_engine->rootObjects()) {
        QVariant depth = root->property("depth");
        QVariant current = root->property("currentItem");
        if (!depth.isValid() || !current.isValid()) continue;

        QObject* current_obj = resolveCurrentLeafItem(current.value<QObject*>());
        if (current_obj) {
            return pageResponse(current_obj);
        }
    }

    return errorResponse(QStringLiteral("Could not determine current page; missing mainPageStack/current page item"));
}

QByteArray TestBridge::cmdGetContextProperty(const QString& name)
{
    if (name.isEmpty()) {
        return errorResponse(QStringLiteral("name is required"));
    }

    QVariant value = m_engine->rootContext()->contextProperty(name);
    const bool exists = value.isValid() && !value.isNull();

    QJsonObject resp;
    resp[QStringLiteral("exists")] = exists;
    if (!exists) {
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    if (QObject* object = value.value<QObject*>()) {
        resp[QStringLiteral("isQObject")] = true;
        resp[QStringLiteral("className")] = QString::fromLatin1(object->metaObject()->className());
        resp[QStringLiteral("objectName")] = object->objectName();
    } else {
        resp[QStringLiteral("isQObject")] = false;
        resp[QStringLiteral("typeName")] = QString::fromLatin1(value.typeName());
        resp[QStringLiteral("value")] = QJsonValue::fromVariant(value);
    }

    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdGetProperty(const QString& object_name, const QString& prop)
{
    if (object_name.isEmpty() || prop.isEmpty()) {
        return errorResponse(QStringLiteral("objectName and prop are required"));
    }

    QObject* obj = findObjectByName(object_name);
    if (!obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));
    }

    auto resolvePropertyPath = [](QObject* target, const QString& path) -> QVariant {
        if (!target || path.isEmpty()) return {};

        const QStringList parts = path.split('.');
        QVariant current = target->property(parts.front().toLatin1().constData());
        if (!current.isValid()) return {};

        auto fontPart = [](const QFont& font, const QString& part) -> QVariant {
            if (part == QLatin1String("family")) return font.family();
            if (part == QLatin1String("pixelSize")) return font.pixelSize();
            if (part == QLatin1String("styleName")) return font.styleName();
            if (part == QLatin1String("bold")) return font.bold();
            return {};
        };

        for (int i = 1; i < parts.size(); ++i) {
            const QString& part = parts.at(i);
            if (current.canConvert<QObject*>()) {
                QObject* nested = current.value<QObject*>();
                if (!nested) return {};
                current = nested->property(part.toLatin1().constData());
            } else if (current.canConvert<QFont>()) {
                current = fontPart(current.value<QFont>(), part);
            } else {
                return {};
            }

            if (!current.isValid()) {
                return {};
            }
        }

        return current;
    };

    QVariant value = resolvePropertyPath(obj, prop);
    if (!value.isValid()) {
        return errorResponse(QStringLiteral("Property not found: %1.%2").arg(object_name, prop));
    }

    QJsonObject resp;
    resp[QStringLiteral("value")] = QJsonValue::fromVariant(value);
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdClick(const QString& object_name)
{
    if (object_name.isEmpty()) {
        return errorResponse(QStringLiteral("objectName is required"));
    }

    QObject* obj = findObjectByName(object_name);
    if (!obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));
    }

    QByteArray response = clickObject(obj);
    if (QJsonDocument::fromJson(response).object().contains(QStringLiteral("error"))) {
        return errorResponse(QStringLiteral("Cannot click object: %1").arg(object_name));
    }
    return response;
}

QByteArray TestBridge::cmdSetText(const QString& object_name, const QString& text)
{
    if (object_name.isEmpty()) {
        return errorResponse(QStringLiteral("objectName is required"));
    }

    QObject* obj = findObjectByName(object_name);
    if (!obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));
    }

    // Try "text" property first (covers TextField, TextInput, TextArea, etc.)
    if (obj->property("text").isValid()) {
        obj->setProperty("text", text);

        // Trigger edit hooks so model-backed fields that update on textEdited
        // or editingFinished are deterministic under test automation.
        const QMetaObject* meta = obj->metaObject();
        bool invoked_edit = false;
        if (int idx = meta->indexOfSignal("textEdited(QString)"); idx >= 0) {
            invoked_edit = meta->method(idx).invoke(obj, Qt::DirectConnection, Q_ARG(QString, text));
        }
        if (!invoked_edit) {
            if (int idx = meta->indexOfSignal("textEdited()"); idx >= 0) {
                meta->method(idx).invoke(obj, Qt::DirectConnection);
            } else if (int idx = meta->indexOfMethod("textEdited()"); idx >= 0) {
                meta->method(idx).invoke(obj, Qt::DirectConnection);
            }
        }
        if (int idx = meta->indexOfSignal("editingFinished()"); idx >= 0) {
            meta->method(idx).invoke(obj, Qt::DirectConnection);
        } else if (int idx = meta->indexOfMethod("editingFinished()"); idx >= 0) {
            meta->method(idx).invoke(obj, Qt::DirectConnection);
        }

        QJsonObject resp;
        resp[QStringLiteral("ok")] = true;
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    return errorResponse(QStringLiteral("Object %1 has no 'text' property").arg(object_name));
}

QByteArray TestBridge::cmdWaitForPage(const QString& page_name, int timeout_ms)
{
    if (page_name.isEmpty()) {
        return errorResponse(QStringLiteral("page is required"));
    }

    auto conditionMatched = [&]() {
        QObject* obj = findObjectByName(page_name);
        if (!obj) return false;

        auto* item = qobject_cast<QQuickItem*>(obj);
        const QVariant visible = obj->property("visible");
        return item ? item->isVisible() : (!visible.isValid() || visible.toBool());
    };

    if (conditionMatched()) {
        QJsonObject resp;
        resp[QStringLiteral("ok")] = true;
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    QEventLoop wait_loop;
    QTimer timeout_timer;
    timeout_timer.setSingleShot(true);
    timeout_timer.setInterval(std::max(0, timeout_ms));

    QTimer poll_timer;
    poll_timer.setInterval(50);

    QObject::connect(&timeout_timer, &QTimer::timeout, &wait_loop, &QEventLoop::quit);
    QObject::connect(&poll_timer, &QTimer::timeout, &wait_loop, [&]() {
        if (conditionMatched()) {
            wait_loop.quit();
        }
    });

    timeout_timer.start();
    poll_timer.start();
    wait_loop.exec();

    if (conditionMatched()) {
        QJsonObject resp;
        resp[QStringLiteral("ok")] = true;
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    return errorResponse(QStringLiteral("Timed out waiting for page: %1").arg(page_name));
}

QByteArray TestBridge::cmdWaitForProperty(const QString& object_name, const QString& prop, int timeout_ms, const QJsonValue& expected, bool has_expected, const QString& contains, bool non_empty)
{
    if (object_name.isEmpty() || prop.isEmpty()) {
        return errorResponse(QStringLiteral("objectName and prop are required"));
    }

    auto resolvePropertyPath = [](QObject* target, const QString& path) -> QVariant {
        if (!target || path.isEmpty()) return {};

        const QStringList parts = path.split('.');
        QVariant current = target->property(parts.front().toLatin1().constData());
        if (!current.isValid()) return {};

        auto fontPart = [](const QFont& font, const QString& part) -> QVariant {
            if (part == QLatin1String("family")) return font.family();
            if (part == QLatin1String("pixelSize")) return font.pixelSize();
            if (part == QLatin1String("styleName")) return font.styleName();
            if (part == QLatin1String("bold")) return font.bold();
            return {};
        };

        for (int i = 1; i < parts.size(); ++i) {
            const QString& part = parts.at(i);
            if (current.canConvert<QObject*>()) {
                QObject* nested = current.value<QObject*>();
                if (!nested) return {};
                current = nested->property(part.toLatin1().constData());
            } else if (current.canConvert<QFont>()) {
                current = fontPart(current.value<QFont>(), part);
            } else {
                return {};
            }

            if (!current.isValid()) {
                return {};
            }
        }

        return current;
    };

    auto conditionMatched = [&](QVariant* out_value) {
        QObject* obj = findObjectByName(object_name);
        if (!obj) return false;

        QVariant value = resolvePropertyPath(obj, prop);
        if (!value.isValid()) return false;

        bool matched = true;
        if (!contains.isEmpty()) {
            matched = value.toString().contains(contains);
        } else if (non_empty) {
            matched = !value.toString().trimmed().isEmpty();
        } else if (has_expected) {
            matched = (QJsonValue::fromVariant(value) == expected);
        }

        if (!matched) return false;

        if (out_value) {
            *out_value = value;
        }
        return true;
    };

    QVariant matched_value;
    if (conditionMatched(&matched_value)) {
        QJsonObject resp;
        resp[QStringLiteral("ok")] = true;
        resp[QStringLiteral("value")] = QJsonValue::fromVariant(matched_value);
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    QEventLoop wait_loop;
    QTimer timeout_timer;
    timeout_timer.setSingleShot(true);
    timeout_timer.setInterval(std::max(0, timeout_ms));

    QTimer poll_timer;
    poll_timer.setInterval(50);

    QObject::connect(&timeout_timer, &QTimer::timeout, &wait_loop, &QEventLoop::quit);
    QObject::connect(&poll_timer, &QTimer::timeout, &wait_loop, [&]() {
        if (conditionMatched(nullptr)) {
            wait_loop.quit();
        }
    });

    timeout_timer.start();
    poll_timer.start();
    wait_loop.exec();

    if (conditionMatched(&matched_value)) {
        QJsonObject resp;
        resp[QStringLiteral("ok")] = true;
        resp[QStringLiteral("value")] = QJsonValue::fromVariant(matched_value);
        return QJsonDocument(resp).toJson(QJsonDocument::Compact);
    }

    return errorResponse(QStringLiteral("Timed out waiting for property: %1.%2").arg(object_name, prop));
}

QByteArray TestBridge::cmdGetText(const QString& object_name)
{
    if (object_name.isEmpty()) {
        return errorResponse(QStringLiteral("objectName is required"));
    }

    QObject* obj = findObjectByName(object_name);
    if (!obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));
    }

    QVariant text_val = obj->property("text");
    if (!text_val.isValid()) {
        return errorResponse(QStringLiteral("Object %1 has no 'text' property").arg(object_name));
    }

    QJsonObject resp;
    resp[QStringLiteral("text")] = text_val.toString();
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdClickListItem(const QString& view_object_name, int row_index, const QString& delegate_child_object_name)
{
    if (view_object_name.isEmpty() || row_index < 0) {
        return errorResponse(QStringLiteral("objectName and non-negative index are required"));
    }

    QObject* view_obj = findObjectByName(view_object_name);
    if (!view_obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(view_object_name));
    }

    QObject* target_obj = findListItem(view_obj, row_index);
    if (!target_obj) {
        return errorResponse(QStringLiteral("List item not found: %1[%2]").arg(view_object_name).arg(row_index));
    }

    if (!delegate_child_object_name.isEmpty()) {
        target_obj = findNamedObjectInSubtree(target_obj, delegate_child_object_name);
        if (!target_obj) {
            return errorResponse(QStringLiteral("Child object not found in list item: %1[%2].%3")
                                     .arg(view_object_name)
                                     .arg(row_index)
                                     .arg(delegate_child_object_name));
        }
    }

    QByteArray response = clickObject(target_obj);
    if (QJsonDocument::fromJson(response).object().contains(QStringLiteral("error"))) {
        return errorResponse(QStringLiteral("Cannot click list item: %1[%2]").arg(view_object_name).arg(row_index));
    }
    return response;
}

QByteArray TestBridge::cmdGetListItemProperty(const QString& view_object_name, int row_index, const QString& prop)
{
    if (view_object_name.isEmpty() || row_index < 0 || prop.isEmpty()) {
        return errorResponse(QStringLiteral("objectName, non-negative index, and prop are required"));
    }

    QObject* view_obj = findObjectByName(view_object_name);
    if (!view_obj) {
        return errorResponse(QStringLiteral("Object not found: %1").arg(view_object_name));
    }

    QObject* item_obj = findListItem(view_obj, row_index);
    if (!item_obj) {
        return errorResponse(QStringLiteral("List item not found: %1[%2]").arg(view_object_name).arg(row_index));
    }

    QVariant value = item_obj->property(prop.toLatin1().constData());
    if (!value.isValid()) {
        return errorResponse(QStringLiteral("Property not found on list item: %1[%2].%3")
                                 .arg(view_object_name)
                                 .arg(row_index)
                                 .arg(prop));
    }

    QJsonObject resp;
    resp[QStringLiteral("value")] = QJsonValue::fromVariant(value);
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdSaveScreenshot(const QString& path)
{
    if (path.isEmpty()) {
        return errorResponse(QStringLiteral("path is required"));
    }

    QQuickWindow* window = nullptr;
    for (QObject* root : m_engine->rootObjects()) {
        window = qobject_cast<QQuickWindow*>(root);
        if (window) break;
    }
    if (!window) {
        return errorResponse(QStringLiteral("No QQuickWindow root object found"));
    }

    // Let pending UI updates settle before capturing.
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    {
        QEventLoop settle_loop;
        QTimer::singleShot(50, &settle_loop, &QEventLoop::quit);
        settle_loop.exec();
    }
    QCoreApplication::processEvents(QEventLoop::AllEvents, 50);

    const QImage image = window->grabWindow();
    if (image.isNull()) {
        return errorResponse(QStringLiteral("Failed to capture screenshot"));
    }

    if (!image.save(path, "PNG")) {
        return errorResponse(QStringLiteral("Failed to save screenshot: %1").arg(path));
    }

    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    resp[QStringLiteral("path")] = path;
    resp[QStringLiteral("width")] = image.width();
    resp[QStringLiteral("height")] = image.height();
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdShowRuntimeDialog(const QString& message, unsigned int style, bool question)
{
    QVariant node_model_value = m_engine->rootContext()->contextProperty(QStringLiteral("nodeModel"));
    QObject* node_model = node_model_value.value<QObject*>();
    if (!node_model) {
        return errorResponse(QStringLiteral("nodeModel context property is not available"));
    }

    const bool invoked = QMetaObject::invokeMethod(
        node_model,
        "showRuntimeDialogForTest",
        Qt::DirectConnection,
        Q_ARG(QString, message),
        Q_ARG(unsigned int, style),
        Q_ARG(bool, question));
    if (!invoked) {
        return errorResponse(QStringLiteral("nodeModel.showRuntimeDialogForTest is not available"));
    }

    return okResponse();
}

QByteArray TestBridge::cmdAnswerRuntimeDialog(unsigned int button)
{
    QVariant node_model_value = m_engine->rootContext()->contextProperty(QStringLiteral("nodeModel"));
    QObject* node_model = node_model_value.value<QObject*>();
    if (!node_model) {
        return errorResponse(QStringLiteral("nodeModel context property is not available"));
    }

    const bool invoked = QMetaObject::invokeMethod(
        node_model,
        "answerRuntimeDialog",
        Qt::DirectConnection,
        Q_ARG(unsigned int, button));
    if (!invoked) {
        return errorResponse(QStringLiteral("nodeModel.answerRuntimeDialog is not available"));
    }

    return okResponse();
}

QByteArray TestBridge::cmdListObjects()
{
    std::vector<NamedObjectEntry> objects;
    QSet<const QObject*> visited;
    for (QObject* root : m_engine->rootObjects()) {
        collectNamedObjects(root, objects, visited, 0);
    }

    QJsonArray arr;
    for (const auto& obj_entry : objects) {
        QJsonObject json_entry;
        json_entry[QStringLiteral("objectName")] = obj_entry.object_name;
        json_entry[QStringLiteral("className")] = obj_entry.class_name;
        json_entry[QStringLiteral("depth")] = obj_entry.depth;
        arr.append(json_entry);
    }

    QJsonObject resp;
    resp[QStringLiteral("objects")] = arr;
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdCloseWindow()
{
    QQuickWindow* window = nullptr;
    for (QObject* root : m_engine->rootObjects()) {
        window = qobject_cast<QQuickWindow*>(root);
        if (window) break;
    }
    if (!window) {
        return errorResponse(QStringLiteral("No QQuickWindow root object found"));
    }

    QCloseEvent close_event;
    QCoreApplication::sendEvent(window, &close_event);

    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::cmdSetClipboardText(const QString& text)
{
    QGuiApplication::clipboard()->setText(text);
    QJsonObject resp;
    resp[QStringLiteral("ok")] = true;
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::errorResponse(const QString& message)
{
    QJsonObject resp;
    resp[QStringLiteral("error")] = message;
    return QJsonDocument(resp).toJson(QJsonDocument::Compact);
}
