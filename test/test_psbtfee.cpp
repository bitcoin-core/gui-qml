// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/psbtfeeservices.h>
#include <qml/psbtpolicy.h>
#include <qml/psbtwork.h>
#include <test/mocks/psbtfeefixture.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/feepreviewchain.h>
#include <test/qt_test_registry.h>

#include <core_io.h>
#include <key.h>
#include <script/script.h>
#include <script/solver.h>

#include <QtTest/QtTest>

#include <limits>
#include <vector>

namespace {
PartiallySignedTransaction Amounts(const std::vector<CAmount>& input_values, const std::vector<CAmount>& output_values)
{
    CMutableTransaction previous;
    previous.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    for (auto value : input_values) previous.vout.emplace_back(value, CScript{} << OP_TRUE);
    const auto parent = MakeTransactionRef(previous);
    CMutableTransaction tx;
    for (size_t i = 0; i < input_values.size(); ++i) tx.vin.emplace_back(COutPoint{parent->GetHash(), uint32_t(i)});
    for (auto value : output_values) tx.vout.emplace_back(value, CScript{} << OP_TRUE);
    PartiallySignedTransaction psbt{tx, /*version=*/2};
    for (auto& input : psbt.inputs) input.non_witness_utxo = parent;
    return psbt;
}
PartiallySignedTransaction Witness(const CScript& script)
{
    auto psbt = Amounts({2000}, {1500});
    psbt.inputs[0].non_witness_utxo = nullptr;
    psbt.inputs[0].witness_utxo = CTxOut{2000, script};
    return psbt;
}

class AdmissionNode : public MockNode
{
public:
    int change_tip_on_read{0};
    int best_hash_reads{0};

    uint256 getBestBlockHash() override
    {
        ++best_hash_reads;
        return change_tip_on_read > 0 && best_hash_reads >= change_tip_on_read ? uint256{} : uint256::ONE;
    }
};

class AdmissionChain : public FeePreviewChain
{
public:
    bool input_spent{false};
    bool unavailable_tip{false};

    bool findBlock(const uint256&, const interfaces::FoundBlock& block = {}) override
    {
        if (unavailable_tip) return false;
        if (block.m_height) *block.m_height = 200;
        if (block.m_mtp_time) *block.m_mtp_time = 1'000;
        if (block.m_in_active_chain) *block.m_in_active_chain = true;
        block.found = true;
        return true;
    }

