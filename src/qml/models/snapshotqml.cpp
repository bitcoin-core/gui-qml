// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include <qml/models/snapshotqml.h>

#include <common/args.h>
#include <index/coinstatsindex.h>
#include <kernel/coinstats.h>
#include <logging/timer.h>
#include <node/context.h>
#include <node/utxo_snapshot.h>
#include <clientversion.h>
#include <validation.h>
#include <sync.h>
#include <util/chaintype.h>
#include <util/fs.h>
#include <util/fs_helpers.h>

#include <QDebug>

using kernel::CCoinsStats;

SnapshotQml::SnapshotQml(interfaces::Node& node, QString path)
    : m_node(node), m_path(path) {}

bool SnapshotQml::processPath()
{
    const fs::path path = fs::u8path(m_path.toStdString());
    if (!fs::exists(path)) {
        return false;
    }

    FILE* snapshot_file{fsbridge::fopen(path, "rb")};
    AutoFile afile{snapshot_file};
    if (afile.IsNull()) {
        return false;
    }

    node::SnapshotMetadata metadata;
    try {
        afile >> metadata;
    } catch (const std::exception& e) {
        return false;
    }

    bool result = m_node.loadSnapshot(afile, metadata, false);
    if (!result) {
        return false;
    }
    return true;
}

static void InvalidateBlock(ChainstateManager& chainman, const uint256 block_hash)
{
    BlockValidationState state;
    CBlockIndex* pblockindex;
    {
        LOCK(chainman.GetMutex());
        pblockindex = chainman.m_blockman.LookupBlockIndex(block_hash);
        if (!pblockindex) {
            LogPrintf("Block not found");
            return;
        }
    }

    chainman.ActiveChainstate().InvalidateBlock(state, pblockindex);

    if (state.IsValid()) {
        chainman.ActiveChainstate().ActivateBestChain(state);
    }

    if (!state.IsValid()) {
        LogPrintf("Invalidate block failed: %s", state.ToString());
        return;
    }
}

static std::optional<kernel::CCoinsStats> GetUTXOStats(CCoinsView* view, node::BlockManager& blockman,
                                                    kernel::CoinStatsHashType hash_type,
                                                    const std::function<void()>& interruption_point = {},
                                                    const CBlockIndex* pindex = nullptr,
                                                    bool index_requested = true)
{
    // Use CoinStatsIndex if it is requested and available and a hash_type of Muhash or None was requested
    if ((hash_type == kernel::CoinStatsHashType::MUHASH || hash_type == kernel::CoinStatsHashType::NONE) && g_coin_stats_index && index_requested) {
        if (pindex) {
            return g_coin_stats_index->LookUpStats(*pindex);
        } else {
            CBlockIndex& block_index = *CHECK_NONFATAL(WITH_LOCK(::cs_main, return blockman.LookupBlockIndex(view->GetBestBlock())));
            return g_coin_stats_index->LookUpStats(block_index);
        }
    }

    // If the coinstats index isn't requested or is otherwise not usable, the
    // pindex should either be null or equal to the view's best block. This is
    // because without the coinstats index we can only get coinstats about the
    // best block.
    CHECK_NONFATAL(!pindex || pindex->GetBlockHash() == view->GetBestBlock());

    return kernel::ComputeUTXOStats(hash_type, view, blockman, interruption_point);
}

