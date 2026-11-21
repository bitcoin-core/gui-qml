// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_SIGNERTRANSACTIONS_H
#define BITCOIN_QML_TEST_MOCKS_SIGNERTRANSACTIONS_H

#include <primitives/transaction.h>
#include <psbt.h>
#include <script/script.h>

#include <array>
#include <utility>
#include <vector>

namespace signerfixture {
enum class Mutation { Version, InputHash, InputIndex, Sequence, InputCount, InputOrder, Destination, Amount, OutputCount, OutputOrder, LockTime };
inline constexpr std::array CASES{
    std::pair{"version", Mutation::Version}, std::pair{"input-hash", Mutation::InputHash},
    std::pair{"input-index", Mutation::InputIndex}, std::pair{"sequence", Mutation::Sequence},
    std::pair{"input-count", Mutation::InputCount}, std::pair{"input-order", Mutation::InputOrder},
    std::pair{"destination", Mutation::Destination}, std::pair{"amount", Mutation::Amount},
    std::pair{"output-count", Mutation::OutputCount}, std::pair{"output-order", Mutation::OutputOrder},
    std::pair{"locktime", Mutation::LockTime}};

inline PartiallySignedTransaction Review()
{
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous.vout.emplace_back(4000, CScript{} << OP_DROP << OP_TRUE);
    previous.vout.emplace_back(3000, CScript{} << OP_DROP << OP_TRUE);
    const auto prev = MakeTransactionRef(previous);
    CMutableTransaction tx;
    tx.nLockTime = 42;
    tx.vin.emplace_back(COutPoint{prev->GetHash(), 0}, CScript{}, 0xfffffffd);
    tx.vin.emplace_back(COutPoint{prev->GetHash(), 1}, CScript{}, 0xfffffffc);
    tx.vout.emplace_back(2000, CScript{} << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, 1) << OP_EQUALVERIFY << OP_CHECKSIG);
    tx.vout.emplace_back(4500, CScript{} << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, 2) << OP_EQUALVERIFY << OP_CHECKSIG);
    PartiallySignedTransaction psbt{tx, /*version=*/2};
    for (auto& input : psbt.inputs) input.non_witness_utxo = prev;
    return psbt;
}

inline void Change(CMutableTransaction& tx, Mutation mutation)
{
    switch (mutation) {
    case Mutation::Version: ++tx.version; break;
    case Mutation::InputHash: tx.vin[0].prevout.hash = Txid::FromUint256(uint256::ONE); break;
    case Mutation::InputIndex: tx.vin[0].prevout.n ^= 1; break;
    case Mutation::Sequence: --tx.vin[0].nSequence; break;
    case Mutation::InputCount: tx.vin.push_back(tx.vin.back()); break;
    case Mutation::InputOrder: std::swap(tx.vin[0], tx.vin[1]); break;
    case Mutation::Destination: tx.vout[0].scriptPubKey << OP_TRUE; break;
    case Mutation::Amount: --tx.vout[0].nValue; break;
    case Mutation::OutputCount: tx.vout.emplace_back(1, CScript{} << OP_TRUE); break;
    case Mutation::OutputOrder: std::swap(tx.vout[0], tx.vout[1]); break;
    case Mutation::LockTime: ++tx.nLockTime; break;
    }
}
} // namespace signerfixture
#endif // BITCOIN_QML_TEST_MOCKS_SIGNERTRANSACTIONS_H
