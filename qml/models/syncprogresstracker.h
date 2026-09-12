// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SYNCPROGRESSTRACKER_H
#define BITCOIN_QML_MODELS_SYNCPROGRESSTRACKER_H

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>

/**
 * Bounded, amortized-O(1) estimator for remaining block synchronization time.
 *
 * Sample times are monotonic milliseconds, not wall-clock timestamps. The
 * tracker keeps one sample at the beginning of a rolling window and publishes
 * only periodically, avoiding a vector shift and linear scan for every block.
 */
class SyncProgressTracker
{
public:
    static constexpr int64_t WINDOW_MILLISECONDS{500'000};
    static constexpr std::size_t MAX_SAMPLES{5'000};
    static constexpr std::size_t PUBLISH_SAMPLE_INTERVAL{1'000};

    std::optional<int64_t> addSample(int64_t monotonic_milliseconds, double progress);
    void reset();
    std::size_t sampleCount() const { return m_samples.size(); }

private:
    struct Sample {
        int64_t milliseconds;
        double progress;
    };

    std::deque<Sample> m_samples;
    std::size_t m_samples_since_publication{0};
};

#endif // BITCOIN_QML_MODELS_SYNCPROGRESSTRACKER_H
