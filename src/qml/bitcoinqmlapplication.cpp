// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/bitcoinqmlapplication.h>

#include <common/args.h>
#include <init.h>
#include <interfaces/init.h>
#include <interfaces/node.h>
#include <qml/initexecutor.h>
#include <qml/models/nodemodel.h>
#include <qml/test/testbridge.h>

#include <QMetaType>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QString>
#include <QStringLiteral>
#include <QTimer>
#include <QUrl>

#include <cassert>
#include <cstdlib>
#include <memory>

BitcoinQmlApplication::BitcoinQmlApplication(int& argc, char** argv)
    : QGuiApplication{argc, argv}
{
    setApplicationDisplayName(translate("bitcoin-core", "Bitcoin Core"));
    setQuitOnLastWindowClosed(false);
}

BitcoinQmlApplication::~BitcoinQmlApplication()
{
    m_test_bridge.reset();
    m_engine.reset();
    m_init_executor.reset();
    m_node_model.reset();
    if (m_node && m_base_initialized && !m_shutdown_complete) {
        m_node->startShutdown();
        m_node->appShutdown();
    }
}

void BitcoinQmlApplication::parameterSetup()
{
    gArgs.SoftSetBoolArg("-printtoconsole", false);
    InitLogging(gArgs);
    InitParameterInteraction(gArgs);
}

void BitcoinQmlApplication::createNode(interfaces::Init& init)
{
    assert(!m_node);
    m_node = init.makeNode();
}

bool BitcoinQmlApplication::baseInitialize()
{
    assert(m_node);
    m_base_initialized = m_node->baseInitialize();
    return m_base_initialized;
}

bool BitcoinQmlApplication::createWindow()
{
    assert(m_node);
    assert(!m_node_model);

    qRegisterMetaType<interfaces::BlockAndHeaderTipInfo>("interfaces::BlockAndHeaderTipInfo");

    m_node_model = std::make_unique<NodeModel>(*m_node);
    m_init_executor = std::make_unique<QmlInitExecutor>(*m_node);
    connect(m_node_model.get(), &NodeModel::requestedInitialize, m_init_executor.get(), &QmlInitExecutor::initialize);
    connect(m_node_model.get(), &NodeModel::requestedShutdown, m_init_executor.get(), &QmlInitExecutor::shutdown);
    connect(m_init_executor.get(), &QmlInitExecutor::initializeResult, m_node_model.get(), &NodeModel::initializeResult);
    connect(m_init_executor.get(), &QmlInitExecutor::shutdownResult, m_node_model.get(), &NodeModel::shutdownResult);
    connect(m_init_executor.get(), &QmlInitExecutor::runawayException, this, &BitcoinQmlApplication::handleRunawayException);
    connect(m_node_model.get(), &NodeModel::shutdownComplete, this, [this] {
        m_shutdown_complete = true;
        exit(node().getExitStatus());
    });
    connect(this, &QGuiApplication::lastWindowClosed, this, &BitcoinQmlApplication::requestShutdown);

    m_engine = std::make_unique<QQmlApplicationEngine>();
    m_engine->rootContext()->setContextProperty(QStringLiteral("nodeModel"), m_node_model.get());
    m_engine->load(QUrl{QStringLiteral("qrc:///qml/pages/MainWindow.qml")});
    return !m_engine->rootObjects().isEmpty();
}

bool BitcoinQmlApplication::createTestBridge(const QString& socket_path)
{
    assert(m_engine);
    assert(!m_test_bridge);
    m_test_bridge = std::make_unique<TestBridge>(*m_engine, socket_path);
    return m_test_bridge->isListening();
}

[[noreturn]] void BitcoinQmlApplication::handleRunawayException(const QString& message)
{
    m_node_model->handleRunawayException(message);
    ::exit(EXIT_FAILURE);
}

void BitcoinQmlApplication::requestInitialize()
{
    assert(m_node_model);
    QTimer::singleShot(0, m_node_model.get(), &NodeModel::start);
}

void BitcoinQmlApplication::requestShutdown()
{
    assert(m_node_model);
    m_node_model->requestShutdown();
}

interfaces::Node& BitcoinQmlApplication::node() const
{
    assert(m_node);
    return *m_node;
}

NodeModel& BitcoinQmlApplication::nodeModel() const
{
    assert(m_node_model);
    return *m_node_model;
}

QQmlApplicationEngine& BitcoinQmlApplication::engine() const
{
    assert(m_engine);
    return *m_engine;
}
