// Copyright (c) 2023 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_NETWORKTRAFFICTOWER_H
#define BITCOIN_QML_MODELS_NETWORKTRAFFICTOWER_H

#include <QObject>
#include <QQueue>

namespace interfaces {
class Node;
}

class NetworkTrafficWorker;
class QThread;

class NetworkTrafficTower : public QObject
{
    Q_OBJECT
    // Raw samples are always retained by the worker. Active only controls
    // whether derived history snapshots are copied to the GUI thread.
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(quint64 totalBytesReceived READ totalBytesReceived NOTIFY totalBytesReceivedChanged)
    Q_PROPERTY(quint64 totalBytesSent READ totalBytesSent NOTIFY totalBytesSentChanged)
    Q_PROPERTY(float maxReceivedRateBps READ maxReceivedRateBps NOTIFY maxReceivedRateBpsChanged)
    Q_PROPERTY(float maxSentRateBps READ maxSentRateBps NOTIFY maxSentRateBpsChanged)
    Q_PROPERTY(QQueue<float> receivedRateList READ receivedRateList NOTIFY receivedRateListChanged)
    Q_PROPERTY(QQueue<float> sentRateList READ sentRateList NOTIFY sentRateListChanged)

public:
    explicit NetworkTrafficTower(interfaces::Node& node, int sample_interval_ms = 1000);
    ~NetworkTrafficTower() override;
    void stop();

    bool active() const { return m_active; }
    quint64 totalBytesReceived() const { return m_total_bytes_received; }
    quint64 totalBytesSent() const { return m_total_bytes_sent; }
    float maxReceivedRateBps() const { return m_max_received_rate_bps; }
    float maxSentRateBps() const { return m_max_sent_rate_bps; }
    QQueue<float> receivedRateList() const { return m_received_rate_list; }
    QQueue<float> sentRateList() const { return m_sent_rate_list; }

public Q_SLOTS:
    void setActive(bool active);

    Q_INVOKABLE void updateFilterWindowSize(int new_size);

Q_SIGNALS:
    void activeChanged();
    void totalBytesReceivedChanged();
    void totalBytesSentChanged();
    void maxReceivedRateBpsChanged();
    void maxSentRateBpsChanged();
    void receivedRateListChanged();
    void sentRateListChanged();

private:
    NetworkTrafficWorker* m_worker{nullptr};
    QThread* m_worker_thread{nullptr};
    bool m_active{false};
    quint64 m_activation_generation{0};
    quint64 m_total_bytes_received{0};
    quint64 m_total_bytes_sent{0};
    float m_max_received_rate_bps{0.0f};
    float m_max_sent_rate_bps{0.0f};
    QQueue<float> m_received_rate_list;
    QQueue<float> m_sent_rate_list;
};

#endif // BITCOIN_QML_MODELS_NETWORKTRAFFICTOWER_H
