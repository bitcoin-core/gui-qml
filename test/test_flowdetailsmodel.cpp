// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/flowdetailsmodel.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/executor.h>
#include <qml/retainedvalue.h>
#include <qml/walletopening.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QScopeGuard>
#include <QSemaphore>
#include <utility>
#include <stdexcept>

namespace {
QVariantMap Flow(int count, QString text = {}) {
    QVariantList outputs;
    for (int i = 0; i < count; ++i) outputs.append(QVariantMap{{"id", QString::number(i)}, {"amountKnown", true}, {"amountSat", 1000}, {"dataText", text}});
    return {{"inputs", QVariantList{QVariantMap{{"id", "input"}}}}, {"outputs", outputs}};
}
QVariantMap Row(QAbstractItemModel* model, int row) { return model->data(model->index(row, 0), Qt::UserRole).toMap(); }
QString Field(QAbstractItemModel* model, const QString& field) {
    QString result; for (int row = 0; row < model->rowCount(); ++row) { const auto value = Row(model, row); if (value.value("field").toString() == field) result += value.value("content").toString(); } return result;
}
struct Fixture {
    using Lane = execution::Lane;
    ManualStartupExecution commands;
    execution::SerialQueue outcomes{commands.executor().makeQueue(Lane::Utility)};
    const std::shared_ptr<execution::ValueCleanupQueue> values{std::make_shared<execution::ValueCleanupQueue>(
        std::make_shared<execution::SerialQueue>(commands.executor().makeQueue(Lane::Utility)))};
    std::deque<std::function<void()>> publications;
    std::function<void()> on_submit, on_hold;
    QObject original, successor;
    std::unique_ptr<FlowDetailsModel> model{std::make_unique<FlowDetailsModel>()};

    explicit Fixture(presentation::Post post = {})
    {
        if (!post) post = [this](auto turn) { publications.push_back(std::move(turn)); };
        model->setServices(std::move(post), [this](auto task) {
            const bool accepted = commands.outcomes.post([this, task = std::move(task)] {
                task->execute();
                outcomes.post([task] { task->deliver(); });
            });
            if (accepted) {
                if (const auto hook = std::exchange(on_submit, {})) hook();
            }
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        }, [this](auto cleanup) {
            auto held = values->hold(std::move(cleanup));
            if (held) {
                if (const auto hook = std::exchange(on_hold, {})) hook();
            }
            return held;
        });
    }

    ~Fixture()
    {
        on_submit = {}; on_hold = {};
        if (model) model->close();
        model.reset();
        bool retired{false};
        values->close([&] { retired = true; });
        finish();
        QVERIFY(retired);
    }

