// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_NETWORKMODEL_H
#define BITCOIN_QML_MODELS_NETWORKMODEL_H

#include <qml/networkcommand.h>

#include <QObject>
#include <QString>

#include <cstdint>
#include <functional>
#include <memory>

// GUI facade: cached getters and narrow scalar ingress; decisions stay Domain.
class NetworkModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool observedActive READ observedActive NOTIFY changed)
    Q_PROPERTY(bool known READ known NOTIFY changed)
    Q_PROPERTY(bool ready READ ready NOTIFY changed)
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(Outcome outcome READ outcome NOTIFY changed)
    Q_PROPERTY(quint64 requestId READ requestId NOTIFY changed)
    Q_PROPERTY(bool drained READ drained NOTIFY changed)
public:
    enum Outcome { Idle, Pending, Succeeded, Superseded, Rejected, Uncertain, Closed };
    Q_ENUM(Outcome)
    struct Ports {
        // post must enqueue on GUI, never invoke its callback inline.
        execution::SnapshotServices execution;
        std::function<networkcommand::Observation()> observe;
        std::function<void(bool)> set_active;
        std::function<uint64_t()> generation;
    };
    explicit NetworkModel(Ports ports, QObject* parent = nullptr);
    ~NetworkModel() override;
    bool observedActive() const { return m_projection.active; }
    bool known() const { return m_projection.known; }
    uint64_t observedGeneration() const { return m_projection.generation; }
    bool ready() const { return !m_closed && m_available && m_projection.ready && m_projection.availability == m_availability; }
    bool pending() const { return m_ingress_pending || m_projection.pending; }
    QString error() const;
    Outcome outcome() const { return m_outcome; }
    quint64 requestId() const { return m_request_id; }
    bool drained() const { return m_drained; }
    const networkcommand::Effect& effect() const { return m_projection.effect; }
    Q_INVOKABLE bool requestActive(bool active);
    void observe(bool active, uint64_t generation);
    void setAvailable(bool available);
    void retire();
    void close();
Q_SIGNALS:
    void changed();
    void shutdownFinished();
private:
    void publish();
    void finishDrain();
    std::unique_ptr<networkcommand::Service> m_service;
    networkcommand::Projection m_projection;
    networkcommand::Error m_error{networkcommand::Error::None};
    Outcome m_outcome{Idle};
    uint64_t m_request_id{0}, m_availability{0}, m_observation_ingress{0};
    bool m_ingress_pending{false}, m_available{false}, m_closed{false}, m_drained{false};
};

#endif // BITCOIN_QML_MODELS_NETWORKMODEL_H
