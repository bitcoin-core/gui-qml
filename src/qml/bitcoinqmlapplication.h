// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINQMLAPPLICATION_H
#define BITCOIN_QML_BITCOINQMLAPPLICATION_H

#include <memory>

#include <QGuiApplication>

class NodeModel;
class QmlInitExecutor;
class QQmlApplicationEngine;
class QString;
class TestBridge;
namespace interfaces {
class Init;
class Node;
} // namespace interfaces

class BitcoinQmlApplication : public QGuiApplication
{
public:
    BitcoinQmlApplication(int& argc, char** argv);
    ~BitcoinQmlApplication() override;

    void parameterSetup();
    void createNode(interfaces::Init& init);
    bool baseInitialize();
    bool createWindow();
    bool createTestBridge(const QString& socket_path);
    void requestInitialize();
    void requestShutdown();

    interfaces::Node& node() const;
    NodeModel& nodeModel() const;
    QQmlApplicationEngine& engine() const;

private:
    [[noreturn]] void handleRunawayException(const QString& message);

    std::unique_ptr<interfaces::Node> m_node;
    std::unique_ptr<NodeModel> m_node_model;
    std::unique_ptr<QmlInitExecutor> m_init_executor;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<TestBridge> m_test_bridge;
    bool m_base_initialized{false};
    bool m_shutdown_complete{false};
};

#endif // BITCOIN_QML_BITCOINQMLAPPLICATION_H
