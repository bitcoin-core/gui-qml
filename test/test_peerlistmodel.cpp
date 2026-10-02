// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerlistmodel.h>
#include <util/translation.h>

#include <QScopeGuard>
#include <QSemaphore>
#include <QThread>
#include <QTimer>

#include <algorithm>
#include <atomic>
#include <map>
#include <utility>

namespace {
class DrainingPeerListModel : public PeerListModel
{
public:
    using PeerListModel::PeerListModel;
    ~DrainingPeerListModel() override
    {
        beginShutdown();
        if (!QTest::qWaitFor([this] { return isDrained(); }, 10'000)) qFatal("Peer fixture did not drain");
    }
};
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
    void gatedRefreshKeepsGuiResponsiveAndCoalescesRequests();
    void hiddenGenerationDropsStaleSnapshotAndReopens();
    void shutdownDrainsBorrowedBackendAndRejectsCompletion();
    void detailsOwnSnapshotsAcrossReorderAndRemoval();
    void mapsRoleData();
    void refreshUpdatesRows();
    void refreshHandlesGetNodesStatsFailure();
    void startStopAutoRefresh();
    void sortProxySortsByRoles();
};

void PeerListModelTests::mapsRoleData()
{
    auto stats{MakeStats({MakeNodeStats(7, "127.0.0.1:8333", false, ConnectionType::OUTBOUND_FULL_RELAY, NET_IPV4)})};
    std::get<0>(stats[0]).m_session_id = "043604a60a54b3f5";
    std::get<0>(stats[0]).m_bip152_highbandwidth_to = true;
    std::get<2>(stats[0]).m_addr_relay_enabled = true;
    std::get<2>(stats[0]).m_addr_processed = 1'076;
    std::get<2>(stats[0]).m_addr_rate_limited = 3;
    MockNode node;
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = stats;
        return true;
    };

    DrainingPeerListModel model{node, nullptr};
    QTRY_VERIFY(model.ready());
    QCOMPARE(model.rowCount(), 1);
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

    const CNodeCombinedStats* stats_ptr = model.data(index, PeerListModel::StatsRole).value<const CNodeCombinedStats*>();
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
    const std::vector<interfaces::Node::NodesStats> responses{stats_initial, stats_remove, stats_insert};
    size_t response_index{0};
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = responses.at(response_index++);
        return true;
    };

    DrainingPeerListModel model{node, nullptr};
    QTRY_VERIFY(model.ready());
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);
    QCOMPARE(model.data(model.index(1, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);

    model.refresh();
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);

    model.refresh();
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);
    QCOMPARE(model.data(model.index(1, 0), PeerListModel::NetNodeId).toLongLong(), 3LL);
    QCOMPARE(node.calls.getNodesStats.load(), 3);
}

void PeerListModelTests::refreshHandlesGetNodesStatsFailure()
{
    const auto stats{MakeStats({MakeNodeStats(1, "10.0.0.1:8333", true, ConnectionType::INBOUND, NET_IPV4)})};

    MockNode node;
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        if (node.calls.getNodesStats.load() == 1) {
            result = stats;
            return true;
        }
        return false;
    };

    DrainingPeerListModel model{node, nullptr};
    QTRY_VERIFY(model.ready());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);

    model.refresh();
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);
    QCOMPARE(node.calls.getNodesStats.load(), 2);
    QVERIFY(!model.refreshError().isEmpty());
}

