// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/signertransaction.h>

#include <algorithm>

bool MatchesUnsignedTransaction(const CMutableTransaction& approved, const CMutableTransaction& received)
{
    if (approved.version != received.version || approved.nLockTime != received.nLockTime ||
        approved.vout != received.vout || approved.vin.size() != received.vin.size()) return false;
    return std::equal(approved.vin.begin(), approved.vin.end(), received.vin.begin(), [](const CTxIn& expected, const CTxIn& actual) {
        return expected.prevout == actual.prevout && expected.nSequence == actual.nSequence;
    });
}
