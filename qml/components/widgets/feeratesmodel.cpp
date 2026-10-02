// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/feeratesmodel.h>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>
#include <cmath>
#include <memory>
#include <utility>

namespace {
constexpr qint64 HISTORY_SECONDS{24 * 60 * 60};
constexpr auto HISTORY_KEY{"dashboard/feeRateHistory"};

std::unique_ptr<QSettings> Settings(const QString& file)
{
    return file.isEmpty() ? std::make_unique<QSettings>() : std::make_unique<QSettings>(file, QSettings::IniFormat);
}

double Median(QList<double> values)
{
    if (values.isEmpty()) return -1.0;
    std::sort(values.begin(), values.end());
    const auto middle = values.size() / 2;
    return values.size() % 2 ? values[middle] : values[middle - 1] / 2 + values[middle] / 2;
}
} // namespace

FeeRatesModel::FeeRatesModel(EstimateFn estimate, QObject* parent, const QString& settings_file, NowFn now)
    : QObject(parent), m_settings_writer(settings_file), m_settings_file(settings_file),
      m_now(now ? std::move(now) : NowFn{QDateTime::currentSecsSinceEpoch}),
      m_estimate(std::move(estimate)), m_worker(new QObject)
{
    m_worker->moveToThread(&m_thread);
    connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
    m_thread.setObjectName("qml-feerates");
    m_thread.start();
    m_timer.setInterval(60000);
    connect(&m_timer, &QTimer::timeout, this, &FeeRatesModel::refresh);
}

FeeRatesModel::~FeeRatesModel()
{
    m_timer.stop();
    m_thread.quit();
    m_thread.wait();
}

void FeeRatesModel::setReady(bool ready)
{
    if (m_ready == ready) return;
    m_ready = ready;
    ++m_generation;
    if (!ready) {
        m_timer.stop();
        m_rates = {-1.0, -1.0, -1.0, -1.0};
        m_block_target_rates = {-1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0};
        Q_EMIT ratesChanged();
    }
    Q_EMIT readyChanged();
    updatePolling();
}

void FeeRatesModel::setActive(bool active)
{
    if (m_active == active) return;
    m_active = active;
    ++m_generation;
    Q_EMIT activeChanged();
    updatePolling();
}

void FeeRatesModel::updatePolling()
{
    if (m_ready && m_active) {
        // Delay settings access until the app has selected its network scope.
        if (!m_history_loaded) restoreHistory();
        updateReference();
        refresh();
        m_timer.start();
    } else {
        m_timer.stop();
    }
}

void FeeRatesModel::restoreHistory()
{
    m_history_loaded = true;
    const auto settings = Settings(m_settings_file);
    const auto document = QJsonDocument::fromJson(settings->value(HISTORY_KEY).toByteArray()).object();
    if (document.value("version").toInt() != 1) return;
    const qint64 now = m_now();
    for (const auto value : document.value("samples").toArray()) {
        const auto sample = value.toObject();
        const double timestamp = sample.value("time").toDouble(-1);
        const double rate = sample.value("rate").toDouble(-1);
        if (!std::isfinite(timestamp) || timestamp < now - HISTORY_SECONDS || timestamp > now ||
            !std::isfinite(rate) || rate <= 0) continue;
        const qint64 minute = static_cast<qint64>(timestamp) / 60 * 60;
        m_history.append({minute, rate});
    }
    std::sort(m_history.begin(), m_history.end(), [](const auto& left, const auto& right) { return left.time < right.time; });
    m_history.erase(std::unique(m_history.begin(), m_history.end(), [](const auto& left, const auto& right) {
        return left.time == right.time;
    }), m_history.end());
    while (m_history.size() > 1440) m_history.removeFirst();
}

void FeeRatesModel::updateReference()
{
    const qint64 now = m_now();
    m_history.removeIf([&](const auto& sample) { return sample.time < now - HISTORY_SECONDS || sample.time > now; });
    QList<double> recent;
    for (const auto& sample : m_history) {
        // A spike must be compared with previous observations, not itself.
        if (sample.time < now / 60 * 60) recent.append(sample.rate);
    }
    const double reference = Median(recent);
    if (reference != m_reference_rate) {
        m_reference_rate = reference;
        Q_EMIT referenceRateChanged();
    }
}

void FeeRatesModel::recordObservation(const QVariantList& rates)
{
    updateReference();
    const qint64 minute = m_now() / 60 * 60;
    // At most one observation per minute, even across visibility changes/restarts.
    if (!m_history.isEmpty() && m_history.last().time == minute) return;
    QList<double> available;
    for (const auto& value : rates) {
        const double rate = value.toDouble();
        if (std::isfinite(rate) && rate > 0) available.append(rate);
    }
    if (available.isEmpty()) return;
    m_history.append({minute, Median(available)});
    while (m_history.size() > 1440) m_history.removeFirst();
    saveHistory();
}

void FeeRatesModel::saveHistory()
{
    m_settings_writer.save(HISTORY_KEY, [history = m_history] {
        QJsonArray samples;
        for (const auto& sample : history) samples.append(QJsonObject{{"time", sample.time}, {"rate", sample.rate}});
        return QJsonDocument(QJsonObject{{"version", 1}, {"samples", samples}}).toJson(QJsonDocument::Compact);
    });
}

void FeeRatesModel::refresh()
{
    if (!m_ready || !m_active || m_pending) return;
    m_pending = true;
    Q_EMIT pendingChanged();
    QMetaObject::invokeMethod(m_worker, [this, generation = m_generation] {
        QVariantList rates;
        for (const int target : {2, 3, 4, 6, 10, 25, 50, 144}) {
            const qint64 per_kvb = m_estimate(target);
            rates.append(per_kvb > 0 ? per_kvb / 1000.0 : -1.0);
        }
        QMetaObject::invokeMethod(this, [this, generation, rates = std::move(rates)] {
            m_pending = false;
            Q_EMIT pendingChanged();
            if (generation != m_generation) {
                refresh();
                return;
            }
            const QVariantList widget_rates{rates[0], rates[2], rates[3], rates[7]};
            const QVariantList block_target_rates = rates.mid(0, 7);
            // Keep the dashboard's historical baseline independent of slider targets.
            recordObservation(widget_rates);
            if (widget_rates != m_rates || block_target_rates != m_block_target_rates) {
                m_rates = widget_rates;
                m_block_target_rates = block_target_rates;
                Q_EMIT ratesChanged();
            }
        }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}

void FeeRatesModel::stopForShutdown()
{
    setReady(false);
    // A queued fence acknowledges all earlier samples without blocking input.
    QMetaObject::invokeMethod(m_worker, [this] {
        QMetaObject::invokeMethod(this, [this] { Q_EMIT shutdownFinished(); }, Qt::QueuedConnection);
    }, Qt::QueuedConnection);
}
