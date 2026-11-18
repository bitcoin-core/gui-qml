// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/walletbalanceadapter.h>

#include <qml/walletopening.h>

#include <interfaces/wallet.h>
#include <uint256.h>

namespace walletbalance {
Reader::Read WalletRead(std::weak_ptr<walletopening::Session> opening)
{
    return [opening = std::move(opening)] {
        const auto session = opening.lock();
        const auto handle = session ? session->borrow() : nullptr;
        if (!handle) return Result{Status::Unavailable, {}};
        interfaces::WalletBalances balances;
        uint256 observed_block;
        if (!handle->tryGetBalances(balances, observed_block)) return Result{Status::Busy, {}};
        return Result{Status::Ready, Snapshot{balances.balance, observed_block.ToString()}};
    };
}
} // namespace walletbalance
