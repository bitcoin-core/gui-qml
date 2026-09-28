// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_WIDGETS_MEMPOOLACTIVITYMODEL_H
#define BITCOIN_QML_COMPONENTS_WIDGETS_MEMPOOLACTIVITYMODEL_H

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>

#include <functional>

/** Bounded, session-local history of accepted incoming vbytes per second. */
class MempoolActivityModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList history READ history NOTIFY historyChanged)
    Q_PROPERTY(double incomingRate READ incomingRate NOTIFY snapshotChanged)
    Q_PROPERTY(double minimumFee READ minimumFee NOTIFY snapshotChanged)
    Q_PROPERTY(double baseline READ baseline CONSTANT)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
public:
    struct Snapshot { quint64 incoming_vbytes; double minimum_fee; bool loaded{true}; };
    using SampleFn = std::function<Snapshot()>;
    using NowFn = std::function<qint64()>;
    explicit MempoolActivityModel(SampleFn sample, double baseline = 1000000.0 / 600,
                                  NowFn now = {}, QObject* parent = nullptr);
    ~MempoolActivityModel() override;
    QVariantList history() const;
    double incomingRate() const { return m_incoming_rate; }
    double minimumFee() const { return m_minimum_fee; }
    double baseline() const { return m_baseline; }
    bool ready() const { return m_ready; }
    bool active() const { return m_active; }
    bool pending() const { return m_pending; }
    void setReady(bool ready);
    void setActive(bool active);
    void refresh();
Q_SIGNALS:
    void historyChanged();
    void snapshotChanged();
    void readyChanged();
    void activeChanged();
    void pendingChanged();
    void statsRefreshRequested();
private:
    void applySnapshot(const Snapshot& snapshot, qint64 time);
    struct Sample { qint64 time; double rate; };
    QList<Sample> m_history;
    SampleFn m_sample;
    NowFn m_now;
    const double m_baseline;
    QThread m_thread;
    QObject* m_worker;
    QTimer m_timer;
    double m_incoming_rate{-1};
    double m_minimum_fee{-1};
    quint64 m_previous_vbytes{0};
    qint64 m_previous_time{0};
    quint64 m_generation{0};
    bool m_has_previous{false};
    bool m_ready{false};
    bool m_active{false};
    bool m_pending{false};
};

#endif // BITCOIN_QML_COMPONENTS_WIDGETS_MEMPOOLACTIVITYMODEL_H