static std::tuple<std::unique_ptr<CCoinsViewCursor>, CCoinsStats, const CBlockIndex*>
PrepareUTXOSnapshot(
    Chainstate& chainstate,
    const std::function<void()>& interruption_point)
{
    std::unique_ptr<CCoinsViewCursor> pcursor;
    std::optional<CCoinsStats> maybe_stats;
    const CBlockIndex* tip;

    {
        // We need to lock cs_main to ensure that the coinsdb isn't written to
        // between (i) flushing coins cache to disk (coinsdb), (ii) getting stats
        // based upon the coinsdb, and (iii) constructing a cursor to the
        // coinsdb for use in WriteUTXOSnapshot.
        //
        // Cursors returned by leveldb iterate over snapshots, so the contents
        // of the pcursor will not be affected by simultaneous writes during
        // use below this block.
        //
        // See discussion here:
        //   https://github.com/bitcoin/bitcoin/pull/15606#discussion_r274479369
        //
        AssertLockHeld(::cs_main);

        chainstate.ForceFlushStateToDisk();

        maybe_stats = GetUTXOStats(&chainstate.CoinsDB(), chainstate.m_blockman, kernel::CoinStatsHashType::HASH_SERIALIZED, interruption_point);
        if (!maybe_stats) {
            LogPrintf("Unable to read UTXO set");
            return std::tuple<std::unique_ptr<CCoinsViewCursor>, CCoinsStats, const CBlockIndex*>{};
        }

        pcursor = chainstate.CoinsDB().Cursor();
        tip = CHECK_NONFATAL(chainstate.m_blockman.LookupBlockIndex(maybe_stats->hashBlock));
    }

    return {std::move(pcursor), *CHECK_NONFATAL(maybe_stats), tip};
}

static void WriteUTXOSnapshot(
    Chainstate& chainstate,
    CCoinsViewCursor* pcursor,
    CCoinsStats* maybe_stats,
    const CBlockIndex* tip,
    AutoFile& afile,
    const fs::path& path,
    const fs::path& temppath,
    const std::function<void()>& interruption_point,
    std::atomic<bool>* snapshot_cancel)
{
    LOG_TIME_SECONDS(strprintf("writing UTXO snapshot at height %s (%s) to file %s (via %s)",
        tip->nHeight, tip->GetBlockHash().ToString(),
        fs::PathToString(path), fs::PathToString(temppath)));

    node::SnapshotMetadata metadata{tip->GetBlockHash(), maybe_stats->coins_count, tip->nChainTx};

    afile << metadata;

    COutPoint key;
    Coin coin;
    unsigned int iter{0};

    while (pcursor->Valid()) {
        if (snapshot_cancel && *snapshot_cancel) {
            if (fs::exists(temppath)) {
                fs::remove(temppath);
            }
            return;
        }
        ++iter;
        if (pcursor->GetKey(key) && pcursor->GetValue(coin)) {
            afile << key;
            afile << coin;
        }

        pcursor->Next();
    }

    afile.fclose();
    return;
}

static void ReconsiderBlock(ChainstateManager& chainman, uint256 block_hash) {
    {
        LOCK(chainman.GetMutex());
        CBlockIndex* pblockindex = chainman.m_blockman.LookupBlockIndex(block_hash);
        if (!pblockindex) {
            LogPrintf("Block not found");
            return;
        }

        chainman.ActiveChainstate().ResetBlockFailureFlags(pblockindex);
    }

    BlockValidationState state;
    chainman.ActiveChainstate().ActivateBestChain(state);

    if (!state.IsValid()) {
        LogPrintf("Reconsider block failed: %s", state.ToString());
    }
}

/**
 * RAII class that disables the network in its constructor and enables it in its
 * destructor.
 */
class NetworkDisable
{
    CConnman& m_connman;
public:
    NetworkDisable(CConnman& connman) : m_connman(connman) {
        m_connman.SetNetworkActive(false);
        if (m_connman.GetNetworkActive()) {
            LogPrintf("Network activity could not be suspended.");
        }
    };
    ~NetworkDisable() {
        m_connman.SetNetworkActive(true);
    };
};

/**
 * RAII class that temporarily rolls back the local chain in it's constructor
 * and rolls it forward again in it's destructor.
 */
class TemporaryRollback
{
    ChainstateManager& m_chainman;
    const CBlockIndex& m_invalidate_index;

public:
    TemporaryRollback(ChainstateManager& chainman, const CBlockIndex& index)
        : m_chainman(chainman)
        , m_invalidate_index(index)
    {
        InvalidateBlock(m_chainman, m_invalidate_index.GetBlockHash());
    }

    ~TemporaryRollback() {
        ReconsiderBlock(m_chainman, m_invalidate_index.GetBlockHash());
    }
};

void SnapshotQml::setIsPruned(bool is_pruned) {
    if (m_is_pruned != is_pruned) {
        m_is_pruned = is_pruned;
        Q_EMIT isPrunedChanged();
    }
}

