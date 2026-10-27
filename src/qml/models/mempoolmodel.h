// Copyright (c) 2021 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_MEMPOOLMODEL_H
#define BITCOIN_QML_MODELS_MEMPOOLMODEL_H
#include <interfaces/node.h>
#include <QObject>
#include <functional>
class QThread;
class QTimer;

/** Visible-page mempool sampling; one request in flight and stale results discarded. */
class MempoolModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int mempoolTransactionCount READ mempoolTransactionCount NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolUsageMB READ mempoolUsageMB NOTIFY mempoolInfoChanged)
    Q_PROPERTY(double mempoolMaxUsageMB READ mempoolMaxUsageMB NOTIFY mempoolInfoChanged)
    Q_PROPERTY(bool mempoolInfoPollingActive READ mempoolInfoPollingActive WRITE setMempoolInfoPollingActive NOTIFY mempoolInfoPollingActiveChanged)
    Q_PROPERTY(bool mempoolInformationAvailable READ mempoolInformationAvailable CONSTANT)
public:
    struct Snapshot {
        int transaction_count{0};
        double usage_mb{0.0};
        double max_usage_mb{0.0};
    };
    using Fetch = std::function<Snapshot()>;
    explicit MempoolModel(interfaces::Node& node);
    // Same worker path with an injected data source for deterministic unit tests.
    explicit MempoolModel(Fetch fetch, bool available = true);
    ~MempoolModel() override;
    int mempoolTransactionCount() const { return m_snapshot.transaction_count; }
    double mempoolUsageMB() const { return m_snapshot.usage_mb; }
    double mempoolMaxUsageMB() const { return m_snapshot.max_usage_mb; }
    bool mempoolInfoPollingActive() const { return m_active; }
    bool mempoolInformationAvailable() const { return m_available; }
    void setMempoolInfoPollingActive(bool active);
    Q_INVOKABLE void refreshMempoolInfo();
public Q_SLOTS:
    void initializeResult(bool success, interfaces::BlockAndHeaderTipInfo tip_info);
    void stop();
Q_SIGNALS:
    void mempoolInfoChanged();
    void mempoolInfoPollingActiveChanged(bool active);
private:
    Snapshot m_snapshot;
    Fetch m_fetch;
    QObject* m_worker;
    QThread* m_thread;
    QTimer* m_timer;
    const bool m_available;
    bool m_ready{false};
    bool m_active{false};
    bool m_stopped{false};
    bool m_in_flight{false};
    bool m_pending{false};
    quint64 m_generation{0};
};
#endif // BITCOIN_QML_MODELS_MEMPOOLMODEL_H
