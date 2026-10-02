// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_WIDGETS_FEERATESMODEL_H
#define BITCOIN_QML_COMPONENTS_WIDGETS_FEERATESMODEL_H

#include <qml/components/widgets/asyncsettingswriter.h>

#include <QObject>
#include <QThread>
#include <QTimer>
#include <QVariantList>

#include <functional>

/** Wallet-independent estimates for the dashboard and Send block targets, in sat/vB.
 * Negative values mean unavailable. The estimator returns sat/kvB and runs on
 * a worker; stopForShutdown() asynchronously drains it before the node shuts down. */
class FeeRatesModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList rates READ rates NOTIFY ratesChanged)
    Q_PROPERTY(QVariantList blockTargetRates READ blockTargetRates NOTIFY ratesChanged)
    Q_PROPERTY(double referenceRate READ referenceRate NOTIFY referenceRateChanged)
    Q_PROPERTY(bool ready READ ready NOTIFY readyChanged)
    Q_PROPERTY(bool pending READ pending NOTIFY pendingChanged)
    Q_PROPERTY(bool active READ active WRITE setActive NOTIFY activeChanged)

public:
    bool persistencePending() const { return m_settings_writer.pending(); }
    using EstimateFn = std::function<qint64(int)>;
    using NowFn = std::function<qint64()>;
    explicit FeeRatesModel(EstimateFn estimate, QObject* parent = nullptr,
                           const QString& settings_file = {}, NowFn now = {});
    ~FeeRatesModel() override;
    QVariantList rates() const { return m_rates; } // Dashboard: 2, 4, 6, 144 blocks.
    QVariantList blockTargetRates() const { return m_block_target_rates; } // Send: 2, 3, 4, 6, 10, 25, 50 blocks.
    double referenceRate() const { return m_reference_rate; }
    bool ready() const { return m_ready; }
    bool pending() const { return m_pending; }
    bool active() const { return m_active; }
    void setReady(bool ready);
    void setActive(bool active);
    void refresh();
    void stopForShutdown();

Q_SIGNALS:
    void shutdownFinished();
    void ratesChanged();
    void referenceRateChanged();
    void readyChanged();
    void pendingChanged();
    void activeChanged();

private:
    AsyncSettingsWriter m_settings_writer;
    void updatePolling();
    void restoreHistory();
    void recordObservation(const QVariantList& rates);
    void updateReference();
    void saveHistory();
    struct Observation { qint64 time; double rate; };
    QList<Observation> m_history;
    QString m_settings_file;
    NowFn m_now;
    bool m_history_loaded{false};
    double m_reference_rate{-1.0};
    EstimateFn m_estimate;
    QThread m_thread;
    QObject* m_worker;
    QTimer m_timer;
    QVariantList m_rates{-1.0, -1.0, -1.0, -1.0};
    QVariantList m_block_target_rates{-1.0, -1.0, -1.0, -1.0, -1.0, -1.0, -1.0};
    bool m_ready{false};
    bool m_active{false};
    bool m_pending{false};
    quint64 m_generation{0};
};

#endif // BITCOIN_QML_COMPONENTS_WIDGETS_FEERATESMODEL_H