QString SnapshotQml::getSnapshotDirectory() {
    const fs::path path = gArgs.GetDataDirNet();
    return QString::fromStdString(fs::PathToString(path));
}

bool SnapshotQml::isSnapshotFileExists() {
    ChainstateManager& chainman = *Assert(m_node.context()->chainman);
    const MapAssumeutxo& assumeutxo_map = chainman.GetParams().Assumeutxo();
    int snapshot_height = 0;
    if (!assumeutxo_map.empty()) {
        snapshot_height = assumeutxo_map.rbegin()->first;
    }
    std::string network = chainman.GetParams().GetChainTypeString();
    std::string filename = strprintf("utxo_%s_%d.dat", network, snapshot_height);
    const fs::path path = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), fs::u8path(filename));
    return fs::exists(path);
}

void SnapshotQml::setIsRewinding(bool is_rewinding) {
    if (m_is_rewinding != is_rewinding) {
        m_is_rewinding = is_rewinding;
        Q_EMIT isRewindingChanged();
    }
}

void SnapshotQml::SnapshotGen() {
    if (!m_snapshot_cancel) return;
    ChainstateManager& chainman = *Assert(m_node.context()->chainman);
    const CChain& active_chain = chainman.ActiveChain();
    const CBlockIndex* tip{WITH_LOCK(::cs_main, return active_chain.Tip())};
    const MapAssumeutxo& assumeutxo_map = chainman.GetParams().Assumeutxo();
    int snapshot_height = 0;
    if (!assumeutxo_map.empty()) {
        snapshot_height = assumeutxo_map.rbegin()->first;
    }
    const CBlockIndex* target_index = active_chain[snapshot_height];
    const CBlockIndex* invalidate_index;
    std::string network = chainman.GetParams().GetChainTypeString();
    std::string filename = strprintf("utxo_%s_%d.dat", network, snapshot_height);
    const fs::path path = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), fs::u8path(filename));
    const fs::path temppath = fsbridge::AbsPathJoin(gArgs.GetDataDirNet(), fs::u8path(filename + ".incomplete"));
    FILE* file{fsbridge::fopen(temppath, "wb")};
    AutoFile afile{file};
    if (afile.IsNull()) {
        return;
    }
    CConnman& connman = *Assert(m_node.context()->connman);
    std::optional<NetworkDisable> disable_network;
    std::optional<TemporaryRollback> temporary_rollback;
    if (chainman.m_blockman.IsPruneMode()) {
        LOCK(chainman.GetMutex());
        const CBlockIndex* current_tip = active_chain.Tip();
        const CBlockIndex* first_block = chainman.m_blockman.GetFirstStoredBlock(*current_tip);
        if (first_block->nHeight > target_index->nHeight) {
            LogPrintf("first_block->nHeight: %d, target_index->nHeight: %d\n", first_block->nHeight, target_index->nHeight);
            LogPrintf("Could not roll back to requested height since necessary block data is already pruned. \n");
            return;
        }
    }

    if (connman.GetNetworkActive()) {
        disable_network.emplace(connman);
    }

    invalidate_index = WITH_LOCK(::cs_main, return active_chain.Next(target_index));
    setIsRewinding(true);
    temporary_rollback.emplace(chainman, *invalidate_index);


    Chainstate* chainstate;
    std::unique_ptr<CCoinsViewCursor> cursor;
    CCoinsStats maybe_stats;
    {
        LOCK(chainman.GetMutex());
        chainstate = &chainman.ActiveChainstate();
        if (m_snapshot_cancel && *m_snapshot_cancel) {
            LogPrintf("generateUTXOSnapshot canceled\n");
            return;
        }
        if (target_index != chainstate->m_chain.Tip()) {
            LogPrintf("generateUTXOSnapshot failed to roll back to requested height, reverting to tip.\n");
            return;
        } else {
            std::tie(cursor, maybe_stats, tip) = PrepareUTXOSnapshot(*chainstate, []() { });
        }
    }

    WriteUTXOSnapshot(*chainstate, cursor.get(), &maybe_stats, tip, afile, path, temppath, []() { }, m_snapshot_cancel);
    fs::rename(temppath, path);
    setIsRewinding(false);
    return;
}
