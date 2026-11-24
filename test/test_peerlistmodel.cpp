// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <test/mocks/drainexecutors.h>
#include <test/mocks/manualstartupexecution.h>
#include <test/mocks/nativelistqueues.h>
#include <qml/models/banlistmodel.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistproxymodel.h>
#include <qml/models/peerlistmodel.h>
#include <qml/peerstatsutil.h>
#include <QPersistentModelIndex>
#include <QThread>
#include <QScopeGuard>
#include <util/translation.h>
#include <netbase.h>

#include <algorithm>
#include <atomic>
#include <map>
#include <optional>
#include <utility>

namespace {
interfaces::Node::NodesStats MakeStats(std::initializer_list<CNodeStats> node_stats)
{
    interfaces::Node::NodesStats stats;
    for (const auto& node_stat : node_stats) {
        stats.emplace_back(node_stat, true, CNodeStateStats{});
    }
    return stats;
}

CNodeStats MakeNodeStats(NodeId node_id, std::string address, bool inbound, ConnectionType connection_type, Network network)
{
    CNodeStats stats{};
    stats.nodeid = node_id;
    stats.m_connected = NodeClock::time_point{std::chrono::seconds{1'000}};
    stats.addr = CAddress{LookupNumeric(address), NODE_NONE};
    stats.m_addr_name = std::move(address);
    stats.fInbound = inbound;
    stats.m_conn_type = connection_type;
    stats.m_network = network;
    stats.m_min_ping_time = std::chrono::microseconds{1'500};
    stats.nSendBytes = 1'200;
    stats.nRecvBytes = 900;
    stats.cleanSubVer = "/Satoshi:28.0.0/";
    stats.m_transport_type = TransportProtocolType::V1;
    return stats;
}

constexpr auto AUTO_REFRESH_TRIGGER_TIMEOUT{2'000};
constexpr auto AUTO_REFRESH_STOP_WAIT{450};
} // namespace

class PeerListModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void mapsRoleData();
    void refreshUpdatesRows();
    void refreshHandlesGetNodesStatsFailure();
    void startStopAutoRefresh();
    void sortProxySortsByRoles();
    void manualQuerySeparatesCoreOutcomeAndPresentation();
    void sharedQueuesDeliverListsAndFinishAcceptedUnbanAfterClose();
    void acceptedCommandRetainsOutcomeAfterObserverLoss();
    void manualProjectionBudgetsAndPersistentIndexes();
    void manualFilterStaleAndQuietClose();
    void realThreadReadsNeverRunOnGui();
    void neutralPresentationObserversPreserveBoundedDeliveryAndLifetime();
    void displayedPeerIdSearchPreservesOrdinaryText_data();
    void displayedPeerIdSearchPreservesOrdinaryText();
    void copiedDetailsRejectReplacementConnection_data();
    void copiedDetailsRejectReplacementConnection();
};

void PeerListModelTests::displayedPeerIdSearchPreservesOrdinaryText_data()
{
    QTest::addColumn<QString>("query"); QTest::addColumn<qint64>("expected_id"); QTest::addColumn<int>("count");
    QTest::newRow("displayed-id") << QStringLiteral("#72") << qint64{72} << 1;
    QTest::newRow("numeric-control") << QStringLiteral("72") << qint64{72} << 1;
    QTest::newRow("displayed-trim") << QStringLiteral("  #72  ") << qint64{72} << 1;
    QTest::newRow("displayed-leading-zero") << QStringLiteral("#0072") << qint64{72} << 1;
    QTest::newRow("other-peer") << QStringLiteral("#71") << qint64{71} << 1;
    QTest::newRow("ordinary-hash-tag") << QStringLiteral("#peer") << qint64{9} << 1;
    QTest::newRow("no-partial-id-token") << QStringLiteral("#7") << qint64{-1} << 0;
    QTest::newRow("not-a-display-id") << QStringLiteral("#72suffix") << qint64{-1} << 0;
    QTest::newRow("overflow-does-not-wrap") << QStringLiteral("#9223372036854775808") << qint64{-1} << 0;
    QTest::newRow("negative-is-not-an-id") << QStringLiteral("#-1") << qint64{-1} << 0;
}
void PeerListModelTests::displayedPeerIdSearchPreservesOrdinaryText()
{
    QFETCH(QString, query); QFETCH(qint64, expected_id); QFETCH(int, count);
    ManualStartupExecution transport; std::deque<std::function<void()>> gui;
    auto stats = MakeStats({MakeNodeStats(72,"10.0.0.72:8333",false,ConnectionType::MANUAL,NET_IPV4),
        MakeNodeStats(71,"10.0.0.71:8333",true,ConnectionType::INBOUND,NET_IPV4),
        MakeNodeStats(9,"10.0.0.9:8333",false,ConnectionType::MANUAL,NET_IPV4)});
    std::get<0>(stats[2]).cleanSubVer = "/ordinary #peer text/";
    MockNode node; node.get_nodes_stats_fn = [&](auto& out) { out = stats; return true; };
    std::unique_ptr<PeerListModel> model; std::unique_ptr<PeerListProxyModel> proxy;
    const auto cleanup = qScopeGuard([&] { proxy.reset(); model.reset(); transport.drain(); while (!gui.empty()) { auto callback = std::move(gui.front()); gui.pop_front(); callback(); } transport.drain(); });
    model = std::make_unique<PeerListModel>(node,nullptr,true,[&](auto callback) { gui.push_back(std::move(callback)); }, [] { return NodeClock::now(); }, NativeListQueues(transport.executor()));
    proxy = std::make_unique<PeerListProxyModel>(nullptr); proxy->setSourceModel(model.get());
    const auto settle = [&] {
        for (int turn = 0; turn < 20; ++turn) {
            transport.drain(); while (!gui.empty()) { auto callback = std::move(gui.front()); gui.pop_front(); callback(); }
            if (transport.quiescent()) return true;
        } return false;
    };
    QVERIFY(settle()); QCOMPARE(proxy->rowCount(),3); QCOMPARE(node.calls.getNodesStats.load(),1);
    proxy->setSearchText(query); QVERIFY(settle()); QCOMPARE(proxy->rowCount(),count);
    if (count) QCOMPARE(proxy->data(proxy->index(0),PeerListModel::NetNodeId).toLongLong(),expected_id);
    QCOMPARE(node.calls.getNodesStats.load(),1); // Search is a Domain projection over the original public read.
}
void PeerListModelTests::copiedDetailsRejectReplacementConnection_data()
{
    QTest::addColumn<int>("change");
    QTest::newRow("reused-id-new-connection-time") << 0; QTest::newRow("reused-id-new-endpoint") << 1;
    QTest::newRow("reused-id-new-session") << 2; QTest::newRow("missing-peer-control") << 3;
}
void PeerListModelTests::copiedDetailsRejectReplacementConnection()
{
    QFETCH(int,change); ManualStartupExecution transport; std::deque<std::function<void()>> gui;
    auto stats = MakeStats({MakeNodeStats(72,"10.0.0.72:8333",false,ConnectionType::MANUAL,NET_IPV4)});
    std::get<0>(stats.front()).m_session_id = "original";
    MockNode node; node.get_nodes_stats_fn = [&](auto& out) { out = stats; return true; };
    std::unique_ptr<PeerListModel> model;
    const auto cleanup = qScopeGuard([&] { model.reset(); transport.drain(); while (!gui.empty()) { auto callback = std::move(gui.front()); gui.pop_front(); callback(); } transport.drain(); });
    model = std::make_unique<PeerListModel>(node,nullptr,true,[&](auto callback) { gui.push_back(std::move(callback)); }, [] { return NodeClock::now(); }, NativeListQueues(transport.executor()));
    const auto settle = [&] { transport.drain(); while (!gui.empty()) { auto callback = std::move(gui.front()); gui.pop_front(); callback(); } };
    settle(); QCOMPARE(model->rowCount(),1);
    auto original = model->data(model->index(0),PeerListModel::StatsRole).value<PeerStatsHandle>(); QVERIFY(original);
    PeerDetailsModel details{original,model.get()}; QSignalSpy disconnected{&details,&PeerDetailsModel::disconnected};
    const auto old_address = details.address(); const auto old_user_agent = details.userAgent();
    if (change == 3) stats.clear();
    else {
        auto& replacement = std::get<0>(stats.front()); replacement.m_addr_name = "replacement.example:8333"; replacement.cleanSubVer = "/replacement/";
        if (change == 0) replacement.m_connected += std::chrono::seconds{1};
        else if (change == 1) replacement.addr = CAddress{LookupNumeric("10.0.0.73:8333"),NODE_NONE};
        else replacement.m_session_id = "replacement";
    }
    model->refresh(); settle(); QCOMPARE(disconnected.size(),1);
    QCOMPARE(details.nodeId(),qint64{72}); QCOMPARE(details.address(),old_address); QCOMPARE(details.userAgent(),old_user_agent);
    QCOMPARE(original->nodeStats.cleanSubVer,std::string{"/Satoshi:28.0.0/"}); // Owned StatsRole control, never a borrowed freed element.
    const auto reads = node.calls.getNodesStats.load(); original.reset();
    for (int repeat = 0; repeat < 100; ++repeat) { QCOMPARE(details.address(),old_address); QCOMPARE(details.userAgent(),old_user_agent); }
    QCOMPARE(node.calls.getNodesStats.load(),reads);
}

void PeerListModelTests::neutralPresentationObserversPreserveBoundedDeliveryAndLifetime()
{
    struct Observer final : QObject, PeerPresentationObserver {
        int updates{0}, removals{0};
        bool available{false};
        QString user_agent;
        QObject* observerObject() override { return this; }
        void update(const PeerPresentation& value, bool current) override { ++updates; available = current; user_agent = value.userAgent; }
        void markDisconnected() override { ++removals; }
    };
    ManualStartupExecution transport;
    std::deque<std::function<void()>> gui;
    auto stats = MakeStats({MakeNodeStats(2, "10.0.0.2:8333", true, ConnectionType::INBOUND, NET_IPV4)});
    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_nodes_stats_fn = [&](auto& result) { result = stats; return true; };
    PeerListModel model{node, nullptr, true, [&](auto turn) { gui.push_back(std::move(turn)); }, [] { return NodeClock::now(); }, NativeListQueues(transport.executor())};
    const auto publish = [&] { if (gui.empty()) return false; auto turn = std::move(gui.front()); gui.pop_front(); turn(); return true; };
    QVERIFY(transport.step(execution::Lane::Core)); while (transport.step(execution::Lane::Utility)) {}
    QVERIFY(!gui.empty());
    while (publish()) {}
    QCOMPARE(model.rowCount(), 1);
    std::vector<std::unique_ptr<Observer>> observers;
    for (int i = 0; i < 65; ++i) { auto observer = std::make_unique<Observer>(); model.watchDetails(2, observer.get()); observers.push_back(std::move(observer)); }
    const auto updates = [&] { int total{0}; for (const auto& observer : observers) if (observer) total += observer->updates; return total; };
    std::get<0>(stats.front()).cleanSubVer = "updated-neutral-port";
    model.refresh(); QVERIFY(transport.step(execution::Lane::Core)); while (transport.step(execution::Lane::Utility)) {}
    QVERIFY(!gui.empty());
    QVERIFY(publish()); QCOMPARE(updates(), 32);
    observers.front().reset(); // QObject destruction unregisters a port before a queued continuation.
    while (!gui.empty()) { const int before = updates(); QVERIFY(publish()); QVERIFY(updates() - before <= 32); }
    for (const auto& observer : observers) if (observer) {
        QCOMPARE(observer->updates, 1); QVERIFY(observer->available);
        QCOMPARE(observer->user_agent, QStringLiteral("updated-neutral-port"));
    }
    stats.clear(); model.refresh();
    QVERIFY(transport.step(execution::Lane::Core)); while (transport.step(execution::Lane::Utility)) {}
    QVERIFY(!gui.empty());
    while (publish()) {}
    QCOMPARE(model.rowCount(), 0);
    for (const auto& observer : observers) if (observer) QCOMPARE(observer->removals, 1);
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void PeerListModelTests::manualQuerySeparatesCoreOutcomeAndPresentation()
{
    ManualStartupExecution transport;
    std::deque<std::function<void()>> gui;
    auto stats = MakeStats({MakeNodeStats(2, "10.0.0.2:8333", true, ConnectionType::INBOUND, NET_IPV4)});
    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) { result = stats; return true; };
    PeerListModel model{node, nullptr, true,
        [&](std::function<void()> turn) { gui.push_back(std::move(turn)); }, [] { return NodeClock::now(); }, NativeListQueues(transport.executor())};
    for (int i = 0; i < 100; ++i) model.refresh();
    QCOMPARE(node.calls.getNodesStats.load(), 0);
    QTRY_COMPARE(model.rowCount(), 0);
    QVERIFY(transport.step(execution::Lane::Core));
    QCOMPARE(node.calls.getNodesStats.load(), 1);
    QTRY_COMPARE(model.rowCount(), 0);
    QVERIFY(gui.empty());
    QVERIFY(transport.step(execution::Lane::Utility));
    QCOMPARE(gui.size(), size_t{1});
    QTRY_COMPARE(model.rowCount(), 0);
    auto publish = std::move(gui.front()); gui.pop_front(); publish();
    QTRY_COMPARE(model.rowCount(), 1);
    QSignalSpy resets{&model, &QAbstractItemModel::modelReset};
    auto original = model.data(model.index(0), PeerListModel::StatsRole).value<PeerStatsHandle>();
    PeerDetailsModel details{original, &model};
    QSignalSpy disconnected{&details, &PeerDetailsModel::disconnected};
    stats = MakeStats({MakeNodeStats(1, "10.0.0.1:8333", false, ConnectionType::MANUAL, NET_IPV4),
                      MakeNodeStats(2, "10.0.0.2:8333", true, ConnectionType::INBOUND, NET_IPV4)});
    // All 100 requests while the first read was pending create one follow-up.
    QVERIFY(transport.step(execution::Lane::Core));
    QVERIFY(transport.step(execution::Lane::Utility));
    QCOMPARE(gui.size(), size_t{1});
    publish = std::move(gui.front()); gui.pop_front(); publish();
    QCOMPARE(node.calls.getNodesStats.load(), 2);
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(resets.size(), 0);
    QCOMPARE(details.nodeId(), qint64{2});
    QCOMPARE(disconnected.size(), 0);
    QCOMPARE(details.rawAddress(), QStringLiteral("10.0.0.2"));
    model.close();
    model.refresh();
    original.reset();
    transport.drain();
    QVERIFY(transport.quiescent());
}

void PeerListModelTests::sharedQueuesDeliverListsAndFinishAcceptedUnbanAfterClose()
{
    using execution::Lane;
    ManualStartupExecution transport, retirement_transport;
    const auto list_outcomes = std::make_shared<execution::SerialQueue>(transport.executor().makeQueue(Lane::Utility));
    const auto list_retirement = std::make_shared<execution::SerialQueue>(retirement_transport.executor().makeQueue(Lane::Utility));
    std::deque<std::function<void()>> gui;
    const presentation::Post post = [&](auto turn) { gui.push_back(std::move(turn)); };
    PresentationServices list_values{{post, [list_retirement](auto work) {
        return list_retirement->post(std::move(work));
    }, {}, list_outcomes}};
    NativeListModel::Queues queues{
        std::make_shared<execution::SerialQueue>(transport.executor().makeQueue(Lane::Core)),
        list_outcomes, list_values.valueRetirement(), {},
    };
    const auto publish = [&] {
        while (!gui.empty()) {
            auto turn = std::move(gui.front());
            gui.pop_front();
            turn();
        }
    };
    MockNode node;
    node.get_nodes_stats_fn = [](auto& rows) {
        rows = MakeStats({MakeNodeStats(2, "10.0.0.2:8333", true, ConnectionType::INBOUND, NET_IPV4)});
        return true;
    };
    const auto subnet = LookupSubNet("192.0.2.0/24");
    QVERIFY(subnet.IsValid());
    node.get_banned_fn = [&](auto& rows) {
        rows.emplace(subnet, CBanEntry{});
        return true;
    };
    std::optional<CSubNet> unbanned;
    node.unban_fn = [&](const auto& value) { unbanned = value; return true; };
    PeerListModel peers{node, nullptr, true, post, [] { return NodeClock::now(); }, queues};
    BanListModel bans{node, nullptr, true, post, queues};
    PeerListProxyModel sorted{nullptr};
    sorted.setSourceModel(&peers);
    QObject receiver;
    bool drained{false};
    const auto cleanup = qScopeGuard([&] {
        sorted.close();
        peers.close();
        bans.close();
        transport.drain();
        publish();
        retirement_transport.drain();
    });
    bans.refresh();
    QCOMPARE(node.calls.getNodesStats.load(), 0);
    QCOMPARE(node.calls.getBanned.load(), 0);
    QVERIFY(transport.step(Lane::Core));
    QCOMPARE(node.calls.getNodesStats.load(), 1);
    QCOMPARE(node.calls.getBanned.load(), 0);
    QVERIFY(transport.step(Lane::Core));
    QCOMPARE(node.calls.getBanned.load(), 1);
    QVERIFY(gui.empty());
    while (transport.step(Lane::Utility)) {}
    QVERIFY(!gui.empty());
    QCOMPARE(peers.rowCount(), 0);
    QCOMPARE(bans.rowCount(), 0);
    publish();
    QCOMPARE(peers.rowCount(), 1);
    QCOMPARE(bans.rowCount(), 1);
    while (transport.step(Lane::Utility)) { publish(); }
    QCOMPARE(sorted.rowCount(), 1);
    QCOMPARE(sorted.data(sorted.index(0), PeerListModel::NetNodeId).toLongLong(), qint64{2});

    QSignalSpy unban_finished{&bans, &BanListModel::unbanFinished};
    QVERIFY(bans.unbanAt(0));
    QVERIFY(!bans.unbanAt(0));
    sorted.close();
    peers.close();
    bans.close();
    queues.core->close();
    queues.core->whenDrained([&] {
        list_values.close();
        list_values.whenRetired(&receiver, [&] { drained = true; });
    });
    QCOMPARE(node.calls.unban.load(), 0);
    QVERIFY(!drained);
    QVERIFY(transport.step(Lane::Core));
    QCOMPARE(node.calls.unban.load(), 1);
    QVERIFY(unbanned && *unbanned == subnet);
    QVERIFY(!drained);
    transport.drain();
    QCoreApplication::processEvents();
    QVERIFY(!drained);
    QVERIFY(list_values.pendingRetirements() > 0);
    retirement_transport.drain();
    QTRY_VERIFY(drained);
    QCOMPARE(list_values.pendingRetirements(), size_t{0});
    QCOMPARE(retirement_transport.coreSubmissions(), size_t{0});
    publish();
    QCOMPARE(unban_finished.count(), 0);
}

void PeerListModelTests::acceptedCommandRetainsOutcomeAfterObserverLoss()
{
    struct List : NativeListModel {
        using NativeListModel::NativeListModel;
        using NativeListModel::startCommand;
    };
    ManualStartupExecution transport;
    auto queues = NativeListQueues(transport.executor());
    List list{{}, nullptr, queues};
    int effects{0}, notices{0};
    bool observer_alive{true};
    auto command = std::make_shared<execution::OwnedCommand<int>>(
        [&] { if (observer_alive) ++notices; });
    const auto effect = [&] { return ++effects; };
    QVERIFY(list.startCommand(*command, effect));
    QVERIFY(!list.startCommand(*command, effect));
    QVERIFY(!command->result());
    observer_alive = false;
    queues.core->close();
    QVERIFY(transport.step(execution::Lane::Core));
    QCOMPARE(effects, 1);
    QVERIFY(!command->result());
    QVERIFY(transport.step(execution::Lane::Utility));
    QVERIFY(command->result());
    QCOMPARE(command->result()->value.value(), 1);
    QCOMPARE(notices, 0);
    QVERIFY(transport.quiescent());
}

void PeerListModelTests::mapsRoleData()
{
    auto stats{MakeStats({MakeNodeStats(7, "127.0.0.1:8333", false, ConnectionType::OUTBOUND_FULL_RELAY, NET_IPV4)})};
    std::get<0>(stats[0]).m_session_id = "043604a60a54b3f5";
    std::get<0>(stats[0]).m_bip152_highbandwidth_to = true;
    std::get<2>(stats[0]).m_addr_relay_enabled = true;
    std::get<2>(stats[0]).m_addr_processed = 1'076;
    std::get<2>(stats[0]).m_addr_rate_limited = 3;
    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = stats;
        return true;
    };

    PeerListModel model{node, nullptr};
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);

    const QModelIndex index = model.index(0, 0);
    QVERIFY(index.isValid());

    const auto roles = model.roleNames();
    QCOMPARE(roles.value(PeerListModel::NetNodeId), QByteArray{"nodeId"});
    QCOMPARE(roles.value(PeerListModel::Address), QByteArray{"address"});
    QCOMPARE(roles.value(PeerListModel::ConnectionType), QByteArray{"connectionType"});
    QCOMPARE(roles.value(PeerListModel::Transport), QByteArray{"transport"});
    QCOMPARE(roles.value(PeerListModel::StatsRole), QByteArray{"stats"});

    QCOMPARE(model.data(index, PeerListModel::NetNodeId).toLongLong(), 7LL);
    QCOMPARE(model.data(index, PeerListModel::Address).toString(), QString{"127.0.0.1:8333"});
    QCOMPARE(model.data(index, PeerListModel::Direction).toString(), QString{"Outbound"});
    QCOMPARE(model.data(index, PeerListModel::ConnectionType).toString(), QString{"Full Relay"});
    QCOMPARE(model.data(index, PeerListModel::Network).toString(), QString{"IPv4"});
    QCOMPARE(model.data(index, PeerListModel::Ping).toString(), QString{"1 ms"});
    QCOMPARE(model.data(index, PeerListModel::Sent).toString(), QString{"1 kB"});
    QCOMPARE(model.data(index, PeerListModel::Received).toString(), QString{"900 B"});
    QCOMPARE(model.data(index, PeerListModel::Subversion).toString(), QString{"/Satoshi:28.0.0/"});
    QCOMPARE(model.data(index, PeerListModel::Transport).toString(), QString{"v1"});
    QVERIFY(!model.data(index, PeerListModel::Age).toString().isEmpty());

    const auto stats_ptr = model.data(index, PeerListModel::StatsRole).value<PeerStatsHandle>();
    QVERIFY(stats_ptr != nullptr);
    QCOMPARE(stats_ptr->nodeStats.nodeid, 7);

    PeerDetailsModel details{stats_ptr, &model};
    QCOMPARE(details.sessionId(), QString{"043604a60a54b3f5"});
    QVERIFY(details.mappedAS().isEmpty());
    QVERIFY(details.permission().isEmpty());
    QVERIFY(details.startingHeight().isEmpty());
    QVERIFY(details.highBandwidth());
    QVERIFY(details.addressRelay());
    QCOMPARE(details.addressesProcessed(), QString{"1076"});
    QCOMPARE(details.addressesRateLimited(), QString{"3"});

    QCOMPARE(model.flags(QModelIndex{}), Qt::NoItemFlags);
    QVERIFY(model.flags(index).testFlag(Qt::ItemIsSelectable));
    QVERIFY(model.flags(index).testFlag(Qt::ItemIsEnabled));
    QCOMPARE(node.calls.getNodesStats.load(), 1);
}

void PeerListModelTests::refreshUpdatesRows()
{
    const auto stats_initial{MakeStats({
        MakeNodeStats(1, "10.0.0.1:8333", true, ConnectionType::INBOUND, NET_IPV4),
        MakeNodeStats(2, "10.0.0.2:8333", false, ConnectionType::MANUAL, NET_IPV6),
    })};
    const auto stats_remove{MakeStats({
        MakeNodeStats(2, "10.0.0.2:8333", false, ConnectionType::MANUAL, NET_IPV6),
    })};
    const auto stats_insert{MakeStats({
        MakeNodeStats(2, "10.0.0.2:8333", false, ConnectionType::MANUAL, NET_IPV6),
        MakeNodeStats(3, "10.0.0.3:8333", false, ConnectionType::BLOCK_RELAY, NET_ONION),
    })};

    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    const std::vector<interfaces::Node::NodesStats> responses{stats_initial, stats_remove, stats_insert};
    size_t response_index{0};
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = responses.at(response_index++);
        return true;
    };

