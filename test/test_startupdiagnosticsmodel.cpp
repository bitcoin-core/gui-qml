// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/startupdiagnosticsmodel.h>
#include <qml/applicationmodels.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>
#include <QAbstractItemModelTester>
#include <atomic>

namespace {
struct Fixture {
    ManualRoutedExecution commands;
    std::deque<std::function<void()>> publications;
    std::unique_ptr<StartupDiagnosticsModel> model;
    Fixture() : model(std::make_unique<StartupDiagnosticsModel>(execution::SnapshotServices{
        commands.scheduler, [this](auto work) { publications.push_back(std::move(work)); }})) {}
    ~Fixture() { model.reset(); commands.drain(); }
    bool publishOne(bool newest = false) {
        if (publications.empty()) return false;
        auto work = std::move(newest ? publications.back() : publications.front());
        if (newest) publications.pop_back(); else publications.pop_front();
        work(); return true;
    }
    void publish() { while (publishOne()) {} }
    void finish() { commands.drain(); publish(); commands.drain(); }
};
QString Complete(const QAbstractItemModel& model) {
    QString text; for (int row = 0; row < model.rowCount(); ++row) text += model.data(model.index(row, 0), Qt::UserRole + 1).toString();
    return text;
}
class RecordingScheduler final : public execution::Scheduler {
    struct Task final : execution::Task {
        std::shared_ptr<execution::Task> inner;
        RecordingScheduler& recorder;
        Task(std::shared_ptr<execution::Task> value, RecordingScheduler& owner) : inner(std::move(value)), recorder(owner) {}
        void execute() override {
            recorder.thread = QThread::currentThread(); recorder.entered.release(); recorder.release.acquire(); inner->execute();
        }
        void deliver() override { inner->deliver(); }
        void notStarted(execution::NotStarted::Cause cause) override { inner->notStarted(cause); }
    };
    std::shared_ptr<execution::Scheduler> m_inner;
public:
    std::atomic<QThread*> thread{nullptr}; QSemaphore entered, release;
    explicit RecordingScheduler(std::shared_ptr<execution::Scheduler> inner) : m_inner(std::move(inner)) {}
    execution::Admission submit(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override {
        if (spec.lane == execution::Lane::Domain) task = std::make_shared<Task>(std::move(task), *this);
        return m_inner->submit(std::move(task), spec);
    }
    execution::Admission reserveCleanup(std::shared_ptr<execution::Task> task, execution::TaskSpec spec) override { return m_inner->reserveCleanup(std::move(task), spec); }
    bool activateCleanup(execution::TaskId id) override { return m_inner->activateCleanup(id); }
    execution::BorrowAdmission borrow(execution::ResourceKey key) override { return m_inner->borrow(key); }
    execution::ResourceKey allocateKey() override { return m_inner->allocateKey(); }
    size_t pending() const override { return m_inner->pending(); }
    void closeOrdinaryAdmission() override { m_inner->closeOrdinaryAdmission(); }
    bool cancelQueued(execution::TaskId id) override { return m_inner->cancelQueued(id); }
};
}
class StartupDiagnosticsModelTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void resetSignalObserversKeepTheSameFrame_data() {
        QTest::addColumn<bool>("publishing"); QTest::addColumn<bool>("about"); QTest::addColumn<bool>("reopen");
        for (bool publishing : {false, true}) for (bool about : {false, true}) for (bool reopen : {false, true}) {
            const QString tag = QStringLiteral("%1-%2-%3").arg(publishing ? QStringLiteral("publish") : QStringLiteral("reset-input"),
                about ? QStringLiteral("about") : QStringLiteral("completed"), reopen ? QStringLiteral("reopen") : QStringLiteral("close"));
            QTest::newRow(qPrintable(tag)) << publishing << about << reopen;
        }
    }
    void resetSignalObserversKeepTheSameFrame() {
        QFETCH(bool, publishing); QFETCH(bool, about); QFETCH(bool, reopen);
        Fixture f; QAbstractItemModelTester tester{f.model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest};
        QVERIFY(f.model->append(QStringLiteral("Original visible diagnostic"), false)); QVERIFY(f.model->project(false));
        f.commands.drain(); if (!publishing) f.publish();
        QString early_about, early_reset, late_reset; bool changed_outside{true}, coherent{true}, acted{false}; int depth{0};
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&] { ++depth; early_about = Complete(*f.model); });
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] { early_reset = Complete(*f.model); });
        const auto action = [&] {
            if (acted) return;
            acted = true; const QString before = Complete(*f.model);
            if (reopen) QVERIFY(f.model->resetInput()); else f.model->closeAdmission();
            coherent &= Complete(*f.model) == before;
        };
        if (about) connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, action);
        else connect(f.model.get(), &QAbstractItemModel::modelReset, this, action);
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&] { coherent &= early_about == Complete(*f.model); });
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] {
            late_reset = Complete(*f.model); coherent &= early_reset == late_reset; --depth;
        });
        connect(f.model.get(), &StartupDiagnosticsModel::changed, this, [&] { changed_outside &= depth == 0; });
        if (publishing) QVERIFY(f.publishOne()); else QVERIFY(f.model->resetInput());
        QVERIFY(acted); QVERIFY(coherent); QVERIFY(changed_outside); QCOMPARE(depth, 0);
        // The early and late view caches must still match the actual model
        // before the independently queued clear turn is released.
        QCOMPARE(early_reset, Complete(*f.model)); QCOMPARE(late_reset, Complete(*f.model));
        QVERIFY(f.publications.size() <= size_t{1}); f.publish();
        QCOMPARE(early_reset, QString{}); QCOMPARE(late_reset, QString{}); QCOMPARE(f.model->rowCount(), 0);
        QVERIFY(!f.model->busy()); QVERIFY(changed_outside); QVERIFY(coherent);
        if (reopen) {
            QVERIFY(f.model->append(QStringLiteral("A later admitted diagnostic"), false)); QVERIFY(f.model->project(false)); f.finish();
            QCOMPARE(early_reset, QStringLiteral("A later admitted diagnostic")); QCOMPARE(late_reset, early_reset);
        } else QVERIFY(!f.model->append(QStringLiteral("late ordinary input"), false));
        QObject::disconnect(f.model.get(), nullptr, this, nullptr);
    }
    void aSuccessorConsumesAnOlderQueuedClear_data() {
        QTest::addColumn<bool>("queued_turn_first");
        QTest::newRow("publication-before-clear") << false; QTest::newRow("clear-turn-observes-ready-successor") << true;
    }
    void aSuccessorConsumesAnOlderQueuedClear() {
        QFETCH(bool, queued_turn_first); Fixture f;
        QAbstractItemModelTester tester{f.model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest};
        QVERIFY(f.model->append(QStringLiteral("Old generation"), false)); QVERIFY(f.model->project(false)); f.commands.drain();
        bool replaced{false}; int finished{0}; QPersistentModelIndex successor;
        const QString expected = QStringLiteral("Exact successor diagnostic"); QString cached;
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] {
            cached = Complete(*f.model);
            if (cached == expected) successor = f.model->index(0, 0);
        });
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] {
            if (replaced) return;
            replaced = true;
            for (int request = 0; request < 3; ++request) QVERIFY(f.model->resetInput());
            QVERIFY(f.model->append(expected, false)); QVERIFY(f.model->project(false)); f.commands.drain();
        });
        connect(f.model.get(), &StartupDiagnosticsModel::finished, this, [&] { ++finished; });
        QVERIFY(f.publishOne()); QVERIFY(replaced); QVERIFY(f.model->busy()); QCOMPARE(finished, 0);
        QCOMPARE(f.publications.size(), size_t{2});
        QVERIFY(f.publishOne(queued_turn_first));
        QCOMPARE(cached, expected); QCOMPARE(Complete(*f.model), expected); QVERIFY(!f.model->busy()); QCOMPARE(finished, 1);
        QVERIFY(successor.isValid()); QSignalSpy extra_resets{f.model.get(), &QAbstractItemModel::modelReset};
        f.publish(); QCOMPARE(extra_resets.size(), 0); QVERIFY(successor.isValid()); QCOMPARE(cached, expected);
        QCOMPARE(finished, 1); f.commands.drain();
        QObject::disconnect(f.model.get(), nullptr, this, nullptr);
    }
    void anOutcomeDeliveredInsideResetIsNotLost_data() {
        QTest::addColumn<bool>("about"); QTest::newRow("about-to-reset") << true; QTest::newRow("reset-completed") << false;
    }
    void anOutcomeDeliveredInsideResetIsNotLost() {
        QFETCH(bool, about); Fixture f;
        QAbstractItemModelTester tester{f.model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest};
        QVERIFY(f.model->append(QStringLiteral("Original result"), false)); QVERIFY(f.model->project(false)); f.commands.drain();
        bool replaced{false}; int finished{0};
        const QString expected = QStringLiteral("Outcome observed during the original reset");
        const auto replace = [&] {
            if (replaced) return;
            replaced = true; QVERIFY(f.model->resetInput()); QVERIFY(f.model->append(expected, false));
            QVERIFY(f.model->project(false)); f.commands.drain();
            // Explicitly deliver the actual successor wake while the original
            // reset signal is still on the stack, as a nested GUI loop can do.
            f.publish(); QVERIFY(f.model->busy());
        };
        if (about) connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, replace);
        else connect(f.model.get(), &QAbstractItemModel::modelReset, this, replace);
        connect(f.model.get(), &StartupDiagnosticsModel::finished, this, [&] { ++finished; });
        QVERIFY(f.publishOne()); QVERIFY(replaced); QVERIFY(f.model->busy()); QCOMPARE(finished, 0);
        QCOMPARE(f.publications.size(), size_t{1}); QVERIFY(f.publishOne());
        QVERIFY(!f.model->busy()); QCOMPARE(Complete(*f.model), expected); QCOMPARE(finished, 1);
        f.finish(); QCOMPARE(finished, 1); QCOMPARE(Complete(*f.model), expected);
        QObject::disconnect(f.model.get(), nullptr, this, nullptr);
    }
    void aQueuedTurnInsideAnotherResetDoesNotSpin_data() {
        QTest::addColumn<bool>("about"); QTest::newRow("during-about") << true; QTest::newRow("during-reset-completed") << false;
    }
    void aQueuedTurnInsideAnotherResetDoesNotSpin() {
        QFETCH(bool, about); Fixture f;
        QAbstractItemModelTester tester{f.model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest};
        QVERIFY(f.model->append(QStringLiteral("First frame"), false)); QVERIFY(f.model->project(false)); f.commands.drain();
        bool restarted{false};
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] {
            if (!restarted) { restarted = true; QVERIFY(f.model->resetInput()); }
        });
        QVERIFY(f.publishOne()); QCOMPARE(f.publications.size(), size_t{1});
        const QString expected = QStringLiteral("New frame before the old clear");
        QVERIFY(f.model->append(expected, false)); QVERIFY(f.model->project(false)); f.commands.drain();
        bool delivered{false}; int depth{0};
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&] { ++depth; QCOMPARE(depth, 1); });
        const auto nested = [&] {
            if (delivered) return;
            delivered = true; QVERIFY(f.publishOne()); QVERIFY(f.publications.empty());
        };
        if (about) connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, nested);
        else connect(f.model.get(), &QAbstractItemModel::modelReset, this, nested);
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] { --depth; });
        QVERIFY(f.publishOne(true)); QVERIFY(delivered); QCOMPARE(depth, 0); QVERIFY(f.publications.empty());
        QVERIFY(!f.model->busy()); QCOMPARE(Complete(*f.model), expected); f.finish(); QCOMPARE(Complete(*f.model), expected);
        QObject::disconnect(f.model.get(), nullptr, this, nullptr);
    }
    void publicResetSignalsMayDestroyTheirObserver_data() {
        QTest::addColumn<int>("phase"); QTest::addColumn<bool>("close");
        for (int phase = 0; phase < 3; ++phase) for (bool close : {false, true}) {
            const QString tag = QStringLiteral("%1-%2").arg(phase).arg(close ? QStringLiteral("close") : QStringLiteral("reset-input"));
            QTest::newRow(qPrintable(tag)) << phase << close;
        }
    }
    void publicResetSignalsMayDestroyTheirObserver() {
        QFETCH(int, phase); QFETCH(bool, close); Fixture f;
        QVERIFY(f.model->append(QStringLiteral("Visible original"), false)); QVERIFY(f.model->project(false)); f.finish();
        auto* tester = new QAbstractItemModelTester{f.model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest, f.model.get()};
        Q_UNUSED(tester);
        QSignalSpy changed{f.model.get(), &StartupDiagnosticsModel::changed}, finished{f.model.get(), &StartupDiagnosticsModel::finished};
        const auto destroy = [&] { f.model.reset(); };
        if (phase == 0) connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, destroy);
        if (phase == 1) connect(f.model.get(), &QAbstractItemModel::modelReset, this, destroy);
        if (phase == 2) connect(f.model.get(), &StartupDiagnosticsModel::changed, this, destroy);
        if (close) f.model->closeAdmission(); else QVERIFY(!f.model->resetInput());
        QVERIFY(!f.model); QCOMPARE(changed.size(), phase == 2 ? 1 : 0); QCOMPARE(finished.size(), 0);
        f.finish(); QVERIFY(f.commands.quiescent());
    }
    void destructionRemainsSilentWithAHeldOutcome() {
        Fixture f; QVERIFY(f.model->append(QStringLiteral("Accepted original diagnostic"), false)); QVERIFY(f.model->project(false));
        f.commands.drain(); QSignalSpy changed{f.model.get(), &StartupDiagnosticsModel::changed};
        QSignalSpy resets{f.model.get(), &QAbstractItemModel::modelReset}, finished{f.model.get(), &StartupDiagnosticsModel::finished};
        f.model.reset(); f.publish(); f.commands.drain();
        QCOMPARE(changed.size(), 0); QCOMPARE(resets.size(), 0); QCOMPARE(finished.size(), 0); QVERIFY(f.commands.quiescent());
    }
    void retirementKeepsResetAndPropertyNotificationsCoherent_data() {
        QTest::addColumn<int>("phase");
        QTest::newRow("displayed-rows") << 0;
        QTest::newRow("about-to-publish") << 1;
        QTest::newRow("reset-completed") << 2;
        QTest::newRow("property-notification") << 3;
    }
    void retirementKeepsResetAndPropertyNotificationsCoherent() {
        QFETCH(int, phase);
        Fixture f;
        QAbstractItemModelTester tester{f.model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest};
        QVERIFY(f.model->append(QStringLiteral("Original retained diagnostic"), false));
        QVERIFY(f.model->project(false));
        f.commands.drain();
        if (phase == 0) f.publish();
        const QPersistentModelIndex original = f.model->index(0, 0);
        if (phase == 0) QVERIFY(original.isValid());
        int reset_depth{0}, changed{0}, finished{0};
        bool closed{false}, balanced{true};
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&] { ++reset_depth; });
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] { --reset_depth; });
        connect(f.model.get(), &StartupDiagnosticsModel::changed, this, [&] { ++changed; balanced &= reset_depth == 0; });
        connect(f.model.get(), &StartupDiagnosticsModel::finished, this, [&] { ++finished; });
        const auto close = [&] { if (!closed) { closed = true; f.model->closeAdmission(); } };
        if (phase == 1) connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, close);
        if (phase == 2) connect(f.model.get(), &QAbstractItemModel::modelReset, this, close);
        if (phase == 3) connect(f.model.get(), &StartupDiagnosticsModel::changed, this, close);
        if (phase == 0) close();
        else f.publish();
        QVERIFY(closed); QVERIFY(balanced); QCOMPARE(reset_depth, 0); QVERIFY(changed > 0);
        QVERIFY(!f.model->busy()); QCOMPARE(f.model->rowCount(), 0); QVERIFY(f.model->summary().isEmpty());
        QVERIFY(!original.isValid()); QCOMPARE(finished, 0);
        QVERIFY(!f.model->project(false)); QVERIFY(!f.model->append(QStringLiteral("late ordinary input"), false));
        f.model->retryPublication(); f.finish();
        QCOMPARE(finished, 0); QCOMPARE(f.model->rowCount(), 0);
        QObject::disconnect(f.model.get(), nullptr, this, nullptr);
    }
    void oneShotDiagnosticRestartSurvivesRetirementNotification_data() {
        QTest::addColumn<int>("phase");
        QTest::newRow("about-to-reset") << 0;
        QTest::newRow("reset-completed") << 1;
        QTest::newRow("property-notification") << 2;
    }
    void oneShotDiagnosticRestartSurvivesRetirementNotification() {
        QFETCH(int, phase);
        Fixture f;
        QAbstractItemModelTester tester{f.model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest};
        QVERIFY(f.model->append(QStringLiteral("Original diagnostic"), false));
        QVERIFY(f.model->project(false)); f.finish();
        bool restarted{false}, admitted{false};
        const auto restart = [&] {
            if (restarted) return;
            restarted = true;
            admitted = f.model->resetInput() && f.model->append(QStringLiteral("New lifecycle diagnostic"), false) && f.model->project(false);
        };
        if (phase == 0) connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, restart);
        if (phase == 1) connect(f.model.get(), &QAbstractItemModel::modelReset, this, restart);
        if (phase == 2) connect(f.model.get(), &StartupDiagnosticsModel::changed, this, restart);
        f.model->closeAdmission();
        QVERIFY(restarted); QVERIFY(admitted); QVERIFY(f.model->busy());
        f.finish();
        QVERIFY(!f.model->busy()); QCOMPARE(Complete(*f.model), QStringLiteral("New lifecycle diagnostic"));
        QObject::disconnect(f.model.get(), nullptr, this, nullptr);
        f.model->closeAdmission(); f.commands.drain(); QVERIFY(f.commands.quiescent());
    }
    void startupFacadeTracksDiagnosticOutcome_data() {
        QTest::addColumn<int>("outcome");
        QTest::newRow("full-summary") << 0;
        QTest::newRow("rejected-projection") << 1;
        QTest::newRow("source-closed-before-publication") << 2;
    }
    void startupFacadeTracksDiagnosticOutcome() {
        QFETCH(int, outcome);
        Fixture f;
        QQmlEngine engine;
        engine.rootContext()->setContextProperty(QStringLiteral("startupDiagnostics"), f.model.get());
        engine.rootContext()->setContextProperty(QStringLiteral("message"), QStringLiteral("Original startup fallback"));
        ApplicationModels models{&engine};
        QSignalSpy changed{&models, &ApplicationModels::startupErrorChanged};
        QCOMPARE(models.startupErrorDetails(), f.model.get());
        QCOMPARE(models.startupError(), QStringLiteral("Original startup fallback"));
        QVERIFY(!models.startupErrorPending());
        QVERIFY(f.model->append(QStringLiteral("Original complete diagnostic"), false));
        if (outcome == 1) {
            f.commands.rejectLane(execution::Lane::Domain);
            QVERIFY(!f.model->project(false));
            QCOMPARE(models.startupError(), QStringLiteral("Startup diagnostics could not be queued. Retry to display the retained diagnostics."));
            QVERIFY(!models.startupErrorPending());
        } else {
            QVERIFY(f.model->project(false));
            QVERIFY(models.startupErrorPending());
            QVERIFY(changed.count() > 0);
            const auto before = changed.count();
            f.commands.drain();
            QVERIFY(models.startupErrorPending()); // Publication belongs to GUI.
            if (outcome == 2) {
                f.model->closeAdmission();
                QCOMPARE(models.startupErrorDetails(), f.model.get());
                QCOMPARE(models.startupError(), QStringLiteral("Original startup fallback"));
            }
            f.publish();
            QVERIFY(!models.startupErrorPending());
            if (outcome == 0) {
                QCOMPARE(models.startupError(), QStringLiteral("Original complete diagnostic"));
                QVERIFY(changed.count() > before);
                QCOMPARE(Complete(*models.startupErrorDetails()), QStringLiteral("Original complete diagnostic"));
            }
        }
        f.model.reset(); f.commands.drain(); QVERIFY(f.commands.quiescent());
    }
    void startupFacadeKeepsFallbackAndContextLifetime() {
        auto engine = std::make_unique<QQmlEngine>();
        engine->rootContext()->setContextProperty(QStringLiteral("message"), QStringLiteral("Context-owned startup fallback"));
        engine->rootContext()->setContextProperty(QStringLiteral("testAutomationEnabled"), true);
        auto models = std::make_unique<ApplicationModels>(engine.get());
        QCOMPARE(models->startupError(), QStringLiteral("Context-owned startup fallback"));
        QVERIFY(!models->startupErrorDetails()); QVERIFY(!models->startupErrorPending());
        QVERIFY(models->testAutomationEnabled());
        QObject unrelated;
        engine->rootContext()->setContextProperty(QStringLiteral("startupDiagnostics"), &unrelated);
        QVERIFY(!models->startupErrorDetails());
        QCOMPARE(models->startupError(), QStringLiteral("Context-owned startup fallback"));
        // A C++ owner can retain this non-owning facade through engine retirement.
        // Its guarded context must stop exposing the retired startup projection.
        models->setParent(nullptr);
        engine.reset();
        QCOMPARE(models->startupError(), QString{});
        QVERIFY(!models->startupErrorDetails()); QVERIFY(!models->startupErrorPending());
        QVERIFY(!models->testAutomationEnabled());
    }
    void nativeOutcomeAndGuiPublicationAreSeparate() {
        Fixture f;
        QVERIFY(f.model->append(QStringList{" first ", "first", "second"}, true));
        QVERIFY(f.model->project(true)); QSignalSpy finished{f.model.get(), &StartupDiagnosticsModel::finished};
        QVERIFY(f.model->busy()); QCOMPARE(f.model->rowCount(), 0);
        QVERIFY(f.commands.step(execution::Lane::Domain)); f.model->retryPublication();
        QVERIFY(f.model->busy()); QCOMPARE(finished.count(), 0);
        QVERIFY(f.commands.step(execution::Lane::Domain)); QVERIFY(f.model->busy()); QCOMPARE(f.model->rowCount(), 0);
        f.publish(); QVERIFY(!f.model->busy()); QCOMPARE(finished.count(), 1);
        QCOMPARE(f.model->summary(), QString{"first\n\nsecond"}); QCOMPARE(Complete(*f.model), f.model->summary());
        QVERIFY(!f.model->append("late startup warning", true));
    }
    void completeLargeMessageSurvivesBoundedSummary() {
        Fixture f;
        const QString original = QStringLiteral("  leading\n") + QString(300'000, QChar{'x'}) + QStringLiteral("\nfinal diagnostic tail  ");
        QVERIFY(f.model->append(original, false));
        QVERIFY(f.model->project(false, {}, {}, {}, false)); f.finish();
        QVERIFY(f.model->summary().size() < 16500); QVERIFY(f.model->rowCount() > 250);
        for (int row = 0; row < f.model->rowCount(); ++row) QVERIFY(f.model->data(f.model->index(row, 0), Qt::UserRole + 1).toString().size() <= 1024);
        QCOMPARE(Complete(*f.model), original);
    }
    void earlyCollectionsKeepOrderAndFailureOwnership() {
        Fixture f; auto early = startupdiagnostics::Create(f.commands.scheduler); QVERIFY(early);
        QVERIFY(early->append(" early warning ", true)); QVERIFY(early->append("early warning", true));
        early->seal();
        QVERIFY(f.model->adopt(early)); QVERIFY(f.model->append(" later warning ", true));
        QVERIFY(f.model->append(" explicit failure ", false));
        QVERIFY(f.model->project(false, "fallback", "Startup warnings:")); f.finish();
        QCOMPARE(Complete(*f.model), QString{"Startup warnings:\nearly warning\n\nlater warning\n\nexplicit failure"});
        QVERIFY(!early->append("arrived after seal", true)); early.reset();
    }
    void heldOldPublicationCannotOverwriteNewFatal() {
        Fixture f; QVERIFY(f.model->append("old", true)); QVERIFY(f.model->project(true)); f.commands.drain();
        QVERIFY(f.model->resetInput()); QVERIFY(f.model->append("new fatal detail", false));
        QVERIFY(f.model->project(false, {}, {}, "Fatal error"));
        f.publish(); QVERIFY(f.model->busy()); QCOMPARE(f.model->rowCount(), 0);
        f.finish(); QCOMPARE(Complete(*f.model), QString{"Fatal error\n\nnew fatal detail"});
    }
    void closeAndDispatchRejectionAreExplicit() {
        Fixture f; QVERIFY(f.model->append("important warning", true)); f.commands.rejectLane(execution::Lane::Domain);
        QVERIFY(!f.model->project(true)); QVERIFY(!f.model->busy()); QVERIFY(!f.model->error().isEmpty()); f.finish();
        QVERIFY(f.model->resetInput()); QVERIFY(f.model->append("closed", true)); QVERIFY(f.model->project(true));
        f.model->closeAdmission(); f.finish(); QVERIFY(f.model->summary().isEmpty());
        QVERIFY(!f.model->append("late", false));
    }
    void observerDestructionAndResetSignalsCannotPublishAfterClose() {
        Fixture f; QVERIFY(f.model->append(QString(200'000, QChar{'z'}), false)); QVERIFY(f.model->project(false));
        f.commands.drain(); f.model.reset(); f.publish(); f.commands.drain(); QVERIFY(f.commands.quiescent());
        f.model = std::make_unique<StartupDiagnosticsModel>(execution::SnapshotServices{f.commands.scheduler, [&f](auto work) { f.publications.push_back(std::move(work)); }});
        QVERIFY(f.model->append("reset destroys observer", false)); QVERIFY(f.model->project(false));
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&f] { f.model.reset(); });
        f.finish(); QVERIFY(!f.model); QVERIFY(f.commands.quiescent());
    }
    void refusalRetainsOriginalForRetry() {
        Fixture f; const QString original = "retained original diagnostic";
        QVERIFY(f.model->append(original, false)); f.commands.rejectLane(execution::Lane::Domain);
        QVERIFY(!f.model->project(false)); QVERIFY(f.model->project(false)); f.finish();
        QCOMPARE(Complete(*f.model), original);
    }
    void intakeBudgetsRefuseWholeBatchWithoutTruncatingAcceptedData() {
        Fixture f;
        const QString accepted(startupdiagnostics::Collection::MAX_CHARS, QChar{'a'});
        QVERIFY(f.model->append(accepted, false)); QVERIFY(!f.model->append("over capacity", false));
        QVERIFY(f.model->project(false)); f.finish(); QVERIFY(Complete(*f.model).endsWith(accepted)); QVERIFY(!f.model->error().isEmpty());
        QVERIFY(f.model->resetInput());
        QStringList too_many; for (size_t i = 0; i <= startupdiagnostics::Collection::MAX_BATCH_MESSAGES; ++i) too_many.push_back("bounded");
        QVERIFY(!f.model->append(too_many, false)); QVERIFY(f.model->append("accepted later", false));
        QVERIFY(f.model->project(false)); f.finish(); QVERIFY(Complete(*f.model).endsWith("accepted later")); QVERIFY(!f.model->error().isEmpty());
    }
    void rawTextBudgetIsReleasedOnlyByNativeCleanup() {
        Fixture f; auto budget = std::make_shared<startupdiagnostics::TextBudget>();
        auto text = startupdiagnostics::RetainText(f.commands.scheduler, budget,
            QString(startupdiagnostics::TextBudget::MAX_CHARS, QChar{'r'}));
        QVERIFY(text); QCOMPARE(budget->used.load(), startupdiagnostics::TextBudget::MAX_CHARS);
        QVERIFY(!startupdiagnostics::RetainText(f.commands.scheduler, budget, "refused"));
        text.reset(); QCOMPARE(budget->used.load(), startupdiagnostics::TextBudget::MAX_CHARS);
        f.commands.drain(); QCOMPARE(budget->used.load(), uint64_t{0});
    }
    void reentrantSuccessorAtEveryPublicationPhaseKeepsItsOwnOutcome_data() {
        QTest::addColumn<int>("phase");
        QTest::newRow("before-reset") << 0; QTest::newRow("after-reset") << 1; QTest::newRow("changed") << 2;
    }
    void reentrantSuccessorAtEveryPublicationPhaseKeepsItsOwnOutcome() {
        QFETCH(int, phase); Fixture f;
        QVERIFY(f.model->append("old result", false)); QVERIFY(f.model->project(false)); f.commands.drain();
        bool replaced{false}; bool replacement_ok{true}; int finished{0};
        const auto replace = [&] {
            if (replaced) return;
            replaced = true;
            replacement_ok = f.model->resetInput() && f.model->append("successor result", false) && f.model->project(false);
        };
        if (phase == 0) connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, replace);
        if (phase == 1) connect(f.model.get(), &QAbstractItemModel::modelReset, this, replace);
        if (phase == 2) connect(f.model.get(), &StartupDiagnosticsModel::changed, this, replace);
        connect(f.model.get(), &StartupDiagnosticsModel::finished, this, [&] { ++finished; });
        f.publish(); QVERIFY(replaced); QVERIFY(replacement_ok); QCOMPARE(finished, 0); QVERIFY(f.model->busy());
        f.finish(); QCOMPARE(finished, 1); QCOMPARE(Complete(*f.model), QString{"successor result"});
    }
    void actualDiagnosticLossFailsTheFinalReceipt() {
        QCOMPARE(startupdiagnostics::FinalExitStatus(EXIT_SUCCESS, false), EXIT_SUCCESS);
        QCOMPARE(startupdiagnostics::FinalExitStatus(EXIT_SUCCESS, true), EXIT_FAILURE);
        QCOMPARE(startupdiagnostics::FinalExitStatus(17, true), 17);
        // Ordinary explicit intent refusal is not itself a diagnostic-loss receipt.
        Fixture f; f.model->closeAdmission(); QVERIFY(!f.model->append("not admitted", false));
        QCOMPARE(startupdiagnostics::FinalExitStatus(EXIT_SUCCESS, false), EXIT_SUCCESS);
    }
    void realDomainThreadPreservesGuiHeartbeatAndExactTail() {
        BackendExecutor backend{execution::Options{}};
        auto scheduler = std::make_shared<RecordingScheduler>(backend.scheduler());
        StartupDiagnosticsModel model{{scheduler, {}}};
        const QString text = QString(400'000, QChar{'q'}) + "actual final error";
        QVERIFY(model.append(text, false)); QVERIFY(model.project(false));
        const auto unblock = qScopeGuard([scheduler] { scheduler->release.release(8); });
        QTRY_VERIFY_WITH_TIMEOUT(scheduler->entered.available() > 0, 5000);
        QVERIFY(scheduler->thread.load() != QThread::currentThread());
        bool heartbeat{false}; QTimer::singleShot(0, this, [&] { heartbeat = true; });
        QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 5000); QVERIFY(model.busy());
        scheduler->release.release(8);
        QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000); QCOMPARE(Complete(model), text);
        model.closeAdmission(); backend.shutdown(); QTRY_VERIFY_WITH_TIMEOUT(backend.isDrained(), 5000);
    }
};
BITCOINQML_REGISTER_QT_TEST(StartupDiagnosticsModelTests)
#include <test_startupdiagnosticsmodel.moc>
