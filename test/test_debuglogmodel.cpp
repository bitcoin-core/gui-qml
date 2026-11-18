// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QPersistentModelIndex>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <qml/models/debuglogmodel.h>
#include <qml/models/rpcconsolemodel.h>
#include <test/mocks/stubnode.h>
#include <univalue.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/admissionhookscheduler.h>
#include <stdexcept>
#include <string>
#include <array>
#include <QScopeGuard>

#include <util/fs.h>

namespace {
QByteArray Record(const QByteArray& message)
{
    return QByteArray{"2026-06-19T10:00:00Z "} + message + '\n';
}

bool WriteBytes(const QString& path, const QByteArray& bytes,
                QIODevice::OpenMode mode = QIODevice::WriteOnly)
{
    QFile file(path);
    if (!file.open(mode)) return false;
    return file.write(bytes) == bytes.size();
}

QByteArray NumberedRecords(int first, int count)
{
    QByteArray bytes;
    for (int i = first; i < first + count; ++i) {
        bytes += Record("line " + QByteArray::number(i));
    }
    return bytes;
}

QByteArray OversizedLine(char fill)
{
    return QByteArray(DebugLogModel::kMaxLogLineBytes + 1, fill);
}

QString ContentAt(const DebugLogModel& model, int row)
{
    return model.data(model.index(row, 0), DebugLogModel::MessageRole).toString();
}
// Exercise this producer's exceptional eager admission without replacing the
// original TaskQueue ledger, final cleanup reservation or accepted effects.
class ThrowingDebugCleanupScheduler final : public execution::Scheduler
{
    const std::shared_ptr<execution::Scheduler> m_inner;
public:
    bool armed{false}, after_admission{false};
    execution::TaskId actual_admitted{};
    explicit ThrowingDebugCleanupScheduler(std::shared_ptr<execution::Scheduler> inner) : m_inner(std::move(inner)) {}
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec = {}) override
    {
        if (!armed || spec.lane != execution::Lane::Cleanup) return m_inner->submit(std::move(task), spec);
        armed = false;
        if (!after_admission) throw std::runtime_error{"Controlled eager Cleanup admission exception"};
        const auto admission = m_inner->submit(std::move(task), spec);
        if (!admission) return admission;
        actual_admitted = admission.id;
        throw std::runtime_error{"Controlled exception after actual eager Cleanup admission"};
    }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override
    { return m_inner->reserveCleanup(std::move(task), spec); }
    bool activateCleanup(execution::TaskId id) override { return m_inner->activateCleanup(id); }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override { return m_inner->borrow(key); }
    execution::ResourceKey allocateKey() override { return m_inner->allocateKey(); }
    size_t pending() const override { return m_inner->pending(); }
    void closeOrdinaryAdmission() override { m_inner->closeOrdinaryAdmission(); }
    bool cancelQueued(execution::TaskId id) override { return m_inner->cancelQueued(id); }
};

} // namespace

class DebugLogModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void cleanup() {
        bool done{false}; BackendExecutor::shutdownAll(this, [&] { done = true; });
        QTRY_VERIFY(done);
    }
    void manualReadAndProjectionUseSeparateDeliveryTurns();
    void observerLossDoesNotCancelAcceptedRead();
    void constructorReservationKeepsOriginalParentLifetime();
    void constructorCleanupRefusalStartsNoRead();
    void injectedRetirementUsesOriginalScheduler_data();
    void injectedRetirementUsesOriginalScheduler();
    void eagerCleanupCoalescesThroughActualPacketRetirement();
    void throwingEagerAdmissionRetainsOriginalOwner_data();
    void throwingEagerAdmissionRetainsOriginalOwner();
    void workAdmissionKeepsOriginalObserver_data();
    void workAdmissionKeepsOriginalObserver();
    void factPublicationKeepsOriginalObserver_data();
    void factPublicationKeepsOriginalObserver();
    void structuralCloseKeepsOriginalRowTransactions_data();
    void structuralCloseKeepsOriginalRowTransactions();
    void setterContinuationKeepsOriginalInterest_data();
    void setterContinuationKeepsOriginalInterest();
    void nativeHandoffKeepsActualOutcome_data();
    void nativeHandoffKeepsActualOutcome();
    void postEnqueueCannotResumeRevokedObserver_data();
    void postEnqueueCannotResumeRevokedObserver();
    void inactiveModel_ignoresRefreshUntilActivated();
    void parsedRoles_extractStructuredLogLines();
    void initialLoad_usesBoundedBatchesAndDetectsHasMore();
    void initialLoad_handlesBlankLinesAtBlockBoundaries();
    void initialLoad_discardsOversizedPartialAndResynchronizes();
    void initialLoad_skipsOversizedCompleteLine();
    void largeTailKeepsCompleteRecordFraming_data();
    void largeTailKeepsCompleteRecordFraming();
    void largeTailStopsAtRetainedByteBudget();
    void deltaAfterEmptyLoad_preservesHasMoreSentinel();
    void liveRefresh_appendsWithoutResetAndPrunesHead();
    void liveRefresh_canFullyDisplaceCacheWithoutReset();
    void liveRefresh_handlesDuplicateRecordsAndPartialWrites();
    void chunkedDuplicateRecordsKeepOriginalOverlapIdentity();
    void liveRefresh_discardsOversizedPartialUntilNewline();
    void liveRefresh_skipsOversizedCompleteLine();
    void loadMore_insertsOlderRowsAtTop();
    void widerTailRequest_survivesRacesAndDeactivation();
    void filter_updatesIncrementallyAndWhileInactive();
    void rotation_fallsBackToFullSnapshot();
    void replacementWithMatchingSuffixUsesFileIdentity_data();
    void replacementWithMatchingSuffixUsesFileIdentity();
    void staleActivationCannotPublishOrLoseRetainedRows();
    void hiddenReadDoesNotCancelIndependentCommand();
    void hugeUnfinishedSuffixHasBoundedReadAndRecovery();
    void loadLimit_changesKeepRetainedCacheBounded();
    void openErrorEmptyByDefault();
    void openErrorPersistsAcrossSuccessfulRead();
    void clearOpenErrorClearsOnlyWhenSet();
    void openMissingFileIsLeftToTheReader();
    void logAvailableTracksReadFailures();
    void fileLoggingOff_readsNothingAndSaysWhy();
};

void DebugLogModelTests::manualReadAndProjectionUseSeparateDeliveryTurns()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log");
    QVERIFY(WriteBytes(path, Record("keep first") + Record("drop second")));
    ManualExecutionEnvironment env;
    DebugLogModel model{fs::PathFromString(path.toStdString()), true,
        {env.scheduler, [&](auto callback) { env.post(std::move(callback)); }}};
    const auto settle = qScopeGuard([&] {
        model.stop();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The original debug-log graph did not physically drain", __FILE__, __LINE__);
    });
    model.setActive(true);
    QVERIFY(model.busy()); QCOMPARE(model.rowCount(), 0);
    QVERIFY(env.step()); // Actual file read and reduction.
    QCOMPARE(model.rowCount(), 0); QVERIFY(!model.logAvailable());
    QVERIFY(env.step()); // Domain owns facts; still no GUI publication.
    QCOMPARE(model.rowCount(), 0);
    env.publishAll();
    QVERIFY(!model.busy()); QCOMPARE(model.rowCount(), 2); QVERIFY(model.logAvailable());
    model.setFilter("keep");
    QVERIFY(model.busy()); QCOMPARE(model.rowCount(), 2);
    env.drain(); QCOMPARE(model.rowCount(), 2);
    env.publishAll();
    QCOMPARE(model.rowCount(), 1); QCOMPARE(ContentAt(model, 0), QStringLiteral("keep first"));
    QVERIFY(!model.busy());
}

void DebugLogModelTests::observerLossDoesNotCancelAcceptedRead()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log"); QVERIFY(WriteBytes(path, NumberedRecords(0, 40)));
    ManualExecutionEnvironment env;
    const auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
    const auto reservation = scheduler->armCleanup(1, [] {});
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    const auto cleanup = qScopeGuard([&] {
        scheduler->disarm(); model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The original debug-log graph did not physically drain", __FILE__, __LINE__);
    });
    const execution::TaskId reserved{reservation->originalId.load()};
    QVERIFY(reserved.value != 0); QCOMPARE(reservation->calls.load(), size_t{1});
    const auto reserved_phase = env.scheduler->phase(reserved); QVERIFY(reserved_phase);
    QCOMPARE(*reserved_phase, execution::Phase::Reserved);
    QCOMPARE(env.scheduler->pending(), size_t{2}); // Reserved task + its one live ResourceLease borrower.
    QVERIFY(model->findChildren<BackendExecutor*>().isEmpty());
    const auto read = scheduler->armSubmit(execution::Lane::Core, 1, [] {});
    model->setActive(true);
    const execution::TaskId original_read{read->originalId.load()};
    QVERIFY(original_read.value != 0); QVERIFY(original_read != reserved);
    const auto read_phase = env.scheduler->phase(original_read); QVERIFY(read_phase);
    QCOMPARE(*read_phase, execution::Phase::Queued);
    QCOMPARE(env.queuedTurnCount(), size_t{1});
    QCOMPARE(env.scheduler->pending(), size_t{3}); // Reserved task + borrower + original queued read.
    model.reset();
    env.drain();
    QVERIFY(!env.scheduler->phase(original_read));
    const auto held_phase = env.scheduler->phase(reserved); QVERIFY(held_phase);
    QCOMPARE(*held_phase, execution::Phase::Reserved); // Held original GUI retirement post still owns the borrow.
    const auto eager = scheduler->armSubmit(execution::Lane::Cleanup, 1, [] {});
    env.publishAll();
    const execution::TaskId original_eager{eager->originalId.load()};
    QVERIFY(original_eager.value != 0); QCOMPARE(eager->calls.load(), size_t{1});
    const auto eager_phase = env.scheduler->phase(original_eager); QVERIFY(eager_phase);
    QCOMPARE(*eager_phase, execution::Phase::Queued);
    qInfo() << "Original DebugLog reservation/read/eager TaskIds" << reserved.value << original_read.value << original_eager.value;
    env.drain(); env.publishAll();
    QVERIFY(env.quiescent());
}

