// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/networktraffictower.h>

#include <interfaces/node.h>
#include <util/threadnames.h>

#include <algorithm>
#include <cstdint>
#include <functional>
#include <utility>

#include <QObject>
#include <QThread>
#include <QTimer>

namespace {
constexpr int MAX_SAMPLES{86400};
constexpr int DEFAULT_FILTER_WINDOW_SIZE{30};

struct TrafficSnapshot {
    quint64 generation{0};
    quint64 total_bytes_received{0};
    quint64 total_bytes_sent{0};
    float max_received_rate_bps{0.0f};
    float max_sent_rate_bps{0.0f};
    QQueue<float> received_rate_list;
    QQueue<float> sent_rate_list;
};

quint64 NonNegativeTotal(int64_t total)
{
    return total > 0 ? static_cast<quint64>(total) : 0;
}

float RateDelta(quint64 current, quint64 previous)
{
    return current >= previous ? static_cast<float>(current - previous) : 0.0f;
}
} // namespace

class NetworkTrafficWorker : public QObject
{
public:
    using PublishFn = std::function<void(TrafficSnapshot)>;

    NetworkTrafficWorker(interfaces::Node& node, int sample_interval_ms, PublishFn publish)
        : m_node{node}
        , m_publish{std::move(publish)}
        , m_timer{new QTimer(this)}
    {
        m_timer->setInterval(std::max(1, sample_interval_ms));
        connect(m_timer, &QTimer::timeout, this, [this] {
            sample();
        });
    }

    void start()
    {
        util::ThreadRename("qml-netstats");
        sample();
        m_timer->start();
    }

    void stop()
    {
        m_timer->stop();
    }

    void setActive(bool active, quint64 generation)
    {
        m_active = active;
        m_generation = generation;
        if (!m_active) {
            // Raw samples continue in the background. Derived history is rebuilt
            // on the worker when the page becomes active again.
            m_smoothed_history_valid = false;
            return;
        }

        ensureSmoothedHistory();
        publishSnapshot();
    }

    void setFilterWindowSize(int window_size)
    {
        const int clamped_size{std::clamp(window_size, 1, MAX_SAMPLES)};
        if (m_filter_window_size == clamped_size) return;

        m_filter_window_size = clamped_size;
        m_smoothed_history_valid = false;
        if (m_active) {
            ensureSmoothedHistory();
            publishSnapshot();
        }
    }

private:
    float currentMovingAverage(const QQueue<float>& rates) const
    {
        const qsizetype count{std::min<qsizetype>(rates.size(), m_filter_window_size)};
        if (count == 0) return 0.0f;

        double sum{0.0};
        for (qsizetype i = 0; i < count; ++i) {
            sum += rates.at(i);
        }
        return static_cast<float>(sum / count);
    }

    QQueue<float> calculateSmoothedHistory(const QQueue<float>& rates) const
    {
        QQueue<float> smoothed;
        if (rates.isEmpty()) return smoothed;

        smoothed.reserve(rates.size());
        const qsizetype window_size{std::min<qsizetype>(rates.size(), m_filter_window_size)};
        double window_sum{0.0};
        for (qsizetype i = 0; i < window_size; ++i) {
            window_sum += rates.at(i);
        }

        for (qsizetype i = 0; i < rates.size(); ++i) {
            const qsizetype sample_count{std::min<qsizetype>(window_size, rates.size() - i)};
            smoothed.push_back(static_cast<float>(window_sum / sample_count));

            window_sum -= rates.at(i);
            const qsizetype incoming_index{i + window_size};
            if (incoming_index < rates.size()) {
                window_sum += rates.at(incoming_index);
            }
        }
        return smoothed;
    }

    float calculateMaxRate(const QQueue<float>& smoothed_rates) const
    {
        const qsizetype lookback{std::min<qsizetype>(smoothed_rates.size(), m_filter_window_size * 10)};
        float max_rate{0.0f};
        for (qsizetype i = 0; i < lookback; ++i) {
            max_rate = std::max(max_rate, smoothed_rates.at(i));
        }
        return max_rate;
    }

    QQueue<float> visibleHistory(const QQueue<float>& smoothed_rates) const
    {
        const qsizetype sample_count{std::min<qsizetype>(smoothed_rates.size(), m_filter_window_size * 10)};
        QQueue<float> visible_rates;
        visible_rates.reserve(sample_count);
        for (qsizetype i = 0; i < sample_count; ++i) {
            visible_rates.push_back(smoothed_rates.at(i));
        }
        return visible_rates;
    }

    void ensureSmoothedHistory()
    {
        if (m_smoothed_history_valid) return;

        m_smoothed_received_rate_list = calculateSmoothedHistory(m_received_rate_list);
        m_smoothed_sent_rate_list = calculateSmoothedHistory(m_sent_rate_list);
        m_max_received_rate_bps = calculateMaxRate(m_smoothed_received_rate_list);
        m_max_sent_rate_bps = calculateMaxRate(m_smoothed_sent_rate_list);
        m_smoothed_history_valid = true;
    }

    void appendSmoothedSample()
    {
        m_smoothed_received_rate_list.push_front(currentMovingAverage(m_received_rate_list));
        m_smoothed_sent_rate_list.push_front(currentMovingAverage(m_sent_rate_list));
        while (m_smoothed_received_rate_list.size() > m_received_rate_list.size()) {
            m_smoothed_received_rate_list.pop_back();
        }
        while (m_smoothed_sent_rate_list.size() > m_sent_rate_list.size()) {
            m_smoothed_sent_rate_list.pop_back();
        }
        m_max_received_rate_bps = calculateMaxRate(m_smoothed_received_rate_list);
        m_max_sent_rate_bps = calculateMaxRate(m_smoothed_sent_rate_list);
    }

