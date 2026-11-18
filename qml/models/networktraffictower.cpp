// Copyright (c) 2023-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/networktraffictower.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <interfaces/node.h>
#include <QPointer>
#include <QTimer>
#include <algorithm>
#include <stdexcept>
namespace {
constexpr int MAX_SAMPLES{86400};
constexpr int DEFAULT_FILTER_WINDOW_SIZE{30};
class QtTrafficTimer final : public TrafficTimer {
    QTimer m_timer;
public:
    void restart(int interval,std::function<void()> tick) override {
        m_timer.stop();QObject::disconnect(&m_timer,nullptr,nullptr,nullptr);
        QObject::connect(&m_timer,&QTimer::timeout,&m_timer,std::move(tick));m_timer.start(interval);
    }
    void stop() override {m_timer.stop();QObject::disconnect(&m_timer,nullptr,nullptr,nullptr);}
};
}
struct TrafficSnapshot {
    quint64 generation{0};
    quint64 total_bytes_received{0};
    quint64 total_bytes_sent{0};
    float max_received_rate_bps{0.0f};
    float max_sent_rate_bps{0.0f};
    QQueue<float> received_rate_list;
    QQueue<float> sent_rate_list;
};

namespace {
quint64 NonNegativeTotal(int64_t total)
{
    return total > 0 ? static_cast<quint64>(total) : 0;
}

float RateDelta(quint64 current, quint64 previous)
{
    return current >= previous ? static_cast<float>(current - previous) : 0.0f;
}
} // namespace

struct TrafficEngine
{
    TrafficSnapshot snapshot;
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

