// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/executor.h>
#include <qml/psbtdocument.h>
#include <qml/psbtwork.h>

#include <QSemaphore>
#include <QScopeGuard>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>

namespace {
std::shared_ptr<psbtwork::Snapshot> Analysis(bool known = true, CAmount output = 1'500)
{
    CMutableTransaction funding;
    funding.vin.emplace_back(COutPoint{Txid::FromUint256(uint256::ONE), 0});
    funding.vout.emplace_back(2'000, CScript{} << OP_DROP << OP_TRUE);
    const auto parent = MakeTransactionRef(funding);
    CMutableTransaction transaction;
    transaction.vin.emplace_back(COutPoint{parent->GetHash(), 0});
    transaction.vout.emplace_back(output, CScript{} << OP_TRUE);
    PartiallySignedTransaction document{transaction, 2};
    if (known) document.inputs[0].non_witness_utxo = parent;
    auto snapshot = std::make_shared<psbtwork::Snapshot>();
    snapshot->document = std::make_shared<const PartiallySignedTransaction>(std::move(document));
    snapshot->proof = std::make_shared<const psbtfee::Analysis>(psbtfee::Analyze(*snapshot->document));
    snapshot->facts = {true, true, {}};
    snapshot->unsigned_inputs = snapshot->could_sign = 1;
    return snapshot;
}
psbtwork::Result Result(std::shared_ptr<const psbtwork::Snapshot> snapshot)
{
    psbtwork::Result result;
    result.snapshot = std::move(snapshot);
    return result;
}
} // namespace

class PsbtDocumentTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void feeFactsKeepUnknownSeparateFromZero_data()
    {
        QTest::addColumn<bool>("known");
        QTest::addColumn<CAmount>("output");
        QTest::addColumn<bool>("authorized");
        QTest::newRow("known-positive-fee") << true << CAmount{1'500} << true;
        QTest::newRow("known-zero-fee") << true << CAmount{2'000} << true;
        QTest::newRow("unknown-fee") << false << CAmount{1'500} << false;
        QTest::newRow("negative-fee") << true << CAmount{2'001} << false;
        QTest::newRow("negative-output") << true << CAmount{-1} << false;
        QTest::newRow("output-exceeds-money-range") << true << CAmount{MAX_MONEY + 1} << false;
    }
    void feeFactsKeepUnknownSeparateFromZero()
    {
        QFETCH(bool, known);
        QFETCH(CAmount, output);
        QFETCH(bool, authorized);
        auto facts = Analysis(known, output);
        const auto original = psbtwork::SerializeRaw(*facts->document);
        facts->can_sign = facts->can_broadcast = true;
        psbtdocument::Qualify(*facts);
        QCOMPARE(facts->can_sign, authorized);
        QVERIFY(!facts->can_broadcast);
        QCOMPARE(facts->proof->amount.has_value(), authorized);
        QCOMPARE(facts->signature_state, authorized ? psbtwork::SignatureState::Incomplete : psbtwork::SignatureState::Unverified);
        if (authorized) QCOMPARE(*facts->proof->amount, CAmount{2'000 - output});
        else QVERIFY(!facts->error.isEmpty());
        QCOMPARE(psbtwork::SerializeRaw(*facts->document), original);
    }

    void nodePolicyIsIndependentFromVerifiedCompletion_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::addColumn<bool>("expected_broadcast");
        QTest::addColumn<int>("expected_state");
        for (const auto& [name, value] : {std::pair{"unknown", 0}, {"no-known-obstacle", 1},
                 {"missing-or-spent", 2}, {"immature", 3}, {"nonfinal", 4}, {"below-relay", 5}, {"chain-limits", 6}}) {
            QTest::newRow(name) << value << (value < 2) << (value == 0 ? int(psbtpolicy::State::Unknown)
                : value == 1 ? int(psbtpolicy::State::NoKnownObstacle) : int(psbtpolicy::State::Blocked));
        }
        QTest::newRow("node-started-allowed-without-chain-snapshot") << 7 << true << int(psbtpolicy::State::NoKnownObstacle);
        QTest::newRow("node-started-rejected-without-chain-snapshot") << 8 << false << int(psbtpolicy::State::Blocked);
        QTest::newRow("known-chain-blocker-takes-precedence") << 9 << false << int(psbtpolicy::State::Blocked);
    }
    void nodePolicyIsIndependentFromVerifiedCompletion()
    {
        QFETCH(int, scenario);
        QFETCH(bool, expected_broadcast);
        QFETCH(int, expected_state);
        auto snapshot = Analysis();
        auto document = *snapshot->document;
        document.inputs[0].final_script_sig = CScript{} << std::vector<unsigned char>{};
        snapshot->document = std::make_shared<const PartiallySignedTransaction>(document);
        snapshot->assessment = snapshot->document;
        snapshot->complete = true;
        snapshot->node_facts.observed = (scenario != 0 && scenario < 7) || scenario == 9;
        snapshot->node_facts.missing_inputs = scenario == 2 || scenario == 9;
        snapshot->node_facts.immature_inputs = scenario == 3;
        snapshot->node_facts.final = scenario != 4;
        snapshot->node_facts.sufficient_fee = scenario != 5;
        snapshot->node_facts.chain_limits = scenario != 6;
        if (scenario == 7) snapshot->node_facts.admission = psbtpolicy::Admission::Allowed;
        if (scenario == 8 || scenario == 9) snapshot->node_facts.admission = psbtpolicy::Admission::Rejected;
        if (scenario == 8) snapshot->node_facts.admission_error = QStringLiteral("txn-mempool-conflict");
        const auto raw = psbtwork::SerializeRaw(document);
        psbtdocument::Qualify(*snapshot);
        QVERIFY(snapshot->complete);
        QCOMPARE(snapshot->signature_state, psbtwork::SignatureState::Verified);
        QCOMPARE(snapshot->can_broadcast, expected_broadcast);
        QCOMPARE(static_cast<int>(snapshot->node_state), expected_state);
        if (scenario == 8) QVERIFY(snapshot->status.contains(QStringLiteral("txn-mempool-conflict")));
        if (scenario == 9) QVERIFY(snapshot->status.contains(QStringLiteral("spent")));
        QVERIFY(!snapshot->status.isEmpty());
        QCOMPARE(psbtwork::SerializeRaw(*snapshot->document), raw);
    }

    void loadFailureClearsOneCoherentTuple()
    {
        psbtdocument::Store owner{9};
        auto facts = Analysis();
        const auto first = owner.consume(1, psbtwork::Kind::Load, Result(facts));
        QVERIFY(first.snapshot);
        QVERIFY(owner.snapshot().analysis->can_sign);
        facts->could_sign = 0;
        QCOMPARE(owner.snapshot().analysis->could_sign, 1);
        psbtwork::Result failure;
        failure.code = psbtwork::Error::Invalid;
        failure.error = QStringLiteral("invalid document");
        const auto failed = owner.consume(2, psbtwork::Kind::Load, std::move(failure));
        QVERIFY(!failed.snapshot);
        QVERIFY(!owner.snapshot().analysis);
        QCOMPARE(owner.snapshot().generation, std::uint64_t{9});
        QCOMPARE(owner.snapshot().operation, std::uint64_t{2});
        QCOMPARE(owner.snapshot().sequence, std::uint64_t{2});
        QCOMPARE(owner.snapshot().phase, psbtdocument::Phase::Failed);
    }

    void olderOutcomeCannotMixWithTheCurrentAnalysis()
    {
        psbtdocument::Store owner{11};
        const auto current = owner.consume(2, psbtwork::Kind::Load, Result(Analysis()));
        const auto accepted = owner.snapshot().analysis;
        QVERIFY(current.snapshot);
        auto old = Result(Analysis(false));
        old.submitted = true;
        old.path = QStringLiteral("original.psbt");
        const auto retired = owner.consume(1, psbtwork::Kind::Inspect, std::move(old));
        QVERIFY(!retired.snapshot);
        QVERIFY(retired.submitted);
        QCOMPARE(retired.path, QStringLiteral("original.psbt"));
        QCOMPARE(owner.snapshot().analysis, accepted);
        QCOMPARE(owner.snapshot().sequence, std::uint64_t{1});
    }

    void withdrawnOwnerKeepsEffectReceiptWithoutRetargeting()
    {
        psbtdocument::Store owner{12};
        owner.withdraw();
        auto completed = Result(Analysis());
        completed.submitted = true;
        completed.txid = QStringLiteral("original transaction identity");
        const auto result = owner.consume(1, psbtwork::Kind::Broadcast, std::move(completed));
        QVERIFY(!result.snapshot);
        QVERIFY(result.submitted);
        QCOMPARE(result.txid, QStringLiteral("original transaction identity"));
        QVERIFY(!owner.snapshot().analysis);
        QCOMPARE(owner.snapshot().sequence, std::uint64_t{0});
    }

    void nativeFactsAreConsumedOnTheRealDomainWorker()
    {
        execution::Executor executor{execution::Executor::Config{}};
        auto core = executor.makeQueue(execution::Lane::Core);
        auto utility = executor.makeQueue(execution::Lane::Utility);
        auto owner = std::make_shared<psbtdocument::Store>(3);
        const auto gui = QThread::currentThread();
        std::atomic<QThread*> native_thread{nullptr}, domain_thread{nullptr};
        std::atomic_bool qualified{false};
        QSemaphore completed;
        const auto cleanup = qScopeGuard([&] {
            utility.post([owner = std::move(owner)] { owner->withdraw(); });
            executor.stopAndJoin();
        });
        QVERIFY(core.post([&, owner] {
            native_thread = QThread::currentThread();
            auto result = Result(Analysis());
            utility.post([&, owner, result = std::move(result)]() mutable {
                domain_thread = QThread::currentThread();
                const auto accepted = owner->consume(1, psbtwork::Kind::Load, std::move(result));
                qualified = accepted.snapshot && accepted.snapshot->can_sign;
                completed.release();
            });
        }));
        QVERIFY(completed.tryAcquire(1, 5'000));
        QVERIFY(qualified.load());
        QVERIFY(native_thread.load() && native_thread.load() != gui);
        QVERIFY(domain_thread.load() && domain_thread.load() != gui);
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PsbtDocumentTests)
#else
QTEST_MAIN(PsbtDocumentTests)
#endif
#include <test_psbtdocument.moc>
