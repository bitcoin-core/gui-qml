// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/startupdiagnosticsmodel.h>
#include <qml/applicationmodels.h>
#include <qml/executor.h>
#include <qml/startuppolicy.h>
#include <test/mocks/manualstartupexecution.h>
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
    ManualStartupExecution worker;
    std::deque<std::function<void()>> publications;
    presentation::Post post = [this](auto work) { publications.push_back(std::move(work)); };
    std::shared_ptr<execution::SerialQueue> retirement =
        std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(execution::Lane::Utility));
    PresentationServices services{{post, [queue = retirement](auto work) {
        return queue->post(std::move(work));
    }, {}, std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(execution::Lane::Utility))}};
    std::unique_ptr<StartupDiagnosticsModel> model;

    explicit Fixture(bool with_model = true)
    {
        if (with_model) model = makeModel();
    }

    ~Fixture() { model.reset(); worker.drain(); }

    std::unique_ptr<StartupDiagnosticsModel> makeModel()
    {
        return std::make_unique<StartupDiagnosticsModel>(post, services);
    }

    bool publishOne(bool newest = false)
    {
        if (publications.empty()) return false;
        auto work = std::move(newest ? publications.back() : publications.front());
        if (newest) publications.pop_back(); else publications.pop_front();
        work();
        return true;
    }

    void publish() { while (publishOne()) {} }
    void finish() { worker.drain(); publish(); worker.drain(); }
};
QString Complete(const QAbstractItemModel& model) {
    QString text; for (int row = 0; row < model.rowCount(); ++row) text += model.data(model.index(row, 0), Qt::UserRole + 1).toString();
    return text;
}

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
        f.worker.drain(); if (!publishing) f.publish();
        QString early_about, early_reset, late_reset; bool changed_outside{true}, coherent{true}, acted{false}; int depth{0};
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&] { ++depth; early_about = Complete(*f.model); });
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] { early_reset = Complete(*f.model); });
        const auto action = [&] {
            if (acted) return;
            acted = true; const QString before = Complete(*f.model);
            if (reopen) QVERIFY(f.model->resetInput()); else f.model->close();
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
        QVERIFY(f.model->append(QStringLiteral("Old generation"), false)); QVERIFY(f.model->project(false)); f.worker.drain();
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
            QVERIFY(f.model->append(expected, false)); QVERIFY(f.model->project(false)); f.worker.drain();
        });
        connect(f.model.get(), &StartupDiagnosticsModel::finished, this, [&] { ++finished; });
        QVERIFY(f.publishOne()); QVERIFY(replaced); QVERIFY(f.model->busy()); QCOMPARE(finished, 0);
        QCOMPARE(f.publications.size(), size_t{2});
        QVERIFY(f.publishOne(queued_turn_first));
        QCOMPARE(cached, expected); QCOMPARE(Complete(*f.model), expected); QVERIFY(!f.model->busy()); QCOMPARE(finished, 1);
        QVERIFY(successor.isValid()); QSignalSpy extra_resets{f.model.get(), &QAbstractItemModel::modelReset};
        f.publish(); QCOMPARE(extra_resets.size(), 0); QVERIFY(successor.isValid()); QCOMPARE(cached, expected);
        QCOMPARE(finished, 1); f.worker.drain();
        QObject::disconnect(f.model.get(), nullptr, this, nullptr);
    }
    void anOutcomeDeliveredInsideResetIsNotLost_data() {
        QTest::addColumn<bool>("about"); QTest::newRow("about-to-reset") << true; QTest::newRow("reset-completed") << false;
    }
    void anOutcomeDeliveredInsideResetIsNotLost() {
        QFETCH(bool, about); Fixture f;
        QAbstractItemModelTester tester{f.model.get(), QAbstractItemModelTester::FailureReportingMode::QtTest};
        QVERIFY(f.model->append(QStringLiteral("Original result"), false)); QVERIFY(f.model->project(false)); f.worker.drain();
        bool replaced{false}; int finished{0};
        const QString expected = QStringLiteral("Outcome observed during the original reset");
        const auto replace = [&] {
            if (replaced) return;
            replaced = true; QVERIFY(f.model->resetInput()); QVERIFY(f.model->append(expected, false));
            QVERIFY(f.model->project(false)); f.worker.drain();
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
        QVERIFY(f.model->append(QStringLiteral("First frame"), false)); QVERIFY(f.model->project(false)); f.worker.drain();
        bool restarted{false};
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] {
            if (!restarted) { restarted = true; QVERIFY(f.model->resetInput()); }
        });
        QVERIFY(f.publishOne()); QCOMPARE(f.publications.size(), size_t{1});
        const QString expected = QStringLiteral("New frame before the old clear");
        QVERIFY(f.model->append(expected, false)); QVERIFY(f.model->project(false)); f.worker.drain();
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
        if (close) f.model->close(); else QVERIFY(!f.model->resetInput());
        QVERIFY(!f.model); QCOMPARE(changed.size(), phase == 2 ? 1 : 0); QCOMPARE(finished.size(), 0);
        f.finish(); QVERIFY(f.worker.quiescent());
    }
    void destructionRemainsSilentWithAHeldOutcome() {
        Fixture f; QVERIFY(f.model->append(QStringLiteral("Accepted original diagnostic"), false)); QVERIFY(f.model->project(false));
        f.worker.drain(); QSignalSpy changed{f.model.get(), &StartupDiagnosticsModel::changed};
        QSignalSpy resets{f.model.get(), &QAbstractItemModel::modelReset}, finished{f.model.get(), &StartupDiagnosticsModel::finished};
        f.model.reset(); f.publish(); f.worker.drain();
        QCOMPARE(changed.size(), 0); QCOMPARE(resets.size(), 0); QCOMPARE(finished.size(), 0); QVERIFY(f.worker.quiescent());
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
        f.worker.drain();
        if (phase == 0) f.publish();
        const QPersistentModelIndex original = f.model->index(0, 0);
        if (phase == 0) QVERIFY(original.isValid());
        int reset_depth{0}, changed{0}, finished{0};
        bool closed{false}, balanced{true};
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&] { ++reset_depth; });
        connect(f.model.get(), &QAbstractItemModel::modelReset, this, [&] { --reset_depth; });
        connect(f.model.get(), &StartupDiagnosticsModel::changed, this, [&] { ++changed; balanced &= reset_depth == 0; });
        connect(f.model.get(), &StartupDiagnosticsModel::finished, this, [&] { ++finished; });
        const auto close = [&] { if (!closed) { closed = true; f.model->close(); } };
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
        f.model->close();
        QVERIFY(restarted); QVERIFY(admitted); QVERIFY(f.model->busy());
        f.finish();
        QVERIFY(!f.model->busy()); QCOMPARE(Complete(*f.model), QStringLiteral("New lifecycle diagnostic"));
        QObject::disconnect(f.model.get(), nullptr, this, nullptr);
        f.model->close(); f.worker.drain(); QVERIFY(f.worker.quiescent());
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
            f.worker.reject_utility = true;
            QVERIFY(!f.model->project(false));
            QCOMPARE(models.startupError(), QStringLiteral("Startup diagnostics could not be shown. Try again."));
            QVERIFY(!models.startupErrorPending());
        } else {
            QVERIFY(f.model->project(false));
            QVERIFY(models.startupErrorPending());
            QVERIFY(changed.count() > 0);
            const auto before = changed.count();
            f.worker.drain();
            QVERIFY(models.startupErrorPending()); // Publication belongs to GUI.
            if (outcome == 2) {
                f.model->close();
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
        f.model.reset(); f.worker.drain(); QVERIFY(f.worker.quiescent());
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
        f.model->retryPublication();
        QVERIFY(f.model->busy()); QCOMPARE(finished.count(), 0);
        QVERIFY(f.worker.step(execution::Lane::Utility));
        QVERIFY(!f.publications.empty()); QVERIFY(f.model->busy()); QCOMPARE(f.model->rowCount(), 0);
        f.publish(); QVERIFY(!f.model->busy()); QCOMPARE(finished.count(), 1);
        QCOMPARE(f.model->summary(), QString{"first\n\nsecond"}); QCOMPARE(Complete(*f.model), f.model->summary());
        QVERIFY(!f.model->append("late startup warning", true));
    }
    void completeLargeMessageSurvivesBoundedSummary() {
        Fixture f{/*with_model=*/false};
        f.worker.core.close();
        const QString original = QStringLiteral("  leading\n") + QString(300'000, QChar{'x'}) + QStringLiteral("\nfinal diagnostic tail  ");
        auto early = startupdiagnostics::Create(f.services.valueRetirement());
        QVERIFY(early);
        QVERIFY(early->append(original, false));
        early->seal();
        const auto loss = std::make_shared<std::atomic_bool>(false);
        auto details = startup::PrepareErrorDetails(f.post, QStringLiteral("fallback"),
            std::move(early), nullptr, loss, f.services);
        QVERIFY(details.model);
        QVERIFY(details.model->busy());
        QVERIFY(f.worker.step(execution::Lane::Utility));
        QVERIFY(details.model->busy());
        QCOMPARE(details.model->rowCount(), 0);
        QVERIFY(!f.publications.empty());
        f.publish();
        QVERIFY(!details.model->busy());
        QVERIFY(!loss->load());
        QVERIFY(details.model->summary().size() < 16500);
        QVERIFY(details.model->rowCount() > 250);
        for (int row = 0; row < details.model->rowCount(); ++row) {
            QVERIFY(details.model->data(details.model->index(row, 0), Qt::UserRole + 1).toString().size() <= 1024);
        }
        QCOMPARE(Complete(*details.model), original);

        details.model->close();
        f.services.close();
        QObject receiver;
        bool drained{false};
        f.services.whenRetired(&receiver, [&] { drained = true; });
        QVERIFY(f.services.pendingRetirements() > 0);
        f.worker.drain();
        QTRY_VERIFY(drained);
        QCOMPARE(f.services.pendingRetirements(), size_t{0});
        QCOMPARE(f.worker.coreSubmissions(), size_t{0});
        QVERIFY(f.worker.quiescent());
    }
    void earlyCollectionsKeepOrderAndFailureOwnership() {
        Fixture f; auto early = startupdiagnostics::Create(f.services.valueRetirement()); QVERIFY(early);
        QVERIFY(early->append(" early warning ", true)); QVERIFY(early->append("early warning", true));
        early->seal();
        QVERIFY(f.model->adopt(early)); QVERIFY(f.model->append(" later warning ", true));
        QVERIFY(f.model->append(" explicit failure ", false));
        QVERIFY(f.model->project(false, "fallback", "Startup warnings:")); f.finish();
        QCOMPARE(Complete(*f.model), QString{"Startup warnings:\nearly warning\n\nlater warning\n\nexplicit failure"});
        QVERIFY(!early->append("arrived after seal", true)); early.reset();
    }
    void heldOldPublicationCannotOverwriteNewFatal() {
        Fixture f{/*with_model=*/false};
        f.worker.core.close();
        StartupDiagnosticsModel model{f.post, f.services};
        QVERIFY(model.append("old", true));
        QVERIFY(model.project(true));
        f.worker.drain();
        QVERIFY(!f.publications.empty());
        model.close();
        QVERIFY(model.resetInput());
        QVERIFY(model.append("new fatal detail", false));
        QVERIFY(model.project(false, {}, {}, "Fatal error"));
        f.publish();
        QVERIFY(model.busy());
        QCOMPARE(model.rowCount(), 0);
        f.worker.drain();
        f.publish();
        QCOMPARE(Complete(model), QString{"Fatal error\n\nnew fatal detail"});
        model.close();
        f.services.close();
        f.worker.drain();
        QVERIFY(f.worker.quiescent());
        QCOMPARE(f.worker.coreSubmissions(), size_t{0});
    }
    void closeAndDispatchRejectionAreExplicit() {
        Fixture f; QVERIFY(f.model->append("important warning", true)); f.worker.reject_utility = true;
        QVERIFY(!f.model->project(true)); QVERIFY(!f.model->busy()); QVERIFY(!f.model->error().isEmpty()); f.finish();
        QVERIFY(f.model->resetInput()); QVERIFY(f.model->append("closed", true)); QVERIFY(f.model->project(true));
        f.model->close(); f.finish(); QVERIFY(f.model->summary().isEmpty());
        QVERIFY(!f.model->append("late", false));
    }
    void observerDestructionAndResetSignalsCannotPublishAfterClose() {
        Fixture f; QVERIFY(f.model->append(QString(200'000, QChar{'z'}), false)); QVERIFY(f.model->project(false));
        f.worker.drain(); f.model.reset(); f.publish(); f.worker.drain(); QVERIFY(f.worker.quiescent());
        f.model = f.makeModel();
        QVERIFY(f.model->append("reset destroys observer", false)); QVERIFY(f.model->project(false));
        connect(f.model.get(), &QAbstractItemModel::modelAboutToBeReset, this, [&f] { f.model.reset(); });
        f.finish(); QVERIFY(!f.model); QVERIFY(f.worker.quiescent());
    }
    void refusalRetainsOriginalForRetry() {
        Fixture f; const QString original = "retained original diagnostic";
        QVERIFY(f.model->append(original, false)); f.worker.reject_utility = true;
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
        Fixture f{/*with_model=*/false}; auto budget = std::make_shared<startupdiagnostics::TextBudget>();
        f.worker.core.close();
        const auto retire = f.services.valueRetirement();
        auto text = startupdiagnostics::RetainText(budget,
            QString(startupdiagnostics::TextBudget::MAX_CHARS, QChar{'r'}), retire);
        QVERIFY(text); QCOMPARE(budget->used.load(), startupdiagnostics::TextBudget::MAX_CHARS);
        QVERIFY(!startupdiagnostics::RetainText(budget, "refused", retire));
        text.reset(); QCOMPARE(budget->used.load(), startupdiagnostics::TextBudget::MAX_CHARS);
        QVERIFY(f.worker.step(execution::Lane::Utility));
        QCOMPARE(budget->used.load(), uint64_t{0});
        QCOMPARE(f.worker.coreSubmissions(), size_t{0});
        QVERIFY(f.worker.quiescent());
    }
    void reentrantSuccessorAtEveryPublicationPhaseKeepsItsOwnOutcome_data() {
        QTest::addColumn<int>("phase");
        QTest::newRow("before-reset") << 0; QTest::newRow("after-reset") << 1; QTest::newRow("changed") << 2;
    }
    void reentrantSuccessorAtEveryPublicationPhaseKeepsItsOwnOutcome() {
        QFETCH(int, phase); Fixture f;
        QVERIFY(f.model->append("old result", false)); QVERIFY(f.model->project(false)); f.worker.drain();
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
        Fixture f; f.model->close(); QVERIFY(!f.model->append("not admitted", false));
        QCOMPARE(startupdiagnostics::FinalExitStatus(EXIT_SUCCESS, false), EXIT_SUCCESS);
    }
    void realDomainThreadPreservesGuiHeartbeatAndExactTail() {
        execution::Executor worker{execution::Executor::Config{}};
        const auto utility = std::make_shared<execution::SerialQueue>(worker.makeQueue(execution::Lane::Utility));
        PresentationServices services{{{}, [utility](auto work) {
            return utility->post(std::move(work));
        }, {}, utility}};
        StartupDiagnosticsModel model{{}, services};
        QSemaphore entered, release;
        std::atomic<QThread*> worker_thread{nullptr};
        const auto cleanup = qScopeGuard([&] {
            model.close();
            release.release();
            worker.stopAndJoin();
        });
        QVERIFY(utility->post([&] {
            worker_thread = QThread::currentThread();
            entered.release();
            release.acquire();
        }));
        const QString text = QString(400'000, QChar{'q'}) + "actual final error";
        QVERIFY(model.append(text, false)); QVERIFY(model.project(false));
        QTRY_VERIFY_WITH_TIMEOUT(entered.available() > 0, 5000);
        QVERIFY(worker_thread.load() != QThread::currentThread());
        bool heartbeat{false}; QTimer::singleShot(0, this, [&] { heartbeat = true; });
        QTRY_VERIFY_WITH_TIMEOUT(heartbeat, 5000); QVERIFY(model.busy());
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!model.busy(), 5000); QCOMPARE(Complete(model), text);
        model.close();
        QTRY_COMPARE(services.pendingRetirements(), size_t{0});
    }
};
BITCOINQML_REGISTER_QT_TEST(StartupDiagnosticsModelTests)
#include <test_startupdiagnosticsmodel.moc>
