// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_DESKTOPINTENTMODEL_H
#define BITCOIN_QML_MODELS_DESKTOPINTENTMODEL_H
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/desktopintentowner.h>
#include <qml/snapshotquery.h>
#include <qml/atomicsharedptr.h>
#include <QObject>
#include <QPointer>
#ifdef ENABLE_WALLET
#include <qml/models/paymentimportoperation.h>
class BitcoinUriModel;
#endif

/** Cached application mailbox. Producers submit owned values directly to the
 * Domain owner. GUI commands perform only bounded presentation handoffs. */
class DesktopIntentModel final : public QObject {
    Q_OBJECT
    Q_PROPERTY(State state READ state NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
    Q_PROPERTY(bool canInspect READ canInspect NOTIFY changed)
    Q_PROPERTY(bool canDismiss READ canDismiss NOTIFY changed)
    Q_PROPERTY(bool canReconcile READ canReconcile NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(bool hasInspection READ hasInspection NOTIFY inspectionChanged)
    Q_PROPERTY(bool inspectionPending READ inspectionPending NOTIFY inspectionChanged)
    Q_PROPERTY(bool canContinue READ canContinue NOTIFY inspectionChanged)
    Q_PROPERTY(QString inspectionText READ inspectionText NOTIFY inspectionChanged)
#ifdef ENABLE_WALLET
    Q_PROPERTY(PaymentImportOperation* inspection READ inspection NOTIFY inspectionChanged)
#endif
public:
    enum class State { WaitingReady, Ready, Delivering, ReconcilingDelivery, Stopping, Stopped, Forwarding };
    Q_ENUM(State)
    explicit DesktopIntentModel(execution::SnapshotServices services, bool wallet_enabled, QObject* parent = nullptr);
    ~DesktopIntentModel() override;
    std::shared_ptr<desktopintent::Owner> owner() const { return m_owner; }
    State state() const { return static_cast<State>(m_snapshot.phase); }
    int pendingCount() const { return static_cast<int>(m_snapshot.pending); }
    bool canInspect() const { return !m_closed && m_ready && m_wallet_enabled && m_sink && m_snapshot.pending && !m_snapshot.active && !hasInspection()
        && (m_snapshot.phase == desktopintent::Phase::Ready || m_snapshot.error == desktopintent::Error::TargetUnavailable); }
    bool canDismiss() const { return !m_closed && !m_snapshot.active && (m_snapshot.pending || m_snapshot.error != desktopintent::Error::None || !m_ingress_error.isEmpty()); }
    bool canReconcile() const { return m_snapshot.phase == desktopintent::Phase::ReconcilingDelivery; }
    QString error() const;
    bool hasInspection() const;
    bool inspectionPending() const;
    bool canContinue() const;
    QString inspectionText() const;
    void installFileOpenHandler();
    void setWalletEnabled(bool enabled);
    void setReady(bool ready);
    void setAdmissionGate(std::shared_ptr<const std::atomic_bool> closed) { m_parent_admission->store(std::move(closed)); }
    void close();
#ifdef ENABLE_WALLET
    void setFactory(BitcoinUriModel* factory);
    PaymentImportOperation* inspection() const { return m_inspection; }
    Q_INVOKABLE PaymentImportOperation* takeInspection(QObject* receiver);
#endif
    Q_INVOKABLE void closeInspection();
    Q_INVOKABLE void inspectNext();
    Q_INVOKABLE void dismiss();
    Q_INVOKABLE void reconcile();
Q_SIGNALS:
    void changed();
    void inspectionChanged();
    void requestAttention();
protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
private:
    void project(desktopintent::Snapshot snapshot);
    const execution::SnapshotServices m_services;
    bool m_wallet_enabled;
    const std::shared_ptr<std::atomic_bool> m_closed_gate{std::make_shared<std::atomic_bool>(false)};
    const std::shared_ptr<ownership::AtomicSharedPtr<const std::atomic_bool>> m_parent_admission{std::make_shared<ownership::AtomicSharedPtr<const std::atomic_bool>>()};
    std::shared_ptr<desktopintent::Owner> m_owner;
    std::shared_ptr<const desktopintent::Sink> m_sink;
    desktopintent::Snapshot m_snapshot;
    uint64_t m_readiness{0};
    uint64_t m_file_sequence{0};
    QString m_ingress_error;
    bool m_ready{false}, m_closed{false};
#ifdef ENABLE_WALLET
    QPointer<PaymentImportOperation> m_inspection;
    std::optional<desktopintent::Ticket> m_inspection_ticket;
#endif
};
#endif // BITCOIN_QML_MODELS_DESKTOPINTENTMODEL_H
