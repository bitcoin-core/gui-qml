// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/syncprogresstracker.h>

#include <cmath>
#include <limits>

std::optional<int64_t> SyncProgressTracker::addSample(int64_t monotonic_milliseconds, double progress)
{
    if (!std::isfinite(progress)) return std::nullopt;
    if (!m_samples.empty() &&
        (monotonic_milliseconds < m_samples.back().milliseconds || progress < m_samples.back().progress)) {
        reset();
    }

    m_samples.push_back({monotonic_milliseconds, progress});
    ++m_samples_since_publication;

    const int64_t cutoff{monotonic_milliseconds - WINDOW_MILLISECONDS};
    // Retain the newest sample at or before the cutoff as the baseline.
    while (m_samples.size() > 2 && m_samples[1].milliseconds <= cutoff) {
        m_samples.pop_front();
    }
    while (m_samples.size() > MAX_SAMPLES) m_samples.pop_front();

    if (m_samples_since_publication < PUBLISH_SAMPLE_INTERVAL || m_samples.size() < 2) {
        return std::nullopt;
    }
    m_samples_since_publication = 0;

    const Sample& oldest{m_samples.front()};
    const Sample& newest{m_samples.back()};
    const double progress_delta{newest.progress - oldest.progress};
    const int64_t time_delta{newest.milliseconds - oldest.milliseconds};
    if (progress_delta <= 0.0 || time_delta <= 0) return std::nullopt;

    const long double remaining{
        static_cast<long double>(1.0 - newest.progress) /
        progress_delta *
        static_cast<long double>(time_delta)};
    if (!std::isfinite(remaining) || remaining < 0.0L) return std::nullopt;
    if (remaining > std::numeric_limits<int64_t>::max()) return std::numeric_limits<int64_t>::max();
    return static_cast<int64_t>(remaining);
}

void SyncProgressTracker::reset()
{
    m_samples.clear();
    m_samples_since_publication = 0;
}
