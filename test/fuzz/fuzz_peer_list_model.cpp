// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/peerlistmodel.h>

#include <test/mocks/mocknode.h>

#include <net.h>
#include <net_processing.h>

#include <QByteArray>
#include <QApplication>
#include <QString>

#include <cstddef>
#include <cstdint>
#include <string>
#include <tuple>
#include <vector>

static int argc = 1;
static char arg0[] = "fuzz_peer_list_model";
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

    PeerListModel model(node, /*parent=*/nullptr, /*backend_ready=*/true);
    model.refresh();
    (void)model.rowCount();
    model.startAutoRefresh();
    model.stopAutoRefresh();
    model.beginShutdown();
    return 0;
}
