// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/syncprogresstracker.h>

#include <QByteArray>
#include <QApplication>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_syncprogresstracker";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 16) return 0;

    SyncProgressTracker tracker;
    size_t i = 0;
    while (i + 16 <= size) {
        int64_t ms = 0;
        double progress = 0.0;
        for (int j = 0; j < 8; ++j) ms = (ms << 8) | data[i + j];
        // interpret next 8 bytes as double bits
        uint64_t bits = 0;
        for (int j = 0; j < 8; ++j) bits = (bits << 8) | data[i + 8 + j];
        progress = *reinterpret_cast<double*>(&bits);
        (void)tracker.addSample(ms, progress);
        i += 16;
    }
    tracker.reset();
    return 0;
}
