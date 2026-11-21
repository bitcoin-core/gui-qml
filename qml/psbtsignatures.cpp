// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/psbtsignatures.h>

#include <psbt.h>
#include <addresstype.h>
#include <script/interpreter.h>
#include <script/solver.h>
#include <script/verify_flags.h>

#include <algorithm>
#include <limits>
#include <optional>

namespace {
bool HasCodeSeparator(const CScript& script)
{
    auto cursor = script.begin();
    opcodetype opcode;
    while (cursor != script.end()) {
        if (!script.GetOp(cursor, opcode) || opcode == OP_CODESEPARATOR) return true;
    }
    return false;
}

struct EcdsaContext { CScript script; SigVersion version{SigVersion::BASE}; };
std::optional<bool> ContainsSigningKey(const CScript& script, const CPubKey& key)
{
    std::vector<std::vector<unsigned char>> solutions;
    const auto type = Solver(script, solutions);
    const std::vector<unsigned char> bytes{key.begin(), key.end()};
    if (type == TxoutType::PUBKEY) return solutions.front() == bytes;
    if (type == TxoutType::PUBKEYHASH) {
        const auto id = key.GetID();
        return solutions.front() == std::vector<unsigned char>{id.begin(), id.end()};
    }
    if (type == TxoutType::MULTISIG) return std::find(solutions.begin() + 1, solutions.end() - 1, bytes) != solutions.end() - 1;
    return {};
}
std::optional<EcdsaContext> GetEcdsaContext(const PSBTInput& input, const CTxOut& utxo)
{
    EcdsaContext result{utxo.scriptPubKey};
    if (result.script.IsPayToScriptHash()) {
        if (input.redeem_script.empty() || GetScriptForDestination(ScriptHash(input.redeem_script)) != result.script) return {};
        result.script = input.redeem_script;
    }
    int version;
    std::vector<unsigned char> program;
    if (result.script.IsWitnessProgram(version, program)) {
        if (version != 0) return {};
        result.version = SigVersion::WITNESS_V0;
        if (program.size() == 20) {
            result.script = CScript{} << OP_DUP << OP_HASH160 << program << OP_EQUALVERIFY << OP_CHECKSIG;
        } else if (program.size() == 32 && !input.witness_script.empty() &&
                   GetScriptForDestination(WitnessV0ScriptHash(input.witness_script)) == result.script) {
            result.script = input.witness_script;
        } else return {};
    }
    if (HasCodeSeparator(result.script)) return {};
    return result;
}

bool HasTapleafContext(const PSBTInput& input, const std::vector<unsigned char>& program,
                       const XOnlyPubKey& key, const uint256& leaf)
{
    for (const auto& [script_version, controls] : input.m_tap_scripts) {
        const auto& [bytes, version] = script_version;
        if (version != TAPROOT_LEAF_TAPSCRIPT || ComputeTapleafHash(version, bytes) != leaf) continue;
        const CScript script{bytes.begin(), bytes.end()};
        if (HasCodeSeparator(script)) continue;
        // Partial verification does not execute a script. Recognize one
        // unambiguous signing policy and quarantine other script forms.
        auto cursor = script.begin();
        opcodetype opcode;
        std::vector<unsigned char> push;
        if (!script.GetOp(cursor, opcode, push) || push.size() != 32 || !std::equal(push.begin(), push.end(), key.begin()) ||
            !script.GetOp(cursor, opcode) || opcode != OP_CHECKSIG || cursor != script.end()) continue;
        for (const auto& control : controls) {
            if (control.size() < TAPROOT_CONTROL_BASE_SIZE || control.size() > TAPROOT_CONTROL_MAX_SIZE ||
                (control.size() - TAPROOT_CONTROL_BASE_SIZE) % TAPROOT_CONTROL_NODE_SIZE != 0 ||
                (control[0] & TAPROOT_LEAF_MASK) != version) continue;
            const XOnlyPubKey internal{std::span{control}.subspan(1, 32)};
            if (XOnlyPubKey{program}.CheckTapTweak(internal, ComputeTaprootMerkleRoot(control, leaf), control[0] & 1)) return true;
        }
    }
    return false;
}
} // namespace

