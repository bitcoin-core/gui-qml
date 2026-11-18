// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/executor.h>
#include <qml/models/backuppolicyoperation.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QEventLoop>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>
#include <stdexcept>

namespace {
// Exercise a real worker while the GUI processes events. Use Qt's integer
// timer API so this contract also links with the CI instrumented Qt build.
bool WaitFor(const std::function<bool()>& ready)
{
    if (ready()) return true;
    QEventLoop loop; QTimer poll, deadline;
    QObject::connect(&poll, &QTimer::timeout, &loop, [&] { if (ready()) loop.quit(); });
    QObject::connect(&deadline, &QTimer::timeout, &loop, &QEventLoop::quit);
    deadline.setSingleShot(true); deadline.start(5000); poll.start(1); loop.exec();
    return ready();
}
struct Fixture {
    ManualExecutionEnvironment environment;
    int inspected{0}, written{0};
    backup::Proposal proposal{"/final.bak", "hash", true};
    backup::WriteResult result;
    std::vector<backup::Snapshot> outcomes;
    backup::Services services()
    {
        backup::Services ports;
        ports.scheduler = environment.scheduler;
        ports.post = [this](auto publication) { environment.post(std::move(publication)); };
        ports.wallet_name = [] { return "test-wallet"; };
        ports.inspect = [this](const auto& raw, const auto& name) {
            if (raw.empty() || name != "test-wallet") throw std::runtime_error("invalid command");
            ++inspected; return proposal;
        };
        ports.write = [this](const backup::Proposal& approved, bool replace) {
            if (approved.path != proposal.path || replace != (proposal.exists || proposal.renamed)) throw std::runtime_error("unapproved write");
            ++written; return result;
        };
        ports.outcome = [this](backup::Snapshot outcome) { outcomes.push_back(std::move(outcome)); };
        return ports;
    }
    void drain() { environment.drain(); environment.publishAll(); }
};
} // namespace

class BackupPolicyOperationTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void nativeOpeningCloseDoesNotWaitForGuiPublication_data()
    {
        QTest::addColumn<int>("moment");
        QTest::newRow("before-inspection") << 0;
        QTest::newRow("inside-inspection") << 1;
        QTest::newRow("awaiting-confirmation") << 2;
        QTest::newRow("approval-queued") << 3;
        QTest::newRow("before-write-admission") << 4;
        QTest::newRow("after-write-admission-retains-actual-result") << 5;
    }
    void nativeOpeningCloseDoesNotWaitForGuiPublication()
    {
        QFETCH(int, moment);
        Fixture f;
        bool available{moment != 0};
        auto ports = f.services();
        ports.available = [&] { return available; };
        if (moment == 1) ports.inspect = [&](const auto&, const auto&) { ++f.inspected; available = false; return f.proposal; };
        if (moment == 4 || moment == 5) f.proposal.exists = false;
        int publications{0};
        if (moment == 4) ports.post = [&](auto publication) {
            f.environment.post(std::move(publication));
            if (++publications == 2) available = false;
        };
        if (moment == 5) ports.write = [&](const auto&, bool) { ++f.written; available = false; return backup::WriteResult{}; };
        BackupFileOperation operation{"/selected", ports};
        QVERIFY(operation.start());
        f.environment.drain(); // Hold all GUI notifications while Native observes closure.
        if (moment == 2 || moment == 3) {
            if (moment == 2) available = false;
            QVERIFY(operation.confirm());
            available = false;
            f.environment.drain();
        }
        QCOMPARE(f.written, moment == 5 ? 1 : 0);
        QCOMPARE(f.outcomes.size(), size_t{1});
        QCOMPARE(f.outcomes.front().phase, moment == 5 ? backup::Phase::Succeeded : backup::Phase::Retired);
        f.environment.publishAll();
        QCOMPARE(operation.phase(), moment == 5 ? BackupFileOperation::Phase::Succeeded : BackupFileOperation::Phase::Retired);
    }
    void refusalHasNoWriteEffect()
    {
        Fixture f; BackupFileOperation operation{"/selected.v1", f.services()};
        QVERIFY(!operation.confirm()); QVERIFY(!operation.cancel());
        QVERIFY(operation.start()); QVERIFY(!operation.start()); QCOMPARE(f.inspected, 0); QVERIFY(operation.busy());
        f.drain(); QVERIFY(operation.confirmationRequired()); QVERIFY(operation.replacing());
        QCOMPARE(operation.finalPath(), QString("/final.bak")); QCOMPARE(f.inspected, 1); QCOMPARE(f.written, 0);
        QVERIFY(operation.cancel()); f.drain(); QCOMPARE(operation.phase(), BackupFileOperation::Phase::Declined);
        QCOMPARE(f.written, 0); QCOMPARE(f.outcomes.size(), size_t{1}); QVERIFY(operation.error().isEmpty());
        QVERIFY(operation.confirm()); f.drain(); QCOMPARE(f.written, 0); QCOMPARE(f.outcomes.size(), size_t{1});
    }
    void approvalOwnsExactlyOneWrite_data()
    {
        QTest::addColumn<bool>("exists"); QTest::addColumn<bool>("renamed"); QTest::addColumn<int>("error");
        for (int existing = 0; existing < 2; ++existing) for (int renamed = 0; renamed < 2; ++renamed)
            for (int error = 0; error <= int(backup::Error::Transport) + 1; ++error)
                QTest::newRow(qPrintable(QString("exists-%1-renamed-%2-error-%3").arg(existing).arg(renamed).arg(error))) << bool(existing) << bool(renamed) << error;
    }
    void approvalOwnsExactlyOneWrite()
    {
        QFETCH(bool, exists); QFETCH(bool, renamed); QFETCH(int, error);
        Fixture f; f.proposal.exists = exists; f.proposal.renamed = renamed; f.result.error = backup::Error(error);
        BackupFileOperation operation{"/selected", f.services()}; QVERIFY(operation.start()); f.drain();
        if (exists || renamed) {
            QVERIFY(operation.confirmationRequired()); QCOMPARE(f.written, 0);
            QVERIFY(operation.confirm()); QVERIFY(operation.confirm()); QVERIFY(operation.cancel()); f.drain();
        }
        QCOMPARE(f.written, 1); QCOMPARE(f.outcomes.size(), size_t{1}); QVERIFY(!operation.busy());
        QCOMPARE(operation.phase(), error ? BackupFileOperation::Phase::Failed : BackupFileOperation::Phase::Succeeded);
        QCOMPARE(operation.error().isEmpty(), error == 0);
    }
    void retirementBeforeWrite_data()
    {
        QTest::addColumn<int>("moment");
        QTest::newRow("queued") << 0; QTest::newRow("inspecting") << 1;
        QTest::newRow("awaiting-confirmation") << 2; QTest::newRow("approval-queued") << 3;
        QTest::newRow("ready-before-write-admission") << 4;
    }
    void retirementBeforeWrite()
    {
        QFETCH(int, moment); Fixture f; auto ports = f.services(); BackupFileOperation* view{nullptr};
        int publications{0};
        if (moment == 4) {
            f.proposal.exists = false;
            ports.post = [&](auto publication) { f.environment.post(std::move(publication)); if (++publications == 2) view->retire(); };
        }
        if (moment == 1) ports.inspect = [&](const auto&, const auto&) { ++f.inspected; view->retire(); return f.proposal; };
        BackupFileOperation operation{"/selected", ports}; view = &operation;
        QVERIFY(operation.start());
        if (moment == 2 || moment == 3) { f.drain(); QVERIFY(operation.confirmationRequired()); }
        if (moment == 3) QVERIFY(operation.confirm());
        if (moment != 1 && moment != 4) operation.retire();
        f.drain(); QCOMPARE(operation.phase(), BackupFileOperation::Phase::Retired);
        QCOMPARE(f.written, 0); QCOMPARE(f.outcomes.size(), size_t{1});
        qInfo("Manual backup retirement schedule: %s", f.environment.trace().c_str());
        QVERIFY(!operation.confirm()); QVERIFY(!operation.cancel());
    }
    void vanishedObserverDoesNotEraseOutcome()
    {
        Fixture f; f.proposal.exists = false;
        auto operation = std::make_unique<BackupFileOperation>("/final.bak", f.services());
        QVERIFY(operation->start()); f.environment.drain(); QCOMPARE(f.written, 1);
        operation.reset(); f.drain(); QCOMPARE(f.outcomes.size(), size_t{1});
        QCOMPARE(f.outcomes.front().phase, backup::Phase::Succeeded); QVERIFY(f.environment.quiescent());
    }
    void reversedNotificationsCannotRestoreOldState()
    {
        Fixture f; f.proposal.exists = false; BackupFileOperation operation{"/final.bak", f.services()};
        QVERIFY(operation.start()); f.environment.drain(); QCOMPARE(f.written, 1);
        while (auto publication = f.environment.takePublicationBack()) {
            publication();
            QCOMPARE(operation.phase(), BackupFileOperation::Phase::Succeeded);
        }
        QCOMPARE(f.outcomes.size(), size_t{1});
    }
    void missingServices_data()
    {
        QTest::addColumn<int>("missing"); for (int i = 0; i < 5; ++i) QTest::newRow(qPrintable(QString("missing-%1").arg(i))) << i;
    }
    void missingServices()
    {
        QFETCH(int, missing); Fixture f; auto services = f.services();
        switch (missing) { case 0: services.scheduler.reset(); break; case 1: services.post = {}; break;
        case 2: services.wallet_name = {}; break; case 3: services.inspect = {}; break; case 4: services.write = {}; break; }
        BackupFileOperation operation{"/selected", services}; QVERIFY(!operation.start());
        QCOMPARE(operation.phase(), BackupFileOperation::Phase::Failed); QVERIFY(!operation.error().isEmpty());
        QCOMPARE(f.written, 0); QVERIFY(f.outcomes.empty());
    }
    void admissionFailureDoesNotClaimWrite()
    {
        Fixture f; f.environment.rejectNext(true); BackupFileOperation operation{"/selected", f.services()};
        QVERIFY(!operation.start()); QCOMPARE(operation.phase(), BackupFileOperation::Phase::Failed);
        QVERIFY(!operation.error().isEmpty()); f.drain(); QCOMPARE(f.written, 0);
    }
    void rejectedDecisionCanBeRetried_data()
    {
        QTest::addColumn<bool>("approve");
        QTest::newRow("approve") << true; QTest::newRow("refuse") << false;
    }
    void rejectedDecisionCanBeRetried()
    {
        QFETCH(bool, approve);
        Fixture f; BackupFileOperation operation{"/selected", f.services()}; QVERIFY(operation.start()); f.drain();
        f.environment.rejectNext(true); QVERIFY(!(approve ? operation.confirm() : operation.cancel())); f.drain();
        QVERIFY(operation.confirmationRequired()); QCOMPARE(f.written, 0);
        QVERIFY(approve ? operation.confirm() : operation.cancel()); f.drain(); QCOMPARE(f.written, approve ? 1 : 0);
    }
    void failedInspectionOrWriterSettlesOnce_data()
    {
        QTest::addColumn<int>("failure");
        QTest::newRow("inspection-error") << 0; QTest::newRow("inspection-throws") << 1;
        QTest::newRow("writer-throws") << 2; QTest::newRow("oversized-command") << 3;
        QTest::newRow("observer-transport-throws") << 4; QTest::newRow("no-observer") << 5;
    }
    void failedInspectionOrWriterSettlesOnce()
    {
        QFETCH(int, failure); Fixture f; f.proposal.exists = false; auto services = f.services();
        if (failure == 0) f.proposal.error = backup::Error::Inspection;
        if (failure == 1) services.inspect = [](const auto&, const auto&) -> backup::Proposal { throw std::runtime_error("inspection"); };
        if (failure == 2) services.write = [](const auto&, bool) -> backup::WriteResult { throw std::runtime_error("write"); };
        if (failure == 4) services.post = [](auto) { throw std::runtime_error("observer transport"); };
        if (failure == 5) services.outcome = {};
        BackupFileOperation operation{failure == 3 ? QString(32'769, 'x') : QString("/selected"), services};
        QVERIFY(operation.start()); f.drain();
        QCOMPARE(f.outcomes.size(), failure == 5 ? size_t{0} : size_t{1});
        if (failure < 4) { QCOMPARE(operation.phase(), BackupFileOperation::Phase::Failed); QVERIFY(!operation.error().isEmpty()); }
        else if (failure == 4) { QCOMPARE(f.written, 1); QCOMPARE(f.outcomes.front().phase, backup::Phase::Succeeded); }
        else QCOMPARE(operation.phase(), BackupFileOperation::Phase::Succeeded);
    }
    void realWorkerRetainsAdmittedWriteAfterViewDestruction()
    {
        execution::Executor executor{execution::Executor::Config{}};
        const auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(execution::Lane::Core));
        QObject gui;
        QSemaphore entered, release; std::atomic<int> consumed{0}; std::atomic<bool> off_gui{true};
        const auto finish = qScopeGuard([&] { release.release(); executor.stopAndJoin(); });
        backup::Services services;
        services.post = [&gui](auto publication) { QMetaObject::invokeMethod(&gui, std::move(publication), Qt::QueuedConnection); };
        services.wallet_name = [&] { off_gui = off_gui && QThread::currentThread() != qApp->thread(); return "wallet"; };
        services.inspect = [&](const auto&, const auto&) { off_gui = off_gui && QThread::currentThread() != qApp->thread(); return backup::Proposal{"/final.bak", {}}; };
        services.write = [&](const auto&, bool) { off_gui = off_gui && QThread::currentThread() != qApp->thread(); entered.release(); release.acquire(); return backup::WriteResult{}; };
        services.submit = [core, writer = services.write](backup::Services::Work work) {
            const bool accepted = core->post([writer, work = std::move(work)] { work(writer); });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        services.outcome = [&](backup::Snapshot result) { off_gui = off_gui && QThread::currentThread() != qApp->thread(); consumed = result.phase == backup::Phase::Succeeded ? 1 : -1; };
        auto operation = std::make_unique<BackupFileOperation>("/final.bak", services); QVERIFY(operation->start());
        QVERIFY(WaitFor([&] { return entered.available() > 0; }));
        operation.reset(); QCOMPARE(consumed.load(), 0); release.release();
        QVERIFY(WaitFor([&] { return consumed.load() == 1; }));
        executor.stopAndJoin();
        QVERIFY(off_gui.load());
    }
};

BITCOINQML_REGISTER_QT_TEST(BackupPolicyOperationTests)
#include <test_backuppolicyoperation.moc>
