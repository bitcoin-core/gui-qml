// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_MODELS_RECEIPTRECONCILIATIONMODEL_H
#define BITCOIN_QML_MODELS_RECEIPTRECONCILIATIONMODEL_H

#include <qml/walletreceipts.h>

#include <QObject>
#include <QPointer>
#include <QTimer>

/** Bounded presentation adapter for the single opening's receipt Reader.
 * It never reads Core or owns a second credit ledger. Immutable inventories
 * are returned to Reader for background retirement, including at shutdown. */
class ReceiptReconciliationModel final : public QObject
{
    Q_OBJECT
public:
    enum Phase { Loading, Refreshing, Ready, Failed, Rejected, Closed };
    Q_ENUM(Phase)
    struct Ports {
        walletopening::Opening opening;
        std::function<std::optional<walletreceipts::Publication>()> take;
        std::function<void(std::shared_ptr<const walletreceipts::Snapshot>)> retire;
        std::function<bool()> current;
        std::function<uint64_t()> nativeRevision;
        std::function<void()> poll, retry, acknowledge;
        std::function<void(const std::map<Txid, ChangeType>&)> transactions;
    };
    explicit ReceiptReconciliationModel(Ports ports, QObject* parent = nullptr);
    ~ReceiptReconciliationModel() override;
    Phase phase() const { return m_phase; }
    bool fresh() const;
    bool pending() const { return !m_closed && !fresh(); }
    bool applying() const { return m_applying; }
    QString errorText() const;
    CAmount amount(const QString& address) const;
    bool observed(const QString& address) const;
    bool knows(const QString& address) const { return m_snapshot && m_snapshot->addresses.contains(address); }
    uint64_t revision() const { return m_revision; }
    void consume();
    void invalidate();
    void poll();
    void retry();
    void close();
Q_SIGNALS:
    void changed();
    void committed();
    void rescanRequested();
private:
    bool configured() const { return m_ports.take && m_ports.retire && m_ports.current && m_ports.nativeRevision; }
    Ports m_ports;
    std::shared_ptr<const walletreceipts::Snapshot> m_snapshot;
    QTimer m_poll;
    Phase m_phase{Loading};
    uint64_t m_revision{0};
    bool m_closed{false}, m_applying{false}, m_again{false};
};

/** Exact request/address receipt projection. Positive current credit is an
 * observation, never a promise that the requested amount was paid in full.
 * Historical use survives zero credit and does not grant sharing permission. */
class RequestReceiptModel final : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString requestId READ requestId NOTIFY changed)
    Q_PROPERTY(QString address READ address NOTIFY changed)
    Q_PROPERTY(qint64 receivedAmountSatoshi READ receivedAmountSatoshi NOTIFY changed)
    Q_PROPERTY(ReceiptState receiptState READ receiptState NOTIFY changed)
    Q_PROPERTY(bool historicallyUsed READ historicallyUsed NOTIFY changed)
    Q_PROPERTY(bool fresh READ fresh NOTIFY changed)
    Q_PROPERTY(ReceiptReconciliationModel::Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(QString errorText READ errorText NOTIFY changed)
public:
    enum ReceiptState { Unknown, Awaiting, Received };
    Q_ENUM(ReceiptState)
    explicit RequestReceiptModel(QObject* parent = nullptr) : QObject(parent) {}
    QString requestId() const { return m_request; }
    QString address() const { return m_address; }
    qint64 receivedAmountSatoshi() const { return m_amount; }
    ReceiptState receiptState() const { return fresh() ? m_amount > 0 ? Received : Awaiting : Unknown; }
    bool historicallyUsed() const { return m_used; }
    bool fresh() const { return m_available && m_reader && m_reader->fresh() && m_reader->knows(m_address); }
    ReceiptReconciliationModel::Phase phase() const { return !m_available || m_reader_closed ? ReceiptReconciliationModel::Closed : m_reader ? m_reader->phase() : ReceiptReconciliationModel::Loading; }
    QString errorText() const { return m_reader && m_available ? m_reader->errorText() : QString{}; }
    void setReader(ReceiptReconciliationModel* reader);
    void setSource(QString request, QString address, bool used, bool available);
    Q_INVOKABLE void retry() { if (m_available && m_reader) m_reader->retry(); }
Q_SIGNALS:
    void changed();
private:
    void refresh();
    QPointer<ReceiptReconciliationModel> m_reader;
    QMetaObject::Connection m_connection, m_destroyed;
    QString m_request, m_address;
    CAmount m_amount{0};
    bool m_used{false}, m_source_used{false}, m_available{false}, m_bound{false}, m_reader_closed{false};
};
#endif // BITCOIN_QML_MODELS_RECEIPTRECONCILIATIONMODEL_H