psbtsignatures::Status psbtsignatures::VerifyPartialInputs(const PartiallySignedTransaction& document)
{
    if (!VerifyFinalizedInputs(document, false)) return Status::Invalid;
    const auto transaction = document.GetUnsignedTx();
    const auto data = PrecomputePSBTData(document);
    if (!transaction || !data || transaction->vin.size() != document.inputs.size()) return Status::Unknown;
    Status result{Status::Valid};
    for (size_t i = 0; i < document.inputs.size(); ++i) {
        const auto& input = document.inputs[i];
        CTxOut utxo;
        if (!input.GetUTXO(utxo)) { result = Status::Unknown; continue; }
        MutableTransactionSignatureChecker checker{&*transaction, static_cast<unsigned int>(i), utxo.nValue, *data, MissingDataBehavior::FAIL};
        const auto context = GetEcdsaContext(input, utxo);
        for (const auto& [id, signature] : input.partial_sigs) {
            const auto& [key, bytes] = signature;
            if (!key.IsFullyValid() || key.GetID() != id ||
                !CheckSignatureEncoding(bytes, SCRIPT_VERIFY_DERSIG | SCRIPT_VERIFY_STRICTENC | SCRIPT_VERIFY_LOW_S, nullptr) || bytes.empty()) return Status::Invalid;
            if (!context) { result = Status::Unknown; continue; }
            const auto member = ContainsSigningKey(context->script, key);
            if (!member) { result = Status::Unknown; continue; }
            if (!*member) return Status::Invalid;
            if (
                (context->version == SigVersion::WITNESS_V0 && !key.IsCompressed()) ||
                !checker.CheckECDSASignature(bytes, {key.begin(), key.end()}, context->script, context->version)) return Status::Invalid;
        }
        int version;
        std::vector<unsigned char> program;
        const bool taproot = utxo.scriptPubKey.IsWitnessProgram(version, program) && version == 1 && program.size() == 32;
        if ((!input.m_tap_key_sig.empty() || !input.m_tap_script_sigs.empty()) && !data->m_spent_outputs_ready) {
            result = Status::Unknown;
            continue;
        }
        if (!input.m_tap_key_sig.empty()) {
            if (!taproot) return Status::Invalid;
            ScriptExecutionData execution;
            execution.m_annex_init = true;
            execution.m_annex_present = false;
            if (!checker.CheckSchnorrSignature(input.m_tap_key_sig, program, SigVersion::TAPROOT, execution)) return Status::Invalid;
        }
        for (const auto& [key_leaf, signature] : input.m_tap_script_sigs) {
            const auto& [key, leaf] = key_leaf;
            if (!taproot) return Status::Invalid;
            if (!HasTapleafContext(input, program, key, leaf)) { result = Status::Unknown; continue; }
            ScriptExecutionData execution;
            execution.m_annex_init = true;
            execution.m_annex_present = false;
            execution.m_tapleaf_hash_init = true;
            execution.m_tapleaf_hash = leaf;
            execution.m_codeseparator_pos_init = true;
            execution.m_codeseparator_pos = std::numeric_limits<uint32_t>::max();
            if (!checker.CheckSchnorrSignature(signature, key, SigVersion::TAPSCRIPT, execution)) return Status::Invalid;
        }
        // Do not recreate the MuSig2 protocol or an interpreter here.
        if (!input.m_musig2_partial_sigs.empty()) result = Status::Unknown;
    }
    return result;
}

psbtsignatures::Status psbtsignatures::VerifyContributions(const PartiallySignedTransaction& original,
                                                        const PartiallySignedTransaction& returned)
{
    if (original.inputs.size() != returned.inputs.size()) return Status::Invalid;
    auto contributions = returned;
    const auto remove_unchanged = [](auto& values, const auto& prior) {
        std::erase_if(values, [&prior](const auto& entry) {
            const auto found = prior.find(entry.first);
            return found != prior.end() && found->second == entry.second;
        });
    };
    for (size_t i = 0; i < contributions.inputs.size(); ++i) {
        auto& input = contributions.inputs[i];
        const auto& prior = original.inputs[i];
        remove_unchanged(input.partial_sigs, prior.partial_sigs);
        remove_unchanged(input.m_tap_script_sigs, prior.m_tap_script_sigs);
        if (input.m_tap_key_sig == prior.m_tap_key_sig) input.m_tap_key_sig.clear();
        for (auto& [group, values] : input.m_musig2_partial_sigs) {
            const auto found = prior.m_musig2_partial_sigs.find(group);
            if (found != prior.m_musig2_partial_sigs.end()) remove_unchanged(values, found->second);
        }
        std::erase_if(input.m_musig2_partial_sigs, [](const auto& group) { return group.second.empty(); });
    }
    return VerifyPartialInputs(contributions);
}

bool psbtsignatures::VerifyFinalizedInputs(const PartiallySignedTransaction& document, bool require_all)
{
    if (document.inputs.empty()) return false;
    std::optional<PrecomputedTransactionData> data;
    for (size_t i = 0; i < document.inputs.size(); ++i) {
        if (!PSBTInputSigned(document.inputs[i])) {
            if (require_all) return false;
            continue;
        }
        if (!data) data = PrecomputePSBTData(document);
        if (!data || !PSBTInputSignedAndVerified(document, i, &*data)) return false;
    }
    return true;
}
