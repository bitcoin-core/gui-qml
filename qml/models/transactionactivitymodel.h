// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H
#define BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H

#include <qml/models/transaction.h>
#include <qml/models/activitysnapshot.h>
#include <qml/models/activitystore.h>
#include <qml/walletopening.h>
#include <qml/models/transactionactivity.h>

#include <atomic>
#include <memory>
#include <QAbstractListModel>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include <optional>

class WalletQmlModel;
class ActivityDetailModel;
Q_MOC_INCLUDE("qml/models/activitydetailmodel.h")
namespace walletrequests { struct Snapshot; }
namespace backup { struct InstallCoordinator; }

/** Virtualized views read one bounded, already-prepared entry at a time. */
class ActivityValueListModel final : public QAbstractListModel
{
public:
    enum class Kind { Inputs, Outputs, Actions, Raw, EntryDocument };
    ActivityValueListModel(Kind kind, QObject* parent) : QAbstractListModel(parent), m_kind(kind) {}
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override { return {{Qt::UserRole, m_kind == Kind::Raw ? "content" : "entry"}}; }
    void setSnapshot(std::shared_ptr<const activityview::Hold> snapshot, bool quiet = false);
private:
    std::shared_ptr<const QVariantList> values() const;
    Kind m_kind;
    quint64 m_revision{0};
    std::shared_ptr<const activityview::Hold> m_snapshot;
};

/** One row per wallet transaction or unpaid request. Actions belong to their
 * transaction and are never independently sorted, filtered or counted.
 *
 * This model owns the roles shared with its filter/export proxy.
 * activityType describes the transaction; each action has its own direction,
 * source, outputIndex (or inputs), amountSat, label and paymentRequests.
 */
class TransactionActivityModel : public QAbstractListModel, public activityview::Source
{
    Q_OBJECT
    Q_INTERFACES(activityview::Source)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(bool initialLoading READ initialLoading NOTIFY loadingChanged)
    Q_PROPERTY(bool refreshing READ refreshing NOTIFY loadingChanged)
    Q_PROPERTY(QString loadError READ loadError NOTIFY loadingChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int transactionCount READ transactionCount NOTIFY countChanged)
    Q_PROPERTY(int requestCount READ requestCount NOTIFY countChanged)

public:
    enum ActivityType {
        Send = int(TransactionActivity::Type::Send),
        Receive = int(TransactionActivity::Type::Receive),
        Multiple = int(TransactionActivity::Type::Multiple),
        Consolidation = int(TransactionActivity::Type::Consolidation),
        Split = int(TransactionActivity::Type::Split),
        InternalTransfer = int(TransactionActivity::Type::InternalTransfer),
        Mined = int(TransactionActivity::Type::Mined),
        Other = int(TransactionActivity::Type::Other),
    };
    Q_ENUM(ActivityType)
    enum ActionDirection {
        SendAction = int(TransactionAction::Direction::Send),
        ReceiveAction = int(TransactionAction::Direction::Receive),
        InternalAction = int(TransactionAction::Direction::Internal),
    };
    Q_ENUM(ActionDirection)
    enum ActionSource { Output = int(TransactionAction::Source::Output), WalletInputs = int(TransactionAction::Source::WalletInputs) };
    Q_ENUM(ActionSource)
    enum Role {
        AddressRole = Qt::UserRole + 1,
        AmountRole,
        DateTimeRole,
        DepthRole,
        LabelRole,
        StatusRole,
        TypeRole,
        TxidRole,
        ReplacesTxidRole,
        ReplacedByTxidRole,
        TimestampRole,
        IsPendingRequestRole,
        RequestIdRole,
        NetAmountSatRole,
        IdRole = Qt::UserRole + 100,
        ActivityTypeRole,
        ActionsRole,
        ActionCountRole,
        WalletDebitSatRole,
        WalletCreditSatRole,
        FeeSatRole,
        FeeKnownRole,
        StatusKnownRole,
        IsPendingRole,
        IsInactiveRole,
        PaymentRequestsRole,
        HasPaymentRequestRole,
        SearchTextRole,
        BlocksToMaturityRole,
        CanBumpRole,
        PaymentRequestCountRole,
        PresentationLimitedRole,
    };

