// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/testbridge.h>

#include <QByteArrayView>
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QKeyEvent>
#include <QLocalSocket>
#include <QMouseEvent>
#include <QPointer>
#include <QQuickItem>
#include <QQuickWindow>
#include <QSet>
#include <QTimer>
#include <QVariant>
#include <QtCore/qnumeric.h>

namespace {
constexpr qsizetype MAX_COMMAND_BYTES{64 * 1024};
constexpr int MAX_CLIENTS{1};
constexpr qsizetype MAX_OBJECTS{4096};
constexpr qsizetype MAX_OBJECT_DEPTH{128};
constexpr qsizetype MAX_JSON_DEPTH{32};
constexpr char READ_BUFFER_PROPERTY[]{"_bitcoin_test_bridge_read_buffer"};

struct PendingObject {
    QPointer<QObject> object;
    qsizetype depth{0};
};

bool HasExcessiveJsonNesting(QByteArrayView input)
{
    qsizetype depth{0};
    bool in_string{false};
    bool escaped{false};
    for (const char character : input) {
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                in_string = false;
            }
            continue;
        }

        if (character == '"') {
            in_string = true;
        } else if (character == '{' || character == '[') {
            if (depth >= MAX_JSON_DEPTH) return true;
            ++depth;
        } else if ((character == '}' || character == ']') && depth > 0) {
            --depth;
        }
    }
    return false;
}

bool AppendChildren(QObject& object, qsizetype depth, QList<PendingObject>& pending, QSet<const QObject*>& queued)
{
    const auto append = [&](QObject* child) {
        if (!child || queued.contains(child)) return true;
        if (depth >= MAX_OBJECT_DEPTH || queued.size() >= MAX_OBJECTS) return false;
        queued.insert(child);
        pending.append({QPointer<QObject>{child}, depth + 1});
        return true;
    };

    for (QObject* child : object.children()) {
        if (!append(child)) return false;
    }
    if (auto* item = qobject_cast<QQuickItem*>(&object)) {
        for (QQuickItem* child : item->childItems()) {
            if (!append(child)) return false;
        }
    }
    return true;
}
} // namespace

TestBridge::TestBridge(QQmlApplicationEngine& engine, const QString& socket_path)
    : m_engine{&engine}
{
    QLocalServer::removeServer(socket_path);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    m_server.setMaxPendingConnections(MAX_CLIENTS);
    if (!m_server.listen(socket_path)) {
        qWarning("TestBridge: failed to listen: %s", qPrintable(m_server.errorString()));
        return;
    }

    connect(&m_server, &QLocalServer::newConnection, this, &TestBridge::handleNewConnection);
    qInfo("TestBridge: listening");
}

void TestBridge::handleNewConnection()
{
    // QLocalServer owns every socket returned by nextPendingConnection().
    while (QLocalSocket* client = m_server.nextPendingConnection()) {
        if (m_active_clients >= static_cast<qsizetype>(MAX_CLIENTS)) {
            client->disconnectFromServer();
            client->deleteLater();
            continue;
        }

        ++m_active_clients;
        client->setReadBufferSize(static_cast<qint64>(MAX_COMMAND_BYTES + 1));
        client->setProperty(READ_BUFFER_PROPERTY, QByteArray{});
        connect(client, &QLocalSocket::readyRead, this, &TestBridge::handleClientData);
        connect(client, &QLocalSocket::disconnected, this, &TestBridge::handleClientDisconnected);
    }
}

void TestBridge::handleClientData()
{
    auto* client = qobject_cast<QLocalSocket*>(sender());
    if (!client) return;

    QByteArray read_buffer{client->property(READ_BUFFER_PROPERTY).toByteArray()};
    const QByteArray incoming{client->readAll()};
    if (read_buffer.size() > MAX_COMMAND_BYTES || incoming.size() > MAX_COMMAND_BYTES - read_buffer.size()) {
        QByteArray response{errorResponse(QStringLiteral("Command exceeds the 64 KiB limit"))};
        response.append('\n');
        client->write(response);
        client->disconnectFromServer();
        return;
    }
    read_buffer.append(incoming);

    qsizetype newline_position{read_buffer.indexOf('\n')};
    while (newline_position >= 0) {
        const QByteArray line{read_buffer.left(newline_position)};
        qsizetype consumed{0};
        if (qAddOverflow(newline_position, qsizetype{1}, &consumed)) {
            client->disconnectFromServer();
            return;
        }
        read_buffer.remove(0, consumed);
        newline_position = read_buffer.indexOf('\n');
        if (line.trimmed().isEmpty()) continue;

        QByteArray response{processCommand(line)};
        response.append('\n');
        client->write(response);
        client->flush();
    }
    client->setProperty(READ_BUFFER_PROPERTY, read_buffer);
}

