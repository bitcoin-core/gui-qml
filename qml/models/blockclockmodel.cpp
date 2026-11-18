// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/atomicsharedptr.h>
#include <qml/models/blockclockmodel.h>
#include <qml/models/blockclockhistory.h>
#include <qml/guipresentation.h>
#include <qml/retainedsnapshot.h>
#include <algorithm>
#include <QPointer>
#include <QTime>

struct BlockClockModel::Native {
    std::shared_ptr<BlockClockHistory> history;
    std::atomic_bool closed{false}, wake_pending{false};
    std::atomic<qint64> period{0};
    std::atomic<quint64> revision{0}, interest{0};
    ownership::AtomicSharedPtr<const blockclock::Projection> published;
    QList<qint64> timestamps;
    bool registered{false}, have_snapshot{false};
    quint64 callback_token{0};
    quint64 next_serial{0}, captured_generation{0};
    ~Native() { if (registered) history->clearChangedCallback(callback_token); }
};

BlockClockModel::BlockClockModel(std::shared_ptr<BlockClockHistory> history, execution::SnapshotServices services,
                                 bool start_timer, CurrentTimeProvider current_time_provider, QObject* parent)
    : QObject{parent}, m_services{std::move(services)},
      m_current_time_provider{std::move(current_time_provider)}, m_clock_timer{this}
{
    m_services.post = presentation::ReliablePost(std::move(m_services.post));
    if (!m_current_time_provider) m_current_time_provider = [] { return QDateTime::currentDateTime(); };
    if (!history) history = std::make_shared<BlockClockHistory>();
    if (m_services.scheduler) m_native = execution::RetainSnapshot<Native>(m_services.scheduler, [&] {
        auto value = std::make_unique<Native>(); value->history = history; return value;
    });
    if (!m_native) m_error = tr("Block history background services are unavailable.");
    m_clock_timer.setSingleShot(true); m_clock_timer.setTimerType(Qt::PreciseTimer);
    connect(&m_clock_timer, &QTimer::timeout, this, [this] {
        const QPointer<BlockClockModel> observer{this};
        const auto now = m_current_time_provider(); updateCurrentTime(now);
        if (observer) observer->scheduleNextClockUpdate(now);
    });
    const auto now = m_current_time_provider();
    updateCurrentTime(now);
    if (start_timer) scheduleNextClockUpdate(now);
}
BlockClockModel::~BlockClockModel() { closeAdmission(); }
void BlockClockModel::stop()
{
    if (std::exchange(m_stopped, true)) return;
    if (m_native) m_native->closed.store(true);
    if (m_query) m_query->close();
    m_query.reset(); m_pending = false; m_clock_timer.stop();
}
void BlockClockModel::closeAdmission()
{
    stop();
    m_projection.reset(); m_native.reset();
}
qint64 BlockClockModel::PeriodStartFor(const QDateTime& current_time)
{
    const qint64 seconds = current_time.time().msecsSinceStartOfDay() / 1000;
    return current_time.toSecsSinceEpoch() - seconds % PERIOD_SECONDS;
}
void BlockClockModel::updateCurrentTime(const QDateTime& now)
{
    if (m_stopped) return;
    const auto period = PeriodStartFor(now);
    const bool changed = period != m_period_start;
    if (changed) {
        m_period_start = period; ++m_revision;
        if (m_native) {
            // This is an atomic scalar request only. All history locks, map
            // pruning and complete snapshot work belong to the Native reader.
            m_native->history->requestRetentionStart(period - PERIOD_SECONDS);
            m_native->period.store(period); m_native->revision.store(m_revision);
        }
    }
    const auto fraction = fractionForTimestamp(now.toSecsSinceEpoch());
    const bool time_changed = !qFuzzyCompare(m_current_time_fraction + 1.0, fraction + 1.0);
    m_current_time_fraction = fraction;
    const QPointer<BlockClockModel> observer{this};
    if (changed) { refreshHistory(); if (!observer || m_stopped) return; Q_EMIT periodChanged(); if (!observer || m_stopped) return;
        Q_EMIT blockSnapshotChanged(); if (!observer || m_stopped) return; }
    if (time_changed) Q_EMIT currentTimeFractionChanged();
}
void BlockClockModel::scheduleNextClockUpdate(const QDateTime& now)
{
    if (m_stopped) return;
    m_clock_timer.start(CLOCK_UPDATE_INTERVAL_MS - now.time().second() * 1000 - now.time().msec());
}
void BlockClockModel::refreshHistory()
{
    if (m_stopped || !m_native) return;
    if (!m_query) {
        const QPointer<BlockClockModel> observer{this};
        const auto state = m_native;
        const std::weak_ptr<Native> weak = state;
        const auto post = m_services.post;
        m_query = std::make_shared<execution::SnapshotQuery<blockclock::Snapshot>>(m_services.scheduler,
            [state, weak, observer, post, scheduler = m_services.scheduler] {
                if (state->closed.load()) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
                const auto revision = state->revision.load();
                const auto period = state->period.load();
                const auto interest = state->interest.load();
                try {
                    if (!state->registered) {
                        state->callback_token = state->history->installChangedCallback([weak, observer, post] {
                            const auto state = weak.lock();
                            if (!state || state->closed.load() || state->wake_pending.exchange(true)) return;
                            try { post([weak, observer] {
                                const auto state = weak.lock();
                                if (!state) return;
                                state->wake_pending.store(false);
                                if (!state->closed.load() && observer) {
                                    observer->updateCurrentTime(observer->m_current_time_provider());
                                    if (observer && !observer->m_stopped) observer->refreshHistory();
                                }
                            }); } catch (...) { state->wake_pending.store(false); }
                        });
                        state->registered = true;
                    }
                    state->history->applyRequestedRetention();
                    if (auto snapshot = state->history->takeSnapshotWithGeneration(!state->have_snapshot)) { state->timestamps = std::move(snapshot->first); state->captured_generation = snapshot->second; state->have_snapshot = true; }
                    const auto previous = state->published.load();
                    auto frame = execution::RetainSnapshot<blockclock::Projection>(scheduler, [&] {
                        auto value = std::make_unique<blockclock::Projection>();
                        const auto count = std::count_if(state->timestamps.cbegin(), state->timestamps.cend(), [period](auto timestamp) { return timestamp >= period && timestamp < period + PERIOD_SECONDS; });
                        const auto bytes = size_t(count) * sizeof(qreal);
                        if (bytes > 8 * 1024 * 1024) throw std::length_error("Block history presentation is too large");
                        value->fractions.reserve(count);
                        value->period = period; value->revision = revision; value->interest = interest;
                        value->history_generation = state->captured_generation;
                        value->serial = ++state->next_serial; value->previous_serial = previous ? previous->serial : 0;
                        for (auto timestamp : state->timestamps) if (timestamp >= period && timestamp < period + PERIOD_SECONDS)
                            value->fractions.append(qreal(timestamp - period) / PERIOD_SECONDS);
                        std::sort(value->fractions.begin(), value->fractions.end());
                        value->changed = previous ? previous->fractions != value->fractions : !value->fractions.empty();
                        return value;
                    });
                    if (!frame) throw std::runtime_error("Block history projection retention unavailable");
                    return blockclock::Snapshot{std::move(frame)};
                } catch (...) {
                    auto failed = std::make_shared<blockclock::Projection>(); failed->failed = true; failed->revision = revision; failed->period = period;
                    failed->interest = interest;
                    return blockclock::Snapshot{std::move(failed)};
                }
            }, [observer, post] { post([observer] { if (observer) observer->publishHistory(); }); }, execution::Lane::Domain);
    }
    const bool changed = !std::exchange(m_pending, true);
    m_native->interest.fetch_add(1);
    m_query->discardReadFailure();
    m_query->request();
    if (changed) Q_EMIT historyStateChanged();
}
void BlockClockModel::publishHistory()
{
    if (m_stopped || !m_query) return;
    const auto result = m_query->take(); if (!result) return;
    if (!result->snapshot || !*result->snapshot) {
        m_pending = false; m_error = tr("Block history could not be prepared. Retry after background work finishes.");
        Q_EMIT historyStateChanged(); return;
    }
    const auto frame = *result->snapshot;
    if (frame->revision != m_revision || frame->period != m_period_start || frame->interest != m_native->interest.load()) return;
    if (frame->failed) { m_pending = false; m_error = tr("Block history could not be retained. Retry after background work finishes."); Q_EMIT historyStateChanged(); return; }
    if (frame->history_generation != m_native->history->generation()) { m_query->request(); return; }
    if (frame->previous_serial != (m_projection ? m_projection->serial : 0)) { m_query->request(); return; }
    const bool source_changed = frame->changed || !m_projection || m_projection->period != frame->period;
    m_projection = frame; m_native->published.store(frame); m_pending = false; m_error.clear();
    const QPointer<BlockClockModel> observer{this};
    if (source_changed) { Q_EMIT blockSnapshotChanged(); if (!observer || m_stopped) return; }
    if (frame->changed) { Q_EMIT blockTimeFractionsChanged(); if (!observer || m_stopped) return; }
    Q_EMIT historyStateChanged();
}
qreal BlockClockModel::fractionForTimestamp(qint64 timestamp) const
{
    return !m_period_start ? 0.0 : std::clamp(qreal(timestamp - m_period_start) / PERIOD_SECONDS, 0.0, 1.0);
}
