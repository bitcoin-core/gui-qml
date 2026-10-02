// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_STARTUPDIALOG_H
#define BITCOIN_QML_STARTUPDIALOG_H

#include <qml/quithandler.h>

#include <QEventLoop>
#include <QQmlEngine>
#include <QQuickWindow>

/** Dismiss a startup dialog without stopping subsequent worker-drain loops. */
inline void ExecStartupDialog(QQmlEngine& engine, QQuickWindow& window, QmlQuitHandler& quit_handler)
{
    QEventLoop loop;
    QObject::connect(&engine, &QQmlEngine::quit, &loop, &QEventLoop::quit);
    // QQuickCloseEvent is private and incomplete in older supported Qt versions.
    QObject::connect(&window, SIGNAL(closing(QQuickCloseEvent*)), &loop, SLOT(quit()));
    QObject::connect(&quit_handler, &QmlQuitHandler::quitRequested, &loop, &QEventLoop::quit);
    loop.exec();
}

#endif // BITCOIN_QML_STARTUPDIALOG_H
