// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H
#define BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H

#include <qml/models/transaction.h>
#include <qml/models/transactionactivity.h>

#include <QFutureWatcher>
#include <QAbstractListModel>
#include <QMap>
#include <QPromise>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

#include <optional>

class WalletQmlModel;

/** One row per wallet transaction or unpaid request. Actions belong to their
 * transaction and are never independently sorted, filtered or counted.
 *
 * This model owns the roles shared with its filter/export proxy.
 * activityType describes the transaction; each action has its own direction,
 * source, outputIndex (or inputs), amountSat, label and paymentRequests.
 */
class TransactionActivityModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
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
    };

    explicit TransactionActivityModel(WalletQmlModel* wallet_model);
    ~TransactionActivityModel() override;
    bool loading() const { return m_loading; }
    QString loadError() const { return m_load_error; }
    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return rowCount(); }
    int transactionCount() const { return m_transactions.size(); }
    int requestCount() const { return rowCount() - transactionCount(); }

    Q_INVOKABLE QString rawTransaction(const QString& txid) const;
    Q_INVOKABLE QString paymentRequestUri(const QString& request_id) const;
    Q_INVOKABLE void reload();
    // Status reads are cached. A busy wallet never changes a row to failed.
    Q_INVOKABLE void refreshStatuses();
    // Both getters use cached state. requestTransactionDetails resolves the
    // selected transaction on a worker and emits transactionDetailsChanged.
    Q_INVOKABLE QVariantMap transactionDetails(const QString& txid, bool include_flow = false) const;
    Q_INVOKABLE void requestTransactionDetails(const QString& txid);
    void setDisplayUnit(int unit);

Q_SIGNALS:
    void loadingChanged();
    void countChanged();
    // Full details can change without changing any high-level activity role
    // (for example, a previously unknown input's transaction becomes available).
    void transactionDetailsChanged();

private:
    struct Record {
        TransactionActivity activity;
        QStringList labels;
        int status{Transaction::Unconfirmed};
        int depth{0};
        int blocks_to_maturity{0};
        int block_height{0};
        bool status_known{false};
        CTransactionRef transaction;
        // Compact ownership flags let a detail preview use the cached
        // transaction without another wallet read or address-book scan.
        std::vector<bool> inputs_mine, outputs_mine, outputs_change;
    };
    using Row = QMap<int, QVariant>;

    void updateTransaction(const QString& txid, int change);
    struct Work {
        bool reload{false}, labels{false}, statuses{false}, poll{false}, details{false};
        QSet<QString> transactions;
        bool empty() const { return !reload && !labels && !statuses && !poll && !details && transactions.isEmpty(); }
        void merge(const Work& other);
    };
    struct Snapshot {
        QMap<QString, Record> records;
        QSet<QString> retry;
        std::optional<uint256> tip;
        QString detail_txid, raw_transaction;
        QVariantMap flow;
        QMap<QString, QString> detail_labels;
        bool can_bump{false}, detail_missing{false}, rows_changed{false}, details_changed{false}, flow_resolved{false};
    };
    static bool updateStatus(interfaces::Wallet& wallet, Record& record);
    static void updateLabels(interfaces::Wallet& wallet, Record& record);
    static Snapshot readSnapshot(interfaces::Wallet& wallet, Snapshot snapshot, const Work& work, const QPromise<Snapshot>& promise);
    void schedule();
    void startWork();
    void stop();
    void poll();
    void rebuildRows();
    QString formatAmount(CAmount amount, bool receive) const;

    WalletQmlModel* const m_wallet_model;
    int m_display_unit{0};
    QMap<QString, Record> m_transactions;
    QSet<QString> m_retry;
    QList<Row> m_rows;
    QTimer m_timer;
    std::optional<uint256> m_last_tip;
    QString m_detail_txid;
    QString m_detail_raw_transaction;
    QVariantMap m_detail_flow;
    bool m_detail_flow_resolved{false};
    QMap<QString, QString> m_detail_labels;
    bool m_detail_can_bump{false}, m_detail_missing{false}, m_details_loading{false};
    Work m_pending;
    QFutureWatcher<Snapshot>* m_watcher{nullptr};
    quint64 m_generation{0}, m_detail_generation{0};
    bool m_scheduled{false}, m_stopped{false}, m_loading{false};
    QString m_load_error;
};

#endif // BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H