void TestBridge::handleClientDisconnected()
{
    auto* client = qobject_cast<QLocalSocket*>(sender());
    if (!client) return;

    if (m_active_clients > 0) --m_active_clients;
    client->setProperty(READ_BUFFER_PROPERTY, QVariant{});
    client->deleteLater();
}

QObject* TestBridge::findObjectByName(const QString& name) const
{
    const auto* engine{m_engine.data()};
    if (!engine) return nullptr;

    QList<PendingObject> pending;
    QSet<const QObject*> queued;
    for (QObject* root : engine->rootObjects()) {
        if (!root || queued.size() >= MAX_OBJECTS) continue;
        queued.insert(root);
        pending.append({QPointer<QObject>{root}, 0});
    }

    QPointer<QObject> fallback;
    while (!pending.isEmpty()) {
        const PendingObject entry{pending.takeLast()};
        QObject* const object{entry.object.data()};
        if (!object) continue;

        if (object->objectName() == name) {
            if (auto* item = qobject_cast<QQuickItem*>(object); item && item->isVisible()) return item;
            if (auto* window = qobject_cast<QQuickWindow*>(object); window && window->isVisible()) return window;
            if (!fallback) fallback = object;
        }
        if (!AppendChildren(*object, entry.depth, pending, queued)) return nullptr;
    }
    return fallback.data();
}

QByteArray TestBridge::processCommand(const QByteArray& command) const
{
    if (HasExcessiveJsonNesting(command)) {
        return errorResponse(QStringLiteral("Command exceeds the JSON nesting limit"));
    }

    QJsonParseError parse_error;
    const QJsonDocument document{QJsonDocument::fromJson(command, &parse_error)};
    if (document.isNull()) {
        return errorResponse(QStringLiteral("JSON parse error: %1").arg(parse_error.errorString()));
    }
    if (!document.isObject()) {
        return errorResponse(QStringLiteral("Command must be a JSON object"));
    }

    const QJsonObject object{document.object()};
    const QString command_name{object.value(QStringLiteral("cmd")).toString()};
    if (command_name == QLatin1String("get_property")) {
        return getProperty(object.value(QStringLiteral("objectName")).toString(), object.value(QStringLiteral("prop")).toString());
    }
    if (command_name == QLatin1String("list_objects")) return listObjects();
    if (command_name == QLatin1String("close_window")) return closeWindow();
    if (command_name == QLatin1String("click") || command_name == QLatin1String("type_text") || command_name == QLatin1String("press_key")) {
        return input(object.value(QStringLiteral("objectName")).toString(), command_name,
                     object.value(QStringLiteral("text")).toString());
    }
    return errorResponse(QStringLiteral("Unknown command: %1").arg(command_name));
}

