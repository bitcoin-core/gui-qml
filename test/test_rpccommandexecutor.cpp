// Copyright (c) 2024 The Bitcoin Core developers
// Distributed under the MIT software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

// Tests for RpcCommandExecutor::RPCParseCommandLine (parse-only mode).
// These are modelled after src/qt/test/rpcnestedtests.cpp.
// All tests use fExecute=false so no live node is required.

#include <QtTest/QtTest>

#include <qml/models/rpccommandexecutor.h>
#include <util/translation.h>

#include <string>

const TranslateFn G_TRANSLATION_FUN{nullptr};

class RpcCommandExecutorTests : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parseSpaceSeparated();
    void parseFunctionalNotation();
    void parseNestedCommands();
    void parseQuotedArguments();
    void parseSubscriptSyntax();
    void parseUnbalancedQuoteFails();
    void parseUnbalancedParenFails();
    void sensitiveCmdWalletPassphrase();
    void sensitiveCmdNestedWalletPassphrase();
    void sensitiveCmdSignMessage();
    void sensitiveCmdEncryptWallet();
    void sensitiveCmdNonSensitiveUnredacted();
};

void RpcCommandExecutorTests::parseSpaceSeparated()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash 0", false));
}

void RpcCommandExecutorTests::parseFunctionalNotation()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash(0)", false));
}

void RpcCommandExecutorTests::parseNestedCommands()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblock(getblockhash(0) 1)", false));
}

void RpcCommandExecutorTests::parseQuotedArguments()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "signmessage \"addr\" \"msg\"", false));
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash '0'", false));
}

void RpcCommandExecutorTests::parseSubscriptSyntax()
{
    std::string result;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(
        nullptr, result, "getblock(getblockhash(0) 1)[tx]", false));
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(
        nullptr, result, "getblock(getblockhash(0),1)[tx][0]", false));
}

void RpcCommandExecutorTests::parseUnbalancedQuoteFails()
{
    std::string result;
    QVERIFY(!RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash '0", false));
    QVERIFY(!RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash \"0", false));
}

void RpcCommandExecutorTests::parseUnbalancedParenFails()
{
    std::string result;
    // An unbalanced open paren is implicitly closed by the trailing newline the
    // parser appends, so it parses successfully (same behaviour as the original
    // Qt GUI console). Only unbalanced quotes produce a parse error.
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockhash(0", false));
}

void RpcCommandExecutorTests::sensitiveCmdWalletPassphrase()
{
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "walletpassphrase foo bar", false, &filtered));
    QVERIFY(!QString::fromStdString(filtered).contains("foo"));
    QVERIFY(QString::fromStdString(filtered).contains(QString::fromUtf8("(\xe2\x80\xa6)"))); // (…)
}

void RpcCommandExecutorTests::sensitiveCmdNestedWalletPassphrase()
{
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "walletpassphrase(help())", false, &filtered));
    QVERIFY(QString::fromStdString(filtered).contains("(\xe2\x80\xa6)"));
}

void RpcCommandExecutorTests::sensitiveCmdSignMessage()
{
    std::string result, filtered;
    // Use argument strings that are not substrings of the command name itself.
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "signmessagewithprivkey myprivatearg mymsgarg", false, &filtered));
    QVERIFY(!QString::fromStdString(filtered).contains("myprivatearg"));
    QVERIFY(!QString::fromStdString(filtered).contains("mymsgarg"));
    QVERIFY(QString::fromStdString(filtered).contains("(\xe2\x80\xa6)"));
}

void RpcCommandExecutorTests::sensitiveCmdEncryptWallet()
{
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "encryptwallet mypassword", false, &filtered));
    QVERIFY(!QString::fromStdString(filtered).contains("mypassword"));
    QVERIFY(QString::fromStdString(filtered).contains("(\xe2\x80\xa6)"));
}

void RpcCommandExecutorTests::sensitiveCmdNonSensitiveUnredacted()
{
    std::string result, filtered;
    QVERIFY(RpcCommandExecutor::RPCParseCommandLine(nullptr, result, "getblockcount", false, &filtered));
    // Non-sensitive command must not be redacted.
    QCOMPARE(QString::fromStdString(filtered), QString("getblockcount"));
}

QTEST_MAIN(RpcCommandExecutorTests)
#include "test_rpccommandexecutor.moc"
