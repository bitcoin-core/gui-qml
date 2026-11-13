// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/onboarding_storage.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>

static int argc = 1;
static char arg0[] = "fuzz_onboarding_storage";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (size < 32) return 0;

    QmlOnboardingStorage::State state;
    state.check_pending = data[0] & 1;
    state.result_valid = data[1] & 1;
    uint64_t avail = 0;
    for (int i = 0; i < 8; ++i) avail = (avail << 8) | data[2 + i];
    state.available_bytes = avail;
    state.assumed_blockchain_size_gb = static_cast<int>(data[10]);
    state.assumed_chainstate_size_gb = static_cast<int>(data[11]);
    state.prune = data[12] & 1;
    state.prune_size_gb = static_cast<int>(data[13]);
    state.existing_profile = data[14] & 1;

    (void)QmlOnboardingStorage::Evaluate(state);
    (void)QmlOnboardingStorage::ShouldRecommendPrune(state);
    return 0;
}
