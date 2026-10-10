// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H
#define BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H

#include <atomic>
#include <functional>
#include <memory>

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QTimer>

class BlockClockHistory;

/**
 * Raw data represented by the block clock.
 *
 * The dial is a twelve-hour clock face whose origin is the most recent local
 * midnight or noon. It shows the blocks of the last twelve hours at the time
 * they arrived. Timestamps are Unix seconds. Block timestamps are sorted and
 * restricted to those at or after current_time - PERIOD_SECONDS; timestamps
 * ahead of the local clock are kept. Equal timestamps are kept: distinct
 * blocks can share a timestamp and each contributes a confirmation.
 */
struct BlockClockTimeline
{
    qint64 period_start{0};
    qint64 current_time{0};
    QList<qint64> block_timestamps;
};

/**
 * Maintains block-clock time and history independently of its presentation.
 *
 * This model remains current while the dial is hidden. currentTimeFraction is
 * refreshed at minute boundaries and when a block arrives. It is separate
 * from blockTimeFractions so a clock tick only republishes the block history
 * when a block leaves the twelve-hour window or the period origin moves.
 *
 * Fractions are measured from periodStart in units of twelve hours.
 * currentTimeFraction is in [0, 1]. blockTimeFractions are chronological and
 * lie in [currentTimeFraction - 1, currentTimeFraction]: negative values are
 * blocks from the previous period that are still within the last twelve
 * hours, and timestamps ahead of the local clock are clamped to the current
 * time. An empty blockTimeFractions list is valid: it means the active chain
 * contains no blocks timestamped in the last twelve hours, not that
 * synchronization is incomplete. Initial-sync state belongs to NodeModel.
 *
 * lastBlockAge is the number of seconds since the newest cached block
 * timestamp, or -1 when the cache holds none (for example during initial
 * sync, when the tip is older than the retained history).
 *
 * History comes from a local notification cache. A callback only schedules
 * one pending GUI refresh; all presentation work and the minute timer run on
 * this object's thread. This model never calls the node or chain interfaces.
 * Block updates wake the GUI even while the clock is hidden, without polling.
 */
class BlockClockModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 periodStart READ periodStart NOTIFY periodChanged)
    Q_PROPERTY(qreal currentTimeFraction READ currentTimeFraction NOTIFY currentTimeFractionChanged)
    Q_PROPERTY(QList<qreal> blockTimeFractions READ blockTimeFractions NOTIFY blockTimeFractionsChanged)
    Q_PROPERTY(qint64 lastBlockAge READ lastBlockAge NOTIFY lastBlockAgeChanged)

public:
    static constexpr qint64 PERIOD_SECONDS{12 * 60 * 60};
    static constexpr int CLOCK_UPDATE_INTERVAL_MS{60 * 1000};
    using CurrentTimeProvider = std::function<QDateTime()>;

    explicit BlockClockModel(std::shared_ptr<BlockClockHistory> history = {}, bool start_timer = true,
                             CurrentTimeProvider current_time_provider = {}, QObject* parent = nullptr);
    ~BlockClockModel() override;

    qint64 periodStart() const { return m_timeline.period_start; }
    qreal currentTimeFraction() const { return m_current_time_fraction; }
    QList<qreal> blockTimeFractions() const { return m_block_time_fractions; }
    qint64 lastBlockAge() const { return m_last_block_age; }
    bool timerActive() const { return m_clock_timer.isActive(); }

    /** Return the midnight/noon boundary containing @p current_time. */
    static qint64 PeriodStartFor(const QDateTime& current_time);

    /** Update the scalar clock position. Public to allow deterministic tests. */
    void updateCurrentTime(const QDateTime& current_time);

public Q_SLOTS:
    void stop();

    /** Consume coalesced block notifications without querying the backend. */
    void refreshHistory();

Q_SIGNALS:
    void periodChanged();
    void currentTimeFractionChanged();
    void blockTimeFractionsChanged();
    void lastBlockAgeChanged();

private:
    /** Called only by the cache under its mutex; stop() detaches before destruction. */
    void scheduleHistoryRefresh();
    void scheduleNextClockUpdate(const QDateTime& current_time);
    void replaceBlockHistory(QList<qint64> block_timestamps);
    void rebuildBlockTimeFractions();
    void updateLastBlockAge();
    qreal fractionForTimestamp(qint64 timestamp) const;

    std::shared_ptr<BlockClockHistory> m_history;
    CurrentTimeProvider m_current_time_provider;
    QTimer m_clock_timer;
    std::atomic_bool m_history_refresh_pending{false};
    QList<qint64> m_cached_block_timestamps;
    BlockClockTimeline m_timeline;
    qreal m_current_time_fraction{0.0};
    QList<qreal> m_block_time_fractions;
    qint64 m_last_block_age{-1};
    bool m_stopped{false};
};

#endif // BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H
