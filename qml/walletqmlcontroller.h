// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETQMLCONTROLLER_H
#define BITCOIN_QML_WALLETQMLCONTROLLER_H

#include <qml/models/walletqmlmodel.h>

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>

#include <memory>

#include <QMutex>
#include <QObject>
#include <QThread>

class WalletQmlController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(WalletQmlModel* selectedWallet READ selectedWallet NOTIFY selectedWalletChanged)
    Q_PROPERTY(bool initialized READ initialized NOTIFY initializedChanged)
    Q_PROPERTY(bool isWalletLoaded READ isWalletLoaded NOTIFY isWalletLoadedChanged)

public:
    explicit WalletQmlController(interfaces::Node& node, QObject *parent = nullptr);
    ~WalletQmlController();

    Q_INVOKABLE void setSelectedWallet(QString path);
    Q_INVOKABLE void createSingleSigWallet(const QString &name, const QString &passphrase);

    WalletQmlModel* selectedWallet() const;
    void unloadWallets();
    bool initialized() const { return m_initialized; }
    bool isWalletLoaded() const { return m_is_wallet_loaded; }
    void setWalletLoaded(bool loaded);

Q_SIGNALS:
    void selectedWalletChanged();
    void initializedChanged();
    void isWalletLoadedChanged();

public Q_SLOTS:
    void initialize();

private:
    void handleLoadWallet(std::unique_ptr<interfaces::Wallet> wallet);

    bool m_initialized{false};
    interfaces::Node& m_node;
    WalletQmlModel* m_selected_wallet;
    QObject* m_worker;
    QThread* m_worker_thread;
    QMutex m_wallets_mutex;
    std::vector<WalletQmlModel*> m_wallets;
    std::unique_ptr<interfaces::Handler> m_handler_load_wallet;
    bool m_is_wallet_loaded{false};

    bilingual_str m_error_message;
    std::vector<bilingual_str> m_warning_messages;
};

#endif // BITCOIN_QML_WALLETQMLCONTROLLER_H
