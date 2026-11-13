// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/rpccommandexecutor.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>

static int argc = 1;
static char arg0[] = "fuzz_rpc_command_executor";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const std::string cmd(raw.constData(), raw.size());

    std::string result;
    std::string filtered;
    (void)RpcCommandExecutor::RPCParseCommandLine(nullptr, result, cmd, false, &filtered);
    return 0;
}
