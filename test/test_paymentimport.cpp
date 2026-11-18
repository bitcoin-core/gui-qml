// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/paymentimport.h>
#include <qml/backendexecutor.h>
#include <qml/guipresentation.h>
#include <qml/paymentimportreview.h>
#include <qml/models/bitcoinurimodel.h>
#include <qml/psbtwork.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <test/mocks/mockwallet.h>
#include <test/mocks/manualwalletdrain.h>
#include <test/mocks/psbtfeefixture.h>
#include <key_io.h>
#include <QFile>
#include <QEvent>
#include <QScopeGuard>
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
struct Fixture {
    ManualRoutedExecution source, target;
    paymentimport::Snapshot snapshot;
    std::chrono::steady_clock::time_point now{};
    std::vector<std::function<void()>> wakes;
    std::shared_ptr<paymentimport::Owner> owner;
    std::shared_ptr<paymentimport::TargetPort> port{std::make_shared<paymentimport::TargetPort>()};
    paymentimport::Target facts{Target()};
    int reads{0}, analyses{0}, writes{0}, dispatches{0}, queries{0}, acks{0}, retired{0};
    bool lose_reply{false}, unknown_query{false}, reject_ack{false}, read_fail{false};
    std::optional<paymentimport::ChildReply> result;
    Fixture() {
        port->opening = facts.opening; port->draft = facts.draft; port->recipient = facts.recipient;
        port->observe = [this](auto, auto done, auto failed) {
            return execution::Submit(*target.scheduler, [this] { return facts; }, std::move(done), std::move(failed), {execution::Lane::Domain});
        };
        port->dispatch = [this](auto, auto child, auto, auto done, auto failed) {
            return execution::Submit(*target.scheduler, [this, child] {
                ++dispatches;
                if (!facts.available) result = paymentimport::ChildReply{{child, paymentimport::Outcome::Rejected, 0, facts.revision}, facts, {}, {}};
                else if (child.target.revision != facts.revision) result = paymentimport::ChildReply{{child, paymentimport::Outcome::Stale, 0, facts.revision}, facts, {}, {}};
                else { ++writes; result = paymentimport::ChildReply{{child, paymentimport::Outcome::Applied, facts.revision + 1, facts.revision + 1}, {}, {}, {}}; }
                return *result;
            }, [this, done](auto reply) { if (!lose_reply) done(std::move(reply)); }, std::move(failed), {execution::Lane::Domain});
        };
        port->query = [this](auto, auto child, auto done, auto failed) {
            return execution::Submit(*target.scheduler, [this, child] {
                ++queries; return unknown_query || !result ? paymentimport::ChildReply{{child, paymentimport::Outcome::Unknown, 0, 0}, {}, {}, {}} : *result;
            }, std::move(done), std::move(failed), {execution::Lane::Domain});
        };
        port->acknowledge = [this](auto, auto) { ++acks; return execution::Admission{{}, reject_ack ? execution::Rejection::Transport : execution::Rejection::None}; };
        paymentimport::Services services; services.scheduler = source.scheduler;
        services.read = [this](const auto& input) { ++reads; if (read_fail) { paymentimport::Inspection failed; failed.error = "read unavailable"; return failed; } return paymentimport::ReadSource(input); };
        services.analyze = [this](const auto& input) { ++analyses; return paymentimport::AnalyzeSource(input); };
        services.now = [this] { return now; }; services.wake = [this](auto, auto wake) { wakes.push_back(std::move(wake)); };
        owner = paymentimport::Owner::Create(42, services, [this](auto value) { snapshot = std::move(value); }, [this](auto) { ++retired; });
    }
    ~Fixture() {
        owner->close(); drain();
        // Release clock callbacks and published Native aliases before the final
        // manual transport drain; its queued cleanup must outlive the fixture.
        wakes.clear(); owner.reset(); snapshot = {}; port.reset(); result.reset(); drain();
    }
    void drain() { for (size_t n{0}; n < 20 && (!source.queuedIdle() || !target.queuedIdle()); ++n) { source.drain(); target.drain(); } }
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
        ManualRoutedExecution commands; BitcoinUriModel importer; QObject view; importer.setServices({commands.scheduler, {}});
        QTemporaryDir dir; QVERIFY(dir.isValid()); const auto path = dir.filePath("request-without-extension");
        { QFile file{path}; QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(Uri().toUtf8()), Uri().toUtf8().size()); }
        auto* source = importer.captureImport(nullptr, nullptr, &view); QVERIFY(source); commands.drain();
        QCOMPARE(source->state(), PaymentImportOperation::State::Empty);
        QVERIFY(source->requestFile(QUrl::fromLocalFile(path))); QVERIFY(!source->requestFile(QUrl::fromLocalFile(path))); commands.drain();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); QCOMPARE(source->sourceKind(), PaymentImportOperation::SourceKind::PaymentSource);
        source->cancel(); QVERIFY(!source->requestFile(QUrl::fromLocalFile(path))); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        auto* cancelled = importer.captureImport(nullptr, nullptr, &view); QVERIFY(cancelled); cancelled->cancel();
        QVERIFY(!cancelled->requestFile(QUrl::fromLocalFile(path))); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        importer.closeAdmission(); commands.drain();
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
        f.source.drain(); QCOMPARE(f.reads, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Inspecting);
        QVERIFY(!f.snapshot.can_apply); QCOMPARE(f.writes, 0);
        f.target.drain(); QVERIFY(!f.snapshot.can_apply); // Foreign Domain only fills a reply cell.
        f.source.drain(); QVERIFY(f.snapshot.can_apply); QCOMPARE(f.writes, 0);
        f.owner->apply(); f.source.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying);
        f.target.drain(); QCOMPARE(f.writes, 1); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applying);
        f.source.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Applied); QVERIFY(!f.snapshot.pending); QCOMPARE(f.acks, 1);
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
        paymentimport::ReviewServices services; services.scheduler = f.target.scheduler; services.view = 7; services.current = [] { return true; };
        services.prepare = [&](auto, auto done, auto failed) {
            return execution::Submit(*f.target.scheduler, [&, done] {
                ++creates; psbtwork::ImportResult result; result.review = std::make_shared<const walletreview::Snapshot>(); result.document = Document(); done(result);
            }, [] {}, std::move(failed), {execution::Lane::Core});
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
        paymentimport::ReviewServices services; services.scheduler = f.target.scheduler; services.view = 7; services.current = [&] { return target_open; };
        services.prepare = [&](auto, auto done, auto failed) {
            return execution::Submit(*f.target.scheduler, [&, done] {
                ++creates; psbtwork::ImportResult result; result.review = std::make_shared<const walletreview::Snapshot>(); result.document = Document(); done(result);
            }, [] {}, std::move(failed), {execution::Lane::Core});
        };
        const auto factory = paymentimport::WithReviewFactory(second, services); QVERIFY(factory);
        auto lossy = std::make_shared<paymentimport::TargetPort>(*factory);
        lossy->dispatch = [factory](auto key, auto child, auto source, auto, auto failed) {
            return factory->dispatch(key, child, std::move(source), [](auto) {}, std::move(failed));
        };
        f.owner->inspect({paymentimport::Input::Kind::Text, QString::fromLatin1(psbtwork::SerializeRaw(*Document()).toBase64())}); f.owner->chooseTarget(f.port);
        while (f.source.step(execution::Lane::Domain)) {}
        QVERIFY(f.source.step(execution::Lane::Core)); while (f.source.step(execution::Lane::Domain)) {}
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Analyzing); QCOMPARE(f.analyses, 0);
        f.owner->chooseTarget(lossy); while (f.source.step(execution::Lane::Domain)) {}
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
        f.target.rejectLane(execution::Lane::Domain); f.advance();
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
        ManualRoutedExecution commands; auto base = std::make_shared<paymentimport::TargetPort>();
        base->opening = Target().opening; base->draft = 2; base->recipient = 3;
        int creates{0}, presentations{0}; bool current{true};
        paymentimport::ReviewServices services; services.scheduler = commands.scheduler; services.view = 7; services.current = [&] { return current; };
        services.prepare = [&](auto, auto done, auto failed) {
            return execution::Submit(*commands.scheduler, [&, done] {
                ++creates; psbtwork::ImportResult result; result.review = std::make_shared<const walletreview::Snapshot>(); result.document = Document(); done(result);
            }, [] {}, std::move(failed), {execution::Lane::Core});
        };
        services.present = [&](auto, auto, auto, auto) { ++presentations; };
        auto port = paymentimport::WithReviewFactory(base, services); QVERIFY(port);
        const paymentimport::Child child{1, 1, paymentimport::ChildKind::CreateReview, Target(), false};
        std::optional<paymentimport::ChildReply> queried; const auto source = std::make_shared<const paymentimport::Inspection>(Psbt());
        QVERIFY(port->dispatch({42, 1}, child, source, [](auto) {}, {}));
        commands.drain(); QCOMPARE(creates, 1); QCOMPARE(presentations, 1);
        current = false; commands.scheduler->closeOrdinaryAdmission();
        QVERIFY(port->query({42, 1}, child, [&](auto reply) { queried = reply; }, {})); commands.drain();
        QVERIFY(queried); QCOMPARE(queried->receipt.outcome, paymentimport::Outcome::ReviewOpened); QCOMPARE(creates, 1);
        QVERIFY(port->acknowledge({42, 1}, child)); commands.drain();
        queried.reset(); port.reset(); commands.drain();
        QVERIFY(commands.quiescent());
    }
    void rejectedApplyAdmissionIsKnownNoEffect() {
        Fixture f; f.inspect(); f.target.rejectLane(execution::Lane::Domain); f.owner->apply(); f.drain();
        QCOMPARE(f.snapshot.phase, paymentimport::Phase::Failed); QVERIFY(!f.snapshot.pending);
        QCOMPARE(f.writes, 0); QCOMPARE(f.queries, 0); QVERIFY(!f.snapshot.error.isEmpty());
    }
    void closeWhileNativeReadIsHeldCannotResurrectTargetObservation() {
        Fixture f; f.owner->inspect({paymentimport::Input::Kind::Text, Uri()});
        QVERIFY(f.source.step(execution::Lane::Domain));
        f.owner->chooseTarget(f.port); f.owner->close();
        // Deliver reserved close through Cleanup while the original Core read
        // stays held. Its lifecycle decision still runs only on Domain.
        while (f.source.step(execution::Lane::Cleanup) || f.source.step(execution::Lane::Domain)) {}
        QCOMPARE(f.reads, 0); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closing); QCOMPARE(f.retired, 0);
        f.drain(); QCOMPARE(f.snapshot.phase, paymentimport::Phase::Closed); QCOMPARE(f.retired, 1);
        QCOMPARE(f.writes, 0); QVERIFY(!f.snapshot.can_apply);
    }
    void replacedTargetCannotReceiveOldObservation() {
        Fixture f; f.owner->inspect({paymentimport::Input::Kind::Text, Uri()}); f.owner->chooseTarget(f.port); f.source.drain();
        f.owner->chooseTarget({}); f.source.drain(); f.target.drain(); f.source.drain();
        QVERIFY(!f.snapshot.target); QVERIFY(!f.snapshot.can_apply); QCOMPARE(f.writes, 0);
    }
    void reviewCloseRevokesOnlyUnusedPresentationAndRetainsLogicalOutcome() {
        ManualRoutedExecution commands; auto base = std::make_shared<paymentimport::TargetPort>();
        base->opening = Target().opening; base->draft = 2; base->recipient = 3;
        int creates{0}, installs{0}; std::function<void()> gui;
        paymentimport::ReviewServices services; services.scheduler = commands.scheduler; services.view = 7; services.current = [] { return true; };
        services.prepare = [&](auto, auto done, auto failed) {
            return execution::Submit(*commands.scheduler, [&, done] {
                ++creates; psbtwork::ImportResult value; value.review = std::make_shared<const walletreview::Snapshot>(); done(value);
            }, [] {}, std::move(failed), {execution::Lane::Core});
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
        ManualRoutedExecution commands; auto base = std::make_shared<paymentimport::TargetPort>();
        base->opening = Target().opening; base->draft = 2; base->recipient = 3; int creates{0};
        paymentimport::ReviewServices services; services.scheduler = commands.scheduler; services.view = 7; services.current = [] { return true; };
        services.prepare = [&](auto, auto done, auto failed) {
            return execution::Submit(*commands.scheduler, [&] { ++creates; throw std::runtime_error{"read failure"}; },
                [] {}, std::move(failed), {execution::Lane::Core});
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
        ManualRoutedExecution commands; std::deque<std::function<void()>> gui;
        BitcoinUriModel model; QObject owner;
        model.setServices({commands.scheduler, [&](auto post) { gui.push_back(std::move(post)); }});
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
        model.closeAdmission(); delete operation;
        do {
            commands.drain();
            while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
        } while (!commands.queuedIdle() || !commands.activeIdle());
        QVERIFY(commands.quiescent());
    }
    void closedTypedSourcesReleaseTheirActionScopes() {
        ManualRoutedExecution commands; BitcoinUriModel importer; QObject view;
        importer.setServices({commands.scheduler, {}});
        for (int n{0}; n < 32; ++n) {
            auto* operation = importer.requestImport({paymentimport::Input::Kind::Text, Uri()}, {}, &view); QVERIFY(operation);
            commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
            operation->cancel(); commands.drain(); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); commands.drain();
        }
        importer.closeAdmission(); commands.drain();
    }
    void closedSchedulerAndUnusedActorCannotRetainScope() {
        ManualRoutedExecution refused; refused.scheduler->closeAdmission(); paymentimport::Services services; services.scheduler = refused.scheduler;
        QVERIFY(!paymentimport::Owner::Create(89, std::move(services), {}));
        refused.drain(); QCOMPARE(refused.scheduler->pending(), size_t{0});
        ManualRoutedExecution unused; services = {}; services.scheduler = unused.scheduler;
        auto owner = paymentimport::Owner::Create(90, std::move(services), {}); QVERIFY(owner);
        const std::weak_ptr<paymentimport::Owner> original{owner}; owner.reset();
        unused.drain(); QVERIFY(original.expired()); QCOMPARE(unused.scheduler->pending(), size_t{0});
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
        BackendExecutor executor{execution::Options{}};
        std::atomic<QThread*> native{nullptr}, domain{nullptr};
        {
            paymentimport::Snapshot observed;
            const auto gui = presentation::QueuedPost();
            paymentimport::Services services; services.scheduler = executor.scheduler();
            services.read = [&](const auto& input) { native.store(QThread::currentThread()); return paymentimport::ReadSource(input); };
            auto owner = paymentimport::Owner::Create(73, services, [&](auto snapshot) {
                domain.store(QThread::currentThread()); gui([&, snapshot = std::move(snapshot)] { observed = snapshot; });
            });
            QVERIFY(owner); QVERIFY(owner->inspect({paymentimport::Input::Kind::Text, Uri()}));
            QTRY_COMPARE(observed.phase, paymentimport::Phase::Inspecting);
            QVERIFY(native.load()); QVERIFY(domain.load());
            QVERIFY(native.load() != QThread::currentThread()); QVERIFY(domain.load() != QThread::currentThread());
            QVERIFY(native.load() != domain.load()); QCOMPARE(observed.inspection->payment.address, Address());
            owner->close(); QTRY_COMPARE(observed.phase, paymentimport::Phase::Closed); owner.reset();
        }
        executor.shutdown(); QTRY_VERIFY(executor.isDrained());
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
        ManualRoutedExecution commands, sources;
        std::deque<std::function<void()>> target_gui, source_gui;
        FeeReadServices fees; fees.post = [&](auto post) { target_gui.push_back(std::move(post)); };
        auto wallet = std::make_shared<MockWallet>();
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, commands.scheduler}, nullptr, fees, {}, {}};
        ManualWalletDrain drain{commands, &model};
        BitcoinUriModel importer; QObject view;
        importer.setServices({sources.scheduler, [&](auto post) { source_gui.push_back(std::move(post)); }});
        const auto publish = [](auto& queue) { while (!queue.empty()) { auto turn = std::move(queue.front()); queue.pop_front(); turn(); } };
        const auto pump = [&] { sources.drain(); commands.drain(); publish(target_gui); publish(source_gui); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); };
        const auto cleanup = qScopeGuard([&] {
            importer.closeAdmission(); model.beginShutdown();
            if (!QTest::qWaitFor([&] { pump(); return model.shutdownComplete() && model.backendExecutor()->isDrained() && commands.quiescent(); }, 3000))
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
        if (boundary == 3) model.closeAdmission();
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
        ManualRoutedExecution commands, sources; std::deque<std::function<void()>> target_gui, source_gui;
        FeeReadServices fees; fees.post = [&](auto post) { target_gui.push_back(std::move(post)); };
        auto wallet = std::make_shared<MockWallet>();
        WalletQmlModel model{WalletQmlModel::SharedWallet{wallet, commands.scheduler}, nullptr, fees, {}, {}};
        ManualWalletDrain drain{commands, &model}; BitcoinUriModel importer; QObject view;
        importer.setServices({sources.scheduler, [&](auto post) { source_gui.push_back(std::move(post)); }});
        const auto publish = [](auto& queue) { while (!queue.empty()) { auto turn = std::move(queue.front()); queue.pop_front(); turn(); } };
        const auto pump = [&] { sources.drain(); commands.drain(); publish(target_gui); publish(source_gui); QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall); };
        const auto cleanup = qScopeGuard([&] {
            importer.closeAdmission(); model.beginShutdown();
            if (!QTest::qWaitFor([&] { pump(); return model.shutdownComplete() && model.backendExecutor()->isDrained() && commands.quiescent() && sources.quiescent() && target_gui.empty() && source_gui.empty(); }, 3000))
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
        model.closeAdmission(); commands.scheduler->closeOrdinaryAdmission(); operation->cancel();
        QVERIFY(QTest::qWaitFor([&] { pump(); return operation->state() == PaymentImportOperation::State::Closed; }, 5000));
        QCOMPARE(operation->outcome(), PaymentImportOperation::Outcome::AppliedOutcome);
        QVERIFY(operation->resultId() > 0); QCOMPARE(dispatches, 1); QVERIFY(queries >= 1); QVERIFY(!operation->pending());
        delete operation; // Release the retained Source snapshot before its manual Native transport.
    }
    void typedFacadePublishesBoundedSourceWithoutTargetOrGuiParsing() {
        ManualRoutedExecution commands; std::deque<std::function<void()>> gui;
        BitcoinUriModel model; QObject owner;
        model.setServices({commands.scheduler, [&](auto post) { gui.push_back(std::move(post)); }});
        auto* operation = model.requestImport({paymentimport::Input::Kind::Text, Uri()}, {}, &owner); QVERIFY(operation); QVERIFY(operation->pending());
        commands.drain(); QVERIFY(operation->address().isEmpty());
        while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
        QCOMPARE(operation->sourceKind(), PaymentImportOperation::SourceKind::PaymentSource);
        QCOMPARE(operation->outcome(), PaymentImportOperation::Outcome::NoOutcome);
        QCOMPARE(operation->address(), Address()); QCOMPARE(operation->publicLabel(), QStringLiteral("Public"));
        QCOMPARE(operation->message(), QStringLiteral("Message")); QCOMPARE(operation->amount(), qint64{12'345'678}); QVERIFY(!operation->canApply());
        operation->cancel(); commands.drain(); model.closeAdmission(); delete operation;
        do {
            commands.drain();
            while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
        } while (!commands.queuedIdle() || !commands.activeIdle());
        QVERIFY(commands.quiescent());
    }
};
BITCOINQML_REGISTER_QT_TEST(PaymentImportTests)
#include <test_paymentimport.moc>
