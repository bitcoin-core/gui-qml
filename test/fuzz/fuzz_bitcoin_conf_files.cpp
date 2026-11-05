// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/guiargs.h>

#include <chainparams.h>
#include <common/args.h>
#include <init.h>
#include <util/fs.h>

#include <QApplication>
#include <QByteArray>
#include <QFile>
#include <QTemporaryDir>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace {
static int argc = 1;
static char arg0[] = "fuzz_bitcoin_conf_files";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

static bool params_selected = false;
static QTemporaryDir g_temp_dir;

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

    if (!g_temp_dir.isValid()) return 0;

    const QString conf_path = g_temp_dir.filePath(QStringLiteral("bitcoin.conf"));
    QFile file(conf_path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return 0;
    file.write(reinterpret_cast<const char*>(data), static_cast<qint64>(size));
    file.close();

    ArgsManager args;
    std::string error;
    if (!PrepareArgs(args, error)) return 0;
    args.SelectConfigNetwork(args.GetChainTypeString());
    args.SetConfigFilePath(std::filesystem::path(conf_path.toStdString()));

    const bool ignore_invalid = (data[0] & 1) != 0;
    (void)args.ReadConfigFiles(error, ignore_invalid);
    (void)args.GetUnrecognizedSections();

    return 0;
}
