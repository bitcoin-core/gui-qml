// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <chainparams.h>
#include <key_io.h>
#include <util/chaintype.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_bech32";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    static bool params_selected = false;
    if (!params_selected) {
        SelectParams(ChainType::MAIN);
        params_selected = true;
    }

    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const std::string addr(raw.constData(), raw.size());

    const auto dest = DecodeDestination(addr);
    (void)IsValidDestination(dest);
    (void)EncodeDestination(dest);
    return 0;
}
