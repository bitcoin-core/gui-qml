// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/blockclockmodel.h>

#include <qml/models/blockclockhistory.h>

#include <algorithm>
#include <utility>

#include <QMetaObject>
#include <QTime>

BlockClockModel::BlockClockModel(std::shared_ptr<BlockClockHistory> history, bool start_timer,
                                 CurrentTimeProvider current_time_provider, QObject* parent)
    : QObject{parent},
      m_history{history ? std::move(history) : std::make_shared<BlockClockHistory>()},
      m_current_time_provider{std::move(current_time_provider)},
      m_clock_timer{this}
{
    if (!m_current_time_provider) {
        m_current_time_provider = [] { return QDateTime::currentDateTime(); };
    }

    m_clock_timer.setSingleShot(true);
    m_clock_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_clock_timer, &QTimer::timeout, this, [this] {
        const QDateTime current_time{m_current_time_provider()};
        updateCurrentTime(current_time);
        scheduleNextClockUpdate(current_time);
    });
    const QDateTime current_time{m_current_time_provider()};
    updateCurrentTime(current_time);
    if (start_timer) scheduleNextClockUpdate(current_time);
    m_history->setChangedCallback([this] { scheduleHistoryRefresh(); });
}

BlockClockModel::~BlockClockModel()
{
    stop();
}

void BlockClockModel::stop()
{
    if (m_stopped) return;
    m_stopped = true;
    // The cache invokes this callback under its mutex. Detaching therefore
    // finishes any in-flight event posting before this QObject can be destroyed.
    m_history->setChangedCallback({});
    m_clock_timer.stop();
}

void BlockClockModel::scheduleHistoryRefresh()
{
    if (m_history_refresh_pending.exchange(true)) return;
    QMetaObject::invokeMethod(this, [this] {
        // Changes arriving during consumption schedule the next refresh.
        m_history_refresh_pending = false;
        refreshHistory();
    }, Qt::QueuedConnection);
}

qint64 BlockClockModel::PeriodStartFor(const QDateTime& current_time)
{
    const qint64 seconds_since_midnight{current_time.time().msecsSinceStartOfDay() / 1000};
    return current_time.toSecsSinceEpoch() - (seconds_since_midnight % PERIOD_SECONDS);
}

void BlockClockModel::updateCurrentTime(const QDateTime& current_time)
{
    if (m_stopped) return;
    const qint64 period_start{PeriodStartFor(current_time)};
    if (m_timeline.period_start != period_start) {
        m_timeline.period_start = period_start;
        // Keep the previous period for ordinary backwards clock/DST changes,
        // and retain future timestamps so rollover needs no backend reads.
        m_history->setRetentionStart(period_start - PERIOD_SECONDS);
        if (auto snapshot = m_history->takeSnapshot()) m_cached_block_timestamps = std::move(*snapshot);
        replaceBlockHistory(m_cached_block_timestamps, /*period_changed=*/true);
        Q_EMIT periodChanged();
    }

    const qreal fraction{fractionForTimestamp(current_time.toSecsSinceEpoch())};
    if (!qFuzzyCompare(m_current_time_fraction + 1.0, fraction + 1.0)) {
        m_current_time_fraction = fraction;
        Q_EMIT currentTimeFractionChanged();
    }
}

void BlockClockModel::scheduleNextClockUpdate(const QDateTime& current_time)
{
    if (m_stopped) return;
    const QTime time{current_time.time()};
    const int milliseconds_into_minute{time.second() * 1000 + time.msec()};
    m_clock_timer.start(CLOCK_UPDATE_INTERVAL_MS - milliseconds_into_minute);
}

void BlockClockModel::refreshHistory()
{
    if (m_stopped) return;
    auto snapshot{m_history->takeSnapshot()};
    if (!snapshot) return;
    m_cached_block_timestamps = std::move(*snapshot);
    updateCurrentTime(m_current_time_provider());
    replaceBlockHistory(m_cached_block_timestamps);
}

void BlockClockModel::replaceBlockHistory(QList<qint64> block_timestamps, bool period_changed)
{
    const qint64 period_end{m_timeline.period_start + PERIOD_SECONDS};
    std::sort(block_timestamps.begin(), block_timestamps.end());
    block_timestamps.erase(std::remove_if(block_timestamps.begin(), block_timestamps.end(), [this, period_end](qint64 timestamp) {
        return timestamp < m_timeline.period_start || timestamp >= period_end;
    }), block_timestamps.end());

    if (m_timeline.block_timestamps == block_timestamps && !period_changed) return;
    const auto previous_fractions{m_block_time_fractions};
    m_timeline.block_timestamps = std::move(block_timestamps);
    rebuildBlockTimeFractions();
    if (previous_fractions != m_block_time_fractions) Q_EMIT blockTimeFractionsChanged();
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
