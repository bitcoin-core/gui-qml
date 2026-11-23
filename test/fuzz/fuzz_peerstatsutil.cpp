// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/peerstatsutil.h>

#include <QByteArray>
#include <QApplication>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_peerstatsutil";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 8) return 0;

    uint64_t bytes = 0;
    int64_t offset = 0;
    for (size_t i = 0; i < 8; ++i) {
        bytes = (bytes << 8) | data[i];
        offset = (offset << 8) | static_cast<signed char>(data[i]);
    }

    (void)PeerStatsUtil::FormatBytes(bytes);
    (void)PeerStatsUtil::FormatTimeOffset(offset);
    (void)PeerStatsUtil::FormatPingTime(std::chrono::duration_cast<NodeClock::duration>(std::chrono::nanoseconds(offset)));
    (void)PeerStatsUtil::FormatDurationStr(std::chrono::nanoseconds(offset));
    return 0;
}
