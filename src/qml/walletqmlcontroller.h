// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETQMLCONTROLLER_H
#define BITCOIN_QML_WALLETQMLCONTROLLER_H

#include <qml/models/walletqmlmodel.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>

#include <QMutex>
#include <QObject>
#include <QThread>
#include <memory>

class WalletQmlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletQmlModel* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)

public:
    explicit WalletQmlController(interfaces::Node& node, QObject *parent = nullptr);
    ~WalletQmlController();

    Q_INVOKABLE void setSelectedWallet(QString path);

    WalletQmlModel* selectedWallet() const;
    void unloadWallets();

Q_SIGNALS:
    void selectedWalletChanged();

public Q_SLOTS:
    void initialize();

private:
    void handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet);

    interfaces::Node& m_node;
    WalletQmlModel* m_selected_wallet;
    QObject* m_worker;
    QThread* m_worker_thread;
    QMutex m_wallets_mutex;
    std::vector<WalletQmlModel*> m_wallets;
    std::unique_ptr<interfaces::Handler> m_handler_load_wallet;
};

#endif // BITCOIN_QML_WALLETQMLCONTROLLER_H
