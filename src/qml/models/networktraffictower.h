// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NETWORKTRAFFICTOWER_H
#define BITCOIN_QML_MODELS_NETWORKTRAFFICTOWER_H

#include <qml/models/nodemodel.h>

#include <QObject>
#include <QQueue>

class NetworkTrafficTower : public QObject
{
    Q_OBJECT
    Q_PROPERTY(float totalBytesReceived READ totalBytesReceived NOTIFY totalBytesReceivedChanged)
    Q_PROPERTY(float totalBytesSent READ totalBytesSent NOTIFY totalBytesSentChanged)
    Q_PROPERTY(float maxReceivedRateBps READ maxReceivedRateBps NOTIFY maxReceivedRateBpsChanged)
    Q_PROPERTY(float maxSentRateBps READ maxSentRateBps NOTIFY maxSentRateBpsChanged)
    Q_PROPERTY(QQueue<float> receivedRateList READ receivedRateList NOTIFY receivedRateListChanged)
    Q_PROPERTY(QQueue<float> sentRateList READ sentRateList NOTIFY sentRateListChanged)

public:
    explicit NetworkTrafficTower(NodeModel& node);

    float totalBytesReceived() const { return m_total_bytes_received; }
    float totalBytesSent() const { return m_total_bytes_sent; }
    float maxReceivedRateBps() const { return m_max_received_rate_bps; }
    float maxSentRateBps() const { return m_max_sent_rate_bps; }
    QQueue<float> receivedRateList() { return m_smoothed_received_rate_list; }
    QQueue<float> sentRateList() { return m_smoothed_sent_rate_list; }

public Q_SLOTS:
    void setTotalBytesReceived(float new_total);
    void setTotalBytesSent(float new_total);
    void setMaxReceivedRateBps(float new_max);
    void setMaxSentRateBps(float new_max);

    Q_INVOKABLE void updateFilterWindowSize(int new_size);

Q_SIGNALS:
    void totalBytesReceivedChanged();
    void totalBytesSentChanged();
    void maxReceivedRateBpsChanged();
    void maxSentRateBpsChanged();
    void receivedRateListChanged();
    void sentRateListChanged();

private:
    float applyMovingAverageFilter(QQueue<float> * rate_list);
    float calculateMaxRateBps(QQueue<float> * smoothed_rate_list);
    void recalculateSmoothedRateList(QQueue<float> * raw_rate_list, QQueue<float> * smoothed_rate_list);
    void updateTrafficStats();

    NodeModel& m_node;
    int m_filter_window_size;
    float m_total_bytes_received{0.0f};
    float m_total_bytes_sent{0.0f};
    float m_max_received_rate_bps{0.0f};
    float m_max_sent_rate_bps{0.0f};
    QQueue<float> m_received_rate_list;
    QQueue<float> m_smoothed_received_rate_list;
    QQueue<float> m_sent_rate_list;
    QQueue<float> m_smoothed_sent_rate_list;
};

#endif // BITCOIN_QML_MODELS_NETWORKTRAFFICTOWER_H
