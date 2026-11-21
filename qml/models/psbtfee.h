// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_PSBTFEE_H
#define BITCOIN_QML_MODELS_PSBTFEE_H

#include <psbt.h>

#include <map>
#include <optional>
#include <vector>

namespace psbtfee {
enum class State { Known, Missing, Invalid };
using Outputs = std::map<COutPoint, CTxOut>;
struct Analysis {
    State state{State::Invalid};
    std::optional<CAmount> amount;
    std::optional<Txid> commitment;
    Outputs verified;
    std::vector<std::optional<CTxOut>> prevouts;
    CAmount output_total{0};
    bool outputs_valid{false};
    bool wallet_prepared{false};
};
/** Only full transaction commitments or independently verified outputs prove input amounts. */
Analysis Analyze(const PartiallySignedTransaction& psbt, const Outputs& verified = {});
Analysis Prepared(const CTransaction& transaction, CAmount fee);
bool MatchesReview(const PartiallySignedTransaction& psbt, const Analysis& proof, std::optional<CAmount> approved_fee);
void FillMissing(PartiallySignedTransaction& psbt, const Analysis& proof);
} // namespace psbtfee

#endif // BITCOIN_QML_MODELS_PSBTFEE_H
