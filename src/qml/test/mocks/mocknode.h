// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MOCKNODE_H
#define BITCOIN_QML_TEST_MOCKS_MOCKNODE_H

#include <interfaces/handler.h>
#include <qml/test/mocks/callcounter.h>
#include <qml/test/mocks/stubnode.h>

#include <functional>
#include <utility>

class MockNode : public StubNode
{
public:
    std::function<void()> start_shutdown_fn;
    std::function<bool()> shutdown_requested_fn;
    std::function<bool(NodesStats&)> get_nodes_stats_fn;
    std::function<bool(banmap_t&)> get_banned_fn;
    std::function<bool(const CSubNet&)> unban_fn;
    std::function<int64_t()> get_total_bytes_recv_fn;
    std::function<int64_t()> get_total_bytes_sent_fn;
    NotifyBlockTipFn notify_block_tip_fn;

    struct Calls {
        CallCounter startShutdown{"startShutdown"};
        CallCounter shutdownRequested{"shutdownRequested"};
        CallCounter getNodesStats{"getNodesStats"};
        CallCounter getBanned{"getBanned"};
        CallCounter unban{"unban"};
        CallCounter getTotalBytesRecv{"getTotalBytesRecv"};
        CallCounter getTotalBytesSent{"getTotalBytesSent"};
        CallCounter handleNotifyBlockTip{"handleNotifyBlockTip"};
    } calls;

    void startShutdown() override
    {
        ++calls.startShutdown;
        if (start_shutdown_fn) start_shutdown_fn();
    }

    bool shutdownRequested() override
    {
        ++calls.shutdownRequested;
        return shutdown_requested_fn ? shutdown_requested_fn() : false;
    }

    bool getNodesStats(NodesStats& stats) override
    {
        ++calls.getNodesStats;
        return get_nodes_stats_fn ? get_nodes_stats_fn(stats) : false;
    }

    bool getBanned(banmap_t& banned) override
    {
        ++calls.getBanned;
        return get_banned_fn ? get_banned_fn(banned) : false;
    }

    bool unban(const CSubNet& subnet) override
    {
        ++calls.unban;
        return unban_fn ? unban_fn(subnet) : false;
    }

    int64_t getTotalBytesRecv() override
    {
        ++calls.getTotalBytesRecv;
        return get_total_bytes_recv_fn ? get_total_bytes_recv_fn() : 0;
    }

    int64_t getTotalBytesSent() override
    {
        ++calls.getTotalBytesSent;
        return get_total_bytes_sent_fn ? get_total_bytes_sent_fn() : 0;
    }

    std::unique_ptr<interfaces::Handler> handleNotifyBlockTip(NotifyBlockTipFn fn) override
    {
        ++calls.handleNotifyBlockTip;
        notify_block_tip_fn = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { notify_block_tip_fn = {}; });
    }
};

#endif // BITCOIN_QML_TEST_MOCKS_MOCKNODE_H
