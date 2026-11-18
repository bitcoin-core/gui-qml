// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/fileexportoperation.h>
#include <qml/backendexecutor.h>
#include <qml/psbtwork.h>
#include <chainparams.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/psbtfeefixture.h>
#include <test/qt_test_registry.h>

#include <QFile>
#include <QPointer>
#include <QTemporaryDir>
#include <QThread>
#include <QtTest/QtTest>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <utility>

namespace {
struct Fixture {
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui_posts;

    Fixture() = default;

    ~Fixture()
    {
        // Model destruction and its last GUI publication can release retained
        // values after the test's final explicit drain. Complete their Cleanup
        // turns while the manual transport is still owned by this fixture.
        for (int turns = 0; turns < 4096; ++turns) {
            execution.drain();
            deliverGui();
            if (execution.transportIdle() && gui_posts.empty()) break;
        }
        QVERIFY(gui_posts.empty());
        QVERIFY(execution.quiescent());
    }

    backup::Services services()
    {
        backup::Services result;
        result.scheduler = execution.scheduler;
        result.post = [this](std::function<void()> work) { gui_posts.push_back(std::move(work)); };
        result.coordinator = std::make_shared<backup::InstallCoordinator>();
        return result;
    }
    void deliverGui()
    {
        while (!gui_posts.empty()) {
            auto work = std::move(gui_posts.front());
            gui_posts.pop_front();
            work();
        }
    }
};
void UseSerialPromptRetirement(backup::Services& services, std::function<void()> after_first_admission = {})
{
    const auto scheduler = services.scheduler;
    const auto action_key = scheduler->allocateKey();
    auto after_admission = std::make_shared<std::function<void()>>(std::move(after_first_admission));
    services.submit = [scheduler, action_key, after_admission](std::function<void()> work) {
        auto submitted = execution::Submit(*scheduler, std::move(work), [] {}, {},
            {execution::Lane::Core, action_key, execution::WorkClass::Action});
        // Model an accepted enqueue whose caller is synchronously closed
        // before submit() returns the admission receipt.
        if (submitted && *after_admission) {
            auto hook = std::exchange(*after_admission, {});
            hook();
        }
        return submitted;
    };
    services.reserve_prompt_retirement = [scheduler, action_key](std::function<void()> work) {
        return execution::Submit(*scheduler, std::move(work), [] {}, {},
            {execution::Lane::Cleanup, action_key, execution::WorkClass::Action}, true);
    };
    services.activate_prompt_retirement = [scheduler](execution::TaskId id) {
        return scheduler->activateCleanup(id);
    };
}
struct DeletionState {
    std::atomic<bool> destroyed{false};
    std::atomic<bool> destroyed_on_gui{false};
};
std::shared_ptr<const PartiallySignedTransaction> TrackedDocument(const std::shared_ptr<DeletionState>& state)
{
    return {new PartiallySignedTransaction{psbtfeefixture::Legacy(2, false)}, [state](const PartiallySignedTransaction* value) {
        state->destroyed_on_gui = QThread::currentThread() == qApp->thread();
        delete value;
        state->destroyed = true;
    }};
}
QByteArray ReadFile(const QString& path)
{
    QFile file{path};
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
}

class FileExportOperationTests : public QObject
{
    Q_OBJECT
    std::unique_ptr<ECC_Context> m_ecc;
private Q_SLOTS:
    void initTestCase()
    {
        SelectParams(ChainType::MAIN);
        m_ecc = std::make_unique<ECC_Context>();
    }

