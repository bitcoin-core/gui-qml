// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#include <qml/models/feeestimatevalues.h>
#include <chainparams.h>
#include <interfaces/wallet.h>
#include <policy/feerate.h>
#include <algorithm>
namespace feeestimate {
Provenance ProvenanceFor(FeeReason reason)
{
    switch (reason) {
    case FeeReason::FEE_RATE_ESTIMATOR: return Provenance::Estimated;
    case FeeReason::MEMPOOL_MIN: case FeeReason::REQUIRED: return Provenance::Required;
    case FeeReason::USER_SPECIFIED: return Provenance::Manual;
    case FeeReason::FALLBACK: return Provenance::Fallback;
    }
    return Provenance::Unknown;
}
void ApplyAutomaticPolicy(interfaces::Wallet& wallet, wallet::CCoinControl& control)
{
    if (Params().GetChainType() != ChainType::REGTEST) return;
    // The app's regtest static policy is one sat/vB, raised to the actual
    // public wallet/relay requirement. Preview and preparation share this.
    control.m_confirm_target.reset();
    control.m_feerate = CFeeRate{std::max<CAmount>(1000, wallet.getRequiredFee(1000))};
}
}