void DebugLogModelTests::constructorReservationKeepsOriginalParentLifetime()
{
    ManualExecutionEnvironment env;
    const auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
    auto parent = std::make_unique<QObject>();
    std::unique_ptr<DebugLogModel> model;
    const auto cleanup = qScopeGuard([&] {
        scheduler->disarm(); model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The original debug-log constructor resource did not drain", __FILE__, __LINE__);
    });
    int children_at_admission{-1};
    const auto trigger = scheduler->armCleanup(1, [&] {
        children_at_admission = parent->children().size(); parent.reset();
    });
    bool parent_lost{false};
    try {
        model = std::make_unique<DebugLogModel>(fs::path{}, true,
            execution::SnapshotServices{scheduler, [&](auto callback) { env.post(std::move(callback)); }}, parent.get());
    } catch (const std::runtime_error&) { parent_lost = true; }
    QVERIFY(parent_lost); QVERIFY(!model); QVERIFY(!parent);
    QCOMPARE(children_at_admission, 0);
    QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
    QCOMPARE(env.scheduler->pending(), size_t{1}); // Real original cleanup, no read.
    env.drain(); env.publishAll();
    QVERIFY(env.quiescent());
}

void DebugLogModelTests::constructorCleanupRefusalStartsNoRead()
{
    ManualExecutionEnvironment env;
    env.scheduler->closeAdmission();
    auto model = std::make_unique<DebugLogModel>(fs::path{}, true,
        execution::SnapshotServices{env.scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    const auto cleanup = qScopeGuard([&] {
        model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The refused debug-log setup did not drain", __FILE__, __LINE__);
    });
    QVERIFY(model->findChildren<BackendExecutor*>().isEmpty());
    QVERIFY(!model->openError().isEmpty());
    model->setActive(true); model->refresh(true);
    QCOMPARE(model->rowCount(), 0); QVERIFY(!model->busy());
    QVERIFY(env.quiescent());
    QSignalSpy drained{model.get(), &DebugLogModel::drained};
    model->stop(); QCOMPARE(drained.count(), 1);
}

void DebugLogModelTests::injectedRetirementUsesOriginalScheduler_data()
{
    QTest::addColumn<bool>("reject_eager_cleanup");
    QTest::addColumn<bool>("destroy_observer");
    QTest::newRow("closed-admission") << false << false;
    QTest::newRow("eager-rejected") << true << false;
    QTest::newRow("observer-loss") << false << true;
}

void DebugLogModelTests::injectedRetirementUsesOriginalScheduler()
{
    QFETCH(bool, reject_eager_cleanup); QFETCH(bool, destroy_observer);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log");
    QVERIFY(WriteBytes(path, Record("keep first") + Record("drop second")));
    ManualExecutionEnvironment env;
    const auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
    const auto reservation = scheduler->armCleanup(1, [] {});
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    const auto cleanup = qScopeGuard([&] {
        scheduler->disarm(); model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The original injected debug-log graph did not drain", __FILE__, __LINE__);
    });
    QVERIFY(model->findChildren<BackendExecutor*>().isEmpty());
    QVERIFY(reservation->originalId.load() != 0); QCOMPARE(reservation->calls.load(), size_t{1});
    scheduler->disarm();
    model->setActive(true);
    for (int turn{0}; turn < 128 && !env.transportIdle(); ++turn) { env.drain(); env.publishAll(); }
    QCOMPARE(model->rowCount(), 2); QVERIFY(env.transportIdle());
    if (reject_eager_cleanup) {
        const auto before = env.trace();
        model->setFilter("keep"); env.drain();
        env.rejectNext(); env.publishAll(); env.drain(); env.publishAll();
        QCOMPARE(model->rowCount(), 1); QCOMPARE(ContentAt(*model, 0), QStringLiteral("keep first"));
        const auto rejected_trace = env.trace().substr(before.size());
        QVERIFY(rejected_trace.find("reject:") != std::string::npos);
        qInfo() << "Original rejected DebugLog Cleanup chronology" << QString::fromStdString(rejected_trace);
        QVERIFY(!model->busy()); QVERIFY(!model->openError().isEmpty());
        model->refresh(); // Explicit interest retries retained cleanup before final close.
        for (int turn{0}; turn < 16 && !env.transportIdle(); ++turn) { env.drain(); env.publishAll(); }
        QVERIFY(env.transportIdle()); QVERIFY(model->openError().isEmpty());
        QCOMPARE(model->rowCount(), 1); QCOMPARE(ContentAt(*model, 0), QStringLiteral("keep first"));
    }
    const execution::TaskId reserved{reservation->originalId.load()};
    const auto reserved_phase = env.scheduler->phase(reserved); QVERIFY(reserved_phase);
    QCOMPARE(*reserved_phase, execution::Phase::Reserved);
    QCOMPARE(env.queuedTurnCount(), size_t{0});
    QCOMPARE(env.scheduler->pending(), size_t{2}); // Original reserved task + live ResourceLease borrower.
    qInfo() << "Original live DebugLog reservation TaskId" << reserved.value << "with one accounted borrower";
    QSignalSpy drained{model.get(), &DebugLogModel::drained};
    env.scheduler->closeAdmission();
    if (destroy_observer) model.reset(); else model->stop();
    QCOMPARE(drained.count(), 0); // No unrelated worker can release the data.
    QCOMPARE(env.scheduler->pending(), size_t{1});
    auto final_cleanup = env.takeTurn(execution::TaskId{reservation->originalId.load()});
    QVERIFY(final_cleanup); QCOMPARE(final_cleanup->phase, execution::Phase::Queued);
    final_cleanup->turn(); final_cleanup.reset();
    // Native cleanup happened, but the original GUI receipt is still held.
    QCOMPARE(drained.count(), 0);
    env.drain(); env.publishAll();
    QCOMPARE(drained.count(), destroy_observer ? 0 : 1);
    QVERIFY(env.quiescent());
    if (model) { model->stop(); QCOMPARE(drained.count(), 1); }
}


void DebugLogModelTests::eagerCleanupCoalescesThroughActualPacketRetirement()
{
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log"); QVERIFY(WriteBytes(path, NumberedRecords(0, 2)));
    ManualRoutedExecution env;
    const auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
    const auto reservation = scheduler->armCleanup(1, [] {});
    std::deque<std::function<void()>> publications;
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{scheduler, [&](auto callback) { publications.push_back(std::move(callback)); }});
    QSignalSpy errors{model.get(), &DebugLogModel::openErrorChanged};
    const auto publish = [&] {
        while (!publications.empty()) { auto callback = std::move(publications.front()); publications.pop_front(); callback(); }
    };
    const auto cleanup = qScopeGuard([&] {
        scheduler->disarm(); model.reset();
        for (int turn{0}; turn < 128 && (!env.quiescent() || !publications.empty()); ++turn) { env.drain(); publish(); }
        if (!env.quiescent() || !publications.empty()) QTest::qFail("The original coalesced Cleanup graph did not drain", __FILE__, __LINE__);
    });
    const auto read = scheduler->armSubmit(execution::Lane::Core, 1, [] {});
    model->setActive(true);
    const execution::TaskId original_read{read->originalId.load()}; QVERIFY(original_read.value != 0);
    const auto first = scheduler->armSubmit(execution::Lane::Cleanup, 1, [] {});
    publish(); // Original State retirement posts its one accepted Cleanup.
    const execution::TaskId original_cleanup{first->originalId.load()}; QVERIFY(original_cleanup.value != 0);
    QCOMPARE(env.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Cleanup; }), size_t{1});
    QVERIFY(env.step(execution::Lane::Core)); QVERIFY(env.step(execution::Lane::Domain)); publish();
    QCOMPARE(model->rowCount(), 2); QVERIFY(model->openError().isEmpty()); QCOMPARE(errors.count(), 0);
    // Release Native values, but retain the actual Cleanup OutcomeReady packet
    // and its only task slot. A later GUI State owner must coalesce with it.
    QVERIFY(env.step(execution::Lane::Cleanup));
    const auto ready = env.scheduler->phase(original_cleanup); QVERIFY(ready);
    QCOMPARE(*ready, execution::Phase::OutcomeReady); QVERIFY(publications.empty());
    const auto successor = scheduler->armSubmit(execution::Lane::Core, 1, [] {});
    model->setFilter("line");
    const execution::TaskId successor_read{successor->originalId.load()}; QVERIFY(successor_read.value != 0);
    QVERIFY(publications.empty()); QVERIFY(model->openError().isEmpty()); QCOMPARE(errors.count(), 0);
    QCOMPARE(env.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Cleanup; }), size_t{0});
    const auto held = env.dispatchNext(execution::Lane::Domain); QVERIFY(held); QCOMPARE(held->value, original_cleanup.value);
    QVERIFY(env.finishWork(*held)); QVERIFY(!env.scheduler->phase(original_cleanup));
    const auto second = scheduler->armSubmit(execution::Lane::Cleanup, 1, [] {});
    publish(); // Actual packet retirement now admits precisely one successor.
    const execution::TaskId successor_cleanup{second->originalId.load()};
    QVERIFY(successor_cleanup.value != 0); QVERIFY(successor_cleanup != original_cleanup);
    QCOMPARE(second->calls.load(), size_t{1});
    QCOMPARE(env.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Cleanup; }), size_t{1});
    QVERIFY(env.step(execution::Lane::Core)); QVERIFY(env.step(execution::Lane::Domain)); publish();
    QCOMPARE(model->rowCount(), 2); QCOMPARE(model->filter(), QStringLiteral("line"));
    QCOMPARE(env.queuedCountIf([](const auto& turn) { return turn.lane == execution::Lane::Cleanup; }), size_t{1});
    QVERIFY(env.step(execution::Lane::Cleanup)); QVERIFY(env.step(execution::Lane::Domain)); publish();
    QCOMPARE(env.coreSubmissions(), size_t{2}); QCOMPARE(errors.count(), 0); QVERIFY(model->openError().isEmpty());
    QVERIFY(env.transportIdle());
    qInfo() << "DebugLog reservation/read/Cleanup/successor-read/successor-Cleanup TaskIds"
            << reservation->originalId.load() << original_read.value << original_cleanup.value << successor_read.value << successor_cleanup.value;
    model.reset(); env.drain(); publish(); QVERIFY(env.quiescent());
}

