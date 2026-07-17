// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H
#define BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H

#include <functional>

#include <QDateTime>
#include <QList>
#include <QObject>
#include <QTimer>

namespace interfaces {
class Chain;
}

/**
 * Raw data represented by the block clock.
 *
 * The dial covers one local twelve-hour period, beginning at midnight or
 * noon. Timestamps are Unix seconds. Block timestamps are sorted, unique, and
 * restricted to [period_start, period_start + PERIOD_SECONDS).
 */
struct BlockClockTimeline
{
    qint64 period_start{0};
    QList<qint64> block_timestamps;
};

/**
 * Maintains block-clock time and history independently of its presentation.
 *
 * This model remains current while the dial is hidden. The once-per-second
 * currentTimeFraction signal is separate from blockTimeFractions so a clock
 * tick never republishes the full block history. Fractions are normalized to
 * the current twelve-hour period and are always in the range [0, 1].
 *
 * All methods and the owned timer run on this object's thread (the GUI thread
 * in production). History loading is infrequent: once after node
 * initialization and once when the local clock crosses midnight or noon.
 */
class BlockClockModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 periodStart READ periodStart NOTIFY periodChanged)
    Q_PROPERTY(qreal currentTimeFraction READ currentTimeFraction NOTIFY currentTimeFractionChanged)
    Q_PROPERTY(QList<qreal> blockTimeFractions READ blockTimeFractions NOTIFY blockTimeFractionsChanged)

public:
    static constexpr qint64 PERIOD_SECONDS{12 * 60 * 60};
    using HistoryLoader = std::function<QList<qint64>(qint64 period_start, qint64 period_end)>;

    explicit BlockClockModel(HistoryLoader history_loader = {}, bool start_timer = true, QObject* parent = nullptr);

    qint64 periodStart() const { return m_timeline.period_start; }
    qreal currentTimeFraction() const { return m_current_time_fraction; }
    QList<qreal> blockTimeFractions() const { return m_block_time_fractions; }
    bool timerActive() const { return m_clock_timer.isActive(); }

    /** Return the midnight/noon boundary containing @p current_time. */
    static qint64 PeriodStartFor(const QDateTime& current_time);

    /** Update the scalar clock position. Public to allow deterministic tests. */
    void updateCurrentTime(const QDateTime& current_time);

public Q_SLOTS:
    /** Load the current period's active-chain block history. */
    void initializeHistory();

    /** Record a new active-chain block timestamp, if it belongs on this dial. */
    void recordBlockTime(qint64 block_timestamp);

Q_SIGNALS:
    void periodChanged();
    void currentTimeFractionChanged();
    void blockTimeFractionsChanged();

private:
    void loadHistory();
    void replaceBlockHistory(QList<qint64> block_timestamps);
    void rebuildBlockTimeFractions();
    qreal fractionForTimestamp(qint64 timestamp) const;

    HistoryLoader m_history_loader;
    QTimer m_clock_timer;
    BlockClockTimeline m_timeline;
    qreal m_current_time_fraction{0.0};
    QList<qreal> m_block_time_fractions;
    bool m_history_initialized{false};
};

/** Read active-chain block timestamps belonging to the requested dial period. */
QList<qint64> LoadBlockClockHistory(interfaces::Chain& chain, qint64 period_start, qint64 period_end);

#endif // BITCOIN_QML_MODELS_BLOCKCLOCKMODEL_H