    void exactCapturedPsbtBytesUseApprovedFinalPath()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("captured.psbt"));
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        const QByteArray expected = psbtwork::SerializeRaw(*document);
        QVERIFY(!expected.isEmpty());
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        QVERIFY(operation.busy());
        QVERIFY(!QFile::exists(path));
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(operation.saved());
        QVERIFY(!operation.busy());
        QCOMPARE(operation.finalPath(), path);
        QCOMPARE(ReadFile(path), expected);
        QVERIFY(operation.error().isEmpty());
    }

    void realWorkerExportsCapturedPsbtOffGui()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("worker.psbt"));
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        const QByteArray expected = psbtwork::SerializeRaw(*document);
        QVERIFY(!expected.isEmpty());

        BackendExecutor executor;
        QObject gui;
        std::atomic<bool> off_gui{true};
        std::atomic<int> outcomes{0};
        std::atomic<int> outcome_phase{-1};
        backup::Services services;
        services.scheduler = executor.scheduler();
        services.post = [&gui](auto publication) {
            if (!QMetaObject::invokeMethod(&gui, std::move(publication), Qt::QueuedConnection)) {
                throw std::runtime_error("GUI publication could not be queued");
            }
        };
        services.available = [&] {
            if (QThread::currentThread() == qApp->thread()) off_gui = false;
            return true;
        };
        services.outcome = [&](backup::Snapshot result) {
            if (QThread::currentThread() == qApp->thread()) off_gui = false;
            outcome_phase = static_cast<int>(result.phase);
            ++outcomes;
        };
        FileExportOperationModel operation{path, document, std::move(services)};

        QVERIFY(operation.start());
        QTRY_VERIFY_WITH_TIMEOUT(operation.saved(), 5'000);
        QCOMPARE(ReadFile(path), expected);
        QCOMPARE(operation.finalPath(), path);
        QVERIFY(operation.error().isEmpty());
        QCOMPARE(outcomes.load(), 1);
        QCOMPARE(outcome_phase.load(), static_cast<int>(backup::Phase::Succeeded));
        QVERIFY(off_gui.load());

        executor.shutdown();
        QTRY_VERIFY(executor.isDrained());
    }

    void startAdmissionCanDeleteProjection_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("accepted-original-write") << true;
        QTest::newRow("rejected-without-write") << false;
    }

    void startAdmissionCanDeleteProjection()
    {
        QFETCH(bool, accept);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("captured.psbt"));
        auto deletion = std::make_shared<DeletionState>();
        auto document = TrackedDocument(deletion);
        const auto expected = psbtwork::SerializeRaw(*document);
        int outcomes{0};
        int terminal_phase{-1};
        Fixture fixture;
        std::unique_ptr<FileExportOperationModel> operation;
        auto services = fixture.services();
        const auto scheduler = services.scheduler;
        const auto key = scheduler->allocateKey();
        services.submit = [scheduler, key, accept, &operation](std::function<void()> work) {
            const auto receipt = accept
                ? execution::Submit(*scheduler, std::move(work), [] {}, {},
                    {execution::Lane::Core, key, execution::WorkClass::Action})
                : execution::SubmitResult{{}, execution::Rejection::Transport};
            // Admission may close the public projection before returning its
            // receipt. Its original accepted write and cleanup remain owned.
            operation.reset();
            return receipt;
        };
        services.outcome = [&](backup::Snapshot snapshot) {
            ++outcomes;
            terminal_phase = static_cast<int>(snapshot.phase);
        };
        operation = std::make_unique<FileExportOperationModel>(path, document, std::move(services));
        document.reset();
        const QPointer<FileExportOperationModel> original{operation.get()};
        auto request = operation->requestForAcceptedContinuation();
        QCOMPARE(original->start(), accept);
        QVERIFY(!original);
        fixture.execution.drain();
        fixture.deliverGui();
        QCOMPARE(QFile::exists(path), accept);
        QCOMPARE(ReadFile(path), accept ? expected : QByteArray{});
        QCOMPARE(outcomes, accept ? 1 : 0);
        if (accept) QCOMPARE(terminal_phase, static_cast<int>(backup::Phase::Succeeded));
        request.reset();
        fixture.execution.drain();
        QVERIFY(deletion->destroyed.load());
        QVERIFY(fixture.execution.quiescent());
    }

    void retiringBeforeInspectionReleasesPsbtThroughCleanup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("retired.psbt"));
        auto state = std::make_shared<DeletionState>();
        auto document = TrackedDocument(state);
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};
        document.reset();

        QVERIFY(operation.start());
        QVERIFY(operation.busy());
        operation.retire();
        QVERIFY(!state->destroyed);

        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(!operation.busy());
        QVERIFY(!QFile::exists(path));
        QVERIFY(!state->destroyed);
        QCOMPARE(fixture.execution.queuedCountIf([](const auto& turn) {
            return turn.lane == execution::Lane::Cleanup && turn.phase == execution::Phase::Queued;
        }), 1);
        QVERIFY(fixture.execution.step(execution::Lane::Cleanup));
        QVERIFY(state->destroyed);
        fixture.execution.drain();
        QVERIFY(fixture.execution.quiescent());
    }

    void refusalReleasesPsbtThroughCleanup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"keep the existing file"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        auto state = std::make_shared<DeletionState>();
        auto document = TrackedDocument(state);
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};
        document.reset();

        QVERIFY(operation.start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation.confirmationRequired());
        QVERIFY(!state->destroyed);
        QVERIFY(operation.cancel());
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(!operation.busy());
        QVERIFY(operation.error().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
        QCOMPARE(ReadFile(path), original);
        QVERIFY(!state->destroyed);
        QCOMPARE(fixture.execution.queuedCountIf([](const auto& turn) {
            return turn.lane == execution::Lane::Cleanup && turn.phase == execution::Phase::Queued;
        }), 1);
        QVERIFY(fixture.execution.step(execution::Lane::Cleanup));
        QVERIFY(state->destroyed);
        fixture.execution.drain();
        QVERIFY(fixture.execution.quiescent());
    }

    void rejectedStartAdmissionReleasesPsbtThroughCleanup()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        auto state = std::make_shared<DeletionState>();
        auto document = TrackedDocument(state);
        Fixture fixture;
        FileExportOperationModel operation{directory.filePath(QStringLiteral("rejected.psbt")), document, fixture.services()};
        document.reset();
        fixture.execution.rejectLane(execution::Lane::Core);

        QVERIFY(!operation.start());
        QVERIFY(!operation.busy());
        QCOMPARE(operation.error(), QStringLiteral("The PSBT could not be saved. Try again."));
        QVERIFY(!state->destroyed);
        // The rejected request releases its retained PSBT through Cleanup.
        QCOMPARE(fixture.execution.queuedCountIf([](const auto& turn) {
            return turn.lane == execution::Lane::Cleanup && turn.phase == execution::Phase::Queued;
        }), 1);
        QVERIFY(fixture.execution.step(execution::Lane::Cleanup));
        QVERIFY(state->destroyed);
        fixture.execution.drain();
        QVERIFY(fixture.execution.quiescent());
    }

    void realWorkerRefusalDestroysCapturedPsbtOffGui()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"do not replace after refusal"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        auto state = std::make_shared<DeletionState>();
        auto document = TrackedDocument(state);
        BackendExecutor executor;
        QObject gui;
        backup::Services services;
        services.scheduler = executor.scheduler();
        services.post = [&gui](auto publication) {
            if (!QMetaObject::invokeMethod(&gui, std::move(publication), Qt::QueuedConnection)) {
                throw std::runtime_error("GUI publication could not be queued");
            }
        };
        services.available = [] { return true; };
        FileExportOperationModel operation{path, document, std::move(services)};
        document.reset();

        QVERIFY(operation.start());
        QTRY_VERIFY_WITH_TIMEOUT(operation.confirmationRequired(), 5'000);
        QVERIFY(!state->destroyed);
        QVERIFY(operation.cancel());
        QTRY_VERIFY_WITH_TIMEOUT(!operation.busy(), 5'000);
        QTRY_VERIFY_WITH_TIMEOUT(state->destroyed.load(), 5'000);

        QVERIFY(!state->destroyed_on_gui.load());
        QCOMPARE(ReadFile(path), original);
        executor.shutdown();
        QTRY_VERIFY(executor.isDrained());
    }

    void overwriteNeedsApprovalAndRechecksDestinationVersion()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"previous reviewed export"};
        const QByteArray intervening{"concurrent replacement"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation.busy());
        QVERIFY(operation.confirmationRequired());
        QVERIFY(operation.replacing());
        QCOMPARE(ReadFile(path), original);

        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
            QCOMPARE(file.write(intervening), qint64(intervening.size()));
        }
        QVERIFY(operation.confirm());
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(!operation.busy());
        QVERIFY(!operation.saved());
        QVERIFY(operation.error().contains(QStringLiteral("changed"), Qt::CaseInsensitive));
        QCOMPARE(ReadFile(path), intervening);
    }

    void refusalDoesNotReplaceExistingFile()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"do not overwrite"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        Fixture fixture;
        FileExportOperationModel operation{path, document, fixture.services()};

        QVERIFY(operation.start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation.confirmationRequired());
        QVERIFY(operation.cancel());
        fixture.execution.drain();
        fixture.deliverGui();

        QVERIFY(!operation.busy());
        QVERIFY(!operation.saved());
        QVERIFY(operation.error().contains(QStringLiteral("cancelled"), Qt::CaseInsensitive));
        QCOMPARE(ReadFile(path), original);
    }

    void acceptedContinuationRetiresAtOverwritePrompt_data()
    {
        QTest::addColumn<bool>("delete_projection");
        QTest::newRow("replace-export-projection") << false;
        QTest::newRow("delete-export-projection") << true;
    }

    void acceptedContinuationRetiresAtOverwritePrompt()
    {
        QFETCH(bool, delete_projection);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"preserve the existing reviewed export"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }

        auto deletion = std::make_shared<DeletionState>();
        auto document = TrackedDocument(deletion);
        Fixture fixture;
        std::atomic_int outcomes{0};
        std::atomic_int terminal_phase{-1};
        auto services = fixture.services();
        UseSerialPromptRetirement(services);
        services.outcome = [&](backup::Snapshot snapshot) {
            ++outcomes;
            terminal_phase = static_cast<int>(snapshot.phase);
        };
        auto operation = std::make_unique<FileExportOperationModel>(path, document, std::move(services));
        document.reset();
        auto request = operation->requestForAcceptedContinuation();
        QVERIFY(request);

        QVERIFY(operation->start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation->confirmationRequired());
        QCOMPARE(ReadFile(path), original);
        QVERIFY(!deletion->destroyed);

        // Keep GUI delivery stopped while the old accepted export is replaced
        // or its projection is destroyed. Its operation still owns the
        // terminal outcome and must retire before any overwrite is admitted.
        if (delete_projection) operation.reset();
        else operation->retire();
        QVERIFY(fixture.execution.step(execution::Lane::Cleanup));

        QCOMPARE(outcomes.load(), 1);
        QCOMPARE(terminal_phase.load(), static_cast<int>(backup::Phase::Retired));
        QCOMPARE(ReadFile(path), original);
        QVERIFY(!deletion->destroyed);
        QCOMPARE(fixture.execution.queuedCountIf([](const auto& turn) {
            return turn.lane == execution::Lane::Cleanup && turn.phase == execution::Phase::Queued;
        }), 1);
        QVERIFY(fixture.execution.step(execution::Lane::Cleanup));
        QVERIFY(deletion->destroyed);

        fixture.deliverGui();
        if (operation) {
            QVERIFY(!operation->busy());
            QVERIFY(operation->error().contains(QStringLiteral("ended before"), Qt::CaseInsensitive));
        }
        fixture.execution.drain();
        request.reset();
        QVERIFY(fixture.execution.quiescent());
    }

    void acceptedFreshContinuationSurvivesRetirementBeforeInspection_data()
    {
        QTest::addColumn<bool>("delete_projection");
        QTest::newRow("replace-export-projection") << false;
        QTest::newRow("delete-export-projection") << true;
    }

    void acceptedFreshContinuationSurvivesRetirementBeforeInspection()
    {
        QFETCH(bool, delete_projection);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("new.psbt"));
        const auto document = std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false));
        const auto expected = psbtwork::SerializeRaw(*document);
        QVERIFY(!expected.isEmpty());

        Fixture fixture;
        std::atomic_int outcomes{0};
        std::atomic_int terminal_phase{-1};
        auto services = fixture.services();
        services.outcome = [&](backup::Snapshot snapshot) {
            ++outcomes;
            terminal_phase = static_cast<int>(snapshot.phase);
        };
        auto operation = std::make_unique<FileExportOperationModel>(path, document, std::move(services));
        auto request = operation->requestForAcceptedContinuation();
        QVERIFY(request);
        QVERIFY(operation->start());

        // Retire after start() returned, before the Native inspect turn. A
        // fresh path is the accepted Save effect and must not be cancelled by
        // loss of its presentation.
        if (delete_projection) operation.reset();
        else operation->retire();
        fixture.execution.drain();

        QCOMPARE(outcomes.load(), 1);
        QCOMPARE(terminal_phase.load(), static_cast<int>(backup::Phase::Succeeded));
        QCOMPARE(ReadFile(path), expected);
        fixture.deliverGui();
        if (operation) {
            QVERIFY(!operation->busy());
            QVERIFY(operation->saved());
        }
        request.reset();
        QVERIFY(fixture.execution.quiescent());
    }

    void retirementDuringInspectionUsesTheInspectedDestinationPolicy_data()
    {
        QTest::addColumn<bool>("existing_destination");
        QTest::newRow("fresh-destination-keeps-accepted-write") << false;
        QTest::newRow("existing-destination-retires-without-write") << true;
    }

    void retirementDuringInspectionUsesTheInspectedDestinationPolicy()
    {
        QFETCH(bool, existing_destination);
        Fixture fixture;
        std::atomic_int writes{0};
        std::atomic_int outcomes{0};
        std::atomic_int terminal_phase{-1};
        BackupFileOperation* projection{};
        auto services = fixture.services();
        services.wallet_name = [] { return std::string{"transaction"}; };
        services.inspect = [&](const std::string& raw, const std::string&) {
            backup::Proposal proposal;
            proposal.path = raw;
            proposal.exists = existing_destination;
            // Models a projection close after inspection has established the
            // proposal but before the state machine admits its next effect.
            projection->backend()->retireIfAwaitingConfirmation();
            return proposal;
        };
        services.write = [&](const backup::Proposal&, bool) {
            ++writes;
            return backup::WriteResult{};
        };
        services.outcome = [&](backup::Snapshot snapshot) {
            ++outcomes;
            terminal_phase = static_cast<int>(snapshot.phase);
        };
        BackupFileOperation operation{QStringLiteral("captured.psbt"), std::move(services)};
        projection = &operation;
        QVERIFY(operation.start());
        fixture.execution.drain();

        QCOMPARE(outcomes.load(), 1);
        QCOMPARE(terminal_phase.load(), static_cast<int>(existing_destination
            ? backup::Phase::Retired : backup::Phase::Succeeded));
        QCOMPARE(writes.load(), existing_destination ? 0 : 1);
        fixture.deliverGui();
        QCOMPARE(operation.phase(), existing_destination
            ? BackupFileOperation::Phase::Retired : BackupFileOperation::Phase::Succeeded);
        QVERIFY(fixture.execution.quiescent());
    }

    void acceptedWriteContinuesAfterRetirementDuringWrite()
    {
        Fixture fixture;
        std::atomic_int writes{0};
        std::atomic_int outcomes{0};
        std::atomic_int terminal_phase{-1};
        BackupFileOperation* projection{};
        auto services = fixture.services();
        services.wallet_name = [] { return std::string{"transaction"}; };
        services.inspect = [](const std::string& raw, const std::string&) {
            backup::Proposal proposal;
            proposal.path = raw;
            return proposal;
        };
        services.write = [&](const backup::Proposal&, bool) {
            ++writes;
            // Projection retirement during the already-admitted write must
            // leave its original completion and terminal receipt intact.
            projection->backend()->retireIfAwaitingConfirmation();
            return backup::WriteResult{};
        };
        services.outcome = [&](backup::Snapshot snapshot) {
            ++outcomes;
            terminal_phase = static_cast<int>(snapshot.phase);
        };
        BackupFileOperation operation{QStringLiteral("captured.psbt"), std::move(services)};
        projection = &operation;

        QVERIFY(operation.start());
        fixture.execution.drain();

        QCOMPARE(writes.load(), 1);
        QCOMPARE(outcomes.load(), 1);
        QCOMPARE(terminal_phase.load(), static_cast<int>(backup::Phase::Succeeded));
        fixture.deliverGui();
        QCOMPARE(operation.phase(), BackupFileOperation::Phase::Succeeded);
        QVERIFY(fixture.execution.quiescent());
    }

    void acceptedContinuationCloseDuringStartAdmission_data()
    {
        QTest::addColumn<bool>("existing_target");
        QTest::newRow("existing-destination-retires") << true;
        QTest::newRow("fresh-destination-keeps-accepted-write") << false;
    }

    void acceptedContinuationCloseDuringStartAdmission()
    {
        QFETCH(bool, existing_target);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("captured.psbt"));
        const QByteArray original{"preserve the existing reviewed destination"};
        if (existing_target) {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        auto deletion = std::make_shared<DeletionState>();
        auto document = TrackedDocument(deletion);
        const auto expected = psbtwork::SerializeRaw(*document);
        Fixture fixture;
        FileExportOperationModel* projection{nullptr};
        auto services = fixture.services();
        UseSerialPromptRetirement(services, [&] {
            if (projection) projection->retire();
        });
        std::atomic_int outcomes{0};
        std::atomic_int terminal_phase{-1};
        services.outcome = [&](backup::Snapshot snapshot) {
            ++outcomes;
            terminal_phase = static_cast<int>(snapshot.phase);
        };
        auto operation = std::make_unique<FileExportOperationModel>(
            path, document, std::move(services));
        projection = operation.get();
        document.reset();
        auto request = operation->requestForAcceptedContinuation();
        QVERIFY(request);

        // Close after the Start work was accepted but before submit() has
        // returned and recorded admission. An existing target must settle
        // Retired; a fresh accepted save still writes.
        QVERIFY(operation->start());
        fixture.execution.drain();
        QCOMPARE(outcomes.load(), 1);
        QCOMPARE(terminal_phase.load(), static_cast<int>(existing_target
            ? backup::Phase::Retired : backup::Phase::Succeeded));
        if (existing_target) QCOMPARE(ReadFile(path), original);
        else QCOMPARE(ReadFile(path), expected);

        operation.reset();
        request.reset();
        fixture.execution.drain();
        QVERIFY(deletion->destroyed.load());
        QVERIFY(fixture.execution.quiescent());
    }

    void acceptedContinuationDeleteAfterInspection_data()
    {
        QTest::addColumn<bool>("existing_target");
        QTest::newRow("existing-target-retires") << true;
        QTest::newRow("fresh-target-keeps-accepted-write") << false;
    }

    void acceptedContinuationDeleteAfterInspection()
    {
        QFETCH(bool, existing_target);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const auto path = directory.filePath(QStringLiteral("captured.psbt"));
        const QByteArray original{"retain unresolved existing output"};
        if (existing_target) {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }

        auto deletion = std::make_shared<DeletionState>();
        auto document = TrackedDocument(deletion);
        const auto expected = psbtwork::SerializeRaw(*document);
        Fixture fixture;
        int published_phases{0};
        std::atomic_int outcomes{0};
        std::atomic_int terminal_phase{-1};
        auto services = fixture.services();
        // Deliver the bounded presentation callback synchronously so the
        // test can remove the projection at the inspected decision boundary.
        services.post = [&](std::function<void()> publication) {
            ++published_phases;
            publication();
        };
        services.outcome = [&](backup::Snapshot result) {
            ++outcomes;
            terminal_phase = static_cast<int>(result.phase);
        };

        auto projection = std::make_unique<FileExportOperationModel>(path, std::move(services));
        QPointer<FileExportOperationModel> observer{projection.get()};
        bool removed_after_inspection{false};
        connect(projection.get(), &FileExportOperationModel::changed, [&] {
            if (published_phases != 2 || !projection) return;
            removed_after_inspection = existing_target ? projection->confirmationRequired() : projection->busy();
            projection.reset();
        });
        auto request = projection->requestForAcceptedContinuation();
        QVERIFY(request);

        // This is the production continuation entry point. Its Start has
        // returned before the test executor runs inspection. Deleting the Qt
        // projection on the inspected publication must retire a pending
        // overwrite, while a fresh accepted destination still gets written.
        QVERIFY(request->start(document));
        document.reset();
        QVERIFY(projection);
        fixture.execution.drain();

        QVERIFY(!observer);
        QVERIFY(removed_after_inspection);
        QCOMPARE(outcomes.load(), 1);
        QCOMPARE(terminal_phase.load(), static_cast<int>(existing_target
            ? backup::Phase::Retired : backup::Phase::Succeeded));
        if (existing_target) QCOMPARE(ReadFile(path), original);
        else QCOMPARE(ReadFile(path), expected);

        request.reset();
        fixture.execution.drain();
        QVERIFY(deletion->destroyed.load());
        QVERIFY(fixture.execution.quiescent());
    }

    void overwritePromptRetirementSurvivesRejectedCoreAdmission()
    {
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString path = directory.filePath(QStringLiteral("existing.psbt"));
        const QByteArray original{"preserve the unresolved overwrite target"};
        {
            QFile file{path};
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(original), qint64(original.size()));
        }
        Fixture fixture;
        auto services = fixture.services();
        UseSerialPromptRetirement(services);
        std::atomic_int outcomes{0};
        std::atomic_int terminal_phase{-1};
        services.outcome = [&](backup::Snapshot snapshot) {
            ++outcomes;
            terminal_phase = static_cast<int>(snapshot.phase);
        };
        auto operation = std::make_unique<FileExportOperationModel>(path,
            std::make_shared<const PartiallySignedTransaction>(psbtfeefixture::Legacy(2, false)), std::move(services));
        auto request = operation->requestForAcceptedContinuation();
        QVERIFY(request);
        QVERIFY(operation->start());
        fixture.execution.drain();
        fixture.deliverGui();
        QVERIFY(operation->confirmationRequired());

        // Retirement remains admitted through Cleanup even when all later
        // Core submissions reject.
        fixture.execution.rejectLane(execution::Lane::Core);
        operation->retire();
        fixture.execution.drain();
        const auto outcomes_after_close_admission = outcomes.load();
        if (outcomes_after_close_admission != 1) {
            // Keep failure evidence bounded and fully cleaned up.
            operation->retire();
            fixture.execution.drain();
        }
        fixture.deliverGui();
        QCOMPARE(outcomes_after_close_admission, 1);
        QCOMPARE(terminal_phase.load(), static_cast<int>(backup::Phase::Retired));
        QCOMPARE(ReadFile(path), original);
        operation.reset();
        request.reset();
        fixture.execution.drain();
        QVERIFY(fixture.execution.quiescent());
    }
};

BITCOINQML_REGISTER_QT_TEST(FileExportOperationTests)

#include <test/test_fileexportoperation.moc>
