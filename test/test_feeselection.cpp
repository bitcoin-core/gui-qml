// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/backendexecutor.h>
#include <qml/models/feeestimatereader.h>
#include <qml/models/feeselectionmodel.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QSignalSpy>
#include <array>
#include <deque>
#include <limits>
#include <stdexcept>
namespace {
struct PolicyFixture {
    ManualRoutedExecution executor;
    std::deque<std::function<void()>> gui;
    FeeSelectionModel model;
    PolicyFixture() {
        model.setServices({executor.scheduler, [this](auto turn) { gui.push_back(std::move(turn)); }});
        model.observe(100, {{2, 1000}, {3, 1000}, {4, 1000}, {6, 1000}, {10, 1000}, {25, 1000}, {50, 1000}}); finish();
    }
    ~PolicyFixture() { model.close(); executor.drain(); while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); } }
    void publish() { while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); } }
    void finish() { do { executor.drain(); publish(); } while (!executor.queuedIdle() || !executor.activeIdle() || !gui.empty()); }
};
FeeReadInput Input(CAmount amount) { FeeReadInput value; value.recipients.push_back({"captured", amount}); value.dependencies = {quint64(amount), 2, 3, 4, 5}; return value; }
FeeReadResult Estimate(CAmount fee = 700) { FeeReadResult value; value.estimates.insert(6, SendFeePreview{fee, 1, 2500, {}, false, feeestimate::Provenance::Estimated, 6}); return value; }
struct ReaderFixture {
    ManualRoutedExecution executor;
    int notices{0}, drained{0};
    std::vector<CAmount> effects;
    bool throw_first{false};
    std::shared_ptr<feeestimate::Reader> reader;
    ReaderFixture() {
        reader = std::make_shared<feeestimate::Reader>(executor.scheduler, [this](const FeeReadInput& input) {
            effects.push_back(input.recipients.at(0).amount);
            if (throw_first && input.recipients.at(0).amount == 1) throw std::runtime_error{"original Core failure"};
            return Estimate();
        }, [this] { ++notices; }, [this] { ++drained; });
    }
    ~ReaderFixture() { reader->close(); executor.drain(); reader.reset(); }
    void domain() { while (executor.step(execution::Lane::Domain)) {} }
};
}
class FeeSelectionTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void manualRateSurvivesReselectionAndObservedDefaults() {
        PolicyFixture f; f.model.chooseCustom(); f.finish(); QCOMPARE(f.model.customRate()->text(), QStringLiteral("1.000"));
        f.model.customRate()->edit("17"); f.finish(); QCOMPARE(f.model.ratePerKvB(), std::optional<CAmount>{17000});
        const auto revision = f.model.revision(); f.model.chooseCustom(); f.model.chooseCustomTarget(6);
        QCOMPARE(f.model.revision(), revision); QVERIFY(f.executor.quiescent());
        f.model.observe(100, {{2, 2000}, {3, 2000}, {4, 2000}, {6, 2000}, {10, 2000}, {25, 2000}, {50, 2000}}); f.finish();
        QCOMPARE(f.model.customRate()->text(), QStringLiteral("17")); QCOMPARE(f.model.ratePerKvB(), std::optional<CAmount>{17000});
        f.model.choosePreset(2); f.finish(); QCOMPARE(f.model.mode(), FeeSelectionModel::Automatic);
        f.model.chooseCustom(); f.finish(); QCOMPARE(f.model.customRate()->text(), QStringLiteral("17"));
    }
    void customActivationNormalizesTheTargetOnDomain()
    {
        PolicyFixture f; f.model.captureTarget(42); f.finish(); QCOMPARE(f.model.targetBlocks(), 42);
        f.model.chooseCustom(); QVERIFY(f.model.policyPending()); QCOMPARE(f.model.targetBlocks(), 42);
        f.finish(); QCOMPARE(f.model.targetBlocks(), 50); QCOMPARE(f.model.targetIndex(), 6);
        QCOMPARE(f.model.customRate()->text(), QStringLiteral("1.000"));
    }
    void defaultPendingDoesNotDisableExplicitRateValidation()
    {
        PolicyFixture f; f.model.chooseCustom(); QVERIFY(f.model.customDefaultPending()); QVERIFY(f.model.policyPending());
        f.model.customRate()->edit("17"); QVERIFY(!f.model.customDefaultPending()); QVERIFY(f.model.policyPending());
        f.finish(); QCOMPARE(f.model.ratePerKvB(), std::optional<CAmount>{17000});
        f.model.observe(1000, {{2, 2000}, {6, 2000}}); QVERIFY(f.model.policyPending()); QVERIFY(!f.model.customDefaultPending());
        f.finish(); QCOMPARE(f.model.customRate()->text(), QStringLiteral("17"));
    }
    void lateTargetDefaultCannotOverwriteManualRate() {
        PolicyFixture f; f.model.chooseCustomTarget(3); f.executor.drain(); QVERIFY(!f.gui.empty());
        f.model.customRate()->edit("17"); f.publish(); QCOMPARE(f.model.customRate()->text(), QStringLiteral("17"));
        QVERIFY(f.model.policyPending()); f.finish(); QCOMPARE(f.model.ratePerKvB(), std::optional<CAmount>{17000});
    }
    void exactRateTransport_data() {
        QTest::addColumn<QString>("text"); QTest::addColumn<qint64>("rate");
        QTest::newRow("minimum") << QStringLiteral("0.1") << qint64{100};
        QTest::newRow("padded") << QStringLiteral(" 0.100 ") << qint64{100};
        QTest::newRow("fraction") << QStringLiteral("17.125") << qint64{17125};
        QTest::newRow("empty") << QStringLiteral("") << qint64{-1};
        QTest::newRow("zero") << QStringLiteral("0") << qint64{-1};
        QTest::newRow("below-minimum") << QStringLiteral("0.099") << qint64{-1};
        QTest::newRow("negative") << QStringLiteral("-1") << qint64{-1};
        QTest::newRow("original265-wrap-window") << QStringLiteral("18446744073709552") << qint64{-1};
        QTest::newRow("maximum-exact") << QStringLiteral("9223372036854775.807") << std::numeric_limits<qint64>::max();
        QTest::newRow("scaled-sum-overflow") << QStringLiteral("9223372036854775.808") << qint64{-1};
        QTest::newRow("scaled-whole-overflow") << QStringLiteral("9223372036854776") << qint64{-1};
        QTest::newRow("whole-overflow") << QStringLiteral("9223372036854775808") << qint64{-1};
        QTest::newRow("excess-precision") << QStringLiteral("0.1001") << qint64{-1};
        QTest::newRow("exponent") << QStringLiteral("1e3") << qint64{-1};
        QTest::newRow("trailing-junk") << QStringLiteral("17x") << qint64{-1};
        QTest::newRow("original-over-budget") << (QStringLiteral("1") + QString(40, QLatin1Char{' '})) << qint64{-1};
    }
    void exactRateTransport() {
        QFETCH(QString, text); QFETCH(qint64, rate); PolicyFixture f;
        f.model.chooseCustom(); f.finish(); f.model.customRate()->edit(text);
        QVERIFY(f.model.policyPending()); QVERIFY(!f.model.ratePerKvB()); f.finish();
        QCOMPARE(f.model.customRate()->text(), text.left(33));
        QCOMPARE(f.model.customRate()->valid(), rate >= 0);
        if (rate >= 0) QCOMPARE(f.model.ratePerKvB(), std::optional<CAmount>{rate});
        else { QVERIFY(!f.model.ratePerKvB()); QVERIFY(!f.model.customRate()->error().isEmpty()); }
    }
    void rejectedValidationRecoversOnlyWithExplicitInterest()
    {
        PolicyFixture f; f.model.chooseCustom(); f.finish(); f.executor.rejectLane(execution::Lane::Domain);
        f.model.customRate()->edit("17"); f.finish(); QVERIFY(!f.model.customRate()->valid());
        QVERIFY(!f.model.customRate()->error().isEmpty()); QVERIFY(!f.model.policyPending());
        const auto intent = f.model.revision(); f.model.chooseCustom(); f.model.customRate()->edit("17");
        QVERIFY(f.executor.quiescent()); QVERIFY(!f.model.customRate()->valid());
        f.model.refresh(); QVERIFY(f.model.policyPending()); f.finish();
        QCOMPARE(f.model.revision(), intent); QCOMPARE(f.model.ratePerKvB(), std::optional<CAmount>{17000});
    }
    void failedEstimateRetainsVisibleStaleDataWithoutAuthority() {
        PolicyFixture f; f.model.publishEstimate(feeestimate::Phase::Current, Estimate(), true, {1, 2, 3, 4, 5});
        QVERIFY(f.model.estimateFresh()); QCOMPARE(f.model.estimatedFeeSatoshi(), qint64{700});
        f.model.invalidateEstimate(FeeSelectionModel::Scheduled);
        QVERIFY(f.model.hasPreviousEstimate()); QVERIFY(!f.model.selectedPreview());
        FeeReadResult failed; failed.error = feeestimate::Error::Backend; failed.message = "original backend cause";
        f.model.publishEstimate(feeestimate::Phase::Failed, std::move(failed), false);
        QVERIFY(!f.model.estimateFresh()); QCOMPARE(f.model.estimatedFeeSatoshi(), qint64{700});
        QCOMPARE(f.model.errorText(), QStringLiteral("original backend cause")); QVERIFY(!f.model.selectedPreview());
        QCOMPARE(f.model.estimateDraftRevision(), QStringLiteral("1"));
    }
    void partialFailureRetainsSelectedPreviousData()
    {
        PolicyFixture f; f.model.choosePreset(2); f.finish();
        auto first = Estimate(); first.estimates.insert(2, SendFeePreview{700, 1, 2500, {}, false, feeestimate::Provenance::Estimated, 2});
        f.model.publishEstimate(feeestimate::Phase::Current, first, true, {1, 2, 3, 4, 5});
        FeeReadResult partial; partial.estimates.insert(6, SendFeePreview{600, 1, 2000, {}, false}); partial.errors.insert(2, "Original priority fee limit.");
        f.model.publishEstimate(feeestimate::Phase::Current, partial, true, {2, 2, 3, 4, 5});
        QCOMPARE(f.model.estimateState(), FeeSelectionModel::Failed); QCOMPARE(f.model.error(), FeeSelectionModel::Unavailable);
        QVERIFY(!f.model.selectedPreview()); QVERIFY(f.model.hasPreviousEstimate()); QCOMPARE(f.model.estimatedFeeSatoshi(), qint64{700});
        QCOMPARE(f.model.estimateDraftRevision(), QStringLiteral("1")); QCOMPARE(f.model.errorText(), QStringLiteral("Original priority fee limit."));
    }
    void readerPartialFailureCannotReplaceTheLastSelectedSuccess()
    {
        ManualRoutedExecution executor; int reads{0};
        auto reader = std::make_shared<feeestimate::Reader>(executor.scheduler, [&](const FeeReadInput&) {
            FeeReadResult value;
            if (++reads == 1) value.estimates.insert(2, SendFeePreview{700, 1, 2500, {}, false});
            else { value.estimates.insert(6, SendFeePreview{600, 1, 2000, {}, false}); value.errors.insert(2, "Original priority fee limit."); }
            return value;
        }, [] {});
        auto first = Input(1); first.coin_control.m_confirm_target = 2;
        QVERIFY(reader->request(1, first)); executor.drain(); const auto good = reader->take(); QVERIFY(good && good->fresh);
        auto second = Input(2); second.coin_control.m_confirm_target = 2;
        QVERIFY(reader->request(2, second)); executor.drain(); const auto failed = reader->take(); QVERIFY(failed);
        QCOMPARE(failed->phase, feeestimate::Phase::Failed); QCOMPARE(failed->result.error, feeestimate::Error::Unavailable);
        QCOMPARE(failed->result.message, QStringLiteral("Original priority fee limit.")); QVERIFY(!failed->fresh);
        QVERIFY(reader->request(3, second)); while (executor.step(execution::Lane::Domain)) {}
        const auto pending = reader->take(); QVERIFY(pending); QVERIFY(!pending->fresh); QCOMPARE(pending->source_revision, quint64{1});
        QCOMPARE(pending->result.estimates.value(2).fee, CAmount{700});
        reader->close(); executor.drain(); QVERIFY(executor.quiescent());
    }
    void readerCoalescesBeforeDomainDispatch() {
        ReaderFixture f; QVERIFY(f.reader->request(1, Input(1))); QVERIFY(f.reader->request(2, Input(2))); QVERIFY(f.reader->request(3, Input(3)));
        QVERIFY(f.effects.empty()); f.executor.drain(); QCOMPARE(f.effects, std::vector<CAmount>{3});
        const auto output = f.reader->take(); QVERIFY(output); QCOMPARE(output->revision, quint64{3});
        QCOMPARE(output->dependencies, Input(3).dependencies); QCOMPARE(output->phase, feeestimate::Phase::Current); QVERIFY(output->fresh);
        QCOMPARE(output->result.estimates.value(6).fee, CAmount{700}); QVERIFY(f.executor.quiescent());
    }
    void staleOutcomePreservesTheLatestInput_data() {
        QTest::addColumn<bool>("failure"); QTest::addColumn<bool>("notice_before_outcome");
        QTest::newRow("success-new-wake-first") << false << true;
        QTest::newRow("error-new-wake-first") << true << true;
        QTest::newRow("success-old-outcome-before-new-wake") << false << false;
        QTest::newRow("error-old-outcome-before-new-wake") << true << false;
    }
    void staleOutcomePreservesTheLatestInput() {
        QFETCH(bool, failure); QFETCH(bool, notice_before_outcome); ReaderFixture f; f.throw_first = failure;
        QVERIFY(f.reader->request(1, Input(1))); f.domain(); const auto old = f.executor.dispatchNext(execution::Lane::Core); QVERIFY(old);
        f.reader->take();
        if (!notice_before_outcome) QVERIFY(f.executor.finishWork(*old));
        QVERIFY(f.reader->request(2, Input(2))); QVERIFY(f.reader->request(3, Input(3)));
        if (notice_before_outcome) { f.domain(); QVERIFY(f.executor.finishWork(*old)); }
        f.domain(); const auto interim = f.reader->take();
        QVERIFY(!interim || (interim->revision == 3 && !interim->fresh));
        f.executor.drain(); const auto output = f.reader->take(); QVERIFY(output); QVERIFY(output->fresh);
        QCOMPARE(output->revision, quint64{3}); QCOMPARE(output->result.error, feeestimate::Error::None);
        QCOMPARE(f.effects, (std::vector<CAmount>{1, 3})); QVERIFY(f.executor.quiescent());
    }
    void rejectedAdmissionHasNoEffectAndCanRecover_data() {
        QTest::addColumn<bool>("domain"); QTest::newRow("Domain") << true; QTest::newRow("Core") << false;
    }
    void rejectedAdmissionHasNoEffectAndCanRecover() {
        QFETCH(bool, domain); ReaderFixture f; f.executor.rejectLane(domain ? execution::Lane::Domain : execution::Lane::Core);
        const auto admitted = f.reader->request(1, Input(1)); QCOMPARE(bool(admitted), !domain); f.executor.drain();
        QVERIFY(f.effects.empty()); const auto failed = f.reader->take(); QVERIFY(failed);
        QCOMPARE(failed->phase, feeestimate::Phase::Failed); QCOMPARE(failed->result.error, feeestimate::Error::Rejected);
        QVERIFY(f.executor.quiescent()); QVERIFY(f.reader->request(2, Input(2))); f.executor.drain();
        const auto current = f.reader->take(); QVERIFY(current && current->fresh); QCOMPARE(f.effects, std::vector<CAmount>{2});
    }
    void invalidationRetainsScheduledInterestWithoutOldAuthority() {
        ReaderFixture f; QVERIFY(f.reader->request(1, Input(1))); f.domain();
        f.reader->invalidate(2, true); f.domain();
        auto pending = f.reader->take(); QVERIFY(pending); QVERIFY(!pending->fresh);
        QCOMPARE(pending->phase, feeestimate::Phase::ReadingWithPendingWork);
        f.executor.drain(); auto scheduled = f.reader->take(); QVERIFY(scheduled);
        QCOMPARE(scheduled->phase, feeestimate::Phase::Scheduled); QVERIFY(!scheduled->fresh);
        QCOMPARE(f.effects, std::vector<CAmount>{1});
        QVERIFY(f.reader->request(3, Input(3))); f.executor.drain();
        auto current = f.reader->take(); QVERIFY(current && current->fresh); QCOMPARE(current->revision, quint64{3});
    }
    void invalidWorkerValuesNeverBecomeCurrent_data() {
        QTest::addColumn<int>("kind");
        QTest::newRow("negative-fee") << 0; QTest::newRow("out-of-money-range") << 1;
        QTest::newRow("negative-rate") << 2; QTest::newRow("negative-input-count") << 3;
    }
    void invalidWorkerValuesNeverBecomeCurrent() {
        QFETCH(int, kind); ManualRoutedExecution executor;
        auto reader = std::make_shared<feeestimate::Reader>(executor.scheduler, [kind](const FeeReadInput&) {
            auto result = Estimate(); auto& preview = result.estimates[6];
            if (kind == 0) preview.fee = -1;
            if (kind == 1) preview.fee = MAX_MONEY + 1;
            if (kind == 2) preview.rate_per_kvb = -1;
            if (kind == 3) preview.inputs = -1;
            return result;
        }, [] {});
        QVERIFY(reader->request(1, Input(1))); executor.drain(); const auto result = reader->take(); QVERIFY(result);
        QCOMPARE(result->phase, feeestimate::Phase::Failed); QCOMPARE(result->result.error, feeestimate::Error::Invalid); QVERIFY(!result->fresh);
        reader->close(); executor.drain(); QVERIFY(executor.quiescent());
    }
    void eachDependencyInvalidatesAnOldSuccess_data() {
        QTest::addColumn<int>("dependency");
        for (int dependency{0}; dependency < 6; ++dependency)
            QTest::newRow((std::array<const char*, 6>{"draft", "intent", "coins", "policy", "backend", "requirements"})[dependency]) << dependency;
    }
    void eachDependencyInvalidatesAnOldSuccess() {
        QFETCH(int, dependency); ReaderFixture f; auto original = Input(1), newer = original;
        auto& d = newer.dependencies;
        switch (dependency) { case 0: ++d.draft; break; case 1: ++d.intent; break; case 2: ++d.coins; break;
            case 3: ++d.policy; break; case 4: ++d.backend; break; case 5: ++d.requirements; break; }
        QVERIFY(f.reader->request(1, original)); f.domain(); const auto old = f.executor.dispatchNext(execution::Lane::Core); QVERIFY(old);
        f.reader->take(); QVERIFY(f.reader->request(2, newer)); f.domain(); QVERIFY(f.executor.finishWork(*old)); f.domain();
        const auto previous = f.reader->take(); QVERIFY(previous); QVERIFY(!previous->fresh);
        QCOMPARE(previous->dependencies, newer.dependencies); QCOMPARE(previous->observed_dependencies, original.dependencies);
        QCOMPARE(previous->source_revision, quint64{1});
        f.executor.drain(); const auto current = f.reader->take(); QVERIFY(current && current->fresh);
        QCOMPARE(current->dependencies, newer.dependencies); QCOMPARE(current->revision, quint64{2});
        QCOMPARE(f.effects, (std::vector<CAmount>{1, 1})); QVERIFY(f.executor.quiescent());
    }
    void burstDuringReadRetainsOneSuccessor() {
        ReaderFixture f; QVERIFY(f.reader->request(1, Input(1))); f.domain();
        const auto first = f.executor.dispatchNext(execution::Lane::Core); QVERIFY(first);
        for (quint64 revision{2}; revision <= 256; ++revision) QVERIFY(f.reader->request(revision, Input(revision)));
        QCOMPARE(f.executor.queuedCount(), size_t{1}); f.domain();
        QVERIFY(f.executor.finishWork(*first)); f.executor.drain();
        const auto current = f.reader->take(); QVERIFY(current && current->fresh); QCOMPARE(current->revision, quint64{256});
        QCOMPARE(f.effects, (std::vector<CAmount>{1, 256})); QCOMPARE(f.executor.coreSubmissions(), size_t{2}); QVERIFY(f.executor.quiescent());
    }
    void equalRatesDoNotInventEstimatorProvenance_data() {
        QTest::addColumn<int>("source");
        QTest::newRow("actual-fallback") << int(feeestimate::Provenance::Fallback);
        QTest::newRow("actual-estimator-same-rate") << int(feeestimate::Provenance::Estimated);
        QTest::newRow("static-same-rate") << int(feeestimate::Provenance::Static);
    }
    void equalRatesDoNotInventEstimatorProvenance() {
        QFETCH(int, source); PolicyFixture f; auto result = Estimate();
        auto& selected = result.estimates[6]; selected.provenance = static_cast<feeestimate::Provenance>(source); selected.observed_target = source == int(feeestimate::Provenance::Estimated) ? 6 : 0;
        result.estimates.insert(2, selected); result.estimates.insert(10, selected);
        f.model.publishEstimate(feeestimate::Phase::Current, result, true);
        QCOMPARE(f.model.provenance(), static_cast<FeeSelectionModel::Provenance>(source));
        QCOMPARE(f.model.hasEstimatorTarget(), source == int(feeestimate::Provenance::Estimated));
        QCOMPARE(f.model.estimatedRate(), QStringLiteral("2.500"));
    }
    void stoppedPolicyCannotPublishHeldValidation() {
        PolicyFixture f; f.model.chooseCustomTarget(3); f.executor.drain();
        QSignalSpy changes{&f.model, &FeeSelectionModel::policyChanged}; f.model.close();
        f.publish(); QCOMPARE(changes.size(), 0); QVERIFY(!f.model.ratePerKvB());
        QCOMPARE(f.model.estimateState(), FeeSelectionModel::Stopping);
        QVERIFY(f.executor.quiescent()); f.model.finishStop(); QCOMPARE(f.model.estimateState(), FeeSelectionModel::Stopped);
    }
    void closeRetainsActualDrainAndRejectsLatePublication_data() {
        QTest::addColumn<int>("point"); QTest::newRow("Domain-queued") << 0; QTest::newRow("Core-queued") << 1; QTest::newRow("Core-finished-outcome-held") << 2;
    }
    void closeRetainsActualDrainAndRejectsLatePublication() {
        QFETCH(int, point); ReaderFixture f; QVERIFY(f.reader->request(1, Input(1)));
        if (point > 0) f.domain();
        if (point == 2) QVERIFY(f.executor.step(execution::Lane::Core));
        const auto notices = f.notices; f.reader->close(); QCOMPARE(f.drained, 0);
        f.executor.drain(); QCOMPARE(f.drained, 1); QCOMPARE(f.notices, notices); QVERIFY(!f.reader->take());
        QCOMPARE(f.effects.size(), size_t(point > 0));
        QCOMPARE(f.reader->request(2, Input(2)).rejection, execution::Rejection::Closed); QVERIFY(f.executor.quiescent());
    }
    void realReaderKeepsNativeWorkOffGui() {
        BackendExecutor executor; QObject receiver; QThread* native_thread{nullptr}; std::optional<feeestimate::Publication> result;
        std::shared_ptr<feeestimate::Reader> reader;
        reader = std::make_shared<feeestimate::Reader>(executor.scheduler(), [&](const FeeReadInput&) { native_thread = QThread::currentThread(); return Estimate(); },
            [&] { QMetaObject::invokeMethod(&receiver, [&] { if (auto value = reader->take()) result = std::move(value); }, Qt::QueuedConnection); });
        QVERIFY(reader->request(1, Input(1))); QTRY_VERIFY(result && result->fresh);
        QVERIFY(native_thread); QVERIFY(native_thread != QThread::currentThread());
        reader->close(); executor.shutdown(); QTRY_VERIFY(executor.isDrained()); reader.reset();
    }
};
BITCOINQML_REGISTER_QT_TEST(FeeSelectionTests)
#include <test_feeselection.moc>
