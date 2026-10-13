// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOINQMLAPPLICATION_H
#define BITCOIN_QML_BITCOINQMLAPPLICATION_H

class NodeModel;
class QQmlApplicationEngine;
class QQuickWindow;
namespace interfaces {
class Node;
}

// Runs the production QML application once, including its QApplication lifetime.
class BitcoinQmlApplication
{
public:
    BitcoinQmlApplication() = default;
    virtual ~BitcoinQmlApplication() = default;
    BitcoinQmlApplication(const BitcoinQmlApplication&) = delete;
    BitcoinQmlApplication& operator=(const BitcoinQmlApplication&) = delete;

    // Use native Unicode arguments on Windows; run() keeps explicit test arguments.
    int runFromCommandLine(int argc, char* argv[]);
    int run(int argc, char* argv[]);

    // Runtime objects are available from windowCreated until run returns.
    interfaces::Node& node() const;
    NodeModel& nodeModel() const;
    QQmlApplicationEngine& engine() const;
    QQuickWindow* window() const;
    void requestShutdown();

protected:
    // Called after signal wiring, before the corresponding event loop starts.
    virtual void onboardingCreated(QQmlApplicationEngine&) {}
    virtual void windowCreated() {}

private:
    interfaces::Node* m_node{nullptr};
    NodeModel* m_node_model{nullptr};
    QQmlApplicationEngine* m_engine{nullptr};
};

#endif // BITCOIN_QML_BITCOINQMLAPPLICATION_H
