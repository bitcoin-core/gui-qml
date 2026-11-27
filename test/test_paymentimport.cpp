// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/paymentimport.h>
#include <qml/executor.h>
#include <qml/guipresentation.h>
#include <qml/paymentimportreview.h>
#include <qml/models/bitcoinurimodel.h>
#include <qml/psbtwork.h>
#include <qml/walletopening.h>
#include <test/mocks/walletmodelservices.h>
#include <qml/retainedvalue.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>
#include <test/mocks/mockwallet.h>
#include <test/mocks/manualwalletdrain.h>
#include <test/mocks/psbtfeefixture.h>
#include <key_io.h>
#include <QDir>
#include <QFile>
#include <QEvent>
#include <QScopeGuard>
#include <QSemaphore>
#include <QUrl>
#include <QTemporaryDir>
#include <QtTest/QtTest>
namespace {
QString Address() { return QString::fromStdString(EncodeDestination(PKHash{uint160{std::vector<unsigned char>(20, 3)}})); }
QString Uri() { return "bitcoin:" + Address() + "?amount=0.12345678&label=Public&message=Message"; }
std::shared_ptr<const PartiallySignedTransaction> Document()
{
    CMutableTransaction tx; tx.vin.emplace_back(COutPoint{Txid::FromHex("0101010101010101010101010101010101010101010101010101010101010101").value(), 0});
    tx.vout.emplace_back(1, CScript{} << OP_TRUE);
    return std::make_shared<const PartiallySignedTransaction>(tx, 2);
}
paymentimport::Inspection Psbt()
{
    paymentimport::Inspection source; source.kind = paymentimport::Kind::Psbt;
    auto snapshot = std::make_shared<psbtwork::Snapshot>(); snapshot->document = Document(); source.psbt = snapshot; return source;
}
paymentimport::Target Target(uint64_t revision = 7, bool conflict = false)
{ static const auto opening = std::make_shared<const walletopening::Identity>(); return {opening, 2, revision, 3, true, true, conflict}; }
execution::SubmitResult PostTask(execution::SerialQueue& queue, std::shared_ptr<execution::Task> task)
{
    const bool accepted = queue.post([task = std::move(task)] {
        task->execute();
        task->deliver();
    });
    return {{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
}
paymentimport::ReviewServices ReviewPorts(ManualStartupExecution& worker)
{
    auto cleanup = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(execution::Lane::Utility));
    auto values = std::make_shared<execution::ValueCleanupQueue>(cleanup);
    paymentimport::ReviewServices services;
    services.observe = [&worker](auto task) { return PostTask(worker.outcomes, std::move(task)); };
    services.retire = [cleanup](auto work) {
        return execution::SubmitResult{{}, cleanup->post(std::move(work)) ? execution::Rejection::None : execution::Rejection::Transport};
    };
    services.hold = [values](auto cleanup) { return values->hold(std::move(cleanup)); };
    return services;
}
struct Fixture {
    ManualStartupExecution source, file_work;
    ManualStartupExecution target;
    paymentimport::Snapshot snapshot;
    std::chrono::steady_clock::time_point now{};
    std::vector<std::function<void()>> wakes;
    std::shared_ptr<paymentimport::Service> owner;
    std::shared_ptr<paymentimport::TargetPort> port{std::make_shared<paymentimport::TargetPort>()};
    paymentimport::Target facts{Target()};
    int reads{0}, analyses{0}, writes{0}, dispatches{0}, queries{0}, acks{0}, retired{0};
    bool lose_reply{false}, unknown_query{false}, reject_ack{false}, read_fail{false};
    std::optional<paymentimport::ChildReply> result;
    Fixture() {
        port->opening = facts.opening; port->draft = facts.draft; port->recipient = facts.recipient;
        port->observe = [this](auto, auto done, auto failed) {
            return PostTask(target.outcomes, execution::MakeTask([this] { return facts; }, std::move(done), std::move(failed)));
        };
        port->dispatch = [this](auto, auto child, auto, auto done, auto failed) {
            return PostTask(target.outcomes, execution::MakeTask([this, child] {
                ++dispatches;
                if (!facts.available) result = paymentimport::ChildReply{{child, paymentimport::Outcome::Rejected, 0, facts.revision}, facts, {}, {}};
                else if (child.target.revision != facts.revision) result = paymentimport::ChildReply{{child, paymentimport::Outcome::Stale, 0, facts.revision}, facts, {}, {}};
                else { ++writes; result = paymentimport::ChildReply{{child, paymentimport::Outcome::Applied, facts.revision + 1, facts.revision + 1}, {}, {}, {}}; }
                return *result;
            }, [this, done](auto reply) { if (!lose_reply) done(std::move(reply)); }, std::move(failed)));
        };
        port->query = [this](auto, auto child, auto done, auto failed) {
            return PostTask(target.outcomes, execution::MakeTask([this, child] {
                ++queries; return unknown_query || !result ? paymentimport::ChildReply{{child, paymentimport::Outcome::Unknown, 0, 0}, {}, {}, {}} : *result;
            }, std::move(done), std::move(failed)));
        };
        port->acknowledge = [this](auto, auto) { ++acks; return execution::SubmitResult{{}, reject_ack ? execution::Rejection::Transport : execution::Rejection::None}; };
        paymentimport::Services services;
        services.reads = std::make_shared<execution::SerialQueue>(file_work.executor().makeQueue(execution::Lane::Utility));
        services.outcomes = std::make_shared<execution::SerialQueue>(source.executor().makeQueue(execution::Lane::Utility));
        services.read = [this](const auto& input) { ++reads; if (read_fail) { paymentimport::Inspection failed; failed.error = "read unavailable"; return failed; } return paymentimport::ReadSource(input); };
        services.analyze = [this](const auto& input) { ++analyses; return paymentimport::AnalyzeSource(input); };
        services.now = [this] { return now; }; services.wake = [this](auto, auto wake) { wakes.push_back(std::move(wake)); };
        owner = paymentimport::Service::Create(42, services, [this](auto value) { snapshot = std::move(value); }, [this](auto) { ++retired; });
    }
    ~Fixture() {
        owner->close(); drain();
        // Release clock callbacks and published Native aliases before the final
        // manual transport drain; its queued cleanup must outlive the fixture.
        wakes.clear(); owner.reset(); snapshot = {}; port.reset(); result.reset(); drain();
    }
    void drainSource() { do { source.drain(); file_work.drain(); } while (!source.quiescent() || !file_work.quiescent()); }
    void drain() {
        for (size_t n{0}; n < 20 && (!source.quiescent() || !file_work.quiescent() || !target.quiescent()); ++n) {
            drainSource();
            target.drain();
        }
    }
    void inspect() { owner->inspect({paymentimport::Input::Kind::Text, Uri()}); owner->chooseTarget(port); drain(); }
    void advance() { now += std::chrono::milliseconds{100}; auto pending = std::exchange(wakes, {}); for (auto& wake : pending) wake(); drain(); }
};
} // namespace
class PaymentImportTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<ECC_Context> m_ecc;
private Q_SLOTS:
    void initTestCase() { m_ecc = std::make_unique<ECC_Context>(); }
    void cleanupTestCase() { m_ecc.reset(); }
    void acceptedReadRetiresAfterOutcomeQueueCloses()
    {
        Fixture f;
        QVERIFY(f.owner->inspect({paymentimport::Input::Kind::Text, Uri()}));
        f.source.drain();
        QCOMPARE(f.reads, 0);
        f.source.executor().stopAndJoin();
        QVERIFY(f.owner->close());
        f.source.drain();
        QCOMPARE(f.retired, 0);
        f.file_work.drain();
        QCOMPARE(f.reads, 1);
        QCOMPARE(f.retired, 0);
        f.source.drain();
        QCOMPARE(f.retired, 1);
    }

