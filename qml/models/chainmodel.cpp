// Copyright (c) 2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/chainmodel.h>

#include <chainparams.h>
#include <interfaces/chain.h>

#include <QDateTime>
#include <QString>
#include <QTime>

#include <utility>

using interfaces::FoundBlock;

ChainModel::ChainModel(interfaces::Chain& chain)
    : m_assumed_blockchain_size{Params().AssumedBlockchainSize()},
      m_assumed_chainstate_size{Params().AssumedChainStateSize()},
      m_chain{chain}
{
    connect(&m_backend, &BackendWorker::drained, this, &ChainModel::backendDrained);
    connect(&m_timer, &QTimer::timeout, this, &ChainModel::setCurrentTimeRatio);
    m_timer.start(1000);
}

void ChainModel::setCurrentNetworkName(QString network_name)
{
    m_current_network_name = network_name.toUpper();
    Q_EMIT currentNetworkNameChanged();
}

void ChainModel::setTimeRatioList(int new_time)
{
    RequireModelThread(this);
    if (m_draining || !m_node_ready) return;
    if (m_time_ratio_list.size() < 2 || m_pending) {
        setTimeRatioListInitial();
        return;
    }
    const int time_at_meridian = timestampAtMeridian();
    if (new_time < time_at_meridian) return;
    m_time_ratio_list.push_back(double(new_time - time_at_meridian) / SECS_IN_12_HOURS);
    Q_EMIT timeRatioListChanged();
}

void ChainModel::onNodeReady()
{
    RequireModelThread(this);
    if (m_draining || m_node_ready) return;
    m_node_ready = true;
    setTimeRatioListInitial();
}

void ChainModel::drainBackend()
{
    RequireModelThread(this);
    m_draining = true;
    m_timer.stop();
    m_backend.drain();
}

int ChainModel::timestampAtMeridian()
{
    int secs_since_meridian = (QTime::currentTime().msecsSinceStartOfDay() / 1000) % SECS_IN_12_HOURS;
    int current_timestamp = QDateTime::currentSecsSinceEpoch();

    return current_timestamp - secs_since_meridian;
}

void ChainModel::setTimeRatioListInitial()
{
    RequireModelThread(this);
    if (m_draining || !m_node_ready) return;
    if (m_pending) {
        m_again = true;
        return;
    }
    m_pending = true;
    const int meridian = timestampAtMeridian();
    m_backend.submit([chain = &m_chain, meridian] {
        QVariantList ratios{double(QDateTime::currentSecsSinceEpoch() - meridian) / SECS_IN_12_HOURS, 0};
        const auto height = chain->getHeight();
        int first_height{0};
        if (height && chain->findFirstBlockWithTimeAndHeight(meridian, 0, FoundBlock().height(first_height))) {
            for (int i = first_height; i <= *height; ++i) {
                int64_t time{0};
                if (chain->findBlock(chain->getBlockHash(i), FoundBlock().time(time))) {
                    ratios.push_back(double(time - meridian) / SECS_IN_12_HOURS);
                }
            }
        }
        return ratios;
    }, [this, meridian](QVariantList ratios) {
        RequireModelThread(this);
        m_pending = false;
        if (std::exchange(m_again, false) || meridian != timestampAtMeridian()) {
            setTimeRatioListInitial();
            return;
        }
        m_time_ratio_list = std::move(ratios);
        Q_EMIT timeRatioListChanged();
    });
}

void ChainModel::setCurrentTimeRatio()
{
    RequireModelThread(this);
    int secs_since_meridian = (QTime::currentTime().msecsSinceStartOfDay() / 1000) % SECS_IN_12_HOURS;
    double current_time_ratio = double(secs_since_meridian) / SECS_IN_12_HOURS;

    if (!m_time_ratio_list.isEmpty() && current_time_ratio < m_time_ratio_list[0].toDouble()) { // That means time has crossed a meridian
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
