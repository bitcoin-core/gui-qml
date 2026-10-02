// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/components/widgets/difficultyperiodsource.h>

#include <arith_uint256.h>
#include <chainparams.h>
#include <interfaces/chain.h>
#include <interfaces/node.h>
#include <univalue.h>

namespace {
UniValue Header(interfaces::Node& node, const uint256& hash)
{
    UniValue args{UniValue::VARR};
    args.push_back(hash.GetHex());
    return node.executeRpc("getblockheader", args, "");
}

double Target(const UniValue& header)
{
    return arith_uint256{}.SetCompact(std::stoul(header["bits"].get_str(), nullptr, 16)).getdouble();
}
}

DifficultyPeriodModel::Snapshot ReadDifficultyPeriod(interfaces::Node& node, interfaces::Chain& chain)
{
    if (node.isInitialBlockDownload()) return {};
    const auto& params = Params().GetConsensus();
    const auto tip_hash = node.getBestBlockHash();
    const auto tip = Header(node, tip_hash);
    DifficultyPeriodModel::Snapshot snapshot;
    snapshot.height = tip["height"].getInt<int>();
    snapshot.interval = params.DifficultyAdjustmentInterval();
    snapshot.spacing = params.nPowTargetSpacing;
    snapshot.tip_time = tip["time"].getInt<int64_t>();
    snapshot.no_retargeting = params.fPowNoRetargeting;
    snapshot.min_difficulty_blocks = params.fPowAllowMinDifficultyBlocks;
    snapshot.pow_limit = UintToArith256(params.powLimit).getdouble();
    const int start = snapshot.height - snapshot.height % snapshot.interval;
    uint256 start_hash;
    int64_t start_time;
    // Pin ancestor reads to one tip. Header RPCs work even on a pruned node.
    if (!chain.findAncestorByHeight(tip_hash, start, interfaces::FoundBlock().hash(start_hash).time(start_time))) return {};
    snapshot.start_time = start_time;
    snapshot.current_target = Target(Header(node, start_hash));
    if (start > 0) {
        uint256 previous_hash;
        if (!chain.findAncestorByHeight(tip_hash, start - 1, interfaces::FoundBlock().hash(previous_hash))) return {};
        snapshot.previous_target = Target(Header(node, previous_hash));
    }
    bool in_active_chain{false};
    if (!chain.findBlock(tip_hash, interfaces::FoundBlock().inActiveChain(in_active_chain)) || !in_active_chain) return {};
    return snapshot;
}
