// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/mempoolactivitysource.h>

#include <policy/policy.h>

void MempoolActivitySource::transactionAddedToMempool(const CTransactionRef& tx)
{
    m_incoming_vbytes.fetch_add(GetVirtualTransactionSize(*tx), std::memory_order_relaxed);
}
