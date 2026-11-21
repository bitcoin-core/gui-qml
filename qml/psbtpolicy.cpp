// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/psbtpolicy.h>

#include <coins.h>
#include <consensus/consensus.h>
#include <consensus/tx_verify.h>
#include <core_io.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <policy/policy.h>
#include <univalue.h>
#include <util/result.h>

#include <QCoreApplication>

#include <algorithm>
#include <limits>
#include <map>
#include <utility>

namespace {
QString Tr(const char* text) { return QCoreApplication::translate("PsbtQmlModel", text); }

void ReadAdmission(interfaces::Node& node, const CTransaction& transaction, psbtpolicy::Facts& facts)
{
    try {
        UniValue raw_transactions{UniValue::VARR};
        raw_transactions.push_back(EncodeHexTx(transaction));
        UniValue parameters{UniValue::VARR};
        parameters.push_back(std::move(raw_transactions));
        parameters.push_back(0); // Disable only testmempoolaccept's independent client max-fee-rate cap.
        const UniValue response = node.executeRpc("testmempoolaccept", parameters, "");
        if (!response.isArray() || response.size() != 1) return;
        const UniValue& result = response[0];
        if (!result.isObject() || !result.exists("txid") || !result["txid"].isStr()
            || !result.exists("wtxid") || !result["wtxid"].isStr()
            || result["txid"].get_str() != transaction.GetHash().ToString()
            || result["wtxid"].get_str() != transaction.GetWitnessHash().ToString()
            || !result.exists("allowed") || !result["allowed"].isBool()) return;

        const bool allowed = result["allowed"].get_bool();
        QString reason;
        if (!allowed && result.exists("reject-reason")) {
            if (!result["reject-reason"].isStr()) return;
            reason = QString::fromStdString(result["reject-reason"].get_str()).left(1024);
        }
        facts.admission = allowed ? psbtpolicy::Admission::Allowed : psbtpolicy::Admission::Rejected;
        facts.admission_error = std::move(reason);
        if (!allowed) return;
        // An allowed conflict is a replacement. Name what it evicts.
        UniValue outpoints{UniValue::VARR};
        for (const auto& input : transaction.vin) {
            UniValue outpoint{UniValue::VOBJ};
            outpoint.pushKV("txid", input.prevout.hash.GetHex());
            outpoint.pushKV("vout", uint64_t{input.prevout.n});
            outpoints.push_back(std::move(outpoint));
        }
        UniValue spending_parameters{UniValue::VARR};
        spending_parameters.push_back(std::move(outpoints));
        const UniValue spending = node.executeRpc("gettxspendingprevout", spending_parameters, "");
        if (!spending.isArray()) return;
        for (const auto& item : spending.getValues()) {
            if (!item.isObject() || !item.exists("spendingtxid") || !item["spendingtxid"].isStr()) continue;
            const auto txid = QString::fromStdString(item["spendingtxid"].get_str());
            if (txid != QString::fromStdString(transaction.GetHash().ToString()) && !facts.replaces.contains(txid)) facts.replaces.push_back(txid);
        }
    } catch (...) {
        // An RPC exception is an unavailable observation, not a transaction result.
    }
}

void ClearChainFacts(psbtpolicy::Facts& facts)
{
    facts.observed = false;
    facts.missing_inputs = false;
    facts.immature_inputs = false;
    facts.final = true;
    facts.sufficient_fee = true;
    facts.chain_limits = true;
    facts.tip.clear();
    facts.chain_error.clear();
}

void ClearAdmissionFacts(psbtpolicy::Facts& facts)
{
    facts.admission = psbtpolicy::Admission::Unknown;
    facts.admission_error.clear();
    facts.replaces.clear();
}
} // namespace