    void publish()
    {
        while (!publications.empty()) {
            auto turn = std::move(publications.front()); publications.pop_front(); turn();
        }
    }
    void finish() { do { commands.drain(); publish(); } while (!commands.quiescent()); }
};
}
class FlowDetailsModelTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void throwingPosterKeepsOriginalProjection_data()
    {
        QTest::addColumn<bool>("enqueue");
        QTest::newRow("before-enqueue") << false; QTest::newRow("after-enqueue") << true;
    }
    void throwingPosterKeepsOriginalProjection()
    {
        QFETCH(bool, enqueue);
        std::deque<std::function<void()>> duplicates;
        QObject context;
        Fixture f{[&](auto callback) {
            if (enqueue) duplicates.push_back(std::move(callback));
            throw std::runtime_error{"post failed"};
        }};
        QSignalSpy changed{f.model.get(), &FlowDetailsModel::snapshotChanged};
        int calls{0};
        f.on_submit = [&] { ++calls; };
        f.model->publishSource(Flow(3, "original"), {}, &context, 42);
        const auto before = changed.count();
        f.commands.drain();
        QCOMPARE(calls, 1);
        QVERIFY(f.commands.quiescent());
        QVERIFY(f.model->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!f.model->pending()); QVERIFY(f.model->error().isEmpty());
        QCOMPARE(f.model->outputRows()->rowCount(), 3); QCOMPARE(changed.count(), before + 1);
        QCOMPARE(Row(f.model->outputRows(), 2).value("dataText").toString(), QStringLiteral("original"));
        while (!duplicates.empty()) { auto callback = std::move(duplicates.front()); duplicates.pop_front(); callback(); }
        QCOMPARE(changed.count(), before + 1);
    }
    void sourceAdmissionKeepsOriginalContext_data()
    {
        QTest::addColumn<bool>("domain");
        QTest::addColumn<int>("action");
        for (const bool domain : {false, true}) {
            for (int action{0}; action < 3; ++action) {
                const char* outcome = action == 0 ? "close" : action == 1 ? "delete" : "successor";
                const auto tag = QStringLiteral("%1-%2").arg(domain ? "Domain" : "Cleanup").arg(outcome).toUtf8();
                QTest::newRow(tag.constData()) << domain << action;
            }
        }
    }
    void sourceAdmissionKeepsOriginalContext()
    {
        QFETCH(bool, domain);
        QFETCH(int, action);
        Fixture f;
        const auto retired = std::make_shared<std::atomic_int>(0);
        struct Anchor {
            std::shared_ptr<std::atomic_int> retired;
            explicit Anchor(std::shared_ptr<std::atomic_int> value) : retired(std::move(value)) {}
            ~Anchor() { retired->fetch_add(1); }
        };
        auto anchor = execution::RetainValues<Anchor>([values = f.values](auto cleanup) { return values->hold(std::move(cleanup)); },
            [&] { return std::make_unique<Anchor>(retired); });
        QVERIFY(anchor);
        const QPointer<FlowDetailsModel> original{f.model.get()};
        QSignalSpy notices{f.model.get(), &FlowDetailsModel::snapshotChanged};
        QSignalSpy documents{f.model.get(), &FlowDetailsModel::entryDetailsChanged};
        int notices_after{-1}, documents_after{-1}, calls{0};
        auto change = [&] {
            ++calls;
            if (action == 0) f.model->close();
            else if (action == 1) f.model.reset();
            else f.model->publishSource(Flow(7, "successor"),
                QVariantList{QVariantMap{{"label", "successor recipient"}, {"amountSat", 2000}}}, &f.successor, 2);
            notices_after = notices.count();
            documents_after = documents.count();
        };
        (domain ? f.on_submit : f.on_hold) = change;
        f.model->publishSource(Flow(3, "original"),
            QVariantList{QVariantMap{{"label", "original recipient"}, {"amountSat", 1000}}}, &f.original, 1, std::move(anchor));
        QCOMPARE(calls, 1);
        QVERIFY(f.commands.hasWork(execution::Lane::Utility));
        QCOMPARE(notices.count(), notices_after);
        QCOMPARE(documents.count(), documents_after);
        QCOMPARE(retired->load(), 0); // Actual Native backing still awaits Cleanup.
        f.finish();
        QCOMPARE(retired->load(), 1);
        if (action == 2) {
            QVERIFY(original);
            QVERIFY(!f.model->pending());
            QVERIFY(f.model->error().isEmpty());
            QCOMPARE(f.model->context(), &f.successor);
            QCOMPARE(f.model->revision(), quint64{2});
            QCOMPARE(f.model->outputRows()->rowCount(), 7);
            QCOMPARE(Row(f.model->outputRows(), 0).value("dataText").toString(), QStringLiteral("successor"));
            QCOMPARE(Row(f.model->recipientRows(), 0).value("label").toString(), QStringLiteral("successor recipient"));
            QVERIFY(!f.model->requestEntryDetails("outputs", 0, &f.original));
        } else {
            QCOMPARE(bool(original), action == 0);
            QCOMPARE(notices.count(), notices_after);
            QCOMPARE(documents.count(), documents_after);
            if (original) {
                QVERIFY(!f.model->pending());
                QCOMPARE(f.model->outputRows()->rowCount(), 0);
            }
        }
        if (f.model) f.model->close();
        f.model.reset();
        f.finish();
        QVERIFY(f.commands.quiescent());
    }
    void quietCloseRetiresHeldAndPublishedSnapshots_data() {
        QTest::addColumn<int>("stage");
        QTest::newRow("before-domain")<<0;
        QTest::newRow("held-outcome")<<1;
        QTest::newRow("published")<<2;
    }
    void resetCannotClearAReentrantSuccessorProjection()
    {
        Fixture f;
        f.model->publishSource(Flow(3, "initial"), {}, &f.original, 1);
        f.finish();
        bool replaced{false};
        connect(f.model->inputRows(), &QAbstractItemModel::modelAboutToBeReset, f.model.get(), [&] {
            if (std::exchange(replaced, true)) return;
            f.model->publishSource(Flow(7, "successor"), {}, &f.successor, 3);
            f.finish(); // The successor can finish inside an original reset observer.
        });
        f.model->publishSource(Flow(5, "obsolete"), {}, &f.original, 2);
        f.finish();
        QVERIFY(replaced);
        QVERIFY(!f.model->pending());
        QVERIFY(f.model->error().isEmpty());
        QCOMPARE(f.model->context(), &f.successor);
        QCOMPARE(f.model->revision(), quint64{3});
        QCOMPARE(f.model->outputRows()->rowCount(), 7);
        QCOMPARE(Row(f.model->outputRows(), 0).value("dataText").toString(), QStringLiteral("successor"));
    }
    void quietCloseRetiresHeldAndPublishedSnapshots() {
        QFETCH(int,stage);Fixture f;
        f.model->publishSource(Flow(1001),{},&f.original,7);
        if(stage==1){f.commands.drain();QVERIFY(f.model->pending());QCOMPARE(f.model->outputRows()->rowCount(),0);}
        if(stage==2){f.finish();QCOMPARE(f.model->outputRows()->rowCount(),1001);}
        QSignalSpy notices{f.model.get(),&FlowDetailsModel::snapshotChanged};
        f.model->close();const auto closed_notices=notices.count();f.finish();
        QCOMPARE(notices.count(),closed_notices);QCOMPARE(f.model->outputRows()->rowCount(),0);
        QVERIFY(f.commands.quiescent());
    }
    void projectionWaitsForDomainOutcomeAndGuiPublication() {
        Fixture f; f.model->publishSource(Flow(1001), QVariantList{QVariantMap{{"label", "recipient"}, {"amountSat", 1000}}}, &f.original, 7);
        QVERIFY(f.model->pending()); QCOMPARE(f.model->outputRows()->rowCount(), 0);
        QVERIFY(f.commands.step(execution::Lane::Utility)); f.model->retryPublications(); QVERIFY(f.model->pending());
        QVERIFY(f.commands.step(execution::Lane::Utility)); QVERIFY(f.model->pending()); QCOMPARE(f.model->outputRows()->rowCount(), 0);
        f.publish(); QVERIFY(!f.model->pending()); QCOMPARE(f.model->outputRows()->rowCount(), 1001);
        QCOMPARE(f.model->recipientRows()->rowCount(), 1); QCOMPARE(f.model->context(), &f.original); QCOMPARE(f.model->revision(), quint64{7});
    }
    void successorSourceFencesAnOriginalHeldGuiPublication() {
        Fixture f; f.model->publishSource(Flow(9), {}, &f.original, 1); f.commands.drain();
        f.model->publishSource(Flow(17), {}, &f.successor, 2);
        f.publish(); QCOMPARE(f.model->outputRows()->rowCount(), 0); QVERIFY(f.model->pending());
        f.finish(); QCOMPARE(f.model->outputRows()->rowCount(), 17); QCOMPARE(f.model->context(), &f.successor);
        QVERIFY(!f.model->requestEntryDetails("outputs", 0, &f.original));
    }
    void sessionProjectsAndRetiresFlowDetailsOnUtility()
    {
        using execution::Lane;
        std::deque<std::function<void()>> publications;
        const auto publishAll = [&] {
            while (!publications.empty()) {
                auto turn = std::move(publications.front());
                publications.pop_front();
                turn();
            }
        };
        ManualStartupExecution worker;
        const auto utility = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(Lane::Utility));
