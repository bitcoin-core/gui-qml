// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_WALLETTRANSACTION_H
#define BITCOIN_QML_WALLETTRANSACTION_H

#include <interfaces/wallet.h>
#include <util/result.h>
#include <wallet/coincontrol.h>
#include <wallet/types.h>

namespace wallettransaction {
// Core-lane adapter. Construct and enforce wallet policy through the public
// interface, without accessing CWallet, its mutex, flags or signing providers.
util::Result<wallet::CreatedTransactionResult> Create(interfaces::Wallet& wallet,
    const std::vector<wallet::CRecipient>& recipients, const wallet::CCoinControl& control,
    int remainder, bool sign);
// Unknown means the public observations could not establish a coherent policy result.
std::optional<bool> Sweeps(interfaces::Wallet& wallet, const CTransaction& transaction,
            const wallet::CCoinControl& control);
std::optional<wallet::CreatedTransactionResult> InsufficientFeeProbe(interfaces::Wallet& wallet,
    const std::vector<wallet::CRecipient>& recipients, const wallet::CCoinControl& control);
} // namespace wallettransaction

#endif // BITCOIN_QML_WALLETTRANSACTION_H
