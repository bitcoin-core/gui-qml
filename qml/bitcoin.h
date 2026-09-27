// Copyright (c) 2021-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_BITCOIN_H
#define BITCOIN_QML_BITCOIN_H

#include <functional>
#include <memory>

class QString;
class QQmlApplicationEngine;
namespace interfaces {
class Node;
class Chain;
}

// Optional observers for driving the real application lifecycle in tests.
// Window observers run after signal wiring and before their event loop starts.
struct QmlApplicationHooks {
    std::function<void(QQmlApplicationEngine&)> onboarding_created;
    std::function<void(std::unique_ptr<interfaces::Node>&, std::unique_ptr<interfaces::Chain>&)> interfaces_created;
    std::function<void(interfaces::Node&, QQmlApplicationEngine&)> window_created;
};

int QmlGuiMain(int argc, char* argv[], const QmlApplicationHooks& hooks = {});

// Run with explicit arguments, also on Windows where QmlGuiMain normally reads
// the Unicode command line. The observer runs before the event loop, letting
// integration tests exercise the production application in process.
int RunQmlApplication(int argc, char* argv[], const QmlApplicationHooks& hooks = {});

//! Returns true for benign Qt font fallback warnings ("OpenType support
//! missing for ...") that should be logged at debug level rather than printed.
bool IsBenignQtFontWarning(const QString& msg);

#endif // BITCOIN_QML_BITCOIN_H
