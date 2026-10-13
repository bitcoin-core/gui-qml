// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_CASES_H
#define BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_CASES_H

#include <span>
#include <string_view>
#include <vector>

class BitcoinQmlApplication;
enum class WalletMode;

struct IntegrationTestModule {
    const char* name;
    std::vector<WalletMode> wallet_modes;
    int (*run)(BitcoinQmlApplication&, WalletMode, int, char**);
};

std::span<const IntegrationTestModule> IntegrationTestModules();
const IntegrationTestModule* FindIntegrationTestModule(std::string_view name);
const char* WalletModeName(WalletMode mode);

int RunLifecycleTests(BitcoinQmlApplication& app, WalletMode wallet_mode, int argc, char* argv[]);
int RunNavigationTests(BitcoinQmlApplication& app, WalletMode wallet_mode, int argc, char* argv[]);
int RunNodeTests(BitcoinQmlApplication& app, WalletMode wallet_mode, int argc, char* argv[]);

#endif // BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_CASES_H
