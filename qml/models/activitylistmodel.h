// Copyright (c) 2024-2025 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYLISTMODEL_H
#define BITCOIN_QML_MODELS_ACTIVITYLISTMODEL_H

#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <qml/models/transaction.h>

#include <memory>
#include <QAbstractListModel>
#include <QList>
#include <QSharedPointer>
#include <QString>

class WalletQmlModel;

class ActivityListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)

public:
    explicit ActivityListModel(WalletQmlModel * parent = nullptr);
    ~ActivityListModel();

    enum TransactionRoles {
        AddressRole = Qt::UserRole + 1,
        AmountRole,
        DateTimeRole,
        DepthRole,
        LabelRole,
        StatusRole,
        TypeRole,
        TxidRole,
        TxIdRole = TxidRole,
        CanBumpRole,
        ReplacesTxidRole,
        ReplacedByTxidRole,
        TimestampRole,
        IsPendingRequestRole,
        RequestIdRole,
        NetAmountSatRole,
        OutputIndexRole,
        CountsForBalanceRole
    };

    Q_INVOKABLE void reload();
    Q_INVOKABLE QVariantMap firstTransactionDetails(const QString& txid) const;
    Q_INVOKABLE QVariantMap transactionDetails(const QString& txid, int output_index) const;
    void refreshStatuses();
    void refreshLabels();
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int count() const { return rowCount(); }
    bool rowSortsBefore(int lhs_row, int rhs_row) const;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

    void setDisplayUnit(int unit);
    void addReceiveRequest(const QString& address, const QString& label,
                           CAmount amount, qint64 timestamp, const QString& requestId);
    void updateReceiveRequest(const QString& requestId, const QString& label, CAmount amount);
    void removePendingReceiveRequest(const QString& requestId);

Q_SIGNALS:
    void countChanged();

private:
    void refreshWallet();
    void addPendingReceiveRequests();
    void updateTransactionStatus(QSharedPointer<Transaction> tx) const;
    void updateTransactionLabel(QSharedPointer<Transaction> tx) const;
    void subscribeToCoreSignals();
    void unsubscribeFromCoreSignals();
    void updateTransaction(const uint256& hash, const interfaces::WalletTxStatus& wtx,
                           int num_blocks, int64_t block_time);
    QVariantMap transactionDetails(const QSharedPointer<Transaction>& tx) const;
    static bool transactionSortsBefore(const QSharedPointer<Transaction>& a,
                                       const QSharedPointer<Transaction>& b);
    int sortedInsertPosition(const QSharedPointer<Transaction>& tx) const;
    void repositionTransaction(int index);
    int findPendingRequestIndex(const QString& address) const;
    void fulfillPendingRequest(int index, const QSharedPointer<Transaction>& real_tx);

    int m_display_unit{0};
    QList<QSharedPointer<Transaction>> m_transactions;
    WalletQmlModel* m_wallet_model;
    std::unique_ptr<interfaces::Handler> m_handler_transaction_changed;
    std::unique_ptr<interfaces::Handler> m_handler_show_progress;
};

#endif // BITCOIN_QML_MODELS_ACTIVITYLISTMODEL_H
