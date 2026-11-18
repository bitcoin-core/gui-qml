// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/desktopintentowner.h>
#include <bitcoin-build-config.h> // IWYU pragma: keep
#include <qml/desktopintentargs.h>
#include <qml/models/desktopintentmodel.h>
#ifdef ENABLE_WALLET
#include <qml/models/bitcoinurimodel.h>
#endif
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/qt_test_registry.h>
#include <QtTest/QtTest>
#include <QFileOpenEvent>
#include <QScopeGuard>

namespace {
desktopintent::Input Input(int id, QString value = QStringLiteral("bitcoin:request"))
{
    return {QString::number(id), desktopintent::Kind::Text, std::move(value), desktopintent::Origin::Arguments};
}
}
class DesktopIntentTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void forwardingSnapshotMergesEarlyInputsWithoutAnAcceptedPrefix()
    {
        desktopintent::Queue queue;
        QVERIFY(queue.admit({Input(1)}));
        const auto snapshot = queue.prepareForwarding({Input(2), Input(1)});
        QVERIFY(snapshot); QCOMPARE(snapshot.inputs.size(), size_t{2});
        QCOMPARE(snapshot.inputs[0].producer_key, QStringLiteral("1"));
        QCOMPARE(snapshot.inputs[1].producer_key, QStringLiteral("2"));
        QVERIFY(queue.snapshot().phase == desktopintent::Phase::Forwarding);
        queue.readiness(1, true); QVERIFY(!queue.deliver(10));
        QVERIFY(queue.admit({Input(3)}).error == desktopintent::Error::Closed);
        QCOMPARE(queue.lookup(QStringLiteral("1"))->inspection, uint64_t{0});
        desktopintent::Queue full;
        std::vector<desktopintent::Input> before;
        for (int id = 1; id <= 15; ++id) before.push_back(Input(id));
        QVERIFY(full.admit(before));
        const auto rejected = full.prepareForwarding({Input(16), Input(17)});
        QVERIFY(rejected.error == desktopintent::Error::Capacity); QVERIFY(rejected.inputs.empty());
        QCOMPARE(full.snapshot().pending, size_t{15}); QVERIFY(!full.lookup(QStringLiteral("16")));
        full.stop(); QVERIFY(full.snapshot().phase == desktopintent::Phase::Stopped);
        desktopintent::Queue active;
        QVERIFY(active.admit({Input(1)})); active.readiness(1, true);
        const auto original = active.deliver(5); QVERIFY(original);
        QVERIFY(active.prepareForwarding({Input(2)}).error == desktopintent::Error::Invalid);
        QVERIFY(active.accepted(original->key, 99));
    }
    void forwardingFenceOrdersEarlierAdmissionsAndRefusesLateCapture()
    {
        ManualStartupExecution transport;
        auto queue = std::make_shared<execution::SerialQueue>(transport.executor().makeQueue(execution::Lane::Utility));
        auto owner = desktopintent::Owner::Create({}, {}, queue); QVERIFY(owner);
        bool early_admitted{false};
        QVERIFY(owner->admit({Input(1)}, [&](auto admitted) { early_admitted = static_cast<bool>(admitted); }));
        desktopintent::ForwardingSnapshot captured;
        QVERIFY(owner->prepareForwarding({Input(2)}, [&](auto snapshot) { captured = std::move(snapshot); }));
        QVERIFY(!owner->admit({Input(3)}, {}));
        QVERIFY(!owner->readiness(1, true));
        transport.drain(); QVERIFY(early_admitted); QVERIFY(captured);
        QCOMPARE(captured.inputs.size(), size_t{2});
        QCOMPARE(captured.inputs.front().producer_key, QStringLiteral("1"));
        QCOMPARE(captured.inputs.back().producer_key, QStringLiteral("2"));
        QVERIFY(owner->stop()); transport.drain(); owner.reset(); transport.drain();
        QVERIFY(transport.quiescent());
    }
    void actualQtFileOpenQueuesBeforeReadinessAndFencesLateEvents()
    {
        ManualStartupExecution commands;
        std::deque<std::function<void()>> gui;
        const auto post = [&](auto turn) { gui.push_back(std::move(turn)); };
        const auto drain = [&] {
            do {
                commands.drain();
                while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
            } while (!commands.quiescent());
        };
        DesktopIntentModel model{commands.executor(), post, false};
        const auto finish = qScopeGuard([&] { model.close(); drain(); });
        model.installFileOpenHandler();
        QFileOpenEvent link{QUrl{QStringLiteral("bitcoin:request?amount=1")}};
        QVERIFY(QCoreApplication::sendEvent(QCoreApplication::instance(), &link));
        QCOMPARE(model.pendingCount(), 0); // The event handler does not decide/admit on GUI.
        drain(); QCOMPARE(model.pendingCount(), 1); QVERIFY(!model.canInspect());
        QVERIFY(!model.error().isEmpty()); // Compiled/runtime wallet absence is explicit.
        QFileOpenEvent relative{QStringLiteral("relative-to-a-later-directory.psbt")};
        QVERIFY(QCoreApplication::sendEvent(QCoreApplication::instance(), &relative));
        drain(); QCOMPARE(model.pendingCount(), 1); // Never reinterpret an OS path against a later working directory.
        model.dismiss(); drain(); QCOMPARE(model.pendingCount(), 0);
        QFileOpenEvent invalid{QUrl{}};
        QVERIFY(QCoreApplication::sendEvent(QCoreApplication::instance(), &invalid));
        drain(); QVERIFY(!model.error().isEmpty()); QVERIFY(model.canDismiss());
        model.dismiss(); drain(); QVERIFY(model.error().isEmpty());
        model.close();
        QFileOpenEvent late{QUrl{QStringLiteral("bitcoin:late")}};
        QVERIFY(QCoreApplication::sendEvent(QCoreApplication::instance(), &late));
        drain(); QCOMPARE(model.pendingCount(), 0);
        QVERIFY(model.state() == DesktopIntentModel::State::Stopped);
        QVERIFY(commands.quiescent());
    }
    void actualArgumentsKeepCoreOptionsBeforeRequestsOnly()
    {
        QByteArray executable{"bitcoin-core-app"}, option{"-regtest"}, uri{"bitcoin:request"}, injected{"-platform"};
        char* valid[]{executable.data(), option.data(), uri.data()};
        const auto parsed = desktopintent::ParseArguments(3, valid, QStringLiteral("process"), QStringLiteral("/captured"));
        QVERIFY(parsed.error.isEmpty()); QCOMPARE(parsed.options_argc, 2);
        QCOMPARE(parsed.intents.size(), size_t{1}); QCOMPARE(parsed.intents.front().value, QStringLiteral("bitcoin:request"));
        char* invalid[]{executable.data(), option.data(), uri.data(), injected.data()};
        const auto refused = desktopintent::ParseArguments(4, invalid, QStringLiteral("process"), QStringLiteral("/captured"));
        QVERIFY(!refused.error.isEmpty()); QVERIFY(refused.intents.empty());
    }
    void argumentFilesUseCapturedDirectoryAndRejectRemoteAndInvalidUtf8()
    {
        QByteArray executable{"bitcoin-core-app"}, path{"draft.psbt"}, remote{"file://other-host/draft.psbt"}, invalid{1, static_cast<char>(0xff)};
        char* arguments[]{executable.data(), path.data()};
        const auto parsed = desktopintent::ParseArguments(2, arguments, QStringLiteral("process"), QStringLiteral("/captured"));
        QVERIFY(parsed.error.isEmpty()); QVERIFY(parsed.intents.front().kind == desktopintent::Kind::File);
        QCOMPARE(parsed.intents.front().value, QStringLiteral("/captured/draft.psbt"));
        arguments[1] = remote.data();
        QVERIFY(!desktopintent::ParseArguments(2, arguments, QStringLiteral("process"), QStringLiteral("/captured")).error.isEmpty());
        arguments[1] = invalid.data();
        QVERIFY(!desktopintent::ParseArguments(2, arguments, QStringLiteral("process"), QStringLiteral("/captured")).error.isEmpty());
    }