    void sample()
    {
        const quint64 total_received{NonNegativeTotal(m_node.getTotalBytesRecv())};
        const quint64 total_sent{NonNegativeTotal(m_node.getTotalBytesSent())};

        if (!m_has_baseline) {
            m_has_baseline = true;
            m_previous_total_bytes_received = total_received;
            m_previous_total_bytes_sent = total_sent;
            m_total_bytes_received = total_received;
            m_total_bytes_sent = total_sent;
            if (m_active) publishSnapshot();
            return;
        }

        m_received_rate_list.push_front(RateDelta(total_received, m_previous_total_bytes_received));
        m_sent_rate_list.push_front(RateDelta(total_sent, m_previous_total_bytes_sent));
        m_previous_total_bytes_received = total_received;
        m_previous_total_bytes_sent = total_sent;
        m_total_bytes_received = total_received;
        m_total_bytes_sent = total_sent;

        while (m_received_rate_list.size() > MAX_SAMPLES) {
            m_received_rate_list.pop_back();
        }
        while (m_sent_rate_list.size() > MAX_SAMPLES) {
            m_sent_rate_list.pop_back();
        }

        if (!m_active) {
            m_smoothed_history_valid = false;
            return;
        }

        if (m_smoothed_history_valid) {
            appendSmoothedSample();
        } else {
            ensureSmoothedHistory();
        }
        publishSnapshot();
    }

    void publishSnapshot()
    {
        if (!m_active) return;

        m_publish(TrafficSnapshot{
            m_generation,
            m_total_bytes_received,
            m_total_bytes_sent,
            m_max_received_rate_bps,
            m_max_sent_rate_bps,
            visibleHistory(m_smoothed_received_rate_list),
            visibleHistory(m_smoothed_sent_rate_list),
        });
    }

    interfaces::Node& m_node;
    PublishFn m_publish;
    QTimer* m_timer;
    bool m_active{false};
    bool m_has_baseline{false};
    bool m_smoothed_history_valid{false};
    quint64 m_generation{0};
    quint64 m_previous_total_bytes_received{0};
    quint64 m_previous_total_bytes_sent{0};
    quint64 m_total_bytes_received{0};
    quint64 m_total_bytes_sent{0};
    int m_filter_window_size{DEFAULT_FILTER_WINDOW_SIZE};
    float m_max_received_rate_bps{0.0f};
    float m_max_sent_rate_bps{0.0f};
    QQueue<float> m_received_rate_list;
    QQueue<float> m_smoothed_received_rate_list;
    QQueue<float> m_sent_rate_list;
    QQueue<float> m_smoothed_sent_rate_list;
};

NetworkTrafficTower::NetworkTrafficTower(interfaces::Node& node, int sample_interval_ms)
    : m_worker_thread{new QThread(this)}
{
    m_worker = new NetworkTrafficWorker(node, sample_interval_ms, [this](TrafficSnapshot snapshot) {
        QMetaObject::invokeMethod(this, [this, snapshot = std::move(snapshot)]() mutable {
            if (!m_active || snapshot.generation != m_activation_generation) return;

            if (m_total_bytes_received != snapshot.total_bytes_received) {
                m_total_bytes_received = snapshot.total_bytes_received;
                Q_EMIT totalBytesReceivedChanged();
            }
            if (m_total_bytes_sent != snapshot.total_bytes_sent) {
                m_total_bytes_sent = snapshot.total_bytes_sent;
                Q_EMIT totalBytesSentChanged();
            }
            if (m_max_received_rate_bps != snapshot.max_received_rate_bps) {
                m_max_received_rate_bps = snapshot.max_received_rate_bps;
                Q_EMIT maxReceivedRateBpsChanged();
            }
            if (m_max_sent_rate_bps != snapshot.max_sent_rate_bps) {
                m_max_sent_rate_bps = snapshot.max_sent_rate_bps;
                Q_EMIT maxSentRateBpsChanged();
            }

            m_received_rate_list = std::move(snapshot.received_rate_list);
            m_sent_rate_list = std::move(snapshot.sent_rate_list);
            Q_EMIT receivedRateListChanged();
            Q_EMIT sentRateListChanged();
        }, Qt::QueuedConnection);
    });
    m_worker->moveToThread(m_worker_thread);
    connect(m_worker_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_worker_thread->start();
    QMetaObject::invokeMethod(m_worker, [worker = m_worker] {
        worker->start();
    }, Qt::QueuedConnection);
}

NetworkTrafficTower::~NetworkTrafficTower()
{
    stop();
}

void NetworkTrafficTower::stop()
{
    if (!m_worker_thread || !m_worker_thread->isRunning()) return;

    m_active = false;
    ++m_activation_generation;
    Q_EMIT activeChanged();

    QMetaObject::invokeMethod(m_worker, [worker = m_worker] {
        worker->stop();
    }, Qt::BlockingQueuedConnection);
    m_worker_thread->quit();
    m_worker_thread->wait();
    m_worker = nullptr;
}

void NetworkTrafficTower::setActive(bool active)
{
    if (!m_worker) return;
    if (m_active == active) return;

    m_active = active;
    ++m_activation_generation;
    Q_EMIT activeChanged();

    QMetaObject::invokeMethod(m_worker, [worker = m_worker, active, generation = m_activation_generation] {
        worker->setActive(active, generation);
    }, Qt::QueuedConnection);
}

void NetworkTrafficTower::updateFilterWindowSize(int new_size)
{
    if (!m_worker) return;
    QMetaObject::invokeMethod(m_worker, [worker = m_worker, new_size] {
        worker->setFilterWindowSize(new_size);
    }, Qt::QueuedConnection);
}
