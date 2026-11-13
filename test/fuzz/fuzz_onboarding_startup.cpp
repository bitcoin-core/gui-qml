// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/onboarding_settings.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

static int argc = 1;
static char arg0[] = "fuzz_onboarding_startup";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    // Split the input on NUL bytes into argv-like tokens.
    std::vector<std::string> args;
    std::string current;
    for (size_t i = 0; i < size; ++i) {
        if (data[i] == 0) {
            args.push_back(current);
            current.clear();
        } else {
            current.push_back(static_cast<char>(data[i]));
        }
    }
    if (!current.empty()) args.push_back(current);

    const bool can_listen_ipc = size > 0 && (data[0] & 1);
    (void)QmlOnboardingSettings::ResolveOnboardingStartupStatus(args, can_listen_ipc);
    return 0;
}
