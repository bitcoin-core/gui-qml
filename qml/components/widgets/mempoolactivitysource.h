// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_COMPONENTS_WIDGETS_MEMPOOLACTIVITYSOURCE_H
#define BITCOIN_QML_COMPONENTS_WIDGETS_MEMPOOLACTIVITYSOURCE_H

#include <interfaces/chain.h>

#include <atomic>
#include <cstdint>

/** Counts accepted arrivals, independently of removals, mining, and UI state. */
class MempoolActivitySource final : public interfaces::Chain::Notifications
{
public:
    void transactionAddedToMempool(const CTransactionRef& tx) override;
    uint64_t incomingVbytes() const { return m_incoming_vbytes.load(std::memory_order_relaxed); }
private:
    std::atomic<uint64_t> m_incoming_vbytes{0};
};

#endif // BITCOIN_QML_COMPONENTS_WIDGETS_MEMPOOLACTIVITYSOURCE_H
