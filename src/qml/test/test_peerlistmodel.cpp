// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <qml/test/mocks/mocknode.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerlistmodel.h>
#include <qml/models/peerdetailsmodel.h>
#include <util/translation.h>

#include <algorithm>
#include <map>
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
    CNodeStats stats;
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
    void detailsRetainSafeSnapshotsAcrossRemoval();
    void peerActionsValidateInputAndStopWithTheModel();
};

void PeerListModelTests::detailsRetainSafeSnapshotsAcrossRemoval()
{
    auto stats{MakeStats({MakeNodeStats(3'000'000'000LL, "192.0.2.1:8333", false, ConnectionType::MANUAL, NET_IPV4)})};
    MockNode node;
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) { result = stats; return true; };
    PeerListModel model{node, nullptr};
    PeerListSortProxy proxy{nullptr};
    proxy.setSourceModel(&model);
    auto* details{proxy.data(proxy.index(0, 0), PeerListModel::StatsRole).value<PeerDetailsModel*>()};
    QVERIFY(details);
    QCOMPARE(details->parent(), &model);
    QCOMPARE(details->nodeId(), 3'000'000'000LL);
    QCOMPARE(proxy.data(proxy.index(0, 0), PeerListModel::StatsRole).value<PeerDetailsModel*>(), details);
    QSignalSpy disconnected{details, &PeerDetailsModel::disconnected};
    stats.clear();
    model.refresh();
    QVERIFY(!details->connected());
    QCOMPARE(disconnected.count(), 1);
    QCOMPARE(details->address(), QStringLiteral("192.0.2.1:8333"));
    model.refresh();
    QCOMPARE(disconnected.count(), 1);
    QVERIFY(!proxy.data(proxy.index(99, 0), PeerListModel::StatsRole).isValid());
}

void PeerListModelTests::peerActionsValidateInputAndStopWithTheModel()
{
    class ActionNode : public MockNode {
    public:
        NodeId disconnected_id{-1};
        int bans{0}, address_disconnects{0};
        bool disconnectById(NodeId id) override { disconnected_id = id; return true; }
        bool ban(const CNetAddr&, int64_t) override { ++bans; return true; }
        bool disconnectByAddress(const CNetAddr&) override { ++address_disconnects; return true; }
    } node;
    node.get_nodes_stats_fn = [](interfaces::Node::NodesStats&) { return true; };
    PeerListModel model{node, nullptr};
    QVERIFY(model.disconnectPeer(3'000'000'000LL));
    QCOMPARE(node.disconnected_id, 3'000'000'000LL);
    QVERIFY(!model.banPeer(QStringLiteral("example.invalid"), 60));
    QVERIFY(!model.banPeer(QStringLiteral("192.0.2.1"), 0));
    QCOMPARE(node.bans, 0);
    QVERIFY(model.banPeer(QStringLiteral("192.0.2.1"), 60));
    QCOMPARE(node.bans, 1);
    QCOMPARE(node.address_disconnects, 1);
    QCOMPARE(node.calls.getNodesStats.load(), 3);
    model.stop();
    QVERIFY(!model.disconnectPeer(1));
    QVERIFY(!model.banPeer(QStringLiteral("192.0.2.1"), 60));
    model.refresh();
    QCOMPARE(node.calls.getNodesStats.load(), 3);
}

void PeerListModelTests::mapsRoleData()
{
    const auto stats{MakeStats({MakeNodeStats(7, "127.0.0.1:8333", false, ConnectionType::OUTBOUND_FULL_RELAY, NET_IPV4)})};
    MockNode node;
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = stats;
        return true;
    };

    PeerListModel model{node, nullptr};
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.rowCount(model.index(0, 0)), 0);

    const QModelIndex index = model.index(0, 0);
    QVERIFY(index.isValid());

    const auto roles = model.roleNames();
    QCOMPARE(roles.value(PeerListModel::NetNodeId), QByteArray{"nodeId"});
    QCOMPARE(roles.value(PeerListModel::Address), QByteArray{"address"});
    QCOMPARE(roles.value(PeerListModel::ConnectionType), QByteArray{"connectionType"});
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
    QVERIFY(!model.data(index, PeerListModel::Age).toString().isEmpty());

    const CNodeCombinedStats* stats_ptr = model.data(index, PeerListModel::StatsRole).value<const CNodeCombinedStats*>();
    QVERIFY(stats_ptr != nullptr);
    QCOMPARE(stats_ptr->nodeStats.nodeid, 7);

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

    PeerListModel model{node, nullptr};
    QCOMPARE(model.rowCount(), 2);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);
    QCOMPARE(model.data(model.index(1, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);

    model.refresh();
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 2LL);

    model.refresh();
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

    PeerListModel model{node, nullptr};
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);

    model.refresh();
    QCOMPARE(model.rowCount(), 1);
    QCOMPARE(model.data(model.index(0, 0), PeerListModel::NetNodeId).toLongLong(), 1LL);
    QCOMPARE(node.calls.getNodesStats.load(), 2);
}

void PeerListModelTests::startStopAutoRefresh()
{
    const auto stats{MakeStats({MakeNodeStats(1, "10.0.0.1:8333", true, ConnectionType::INBOUND, NET_IPV4)})};

    MockNode node;
    int get_nodes_stats_calls{0};
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& out_stats) {
        ++get_nodes_stats_calls;
        out_stats = stats;
        return true;
    };

    PeerListModel model{node, nullptr};
    const int calls_after_ctor = get_nodes_stats_calls;
    QCOMPARE(calls_after_ctor, 1);

    model.startAutoRefresh();
    QTRY_VERIFY_WITH_TIMEOUT(get_nodes_stats_calls > calls_after_ctor, AUTO_REFRESH_TRIGGER_TIMEOUT);

    model.stopAutoRefresh();
    const int calls_after_stop = get_nodes_stats_calls;
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
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = stats;
        return true;
    };

    PeerListModel model{node, nullptr};
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
    assert_sort("connectionType", [](const CNodeStats& left, const CNodeStats& right) { return left.m_conn_type < right.m_conn_type; });
    assert_sort("network", [](const CNodeStats& left, const CNodeStats& right) { return left.m_network < right.m_network; });
    assert_sort("ping", [](const CNodeStats& left, const CNodeStats& right) { return left.m_min_ping_time < right.m_min_ping_time; });
    assert_sort("sent", [](const CNodeStats& left, const CNodeStats& right) { return left.nSendBytes < right.nSendBytes; });
    assert_sort("received", [](const CNodeStats& left, const CNodeStats& right) { return left.nRecvBytes < right.nRecvBytes; });
    assert_sort("subversion", [](const CNodeStats& left, const CNodeStats& right) { return left.cleanSubVer.compare(right.cleanSubVer) < 0; });
    QCOMPARE(node.calls.getNodesStats.load(), 1);
}

#ifdef BITCOINQML_NO_TEST_MAIN
#include <qml/test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PeerListModelTests)
#else
QTEST_MAIN(PeerListModelTests)
#endif
#include "test_peerlistmodel.moc"