void DebugLogModelTests::throwingEagerAdmissionRetainsOriginalOwner_data()
{
    QTest::addColumn<bool>("after_admission");
    QTest::newRow("throw-before-admission") << false;
    QTest::newRow("throw-after-admission") << true;
}

void DebugLogModelTests::throwingEagerAdmissionRetainsOriginalOwner()
{
    QFETCH(bool, after_admission);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log"); QVERIFY(WriteBytes(path, Record("keep first") + Record("drop second")));
    ManualExecutionEnvironment env;
    const auto scheduler = std::make_shared<ThrowingDebugCleanupScheduler>(env.scheduler);
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    const auto settle = [&] {
        for (int turn{0}; turn < 128 && !env.transportIdle(); ++turn) { env.drain(); env.publishAll(); }
    };
    const auto cleanup = qScopeGuard([&] {
        scheduler->armed = false; model.reset(); settle();
        if (!env.quiescent()) QTest::qFail("The exceptional original Cleanup graph did not drain", __FILE__, __LINE__);
    });
    model->setActive(true); settle(); QCOMPARE(model->rowCount(), 2); QVERIFY(model->openError().isEmpty());
    QSignalSpy errors{model.get(), &DebugLogModel::openErrorChanged};
    scheduler->after_admission = after_admission; scheduler->armed = true;
    model->setFilter("keep"); env.drain(); env.publishAll();
    QVERIFY(!scheduler->armed); QCOMPARE(model->rowCount(), 1);
    QCOMPARE(ContentAt(*model, 0), QStringLiteral("keep first"));
    QVERIFY(!model->openError().isEmpty()); QCOMPARE(errors.count(), 1);
    if (after_admission) {
        QVERIFY(scheduler->actual_admitted.value != 0);
        const auto phase = env.scheduler->phase(scheduler->actual_admitted); QVERIFY(phase);
        QCOMPARE(*phase, execution::Phase::Queued);
        qInfo() << "Actual eager Cleanup TaskId survived an admission exception" << scheduler->actual_admitted.value;
    } else {
        QCOMPARE(scheduler->actual_admitted.value, uint64_t{0});
        QVERIFY(env.turnsIdle());
        model->refresh(); // Original owners survived the non-admitted attempt.
        QVERIFY(!model->openError().isEmpty()); // Posting retry is not Native success.
    }
    settle();
    QCOMPARE(model->rowCount(), 1); QCOMPARE(ContentAt(*model, 0), QStringLiteral("keep first"));
    QVERIFY(model->openError().isEmpty()); QCOMPARE(errors.count(), 2);
    model.reset(); settle(); QVERIFY(env.quiescent());
}

void DebugLogModelTests::workAdmissionKeepsOriginalObserver_data()
{
    QTest::addColumn<bool>("during_admission"); QTest::addColumn<bool>("destroy_observer");
    QTest::newRow("busy-stop") << false << false;
    QTest::newRow("busy-delete") << false << true;
    QTest::newRow("admission-stop") << true << false;
    QTest::newRow("admission-delete") << true << true;
}

void DebugLogModelTests::workAdmissionKeepsOriginalObserver()
{
    QFETCH(bool, during_admission); QFETCH(bool, destroy_observer);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log"); QVERIFY(WriteBytes(path, Record("original")));
    ManualExecutionEnvironment env;
    const auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    bool revoked{false}; QMetaObject::Connection connection;
    QSignalSpy inserted{model.get(), &QAbstractItemModel::rowsInserted};
    const auto cleanup = qScopeGuard([&] {
        disconnect(connection); scheduler->disarm(); model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The revoked debug-log read did not drain", __FILE__, __LINE__);
    });
    const auto revoke = [&] { revoked = true; if (destroy_observer) model.reset(); else model->stop(); };
    std::shared_ptr<AdmissionHookScheduler::Trigger> trigger;
    if (during_admission) trigger = scheduler->armSubmit(execution::Lane::Core, 1, revoke);
    else connection = connect(model.get(), &DebugLogModel::busyChanged, this, revoke);
    model->setActive(true);
    QVERIFY(revoked); QCOMPARE(inserted.count(), 0);
    if (trigger) { QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0); }
    env.drain(); env.publishAll(); env.drain(); env.publishAll();
    QCOMPARE(inserted.count(), 0);
    model.reset(); QVERIFY(env.quiescent());
}

void DebugLogModelTests::factPublicationKeepsOriginalObserver_data()
{
    QTest::addColumn<int>("fact"); QTest::addColumn<bool>("destroy_observer");
    QTest::newRow("available-stop") << 0 << false;
    QTest::newRow("available-delete") << 0 << true;
    QTest::newRow("error-stop") << 1 << false;
    QTest::newRow("error-delete") << 1 << true;
    QTest::newRow("has-more-stop") << 2 << false;
    QTest::newRow("has-more-delete") << 2 << true;
}