#ifdef ENABLE_WALLET
    void nativeAdmissionFenceBeatsAHeldReadyProjection_data()
    {
        QTest::addColumn<bool>("hold_factory");
        QTest::newRow("before-domain-handoff") << false;
        QTest::newRow("after-domain-before-gui-factory") << true;
    }
    void nativeAdmissionFenceBeatsAHeldReadyProjection()
    {
        QFETCH(bool, hold_factory);
        ManualStartupExecution commands;
        ManualStartupExecution worker;
        std::deque<std::function<void()>> gui;
        const auto post = [&](auto turn) { gui.push_back(std::move(turn)); };
        const auto drain = [&] {
            do {
                worker.drain();
                commands.drain();
                while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
            } while (!worker.quiescent() || !commands.quiescent());
        };
        BitcoinUriModel factory; factory.setServices(post, &commands.executor());
        DesktopIntentModel model{worker.executor(), post, true}; model.setFactory(&factory);
        const auto finish = qScopeGuard([&] { model.close(); factory.close(); drain(); });
        const auto native_closed = std::make_shared<std::atomic_bool>(false);
        model.setClosedFlag(native_closed);
        model.setReady(true); drain();
        QVERIFY(model.state() == DesktopIntentModel::State::Ready);
        if (hold_factory) {
            QVERIFY(model.owner()->admit({Input(1)}, {}));
            worker.drain(); // Utility has posted the factory handoff; GUI remains held.
            QVERIFY(!model.hasInspection()); QVERIFY(!gui.empty());
        }
        native_closed->store(true); // The terminal phase's GUI notification is deliberately held.
        if (!hold_factory) QVERIFY(model.owner()->admit({Input(1)}, {}));
        drain();
        QVERIFY(!model.hasInspection()); QCOMPARE(model.pendingCount(), 1);
        QVERIFY(model.state() == DesktopIntentModel::State::WaitingReady);
        model.inspectNext(); drain(); // An explicit retry still cannot bypass Native admission.
        QVERIFY(!model.hasInspection());
        QObject control_view;
        auto* control = factory.requestImport({paymentimport::Input::Kind::Text, QStringLiteral("bitcoin:control")}, {}, &control_view);
        QVERIFY(control); QCOMPARE(control->requestId(), quint64{1}); // Neither rejected handoff called the Source factory.
        delete control; model.close(); factory.close(); drain();
        QVERIFY(commands.quiescent());
    }
    void retainedFacadeDoesNotHoldPhysicalShutdownAfterStop()
    {
        ManualStartupExecution commands;
        ManualStartupExecution worker;
        std::deque<std::function<void()>> gui;
        const auto post = [&](auto turn) { gui.push_back(std::move(turn)); };
        const auto drain = [&] {
            do {
                worker.drain();
                commands.drain();
                while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
            } while (!worker.quiescent() || !commands.quiescent());
        };
        BitcoinUriModel factory; factory.setServices(post, &commands.executor());
        DesktopIntentModel model{worker.executor(), post, true}; model.setFactory(&factory);
        const auto finish = qScopeGuard([&] { model.close(); factory.close(); drain(); });
        QVERIFY(model.owner()->admit({Input(1)}, {})); model.setReady(true); drain();
        model.inspectNext(); drain(); QVERIFY(model.inspection());
        const QPointer<PaymentImportOperation> inspection{model.inspection()};
        QSignalSpy changed{&model, &DesktopIntentModel::changed};
        QSignalSpy inspection_changed{inspection.data(), &PaymentImportOperation::changed};
        int drained{0};
        const auto guarded_drain = qScopeGuard([&] { model.close(); drain(); });
        model.whenDrained([&] { ++drained; });
        model.close();
        QCOMPARE(drained, 0);
        QCOMPARE(changed.count(), 0); QCOMPARE(inspection_changed.count(), 0);
        factory.close(); drain();
        QVERIFY(inspection); // The cached presentation deliberately remains alive.
        QCOMPARE(drained, 1);
        QVERIFY(commands.quiescent());
    }
    void creationReceiptSurvivesProjectionLossAndCannotCreateTwice()
    {
        ManualStartupExecution commands;
        BitcoinUriModel factory; factory.setServices({}, &commands.executor());
        QObject presentation;
        const auto receipt = std::make_shared<paymentimport::CreationReceipt>();
        auto* operation = factory.requestImport({paymentimport::Input::Kind::Text, QStringLiteral("bitcoin:invalid")}, {}, &presentation, receipt);
        QVERIFY(operation); const auto original = operation->requestId();
        QVERIFY(receipt->state() == paymentimport::CreationReceipt::State::Accepted); QCOMPARE(receipt->source(), original);
        QVERIFY(!factory.requestImport({paymentimport::Input::Kind::Text, QStringLiteral("bitcoin:another")}, {}, &presentation, receipt));
        delete operation;
        commands.drain();
        QVERIFY(receipt->state() == paymentimport::CreationReceipt::State::Accepted); QCOMPARE(receipt->source(), original);
        const auto unused = std::make_shared<paymentimport::CreationReceipt>(); unused->withdraw();
        QVERIFY(!factory.requestImport({paymentimport::Input::Kind::Text, QStringLiteral("bitcoin:unused")}, {}, &presentation, unused));
        QVERIFY(unused->state() == paymentimport::CreationReceipt::State::NotAccepted);
        factory.close(); commands.drain();
    }
    void facadeDefersInspectionUntilReadyAndRetainsOriginalFactoryReceipt()
    {
        ManualStartupExecution commands;
        ManualStartupExecution worker;
        std::deque<std::function<void()>> gui;
        const auto post = [&](auto turn) { gui.push_back(std::move(turn)); };
        const auto drain = [&] {
            do {
                worker.drain();
                commands.drain();
                while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
            } while (!worker.quiescent() || !commands.quiescent());
        };
        BitcoinUriModel factory; factory.setServices(post, &commands.executor());
        DesktopIntentModel model{worker.executor(), post, true}; model.setFactory(&factory);
        const auto finish = qScopeGuard([&] { model.close(); factory.close(); drain(); });
        QVERIFY(model.owner()->admit({Input(1)}, {})); drain();
        QCOMPARE(model.pendingCount(), 1); QVERIFY(!model.canInspect()); model.inspectNext(); drain();
        QVERIFY(!model.inspection());
        model.setReady(true); drain(); QVERIFY(model.inspection()); QCOMPARE(model.pendingCount(), 0);
        const auto original = model.inspection()->requestId();
        QObject recipient_view;
        auto* operation = model.takeInspection(&recipient_view); QVERIFY(operation); QCOMPARE(operation->requestId(), original);
        QVERIFY(!model.inspection());
        auto original_owner = model.owner();
        model.close(); factory.close(); delete operation; drain();
        std::optional<desktopintent::Receipt> receipt;
        QVERIFY(original_owner->lookup(QStringLiteral("1"), [&](auto value) { receipt = value; })); drain();
        QVERIFY(receipt); QCOMPARE(receipt->inspection, static_cast<uint64_t>(original));
        original_owner.reset(); drain();
        QVERIFY(commands.quiescent());
    }
    void automaticInspectionKeepsOneOwnedPresentation_data()
    {
        QTest::addColumn<bool>("before_ack");
        QTest::newRow("close-after-receipt") << false;
        QTest::newRow("close-reentrantly-before-receipt") << true;
    }
    void automaticInspectionKeepsOneOwnedPresentation()
    {
        QFETCH(bool, before_ack);
        ManualStartupExecution commands;
        ManualStartupExecution worker;
        std::deque<std::function<void()>> gui;
        const auto post = [&](auto turn) { gui.push_back(std::move(turn)); };
        const auto drain = [&] {
            do {
                worker.drain();
                commands.drain();
                while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); }
            } while (!worker.quiescent() || !commands.quiescent());
        };
        BitcoinUriModel factory; factory.setServices(post, &commands.executor());
        DesktopIntentModel model{worker.executor(), post, true}; model.setFactory(&factory);
        const auto finish = qScopeGuard([&] { model.close(); factory.close(); drain(); });
        bool retired_first{false}; quint64 first{0};
        if (before_ack) QObject::connect(&model, &DesktopIntentModel::inspectionChanged, &model, [&] {
            if (!retired_first && model.inspection()) {
                retired_first = true; first = model.inspection()->requestId(); model.closeInspection();
            }
        });
        QVERIFY(model.owner()->admit({Input(1), Input(2)}, {})); model.setReady(true); drain();
        if (!before_ack) {
            QVERIFY(model.inspection()); first = model.inspection()->requestId();
            QCOMPARE(model.pendingCount(), 1); model.inspectNext(); drain();
            QCOMPARE(model.inspection()->requestId(), first);
            model.closeInspection(); drain();
        }
        QCOMPARE(first, quint64{1}); QVERIFY(model.inspection());
        QCOMPARE(model.inspection()->requestId(), quint64{2}); QCOMPARE(model.pendingCount(), 0);
        model.close(); factory.close(); drain();
        QVERIFY(commands.quiescent());
    }
