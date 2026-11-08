// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_cases.h>
#include <test/integration/fixture/test_application.h>

std::span<const IntegrationTestModule> IntegrationTestModules()
{
    static const IntegrationTestModule modules[]{
        {"lifecycle", {WalletMode::Disabled, WalletMode::Enabled}, CreateLifecycleTests, 1},
        {"navigation", {WalletMode::Disabled, WalletMode::Enabled}, CreateNavigationTests, 2},
        {"blockclock", {WalletMode::Disabled, WalletMode::Enabled}, CreateBlockClockTests, 2},
        {"settings", {WalletMode::Disabled, WalletMode::Enabled}, CreateSettingsTests, 2},
        {"node", {WalletMode::Disabled, WalletMode::Enabled}, CreateNodeTests, 3},
        {"debug-log", {WalletMode::Disabled, WalletMode::Enabled}, CreateDebugLogTests, 3, DebugLogTestArguments},
        {"console", {WalletMode::Disabled, WalletMode::Enabled}, CreateConsoleTests, 3},
        {"blocksonly", {WalletMode::Disabled, WalletMode::Enabled}, CreateBlocksonlyTests, 3,
         [](const QString&) { return QStringList{"-blocksonly"}; }},
        {"wallet", {WalletMode::Enabled}, CreateWalletTests, 4},
        {"receive", {WalletMode::Enabled}, CreateReceiveTests, 5},
        {"activity", {WalletMode::Enabled}, CreateActivityTests, 5},
        {"send", {WalletMode::Enabled}, CreateSendTests, 6, SendTestArguments},
        {"psbt", {WalletMode::Enabled}, CreatePsbtTests, 7, [](const QString&) { return QStringList{"-fallbackfee=0.00010000"}; }},
        {"watchonly", {WalletMode::Enabled}, CreateWatchOnlyTests, 8},
        {"payment-import", {WalletMode::Enabled}, CreatePaymentImportTests, 9},
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