void DebugLogModelTests::factPublicationKeepsOriginalObserver()
{
    QFETCH(int, fact); QFETCH(bool, destroy_observer);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log");
    if (fact != 1) QVERIFY(WriteBytes(path, NumberedRecords(0, 3)));
    ManualExecutionEnvironment env;
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{env.scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    bool revoked{false}; QMetaObject::Connection connection;
    QSignalSpy inserted{model.get(), &QAbstractItemModel::rowsInserted};
    const auto cleanup = qScopeGuard([&] {
        disconnect(connection); model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The original debug-log fact frame did not drain", __FILE__, __LINE__);
    });
    if (fact == 2) { model->setLoadLimit(1); env.drain(); env.publishAll(); env.drain(); env.publishAll(); }
    const auto revoke = [&] { revoked = true; if (destroy_observer) model.reset(); else model->stop(); };
    if (fact == 0) connection = connect(model.get(), &DebugLogModel::logAvailableChanged, this, revoke);
    else if (fact == 1) connection = connect(model.get(), &DebugLogModel::openErrorChanged, this, revoke);
    else connection = connect(model.get(), &DebugLogModel::hasMoreLinesChanged, this, revoke);
    model->setActive(true); env.drain();
    QVERIFY(!revoked); QCOMPARE(inserted.count(), 0);
    env.publishAll(); QVERIFY(revoked); QCOMPARE(inserted.count(), 0);
    model.reset();
    for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
    QVERIFY(env.quiescent());
}

void DebugLogModelTests::structuralCloseKeepsOriginalRowTransactions_data()
{
    QTest::addColumn<int>("change"); QTest::addColumn<bool>("at_end"); QTest::addColumn<bool>("destroy_observer");
    QTest::newRow("insert-about-stop") << 0 << false << false;
    QTest::newRow("insert-ended-stop") << 0 << true << false;
    QTest::newRow("insert-ended-delete") << 0 << true << true;
    QTest::newRow("remove-about-stop") << 1 << false << false;
    QTest::newRow("remove-ended-stop") << 1 << true << false;
    QTest::newRow("remove-ended-delete") << 1 << true << true;
    QTest::newRow("reset-about-stop") << 2 << false << false;
    QTest::newRow("reset-ended-stop") << 2 << true << false;
    QTest::newRow("reset-ended-delete") << 2 << true << true;
}

void DebugLogModelTests::structuralCloseKeepsOriginalRowTransactions()
{
    QFETCH(int, change); QFETCH(bool, at_end); QFETCH(bool, destroy_observer);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log"); QVERIFY(WriteBytes(path, NumberedRecords(0, change == 0 ? 40 : 2)));
    ManualExecutionEnvironment env;
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{env.scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    int before{-1}, expected{-1}, ended{-1}, endings{0}, declared{0}; bool revoked{false};
    QMetaObject::Connection about_connection, end_connection;
    const auto cleanup = qScopeGuard([&] {
        disconnect(about_connection); disconnect(end_connection); model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The closed debug-log row transaction did not drain", __FILE__, __LINE__);
    });
    if (change != 0) {
        model->setActive(true); env.drain(); env.publishAll(); env.drain(); env.publishAll();
        QCOMPARE(model->rowCount(), 2);
    }
    const auto revoke = [&] { revoked = true; if (destroy_observer) model.reset(); else model->stop(); };
    const auto about_rows = [&](const QModelIndex&, int first, int last) {
        before = model->rowCount(); declared = last - first + 1;
        expected = before + (change == 0 ? declared : -declared);
        if (!at_end) { revoke(); QCOMPARE(model->rowCount(), before); }
    };
    const auto ended_rows = [&](const QModelIndex&, int, int) {
        ended = model->rowCount(); ++endings; if (at_end) revoke();
    };
    if (change == 0) {
        about_connection = connect(model.get(), &QAbstractItemModel::rowsAboutToBeInserted, this, about_rows);
        end_connection = connect(model.get(), &QAbstractItemModel::rowsInserted, this, ended_rows);
        model->setActive(true);
    } else if (change == 1) {
        about_connection = connect(model.get(), &QAbstractItemModel::rowsAboutToBeRemoved, this, about_rows);
        end_connection = connect(model.get(), &QAbstractItemModel::rowsRemoved, this, ended_rows);
        model->setLoadLimit(1);
    } else {
        about_connection = connect(model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&] {
            before = model->rowCount(); expected = 0;
            if (!at_end) { revoke(); QCOMPARE(model->rowCount(), before); }
        });
        end_connection = connect(model.get(), &QAbstractItemModel::modelReset, this, [&] {
            ended = model->rowCount(); ++endings; if (at_end) revoke();
        });
        model->setFilter("line");
    }
    env.drain(); env.publishAll();
    QVERIFY(revoked); QCOMPARE(endings, 1); QCOMPARE(ended, expected);
    if (change != 2) QVERIFY(declared > 0 && declared <= 16);
    if (change == 0) { QCOMPARE(before, 0); QCOMPARE(ended, 16); }
    if (model) QCOMPARE(model->rowCount(), 0);
    model.reset();
    for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
    QVERIFY(env.quiescent());
}

void DebugLogModelTests::setterContinuationKeepsOriginalInterest_data()
{
    QTest::addColumn<int>("setter"); QTest::addColumn<int>("revocation");
    const std::array<const char*, 5> setters{{"active", "filter", "warnings", "limit", "more"}};
    const std::array<const char*, 3> revocations{{"stop", "delete", "successor"}};
    for (int setter{0}; setter < 5; ++setter) {
        for (int revocation{0}; revocation < 3; ++revocation) {
            const QByteArray tag = QByteArray{setters[setter]} + "-" + revocations[revocation];
            QTest::newRow(tag.constData()) << setter << revocation;
        }
    }
}

void DebugLogModelTests::setterContinuationKeepsOriginalInterest()
{
    QFETCH(int, setter); QFETCH(int, revocation);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log"); QVERIFY(WriteBytes(path, NumberedRecords(0, 2)));
    ManualRoutedExecution env;
    std::deque<std::function<void()>> publications;
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{env.scheduler, [&](auto callback) { publications.push_back(std::move(callback)); }});
    bool revoked{false}; QMetaObject::Connection connection;
    const auto settle = [&] {
        for (int turn{0}; turn < 128 && (!env.transportIdle() || !publications.empty()); ++turn) {
            env.drain();
            while (!publications.empty()) { auto callback = std::move(publications.front()); publications.pop_front(); callback(); }
        }
    };
    const auto cleanup = qScopeGuard([&] {
        disconnect(connection); model.reset(); settle();
        if (!env.quiescent() || !publications.empty()) QTest::qFail("The original setter graph did not drain", __FILE__, __LINE__);
    });
    model->setLoadLimit(2); settle();
    if (setter != 0) { model->setActive(true); settle(); QCOMPARE(model->rowCount(), 2); }
    const auto before = env.coreSubmissions();
    const auto revoke = [&] {
        if (revoked) return;
        revoked = true;
        if (revocation == 0) model->stop();
        else if (revocation == 1) model.reset();
        else if (setter == 0) model->setActive(false);
        else if (setter == 1) model->setFilter("line");
        else if (setter == 2) model->setWarningsAndErrorsOnly(false);
        else model->setLoadLimit(2);
    };
    if (setter == 0) connection = connect(model.get(), &DebugLogModel::activeChanged, this, revoke);
    else if (setter == 1) connection = connect(model.get(), &DebugLogModel::filterChanged, this, revoke);
    else if (setter == 2) connection = connect(model.get(), &DebugLogModel::warningsAndErrorsOnlyChanged, this, revoke);
    else connection = connect(model.get(), &DebugLogModel::loadLimitChanged, this, revoke);
    if (setter == 0) model->setActive(true);
    else if (setter == 1) model->setFilter("absent-original-interest");
    else if (setter == 2) model->setWarningsAndErrorsOnly(true);
    else if (setter == 3) model->setLoadLimit(1);
    else model->loadMore();
    QVERIFY(revoked); settle();
    QCOMPARE(env.coreSubmissions(), before + (revocation == 2 && setter != 0 ? 1 : 0));
    if (revocation == 2) {
        QVERIFY(model);
        if (setter == 0) { QVERIFY(!model->active()); QCOMPARE(model->rowCount(), 0); }
        else {
            QCOMPARE(model->rowCount(), 2);
            QCOMPARE(ContentAt(*model, 0), QStringLiteral("line 0")); QCOMPARE(ContentAt(*model, 1), QStringLiteral("line 1"));
            if (setter == 1) QCOMPARE(model->filter(), QStringLiteral("line"));
            if (setter == 2) QVERIFY(!model->warningsAndErrorsOnly());
            if (setter >= 3) QCOMPARE(model->loadLimit(), 2);
        }
    }
    model.reset(); settle(); QVERIFY(env.quiescent());
}

void DebugLogModelTests::nativeHandoffKeepsActualOutcome_data()
{
    QTest::addColumn<int>("revocation"); QTest::addColumn<bool>("actual_outcome");
    QTest::newRow("stop-success") << 0 << true;
    QTest::newRow("stop-failure") << 0 << false;
    QTest::newRow("delete-success") << 1 << true;
    QTest::newRow("delete-failure") << 1 << false;
    QTest::newRow("filter-success") << 2 << true;
    QTest::newRow("filter-failure") << 2 << false;
    QTest::newRow("new-open-success") << 3 << true;
    QTest::newRow("new-open-failure") << 3 << false;
}

void DebugLogModelTests::nativeHandoffKeepsActualOutcome()
{
    QFETCH(int, revocation); QFETCH(bool, actual_outcome);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log"); QVERIFY(WriteBytes(path, NumberedRecords(0, 2)));
    ManualExecutionEnvironment env;
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{env.scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    int calls{0}; QString actual_path; bool nested_result{false};
    QSignalSpy errors{model.get(), &DebugLogModel::openErrorChanged};
    const auto cleanup = qScopeGuard([&] {
        model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The original native handoff graph did not drain", __FILE__, __LINE__);
    });
    model->setActive(true); env.drain(); env.publishAll(); env.drain(); env.publishAll();
    QVERIFY(model->logAvailable()); QVERIFY(model->openError().isEmpty()); errors.clear();
    model->setOpenLocalFileFnForTesting([&](const QString& handed_path) {
        ++calls; actual_path = handed_path;
        if (revocation == 0) model->stop();
        else if (revocation == 1) model.reset();
        else if (revocation == 2) model->setFilter("line");
        else {
            model->setOpenLocalFileFnForTesting([&](const QString& successor_path) {
                ++calls; actual_path = successor_path; return !actual_outcome;
            });
            nested_result = model->openLogFile();
        }
        return actual_outcome;
    });
    const bool result = model->openLogFile();
    QCOMPARE(result, actual_outcome); QCOMPARE(calls, revocation == 3 ? 2 : 1); QCOMPARE(actual_path, path);
    const bool expected_error = revocation == 2 ? !actual_outcome : revocation == 3 && actual_outcome;
    QCOMPARE(errors.count(), expected_error ? 1 : 0);
    if (revocation == 3) QCOMPARE(nested_result, !actual_outcome);
    if (model) QCOMPARE(!model->openError().isEmpty(), expected_error);
    if (revocation == 2) {
        env.drain(); env.publishAll(); env.drain(); env.publishAll();
        QCOMPARE(model->filter(), QStringLiteral("line")); QCOMPARE(model->rowCount(), 2);
        // Unrelated presentation filtering cannot erase the real open receipt.
        QCOMPARE(!model->openError().isEmpty(), !actual_outcome);
    }
    model.reset();
    for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
    QVERIFY(env.quiescent());
}

void DebugLogModelTests::postEnqueueCannotResumeRevokedObserver_data()
{
    QTest::addColumn<int>("boundary"); QTest::addColumn<bool>("destroy_observer");
    QTest::newRow("refresh-stop") << 0 << false;
    QTest::newRow("refresh-delete") << 0 << true;
    QTest::newRow("reset-stop") << 1 << false;
    QTest::newRow("reset-delete") << 1 << true;
    QTest::newRow("complete-stop") << 2 << false;
    QTest::newRow("complete-delete") << 2 << true;
}

void DebugLogModelTests::postEnqueueCannotResumeRevokedObserver()
{
    QFETCH(int, boundary); QFETCH(bool, destroy_observer);
    QTemporaryDir directory; QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log"); QVERIFY(WriteBytes(path, NumberedRecords(0, 2)));
    ManualExecutionEnvironment env;
    const auto scheduler = std::make_shared<AdmissionHookScheduler>(env.scheduler);
    bool armed{false}, revoked{false};
    std::unique_ptr<DebugLogModel> model;
    QMetaObject::Connection arm_connection;
    const auto cleanup = qScopeGuard([&] {
        disconnect(arm_connection); scheduler->disarm(); armed = false; model.reset();
        for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
        if (!env.quiescent()) QTest::qFail("The original post-enqueue graph did not drain", __FILE__, __LINE__);
    });
    model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{scheduler, [&](auto callback) {
            env.post(std::move(callback)); // Delivery remains queued, even though enqueue can reenter.
            if (armed && !revoked) {
                revoked = true;
                if (destroy_observer) model.reset(); else model->stop();
            }
        }});
    model->setActive(true); env.drain(); env.publishAll(); env.drain(); env.publishAll();
    QCOMPARE(model->rowCount(), 2);
    if (boundary == 0) {
        model->setFilter("line"); env.drain(); env.rejectNext(); env.publishAll(); env.drain(); env.publishAll();
        QVERIFY(!model->openError().isEmpty());
    }
    QSignalSpy busy{model.get(), &DebugLogModel::busyChanged};
    QSignalSpy reset{model.get(), &QAbstractItemModel::modelReset};
    QSignalSpy inserted{model.get(), &QAbstractItemModel::rowsInserted};
    QSignalSpy added{model.get(), &DebugLogModel::newLinesAdded};
    if (boundary == 0) { armed = true; model->refresh(); }
    else if (boundary == 1) {
        arm_connection = connect(model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&] { armed = true; });
        model->setFilter("line");
    } else {
        QVERIFY(WriteBytes(path, Record("line 2"), QIODevice::Append | QIODevice::WriteOnly));
        arm_connection = connect(model.get(), &QAbstractItemModel::rowsInserted, this,
            [&](const QModelIndex&, int, int) { armed = true; });
        model->refresh();
    }
    if (boundary != 0) {
        // Retire the earlier State Cleanup packet before publishing this
        // original read's GUI frame, so its reset/completion post is the
        // exact enqueue boundary under test rather than a coalesced request.
        const auto first = scheduler->armSubmit(execution::Lane::Cleanup, 1, [] {});
        QVERIFY(env.publishNext());
        const execution::TaskId early_cleanup{first->originalId.load()}; QVERIFY(early_cleanup.value != 0);
        const auto queued = env.scheduler->phase(early_cleanup); QVERIFY(queued);
        QCOMPARE(*queued, execution::Phase::Queued);
        env.drain(); QVERIFY(!env.scheduler->phase(early_cleanup));
        qInfo() << "Retired original State Cleanup before GUI post boundary" << early_cleanup.value;
    }
    env.drain(); env.publishAll();
    QVERIFY(revoked); QCOMPARE(added.count(), 0);
    QCOMPARE(busy.count(), boundary == 0 ? 0 : 1);
    QCOMPARE(reset.count(), boundary == 1 && !destroy_observer ? 1 : 0);
    QCOMPARE(inserted.count(), boundary == 2 ? 1 : 0);
    model.reset();
    for (int turn{0}; turn < 128 && !env.quiescent(); ++turn) { env.drain(); env.publishAll(); }
    QVERIFY(env.quiescent());
}