    struct Services {
        std::function<void(std::function<void()>)> post;
        std::function<void(std::chrono::milliseconds, std::function<void()>)> schedule_poll;
        std::function<void()> cancel_poll;
    };
    explicit TransactionActivityModel(WalletQmlModel* wallet_model);
    TransactionActivityModel(WalletQmlModel* wallet_model, Services services);
    ~TransactionActivityModel() override;
    bool loading() const { return m_loading; }
    bool initialLoading() const { return m_loading && !m_published; }
    bool refreshing() const { return m_loading && bool(m_published); }
    QString loadError() const { return m_load_error; }
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }
    int transactionCount() const { return m_transaction_count; }
    int requestCount() const { return m_request_count; }

    bool activityLoading() const override { return m_loading; }
    QString activityError() const override { return m_load_error; }
    int activityRowForId(const QString& id) const override {
        const int row = activityview::Rank(m_root, id);
        const auto* value = activityview::At(m_root, row);
        return value && value->id == id ? row : -1;
    }
    QList<QMetaObject::Connection> observeActivity(QObject* receiver, std::function<void()> changed) override {
        return {connect(this, &TransactionActivityModel::snapshotChanged, receiver, changed),
                connect(this, &TransactionActivityModel::loadingChanged, receiver, std::move(changed))};
    }
    QAbstractItemModel* detailInputs() { return &m_detail_inputs; }
    QAbstractItemModel* detailOutputs() { return &m_detail_outputs; }
    QAbstractItemModel* detailActions() { return &m_detail_actions; }
    QAbstractItemModel* rawTransactionLines() { return &m_raw_lines; }
    QAbstractItemModel* entryDocument() { return &m_entry_document; }
    bool entryDetailsBusy() const { return bool(m_entry_active); }
    QString entryDetailsError() const { return m_entry_error; }
    quint64 requestEntryDetails(const QString& side, int row);
    std::shared_ptr<const activityview::Hold> preparedSnapshot() const override { return m_published; }
    execution::SnapshotServices activityServices() const override { return {m_opening ? m_opening->scheduler() : nullptr, m_services.post}; }
    std::shared_ptr<walletopening::Session> opening() const override { return m_opening; }
    QString rawTransaction(const QString& txid) const;
    quint64 requestCopyRawTransaction(const QString& txid);
    Q_INVOKABLE QString paymentRequestUri(const QString& request_id) const;
    Q_INVOKABLE void reload();
    // Status reads are cached. A busy wallet never changes a row to failed.
    Q_INVOKABLE void refreshStatuses();
    Q_INVOKABLE ActivityDetailModel* createDetailView(QObject* owner);
    // Both getters use cached state. requestTransactionDetails resolves the
    // selected transaction on a worker and emits transactionDetailsChanged.
    QVariantMap selectedTransactionDetails() const { return transactionDetails(m_detail_txid, true); }
    QVariantMap transactionDetails(const QString& txid, bool include_flow = false) const;
    void requestTransactionDetails(const QString& txid);
    void setDisplayUnit(int unit);
    void stop();
    void close();
    activityexport::Files activityFiles() const override;
    // Pure Domain projection shared by immutable activity consumers.
    static activityview::Row presentationRow(const activityview::Row& row);

Q_SIGNALS:
    void loadingChanged();
    void snapshotChanged();
    void copyFinished(quint64 ticket, bool copied, const QString& error);
    void entryDetailsChanged();
    void countChanged();
    // Full details can change without changing any high-level activity role
    // (for example, a previously unknown input's transaction becomes available).
    void transactionDetailsChanged();

private:
    friend class ActivityDetailModel;
    using Record = activitystore::Record;
    using Row = QMap<int, QVariant>;

    void updateTransaction(const QString& txid, int change);
    using Work = activitystore::Work;
    using Snapshot = activitystore::Snapshot;
    struct DetailSource {
        std::shared_ptr<const Snapshot> core;
        std::shared_ptr<const walletrequests::Snapshot> requests;
    };
    void schedule();
    void startWork();
    void poll();
    void rebuildRows();
    static QHash<int, QByteArray> roles();
    static QString formatAmount(CAmount amount, bool receive, int display_unit);
    static std::pair<QString, Row> buildTransactionRow(const Record& record, const QMap<QString, QVariantList>& matches, int unit, QSet<QString>& fulfilled);
    static std::optional<std::pair<QString, Row>> buildRequestRow(const QVariantMap& request, int unit, const QSet<QString>& fulfilled);
    static void prepareDetails(activityview::Snapshot& view, QVariantMap details, int unit, const Row& original_row);
    static QVariantMap buildDetails(const QString& txid, const Row& row, const Snapshot& snapshot,
                                   const std::shared_ptr<const walletrequests::Snapshot>& requests, int unit, bool full);
    struct Owner;
    struct Job;
    struct Mailbox;
    static void planned(const std::shared_ptr<Job>& job);
    static void readCompleted(const std::shared_ptr<Job>& job);
    static void project(const std::shared_ptr<Job>& job);
    void receive();
    void applyPatches();
    void finishClose();
    void armPoll();
    void schedulePreview();
    bool publishDetailModels();

    WalletQmlModel* const m_wallet_model;
    int m_display_unit{0};
    const std::shared_ptr<walletopening::Session> m_opening;
    Services m_services;
    const std::shared_ptr<backup::InstallCoordinator> m_export_installs;
    std::shared_ptr<Mailbox> m_mailbox;
    std::shared_ptr<Job> m_active, m_entry_active;
    quint64 m_entry_ticket{0};
    QString m_entry_error;
    std::shared_ptr<const Snapshot> m_core_state;
    activitystore::Revisions m_reader_revisions;
    std::shared_ptr<execution::ResourceLease> m_core_lease;
    quint64 m_projection_generation{0}, m_work_revision{0};
    std::shared_ptr<const activityview::Hold> m_published;
    activityview::Tree m_root;
    QTimer m_timer, m_apply_timer;
    size_t m_patch_position{0};
    int m_transaction_count{0}, m_request_count{0};
    QString m_detail_txid;
    std::shared_ptr<const QVariantMap> m_preview;
    std::shared_ptr<const activityview::Hold> m_detail_hold;
    ActivityValueListModel m_detail_inputs{ActivityValueListModel::Kind::Inputs, this};
    ActivityValueListModel m_detail_outputs{ActivityValueListModel::Kind::Outputs, this};
    ActivityValueListModel m_detail_actions{ActivityValueListModel::Kind::Actions, this};
    ActivityValueListModel m_raw_lines{ActivityValueListModel::Kind::Raw, this};
    ActivityValueListModel m_entry_document{ActivityValueListModel::Kind::EntryDocument, this};
    bool m_details_loading{false};
    quint64 m_copy_ticket{0};
    Work m_pending;
    bool m_work_pending{false};
    std::shared_ptr<std::atomic_bool> m_cancelled{std::make_shared<std::atomic_bool>(false)};
    quint64 m_generation{0}, m_detail_generation{0};
    bool m_scheduled{false}, m_stopped{false}, m_loading{false}, m_in_model_update{false};
    bool m_request_rebuild_pending{false};
    QString m_load_error;
};

#endif // BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H
