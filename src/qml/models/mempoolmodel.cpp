// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/mempoolmodel.h>
#include <common/args.h>
#include <net_processing.h>
#include <util/threadnames.h>
#include <QThread>
#include <QTimer>

MempoolModel::MempoolModel(interfaces::Node& node)
    : MempoolModel([&node] {
        return Snapshot{
            static_cast<int>(node.getMempoolSize()),
            node.getMempoolDynamicUsage() / 1'000'000.0,
            node.getMempoolMaxUsage() / 1'000'000.0,
        };
    }, !gArgs.GetBoolArg("-blocksonly", DEFAULT_BLOCKSONLY))
{
}

MempoolModel::MempoolModel(Fetch fetch, bool available)
    : m_fetch{std::move(fetch)}, m_worker{new QObject}, m_thread{new QThread(this)},
      m_timer{new QTimer(this)}, m_available{available}
{
    m_timer->setInterval(3000);
    connect(m_timer, &QTimer::timeout, this, &MempoolModel::refreshMempoolInfo);
    m_worker->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread->start();
    QMetaObject::invokeMethod(m_worker, [] { util::ThreadRename("qml-mempool"); }, Qt::QueuedConnection);
}

MempoolModel::~MempoolModel()
{
    stop();
}

void MempoolModel::initializeResult(bool success, interfaces::BlockAndHeaderTipInfo)
{
    if (m_stopped || !success) return;
    m_ready = true;
    if (m_active) {
        m_timer->start();
        refreshMempoolInfo();
    }
}

void MempoolModel::setMempoolInfoPollingActive(bool active)
{
    active = active && m_available && !m_stopped;
    if (m_active == active) return;
    m_active = active;
    ++m_generation;
    Q_EMIT mempoolInfoPollingActiveChanged(active);
    if (active && m_ready) {
        m_timer->start();
        refreshMempoolInfo();
    } else {
        m_timer->stop();
        m_pending = false;
    }
}

void MempoolModel::refreshMempoolInfo()
{
    if (!m_ready || !m_active || m_stopped) return;
    if (m_in_flight) {
        m_pending = true;
        return;
    }
    m_in_flight = true;
    const quint64 generation{m_generation};
    QMetaObject::invokeMethod(m_worker, [this, generation] {
        const Snapshot snapshot{m_fetch()};
        QMetaObject::invokeMethod(this, [this, generation, snapshot] {
            m_in_flight = false;
            if (!m_stopped && m_active && generation == m_generation) {
                if (snapshot.transaction_count != m_snapshot.transaction_count ||
                    snapshot.usage_mb != m_snapshot.usage_mb ||
                    snapshot.max_usage_mb != m_snapshot.max_usage_mb) {
                    m_snapshot = snapshot;
                    Q_EMIT mempoolInfoChanged();
                }
            }
            if (m_pending && !m_stopped && m_active) {
                m_pending = false;
                refreshMempoolInfo();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void MempoolModel::stop()
{
    if (m_stopped) return;
    setMempoolInfoPollingActive(false);
    m_stopped = true;
    m_ready = false;
    ++m_generation;
    m_thread->quit();
    // Core reads are non-cancellable. Join before the application releases Core.
    m_thread->wait();
}
