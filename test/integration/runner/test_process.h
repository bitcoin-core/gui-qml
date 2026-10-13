// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_PROCESS_H
#define BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_PROCESS_H

#include <optional>

struct IntegrationTestModule;
enum class WalletMode;

int RunIntegrationProcesses(int argc, char* argv[], const IntegrationTestModule* module,
                            std::optional<WalletMode> wallet_mode);

#endif // BITCOIN_QML_TEST_INTEGRATION_RUNNER_TEST_PROCESS_H
