// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/backendexecutor.h>
#include <qml/models/peercommandmodel.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/peerselectionmodel.h>
#include <test/mocks/manualexecutionenvironment.h>
#include <test/mocks/mocknode.h>
#include <test/mocks/drainexecutors.h>
#include <test/qt_test_registry.h>
#include <netbase.h>
#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QtTest/QtTest>
#include <atomic>
#include <deque>
#include <stdexcept>

namespace {
CNodeStats Peer(NodeId id, const std::string& address)
{
    CNodeStats value{}; value.nodeid = id; value.addr = CAddress{LookupNumeric(address), NODE_NONE};
    value.m_addr_name = address; value.m_connected = NodeClock::time_point{std::chrono::seconds{1000}};
    value.m_session_id = "original-session"; value.cleanSubVer = "/original/";
    value.m_conn_type = ConnectionType::MANUAL; value.m_network = NET_IPV4;
    return value;
}
struct Fixture {
    ManualRoutedExecution execution;
    std::deque<std::function<void()>> gui;
    interfaces::Node::NodesStats peers;
    banmap_t bans;
    MockNode node;
    bool ready{true}, inventory_available{true};
    int disconnects{0}, ban_writes{0}, address_disconnects{0};
    std::vector<NodeId> disconnected_ids;
    std::unique_ptr<PeerCommandModel> commands;
    std::unique_ptr<PeerListModel> source;
    std::unique_ptr<PeerListSortProxy> proxy;
    Fixture()
    {
        peers.emplace_back(Peer(7, "10.0.0.7:8333"), true, CNodeStateStats{});
        peers.emplace_back(Peer(8, "10.0.0.8:8333"), true, CNodeStateStats{});
        node.get_nodes_stats_fn = [&](auto& out) { out = peers; return inventory_available; };
        node.get_banned_fn = [&](auto& out) { out = bans; return true; };
        node.disconnect_by_id_fn = [&](NodeId id) { ++disconnects; disconnected_ids.push_back(id); return true; };
        node.ban_fn = [&](const CNetAddr&, int64_t) { ++ban_writes; return true; };
        node.disconnect_by_address_fn = [&](const CNetAddr&) { ++address_disconnects; return true; };
        const execution::SnapshotServices services{execution.scheduler, [&](auto turn) { gui.push_back(std::move(turn)); }};
        commands = std::make_unique<PeerCommandModel>(node, services, [&] { return ready; });
        source = std::make_unique<PeerListModel>(node, nullptr, true, services);
        source->setCommands(commands.get());
        proxy = std::make_unique<PeerListSortProxy>(nullptr); proxy->setSourceModel(source.get());
    }
    bool publish()
    {
        if (gui.empty()) return false;
        auto turn = std::move(gui.front()); gui.pop_front(); turn(); return true;
    }
    bool settle()
    {
        for (int turn = 0; turn < 40; ++turn) {
            execution.drain(); while (publish()) {}
            if (execution.transportIdle() && gui.empty()) return true;
        }
        return false;
    }
    PeerDetailsModel* details(NodeId id) { return proxy->peerDetailsAt(proxy->indexOfNodeId(id)); }
    ~Fixture()
    {
        stop();
    }
    void stop()
    {
        if (commands) commands->closeAdmission();
        if (proxy) proxy->closeAdmission();
        if (source) source->closeAdmission();
        proxy.reset(); source.reset(); commands.reset(); settle();
    }
};
}
class PeerCommandTests : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void initTestCase() { qRegisterMetaType<PeerCommandOperation::Outcome>(); qRegisterMetaType<PeerCommandResult>(); }
    void originalCommandIsIndependentOfSelectionAndHeldGuiPublication()
    {
        Fixture f; QVERIFY(f.settle()); auto* first = f.details(7); auto* second = f.details(8); QVERIFY(first && second);
        PeerSelectionModel selection; selection.setSource(f.proxy.get()); QVERIFY(selection.select(first));
        std::unique_ptr<PeerCommandOperation> operation{first->prepareDisconnect()}; QVERIFY(operation);
        const auto id = operation->operationId(); const auto target = operation->connectionId();
        QSignalSpy terminal{f.commands.get(), &PeerCommandModel::finished};
        QVERIFY(operation->confirm()); QVERIFY(operation->pending()); QVERIFY(f.commands->pending());
        QVERIFY(selection.select(second)); QCOMPARE(selection.selectedDetails(), second); QCOMPARE(f.disconnects, 0);
        QVERIFY(f.execution.step(execution::Lane::Core)); QCOMPARE(f.disconnected_ids, std::vector<NodeId>{7});
        QVERIFY(f.execution.step(execution::Lane::Domain)); QVERIFY(operation->pending()); QVERIFY(!operation->resultPresent());
        QVERIFY(f.commands->pending()); QVERIFY(!f.commands->lastResult().present);
        QVERIFY(!f.commands->requestDisconnectPeer(8)); // No idle window before original receipt publication.
        QVERIFY(f.publish()); QVERIFY(!f.commands->pending()); QCOMPARE(terminal.size(), 1);
        QCOMPARE(f.commands->lastResult().operationId, id); QCOMPARE(f.commands->lastResult().connectionId, target);
        QVERIFY(f.settle()); QVERIFY(operation->resultPresent()); QCOMPARE(operation->result().outcome, PeerCommandOperation::Outcome::Succeeded);
        QCOMPARE(operation->result().nodeIdText, QStringLiteral("7")); QCOMPARE(selection.selectedDetails(), second);
        QVERIFY(!operation->confirm()); QCOMPARE(f.disconnects, 1);
    }
    void backendChecksOriginalConnectionBeforeMutation_data()
    {
        QTest::addColumn<int>("change");
        QTest::newRow("reused-id-new-time") << 0; QTest::newRow("reused-id-new-address") << 1;
        QTest::newRow("reused-id-new-port") << 2; QTest::newRow("reused-id-new-session") << 3;
        QTest::newRow("missing-original") << 4; QTest::newRow("read-unavailable") << 5;
    }
    void backendChecksOriginalConnectionBeforeMutation()
    {
        QFETCH(int, change); Fixture f; QVERIFY(f.settle()); auto* original = f.details(7); QVERIFY(original);
        const auto captured = original->commandTarget();
        std::unique_ptr<PeerCommandOperation> operation{original->prepareDisconnect()}; QVERIFY(operation && operation->confirm());
        auto& replacement = std::get<0>(f.peers.front());
        if (change == 0) replacement.m_connected += std::chrono::seconds{1};
        else if (change == 1) replacement.addr = CAddress{LookupNumeric("10.0.0.9:8333"), NODE_NONE};
        else if (change == 2) replacement.addr = CAddress{LookupNumeric("10.0.0.7:18444"), NODE_NONE};
        else if (change == 3) replacement.m_session_id = "replacement-session";
        else if (change == 4) f.peers.erase(f.peers.begin());
        else f.inventory_available = false;
        QVERIFY(f.settle()); QCOMPARE(f.disconnects, 0); QCOMPARE(f.ban_writes, 0);
        QVERIFY(operation->resultPresent()); QVERIFY(operation->outcomeKnown());
        QCOMPARE(operation->result().outcome, PeerCommandOperation::Outcome::Rejected);
        QVERIFY(!operation->result().mutationAttempted); QCOMPARE(operation->connectionId(), captured.key);
        QVERIFY(!operation->errorText().isEmpty()); QVERIFY(!operation->confirm());
    }
    void unknownDisconnectIsObservedWithoutMutationRetry()
    {
        Fixture f; QVERIFY(f.settle()); auto* original = f.details(7); QVERIFY(original);
        f.node.disconnect_by_id_fn = [&](NodeId id) -> bool {
            ++f.disconnects; f.disconnected_ids.push_back(id); f.peers.erase(f.peers.begin());
            throw std::runtime_error("controlled transport loss after effect");
        };
        std::unique_ptr<PeerCommandOperation> operation{original->prepareDisconnect()}; QVERIFY(operation && operation->confirm());
        QVERIFY(f.settle()); QVERIFY(operation->resultPresent()); QVERIFY(!operation->outcomeKnown());
        QCOMPARE(operation->result().outcome, PeerCommandOperation::Outcome::Uncertain); QVERIFY(operation->result().mutationAttempted);
        QVERIFY(!operation->confirm()); QCOMPARE(f.disconnects, 1); const auto reads = f.node.calls.getNodesStats.load();
        QVERIFY(operation->reconcile()); QVERIFY(operation->queryPending()); QVERIFY(!operation->reconcile());
        QVERIFY(f.settle()); QCOMPARE(f.disconnects, 1); QCOMPARE(f.node.calls.getNodesStats.load(), reads + 1);
        QVERIFY(operation->result().observationAvailable); QVERIFY(!operation->result().targetConnected);
        QVERIFY(!operation->outcomeKnown()); QCOMPARE(operation->result().outcome, PeerCommandOperation::Outcome::Uncertain);
        QVERIFY(operation->queryError().isEmpty()); QCOMPARE(operation->result().nodeIdText, QStringLiteral("7"));
    }
    void banPartialOutcomeKeepsItsOriginalAppliedEffect_data()
    {
        QTest::addColumn<int>("fault");
        QTest::newRow("successful-both") << 0; QTest::newRow("ban-refused") << 1;
        QTest::newRow("disconnect-not-confirmed") << 2; QTest::newRow("disconnect-throws-after-ban") << 3;
        QTest::newRow("ban-throws-after-effect") << 4;
    }
    void banPartialOutcomeKeepsItsOriginalAppliedEffect()
    {
        QFETCH(int, fault); Fixture f; QVERIFY(f.settle()); auto* original = f.details(7); QVERIFY(original);
        QString banned, disconnected; int64_t duration{0};
        f.node.ban_fn = [&](const CNetAddr& address, int64_t seconds) {
            ++f.ban_writes; banned = QString::fromStdString(address.ToStringAddr()); duration = seconds;
            if (fault == 1) return false;
            CBanEntry entry; entry.nBanUntil = 1'900'000'000; f.bans[LookupSubNet("10.0.0.7/32")] = entry;
            if (fault == 4) throw std::runtime_error("controlled loss after ban");
            return true;
        };
        f.node.disconnect_by_address_fn = [&](const CNetAddr& address) {
            ++f.address_disconnects; disconnected = QString::fromStdString(address.ToStringAddr());
            if (fault == 3) throw std::runtime_error("controlled disconnect loss");
            return fault != 2;
        };
        std::unique_ptr<PeerCommandOperation> operation{original->prepareBan(PeerCommandOperation::BanDuration::Day)};
        const auto finish_original = qScopeGuard([&] { f.settle(); });
        QVERIFY(operation); QVERIFY(operation->confirm()); QVERIFY(f.settle()); QCOMPARE(f.ban_writes, 1);
        QCOMPARE(banned, QStringLiteral("10.0.0.7")); QCOMPARE(duration, int64_t{86400});
        QCOMPARE(f.address_disconnects, fault == 1 || fault == 4 ? 0 : 1);
        if (f.address_disconnects) QCOMPARE(disconnected, banned);
        const auto actual = operation->result(); QVERIFY(actual.present); QVERIFY(actual.mutationAttempted);
        QCOMPARE(actual.banApplied, fault != 1 && fault != 4); QCOMPARE(actual.disconnectConfirmed, fault == 0);
        QCOMPARE(actual.outcomeKnown, fault < 3);
        QCOMPARE(actual.outcome, fault == 0 ? PeerCommandOperation::Outcome::Succeeded : fault == 1 ? PeerCommandOperation::Outcome::Rejected
            : fault == 4 ? PeerCommandOperation::Outcome::Uncertain : PeerCommandOperation::Outcome::PartiallyApplied);
        QVERIFY(!operation->confirm()); QCOMPARE(f.ban_writes, 1);
        if (fault >= 3) {
            QVERIFY(operation->reconcile()); QVERIFY(f.settle()); QCOMPARE(f.ban_writes, 1);
            QVERIFY(operation->result().banObservationAvailable); QVERIFY(operation->result().targetBanned);
            QCOMPARE(operation->result().banApplied, actual.banApplied); QVERIFY(!operation->outcomeKnown());
        }
    }
    void confirmationAndAdmissionFencesDoNotStartAnotherEffect_data()
    {
        QTest::addColumn<int>("choice");
        QTest::newRow("explicit-cancel") << 0; QTest::newRow("owner-closed") << 1;
        QTest::newRow("parent-ordinary-admission-closed") << 2; QTest::newRow("unsupported-duration") << 3;
        QTest::newRow("duplicate-confirm-control") << 4;
    }
    void confirmationAndAdmissionFencesDoNotStartAnotherEffect()
    {
        QFETCH(int,choice); Fixture f; QVERIFY(f.settle()); auto* detail = f.details(7); QVERIFY(detail);
        std::unique_ptr<PeerCommandOperation> operation{choice == 3 ? detail->prepareBan(static_cast<PeerCommandOperation::BanDuration>(-1)) : detail->prepareDisconnect()};
        QVERIFY(operation); const auto id = operation->operationId(); const auto reads = f.node.calls.getNodesStats.load();
        if (choice == 0) { QVERIFY(operation->cancel()); QVERIFY(!operation->cancel()); QVERIFY(!operation->confirm()); }
        else {
            if (choice == 1) f.commands->closeAdmission();
            if (choice == 2) f.execution.scheduler->closeOrdinaryAdmission();
            const bool admitted = operation->confirm(); QCOMPARE(admitted,choice >= 3); QVERIFY(!operation->confirm());
        }
        QVERIFY(f.settle()); QVERIFY(operation->resultPresent()); QVERIFY(operation->outcomeKnown()); QVERIFY(!operation->pending());
        QCOMPARE(operation->result().operationId,id); QCOMPARE(f.disconnects,choice == 4 ? 1 : 0); QCOMPARE(f.ban_writes,0);
        QCOMPARE(f.node.calls.getNodesStats.load(),reads + (choice == 4 ? 1 : 0));
        QCOMPARE(operation->result().outcome,choice == 0 ? PeerCommandOperation::Outcome::Cancelled : choice == 4 ? PeerCommandOperation::Outcome::Succeeded : PeerCommandOperation::Outcome::Rejected);
        operation.reset(); f.stop(); QVERIFY(f.execution.quiescent());
    }
    void failedRecoveryReadRetainsOriginalOutcomeAndPermitsOnlyAReadRetry()
    {
        Fixture f; QVERIFY(f.settle()); auto* detail = f.details(7); QVERIFY(detail);
        f.node.disconnect_by_id_fn = [&](NodeId) -> bool { ++f.disconnects; throw std::runtime_error("controlled unknown"); };
        std::unique_ptr<PeerCommandOperation> operation{detail->prepareDisconnect()}; QVERIFY(operation && operation->confirm());
        QVERIFY(f.settle()); const auto original = operation->result(); QVERIFY(!original.outcomeKnown); const auto reads = f.node.calls.getNodesStats.load();
        f.execution.rejectLane(execution::Lane::Core); QVERIFY(!operation->reconcile()); QVERIFY(f.settle());
        QVERIFY(!operation->queryPending()); QVERIFY(!operation->pending()); QVERIFY(!operation->queryError().isEmpty());
        QCOMPARE(operation->result().operationId,original.operationId); QCOMPARE(operation->result().outcome,original.outcome);
        QCOMPARE(f.disconnects,1); QCOMPARE(f.node.calls.getNodesStats.load(),reads);
        QVERIFY(operation->reconcile()); QVERIFY(f.settle()); QCOMPARE(f.node.calls.getNodesStats.load(),reads + 1);
        QVERIFY(operation->queryError().isEmpty()); QVERIFY(operation->result().observationAvailable);
        QVERIFY(operation->result().targetConnected); QVERIFY(!operation->outcomeKnown()); QCOMPARE(f.disconnects,1);
    }
    void independentSelectionsAndReentrantFallbackKeepTheirExactFacades()
    {
        Fixture f; f.peers.emplace_back(Peer(9,"10.0.0.9:8333"),true,CNodeStateStats{}); QVERIFY(f.settle());
        auto* first = f.details(7); auto* second = f.details(8); auto* third = f.details(9); QVERIFY(first && second && third);
        PeerSelectionModel left,right; left.setSource(f.proxy.get()); right.setSource(f.proxy.get());
        QVERIFY(left.select(first)); QVERIFY(right.select(second)); QCOMPARE(left.selectedDetails(),first); QCOMPARE(right.selectedDetails(),second);
        left.setAutoSelectFirst(true); bool chose_third{false};
        connect(&left,&PeerSelectionModel::changed,&left,[&] {
            if (!chose_third && left.phase() == PeerSelectionModel::Phase::NoSelection) { chose_third = true; QVERIFY(left.select(third)); }
        });
        f.peers.erase(f.peers.begin()); f.source->refresh(); QVERIFY(f.settle());
        QVERIFY(chose_third); QCOMPARE(left.selectedDetails(),third); QCOMPARE(right.selectedDetails(),second);
        left.clear(); QCOMPARE(left.phase(),PeerSelectionModel::Phase::NoSelection); QCOMPARE(right.selectedDetails(),second);
        f.proxy->closeAdmission(); QCOMPARE(right.phase(),PeerSelectionModel::Phase::Stopped); QVERIFY(!right.select(second));
    }
    void sessionHandshakePromotionAndDelayedConstructorKeepConnectionOwnership()
    {
        Fixture f; std::get<0>(f.peers.front()).m_session_id.clear(); QVERIFY(f.settle());
        auto* original = f.details(7); QVERIFY(original); const QPointer<PeerDetailsModel> old{original}; const auto identity = original->connectionId();
        auto held = f.source->data(f.source->index(f.source->rowForId(7)),PeerListModel::StatsRole).value<PeerStatsHandle>();
        QSignalSpy disconnected{original,&PeerDetailsModel::disconnected};
        std::get<0>(f.peers.front()).m_session_id = "established-session"; f.source->refresh(); QVERIFY(f.settle());
        QCOMPARE(disconnected.size(),0); QVERIFY(original->connected()); QCOMPARE(original->connectionId(),identity);
        QCOMPARE(original->sessionId(),QStringLiteral("established-session"));
        std::get<0>(f.peers.front()).m_connected += std::chrono::seconds{1};
        f.source->refresh(); QVERIFY(f.execution.step(execution::Lane::Core)); QVERIFY(f.execution.step(execution::Lane::Domain));
        QVERIFY(f.publish()); QCOMPARE(disconnected.size(),1);
        QVERIFY(!f.proxy->peerDetailsAt(f.proxy->indexOfNodeId(7))); // Old proxy row cannot construct the replacement.
        PeerDetailsModel delayed{held,f.source.get()}; QVERIFY(!delayed.connected()); QVERIFY(!delayed.canDisconnect());
        QVERIFY(f.settle()); auto* replacement = f.details(7); QVERIFY(replacement && replacement->connected());
        QVERIFY(replacement->connectionId() != identity); QVERIFY(!old || !old->connected());
    }
    void nativeReceiptSurvivesProjectionAndOwnerLoss()
    {
        Fixture f; QVERIFY(f.settle()); auto* detail = f.details(7); QVERIFY(detail);
        peercommands::Request request{QStringLiteral("original-owned-effect"),1,detail->commandTarget(),peercommands::Kind::Disconnect,0,true};
        std::weak_ptr<peercommands::Store> retained;
        std::vector<peercommands::Result> receipts;
        auto store = std::make_shared<peercommands::Store>(f.node,execution::SnapshotServices{f.execution.scheduler,{}},[&] {
            if (auto original = retained.lock()) { const auto snapshot = original->snapshot(); if (snapshot.mutation) receipts.push_back(*snapshot.mutation); }
        });
        retained = store;
        std::unique_ptr<PeerCommandOperation> projection{new PeerCommandOperation(store,request,
            {f.execution.scheduler,[&](auto callback) { f.gui.push_back(std::move(callback)); }},[] { return true; },[] {})};
        const auto finish_original = qScopeGuard([&] {
            projection.reset(); if (store) store->close(); store.reset(); f.execution.drain(); while (f.publish()) {} f.execution.drain();
        });
        QVERIFY(projection->confirm()); projection.reset(); store->close(); store.reset();
        f.execution.scheduler->closeOrdinaryAdmission(); QCOMPARE(f.disconnects,0);
        QVERIFY(f.execution.step(execution::Lane::Core)); QCOMPARE(f.disconnected_ids,std::vector<NodeId>{7});
        QVERIFY(f.execution.step(execution::Lane::Domain)); QCOMPARE(receipts.size(),size_t{1});
        QCOMPARE(receipts.front().request.id,request.id); QCOMPARE(receipts.front().outcome,peercommands::Outcome::Succeeded);
        QVERIFY(receipts.front().disconnect_confirmed); f.execution.drain(); QVERIFY(retained.expired());
        QVERIFY(f.settle()); QCOMPARE(f.disconnects,1); f.stop(); QVERIFY(f.execution.quiescent());
    }
    void realThreadWriteKeepsGuiResponsiveAndAccountsBeforeProjection()
    {
        MockNode node; const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
        BackendExecutor backend; std::atomic<bool> off_gui{true}; std::atomic<int> mutations{0};
        QSemaphore entered,release; bool heartbeat{false};
        const auto peer = Peer(7,"10.0.0.7:8333");
        std::unique_ptr<PeerCommandModel> commands; std::unique_ptr<PeerListModel> source;
        std::unique_ptr<PeerDetailsModel> detail; std::unique_ptr<PeerCommandOperation> operation;
        const auto finish_original = qScopeGuard([&] {
            release.release(); operation.reset(); detail.reset();
            if (source) source->closeAdmission();
            source.reset();
            if (commands) commands->closeAdmission();
            commands.reset();
            backend.shutdown(); QVERIFY2(QTest::qWaitFor([&] { return backend.isDrained(); },5000), "Original native work must drain before fixture destruction");
        });
        node.get_nodes_stats_fn = [&](auto& result) {
            if (QThread::currentThread() == qApp->thread()) off_gui = false;
            result = {{peer,true,CNodeStateStats{}}}; return true;
        };
        node.disconnect_by_id_fn = [&](NodeId id) {
            if (QThread::currentThread() == qApp->thread()) off_gui = false;
            if (id != 7) return false;
            entered.release(); release.acquire(); ++mutations; return true;
        };
        const execution::SnapshotServices services{backend.scheduler(),{}};
        commands = std::make_unique<PeerCommandModel>(node,services,[] { return true; });
        source = std::make_unique<PeerListModel>(node,nullptr,true,services); source->setCommands(commands.get());
        QTRY_COMPARE(source->rowCount(),1);
        detail = std::make_unique<PeerDetailsModel>(source->data(source->index(0),PeerListModel::StatsRole).value<PeerStatsHandle>(),source.get());
        operation.reset(detail->prepareDisconnect()); QVERIFY(operation && operation->confirm());
        const bool running = entered.tryAcquire(1,5000); if (!running) release.release(); QVERIFY(running);
        QMetaObject::invokeMethod(qApp,[&] { heartbeat = true; },Qt::QueuedConnection); QTRY_VERIFY(heartbeat);
        QVERIFY(operation->pending()); QCOMPARE(mutations.load(),0); operation.reset(); release.release();
        QTRY_VERIFY(commands->lastResult().present); QCOMPARE(commands->lastResult().nodeIdText,QStringLiteral("7"));
        QCOMPARE(commands->lastResult().outcome,PeerCommandOperation::Outcome::Succeeded); QCOMPARE(mutations.load(),1); QVERIFY(off_gui.load());
        detail.reset(); source->closeAdmission(); source.reset(); commands->closeAdmission(); commands.reset();
        backend.shutdown(); QTRY_VERIFY(backend.isDrained());
    }
    void retiredProjectionKeepsActualOutcomeWithoutLateSignals_data()
    {
        QTest::addColumn<bool>("parent_closes");
        QTest::newRow("view-interest-retired") << false; QTest::newRow("parent-admission-closed") << true;
    }
    void retiredProjectionKeepsActualOutcomeWithoutLateSignals()
    {
        QFETCH(bool,parent_closes); Fixture f; QVERIFY(f.settle()); auto* detail = f.details(7); QVERIFY(detail);
        std::unique_ptr<PeerCommandOperation> operation{detail->prepareDisconnect()}; QVERIFY(operation);
        QSignalSpy notices{operation.get(),&PeerCommandOperation::changed}; QSignalSpy finished{f.commands.get(),&PeerCommandModel::finished};
        QVERIFY(operation->confirm()); const auto admitted_notices = notices.size(); QVERIFY(admitted_notices > 0);
        if (parent_closes) f.commands->closeAdmission(); else operation->retire();
        QCOMPARE(notices.size(),admitted_notices); QVERIFY(f.settle());
        QCOMPARE(f.disconnects,1); QVERIFY(operation->resultPresent()); QCOMPARE(operation->result().outcome,PeerCommandOperation::Outcome::Succeeded);
        QVERIFY(operation->outcomeKnown()); QCOMPARE(notices.size(),admitted_notices);
        QVERIFY(f.commands->lastResult().present); QCOMPARE(f.commands->lastResult().operationId,operation->operationId());
        QCOMPARE(finished.size(),parent_closes ? 0 : 1);
    }
    void reentrantSuccessorCannotEraseOriginalTerminalReceipt()
    {
        Fixture f; QVERIFY(f.settle()); auto* first = f.details(7); auto* second = f.details(8); QVERIFY(first && second);
        std::unique_ptr<PeerCommandOperation> original{first->prepareDisconnect()}, successor;
        QVERIFY(original); bool successor_admitted{false}, original_published_while_successor_pending{false};
        QSignalSpy finished{f.commands.get(),&PeerCommandModel::finished}; QObject observer;
        connect(f.commands.get(),&PeerCommandModel::changed,&observer,[&] {
            if (successor_admitted || f.commands->pending() || !f.commands->lastResult().present) return;
            QCOMPARE(f.commands->lastResult().operationId,original->operationId()); successor_admitted = true;
            successor.reset(second->prepareDisconnect()); QVERIFY(successor && successor->confirm());
        });
        connect(original.get(),&PeerCommandOperation::changed,&observer,[&] {
            if (original->resultPresent()) original_published_while_successor_pending = f.commands->pending();
        });
        QVERIFY(original->confirm()); QVERIFY(f.settle()); QVERIFY(successor_admitted); QVERIFY(successor);
        QCOMPARE(f.disconnected_ids,(std::vector<NodeId>{7,8})); QCOMPARE(finished.size(),2);
        QVERIFY(finished.at(0).at(0).toULongLong() != finished.at(1).at(0).toULongLong());
        QVERIFY(original_published_while_successor_pending); QCOMPARE(original->result().nodeIdText,QStringLiteral("7"));
        QCOMPARE(successor->result().nodeIdText,QStringLiteral("8")); QCOMPARE(f.commands->lastResult().operationId,successor->operationId());
    }
    void retainedDetailsAndCopiedPresentationDoNotHoldNativeDrain()
    {
        Fixture f; QVERIFY(f.settle());
        auto& raw = std::get<0>(f.peers.front());
        raw.m_addr_name.assign(4096, 'a'); raw.addrLocal.assign(4096, 'b');
        raw.cleanSubVer.assign(4096, 'c');
        f.source->refresh(); QVERIFY(f.settle());
        auto* detail = f.details(7); QVERIFY(detail);
        const auto address = detail->address(), local = detail->addressLocal(), agent = detail->userAgent();
        QCOMPARE(address, QString(512, QLatin1Char{'a'}));
        QCOMPARE(local, QString(512, QLatin1Char{'b'}));
        QCOMPARE(agent, QString(512, QLatin1Char{'c'}));
        auto loan = f.source->data(f.source->index(f.source->rowForId(7)), PeerListModel::StatsRole).value<PeerStatsHandle>();
        const std::weak_ptr<const CNodeCombinedStats> native{loan}; loan.reset();
        const auto reads = f.node.calls.getNodesStats.load();
        f.commands->closeAdmission(); f.proxy->closeAdmission(); f.source->closeAdmission();
        // Keep GUI delivery held: physical Native retirement cannot require
        // releasing a still-live view or presenting a queued notification.
        f.execution.drain(); QVERIFY(f.execution.quiescent()); QVERIFY(native.expired());
        // The model-owned detail and copied values remain readable after all
        // Native resources retire. Neither can prolong physical shutdown.
        QCOMPARE(detail->address(), address); QCOMPARE(detail->addressLocal(), local); QCOMPARE(detail->userAgent(), agent);
        QCOMPARE(detail->nodeIdText(), QStringLiteral("7")); QVERIFY(!detail->connected());
        QVERIFY(!detail->prepareDisconnect()); QVERIFY(!detail->prepareBan(PeerCommandOperation::BanDuration::Day));
        QCOMPARE(f.node.calls.getNodesStats.load(), reads); QCOMPARE(f.disconnects, 0); QCOMPARE(f.ban_writes, 0);
    }
    void durationPolicyPreservesLegacyCustomValuesWithoutWideningTypedCommands_data()
    {
        QTest::addColumn<bool>("legacy"); QTest::addColumn<qint64>("seconds");
        QTest::newRow("legacy-standard-control") << true << qint64{3600};
        QTest::newRow("legacy-positive-custom") << true << qint64{3601};
        QTest::newRow("typed-positive-custom-refused") << false << qint64{3601};
    }
    void durationPolicyPreservesLegacyCustomValuesWithoutWideningTypedCommands()
    {
        QFETCH(bool, legacy); QFETCH(qint64, seconds);
        Fixture f; QVERIFY(f.settle()); qint64 received_seconds{0}; QString received_address;
        const auto cleanup = qScopeGuard([&] { f.settle(); });
        f.node.ban_fn = [&](const CNetAddr& address, int64_t duration) {
            ++f.ban_writes; received_seconds = duration; received_address = QString::fromStdString(address.ToStringAddr()); return true;
        };
        const auto reads = f.node.calls.getNodesStats.load();
        std::unique_ptr<PeerCommandOperation> operation;
        if (legacy) QVERIFY(f.commands->requestBanPeer(QStringLiteral("10.0.0.7"), seconds));
        else {
            auto* details = f.details(7); QVERIFY(details);
            operation.reset(details->prepareBan(static_cast<PeerCommandOperation::BanDuration>(seconds)));
            QVERIFY(operation); QVERIFY(operation->confirm());
        }
        QCOMPARE(f.ban_writes, 0); QVERIFY(f.settle()); QVERIFY(f.commands->lastResult().present);
        QCOMPARE(f.node.calls.getNodesStats.load(), reads);
        QCOMPARE(f.ban_writes, legacy ? 1 : 0); QCOMPARE(f.address_disconnects, legacy ? 1 : 0);
        QCOMPARE(f.commands->lastResult().mutationAttempted, legacy);
        QCOMPARE(f.commands->lastResult().outcome, legacy ? PeerCommandOperation::Outcome::Succeeded : PeerCommandOperation::Outcome::Rejected);
        if (legacy) { QCOMPARE(received_seconds, seconds); QCOMPARE(received_address, QStringLiteral("10.0.0.7")); }
        else { QVERIFY(!operation->pending()); QVERIFY(!operation->errorText().isEmpty()); }
    }
};
BITCOINQML_REGISTER_QT_TEST(PeerCommandTests)
#include <test_peercommands.moc>
