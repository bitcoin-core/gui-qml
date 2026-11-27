// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtfeeservices.h>
#include <qml/executor.h>
#include <qml/psbtdocument.h>
#include <qml/retainedvalue.h>
#include <qml/walletopening.h>

#include <coins.h>
#include <interfaces/node.h>
#include <univalue.h>
#include <util/moneystr.h>
#include <util/strencodings.h>

psbtfee::Lookup psbtfee::NodeLookup(interfaces::Node* node)
{
    if (!node) return {};
    return [node](const COutPoint& outpoint) -> std::optional<CTxOut> {
        const auto coin = node->getUnspentOutput(outpoint);
        if (coin && !coin->IsSpent()) return coin->out;
        // The pinned Node coin API reads confirmed chain state only. The public
        // RPC interface also authenticates unconfirmed outputs in the mempool,
        // without accessing the internal node context or running work on GUI.
        UniValue parameters{UniValue::VARR};
        parameters.push_back(outpoint.hash.ToString());
        parameters.push_back(outpoint.n);
        parameters.push_back(true);
        const auto output = node->executeRpc("gettxout", parameters, "");
        if (output.isNull()) return std::nullopt;
        const auto amount = ParseMoney(output.find_value("value").getValStr());
        const auto script = TryParseHex<unsigned char>(output.find_value("scriptPubKey").find_value("hex").get_str());
        if (!amount || !script) return std::nullopt;
        return CTxOut{*amount, CScript{script->begin(), script->end()}};
    };
}

psbtfee::Resolution psbtfee::Resolve(const PartiallySignedTransaction& document, const Analysis& seed, const Lookup& lookup,
                                         const psbtpolicy::Read& policy)
{
    auto verified = seed.verified;
    auto proof = Analyze(document, verified);
    if (proof.state == State::Missing) {
        for (const auto& input : document.inputs) {
            const auto outpoint = input.GetOutPoint();
            if (input.non_witness_utxo || verified.contains(outpoint)) continue;
            if (auto output = lookup(outpoint)) verified.emplace(outpoint, std::move(*output));
        }
        proof = Analyze(document, verified);
    }
    auto snapshot = std::make_shared<psbtwork::Snapshot>();
    snapshot->document = std::make_shared<const PartiallySignedTransaction>(document);
    snapshot->proof = std::make_shared<const Analysis>(std::move(proof));
    auto candidate = document;
    if (snapshot->proof->state == State::Known) {
        FillMissing(candidate, *snapshot->proof);
        snapshot->complete = FinalizePSBT(candidate);
    }
    snapshot->assessment = std::make_shared<const PartiallySignedTransaction>(std::move(candidate));
    psbtdocument::Qualify(*snapshot);
    Resolution result{snapshot->proof, snapshot->complete, false};
    if (snapshot->signature_state == psbtwork::SignatureState::Verified) {
        auto analyzed = *snapshot->assessment;
        CMutableTransaction transaction;
        if (FinalizeAndExtractPSBT(analyzed, transaction)) {
            result.transaction = MakeTransactionRef(std::move(transaction));
            if (policy) snapshot->node_facts = policy(result.transaction, *snapshot->proof->amount);
            psbtdocument::Qualify(*snapshot);
            result.node_policy_status = psbtpolicy::Evaluate(snapshot->node_facts).explanation;
        } else {
            snapshot->can_broadcast = false;
            snapshot->metadata_failed = true;
        }
    }
    result.analysis = std::move(snapshot);
    return result;
}

execution::SubmitResult psbtfee::Request(const Services& services, std::shared_ptr<const PartiallySignedTransaction> document,
                                         std::shared_ptr<const Analysis> seed, const std::shared_ptr<FeeRequest>& request,
                                         const std::shared_ptr<walletopening::Session>& opening)
{
    if (!services.lookup || !services.post) return {{}, execution::Rejection::Transport};
    const auto submit = opening ? opening->observationPost() : nullptr;
    if (!opening && (!services.core || !services.utility || !services.value_cleanup)) return {{}, execution::Rejection::Transport};
    if (opening && (!submit || !opening->available())) return {{}, execution::Rejection::Closed};
    // A retained result owns the PSBT, authenticated output map and raw
    // transaction on the same cleanup queue even if GUI drops the review.
    const auto make_output = [] { return std::make_unique<Resolution>(); };
    auto output = opening ? walletopening::RetainValues<Resolution>(opening, make_output)
                          : execution::RetainValues<Resolution>(services.value_cleanup, make_output);
    if (!output) return {{}, execution::Rejection::Closed};
    auto work = [opening, document = std::move(document), seed = std::move(seed),
                 lookup = services.lookup, policy = services.policy, output] {
        if (opening && !opening->available()) throw execution::NotStarted{execution::NotStarted::Cause::Cancelled};
        *output = Resolve(*document, *seed, lookup, policy);
        auto value = *output;
        if (value.proof) value.proof = std::shared_ptr<const Analysis>{output, value.proof.get()};
        if (value.analysis) value.analysis = std::shared_ptr<const psbtwork::Snapshot>{output, value.analysis.get()};
        if (value.transaction) value.transaction = CTransactionRef{output, value.transaction.get()};
        return value;
    };
    return request->startWith([&](auto done, auto failed) {
        auto task = execution::MakeTask(std::move(work), std::move(done), std::move(failed));
        if (submit) return submit(std::move(task));
        auto delivery = std::make_shared<execution::ValueCleanupQueue>(services.utility);
        return delivery->postWithOutcome(*services.core, std::move(task));
    });
}
