// Copyright (c) 2011-2026 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RPCCOMMANDEXECUTOR_H
#define BITCOIN_QML_MODELS_RPCCOMMANDEXECUTOR_H

#include <string>
#include <utility>
#include <memory>
#include <optional>

#include <QString>

namespace interfaces {
class Node;
}

/** Explicit target for an RPC request. An empty wallet name identifies the
 *  default wallet when target is Wallet; it does not mean a node request. */
struct RpcCommandContext {
    enum class Target { Node, Wallet };

    Target target{Target::Node};
    QString wallet_name;

    static RpcCommandContext ForNode() { return {}; }
    static RpcCommandContext ForWallet(QString name) { return {Target::Wallet, std::move(name)}; }

    friend bool operator==(const RpcCommandContext&, const RpcCommandContext&) = default;
};

/**
 * Parses and executes RPC commands entered in the console.
 *
 * The parser is ported from RPCConsole::RPCParseCommandLine() in
 * src/qt/rpcconsole.cpp and handles:
 *  - Standard syntax:      getblockhash 0
 *  - Functional syntax:    getblockhash(0)
 *  - Nested commands:      getblock(getblockhash(0) 1)
 *  - Result subscripts:    getblock(getblockhash(0) 1)[tx][0]
 *  - Single/double quoted strings with escape sequences
 *  - Redaction of sensitive command arguments (walletpassphrase, etc.)
 */
class RpcCommandExecutor
{
public:
    struct Plan;
    /** Transient worker-owned syntax tree. Never store this secret-bearing value
     * in history, output, a GUI publication, or a diagnostic. */
    class Prepared
    {
    public:
        const std::string& filtered() const;
    private:
        friend class RpcCommandExecutor;
        explicit Prepared(std::shared_ptr<const Plan> plan) : m_plan(std::move(plan)) {}
        std::shared_ptr<const Plan> m_plan;
    };
    static std::optional<Prepared> Prepare(const std::string& command);
    /** Evaluate the prepared syntax exactly once, with the captured endpoint.
     * attempted becomes true immediately before the first public RPC call. */
    static std::string Execute(interfaces::Node& node, const Prepared& command,
                               const RpcCommandContext& context, bool& attempted);
    /**
     * Split and optionally execute a shell-like command line.
     *
     * @param[in]    node            Node to execute commands on (nullptr = parse only)
     * @param[out]   strResult       Stringified result of the executed command
     * @param[in]    strCommand      Command line to parse/execute
     * @param[in]    fExecute        true to execute, false to parse only
     * @param[out]   pstrFilteredOut Command line with sensitive data replaced by (…)
     * @param[in]    wallet_name     Wallet context for wallet-scoped commands
     * @return true on success, false on parse error
     */
    static bool RPCParseCommandLine(interfaces::Node* node,
                                    std::string& strResult,
                                    const std::string& strCommand,
                                    bool fExecute,
                                    std::string* pstrFilteredOut = nullptr,
                                    const QString& wallet_name = {});

    /** Parse or execute with an explicit node or wallet target. */
    static bool RPCParseCommandLine(interfaces::Node* node,
                                    std::string& strResult,
                                    const std::string& strCommand,
                                    bool fExecute,
                                    std::string* pstrFilteredOut,
                                    const RpcCommandContext& context);

    /**
     * Execute a command line (convenience wrapper around RPCParseCommandLine).
     */
    static bool RPCExecuteCommandLine(interfaces::Node& node,
                                      std::string& strResult,
                                      const std::string& strCommand,
                                      std::string* pstrFilteredOut = nullptr,
                                      const QString& wallet_name = {})
    {
        return RPCParseCommandLine(&node, strResult, strCommand, true, pstrFilteredOut, wallet_name);
    }

    /** Execute with an explicit node or wallet target. */
    static bool RPCExecuteCommandLine(interfaces::Node& node,
                                      std::string& strResult,
                                      const std::string& strCommand,
                                      std::string* pstrFilteredOut,
                                      const RpcCommandContext& context)
    {
        return RPCParseCommandLine(&node, strResult, strCommand, true, pstrFilteredOut, context);
    }
};

#endif // BITCOIN_QML_MODELS_RPCCOMMANDEXECUTOR_H
