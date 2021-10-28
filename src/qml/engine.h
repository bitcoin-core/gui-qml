// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_ENGINE_H
#define BITCOIN_QML_ENGINE_H

#include <QQmlApplicationEngine>

namespace interfaces {
class Chain;
class Node;
}

class NodeContext;

class Engine : public QQmlApplicationEngine
{
    Q_OBJECT

public:
    explicit Engine(interfaces::Node& node);
    ~Engine();

    interfaces::Node& node() const { return m_node; }

    static interfaces::Chain& chain(QObject* object);
    static interfaces::Node& node(QObject* object);
    static NodeContext& nodeContext(QObject* object);

private:
    interfaces::Node& m_node;
};

#endif // BITCOIN_QML_ENGINE_H
