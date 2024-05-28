// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETCONTROLLER_H
#define BITCOIN_QML_WALLETCONTROLLER_H

#include <interfaces/node.h>

#include <QObject>
#include <QString>

class WalletController : public QObject
{
    Q_OBJECT

public:
    explicit WalletController(interfaces::Node& node);
    Q_INVOKABLE void createSingleSigWallet(const QString& name, const QString& passphrase);

private:
    interfaces::Node& m_node;
};


#endif // BITCOIN_QML_WALLETCONTROLLER_H
