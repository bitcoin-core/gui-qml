// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_WALLETSESSION_H
#define BITCOIN_QML_TEST_MOCKS_WALLETSESSION_H

#include <qml/executor.h>
#include <qml/walletopening.h>

inline walletopening::Session::Queues WalletSessionQueues(execution::Executor& executor, execution::Executor& retirement)
{
    using execution::Lane;
    auto core = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Core));
    auto utility = std::make_shared<execution::SerialQueue>(executor.makeQueue(Lane::Utility));
    auto cleanup = std::make_shared<execution::SerialQueue>(retirement.makeQueue(Lane::Utility));
    return {cleanup, [core, utility](auto done) {
        core->close();
        core->whenDrained([utility, done = std::move(done)] {
            utility->close();
            utility->whenDrained(done);
        });
    }, core, utility};
}

inline std::shared_ptr<walletopening::Session> MakeWalletSession(execution::Executor& executor,
    walletopening::Session::Notify notify, walletopening::NativeEvents events = {})
{
    return std::make_shared<walletopening::Session>(
        std::make_shared<std::atomic_bool>(false), std::move(notify), walletopening::Session::Preferences{},
        std::chrono::steady_clock::now, nullptr, std::move(events), WalletSessionQueues(executor, executor));
}

#endif // BITCOIN_QML_TEST_MOCKS_WALLETSESSION_H
