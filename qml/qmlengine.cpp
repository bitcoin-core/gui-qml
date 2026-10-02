// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/qmlengine.h>

#include <qml/imageprovider.h>
#include <qml/qrimageprovider.h>

#include <QQmlApplicationEngine>
#include <QString>

std::unique_ptr<QQmlApplicationEngine> CreateQmlEngine()
{
    return std::make_unique<QQmlApplicationEngine>();
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
