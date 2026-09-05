// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINQMLAPPLICATION_H
#define BITCOIN_QML_BITCOINQMLAPPLICATION_H

#include <memory>

#include <QApplication>
#include <QRect>
#include <QStringList>

class AppMode;
class BuildInfo;
class Clipboard;
class NodeModel;
class QmlInitExecutor;
class QQmlApplicationEngine;
class QString;
class TestBridge;
class ApplicationRouter;
class NavigationModel;
class TranslationManager;
namespace interfaces {
class Init;
class Node;
} // namespace interfaces

class BitcoinQmlApplication : public QApplication
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
    void installLanguage(const QString& language);

    interfaces::Node& node() const;
    NodeModel& nodeModel() const;
    QQmlApplicationEngine& engine() const;
    TranslationManager& translations() const;
    ApplicationRouter& router() const;

private:
    [[noreturn]] void handleRunawayException(const QString& message);

    std::unique_ptr<interfaces::Node> m_node;
    std::unique_ptr<AppMode> m_app_mode;
    std::unique_ptr<BuildInfo> m_build_info;
    std::unique_ptr<Clipboard> m_clipboard;
    std::unique_ptr<NodeModel> m_node_model;
    std::unique_ptr<QmlInitExecutor> m_init_executor;
    std::unique_ptr<QQmlApplicationEngine> m_engine;
    std::unique_ptr<TestBridge> m_test_bridge;
    std::unique_ptr<TranslationManager> m_translations;
    std::unique_ptr<ApplicationRouter> m_router;
    std::unique_ptr<NavigationModel> m_navigation_model;
    bool m_base_initialized{false};
    bool m_shutdown_complete{false};
};

#endif // BITCOIN_QML_BITCOINQMLAPPLICATION_H
