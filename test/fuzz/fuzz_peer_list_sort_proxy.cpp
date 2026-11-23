// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistmodel.h>
#include <qml/models/peerlistsortproxy.h>

#include <test/mocks/mocknode.h>

#include <net.h>
#include <net_processing.h>

#include <QByteArray>
#include <QApplication>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

static int argc = 1;
static char arg0[] = "fuzz_peer_list_sort_proxy";
static char* argv[] = {arg0, nullptr};
static QApplication app(argc, argv);

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    MockNode node;

    interfaces::Node::NodesStats stats;
    const QByteArray raw(reinterpret_cast<const char*>(data), static_cast<int>(size));
    const QString text = QString::fromUtf8(raw);
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (int i = 0; i < lines.size() && i < 4; ++i) {
        CNodeStats node_stats{};
        CNodeStateStats state_stats{};
        node_stats.nodeid = i;
        node_stats.m_addr_name = lines[i].toStdString();
        node_stats.cleanSubVer = lines[i].toStdString();
        stats.emplace_back(node_stats, true, state_stats);
    }

    node.get_nodes_stats_fn = [&stats](interfaces::Node::NodesStats& out) {
        out = stats;
        return true;
    };

    PeerListModel source(node, /*parent=*/nullptr, /*backend_ready=*/true);
    source.refresh();

    PeerListSortProxy proxy(nullptr);
    proxy.setSourceModel(&source);

    // Split remainder of input into null-terminated filter strings.
    int field = 0;
    size_t start = 0;
    for (size_t i = 0; i <= size; ++i) {
        if (i == size || data[i] == '\0') {
            const QByteArray field_raw(reinterpret_cast<const char*>(data + start), static_cast<int>(i - start));
            const QString field_text = QString::fromUtf8(field_raw);
            switch (field % 7) {
            case 0: proxy.setSearchText(field_text); break;
            case 1: proxy.setSortBy(field_text); break;
            case 2: proxy.setSortAscending(field_text.startsWith(QLatin1Char('1'))); break;
            case 3: proxy.setDirectionFilters(field_text.split(QLatin1Char(','), Qt::SkipEmptyParts)); break;
            case 4: proxy.setConnectionTypeFilters(field_text.split(QLatin1Char(','), Qt::SkipEmptyParts)); break;
            case 5: proxy.setNetworkFilters(field_text.split(QLatin1Char(','), Qt::SkipEmptyParts)); break;
            case 6: proxy.setTransportFilters(field_text.split(QLatin1Char(','), Qt::SkipEmptyParts)); break;
            }
            ++field;
            start = i + 1;
        }
    }

    (void)proxy.rowCount();
    (void)proxy.indexOfNodeId(0);
    return 0;
}
