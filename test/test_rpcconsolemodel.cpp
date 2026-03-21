// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.
//
// Tests for RpcConsoleModel history navigation logic.
//
// RpcConsoleModel::browseHistory(), resetHistoryNavigation(), and the history
// append logic in submitCommand() do not use the node interface at all.  The
// worker-thread dispatch in submitCommand() is a QueuedConnection invoke, so
// with no Qt event loop running the worker's execute() slot is never called
// and executeRpc() is never reached.  A minimal MockNode that compiles is
// therefore sufficient.

#include <QtTest/QtTest>

#include <qml/models/rpcconsolemodel.h>

#include <coins.h>
#include <interfaces/handler.h>
#include <interfaces/node.h>
#include <interfaces/wallet.h>
#include <univalue.h>
#include <util/result.h>
#include <util/translation.h>

#include <chrono>
#include <memory>
#include <string>
#include <vector>

// ---------------------------------------------------------------------------
// Minimal mock for interfaces::WalletLoader (required by MockNode::walletLoader)
// ---------------------------------------------------------------------------
class MockWalletLoader : public interfaces::WalletLoader
{
public:
    // ChainClient
    void registerRpcs() override {}
    bool verify() override { return false; }
    bool load() override { return false; }
    void start(CScheduler&) override {}
    void stop() override {}
    void setMockTime(int64_t) override {}
    void schedulerMockForward(std::chrono::seconds) override {}

    // WalletLoader
    util::Result<std::unique_ptr<interfaces::Wallet>> createWallet(
        const std::string&, const SecureString&, uint64_t,
        std::vector<bilingual_str>&) override
    { return util::Error{}; }

    util::Result<std::unique_ptr<interfaces::Wallet>> loadWallet(
        const std::string&, std::vector<bilingual_str>&) override
    { return util::Error{}; }

    std::string getWalletDir() override { return {}; }

    util::Result<std::unique_ptr<interfaces::Wallet>> restoreWallet(
        const fs::path&, const std::string&, std::vector<bilingual_str>&) override
    { return util::Error{}; }

    util::Result<interfaces::WalletMigrationResult> migrateWallet(
        const std::string&, const SecureString&) override
    { return util::Error{}; }

    bool isEncrypted(const std::string&) override { return false; }

    std::vector<std::pair<std::string, std::string>> listWalletDir() override { return {}; }

    std::vector<std::unique_ptr<interfaces::Wallet>> getWallets() override { return {}; }

    std::unique_ptr<interfaces::Handler> handleLoadWallet(LoadWalletFn) override { return {}; }
};

// ---------------------------------------------------------------------------
// Minimal mock for interfaces::Node
// ---------------------------------------------------------------------------
class MockNode : public interfaces::Node
{
public:
    // Trivial stubs — none are called during history tests.
    void initLogging() override {}
    void initParameterInteraction() override {}
    bilingual_str getWarnings() override { return {}; }
    int getExitStatus() override { return 0; }
    BCLog::CategoryMask getLogCategories() override { return {}; }
    bool baseInitialize() override { return false; }
    bool appInitMain(interfaces::BlockAndHeaderTipInfo*) override { return false; }
    void appShutdown() override {}
    void startShutdown() override {}
    bool shutdownRequested() override { return false; }
    bool isSettingIgnored(const std::string&) override { return false; }
    common::SettingsValue getPersistentSetting(const std::string&) override { return {}; }
    void updateRwSetting(const std::string&, const common::SettingsValue&) override {}
    void forceSetting(const std::string&, const common::SettingsValue&) override {}
    void resetSettings() override {}
    void mapPort(bool) override {}
    bool getProxy(Network, Proxy&) override { return false; }
    size_t getNodeCount(ConnectionDirection) override { return 0; }
    bool getNodesStats(NodesStats&) override { return false; }
    bool getBanned(banmap_t&) override { return false; }
    bool ban(const CNetAddr&, int64_t) override { return false; }
    bool unban(const CSubNet&) override { return false; }
    bool disconnectByAddress(const CNetAddr&) override { return false; }
    bool disconnectById(NodeId) override { return false; }
    std::vector<std::unique_ptr<interfaces::ExternalSigner>> listExternalSigners() override { return {}; }
    int64_t getTotalBytesRecv() override { return 0; }
    int64_t getTotalBytesSent() override { return 0; }
    size_t getMempoolSize() override { return 0; }
    size_t getMempoolDynamicUsage() override { return 0; }
    size_t getMempoolMaxUsage() override { return 0; }
    bool getHeaderTip(int&, int64_t&) override { return false; }
    int getNumBlocks() override { return 0; }
    std::map<CNetAddr, LocalServiceInfo> getNetLocalAddresses() override { return {}; }
    uint256 getBestBlockHash() override { return {}; }
    int64_t getLastBlockTime() override { return 0; }
    double getVerificationProgress() override { return 0.0; }
    bool isInitialBlockDownload() override { return false; }
    bool isLoadingBlocks() override { return false; }
    void setNetworkActive(bool) override {}
    bool getNetworkActive() override { return false; }
    CFeeRate getDustRelayFee() override { return {}; }
    UniValue executeRpc(const std::string&, const UniValue&, const std::string&) override { return {}; }
    std::vector<std::string> listRpcCommands() override { return {}; }
    std::optional<Coin> getUnspentOutput(const COutPoint&) override { return std::nullopt; }
    node::TransactionError broadcastTransaction(CTransactionRef, CAmount, std::string&) override
    { return {}; }
    interfaces::WalletLoader& walletLoader() override { return m_wallet_loader; }

