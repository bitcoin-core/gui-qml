// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/networktraffictower.h>

#include <QObject>
#include <QThread>
#include <QTimer>

#define MAX_SAMPLES      86400

NetworkTrafficTower::NetworkTrafficTower(NodeModel& node)
    : m_node{node}
{
    QTimer* timer = new QTimer();
    connect(timer, &QTimer::timeout, this, &NetworkTrafficTower::updateTrafficStats);
    timer->start(1000);

    QThread* timer_thread = new QThread;
    timer->moveToThread(timer_thread);
    timer_thread->start();

    m_filter_window_size = 30;
}

void NetworkTrafficTower::setTotalBytesReceived(float new_total)
{
    m_total_bytes_received = new_total;
    Q_EMIT totalBytesReceivedChanged();
}

void NetworkTrafficTower::setTotalBytesSent(float new_total)
{
    m_total_bytes_sent = new_total;
    Q_EMIT totalBytesSentChanged();
}

void NetworkTrafficTower::setMaxReceivedRateBps(float new_max)
{
    m_max_received_rate_bps = new_max;
    Q_EMIT maxReceivedRateBpsChanged();
}

void NetworkTrafficTower::setMaxSentRateBps(float new_max)
{
    m_max_sent_rate_bps = new_max;
    Q_EMIT maxSentRateBpsChanged();
}

void NetworkTrafficTower::updateFilterWindowSize(int new_size)
{
    if (!(m_filter_window_size == new_size)) {
        m_filter_window_size = new_size;

        if (!m_received_rate_list.isEmpty()) {
            recalculateSmoothedRateList(&m_received_rate_list, &m_smoothed_received_rate_list);
            float new_max_received_rate = calculateMaxRateBps(&m_smoothed_received_rate_list);

            setMaxReceivedRateBps(new_max_received_rate);
            Q_EMIT receivedRateListChanged();
        }
        if (!m_sent_rate_list.isEmpty()) {
            recalculateSmoothedRateList(&m_sent_rate_list, &m_smoothed_sent_rate_list);
            float new_max_sent_rate = calculateMaxRateBps(&m_smoothed_sent_rate_list);

            setMaxSentRateBps(new_max_sent_rate);
            Q_EMIT sentRateListChanged();
        }
    }
    Q_EMIT sentRateListChanged();
}

float NetworkTrafficTower::applyMovingAverageFilter(QQueue<float> * rate_list)
{
    int filter_window_size = std::min(rate_list->size(), m_filter_window_size);
    float sum = 0.0f;
    for (int i = 0; i < filter_window_size; ++i) {
        sum += rate_list->at(i);
    }

    return sum / filter_window_size;
}

float NetworkTrafficTower::calculateMaxRateBps(QQueue<float> * smoothed_rate_list)
{
    float max_rate_bps = 0.0f;
    int lookback = std::min(smoothed_rate_list->size() - 1, m_filter_window_size * 10);
    for (int i = lookback; i > 0; --i) {
        if (smoothed_rate_list->at(i) > max_rate_bps) {
            max_rate_bps = smoothed_rate_list->at(i);
        }
    }
    return max_rate_bps;
}

void NetworkTrafficTower::recalculateSmoothedRateList(QQueue<float> * rate_list, QQueue<float> * smoothed_rate_list)
{
    smoothed_rate_list->clear();
    QQueue<float> temp_list;
    for (int i = rate_list->size() - 1; i > 0; --i) {
        temp_list.push_front(rate_list->at(i));
        float smoothed_rate = applyMovingAverageFilter(&temp_list);
        smoothed_rate_list->push_front(smoothed_rate);
    }
}

void NetworkTrafficTower::updateTrafficStats()
{
    float new_total_bytes_received = m_node.getTotalBytesReceived();
    float new_total_bytes_sent = m_node.getTotalBytesSent();

    float rate_received_bps = (new_total_bytes_received - m_total_bytes_received);
    float rate_sent_bps = (new_total_bytes_sent - m_total_bytes_sent);

    setTotalBytesSent(new_total_bytes_sent);
    setTotalBytesReceived(new_total_bytes_received);

    m_received_rate_list.push_front(rate_received_bps);
    m_sent_rate_list.push_front(rate_sent_bps);

    float smoothed_received_rate_bps = applyMovingAverageFilter(&m_received_rate_list);
    float smoothed_sent_rate_bps = applyMovingAverageFilter(&m_sent_rate_list);

    m_smoothed_received_rate_list.push_front(smoothed_received_rate_bps);
    m_smoothed_sent_rate_list.push_front(smoothed_sent_rate_bps);

    while (m_received_rate_list.size() > MAX_SAMPLES) {
        m_received_rate_list.pop_back();
        m_smoothed_received_rate_list.pop_back();
    }

    while (m_sent_rate_list.size() > MAX_SAMPLES) {
        m_sent_rate_list.pop_back();
        m_smoothed_sent_rate_list.pop_back();
    }

    float new_max_received_rate_bps = calculateMaxRateBps(&m_smoothed_received_rate_list);
    float new_max_sent_rate_bps = calculateMaxRateBps(&m_smoothed_sent_rate_list);

    setTotalBytesSent(new_total_bytes_sent);
    setTotalBytesReceived(new_total_bytes_received);
    setMaxReceivedRateBps(new_max_received_rate_bps);
    setMaxSentRateBps(new_max_sent_rate_bps);

    Q_EMIT receivedRateListChanged();
    Q_EMIT sentRateListChanged();
}
