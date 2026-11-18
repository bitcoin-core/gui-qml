// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETBALANCEADAPTER_H
#define BITCOIN_QML_WALLETBALANCEADAPTER_H

#include <qml/models/walletbalancereader.h>

#include <memory>

namespace walletopening { class Session; }
namespace walletbalance {
Reader::Read WalletRead(std::weak_ptr<walletopening::Session> opening);
} // namespace walletbalance

#endif // BITCOIN_QML_WALLETBALANCEADAPTER_H
