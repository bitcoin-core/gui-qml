// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETQMLMODEL_H
#define BITCOIN_QML_MODELS_WALLETQMLMODEL_H

#include <interfaces/handler.h>
#include <interfaces/wallet.h>
#include <qml/models/activitylistmodel.h>
#include <qml/models/coinslistmodel.h>
#include <qml/models/sendrecipient.h>
#include <qml/models/sendrecipientslistmodel.h>
#include <qml/models/walletqmlmodeltransaction.h>
#include <wallet/coincontrol.h>

#include <QObject>
#include <memory>
#include <vector>

class ActivityListModel;

class WalletQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString balance READ balance NOTIFY balanceChanged)
    Q_PROPERTY(ActivityListModel* activityListModel READ activityListModel CONSTANT)
    Q_PROPERTY(CoinsListModel* coinsListModel READ coinsListModel CONSTANT)
    Q_PROPERTY(SendRecipientsListModel* recipients READ sendRecipientList CONSTANT)
    Q_PROPERTY(WalletQmlModelTransaction* currentTransaction READ currentTransaction NOTIFY currentTransactionChanged)

public:
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, QObject* parent = nullptr);
    WalletQmlModel(QObject *parent = nullptr);
    ~WalletQmlModel();

    QString name() const;
    QString balance() const;
    ActivityListModel* activityListModel() const { return m_activity_list_model; }
    CoinsListModel* coinsListModel() const { return m_coins_list_model; }
    SendRecipientsListModel* sendRecipientList() const { return m_send_recipients; }
    WalletQmlModelTransaction* currentTransaction() const { return m_current_transaction; }
    Q_INVOKABLE bool prepareTransaction();
    Q_INVOKABLE void sendTransaction();

    std::set<interfaces::WalletTx> getWalletTxs() const;
    interfaces::WalletTx getWalletTx(const uint256& hash) const;
    bool tryGetTxStatus(const uint256& txid,
                        interfaces::WalletTxStatus& tx_status,
                        int& num_blocks,
                        int64_t& block_time) const;

    using TransactionChangedFn = std::function<void(const uint256& txid, ChangeType status)>;
    virtual std::unique_ptr<interfaces::Handler> handleTransactionChanged(TransactionChangedFn fn);

    interfaces::Wallet::CoinsList listCoins() const;
    bool lockCoin(const COutPoint& output);
    bool unlockCoin(const COutPoint& output);
    bool isLockedCoin(const COutPoint& output);
    void listLockedCoins(std::vector<COutPoint>& outputs);
    void selectCoin(const COutPoint& output);
    void unselectCoin(const COutPoint& output);
    bool isSelectedCoin(const COutPoint& output);
    std::vector<COutPoint> listSelectedCoins() const;

Q_SIGNALS:
    void nameChanged();
    void balanceChanged();
    void currentTransactionChanged();

private:
    std::unique_ptr<interfaces::Wallet> m_wallet;
    ActivityListModel* m_activity_list_model{nullptr};
    CoinsListModel* m_coins_list_model{nullptr};
    SendRecipientsListModel* m_send_recipients{nullptr};
    WalletQmlModelTransaction* m_current_transaction{nullptr};
    wallet::CCoinControl m_coin_control;
};

#endif // BITCOIN_QML_MODELS_WALLETQMLMODEL_H
