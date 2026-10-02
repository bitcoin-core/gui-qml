// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_QMLENGINE_H
#define BITCOIN_QML_QMLENGINE_H

#include <QQmlApplicationEngine>

#include <memory>

class NetworkStyle;

//! Every QML engine must be created through this function so that it cannot
//! access the network.
std::unique_ptr<QQmlApplicationEngine> CreateQmlEngine();

std::unique_ptr<QQmlApplicationEngine> CreateInitErrorEngine();
std::unique_ptr<QQmlApplicationEngine> CreatePreInitEngine(const NetworkStyle* network_style);
std::unique_ptr<QQmlApplicationEngine> CreateMainEngine(const NetworkStyle* network_style);

#endif // BITCOIN_QML_QMLENGINE_H