void PeerListModelTests::startStopAutoRefresh()
{
    const auto stats{MakeStats({MakeNodeStats(1, "10.0.0.1:8333", true, ConnectionType::INBOUND, NET_IPV4)})};

    MockNode node;
    std::atomic<int> get_nodes_stats_calls{0};
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& out_stats) {
        ++get_nodes_stats_calls;
        out_stats = stats;
        return true;
    };

    DrainingPeerListModel model{node, nullptr};
    QTRY_VERIFY(model.ready());
    const int calls_after_ctor = get_nodes_stats_calls.load();
    QCOMPARE(calls_after_ctor, 1);

    model.startAutoRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(get_nodes_stats_calls.load() > calls_after_ctor, AUTO_REFRESH_TRIGGER_TIMEOUT);

    model.stopAutoRefresh();
    QTRY_VERIFY(!model.refreshPending());
    const int calls_after_stop = get_nodes_stats_calls.load();
    QTest::qWait(AUTO_REFRESH_STOP_WAIT);
    QCOMPARE(get_nodes_stats_calls.load(), calls_after_stop);
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
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = stats;
        return true;
    };

    DrainingPeerListModel model{node, nullptr};
    QTRY_VERIFY(model.ready());
    PeerListSortProxy proxy{nullptr};
    proxy.setSourceModel(&model);

    const auto assert_sort = [&](const QString& role_name, auto less_than) {
        proxy.setSortBy(role_name);

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
    proxy.setSortAscending(false);
    QCOMPARE(proxy.data(proxy.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 30LL);

    proxy.setSearchText("10.0.0.10");
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.data(proxy.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 20LL);
    proxy.setSearchText({});

    proxy.setDirectionFilters({QStringLiteral("outbound")});
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setNetworkFilters({QStringLiteral("onion")});
    QCOMPARE(proxy.rowCount(), 1);
    QCOMPARE(proxy.data(proxy.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 30LL);
    proxy.setDirectionFilters({});
    proxy.setNetworkFilters({});

    proxy.setDirectionFilters({QStringLiteral("inbound"), QStringLiteral("outbound")});
    QCOMPARE(proxy.rowCount(), 3);
    proxy.setConnectionTypeFilters({QStringLiteral("manual"), QStringLiteral("block-relay")});
    QCOMPARE(proxy.rowCount(), 2);
    proxy.setDirectionFilters({});
    proxy.setConnectionTypeFilters({});

    const int node_20_row = proxy.indexOfNodeId(20);
    QVERIFY(node_20_row >= 0);
    auto* details = proxy.peerDetailsAt(node_20_row);
    QVERIFY(details != nullptr);
    QCOMPARE(details->nodeId(), 20);
    QCOMPARE(proxy.peerDetailsAt(node_20_row), details);
    QCOMPARE(node.calls.getNodesStats.load(), 1);
}


void PeerListModelTests::gatedRefreshKeepsGuiResponsiveAndCoalescesRequests()
{
    MockNode node;
    QSemaphore entered, release;
    std::atomic<bool> worker_thread{false};
    const auto* gui_thread = QThread::currentThread();
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        worker_thread = QThread::currentThread() != gui_thread;
        entered.release();
        release.acquire();
        result = MakeStats({MakeNodeStats(node.calls.getNodesStats.load(), "127.0.0.1:8333", false, ConnectionType::MANUAL, NET_IPV4)});
        return true;
    };
    DrainingPeerListModel model{node, nullptr, false};
    const auto unblock = qScopeGuard([&] { release.release(3); });
    model.refresh();
    QTest::qWait(20);
    QCOMPARE(node.calls.getNodesStats.load(), 0);
    model.backendInitialized();
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    QVERIFY(worker_thread.load());
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    for (int i = 0; i < 100; ++i) model.refresh();
    QTRY_VERIFY(gui_progress);
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(model.refreshPending());
    QCOMPARE(node.calls.getNodesStats.load(), 1);
    bool model_signal_on_gui{false};
    connect(&model, &PeerListModel::modelReset, &model, [&] { model_signal_on_gui = QThread::currentThread() == gui_thread; });
    release.release();
    QTRY_VERIFY(entered.available() > 0);
    entered.acquire();
    QCOMPARE(node.calls.getNodesStats.load(), 2);
    QVERIFY(model_signal_on_gui);
    release.release();
    QTRY_VERIFY(!model.refreshPending());
    QTest::qWait(20);
    QCOMPARE(node.calls.getNodesStats.load(), 2);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);
    for (int i = 0; i < 20; ++i) model.data(model.index(0, 0), PeerListModel::Address);
    QCOMPARE(node.calls.getNodesStats.load(), 2);
}

