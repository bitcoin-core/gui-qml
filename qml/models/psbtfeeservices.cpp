// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtfeeservices.h>
#include <qml/psbtdocument.h>
#include <qml/retainedvalue.h>

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
                                     std::shared_ptr<const Analysis> seed, std::function<void(Resolution)> publish)
{
    if (!services.scheduler || !services.lookup || !services.post) return {{}, execution::Rejection::Transport};
    // The GUI only receives aliases. Dropped or obsolete callbacks never free
    // a PSBT, authenticated output map or raw transaction on the GUI thread.
    auto output = execution::RetainValues<Resolution>(services.scheduler, [] { return std::make_unique<Resolution>(); });
    if (!output) return {{}, execution::Rejection::Closed};
    auto post = [transport = services.post, publish = std::move(publish)](Resolution resolution) {
        auto value = std::make_shared<Resolution>(std::move(resolution));
        transport([publish, value] { publish(std::move(*value)); });
    };
    return execution::Submit(*services.scheduler,
        [document = std::move(document), seed = std::move(seed), lookup = services.lookup, policy = services.policy, output] {
            *output = Resolve(*document, *seed, lookup, policy);
            auto value = *output;
            if (value.proof) value.proof = std::shared_ptr<const Analysis>{output, value.proof.get()};
            if (value.analysis) value.analysis = std::shared_ptr<const psbtwork::Snapshot>{output, value.analysis.get()};
            if (value.transaction) value.transaction = CTransactionRef{output, value.transaction.get()};
            return value;
        },
        post, [post](std::exception_ptr) { post({{}, false, true}); });
}
