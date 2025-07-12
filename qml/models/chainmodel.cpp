// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/chainmodel.h>

#include <QDateTime>
#include <QString>
#include <QThread>
#include <QTime>
#include <interfaces/chain.h>

using interfaces::FoundBlock;

ChainModel::ChainModel(interfaces::Chain& chain)
    : m_chain{chain}
{
    QTimer* timer = new QTimer();
    connect(timer, &QTimer::timeout, this, &ChainModel::setCurrentTimeRatio);
    timer->start(1000);

    QThread* timer_thread = new QThread;
    timer->moveToThread(timer_thread);
    timer_thread->start();
}

void ChainModel::setCurrentNetworkName(QString network_name)
{
    m_current_network_name = network_name.toUpper();
    Q_EMIT currentNetworkNameChanged();
}

void ChainModel::setTimeRatioList(int new_time)
{
    if (m_time_ratio_list.isEmpty()) {
        setTimeRatioListInitial();
    }
    int time_at_meridian = timestampAtMeridian();

    if (new_time < time_at_meridian) {
        return;
    }
    m_time_ratio_list.push_back(double(new_time - time_at_meridian) / SECS_IN_12_HOURS);

    Q_EMIT timeRatioListChanged();
}

int ChainModel::timestampAtMeridian()
{
    int secs_since_meridian = (QTime::currentTime().msecsSinceStartOfDay() / 1000) % SECS_IN_12_HOURS;
    int current_timestamp = QDateTime::currentSecsSinceEpoch();

    return current_timestamp - secs_since_meridian;
}

void ChainModel::setTimeRatioListInitial()
{
    int time_at_meridian = timestampAtMeridian();
    m_time_ratio_list.clear();
    /* m_time_ratio_list[0] = current_time_ratio
     * m_time_ratio_list[1] = 0
     * These two positions remain fixed for these
     * values in m_time_ratio_list */
    m_time_ratio_list.push_back(double(QDateTime::currentSecsSinceEpoch() - time_at_meridian) / SECS_IN_12_HOURS);
    m_time_ratio_list.push_back(0);

    if (!m_chain.getHeight()) {
        Q_EMIT timeRatioListChanged();
        return;
    }

    int first_block_height;
    int active_chain_height = m_chain.getHeight().value();
    bool success = m_chain.findFirstBlockWithTimeAndHeight(/*min_time=*/time_at_meridian, /*min_height=*/0, interfaces::FoundBlock().height(first_block_height));

    if (!success) {
        Q_EMIT timeRatioListChanged();
        return;
    }

    for (int height = first_block_height; height < active_chain_height + 1; height++) {
        uint256 block_hash{m_chain.getBlockHash(height)};
        int64_t block_time;
        m_chain.findBlock(block_hash, FoundBlock().time(block_time));
        m_time_ratio_list.push_back(double(block_time - time_at_meridian) / SECS_IN_12_HOURS);
    }

    Q_EMIT timeRatioListChanged();
}

void ChainModel::setCurrentTimeRatio()
{
    int secs_since_meridian = (QTime::currentTime().msecsSinceStartOfDay() / 1000) % SECS_IN_12_HOURS;
    double current_time_ratio = double(secs_since_meridian) / SECS_IN_12_HOURS;

    if (current_time_ratio < m_time_ratio_list[0].toDouble()) { // That means time has crossed a meridian
        m_time_ratio_list.clear();
    }

    if (m_time_ratio_list.isEmpty()) {
        m_time_ratio_list.push_back(current_time_ratio);
        m_time_ratio_list.push_back(0);
    } else {
        m_time_ratio_list[0] = current_time_ratio;
    }

    Q_EMIT timeRatioListChanged();
}
