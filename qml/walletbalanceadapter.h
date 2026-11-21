// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETBALANCEADAPTER_H
#define BITCOIN_QML_WALLETBALANCEADAPTER_H

#include <qml/models/walletbalancereader.h>

#include <memory>

namespace interfaces { class Wallet; }
namespace walletbalance {
// The closure retains only a weak interface. Each actual worker read borrows
// it locally, so its last handle is released on the worker even after GUI loss.
Reader::Read WalletRead(std::weak_ptr<interfaces::Wallet> wallet);
} // namespace walletbalance

#endif // BITCOIN_QML_WALLETBALANCEADAPTER_H
