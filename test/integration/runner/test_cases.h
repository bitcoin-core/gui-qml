// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_CASES_H
#define BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_CASES_H

#include <memory>
#include <span>
#include <string_view>
#include <vector>
#include <QStringList>

class BitcoinQmlApplication;
class QObject;
enum class WalletMode;

struct IntegrationTestModule {
    const char* name;
    std::vector<WalletMode> wallet_modes;
    std::unique_ptr<QObject> (*create)(BitcoinQmlApplication&, WalletMode);
    int stage;
    QStringList (*arguments)(const QString& selector){nullptr};
};

std::span<const IntegrationTestModule> IntegrationTestModules();
const IntegrationTestModule* FindIntegrationTestModule(std::string_view name);
const char* WalletModeName(WalletMode mode);

std::unique_ptr<QObject> CreateLifecycleTests(BitcoinQmlApplication& app, WalletMode wallet_mode);
std::unique_ptr<QObject> CreateNavigationTests(BitcoinQmlApplication& app, WalletMode wallet_mode);
std::unique_ptr<QObject> CreateDebugLogTests(BitcoinQmlApplication& app, WalletMode wallet_mode);
std::unique_ptr<QObject> CreateNodeTests(BitcoinQmlApplication& app, WalletMode wallet_mode);
std::unique_ptr<QObject> CreateBlockClockTests(BitcoinQmlApplication& app, WalletMode wallet_mode);
std::unique_ptr<QObject> CreateSettingsTests(BitcoinQmlApplication& app, WalletMode wallet_mode);
std::unique_ptr<QObject> CreateConsoleTests(BitcoinQmlApplication& app, WalletMode wallet_mode);
QStringList DebugLogTestArguments(const QString& selector);

#endif // BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_CASES_H
