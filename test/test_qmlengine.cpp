// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/networkstyle.h>
#include <qml/qmlengine.h>

#include <QHostAddress>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QSignalSpy>
#include <QTcpServer>

#include <memory>

class QmlEngineTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void initErrorEngine_blocksNetwork();
    void initErrorEngine_loadsBundledImage();
    void preInitEngine_blocksNetwork();
    void preInitEngine_loadsLocalImages();
    void mainEngine_blocksNetwork();
    void mainEngine_loadsLocalImages();

private:
    std::unique_ptr<const NetworkStyle> m_network_style;
};

namespace {

std::unique_ptr<QObject> CreateObject(QQmlApplicationEngine& engine, const QString& qml)
{
    QQmlComponent component{&engine};
    component.setData(qml.toUtf8(), QUrl{});
    std::unique_ptr<QObject> object{component.create()};
    if (!object) {
        qWarning().noquote() << component.errorString();
    }
    return object;
}

void VerifyNetworkBlocked(QQmlApplicationEngine& engine)
{
    QTcpServer server;
    QVERIFY(server.listen(QHostAddress::LocalHost));
    QSignalSpy connections{&server, &QTcpServer::newConnection};
    const QString url{QStringLiteral("http://127.0.0.1:%1/remote.png").arg(server.serverPort())};

    const auto root = CreateObject(engine, QStringLiteral(R"(
        import QtQuick
        Item {
            property bool imageFailed: remoteImage.status === Image.Error
            property bool xhrDone: false
            property int xhrStatus: -1
            Image {
                id: remoteImage
                source: "%1"
            }
            Component.onCompleted: {
                const request = new XMLHttpRequest()
                request.onreadystatechange = function() {
                    if (request.readyState === XMLHttpRequest.DONE) {
                        xhrStatus = request.status
                        xhrDone = true
                    }
                }
                request.open("GET", "%1")
                request.send()
            }
        }
    )").arg(url));
    QVERIFY(root);

    QTRY_VERIFY(connections.count() > 0 ||
                (root->property("imageFailed").toBool() && root->property("xhrDone").toBool()));
    QCOMPARE(connections.count(), 0);
    QCOMPARE(root->property("xhrStatus").toInt(), 0);
    QVERIFY(!server.hasPendingConnections());
}

void VerifyImageLoads(QQmlApplicationEngine& engine, const QString& source)
{
    const auto root = CreateObject(engine, QStringLiteral(R"(
        import QtQuick
        Image {
            property bool loaded: status === Image.Ready
            property bool failed: status === Image.Error
            source: "%1"
            sourceSize: Qt.size(16, 16)
        }
    )").arg(source));
    QVERIFY(root);

    QTRY_VERIFY(root->property("loaded").toBool() || root->property("failed").toBool());
    QVERIFY2(root->property("loaded").toBool(), qPrintable(source));
}

const QString BUNDLED_IMAGE{QStringLiteral("qrc:/icons/alert-filled")};
const QString PROVIDER_IMAGE{QStringLiteral("image://images/alert-filled")};
const QString QR_IMAGE{QStringLiteral("image://qr/bitcoin:test?fg=black&bg=white")};

} // namespace

void QmlEngineTests::initTestCase()
{
    Q_INIT_RESOURCE(bitcoin_qml);
    m_network_style.reset(NetworkStyle::instantiate(ChainType::MAIN));
    QVERIFY(m_network_style);
}

void QmlEngineTests::initErrorEngine_blocksNetwork()
{
    const auto engine = CreateInitErrorEngine();
    VerifyNetworkBlocked(*engine);
}

void QmlEngineTests::initErrorEngine_loadsBundledImage()
{
    const auto engine = CreateInitErrorEngine();
    VerifyImageLoads(*engine, BUNDLED_IMAGE);
}

void QmlEngineTests::preInitEngine_blocksNetwork()
{
    const auto engine = CreatePreInitEngine(m_network_style.get());
    VerifyNetworkBlocked(*engine);
}

void QmlEngineTests::preInitEngine_loadsLocalImages()
{
    const auto engine = CreatePreInitEngine(m_network_style.get());
    VerifyImageLoads(*engine, BUNDLED_IMAGE);
    VerifyImageLoads(*engine, PROVIDER_IMAGE);
}

void QmlEngineTests::mainEngine_blocksNetwork()
{
    const auto engine = CreateMainEngine(m_network_style.get());
    VerifyNetworkBlocked(*engine);
}

void QmlEngineTests::mainEngine_loadsLocalImages()
{
    const auto engine = CreateMainEngine(m_network_style.get());
    VerifyImageLoads(*engine, BUNDLED_IMAGE);
    VerifyImageLoads(*engine, PROVIDER_IMAGE);
    VerifyImageLoads(*engine, QR_IMAGE);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(QmlEngineTests)
#else
QTEST_MAIN(QmlEngineTests)
#endif
#include "test_qmlengine.moc"
