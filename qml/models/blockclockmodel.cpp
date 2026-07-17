// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockclockmodel.h>

#include <interfaces/chain.h>

#include <algorithm>
#include <optional>
#include <utility>

#include <QTime>

BlockClockModel::BlockClockModel(HistoryLoader history_loader, bool start_timer, QObject* parent)
    : QObject{parent},
      m_history_loader{std::move(history_loader)},
      m_clock_timer{this}
{
    m_clock_timer.setInterval(1000);
    connect(&m_clock_timer, &QTimer::timeout, this, [this] {
        updateCurrentTime(QDateTime::currentDateTime());
    });

    updateCurrentTime(QDateTime::currentDateTime());
    if (start_timer) m_clock_timer.start();
}

qint64 BlockClockModel::PeriodStartFor(const QDateTime& current_time)
{
    const qint64 seconds_since_midnight{current_time.time().msecsSinceStartOfDay() / 1000};
    return current_time.toSecsSinceEpoch() - (seconds_since_midnight % PERIOD_SECONDS);
}

void BlockClockModel::updateCurrentTime(const QDateTime& current_time)
{
    const qint64 period_start{PeriodStartFor(current_time)};
    if (m_timeline.period_start != period_start) {
        m_timeline.period_start = period_start;
        Q_EMIT periodChanged();

        if (m_history_initialized) {
            loadHistory();
        } else {
            replaceBlockHistory({});
        }
    }

    const qreal fraction{fractionForTimestamp(current_time.toSecsSinceEpoch())};
    if (!qFuzzyCompare(m_current_time_fraction + 1.0, fraction + 1.0)) {
        m_current_time_fraction = fraction;
        Q_EMIT currentTimeFractionChanged();
    }
}

void BlockClockModel::initializeHistory()
{
    m_history_initialized = true;
    loadHistory();
}

void BlockClockModel::recordBlockTime(qint64 block_timestamp)
{
    const qint64 period_end{m_timeline.period_start + PERIOD_SECONDS};
    if (block_timestamp < m_timeline.period_start || block_timestamp >= period_end) return;

    auto insert_position{std::lower_bound(m_timeline.block_timestamps.begin(), m_timeline.block_timestamps.end(), block_timestamp)};
    if (insert_position != m_timeline.block_timestamps.end() && *insert_position == block_timestamp) return;

    m_timeline.block_timestamps.insert(insert_position, block_timestamp);
    rebuildBlockTimeFractions();
    Q_EMIT blockTimeFractionsChanged();
}

void BlockClockModel::loadHistory()
{
    if (!m_history_loader) {
        replaceBlockHistory({});
        return;
    }

    replaceBlockHistory(m_history_loader(m_timeline.period_start, m_timeline.period_start + PERIOD_SECONDS));
}

void BlockClockModel::replaceBlockHistory(QList<qint64> block_timestamps)
{
    const qint64 period_end{m_timeline.period_start + PERIOD_SECONDS};
    std::sort(block_timestamps.begin(), block_timestamps.end());
    block_timestamps.erase(std::remove_if(block_timestamps.begin(), block_timestamps.end(), [this, period_end](qint64 timestamp) {
        return timestamp < m_timeline.period_start || timestamp >= period_end;
    }), block_timestamps.end());
    block_timestamps.erase(std::unique(block_timestamps.begin(), block_timestamps.end()), block_timestamps.end());

    if (m_timeline.block_timestamps == block_timestamps) return;
    m_timeline.block_timestamps = std::move(block_timestamps);
    rebuildBlockTimeFractions();
    Q_EMIT blockTimeFractionsChanged();
}

void BlockClockModel::rebuildBlockTimeFractions()
{
    m_block_time_fractions.clear();
    m_block_time_fractions.reserve(m_timeline.block_timestamps.size());
    for (qint64 timestamp : m_timeline.block_timestamps) {
        m_block_time_fractions.push_back(fractionForTimestamp(timestamp));
    }
}

qreal BlockClockModel::fractionForTimestamp(qint64 timestamp) const
{
    if (m_timeline.period_start == 0) return 0.0;
    return std::clamp<qreal>(
        static_cast<qreal>(timestamp - m_timeline.period_start) / PERIOD_SECONDS,
        0.0,
        1.0);
}

QList<qint64> LoadBlockClockHistory(interfaces::Chain& chain, qint64 period_start, qint64 period_end)
{
    QList<qint64> block_timestamps;
    const std::optional<int> active_height{chain.getHeight()};
    if (!active_height) return block_timestamps;

    int first_height{0};
    if (!chain.findFirstBlockWithTimeAndHeight(period_start, /*min_height=*/0, interfaces::FoundBlock{}.height(first_height))) {
        return block_timestamps;
    }

    for (int height{first_height}; height <= *active_height; ++height) {
        const uint256 block_hash{chain.getBlockHash(height)};
        int64_t block_time{0};
        if (!chain.findBlock(block_hash, interfaces::FoundBlock{}.time(block_time))) continue;
        if (block_time >= period_start && block_time < period_end) block_timestamps.push_back(block_time);
    }
    return block_timestamps;
}
