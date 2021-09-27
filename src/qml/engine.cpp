// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/engine.h>

#include <interfaces/node.h>
#include <node/context.h>

#include <QQmlContext>

Engine::Engine(interfaces::Node& node)
    : m_node(node)
{
}

Engine::~Engine()
{
}

interfaces::Node& Engine::node(QObject* object)
{
    auto context = Assert(QQmlEngine::contextForObject(object));
    auto engine = Assert(context->engine());
    return Assert(qobject_cast<Engine*>(engine))->node();
}
