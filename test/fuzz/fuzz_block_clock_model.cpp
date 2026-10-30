// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockclockmodel.h>

#include <QByteArray>
#include <QApplication>
#include <QDateTime>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_block_clock_model";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < sizeof(qint64)) return 0;

    qint64 timestamp = 0;
    for (size_t i = 0; i < sizeof(qint64); ++i) {
        timestamp = (timestamp << 8) | static_cast<uint8_t>(data[i]);
    }

    // Use the rest of the input as a pool of block timestamps.
    QList<qint64> block_timestamps;
    for (size_t i = sizeof(qint64); i + sizeof(qint64) <= size; i += sizeof(qint64)) {
        qint64 ts = 0;
        for (size_t j = 0; j < sizeof(qint64); ++j) {
            ts = (ts << 8) | static_cast<uint8_t>(data[i + j]);
        }
        block_timestamps.append(ts);
    }

    BlockClockModel model(
        [block_timestamps](qint64, qint64) { return block_timestamps; },
        /*start_timer=*/false,
        [timestamp]() { return QDateTime::fromSecsSinceEpoch(timestamp); });

    model.initializeHistory();
    model.recordBlockTime(timestamp);
    model.updateCurrentTime(QDateTime::fromSecsSinceEpoch(timestamp));
    (void)model.periodStart();
    (void)model.currentTimeFraction();
    (void)model.blockTimeFractions();
    model.stop();
    return 0;
}
