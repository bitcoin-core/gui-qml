// Copyright (c) 2011-2022 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#ifndef BITCOIN_QML_MODELS_RPCCOMMANDEXECUTOR_H
#define BITCOIN_QML_MODELS_RPCCOMMANDEXECUTOR_H

#include <string>

#include <QString>

namespace interfaces {
class Node;
}

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
};

#endif // BITCOIN_QML_MODELS_RPCCOMMANDEXECUTOR_H
