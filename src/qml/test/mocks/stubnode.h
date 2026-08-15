// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_STUBNODE_H
#define BITCOIN_QML_TEST_MOCKS_STUBNODE_H

#include <coins.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <net_processing.h>
#include <node/types.h>
#include <policy/feerate.h>
#include <scheduler.h>
#include <univalue.h>
#include <util/result.h>
#include <util/translation.h>

#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

class StubWalletLoader : public interfaces::WalletLoader
{
public:
    void registerRpcs() override {}
    bool verify() override { return false; }
    bool load() override { return false; }
    void start(CScheduler&) override {}
    void stop() override {}
    void setMockTime(int64_t) override {}
    void schedulerMockForward(std::chrono::seconds) override {}

    util::Result<std::unique_ptr<interfaces::Wallet>> createWallet(
        const std::string&, const SecureString&, uint64_t, std::vector<bilingual_str>&) override
    {
        return util::Error{};
    }

    util::Result<std::unique_ptr<interfaces::Wallet>> loadWallet(
        const std::string&, std::vector<bilingual_str>&) override
    {
        return util::Error{};
    }

    std::string getWalletDir() override { return {}; }

    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(
        const fs::path&, const std::string&, std::vector<bilingual_str>&, bool) override
    {
        return util::Error{};
    }

    util::Result<interfaces::WalletMigrationResult> migrateWallet(
        const std::string&, const SecureString&, bool) override
    {
        return util::Error{};
    }

    bool isEncrypted(const std::string&) override { return false; }
    std::vector<std::pair<std::string, std::string>> listWalletDir() override { return {}; }
    std::vector<std::unique_ptr<interfaces::Wallet>> getWallets() override { return {}; }
    std::unique_ptr<interfaces::Handler> handleLoadWallet(LoadWalletFn) override { return {}; }
};

