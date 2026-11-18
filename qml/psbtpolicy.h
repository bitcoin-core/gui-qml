// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_PSBTPOLICY_H
#define BITCOIN_QML_PSBTPOLICY_H

#include <consensus/amount.h>
#include <node/transaction.h>
#include <policy/feerate.h>
#include <primitives/transaction.h>
#include <QString>
#include <QStringList>
#include <functional>

namespace interfaces { class Node; class Chain; }
namespace psbtpolicy {
// Advisory observations are separate from signature verification and the
// authoritative submit outcome. No snapshot promises mempool acceptance.
enum class State { Unknown, NoKnownObstacle, Blocked };
enum class Admission { Unknown, Allowed, Rejected };
// The limits submission passes to broadcastTransaction. Readiness applies the
// same ones, so a review is not ready when the node would refuse its fee.
inline constexpr CFeeRate MAX_FEE_RATE{node::DEFAULT_MAX_RAW_TX_FEE_RATE};
inline CAmount MaxFee() { return MAX_FEE_RATE.GetFeePerK(); }
struct Facts {
    bool observed{false}, missing_inputs{false}, immature_inputs{false};
    bool final{true}, sufficient_fee{true}, chain_limits{true};
    bool within_fee_cap{true};
    Admission admission{Admission::Unknown};
    QString tip, chain_error;
    QString admission_error;
    // Unconfirmed transactions an allowed submission would evict.
    QStringList replaces;
    // The eviction lookup did not complete, so replaces may be incomplete.
    bool replaces_unknown{false};
};
struct Decision { State state{State::Unknown}; QString explanation; };
using Read = std::function<Facts(CTransactionRef, CAmount)>;
// Composition retains both interfaces through the original scheduler drain.
// The returned port performs read-only public interface calls on Native.
// Raw PSBT broadcasts enforce broadcastTransaction's fee cap; wallet-built
// transactions use the wallet's own configured fee limit instead.
Read ReadNode(interfaces::Node* node, interfaces::Chain* chain, bool enforce_raw_fee_cap = true);
Decision Evaluate(const Facts& facts);
} // namespace psbtpolicy

#endif // BITCOIN_QML_PSBTPOLICY_H
