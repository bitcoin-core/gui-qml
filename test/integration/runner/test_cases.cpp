// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <bitcoin-build-config.h> // IWYU pragma: keep

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_application.h>

std::span<const IntegrationTestModule> IntegrationTestModules()
{
#ifdef ENABLE_WALLET
    const std::vector<WalletMode> node_modes{WalletMode::Disabled, WalletMode::Enabled};
#else
    const std::vector<WalletMode> node_modes{WalletMode::Disabled};
#endif
    static const IntegrationTestModule modules[]{
        {"lifecycle", node_modes, CreateLifecycleTests, 1},
        {"navigation", node_modes, CreateNavigationTests, 2},
        {"blockclock", node_modes, CreateBlockClockTests, 2},
        {"settings", node_modes, CreateSettingsTests, 2, SettingsTestArguments},
        {"node", node_modes, CreateNodeTests, 3},
        {"debug-log", node_modes, CreateDebugLogTests, 3, DebugLogTestArguments},
        {"console", node_modes, CreateConsoleTests, 3},
        {"blocksonly", node_modes, CreateBlocksonlyTests, 3,
         [](const QString&) { return QStringList{"-blocksonly"}; }},
#ifdef ENABLE_WALLET
        {"wallet", {WalletMode::Enabled}, CreateWalletTests, 4, WalletTestArguments, WalletPreparedWallet},
        {"receive", {WalletMode::Enabled}, CreateReceiveTests, 5, ReceiveTestArguments},
        {"activity", {WalletMode::Enabled}, CreateActivityTests, 5},
        {"transaction-policy", {WalletMode::Enabled}, CreateTransactionPolicyTests, 6, [](const QString&) { return QStringList{"-fallbackfee=0.00010000"}; }},
        {"send", {WalletMode::Enabled}, CreateSendTests, 6, SendTestArguments, SendPreparedWallet},
        {"fee-bump", {WalletMode::Enabled}, CreateFeeBumpTests, 6, [](const QString&) { return QStringList{"-fallbackfee=0.00010000"}; }},
        {"psbt", {WalletMode::Enabled}, CreatePsbtTests, 7, [](const QString&) { return QStringList{"-fallbackfee=0.00010000"}; }, PsbtPreparedWallet},
        {"watchonly", {WalletMode::Enabled}, CreateWatchOnlyTests, 8, WatchOnlyTestArguments},
        {"payment-import", {WalletMode::Enabled}, CreatePaymentImportTests, 9},
#endif
    };
    return modules;
}

const IntegrationTestModule* FindIntegrationTestModule(std::string_view name)
{
    for (const auto& module : IntegrationTestModules()) {
        if (name == module.name) return &module;
    }
    return nullptr;
}

const char* WalletModeName(WalletMode mode)
{
    return mode == WalletMode::Enabled ? "enabled" : "disabled";
}
