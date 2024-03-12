// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOIN_H
#define BITCOIN_QML_BITCOIN_H

#include <common/args.h>
#include <interfaces/init.h>

#include <interfaces/node.h>
#include <qt/initexecutor.h>

#include <assert.h>
#include <memory>
#include <optional>

#include <boost/signals2/connection.hpp>

#include <QObject>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQuickWindow>
#include <QThread>

class NodeModel;
class ChainModel;
class OptionsQmlModel;
class NetworkTrafficTower;
class PeerTableModel;
class PeerListSortProxy;
class OnboardingModel;

class BitcoinQmlApplication: public QGuiApplication
{
    Q_OBJECT
public:
    explicit BitcoinQmlApplication(int& argc, char* argv[]);

    void setHandlerMessageBox(boost::signals2::connection handler) { m_handler_message_box = handler;}
    void setEngine(QQmlApplicationEngine* engine) { m_engine = engine;}
    void setOnboardingModel(OnboardingModel* model) {m_onboarding_model = model;}
    bool createNode(QQmlApplicationEngine& engine, int& argc, char* argv[], ArgsManager& gArgs);
    bool startNode(QQmlApplicationEngine& engine, int& argc, char* argv[]);
    bool startOnboarding(QQmlApplicationEngine& engine, ArgsManager& gArgs);
    void setNeedsOnboarding(bool needsOnboarding) {m_isOnboarding = needsOnboarding;}

    interfaces::Node& node() const { assert(m_node); return *m_node; }

public Q_SLOTS:

    void startNodeAndTransitionSlot() { createNode(*m_engine, m_argc, m_argv, *m_gArgs); }

private:
    QQmlApplicationEngine* m_engine;
    ArgsManager* m_gArgs;
    QQuickWindow* m_window;
    boost::signals2::connection m_handler_message_box;
    std::unique_ptr<interfaces::Init> m_init;
    std::unique_ptr<interfaces::Node> m_node;
    std::unique_ptr<interfaces::Chain> m_chain;
    NodeModel* m_node_model{nullptr};
    InitExecutor* m_executor{nullptr};
    ChainModel* m_chain_model{nullptr};
    OptionsQmlModel* m_options_model{nullptr};
    OnboardingModel* m_onboarding_model{nullptr};
    int m_argc;
    char** m_argv;
    bool m_prune;
    int m_prune_size_gb;
    NetworkTrafficTower* m_network_traffic_tower;
    PeerTableModel* m_peer_model;
    PeerListSortProxy* m_peer_model_sort_proxy;
    QThread* m_introThread;
    bool m_isOnboarding;
};


int QmlGuiMain(int argc, char* argv[]);

#endif // BITCOIN_QML_BITCOIN_H