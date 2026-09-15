// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H
#define BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H

#include <qml/models/transaction.h>
#include <qml/models/transactionactivity.h>

#include <QAbstractListModel>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVariantMap>

class WalletQmlModel;

/** One row per wallet transaction or unpaid request. Actions belong to their
 * transaction and are never independently sorted, filtered or counted.
 *
 * Roles belong to this model. Basic role IDs remain compatible with the
 * previous activity model while its screens are being migrated.
 * activityType describes the transaction; each action has its own direction,
 * source, outputIndex (or inputs), amountSat, label and paymentRequests.
 */
class TransactionActivityModel : public QAbstractListModel
{
    Q_OBJECT
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
    Q_INVOKABLE QVariantMap transactionDetails(const QString& txid) const;
    void setDisplayUnit(int unit);

Q_SIGNALS:
    void countChanged();

private:
    struct Record {
        TransactionActivity activity;
        QStringList labels;
        int status{Transaction::Unconfirmed};
        int depth{0};
        int blocks_to_maturity{0};
        bool status_known{false};
        bool can_bump{false};
    };
    using Row = QMap<int, QVariant>;

    void updateTransaction(const QString& txid, int change);
    bool updateStatus(Record& record);
    void updateLabels(Record& record);
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
};

#endif // BITCOIN_QML_MODELS_TRANSACTIONACTIVITYMODEL_H
