// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_TESTBRIDGE_H
#define BITCOIN_QML_TEST_TESTBRIDGE_H

#include <QByteArray>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QQmlApplicationEngine>
#include <QString>

/** Expose the QML object tree to functional tests over a local socket. */
class TestBridge : public QObject
{
    Q_OBJECT

public:
    explicit TestBridge(QQmlApplicationEngine& engine, const QString& socket_path);
    bool isListening() const { return m_server.isListening(); }

private Q_SLOTS:
    void handleNewConnection();
    void handleClientData();
    void handleClientDisconnected();

private:
    QObject* findObjectByName(const QString& name) const;

    QByteArray processCommand(const QByteArray& command) const;
    QByteArray getProperty(const QString& object_name, const QString& property_name) const;
    QByteArray listObjects() const;
    QByteArray closeWindow() const;
    static QByteArray errorResponse(const QString& message);

    QPointer<QQmlApplicationEngine> m_engine;
    QLocalServer m_server;
    qsizetype m_active_clients{0};
};

#endif // BITCOIN_QML_TEST_TESTBRIDGE_H
