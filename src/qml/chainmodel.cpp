// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/chainmodel.h>

#include <QDateTime>
#include <QThread>
#include <QTime>
#include <interfaces/chain.h>

ChainModel::ChainModel(interfaces::Chain& chain)
    : m_chain{chain}
{
    timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &ChainModel::setCurrentTimeRatio);
    timer->start(1000);

    QThread* timer_thread = new QThread;
    timer->moveToThread(timer_thread);
    timer_thread->start();
}

void ChainModel::setTimeRatioList(int new_time)
{
    int timeAtMeridian = timestampAtMeridian();

    if (new_time < timeAtMeridian) {
        return;
    }

    m_time_ratio_list.push_back(double(new_time - timeAtMeridian) / SECS_IN_12_HOURS);

    Q_EMIT timeRatioListChanged();
}

int ChainModel::timestampAtMeridian()
{
    int secsSinceMeridian = (QTime::currentTime().msecsSinceStartOfDay() / 1000) % SECS_IN_12_HOURS;
    int currentTimestamp = QDateTime::currentSecsSinceEpoch();

    return currentTimestamp - secsSinceMeridian;
}

void ChainModel::setTimeRatioListInitial()
{
    int timeAtMeridian = timestampAtMeridian();
    int first_block_height;
    int active_chain_height = m_chain.getHeight().value();
    bool success = m_chain.findFirstBlockWithTimeAndHeight(/*min_time=*/timeAtMeridian, /*min_height=*/0, interfaces::FoundBlock().height(first_block_height));

    if (!success) {
        return;
    }

    m_time_ratio_list.clear();
    m_time_ratio_list.push_back(double(QDateTime::currentSecsSinceEpoch() - timeAtMeridian) / SECS_IN_12_HOURS);

    for (int height = first_block_height; height < active_chain_height + 1; height++) {
        m_time_ratio_list.push_back(double(m_chain.getBlockTime(height) - timeAtMeridian) / SECS_IN_12_HOURS);
    }

    Q_EMIT timeRatioListChanged();
}

void ChainModel::setCurrentTimeRatio()
{
    int secsSinceMeridian = (QTime::currentTime().msecsSinceStartOfDay() / 1000) % SECS_IN_12_HOURS;
    double currentTimeRatio = double(secsSinceMeridian) / SECS_IN_12_HOURS;

    if (currentTimeRatio < m_time_ratio_list[0].toDouble()) { // That means time has crossed a meridian
        m_time_ratio_list.erase(m_time_ratio_list.begin() + 1, m_time_ratio_list.end());
    }
    m_time_ratio_list[0] = currentTimeRatio;
}