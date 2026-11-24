// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_WALLETMODELSERVICES_H
#define BITCOIN_QML_TEST_MOCKS_WALLETMODELSERVICES_H

#include <qml/executor.h>
#include <qml/models/walletmodel.h>

inline WalletModel::SharedWallet WalletWithQueues(walletopening::Source source, execution::Executor& executor,
                                        walletopening::Session::Preferences preferences = {}, QString name = {})
{
    WalletModel::SharedWallet wallet{std::move(source), std::move(name), std::move(preferences)};
    wallet.services.core = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Core));
    wallet.services.outcomes = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
    wallet.services.cleanup = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
    return wallet;
}

#endif // BITCOIN_QML_TEST_MOCKS_WALLETMODELSERVICES_H
