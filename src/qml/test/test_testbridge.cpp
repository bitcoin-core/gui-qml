// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/test/qt_test_registry.h>
#include <qml/test/testbridge.h>

#include <QByteArray>
#include <QLocalSocket>
#include <QQmlApplicationEngine>
#include <QTemporaryDir>
#include <QTest>

class TestBridgeTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void rejectsOversizedCommand()
    {
        QTemporaryDir socket_dir;
        QVERIFY(socket_dir.isValid());

        QQmlApplicationEngine engine;
        TestBridge bridge{engine, socket_dir.filePath(QStringLiteral("bridge.sock"))};
        QVERIFY(bridge.isListening());
        QLocalSocket client;
        client.connectToServer(socket_dir.filePath(QStringLiteral("bridge.sock")));
        QVERIFY(client.waitForConnected());

        constexpr qsizetype OVERSIZED_COMMAND_BYTES{64 * 1024 + 1};
        const QByteArray command(OVERSIZED_COMMAND_BYTES, 'x');
        QCOMPARE(client.write(command), command.size());
        QTRY_COMPARE_WITH_TIMEOUT(client.state(), QLocalSocket::UnconnectedState, 1000);
    }

    void rejectsDeeplyNestedCommand()
    {
        QTemporaryDir socket_dir;
        QVERIFY(socket_dir.isValid());

        QQmlApplicationEngine engine;
        TestBridge bridge{engine, socket_dir.filePath(QStringLiteral("bridge.sock"))};
        QVERIFY(bridge.isListening());
        QLocalSocket client;
        client.connectToServer(socket_dir.filePath(QStringLiteral("bridge.sock")));
        QVERIFY(client.waitForConnected());

        QByteArray command{"{\"cmd\":\"list_objects\",\"nested\":"};
        command.append(QByteArray(33, '['));
        command.append(QByteArray(33, ']'));
        command.append("}\n");
        QCOMPARE(client.write(command), command.size());
        QTRY_VERIFY_WITH_TIMEOUT(client.bytesAvailable() > 0, 1000);
        QVERIFY(client.readAll().contains("JSON nesting limit"));
    }
};

BITCOINQML_REGISTER_QT_TEST(TestBridgeTests)

#include <test_testbridge.moc>