    PeerListModel model{node, nullptr};
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);
    QCOMPARE(model.data(model.index(1, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);

    model.refresh();
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);

    model.refresh();
    QTRY_COMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);
    QCOMPARE(model.data(model.index(1, 0), PeerListModel::NetNodeId).toLongLong(), 3LL);
    QCOMPARE(node.calls.getNodesStats.load(), 3);
}

void PeerListModelTests::refreshHandlesGetNodesStatsFailure()
{
    const auto stats{MakeStats({MakeNodeStats(1, "10.0.0.1:8333", true, ConnectionType::INBOUND, NET_IPV4)})};

    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        if (node.calls.getNodesStats.load() == 1) {
            result = stats;
            return true;
        }
        return false;
    };

    PeerListModel model{node, nullptr};
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);

    model.refresh();
    QTRY_VERIFY(!model.readError().isEmpty());
    QTRY_COMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);
    QCOMPARE(node.calls.getNodesStats.load(), 2);
}

void PeerListModelTests::startStopAutoRefresh()
{
    const auto stats{MakeStats({MakeNodeStats(1, "10.0.0.1:8333", true, ConnectionType::INBOUND, NET_IPV4)})};

    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    std::atomic<int> get_nodes_stats_calls{0};
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& out_stats) {
        ++get_nodes_stats_calls;
        out_stats = stats;
        return true;
    };

    PeerListModel model{node, nullptr};
    QTRY_COMPARE(model.rowCount(), 1);
    const int calls_after_ctor = get_nodes_stats_calls.load();
    QCOMPARE(calls_after_ctor, 1);

    model.startAutoRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(get_nodes_stats_calls > calls_after_ctor, AUTO_REFRESH_TRIGGER_TIMEOUT);

    model.stopAutoRefresh();
    const int calls_after_stop = get_nodes_stats_calls.load();
    QTest::qWait(AUTO_REFRESH_STOP_WAIT);
    QCOMPARE(get_nodes_stats_calls, calls_after_stop);
    QVERIFY(node.calls.getNodesStats.load() >= 2);
}

