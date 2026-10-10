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
    m_timeline.current_time = current_time.toSecsSinceEpoch();
    const qint64 period_start{PeriodStartFor(current_time)};
    const bool period_changed{m_timeline.period_start != period_start};
    if (period_changed) {
        m_timeline.period_start = period_start;
        // Keep the previous period, which covers the twelve-hour window and
        // ordinary backwards clock/DST changes, and retain future timestamps
        // so rollover needs no backend reads.
        m_history->setRetentionStart(period_start - PERIOD_SECONDS);
        if (auto snapshot = m_history->takeSnapshot()) m_cached_block_timestamps = std::move(*snapshot);
    }

    const qreal fraction{std::clamp<qreal>(
        static_cast<qreal>(m_timeline.current_time - m_timeline.period_start) / PERIOD_SECONDS, 0.0, 1.0)};
    if (!qFuzzyCompare(m_current_time_fraction + 1.0, fraction + 1.0)) {
        m_current_time_fraction = fraction;
        Q_EMIT currentTimeFractionChanged();
    }

    // The window slides with the clock: blocks older than twelve hours leave
    // the dial without a backend read. Unchanged fractions are not republished.
    replaceBlockHistory(m_cached_block_timestamps);
    if (period_changed) Q_EMIT periodChanged();
    updateLastBlockAge();
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
    // Refiltering the window also republishes the new history if it changed.
    updateCurrentTime(m_current_time_provider());
}

void BlockClockModel::replaceBlockHistory(QList<qint64> block_timestamps)
{
    const qint64 window_start{m_timeline.current_time - PERIOD_SECONDS};
    std::sort(block_timestamps.begin(), block_timestamps.end());
    block_timestamps.erase(std::remove_if(block_timestamps.begin(), block_timestamps.end(), [window_start](qint64 timestamp) {
        return timestamp < window_start;
    }), block_timestamps.end());

    // Fractions can move without the list changing: a new period origin, or a
    // future-dated block that is clamped to the moving current time. A few
    // hundred fractions are cheap to rebuild; only real changes are published.
    const auto previous_fractions{m_block_time_fractions};
    m_timeline.block_timestamps = std::move(block_timestamps);
    rebuildBlockTimeFractions();
    if (previous_fractions != m_block_time_fractions) Q_EMIT blockTimeFractionsChanged();
}

void BlockClockModel::updateLastBlockAge()
{
    qint64 age{-1};
    if (!m_cached_block_timestamps.isEmpty()) {
        const qint64 newest{*std::max_element(m_cached_block_timestamps.cbegin(), m_cached_block_timestamps.cend())};
        age = std::max<qint64>(0, m_timeline.current_time - newest);
    }
    if (age == m_last_block_age) return;
    m_last_block_age = age;
    Q_EMIT lastBlockAgeChanged();
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
        m_current_time_fraction - 1.0,
        m_current_time_fraction);
}
