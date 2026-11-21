// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
#ifndef BITCOIN_QML_TEST_MOCKS_FEEPREVIEWCHAIN_H
#define BITCOIN_QML_TEST_MOCKS_FEEPREVIEWCHAIN_H
#include <interfaces/chain.h>
#include <interfaces/handler.h>
#include <policy/feerate.h>
#include <univalue.h>
#include <stdexcept>
#include <string>
#include <vector>
/** Deterministic chain boundary for concrete CWallet fee tests. Unspecified
 * operations fail rather than inventing consensus, mempool or RPC behavior. */
class FeePreviewChain : public interfaces::Chain
{
public:
    mutable std::vector<std::string> unexpected;
    std::optional<int> getHeight() override { unexpected.emplace_back("getHeight"); throw std::logic_error("Unexpected chain call: getHeight"); }
    uint256 getBlockHash(int height) override { unexpected.emplace_back("getBlockHash"); throw std::logic_error("Unexpected chain call: getBlockHash"); }
    bool haveBlockOnDisk(int height) override { unexpected.emplace_back("haveBlockOnDisk"); throw std::logic_error("Unexpected chain call: haveBlockOnDisk"); }
    std::optional<int> findLocatorFork(const CBlockLocator& locator) override { unexpected.emplace_back("findLocatorFork"); throw std::logic_error("Unexpected chain call: findLocatorFork"); }
    bool hasBlockFilterIndex(BlockFilterType filter_type) override { unexpected.emplace_back("hasBlockFilterIndex"); throw std::logic_error("Unexpected chain call: hasBlockFilterIndex"); }
    std::optional<bool> blockFilterMatchesAny(BlockFilterType filter_type, const uint256& block_hash, const GCSFilter::ElementSet& filter_set) override { unexpected.emplace_back("blockFilterMatchesAny"); throw std::logic_error("Unexpected chain call: blockFilterMatchesAny"); }
    bool findBlock(const uint256&, const interfaces::FoundBlock& block = {}) override
    {
        if (block.m_time) *block.m_time = 1;
        if (block.m_max_time) *block.m_max_time = 1;
        if (block.m_hash || block.m_height || block.m_mtp_time || block.m_in_active_chain || block.m_locator || block.m_next_block || block.m_data) {
            unexpected.emplace_back("findBlock fields"); throw std::logic_error("Unexpected block metadata request");
        }
        block.found = true;
        return true;
    }
    bool findFirstBlockWithTimeAndHeight(int64_t min_time, int min_height, const interfaces::FoundBlock& block={}) override { unexpected.emplace_back("findFirstBlockWithTimeAndHeight"); throw std::logic_error("Unexpected chain call: findFirstBlockWithTimeAndHeight"); }
    bool findAncestorByHeight(const uint256& block_hash, int ancestor_height, const interfaces::FoundBlock& ancestor_out={}) override { unexpected.emplace_back("findAncestorByHeight"); throw std::logic_error("Unexpected chain call: findAncestorByHeight"); }
    bool findAncestorByHash(const uint256& block_hash,
        const uint256& ancestor_hash,
        const interfaces::FoundBlock& ancestor_out={}) override { unexpected.emplace_back("findAncestorByHash"); throw std::logic_error("Unexpected chain call: findAncestorByHash"); }
    bool findCommonAncestor(const uint256& block_hash1,
        const uint256& block_hash2,
        const interfaces::FoundBlock& ancestor_out={},
        const interfaces::FoundBlock& block1_out={},
        const interfaces::FoundBlock& block2_out={}) override { unexpected.emplace_back("findCommonAncestor"); throw std::logic_error("Unexpected chain call: findCommonAncestor"); }
    void findCoins(std::map<COutPoint, Coin>& coins) override { unexpected.emplace_back("findCoins"); throw std::logic_error("Unexpected chain call: findCoins"); }
    double guessVerificationProgress(const uint256& block_hash) override { unexpected.emplace_back("guessVerificationProgress"); throw std::logic_error("Unexpected chain call: guessVerificationProgress"); }
    bool hasBlocks(const uint256& block_hash, int min_height = 0, std::optional<int> max_height = {}) override { unexpected.emplace_back("hasBlocks"); throw std::logic_error("Unexpected chain call: hasBlocks"); }
    RBFTransactionState isRBFOptIn(const CTransaction& tx) override { unexpected.emplace_back("isRBFOptIn"); throw std::logic_error("Unexpected chain call: isRBFOptIn"); }
    bool isInMempool(const Txid& txid) override { return true; }
    bool hasDescendantsInMempool(const Txid& txid) override { unexpected.emplace_back("hasDescendantsInMempool"); throw std::logic_error("Unexpected chain call: hasDescendantsInMempool"); }
    bool broadcastTransaction(const CTransactionRef& tx,
                                      const CAmount& max_tx_fee,
                                      const CFeeRate& max_tx_fee_rate,
                                      node::TxBroadcast broadcast_method,
                                      std::string& err_string) override { unexpected.emplace_back("broadcastTransaction"); throw std::logic_error("Unexpected chain call: broadcastTransaction"); }
    void getTransactionAncestry(const Txid& txid, size_t& ancestors, size_t& cluster_count, size_t* ancestorsize = nullptr, CAmount* ancestorfees = nullptr) override { unexpected.emplace_back("getTransactionAncestry"); throw std::logic_error("Unexpected chain call: getTransactionAncestry"); }
    std::map<COutPoint, CAmount> calculateIndividualBumpFees(const std::vector<COutPoint>& outpoints, const CFeeRate&) override
    {
        // This fixture has no mempool ancestors. Match the public Chain
        // contract (and ChainImpl's no-mempool branch): every requested
        // outpoint must be present, with zero ancestor fee to contribute.
        std::map<COutPoint, CAmount> fees;
        for (const auto& outpoint : outpoints) fees.emplace(outpoint, CAmount{0});
        return fees;
    }
    std::optional<CAmount> calculateCombinedBumpFee(const std::vector<COutPoint>& outpoints, const CFeeRate& target_feerate) override { return CAmount{0}; }
    void getPackageLimits(unsigned int& limit_ancestor_count, unsigned int& limit_descendant_count) override { unexpected.emplace_back("getPackageLimits"); throw std::logic_error("Unexpected chain call: getPackageLimits"); }
    util::Result<void> checkChainLimits(const CTransactionRef&) override
    {
        // Match ChainImpl without a mempool: no ancestor/cluster policy can
        // reject this fixture's transaction after native fee construction.
        return {};
    }
    util::Expected<FeeRateEstimation, FeeRateEstimationError> getFeeRateEstimate(int num_blocks, bool conservative) const override { return EstimationError(FeeRateEstimatorType::AUTO, 0, {}); }
    unsigned int maximumFeeEstimationTargetBlocks() const override
    {
        // GetDiscardRate queries this even when estimation is unavailable.
        // Zero is ChainImpl's exact answer without a fee estimator manager.
        return 0;
    }
    CFeeRate mempoolMinFee() override { return CFeeRate{0}; }
    CFeeRate relayMinFee() override { return CFeeRate{1000}; }
    CFeeRate relayIncrementalFee() override { return CFeeRate{1000}; }
    CFeeRate relayDustFee() override { return CFeeRate{3000}; }
    bool havePruned() override { unexpected.emplace_back("havePruned"); throw std::logic_error("Unexpected chain call: havePruned"); }
    std::optional<int> getPruneHeight() override { unexpected.emplace_back("getPruneHeight"); throw std::logic_error("Unexpected chain call: getPruneHeight"); }
    bool isReadyToBroadcast() override { unexpected.emplace_back("isReadyToBroadcast"); throw std::logic_error("Unexpected chain call: isReadyToBroadcast"); }
    bool isInitialBlockDownload() override { return true; }
    bool shutdownRequested() override { unexpected.emplace_back("shutdownRequested"); throw std::logic_error("Unexpected chain call: shutdownRequested"); }
    void initMessage(const std::string& message) override { unexpected.emplace_back("initMessage"); throw std::logic_error("Unexpected chain call: initMessage"); }
    void initWarning(const bilingual_str& message) override { unexpected.emplace_back("initWarning"); throw std::logic_error("Unexpected chain call: initWarning"); }
    void initError(const bilingual_str& message) override { unexpected.emplace_back("initError"); throw std::logic_error("Unexpected chain call: initError"); }
    void showProgress(const std::string& title, int progress, bool resume_possible) override { unexpected.emplace_back("showProgress"); throw std::logic_error("Unexpected chain call: showProgress"); }
    std::unique_ptr<interfaces::Handler> handleNotifications(std::shared_ptr<Notifications> notifications) override { unexpected.emplace_back("handleNotifications"); throw std::logic_error("Unexpected chain call: handleNotifications"); }
    void waitForNotificationsIfTipChanged(const uint256& old_tip) override { unexpected.emplace_back("waitForNotificationsIfTipChanged"); throw std::logic_error("Unexpected chain call: waitForNotificationsIfTipChanged"); }
    void waitForNotifications() override { unexpected.emplace_back("waitForNotifications"); throw std::logic_error("Unexpected chain call: waitForNotifications"); }
    std::unique_ptr<interfaces::Handler> handleRpc(const CRPCCommand& command) override { unexpected.emplace_back("handleRpc"); throw std::logic_error("Unexpected chain call: handleRpc"); }
    bool rpcEnableDeprecated(const std::string& method) override { unexpected.emplace_back("rpcEnableDeprecated"); throw std::logic_error("Unexpected chain call: rpcEnableDeprecated"); }
    common::SettingsValue getSetting(const std::string& arg) override { unexpected.emplace_back("getSetting"); throw std::logic_error("Unexpected chain call: getSetting"); }
    std::vector<common::SettingsValue> getSettingsList(const std::string& arg) override { unexpected.emplace_back("getSettingsList"); throw std::logic_error("Unexpected chain call: getSettingsList"); }
    common::SettingsValue getRwSetting(const std::string& name) override { unexpected.emplace_back("getRwSetting"); throw std::logic_error("Unexpected chain call: getRwSetting"); }
    bool updateRwSetting(const std::string& name, const interfaces::SettingsUpdate& update_function) override { unexpected.emplace_back("updateRwSetting"); throw std::logic_error("Unexpected chain call: updateRwSetting"); }
    bool overwriteRwSetting(const std::string& name, common::SettingsValue value, interfaces::SettingsAction action = interfaces::SettingsAction::WRITE) override { unexpected.emplace_back("overwriteRwSetting"); throw std::logic_error("Unexpected chain call: overwriteRwSetting"); }
    bool deleteRwSettings(const std::string& name, interfaces::SettingsAction action = interfaces::SettingsAction::WRITE) override { unexpected.emplace_back("deleteRwSettings"); throw std::logic_error("Unexpected chain call: deleteRwSettings"); }
    void requestMempoolTransactions(Notifications& notifications) override { unexpected.emplace_back("requestMempoolTransactions"); throw std::logic_error("Unexpected chain call: requestMempoolTransactions"); }
    bool hasAssumedValidChain() override { return false; }
};
#endif // BITCOIN_QML_TEST_MOCKS_FEEPREVIEWCHAIN_H
