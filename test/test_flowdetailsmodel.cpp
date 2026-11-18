// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/flowdetailsmodel.h>
#include <qml/backendexecutor.h>
#include <qml/retainedvalue.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/admissionhookscheduler.h>
#include <test/mocks/threadrecordingscheduler.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QScopeGuard>
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
    ManualRoutedExecution commands;
    const std::shared_ptr<AdmissionHookScheduler> scheduler{std::make_shared<AdmissionHookScheduler>(commands.scheduler)};
    std::deque<std::function<void()>> publications;
    QObject original, successor;
    std::unique_ptr<FlowDetailsModel> model{std::make_unique<FlowDetailsModel>()};
    Fixture() { model->setServices({scheduler, [this](std::function<void()> turn) { publications.push_back(std::move(turn)); }}); }
    ~Fixture() { scheduler->disarm(); if (model) model->closeAdmission(); model.reset(); commands.drain(); }
    void publish() { while (!publications.empty()) { auto turn = std::move(publications.front()); publications.pop_front(); turn(); } }
    void finish() { do { commands.drain(); publish(); } while (!commands.queuedIdle() || !commands.activeIdle()); }
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
        ManualRoutedExecution commands;
        auto scheduler = std::make_shared<AdmissionHookScheduler>(commands.scheduler);
        std::deque<std::function<void()>> duplicates;
        QObject context;
        auto model = std::make_unique<FlowDetailsModel>();
        const auto cleanup = qScopeGuard([&] {
            scheduler->disarm(); duplicates.clear(); if (model) model->closeAdmission(); model.reset(); commands.drain();
        });
        model->setServices({scheduler, [&](auto callback) {
            if (enqueue) duplicates.push_back(std::move(callback));
            throw std::runtime_error{"post failed"};
        }});
        QSignalSpy changed{model.get(), &FlowDetailsModel::snapshotChanged};
        const auto trigger = scheduler->armSubmit(execution::Lane::Domain, 1, [] {});
        model->publishSource(Flow(3, "original"), {}, &context, 42);
        const auto before = changed.count();
        commands.drain(); QCOMPARE(trigger->calls.load(), size_t{1}); QVERIFY(trigger->originalId.load() != 0);
        QVERIFY(!commands.scheduler->phase(execution::TaskId{trigger->originalId.load()}));
        QVERIFY(commands.queuedIdle()); QVERIFY(commands.activeIdle()); QVERIFY(model->pending());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        QVERIFY(!model->pending()); QVERIFY(model->error().isEmpty());
        QCOMPARE(model->outputRows()->rowCount(), 3); QCOMPARE(changed.count(), before + 1);
        QCOMPARE(Row(model->outputRows(), 2).value("dataText").toString(), QStringLiteral("original"));
        while (!duplicates.empty()) { auto callback = std::move(duplicates.front()); duplicates.pop_front(); callback(); }
        QCOMPARE(changed.count(), before + 1);
        model->closeAdmission(); model.reset(); commands.drain();
        QCOMPARE(commands.scheduler->pending(), size_t{0});
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
        auto anchor = execution::RetainValues<Anchor>(f.commands.scheduler, [&] {
            return std::make_unique<Anchor>(retired);
        });
        QVERIFY(anchor);
        const auto disarm = qScopeGuard([&] { f.scheduler->disarm(); });
        const QPointer<FlowDetailsModel> original{f.model.get()};
        QSignalSpy notices{f.model.get(), &FlowDetailsModel::snapshotChanged};
        QSignalSpy documents{f.model.get(), &FlowDetailsModel::entryDetailsChanged};
        int notices_after{-1}, documents_after{-1};
        auto change = [&] {
            if (action == 0) f.model->closeAdmission();
            else if (action == 1) f.model.reset();
            else f.model->publishSource(Flow(7, "successor"),
                QVariantList{QVariantMap{{"label", "successor recipient"}, {"amountSat", 2000}}}, &f.successor, 2);
            notices_after = notices.count();
            documents_after = documents.count();
        };
        const auto trigger = domain ? f.scheduler->armSubmit(execution::Lane::Domain, 1, change)
                                    : f.scheduler->armCleanup(1, change);
        f.model->publishSource(Flow(3, "original"),
            QVariantList{QVariantMap{{"label", "original recipient"}, {"amountSat", 1000}}}, &f.original, 1, std::move(anchor));
        QCOMPARE(trigger->calls.load(), size_t{1});
        QVERIFY(trigger->originalId.load() != 0);
        const execution::TaskId admitted{trigger->originalId.load()};
        QVERIFY(f.commands.scheduler->phase(admitted));
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
            QCOMPARE(f.model->requestEntryDetails("outputs", 0, &f.original, 1), quint64{0});
        } else {
            QCOMPARE(bool(original), action == 0);
            QCOMPARE(notices.count(), notices_after);
            QCOMPARE(documents.count(), documents_after);
            if (original) {
                QVERIFY(!f.model->pending());
                QCOMPARE(f.model->outputRows()->rowCount(), 0);
            }
        }
        if (f.model) f.model->closeAdmission();
        f.model.reset();
        f.finish();
        QVERIFY(!f.commands.scheduler->phase(admitted));
        QCOMPARE(f.commands.scheduler->pending(), size_t{0});
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
        f.model->closeAdmission();const auto closed_notices=notices.count();f.finish();
        QCOMPARE(notices.count(),closed_notices);QCOMPARE(f.model->outputRows()->rowCount(),0);
        QCOMPARE(f.commands.scheduler->pending(),size_t{0});QVERIFY(f.commands.quiescent());
    }
    void projectionWaitsForDomainOutcomeAndGuiPublication() {
        Fixture f; f.model->publishSource(Flow(1001), QVariantList{QVariantMap{{"label", "recipient"}, {"amountSat", 1000}}}, &f.original, 7);
        QVERIFY(f.model->pending()); QCOMPARE(f.model->outputRows()->rowCount(), 0);
        QVERIFY(f.commands.step(execution::Lane::Domain)); f.model->retryPublications(); QVERIFY(f.model->pending());
        QVERIFY(f.commands.step(execution::Lane::Domain)); QVERIFY(f.model->pending()); QCOMPARE(f.model->outputRows()->rowCount(), 0);
        f.publish(); QVERIFY(!f.model->pending()); QCOMPARE(f.model->outputRows()->rowCount(), 1001);
        QCOMPARE(f.model->recipientRows()->rowCount(), 1); QCOMPARE(f.model->context(), &f.original); QCOMPARE(f.model->revision(), quint64{7});
    }
    void successorSourceFencesAnOriginalHeldGuiPublication() {
        Fixture f; f.model->publishSource(Flow(9), {}, &f.original, 1); f.commands.drain();
        f.model->publishSource(Flow(17), {}, &f.successor, 2);
        f.publish(); QCOMPARE(f.model->outputRows()->rowCount(), 0); QVERIFY(f.model->pending());
        f.finish(); QCOMPARE(f.model->outputRows()->rowCount(), 17); QCOMPARE(f.model->context(), &f.successor);
        QCOMPARE(f.model->requestEntryDetails("outputs", 0, &f.original, 1), quint64{0});
    }
    void completeEntryKeepsAllLongFieldsBehindBoundedRows() {
        Fixture f; const QString text(131072, 'x'); auto flow = Flow(1001);
        auto values = flow.value("outputs").toList(); auto first = values[0].toMap(); first["dataText"] = text;
        QVariantList requests; for (int i = 0; i < 20; ++i) requests.append(QVariantMap{{"requestId", QString::number(i)}});
        first["paymentRequests"] = requests; values[0] = first; flow["outputs"] = values;
        f.model->publishSource(flow, {}, &f.original, 4); f.finish(); QCOMPARE(f.model->outputRows()->rowCount(), 1001);
        const auto preview = Row(f.model->outputRows(), 0); QVERIFY(preview.value("textTruncated").toBool());
        QVERIFY(preview.value("dataText").toString().size() <= 1024); QCOMPARE(preview.value("paymentRequests").toList().size(), qsizetype{4});
        QVERIFY(f.model->requestEntryDetails("outputs", 0, &f.original, 4)); QVERIFY(f.model->entryDetailsBusy()); QCOMPARE(f.model->entryDocument()->rowCount(), 0);
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
        QVERIFY(f.model->requestEntryDetails("recipients", 0, &f.original, 2)); f.finish();
        QCOMPARE(Field(f.model->entryDocument(), "amount"), QLocale{}.toString(qlonglong{1000}) + QStringLiteral(" sats"));
    }
    void rejectionAndLostContextCannotReportEmptySuccess() {
        Fixture f; f.commands.rejectLane(execution::Lane::Domain);
        f.model->publishSource(Flow(9), {}, &f.original, 1); f.publish(); QVERIFY(!f.model->pending()); QVERIFY(!f.model->error().isEmpty());
        f.commands.rejectLane({}); auto context = std::make_unique<QObject>();
        f.model->publishSource(Flow(9), {}, context.get(), 2); f.commands.drain(); context.reset(); f.publish();
        QCOMPARE(f.model->context(), nullptr); QCOMPARE(f.model->outputRows()->rowCount(), 0); QVERIFY(f.model->error().isEmpty());
    }
    void closeDuringModelResetDoesNotPublishOrLeaveNativeOwnersOnGui() {
        Fixture f; f.model->publishSource(Flow(30), {}, &f.original, 1);
        QSignalSpy changed{f.model.get(), &FlowDetailsModel::snapshotChanged};
        connect(f.model->inputRows(), &QAbstractItemModel::modelAboutToBeReset, f.model.get(), [&] { f.model->closeAdmission(); });
        f.finish(); QCOMPARE(changed.count(), 0); QCOMPARE(f.model->inputRows()->rowCount(), 0); QCOMPARE(f.model->outputRows()->rowCount(), 0);
        QCOMPARE(f.model->requestEntryDetails("outputs", 0, &f.original, 1), quint64{0});
    }
    void realDomainProjectionAndOriginalAnchorRetirementAvoidGui() {
        BackendExecutor executor; auto recording = std::make_shared<ThreadRecordingScheduler>(executor.scheduler());
        auto retired = std::make_shared<std::atomic<QThread*>>(nullptr);
        const auto cleanup = qScopeGuard([&] {
            if (executor.isDrained()) return;
            executor.shutdown();
            if (!QTest::qWaitFor([&] { return executor.isDrained(); }))
                QTest::qFail("The original flow executor did not physically drain", __FILE__, __LINE__);
        });
        struct Anchor {
            std::shared_ptr<std::atomic<QThread*>> retired;
            ~Anchor() { *retired = QThread::currentThread(); }
        };
        {
            QObject context; FlowDetailsModel model; model.setServices({recording, {}});
            auto anchor = std::make_shared<Anchor>(); anchor->retired = retired;
            model.publishSource(Flow(1001), {}, &context, 11, std::move(anchor));
            QTRY_VERIFY(!model.pending()); QCOMPARE(model.outputRows()->rowCount(), 1001);
            QVERIFY(recording->thread(execution::Lane::Domain) != nullptr && recording->thread(execution::Lane::Domain) != QThread::currentThread());
            model.closeAdmission();
        }
        QTRY_VERIFY(retired->load() != nullptr); QVERIFY(retired->load() != QThread::currentThread());
        executor.shutdown(); QTRY_VERIFY(executor.isDrained());
    }
};
BITCOINQML_REGISTER_QT_TEST(FlowDetailsModelTests)
#include <test_flowdetailsmodel.moc>
