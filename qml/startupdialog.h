// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_STARTUPDIALOG_H
#define BITCOIN_QML_STARTUPDIALOG_H

#include <qml/quithandler.h>

#include <QEventLoop>
#include <QQmlEngine>
#include <QQuickWindow>

inline void ExecStartupDialog(QQmlEngine& engine, QQuickWindow& window, QmlQuitHandler& quit_handler)
{
    QEventLoop dialog_loop;
    QObject::connect(&engine, &QQmlEngine::quit, &dialog_loop, &QEventLoop::quit);
    QObject::connect(&window, SIGNAL(closing(QQuickCloseEvent*)), &dialog_loop, SLOT(quit()));
    QObject::connect(&quit_handler, &QmlQuitHandler::quitRequested, &dialog_loop, &QEventLoop::quit);
    dialog_loop.exec();
}

#endif // BITCOIN_QML_STARTUPDIALOG_H
