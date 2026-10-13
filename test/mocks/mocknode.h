// Copyright (c) 2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_TEST_MOCKS_MOCKNODE_H
#define BITCOIN_QML_TEST_MOCKS_MOCKNODE_H

#include <test/mocks/callcounter.h>
#include <test/mocks/stubnode.h>

#include <QtTest/qtestcase.h>

#include <functional>
#include <mutex>
#include <source_location>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

class MockNode : public StubNode
{
public:
    class Verification
    {
    public:
        Verification(MockNode& node, std::source_location where)
            : m_node{&node}, m_where{where}
        {
        }

        Verification(const Verification&) = delete;
        Verification& operator=(const Verification&) = delete;
        Verification(Verification&& other) noexcept
            : m_node{std::exchange(other.m_node, nullptr)}, m_where{other.m_where}
        {
        }

        ~Verification()
        {
            if (!m_node) return;
            for (const auto& failure : m_node->VerificationFailures(m_where)) {
                QTest::qFail(failure.message.c_str(), failure.where.file_name(), static_cast<int>(failure.where.line()));
            }
        }

    private:
        MockNode* m_node;
        std::source_location m_where;
    };

    explicit MockNode(bool strict = false) : m_strict{strict} {
        get_persistent_setting_fn = [this](const std::string& setting_name) {
            std::lock_guard lock{m_settings_mutex};
            if (!m_settings_initialized) UnexpectedCall("getPersistentSetting");
            const auto it = m_persistent_settings.find(setting_name);
            return it == m_persistent_settings.end() ? common::SettingsValue{} : it->second;
        };
    }

    [[nodiscard]] Verification VerifyOnExit(std::source_location where = std::source_location::current())
    {
        return Verification{*this, where};
    }

    void ExpectExactly(const CallCounter& counter, int expected, std::source_location where = std::source_location::current())
    {
        m_call_expectations.push_back(CallExpectation{&counter, counter.load(), expected, expected, where});
    }

    void ExpectNoCalls(const CallCounter& counter, std::source_location where = std::source_location::current())
    {
        ExpectExactly(counter, 0, where);
    }

    void ExpectAtLeast(const CallCounter& counter, int minimum, std::source_location where = std::source_location::current())
    {
        m_call_expectations.push_back(CallExpectation{&counter, counter.load(), minimum, std::nullopt, where});
    }

    void SetPersistentSetting(std::string name, common::SettingsValue value)
    {
        std::lock_guard lock{m_settings_mutex};
        m_settings_initialized = true;
        m_persistent_settings.insert_or_assign(std::move(name), std::move(value));
    }

    std::function<bilingual_str()> get_warnings_fn;
    std::function<bool(interfaces::BlockAndHeaderTipInfo*)> app_init_main_fn;
    std::function<void()> app_shutdown_fn;
    std::function<void()> start_shutdown_fn;
    std::function<bool()> shutdown_requested_fn;
    std::function<common::SettingsValue(const std::string&)> get_persistent_setting_fn;
    std::function<void(const std::string&, const common::SettingsValue&)> update_rw_setting_fn;
    std::function<void(const std::string&, const common::SettingsValue&)> force_setting_fn;
    std::function<void(bool)> map_port_fn;
    std::function<size_t(ConnectionDirection)> get_node_count_fn;
    std::function<bool(NodesStats&)> get_nodes_stats_fn;
    std::function<bool(banmap_t&)> get_banned_fn;
    std::function<bool(const CNetAddr&, int64_t)> ban_fn;
    std::function<bool(const CSubNet&)> unban_fn;
    std::function<bool(const CNetAddr&)> disconnect_by_address_fn;
    std::function<bool(NodeId)> disconnect_by_id_fn;
    std::function<std::vector<std::unique_ptr<interfaces::ExternalSigner>>()> list_external_signers_fn;
    std::function<int64_t()> get_total_bytes_recv_fn;
    std::function<int64_t()> get_total_bytes_sent_fn;
    std::function<size_t()> get_mempool_size_fn;
    std::function<size_t()> get_mempool_dynamic_usage_fn;
    std::function<size_t()> get_mempool_max_usage_fn;
    std::function<bool(int&, int64_t&)> get_header_tip_fn;
    std::function<int()> get_num_blocks_fn;
    std::function<std::map<CNetAddr, LocalServiceInfo>()> get_net_local_addresses_fn;
    std::function<int64_t()> get_last_block_time_fn;
    std::function<bool()> is_initial_block_download_fn;
    std::function<bool()> get_network_active_fn;
    std::function<CFeeRate()> get_dust_relay_fee_fn;
    std::function<node::TransactionError(CTransactionRef, CAmount, std::string&)> broadcast_transaction_fn;
    std::function<interfaces::WalletLoader&()> wallet_loader_fn;
    std::function<std::unique_ptr<interfaces::Handler>(MessageBoxFn)> handle_message_box_fn;
    std::function<std::unique_ptr<interfaces::Handler>(QuestionFn)> handle_question_fn;
    std::function<std::unique_ptr<interfaces::Handler>(NotifyNumConnectionsChangedFn)> handle_notify_num_connections_changed_fn;
    std::function<std::unique_ptr<interfaces::Handler>(NotifyNetworkActiveChangedFn)> handle_notify_network_active_changed_fn;
    std::function<std::unique_ptr<interfaces::Handler>(NotifyAlertChangedFn)> handle_notify_alert_changed_fn;
    std::function<std::unique_ptr<interfaces::Handler>(BannedListChangedFn)> handle_banned_list_changed_fn;
    std::function<std::unique_ptr<interfaces::Handler>(NotifyBlockTipFn)> handle_notify_block_tip_fn;
    std::function<std::unique_ptr<interfaces::Handler>(NotifyHeaderTipFn)> handle_notify_header_tip_fn;