#ifdef ENABLE_WALLET
        const auto core = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(Lane::Core));
        auto opening = std::make_shared<walletopening::Session>(
            std::make_shared<std::atomic_bool>(false), [] {}, walletopening::Session::Preferences{},
            std::chrono::steady_clock::now, nullptr, walletopening::NativeEvents{},
            walletopening::Session::Queues{utility, [core, utility](auto done) {
                core->close();
                core->whenDrained([utility, done = std::move(done)] {
                    utility->close();
                    utility->whenDrained(done);
                });
            }, core, utility});
        const auto submit = opening->observationPost(Lane::Domain);
        const execution::ValueCleanup hold = [opening](auto cleanup) { return opening->holdValues(std::move(cleanup)); };
        const auto stop = [opening](auto done) { opening->stop(false, std::move(done)); };
#else
        const auto values = std::make_shared<execution::ValueCleanupQueue>(
            std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(Lane::Utility)));
        const auto submit = [utility](auto task) {
            const bool accepted = utility->post([task] { task->execute(); task->deliver(); });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        };
        const execution::ValueCleanup hold = [values](auto cleanup) { return values->hold(std::move(cleanup)); };
        const auto stop = [values](auto done) { values->close(std::move(done)); };
#endif
        QObject context;
        FlowDetailsModel model;
        bool drained{false};
        const auto cleanup = qScopeGuard([&] {
            model.close();
            stop([&] { drained = true; });
            worker.drain();
            publishAll();
            QVERIFY(drained);
        });
        const auto releases = std::make_shared<int>(0);
        struct Anchor {
            explicit Anchor(std::shared_ptr<int> count) : releases(std::move(count)) {}
            ~Anchor() { ++*releases; }
            std::shared_ptr<int> releases;
        };
        model.setServices([&](auto publish) { publications.push_back(std::move(publish)); }, submit, hold);
        const QString text(4096, 'x');
        model.publishSource(Flow(2, text), {}, &context, 7, std::make_shared<Anchor>(releases));
        QVERIFY(model.pending());
        QVERIFY(!worker.step(Lane::Core));
        QVERIFY(worker.step(Lane::Utility));
        QVERIFY(model.pending());
        QCOMPARE(model.outputRows()->rowCount(), 0);
        publishAll();
        QCOMPARE(model.outputRows()->rowCount(), 2);
        QVERIFY(Row(model.outputRows(), 0).value("textTruncated").toBool());

        QVERIFY(model.requestEntryDetails("outputs", 0, &context));
        QVERIFY(!worker.step(Lane::Core));
        QVERIFY(worker.step(Lane::Utility));
        QVERIFY(model.entryDetailsBusy());
        publishAll();
        QVERIFY(!model.entryDetailsBusy());
        QCOMPARE(Field(model.entryDocument(), "dataText"), text);

        QVERIFY(model.requestEntryDetails("outputs", 1, &context));
        QVERIFY(worker.step(Lane::Utility));
        QVERIFY(model.entryDetailsBusy());
        model.close();
        stop([&] { drained = true; });
        QCOMPARE(*releases, 0);
        worker.drain();
        QVERIFY(drained);
        QCOMPARE(*releases, 1);
        publishAll();
        QCOMPARE(model.outputRows()->rowCount(), 0);
        QCOMPARE(model.entryDocument()->rowCount(), 0);
        QVERIFY(!model.entryDetailsBusy());
        QVERIFY(publications.empty());
        QVERIFY(worker.quiescent());
    }

    void completeEntryKeepsAllLongFieldsBehindBoundedRows() {
        Fixture f; const QString text(131072, 'x'); auto flow = Flow(1001);
        auto values = flow.value("outputs").toList(); auto first = values[0].toMap(); first["dataText"] = text;
        QVariantList requests; for (int i = 0; i < 20; ++i) requests.append(QVariantMap{{"requestId", QString::number(i)}});
        first["paymentRequests"] = requests; values[0] = first; flow["outputs"] = values;
        f.model->publishSource(flow, {}, &f.original, 4); f.finish(); QCOMPARE(f.model->outputRows()->rowCount(), 1001);
        const auto preview = Row(f.model->outputRows(), 0); QVERIFY(preview.value("textTruncated").toBool());
        QVERIFY(preview.value("dataText").toString().size() <= 1024); QCOMPARE(preview.value("paymentRequests").toList().size(), qsizetype{4});
        QVERIFY(f.model->requestEntryDetails("outputs", 0, &f.original)); QVERIFY(f.model->entryDetailsBusy()); QCOMPARE(f.model->entryDocument()->rowCount(), 0);
        f.finish(); QVERIFY(!f.model->entryDetailsBusy()); QCOMPARE(Field(f.model->entryDocument(), "dataText"), text);
        QCOMPARE(Field(f.model->entryDocument(), "paymentRequests[19].requestId"), QStringLiteral("19"));
        for (int i = 0; i < f.model->entryDocument()->rowCount(); ++i) QVERIFY(Row(f.model->entryDocument(), i).value("content").toString().size() <= 1024);
    }
    void capturedUnitReformatsRawRecipientWhileOlderFormatterPublicationIsHeld() {
        Fixture f; const QVariantList recipient{QVariantMap{{"amountSat", 1000}, {"amount", "0.00001000 BTC"}, {"label", "original"}}};
        f.model->publishSource(Flow(1), recipient, &f.original, 1, {}, 0); f.commands.drain();
        // The original formatted QString is still BTC, independently of the
        // newly captured flow display unit. A late old GUI turn cannot choose it.
        f.model->publishSource(Flow(1), recipient, &f.original, 2, {}, 3); f.publish();
        QCOMPARE(f.model->recipientRows()->rowCount(), 0); f.finish();
        QCOMPARE(Row(f.model->recipientRows(), 0).value("amount").toString(), QLocale{}.toString(qlonglong{1000}) + QStringLiteral(" sats"));
        QVERIFY(f.model->requestEntryDetails("recipients", 0, &f.original)); f.finish();
        QCOMPARE(Field(f.model->entryDocument(), "amount"), QLocale{}.toString(qlonglong{1000}) + QStringLiteral(" sats"));
    }
    void rejectionAndLostContextCannotReportEmptySuccess() {
        Fixture f; f.commands.reject_utility = true;
        f.model->publishSource(Flow(9), {}, &f.original, 1); f.publish(); QVERIFY(!f.model->pending()); QVERIFY(!f.model->error().isEmpty());
        auto context = std::make_unique<QObject>();
        f.model->publishSource(Flow(9), {}, context.get(), 2); f.commands.drain(); context.reset(); f.publish();
        QCOMPARE(f.model->context(), nullptr); QCOMPARE(f.model->outputRows()->rowCount(), 0); QVERIFY(f.model->error().isEmpty());
    }
    void closeDuringModelResetDoesNotPublishOrLeaveNativeOwnersOnGui() {
        Fixture f; f.model->publishSource(Flow(30), {}, &f.original, 1);
        QSignalSpy changed{f.model.get(), &FlowDetailsModel::snapshotChanged};
        connect(f.model->inputRows(), &QAbstractItemModel::modelAboutToBeReset, f.model.get(), [&] { f.model->close(); });
        f.finish(); QCOMPARE(changed.count(), 0); QCOMPARE(f.model->inputRows()->rowCount(), 0); QCOMPARE(f.model->outputRows()->rowCount(), 0);
        QVERIFY(!f.model->requestEntryDetails("outputs", 0, &f.original));
    }
    void realDomainProjectionAndOriginalAnchorRetirementAvoidGui() {
        using execution::Lane;
        execution::Executor executor{execution::Executor::Config{}};
        auto utility = executor.makeQueue(Lane::Utility);
        const auto values = std::make_shared<execution::ValueCleanupQueue>(
            std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility)));
        const auto retired = std::make_shared<std::atomic<QThread*>>(nullptr);
        std::atomic<QThread*> background{nullptr};
        QSemaphore entered, release, work_drained, values_retired;
        struct Anchor {
            std::shared_ptr<std::atomic<QThread*>> retired;
            ~Anchor() { *retired = QThread::currentThread(); }
        };
        QObject context;
        FlowDetailsModel model;
        const auto cleanup = qScopeGuard([&] {
            release.release();
            model.close();
            utility.close();
            utility.whenDrained([&] { work_drained.release(); });
            const bool drained = work_drained.tryAcquire(1, 5'000);
            values->close([&] { values_retired.release(); });
            const bool released = values_retired.tryAcquire(1, 5'000);
            executor.stopAndJoin();
            QVERIFY(drained && released);
        });
        QVERIFY(utility.post([&] { entered.release(); release.acquire(); }));
        QVERIFY(entered.tryAcquire(1, 5'000));
        model.setServices(presentation::QueuedPost(), [&](auto task) {
            const bool accepted = utility.post([&, task = std::move(task)] {
                background = QThread::currentThread();
                task->execute();
                task->deliver();
            });
            return execution::SubmitResult{{}, accepted ? execution::Rejection::None : execution::Rejection::Transport};
        }, [values](auto cleanup) { return values->hold(std::move(cleanup)); });
        auto anchor = std::make_shared<Anchor>();
        anchor->retired = retired;
        model.publishSource(Flow(1001), {}, &context, 11, std::move(anchor));
        QVERIFY(model.pending());
        QCOMPARE(model.outputRows()->rowCount(), 0);
        release.release();
        QTRY_VERIFY(!model.pending());
        QCOMPARE(model.outputRows()->rowCount(), 1001);
        QVERIFY(background.load() && background.load() != QThread::currentThread());
        model.close();
        QTRY_VERIFY(retired->load() != nullptr);
        QVERIFY(retired->load() != QThread::currentThread());
    }
};
BITCOINQML_REGISTER_QT_TEST(FlowDetailsModelTests)
#include <test_flowdetailsmodel.moc>