class StubNode : public interfaces::Node
{
public:
    void initLogging() override { unhandledCall("initLogging"); }
    void initParameterInteraction() override { unhandledCall("initParameterInteraction"); }
    bilingual_str getWarnings() override
    {
        unhandledCall("getWarnings");
        return {};
    }
    int getExitStatus() override
    {
        unhandledCall("getExitStatus");
        return 0;
    }
    BCLog::CategoryMask getLogCategories() override
    {
        unhandledCall("getLogCategories");
        return {};
    }
    bool baseInitialize() override
    {
        unhandledCall("baseInitialize");
        return false;
    }
    bool appInitMain(interfaces::BlockAndHeaderTipInfo*) override
    {
        unhandledCall("appInitMain");
        return false;
    }
    void appShutdown() override { unhandledCall("appShutdown"); }
    void startShutdown() override { unhandledCall("startShutdown"); }
    bool shutdownRequested() override
    {
        unhandledCall("shutdownRequested");
        return false;
    }
    bool isSettingIgnored(const std::string&) override
    {
        unhandledCall("isSettingIgnored");
        return false;
    }
    common::SettingsValue getPersistentSetting(const std::string&) override
    {
        unhandledCall("getPersistentSetting");
        return {};
    }
    void updateRwSetting(const std::string&, const common::SettingsValue&) override { unhandledCall("updateRwSetting"); }
    void forceSetting(const std::string&, const common::SettingsValue&) override { unhandledCall("forceSetting"); }
    void resetSettings() override { unhandledCall("resetSettings"); }
    void mapPort(bool) override { unhandledCall("mapPort"); }
    std::optional<Proxy> getProxy(Network) override
    {
        unhandledCall("getProxy");
        return std::nullopt;
    }
    size_t getNodeCount(ConnectionDirection) override
    {
        unhandledCall("getNodeCount");
        return 0;
    }
    bool getNodesStats(NodesStats&) override
    {
        unhandledCall("getNodesStats");
        return false;
    }
    bool getBanned(banmap_t&) override
    {
        unhandledCall("getBanned");
        return false;
    }
    bool ban(const CNetAddr&, int64_t) override
    {
        unhandledCall("ban");
        return false;
    }
    bool unban(const CSubNet&) override
    {
        unhandledCall("unban");
        return false;
    }
    bool disconnectByAddress(const CNetAddr&) override
    {
        unhandledCall("disconnectByAddress");
        return false;
    }
    bool disconnectById(NodeId) override
    {
        unhandledCall("disconnectById");
        return false;
    }
    std::vector<std::unique_ptr<interfaces::ExternalSigner>> listExternalSigners() override
    {
        unhandledCall("listExternalSigners");
        return {};
    }
    int64_t getTotalBytesRecv() override
    {
        unhandledCall("getTotalBytesRecv");
        return 0;
    }
    int64_t getTotalBytesSent() override
    {
        unhandledCall("getTotalBytesSent");
        return 0;
    }
    size_t getMempoolSize() override
    {
        unhandledCall("getMempoolSize");
        return 0;
    }
    size_t getMempoolDynamicUsage() override
    {
        unhandledCall("getMempoolDynamicUsage");
        return 0;
    }
    size_t getMempoolMaxUsage() override
    {
        unhandledCall("getMempoolMaxUsage");
        return 0;
    }
    bool getHeaderTip(int&, int64_t&) override
    {
        unhandledCall("getHeaderTip");
        return false;
    }
    int getNumBlocks() override
    {
        unhandledCall("getNumBlocks");
        return 0;
    }
    std::map<CNetAddr, LocalServiceInfo> getNetLocalAddresses() override
    {
        unhandledCall("getNetLocalAddresses");
        return {};
    }
    uint256 getBestBlockHash() override
    {
        unhandledCall("getBestBlockHash");
        return {};
    }
    int64_t getLastBlockTime() override
    {
        unhandledCall("getLastBlockTime");
        return 0;
    }
    double getVerificationProgress() override
    {
        unhandledCall("getVerificationProgress");
        return 0.0;
    }
    bool isInitialBlockDownload() override
    {
        unhandledCall("isInitialBlockDownload");
        return false;
    }
    bool isLoadingBlocks() override
    {
        unhandledCall("isLoadingBlocks");
        return false;
    }
    void setNetworkActive(bool) override { unhandledCall("setNetworkActive"); }
    bool getNetworkActive() override
    {
        unhandledCall("getNetworkActive");
        return false;
    }
    CFeeRate getDustRelayFee() override
    {
        unhandledCall("getDustRelayFee");
        return {};
    }
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override
    {
        unhandledCall("executeRpc");
        return {};
    }
    std::vector<std::string> listRpcCommands() override
    {
        unhandledCall("listRpcCommands");
        return {};
    }
    std::optional<Coin> getUnspentOutput(const COutPoint&) override
    {
        unhandledCall("getUnspentOutput");
        return std::nullopt;
    }
    node::TransactionError broadcastTransaction(CTransactionRef, CAmount, std::string&) override
    {
        unhandledCall("broadcastTransaction");
        return {};
    }
    interfaces::WalletLoader& walletLoader() override
    {
        unhandledCall("walletLoader");
        return m_wallet_loader;
    }
    std::unique_ptr<interfaces::Handler> handleInitMessage(InitMessageFn) override
    {
        unhandledCall("handleInitMessage");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleMessageBox(MessageBoxFn) override
    {
        unhandledCall("handleMessageBox");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleQuestion(QuestionFn) override
    {
        unhandledCall("handleQuestion");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override
    {
        unhandledCall("handleShowProgress");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleInitWallet(InitWalletFn) override
    {
        unhandledCall("handleInitWallet");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyNumConnectionsChanged(NotifyNumConnectionsChangedFn) override
    {
        unhandledCall("handleNotifyNumConnectionsChanged");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyNetworkActiveChanged(NotifyNetworkActiveChangedFn) override
    {
        unhandledCall("handleNotifyNetworkActiveChanged");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyAlertChanged(NotifyAlertChangedFn) override
    {
        unhandledCall("handleNotifyAlertChanged");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleBannedListChanged(BannedListChangedFn) override
    {
        unhandledCall("handleBannedListChanged");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyBlockTip(NotifyBlockTipFn) override
    {
        unhandledCall("handleNotifyBlockTip");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyHeaderTip(NotifyHeaderTipFn) override
    {
        unhandledCall("handleNotifyHeaderTip");
        return {};
    }

protected:
    virtual void unhandledCall(const char*) {}

private:
    StubWalletLoader m_wallet_loader;
};

#endif // BITCOIN_QML_TEST_MOCKS_STUBNODE_H
