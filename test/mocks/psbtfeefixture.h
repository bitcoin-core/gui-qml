// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_PSBTFEEFIXTURE_H
#define BITCOIN_QML_TEST_MOCKS_PSBTFEEFIXTURE_H

#include <key.h>
#include <psbt.h>
#include <script/signingprovider.h>
#include <script/solver.h>

#include <stdexcept>
#include <iterator>
#include <utility>
#include <vector>

namespace psbtfeefixture {
struct WithheldScripts {
    PartiallySignedTransaction document;
    CScript witness, redeem;
};
inline WithheldScripts SignedWithoutScripts(bool nested)
{
    CKey first, second;
    first.MakeNewKey(true); second.MakeNewKey(true);
    FillableSigningProvider provider;
    if (!provider.AddKey(first) || !provider.AddKey(second)) throw std::runtime_error("fixture keys");
    const CScript witness = GetScriptForMultisig(2, {first.GetPubKey(), second.GetPubKey()});
    const CScript program = GetScriptForDestination(WitnessV0ScriptHash(witness));
    if (!provider.AddCScript(witness) || (nested && !provider.AddCScript(program))) throw std::runtime_error("fixture scripts");
    CMutableTransaction parent;
    parent.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    parent.vout.emplace_back(2'000, nested ? GetScriptForDestination(ScriptHash(program)) : program);
    const auto previous = MakeTransactionRef(parent);
    CMutableTransaction transaction;
    transaction.vin.emplace_back(COutPoint{previous->GetHash(), 0});
    transaction.vout.emplace_back(1'500, GetScriptForDestination(PKHash(first.GetPubKey())));
    PartiallySignedTransaction document{transaction, 2};
    document.inputs[0].non_witness_utxo = previous;
    const auto data = PrecomputePSBTData(document);
    if (!data || !SignPSBTInput(provider, document, 0, &*data, {.finalize = false})) throw std::runtime_error("fixture signing");
    if (document.inputs[0].partial_sigs.size() != 2) throw std::runtime_error("fixture signatures");
    auto complete = document;
    if (!FinalizePSBT(complete)) throw std::runtime_error("fixture completeness");
    document.inputs[0].witness_script.clear();
    document.inputs[0].redeem_script.clear();
    return {std::move(document), witness, nested ? program : CScript{}};
}
inline constexpr CAmount ACTUAL_FEE{500'000};
struct UnfinishedContribution {
    PartiallySignedTransaction original, returned;
};
inline UnfinishedContribution Contribution(bool include_scripts, bool corrupt)
{
    const auto fixture = SignedWithoutScripts(false);
    auto original = fixture.document;
    original.inputs[0].partial_sigs.clear();
    auto returned = fixture.document;
    returned.inputs[0].partial_sigs.erase(std::next(returned.inputs[0].partial_sigs.begin()));
    if (include_scripts) {
        original.inputs[0].witness_script = fixture.witness;
        returned.inputs[0].witness_script = fixture.witness;
    }
    if (corrupt) returned.inputs[0].partial_sigs.begin()->second.second[5] ^= 1;
    return {std::move(original), std::move(returned)};
}
inline PartiallySignedTransaction Legacy(uint32_t version, bool forged, bool with_change = false)
{
    CKey first, second;
    first.MakeNewKey(true); second.MakeNewKey(true);
    FillableSigningProvider provider;
    if (!provider.AddKey(first) || !provider.AddKey(second)) throw std::runtime_error("fixture keys");
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    previous.vout.emplace_back(1'000'000, GetScriptForDestination(PKHash(first.GetPubKey())));
    previous.vout.emplace_back(1'000'000, GetScriptForDestination(PKHash(second.GetPubKey())));
    const auto parent = MakeTransactionRef(previous);
    CMutableTransaction tx;
    tx.vin.emplace_back(COutPoint{parent->GetHash(), 0});
    tx.vin.emplace_back(COutPoint{parent->GetHash(), 1});
    tx.vout.emplace_back(1'500'000, GetScriptForDestination(PKHash(first.GetPubKey())));
    if (with_change) {
        tx.vout[0] = CTxOut{1'300'000, CScript{} << OP_DUP << OP_HASH160 << std::vector<unsigned char>(20, 0x42) << OP_EQUALVERIFY << OP_CHECKSIG};
        tx.vout.emplace_back(200'000, GetScriptForDestination(PKHash(first.GetPubKey())));
    }
    PartiallySignedTransaction psbt{tx, version};
    for (auto& input : psbt.inputs) input.non_witness_utxo = parent;
    const auto data = PrecomputePSBTData(psbt);
    if (!data) throw std::runtime_error("fixture precompute");
    for (size_t i = 0; i < psbt.inputs.size(); ++i) {
        if (!SignPSBTInput(provider, psbt, i, &*data, {})) throw std::runtime_error("fixture signing");
    }
    if (!FinalizePSBT(psbt)) throw std::runtime_error("fixture finalization");
    if (forged) {
        psbt.inputs[1].non_witness_utxo = nullptr;
        psbt.inputs[1].witness_utxo = parent->vout[1];
        psbt.inputs[1].witness_utxo.nValue -= ACTUAL_FEE - 1;
    }
    return psbt;
}
} // namespace psbtfeefixture

#endif // BITCOIN_QML_TEST_MOCKS_PSBTFEEFIXTURE_H
