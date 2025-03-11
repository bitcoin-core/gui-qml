// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_WALLETQMLMODEL_H
#define BITCOIN_QML_MODELS_WALLETQMLMODEL_H

#include <interfaces/wallet.h>

#include <qml/models/sendrecipient.h>
#include <qml/models/walletqmlmodeltransaction.h>

#include <QObject>

class WalletQmlModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString balance READ balance NOTIFY balanceChanged)
    Q_PROPERTY(SendRecipient* sendRecipient READ sendRecipient CONSTANT)
    Q_PROPERTY(WalletQmlModelTransaction* currentTransaction READ currentTransaction NOTIFY currentTransactionChanged)

public:
    WalletQmlModel(std::unique_ptr<interfaces::Wallet> wallet, QObject *parent = nullptr);
    WalletQmlModel(QObject *parent = nullptr);
    ~WalletQmlModel() = default;

    QString name() const;
    QString balance() const;
    SendRecipient* sendRecipient() const { return m_current_recipient; }
    WalletQmlModelTransaction* currentTransaction() const { return m_current_transaction; }
    Q_INVOKABLE bool prepareTransaction();
    Q_INVOKABLE void sendTransaction();

Q_SIGNALS:
    void nameChanged();
    void balanceChanged();
    void currentTransactionChanged();

private:
    std::unique_ptr<interfaces::Wallet> m_wallet;
    SendRecipient* m_current_recipient{nullptr};
    WalletQmlModelTransaction* m_current_transaction{nullptr};
};

#endif // BITCOIN_QML_MODELS_WALLETQMLMODEL_H