#endif
    void failedBatchHasNoAcceptedPrefixOrConsumedIdentity()
    {
        desktopintent::Queue queue;
        std::vector<desktopintent::Input> full;
        for (int index = 1; index <= 15; ++index) full.push_back(Input(index));
        QVERIFY(queue.admit(full));
        QVERIFY(queue.admit({Input(16), Input(17)}).error == desktopintent::Error::Capacity);
        QCOMPARE(queue.snapshot().pending, size_t{15});
        QVERIFY(!queue.lookup(QStringLiteral("16")));
        const auto last = queue.admit({Input(16)}); QVERIFY(last);
        QCOMPARE(last.receipts.front().delivery, uint64_t{16});
        const auto duplicate = queue.admit({Input(16)}); QVERIFY(duplicate);
        QCOMPARE(duplicate.receipts.front().delivery, uint64_t{16});
        QVERIFY(queue.admit({Input(16, QStringLiteral("bitcoin:different"))}).error == desktopintent::Error::Invalid);
        QCOMPARE(queue.snapshot().pending, size_t{16});
    }
    void unaccountedDeliverySurvivesReadinessAndStop()
    {
        desktopintent::Queue queue; QVERIFY(queue.admit({Input(1)}));
        queue.readiness(1, true);
        const auto original = queue.deliver(41); QVERIFY(original);
        QVERIFY(queue.unknown(original->key));
        for (int index = 2; index <= 16; ++index) QVERIFY(queue.admit({Input(index)}));
        QVERIFY(!queue.deliver(99));
        auto wrong = original->key; ++wrong.route;
        QVERIFY(!queue.accepted(wrong, 100));
        QVERIFY(queue.snapshot().active == original->key);
        queue.stop();
        QVERIFY(queue.snapshot().phase == desktopintent::Phase::Stopping);
        QCOMPARE(queue.snapshot().pending, size_t{1});
        QVERIFY(queue.lookup(QStringLiteral("2"))->disposition == desktopintent::Disposition::DroppedOnStop);
        QVERIFY(queue.accepted(original->key, 700));
        QVERIFY(queue.snapshot().phase == desktopintent::Phase::Stopped);
        QCOMPARE(queue.lookup(QStringLiteral("1"))->inspection, uint64_t{700});
        QVERIFY(queue.admit({Input(17)}).error == desktopintent::Error::Closed);
    }
    void byteBudgetAndOldReadinessCannotAuthorizeDelivery()
    {
        desktopintent::Queue queue;
        std::vector<desktopintent::Input> batch;
        for (int index = 1; index <= 4; ++index)
            batch.push_back(Input(index, QString(desktopintent::Queue::MAX_INPUT_CHARS, QLatin1Char{'x'})));
        QVERIFY(queue.admit(batch));
        QVERIFY(queue.admit({Input(5)}).error == desktopintent::Error::Capacity);
        queue.readiness(2, false); queue.readiness(1, true); QVERIFY(!queue.deliver(4));
        queue.readiness(3, true);
        const auto active = queue.deliver(4); QVERIFY(active);
        QVERIFY(queue.rejected(active->key));
        QCOMPARE(queue.snapshot().pending, size_t{4}); QVERIFY(!queue.deliver(5));
        queue.readiness(4, true);
        const auto retry = queue.deliver(5); QVERIFY(retry);
        QCOMPARE(retry->key.delivery, active->key.delivery);
        QVERIFY(retry->key.route != active->key.route);
        QVERIFY(!queue.accepted(active->key, 1));
    }
    void sharedStopDrainsAfterTheOriginalInspectionReply()
    {
        desktopintent::Snapshot snapshot;
        int drained{0}, creations{0}, queries{0}, retirements{0};
        desktopintent::Sink::Completed original_reply, query_reply;
        ManualStartupExecution worker;
        auto queue = std::make_shared<execution::SerialQueue>(worker.executor().makeQueue(execution::Lane::Utility));
        auto owner = desktopintent::Owner::Create({}, [&](auto value) { snapshot = value; }, queue);
        auto sink = std::make_shared<desktopintent::Sink>();
        sink->identity = 1;
        sink->inspect = [&](desktopintent::Delivery, auto done) {
            ++creations;
            original_reply = std::move(done);
            return execution::SubmitResult{};
        };
        sink->query = [&](desktopintent::DeliveryKey, auto done) {
            ++queries;
            query_reply = std::move(done);
            return execution::SubmitResult{};
        };
        sink->retire = [&](desktopintent::DeliveryKey) { ++retirements; };
        const auto finish = qScopeGuard([&] {
            if (owner) { owner->stop(); owner.reset(); }
            worker.drain();
            if (original_reply) original_reply({desktopintent::Knowledge::NotAccepted, 0});
            if (query_reply) query_reply({desktopintent::Knowledge::NotAccepted, 0});
            worker.drain();
        });

        QVERIFY(owner);
        QVERIFY(owner->admit({Input(1)}, {}));
        QVERIFY(owner->readiness(1, true));
        QVERIFY(owner->review(sink));
        while (!original_reply) QVERIFY(worker.step(execution::Lane::Utility));
        QVERIFY(original_reply);
        QCOMPARE(snapshot.phase, desktopintent::Phase::Delivering);
        QVERIFY(owner->stop());
        queue->whenDrained([&] { ++drained; });
        owner.reset();
        worker.drain();
        QCOMPARE(snapshot.phase, desktopintent::Phase::Stopping);
        QCOMPARE(drained, 0);
        QCOMPARE(creations, 1);
        QCOMPARE(queries, 1);
        QCOMPARE(retirements, 1);

        original_reply({desktopintent::Knowledge::Accepted, 91});
        QCOMPARE(drained, 0);
        worker.drain();
        QCOMPARE(snapshot.phase, desktopintent::Phase::Stopped);
        QCOMPARE(drained, 1);
        QVERIFY(query_reply);
        const auto sequence = snapshot.sequence;
        query_reply({desktopintent::Knowledge::NotAccepted, 0});
        original_reply({desktopintent::Knowledge::NotAccepted, 0});
        worker.drain();
        QCOMPARE(snapshot.sequence, sequence);
        QCOMPARE(drained, 1);
        QVERIFY(worker.quiescent());
    }
    void lostFactoryAcknowledgmentQueriesOnlyTheOriginalSink()
    {
        ManualStartupExecution transport;
        auto queue = std::make_shared<execution::SerialQueue>(transport.executor().makeQueue(execution::Lane::Utility));
        desktopintent::Services::Time now{};
        desktopintent::Snapshot snapshot;
        auto actor = desktopintent::Owner::Create({[&] { return now; }, {}, std::chrono::milliseconds{10}},
            [&](auto value) { snapshot = value; }, queue);
        QVERIFY(actor);
        auto sink = std::make_shared<desktopintent::Sink>();
        int created{0}, queried{0}, retired{0};
        uint64_t inspect_target{0}, query_target{0}, retired_target{0};
        desktopintent::Sink::Completed late;
        sink->identity = 10;
        sink->inspect = [&](auto delivery, auto reply) {
            ++created; late = std::move(reply); inspect_target = delivery.key.target;
            return execution::SubmitResult{};
        };
        sink->query = [&](auto key, auto reply) {
            ++queried; query_target = key.target;
            reply({desktopintent::Knowledge::Accepted, 900}); return execution::SubmitResult{};
        };
        sink->retire = [&](auto key) { ++retired; retired_target = key.target; };
        QVERIFY(actor->admit({Input(1)}, {})); QVERIFY(actor->readiness(1, true)); QVERIFY(actor->review(sink));
        transport.drain(); QCOMPARE(created, 1);
        QVERIFY(snapshot.phase == desktopintent::Phase::Delivering);
        now += std::chrono::milliseconds{11}; QVERIFY(actor->tick()); transport.drain();
        QVERIFY(snapshot.phase == desktopintent::Phase::ReconcilingDelivery);
        auto other = std::make_shared<desktopintent::Sink>(*sink); other->identity = 99;
        QVERIFY(actor->readiness(2, true)); QVERIFY(actor->review(other)); transport.drain();
        QCOMPARE(created, 1);
        QVERIFY(actor->stop()); transport.drain();
        QCOMPARE(queried, 1); QCOMPARE(retired, 1);
        QCOMPARE(inspect_target, uint64_t{10}); QCOMPARE(query_target, uint64_t{10}); QCOMPARE(retired_target, uint64_t{10});
        QVERIFY(snapshot.phase == desktopintent::Phase::Stopped);
        late({desktopintent::Knowledge::NotAccepted, 0}); transport.drain();
        QVERIFY(snapshot.phase == desktopintent::Phase::Stopped);
        std::optional<desktopintent::Receipt> receipt;
        QVERIFY(actor->lookup(QStringLiteral("1"), [&](auto value) { receipt = value; })); transport.drain();
        QVERIFY(receipt); QCOMPARE(receipt->inspection, uint64_t{900});
        late = {}; sink.reset(); other.reset(); actor.reset(); transport.drain();
        QVERIFY(transport.quiescent());
    }
};
BITCOINQML_REGISTER_QT_TEST(DesktopIntentTests)
#include <test_desktopintent.moc>
