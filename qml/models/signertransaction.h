// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_SIGNERTRANSACTION_H
#define BITCOIN_QML_MODELS_SIGNERTRANSACTION_H

#include <primitives/transaction.h>

/** Exact reviewed terms. Signing may add scriptSig/witness, not change terms. */
bool MatchesUnsignedTransaction(const CMutableTransaction& approved, const CMutableTransaction& received);

#endif // BITCOIN_QML_MODELS_SIGNERTRANSACTION_H
