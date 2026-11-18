// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_WALLETNAMES_H
#define BITCOIN_QML_WALLETNAMES_H
#include <QString>
#include <mutex>
#include <set>
// Observed open names only; never an operation admission/reservation owner.
struct WalletNameRegistry { std::mutex mutex; std::set<QString> names; };
#endif // BITCOIN_QML_WALLETNAMES_H