    struct Calls {
        CallCounter getWarnings{"getWarnings"};
        CallCounter appInitMain{"appInitMain"};
        CallCounter appShutdown{"appShutdown"};
        CallCounter startShutdown{"startShutdown"};
        CallCounter shutdownRequested{"shutdownRequested"};
        CallCounter getPersistentSetting{"getPersistentSetting"};
        CallCounter updateRwSetting{"updateRwSetting"};
        CallCounter forceSetting{"forceSetting"};
        CallCounter mapPort{"mapPort"};
        CallCounter getNodeCount{"getNodeCount"};
        CallCounter getNodesStats{"getNodesStats"};
        CallCounter getBanned{"getBanned"};
        CallCounter ban{"ban"};
        CallCounter unban{"unban"};
        CallCounter disconnectByAddress{"disconnectByAddress"};
        CallCounter disconnectById{"disconnectById"};
        CallCounter listExternalSigners{"listExternalSigners"};
        CallCounter getTotalBytesRecv{"getTotalBytesRecv"};
        CallCounter getTotalBytesSent{"getTotalBytesSent"};
        CallCounter getMempoolSize{"getMempoolSize"};
        CallCounter getMempoolDynamicUsage{"getMempoolDynamicUsage"};
        CallCounter getMempoolMaxUsage{"getMempoolMaxUsage"};
        CallCounter getHeaderTip{"getHeaderTip"};
        CallCounter getNumBlocks{"getNumBlocks"};
        CallCounter getNetLocalAddresses{"getNetLocalAddresses"};
        CallCounter getLastBlockTime{"getLastBlockTime"};
        CallCounter isInitialBlockDownload{"isInitialBlockDownload"};
        CallCounter getNetworkActive{"getNetworkActive"};
        CallCounter getDustRelayFee{"getDustRelayFee"};
        CallCounter broadcastTransaction{"broadcastTransaction"};
        CallCounter walletLoader{"walletLoader"};
        CallCounter handleMessageBox{"handleMessageBox"};
        CallCounter handleQuestion{"handleQuestion"};
        CallCounter handleNotifyNumConnectionsChanged{"handleNotifyNumConnectionsChanged"};
        CallCounter handleNotifyNetworkActiveChanged{"handleNotifyNetworkActiveChanged"};
        CallCounter handleNotifyAlertChanged{"handleNotifyAlertChanged"};
        CallCounter handleBannedListChanged{"handleBannedListChanged"};
        CallCounter handleNotifyBlockTip{"handleNotifyBlockTip"};
        CallCounter handleNotifyHeaderTip{"handleNotifyHeaderTip"};
    } calls;

    std::vector<std::pair<std::string, common::SettingsValue>> update_rw_setting_arguments;
    std::vector<std::pair<std::string, common::SettingsValue>> force_setting_arguments;
    std::vector<bool> map_port_arguments;

