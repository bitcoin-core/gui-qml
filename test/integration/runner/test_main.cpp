// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/fixture/test_application.h>
#include <test/integration/runner/test_cases.h>
#include <test/integration/runner/test_process.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <string_view>

int main(int argc, char* argv[])
{
    std::optional<WalletMode> wallet_mode;
    const IntegrationTestModule* module{nullptr};
    QString profile;
    int output_index{1};
    for (int i{1}; i < argc; ++i) {
        const std::string_view argument{argv[i]};
        if (argument == "--help" || argument == "--list-modules") {
            if (argument == "--help") {
                std::cout << "Run integration modules in isolated application processes.\n"
                             "Options: --module=NAME --wallet=disabled|enabled --list-modules\n"
                             "Omitted options select all modules and their supported wallet modes.\n"
                             "Remaining arguments are passed to Qt Test in the selected module.\n";
            }
            for (const auto& entry : IntegrationTestModules()) {
                std::cout << entry.name << ':';
                for (const auto mode : entry.wallet_modes) std::cout << ' ' << WalletModeName(mode);
                std::cout << '\n';
            }
            return EXIT_SUCCESS;
        } else if (argument.starts_with("--module=")) {
            const auto name = argument.substr(std::string_view{"--module="}.size());
            if (module) {
                std::cerr << "Specify --module once.\n";
                return EXIT_FAILURE;
            }
            module = FindIntegrationTestModule(name);
            if (!module) {
                std::cerr << "Unknown integration module: " << name << '\n';
                return EXIT_FAILURE;
            }
        } else if (argument.starts_with("--wallet=")) {
            const auto value = argument.substr(std::string_view{"--wallet="}.size());
            if (wallet_mode || (value != "disabled" && value != "enabled")) {
                std::cerr << "Specify --wallet=disabled or --wallet=enabled once.\n";
                return EXIT_FAILURE;
            }
            wallet_mode = value == "enabled" ? WalletMode::Enabled : WalletMode::Disabled;
        } else if (argument.starts_with("--test-profile=")) {
            if (!profile.isEmpty() || argument == "--test-profile=") {
                std::cerr << "Specify a nonempty --test-profile once.\n";
                return EXIT_FAILURE;
            }
            profile = QString::fromUtf8(argv[i] + std::string_view{"--test-profile="}.size());
        } else {
            argv[output_index++] = argv[i];
        }
    }
    argc = output_index;
    argv[argc] = nullptr;
    if (module && wallet_mode && std::find(module->wallet_modes.begin(), module->wallet_modes.end(), *wallet_mode) == module->wallet_modes.end()) {
        std::cerr << "Module " << module->name << " does not support wallets " << WalletModeName(*wallet_mode) << '\n';
        return EXIT_FAILURE;
    }
    if (profile.isEmpty()) return RunIntegrationProcesses(argc, argv, module, wallet_mode);
    if (!module || !wallet_mode) {
        std::cerr << "A child test process requires both --module and --wallet.\n";
        return EXIT_FAILURE;
    }

    TestApplication app{*wallet_mode, profile};
    return app.runTests(argv[0], {"-qml_onboarded=1"}, [&](BitcoinQmlApplication& application) {
        return module->run(application, *wallet_mode, argc, argv);
    });
}
