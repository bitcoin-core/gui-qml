// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PSBTQMLMODEL_H
#define BITCOIN_QML_MODELS_PSBTQMLMODEL_H

#include <qml/psbtwork.h>
#include <qml/snapshotquery.h>
#include <qml/walletopening.h>
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <mutex>

class BackendExecutor;
namespace psbtdocument { class Owner; }
namespace interfaces { class Node; }

class PsbtQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool loaded READ loaded NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QStringList summary READ summary NOTIFY changed)
    Q_PROPERTY(int summaryCount READ summaryCount NOTIFY changed)
    Q_PROPERTY(int summaryOffset READ summaryOffset NOTIFY changed)
    Q_PROPERTY(bool summaryPending READ summaryPending NOTIFY changed)
    Q_PROPERTY(QString summaryError READ summaryError NOTIFY changed)
    Q_PROPERTY(bool canSign READ canSign NOTIFY changed)
    Q_PROPERTY(bool canBroadcast READ canBroadcast NOTIFY changed)
    Q_PROPERTY(bool complete READ complete NOTIFY changed)
    Q_PROPERTY(bool signaturesVerified READ signaturesVerified NOTIFY changed)
    Q_PROPERTY(NodeReadiness nodeReadiness READ nodeReadiness NOTIFY changed)
    Q_PROPERTY(int unsignedInputCount READ unsignedInputCount NOTIFY changed)
    Q_PROPERTY(int couldSignInputCount READ couldSignInputCount NOTIFY changed)
    Q_PROPERTY(QString matchedTxid READ matchedTxid NOTIFY changed)
    Q_PROPERTY(Phase phase READ phase NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(quint64 operationId READ operationId NOTIFY changed)
    Q_PROPERTY(quint64 documentGeneration READ documentGeneration NOTIFY changed)
    Q_PROPERTY(QString submittedTxid READ submittedTxid NOTIFY changed)
    Q_PROPERTY(bool submitted READ submitted NOTIFY changed)
    Q_PROPERTY(QString unverifiedResultToken READ unverifiedResultToken NOTIFY changed)
public:
    enum class Phase { Idle, Pending, Ready, Failed, NeedsUnlock, Uncertain, Stale, Closed, Unverified };
    Q_ENUM(Phase)
    enum class NodeReadiness { Unknown, NoKnownObstacle, Blocked };
    Q_ENUM(NodeReadiness)
    // A raw Wallet argument records the expected context only. Wallet work is
    // rejected until composition supplies the retained opening below. Node must
    // outlive this model's configured scheduler drain (the app-wide Node service).
    explicit PsbtQmlModel(bool wallet_expected, interfaces::Node* node, QObject* parent = nullptr);
    ~PsbtQmlModel() override;
    void configureSession(std::shared_ptr<walletopening::Session> opening, psbtfee::Services services);
    void setFeeServices(psbtfee::Services services);
    void setNode(interfaces::Node* node);
    void close();
    void detachWallet();
    bool loaded() const { return bool(m_snapshot && m_snapshot->document); }
    QString status() const { return m_status; }
    QString error() const;
    QStringList summary() const { return m_summary_window; }
    int summaryCount() const { return m_snapshot ? m_snapshot->summary_count : 0; }
    int summaryOffset() const { return m_summary_offset; }
    bool summaryPending() const { return m_summary_pending; }
    QString summaryError() const { return m_summary_error; }
    bool canSign() const { return !busy() && !m_closed && m_phase != Phase::Uncertain && !m_signing_blocked && m_snapshot && m_snapshot->can_sign; }
    bool canBroadcast() const { return !busy() && !m_closed && m_snapshot && m_snapshot->can_broadcast && !m_broadcast_blocked; }
    NodeReadiness nodeReadiness() const { return m_snapshot ? static_cast<NodeReadiness>(m_snapshot->node_state) : NodeReadiness::Unknown; }
    bool complete() const { return m_snapshot && m_snapshot->complete; }
    bool signaturesVerified() const { return m_snapshot && m_snapshot->signature_state == psbtwork::SignatureState::Verified; }
    int unsignedInputCount() const { return m_snapshot ? m_snapshot->unsigned_inputs : 0; }
    int couldSignInputCount() const { return m_snapshot ? m_snapshot->could_sign : 0; }
    QString matchedTxid() const { return m_matched_txid; }
    Phase phase() const { return m_phase; }
    bool busy() const { return bool(m_active); }
    quint64 operationId() const { return m_operation_id; }
    quint64 documentGeneration() const { return m_generation; }
    QString submittedTxid() const;
    bool submitted() const;
    Q_INVOKABLE quint64 requestLoadFromFile(const QString& path);
    QString unverifiedResultToken() const;
    Q_INVOKABLE quint64 requestSign();
    Q_INVOKABLE quint64 requestSignWithPassphrase(const QString& passphrase);
    Q_INVOKABLE quint64 requestBroadcast();
    Q_INVOKABLE quint64 requestCopyToClipboard();
    Q_INVOKABLE void requestSummaryWindow(int offset, int count = 128);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void sign() { requestSign(); }
    Q_INVOKABLE void broadcast() { requestBroadcast(); }
    void refreshState(const QString& status_override = {});
    void retryPublication();
    const PartiallySignedTransaction* psbt() const { return m_snapshot ? m_snapshot->document.get() : nullptr; }
    std::shared_ptr<const PartiallySignedTransaction> document() const { return m_snapshot ? m_snapshot->document : nullptr; }
    void setError(const QString& error);
    void setMatchedTxid(const QString& txid);
    // Worker/test-setup utilities. Model commands never call these on GUI.
    static QString LocalFilePath(const QString& path);
    static QString LoadPsbtFromFile(const QString& path, PartiallySignedTransaction& psbt);
    static QByteArray SerializePsbtRaw(const PartiallySignedTransaction& psbt);
    static QString PsbtErrorText(common::PSBTError error);
    static bool IsMultisigPsbtInput(const PartiallySignedTransaction& psbt, std::size_t index);
Q_SIGNALS:
    void changed();
    void finished(quint64 operation_id, quint64 document_generation);
private:
    quint64 start(psbtwork::Request request);
    void publish(quint64 operation, quint64 generation);
    void releaseSnapshot();
    void releaseQuarantine();
    void retireDocumentOwner();
    void retireSummaryQuery();
    void ensureServices();
    void prepareSummaryPaging();
    void ensureSummaryQuery();
    void publishSummaryPage(quint64 generation, quint64 epoch);
    struct SummaryRequestState;
    using Operation = execution::OwnedCommand<psbtwork::Result>;
    struct Receipt {
        std::mutex mutex;
        bool ready{false}, submitted{false};
        QString path, txid, error;
        void record(const psbtwork::Result& result) {
            std::lock_guard lock{mutex};
            ready = true; submitted = result.submitted;
            path = result.path; txid = result.txid; error = result.error;
        }
    };
    std::shared_ptr<Receipt> m_receipt, m_submission_receipt;
    std::shared_ptr<walletopening::Session> m_opening;
    psbtfee::Services m_services;
    std::shared_ptr<QObject> m_bridge;
    std::unique_ptr<BackendExecutor> m_owned_executor;
    interfaces::Node* m_node{nullptr};
    bool m_wallet_expected{false}, m_closed{false};
    std::shared_ptr<Operation> m_active;
    std::optional<execution::ResourceLease> m_active_lease, m_snapshot_lease;
    std::optional<execution::ResourceLease> m_quarantine_lease;
    std::shared_ptr<const PartiallySignedTransaction> m_quarantined_document;
    quint64 m_quarantined_operation{0}, m_quarantined_generation{0};
    std::shared_ptr<const psbtwork::Snapshot> m_snapshot;
    std::shared_ptr<SummaryRequestState> m_summary_request_state;
    std::shared_ptr<execution::SnapshotQuery<psbtwork::SummaryPage>> m_summary_query;
    std::shared_ptr<psbtdocument::Owner> m_document_owner;
    quint64 m_operation_id{0}, m_generation{0}, m_active_generation{0};
    quint64 m_summary_epoch{0}, m_summary_request_id{0};
    psbtwork::Kind m_active_kind{psbtwork::Kind::Inspect};
    Phase m_phase{Phase::Idle};
    QString m_status, m_error, m_matched_txid, m_submitted_txid;
    QString m_summary_error;
    QStringList m_summary_window;
    int m_summary_offset{0}, m_summary_size{128};
    bool m_summary_pending{false};
    bool m_submitted{false}, m_broadcast_blocked{false}, m_signing_blocked{false};
};
#endif // BITCOIN_QML_MODELS_PSBTQMLMODEL_H