void DebugLogModelTests::inactiveModel_ignoresRefreshUntilActivated()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, Record("line one")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    QVERIFY(!model.active());

    model.refresh(/*full_load=*/true);
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 0);

    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 1);

    model.setActive(false);
    QVERIFY(WriteBytes(log_path, Record("line two"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh(/*full_load=*/true);
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 1);

    // Reactivation paints the retained row immediately, then catches up from
    // the saved byte offset without requiring another full tail read.
    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setActive(true);
    QTRY_COMPARE(ContentAt(model, 1), QStringLiteral("line two"));
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);
}

void DebugLogModelTests::parsedRoles_extractStructuredLogLines()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");

    QByteArray records;
    records += "2026-06-19T10:00:00.123456Z [net] Bound to 127.0.0.1\n";
    records += "2026-06-19T10:00:01Z [rpc:error] boom <bad>\n";
    records += "2026-06-19T10:00:02Z [mempool] Imported transactions\n";
    records += "2026-06-19T10:00:02Z [bench] benchmark completed\n";
    records += "2026-06-19T10:00:02Z [net:warning] peer is slow\n";
    records += "2026-06-19T10:00:03Z ERROR: legacy failure\n";
    records += "continuation without metadata\n";
    QVERIFY(WriteBytes(log_path, records));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 7);

    const QModelIndex network = model.index(0, 0);
    QCOMPARE(model.data(network, DebugLogModel::MessageRole).toString(), QStringLiteral("Bound to 127.0.0.1"));
    QCOMPARE(model.data(network, DebugLogModel::IsErrorRole).toBool(), false);
    QCOMPARE(model.data(network, DebugLogModel::IsWarningRole).toBool(), false);
    const QDateTime utc_time = QDateTime::fromString(
        QStringLiteral("2026-06-19T10:00:00Z"), Qt::ISODate);
    const QString expected_local_time = utc_time.toLocalTime().toString(
        QStringLiteral("HH:mm:ss"));
    QCOMPARE(model.data(network, DebugLogModel::TimestampRole).toString(), expected_local_time);

    const QModelIndex rpc_error = model.index(1, 0);
    QCOMPARE(model.data(rpc_error, DebugLogModel::MessageRole).toString(), QStringLiteral("boom <bad>"));
    QCOMPARE(model.data(rpc_error, DebugLogModel::IsErrorRole).toBool(), true);
    QCOMPARE(model.data(rpc_error, DebugLogModel::IsWarningRole).toBool(), false);

    const QModelIndex mempool = model.index(2, 0);
    QCOMPARE(model.data(mempool, DebugLogModel::MessageRole).toString(), QStringLiteral("Imported transactions"));
    QCOMPARE(model.data(mempool, DebugLogModel::IsErrorRole).toBool(), false);

    const QModelIndex bench = model.index(3, 0);
    QCOMPARE(model.data(bench, DebugLogModel::MessageRole).toString(), QStringLiteral("benchmark completed"));

    const QModelIndex warning = model.index(4, 0);
    QCOMPARE(model.data(warning, DebugLogModel::MessageRole).toString(), QStringLiteral("peer is slow"));
    QCOMPARE(model.data(warning, DebugLogModel::IsErrorRole).toBool(), false);
    QCOMPARE(model.data(warning, DebugLogModel::IsWarningRole).toBool(), true);

    const QModelIndex legacy_error = model.index(5, 0);
    QCOMPARE(model.data(legacy_error, DebugLogModel::MessageRole).toString(), QStringLiteral("legacy failure"));
    QCOMPARE(model.data(legacy_error, DebugLogModel::IsErrorRole).toBool(), true);
    QCOMPARE(model.data(legacy_error, DebugLogModel::IsWarningRole).toBool(), false);

    const QModelIndex continuation = model.index(6, 0);
    QCOMPARE(model.data(continuation, DebugLogModel::TimestampRole).toString(), QString{});

    model.setWarningsAndErrorsOnly(true);
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("boom <bad>"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("peer is slow"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("legacy failure"));
}

void DebugLogModelTests::initialLoad_usesBoundedBatchesAndDetectsHasMore()
{
    QTemporaryDir exact_dir;
    QVERIFY(exact_dir.isValid());
    const QString exact_path = exact_dir.filePath("debug.log");
    QVERIFY(WriteBytes(exact_path, NumberedRecords(0, 1000)));

    DebugLogModel exact_model(fs::PathFromString(exact_path.toStdString()));
    QSignalSpy exact_reset_spy(&exact_model, &QAbstractItemModel::modelReset);
    QSignalSpy exact_insert_spy(&exact_model, &QAbstractItemModel::rowsInserted);
    exact_model.setActive(true);
    QTRY_COMPARE(exact_model.rowCount(), 1000);
    QVERIFY(!exact_model.hasMoreLines());
    QCOMPARE(exact_reset_spy.count(), 1);
    QCOMPARE(exact_insert_spy.count(), 63);
    int published{0};
    for (const auto& insertion : exact_insert_spy) {
        const int count = insertion.at(2).toInt() - insertion.at(1).toInt() + 1;
        QVERIFY(count > 0 && count <= 16); published += count;
    }
    QCOMPARE(published, 1000);

    QTemporaryDir extra_dir;
    QVERIFY(extra_dir.isValid());
    const QString extra_path = extra_dir.filePath("debug.log");
    QVERIFY(WriteBytes(extra_path, NumberedRecords(0, 1001)));

    DebugLogModel extra_model(fs::PathFromString(extra_path.toStdString()));
    QSignalSpy reset_spy(&extra_model, &QAbstractItemModel::modelReset);
    extra_model.setActive(true);
    QTRY_COMPARE(extra_model.rowCount(), 1000);
    QTRY_VERIFY(extra_model.hasMoreLines());
    QCOMPARE(reset_spy.count(), 1);
    QCOMPARE(ContentAt(extra_model, 0), QStringLiteral("line 1"));
    QCOMPARE(ContentAt(extra_model, 999), QStringLiteral("line 1000"));
}

void DebugLogModelTests::initialLoad_handlesBlankLinesAtBlockBoundaries()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");

    QByteArray bytes;
    // More than one 64 KiB read block, with empty records around and across
    // block boundaries. Empty lines must neither hang parsing nor consume the
    // requested nonblank-line budget.
    for (int i = 0; i < 1400; ++i) {
        bytes += (i % 3 == 0) ? QByteArray{"\n"} : Record("payload " + QByteArray::number(i) + QByteArray(90, 'x'));
    }
    bytes += Record("very long " + QByteArray(70 * 1024, 'y'));
    QVERIFY(bytes.size() > 64 * 1024);
    QVERIFY(WriteBytes(log_path, bytes));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setLoadLimit(400);
    model.setActive(true);
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 470);
    QVERIFY(model.hasMoreLines());
    QString long_record;
    for (int row = 399; row < model.rowCount(); ++row) {
        const auto chunk = ContentAt(model, row);
        QVERIFY(chunk.size() <= 1024); long_record += chunk;
    }
    QCOMPARE(long_record, QStringLiteral("very long ") + QString(70 * 1024, QLatin1Char('y')));
}

void DebugLogModelTests::initialLoad_discardsOversizedPartialAndResynchronizes()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path,
                       Record("older one") + Record("older two") + OversizedLine('x')));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("older one"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("older two"));

    // The newline completes the discarded physical line. Parsing resumes with
    // the first normal record after it instead of exposing a tail fragment.
    QVERIFY(WriteBytes(log_path, QByteArray{"\n"} + Record("after oversized"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 1), QStringLiteral("older two"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("after oversized"));
}

void DebugLogModelTests::initialLoad_skipsOversizedCompleteLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path,
                       Record("older") + OversizedLine('x') + '\n' + Record("newer")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("older"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("newer"));
}

