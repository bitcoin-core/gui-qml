// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/guiargs.h>

#include <chainparams.h>
#include <common/args.h>

struct FuzzArgsManager : ArgsManager {
    using ArgsManager::ReadConfigStream;
    using ArgsManager::ReadConfigString;
};
#include <init.h>

#include <QApplication>
#include <QByteArray>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace {
static int argc = 1;
static char arg0[] = "fuzz_bitcoin_conf";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

static bool params_selected = false;

void EnsureParamsSelected()
{
    if (!params_selected) {
        SelectParams(ChainType::REGTEST);
        params_selected = true;
    }
}

bool PrepareArgs(ArgsManager& args, std::string& error)
{
    SetupServerArgs(args, /*can_listen_ipc=*/false);
    SetupQmlGuiArgs(args);
    const std::vector<std::string> argv{"bitcoinqml", "-regtest"};
    std::vector<const char*> raw_argv;
    raw_argv.reserve(argv.size());
    for (const std::string& arg : argv) raw_argv.push_back(arg.c_str());
    return args.ParseParameters(static_cast<int>(raw_argv.size()), raw_argv.data(), error);
}
} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size == 0) return 0;

    EnsureParamsSelected();

    FuzzArgsManager args;
    std::string error;
    if (!PrepareArgs(args, error)) return 0;

    const std::string config(reinterpret_cast<const char*>(data), size);

    (void)args.ReadConfigString(config);
    (void)args.GetUnrecognizedSections();

    std::istringstream stream(config);
    std::string stream_error;
    const bool ignore_invalid = (data[0] & 1) != 0;
    (void)args.ReadConfigStream(stream, "fuzz.conf", stream_error, ignore_invalid);

    return 0;
}
