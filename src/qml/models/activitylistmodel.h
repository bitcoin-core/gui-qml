// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_ACTIVITYLISTMODEL_H
#define BITCOIN_QML_MODELS_ACTIVITYLISTMODEL_H

#include <interfaces/handler.h>
#include <interfaces/wallet.h>

#include <qml/models/walletqmlmodel.h>
#include <qml/models/transaction.h>

#include <QAbstractListModel>
#include <QList>
#include <QSharedPointer>
#include <QString>

#include <memory>

class WalletQmlModel;

class ActivityListModel : public QAbstractListModel
{
    Q_OBJECT

public:
    explicit ActivityListModel(WalletQmlModel * parent = nullptr);

    enum TransactionRoles {
        AmountRole = Qt::UserRole + 1,
        AddressRole,
        LabelRole,
        DateTimeRole,
        StatusRole,
        TypeRole,
        DepthRole
    };

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

private:
    void refreshWallet();
    void updateTransactionStatus(QSharedPointer<Transaction> tx) const;
    void subsctribeToCoreSignals();
    void unsubscribeFromCoreSignals();
    void updateTransaction(const uint256& hash, const interfaces::WalletTxStatus& wtx,
                           int num_blocks, int64_t block_time);
    int findTransactionIndex(const uint256& hash) const;

    QList<QSharedPointer<Transaction>> m_transactions;
    WalletQmlModel* m_wallet_model;
    std::unique_ptr<interfaces::Handler> m_handler_transaction_changed;
    std::unique_ptr<interfaces::Handler> m_handler_show_progress;
};

#endif // BITCOIN_QML_MODELS_ACTIVITYLISTMODEL_H
