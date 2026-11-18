// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/networkcommand.h>
#include <qml/models/networkmodel.h>
#include <qml/backendexecutor.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/qt_test_registry.h>

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QScopeGuard>
#include <QSemaphore>
#include <QSignalSpy>
#include <QThread>
#include <QTimer>
#include <QtTest/QtTest>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>
#include <stdexcept>
#include <utility>

namespace {
using namespace networkcommand;
Effect Applied(uint64_t id, bool active, uint64_t before, uint64_t after)
{
    Effect result;
    result.id = id; result.status = EffectStatus::Applied;
    result.attempted = true; result.succeeded = true; result.own_frontier = true;
    result.before = before; result.after = result.current = after;
    result.observation = Observation{active, after};
    return result;
}
struct Fixture {
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui;
    bool active{true}, throw_write{false}, throw_read{false}, throw_post{false}, advance_generation_on_write{true};
    uint64_t generation{1};
    int reads{0}, writes{0}, generation_reads{0};
    std::unique_ptr<NetworkModel> model;
    Fixture()
    {
        NetworkModel::Ports ports;
        ports.execution = {execution.scheduler, [this](auto turn) {
            if (std::exchange(throw_post, false)) throw std::runtime_error{"GUI transport rejected"};
            gui.push_back(std::move(turn));
        }};
        ports.observe = [this] {
            ++reads;
            if (std::exchange(throw_read, false)) throw std::runtime_error{"Observation unavailable"};
            return Observation{active, generation};
        };
        ports.generation = [this] { ++generation_reads; return generation; };
        ports.set_active = [this](bool value) {
            ++writes; if(advance_generation_on_write)rpc(value);else active=value;
            if (std::exchange(throw_write, false)) throw std::runtime_error{"Original effect outcome unavailable"};
        };
        model = std::make_unique<NetworkModel>(std::move(ports));
    }
    ~Fixture() { model.reset(); settle(); }
    void rpc(bool value) { if (active != value) { active = value; ++generation; } }
    void publish() { while (!gui.empty()) { auto turn = std::move(gui.front()); gui.pop_front(); turn(); } }
    void settle()
    {
        for (int pass = 0; pass < 8; ++pass) {
            execution.drain(); publish();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::MetaCall);
        }
    }
    void ready() { model->setAvailable(true); settle(); }
};
} // namespace

class NetworkCommandTests : public QObject
{
    Q_OBJECT
private Q_SLOTS:
    void observationsAndAvailabilityAuthorizeEffects()
    {
        Command command;
        QVERIFY(!command.request({1, false, 0, 0}));
        QCOMPARE(command.projection().outcome, Outcome::Rejected);
        QVERIFY(!command.projection().known);
        command.setAvailable(true, 1);
        QVERIFY(!command.projection().ready);
        QVERIFY(command.observe({true, 7}));
        QVERIFY(command.projection().ready);
        const auto intent = command.request({2, false, 7, 1});
        QVERIFY(intent);
        QVERIFY(command.projection().active);
        QVERIFY(command.projection().pending);
        QVERIFY(!command.observe({false, 6}));
        QVERIFY(command.projection().active);
        QVERIFY(!command.finish(Applied(2, false, 7, 8)));
        QVERIFY(!command.projection().active);
        QCOMPARE(command.projection().outcome, Outcome::Succeeded);
    }

    void ownNotificationAllowsLatestSuccessor()
    {
        Command command;
        command.setAvailable(true, 1); command.observe({true, 10});
        QVERIFY(command.request({1, false, 10, 1}));
        QVERIFY(!command.request({2, true, 10, 1}));
        command.observe({false, 11});
        const auto successor = command.finish(Applied(1, false, 10, 11));
        QVERIFY(successor);
        QCOMPARE(successor->id, uint64_t{2});
        QCOMPARE(successor->base, uint64_t{11});
        QVERIFY(!command.projection().active);
        QVERIFY(!command.finish(Applied(2, true, 11, 12)));
        QCOMPARE(command.projection().outcome, Outcome::Succeeded);
        QVERIFY(command.projection().active);
    }

