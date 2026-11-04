// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qmlengine.h>

#include <qml/imageprovider.h>
#include <qml/qrimageprovider.h>

#include <QIODevice>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QQmlApplicationEngine>
#include <QQmlNetworkAccessManagerFactory>
#include <QString>

namespace {

//! Fails without starting a DNS lookup or a connection. Bundled resources and
//! image providers do not go through the network access manager, so every
//! request that reaches it is rejected.
class BlockedNetworkReply : public QNetworkReply
{
public:
    BlockedNetworkReply(QNetworkAccessManager::Operation operation, const QNetworkRequest& request, QObject* parent)
        : QNetworkReply{parent}
    {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        setError(QNetworkReply::ContentAccessDenied, QStringLiteral("Network access is disabled in QML"));
        setFinished(true);
        // Callers connect to the reply after createRequest() returns.
        QMetaObject::invokeMethod(this, [this] {
            Q_EMIT errorOccurred(error());
            Q_EMIT finished();
        }, Qt::QueuedConnection);
    }

    void abort() override {}
    qint64 bytesAvailable() const override { return 0; }

protected:
    qint64 readData(char*, qint64) override { return -1; }
};

class BlockedNetworkAccessManager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply* createRequest(Operation operation, const QNetworkRequest& request, QIODevice*) override
    {
        return new BlockedNetworkReply{operation, request, this};
    }
};

class BlockedNetworkAccessManagerFactory : public QQmlNetworkAccessManagerFactory
{
public:
    QNetworkAccessManager* create(QObject* parent) override
    {
        return new BlockedNetworkAccessManager{parent};
    }
};

} // namespace

std::unique_ptr<QQmlApplicationEngine> CreateQmlEngine()
{
    // The engine does not take ownership of the factory, and create() may be
    // called from QML loader threads for the lifetime of any engine.
    static BlockedNetworkAccessManagerFactory factory;
    auto engine = std::make_unique<QQmlApplicationEngine>();
    engine->setNetworkAccessManagerFactory(&factory);
    return engine;
}

std::unique_ptr<QQmlApplicationEngine> CreateInitErrorEngine()
{
    return CreateQmlEngine();
}

std::unique_ptr<QQmlApplicationEngine> CreatePreInitEngine(const NetworkStyle* network_style)
{
    auto engine = CreateQmlEngine();
    engine->addImageProvider(QStringLiteral("images"), new ImageProvider{network_style});
    return engine;
}

std::unique_ptr<QQmlApplicationEngine> CreateMainEngine(const NetworkStyle* network_style)
{
    auto engine = CreateQmlEngine();
    engine->addImageProvider(QStringLiteral("images"), new ImageProvider{network_style});
    engine->addImageProvider(QStringLiteral("qr"), new QRImageProvider);
    return engine;
}
