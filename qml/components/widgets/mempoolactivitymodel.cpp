// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/mempoolactivitymodel.h>

#include <QDateTime>
#include <QVariantMap>

#include <cmath>
#include <utility>

namespace {
constexpr int SAMPLE_MS{5000};
constexpr qint64 HISTORY_MS{2 * 60 * 60 * 1000};
constexpr int MAX_SAMPLES{1440};
}

MempoolActivityModel::MempoolActivityModel(SampleFn sample, double baseline, NowFn now, QObject* parent)
    : QObject(parent), m_sample(std::move(sample)),
      m_now(now ? std::move(now) : NowFn{QDateTime::currentMSecsSinceEpoch}),
      m_baseline(baseline), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName("qml-incoming");
    m_thread.start();
    m_timer.setInterval(SAMPLE_MS);
    connect(&m_timer, &QTimer::timeout, this, &MempoolActivityModel::refresh);
}

MempoolActivityModel::~MempoolActivityModel()
{
    m_timer.stop();
    m_thread.quit();
    m_thread.wait();
}

QVariantList MempoolActivityModel::history() const
{
    QVariantList result;
    result.reserve(m_history.size());
    for (const auto& point : m_history) result.append(QVariantMap{{"time", point.time}, {"rate", point.rate}});
    return result;
}

void MempoolActivityModel::setReady(bool ready)
{
    if (m_ready == ready) return;
    m_ready = ready;
    ++m_generation;
    m_has_previous = false;
    if (ready) {
        refresh();
        m_timer.start();
    } else {
        m_timer.stop();
        QMetaObject::invokeMethod(m_worker, [] {}, Qt::BlockingQueuedConnection);
        m_minimum_fee = -1;
        m_incoming_rate = -1;
        m_history.clear();
        Q_EMIT snapshotChanged();
        Q_EMIT historyChanged();
    }
    Q_EMIT readyChanged();
}

void MempoolActivityModel::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    Q_EMIT activeChanged();
    if (active) {
        Q_EMIT historyChanged();
        refresh();
    }
}

void MempoolActivityModel::refresh()
{
    if (!m_ready || m_pending) return;
    if (m_active) Q_EMIT statsRefreshRequested();
    m_pending = true;
    Q_EMIT pendingChanged();
    QMetaObject::invokeMethod(m_worker, [this, generation = m_generation] {
        const auto snapshot = m_sample();
        const auto time = m_now();
        QMetaObject::invokeMethod(this, [this, generation, snapshot, time] {
            m_pending = false;
            Q_EMIT pendingChanged();
            if (generation != m_generation) { refresh(); return; }
            applySnapshot(snapshot, time);
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MempoolActivityModel::applySnapshot(const Snapshot& snapshot, qint64 time)
{
    if (!snapshot.loaded) {
        m_has_previous = false;
        return;
    }
    m_minimum_fee = std::isfinite(snapshot.minimum_fee) && snapshot.minimum_fee >= 0 ? snapshot.minimum_fee : -1;
    if (m_has_previous) {
        const qint64 elapsed = time - m_previous_time;
        if (elapsed == 0) { Q_EMIT snapshotChanged(); return; }
        if (elapsed < 0 || snapshot.incoming_vbytes < m_previous_vbytes) {
            m_history.clear(); // Clock/counter reset: start a new observation window.
            m_incoming_rate = -1;
        } else if (elapsed > 0) {
            // Suspend/stalled sampling is a gap, not an invented long-term average.
            m_incoming_rate = elapsed > SAMPLE_MS * 3 ? -1 : (snapshot.incoming_vbytes - m_previous_vbytes) * 1000.0 / elapsed;
            m_history.append({time, m_incoming_rate});
        }
    }
    m_previous_time = time;
    m_previous_vbytes = snapshot.incoming_vbytes;
    m_has_previous = true;
    m_history.removeIf([&](const auto& point) { return point.time < time - HISTORY_MS; });
    while (m_history.size() > MAX_SAMPLES) m_history.removeFirst();
    Q_EMIT snapshotChanged();
    if (m_active) Q_EMIT historyChanged();
}