void PeerListModelTests::hiddenGenerationDropsStaleSnapshotAndReopens()
{
    MockNode node;
    QSemaphore entered, release;
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        if (node.calls.getNodesStats.load() == 1) { entered.release(); release.acquire(); }
        result = MakeStats({MakeNodeStats(node.calls.getNodesStats.load(), "127.0.0.1:8333", false, ConnectionType::MANUAL, NET_IPV4)});
        return true;
    };
    DrainingPeerListModel model{node, nullptr, false};
    const auto unblock = qScopeGuard([&] { release.release(); });
    model.startAutoRefresh();
    model.backendInitialized();
    QTRY_VERIFY(entered.available() > 0);
    model.stopAutoRefresh();
    release.release();
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(model.rowCount(), 0);
    QVERIFY(!model.ready());
    model.startAutoRefresh();
    QTRY_VERIFY(model.ready());
    model.stopAutoRefresh();
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);
}

void PeerListModelTests::shutdownDrainsBorrowedBackendAndRejectsCompletion()
{
    MockNode node;
    QSemaphore entered, release;
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        entered.release(); release.acquire();
        result = MakeStats({MakeNodeStats(1, "127.0.0.1:8333", false, ConnectionType::MANUAL, NET_IPV4)});
        return true;
    };
    DrainingPeerListModel model{node, nullptr};
    const auto unblock = qScopeGuard([&] { release.release(); });
    QSignalSpy resets{&model, &PeerListModel::modelReset};
    QSignalSpy drained{&model, &PeerListModel::drained};
    QTRY_VERIFY(entered.available() > 0);
    model.beginShutdown();
    model.refresh();
    model.startAutoRefresh();
    bool gui_progress{false};
    QTimer::singleShot(0, this, [&] { gui_progress = true; });
    QTRY_VERIFY(gui_progress);
    QVERIFY(!model.isDrained());
    QCOMPARE(node.calls.getNodesStats.load(), 1);
    release.release();
    QTRY_COMPARE(drained.count(), 1);
    QCOMPARE(resets.count(), 0);
    QCOMPARE(model.rowCount(), 0);
}

void PeerListModelTests::detailsOwnSnapshotsAcrossReorderAndRemoval()
{
    MockNode node;
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        const int call = node.calls.getNodesStats.load();
        if (call == 1) result = MakeStats({MakeNodeStats(1, "first", true, ConnectionType::INBOUND, NET_IPV4), MakeNodeStats(2, "selected", false, ConnectionType::MANUAL, NET_IPV4)});
        else if (call == 2) result = MakeStats({MakeNodeStats(2, "updated", false, ConnectionType::MANUAL, NET_IPV4), MakeNodeStats(1, "first", true, ConnectionType::INBOUND, NET_IPV4)});
        else result = {};
        return true;
    };
    DrainingPeerListModel model{node, nullptr};
    QTRY_VERIFY(model.ready());
    PeerDetailsModel* selected{nullptr};
    QString during_reset;
    // This subscriber runs before the detail object's own reset handler.
    connect(&model, &PeerListModel::modelReset, &model, [&] { during_reset = selected->address(); });
    PeerDetailsModel details{model.data(model.index(1, 0), PeerListModel::StatsRole).value<const CNodeCombinedStats*>(), &model};
    selected = &details;
    QSignalSpy disconnected{&details, &PeerDetailsModel::disconnected};
    model.refresh();
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(during_reset, QStringLiteral("selected"));
    QCOMPARE(details.nodeId(), 2);
    QCOMPARE(details.address(), QStringLiteral("updated"));
    QCOMPARE(disconnected.count(), 0);
    model.refresh();
    QTRY_VERIFY(!model.refreshPending());
    QCOMPARE(disconnected.count(), 1);
    QCOMPARE(details.address(), QStringLiteral("updated"));
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PeerListModelTests)
#else
QTEST_MAIN(PeerListModelTests)
#endif
#include "test_peerlistmodel.moc"