QByteArray TestBridge::getProperty(const QString& object_name, const QString& property_name) const
{
    if (object_name.isEmpty() || property_name.isEmpty()) {
        return errorResponse(QStringLiteral("objectName and prop are required"));
    }

    QObject* object{findObjectByName(object_name)};
    if (!object) return errorResponse(QStringLiteral("Object not found: %1").arg(object_name));

    const QVariant value{object->property(property_name.toLatin1().constData())};
    if (!value.isValid()) {
        return errorResponse(QStringLiteral("Property not found: %1.%2").arg(object_name, property_name));
    }

    QJsonObject response;
    response[QStringLiteral("value")] = QJsonValue::fromVariant(value);
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::listObjects() const
{
    const auto* engine{m_engine.data()};
    if (!engine) return errorResponse(QStringLiteral("QML engine is unavailable"));

    QList<PendingObject> pending;
    QSet<const QObject*> queued;
    for (QObject* root : engine->rootObjects()) {
        if (!root || queued.size() >= MAX_OBJECTS) continue;
        queued.insert(root);
        pending.append({QPointer<QObject>{root}, 0});
    }

    QJsonArray entries;
    while (!pending.isEmpty()) {
        const PendingObject pending_entry{pending.takeLast()};
        QObject* const object{pending_entry.object.data()};
        if (!object) continue;

        if (!object->objectName().isEmpty()) {
            QJsonObject entry;
            entry[QStringLiteral("objectName")] = object->objectName();
            entry[QStringLiteral("className")] = QString::fromLatin1(object->metaObject()->className());
            entry[QStringLiteral("depth")] = static_cast<qint64>(pending_entry.depth);
            entries.append(entry);
        }
        if (!AppendChildren(*object, pending_entry.depth, pending, queued)) {
            return errorResponse(QStringLiteral("QML object tree exceeds automation limits"));
        }
    }

    QJsonObject response;
    response[QStringLiteral("objects")] = entries;
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

QByteArray TestBridge::closeWindow() const
{
    const auto* engine{m_engine.data()};
    if (!engine) return errorResponse(QStringLiteral("QML engine is unavailable"));

    for (QObject* root : engine->rootObjects()) {
        if (!root) continue;
        if (auto* window = qobject_cast<QQuickWindow*>(root)) {
            QTimer::singleShot(0, window, &QQuickWindow::close);
            QJsonObject response;
            response[QStringLiteral("ok")] = true;
            return QJsonDocument(response).toJson(QJsonDocument::Compact);
        }
    }
    return errorResponse(QStringLiteral("No QQuickWindow root object found"));
}

QByteArray TestBridge::input(const QString& object_name, const QString& action, const QString& text) const
{
    // Exercise normal event handlers, never arbitrary QML methods or model
    // setters. The bridge remains regtest-only and uses its bounded protocol.
    QPointer<QQuickItem> item{qobject_cast<QQuickItem*>(findObjectByName(object_name))};
    if (!item || !item->isVisible() || !item->isEnabled() || !item->window()) {
        return errorResponse(QStringLiteral("Input target must be a visible, enabled item: %1").arg(object_name));
    }
    QPointer<QQuickWindow> window{item->window()};
    if (!window->isVisible()) return errorResponse(QStringLiteral("Input target window is hidden"));
    if (action == QLatin1String("click")) {
        const QPointF position = item->mapToScene(QPointF(item->width() / 2, item->height() / 2));
        if (item->width() <= 0 || item->height() <= 0 || !window->contentItem()->contains(position)) {
            return errorResponse(QStringLiteral("Input target is outside the window"));
        }
        const QPointF global = window->mapToGlobal(position.toPoint());
        QMouseEvent press(QEvent::MouseButtonPress, position, global, Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(window, &press);
        if (window) {
            QMouseEvent release(QEvent::MouseButtonRelease, position, global, Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(window, &release);
        }
    } else {
        int key = Qt::Key_unknown;
        QString event_text = text;
        if (action == QLatin1String("press_key")) {
            if (text == QLatin1String("Return")) key = Qt::Key_Return;
            else if (text == QLatin1String("Tab")) key = Qt::Key_Tab;
            else if (text == QLatin1String("Escape")) key = Qt::Key_Escape;
            else if (text == QLatin1String("Backspace")) key = Qt::Key_Backspace;
            else return errorResponse(QStringLiteral("Unsupported input key"));
            event_text.clear();
        }
        item->forceActiveFocus(Qt::OtherFocusReason);
        QKeyEvent press(QEvent::KeyPress, key, Qt::NoModifier, event_text);
        QCoreApplication::sendEvent(window, &press);
        if (window) {
            QKeyEvent release(QEvent::KeyRelease, key, Qt::NoModifier, event_text);
            QCoreApplication::sendEvent(window, &release);
        }
    }
    return QByteArrayLiteral("{\"ok\":true}");
}

QByteArray TestBridge::errorResponse(const QString& message)
{
    QJsonObject response;
    response[QStringLiteral("error")] = message;
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}