    bilingual_str getWarnings() override { return Invoke(calls.getWarnings, get_warnings_fn, bilingual_str{}); }
    bool appInitMain(interfaces::BlockAndHeaderTipInfo* tip_info) override { return Invoke(calls.appInitMain, app_init_main_fn, false, tip_info); }
    void appShutdown() override { InvokeVoid(calls.appShutdown, app_shutdown_fn); }
    void startShutdown() override { InvokeVoid(calls.startShutdown, start_shutdown_fn); }
    bool shutdownRequested() override { return Invoke(calls.shutdownRequested, shutdown_requested_fn, false); }
    common::SettingsValue getPersistentSetting(const std::string& name) override { return Invoke(calls.getPersistentSetting, get_persistent_setting_fn, common::SettingsValue{}, name); }
    void updateRwSetting(const std::string& name, const common::SettingsValue& value) override
    {
        update_rw_setting_arguments.emplace_back(name, value);
        InvokeVoid(calls.updateRwSetting, update_rw_setting_fn, name, value);
    }
    void forceSetting(const std::string& name, const common::SettingsValue& value) override
    {
        force_setting_arguments.emplace_back(name, value);
        InvokeVoid(calls.forceSetting, force_setting_fn, name, value);
    }
    void mapPort(bool use_natpmp) override
    {
        map_port_arguments.push_back(use_natpmp);
        InvokeVoid(calls.mapPort, map_port_fn, use_natpmp);
    }
    size_t getNodeCount(ConnectionDirection direction) override { return Invoke(calls.getNodeCount, get_node_count_fn, size_t{0}, direction); }
    bool getNodesStats(NodesStats& stats) override { return Invoke(calls.getNodesStats, get_nodes_stats_fn, false, stats); }
    bool getBanned(banmap_t& banned) override { return Invoke(calls.getBanned, get_banned_fn, false, banned); }
    bool ban(const CNetAddr& net_addr, int64_t ban_time_offset) override { return Invoke(calls.ban, ban_fn, false, net_addr, ban_time_offset); }
    bool unban(const CSubNet& subnet) override { return Invoke(calls.unban, unban_fn, false, subnet); }
    bool disconnectByAddress(const CNetAddr& net_addr) override { return Invoke(calls.disconnectByAddress, disconnect_by_address_fn, false, net_addr); }
    bool disconnectById(NodeId id) override { return Invoke(calls.disconnectById, disconnect_by_id_fn, false, id); }
    std::vector<std::unique_ptr<interfaces::ExternalSigner>> listExternalSigners() override { return Invoke(calls.listExternalSigners, list_external_signers_fn, std::vector<std::unique_ptr<interfaces::ExternalSigner>>{}); }
    int64_t getTotalBytesRecv() override { return Invoke(calls.getTotalBytesRecv, get_total_bytes_recv_fn, int64_t{0}); }
    int64_t getTotalBytesSent() override { return Invoke(calls.getTotalBytesSent, get_total_bytes_sent_fn, int64_t{0}); }
    size_t getMempoolSize() override { return Invoke(calls.getMempoolSize, get_mempool_size_fn, size_t{0}); }
    size_t getMempoolDynamicUsage() override { return Invoke(calls.getMempoolDynamicUsage, get_mempool_dynamic_usage_fn, size_t{0}); }
    size_t getMempoolMaxUsage() override { return Invoke(calls.getMempoolMaxUsage, get_mempool_max_usage_fn, size_t{0}); }
    bool getHeaderTip(int& height, int64_t& block_time) override { return Invoke(calls.getHeaderTip, get_header_tip_fn, false, height, block_time); }
    int getNumBlocks() override { return Invoke(calls.getNumBlocks, get_num_blocks_fn, 0); }
    std::map<CNetAddr, LocalServiceInfo> getNetLocalAddresses() override { return Invoke(calls.getNetLocalAddresses, get_net_local_addresses_fn, std::map<CNetAddr, LocalServiceInfo>{}); }
    int64_t getLastBlockTime() override { return Invoke(calls.getLastBlockTime, get_last_block_time_fn, int64_t{0}); }
    bool isInitialBlockDownload() override { return Invoke(calls.isInitialBlockDownload, is_initial_block_download_fn, false); }
    bool getNetworkActive() override { return Invoke(calls.getNetworkActive, get_network_active_fn, false); }
    CFeeRate getDustRelayFee() override { return Invoke(calls.getDustRelayFee, get_dust_relay_fee_fn, CFeeRate{}); }
    node::TransactionError broadcastTransaction(CTransactionRef tx, CAmount max_tx_fee, std::string& err_string) override { return Invoke(calls.broadcastTransaction, broadcast_transaction_fn, node::TransactionError{}, std::move(tx), max_tx_fee, err_string); }
    interfaces::WalletLoader& walletLoader() override
    {
        ++calls.walletLoader;
        if (wallet_loader_fn) return wallet_loader_fn();
        UnexpectedCall(calls.walletLoader.Name());
        return FallbackWalletLoader();
    }
    std::unique_ptr<interfaces::Handler> handleMessageBox(MessageBoxFn fn) override { return Invoke(calls.handleMessageBox, handle_message_box_fn, std::unique_ptr<interfaces::Handler>{}, std::move(fn)); }
    std::unique_ptr<interfaces::Handler> handleQuestion(QuestionFn fn) override { return Invoke(calls.handleQuestion, handle_question_fn, std::unique_ptr<interfaces::Handler>{}, std::move(fn)); }
    std::unique_ptr<interfaces::Handler> handleNotifyNumConnectionsChanged(NotifyNumConnectionsChangedFn fn) override { return Invoke(calls.handleNotifyNumConnectionsChanged, handle_notify_num_connections_changed_fn, std::unique_ptr<interfaces::Handler>{}, std::move(fn)); }
    std::unique_ptr<interfaces::Handler> handleNotifyNetworkActiveChanged(NotifyNetworkActiveChangedFn fn) override { return Invoke(calls.handleNotifyNetworkActiveChanged, handle_notify_network_active_changed_fn, std::unique_ptr<interfaces::Handler>{}, std::move(fn)); }
    std::unique_ptr<interfaces::Handler> handleNotifyAlertChanged(NotifyAlertChangedFn fn) override { return Invoke(calls.handleNotifyAlertChanged, handle_notify_alert_changed_fn, std::unique_ptr<interfaces::Handler>{}, std::move(fn)); }
    std::unique_ptr<interfaces::Handler> handleBannedListChanged(BannedListChangedFn fn) override { return Invoke(calls.handleBannedListChanged, handle_banned_list_changed_fn, std::unique_ptr<interfaces::Handler>{}, std::move(fn)); }
    std::unique_ptr<interfaces::Handler> handleNotifyBlockTip(NotifyBlockTipFn fn) override { return Invoke(calls.handleNotifyBlockTip, handle_notify_block_tip_fn, std::unique_ptr<interfaces::Handler>{}, std::move(fn)); }
    std::unique_ptr<interfaces::Handler> handleNotifyHeaderTip(NotifyHeaderTipFn fn) override { return Invoke(calls.handleNotifyHeaderTip, handle_notify_header_tip_fn, std::unique_ptr<interfaces::Handler>{}, std::move(fn)); }

protected:
    void UnhandledCall(const char* name) override { UnexpectedCall(name); }

private:
    struct CallExpectation {
        const CallCounter* counter;
        int baseline;
        int minimum;
        std::optional<int> maximum;
        std::source_location where;
    };

