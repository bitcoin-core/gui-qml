// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_SOURCEEPOCH_H
#define BITCOIN_QML_SOURCEEPOCH_H
#include <atomic>
#include <cstdint>
#include <limits>

/** Value-only invalidation ingress. Captured readers own neither its producer
 * nor a backend lease. Exhaustion closes the capability instead of wrapping. */
struct SourceEpoch {
    std::atomic<uint64_t> value{0};
    std::atomic_bool closed{false};
    void advance()
    {
        auto old = value.load();
        do {
            if (old == std::numeric_limits<uint64_t>::max()) { closed.store(true); return; }
        } while (!value.compare_exchange_weak(old, old + 1));
    }
    bool current(uint64_t expected) const { return !closed.load() && value.load() == expected; }
};
#endif // BITCOIN_QML_SOURCEEPOCH_H
