// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/backendexecutor.h>
#include <qml/models/activityexportoperation.h>
#include <qml/models/activityfilterproxymodel.h>
#include <qml/models/transactionactivitymodel.h>
#include <qml/models/transaction.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QFile>
#include <QSemaphore>
#include <QSaveFile>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <deque>
#include <mutex>
#include <stdexcept>

namespace {
QByteArray Read(const QString& path) { QFile file{path}; return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool Put(const QString& path, const QByteArray& bytes) { QFile file{path}; return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size(); }
std::shared_ptr<const activityview::Hold> Source()
{
    using A = TransactionActivityModel;
    auto frame = std::make_shared<activityview::Snapshot>(); frame->revision = 71;
    auto row = std::make_shared<const activityview::Row>(activityview::Row{
        {A::IdRole, "tx:original"}, {A::TxidRole, "original"}, {A::TimestampRole, qint64{1000}}, {A::LabelRole, "Original, \"note\"\nnext"},
        {A::AddressRole, "destination"}, {A::NetAmountSatRole, qint64{123'456'789}}, {A::StatusRole, int(Transaction::Confirmed)},
        {A::StatusKnownRole, true}, {A::DepthRole, 1}, {A::IsPendingRequestRole, false}, {A::ActivityTypeRole, int(A::Receive)}});
    frame->rows.emplace("tx:original", row); frame->presentation = activityview::Insert({}, "tx:original", row);
    auto hold = std::make_shared<activityview::Hold>(); hold->snapshot = std::move(frame); return hold;
}
struct Fixture {
    ManualRoutedExecution commands;
    std::deque<std::function<void()>> gui;
    std::vector<activityexport::Receipt> outcomes;
    int inspected{0}, installed{0};
    explicit Fixture(size_t slots = 1) : commands(execution::Options{}, slots) {}
    activityexport::Files files()
    {
        auto coordinator = std::make_shared<backup::InstallCoordinator>(); activityexport::Files result;
        result.inspect = [this](const QString& path) { ++inspected; const auto bytes = path.toUtf8(); return backup::Inspect({bytes.constData(), size_t(bytes.size())}, "activity", ".csv"); };
        result.install = [this, coordinator](const backup::Proposal& target, bool approved, const auto& producer) { ++installed; return backup::Write(target, approved, producer, *coordinator); };
        result.available = [] { return true; }; result.outcome = [this](const auto& receipt) { outcomes.push_back(receipt); }; return result;
    }
    execution::SnapshotServices execution() { return {commands.scheduler, [this](auto callback) { gui.push_back(std::move(callback)); }}; }
    void publish() { while (!gui.empty()) { auto callback = std::move(gui.front()); gui.pop_front(); callback(); } }
};
}
class ActivityExportOperationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void originalSnapshotUnitAndTerminalOutcomeAreOwnedBeforeGuiDelivery()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f; const auto path = directory.filePath("original.csv");
        auto source = Source(); const std::weak_ptr<const activityview::Snapshot> original = source->snapshot;
        ActivityExportOperation operation{{path, source, 0}, f.files(), f.execution(), ActivityFilterProxyModel::WriteCsv};
        source.reset(); QVERIFY(operation.start()); QVERIFY(operation.pending()); QCOMPARE(f.installed, 0);
        f.commands.drain(); QCOMPARE(f.outcomes.size(), size_t{1}); QVERIFY(f.outcomes.front().written); QVERIFY(f.outcomes.front().outcome_known);
        QCOMPARE(f.outcomes.front().operation, operation.operationId()); QCOMPARE(f.outcomes.front().snapshot_revision, QStringLiteral("71")); QCOMPARE(f.outcomes.front().unit, 0);
        QVERIFY(original.expired()); QVERIFY(operation.pending()); // Native cleanup/outcome does not wait for the observer.
        const auto bytes = Read(path); QVERIFY(bytes.contains("Amount (BTC)")); QVERIFY(bytes.contains("1.23456789")); QVERIFY(bytes.contains("\"Original, \"\"note\"\"\nnext\""));
        f.publish(); QVERIFY(operation.saved()); QVERIFY(operation.resultPresent()); QCOMPARE(operation.finalPath(), path); QVERIFY(operation.error().isEmpty());
    }
    void existingDestinationNeedsExactVersionConsentAndRetainsTheOriginalFrame()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f; const auto path = directory.filePath("original.csv"); QVERIFY(Put(path, "old"));
        ActivityExportOperation operation{{path, Source(), 3}, f.files(), f.execution(), ActivityFilterProxyModel::WriteCsv};
        QVERIFY(operation.start()); f.commands.drain(); f.publish(); QVERIFY(operation.confirmationRequired()); QVERIFY(operation.replacing()); QCOMPARE(f.installed, 0); QCOMPARE(Read(path), QByteArray{"old"});
        QCOMPARE(operation.snapshotRevision(), QStringLiteral("71")); QCOMPARE(operation.displayUnit(), 3); QVERIFY(operation.approve()); f.commands.drain(); f.publish();
        QVERIFY(operation.saved()); QCOMPARE(f.installed, 1); QVERIFY(Read(path).contains("123456789")); QCOMPARE(f.outcomes.size(), size_t{1}); QVERIFY(f.outcomes.front().written);
    }
    void changedDestinationCannotUseOldConsent()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f; const auto path = directory.filePath("original.csv"); QVERIFY(Put(path, "old"));
        ActivityExportOperation operation{{path, Source(), 0}, f.files(), f.execution(), ActivityFilterProxyModel::WriteCsv};
        QVERIFY(operation.start()); f.commands.drain(); f.publish(); QVERIFY(operation.confirmationRequired()); QVERIFY(Put(path, "changed independently"));
        QVERIFY(operation.approve()); f.commands.drain(); f.publish(); QVERIFY(!operation.saved()); QVERIFY(operation.resultPresent()); QVERIFY(operation.outcomeKnown());
        QCOMPARE(Read(path), QByteArray{"changed independently"}); QCOMPARE(f.outcomes.size(), size_t{1}); QCOMPARE(f.outcomes.front().error, backup::Error::Changed);
    }
    void rewrittenSuffixRequiresConsentToTheActualFinalPath()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f; const auto requested = directory.filePath("original.txt");
        ActivityExportOperation operation{{requested, Source(), 0}, f.files(), f.execution(), ActivityFilterProxyModel::WriteCsv};
        QVERIFY(operation.start()); f.commands.drain(); f.publish(); QVERIFY(operation.confirmationRequired()); QVERIFY(!operation.replacing());
        QVERIFY(operation.finalPath().endsWith(".csv")); QVERIFY(operation.finalPath() != requested); QVERIFY(!QFile::exists(requested)); QCOMPARE(f.installed, 0);
        QVERIFY(operation.approve()); f.commands.drain(); f.publish(); QVERIFY(operation.saved()); QVERIFY(!QFile::exists(requested)); QVERIFY(!Read(operation.finalPath()).isEmpty());
    }
    void refusalAndRetirementBeforeAdmissionNeverWrite()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f; const auto path = directory.filePath("original.csv"); QVERIFY(Put(path, "old"));
        auto operation = std::make_unique<ActivityExportOperation>(activityexport::Request{path, Source(), 0}, f.files(), f.execution(), ActivityFilterProxyModel::WriteCsv);
        QVERIFY(operation->start()); f.commands.drain(); f.publish(); QVERIFY(operation->confirmationRequired()); QVERIFY(operation->refuse()); f.commands.drain(); f.publish();
        QCOMPARE(operation->phase(), BackupFileOperation::Phase::Declined); QVERIFY(operation->cancelled()); QCOMPARE(f.installed, 0); QCOMPARE(Read(path), QByteArray{"old"});
        operation = std::make_unique<ActivityExportOperation>(activityexport::Request{directory.filePath("unstarted.csv"), Source(), 0}, f.files(), f.execution(), ActivityFilterProxyModel::WriteCsv);
        QVERIFY(operation->start()); operation.reset(); f.commands.drain(); f.publish(); QCOMPARE(f.installed, 0); QVERIFY(!QFile::exists(directory.filePath("unstarted.csv")));
        Fixture closing;
        auto source = Source(); const std::weak_ptr<const activityview::Snapshot> frame{source->snapshot};
        ActivityExportOperation retained{{path, source, 0}, closing.files(), closing.execution(), ActivityFilterProxyModel::WriteCsv};
        source.reset(); QVERIFY(retained.start()); closing.commands.drain(); closing.publish(); QVERIFY(retained.confirmationRequired());
        closing.commands.scheduler->closeOrdinaryAdmission(); retained.retire(); QVERIFY(!frame.expired());
        closing.commands.drain(); closing.publish(); QVERIFY(frame.expired()); QCOMPARE(closing.commands.scheduler->pending(), size_t{0});
        QVERIFY(!retained.approve()); QCOMPARE(retained.phase(), BackupFileOperation::Phase::Retired);
        QVERIFY(retained.resultPresent()); QVERIFY(retained.cancelled()); QVERIFY(retained.outcomeKnown());
        QCOMPARE(closing.installed, 0); QCOMPARE(Read(path), QByteArray{"old"});
    }
    void failedProducerPreservesTheOriginalDestination()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f; const auto path = directory.filePath("original.csv"); QVERIFY(Put(path, "original bytes"));
        int produced{0}; ActivityExportOperation operation{{path, Source(), 0}, f.files(), f.execution(), [&](const auto& request) {
            ++produced; if (!Put(request.path, "partial staging")) return activityexport::Result{false, request.path, "staging fixture failure"};
            return activityexport::Result{false, request.path, "controlled output failure"};
        }};
        QVERIFY(operation.start()); f.commands.drain(); f.publish(); QVERIFY(operation.confirmationRequired()); QVERIFY(operation.approve()); f.commands.drain(); f.publish();
        QCOMPARE(produced, 1); QVERIFY(!operation.saved()); QVERIFY(operation.outcomeKnown()); QCOMPARE(Read(path), QByteArray{"original bytes"}); QCOMPARE(f.outcomes.front().error, backup::Error::CoreBackup);
    }
    void streamWriteFailureAfterSuccessfulOpenNeverCommitsOrTruncatesTheOriginal()
    {
        struct Failure : QSaveFile {
            bool& opened;
            Failure(const QString& path, bool& did_open) : QSaveFile(path), opened(did_open) {}
            bool open(OpenMode mode) override { opened = QSaveFile::open(mode); return opened; }
            qint64 writeData(const char*, qint64) override { return -1; }
        };
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("stream.csv"); QVERIFY(Put(path, "original"));
        auto request = activityexport::Request{path, Source(), 0}; bool opened{false};
        request.destination = [&](const QString& target) {
            return std::make_unique<Failure>(target, opened);
        };
        const auto result = ActivityFilterProxyModel::WriteCsv(request);
        QVERIFY(!result.saved); QVERIFY(opened); QCOMPARE(Read(path), QByteArray{"original"}); QVERIFY(!result.error.isEmpty());
        request.destination = {}; const auto control = ActivityFilterProxyModel::WriteCsv(request); QVERIFY(control.saved); QVERIFY(Read(path) != QByteArray{"original"});
    }
    void unknownNativeOutcomeCannotBeReportedAsKnownFailureOrRetriedImplicitly()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f; const auto path = directory.filePath("actual.csv"); auto files = f.files();
        files.install = [&](const backup::Proposal&, bool, const auto&) -> backup::WriteResult { ++f.installed; if (!Put(path, "physically installed")) return {backup::Error::Install}; throw std::runtime_error("late transport fault"); };
        ActivityExportOperation operation{{path, Source(), 0}, files, f.execution(), ActivityFilterProxyModel::WriteCsv}; QVERIFY(operation.start()); f.commands.drain(); f.publish();
        QVERIFY(operation.resultPresent()); QVERIFY(!operation.saved()); QVERIFY(!operation.outcomeKnown()); QCOMPARE(operation.errorCode(), ActivityExportOperation::OutcomeUnknown); QCOMPARE(f.installed, 1); QCOMPARE(Read(path), QByteArray{"physically installed"}); QCOMPARE(f.outcomes.size(), size_t{1}); QVERIFY(!f.outcomes.front().outcome_known);
        QVERIFY(!operation.start()); f.commands.drain(); QCOMPARE(f.installed, 1);
    }
    void closedOrdinaryAdmissionRefusesANewExportWithoutAFileEffect()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f;
        const auto cleanup = qScopeGuard([&] { f.commands.drain(); f.publish(); });
        auto source = Source(); const std::weak_ptr<const activityview::Snapshot> frame{source->snapshot};
        ActivityExportOperation operation{{directory.filePath("closed.csv"), source, 0}, f.files(), f.execution(), ActivityFilterProxyModel::WriteCsv};
        source.reset(); f.commands.scheduler->closeOrdinaryAdmission(); QVERIFY(!operation.start());
        QVERIFY(!frame.expired()); // Rejection cannot free the original large value on GUI.
        f.commands.drain(); f.publish(); QCOMPARE(f.inspected, 0); QCOMPARE(f.installed, 0); QVERIFY(!operation.saved()); QVERIFY(operation.resultPresent());
        QVERIFY2(frame.expired(), "Rejected CSV frame must retire on Native while the failed projection remains alive");
        QCOMPARE(f.commands.scheduler->pending(), size_t{0}); QCOMPARE(operation.snapshotRevision(), QStringLiteral("71"));
    }
    void competingConsentCommandsHaveOneOrderedNativeOutcome_data()
    {
        QTest::addColumn<bool>("approve_first"); QTest::newRow("approval-admitted-first") << true; QTest::newRow("refusal-admitted-first") << false;
    }
    void competingConsentCommandsHaveOneOrderedNativeOutcome()
    {
        QFETCH(bool, approve_first); QTemporaryDir directory; QVERIFY(directory.isValid()); Fixture f{2}; const auto path = directory.filePath("ordered.csv"); QVERIFY(Put(path, "original"));
        const auto cleanup = qScopeGuard([&] {
            do { f.commands.drain(); f.publish(); } while (!f.commands.queuedIdle() || !f.commands.activeIdle());
            QVERIFY(f.commands.quiescent());
            QCOMPARE(f.outcomes.size(), size_t{1}); QCOMPARE(f.installed, approve_first ? 1 : 0);
            if (!approve_first) QCOMPARE(Read(path), QByteArray{"original"});
        });
        ActivityExportOperation operation{{path, Source(), 0}, f.files(), f.execution(), ActivityFilterProxyModel::WriteCsv};
        QVERIFY(operation.start()); f.commands.drain(); f.publish(); QVERIFY(operation.confirmationRequired());
        if (approve_first) { QVERIFY(operation.approve()); QVERIFY(operation.refuse()); }
        else { QVERIFY(operation.refuse()); QVERIFY(operation.approve()); }
        const auto first = f.commands.dispatchNext(execution::Lane::Core); QVERIFY(first);
        const auto concurrent = f.commands.dispatchNext(execution::Lane::Core);
        const bool first_finished = f.commands.finishWork(*first);
        const bool second_finished = !concurrent || f.commands.finishWork(*concurrent);
        f.commands.drain(); f.publish(); QVERIFY(first_finished && second_finished);
        QVERIFY(!concurrent); // A two-slot scheduler must still serialize this operation's state writer.
        QCOMPARE(f.outcomes.size(), size_t{1}); QCOMPARE(f.installed, approve_first ? 1 : 0); QCOMPARE(operation.saved(), approve_first);
        if (!approve_first) QCOMPARE(Read(path), QByteArray{"original"});
        f.commands.drain(); f.publish(); QCOMPARE(f.outcomes.size(), size_t{1});
    }
    void realWorkerWriteSurvivesObserverLossAndDoesNotBlockGuiOrNativeOutcome()
    {
        QTemporaryDir directory; QVERIFY(directory.isValid()); const auto path = directory.filePath("worker.csv"); BackendExecutor backend;
        QSemaphore entered, release; std::atomic<int> outcomes{0}; std::atomic<bool> off_gui{false}; std::mutex mutex; std::deque<std::function<void()>> gui;
        activityexport::Files files; const auto coordinator = std::make_shared<backup::InstallCoordinator>();
        files.inspect = [](const QString& raw) { const auto bytes = raw.toUtf8(); return backup::Inspect({bytes.constData(), size_t(bytes.size())}, "activity", ".csv"); };
        files.install = [&](const auto& target, bool approved, const auto& producer) { off_gui = QThread::currentThread() != qApp->thread(); entered.release(); release.acquire(); return backup::Write(target, approved, producer, *coordinator); };
        files.outcome = [&](const auto& receipt) { if (receipt.written) ++outcomes; };
        execution::SnapshotServices execution{backend.scheduler(), [&](auto callback) { std::lock_guard lock{mutex}; gui.push_back(std::move(callback)); }};
        auto operation = std::make_unique<ActivityExportOperation>(activityexport::Request{path, Source(), 0}, files, execution, ActivityFilterProxyModel::WriteCsv);
        QVERIFY(operation->start()); const bool writing = entered.tryAcquire(1, 5000); if (!writing) release.release(); QVERIFY(writing);
        bool heartbeat{false}; QMetaObject::invokeMethod(qApp, [&] { heartbeat = true; }, Qt::QueuedConnection); QTRY_VERIFY(heartbeat);
        operation.reset(); release.release(); QTRY_COMPARE(outcomes.load(), 1); QVERIFY(off_gui.load()); QVERIFY(!Read(path).isEmpty());
        backend.shutdown(); QTRY_VERIFY(backend.isDrained());
        { std::lock_guard lock{mutex}; gui.clear(); }
    }
};
BITCOINQML_REGISTER_QT_TEST(ActivityExportOperationTests)
#include <test_activityexportoperation.moc>
