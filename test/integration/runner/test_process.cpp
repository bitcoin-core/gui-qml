// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <test/integration/runner/test_process.h>
#include <test/integration/runner/test_cases.h>
#include <util/subprocess.h>

#include <QCoreApplication>
#include <QTemporaryDir>

#include <cstdlib>
#include <exception>
#include <iostream>
#include <string>
#include <vector>

int RunIntegrationProcesses(int argc, char* argv[], const IntegrationTestModule* selected_module,
                            std::optional<WalletMode> wallet_mode)
{
    QCoreApplication app(argc, argv);
    int status{EXIT_SUCCESS};
    bool ran{false};
    for (const auto& module : IntegrationTestModules()) {
        if (selected_module && &module != selected_module) continue;
        for (const auto mode : module.wallet_modes) {
            if (wallet_mode && mode != *wallet_mode) continue;
            ran = true;
            QTemporaryDir profile;
            if (!profile.isValid()) return EXIT_FAILURE;
            std::vector<std::string> arguments{app.applicationFilePath().toStdString()};
            for (const auto& argument : app.arguments().mid(1)) arguments.push_back(argument.toStdString());
            arguments.push_back(std::string{"--module="} + module.name);
            arguments.push_back(std::string{"--wallet="} + WalletModeName(mode));
            arguments.push_back("--test-profile=" + profile.path().toStdString());
            std::cout << "Running " << module.name << " with wallets " << WalletModeName(mode) << std::endl;
            try {
                subprocess::Popen process{arguments, subprocess::input{subprocess::PIPE},
                                          subprocess::output{subprocess::PIPE}, subprocess::error{subprocess::PIPE}};
                const auto [output, error] = process.communicate();
                std::cout.write(output.buf.data(), output.length);
                std::cerr.write(error.buf.data(), error.length);
                if (process.retcode() != EXIT_SUCCESS) {
                    std::cerr << "Integration module " << module.name << " failed with wallets " << WalletModeName(mode)
                              << " (exit " << process.retcode() << ")\n";
                    status = EXIT_FAILURE;
                }
            } catch (const std::exception& error) {
                std::cerr << "Integration module " << module.name << " failed with wallets " << WalletModeName(mode)
                          << ": " << error.what() << '\n';
                status = EXIT_FAILURE;
            }
        }
    }
    if (!ran) std::cerr << "No integration modules support the selected wallet mode\n";
    return ran ? status : EXIT_FAILURE;
}
