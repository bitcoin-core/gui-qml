// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETRECEIVEADAPTER_H
#define BITCOIN_QML_WALLETRECEIVEADAPTER_H
#include <qml/models/walletreceivemodel.h>
#include <qml/models/backuppolicyio.h>

WalletReceiveModel::Ports MakeReceivePorts(const std::shared_ptr<walletopening::Session>& opening,
    const std::shared_ptr<walletrequests::Store>& store, execution::SnapshotServices execution,
    std::shared_ptr<backup::InstallCoordinator> coordinator);
#endif // BITCOIN_QML_WALLETRECEIVEADAPTER_H
