// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_PEERCOMMANDMODEL_H
#define BITCOIN_QML_MODELS_PEERCOMMANDMODEL_H
#include <qml/peercommandport.h>

/** Application command/outcome owner, independent of selected detail/view. */
class PeerCommandModel final : public QObject, public PeerCommandPort {
    Q_OBJECT
    Q_PROPERTY(bool pending READ pending NOTIFY changed)
    Q_PROPERTY(bool canStart READ canStart NOTIFY changed)
    Q_PROPERTY(PeerCommandResult lastResult READ lastResult NOTIFY changed)
public:
    PeerCommandModel(interfaces::Node& node, execution::SnapshotServices services, std::function<bool()> ready, QObject* parent = nullptr);
    ~PeerCommandModel() override;
    QObject* observerObject() override { return this; }
    bool pending() const { return m_pending; }
    bool canStart() const override { return !m_closed && !m_pending && m_ready && m_ready(); }
    PeerCommandResult lastResult() const { return m_result; }
    PeerCommandOperation* prepare(const PeerConnection& target, peercommands::Kind kind, int64_t duration) override;
    QMetaObject::Connection observe(QObject* receiver, std::function<void()> changed) override;
    quint64 requestDisconnectPeer(qint64 id); // Bare-ID compatibility only.
    quint64 requestBanPeer(const QString& raw_address, int64_t duration);
    void readinessChanged() { if (!m_closed) Q_EMIT changed(); }
    void closeAdmission();
    void refresh();
Q_SIGNALS:
    void changed();
    void finished(quint64 ticket, PeerCommandOperation::Outcome outcome, const QString& error);
private:
    peercommands::Request request(PeerConnection target, peercommands::Kind kind, int64_t duration, bool exact);
    quint64 legacy(peercommands::Request request);
    void admitted();
    const execution::SnapshotServices m_services;
    const std::function<bool()> m_ready;
    std::shared_ptr<peercommands::Store> m_store;
    quint64 m_ticket{0}, m_mutation_revision{0};
    bool m_pending{false}, m_closed{false};
    PeerCommandResult m_result;
};
#endif // BITCOIN_QML_MODELS_PEERCOMMANDMODEL_H