    void sample(interfaces::Node& node)
    {
        const quint64 total_received{NonNegativeTotal(node.getTotalBytesRecv())};
        const quint64 total_sent{NonNegativeTotal(node.getTotalBytesSent())};

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

        snapshot = TrafficSnapshot{
            m_generation,
            m_total_bytes_received,
            m_total_bytes_sent,
            m_max_received_rate_bps,
            m_max_sent_rate_bps,
            visibleHistory(m_smoothed_received_rate_list),
            visibleHistory(m_smoothed_sent_rate_list),
        };
    }


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


NetworkTrafficTower::NetworkTrafficTower(interfaces::Node& node,int interval,execution::SnapshotServices services,std::shared_ptr<TrafficTimer> timer)
    :m_node(node),m_services(std::move(services)),m_timer(std::move(timer)),m_interval(std::max(1,interval))
{
    if(!m_services.scheduler){m_executor=std::make_unique<BackendExecutor>(execution::Options{},this);m_services.scheduler=m_executor->scheduler();}
    m_services.post=presentation::ReliablePost(std::move(m_services.post));
    if(!m_timer)m_timer=std::make_shared<QtTrafficTimer>();
    m_work=std::make_unique<execution::OwnedWork>(m_services);m_key=m_services.scheduler->allocateKey();
    m_engine=execution::RetainSnapshot<TrafficEngine>(m_services.scheduler,[]{return std::make_unique<TrafficEngine>();});
    if(!m_engine)m_error=tr("Traffic history is unavailable.");
}
NetworkTrafficTower::~NetworkTrafficTower(){closeAdmission();if(m_executor)m_executor->shutdown();}
void NetworkTrafficTower::closeAdmission(){m_stopping=true;m_timer->stop();m_inflight=false;m_again=false;m_work->close();m_snapshot.reset();m_engine.reset();}
void NetworkTrafficTower::beginShutdown(){
    if(std::exchange(m_shutdown_started,true))return;
    closeAdmission();const QPointer<NetworkTrafficTower> observer{this};
    if(m_executor){m_executor->whenDrained(this,[observer]{if(observer)Q_EMIT observer->drained();});m_executor->shutdown();}
    else m_work->close([observer]{if(observer)Q_EMIT observer->drained();});
}
void NetworkTrafficTower::startSampling(){
    if(m_stopping||m_sampling_started)return;
    m_sampling_started=true;
    const QPointer<NetworkTrafficTower> observer{this};
    m_timer->restart(m_interval,[observer]{if(observer)observer->requestSnapshot(true);});requestSnapshot(true);
}
void NetworkTrafficTower::setActive(bool active){
    if(m_stopping||m_active==active)return;
    m_active=active;++m_activation_generation;
    const QPointer<NetworkTrafficTower> observer{this};Q_EMIT activeChanged();if(observer&&!m_stopping)requestSnapshot(false);
}
void NetworkTrafficTower::updateFilterWindowSize(int size){
    if(m_stopping)return;
    size=std::clamp(size,1,MAX_SAMPLES);if(m_window==size)return;m_window=size;++m_activation_generation;requestSnapshot(false);
}
void NetworkTrafficTower::requestSnapshot(bool sample){
    if(m_stopping||!m_engine)return;
    m_need_sample=m_need_sample||(sample&&m_sampling_started);
    if(m_inflight){m_again=true;return;}m_inflight=true;
    const bool take_sample=std::exchange(m_need_sample,false);const bool active=m_active;const auto generation=m_activation_generation;const auto window=m_window;
    const QPointer<NetworkTrafficTower> observer{this};
    m_work->submit([engine=m_engine,node=&m_node,scheduler=m_services.scheduler,take_sample,active,generation,window] {
        engine->setFilterWindowSize(window);engine->setActive(active,generation);if(take_sample)engine->sample(*node);
        auto result=execution::RetainSnapshot<TrafficSnapshot>(scheduler,[&]{return std::make_unique<TrafficSnapshot>(engine->snapshot);});
        if(!result)throw std::runtime_error("Traffic snapshot capacity unavailable");
        return std::shared_ptr<const TrafficSnapshot>{std::move(result)};
    },[observer](auto snapshot){if(!observer||observer->m_stopping)return;observer->m_inflight=false;observer->m_error.clear();observer->publish(std::move(snapshot));
        if(!observer||observer->m_stopping)return;
        Q_EMIT observer->sampleStateChanged();if(observer&&!observer->m_stopping&&std::exchange(observer->m_again,false))observer->m_services.post([observer]{if(observer&&!observer->m_stopping)observer->requestSnapshot(false);});
    },[observer](std::exception_ptr){if(!observer||observer->m_stopping)return;observer->m_inflight=false;observer->m_error=tr("Traffic statistics could not be refreshed.");
        Q_EMIT observer->sampleStateChanged();if(observer&&!observer->m_stopping&&std::exchange(observer->m_again,false))observer->m_services.post([observer]{if(observer&&!observer->m_stopping)observer->requestSnapshot(false);});
    },{execution::Lane::Core,m_key,execution::WorkClass::Observation});
    if(observer&&!m_stopping)Q_EMIT sampleStateChanged();
}
QQueue<float> NetworkTrafficTower::receivedRateList() const{return m_snapshot?m_snapshot->received_rate_list:QQueue<float>{};}
QQueue<float> NetworkTrafficTower::sentRateList() const{return m_snapshot?m_snapshot->sent_rate_list:QQueue<float>{};}
void NetworkTrafficTower::publish(std::shared_ptr<const TrafficSnapshot> value){
    if(!m_active||value->generation!=m_activation_generation)return;
    const bool received=m_total_bytes_received!=value->total_bytes_received,sent=m_total_bytes_sent!=value->total_bytes_sent;
    const bool max_received=m_max_received_rate_bps!=value->max_received_rate_bps,max_sent=m_max_sent_rate_bps!=value->max_sent_rate_bps;
    m_snapshot=std::move(value);m_total_bytes_received=m_snapshot->total_bytes_received;m_total_bytes_sent=m_snapshot->total_bytes_sent;
    m_max_received_rate_bps=m_snapshot->max_received_rate_bps;m_max_sent_rate_bps=m_snapshot->max_sent_rate_bps;
    const QPointer<NetworkTrafficTower> observer{this};
    if(received)Q_EMIT totalBytesReceivedChanged();
    if(!observer||m_stopping)return;
    if(sent)Q_EMIT totalBytesSentChanged();
    if(!observer||m_stopping)return;
    if(max_received)Q_EMIT maxReceivedRateBpsChanged();
    if(!observer||m_stopping)return;
    if(max_sent)Q_EMIT maxSentRateBpsChanged();
    if(!observer||m_stopping)return;
    Q_EMIT receivedRateListChanged();if(!observer||m_stopping)return;Q_EMIT sentRateListChanged();
}