    void pastedUriAppliesWithoutPromptingForItsOwnField() {
        Fixture f;
        paymentimport::Input input{paymentimport::Input::Kind::Text, Uri()}; input.field = paymentimport::SourceField::Address;
        QVERIFY(f.owner->inspect(input)); QVERIFY(f.owner->chooseTarget(f.port)); QVERIFY(f.owner->use()); f.drain();
        QCOMPARE(f.dispatches, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied);
    }
    void multipleDroppedUrlsAreRejectedBeforeOpeningAnyPrefix() {
        paymentimport::Input input{paymentimport::Input::Kind::Drop, "file:///does-not-exist.psbt"}; input.url_count = 2;
        const auto rejected = paymentimport::ReadSource(input); QCOMPARE(rejected.kind, paymentimport::Kind::Unknown);
        QCOMPARE(rejected.error, QStringLiteral("Drop one payment request or PSBT at a time."));
        Fixture f; QVERIFY(f.owner->inspect(input)); QVERIFY(f.owner->chooseTarget(f.port)); QVERIFY(f.owner->use()); f.drain();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Failed); QCOMPARE(f.dispatches, 0); QCOMPARE(f.writes, 0);
    }
    void publicSemanticIdentityAndMatchingIncludeMessageButNeverPrivateNote() {
        const auto original = paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri()});
        const auto reordered = paymentimport::ReadSource({paymentimport::Input::Kind::Text,
            "BITCOIN:" + Address() + "?message=%4Dessage&label=Public&amount=0.12345678"});
        QCOMPARE(original.payment_identity, reordered.payment_identity); QVERIFY(original.source_identity != reordered.source_identity);
        const auto changed = paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri() + "2"});
        QVERIFY(original.payment_identity != changed.payment_identity);
        senddraft::Recipient current; current.address = Address(); current.amount_set = true; current.amount = *original.payment.amount;
        current.request_label = original.payment.label; current.message = original.payment.message; current.label = "Private bookkeeping";
        QVERIFY(paymentimport::MatchesPaymentRequest(current, original.payment));
        current.label = "Edited private note"; QVERIFY(paymentimport::MatchesPaymentRequest(current, original.payment));
        QVERIFY(!paymentimport::MatchesPaymentRequest(current, changed.payment));
        current.message.clear(); QVERIFY(!paymentimport::MatchesPaymentRequest(current, original.payment));
        current.message = original.payment.message; current.mode = senddraft::AmountMode::Maximum;
        QVERIFY(!paymentimport::MatchesPaymentRequest(current, original.payment));
        auto omitted = original.payment; omitted.amount.reset(); QVERIFY(paymentimport::MatchesPaymentRequest(current, omitted));
    }
    void capturedFileSourceCannotBeReusedOrRevived() {
        ManualStartupExecution commands; BitcoinUriModel importer; QObject view; importer.setServices({}, &commands.executor());
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("request-without-extension");
        { QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(Uri().toUtf8()), Uri().toUtf8().size()); }
        auto* source = importer.captureImport(nullptr, nullptr, &view); QVERIFY(source); commands.drain();
        QCOMPARE(source->state(), PaymentImportOperation::State::Empty);
        QVERIFY(source->requestFile(QUrl::fromLocalFile(path))); QVERIFY(!source->requestFile(QUrl::fromLocalFile(path))); commands.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); QCOMPARE(source->sourceKind(), PaymentImportOperation::SourceKind::PaymentSource);
        source->cancel(); QVERIFY(!source->requestFile(QUrl::fromLocalFile(path))); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        auto* cancelled = importer.captureImport(nullptr, nullptr, &view); QVERIFY(cancelled); cancelled->cancel();
        QVERIFY(!cancelled->requestFile(QUrl::fromLocalFile(path))); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        importer.close(); commands.drain();
    }
    void readerKeepsPublicMetadataSeparateFromPrivateNote() {
        const auto result = paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri()});
        QCOMPARE(result.kind, paymentimport::Kind::Payment); QCOMPARE(result.payment.address, Address());
        QCOMPARE(result.payment.amount, std::optional<CAmount>{12'345'678});
        QCOMPARE(result.payment.label, QStringLiteral("Public")); QCOMPARE(result.payment.message, QStringLiteral("Message"));
        QVERIFY(result.payment.private_note.isEmpty());
    }
    void sourceIdentityIncludesMessageAndCanonicalPath() {
        const auto first = paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri()});
        const auto second = paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri() + "2"});
        QVERIFY(first.source_identity != second.source_identity);
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("payment");
        { QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(Uri().toUtf8()), Uri().toUtf8().size()); }
        const auto local = paymentimport::ReadSource({paymentimport::Input::Kind::File, path});
        QCOMPARE(local.kind, paymentimport::Kind::Payment); QVERIFY(first.source_identity != local.source_identity);
        QCOMPARE(local.source_identity, paymentimport::ReadSource({paymentimport::Input::Kind::File, QUrl::fromLocalFile(path).toString()}).source_identity);
    }
    void boundedAndMalformedSourcesNeverBecomeCapabilities_data() {
        QTest::addColumn<QString>("text");
        QTest::newRow("malformed") << "bitcoin://" + Address();
        QTest::newRow("required") << "bitcoin:" + Address() + "?req-unknown=value";
        QTest::newRow("amount") << "bitcoin:" + Address() + "?amount=1.000000001";
        QTest::newRow("large") << QString(1024 * 1024 + 1, 'x');
    }
    void boundedAndMalformedSourcesNeverBecomeCapabilities() {
        QFETCH(QString, text); const auto result = paymentimport::ReadSource({paymentimport::Input::Kind::Text, text});
        QCOMPARE(result.kind, paymentimport::Kind::Unknown); QVERIFY(!result.error.isEmpty());
    }
    void psbtInspectionIsStandaloneAndRetainsOriginalDocument() {
        const auto parsed = paymentimport::ReadSource({paymentimport::Input::Kind::Text, QString::fromLatin1(psbtwork::SerializeRaw(*Document()).toBase64())});
        QCOMPARE(parsed.kind, paymentimport::Kind::Psbt); QVERIFY(parsed.psbt->document);
        const auto analyzed = paymentimport::AnalyzeSource(parsed); QCOMPARE(analyzed.kind, paymentimport::Kind::Psbt);
        QVERIFY(analyzed.psbt); QCOMPARE(psbtwork::SerializeRaw(*analyzed.psbt->document), psbtwork::SerializeRaw(*Document()));
    }
    void retryReadsFreshSourceWithoutCarryingOldApplyIntent() {
        Fixture f; f.read_fail = true; f.owner->use(); f.inspect();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Failed); const auto original = f.snapshot.source;
        f.read_fail = false; f.owner->retry(); f.drain();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Inspecting); QVERIFY(f.snapshot.source != original); QCOMPARE(f.reads, 2); QCOMPARE(f.writes, 0);
        QVERIFY(f.snapshot.can_apply); f.owner->apply(); f.drain(); QCOMPARE(f.writes, 1);
    }
    void sourceAndTargetExecutorsKeepSeparateDecisions() {
        Fixture f; f.owner->inspect({paymentimport::Input::Kind::Text, Uri()}); f.owner->chooseTarget(f.port);
        f.drainSource(); QCOMPARE(f.reads, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Inspecting);
        QVERIFY(!f.snapshot.can_apply); QCOMPARE(f.writes, 0);
        f.target.drain(); QVERIFY(!f.snapshot.can_apply); // Foreign Domain only fills a reply cell.
        f.drainSource(); QVERIFY(f.snapshot.can_apply); QCOMPARE(f.writes, 0);
        f.owner->apply(); f.drainSource(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying);
        f.target.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying);
        f.drainSource(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied); QVERIFY(!f.snapshot.pending); QCOMPARE(f.acks, 1);
    }
    void acceptedTargetReplySurvivesOutcomeQueueClosure()
    {
        Fixture f;
        f.inspect();
        QVERIFY(f.snapshot.can_apply);
        QVERIFY(f.owner->apply());
        f.drainSource();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying);
        f.source.executor().stopAndJoin();
        f.target.drain();
        QCOMPARE(f.writes, 1);
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying);
        f.drainSource();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied);
        QCOMPARE(f.acks, 1);
        QVERIFY(f.owner->close());
        f.drain();
        QCOMPARE(f.retired, 1);
    }

    void explicitUseWaitsForInspectionAndOriginalTarget() {
        Fixture f; f.owner->use(); f.owner->inspect({paymentimport::Input::Kind::Text, Uri()}); f.owner->chooseTarget(f.port); f.drain();
        QCOMPARE(f.writes, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied);
    }
    void replacementPromptIsOriginalSourceAndRevisionBound() {
        Fixture f; f.facts.conflicts = true; f.inspect(); f.owner->apply(); f.drain();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::AwaitingReplacement); QVERIFY(f.snapshot.prompt);
        const auto old = f.snapshot.prompt->id; f.owner->replace(old + 1); f.drain(); QCOMPARE(f.writes, 0);
        f.owner->keepDraft(old); f.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Inspecting);
        f.owner->replace(old); f.drain(); QCOMPARE(f.writes, 0);
        f.owner->apply(); f.drain(); QVERIFY(f.snapshot.prompt->id != old);
        f.owner->replace(f.snapshot.prompt->id); f.drain(); QCOMPARE(f.writes, 1);
    }
    void changedTargetRetiresOldReplacementAndRequiresFreshConsent() {
        Fixture f; f.facts.conflicts = true; f.inspect(); f.owner->apply(); f.drain();
        QVERIFY(f.snapshot.prompt); const auto old = f.snapshot.prompt->id;
        f.facts.revision = 8; f.owner->replace(old); f.drain();
        QCOMPARE(f.writes, 0); QCOMPARE(f.dispatches, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::AwaitingReplacement);
        QVERIFY(f.snapshot.prompt); QVERIFY(f.snapshot.prompt->id != old); QCOMPARE(f.snapshot.prompt->target.revision, uint64_t{8});
        f.owner->replace(old); f.drain(); QCOMPARE(f.dispatches, 1); QCOMPARE(f.writes, 0);
        f.owner->replace(f.snapshot.prompt->id); f.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.dispatches, 2);
    }
    void lostReviewFactoryAcknowledgmentQueriesOriginalResultWithoutReplay() {
        Fixture f; int creates{0}, projections{0};
        auto services = ReviewPorts(f.target); services.view = 7; services.current = [] { return true; };
        services.prepare = [&](auto, auto done, auto failed) {
            return PostTask(f.target.core, execution::MakeTask([&, done] {
                ++creates; psbtwork::ImportResult result; result.review = std::make_shared<const walletreview::Snapshot>(); result.document = Document(); done(result);
            }, [] {}, std::move(failed)));
        };
        services.present = [&](auto, auto, auto, auto) { ++projections; };
        const auto factory = paymentimport::WithReviewFactory(f.port, services); QVERIFY(factory);
        auto lossy = std::make_shared<paymentimport::TargetPort>(*factory);
        lossy->dispatch = [factory](auto key, auto child, auto source, auto, auto failed) {
            return factory->dispatch(key, child, std::move(source), [](auto) {}, std::move(failed));
        };
        f.owner->inspect({paymentimport::Input::Kind::Text, QString::fromLatin1(psbtwork::SerializeRaw(*Document()).toBase64())});
        f.owner->chooseTarget(lossy); f.drain(); QVERIFY(f.snapshot.can_review);
        f.owner->openReview(); f.drain(); QCOMPARE(creates, 1); QCOMPARE(projections, 1);
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::CreatingReview);
        f.advance(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::ReviewOpened); QCOMPARE(creates, 1); QVERIFY(!f.snapshot.pending);
        QVERIFY(f.snapshot.child_result && f.snapshot.child_result->review); f.owner->openReview(); f.drain(); QCOMPARE(creates, 1);
    }
    void psbtTargetSwitchDuringAnalysisAndClosedTargetKeepsOriginalFactory() {
        Fixture f; int creates{0}; bool target_open{true};
        auto second = std::make_shared<paymentimport::TargetPort>(*f.port);
        second->opening = std::make_shared<const walletopening::Identity>(); second->draft = 4; second->recipient = 5;
        auto services = ReviewPorts(f.target); services.view = 7; services.current = [&] { return target_open; };
        services.prepare = [&](auto, auto done, auto failed) {
            return PostTask(f.target.core, execution::MakeTask([&, done] {
                ++creates; psbtwork::ImportResult result; result.review = std::make_shared<const walletreview::Snapshot>(); result.document = Document(); done(result);
            }, [] {}, std::move(failed)));
        };
        const auto factory = paymentimport::WithReviewFactory(second, services); QVERIFY(factory);
        auto lossy = std::make_shared<paymentimport::TargetPort>(*factory);
        lossy->dispatch = [factory](auto key, auto child, auto source, auto, auto failed) {
            return factory->dispatch(key, child, std::move(source), [](auto) {}, std::move(failed));
        };
        f.owner->inspect({paymentimport::Input::Kind::Text, QString::fromLatin1(psbtwork::SerializeRaw(*Document()).toBase64())}); f.owner->chooseTarget(f.port);
        while (f.source.step(execution::Lane::Utility)) {}
        QVERIFY(f.file_work.step(execution::Lane::Utility)); while (f.source.step(execution::Lane::Utility)) {}
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Analyzing); QCOMPARE(f.analyses, 0);
        f.owner->chooseTarget(lossy); while (f.source.step(execution::Lane::Utility)) {}
        f.drain(); QCOMPARE(f.analyses, 1); QVERIFY(f.snapshot.can_review); QCOMPARE(f.snapshot.target->opening, second->opening);
        f.owner->openReview(); f.drain(); QCOMPARE(creates, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::CreatingReview);
        target_open = false; f.owner->chooseTarget(f.port); f.advance();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::ReviewOpened); QCOMPARE(creates, 1); QCOMPARE(f.writes, 0);
        QVERIFY(f.snapshot.result); QCOMPARE(f.snapshot.result->child.target.opening, second->opening);
        QCOMPARE(f.snapshot.result->child.target.draft, uint64_t{4}); QVERIFY(!f.snapshot.pending); QVERIFY(f.snapshot.can_export);
    }
    void lostApplyAcknowledgmentQueriesWithoutRepeatingMutation() {
        Fixture f; f.lose_reply = true; f.inspect(); f.owner->apply(); f.drain(); QCOMPARE(f.writes, 1);
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying); f.advance();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied); QCOMPARE(f.queries, 1); QCOMPARE(f.writes, 1); QCOMPARE(f.acks, 1);
    }
    void rejectedOutcomeQueryCanRetryWithoutRepeatingApply() {
        Fixture f; f.lose_reply = true; f.inspect(); f.owner->apply(); f.drain();
        f.target.reject_utility = true; f.advance();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Reconciling); QVERIFY(f.snapshot.can_reconcile); QCOMPARE(f.writes, 1);
        f.advance(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied); QCOMPARE(f.writes, 1); QCOMPARE(f.queries, 1);
    }
    void knownOutcomeRetainedUntilAcknowledgeAdmission() {
        Fixture f; f.reject_ack = true; f.inspect(); f.owner->apply(); f.drain();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied); QVERIFY(f.snapshot.pending); QCOMPARE(f.writes, 1);
        f.owner->close(); f.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closing); QCOMPARE(f.retired, 0);
        f.reject_ack = false; f.advance(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closed); QCOMPARE(f.retired, 1); QCOMPARE(f.writes, 1);
    }
    void unknownAfterCloseKeepsOriginalChildAndReadOnlyRecovery() {
        Fixture f; f.lose_reply = true; f.unknown_query = true; f.inspect(); f.owner->apply(); f.drain();
        f.owner->close(); f.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closing); QVERIFY(f.snapshot.pending); QCOMPARE(f.retired, 0);
        f.advance(); QCOMPARE(f.writes, 1); QVERIFY(f.queries >= 1); QCOMPARE(f.retired, 0);
        f.unknown_query = false; f.advance(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closed); QCOMPARE(f.retired, 1); QCOMPARE(f.writes, 1);
    }
    void unavailableTargetDoesNotInvalidateStandaloneSource() {
        Fixture f; f.facts.available = false; f.inspect(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Inspecting);
        QVERIFY(f.snapshot.inspection); QVERIFY(!f.snapshot.can_apply); f.owner->apply(); f.drain(); QCOMPARE(f.writes, 0);
    }
    void flowRetainsKnownRejectionAcrossAcknowledgmentFailure() {
        paymentimport::Flow flow; const auto source = flow.inspect(); QVERIFY(source); QVERIFY(flow.parsed(*source, paymentimport::Kind::Payment));
        QVERIFY(flow.chooseTarget(Target())); const auto child = flow.apply(); QVERIFY(child);
        QVERIFY(flow.finished({*child, paymentimport::Outcome::Rejected, 0, 7})); QVERIFY(flow.child());
        QVERIFY(flow.acknowledgmentLost(*child)); QVERIFY(flow.acknowledged(*child));
        QCOMPARE(flow.phase(), paymentimport::Phase::Failed); QCOMPARE(flow.error(), paymentimport::Error::Rejected);
    }
    void batchProtectsPrivateNoteAndPreservesOmittedMaximumIntent() {
        senddraft::Recipient current; current.token = 3; current.revision = 10; current.amount_intent_revision = 4;
        current.address = Address(); current.label = QStringLiteral("My private note");
        current.mode = senddraft::AmountMode::Maximum; current.amount = 1000; current.amount_set = true;
        paymentimport::PaymentBatch batch{3, 10, 4, {Address(), "public", "message", {}, {}, false}};
        auto prepared = paymentimport::PreparePaymentBatch(current, batch);
        QCOMPARE(prepared.status, paymentimport::BatchStatus::Prepared); QVERIFY(prepared.recipient);
        QCOMPARE(prepared.recipient->label, current.label); QCOMPARE(prepared.recipient->mode, senddraft::AmountMode::Maximum);
        QCOMPARE(prepared.recipient->amount, CAmount{1000}); QVERIFY(!prepared.amount_changed); QVERIFY(!prepared.private_note_changed);
        batch.payment.amount = 1000; prepared = paymentimport::PreparePaymentBatch(current, batch);
        QCOMPARE(prepared.status, paymentimport::BatchStatus::ReplacementRequired);
        batch.replace_existing = true; prepared = paymentimport::PreparePaymentBatch(current, batch);
        QCOMPARE(prepared.status, paymentimport::BatchStatus::Prepared); QCOMPARE(prepared.recipient->mode, senddraft::AmountMode::Fixed);
        QCOMPARE(prepared.recipient->label, current.label); QVERIFY(prepared.amount_changed);
        batch.payment.private_note = "explicit note"; prepared = paymentimport::PreparePaymentBatch(current, batch);
        QCOMPARE(prepared.recipient->label, current.label); QVERIFY(!prepared.private_note_changed);
        batch.payment.replace_private_note = true; prepared = paymentimport::PreparePaymentBatch(current, batch);
        QCOMPARE(prepared.recipient->label, QStringLiteral("explicit note")); QVERIFY(prepared.private_note_changed);
    }
    void draftLedgerCommitsCompleteBatchOnceAndRetainsOriginalReceipt() {
        senddraft::Draft draft; senddraft::Recipient current; current.token = 3; current.revision = 10; current.amount_intent_revision = 4;
        current.address = Address(); current.label = "private"; current.amount = 1000; current.amount_set = true; QVERIFY(draft.update(current));
        QVERIFY(draft.choose(3, senddraft::AmountMode::Maximum, 4, {}));
        paymentimport::DraftTarget ledger; const auto target = Target();
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::Apply, target, true};
        paymentimport::PaymentBatch batch{3, 10, 4, {Address(), "public", "message", {}, CAmount{1000}, false}, paymentimport::SourceField::WholeRequest, true};
        const auto receipt = ledger.apply({42, 1}, child, batch, target, draft); QVERIFY(receipt);
        QCOMPARE(receipt->receipt.outcome, paymentimport::Outcome::Applied); QCOMPARE(draft.maximum(), quint64{0});
        QCOMPARE(draft.recipients().at(3).label, QStringLiteral("private"));
        QCOMPARE(draft.recipients().at(3).message, QStringLiteral("message"));
        QCOMPARE(draft.recipients().at(3).amount_intent_revision, quint64{5});
        batch.payment.message = "different retry";
        const auto duplicate = ledger.apply({42, 1}, child, batch, target, draft); QVERIFY(duplicate);
        QCOMPARE(duplicate->receipt.result, receipt->receipt.result); QCOMPARE(draft.recipients().at(3).message, QStringLiteral("message"));
        QVERIFY(ledger.query({42, 1})); QVERIFY(ledger.acknowledge({42, 1}, child)); QVERIFY(!ledger.query({42, 1}));
    }
    void reviewFactoryQueriesOriginalOutcomeWithoutRecreating() {
        ManualStartupExecution commands; auto base = std::make_shared<paymentimport::TargetPort>();
        base->opening = Target().opening; base->draft = 2; base->recipient = 3;
        int creates{0}, presentations{0}; bool current{true};
        auto services = ReviewPorts(commands); services.view = 7; services.current = [&] { return current; };
        services.prepare = [&](auto, auto done, auto failed) {
            return PostTask(commands.core, execution::MakeTask([&, done] {
                ++creates; psbtwork::ImportResult result; result.review = std::make_shared<const walletreview::Snapshot>(); result.document = Document(); done(result);
            }, [] {}, std::move(failed)));
        };
        services.present = [&](auto, auto, auto, auto) { ++presentations; };
        auto port = paymentimport::WithReviewFactory(base, services); QVERIFY(port);
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::CreateReview, Target(), false};
        std::optional<paymentimport::ChildReply> queried; const auto source = std::make_shared<const paymentimport::Inspection>(Psbt());
        QVERIFY(port->dispatch({42, 1}, child, source, [](auto) {}, {}));
        commands.drain(); QCOMPARE(creates, 1); QCOMPARE(presentations, 1);
        current = false; commands.core.close(); commands.outcomes.close();
        QVERIFY(port->query({42, 1}, child, [&](auto reply) { queried = reply; }, {})); commands.drain();
        QVERIFY(queried); QCOMPARE(queried->receipt.outcome, paymentimport::Outcome::ReviewOpened); QCOMPARE(creates, 1);
        QVERIFY(port->acknowledge({42, 1}, child)); commands.drain();
        queried.reset(); port.reset(); commands.drain();
        QVERIFY(commands.quiescent());
    }
    void reviewReceiptRetiresOnUtilityAfterOriginalOpeningStops()
    {
        using execution::Lane;
        std::deque<std::pair<Lane, std::function<void()>>> turns;
        execution::Executor executor{[&](Lane lane, execution::UtilityWorker, auto turn) {
            turns.emplace_back(lane, std::move(turn));
            return true;
        }};
        std::optional<Lane> running;
        const auto drain = [&] {
            while (!turns.empty()) {
                auto [lane, turn] = std::move(turns.front());
                turns.pop_front();
                running = lane;
                turn();
            }
            running.reset();
        };
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
        const auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        const auto native_cleanup = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
        auto opening = std::make_shared<walletopening::Session>(std::make_shared<std::atomic_bool>(false),
            [] {}, walletopening::Session::Preferences{}, std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            walletopening::Session::Queues{native_cleanup, [core, utility](auto done) {
                core->close();
                core->whenDrained([utility, done = std::move(done)]() mutable {
                    utility->close();
                    utility->whenDrained(std::move(done));
                });
            }, core, utility});
        bool stopped{false};
        const auto cleanup = qScopeGuard([&] {
            opening->stop(false, [&] { stopped = true; });
            drain();
            QVERIFY(stopped);
        });
        QVERIFY(opening->start(std::make_shared<StubWallet>()));
        drain();
        QVERIFY(opening->available());

        auto facts = Target();
        facts.opening = opening->identity();
        auto base = std::make_shared<paymentimport::TargetPort>();
        base->opening = facts.opening;
        base->draft = facts.draft;
        base->recipient = facts.recipient;
        int prepared{0};
        std::function<void()> show;
        int shown{0};
        bool prepared_on_core{false}, queried_on_utility{false};
        paymentimport::ReviewServices services;
        services.observe = opening->observationPost(execution::Lane::Domain);
        services.retire = [opening](auto work) { return opening->retireValues(std::move(work)); };
        services.hold = [opening](auto cleanup) { return opening->holdValues(std::move(cleanup)); };
        services.view = facts.revision;
        services.current = [opening] { return opening->available(); };
        services.prepare = [&](auto source, auto done, auto failed) {
            return opening->read([&, source, done = std::move(done)](interfaces::Wallet&) {
                ++prepared;
                prepared_on_core = running == Lane::Core;
                psbtwork::ImportResult result;
                result.review = std::make_shared<const walletreview::Snapshot>();
                result.document = source->psbt->document;
                done(std::move(result));
                return true;
            }, [](bool) {}, std::move(failed));
        };
        services.present = [&](auto, auto, auto, auto permitted) {
            show = [&, permitted] { if (permitted->load()) ++shown; };
        };
        auto port = paymentimport::WithReviewFactory(base, std::move(services));
        QVERIFY(port);
        const auto source = std::make_shared<const paymentimport::Inspection>(Psbt());
        std::optional<paymentimport::Target> observed;
        QVERIFY(port->observe(source, [&](auto target) { observed = std::move(target); }, {}));
        drain();
        QCOMPARE(observed, std::optional{facts});
        const paymentimport::CommandKey key{42, 1};
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::CreateReview, facts, false};
        QVERIFY(port->dispatch(key, child, source, [](auto) {}, {}));
        drain();
        QCOMPARE(prepared, 1);
        QVERIFY(prepared_on_core);
        QVERIFY(show);
        port->retire(key, child);
        show();
        QCOMPARE(shown, 0);
        opening->stop(false, [&] { stopped = true; });
        drain();
        QVERIFY(!stopped);

        std::optional<paymentimport::ChildReply> reply;
        QVERIFY(port->query(key, child, [&](auto result) {
            queried_on_utility = running == Lane::Utility;
            reply = std::move(result);
        }, {}));
        drain();
        QVERIFY(queried_on_utility && reply);
        QCOMPARE(reply->receipt.outcome, paymentimport::Outcome::ReviewOpened);
        QCOMPARE(reply->review->document, source->psbt->document);
        QCOMPARE(prepared, 1);
        QVERIFY(port->acknowledge(key, child));
        drain();
        QVERIFY(!port->query(key, child, [](auto) {}, {}));
        port.reset();
        drain();
        QVERIFY(!stopped);
        reply.reset();
        drain();
        QVERIFY(stopped);
    }
    void rejectedApplyAdmissionIsKnownNoEffect() {
        Fixture f; f.inspect(); f.target.reject_utility = true; f.owner->apply(); f.drain();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Failed); QVERIFY(!f.snapshot.pending);
        QCOMPARE(f.writes, 0); QCOMPARE(f.queries, 0); QVERIFY(!f.snapshot.error.isEmpty());
    }
    void closeWhileNativeReadIsHeldCannotResurrectTargetObservation() {
        Fixture f; f.owner->inspect({paymentimport::Input::Kind::Text, Uri()});
        QVERIFY(f.source.step(execution::Lane::Utility));
        f.owner->chooseTarget(f.port); f.owner->close();
        // Run the close decision while the original file read stays queued.
        f.source.drain();
        QCOMPARE(f.reads, 0); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closing); QCOMPARE(f.retired, 0);
        f.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closed); QCOMPARE(f.retired, 1);
        QCOMPARE(f.writes, 0); QVERIFY(!f.snapshot.can_apply);
    }
    void replacedTargetCannotReceiveOldObservation() {
        Fixture f; f.owner->inspect({paymentimport::Input::Kind::Text, Uri()}); f.owner->chooseTarget(f.port); f.drainSource();
        f.owner->chooseTarget({}); f.drainSource(); f.target.drain(); f.drainSource();
        QVERIFY(!f.snapshot.target); QVERIFY(!f.snapshot.can_apply); QCOMPARE(f.writes, 0);
    }
    void reviewCloseRevokesOnlyUnusedPresentationAndRetainsLogicalOutcome() {
        ManualStartupExecution commands; auto base = std::make_shared<paymentimport::TargetPort>();
        base->opening = Target().opening; base->draft = 2; base->recipient = 3;
        int creates{0}, installs{0}; std::function<void()> gui;
        auto services = ReviewPorts(commands); services.view = 7; services.current = [] { return true; };
        services.prepare = [&](auto, auto done, auto failed) {
            return PostTask(commands.core, execution::MakeTask([&, done] {
                ++creates; psbtwork::ImportResult value; value.review = std::make_shared<const walletreview::Snapshot>(); done(value);
            }, [] {}, std::move(failed)));
        };
        services.present = [&](auto, auto, auto, auto allowed) { gui = [&, allowed] { if (allowed->load()) ++installs; }; };
        auto port = paymentimport::WithReviewFactory(base, services); QVERIFY(port);
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::CreateReview, Target(), false};
        QVERIFY(port->dispatch({42, 1}, child, std::make_shared<const paymentimport::Inspection>(Psbt()), [](auto) {}, {}));
        commands.drain(); QCOMPARE(creates, 1); QVERIFY(gui);
        port->retire({42, 1}, child); gui(); QCOMPARE(installs, 0);
        std::optional<paymentimport::ChildReply> result;
        QVERIFY(port->query({42, 1}, child, [&](auto reply) { result = reply; }, {})); commands.drain();
        QVERIFY(result); QCOMPARE(result->receipt.outcome, paymentimport::Outcome::ReviewOpened); QCOMPARE(creates, 1);
        QVERIFY(port->acknowledge({42, 1}, child)); commands.drain();
        result.reset(); port.reset(); commands.drain();
        QVERIFY(commands.quiescent());
    }
    void reviewFactoryRetainsKnownFailuresForLostAcknowledgment() {
        ManualStartupExecution commands; auto base = std::make_shared<paymentimport::TargetPort>();
        base->opening = Target().opening; base->draft = 2; base->recipient = 3; int creates{0};
        auto services = ReviewPorts(commands); services.view = 7; services.current = [] { return true; };
        services.prepare = [&](auto, auto done, auto failed) {
            return PostTask(commands.core, execution::MakeTask([&] { ++creates; throw std::runtime_error{"read failure"}; },
                [] {}, std::move(failed)));
        };
        auto port = paymentimport::WithReviewFactory(base, services); QVERIFY(port);
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::CreateReview, Target(), false};
        QVERIFY(port->dispatch({42, 1}, child, std::make_shared<const paymentimport::Inspection>(Psbt()), [](auto) {}, {})); commands.drain();
        std::optional<paymentimport::ChildReply> result;
        QVERIFY(port->query({42, 1}, child, [&](auto reply) { result = reply; }, {})); commands.drain();
        QVERIFY(result); QCOMPARE(result->receipt.outcome, paymentimport::Outcome::Rejected); QCOMPARE(creates, 1);
        QVERIFY(!result->review->message.isEmpty()); QVERIFY(port->acknowledge({42, 1}, child)); commands.drain();
        result.reset(); port.reset(); commands.drain();
        QVERIFY(commands.quiescent());
    }
    void originalExportRequiresMatchingSourceToken() {
        ManualStartupExecution commands; std::deque<std::function<void()>> gui;
        BitcoinUriModel model; QObject owner;
        model.setServices([&](auto post) { gui.push_back(std::move(post)); }, &commands.executor());
        const auto bytes = psbtwork::SerializeRaw(*Document());
        auto* operation = model.requestImport({paymentimport::Input::Kind::Text, QString::fromLatin1(bytes.toBase64())}, {}, &owner); QVERIFY(operation);
        commands.drain(); while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
        QVERIFY(operation->canExport()); QVERIFY(!operation->exportToken().isEmpty());
        QTemporaryDir dir; QVERIFY(dir.isValid());
        QVERIFY(!operation->requestSaveOriginal(operation->exportToken() + "wrong", dir.filePath("wrong")));
        QVERIFY(operation->requestSaveOriginal(operation->exportToken(), dir.filePath("original.psbt")));
        for (int turn{0}; turn < 20; ++turn) { commands.drain(); while (!gui.empty()) { auto post = std::move(gui.front()); gui.pop_front(); post(); } }
        QVERIFY(operation->fileExportOperation()->saved());
        QFile saved{dir.filePath("original.psbt")}; QVERIFY(saved.open(QIODevice::ReadOnly)); QCOMPARE(saved.readAll(), bytes);
        operation->cancel(); QVERIFY(!operation->requestSaveOriginal(operation->exportToken(), dir.filePath("closed")));
        model.close(); delete operation;
        do {
            commands.drain();
            while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
        } while (!commands.quiescent());
        QVERIFY(commands.quiescent());
    }
    void originalExportDrainsOnUtility_data()
    {
        QTest::addColumn<int>("mode");
        QTest::newRow("retire-unanswered-overwrite") << 0;
        QTest::newRow("finish-admitted-write") << 1;
        QTest::newRow("refuse-wallet-file") << 2;
    }

    void originalExportDrainsOnUtility()
    {
        QFETCH(int, mode);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("original.psbt"));
        const QByteArray previous{"original contents"};
        if (mode != 1) {
            QFile existing{path};
            QVERIFY(existing.open(QIODevice::WriteOnly));
            QCOMPARE(existing.write(previous), previous.size());
        }
        const auto bytes = psbtwork::SerializeRaw(*Document());
        const auto coordinator = std::make_shared<backup::InstallCoordinator>();
        if (mode == 2) coordinator->wallet_files = [path] { return std::vector<std::string>{path.toStdString()}; };
        execution::Executor executor{execution::Executor::Config{}};
        BitcoinUriModel model;
        model.setServices(presentation::QueuedPost(), &executor);
        model.setExportCoordinator(coordinator);
        auto view = std::make_unique<QObject>();
        const auto drained = std::make_shared<std::atomic_bool>(false);
        const auto close = [&] {
            model.close();
            model.whenDrained([drained] { drained->store(true); });
        };
        const auto cleanup = qScopeGuard([&] {
            if (!drained->load()) close();
            QVERIFY(QTest::qWaitFor([&] { return drained->load(); }, 5000));
        });

        auto* operation = model.requestImport({paymentimport::Input::Kind::Text, QString::fromLatin1(bytes.toBase64())}, {}, view.get());
        QVERIFY(operation);
        QTRY_VERIFY(operation->canExport());
        std::unique_lock install_lock{coordinator->mutex, std::defer_lock};
        if (mode == 1) install_lock.lock();
        QVERIFY(operation->requestSaveOriginal(operation->exportToken(), path));
        const QPointer<FileExportOperationModel> export_operation{operation->fileExportOperation()};
        QVERIFY(export_operation);
        if (mode == 1) {
            // Installation follows staging and will wait on the shared lock.
            // Retiring here leaves the already admitted writer in charge.
            QTRY_VERIFY(!QDir(directory.path()).entryList({QStringLiteral(".bitcoin-backup-*")}, QDir::Dirs | QDir::Hidden).isEmpty());
            close();
            view.reset();
            QVERIFY(!export_operation);
            QVERIFY(!drained->load());
            QVERIFY(!QFileInfo::exists(path));
            install_lock.unlock();
        } else if (mode == 0) {
            QTRY_VERIFY(export_operation->confirmationRequired());
            close();
            QTRY_VERIFY(!export_operation->busy());
            QVERIFY(!export_operation->saved());
        } else {
            QTRY_VERIFY(!export_operation->busy());
            QCOMPARE(export_operation->error(), QStringLiteral("Choose a file that is not a wallet."));
            QVERIFY(!export_operation->confirmationRequired());
            close();
        }
        QTRY_VERIFY(drained->load());
        QFile saved{path};
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), mode == 1 ? bytes : previous);
    }
    void closedTypedSourcesReleaseTheirActionScopes() {
        ManualStartupExecution commands; BitcoinUriModel importer; QObject view;
        importer.setServices({}, &commands.executor());
        for (int n{0}; n < 32; ++n) {
            auto* operation = importer.requestImport({paymentimport::Input::Kind::Text, Uri()}, {}, &view); QVERIFY(operation);
            commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            operation->cancel(); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); commands.drain();
        }
        bool drained{false};
        importer.close();
        importer.whenDrained([&] { drained = true; });
        commands.drain();
        QVERIFY(drained);
        QVERIFY(commands.quiescent());
    }
    void closedSchedulerAndUnusedActorCannotRetainScope() {
        ManualStartupExecution refused;
        paymentimport::Services services;
        services.reads = std::make_shared<execution::SerialQueue>(refused.executor().makeQueue(execution::Lane::Utility));
        services.outcomes = std::make_shared<execution::SerialQueue>(refused.executor().makeQueue(execution::Lane::Utility));
        services.outcomes->close();
        int publications{0};
        auto rejected = paymentimport::Service::Create(89, services, [&](auto) { ++publications; });
        QVERIFY(rejected);
        QCOMPARE(rejected->inspect({paymentimport::Input::Kind::Text, Uri()}).rejection, execution::Rejection::Transport);
        const std::weak_ptr<paymentimport::Service> original_rejected{rejected};
        rejected.reset();
        refused.drain();
        QCOMPARE(publications, 0);
        QVERIFY(original_rejected.expired());
        QVERIFY(refused.quiescent());
        ManualStartupExecution unused;
        services.reads = std::make_shared<execution::SerialQueue>(unused.executor().makeQueue(execution::Lane::Utility));
        services.outcomes = std::make_shared<execution::SerialQueue>(unused.executor().makeQueue(execution::Lane::Utility));
        auto owner = paymentimport::Service::Create(90, std::move(services), {});
        QVERIFY(owner);
        const std::weak_ptr<paymentimport::Service> original{owner};
        owner.reset();
        unused.drain();
        QVERIFY(original.expired());
        QVERIFY(unused.quiescent());
    }
    void throwingRetireKeepsOriginalChildOutcomeAndDoesNotDispatchAgain() {
        Fixture f; f.inspect(); f.lose_reply = true;
        QVERIFY(f.owner->apply()); f.drain(); QCOMPARE(f.writes, 1);
        int retire_calls{0};
        f.port->retire = [&](auto, auto) { ++retire_calls; throw std::runtime_error{"Retirement transport failed"}; };
        QVERIFY(f.owner->close()); f.drain();
        QCOMPARE(retire_calls, 1); QCOMPARE(f.queries, 1); QCOMPARE(f.writes, 1); QCOMPARE(f.dispatches, 1);
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closed); QCOMPARE(f.retired, 1);
        QVERIFY(f.snapshot.result); QCOMPARE(f.snapshot.result->outcome, paymentimport::Outcome::Applied);
    }
    void realThreadSourceReadAndDomainDecisionsStayOutsideGui() {
        execution::Executor executor{execution::Executor::Config{}};
        std::atomic<QThread*> native{nullptr}, decision{nullptr};
        QSemaphore entered, release;
        paymentimport::Snapshot observed;
        QObject receiver;
        paymentimport::Services services;
        services.reads = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        services.outcomes = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Utility));
        services.read = [&](const auto& input) {
            native = QThread::currentThread();
            entered.release();
            release.acquire();
            return paymentimport::ReadSource(input);
        };
        auto owner = paymentimport::Service::Create(73, services, [&](auto snapshot) {
            decision = QThread::currentThread();
            QMetaObject::invokeMethod(&receiver, [&, snapshot = std::move(snapshot)] { observed = snapshot; }, Qt::QueuedConnection);
        });
        const auto cleanup = qScopeGuard([&] {
            release.release();
            bool retired{true};
            if (owner) {
                owner->close();
                retired = QTest::qWaitFor([&] { return observed.phase == paymentimport::Phase::Closed; }, 5'000);
                owner.reset();
            }
            executor.stopAndJoin();
            QVERIFY(retired);
        });
        QVERIFY(owner);
        QVERIFY(owner->inspect({paymentimport::Input::Kind::Text, Uri()}));
        QTRY_VERIFY(entered.available() > 0);
        QTRY_COMPARE(observed.phase, paymentimport::Phase::Reading);
        QVERIFY(!observed.inspection);
        release.release();
        QTRY_COMPARE(observed.phase, paymentimport::Phase::Inspecting);
        QVERIFY(native.load()); QVERIFY(decision.load());
        QVERIFY(native.load() != QThread::currentThread());
        QVERIFY(decision.load() != QThread::currentThread());
        QVERIFY(native.load() == decision.load());
        QCOMPARE(observed.inspection->payment.address, Address());
    }
    void realTypedConsumerUsesPublicReviewFactoryAndOriginalView_data() {
        QTest::addColumn<int>("boundary");
        QTest::newRow("original") << 0;
        QTest::newRow("discard-before-gui-adoption") << 1;
        QTest::newRow("source-close-after-receipt-before-adoption") << 2;
        QTest::newRow("target-close-before-native-read") << 3;
        QTest::newRow("source-close-domain-held-after-receipt") << 4;
        QTest::newRow("source-close-domain-held-before-receipt") << 5;
    }
    void realTypedConsumerUsesPublicReviewFactoryAndOriginalView() {
        QFETCH(int, boundary);
        ManualStartupExecution commands;
        ManualStartupExecution sources;
        std::deque<std::function<void()>> target_gui, source_gui;
        FeeReadServices fees; fees.post = [&](auto post) { target_gui.push_back(std::move(post)); };
        auto wallet = std::make_shared<MockWallet>();
        WalletModel model{WalletWithQueues(wallet, commands.executor()), nullptr, fees, {}, {}};
        ManualWalletDrain drain{commands, &model};
        BitcoinUriModel importer; QObject view;
        importer.setServices([&](auto post) { source_gui.push_back(std::move(post)); }, &sources.executor());
        const auto publish = [](auto& queue) { while (!queue.empty()) { auto turn = std::move(queue.front()); queue.pop_front(); turn(); } };
        const auto pump = [&] { sources.drain(); commands.drain(); publish(target_gui); publish(source_gui); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); };
        const auto cleanup = qScopeGuard([&] {
            importer.close(); model.beginShutdown();
            if (!QTest::qWaitFor([&] { pump(); return model.shutdownComplete() && drain.nativeDrained() && commands.quiescent(); }, 3000))
                QTest::qFail("Original typed import and wallet graph did not physically drain", __FILE__, __LINE__);
        });
        QVERIFY(QTest::qWaitFor([&] { pump(); return model.capabilitiesKnown() && !model.sendRecipientList()->validationPending(); }, 5000));
        const auto document = psbtfeefixture::Legacy(2, false);
        const auto bytes = psbtwork::SerializeRaw(document);
        auto* operation = importer.requestImportText(QString::fromLatin1(bytes.toBase64()), &model, model.sendRecipientList()->currentRecipient(), &view);
        QVERIFY(operation);
        QVERIFY(QTest::qWaitFor([&] { pump(); return operation->canReview() || operation->state() == PaymentImportOperation::State::Failed; }, 5000));
        QCOMPARE(operation->state(), PaymentImportOperation::State::Inspecting); QVERIFY(operation->canReview());
        operation->openReview(); sources.drain();
        if (boundary == 3) model.close();
        commands.drain();
        QVERIFY(!model.currentTransaction()); // The Native factory is separate from GUI adoption.
        if (boundary != 5) {
            sources.drain(); publish(source_gui);
            QCOMPARE(operation->state(), boundary == 3 ? PaymentImportOperation::State::Inspecting : PaymentImportOperation::State::ReviewOpened);
            if (boundary == 3) { QVERIFY(!operation->canReview()); QVERIFY(!operation->error().isEmpty()); }
        }
        if (boundary == 1) model.discardCurrentTransaction();
        if (boundary == 2) { operation->cancel(); sources.drain(); }
        if (boundary == 4 || boundary == 5) operation->cancel(); // Hold Source close/receipt; GUI cancellation already happened.
        publish(target_gui);
        if (boundary == 0) {
            QVERIFY(model.currentTransaction()); QVERIFY(model.currentTransactionIsImportedPsbt());
            QCOMPARE(model.importedReviewOwner(), operation->requestId()); QCOMPARE(model.importedReviewChild(), operation->resultId());
            QCOMPARE(model.importedReviewToken(), operation->resultToken());
            QCOMPARE(psbtwork::SerializeRaw(*model.currentTransaction()->assessedPsbt()), bytes);
            QVERIFY(!model.currentTransactionCanSend()); // Mock owns no inputs; import carries no send intent.
        } else QVERIFY(!model.currentTransaction());
        QVERIFY(operation->canExport() || boundary == 2 || boundary == 4 || boundary == 5);
        operation->cancel(); for (int turn{0}; turn < 10; ++turn) pump();
        if (boundary != 3) {
            QCOMPARE(operation->state(), PaymentImportOperation::State::Closed);
            QCOMPARE(operation->outcome(), PaymentImportOperation::Outcome::ReviewOpenedOutcome);
            QVERIFY(!operation->resultToken().isEmpty()); // Cancellation retires presentation, never erases the created child's outcome.
        }
    }
    void actualDraftReceiptSurvivesSourceDismissalAndQuietTargetClose() {
        ManualStartupExecution commands;
        ManualStartupExecution sources; std::deque<std::function<void()>> target_gui, source_gui;
        FeeReadServices fees; fees.post = [&](auto post) { target_gui.push_back(std::move(post)); };
        auto wallet = std::make_shared<MockWallet>();
        WalletModel model{WalletWithQueues(wallet, commands.executor()), nullptr, fees, {}, {}};
        ManualWalletDrain drain{commands, &model}; BitcoinUriModel importer; QObject view;
        importer.setServices([&](auto post) { source_gui.push_back(std::move(post)); }, &sources.executor());
        const auto publish = [](auto& queue) { while (!queue.empty()) { auto turn = std::move(queue.front()); queue.pop_front(); turn(); } };
        const auto pump = [&] { sources.drain(); commands.drain(); publish(target_gui); publish(source_gui); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); };
        const auto cleanup = qScopeGuard([&] {
            importer.close(); model.beginShutdown();
            if (!QTest::qWaitFor([&] { pump(); return model.shutdownComplete() && drain.nativeDrained() && commands.quiescent() && sources.quiescent() && target_gui.empty() && source_gui.empty(); }, 3000))
                QTest::qFail("Original applied import graph did not physically drain", __FILE__, __LINE__);
        });
        QVERIFY(QTest::qWaitFor([&] { pump(); return model.capabilitiesKnown() && !model.sendRecipientList()->validationPending(); }, 5000));
        const auto target = model.sendRecipientList()->importTarget(model.sendRecipientList()->currentRecipient()); QVERIFY(target);
        auto lossy = std::make_shared<paymentimport::TargetPort>(*target); int dispatches{0}, queries{0};
        lossy->dispatch = [target, &dispatches](auto key, auto child, auto source, auto, auto failed) {
            ++dispatches; return target->dispatch(key, child, std::move(source), [](auto) {}, std::move(failed));
        };
        lossy->query = [target, &queries](auto key, auto child, auto done, auto failed) {
            ++queries; return target->query(key, child, std::move(done), std::move(failed));
        };
        auto* operation = importer.requestImport({paymentimport::Input::Kind::Text, Uri()}, lossy, &view); QVERIFY(operation);
        QVERIFY(QTest::qWaitFor([&] { pump(); return operation->canApply(); }, 5000));
        QCOMPARE(operation->sourceKind(), PaymentImportOperation::SourceKind::PaymentSource);
        operation->apply(); sources.drain(); commands.drain(); publish(source_gui);
        QCOMPARE(dispatches, 1); QCOMPARE(operation->state(), PaymentImportOperation::State::Applying);
        model.close(); model.opening()->coreQueue()->close(); operation->cancel();
        QVERIFY(QTest::qWaitFor([&] { pump(); return operation->state() == PaymentImportOperation::State::Closed; }, 5000));
        QCOMPARE(operation->outcome(), PaymentImportOperation::Outcome::AppliedOutcome);
        QVERIFY(operation->resultId() > 0); QCOMPARE(dispatches, 1); QVERIFY(queries >= 1); QVERIFY(!operation->pending());
        delete operation; // Release the retained Source snapshot before its manual Native transport.
    }
    void appliedDraftReceiptStaysOnOriginalUtilityQueueThroughClose()
    {
        const auto node_free_wallet = std::make_shared<MockWallet>();
        WalletModel model{WalletModel::SharedWallet{node_free_wallet}, nullptr, {}, {}, {}};
        const auto native_drained = WatchWalletNativeDrain(model);
        const auto cleanup = qScopeGuard([&] {
            model.beginShutdown();
            QVERIFY(QTest::qWaitFor([&] { return model.shutdownComplete() && *native_drained; }, 5000));
        });
        const auto opening = model.opening();
        QVERIFY(opening && opening->utilityQueue());
        QTRY_VERIFY(model.capabilitiesKnown() && !model.sendRecipientList()->validationPending());
        auto* recipient = model.sendRecipientList()->currentRecipient();
        QVERIFY(recipient);
        recipient->setLabel(QStringLiteral("private note"));
        QTRY_VERIFY(!model.sendRecipientList()->validationPending());
        QVERIFY(opening->utilityQueue());
        auto port = model.importTarget(recipient);
        QVERIFY(port);

        auto source = std::make_shared<const paymentimport::Inspection>(
            paymentimport::ReadSource({paymentimport::Input::Kind::Text, Uri()}));
        struct Replies {
            QSemaphore observed, applied, queried;
            paymentimport::Target facts;
            std::optional<paymentimport::ChildReply> reply;
            QThread* observation_thread{nullptr};
            QThread* query_thread{nullptr};
        };
        const auto replies = std::make_shared<Replies>();
        QVERIFY(port->observe(source, [replies](auto facts) {
            replies->facts = std::move(facts);
            replies->observation_thread = QThread::currentThread();
            replies->observed.release();
        }, {}));
        QVERIFY(replies->observed.tryAcquire(1, 5000));
        QVERIFY(replies->facts.compatible);
        QVERIFY(replies->observation_thread != QThread::currentThread());
        QCOMPARE(replies->facts.recipient, recipient->identity());
        const paymentimport::CommandKey key{42, 1};
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::Apply, replies->facts, false};
        QSignalSpy changed{recipient->amount(), &BitcoinAmount::amountChanged};
        QVERIFY(port->dispatch(key, child, source, [replies](auto) { replies->applied.release(); }, {}));
        QVERIFY(replies->applied.tryAcquire(1, 5000));
        QTRY_COMPARE(recipient->cAmount(), CAmount{12'345'678});
        QCOMPARE(recipient->label(), QStringLiteral("private note"));
        QCOMPARE(changed.size(), 1);

        model.beginShutdown();
        QVERIFY(!model.available());
        QVERIFY(!*native_drained);
        QVERIFY(port->query(key, child, [replies](auto reply) {
            replies->reply = std::move(reply);
            replies->query_thread = QThread::currentThread();
            replies->queried.release();
        }, {}));
        QVERIFY(replies->queried.tryAcquire(1, 5000));
        QVERIFY(replies->query_thread == replies->observation_thread);
        QVERIFY(replies->reply && replies->reply->applied);
        QCOMPARE(replies->reply->receipt.outcome, paymentimport::Outcome::Applied);
        QCOMPARE(replies->reply->receipt.revision, replies->facts.revision + 1);
        QCOMPARE(replies->reply->applied->projection->label, QStringLiteral("private note"));
        QVERIFY(port->acknowledge(key, child));
        port.reset();
        QVERIFY(!*native_drained);
        replies->reply.reset();
        QTRY_VERIFY(*native_drained && model.shutdownComplete());
    }
    void sourceImportUsesUtilityAndDrainsItsOriginalReceipt()
    {
        std::deque<std::function<void()>> work, gui;
        execution::Executor executor{[&](execution::Lane lane, execution::UtilityWorker worker, auto turn) {
            if (lane != execution::Lane::Utility || worker != execution::UtilityWorker::IO) return false;
            work.push_back(std::move(turn));
            return true;
        }};
        const auto drain = [](auto& turns) {
            while (!turns.empty()) {
                auto turn = std::move(turns.front());
                turns.pop_front();
                turn();
            }
        };
        BitcoinUriModel model;
        QObject view;
        model.setServices([&](auto turn) { gui.push_back(std::move(turn)); }, &executor);
        auto target = std::make_shared<paymentimport::TargetPort>();
        const auto original = Target();
        target->opening = original.opening;
        target->draft = original.draft;
        target->recipient = original.recipient;
        target->observe = [original](auto, auto done, auto) {
            done(original);
            return execution::SubmitResult{};
        };
        int applies{0}, queries{0}, acknowledged{0}, retired{0};
        std::optional<paymentimport::ChildReply> receipt;
        paymentimport::TargetPort::Completed late_dispatch, late_query;
        target->dispatch = [&](auto, auto child, auto, auto done, auto) {
            ++applies;
            receipt = paymentimport::ChildReply{{child, paymentimport::Outcome::Applied, 7, original.revision + 1}, {}, {}, {}};
            late_dispatch = std::move(done);
            return execution::SubmitResult{};
        };
        target->query = [&](auto, auto, auto done, auto) {
            ++queries;
            late_query = std::move(done);
            return execution::SubmitResult{};
        };
        target->acknowledge = [&](auto, auto) { ++acknowledged; return execution::SubmitResult{}; };
        target->retire = [&](auto, auto) { ++retired; };
        const auto finish_receipt = [&] {
            if (!receipt) return;
            if (auto reply = std::exchange(late_query, {})) reply(*receipt);
            if (auto reply = std::exchange(late_dispatch, {})) reply(*receipt);
        };
        bool drained{false};
        const auto cleanup = qScopeGuard([&] {
            model.close();
            drain(work);
            finish_receipt();
            drain(work);
            drain(gui);
        });

        auto* operation = model.requestImport({paymentimport::Input::Kind::Text, Uri()}, target, &view);
        QVERIFY(operation);
        QVERIFY(operation->pending());
        QVERIFY(operation->address().isEmpty());
        drain(work);
        QVERIFY(operation->address().isEmpty());
        drain(gui);
        QCOMPARE(operation->address(), Address());
        QCOMPARE(operation->amount(), qint64{12'345'678});
        QVERIFY(operation->canApply());
        operation->apply();
        drain(work);
        drain(gui);
        QCOMPARE(applies, 1);
        QCOMPARE(operation->state(), PaymentImportOperation::State::Applying);

        model.close();
        model.whenDrained([&] { drained = true; });
        drain(work);
        drain(gui);
        QCOMPARE(operation->state(), PaymentImportOperation::State::Closing);
        QCOMPARE(queries, 1);
        QCOMPARE(retired, 1);
        QVERIFY(!drained);
        QVERIFY(receipt && late_query && late_dispatch);

        auto delayed = std::exchange(late_dispatch, {});
        std::exchange(late_query, {})(*receipt);
        drain(work);
        drain(gui);
        QVERIFY(drained);
        QCOMPARE(acknowledged, 1);
        QCOMPARE(operation->state(), PaymentImportOperation::State::Closed);
        QCOMPARE(operation->outcome(), PaymentImportOperation::Outcome::AppliedOutcome);
        QCOMPARE(operation->resultId(), quint64{7});
        delayed(*receipt);
        drain(work);
        drain(gui);
        QCOMPARE(applies, 1);
        QCOMPARE(acknowledged, 1);
    }
    void typedFacadePublishesBoundedSourceWithoutTargetOrGuiParsing() {
        ManualStartupExecution commands; std::deque<std::function<void()>> gui;
        BitcoinUriModel model; QObject owner;
        model.setServices([&](auto post) { gui.push_back(std::move(post)); }, &commands.executor());
        auto* operation = model.requestImport({paymentimport::Input::Kind::Text, Uri()}, {}, &owner); QVERIFY(operation); QVERIFY(operation->pending());
        commands.drain(); QVERIFY(operation->address().isEmpty());
        while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
        QCOMPARE(operation->sourceKind(), PaymentImportOperation::SourceKind::PaymentSource);
        QCOMPARE(operation->outcome(), PaymentImportOperation::Outcome::NoOutcome);
        QCOMPARE(operation->address(), Address()); QCOMPARE(operation->publicLabel(), QStringLiteral("Public"));
        QCOMPARE(operation->message(), QStringLiteral("Message")); QCOMPARE(operation->amount(), qint64{12'345'678}); QVERIFY(!operation->canApply());
        operation->cancel(); commands.drain(); model.close(); delete operation;
        do {
            commands.drain();
            while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
        } while (!commands.quiescent());
        QVERIFY(commands.quiescent());
    }
};
BITCOINQML_REGISTER_QT_TEST(PaymentImportTests)
#include <test_paymentimport.moc>