void DebugLogModelTests::largeTailKeepsCompleteRecordFraming_data()
{
    QTest::addColumn<int>("shape");
    QTest::newRow("unterminated-only") << 0;
    QTest::newRow("oversized-only") << 1;
    QTest::newRow("oversized-middle") << 2;
    QTest::newRow("oversized-suffix") << 3;
}

void DebugLogModelTests::largeTailKeepsCompleteRecordFraming()
{
    QFETCH(int, shape);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log");
    const QByteArray oversized(3 * DebugLogModel::kMaxLogLineBytes, 'x');
    QByteArray original;
    if (shape == 0) original = "unfinished";
    if (shape == 1) original = oversized;
    if (shape == 2) original = Record("before") + oversized + '\n' + Record("after");
    if (shape == 3) original = Record("before") + oversized;
    QVERIFY(WriteBytes(path, original));
    ManualExecutionEnvironment env;
    DebugLogModel model{fs::PathFromString(path.toStdString()), true,
        {env.scheduler, [&](auto callback) { env.post(std::move(callback)); }}};
    const auto drain = [&] {
        for (int turn{0}; turn < 256; ++turn) {
            env.drain();
            env.publishAll();
            QCoreApplication::processEvents();
            if (!model.busy() && env.transportIdle()) return;
        }
        QTest::qFail("The original log read and GUI projection did not settle", __FILE__, __LINE__);
    };
    const auto cleanup = qScopeGuard([&] {
        model.stop(); drain();
        if (!env.quiescent()) QTest::qFail("The original tail owner did not drain", __FILE__, __LINE__);
    });
    model.setActive(true);
    drain();
    QVERIFY(model.logAvailable());
    QVERIFY(!model.hasMoreLines());
    QCOMPARE(model.rowCount(), shape < 2 ? 0 : shape == 2 ? 2 : 1);
    if (shape >= 2) QCOMPARE(ContentAt(model, 0), QStringLiteral("before"));
    if (shape == 2) QCOMPARE(ContentAt(model, 1), QStringLiteral("after"));

    const QByteArray appended = shape == 2 ? Record("last") : QByteArray(QByteArray{"\n"} + Record("after"));
    QVERIFY(WriteBytes(path, appended, QIODevice::WriteOnly | QIODevice::Append));
    model.refresh();
    drain();
    QCOMPARE(model.rowCount(), shape == 1 ? 1 : shape == 2 ? 3 : 2);
    QCOMPARE(ContentAt(model, model.rowCount() - 1), shape == 2 ? QStringLiteral("last") : QStringLiteral("after"));
    if (shape == 0) QCOMPARE(ContentAt(model, 0), QStringLiteral("unfinished"));
    if (shape >= 2) QCOMPARE(ContentAt(model, 0), QStringLiteral("before"));
    QFile unchanged{path};
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), original + appended);
}

void DebugLogModelTests::largeTailStopsAtRetainedByteBudget()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log");
    QByteArray bytes;
    QByteArray newest;
    for (int record{0}; record < 8; ++record) {
        newest = "record " + QByteArray::number(record) + ' ' + QByteArray(768 * 1024, 'x');
        bytes += Record(newest);
    }
    QVERIFY(WriteBytes(path, bytes));
    ManualExecutionEnvironment env;
    DebugLogModel model{fs::PathFromString(path.toStdString()), true,
        {env.scheduler, [&](auto callback) { env.post(std::move(callback)); }}};
    const auto drain = [&] {
        for (int turn{0}; turn < DebugLogModel::kMaxLoadLimit; ++turn) {
            env.drain();
            env.publishAll();
            QCoreApplication::processEvents();
            if (!model.busy() && env.transportIdle()) return;
        }
        QTest::qFail("The byte-bounded tail did not physically settle", __FILE__, __LINE__);
    };
    const auto cleanup = qScopeGuard([&] {
        model.stop(); drain();
        if (!env.quiescent()) QTest::qFail("The byte-bounded tail owner did not drain", __FILE__, __LINE__);
    });
    // Keep the Load More command available: at the public hard row limit the
    // model intentionally hides it, independently of retained-byte pressure.
    model.setLoadLimit(DebugLogModel::kMaxLoadLimit - 1);
    model.setActive(true);
    drain();
    QVERIFY(model.logAvailable());
    QVERIFY(model.hasMoreLines());
    QVERIFY(model.rowCount() > 0);
    QString retained;
    for (int row{0}; row < model.rowCount(); ++row) {
        const auto text = ContentAt(model, row);
        QVERIFY(text.size() <= 1024);
        retained += text;
    }
    // The 8 MiB retained-record budget accounts for metadata as well as UTF-16.
    QVERIFY(retained.size() <= 4 * 1024 * 1024);
    QVERIFY(!retained.contains(QStringLiteral("record 0 ")));
    QVERIFY(retained.endsWith(QString::fromUtf8(newest)));
    QFile unchanged{path};
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), bytes);
}

void DebugLogModelTests::deltaAfterEmptyLoad_preservesHasMoreSentinel()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, {}));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setActive(true);
    QTRY_COMPARE(reset_spy.count(), 1);
    QCOMPARE(model.rowCount(), 0);

    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 1001),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1000);
    QTRY_VERIFY(model.hasMoreLines());
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 1"));
    QCOMPARE(ContentAt(model, 999), QStringLiteral("line 1000"));
    QCOMPARE(reset_spy.count(), 1);

    model.loadMore();
    QTRY_COMPARE(model.rowCount(), 1001);
    QVERIFY(!model.hasMoreLines());
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 0"));
}

