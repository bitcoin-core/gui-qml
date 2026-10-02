// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_WIDGETS_DIFFICULTYPERIODMODEL_H
#define BITCOIN_QML_COMPONENTS_WIDGETS_DIFFICULTYPERIODMODEL_H

#include <QObject>
#include <QThread>
#include <QTimer>

#include <functional>
#include <limits>

/** Local-chain difficulty period data, sampled off the GUI thread. */
class DifficultyPeriodModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(bool available READ available NOTIFY snapshotChanged)
    Q_PROPERTY(bool retargeting READ retargeting NOTIFY snapshotChanged)
    Q_PROPERTY(double progress READ progress NOTIFY snapshotChanged)
    Q_PROPERTY(int blocksLeft READ blocksLeft NOTIFY snapshotChanged)
    Q_PROPERTY(double averageBlockSeconds READ averageBlockSeconds NOTIFY snapshotChanged)
    Q_PROPERTY(double nextChange READ nextChange NOTIFY snapshotChanged)
    Q_PROPERTY(double previousChange READ previousChange NOTIFY snapshotChanged)
public:
    struct Snapshot {
        int height{-1};
        int interval{2016};
        qint64 spacing{600};
        qint64 start_time{0};
        qint64 tip_time{0};
        double current_target{0};
        double previous_target{0};
        double pow_limit{0};
        bool no_retargeting{false};
        bool min_difficulty_blocks{false};
    };
    using SampleFn = std::function<Snapshot()>;
    using NowFn = std::function<qint64()>;
    explicit DifficultyPeriodModel(SampleFn sample, NowFn now = {}, QObject* parent = nullptr);
    ~DifficultyPeriodModel() override;
    bool active() const { return m_active; }
    bool ready() const { return m_ready; }
    bool pending() const { return m_pending; }
    bool available() const { return m_snapshot.height >= 0; }
    bool retargeting() const { return !m_snapshot.no_retargeting; }
    double progress() const;
    int blocksLeft() const;
    double averageBlockSeconds() const;
    double nextChange() const;
    double previousChange() const;
    void setActive(bool active);
    void setReady(bool ready);
    void refresh();
    void stopForShutdown();
Q_SIGNALS:
    void shutdownFinished();
    void activeChanged();
    void readyChanged();
    void pendingChanged();
    void snapshotChanged();
private:
    void updatePolling();
    static constexpr double UNKNOWN{std::numeric_limits<double>::quiet_NaN()};
    SampleFn m_sample;
    NowFn m_now;
    Snapshot m_snapshot;
    qint64 m_time{0};
    QThread m_thread;
    QObject* m_worker;
    QTimer m_timer;
    quint64 m_generation{0};
    bool m_active{false};
    bool m_ready{false};
    bool m_pending{false};
};

#endif // BITCOIN_QML_COMPONENTS_WIDGETS_DIFFICULTYPERIODMODEL_H
