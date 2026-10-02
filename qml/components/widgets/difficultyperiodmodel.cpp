// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/difficultyperiodmodel.h>

#include <QDateTime>

#include <algorithm>
#include <cmath>
#include <utility>

DifficultyPeriodModel::DifficultyPeriodModel(SampleFn sample, NowFn now, QObject* parent)
    : QObject(parent), m_sample(std::move(sample)),
      m_now(now ? std::move(now) : NowFn{QDateTime::currentSecsSinceEpoch}), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName("qml-difficulty");
    m_thread.start();
    m_timer.setInterval(30000);
    connect(&m_timer, &QTimer::timeout, this, &DifficultyPeriodModel::refresh);
}

DifficultyPeriodModel::~DifficultyPeriodModel()
{
    m_timer.stop();
    m_thread.quit();
    m_thread.wait();
}

void DifficultyPeriodModel::setReady(bool ready)
{
    if (m_ready == ready) return;
    m_ready = ready;
    ++m_generation;
    if (!ready) {
        m_timer.stop();
        m_snapshot = {};
        Q_EMIT snapshotChanged();
    }
    Q_EMIT readyChanged();
    updatePolling();
}

void DifficultyPeriodModel::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    ++m_generation;
    Q_EMIT activeChanged();
    updatePolling();
}

void DifficultyPeriodModel::updatePolling()
{
    if (m_ready && m_active) {
        refresh();
        m_timer.start();
    } else {
        m_timer.stop();
    }
}

void DifficultyPeriodModel::refresh()
{
    if (!m_ready || !m_active || m_pending) return;
    m_pending = true;
    Q_EMIT pendingChanged();
    QMetaObject::invokeMethod(m_worker, [this, generation = m_generation] {
        Snapshot snapshot;
        try {
            snapshot = m_sample();
        } catch (...) {
            // RPC failures (including UniValue errors) leave the widget unavailable.
        }
        const auto time = m_now();
        QMetaObject::invokeMethod(this, [this, generation, snapshot, time] {
            m_pending = false;
            Q_EMIT pendingChanged();
            if (generation != m_generation) { refresh(); return; }
            m_snapshot = snapshot.interval > 0 && snapshot.spacing > 0 ? snapshot : Snapshot{};
            m_time = time;
            Q_EMIT snapshotChanged();
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

double DifficultyPeriodModel::progress() const
{
    return available() ? double(m_snapshot.height % m_snapshot.interval) / m_snapshot.interval : 0;
}

int DifficultyPeriodModel::blocksLeft() const
{
    return available() ? m_snapshot.interval - m_snapshot.height % m_snapshot.interval : -1;
}

double DifficultyPeriodModel::averageBlockSeconds() const
{
    const int intervals = available() ? m_snapshot.height % m_snapshot.interval : 0;
    const auto elapsed = m_snapshot.tip_time - m_snapshot.start_time;
    return intervals > 0 && elapsed > 0 ? double(elapsed) / intervals : UNKNOWN;
}

double DifficultyPeriodModel::nextChange() const
{
    if (!available()) return UNKNOWN;
    if (m_snapshot.no_retargeting) return 0;
    // Testnet's special minimum-difficulty blocks need a different forecast.
    if (m_snapshot.min_difficulty_blocks) return UNKNOWN;
    const int intervals = m_snapshot.height % m_snapshot.interval;
    const auto elapsed = std::max(m_time, m_snapshot.tip_time) - m_snapshot.start_time;
    if (intervals == 0 || elapsed <= 0 || m_snapshot.current_target <= 0 || m_snapshot.pow_limit <= 0) return UNKNOWN;
    // Include the current block's wait in the projected pace, but use completed
    // block intervals for the separately displayed average. Bound the forecast
    // by Core's 4x retarget limits and the network's easiest permitted target.
    const double factor = std::clamp(double(elapsed) / intervals / m_snapshot.spacing, 0.25, 4.0);
    const double projected_target = std::min(m_snapshot.pow_limit, m_snapshot.current_target * factor);
    return (m_snapshot.current_target / projected_target - 1) * 100;
}

double DifficultyPeriodModel::previousChange() const
{
    if (!available() || m_snapshot.previous_target <= 0 || m_snapshot.current_target <= 0) return UNKNOWN;
    return (m_snapshot.previous_target / m_snapshot.current_target - 1) * 100;
}

void DifficultyPeriodModel::stopForShutdown()
{
    setReady(false);
    // A queued fence acknowledges all earlier samples without blocking input.
    QMetaObject::invokeMethod(m_worker, [this] {
        QMetaObject::invokeMethod(this, [this] { Q_EMIT shutdownFinished(); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