    struct VerificationFailure {
        std::string message;
        std::source_location where;
    };

    std::vector<VerificationFailure> VerificationFailures(std::source_location unexpected_where) const
    {
        std::vector<VerificationFailure> failures;
        {
            std::lock_guard<std::mutex> lock{m_unexpected_mutex};
            if (!m_unexpected_calls.empty()) {
                std::ostringstream message;
                message << "Unexpected Node call(s):";
                for (const std::string& call : m_unexpected_calls)
                    message << "\n  " << call;
                failures.push_back({message.str(), unexpected_where});
            }
        }
        for (const CallExpectation& expectation : m_call_expectations) {
            const int actual{expectation.counter->load() - expectation.baseline};
            if (actual < expectation.minimum || (expectation.maximum && actual > *expectation.maximum)) {
                std::ostringstream message;
                message << "Expected " << expectation.counter->Name() << " calls to be ";
                if (expectation.maximum && expectation.minimum == *expectation.maximum) {
                    message << expectation.minimum;
                } else if (expectation.maximum) {
                    message << "between " << expectation.minimum << " and " << *expectation.maximum;
                } else {
                    message << "at least " << expectation.minimum;
                }
                message << ", actual " << actual;
                failures.push_back({message.str(), expectation.where});
            }
        }
        return failures;
    }

    template <typename R, typename... FnArgs, typename... CallArgs>
    R Invoke(CallCounter& counter, const std::function<R(FnArgs...)>& fn, R fallback, CallArgs&&... args)
    {
        ++counter;
        if (fn) return fn(std::forward<CallArgs>(args)...);
        UnexpectedCall(counter.Name());
        return std::move(fallback);
    }

    template <typename... FnArgs, typename... CallArgs>
    void InvokeVoid(CallCounter& counter, const std::function<void(FnArgs...)>& fn, CallArgs&&... args)
    {
        ++counter;
        if (fn) {
            fn(std::forward<CallArgs>(args)...);
        } else {
            UnexpectedCall(counter.Name());
        }
    }

    void UnexpectedCall(std::string_view name)
    {
        if (!m_strict) return;
        std::lock_guard<std::mutex> lock{m_unexpected_mutex};
        m_unexpected_calls.emplace_back(name);
    }

    const bool m_strict;
    mutable std::mutex m_unexpected_mutex;
    std::vector<std::string> m_unexpected_calls;
    std::vector<CallExpectation> m_call_expectations;
    std::mutex m_settings_mutex;
    bool m_settings_initialized{false};
    std::map<std::string, common::SettingsValue> m_persistent_settings;
};

class StrictMockNode : public MockNode
{
public:
    StrictMockNode() : MockNode{true} {}
};

#endif // BITCOIN_QML_TEST_MOCKS_MOCKNODE_H
