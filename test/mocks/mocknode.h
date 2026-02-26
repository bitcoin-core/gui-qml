// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MOCKNODE_H
#define BITCOIN_QML_TEST_MOCKS_MOCKNODE_H

// Bitcoin's util/check.h defines Assert(val), while gMock declares internal
// Assert(bool, file, line[, msg]) helpers. Some tests include Bitcoin headers
// before this mock header, so temporarily hide the macro while parsing gMock
// and restore it afterward.
#ifdef Assert
#pragma push_macro("Assert")
#undef Assert
#define BITCOIN_QML_RESTORE_ASSERT_MACRO
#endif

#include <gmock/gmock.h>

#ifdef BITCOIN_QML_RESTORE_ASSERT_MACRO
#pragma pop_macro("Assert")
#undef BITCOIN_QML_RESTORE_ASSERT_MACRO
#endif

#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <net_processing.h>

#include <coins.h>
#include <node/types.h>
#include <policy/feerate.h>
#include <univalue.h>

class MockNode : public interfaces::Node
{
public:
    MOCK_METHOD(void, initLogging, (), (override));
    MOCK_METHOD(void, initParameterInteraction, (), (override));
    MOCK_METHOD(bilingual_str, getWarnings, (), (override));
    MOCK_METHOD(int, getExitStatus, (), (override));
    MOCK_METHOD(BCLog::CategoryMask, getLogCategories, (), (override));
    MOCK_METHOD(bool, baseInitialize, (), (override));
    MOCK_METHOD(bool, appInitMain, (interfaces::BlockAndHeaderTipInfo*), (override));
    MOCK_METHOD(void, appShutdown, (), (override));
    MOCK_METHOD(void, startShutdown, (), (override));
    MOCK_METHOD(bool, shutdownRequested, (), (override));
    MOCK_METHOD(bool, isSettingIgnored, (const std::string&), (override));
    MOCK_METHOD(common::SettingsValue, getPersistentSetting, (const std::string&), (override));
    MOCK_METHOD(void, updateRwSetting, (const std::string&, const common::SettingsValue&), (override));
    MOCK_METHOD(void, forceSetting, (const std::string&, const common::SettingsValue&), (override));
    MOCK_METHOD(void, resetSettings, (), (override));
    MOCK_METHOD(void, mapPort, (bool), (override));
    MOCK_METHOD(std::optional<Proxy>, getProxy, (Network), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Snapshot>), snapshot, (const fs::path&), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleSnapshotLoadProgress, (SnapshotLoadProgressFn), (override));
    MOCK_METHOD(size_t, getNodeCount, (ConnectionDirection), (override));
    MOCK_METHOD(bool, getNodesStats, (NodesStats&), (override));
    MOCK_METHOD(bool, getBanned, (banmap_t&), (override));
    MOCK_METHOD(bool, ban, (const CNetAddr&, int64_t), (override));
    MOCK_METHOD(bool, unban, (const CSubNet&), (override));
    MOCK_METHOD(bool, disconnectByAddress, (const CNetAddr&), (override));
    MOCK_METHOD(bool, disconnectById, (NodeId), (override));
    MOCK_METHOD((std::vector<std::unique_ptr<interfaces::ExternalSigner>>), listExternalSigners, (), (override));
    MOCK_METHOD(int64_t, getTotalBytesRecv, (), (override));
    MOCK_METHOD(int64_t, getTotalBytesSent, (), (override));
    MOCK_METHOD(size_t, getMempoolSize, (), (override));
    MOCK_METHOD(size_t, getMempoolDynamicUsage, (), (override));
    MOCK_METHOD(size_t, getMempoolMaxUsage, (), (override));
    MOCK_METHOD(bool, getHeaderTip, (int&, int64_t&), (override));
    MOCK_METHOD(int, getNumBlocks, (), (override));
    MOCK_METHOD((std::map<CNetAddr, LocalServiceInfo>), getNetLocalAddresses, (), (override));
    MOCK_METHOD(uint256, getBestBlockHash, (), (override));
    MOCK_METHOD(int64_t, getLastBlockTime, (), (override));
    MOCK_METHOD(double, getVerificationProgress, (), (override));
    MOCK_METHOD(bool, isInitialBlockDownload, (), (override));
    MOCK_METHOD(bool, isLoadingBlocks, (), (override));
    MOCK_METHOD(void, setNetworkActive, (bool), (override));
    MOCK_METHOD(bool, getNetworkActive, (), (override));
    MOCK_METHOD(CFeeRate, getDustRelayFee, (), (override));
    MOCK_METHOD(UniValue, executeRpc, (const std::string&, const UniValue&, const std::string&), (override));
    MOCK_METHOD((std::vector<std::string>), listRpcCommands, (), (override));
    MOCK_METHOD((std::optional<Coin>), getUnspentOutput, (const COutPoint&), (override));
    MOCK_METHOD(node::TransactionError, broadcastTransaction, (CTransactionRef, CAmount, std::string&), (override));
    MOCK_METHOD(interfaces::WalletLoader&, walletLoader, (), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleInitMessage, (InitMessageFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleMessageBox, (MessageBoxFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleQuestion, (QuestionFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleShowProgress, (ShowProgressFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleInitWallet, (InitWalletFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleNotifyNumConnectionsChanged, (NotifyNumConnectionsChangedFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleNotifyNetworkActiveChanged, (NotifyNetworkActiveChangedFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleNotifyAlertChanged, (NotifyAlertChangedFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleBannedListChanged, (BannedListChangedFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleNotifyBlockTip, (NotifyBlockTipFn), (override));
    MOCK_METHOD((std::unique_ptr<interfaces::Handler>), handleNotifyHeaderTip, (NotifyHeaderTipFn), (override));
    MOCK_METHOD(node::NodeContext*, context, (), (override));
    MOCK_METHOD(void, setContext, (node::NodeContext*), (override));
};

#endif // BITCOIN_QML_TEST_MOCKS_MOCKNODE_H