    void ownedWriteWithoutCallbackRetainsItsFreshReadAndLatestSuccessor()
    {
        Fixture fixture;
        fixture.advance_generation_on_write = false;
        fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QVERIFY(fixture.execution.step(execution::Lane::Core));
        QCOMPARE(fixture.writes, 1);
        QVERIFY(!fixture.active);
        // The original physical read is held before Domain accounting. The
        // latest explicit successor is still based on the current public clock.
        QVERIFY(fixture.model->requestActive(true));
        fixture.settle();
        QCOMPARE(fixture.writes, 2);
        QVERIFY(fixture.active);
        QVERIFY(fixture.model->observedActive());
        QVERIFY(!fixture.model->pending());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Succeeded);
        QCOMPARE(fixture.model->requestId(), quint64{2});
        QVERIFY(fixture.model->effect().attempted);
        QVERIFY(fixture.model->effect().succeeded);
        QCOMPARE(fixture.model->effect().after, uint64_t{1});
    }

    void passiveFacadeRetirementDrainsTheOriginalAdmittedAction_data()
    {
        QTest::addColumn<bool>("domain_consumed");
        QTest::newRow("original-GUI-submitted") << false;
        QTest::newRow("original-Native-turn-queued") << true;
    }
    void passiveFacadeRetirementDrainsTheOriginalAdmittedAction()
    {
        QFETCH(bool, domain_consumed);
        Fixture fixture;
        fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        if(domain_consumed)QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QCOMPARE(fixture.writes, 0);
        fixture.model.reset();
        QCOMPARE(fixture.writes, 0);
        fixture.settle();
        QCOMPARE(fixture.writes, 1);
        QVERIFY(!fixture.active);
        QVERIFY(fixture.execution.quiescent());
        fixture.settle();
        QCOMPARE(fixture.writes, 1);
    }

    void cachedNoOpCannotSkipTheCurrentNativeValue()
    {
        Fixture fixture;
        fixture.ready();
        QVERIFY(fixture.model->observedActive());
        fixture.active = false; // The public value changed before its notice.
        QVERIFY(fixture.model->requestActive(true));
        QCOMPARE(fixture.writes, 0);
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QCOMPARE(fixture.writes, 0);
        QVERIFY(fixture.execution.step(execution::Lane::Core));
        QCOMPARE(fixture.writes, 1);
        fixture.settle();
        QVERIFY(fixture.active);
        QVERIFY(fixture.model->observedActive());
        QVERIFY(!fixture.model->pending());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Succeeded);
        QVERIFY(fixture.model->effect().attempted);
    }

    void capturedExternalObservationOwnsAnIntentBeforeItsGuiProjection()
    {
        Fixture fixture;fixture.ready();
        fixture.rpc(false);
        fixture.model->observe(false,fixture.generation);
        // Hold both Domain intake and its GUI frame. This explicit successor
        // is based on the captured external notice, not the old displayed one.
        QVERIFY(fixture.model->requestActive(true));
        QCOMPARE(fixture.writes,0);
        fixture.settle();
        QCOMPARE(fixture.writes,1);QVERIFY(fixture.active);
        QVERIFY(fixture.model->observedActive());QVERIFY(!fixture.model->pending());
        QCOMPARE(fixture.model->outcome(),NetworkModel::Succeeded);
        QCOMPARE(fixture.model->effect().before,uint64_t{2});
        QCOMPARE(fixture.model->effect().after,uint64_t{3});
    }

    void externalObservationSupersedesOlderQueuedIntent()
    {
        Command command;
        command.setAvailable(true, 1); command.observe({true, 10});
        QVERIFY(command.request({1, false, 10, 1}));
        command.request({2, true, 10, 1});
        command.observe({false, 13});
        QVERIFY(!command.finish(Applied(1, false, 10, 11)));
        QCOMPARE(command.projection().request_id, uint64_t{2});
        QCOMPARE(command.projection().outcome, Outcome::Superseded);
        QCOMPARE(command.projection().generation, uint64_t{13});
        QVERIFY(!command.projection().pending);
        QVERIFY(command.projection().effect.attempted);
        QVERIFY(command.projection().effect.succeeded);
    }

    void freshIntentAfterExternalObservationRemainsExplicit()
    {
        Command command;
        command.setAvailable(true, 1); command.observe({true, 10});
        QVERIFY(command.request({1, false, 10, 1}));
        command.observe({false, 13});
        command.request({2, true, 13, 1});
        const auto next = command.finish(Applied(1, false, 10, 11));
        QVERIFY(next);
        QCOMPARE(next->id, uint64_t{2});
        QCOMPARE(next->base, uint64_t{13});
    }

    void unknownOutcomeDoesNotReplayQueuedDesire()
    {
        Command command;
        command.setAvailable(true, 1); command.observe({true, 10});
        QVERIFY(command.request({1, false, 10, 1}));
        command.request({2, true, 10, 1});
        Effect unknown; unknown.id = 1; unknown.attempted = true; unknown.status = EffectStatus::Uncertain;
        QVERIFY(!command.finish(unknown));
        QCOMPARE(command.projection().outcome, Outcome::Uncertain);
        QVERIFY(!command.projection().pending);
        command.observe({false, 11});
        QCOMPARE(command.projection().outcome, Outcome::Uncertain);
        QVERIFY(command.request({3, true, 11, 1}));
    }

    void olderReceiptDoesNotRewriteNewerRejection()
    {
        Command command;
        command.setAvailable(true, 1); command.observe({true, 10});
        QVERIFY(command.request({1, false, 10, 1}));
        command.setAvailable(false, 2);
        command.request({2, true, 10, 2});
        Effect unknown; unknown.id = 1; unknown.attempted = true; unknown.status = EffectStatus::Uncertain;
        command.finish(unknown);
        QCOMPARE(command.projection().request_id, uint64_t{2});
        QCOMPARE(command.projection().outcome, Outcome::Rejected);
        QCOMPARE(command.projection().error, Error::Unavailable);
        QCOMPARE(command.projection().effect.status, EffectStatus::Uncertain);
    }

    void closeRetainsFactualOriginalEffect()
    {
        Command command;
        command.setAvailable(true, 1); command.observe({true, 10});
        QVERIFY(command.request({1, false, 10, 1}));
        command.close();
        QVERIFY(command.projection().pending);
        command.finish(Applied(1, false, 10, 11));
        QCOMPARE(command.projection().outcome, Outcome::Closed);
        QVERIFY(!command.projection().pending);
        QVERIFY(!command.projection().active);
        QVERIFY(command.projection().effect.attempted);
        QVERIFY(command.projection().effect.succeeded);
    }

    void modelStartsUnknownAndGettersNeverReadNative()
    {
        Fixture fixture;
        QVERIFY(!fixture.model->known()); QVERIFY(!fixture.model->ready());
        QVERIFY(!fixture.model->requestActive(false));
        for (int n = 0; n < 100; ++n) {
            (void)fixture.model->observedActive(); (void)fixture.model->known();
            (void)fixture.model->ready(); (void)fixture.model->pending();
            (void)fixture.model->error(); (void)fixture.model->outcome();
        }
        QCOMPARE(fixture.reads, 0); QCOMPARE(fixture.writes, 0); QCOMPARE(fixture.generation_reads, 0);
        fixture.ready();
        QVERIFY(fixture.model->known()); QVERIFY(fixture.model->ready()); QVERIFY(fixture.model->observedActive());
        QCOMPARE(fixture.reads, 1);
    }

    void guiIngressPendingWaitsForItsOwnAcknowledgment()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.model->pending());
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QVERIFY(fixture.model->requestActive(true));
        QCOMPARE(fixture.model->requestId(), quint64{2});
        fixture.publish(); // Only request1 has reached Domain.
        QVERIFY(fixture.model->pending());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Pending);
        fixture.settle();
        QVERIFY(!fixture.model->pending());
        QCOMPARE(fixture.model->requestId(), quint64{2});
        QCOMPARE(fixture.model->outcome(), NetworkModel::Succeeded);
    }

    void failedInitialObservationStaysUnknownAndCanBeRefreshed()
    {
        Fixture fixture; fixture.throw_read = true; fixture.ready();
        QVERIFY(!fixture.model->known()); QVERIFY(!fixture.model->ready());
        QVERIFY(!fixture.model->pending()); QVERIFY(!fixture.model->error().isEmpty());
        QCOMPARE(fixture.writes, 0);
        fixture.ready();
        QVERIFY(fixture.model->known()); QVERIFY(fixture.model->ready());
        QVERIFY(fixture.model->error().isEmpty());
        QCOMPARE(fixture.reads, 2);
    }

    void rejectedCoalescedReadIsPublishedWithoutAdmissionRescue()
    {
        Fixture fixture;
        fixture.model->setAvailable(true);
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        const auto held = fixture.execution.dispatchNext(execution::Lane::Core); QVERIFY(held);
        fixture.model->setAvailable(true);
        while (fixture.execution.step(execution::Lane::Domain)) {}
        fixture.execution.rejectLane(execution::Lane::Core);
        QVERIFY(fixture.execution.finishWork(*held)); fixture.settle();
        QVERIFY(!fixture.model->known()); QVERIFY(!fixture.model->ready());
        QVERIFY(!fixture.model->pending()); QVERIFY(!fixture.model->error().isEmpty());
        QCOMPARE(fixture.writes, 0); QCOMPARE(fixture.reads, 0);
        QCOMPARE(fixture.execution.coreSubmissions(), size_t{2});
        fixture.ready();
        QVERIFY(fixture.model->known()); QVERIFY(fixture.model->ready()); QVERIFY(fixture.model->error().isEmpty());
        QCOMPARE(fixture.reads, 1); QCOMPARE(fixture.execution.coreSubmissions(), size_t{3});
    }

    void guiBurstCoalescesWithoutEffectOrGetterLoop()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        const auto held = fixture.execution.dispatchNext(execution::Lane::Core);
        QVERIFY(held);
        for (int n = 0; n < 100; ++n) QVERIFY(fixture.model->requestActive(n % 2 != 0));
        QCOMPARE(fixture.writes, 0); QCOMPARE(fixture.reads, 1);
        QVERIFY(fixture.execution.finishWork(*held));
        fixture.settle();
        QCOMPARE(fixture.writes, 2); QCOMPARE(fixture.reads, 3);
        QVERIFY(fixture.active); QVERIFY(fixture.model->observedActive());
        QVERIFY(!fixture.model->pending());
        QCOMPARE(fixture.model->requestId(), quint64{101});
    }

    void nativeGenerationFenceSkipsSupersededWrite()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        fixture.rpc(false); fixture.rpc(true);
        fixture.model->observe(fixture.active, fixture.generation);
        fixture.settle();
        QCOMPARE(fixture.writes, 0);
        QCOMPARE(fixture.model->outcome(), NetworkModel::Superseded);
        QVERIFY(!fixture.model->effect().attempted);
        QVERIFY(fixture.model->observedActive()); QVERIFY(!fixture.model->pending());
    }

    void queuedSuccessorYieldsToLaterRpcObservation()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QVERIFY(fixture.execution.step(execution::Lane::Core)); // Captured first result; Domain receipt remains held.
        QCOMPARE(fixture.writes, 1);
        QVERIFY(fixture.model->requestActive(true));
        fixture.rpc(true); fixture.rpc(false); // Both transitions notify on real Core.
        fixture.model->observe(fixture.active, fixture.generation);
        fixture.settle();
        QCOMPARE(fixture.writes, 1);
        QVERIFY(!fixture.active); QVERIFY(!fixture.model->observedActive());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Superseded);
        QVERIFY(!fixture.model->pending());
    }

    void sameObservedValueStillNeedsCurrentNativeFence()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(true));
        QVERIFY(fixture.model->pending());
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        fixture.rpc(false);
        fixture.model->observe(fixture.active, fixture.generation);
        fixture.settle();
        QCOMPARE(fixture.writes, 0);
        QVERIFY(!fixture.model->observedActive());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Superseded);
        QVERIFY(!fixture.model->effect().attempted);
    }

    void ownPublicNotificationDoesNotCancelQueuedSuccessor()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QVERIFY(fixture.execution.step(execution::Lane::Core));
        fixture.model->observe(fixture.active, fixture.generation);
        QVERIFY(fixture.model->requestActive(true));
        fixture.settle();
        QCOMPARE(fixture.writes, 2); QVERIFY(fixture.active);
        QCOMPARE(fixture.model->outcome(), NetworkModel::Succeeded);
    }

    void currentObservationFencesOlderSuccessfulReceipt()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QVERIFY(fixture.execution.step(execution::Lane::Core));
        fixture.rpc(true);
        fixture.model->observe(fixture.active, fixture.generation);
        fixture.settle();
        QVERIFY(fixture.model->observedActive());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Superseded);
        QVERIFY(fixture.model->effect().attempted); QVERIFY(fixture.model->effect().succeeded);
    }

    void laterIdleObservationSupersedesWithoutReassertingDesire()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false)); fixture.settle();
        QCOMPARE(fixture.model->outcome(), NetworkModel::Succeeded);
        fixture.rpc(true); fixture.model->observe(fixture.active, fixture.generation);
        fixture.settle();
        QCOMPARE(fixture.writes, 1);
        QVERIFY(fixture.model->observedActive());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Superseded);
        QVERIFY(fixture.model->effect().succeeded);
    }

    void unknownNativeEffectOnlySchedulesObservation()
    {
        Fixture fixture; fixture.ready(); fixture.throw_write = true;
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QVERIFY(fixture.model->requestActive(true));
        fixture.settle();
        QCOMPARE(fixture.writes, 1); QVERIFY(!fixture.active);
        QCOMPARE(fixture.model->outcome(), NetworkModel::Uncertain);
        QVERIFY(!fixture.model->pending()); QVERIFY(!fixture.model->error().isEmpty());
        QVERIFY(fixture.model->known()); QVERIFY(!fixture.model->observedActive());
        QVERIFY(fixture.model->requestActive(true));
        fixture.settle();
        QCOMPARE(fixture.writes, 2); QVERIFY(fixture.active);
    }

    void rejectedRecoveryReadDoesNotRewriteUncertainMutation()
    {
        Fixture fixture; fixture.ready(); fixture.throw_write = true;
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QVERIFY(fixture.execution.step(execution::Lane::Core));
        fixture.execution.rejectLane(execution::Lane::Core);
        fixture.settle();
        QCOMPARE(fixture.model->outcome(), NetworkModel::Uncertain); QVERIFY(!fixture.model->pending());
        QVERIFY(fixture.model->effect().attempted); QCOMPARE(fixture.model->effect().status, EffectStatus::Uncertain);
        QCOMPARE(fixture.writes, 1); QCOMPARE(fixture.reads, 1);
        QCOMPARE(fixture.execution.coreSubmissions(), size_t{3});
        fixture.model->observe(fixture.active, fixture.generation); fixture.settle();
        QCOMPARE(fixture.model->outcome(), NetworkModel::Uncertain);
        QCOMPARE(fixture.writes, 1);
        QVERIFY(fixture.model->requestActive(true)); fixture.settle();
        QCOMPARE(fixture.writes, 2); QVERIFY(fixture.active);
    }

    void ordinaryRootClosureRejectsFreshIntent()
    {
        Fixture fixture; fixture.ready();
        fixture.execution.scheduler->stopAcceptingWork();
        QVERIFY(!fixture.model->requestActive(false));
        QVERIFY(!fixture.model->pending());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Rejected);
        QCOMPARE(fixture.writes, 0);
        fixture.rpc(false); fixture.model->observe(fixture.active, fixture.generation);
        fixture.settle();
        QVERIFY(!fixture.model->observedActive());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Rejected);
    }

    void nativeAdmissionRejectionIsKnownAndRetryIsExplicit()
    {
        Fixture fixture; fixture.ready();
        fixture.execution.rejectLane(execution::Lane::Core);
        QVERIFY(fixture.model->requestActive(false));
        fixture.settle();
        QCOMPARE(fixture.writes, 0);
        QCOMPARE(fixture.model->outcome(), NetworkModel::Rejected);
        QVERIFY(!fixture.model->effect().attempted);
        QVERIFY(!fixture.model->pending());
        QVERIFY(fixture.model->requestActive(false)); fixture.settle();
        QCOMPARE(fixture.writes, 1);
    }


    void availabilityRevisionFencesAlreadyQueuedEffect()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        fixture.model->setAvailable(false); fixture.model->setAvailable(true);
        QVERIFY(!fixture.model->ready());
        fixture.settle();
        QCOMPARE(fixture.writes, 0);
        QVERIFY(!fixture.model->effect().attempted);
        QVERIFY(fixture.model->ready()); QVERIFY(!fixture.model->pending());
    }

    void rejectedGuiTransportStillAcknowledgesOriginalOutcome()
    {
        Fixture fixture; fixture.ready();
        fixture.throw_post = true;
        QVERIFY(fixture.model->requestActive(false));
        fixture.settle();
        QCOMPARE(fixture.writes, 1);
        QVERIFY(!fixture.model->pending());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Succeeded);
    }

    void closeRetiresSnapshotsAndPortsBeforeDrained()
    {
        Fixture fixture; fixture.ready();
        QSignalSpy drained{fixture.model.get(), &NetworkModel::shutdownFinished};
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        QVERIFY(fixture.execution.step(execution::Lane::Core));
        fixture.model->close();
        QVERIFY(!fixture.model->drained());
        fixture.settle();
        QCOMPARE(drained.size(), 1);
        QVERIFY(fixture.model->drained()); QVERIFY(!fixture.model->pending());
        QVERIFY(fixture.model->effect().attempted);
        QCOMPARE(fixture.model->outcome(), NetworkModel::Closed);
        fixture.model->close(); fixture.settle();
        QCOMPARE(drained.size(), 1);
        QVERIFY(fixture.execution.quiescent());
    }

    void closeBeforeNativeEffectRecordsNoAttempt()
    {
        Fixture fixture; fixture.ready();
        QVERIFY(fixture.model->requestActive(false));
        QVERIFY(fixture.execution.step(execution::Lane::Domain));
        fixture.model->close(); fixture.settle();
        QCOMPARE(fixture.writes, 0);
        QVERIFY(!fixture.model->effect().attempted);
        QVERIFY(fixture.model->drained());
        QVERIFY(!fixture.model->pending());
        QCOMPARE(fixture.model->outcome(), NetworkModel::Closed);
    }

    void realNativeEffectAndPortRetirementOutliveFacade()
    {
        struct Trace {
            std::atomic_bool active{true}, off_gui{true}, destructor_off_gui{true};
            std::atomic<uint64_t> generation{1};
            std::atomic_int writes{0}, destroyed{0};
        };
        struct Captured {
            std::shared_ptr<Trace> trace;
            ~Captured() {
                trace->destructor_off_gui = QThread::currentThread() != QCoreApplication::instance()->thread();
                ++trace->destroyed;
            }
        };
        const auto trace = std::make_shared<Trace>();
        auto captured = std::make_shared<Captured>(); captured->trace = trace;
        BackendExecutor executor{execution::Options{}};
        QSemaphore entered, release;
        NetworkModel::Ports ports;
        ports.execution.scheduler = executor.scheduler();
        const auto native = [trace] { if (QThread::currentThread() == QCoreApplication::instance()->thread()) trace->off_gui = false; };
        ports.observe = [captured, native] { native(); return Observation{captured->trace->active.load(), captured->trace->generation.load()}; };
        ports.generation = [captured, native] { native(); return captured->trace->generation.load(); };
        ports.set_active = [captured, native, &entered, &release](bool active) {
            native(); entered.release(); release.acquire();
            captured->trace->active = active; ++captured->trace->generation; ++captured->trace->writes;
        };
        auto model = std::make_unique<NetworkModel>(std::move(ports)); captured.reset();
        bool gui_publication{true};
        QObject observer;
        connect(model.get(), &NetworkModel::changed, &observer, [&] {
            gui_publication = gui_publication && QThread::currentThread() == QCoreApplication::instance()->thread();
        });
        const auto finish = qScopeGuard([&] { release.release(); executor.shutdown(); });
        QEventLoop readiness;
        QTimer timeout; timeout.setSingleShot(true);
        connect(&timeout, &QTimer::timeout, &readiness, &QEventLoop::quit);
        const auto readiness_connection = connect(model.get(), &NetworkModel::changed, &readiness, [&] { if (model->ready()) readiness.quit(); });
        model->setAvailable(true); timeout.start(5000);
        if (!model->ready()) readiness.exec();
        QVERIFY(model->ready());
        disconnect(readiness_connection);
        timeout.stop();
        QVERIFY(model->requestActive(false));
        QVERIFY(entered.tryAcquire(1, 5000));
        model.reset();
        QCOMPARE(trace->destroyed.load(), 0);
        release.release();
        QEventLoop drained;
        QTimer drain_timeout; drain_timeout.setSingleShot(true);
        connect(&drain_timeout, &QTimer::timeout, &drained, &QEventLoop::quit);
        connect(&executor, &BackendExecutor::drained, &drained, &QEventLoop::quit);
        executor.shutdown(); drain_timeout.start(5000);
        if (!executor.isDrained()) drained.exec();
        QVERIFY(executor.isDrained());
        QCOMPARE(trace->writes.load(), 1); QVERIFY(!trace->active.load());
        QCOMPARE(trace->destroyed.load(), 1);
        QVERIFY(trace->off_gui.load()); QVERIFY(trace->destructor_off_gui.load()); QVERIFY(gui_publication);
    }
};

#ifdef BITCOINQML_NO_TEST_MAIN
BITCOINQML_REGISTER_QT_TEST(NetworkCommandTests)
#else
QTEST_MAIN(NetworkCommandTests)
#endif
#include <test_networkcommand.moc>
