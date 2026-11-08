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
