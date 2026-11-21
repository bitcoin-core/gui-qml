// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtfee.h>

#include <set>

namespace {
bool AddAmount(CAmount& total, CAmount value)
{
    if (!MoneyRange(value) || value > MAX_MONEY - total) return false;
    total += value;
    return true;
}
}

psbtfee::Analysis psbtfee::Analyze(const PartiallySignedTransaction& psbt, const Outputs& verified)
{
    Analysis result;
    const auto tx = psbt.GetUnsignedTx();
    if (!tx || tx->vin.empty() || tx->vout.empty()) return result;
    result.commitment = tx->GetHash();
    for (const auto& output : tx->vout) {
        if (!AddAmount(result.output_total, output.nValue)) return result;
    }
    result.outputs_valid = true;
    CAmount input_total{0};
    bool missing{false};
    std::set<COutPoint> seen;
    for (const auto& input : psbt.inputs) {
        const auto outpoint = input.GetOutPoint();
        if (!seen.insert(outpoint).second) return result;
        if (!input.witness_utxo.IsNull() && !MoneyRange(input.witness_utxo.nValue)) return result;
        const auto trusted = verified.find(outpoint);
        std::optional<CTxOut> prevout;
        if (input.non_witness_utxo) {
            if (input.non_witness_utxo->GetHash() != outpoint.hash || outpoint.n >= input.non_witness_utxo->vout.size()) return result;
            prevout = input.non_witness_utxo->vout[outpoint.n];
            if (trusted != verified.end() && trusted->second != *prevout) return result;
        } else if (trusted != verified.end()) {
            prevout = trusted->second;
        } else {
            missing = true;
        }
        if (prevout) {
            if (!input.witness_utxo.IsNull() && input.witness_utxo != *prevout) return result;
            if (!AddAmount(input_total, prevout->nValue)) return result;
            result.verified.emplace(outpoint, *prevout);
        }
        result.prevouts.push_back(std::move(prevout));
    }
    if (missing) {
        result.state = State::Missing;
        return result;
    }
    if (input_total < result.output_total) return result;
    result.state = State::Known;
    result.amount = input_total - result.output_total;
    return result;
}

psbtfee::Analysis psbtfee::Prepared(const CTransaction& transaction, CAmount fee)
{
    Analysis result;
    if (!MoneyRange(fee)) return result;
    const PartiallySignedTransaction psbt{CMutableTransaction{transaction}, /*version=*/2};
    result.commitment = psbt.GetUnsignedTx()->GetHash();
    result.amount = fee;
    result.state = State::Known;
    result.wallet_prepared = true;
    return result;
}

bool psbtfee::MatchesReview(const PartiallySignedTransaction& psbt, const Analysis& proof, std::optional<CAmount> approved_fee)
{
    if (proof.state != State::Known || !approved_fee || proof.amount != approved_fee) return false;
    const auto tx = psbt.GetUnsignedTx();
    if (!tx || proof.commitment != tx->GetHash()) return false;
    if (proof.wallet_prepared) {
        // This aggregate fee came from Core's construction of these exact
        // inputs/outputs, not from PSBT metadata. Metadata may only contradict
        // that fact; it must never establish or replace it.
        Outputs claims;
        for (const auto& input : psbt.inputs) {
            if (!input.witness_utxo.IsNull()) claims.emplace(input.GetOutPoint(), input.witness_utxo);
        }
        const auto claimed = Analyze(psbt, claims);
        return claimed.state != State::Invalid && (!claimed.amount || claimed.amount == approved_fee);
    }
    return Analyze(psbt, proof.verified).amount == approved_fee;
}

void psbtfee::FillMissing(PartiallySignedTransaction& psbt, const Analysis& proof)
{
    for (auto& input : psbt.inputs) {
        const auto found = proof.verified.find(input.GetOutPoint());
        if (!input.non_witness_utxo && input.witness_utxo.IsNull() && found != proof.verified.end()) input.witness_utxo = found->second;
    }
}