    void findCoins(std::map<COutPoint, Coin>& coins) override
    {
        for (auto& [outpoint, coin] : coins) {
            coin = input_spent ? Coin{} : Coin{CTxOut{2'000, CScript{} << OP_TRUE}, 1, false};
        }
    }

    CFeeRate mempoolMinFee() override { return CFeeRate{0}; }
    CFeeRate relayMinFee() override { return CFeeRate{0}; }
};

UniValue AdmissionResult(const CTransaction& transaction, bool allowed, std::string reason = {})
{
    UniValue result{UniValue::VOBJ};
    result.pushKV("txid", transaction.GetHash().ToString());
    result.pushKV("wtxid", transaction.GetWitnessHash().ToString());
    result.pushKV("allowed", allowed);
    if (!allowed) result.pushKV("reject-reason", std::move(reason));
    return result;
}
} // namespace

class PsbtFeeTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void malformedRpcReplySettlesAsFailure()
    {
        MockNode node;
        node.get_unspent_output_fn = [](const COutPoint&) { return std::optional<Coin>{}; };
        node.execute_rpc_fn = [](const std::string&, const UniValue&, const std::string&) { return UniValue{true}; };
        const auto document = std::make_shared<const PartiallySignedTransaction>(Witness(CScript{} << OP_TRUE));
        const auto seed = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*document));
        ManualExecutionEnvironment environment;
        bool published{false}, failed{false};
        psbtfee::Services services{environment.scheduler, psbtfee::NodeLookup(&node),
            [&environment](auto publication) { environment.post(std::move(publication)); }};
        QVERIFY(psbtfee::Request(services, document, seed, [&](psbtfee::Resolution result) {
            published = true; failed = result.failed; QVERIFY(!result.proof);
        }));
        environment.drain(); QVERIFY(!published);
        environment.publishAll(); QVERIFY(published); QVERIFY(failed); QVERIFY(environment.quiescent());
    }

    void mempoolLookupUsesPublicNodeInterface_data()
    {
        QTest::addColumn<QString>("amount"); QTest::addColumn<QString>("script"); QTest::addColumn<bool>("known");
        QTest::newRow("valid-mempool-output") << QString("0.00002000") << QString("51") << true;
        QTest::newRow("zero-value") << QString("0.00000000") << QString("51") << true;
        QTest::newRow("empty-script") << QString("0.00002000") << QString("") << true;
        QTest::newRow("invalid-money") << QString("bad") << QString("51") << false;
        QTest::newRow("invalid-script") << QString("0.00002000") << QString("zz") << false;
    }
    void mempoolLookupUsesPublicNodeInterface()
    {
        QFETCH(QString, amount); QFETCH(QString, script); QFETCH(bool, known);
        MockNode node;
        const COutPoint outpoint{Txid::FromUint256(uint256::ONE), 3};
        node.get_unspent_output_fn = [](const COutPoint&) { return std::optional<Coin>{}; };
        std::string called_method, called_uri;
        UniValue called_parameters;
        node.execute_rpc_fn = [&](const std::string& method, const UniValue& parameters, const std::string& uri) {
            called_method = method; called_parameters = parameters; called_uri = uri;
            UniValue output{UniValue::VOBJ}, pubkey{UniValue::VOBJ};
            output.pushKV("value", amount.toStdString());
            pubkey.pushKV("hex", script.toStdString()); output.pushKV("scriptPubKey", pubkey);
            return output;
        };
        const auto result = psbtfee::NodeLookup(&node)(outpoint);
        QCOMPARE(called_method, std::string("gettxout")); QCOMPARE(called_uri, std::string{});
        QCOMPARE(called_parameters.size(), size_t{3});
        QCOMPARE(called_parameters[0].get_str(), outpoint.hash.ToString());
        QCOMPARE(called_parameters[1].getInt<uint32_t>(), outpoint.n);
        QVERIFY(called_parameters[2].get_bool());
        QCOMPARE(result.has_value(), known);
        if (known) {
            QCOMPARE(result->nValue, amount == "0.00000000" ? CAmount{0} : CAmount{2000});
            QCOMPARE(result->scriptPubKey, script.isEmpty() ? CScript{} : CScript{} << OP_TRUE);
        }
        QCOMPARE(node.calls.getUnspentOutput.load(), 1); QCOMPARE(node.calls.executeRpc.load(), 1);
    }

    void malformedMetadataCannotMatchAReview()
    {
        auto document = Amounts({2000, 2000}, {3000});
        const auto proof = psbtfee::Analyze(document);
        document.inputs[0].witness_utxo = CTxOut{-2, CScript{} << OP_TRUE};
        QCOMPARE(psbtfee::Analyze(document).state, psbtfee::State::Invalid);
        QVERIFY(!psbtfee::MatchesReview(document, proof, CAmount{1000}));
        const auto prepared = psbtfee::Prepared(CTransaction{*document.GetUnsignedTx()}, 1000);
        QVERIFY(!psbtfee::MatchesReview(document, prepared, CAmount{1000}));
        document.inputs[0].witness_utxo.SetNull();
        document.inputs[0].height_locktime = 42;
        document.inputs[1].time_locktime = 500000001;
        QVERIFY(!psbtfee::MatchesReview(document, proof, CAmount{1000}));
    }
    void resolvedProofOwnsTheMatchingAssessmentAndRawCandidate_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::newRow("verified-legacy") << 0;
        QTest::newRow("invalid-finalized-key") << 1;
        QTest::newRow("unavailable-output-proof") << 2;
    }
    void resolvedProofOwnsTheMatchingAssessmentAndRawCandidate()
    {
        QFETCH(int, scenario);
        ECC_Context ecc;
        auto source = psbtfeefixture::Legacy(2, false);
        auto expected = source;
        CMutableTransaction transaction; QVERIFY(FinalizeAndExtractPSBT(expected, transaction));
        psbtfee::Outputs outputs;
        for (auto& input : source.inputs) {
            outputs.emplace(input.GetOutPoint(), input.non_witness_utxo->vout[input.prev_out]);
            input.non_witness_utxo = nullptr; input.witness_utxo.SetNull();
        }
        if (scenario == 1) source.inputs[0].final_script_sig = psbtfeefixture::Legacy(2, false).inputs[0].final_script_sig;
        const auto raw = psbtwork::SerializeRaw(source);
        const auto result = psbtfee::Resolve(source, {}, [&](const COutPoint& outpoint) -> std::optional<CTxOut> {
            return scenario == 2 ? std::nullopt : std::optional<CTxOut>{outputs.at(outpoint)};
        });
        QVERIFY(result.analysis); QVERIFY(result.analysis->assessment);
        QCOMPARE(result.proof->amount.has_value(), scenario != 2);
        QCOMPARE(result.analysis->signature_state, scenario == 0 ? psbtwork::SignatureState::Verified
            : scenario == 1 ? psbtwork::SignatureState::Invalid : psbtwork::SignatureState::Unverified);
        QCOMPARE(result.analysis->can_broadcast, scenario == 0);
        QCOMPARE(bool(result.transaction), scenario == 0);
        if (scenario == 0) QCOMPARE(result.transaction->GetWitnessHash(), MakeTransactionRef(transaction)->GetWitnessHash());
        QCOMPARE(psbtwork::SerializeRaw(source), raw);
    }

    void readerReusesIndependentProofAndSkipsUnnecessaryLookups()
    {
        const auto document = Amounts({2000, 2000}, {3000});
        const auto proof = psbtfee::Analyze(document);
        int reads{0};
        auto lookup = [&reads](const COutPoint&) -> std::optional<CTxOut> { ++reads; return std::nullopt; };
        const auto full = psbtfee::Resolve(document, {}, lookup);
        QCOMPARE(full.proof->amount, std::optional<CAmount>{1000});
        QCOMPARE(reads, 0);
        auto incomplete = document;
        incomplete.inputs[1].non_witness_utxo = nullptr;
        const psbtfee::Analysis partial = psbtfee::Analyze(incomplete);
        const auto unavailable = psbtfee::Resolve(incomplete, partial, lookup);
        QVERIFY(!unavailable.proof->amount); QCOMPARE(reads, 1);
        incomplete.inputs[0].non_witness_utxo = nullptr;
        const auto cached = psbtfee::Resolve(incomplete, proof, lookup);
        QCOMPARE(cached.proof->amount, std::optional<CAmount>{1000});
        QCOMPARE(reads, 1);
        incomplete.inputs[0].non_witness_utxo = document.inputs[0].non_witness_utxo;
        const auto partially_cached = psbtfee::Resolve(incomplete, proof, lookup);
        QCOMPARE(partially_cached.proof->amount, std::optional<CAmount>{1000});
        QCOMPARE(reads, 1);
        auto three = Amounts({2000, 2000, 2000}, {3000});
        psbtfee::Analysis one_cached;
        one_cached.verified.emplace(three.inputs[1].GetOutPoint(), three.inputs[1].non_witness_utxo->vout[1]);
        three.inputs[1].non_witness_utxo = three.inputs[2].non_witness_utxo = nullptr;
        const auto still_missing = psbtfee::Resolve(three, one_cached, lookup);
        QVERIFY(!still_missing.proof->amount); QCOMPARE(reads, 2);
    }

    void nodeLookupPreservesMissingAndSpent()
    {
        QVERIFY(!psbtfee::NodeLookup(nullptr));
        MockNode node;
        int state{0};
        node.get_unspent_output_fn = [&state](const COutPoint&) -> std::optional<Coin> {
            if (state == 0) return std::nullopt;
            if (state == 1) return Coin{};
            return Coin{CTxOut{2000, CScript{} << OP_TRUE}, 1, false};
        };
        node.execute_rpc_fn = [](const std::string&, const UniValue&, const std::string&) { return UniValue{}; };
        const auto lookup = psbtfee::NodeLookup(&node);
        const COutPoint outpoint{Txid::FromUint256(uint256::ONE), 0};
        QVERIFY(!lookup(outpoint)); state = 1;
        QVERIFY(!lookup(outpoint)); state = 2;
        QCOMPARE(lookup(outpoint)->nValue, CAmount{2000});
        QCOMPARE(node.calls.getUnspentOutput.load(), 3);
    }

    void finalizedTransactionAdmissionUsesExactPublicNodeRpc_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::addColumn<int>("expected_admission");
        QTest::addColumn<int>("expected_state");
        QTest::newRow("allowed-exact-candidate") << 0 << int(psbtpolicy::Admission::Allowed) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("bounded-rejection-reason") << 1 << int(psbtpolicy::Admission::Rejected) << int(psbtpolicy::State::Blocked);
        QTest::newRow("empty-response-is-unknown") << 2 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("multiple-results-is-unknown") << 3 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("wrong-txid-is-unknown") << 4 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("wrong-wtxid-is-unknown") << 5 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("unfinished-result-is-unknown") << 6 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("malformed-reason-is-unknown") << 7 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("rpc-exception-is-unknown") << 8 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("tip-changes-before-submitted-clears-chain-facts") << 9 << int(psbtpolicy::Admission::Allowed) << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("known-chain-blocker-precedes-submitted") << 10 << int(psbtpolicy::Admission::Allowed) << int(psbtpolicy::State::Blocked);
        QTest::newRow("unavailable-chain-and-submitted-stay-unknown") << 11 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::Unknown);
        QTest::newRow("reorg-during-submitted-clears-both-observations") << 12 << int(psbtpolicy::Admission::Unknown) << int(psbtpolicy::State::Unknown);
    }
    void finalizedTransactionAdmissionUsesExactPublicNodeRpc()
    {
        QFETCH(int, scenario);
        QFETCH(int, expected_admission);
        QFETCH(int, expected_state);

        CMutableTransaction mutable_transaction;
        const COutPoint outpoint{Txid::FromUint256(uint256::ONE), 4};
        mutable_transaction.vin.emplace_back(outpoint);
        mutable_transaction.vout.emplace_back(1'000, CScript{} << OP_TRUE);
        const auto transaction = MakeTransactionRef(std::move(mutable_transaction));

        AdmissionNode node;
        AdmissionChain chain;
        chain.input_spent = scenario == 9 || scenario == 10;
        chain.unavailable_tip = scenario == 11;
        node.change_tip_on_read = scenario == 9 ? 3 : scenario == 12 ? 4 : 0;
        std::string method, uri;
        UniValue parameters;
        node.execute_rpc_fn = [&](const std::string& called_method, const UniValue& called_parameters, const std::string& called_uri) {
            method = called_method; parameters = called_parameters; uri = called_uri;
            if (scenario == 8) throw std::runtime_error("RPC unavailable");
            if (scenario == 2 || scenario == 11) return UniValue{UniValue::VARR};
            UniValue result = AdmissionResult(*transaction, scenario != 1 && scenario != 7,
                scenario == 1 ? std::string(1'500, 'r') : "policy-rejected");
            if (scenario == 3) {
                UniValue response{UniValue::VARR};
                response.push_back(result); response.push_back(result);
                return response;
            }
            if (scenario == 4 || scenario == 5) {
                UniValue wrong{UniValue::VOBJ};
                wrong.pushKV("txid", scenario == 4 ? uint256{}.ToString() : transaction->GetHash().ToString());
                wrong.pushKV("wtxid", scenario == 5 ? uint256{}.ToString() : transaction->GetWitnessHash().ToString());
                wrong.pushKV("allowed", true);
                UniValue response{UniValue::VARR}; response.push_back(std::move(wrong));
                return response;
            }
            if (scenario == 6) {
                UniValue unfinished{UniValue::VOBJ};
                unfinished.pushKV("txid", transaction->GetHash().ToString());
                unfinished.pushKV("wtxid", transaction->GetWitnessHash().ToString());
                UniValue response{UniValue::VARR}; response.push_back(std::move(unfinished));
                return response;
            }
            if (scenario == 7) {
                UniValue malformed{UniValue::VOBJ};
                malformed.pushKV("txid", transaction->GetHash().ToString());
                malformed.pushKV("wtxid", transaction->GetWitnessHash().ToString());
                malformed.pushKV("allowed", false);
                malformed.pushKV("reject-reason", 17);
                UniValue response{UniValue::VARR}; response.push_back(std::move(malformed));
                return response;
            }
            UniValue response{UniValue::VARR}; response.push_back(std::move(result));
            return response;
        };

        const auto facts = psbtpolicy::ReadNode(&node, &chain)(transaction, scenario == 16 ? psbtpolicy::MaxFee() + 1 : CAmount{1'000});
        QCOMPARE(method, std::string("testmempoolaccept"));
        QCOMPARE(uri, std::string{});
        QCOMPARE(parameters.size(), size_t{2});
        QVERIFY(parameters[0].isArray());
        QCOMPARE(parameters[0].size(), size_t{1});
        QCOMPARE(parameters[0][0].get_str(), EncodeHexTx(*transaction));
        QVERIFY(parameters[1].isNum());
        QCOMPARE(parameters[1].getValStr(), ValueFromAmount(psbtpolicy::MaxFee()).getValStr()); // Submission's fee-rate cap.
        QCOMPARE(static_cast<int>(facts.admission), expected_admission);
        QCOMPARE(static_cast<int>(psbtpolicy::Evaluate(facts).state), expected_state);
        QCOMPARE(node.calls.executeRpc.load(), 1);
        QCOMPARE(node.best_hash_reads, scenario == 11 ? 3 : 4);
        if (scenario == 1) QCOMPARE(facts.admission_error.size(), 1024);
        if (scenario == 9) {
            QVERIFY(!facts.observed);
            QVERIFY(!facts.missing_inputs);
            QCOMPARE(facts.admission, psbtpolicy::Admission::Allowed);
        }
        if (scenario == 10) {
            QVERIFY(facts.observed);
            QVERIFY(facts.missing_inputs);
            QCOMPARE(facts.admission, psbtpolicy::Admission::Allowed);
            QVERIFY(psbtpolicy::Evaluate(facts).explanation.contains("spent"));
        }
        if (scenario == 11 || scenario == 12) QVERIFY(!facts.observed);
        if (scenario == 12) QCOMPARE(facts.admission, psbtpolicy::Admission::Unknown);
        QCOMPARE(facts.within_fee_cap, scenario != 16);
        if (scenario == 16) QVERIFY(psbtpolicy::Evaluate(facts).explanation.contains("maximum"));
    }
    void missingServicePortsRejectAdmission()
    {
        ManualExecutionEnvironment environment;
        const auto document = std::make_shared<const PartiallySignedTransaction>(Amounts({2000}, {1500}));
        const auto seed = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*document));
        bool published{false};
        psbtfee::Services services;
        auto publish = [&](psbtfee::Resolution) { published = true; };
        QVERIFY(!psbtfee::Request(services, document, seed, publish));
        services.scheduler = environment.scheduler;
        QVERIFY(!psbtfee::Request(services, document, seed, publish));
        services.lookup = [](const COutPoint&) { return std::optional<CTxOut>{}; };
        QVERIFY(!psbtfee::Request(services, document, seed, publish));
        QVERIFY(!published);
        QVERIFY(environment.quiescent());
    }

    void reviewedFeeMustBePresentAndExact()
    {
        auto psbt = Amounts({2000}, {1500});
        const auto proof = psbtfee::Analyze(psbt);
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, std::nullopt));
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{499}));
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        psbt.inputs[0].non_witness_utxo = nullptr;
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500})); // Retained independent proof.
        psbt.outputs[0].amount = 1400;
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{500}));
    }
    void checkedAmounts_data()
    {
        QTest::addColumn<qint64>("input"); QTest::addColumn<qint64>("output"); QTest::addColumn<bool>("known");
        QTest::newRow("normal") << qint64(2000) << qint64(1500) << true;
        QTest::newRow("zero-fee") << qint64(2000) << qint64(2000) << true;
        QTest::newRow("zero-values") << qint64(0) << qint64(0) << true;
        QTest::newRow("max-money") << qint64(MAX_MONEY) << qint64(MAX_MONEY) << true;
        QTest::newRow("negative-fee") << qint64(1500) << qint64(2000) << false;
        for (auto invalid : {qint64(-1), qint64(MAX_MONEY + 1), std::numeric_limits<qint64>::max(), std::numeric_limits<qint64>::min()}) {
            QTest::newRow(qPrintable(QString("input-%1").arg(invalid))) << invalid << qint64(1) << false;
            QTest::newRow(qPrintable(QString("output-%1").arg(invalid))) << qint64(2000) << invalid << false;
        }
    }
    void checkedAmounts()
    {
        QFETCH(qint64, input); QFETCH(qint64, output); QFETCH(bool, known);
        const auto result = psbtfee::Analyze(Amounts({input}, {output}));
        QCOMPARE(result.state, known ? psbtfee::State::Known : psbtfee::State::Invalid);
        QCOMPARE(result.amount.has_value(), known);
        if (known) QCOMPARE(*result.amount, input - output); // Independent exact arithmetic.
    }
    void invalidStructureAndAuthentication_data()
    {
        QTest::addColumn<int>("mutation");
        for (int i = 0; i < 9; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void invalidStructureAndAuthentication()
    {
        QFETCH(int, mutation);
        auto psbt = Amounts({2000, 2000}, {3000});
        switch (mutation) {
        case 0: psbt.inputs.clear(); break;
        case 1: psbt.outputs.clear(); break;
        case 2: psbt.inputs[0].height_locktime = 42; psbt.inputs[1].time_locktime = 500000001; break;
        case 3: psbt.inputs[1].prev_out = 0; break;
        case 4: psbt.inputs[0].prev_txid = Txid::FromUint256(uint256::ONE); break;
        case 5: psbt.inputs[0].prev_out = 2; break;
        case 6: psbt.inputs[0].witness_utxo = CTxOut{2001, CScript{} << OP_TRUE}; break;
        case 7: psbt = Amounts({MAX_MONEY, MAX_MONEY}, {1}); break;
        case 8: psbt = Amounts({MAX_MONEY}, {MAX_MONEY, MAX_MONEY}); break;
        }
        QCOMPARE(psbtfee::Analyze(psbt).state, psbtfee::State::Invalid);
    }
    void missingAndMatchingMetadata()
    {
        auto psbt = Amounts({2000}, {1500});
        psbt.inputs[0].witness_utxo = psbt.inputs[0].non_witness_utxo->vout[0];
        QCOMPARE(psbtfee::Analyze(psbt).amount, std::optional<CAmount>{500});
        psbt.inputs[0].non_witness_utxo = nullptr;
        auto result = psbtfee::Analyze(psbt);
        QCOMPARE(result.state, psbtfee::State::Missing);
        QVERIFY(!result.amount); QVERIFY(!result.prevouts[0]);
        psbt.inputs[0].witness_utxo.SetNull();
        QCOMPARE(psbtfee::Analyze(psbt).state, psbtfee::State::Missing);
        psbt.outputs[0].amount = -1;
        QCOMPARE(psbtfee::Analyze(psbt).state, psbtfee::State::Invalid);
    }
    void witnessMetadataRequiresIndependentProof_data()
    {
        QTest::addColumn<int>("kind");
        for (int i = 0; i < 5; ++i) QTest::newRow(qPrintable(QString::number(i))) << i;
    }
    void witnessMetadataRequiresIndependentProof()
    {
        QFETCH(int, kind);
        const std::vector<CScript> scripts{
            CScript{} << OP_TRUE,
            CScript{} << OP_0 << std::vector<unsigned char>(20, 1),
            CScript{} << OP_0 << std::vector<unsigned char>(32, 1),
            CScript{} << OP_1 << std::vector<unsigned char>(32, 1),
            CScript{} << OP_2 << std::vector<unsigned char>(32, 1)};
        auto psbt = Witness(scripts[kind]);
        QCOMPARE(psbtfee::Analyze(psbt).state, psbtfee::State::Missing);
        const psbtfee::Outputs trusted{{psbt.inputs[0].GetOutPoint(), CTxOut{2000, scripts[kind]}}};
        const auto proof = psbtfee::Analyze(psbt, trusted);
        QCOMPARE(proof.amount, std::optional<CAmount>{500});
        psbt.inputs[0].witness_utxo.nValue = 1501;
        QCOMPARE(psbtfee::Analyze(psbt, trusted).state, psbtfee::State::Invalid);
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        psbt.inputs[0].witness_utxo.SetNull();
        psbtfee::FillMissing(psbt, proof);
        QCOMPARE(psbt.inputs[0].witness_utxo, trusted.begin()->second);
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500}));
    }
    void trustedProofCannotContradictFullParent()
    {
        const auto psbt = Amounts({2000}, {1500});
        const psbtfee::Outputs mismatch{{psbt.inputs[0].GetOutPoint(), CTxOut{2001, CScript{} << OP_TRUE}}};
        QCOMPARE(psbtfee::Analyze(psbt, mismatch).state, psbtfee::State::Invalid);
    }
    void preparedFeeIsBoundToExactTransaction()
    {
        auto psbt = Amounts({2000}, {1500});
        const auto tx = CTransaction{*psbt.GetUnsignedTx()};
        const auto proof = psbtfee::Prepared(tx, 500);
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        psbt.inputs[0].non_witness_utxo = nullptr;
        QVERIFY(psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        psbt.inputs[0].witness_utxo = CTxOut{1501, CScript{} << OP_TRUE};
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{500}));
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{1}));
        QVERIFY(!psbtfee::Prepared(tx, -1).amount);
        QVERIFY(!psbtfee::Prepared(tx, MAX_MONEY + 1).amount);
        psbt.outputs[0].amount = 1499;
        QVERIFY(!psbtfee::MatchesReview(psbt, proof, CAmount{500}));
    }
};
BITCOINQML_REGISTER_QT_TEST(PsbtFeeTests)
#include <test_psbtfee.moc>