psbtpolicy::Read psbtpolicy::ReadNode(interfaces::Node* node, interfaces::Chain* chain)
{
    return [node, chain](CTransactionRef transaction, CAmount fee) {
        Facts facts;
        if (!node || !transaction) return facts;
        if (chain && MoneyRange(fee)) {
            try {
                const auto tip = node->getBestBlockHash();
                int height{-1}; int64_t mtp{0}; bool active{false};
                if (chain->findBlock(tip, interfaces::FoundBlock{}.height(height).mtpTime(mtp).inActiveChain(active))
                    && active && height >= 0 && height < std::numeric_limits<int>::max()) {
                    std::map<COutPoint, Coin> coins;
                    for (const auto& input : transaction->vin) coins.try_emplace(input.prevout);
                    chain->findCoins(coins); // Includes available mempool parents.
                    for (const auto& [outpoint, coin] : coins) {
                        facts.missing_inputs |= coin.IsSpent();
                        facts.immature_inputs |= !coin.IsSpent() && coin.IsCoinBase()
                            && int64_t(height) + 1 - coin.nHeight < COINBASE_MATURITY;
                    }
                    facts.final = IsFinalTx(*transaction, height + 1, mtp);
                    const auto minimum = std::max(chain->mempoolMinFee(), chain->relayMinFee());
                    facts.sufficient_fee = fee >= minimum.GetFee(GetVirtualTransactionSize(*transaction));
                    const auto limits = chain->checkChainLimits(transaction);
                    facts.chain_limits = bool(limits);
                    if (!limits) facts.chain_error = QString::fromStdString(util::ErrorString(limits).translated).left(4096);
                    // A reorg during this collection invalidates the whole observation.
                    if (node->getBestBlockHash() == tip) {
                        facts.tip = QString::fromStdString(tip.ToString());
                        facts.observed = true;
                    } else {
                        ClearChainFacts(facts);
                    }
                }
            } catch (...) {
                ClearChainFacts(facts);
            }
        }
        try {
            const auto admission_tip = node->getBestBlockHash();
            if (facts.observed && facts.tip != QString::fromStdString(admission_tip.ToString())) {
                ClearChainFacts(facts);
            }
            ReadAdmission(*node, *transaction, facts);
            // Drop both observations if the active tip changed while the RPC ran.
            if (node->getBestBlockHash() != admission_tip) {
                ClearChainFacts(facts);
                ClearAdmissionFacts(facts);
            }
        } catch (...) {
            ClearAdmissionFacts(facts);
        }
        return facts;
    };
}

psbtpolicy::Decision psbtpolicy::Evaluate(const Facts& facts)
{
    if (facts.observed) {
        if (facts.missing_inputs) return {State::Blocked, Tr("An input is missing or already spent at the observed chain tip. Refresh the review before submitting.")};
        if (facts.immature_inputs) return {State::Blocked, Tr("An input spends an immature coinbase output. Refresh the review after more blocks.")};
        if (!facts.final) return {State::Blocked, Tr("The transaction lock time is not final at the observed chain tip.")};
        if (!facts.sufficient_fee) return {State::Blocked, Tr("The fee is below the node's current advertised minimum.")};
        if (!facts.chain_limits) return {State::Blocked, facts.chain_error.isEmpty() ? Tr("The transaction exceeds the node's current mempool chain limits.") : facts.chain_error};
    }
    if (facts.admission == Admission::Rejected) {
        const QString reason = facts.admission_error.isEmpty() ? Tr("The node currently rejects this transaction.") : facts.admission_error;
        return {State::Blocked, reason};
    }
    if (facts.admission == Admission::Allowed) {
        if (!facts.replaces.isEmpty()) {
            return {State::NoKnownObstacle, Tr("The node currently accepts this exact transaction as a replacement. Submitting it removes unconfirmed transaction %1 from the mempool.").arg(facts.replaces.join(QStringLiteral(", ")))};
        }
        return {State::NoKnownObstacle, Tr("The node currently accepts this exact transaction. Submission will check current policy again.")};
    }
    if (facts.observed) return {State::NoKnownObstacle, Tr("Fully signed. Read-only node checks found no obstacle; submission will check current policy, including conflicts and replacements.")};
    return {State::Unknown, Tr("Fully signed. Current node policy will be checked when you submit.")};
}