    std::unique_ptr<interfaces::Handler> handleInitMessage(InitMessageFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleMessageBox(MessageBoxFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleQuestion(QuestionFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleShowProgress(ShowProgressFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleInitWallet(InitWalletFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleNotifyNumConnectionsChanged(NotifyNumConnectionsChangedFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleNotifyNetworkActiveChanged(NotifyNetworkActiveChangedFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleNotifyAlertChanged(NotifyAlertChangedFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleBannedListChanged(BannedListChangedFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleNotifyBlockTip(NotifyBlockTipFn) override { return {}; }
    std::unique_ptr<interfaces::Handler> handleNotifyHeaderTip(NotifyHeaderTipFn) override { return {}; }

private:
    MockWalletLoader m_wallet_loader;
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------
class RpcConsoleModelTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void historyNavigationOldestToNewest();
    void historyNavigationRestoresPendingText();
    void historyDeduplicatesConsecutiveDuplicates();
    void historyTruncatesAtMax();
    void resetHistoryNavigationClearsState();
};

void RpcConsoleModelTests::historyNavigationOldestToNewest()
{
    MockNode mock;
    RpcConsoleModel model{mock};

    // submitCommand dispatches to the worker via QueuedConnection — with no
    // event loop that slot never fires, so only the synchronous history append
    // and CMD_REQUEST emit happen.
    model.submitCommand("cmd_a");
    model.submitCommand("cmd_b");
    model.submitCommand("cmd_c");

    // Navigate backward (direction=1 → older)
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_c"));
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_b"));
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_a"));

    // Clamped at oldest — should stay at cmd_a
    QCOMPARE(model.browseHistory(1, ""), QString("cmd_a"));

    // Navigate forward (direction=-1 → newer)
    QCOMPARE(model.browseHistory(-1, ""), QString("cmd_b"));
    QCOMPARE(model.browseHistory(-1, ""), QString("cmd_c"));

    // Past the newest end → restore pending text (empty string we passed in)
    QCOMPARE(model.browseHistory(-1, ""), QString(""));
}

void RpcConsoleModelTests::historyNavigationRestoresPendingText()
{
    MockNode mock;
    RpcConsoleModel model{mock};

    model.submitCommand("getblockcount");
    model.submitCommand("help");

    const QString pending = "getblock";
    QCOMPARE(model.browseHistory(1, pending), QString("help"));
    QCOMPARE(model.browseHistory(1, pending), QString("getblockcount"));
    // Navigate back past newest → pending text is restored
    QCOMPARE(model.browseHistory(-1, pending), QString("help"));
    QCOMPARE(model.browseHistory(-1, pending), QString("getblock"));
}

void RpcConsoleModelTests::historyDeduplicatesConsecutiveDuplicates()
{
    MockNode mock;
    RpcConsoleModel model{mock};

    model.submitCommand("getblockcount");
    model.submitCommand("getblockcount"); // duplicate — should not be added
    model.submitCommand("getblockcount"); // duplicate — should not be added
    model.submitCommand("help");

    // History should be: [getblockcount, help]
    QCOMPARE(model.browseHistory(1, ""), QString("help"));
    QCOMPARE(model.browseHistory(1, ""), QString("getblockcount"));
    // No more entries
    QCOMPARE(model.browseHistory(1, ""), QString("getblockcount"));
}

void RpcConsoleModelTests::historyTruncatesAtMax()
{
    MockNode mock;
    RpcConsoleModel model{mock};

    // Submit 51 unique commands — only the last 50 should be kept.
    for (int i = 0; i < 51; ++i) {
        model.submitCommand(QString("cmd_%1").arg(i));
    }

    // Navigate to oldest: should be cmd_1 (cmd_0 was evicted), newest cmd_50.
    // Walk all the way back to the oldest.
    QString last;
    for (int i = 0; i < 50; ++i) {
        last = model.browseHistory(1, "");
    }
    QCOMPARE(last, QString("cmd_1")); // cmd_0 was evicted
}

void RpcConsoleModelTests::resetHistoryNavigationClearsState()
{
    MockNode mock;
    RpcConsoleModel model{mock};

    model.submitCommand("getblockcount");
    model.submitCommand("help");

    // Start navigating
    QCOMPARE(model.browseHistory(1, "typing..."), QString("help"));

    // Reset — next browseHistory should restart from the top
    model.resetHistoryNavigation();
    QCOMPARE(model.browseHistory(1, ""), QString("help"));
}

QTEST_MAIN(RpcConsoleModelTests)
#include "test_rpcconsolemodel.moc"