void DebugLogModelTests::liveRefresh_appendsWithoutResetAndPrunesHead()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 3)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setLoadLimit(3);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 3);

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy remove_spy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QSignalSpy new_lines_spy(&model, &DebugLogModel::newLinesAdded);

    QVERIFY(WriteBytes(log_path, NumberedRecords(3, 2),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();

    QTRY_COMPARE(ContentAt(model, 2), QStringLiteral("line 4"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 2"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("line 3"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 1);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 2);
    QCOMPARE(remove_spy.count(), 1);
    QCOMPARE(remove_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(remove_spy.at(0).at(2).toInt(), 1);
    QCOMPARE(reset_spy.count(), 0);
    QCOMPARE(new_lines_spy.count(), 1);
    QCOMPARE(new_lines_spy.at(0).at(0).toInt(), 2);
    QVERIFY(model.hasMoreLines());

    model.refresh();
    QTest::qWait(50);
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);
}

void DebugLogModelTests::liveRefresh_canFullyDisplaceCacheWithoutReset()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 3)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setLoadLimit(3);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 3);

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy remove_spy(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QVERIFY(WriteBytes(log_path, NumberedRecords(3, 4),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();

    QTRY_COMPARE(ContentAt(model, 2), QStringLiteral("line 6"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 4"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("line 5"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 2);
    QCOMPARE(remove_spy.count(), 1);
    QCOMPARE(remove_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(remove_spy.at(0).at(2).toInt(), 2);
    QCOMPARE(reset_spy.count(), 0);
    QVERIFY(model.hasMoreLines());
}

void DebugLogModelTests::liveRefresh_handlesDuplicateRecordsAndPartialWrites()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    const QByteArray duplicate = Record("identical");
    QVERIFY(WriteBytes(log_path, duplicate + duplicate));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QVERIFY(WriteBytes(log_path, duplicate, QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);

    // The incomplete record is retained off-model until its newline arrives.
    QVERIFY(WriteBytes(log_path, QByteArray{"2026-06-19T10:00:00Z split"},
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(insert_spy.count(), 1);

    QVERIFY(WriteBytes(log_path, QByteArray{" record\n"},
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 4);
    QCOMPARE(ContentAt(model, 3), QStringLiteral("split record"));
    QCOMPARE(insert_spy.count(), 2);
    QCOMPARE(reset_spy.count(), 0);
}

void DebugLogModelTests::chunkedDuplicateRecordsKeepOriginalOverlapIdentity()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto path = directory.filePath("debug.log");
    const QByteArray payload{QByteArray(1024, 'a') + QByteArray(1024, 'b') + QByteArray{"cc"}};
    const QByteArray record{Record(payload)};
    QVERIFY(WriteBytes(path, record + record));
    ManualExecutionEnvironment env;
    auto model = std::make_unique<DebugLogModel>(fs::PathFromString(path.toStdString()), true,
        execution::SnapshotServices{env.scheduler, [&](auto callback) { env.post(std::move(callback)); }});
    const auto cleanup = qScopeGuard([&] {
        model->stop();
        model.reset();
        env.drain();
        env.publishAll();
        env.drain();
        if (!env.quiescent()) QTest::qFail("The original debug-log graph did not physically drain", __FILE__, __LINE__);
    });
    model->setLoadLimit(2);
    env.drain();
    env.publishAll(); // Settle the empty inactive projection before reading.
    model->setActive(true);
    env.drain();
    QCOMPARE(model->rowCount(), 0);
    env.publishAll();
    QCOMPARE(model->rowCount(), 6);
    const QPersistentModelIndex removed{model->index(0, 0)};
    const QPersistentModelIndex first{model->index(3, 0)};
    const QPersistentModelIndex second{model->index(4, 0)};
    const QPersistentModelIndex third{model->index(5, 0)};
    QSignalSpy inserted{model.get(), &QAbstractItemModel::rowsInserted};
    QSignalSpy erased{model.get(), &QAbstractItemModel::rowsRemoved};
    QSignalSpy reset{model.get(), &QAbstractItemModel::modelReset};
    QVERIFY(WriteBytes(path, record, QIODevice::Append | QIODevice::WriteOnly));
    model->refresh();
    env.drain();
    QCOMPARE(first.row(), 3); // Native reduction cannot publish the new overlap.
    env.publishAll();
    QCOMPARE(model->rowCount(), 6);
    QVERIFY(!removed.isValid());
    QVERIFY(first.isValid());
    QVERIFY(second.isValid());
    QVERIFY(third.isValid());
    QCOMPARE(first.row(), 0);
    QCOMPARE(second.row(), 1);
    QCOMPARE(third.row(), 2);
    QCOMPARE(ContentAt(*model, 0), QString(1024, QLatin1Char('a')));
    QCOMPARE(ContentAt(*model, 1), QString(1024, QLatin1Char('b')));
    QCOMPARE(ContentAt(*model, 2), QStringLiteral("cc"));
    QCOMPARE(ContentAt(*model, 3), ContentAt(*model, 0));
    QCOMPARE(ContentAt(*model, 4), ContentAt(*model, 1));
    QCOMPARE(ContentAt(*model, 5), ContentAt(*model, 2));
    QCOMPARE(reset.count(), 0);
    QCOMPARE(erased.count(), 1);
    QCOMPARE(erased.at(0).at(1).toInt(), 0);
    QCOMPARE(erased.at(0).at(2).toInt(), 2);
    QCOMPARE(inserted.count(), 1);
    QCOMPARE(inserted.at(0).at(1).toInt(), 3);
    QCOMPARE(inserted.at(0).at(2).toInt(), 5);
    QVERIFY(!model->busy());
}

void DebugLogModelTests::liveRefresh_discardsOversizedPartialUntilNewline()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, Record("baseline")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 1);

    QVERIFY(WriteBytes(log_path,
                       QByteArray(DebugLogModel::kMaxLogLineBytes - 16, 'x'),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(100);
    QCOMPARE(model.rowCount(), 1);

    // Cross the limit in a later watcher read, then verify further fragments
    // remain discarded until the record boundary arrives.
    QVERIFY(WriteBytes(log_path, QByteArray(32, 'y'),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(100);
    QVERIFY(WriteBytes(log_path, QByteArray{"ignored tail"},
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(100);
    QCOMPARE(model.rowCount(), 1);

    QVERIFY(WriteBytes(log_path, QByteArray{"\n"} + Record("after oversized"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("baseline"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("after oversized"));
}

void DebugLogModelTests::liveRefresh_skipsOversizedCompleteLine()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, Record("baseline")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 1);

    QVERIFY(WriteBytes(log_path,
                       Record("before oversized") + OversizedLine('x') + '\n'
                           + Record("after oversized"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 3);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("baseline"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("before oversized"));
    QCOMPARE(ContentAt(model, 2), QStringLiteral("after oversized"));
}

void DebugLogModelTests::loadMore_insertsOlderRowsAtTop()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 1200)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 1000);
    QVERIFY(model.hasMoreLines());

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    model.loadMore();

    QTRY_COMPARE(model.rowCount(), 1200);
    QCOMPARE(model.loadLimit(), 2000);
    QVERIFY(!model.hasMoreLines());
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 0"));
    QCOMPARE(ContentAt(model, 199), QStringLiteral("line 199"));
    QCOMPARE(ContentAt(model, 200), QStringLiteral("line 200"));
    QCOMPARE(ContentAt(model, 1199), QStringLiteral("line 1199"));
    QCOMPARE(insert_spy.count(), 13);
    int inserted{0};
    for (const auto& batch : insert_spy) {
        QCOMPARE(batch.at(1).toInt(), 0);
        const int count = batch.at(2).toInt() + 1;
        QVERIFY(count <= 16); inserted += count;
    }
    QCOMPARE(inserted, 200);
    QCOMPARE(reset_spy.count(), 0);
}

void DebugLogModelTests::widerTailRequest_survivesRacesAndDeactivation()
{
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log_path = dir.filePath("debug.log");
        QVERIFY(WriteBytes(log_path, NumberedRecords(0, 2500)));

        DebugLogModel model(fs::PathFromString(log_path.toStdString()));
        model.setActive(true);
        QTRY_COMPARE(model.rowCount(), 1000);

        // The second request is issued while the first full-tail read may be
        // in flight. A stale 2,000-row result must trigger the pending 3,000
        // capacity read rather than becoming the final snapshot.
        model.loadMore();
        model.loadMore();
        QCOMPARE(model.loadLimit(), 3000);
        QTRY_COMPARE(model.rowCount(), 2500);
        QVERIFY(!model.hasMoreLines());
        QCOMPARE(ContentAt(model, 0), QStringLiteral("line 0"));
        QCOMPARE(ContentAt(model, 2499), QStringLiteral("line 2499"));
    }

    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString log_path = dir.filePath("debug.log");
        QVERIFY(WriteBytes(log_path, NumberedRecords(0, 1200)));

        DebugLogModel model(fs::PathFromString(log_path.toStdString()));
        model.setActive(true);
        QTRY_COMPARE(model.rowCount(), 1000);

        model.loadMore();
        model.setActive(false);
        model.setActive(true);
        QTRY_COMPARE(model.rowCount(), 1200);
        QCOMPARE(model.loadLimit(), 2000);
        QCOMPARE(ContentAt(model, 0), QStringLiteral("line 0"));
    }
}

void DebugLogModelTests::filter_updatesIncrementallyAndWhileInactive()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, Record("keep old") + Record("drop old")));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 2);
    model.setFilter(QStringLiteral("keep"));
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("keep old"));

    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QVERIFY(WriteBytes(log_path, Record("keep new") + Record("drop new"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("keep old"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("keep new"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(reset_spy.count(), 0);

    model.setActive(false);
    model.setFilter(QStringLiteral("drop"));
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("drop old"));
    QVERIFY(WriteBytes(log_path, Record("drop while inactive"),
                       QIODevice::Append | QIODevice::WriteOnly));
    model.refresh();
    QTest::qWait(50);
    QCOMPARE(model.rowCount(), 2);

    QSignalSpy reactivate_reset_spy(&model, &QAbstractItemModel::modelReset);
    model.setActive(true);
    QTRY_COMPARE(ContentAt(model, 2), QStringLiteral("drop while inactive"));
    QCOMPARE(model.rowCount(), 3);
    QCOMPARE(reactivate_reset_spy.count(), 0);
}

void DebugLogModelTests::rotation_fallsBackToFullSnapshot()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 5)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 5);

    QSignalSpy reset_spy(&model, &QAbstractItemModel::modelReset);
    QVERIFY(WriteBytes(log_path, Record("rotated one") + Record("rotated two")));
    model.refresh();
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("rotated one"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("rotated two"));
    QCOMPARE(reset_spy.count(), 1);
}

void DebugLogModelTests::replacementWithMatchingSuffixUsesFileIdentity_data()
{
    QTest::addColumn<bool>("replace");
    QTest::newRow("replacement-file") << true;
    QTest::newRow("rewrite-same-file") << false;
}

void DebugLogModelTests::replacementWithMatchingSuffixUsesFileIdentity()
{
    QFETCH(bool, replace);
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("debug.log");
    const auto suffix = NumberedRecords(0, 20); QVERIFY(suffix.size() > 256);
    QVERIFY(WriteBytes(path, Record("original first") + suffix));
    ManualExecutionEnvironment env;
    DebugLogModel model{fs::PathFromString(path.toStdString()), true, {env.scheduler, [&](auto turn) { env.post(std::move(turn)); }}};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    model.setActive(true);
    QTRY_VERIFY(([&] { env.drain(); env.publishAll(); return !model.busy(); })());
    QCOMPARE(model.rowCount(), 21); QCOMPARE(ContentAt(model, 0), QStringLiteral("original first"));
    QSignalSpy reset{&model, &QAbstractItemModel::modelReset};
    if (replace) QVERIFY(QFile::rename(path, dir.filePath("debug.log.1")));
    const auto modified = QFileInfo{path}.lastModified();
    QVERIFY(WriteBytes(path, Record("replaced first") + suffix)); // Same byte length and same 256-byte suffix.
    if (!replace) {
        QFile rewritten{path}; QVERIFY(rewritten.open(QIODevice::ReadWrite));
        QVERIFY(rewritten.setFileTime(modified.addSecs(1), QFileDevice::FileModificationTime));
    }
    model.refresh();
    QTRY_VERIFY(([&] { env.drain(); env.publishAll(); return !model.busy(); })());
    QCOMPARE(model.rowCount(), 21); QCOMPARE(ContentAt(model, 0), QStringLiteral("replaced first"));
    QCOMPARE(reset.size(), 1); QVERIFY(model.openError().isEmpty());
}

void DebugLogModelTests::staleActivationCannotPublishOrLoseRetainedRows()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("debug.log"); QVERIFY(WriteBytes(path, Record("old activation")));
    ManualExecutionEnvironment env;
    DebugLogModel model{fs::PathFromString(path.toStdString()), true, {env.scheduler, [&](auto turn) { env.post(std::move(turn)); }}};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    model.setActive(true); QVERIFY(env.step()); // Native owns an old snapshot; no GUI adoption.
    model.setActive(false);
    QVERIFY(env.step()); env.publishAll();
    QVERIFY(!model.busy()); QCOMPARE(model.rowCount(), 0);
    QVERIFY(WriteBytes(path, Record("new activation"), QIODevice::WriteOnly | QIODevice::Append));
    model.setActive(true);
    QTRY_VERIFY(([&] { env.drain(); env.publishAll(); return !model.busy(); })());
    QCOMPARE(model.rowCount(), 2); QCOMPARE(ContentAt(model, 0), QStringLiteral("old activation"));
    QCOMPARE(ContentAt(model, 1), QStringLiteral("new activation"));
    QVERIFY(model.openError().isEmpty());
    // Hold another read across a complete deactivate/reactivate cycle. Its
    // superseded availability/error facts cannot publish into the new visit.
    model.refresh(); QVERIFY(env.step()); model.setActive(false); model.setActive(true);
    QVERIFY(WriteBytes(path, Record("latest activation"), QIODevice::WriteOnly | QIODevice::Append));
    QVERIFY(env.step()); env.publishAll();
    QCOMPARE(model.rowCount(), 2); QVERIFY(model.busy());
    QTRY_VERIFY(([&] { env.drain(); env.publishAll(); return !model.busy(); })());
    QCOMPARE(model.rowCount(), 3); QCOMPARE(ContentAt(model, 2), QStringLiteral("latest activation"));
}

void DebugLogModelTests::hiddenReadDoesNotCancelIndependentCommand()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("debug.log"); QVERIFY(WriteBytes(path, Record("diagnostic")));
    struct Node final : StubNode {
        int effects{0};
        UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { ++effects; return UniValue{3}; }
    } node;
    ManualExecutionEnvironment env;
    const execution::SnapshotServices services{env.scheduler, [&](auto turn) { env.post(std::move(turn)); }};
    DebugLogModel log{fs::PathFromString(path.toStdString()), true, services};
    RpcConsoleModel console{node, services};
    const auto settle = qScopeGuard([&] { env.drain(); env.publishAll(); });
    log.setActive(true); QVERIFY(console.submitNodeCommand("getblockcount"));
    const auto receipt = console.commandReceipt(); log.setActive(false);
    env.drain(); env.publishAll();
    QCOMPARE(log.rowCount(), 0); QVERIFY(!log.busy());
    QCOMPARE(node.effects, 1); QVERIFY(!console.executing());
    QVERIFY(receipt->result()); QVERIFY(receipt->result()->value); QVERIFY(receipt->result()->value->attempted);
}

void DebugLogModelTests::hugeUnfinishedSuffixHasBoundedReadAndRecovery()
{
    QTemporaryDir dir; QVERIFY(dir.isValid());
    const auto path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(path, QByteArray(9 * 1024 * 1024, 'x')));
    DebugLogModel model{fs::PathFromString(path.toStdString())};
    model.setActive(true); QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 1);
    QVERIFY(ContentAt(model, 0).contains("omitted"));
    QVERIFY(model.logAvailable());
    QVERIFY(WriteBytes(path, QByteArray{"\n"} + Record("framing recovered"), QIODevice::WriteOnly | QIODevice::Append));
    model.refresh(); QTRY_VERIFY(!model.busy());
    QCOMPARE(ContentAt(model, model.rowCount() - 1), QStringLiteral("framing recovered"));
}

void DebugLogModelTests::loadLimit_changesKeepRetainedCacheBounded()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString log_path = dir.filePath("debug.log");
    QVERIFY(WriteBytes(log_path, NumberedRecords(0, 5)));

    DebugLogModel model(fs::PathFromString(log_path.toStdString()));
    model.setLoadLimit(5);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 5);
    model.setActive(false);

    model.setLoadLimit(2);
    QTRY_VERIFY(!model.busy());
    QCOMPARE(model.rowCount(), 2);
    QVERIFY(model.hasMoreLines());

    // Raising the cap while inactive forces a wider bounded tail read on the
    // next activation; older rows are prepended at the top.
    QSignalSpy insert_spy(&model, &QAbstractItemModel::rowsInserted);
    model.setLoadLimit(4);
    QCOMPARE(model.rowCount(), 2);
    model.setActive(true);
    QTRY_COMPARE(model.rowCount(), 4);
    QCOMPARE(ContentAt(model, 0), QStringLiteral("line 1"));
    QCOMPARE(ContentAt(model, 3), QStringLiteral("line 4"));
    QCOMPARE(insert_spy.count(), 1);
    QCOMPARE(insert_spy.at(0).at(1).toInt(), 0);
    QCOMPARE(insert_spy.at(0).at(2).toInt(), 1);
}

