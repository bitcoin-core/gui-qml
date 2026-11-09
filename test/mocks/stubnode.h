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
    void initLogging() override { UnhandledCall("initLogging"); }
    void initParameterInteraction() override { UnhandledCall("initParameterInteraction"); }
    bilingual_str getWarnings() override
    {
        UnhandledCall("getWarnings");
        return {};
    }
    int getExitStatus() override
    {
        UnhandledCall("getExitStatus");
        return 0;
    }
    BCLog::CategoryMask getLogCategories() override
    {
        UnhandledCall("getLogCategories");
        return {};
    }
    bool baseInitialize() override
    {
        UnhandledCall("baseInitialize");
        return false;
    }
    bool appInitMain(interfaces::BlockAndHeaderTipInfo*) override
    {
        UnhandledCall("appInitMain");
        return false;
    }
    void appShutdown() override { UnhandledCall("appShutdown"); }
    void startShutdown() override { UnhandledCall("startShutdown"); }
    bool shutdownRequested() override
    {
        UnhandledCall("shutdownRequested");
        return false;
    }
    bool isSettingIgnored(const std::string&) override
    {
        UnhandledCall("isSettingIgnored");
        return false;
    }
    common::SettingsValue getPersistentSetting(const std::string&) override
    {
        UnhandledCall("getPersistentSetting");
        return {};
    }
    void updateRwSetting(const std::string&, const common::SettingsValue&) override { UnhandledCall("updateRwSetting"); }
    void forceSetting(const std::string&, const common::SettingsValue&) override { UnhandledCall("forceSetting"); }
    void resetSettings() override { UnhandledCall("resetSettings"); }
    void mapPort(bool) override { UnhandledCall("mapPort"); }
    std::optional<Proxy> getProxy(Network) override
    {
        UnhandledCall("getProxy");
        return std::nullopt;
    }
    size_t getNodeCount(ConnectionDirection) override
    {
        UnhandledCall("getNodeCount");
        return 0;
    }
    bool getNodesStats(NodesStats&) override
    {
        UnhandledCall("getNodesStats");
        return false;
    }
    bool getBanned(banmap_t&) override
    {
        UnhandledCall("getBanned");
        return false;
    }
    bool ban(const CNetAddr&, int64_t) override
    {
        UnhandledCall("ban");
        return false;
    }
    bool unban(const CSubNet&) override
    {
        UnhandledCall("unban");
        return false;
    }
    bool disconnectByAddress(const CNetAddr&) override
    {
        UnhandledCall("disconnectByAddress");
        return false;
    }
    bool disconnectById(NodeId) override
    {
        UnhandledCall("disconnectById");
        return false;
    }
    std::vector<std::unique_ptr<interfaces::ExternalSigner>> listExternalSigners() override
    {
        UnhandledCall("listExternalSigners");
        return {};
    }
    int64_t getTotalBytesRecv() override
    {
        UnhandledCall("getTotalBytesRecv");
        return 0;
    }
    int64_t getTotalBytesSent() override
    {
        UnhandledCall("getTotalBytesSent");
        return 0;
    }
    size_t getMempoolSize() override
    {
        UnhandledCall("getMempoolSize");
        return 0;
    }
    size_t getMempoolDynamicUsage() override
    {
        UnhandledCall("getMempoolDynamicUsage");
        return 0;
    }
    size_t getMempoolMaxUsage() override
    {
        UnhandledCall("getMempoolMaxUsage");
        return 0;
    }
    bool getHeaderTip(int&, int64_t&) override
    {
        UnhandledCall("getHeaderTip");
        return false;
    }
    int getNumBlocks() override
    {
        UnhandledCall("getNumBlocks");
        return 0;
    }
    std::map<CNetAddr, LocalServiceInfo> getNetLocalAddresses() override
    {
        UnhandledCall("getNetLocalAddresses");
        return {};
    }
    uint256 getBestBlockHash() override
    {
        UnhandledCall("getBestBlockHash");
        return {};
    }
    int64_t getLastBlockTime() override
    {
        UnhandledCall("getLastBlockTime");
        return 0;
    }
    double getVerificationProgress() override
    {
        UnhandledCall("getVerificationProgress");
        return 0.0;
    }
    bool isInitialBlockDownload() override
    {
        UnhandledCall("isInitialBlockDownload");
        return false;
    }
    bool isLoadingBlocks() override
    {
        UnhandledCall("isLoadingBlocks");
        return false;
    }
    void setNetworkActive(bool) override { UnhandledCall("setNetworkActive"); }
    bool getNetworkActive() override
    {
        UnhandledCall("getNetworkActive");
        return false;
    }
    CFeeRate getDustRelayFee() override
    {
        UnhandledCall("getDustRelayFee");
        return {};
    }
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override
    {
        UnhandledCall("executeRpc");
        return {};
    }
    std::vector<std::string> listRpcCommands() override
    {
        UnhandledCall("listRpcCommands");
        return {};
    }
    std::optional<Coin> getUnspentOutput(const COutPoint&) override
    {
        UnhandledCall("getUnspentOutput");
        return std::nullopt;
    }
    node::TransactionError broadcastTransaction(CTransactionRef, CAmount, CFeeRate, std::string&) override
    {
        UnhandledCall("broadcastTransaction");
        return {};
    }
    interfaces::WalletLoader& walletLoader() override
    {
        UnhandledCall("walletLoader");
        return m_wallet_loader;
    }
    std::unique_ptr<interfaces::Handler> handleInitMessage(InitMessageFn) override
    {
        UnhandledCall("handleInitMessage");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleMessageBox(MessageBoxFn) override
    {
        UnhandledCall("handleMessageBox");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleQuestion(QuestionFn) override
    {
        UnhandledCall("handleQuestion");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override
    {
        UnhandledCall("handleShowProgress");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleInitWallet(InitWalletFn) override
    {
        UnhandledCall("handleInitWallet");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyNumConnectionsChanged(NotifyNumConnectionsChangedFn) override
    {
        UnhandledCall("handleNotifyNumConnectionsChanged");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyNetworkActiveChanged(NotifyNetworkActiveChangedFn) override
    {
        UnhandledCall("handleNotifyNetworkActiveChanged");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyAlertChanged(NotifyAlertChangedFn) override
    {
        UnhandledCall("handleNotifyAlertChanged");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleBannedListChanged(BannedListChangedFn) override
    {
        UnhandledCall("handleBannedListChanged");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyBlockTip(NotifyBlockTipFn) override
    {
        UnhandledCall("handleNotifyBlockTip");
        return {};
    }
    std::unique_ptr<interfaces::Handler> handleNotifyHeaderTip(NotifyHeaderTipFn) override
    {
        UnhandledCall("handleNotifyHeaderTip");
        return {};
    }

protected:
    virtual void UnhandledCall(const char*) {}
    interfaces::WalletLoader& FallbackWalletLoader() { return m_wallet_loader; }

private:
    StubWalletLoader m_wallet_loader;
};

#endif // BITCOIN_QML_TEST_MOCKS_STUBNODE_H
