// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <QtTest/QtTest>

#include <test/mocks/mocknode.h>
#include <qml/models/peerdetailsmodel.h>
#include <qml/models/peerlistsortproxy.h>
#include <qml/models/peerlistmodel.h>
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
    void mapsRoleData();
    void refreshUpdatesRows();
    void refreshHandlesGetNodesStatsFailure();
    void startStopAutoRefresh();
    void sortProxySortsByRoles();
    void summarizesNetworksAndDirections();
    void widgetAndTableShareRefreshLifecycle();
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

    PeerListModel model{node, nullptr};
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

void PeerListModelTests::summarizesNetworksAndDirections()
{
    auto stats{MakeStats({
        MakeNodeStats(1, "ipv4", true, ConnectionType::INBOUND, NET_IPV4),
        MakeNodeStats(2, "manual ipv4", false, ConnectionType::MANUAL, NET_IPV4),
        MakeNodeStats(3, "ipv6", false, ConnectionType::OUTBOUND_FULL_RELAY, NET_IPV6),
        MakeNodeStats(4, "tor", false, ConnectionType::BLOCK_RELAY, NET_ONION),
        MakeNodeStats(5, "i2p", true, ConnectionType::INBOUND, NET_I2P),
        MakeNodeStats(6, "cjdns", true, ConnectionType::INBOUND, NET_CJDNS),
        MakeNodeStats(7, "other", false, ConnectionType::FEELER, NET_UNROUTABLE),
    })};
    bool available{true};
    MockNode node;
    node.get_nodes_stats_fn = [&](interfaces::Node::NodesStats& result) {
        result = stats;
        return available;
    };
    PeerListModel model{node, nullptr};
    const auto summary = model.summary();
    QCOMPARE(summary.value("ready").toBool(), true);
    QCOMPARE(summary.value("total").toInt(), 7);
    QCOMPARE(summary.value("inbound").toInt(), 3);
    QCOMPARE(summary.value("outbound").toInt(), 4);
    const auto groups = summary.value("groups").toList();
    QCOMPARE(groups.size(), 6);
    QCOMPARE(groups[0].toMap().value("id").toString(), QString{"ipv4"});
    QCOMPARE(groups[0].toMap().value("count").toInt(), 2);
    int sum{0};
    for (const auto& group : groups) sum += group.toMap().value("count").toInt();
    QCOMPARE(sum, summary.value("total").toInt());

    QSignalSpy changed{&model, &PeerListModel::summaryChanged};
    model.refresh();
    QCOMPARE(changed.count(), 0);
    // Changes to a peer's network or direction matter even when the total is unchanged.
    std::get<0>(stats[0]).m_network = NET_IPV6;
    std::get<0>(stats[0]).fInbound = false;
    model.refresh();
    QCOMPARE(changed.count(), 1);
    QCOMPARE(model.summary().value("inbound").toInt(), 2);
    QCOMPARE(model.summary().value("groups").toList()[0].toMap().value("count").toInt(), 1);

    available = false;
    model.refresh();
    QVERIFY(!model.summary().value("ready").toBool());
    available = true;
    stats.clear();
    model.refresh();
    QVERIFY(model.summary().value("ready").toBool());
    QCOMPARE(model.summary().value("total").toInt(), 0);
    QVERIFY(model.summary().value("groups").toList().isEmpty());
}

void PeerListModelTests::widgetAndTableShareRefreshLifecycle()
{
    MockNode node;
    node.get_nodes_stats_fn = [](interfaces::Node::NodesStats& result) {
        result.clear();
        return true;
    };
    PeerListModel model{node, nullptr};
    const auto calls = [&] { return node.calls.getNodesStats.load(); };
    model.setWidgetActive(true);
    QCOMPARE(calls(), 2); // Immediate snapshot on activation.
    model.startAutoRefresh();
    model.stopAutoRefresh(); // Closing the table must not stop an active widget.
    const int widget_calls = calls();
    QTRY_VERIFY_WITH_TIMEOUT(calls() > widget_calls, AUTO_REFRESH_TRIGGER_TIMEOUT);
    model.startAutoRefresh();
    model.setWidgetActive(false); // Hiding the dashboard must not stop the table.
    const int table_calls = calls();
    QTRY_VERIFY_WITH_TIMEOUT(calls() > table_calls, AUTO_REFRESH_TRIGGER_TIMEOUT);
    model.stopAutoRefresh();
    const int stopped_calls = calls();
    QTest::qWait(AUTO_REFRESH_STOP_WAIT);
    QCOMPARE(calls(), stopped_calls);
    model.setWidgetActive(true);
    model.stopForShutdown();
    const int shutdown_calls = calls();
    model.refresh();
    model.startAutoRefresh();
    model.setWidgetActive(false);
    model.setWidgetActive(true);
    QTest::qWait(AUTO_REFRESH_STOP_WAIT);
    QCOMPARE(calls(), shutdown_calls);
    QVERIFY(!model.summary().value("ready").toBool());
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

#ifdef BITCOINQML_NO_TEST_MAIN
#include <test/qt_test_registry.h>
BITCOINQML_REGISTER_QT_TEST(PeerListModelTests)
#else
QTEST_MAIN(PeerListModelTests)
#endif
#include "test_peerlistmodel.moc"