void PeerListModelTests::sortProxySortsByRoles()
{
    auto stats_a = MakeNodeStats(10, "10.0.0.20:8333", false, ConnectionType::MANUAL, NET_IPV6);
    stats_a.m_connected = NodeClock::time_point{std::chrono::seconds{200}};
    stats_a.m_min_ping_time = std::chrono::microseconds{5'000};
    stats_a.nSendBytes = 400;
    stats_a.nRecvBytes = 300;
    stats_a.cleanSubVer = "/Satoshi:27.0.0/";

    auto stats_b = MakeNodeStats(20, "10.0.0.10:8333", true, ConnectionType::OUTBOUND_FULL_RELAY, NET_IPV4);
    stats_b.m_connected = NodeClock::time_point{std::chrono::seconds{400}};
    stats_b.m_min_ping_time = std::chrono::microseconds{2'000};
    stats_b.nSendBytes = 100;
    stats_b.nRecvBytes = 500;
    stats_b.cleanSubVer = "/Satoshi:26.0.0/";

    auto stats_c = MakeNodeStats(30, "10.0.0.30:8333", false, ConnectionType::BLOCK_RELAY, NET_ONION);
    stats_c.m_connected = NodeClock::time_point{std::chrono::seconds{100}};
    stats_c.m_min_ping_time = std::chrono::microseconds{8'000};
    stats_c.nSendBytes = 700;
    stats_c.nRecvBytes = 200;
    stats_c.cleanSubVer = "/Satoshi:28.0.0/";

    const QVector<CNodeStats> source_stats{stats_b, stats_c, stats_a};
    const auto stats{MakeStats({stats_b, stats_c, stats_a})};
    const std::map<qint64, CNodeStats> stats_by_id{
        {stats_a.nodeid, stats_a},
        {stats_b.nodeid, stats_b},
        {stats_c.nodeid, stats_c},
    };

    MockNode node;
    const auto drain_native_owner = DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = stats;
        return true;
    };

    PeerListModel model{node, nullptr};
    QTRY_COMPARE(model.rowCount(), 3);
    PeerListProxyModel proxy{nullptr};
    proxy.setSourceModel(&model);

    const auto assert_sort = [&](const QString& role_name, auto less_than) {
        proxy.setSortBy(role_name);
        QTRY_VERIFY(!proxy.projectionPending());
        QCOMPARE(proxy.rowCount(), 3);

        QVector<qint64> actual_ids;
        actual_ids.reserve(proxy.rowCount());
        for (int row = 0; row < proxy.rowCount(); ++row) {
            actual_ids.append(proxy.data(proxy.index(row, 0), PeerListModel::NetNodeId).toLongLong());
        }

        QVector<qint64> expected_ids;
        expected_ids.reserve(source_stats.size());
        for (const auto& node_stats : source_stats) {
            expected_ids.append(node_stats.nodeid);
        }
        std::sort(expected_ids.begin(), expected_ids.end());

        QVector<qint64> sorted_actual_ids = actual_ids;
        std::sort(sorted_actual_ids.begin(), sorted_actual_ids.end());
        QCOMPARE(sorted_actual_ids, expected_ids);

        for (int i = 1; i < actual_ids.size(); ++i) {
            const auto prev_it = stats_by_id.find(actual_ids.at(i - 1));
            const auto cur_it = stats_by_id.find(actual_ids.at(i));
            QVERIFY(prev_it != stats_by_id.end());
            QVERIFY(cur_it != stats_by_id.end());

            // Allow equal-key items in any order, but disallow an inversion.
            QVERIFY(!less_than(cur_it->second, prev_it->second));
        }
    };

    assert_sort("nodeId", [](const CNodeStats& left, const CNodeStats& right) { return left.nodeid < right.nodeid; });
    assert_sort("age", [](const CNodeStats& left, const CNodeStats& right) { return left.m_connected > right.m_connected; });
    assert_sort("address", [](const CNodeStats& left, const CNodeStats& right) { return left.m_addr_name.compare(right.m_addr_name) < 0; });
    assert_sort("direction", [](const CNodeStats& left, const CNodeStats& right) { return left.fInbound > right.fInbound; });
    assert_sort("connectionType", [](const CNodeStats& left, const CNodeStats& right) {
        return PeerStatsUtil::ConnectionTypeToQString(left.m_conn_type, false).localeAwareCompare(
            PeerStatsUtil::ConnectionTypeToQString(right.m_conn_type, false)) < 0;
    });
    assert_sort("network", [](const CNodeStats& left, const CNodeStats& right) {
        return PeerStatsUtil::NetworkToQString(left.m_network).localeAwareCompare(
            PeerStatsUtil::NetworkToQString(right.m_network)) < 0;
    });
    assert_sort("ping", [](const CNodeStats& left, const CNodeStats& right) { return left.m_min_ping_time < right.m_min_ping_time; });
    assert_sort("sent", [](const CNodeStats& left, const CNodeStats& right) { return left.nSendBytes < right.nSendBytes; });
    assert_sort("received", [](const CNodeStats& left, const CNodeStats& right) { return left.nRecvBytes < right.nRecvBytes; });
    assert_sort("subversion", [](const CNodeStats& left, const CNodeStats& right) { return left.cleanSubVer.compare(right.cleanSubVer) < 0; });
    assert_sort("transport", [](const CNodeStats& left, const CNodeStats& right) {
        return PeerStatsUtil::TransportToQString(left.m_transport_type).localeAwareCompare(
            PeerStatsUtil::TransportToQString(right.m_transport_type)) < 0;
    });

    proxy.setSortBy("nodeId");
    QTRY_VERIFY(!proxy.projectionPending());
    proxy.setSortAscending(false);
    QTRY_VERIFY(!proxy.projectionPending());
    QCOMPARE(proxy.data(proxy.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 30LL);

    proxy.setSearchText("10.0.0.10");
    QTRY_VERIFY(!proxy.projectionPending());
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.data(proxy.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 20LL);
    proxy.setSearchText({});
    QTRY_VERIFY(!proxy.projectionPending());

    proxy.setDirectionFilters({QStringLiteral("outbound")});
    QTRY_VERIFY(!proxy.projectionPending());
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setNetworkFilters({QStringLiteral("onion")});
    QTRY_VERIFY(!proxy.projectionPending());
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.data(proxy.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 30LL);
    proxy.setDirectionFilters({});
    QTRY_VERIFY(!proxy.projectionPending());
    proxy.setNetworkFilters({});
    QTRY_VERIFY(!proxy.projectionPending());

    proxy.setDirectionFilters({QStringLiteral("inbound"), QStringLiteral("outbound")});
    QTRY_VERIFY(!proxy.projectionPending());
    QCOMPARE(proxy.rowCount(), 3);
    proxy.setConnectionTypeFilters({QStringLiteral("manual"), QStringLiteral("block-relay")});
    QTRY_VERIFY(!proxy.projectionPending());
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setDirectionFilters({});
    QTRY_VERIFY(!proxy.projectionPending());
    proxy.setConnectionTypeFilters({});
    QTRY_VERIFY(!proxy.projectionPending());

    const int node_20_row = proxy.indexOfNodeId(20);
    QVERIFY(node_20_row >= 0);
    auto* details = proxy.peerDetailsAt(node_20_row);
    QVERIFY(details != nullptr);
    QCOMPARE(details->nodeId(), 20);
    QCOMPARE(proxy.peerDetailsAt(node_20_row), details);
    QCOMPARE(node.calls.getNodesStats.load(), 1);
}

void PeerListModelTests::manualProjectionBudgetsAndPersistentIndexes()
{
    ManualStartupExecution transport;
    std::deque<std::function<void()>> gui;
    interfaces::Node::NodesStats stats;
    for (int id=0; id<96; ++id) stats.emplace_back(MakeNodeStats(id,"127.0.0.1:8333",false,ConnectionType::MANUAL,NET_IPV4),true,CNodeStateStats{});
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_nodes_stats_fn=[&](auto& result) { result=stats; return true; };
    PeerListModel model{node,nullptr,true,[&](auto turn) { gui.push_back(std::move(turn)); },
        [] { return NodeClock::time_point{std::chrono::seconds{1060}}; }, NativeListQueues(transport.executor())};
    auto publish=[&] { if(gui.empty())return false; auto turn=std::move(gui.front());gui.pop_front();turn();return true; };
    QVERIFY(transport.step(execution::Lane::Core)); QVERIFY(transport.step(execution::Lane::Utility));
    QVERIFY(publish()); QCOMPARE(model.rowCount(),32); QVERIFY(model.projectionPending());
    QVERIFY(publish()); QCOMPARE(model.rowCount(),64);
    QPersistentModelIndex persistent{model.index(model.rowForId(60))};
    QVERIFY(publish()); QCOMPARE(model.rowCount(),96); QVERIFY(!model.projectionPending());
    QCOMPARE(model.data(persistent,PeerListModel::Age).toString(),QStringLiteral("1 m"));
    auto original=model.data(persistent,PeerListModel::StatsRole).value<PeerStatsHandle>();
    PeerDetailsModel detail{original,&model}; original.reset();
    QSignalSpy changed{&detail,&PeerDetailsModel::dataChanged};
    QSignalSpy reset{&model,&QAbstractItemModel::modelReset};
    stats.erase(stats.begin(),stats.begin()+40);
    std::get<0>(stats[20]).m_addr_name="updated.example:8333";
    model.refresh(); QVERIFY(transport.step(execution::Lane::Core)); QVERIFY(transport.step(execution::Lane::Utility));
    while(publish()) {}
    QCOMPARE(model.rowCount(),56); QVERIFY(persistent.isValid()); QCOMPARE(persistent.row(),20);
    QCOMPARE(model.data(persistent,PeerListModel::NetNodeId).toLongLong(),qint64{60});
    QCOMPARE(detail.address(),QStringLiteral("updated.example:8333")); QCOMPARE(changed.size(),1); QCOMPARE(reset.size(),0);
    QCOMPARE(model.rowForId(60),20);
    model.close(); transport.drain(); QVERIFY(transport.quiescent());
}

void PeerListModelTests::manualFilterStaleAndQuietClose()
{
    ManualStartupExecution transport;
    std::deque<std::function<void()>> gui;
    const qint64 large_id=(qint64{1}<<40)+7;
    auto stats=MakeStats({MakeNodeStats(1,"alpha:8333",false,ConnectionType::MANUAL,NET_IPV4),
                          MakeNodeStats(large_id,"beta:8333",true,ConnectionType::INBOUND,NET_IPV6)});
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    node.get_nodes_stats_fn=[&](auto& result) { result=stats;return true; };
    PeerListModel model{node,nullptr,true,[&](auto turn) { gui.push_back(std::move(turn)); }, [] { return NodeClock::now(); }, NativeListQueues(transport.executor())};
    auto publish=[&] { if(gui.empty())return false;auto turn=std::move(gui.front());gui.pop_front();turn();return true; };
    QVERIFY(transport.step(execution::Lane::Core)); QVERIFY(transport.step(execution::Lane::Utility)); QVERIFY(publish());
    PeerListProxyModel proxy{nullptr}; proxy.setSourceModel(&model);
    QVERIFY(transport.step(execution::Lane::Utility)); while (transport.step(execution::Lane::Utility)) {} QVERIFY(publish());
    QCOMPARE(proxy.rowCount(),2); QCOMPARE(proxy.indexOfNodeId(large_id),1);
    QPersistentModelIndex persistent{proxy.index(proxy.indexOfNodeId(large_id))};
    auto* details=proxy.peerDetailsAt(persistent.row()); QVERIFY(details); QCOMPARE(details->nodeId(),large_id);
    proxy.setSearchText("alpha"); QVERIFY(transport.step(execution::Lane::Utility)); while (transport.step(execution::Lane::Utility)) {}
    proxy.setSearchText("beta");
    // The held older filter result cannot remove or retarget the current row.
    QVERIFY(publish()); QCOMPARE(proxy.rowCount(),2); QVERIFY(persistent.isValid());
    QVERIFY(transport.step(execution::Lane::Utility)); while (transport.step(execution::Lane::Utility)) {}
    while(publish()) {}
    QCOMPARE(proxy.rowCount(),1); QVERIFY(persistent.isValid()); QCOMPARE(persistent.row(),0);
    QCOMPARE(proxy.mapToSource(persistent),model.index(model.rowForId(large_id)));
    QCOMPARE(proxy.mapFromSource(model.index(model.rowForId(large_id))),proxy.index(0));
    proxy.setSearchText("alpha"); QVERIFY(transport.step(execution::Lane::Utility)); while (transport.step(execution::Lane::Utility)) {}
    QSignalSpy changes{&proxy,&QAbstractItemModel::rowsRemoved};
    proxy.close(); model.close(); while(publish()) {}
    QCOMPARE(changes.size(),0); QCOMPARE(details->nodeId(),large_id);
    transport.drain(); QVERIFY(transport.quiescent());
}

void PeerListModelTests::realThreadReadsNeverRunOnGui()
{
    MockNode node;
    const auto drain_native_owner=DrainExecutorsBeforeNativeOwnerDestruction();
    const auto* gui=QThread::currentThread(); std::atomic<bool> off_gui{false};
    node.get_nodes_stats_fn=[&](auto& result) { off_gui=QThread::currentThread()!=gui;
        result=MakeStats({MakeNodeStats(5,"real-thread:8333",false,ConnectionType::MANUAL,NET_IPV4)});return true; };
    PeerListModel model{node,nullptr}; QTRY_COMPARE(model.rowCount(),1); QVERIFY(off_gui.load());
    QVERIFY(!model.projectionPending());
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PeerListModelTests)
#else
QTEST_MAIN(PeerListModelTests)
#endif
#include <test_peerlistmodel.moc>
