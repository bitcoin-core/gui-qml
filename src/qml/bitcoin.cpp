// Copyright (c) 2021-present The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoin.h>

#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStringLiteral>
#include <QUrl>

#include <cstdlib>

int QmlGuiMain(int argc, char* argv[])
{
    Q_INIT_RESOURCE(bitcoin_qml);

    QQuickStyle::setStyle(QStringLiteral("Basic"));

    QGuiApplication app(argc, argv);
    QGuiApplication::setApplicationDisplayName(QGuiApplication::translate("bitcoin-core", "Bitcoin Core"));

    QQmlApplicationEngine engine;
    engine.load(QUrl{QStringLiteral("qrc:///qml/pages/MainWindow.qml")});
    if (engine.rootObjects().isEmpty()) {
        return EXIT_FAILURE;
    }

    return app.exec();
}
