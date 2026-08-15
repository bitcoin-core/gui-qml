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
    NotifyBlockTipFn notify_block_tip_fn;

    struct Calls {
        CallCounter startShutdown{"startShutdown"};
        CallCounter shutdownRequested{"shutdownRequested"};
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

    std::unique_ptr<interfaces::Handler> handleNotifyBlockTip(NotifyBlockTipFn fn) override
    {
        ++calls.handleNotifyBlockTip;
        notify_block_tip_fn = std::move(fn);
        return interfaces::MakeCleanupHandler([this] { notify_block_tip_fn = {}; });
    }
};

#endif // BITCOIN_QML_TEST_MOCKS_MOCKNODE_H