void DebugLogModelTests::openErrorEmptyByDefault()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const fs::path log_path = fs::PathFromString(temp_dir.filePath("debug.log").toStdString());

    DebugLogModel model(log_path);
    QVERIFY(model.openError().isEmpty());
}

// The action error (this click failed) and the state error (the log itself
// cannot be read) have different lifetimes, so they cannot share a field: the
// background reader clears its own error on every successful read, which would
// wipe the click failure on the next auto-refresh tick.
void DebugLogModelTests::openErrorPersistsAcrossSuccessfulRead()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("debug.log");
    const fs::path log_path = fs::PathFromString(path.toStdString());
    QVERIFY(WriteBytes(path, NumberedRecords(0, 2)));

    // The log exists and is readable; only the hand-off to another application
    // fails, which is the one thing openLogFile() still reports itself.
    DebugLogModel model(log_path);
    model.setActive(true);
    QTRY_VERIFY(!model.busy());
    QVERIFY(model.logAvailable());
    model.setOpenLocalFileFnForTesting([](const QString&) { return false; });
    QVERIFY(!model.openLogFile());
    const QString action_error = model.openError();
    QVERIFY(!action_error.isEmpty());

    // Let a subsequent background read succeed.
    model.refresh();
    QTRY_VERIFY(!model.busy());
    QTRY_COMPARE(model.rowCount(), 2);

    // The read succeeded, but the click failure must still be reported.
    QCOMPARE(model.openError(), action_error);
}

// clearOpenError() drops the action error (the page calls it when re-entered so
// a stale click failure does not survive navigation) and is a no-op when
// nothing is pending.
void DebugLogModelTests::clearOpenErrorClearsOnlyWhenSet()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("debug.log");
    QVERIFY(WriteBytes(path, NumberedRecords(0, 1)));

    DebugLogModel model(fs::PathFromString(path.toStdString()));
    model.setActive(true);
    QTRY_VERIFY(!model.busy());
    QVERIFY(model.logAvailable());
    model.setOpenLocalFileFnForTesting([](const QString&) { return false; });
    QSignalSpy error_spy(&model, &DebugLogModel::openErrorChanged);

    // No pending error: no change, no signal.
    model.clearOpenError();
    QCOMPARE(error_spy.count(), 0);

    QVERIFY(!model.openLogFile());
    QVERIFY(!model.openError().isEmpty());
    QCOMPARE(error_spy.count(), 1);

    model.clearOpenError();
    QVERIFY(model.openError().isEmpty());
    QCOMPARE(error_spy.count(), 2);

    QVERIFY(!model.openLogFile());
    const auto original_error = model.openError();
    QVERIFY(!original_error.isEmpty()); QCOMPARE(error_spy.count(), 3);
    model.stop();
    model.clearOpenError();
    QCOMPARE(model.openError(), original_error); QCOMPARE(error_spy.count(), 3);
}

// A missing log is a state of the log, not the outcome of one click, so
// openLogFile() must not record it as an action error: doing so would let the
// page clear it on re-entry while the file is still missing. The reader owns
// that message and keeps it for as long as the condition holds.
void DebugLogModelTests::openMissingFileIsLeftToTheReader()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const fs::path missing = fs::PathFromString(temp_dir.filePath("does-not-exist.log").toStdString());

    DebugLogModel model(missing);
    QSignalSpy error_spy(&model, &DebugLogModel::openErrorChanged);

    QVERIFY(!model.openLogFile());
    QVERIFY(model.openError().isEmpty());
    QCOMPARE(error_spy.count(), 0);

    // The reader reports it instead, and clearing on page entry does not drop
    // it, because the log is still missing.
    model.setActive(true);
    QTRY_VERIFY(!model.openError().isEmpty());
    model.clearOpenError();
    QVERIFY(!model.openError().isEmpty());
}

// logAvailable drives the controls that only make sense against readable log
// content, so it has to follow the reader in both directions.
void DebugLogModelTests::logAvailableTracksReadFailures()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("debug.log");
    const fs::path log_path = fs::PathFromString(path.toStdString());

    DebugLogModel model(log_path);
    QSignalSpy available_spy(&model, &DebugLogModel::logAvailableChanged);

    // Availability is a cached fact; no GUI-side filesystem preflight.
    QVERIFY(!model.logAvailable());

    model.setActive(true);
    QTRY_VERIFY(!model.busy());
    QVERIFY(!model.logAvailable());
    QCOMPARE(available_spy.count(), 0);

    // The log appears and a refresh picks it up.
    QVERIFY(WriteBytes(path, NumberedRecords(0, 2)));
    model.refresh(true);
    QTRY_VERIFY(model.logAvailable());
    QCOMPARE(available_spy.count(), 1);
    QVERIFY(model.openError().isEmpty());
}

// With -nodebuglogfile the node writes no log, so a file at the path is left
// over from an earlier run: the model must not show it or hand it out, and it
// says why the log is unavailable instead of reporting a missing file.
void DebugLogModelTests::fileLoggingOff_readsNothingAndSaysWhy()
{
    QTemporaryDir temp_dir;
    QVERIFY(temp_dir.isValid());
    const QString path = temp_dir.filePath("debug.log");
    QVERIFY(WriteBytes(path, NumberedRecords(0, 3)));

    DebugLogModel model(fs::PathFromString(path.toStdString()), /*log_to_file=*/false);
    int open_calls{0};
    model.setOpenLocalFileFnForTesting([&](const QString&) { ++open_calls; return true; });

    QVERIFY(!model.logAvailable());
    QCOMPARE(model.openError(), QStringLiteral("Writing the debug log to a file is turned off."));

    model.setActive(true);
    model.refresh(/*full_load=*/true);
    QTest::qWait(100);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.logAvailable());

    QVERIFY(!model.openLogFile());
    QCOMPARE(open_calls, 0);
    QCOMPARE(model.openError(), QStringLiteral("Writing the debug log to a file is turned off."));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(DebugLogModelTests)
#else
QTEST_MAIN(DebugLogModelTests)
#endif
#include <test_debuglogmodel.moc>
